[CmdletBinding()]
param(
    [string]$TargetFolder='upstream/OrcaSlicer/build/OrcaSlicer/community-bambu',
    [string]$ProofFile='artifacts/dev10-community-proof.json'
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $taskRoot
$source=Join-Path $taskRoot 'upstream/OpenBamboo'
$target=Join-Path $taskRoot $TargetFolder
if(!(Test-Path upstream/OrcaSlicer/build/OrcaSlicer/orca-slicer.exe)){throw 'Complete the native build first.'}
if(!(Test-Path artifacts/community-abi-runtime.log) -or !(Select-String -LiteralPath artifacts/community-abi-runtime.log -Pattern 'agent lifecycle passed' -SimpleMatch -Quiet)){
    throw 'Complete the local community ABI and lifecycle check before staging.'
}
& git -C $source apply --reverse --check (Join-Path $taskRoot 'integration/community/openbamboo.patch')
if($LASTEXITCODE -ne 0){throw 'Community source differs from the maintained integration patch.'}
$proof=[ordered]@{checkedAtUtc=[DateTime]::UtcNow.ToString('o');binaries=[ordered]@{};sourceHashes=[ordered]@{}}
New-Item -ItemType Directory -Force $target | Out-Null
foreach($name in @('bambu_networking.dll','BambuSource.dll')){
    $from=Join-Path $source ('build/Release/'+$name)
    Copy-Item $from (Join-Path $target $name) -Force
    $proof.binaries[$name]=(Get-FileHash $from).Hash
    if((Get-FileHash (Join-Path $target $name)).Hash -ne $proof.binaries[$name]){throw 'Community DLL copy mismatch.'}
}
$snapshot=Join-Path $target 'source'
New-Item -ItemType Directory -Force $snapshot | Out-Null
foreach($name in @('src','include','cmake','stubs','resources','packaging','CMakeLists.txt','LICENSE','README.md','BUILDING.md','vcpkg.json')){
    Copy-Item -LiteralPath (Join-Path $source $name) -Destination $snapshot -Recurse -Force
}
New-Item -ItemType Directory -Force (Join-Path $snapshot 'tools') | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'tools/update_cert_probe') -Destination (Join-Path $snapshot 'tools') -Recurse -Force
foreach($file in Get-ChildItem $snapshot -Recurse -File){
    $relative=$file.FullName.Substring($snapshot.Length+1).Replace('\','/')
    $proof.sourceHashes[$relative]=(Get-FileHash -LiteralPath $file.FullName).Hash
}
Copy-Item integration/community/sources.lock.json (Join-Path $target 'sources.lock.json') -Force
Copy-Item integration/community/openbamboo.patch (Join-Path $target 'integration.patch') -Force
$licenses=Join-Path $target 'licenses'
New-Item -ItemType Directory -Force $licenses | Out-Null
Copy-Item (Join-Path $source 'LICENSE') (Join-Path $licenses 'OpenBamboo-AGPL-3.0.txt') -Force
Copy-Item (Join-Path $source 'build/_deps/cjson-src/LICENSE') (Join-Path $licenses 'cJSON-MIT.txt') -Force
Copy-Item (Join-Path $source 'build/_deps/eclipse_mosquitto-src/edl-v10') (Join-Path $licenses 'Mosquitto-BSD.txt') -Force
Copy-Item tools/openbamboo-deps/x64-windows-static-md/share/pthreads/copyright (Join-Path $licenses 'PThreads4W-copyright.txt') -Force
@'
This optional backend is built from persano/open-bamboo-networking, pinned by
sources.lock.json, with the local integration.patch and helper headers in source.
It is independent of Bambu Lab and is not vendor-endorsed. Source code is provided
under AGPL-3.0-or-later; dependency notices are in licenses. Existing native slicer
dependency notices also apply to the cached curl/OpenSSL/zlib libraries used here.

This build forces cloud routing for printer MQTT/printing and the cloud camera
route, isolates its session in orqa-community, protects session files with Windows
DPAPI and drains background work before agent destruction. It does not install a
system-wide camera filter or change the standard downloaded Bambu plug-in.

The upstream interoperability code uses a Studio-compatible protocol identity,
attestation metadata and app-certificate bootstrap. It does not confer official
vendor authorization; server/firmware changes can break operation. Real account,
printer control and camera functionality require user testing.
'@ | Set-Content (Join-Path $target 'NOTICE.txt') -Encoding utf8
$proof | ConvertTo-Json -Depth 7 | Set-Content (Join-Path $taskRoot $ProofFile) -Encoding utf8
Write-Output 'Optional DLLs, corresponding community source and notices staged. No sessions or vendor plug-ins copied.'

