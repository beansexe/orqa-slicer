$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
Push-Location $taskRoot
try {
    New-Item -ItemType Directory -Force artifacts/solidworks-tests | Out-Null
    $compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
    & $compiler /nologo /target:exe /platform:x64 ('/out:' + (Join-Path $taskRoot 'artifacts/solidworks-tests/tests.exe')) /r:System.dll /r:System.Core.dll /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll (Join-Path $taskRoot 'solidworks/BridgeClient.cs') (Join-Path $taskRoot 'tests/solidworks-bridge-tests.cs')
    if ($LASTEXITCODE -ne 0) { throw 'Bridge tests did not compile.' }
    & artifacts/solidworks-tests/tests.exe
    if ($LASTEXITCODE -ne 0) { throw 'Bridge tests failed.' }
} finally { Pop-Location }
