[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$manager = Join-Path $taskRoot 'manage.ps1'
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($manager, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw "Manager AST errors: $($parseErrors.Message -join '; ')" }

# Synthetic revisions only. Start-Process is intercepted; no application is launched.
$fixture = Join-Path $taskRoot ('artifacts/manager settings checks-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture | Out-Null
$fixtureManager = Join-Path $fixture 'manage.ps1'
Copy-Item -LiteralPath $manager -Destination $fixtureManager
'{"sources":[]}' | Set-Content -LiteralPath (Join-Path $fixture 'sources.lock.json') -Encoding UTF8
$versionsRoot = Join-Path $fixture 'versions'
New-Item -ItemType Directory -Path $versionsRoot | Out-Null

# Exercise the same label/source validators used by Publish without publishing a build.
foreach ($name in @('Assert-VersionLabel', 'Get-ExistingRevisionSettings')) {
    $definition = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name }, $true)
    if (!$definition) { throw "Missing manager validator: $name" }
    . ([scriptblock]::Create($definition.Extent.Text))
}
function Check([bool]$Condition, [string]$Description) {
    if (!$Condition) { throw $Description }
}
function Expect-Failure([scriptblock]$Operation, [string]$Description) {
    $failed = $false
    try { & $Operation | Out-Null } catch { $failed = $true }
    Check $failed $Description
}
function Write-Metadata([object]$Metadata) {
    ConvertTo-Json -InputObject $Metadata -Depth 5 | Set-Content -LiteralPath $script:targetMetadata -Encoding UTF8
}
$fixtureLaunches = [Collections.Generic.List[object]]::new()
function Start-Process {
    param([string]$FilePath, [string]$WorkingDirectory, [string[]]$ArgumentList)
    $fixtureLaunches.Add([pscustomobject]@{ FilePath=$FilePath; WorkingDirectory=$WorkingDirectory; ArgumentList=$ArgumentList })
}
function Invoke-FixtureRun {
    & $fixtureManager Run -Version '0.1.0-dev.2'
}
function Expect-RunFailure([string]$Description) {
    $before = $fixtureLaunches.Count
    Expect-Failure { Invoke-FixtureRun } $Description
    Check ($fixtureLaunches.Count -eq $before) 'Invalid metadata must fail before attempting to launch.'
}

foreach ($label in @('', '.', '..', '../outside', '..\outside', 'C:\outside', 'name/child', 'name\child',
                       'CON', 'con.json', 'NUL', 'aux.txt', 'COM1', 'lpt9.data', 'ends.', 'has space', ('a' * 65))) {
    Expect-Failure { Assert-VersionLabel $label } "Unsafe version label was accepted: $label"
}
Expect-Failure { Assert-VersionLabel $null } 'Null labels must fail.'
Expect-Failure { Assert-VersionLabel 123 } 'Non-string labels must fail.'
Assert-VersionLabel '0.1.0-dev.1'
Assert-VersionLabel 'preview_test-2'
Expect-Failure { Get-ExistingRevisionSettings 'missing' } 'Missing source revisions must fail.'

$source = Join-Path $versionsRoot '0.1.0-dev.1'
New-Item -ItemType Directory -Path $source | Out-Null
'{}' | Set-Content -LiteralPath (Join-Path $source 'build-info.json') -Encoding UTF8
Expect-Failure { Get-ExistingRevisionSettings '0.1.0-dev.1' } 'Missing source settings directories must fail.'
$sourceData = Join-Path $source 'data_dir'
New-Item -ItemType Directory -Path $sourceData | Out-Null
Check ((Get-ExistingRevisionSettings '0.1.0-dev.1') -eq $sourceData) 'A valid source must resolve to its own settings directory.'

$target = Join-Path $versionsRoot '0.1.0-dev.2'
New-Item -ItemType Directory -Path $target | Out-Null
'' | Set-Content -LiteralPath (Join-Path $target 'orca-slicer.exe') -Encoding ASCII
$script:targetMetadata = Join-Path $target 'build-info.json'
Write-Metadata @{ version='0.1.0-dev.2'; settingsVersion='0.1.0-dev.1' }
Invoke-FixtureRun
Check ($fixtureLaunches.Count -eq 1) 'Valid shared settings should prepare one launch.'
Check ($fixtureLaunches[0].FilePath -eq (Join-Path $target 'orca-slicer.exe')) 'The executable must come from the selected revision.'
Check ($fixtureLaunches[0].WorkingDirectory -eq $target) 'The working directory must remain the selected revision.'
Check (($fixtureLaunches[0].ArgumentList -join ' ') -eq ('--datadir "' + $sourceData + '"')) 'The existing settings path must be explicitly quoted, including spaces.'
Check (!(Test-Path -LiteralPath (Join-Path $target 'data_dir'))) 'Shared settings must not create or copy a private settings folder at launch.'
Check (@(Get-ChildItem -LiteralPath $sourceData -Force).Count -eq 0) 'The source settings directory must remain untouched.'

foreach ($value in @('../outside', 'CON', 'missing', '', 12, @('0.1.0-dev.1'), $null)) {
    Write-Metadata @{ version='0.1.0-dev.2'; settingsVersion=$value }
    Expect-RunFailure 'Invalid settingsVersion metadata must fail.'
}
Write-Metadata @{ version='0.1.0-dev.2'; settingsVersion='0.1.0-dev.1' }
Rename-Item -LiteralPath (Join-Path $source 'build-info.json') -NewName 'build-info.test-backup'
Expect-RunFailure 'A settings source without publication metadata must fail.'
Rename-Item -LiteralPath (Join-Path $source 'build-info.test-backup') -NewName 'build-info.json'
Rename-Item -LiteralPath $sourceData -NewName 'data_dir.test-backup'
Expect-RunFailure 'A settings source without a settings directory must fail.'
'' | Set-Content -LiteralPath $sourceData -Encoding ASCII
Expect-RunFailure 'A file in place of a settings directory must fail.'

foreach ($json in @('', ' ', 'null', '[]', '[{}]', '"not metadata"', '{invalid')) {
    $json | Set-Content -LiteralPath $script:targetMetadata -Encoding UTF8
    Expect-RunFailure 'Malformed revision metadata must fail.'
}
Write-Metadata @{ version='0.1.0-dev.2' }
Invoke-FixtureRun
Check ($fixtureLaunches.Count -eq 2 -and $fixtureLaunches[1].ArgumentList.Count -eq 0) 'Revisions without settingsVersion must retain their default launch arguments.'
Check (Test-Path -LiteralPath (Join-Path $target 'data_dir') -PathType Container) 'Default launch must retain the private settings directory behavior.'
Write-Host 'Manager AST and settings checks passed. Only synthetic fixtures were used; no application, build, or publication was run.'
