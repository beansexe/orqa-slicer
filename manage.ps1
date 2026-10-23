[CmdletBinding()]
param(
    [Parameter(Position=0)]
    [ValidateSet('Status','Setup','Fetch','Doctor','ApplyIntegration','BuildDeps','Build','Publish','List','Use','Run')]
    [string]$Action = 'Status',
    [string]$Version,
    [string]$SettingsVersion,
    [string]$BuildFolder,
    [string]$RevisionNotes,
    [ValidateRange(1,64)]
    [int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$sourceRoot = Join-Path $taskRoot 'upstream/OrcaSlicer'
$versionsRoot = Join-Path $taskRoot 'versions'
$lock = Get-Content (Join-Path $taskRoot 'sources.lock.json') -Raw | ConvertFrom-Json

function Invoke-Git([string[]]$GitArgs) {
    & git @GitArgs
    if ($LASTEXITCODE -ne 0) { throw "Git failed (exit $LASTEXITCODE)." }
}
function Assert-VersionLabel([object]$Label, [string]$ParameterName = 'Version') {
    if ($Label -isnot [string] -or !$Label -or $Label -notmatch '^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$' -or
        $Label -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)' -or $Label.EndsWith('.')) {
        throw "Use a $ParameterName label such as 0.1.0-dev.1 (letters, numbers, dots, dashes, underscores)."
    }
}
function Assert-Version { Assert-VersionLabel $Version }
function Get-ExistingRevisionSettings([object]$Label) {
    Assert-VersionLabel $Label 'SettingsVersion'
    $revision = Join-Path $versionsRoot $Label
    if (!(Test-Path -LiteralPath (Join-Path $revision 'build-info.json') -PathType Leaf)) {
        throw 'SettingsVersion must name an existing published revision.'
    }
    $settings = Join-Path $revision 'data_dir'
    if (!(Test-Path -LiteralPath $settings -PathType Container)) {
        throw 'SettingsVersion must have an existing data_dir directory.'
    }
    return $settings
}
function Get-SelectedVersion {
    $selected = Join-Path $versionsRoot 'selected.json'
    if (!(Test-Path -LiteralPath $selected)) { throw 'No version selected. Publish a build, then use: .\manage.ps1 Use -Version <label>' }
    return (Get-Content -LiteralPath $selected -Raw | ConvertFrom-Json).version
}
function Assert-Source {
    if (!(Test-Path -LiteralPath (Join-Path $sourceRoot 'build_win.bat'))) { throw 'Run Setup first.' }
}
function Assert-PortableBuild([string]$Folder) {
    foreach ($required in @('orca-slicer.exe','OrcaSlicer.dll','resources/profiles',
                            'python/python312.dll','python/Lib/encodings/__init__.py',
                            'python/vcruntime140.dll','python/vcruntime140_1.dll',
                            'WebView2Loader.dll','resources/tools/uv/uv.exe')) {
        if (!(Test-Path -LiteralPath (Join-Path $Folder $required))) {
            throw "Portable build is incomplete: missing $required. Finish Build before publishing."
        }
    }
    # CMake writes this only after installation completes. Verify every installed
    # file, including dependency DLLs and resources, before accepting a revision.
    $manifest = Join-Path $sourceRoot 'build/install_manifest.txt'
    if (!(Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw 'Portable build has no completed install manifest. Finish Build before publishing.'
    }
    $installedRoot = [IO.Path]::GetFullPath((Join-Path $sourceRoot 'build/OrcaSlicer')).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    $entries = @(Get-Content -LiteralPath $manifest | Where-Object { $_.Trim() })
    if ($entries.Count -eq 0) { throw 'Portable install manifest is empty.' }
    foreach ($entry in $entries) {
        $installedFile = [IO.Path]::GetFullPath($entry)
        if (!$installedFile.StartsWith($installedRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Install manifest contains a file outside the expected portable installation.'
        }
        $relative = $installedFile.Substring($installedRoot.Length)
        if (!(Test-Path -LiteralPath (Join-Path $Folder $relative) -PathType Leaf)) {
            throw "Portable build is incomplete: missing $relative from the install manifest."
        }
    }
}
function Invoke-Build([string]$Stage) {
    Assert-Source
    $previousPath = $env:PATH
    $previousPythonInterpreter = $env:PYTHON
    $previousGitConfigCount = $env:GIT_CONFIG_COUNT
    $gitConfigIndex = if ($previousGitConfigCount) { [int]$previousGitConfigCount } else { 0 }
    $gitConfigKey = "GIT_CONFIG_KEY_$gitConfigIndex"
    $gitConfigValue = "GIT_CONFIG_VALUE_$gitConfigIndex"
    $previousGitConfigKey = [Environment]::GetEnvironmentVariable($gitConfigKey, 'Process')
    $previousGitConfigValue = [Environment]::GetEnvironmentVariable($gitConfigValue, 'Process')
    # This local preview is unsigned. The upstream script prints its configure
    # command, so never forward an environment-provided updater signing key.
    $previousUpdaterSigningKey = $env:ORCA_UPDATER_SIG_KEY
    $env:ORCA_UPDATER_SIG_KEY = $null
    $cmakeTool = Get-ChildItem -Path (Join-Path $taskRoot 'tools/cmake/*/bin/cmake.exe') -ErrorAction SilentlyContinue | Select-Object -First 1
    $perlBin = Join-Path $taskRoot 'tools/perl/perl/bin'
    $pythonBootstrap = Join-Path $taskRoot 'tools/python-bootstrap/tools/python.exe'
    if ($cmakeTool) { $env:PATH = "$($cmakeTool.Directory.FullName);$env:PATH" }
    if (Test-Path -LiteralPath $perlBin) { $env:PATH = "${perlBin};$env:PATH" }
    # CPython 3.12's regeneration scripts need a compatible host interpreter.
    # Explicit selection avoids an unpinned latest interpreter from NuGet.
    if (Test-Path -LiteralPath $pythonBootstrap) { $env:PYTHON = '"' + $pythonBootstrap + '"' }
    # Dependency submodules contain paths beyond Windows' legacy 260-character limit.
    # Append a process-only Git setting without replacing existing caller settings.
    [Environment]::SetEnvironmentVariable($gitConfigKey, 'core.longpaths', 'Process')
    [Environment]::SetEnvironmentVariable($gitConfigValue, 'true', 'Process')
    $env:GIT_CONFIG_COUNT = [string]($gitConfigIndex + 1)
    # The upstream script retains dependency and application build directories.
    Push-Location $sourceRoot
    try {
        if ($Stage -eq '-s') { & .\build_win.bat $Stage -i -j $Jobs }
        else { & .\build_win.bat $Stage -j $Jobs }
        if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)." }
    } finally {
        Pop-Location
        $env:PATH = $previousPath
        $env:PYTHON = $previousPythonInterpreter
        $env:GIT_CONFIG_COUNT = $previousGitConfigCount
        [Environment]::SetEnvironmentVariable($gitConfigKey, $previousGitConfigKey, 'Process')
        [Environment]::SetEnvironmentVariable($gitConfigValue, $previousGitConfigValue, 'Process')
        $env:ORCA_UPDATER_SIG_KEY = $previousUpdaterSigningKey
    }
}

switch ($Action) {
    'Status' {
        foreach ($source in $lock.sources) {
            $path = Join-Path $taskRoot ('upstream/' + $source.name)
            Write-Host "`n$($source.name): pinned $($source.commit)"
            if (Test-Path -LiteralPath (Join-Path $path '.git')) {
                Invoke-Git @('-C', $path, 'rev-parse', 'HEAD')
                Invoke-Git @('-C', $path, 'status', '--short')
            } else { Write-Host 'Not downloaded.' }
        }
        if (Test-Path -LiteralPath (Join-Path $versionsRoot 'selected.json')) {
            Write-Host "Selected build: $(Get-SelectedVersion)"
        }
    }
    'Setup' {
        New-Item -ItemType Directory -Force (Join-Path $taskRoot 'upstream') | Out-Null
        foreach ($source in $lock.sources) {
            $path = Join-Path $taskRoot ('upstream/' + $source.name)
            if (Test-Path -LiteralPath $path) {
                Write-Host "Reusing $path; existing changes are preserved."
                continue
            }
            Invoke-Git @('clone', '--depth', '1', '--no-checkout', $source.url, $path)
            Invoke-Git @('-C', $path, 'fetch', '--depth', '1', 'origin', $source.commit)
            Invoke-Git @('-C', $path, 'checkout', '-b', 'codex/dual-accounts', $source.commit)
        }
    }
    'Fetch' {
        foreach ($source in $lock.sources) {
            $path = Join-Path $taskRoot ('upstream/' + $source.name)
            if (!(Test-Path -LiteralPath (Join-Path $path '.git'))) { throw 'Run Setup first.' }
            # Fetch transfers new Git objects only; never reset or overwrite our edits.
            Invoke-Git @('-C', $path, 'fetch', '--depth', '1', 'origin')
        }
        Write-Host 'Upstream fetched. Working files and pinned revisions have not changed.'
    }
    'Doctor' {
        foreach ($tool in @('git', 'cmake', 'perl')) {
            $found = Get-Command $tool -ErrorAction SilentlyContinue
            if ($found) { Write-Host "${tool}: $($found.Source)" }
            elseif ($tool -eq 'cmake' -and (Get-ChildItem -Path (Join-Path $taskRoot 'tools/cmake/*/bin/cmake.exe') -ErrorAction SilentlyContinue)) {
                Write-Host 'cmake: portable workspace tool installed'
            }
            elseif ($tool -eq 'perl' -and (Test-Path -LiteralPath (Join-Path $taskRoot 'tools/perl/perl/bin/perl.exe'))) {
                Write-Host 'perl: portable workspace tool installed'
            }
            else { Write-Host "${tool}: MISSING" }
        }
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($vs) { Write-Host "MSVC: $vs" } else { Write-Host 'MSVC C++ workload: MISSING' }
        } else { Write-Host 'MSVC C++ workload: MISSING' }
        $sdkHeaders = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Include/*/ucrt/crtdbg.h'
        if (Get-ChildItem -Path $sdkHeaders -ErrorAction SilentlyContinue) { Write-Host 'Windows SDK C++ headers: installed' }
        else { Write-Host 'Windows SDK C++ headers: MISSING' }
        Write-Host 'First build needs at least 23 GB free disk and 16 GB RAM per upstream guidance.'
    }
    'BuildDeps' { Invoke-Build '-d' }
    'ApplyIntegration' { & (Join-Path $taskRoot 'scripts/apply-integration.ps1') }
    'Build' {
        & (Join-Path $taskRoot 'scripts/apply-integration.ps1')
        $previousArgs = $env:ORCA_SLICER_CMAKE_ARGS
        try {
            $env:ORCA_SLICER_CMAKE_ARGS = "$previousArgs -DORCA_DUAL_VENDOR_ACCOUNTS=ON -DCMAKE_INSTALL_MESSAGE=LAZY"
            Invoke-Build '-s'
        } finally { $env:ORCA_SLICER_CMAKE_ARGS = $previousArgs }
    }
    'Publish' {
        Assert-Version
        if ($PSBoundParameters.ContainsKey('SettingsVersion')) {
            Get-ExistingRevisionSettings $SettingsVersion | Out-Null
        }
        Assert-Source
        if (!$BuildFolder) { $BuildFolder = Join-Path $sourceRoot 'build/OrcaSlicer' }
        $BuildFolder = (Resolve-Path -LiteralPath $BuildFolder).Path
        Assert-PortableBuild $BuildFolder
        if ($RevisionNotes -and !(Test-Path -LiteralPath $RevisionNotes -PathType Leaf)) {
            throw 'RevisionNotes must name an existing release-notes file.'
        }
        $destination = Join-Path $versionsRoot $Version
        if (Test-Path -LiteralPath $destination) { throw 'Version already exists; choose a new label.' }
        New-Item -ItemType Directory -Force $versionsRoot | Out-Null
        $head = & git -C $sourceRoot rev-parse HEAD
        if ($LASTEXITCODE -ne 0) { throw 'Cannot read source revision.' }
        $dirty = & git -C $sourceRoot status --porcelain
        if ($LASTEXITCODE -ne 0) { throw 'Cannot read source status.' }
        New-Item -ItemType Directory $destination | Out-Null
        Get-ChildItem -LiteralPath $BuildFolder -Force | Where-Object {
            $_.Name -ne 'data_dir' -and $_.Extension -ne '.log'
        } |
            Copy-Item -Destination $destination -Recurse -Force
        Assert-PortableBuild $destination
        # Keep an empty private settings folder even when an existing revision is selected.
        # SettingsVersion points to that revision's folder; no settings or credentials are copied.
        New-Item -ItemType Directory (Join-Path $destination 'data_dir') | Out-Null
        # Keep the source changes needed to reproduce this experimental revision.
        # This snapshot excludes build caches, installed vendor binaries, and sessions.
        $revisionSource = Join-Path $destination 'revision-source'
        New-Item -ItemType Directory $revisionSource | Out-Null
        foreach ($entry in @('integration','scripts','tests','docs','solidworks','Install SolidWorks Add-in.cmd','sources.lock.json',
                              'build-tools.lock.json','manage.ps1','README.md','Launch-Slicer.cmd','Manage-Versions.cmd',
                              'LICENSE.txt','NOTICE.md','CONTRIBUTING.md')) {
            Copy-Item -LiteralPath (Join-Path $taskRoot $entry) -Destination $revisionSource -Recurse
        }
        if ($RevisionNotes) {
            Copy-Item -LiteralPath $RevisionNotes -Destination (Join-Path $destination 'REVISION-NOTES.md')
        } else {
            "# $Version`n`nStatus: needs testing. Real accounts and printer workflows require user validation." |
                Set-Content -LiteralPath (Join-Path $destination 'REVISION-NOTES.md') -Encoding UTF8
        }
        $buildInfo = @{ schemaVersion=2; version=$Version; revisionStatus='needs-testing';
            sourceCommit=$head; sourceModified=[bool]$dirty;
            builtAtUtc=(Get-Item -LiteralPath (Join-Path $BuildFolder 'OrcaSlicer.dll')).LastWriteTimeUtc.ToString('o');
            publishedAtUtc=[DateTime]::UtcNow.ToString('o') }
        if ($PSBoundParameters.ContainsKey('SettingsVersion')) {
            $buildInfo.settingsVersion = $SettingsVersion
        }
        $buildInfo |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'build-info.json') -Encoding UTF8
        Write-Host "Published $Version (needs testing). Select it with: .\manage.ps1 Use -Version $Version"
    }
    'List' {
        if (Test-Path -LiteralPath $versionsRoot) {
            Get-ChildItem -LiteralPath $versionsRoot -Directory | Where-Object {
                Test-Path -LiteralPath (Join-Path $_.FullName 'build-info.json')
            } | Select-Object -ExpandProperty Name
        } else { Write-Host 'No builds published yet.' }
    }
    'Use' {
        Assert-Version
        $destination = Join-Path $versionsRoot $Version
        if (!(Test-Path -LiteralPath (Join-Path $destination 'build-info.json'))) { throw 'Unknown or incomplete version.' }
        @{version=$Version} | ConvertTo-Json |
            Set-Content -LiteralPath (Join-Path $versionsRoot 'selected.json') -Encoding UTF8
        Write-Host "Selected $Version. Run: .\manage.ps1 Run"
    }
    'Run' {
        if (!$Version) { $Version = Get-SelectedVersion }
        Assert-Version
        $destination = Join-Path $versionsRoot $Version
        if (!(Test-Path -LiteralPath (Join-Path $destination 'build-info.json') -PathType Leaf)) {
            throw 'Unknown or incomplete version. Publish a complete build before launching.'
        }
        $buildInfoJson = Get-Content -LiteralPath (Join-Path $destination 'build-info.json') -Raw
        if (!$buildInfoJson -or !$buildInfoJson.TrimStart().StartsWith('{')) {
            throw 'Revision metadata must be a JSON object.'
        }
        $buildInfo = $buildInfoJson | ConvertFrom-Json
        $exe = Join-Path $destination 'orca-slicer.exe'
        if (!(Test-Path -LiteralPath $exe)) { throw 'Selected executable is missing.' }
        $settingsProperty = $buildInfo.PSObject.Properties['settingsVersion']
        if ($null -ne $settingsProperty) {
            $revisionData = Get-ExistingRevisionSettings $settingsProperty.Value
            Start-Process -FilePath $exe -WorkingDirectory $destination -ArgumentList @('--datadir', ('"' + $revisionData + '"'))
        } else {
            $revisionData = Join-Path $destination 'data_dir'
            if (Test-Path -LiteralPath $revisionData) {
                if (!(Test-Path -LiteralPath $revisionData -PathType Container)) {
                    throw 'The revision settings path is not a directory.'
                }
            } else {
                New-Item -ItemType Directory -Path $revisionData | Out-Null
            }
            Start-Process -FilePath $exe -WorkingDirectory $destination
        }
    }
}
