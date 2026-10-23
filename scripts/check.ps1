[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
Push-Location $taskRoot
try {
    foreach ($script in @((Get-Item (Join-Path $taskRoot 'manage.ps1'))) + @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1') + @(Get-ChildItem -LiteralPath (Join-Path $taskRoot 'solidworks') -Filter '*.ps1')) {
        $tokens = $null
        $errors = $null
        [Management.Automation.Language.Parser]::ParseFile($script.FullName, [ref]$tokens, [ref]$errors) | Out-Null
        if ($errors.Count) { throw "PowerShell syntax error in $($script.Name): $($errors.Message -join '; ')" }
    }
    # These checks compile from source without the slicer dependency cache,
    # vendor libraries, SolidWorks installation, accounts, or physical printers.
    foreach ($test in @('account', 'cloud-print', 'cloud-connection', 'cloud-material',
                        'printer-catalog', 'firmware-notices', 'fast-start-warning')) {
        Write-Host "Checking $test..."
        & (Join-Path $taskRoot "tests/build-$test-tests.cmd")
        if ($LASTEXITCODE -ne 0) { throw "$test checks failed (exit $LASTEXITCODE)." }
    }
    & (Join-Path $taskRoot 'tests/build-solidworks-tests.ps1')
    & (Join-Path $taskRoot 'tests/check-manager-settings.ps1')
    & (Join-Path $taskRoot 'tests/check-launchers.ps1')
    Write-Host 'All source-only checks passed. Hardware acceptance is separate.'
} finally { Pop-Location }
