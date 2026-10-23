# The compiled helper calls ISldWorks directly, avoiding PowerShell COM dispatch.
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
& (Join-Path $taskRoot 'artifacts/solidworks-0.1.0.16/LoadAddin.exe')
if ($LASTEXITCODE -ne 0) { throw 'SolidWorks add-in loading failed. See the loader error above.' }
