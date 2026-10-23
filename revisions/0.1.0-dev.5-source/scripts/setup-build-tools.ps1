$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$toolsRoot = Join-Path $taskRoot 'tools'
New-Item -ItemType Directory -Force $toolsRoot | Out-Null
$toolLock = Get-Content -LiteralPath (Join-Path $taskRoot 'build-tools.lock.json') -Raw | ConvertFrom-Json
foreach ($asset in $toolLock.tools) {
    $archive = Join-Path $toolsRoot $asset.archive
    if (!(Test-Path -LiteralPath $archive)) {
        Write-Host "Downloading $($asset.archive) once."
        Invoke-WebRequest -Uri $asset.url -OutFile $archive
    }
    $sha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($asset.sha256 -ne $sha256) { throw "Hash mismatch: $($asset.archive)" }
    $destination = Join-Path $toolsRoot $asset.name
    $marker = Join-Path $destination '.complete'
    if (!(Test-Path -LiteralPath $marker)) {
        New-Item -ItemType Directory -Force $destination | Out-Null
        & tar -xf $archive -C $destination
        if ($LASTEXITCODE -ne 0) { throw "Cannot extract $($asset.archive)" }
        Set-Content -LiteralPath $marker -Value $sha256
    }
    Write-Host "$($asset.archive): SHA256 $sha256"
}
