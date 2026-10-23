param(
    [string]$CompileDatabase = 'upstream/OrcaSlicer/build-tidy/compile_commands.json'
)
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    $deviceDatabase = Get-Content -Raw -LiteralPath $CompileDatabase | ConvertFrom-Json
    $deviceTemplate = $deviceDatabase | Where-Object { $_.file -like '*VendorAccountsDialog.cpp' } | Select-Object -First 1
    if (!$deviceTemplate -or !$deviceTemplate.command.Contains('-DORCA_DUAL_VENDOR_ACCOUNTS') -or $deviceTemplate.command.Contains('/Yu')) {
        throw 'Device checks require the existing account-enabled PCH-free compile database.'
    }
    $deviceOutput = Join-Path (Get-Location) 'artifacts/flashforge-device-panel'
    New-Item -ItemType Directory -Force -Path $deviceOutput | Out-Null
    $deviceEntries = foreach ($deviceName in @('FlashforgeDevicePanel.cpp', 'FlashforgeDevicePanel.hpp')) {
        $deviceSource = (Resolve-Path (Join-Path 'integration/devices' $deviceName)).Path
        $deviceCommand = $deviceTemplate.command.Replace($deviceTemplate.file.Replace('/', '\'), $deviceSource)
        $deviceCommand = $deviceCommand.Replace($deviceTemplate.file, $deviceSource.Replace('\', '/'))
        if (!$deviceCommand.Contains($deviceSource) -and !$deviceCommand.Contains($deviceSource.Replace('\', '/'))) {
            throw 'Could not replace the source path in the compiler command.'
        }
        [pscustomobject]@{
            directory = $deviceTemplate.directory
            file = $deviceSource.Replace('\', '/')
            command = $deviceCommand
        }
    }
    ConvertTo-Json -InputObject @($deviceEntries) -Depth 3 |
        Set-Content -LiteralPath (Join-Path $deviceOutput 'compile_commands.json') -Encoding utf8
} finally {
    Pop-Location
}
