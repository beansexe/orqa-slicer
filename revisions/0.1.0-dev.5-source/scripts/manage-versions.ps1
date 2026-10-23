[CmdletBinding()]
param([switch]$ValidateOnly)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$taskRoot = Split-Path $PSScriptRoot -Parent
$versionsRoot = Join-Path $taskRoot 'versions'
$manager = Join-Path $taskRoot 'manage.ps1'

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Orqa slicer revisions'
$form.ClientSize = New-Object System.Drawing.Size(760,420)
$form.MinimumSize = New-Object System.Drawing.Size(700,430)
$form.StartPosition = 'CenterScreen'

$intro = New-Object System.Windows.Forms.Label
$intro.Text = 'Choose a saved revision. Older revisions stay available for rollback.'
$intro.Location = New-Object System.Drawing.Point(16,16)
$intro.Size = New-Object System.Drawing.Size(720,32)
$intro.Anchor = 'Top,Left,Right'
$form.Controls.Add($intro)

$list = New-Object System.Windows.Forms.ListView
$list.Location = New-Object System.Drawing.Point(16,52)
$list.Size = New-Object System.Drawing.Size(728,275)
$list.Anchor = 'Top,Bottom,Left,Right'
$list.View = 'Details'
$list.FullRowSelect = $true
$list.MultiSelect = $false
$list.HideSelection = $false
[void]$list.Columns.Add('Revision',205)
[void]$list.Columns.Add('Testing status',150)
[void]$list.Columns.Add('Published',220)
[void]$list.Columns.Add('Default',85)
$form.Controls.Add($list)

$status = New-Object System.Windows.Forms.Label
$status.Location = New-Object System.Drawing.Point(16,334)
$status.Size = New-Object System.Drawing.Size(728,30)
$status.Anchor = 'Bottom,Left,Right'
$form.Controls.Add($status)

$buttons = @{}
$buttonX = 16
foreach ($name in @('Launch','Make default','Testing notes','Refresh','Close')) {
    $button = New-Object System.Windows.Forms.Button
    $button.Text = $name
    $button.Location = New-Object System.Drawing.Point($buttonX,373)
    $button.Size = New-Object System.Drawing.Size(137,30)
    $button.Anchor = 'Bottom,Left'
    $buttons[$name] = $button
    $form.Controls.Add($button)
    $buttonX += 145
}
$form.CancelButton = $buttons['Close']

function Get-RevisionChoice {
    if ($list.SelectedItems.Count -ne 1) { return $null }
    return $list.SelectedItems[0].Tag
}
function Update-Buttons {
    $choice = Get-RevisionChoice
    $buttons['Launch'].Enabled = [bool]($choice -and $choice.executablePresent)
    $buttons['Make default'].Enabled = [bool]($choice -and $choice.executablePresent)
    $buttons['Testing notes'].Enabled = [bool]($choice -and $choice.notesPresent)
}
function Show-ManagerError([string]$message) {
    [void][System.Windows.Forms.MessageBox]::Show($form,$message,'Revision manager','OK','Error')
}
function Refresh-Revisions {
    $list.Items.Clear()
    $selectedVersion = ''
    $selectionFile = Join-Path $versionsRoot 'selected.json'
    if (Test-Path -LiteralPath $selectionFile) {
        try { $selectedVersion = (Get-Content -LiteralPath $selectionFile -Raw | ConvertFrom-Json).version }
        catch { $status.Text = 'The default revision could not be read. Choose one and make it the default.' }
    }
    if (Test-Path -LiteralPath $versionsRoot) {
        foreach ($folder in (Get-ChildItem -LiteralPath $versionsRoot -Directory | Sort-Object Name -Descending)) {
            $infoPath = Join-Path $folder.FullName 'build-info.json'
            if (!(Test-Path -LiteralPath $infoPath)) { continue }
            try { $info = Get-Content -LiteralPath $infoPath -Raw | ConvertFrom-Json }
            catch { continue }
            $item = New-Object System.Windows.Forms.ListViewItem($folder.Name)
            $testingStatus = if ($info.revisionStatus -eq 'needs-testing') { 'Needs testing' } else { 'Testing status unknown' }
            [void]$item.SubItems.Add($testingStatus)
            $published = if ($info.publishedAtUtc) { $info.publishedAtUtc } else { $info.builtAtUtc }
            try { $published = [DateTimeOffset]::Parse($published).ToLocalTime().ToString('MMM d, yyyy h:mm tt') }
            catch { $published = 'Unknown' }
            [void]$item.SubItems.Add($published)
            [void]$item.SubItems.Add($(if ($selectedVersion -eq $folder.Name) { 'Yes' } else { '' }))
            $item.Tag = @{
                version=$folder.Name;
                executablePresent=(Test-Path -LiteralPath (Join-Path $folder.FullName 'orca-slicer.exe'));
                notesPresent=(Test-Path -LiteralPath (Join-Path $folder.FullName 'REVISION-NOTES.md'));
                notesPath=(Join-Path $folder.FullName 'REVISION-NOTES.md')
            }
            [void]$list.Items.Add($item)
            if ($selectedVersion -eq $folder.Name) { $item.Selected = $true }
        }
    }
    if ($list.Items.Count -eq 0) { $status.Text = 'No runnable revisions yet. The native build must finish before publication.' }
    elseif ($selectedVersion) { $status.Text = "Default revision: $selectedVersion" }
    else { $status.Text = 'Select a revision, then choose Make default or Launch.' }
    Update-Buttons
}

$list.Add_SelectedIndexChanged({ Update-Buttons })
$buttons['Launch'].Add_Click({
    $choice = Get-RevisionChoice
    if (!$choice) { return }
    try { & $manager Run -Version $choice.version }
    catch { Show-ManagerError $_.Exception.Message }
})
$buttons['Make default'].Add_Click({
    $choice = Get-RevisionChoice
    if (!$choice) { return }
    try { & $manager Use -Version $choice.version; Refresh-Revisions }
    catch { Show-ManagerError $_.Exception.Message }
})
$buttons['Testing notes'].Add_Click({
    $choice = Get-RevisionChoice
    if (!$choice) { return }
    try { Start-Process -FilePath 'notepad.exe' -ArgumentList ('"' + $choice.notesPath + '"') }
    catch { Show-ManagerError $_.Exception.Message }
})
$buttons['Refresh'].Add_Click({ Refresh-Revisions })
$buttons['Close'].Add_Click({ $form.Close() })
$form.Add_Shown({ Refresh-Revisions })
if ($ValidateOnly) {
    Refresh-Revisions
    $form.CreateControl()
    $form.PerformLayout()
    Write-Output "Revision chooser initialized ($($list.Items.Count) revisions)."
} else {
    [void]$form.ShowDialog()
}
$form.Dispose()
