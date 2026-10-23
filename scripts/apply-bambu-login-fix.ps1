[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$update=Join-Path $taskRoot 'updates/bambu-login-fix-2026-10-19'
$version=Join-Path $taskRoot 'versions/0.1.0-dev.10'
$target=Join-Path $version 'community-bambu'
$incoming=Join-Path $version 'community-bambu-login-fix-incoming'
$backup=Join-Path $version 'community-bambu-before-login-fix'
$marker=Join-Path $version 'BAMBU-LOGIN-FIX.json'
if(Test-Path -LiteralPath (Join-Path $version 'BAMBU-PRINT-AUTH-FIX.json')){
    & (Join-Path $PSScriptRoot 'apply-bambu-print-auth-fix.ps1')
    exit 0
}
$proof=Get-Content -LiteralPath (Join-Path $update 'proof.json') -Raw | ConvertFrom-Json
function Assert-Package([string]$folder) {
    foreach($entry in $proof.binaries.PSObject.Properties){
        $file=Join-Path $folder $entry.Name
        if(!(Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file).Hash -ne $entry.Value){throw "Component checksum mismatch: $($entry.Name)"}
    }
    foreach($entry in $proof.sourceHashes.PSObject.Properties){
        $file=Join-Path (Join-Path $folder 'source') $entry.Name
        if(!(Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file).Hash -ne $entry.Value){throw "Source checksum mismatch: $($entry.Name)"}
    }
}
Assert-Package (Join-Path $update 'community-bambu')
if(Test-Path -LiteralPath $marker){
    Assert-Package $target
    Write-Output 'The Bambu sign-in fix is already installed.'
    exit 0
}
if(Get-Process -Name 'orca-slicer','orca-slicer-console' -ErrorAction SilentlyContinue){
    throw 'Close the slicer first, then run Apply Bambu Sign-in Fix again. Your open project has not been changed.'
}
if(!(Test-Path -LiteralPath $target) -or (Test-Path -LiteralPath $backup) -or (Test-Path -LiteralPath $incoming)){
    throw 'The component folders need review before updating. No existing files have been replaced.'
}
# All moves are between fixed immediate children of this revision directory.
foreach($path in @($target,$incoming,$backup)){
    if([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($path)) -ne [IO.Path]::GetFullPath($version)){
        throw 'Update path is outside the expected revision.'
    }
}
Copy-Item -LiteralPath (Join-Path $update 'community-bambu') -Destination $incoming -Recurse
Assert-Package $incoming
# Recheck after staging, immediately before swapping the component directory.
if(Get-Process -Name 'orca-slicer','orca-slicer-console' -ErrorAction SilentlyContinue){
    throw 'The slicer opened during staging. Close it and ask for the staged update to be completed.'
}
Move-Item -LiteralPath $target -Destination $backup
try {
    Move-Item -LiteralPath $incoming -Destination $target
} catch {
    Move-Item -LiteralPath $backup -Destination $target
    throw
}
Copy-Item -LiteralPath (Join-Path $update 'proof.json') -Destination $marker
Write-Output 'Bambu sign-in fix installed in revision 10. The previous component is saved for rollback.'
Write-Output 'Open the slicer normally, keep Community Cloud selected, and try Google sign-in again.'
