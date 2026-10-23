[CmdletBinding()]
param([string]$Executable = 'upstream/OrcaSlicer/build/OrcaSlicer/orca-slicer.exe')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
Push-Location $taskRoot
try {
    & tools/python-bootstrap/tools/python.exe tests/prepare-solidworks-slice-smoke.py
    if ($LASTEXITCODE -ne 0) { throw 'Slice fixtures failed.' }
    New-Item -ItemType Directory -Force artifacts/solidworks-tests | Out-Null
    $compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
    & $compiler /nologo /target:exe /platform:x64 ('/out:' + (Join-Path $taskRoot 'artifacts/solidworks-tests/slice-smoke.exe')) /r:System.dll /r:System.Core.dll /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll (Join-Path $taskRoot 'solidworks/BridgeClient.cs') (Join-Path $taskRoot 'tests/solidworks-slice-smoke.cs')
    if ($LASTEXITCODE -ne 0) { throw 'Slice runner did not compile.' }
    & artifacts/solidworks-tests/slice-smoke.exe artifacts/solidworks-slice-smoke $Executable
    if ($LASTEXITCODE -ne 0) { throw 'Real background slicing failed.' }
    & tests/build-solidworks-archive-smoke.cmd
    if ($LASTEXITCODE -ne 0) { throw 'Production archive validation failed.' }
} finally { Pop-Location }
