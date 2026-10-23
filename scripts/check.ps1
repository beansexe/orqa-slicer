[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
Push-Location $taskRoot
try {
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
