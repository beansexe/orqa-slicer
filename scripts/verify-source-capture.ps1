[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$sourceRoot = Join-Path $taskRoot 'upstream/OrcaSlicer'
$lock = Get-Content (Join-Path $taskRoot 'sources.lock.json') -Raw | ConvertFrom-Json
$expected = ($lock.sources | Where-Object name -eq 'OrcaSlicer').commit
$headCommit = & git -C $sourceRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $headCommit -ne $expected) { throw 'Slicer checkout does not match sources.lock.json.' }
# A temporary index reconstructs the pinned source plus patch without changing
# the developer index, working files, build timestamps, or running application.
$artifactRoot = Join-Path $taskRoot 'artifacts'
New-Item -ItemType Directory -Force -Path $artifactRoot | Out-Null
$scratchIndex = Join-Path $artifactRoot ('orqa-index-' + [guid]::NewGuid().ToString('N'))
$previousIndex = $env:GIT_INDEX_FILE
try {
    $env:GIT_INDEX_FILE = $scratchIndex
    & git -C $sourceRoot read-tree $expected
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read pinned source tree.' }
    & git -C $sourceRoot apply --cached --whitespace=nowarn (Join-Path $taskRoot 'integration/native-ui.patch')
    if ($LASTEXITCODE -ne 0) { throw 'Saved integration patch does not apply to the pinned source.' }
    $differences = @(& git -C $sourceRoot diff --name-only)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot compare working source.' }
    if ($differences.Count) { throw "Working source has uncaptured changes: $($differences -join ', ')" }
} finally {
    $env:GIT_INDEX_FILE = $previousIndex
    Remove-Item -LiteralPath $scratchIndex -Force -ErrorAction SilentlyContinue
}
$destination = Join-Path $sourceRoot 'src/slic3r/GUI/VendorAccounts'
$checked = 0
foreach ($relative in @('', 'printers', 'devices', 'printing', 'firmware', 'solidworks')) {
    $folder = Join-Path (Join-Path $taskRoot 'integration') $relative
    foreach ($file in Get-ChildItem -LiteralPath $folder -File | Where-Object Extension -in '.cpp','.hpp','.h') {
        $target = Join-Path (Join-Path $destination $relative) $file.Name
        if (!(Test-Path -LiteralPath $target) -or (Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) {
            throw "Integration source differs from working slicer: $relative/$($file.Name)"
        }
        $checked++
    }
}
$extras = @(& git -C $sourceRoot ls-files --others --exclude-standard)
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect untracked slicer files.' }
foreach ($extra in $extras) {
    if (!$extra.StartsWith('src/slic3r/GUI/VendorAccounts/')) { throw "Uncaptured untracked source: $extra" }
    $relative = $extra.Substring('src/slic3r/GUI/VendorAccounts/'.Length)
    if (!(Test-Path -LiteralPath (Join-Path (Join-Path $taskRoot 'integration') $relative))) { throw "Uncaptured integration file: $extra" }
}
Write-Host "Source capture verified: pinned baseline, complete native patch, $checked integration files."
