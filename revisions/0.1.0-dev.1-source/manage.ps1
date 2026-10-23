[CmdletBinding()]
param(
    [Parameter(Position=0)]
    [ValidateSet('Status','Setup','Fetch','Doctor','ApplyIntegration','BuildDeps','Build','Publish','List','Use','Run')]
    [string]$Action = 'Status',
    [string]$Version,
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
function Assert-Version {
    if (!$Version -or $Version -notmatch '^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$' -or
        $Version -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)' -or $Version.EndsWith('.')) {
        throw 'Use a version label such as 0.1.0-dev.1 (letters, numbers, dots, dashes, underscores).'
    }
}
function Get-SelectedVersion {
    $selected = Join-Path $versionsRoot 'selected.json'
    if (!(Test-Path -LiteralPath $selected)) { throw 'No version selected. Publish a build, then use: .\manage.ps1 Use -Version <label>' }
    return (Get-Content -LiteralPath $selected -Raw | ConvertFrom-Json).version
}
function Assert-Source {
    if (!(Test-Path -LiteralPath (Join-Path $sourceRoot 'build_win.bat'))) { throw 'Run Setup first.' }
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
            $env:ORCA_SLICER_CMAKE_ARGS = "$previousArgs -DORCA_DUAL_VENDOR_ACCOUNTS=ON"
            Invoke-Build '-s'
        } finally { $env:ORCA_SLICER_CMAKE_ARGS = $previousArgs }
    }
    'Publish' {
        Assert-Version
        Assert-Source
        if (!$BuildFolder) { $BuildFolder = Join-Path $sourceRoot 'build/OrcaSlicer' }
        $BuildFolder = (Resolve-Path -LiteralPath $BuildFolder).Path
        foreach ($required in @('orca-slicer.exe','OrcaSlicer.dll','resources/profiles')) {
            if (!(Test-Path -LiteralPath (Join-Path $BuildFolder $required))) {
                throw "Portable build is incomplete: missing $required. Finish Build before publishing."
            }
        }
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
        Get-ChildItem -LiteralPath $BuildFolder -Force | Where-Object Name -ne 'data_dir' |
            Copy-Item -Destination $destination -Recurse -Force
        # Each release has its own settings and sessions; never copy credentials into a release.
        New-Item -ItemType Directory (Join-Path $destination 'data_dir') | Out-Null
        # Keep the source changes needed to reproduce this experimental revision.
        # This snapshot excludes build caches, installed vendor binaries, and sessions.
        $revisionSource = Join-Path $destination 'revision-source'
        New-Item -ItemType Directory $revisionSource | Out-Null
        foreach ($entry in @('integration','scripts','tests','docs','sources.lock.json',
                              'build-tools.lock.json','manage.ps1','README.md','Launch-Slicer.cmd','Manage-Versions.cmd')) {
            Copy-Item -LiteralPath (Join-Path $taskRoot $entry) -Destination $revisionSource -Recurse
        }
        if ($RevisionNotes) {
            Copy-Item -LiteralPath $RevisionNotes -Destination (Join-Path $destination 'REVISION-NOTES.md')
        } else {
            "# $Version`n`nStatus: needs testing. Real accounts and printer workflows require user validation." |
                Set-Content -LiteralPath (Join-Path $destination 'REVISION-NOTES.md') -Encoding UTF8
        }
        @{ schemaVersion=2; version=$Version; revisionStatus='needs-testing';
            sourceCommit=$head; sourceModified=[bool]$dirty;
            builtAtUtc=(Get-Item -LiteralPath (Join-Path $BuildFolder 'OrcaSlicer.dll')).LastWriteTimeUtc.ToString('o');
            publishedAtUtc=[DateTime]::UtcNow.ToString('o') } |
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
        $exe = Join-Path $destination 'orca-slicer.exe'
        if (!(Test-Path -LiteralPath $exe)) { throw 'Selected executable is missing.' }
        Start-Process -FilePath $exe -WorkingDirectory $destination
    }
}
