[CmdletBinding()]
param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'
trap {
    Add-Type -AssemblyName System.Windows.Forms
    [Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Orqa Print installation') | Out-Null
    exit 1
}
$guid = '{4FF7418C-716D-42C2-881C-D6652C61D9DC}'
$dll = Join-Path $PSScriptRoot 'Orqa.SolidWorks.Bridge16.dll'
$admin = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (!$admin) { throw 'Run this installer in an administrator PowerShell. SolidWorks discovers ribbon add-ins through its machine-wide Addins registry key.' }
$regasm = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/RegAsm.exe'
$typeLibrary = Join-Path $PSScriptRoot 'Orqa.SolidWorks.Bridge16.tlb'
if ($Uninstall) {
    & $regasm /nologo /unregister ('/tlb:' + $typeLibrary) $dll
    if ($LASTEXITCODE -ne 0) { throw 'COM unregistration failed.' }
    foreach ($path in @("HKLM:/SOFTWARE/SolidWorks/Addins/$guid", "HKCU:/Software/SolidWorks/AddInsStartup/$guid")) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
    Write-Host 'Orqa add-in unregistered. Restart SolidWorks.'
    return
}
if (!(Test-Path -LiteralPath $dll)) { throw 'Build the add-in first with scripts/build-solidworks.ps1.' }
& $regasm /nologo /codebase ('/tlb:' + $typeLibrary) $dll
if ($LASTEXITCODE -ne 0) { throw 'COM registration failed.' }
$key = "HKLM:/SOFTWARE/SolidWorks/Addins/$guid"
New-Item -Path $key -Force | Out-Null
New-ItemProperty -LiteralPath $key -Name '(default)' -Value 0 -PropertyType DWord -Force | Out-Null
New-ItemProperty -LiteralPath $key -Name Title -Value 'Orqa Print' -PropertyType String -Force | Out-Null
New-ItemProperty -LiteralPath $key -Name Description -Value 'Export the active part, reuse Orca settings, and print through connected accounts.' -PropertyType String -Force | Out-Null
$startup = "HKCU:/Software/SolidWorks/AddInsStartup/$guid"
New-Item -Path $startup -Force | Out-Null
New-ItemProperty -LiteralPath $startup -Name '(default)' -Value 1 -PropertyType DWord -Force | Out-Null
$oldStartup = 'HKCU:/Software/SolidWorks/AddInsStartup/{E5416D9B-EC39-48EA-98E5-7D96DCFA0B78}'
if (Test-Path -LiteralPath $oldStartup) {
    New-ItemProperty -LiteralPath $oldStartup -Name '(default)' -Value 0 -PropertyType DWord -Force | Out-Null
}
$previousStartup = 'HKCU:/Software/SolidWorks/AddInsStartup/{1C49DC85-27B2-4A82-8B15-2190E4F8E1AB}'
if (Test-Path -LiteralPath $previousStartup) {
    New-ItemProperty -LiteralPath $previousStartup -Name '(default)' -Value 0 -PropertyType DWord -Force | Out-Null
}
$previous14 = 'HKCU:/Software/SolidWorks/AddInsStartup/{CB93E759-8D06-4699-999B-0613B7DA06A4}'
if (Test-Path -LiteralPath $previous14) { New-ItemProperty -LiteralPath $previous14 -Name '(default)' -Value 0 -PropertyType DWord -Force | Out-Null }
Write-Host 'Installed Orqa Print. Enable it in SolidWorks Tools > Add-ins, or restart SolidWorks. Parts have an Orqa CommandManager tab.'
$previous15 = 'HKCU:/Software/SolidWorks/AddInsStartup/{2B77DBD1-4196-458F-9425-49B2515E28ED}'
if (Test-Path -LiteralPath $previous15) { New-ItemProperty -LiteralPath $previous15 -Name '(default)' -Value 0 -PropertyType DWord -Force | Out-Null }
@{ installed = $true; dll = $dll; installedAtUtc = [DateTime]::UtcNow.ToString('o') } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'install-status.json') -Encoding UTF8
Add-Type -AssemblyName System.Windows.Forms
[Windows.Forms.MessageBox]::Show('Orqa Print is installed. Restart SolidWorks or enable Orqa Print in Tools > Add-ins. Open a part to see the Orqa ribbon tab.', 'Orqa Print') | Out-Null
