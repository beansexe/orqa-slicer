$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$sourceRoot = Join-Path $taskRoot 'upstream/OrcaSlicer'
$patch = Join-Path $taskRoot 'integration/accounts-ui.patch'
if (!(Test-Path -LiteralPath (Join-Path $sourceRoot '.git'))) { throw 'Run Setup first.' }
# Never overwrite or reset unrelated upstream edits. Applying the same patch is safe.
$alreadyApplied = & git -C $sourceRoot apply --reverse --check $patch 2>&1
if ($LASTEXITCODE -ne 0) {
    & git -C $sourceRoot apply --check $patch
    if ($LASTEXITCODE -ne 0) { throw 'Integration patch conflicts with this source revision. Resolve the conflict before building.' }
    & git -C $sourceRoot apply $patch
    if ($LASTEXITCODE -ne 0) { throw 'Integration patch could not be applied.' }
}
$destination = Join-Path $sourceRoot 'src/slic3r/GUI/VendorAccounts'
New-Item -ItemType Directory -Force $destination | Out-Null
$copiedFiles = 0
Get-ChildItem -LiteralPath (Join-Path $taskRoot 'integration') -File |
    Where-Object Extension -in '.cpp','.hpp','.h' |
    ForEach-Object {
        $targetFile = Join-Path $destination $_.Name
        # Keep unchanged timestamps so repeating Build does not force recompilation.
        if (!(Test-Path -LiteralPath $targetFile -PathType Leaf) -or
            (Get-FileHash -LiteralPath $_.FullName).Hash -ne (Get-FileHash -LiteralPath $targetFile).Hash) {
            Copy-Item -LiteralPath $_.FullName -Destination $targetFile -Force
            $copiedFiles++
        }
    }
Write-Host "Account integration source synchronized ($copiedFiles files changed)."
