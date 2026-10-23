[CmdletBinding()]
param([ValidateRange(1,32)][int]$Jobs=8)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $taskRoot
$lock=Get-Content integration/community/sources.lock.json -Raw | ConvertFrom-Json
$source=Join-Path $taskRoot 'upstream/OpenBamboo'
if(!(Test-Path -LiteralPath (Join-Path $source '.git'))) {
    & git clone --depth 1 --filter=blob:none --no-checkout $lock.source $source
    if($LASTEXITCODE -ne 0){throw 'Cannot fetch community source.'}
    & git -C $source sparse-checkout set --no-cone /src/ /include/ /cmake/ /stubs/ /resources/ /packaging/ /tools/update_cert_probe/ /CMakeLists.txt /BUILDING.md /README.md /LICENSE /vcpkg.json
    if($LASTEXITCODE -ne 0){throw 'Cannot select community source.'}
    & git -C $source fetch --depth 1 origin $lock.commit
    if($LASTEXITCODE -ne 0){throw 'Cannot fetch pinned community commit.'}
    & git -C $source checkout --detach $lock.commit
    if($LASTEXITCODE -ne 0){throw 'Cannot check out pinned community commit.'}
}
if((& git -C $source rev-parse HEAD) -ne $lock.commit){throw 'Community source is not at the pinned commit; preserve local work before changing it.'}
$patch=Join-Path $taskRoot 'integration/community/openbamboo.patch'
& git -C $source apply --reverse --check $patch 2>$null
if($LASTEXITCODE -ne 0) {
    & git -C $source apply --check $patch
    if($LASTEXITCODE -ne 0){throw 'Community integration patch conflicts with local source.'}
    & git -C $source apply $patch
    if($LASTEXITCODE -ne 0){throw 'Cannot apply community integration patch.'}
}
foreach($name in @('orqa_session.hpp','orqa_workers.hpp','orqa_https_trust.hpp')){
    $from=Join-Path $taskRoot ('integration/community/'+$name)
    $to=Join-Path $source ('include/obn/'+$name)
    if(!(Test-Path $to) -or (Get-FileHash $from).Hash -ne (Get-FileHash $to).Hash){Copy-Item $from $to -Force}
}
$vcpkg=Join-Path $taskRoot 'tools/vcpkg'
if(!(Test-Path (Join-Path $vcpkg '.git'))){
    & git clone --depth 1 https://github.com/microsoft/vcpkg.git $vcpkg
    if($LASTEXITCODE -ne 0){throw 'Cannot fetch dependency tool source.'}
    & git -C $vcpkg fetch --depth 1 origin $lock.vcpkgCommit
    if($LASTEXITCODE -ne 0){throw 'Cannot fetch pinned dependency tool source.'}
    & git -C $vcpkg checkout --detach $lock.vcpkgCommit
    if($LASTEXITCODE -ne 0){throw 'Cannot select dependency tool source.'}
}
if((& git -C $vcpkg rev-parse HEAD) -ne $lock.vcpkgCommit){throw 'Dependency tool commit differs from the source lock.'}
if(!(Test-Path (Join-Path $vcpkg 'vcpkg.exe'))){
    & (Join-Path $vcpkg 'bootstrap-vcpkg.bat') -disableMetrics
    if($LASTEXITCODE -ne 0){throw 'Cannot bootstrap dependency tool.'}
}
$dependencyRoot=Join-Path $taskRoot 'tools/openbamboo-deps'
& (Join-Path $vcpkg 'vcpkg.exe') install pthreads:x64-windows-static-md --disable-metrics --classic "--x-install-root=$dependencyRoot"
if($LASTEXITCODE -ne 0){throw 'Cannot build the Windows threading dependency.'}
$cmakeTool=Get-ChildItem tools/cmake/*/bin/cmake.exe | Select-Object -First 1
if(!$cmakeTool){throw 'Run the normal slicer build setup first.'}
$prefix=(Resolve-Path upstream/OrcaSlicer/deps/build/OrcaSlicer_dep/usr/local).Path
$pthreadPrefix=Join-Path $dependencyRoot 'x64-windows-static-md'
$cacheArgs=@()
if(Test-Path (Join-Path $source 'build/_deps/eclipse_mosquitto-src/CMakeLists.txt')) {
    $cacheArgs+="-DFETCHCONTENT_SOURCE_DIR_ECLIPSE_MOSQUITTO=$(Join-Path $source 'build/_deps/eclipse_mosquitto-src')"
}
if(Test-Path (Join-Path $source 'build/_deps/cjson-src/CMakeLists.txt')) {
    $cacheArgs+="-DFETCHCONTENT_SOURCE_DIR_CJSON=$(Join-Path $source 'build/_deps/cjson-src')"
}
& $cmakeTool.FullName -S $source -B (Join-Path $source 'build') -G 'Visual Studio 18 2026' -A x64 `
    "-DCMAKE_PREFIX_PATH=$prefix;$pthreadPrefix" "-D_VCPKG_INSTALLED_DIR=$dependencyRoot" '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md' `
    "-DOBN_VERSION=$($lock.reportedAbiVersion)" '-DOBN_CLIENT_TYPE=orca_slicer' '-DOBN_PATCH_CLIENT_CONF=OFF' `
    '-DOBN_REGISTER_DSHOW_FILTER=OFF' '-DOBN_BUILD_TESTS=OFF' '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' `
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL' '-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /DPTW32_STATIC_LIB' `
    '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /DPTW32_STATIC_LIB /DORQA_COMMUNITY_CLOUD_ONLY /EHsc /utf-8' @cacheArgs
if($LASTEXITCODE -ne 0){throw 'Community library configure failed.'}
if((& git -C (Join-Path $source 'build/_deps/eclipse_mosquitto-src') rev-parse HEAD) -ne $lock.mosquittoCommit -or
   (& git -C (Join-Path $source 'build/_deps/cjson-src') rev-parse HEAD) -ne $lock.cjsonCommit){throw 'A dependency commit differs from the source lock.'}
& $cmakeTool.FullName --build (Join-Path $source 'build') --config Release --target bambu_networking BambuSource --parallel $Jobs
if($LASTEXITCODE -ne 0){throw 'Community library compilation failed.'}
Write-Output 'Community libraries built. This script does not install them, register a camera filter, sign in or contact printers.'
