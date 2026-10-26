[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$fixture = Join-Path $taskRoot ('artifacts/launcher checks-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $fixture | Out-Null
function Write-Workspace([string]$Folder, [bool]$Selected) {
    New-Item -ItemType Directory -Force -Path $Folder | Out-Null
    'param([string]$Action) Write-Output ("CHECKED-WORKSPACE: " + $PSScriptRoot)' |
        Set-Content -LiteralPath (Join-Path $Folder 'manage.ps1') -Encoding UTF8
    '{"sources":[]}' | Set-Content -LiteralPath (Join-Path $Folder 'sources.lock.json') -Encoding UTF8
    if ($Selected) {
        New-Item -ItemType Directory -Force -Path (Join-Path $Folder 'versions') | Out-Null
        '{"version":"fixture"}' | Set-Content -LiteralPath (Join-Path $Folder 'versions/selected.json') -Encoding UTF8
    }
    foreach ($launcher in @('Launch-Slicer.cmd','Manage-Versions.cmd')) {
        Copy-Item -LiteralPath (Join-Path $taskRoot $launcher) -Destination $Folder
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $Folder 'scripts') | Out-Null
    Copy-Item -LiteralPath (Join-Path $taskRoot 'scripts/manage-versions.ps1') -Destination (Join-Path $Folder 'scripts')
}
function Check-Launchers([string]$Folder, [string]$Expected) {
    foreach ($launcher in @('Launch-Slicer.cmd','Manage-Versions.cmd')) {
        $output = @(& (Join-Path $Folder $launcher) --check)
        if ($LASTEXITCODE -ne 0 -or $output -notcontains "CHECKED-WORKSPACE: $Expected") {
            throw "$launcher resolved the wrong workspace: $($output -join '; ')"
        }
    }
}
$fresh = Join-Path $fixture 'fresh source'
Write-Workspace $fresh $false
New-Item -ItemType Directory -Path (Join-Path $fresh '.git') | Out-Null
Check-Launchers $fresh $fresh
$installed = Join-Path $fixture 'installed workspace'
Write-Workspace $installed $true
Check-Launchers $installed $installed
$snapshot = Join-Path $installed 'versions/fixture/revision-source'
Write-Workspace $snapshot $false
Check-Launchers $snapshot $installed
$output = @(& powershell.exe -STA -NoProfile -ExecutionPolicy Bypass -File (Join-Path $fresh 'scripts/manage-versions.ps1') -ValidateOnly)
if ($LASTEXITCODE -ne 0 -or $output -notcontains 'Revision chooser initialized (0 revisions).') {
    throw "Fresh checkout's revision chooser resolved another installation: $($output -join '; ')"
}
Write-Host 'Both launchers resolve fresh source, installed workspace and nested snapshots. No application was launched.'
