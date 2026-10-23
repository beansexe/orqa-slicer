[CmdletBinding()]
param([string]$SolidWorksDirectory, [string]$OutputDirectory = 'artifacts/solidworks-0.1.0.16')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
if (!$SolidWorksDirectory) {
    $setup = Get-ChildItem 'HKLM:/SOFTWARE/SOLIDWORKS' -ErrorAction SilentlyContinue |
        Where-Object PSChildName -Match '^SOLIDWORKS 20\d\d$' | Sort-Object PSChildName -Descending | Select-Object -First 1
    if ($setup) { $SolidWorksDirectory = (Get-ItemProperty ($setup.PSPath + '\Setup')).'SolidWorks Folder' }
}
$redist = Join-Path $SolidWorksDirectory 'api/redist'
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$output = Join-Path $taskRoot $OutputDirectory
New-Item -ItemType Directory -Force -Path $output | Out-Null
$references = @('System.dll','System.Core.dll','System.Drawing.dll','System.Windows.Forms.dll',
    'System.Web.Extensions.dll','System.IO.Compression.dll','System.IO.Compression.FileSystem.dll')
foreach ($name in @('sldworks','swconst','swpublished')) {
    $path = Join-Path $redist "SolidWorks.Interop.$name.dll"
    if (!(Test-Path -LiteralPath $path)) { throw "SolidWorks API assembly missing: $path" }
    $references += $path
    Copy-Item -LiteralPath $path -Destination $output -Force
}
$arguments = @('/nologo','/target:library','/platform:x64','/optimize+',('/out:' + (Join-Path $output 'Orqa.SolidWorks.Bridge16.dll')))
$arguments += $references | ForEach-Object { '/reference:' + $_ }
$arguments += @((Join-Path $taskRoot 'solidworks/BridgeClient.cs'), (Join-Path $taskRoot 'solidworks/OrqaAddin.cs'), (Join-Path $taskRoot 'solidworks/AssemblyInfo.cs'))
& $compiler @arguments
if ($LASTEXITCODE -ne 0) { throw 'SolidWorks add-in compilation failed.' }
$loaderArguments = @('/nologo','/target:exe','/platform:x64',('/out:' + (Join-Path $output 'LoadAddin.exe')),
    '/reference:System.dll','/reference:System.Web.Extensions.dll',('/reference:' + (Join-Path $output 'Orqa.SolidWorks.Bridge16.dll')),
    ('/reference:' + (Join-Path $output 'SolidWorks.Interop.sldworks.dll')),
    ('/reference:' + (Join-Path $output 'SolidWorks.Interop.swpublished.dll')),
    (Join-Path $taskRoot 'solidworks/LoadAddin.cs'))
& $compiler @loaderArguments
if ($LASTEXITCODE -ne 0) { throw 'SolidWorks add-in loader compilation failed.' }
# SolidWorks' legacy icon strip uses white as its transparent color.
Add-Type -AssemblyName System.Drawing
foreach ($size in @(16,24)) {
    foreach ($count in @(1,2)) {
        $bitmap = [Drawing.Bitmap]::new($size * $count, $size)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.Clear([Drawing.Color]::White)
            for ($index = 0; $index -lt $count; $index++) {
                $brush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(0,150,136))
                $pen = [Drawing.Pen]::new([Drawing.Color]::White, 2)
                try {
                    $graphics.FillRectangle($brush, $index * $size + 1, 1, $size - 2, $size - 2)
                    $graphics.DrawRectangle($pen, $index * $size + 4, 4, $size - 8, $size - 8)
                    if ($index -eq 1) { $graphics.DrawLine($pen, $index * $size + 3, $size / 2, ($index + 1) * $size - 3, $size / 2) }
                } finally { $brush.Dispose(); $pen.Dispose() }
            }
            $name = if ($count -eq 1) { "main$size.bmp" } else { "commands$size.bmp" }
            $bitmap.Save((Join-Path $output $name), [Drawing.Imaging.ImageFormat]::Bmp)
        } finally { $graphics.Dispose(); $bitmap.Dispose() }
    }
}
[IO.File]::WriteAllText((Join-Path $output 'workspace.txt'), $taskRoot, [Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $taskRoot 'solidworks/install.ps1') -Destination $output -Force
Write-Host "Built: $output/Orqa.SolidWorks.Bridge16.dll"
