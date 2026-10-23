$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$sourceRoot = Join-Path $taskRoot 'upstream/OrcaSlicer'
if (!(Test-Path -LiteralPath (Join-Path $sourceRoot '.git'))) { throw 'Run Setup first.' }
# Never overwrite or reset unrelated upstream edits. Applying the same patch is safe.
foreach ($patchName in @('native-ui.patch')) {
    $patch = Join-Path $taskRoot ('integration/' + $patchName)
    $alreadyApplied = & git -C $sourceRoot apply --reverse --check $patch 2>&1
    if ($LASTEXITCODE -ne 0) {
        & git -C $sourceRoot apply --check $patch
        if ($LASTEXITCODE -ne 0) { throw "Integration patch $patchName conflicts with this source revision. Resolve the conflict before building." }
        & git -C $sourceRoot apply $patch
        if ($LASTEXITCODE -ne 0) { throw "Integration patch $patchName could not be applied." }
    }
}
$destination = Join-Path $sourceRoot 'src/slic3r/GUI/VendorAccounts'
$copiedFiles = 0
foreach ($relativeDirectory in @('', 'printers', 'devices', 'printing', 'firmware', 'solidworks')) {
    $integrationRoot = Join-Path $taskRoot 'integration'
    $sourceDirectory = if ($relativeDirectory) { Join-Path $integrationRoot $relativeDirectory } else { $integrationRoot }
    $targetDirectory = if ($relativeDirectory) { Join-Path $destination $relativeDirectory } else { $destination }
    New-Item -ItemType Directory -Force $targetDirectory | Out-Null
    Get-ChildItem -LiteralPath $sourceDirectory -File |
        Where-Object Extension -in '.cpp','.hpp','.h' |
        ForEach-Object {
            $targetFile = Join-Path $targetDirectory $_.Name
            # Keep unchanged timestamps so repeating Build does not force recompilation.
            if (!(Test-Path -LiteralPath $targetFile -PathType Leaf) -or
                (Get-FileHash -LiteralPath $_.FullName).Hash -ne (Get-FileHash -LiteralPath $targetFile).Hash) {
                Copy-Item -LiteralPath $_.FullName -Destination $targetFile -Force
                # A source edited during a long build may predate a freshly
                # compiled old copy. Mark the new contents dirty for MSBuild.
                (Get-Item -LiteralPath $targetFile).LastWriteTimeUtc = [DateTime]::UtcNow
                $copiedFiles++
            }
        }
}
Write-Host "Dual-vendor integration source synchronized ($copiedFiles files changed)."
