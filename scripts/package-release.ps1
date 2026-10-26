[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Version,
    [string]$BuildFolder,
    [Parameter(Mandatory=$true)][string]$ReleaseNotes
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
if ($Version -notmatch '^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$' -or $Version.EndsWith('.')) {
    throw 'Use a version label containing letters, digits, dots, dashes or underscores.'
}
if (!$BuildFolder) { $BuildFolder = Join-Path $taskRoot 'upstream/OrcaSlicer/build/OrcaSlicer' }
$BuildFolder = (Resolve-Path -LiteralPath $BuildFolder).Path
$ReleaseNotes = (Resolve-Path -LiteralPath $ReleaseNotes).Path
& (Join-Path $taskRoot 'manage.ps1') Verify -BuildFolder $BuildFolder
$workspaceCommit = & git -C $taskRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Release packaging requires a committed source checkout.' }
$dirty = @(& git -C $taskRoot status --porcelain)
if ($LASTEXITCODE -ne 0 -or $dirty.Count) { throw 'Commit the release source and notes before packaging.' }
$releases = Join-Path $taskRoot 'artifacts/releases'
New-Item -ItemType Directory -Force -Path $releases | Out-Null
$name = "Orqa-$Version-Windows-x64"
$archive = Join-Path $releases ($name + '.zip')
if (Test-Path -LiteralPath $archive) { throw 'This release archive already exists. Use a new version label.' }
$stagingRoot = Join-Path $releases ('staging-' + [guid]::NewGuid().ToString('N'))
$stage = Join-Path $stagingRoot $name
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Write-Host 'Copying the installed runtime into an isolated staging folder...'
$binaryHashes = [ordered]@{}
foreach ($binary in @('orca-slicer.exe','OrcaSlicer.dll')) {
    $binaryHashes[$binary] = (Get-FileHash -LiteralPath (Join-Path $BuildFolder $binary)).Hash
}
# Copy only installed runtime files. Source/dependency archives and account state
# are never collected by traversing the whole working directory.
Get-ChildItem -LiteralPath $BuildFolder -Force | Where-Object {
    $_.Name -ne 'data_dir' -and $_.Extension -ne '.log'
} | Copy-Item -Destination $stage -Recurse -Force
foreach ($binary in $binaryHashes.Keys) {
    if ((Get-FileHash -LiteralPath (Join-Path $stage $binary)).Hash -ne $binaryHashes[$binary] -or
        (Get-FileHash -LiteralPath (Join-Path $BuildFolder $binary)).Hash -ne $binaryHashes[$binary]) {
        throw 'The native build changed during packaging. Wait for the build to finish before retrying.'
    }
}
foreach ($file in @('LICENSE.txt','NOTICE.md')) {
    Copy-Item -LiteralPath (Join-Path $taskRoot $file) -Destination $stage -Force
}
Copy-Item -LiteralPath $ReleaseNotes -Destination (Join-Path $stage 'RELEASE-NOTES.md')
New-Item -ItemType Directory -Path (Join-Path $stage 'data_dir') | Out-Null
@'
@echo off
setlocal
start "" /D "%~dp0" "%~dp0orca-slicer.exe" --datadir "%~dp0data_dir"
'@ | Set-Content -LiteralPath (Join-Path $stage 'START ORQA.cmd') -Encoding ASCII
@'
# Orqa portable preview

Extract this entire ZIP to a writable folder, then double-click START ORQA.cmd.
Complete the normal profile setup. Your accounts and presets stay in this copy's
data_dir; this archive contains no saved accounts or personal settings.

Read RELEASE-NOTES.md before printing. This is a preview with the verification
limits described there. Printer plug-ins are obtained separately; Flashforge's
compatible networking plug-in must be selected in Printer accounts.

Keep an existing installation until you finish testing. Do not delete or replace
its settings. Existing source-workspace users can continue using their version
manager and shared settings; this standalone copy uses its own portable settings.

The SolidWorks add-in is built separately on a PC with SolidWorks installed.
The matching workspace source and build instructions are in revision-source.zip.
'@ | Set-Content -LiteralPath (Join-Path $stage 'START-HERE.md') -Encoding UTF8
$sourceArchive = Join-Path $stage 'revision-source.zip'
Write-Host 'Saving the matching committed source...'
& git -C $taskRoot archive --format=zip "--output=$sourceArchive" $workspaceCommit
if ($LASTEXITCODE -ne 0) { throw 'Cannot archive matching workspace source.' }
$sourceLock = Get-Content -LiteralPath (Join-Path $taskRoot 'sources.lock.json') -Raw | ConvertFrom-Json
@{version=$Version; revisionStatus='needs-testing'; workspaceCommit=$workspaceCommit;
  upstreamSources=$sourceLock.sources; binaryHashes=$binaryHashes; packagedAtUtc=[DateTime]::UtcNow.ToString('o') } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $stage 'BUILD-INFO.json') -Encoding UTF8
& (Join-Path $taskRoot 'manage.ps1') Verify -BuildFolder $stage
# Only the installed runtime tree is copied above. Private state names are
# also rejected anywhere in the resulting package.
$privateFiles = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object {
    $_.Extension -eq '.log' -or $_.Name -in @('user.conf','session.json','auth.json') -or
    $_.FullName -like (Join-Path $stage 'data_dir/*')
})
if ($privateFiles.Count) { throw 'Release contains runtime state or logs. Review the staging folder before retrying.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$partialArchive = Join-Path $releases ($name + '.partial-' + [guid]::NewGuid().ToString('N') + '.zip')
Write-Host 'Compressing the portable package...'
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $partialArchive, [IO.Compression.CompressionLevel]::Optimal, $true)
$zip = [IO.Compression.ZipFile]::OpenRead($partialArchive)
try {
    foreach ($required in @('START ORQA.cmd','START-HERE.md','RELEASE-NOTES.md','BUILD-INFO.json','revision-source.zip','orca-slicer.exe','OrcaSlicer.dll','data_dir/')) {
        if ($null -eq $zip.GetEntry($name + '/' + $required)) { throw "Archive is missing $required." }
    }
} finally { $zip.Dispose() }
$sha = (Get-FileHash -LiteralPath $partialArchive -Algorithm SHA256).Hash.ToLowerInvariant()
Move-Item -LiteralPath $partialArchive -Destination $archive
"$sha  $name.zip" | Set-Content -LiteralPath ($archive + '.sha256') -Encoding ASCII
Write-Host "Packaged: $archive"
Write-Host "SHA256: $sha"
Write-Host 'Staging folder retained for inspection. No installed version or settings were changed.'
