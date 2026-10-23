param(
    [string]$CompileDatabase = 'upstream/OrcaSlicer/build-tidy/compile_commands.json'
)
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    $pickerDatabase = Get-Content -Raw -LiteralPath $CompileDatabase | ConvertFrom-Json
    $pickerTemplate = $pickerDatabase | Where-Object { $_.file -like '*VendorAccountsDialog.cpp' } | Select-Object -First 1
    if (!$pickerTemplate) { throw "Generate Orca's account-enabled compile database before checking the picker." }
    $pickerOutput = Join-Path (Get-Location) 'artifacts/printer-picker'
    New-Item -ItemType Directory -Force -Path $pickerOutput | Out-Null
    $pickerEntries = foreach ($pickerName in @('PrinterPickerDialog.cpp', 'PrinterPickerDialog.hpp')) {
        $pickerSource = (Resolve-Path (Join-Path 'integration/printers' $pickerName)).Path.Replace('\', '/')
        [pscustomobject]@{
            directory = $pickerTemplate.directory
            file = $pickerSource
            command = $pickerTemplate.command.Replace($pickerTemplate.file, $pickerSource)
        }
    }
    ConvertTo-Json -InputObject @($pickerEntries) -Depth 3 |
        Set-Content -LiteralPath (Join-Path $pickerOutput 'compile_commands.json') -Encoding utf8
} finally {
    Pop-Location
}
