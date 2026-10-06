[CmdletBinding()]
param([string]$SettingsPath = (Join-Path $env:LOCALAPPDATA 'SSX-ReXGlue/launcher.json'))
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Settings.ps1')
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$root = Split-Path -Parent $PSScriptRoot
$loadError = ''
try { $script:settings = Read-SsxSettings $SettingsPath $root; Test-SsxSettings $script:settings }
catch { $loadError = "Preferences could not be loaded: $($_.Exception.Message) The file is unchanged."; $script:settings = Get-SsxDefaults $root }
$script:controls = @{}
$schema = @(Get-SsxSchema)
$form = [Windows.Forms.Form]::new()
$form.Text = 'SSX Options - Experimental'
$form.ClientSize = [Drawing.Size]::new(704,720)
$form.AutoScaleMode = 'Dpi'
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.Font = [Drawing.Font]::new('Segoe UI',9)

function Add-Label($Parent, [string]$Text, [int]$X, [int]$Y, [int]$Width, [int]$Height=24) {
    $label = [Windows.Forms.Label]::new()
    $label.Text=$Text; $label.Location=[Drawing.Point]::new($X,$Y); $label.Size=[Drawing.Size]::new($Width,$Height)
    $Parent.Controls.Add($label)
    return $label
}
function Add-Button($Parent, [string]$Text, [int]$X, [int]$Y, [int]$Width=105) {
    $button=[Windows.Forms.Button]::new(); $button.Text=$Text
    $button.Location=[Drawing.Point]::new($X,$Y); $button.Size=[Drawing.Size]::new($Width,30)
    $Parent.Controls.Add($button); return $button
}
function Add-Group($Parent, [string]$Title, [string]$Group, [int]$Y) {
    $fields=@($schema | Where-Object { $_.Group -eq $Group })
    $height=32+$fields.Count*31
    if ($Group -eq 'Files') { $height=32+$fields.Count*76 }
    $box=[Windows.Forms.GroupBox]::new(); $box.Text=$Title
    $box.Location=[Drawing.Point]::new(12,$Y); $box.Size=[Drawing.Size]::new(650,$height)
    $Parent.Controls.Add($box)
    $row=24
    foreach ($field in $fields) {
        $type=if ($field.ContainsKey('Type')) { $field.Type } else { '' }
        if ($type -eq 'bool') {
            $control=[Windows.Forms.CheckBox]::new(); $control.Text=$field.Label
            $control.Location=[Drawing.Point]::new(16,$row); $control.Size=[Drawing.Size]::new(612,26)
        } elseif ($type -in @('file','folder')) {
            $null=Add-Label $box $field.Label 16 $row 600
            $control=[Windows.Forms.TextBox]::new()
            $control.Location=[Drawing.Point]::new(16,($row+26)); $control.Size=[Drawing.Size]::new(510,24)
            $browse=Add-Button $box 'Browse...' 536 ($row+23) 96
            $browse.Tag=@{Kind=$type;Control=$control}
            $browse.Add_Click({
                $target=$this.Tag
                if ($target.Kind -eq 'file') {
                    $dialog=[Windows.Forms.OpenFileDialog]::new(); $dialog.Filter='SSX executable (ssx.exe)|ssx.exe'
                    if ($dialog.ShowDialog($form) -eq 'OK') { $target.Control.Text=$dialog.FileName }
                } else {
                    $dialog=[Windows.Forms.FolderBrowserDialog]::new()
                    if (Test-Path -LiteralPath $target.Control.Text -PathType Container) { $dialog.SelectedPath=$target.Control.Text }
                    if ($dialog.ShowDialog($form) -eq 'OK') { $target.Control.Text=$dialog.SelectedPath }
                }
                $dialog.Dispose()
            })
        } else {
            $null=Add-Label $box $field.Label 16 ($row+3) 240
            if ($field.ContainsKey('Choices')) {
                $control=[Windows.Forms.ComboBox]::new(); $control.DropDownStyle='DropDownList'
                $labels=if ($field.ContainsKey('Labels')) { $field.Labels } else { $field.Choices }
                $control.Items.AddRange([object[]]$labels)
            } else {
                $control=[Windows.Forms.NumericUpDown]::new(); $control.Minimum=$field.Min; $control.Maximum=$field.Max
                if ($field.ContainsKey('Decimals')) { $control.DecimalPlaces=$field.Decimals; $control.Increment=$field.Step }
            }
            $control.Location=[Drawing.Point]::new(266,$row); $control.Size=[Drawing.Size]::new(365,25)
        }
        $control.AccessibleName=$field.Label
        $control.Tag=$field
        $script:controls[$field.Key]=$control
        $box.Controls.Add($control)
        $row+=if ($Group -eq 'Files') {76} else {31}
    }
    return $height
}
function Set-Controls($Values) {
    foreach ($field in $schema) {
        $control=$script:controls[$field.Key]; $value=$Values[$field.Key]
        if ($control -is [Windows.Forms.CheckBox]) { $control.Checked=[bool]$value }
        elseif ($control -is [Windows.Forms.ComboBox]) { $control.SelectedIndex=[array]::IndexOf($field.Choices,[string]$value) }
        elseif ($control -is [Windows.Forms.NumericUpDown]) { $control.Value=[decimal]$value }
        else { $control.Text=[string]$value }
    }
}
function Get-Controls {
    $values=Get-SsxDefaults $root
    foreach ($field in $schema) {
        $control=$script:controls[$field.Key]
        if ($control -is [Windows.Forms.CheckBox]) { $value=$control.Checked }
        elseif ($control -is [Windows.Forms.ComboBox]) {
            $value=$field.Choices[$control.SelectedIndex]
            if ($field.ContainsKey('Type') -and $field.Type -eq 'int') { $value=[int]$value }
        } elseif ($control -is [Windows.Forms.NumericUpDown]) { $value=$control.Value }
        else { $value=$control.Text.Trim() }
        $values[$field.Key]=$value
    }
    return $values
}

$title=Add-Label $form 'SSX  /  Graphics options' 16 14 670 30
$title.Font=[Drawing.Font]::new('Segoe UI',15)
$null=Add-Label $form 'Experimental DLAA, Frame Generation and native HDR' 17 47 670 24
$tabs=[Windows.Forms.TabControl]::new()
$tabs.Location=[Drawing.Point]::new(12,78); $tabs.Size=[Drawing.Size]::new(680,540)
$form.Controls.Add($tabs)
foreach ($name in @('Graphics','HDR brightness','Advanced','Files')) {
    $page=[Windows.Forms.TabPage]::new(); $page.Text=$name; $page.UseVisualStyleBackColor=$true
    $tabs.TabPages.Add($page)
}
$null=Add-Group $tabs.TabPages[0] 'Display and resolution' 'Display' 10
$null=Add-Group $tabs.TabPages[0] 'Antialiasing and frame delivery' 'Rendering' 179
$note=Add-Label $tabs.TabPages[0] "HDR is independent. FG requires DLAA and Reflex; unsupported multipliers stay off.`nDLSS uses integer-scale rendering plus optimal-size input resampling (experimental).`nFullscreen follows the desktop mode; internal scale sets the reconstruction / HUD frame." 24 375 624 60
$note.ForeColor=[Drawing.Color]::DimGray
$presets=[Windows.Forms.GroupBox]::new(); $presets.Text='Quick settings'; $presets.Location=[Drawing.Point]::new(12,436); $presets.Size=[Drawing.Size]::new(650,66)
$tabs.TabPages[0].Controls.Add($presets)
$x=16
foreach ($preset in @('Original','DLAA','HDR + 3x FG','Reset')) {
    $button=Add-Button $presets $preset $x 24 145; $button.Tag=$preset; $x+=156
    $button.Add_Click({
        $v=Get-Controls
        $v.Scale=3; $v.Output='3840x2160'; $v.AA='dlaa'; $v.DLSSPreset='L'; $v.FG=0; $v.HDR=$false
        $v.Reflex='on'; $v.Renderer='auto'; $v.Calibration=$false
        if ($this.Tag -eq 'Original') { $v.Scale=1; $v.Output='1280x720'; $v.AA='original'; $v.Reflex='disabled' }
        elseif ($this.Tag -eq 'HDR + 3x FG') { $v.HDR=$true; $v.FG=3 }
        elseif ($this.Tag -eq 'Reset') {
            $default=Get-SsxDefaults $root
            foreach ($key in @($default.Keys)) { if ($key -notin @('Executable','GameDirectory','SaveDirectory','StreamlineDirectory')) { $v[$key]=$default[$key] } }
        }
        Set-Controls $v; Update-Status
    })
}
$null=Add-Group $tabs.TabPages[1] 'Native HDR brightness' 'HDR' 10
$null=Add-Label $tabs.TabPages[1] "Peak brightness sets the highlight ceiling; paper white sets ordinary scene brightness.`nHUD brightness is independent. Original grading, bloom and HUD blending stay enabled.`n`n1,000 nits is the gameplay checkpoint. Other brightness settings and FG amounts have not been retested for this submission. 3,000 nits is available for a suitable TV.`n`nUnsupported frames use the labeled SDR fallback. Menus and pause screens use SDR mapped into HDR." 26 272 615 175
$null=Add-Group $tabs.TabPages[2] 'Original game and renderer options' 'Advanced' 10
$null=Add-Group $tabs.TabPages[2] 'Optional diagnostics (may affect performance)' 'Diagnostics' 240
$null=Add-Label $tabs.TabPages[2] 'Guest refresh / flip interval also constrain FPS. These do not change the 30 Hz physics tick.' 26 441 615 45
$null=Add-Group $tabs.TabPages[3] 'Local files' 'Files' 10
$null=Add-Label $tabs.TabPages[3] "Use your own extracted game files. Nothing is uploaded.`nExisting saves are used in place. Reset preserves these paths.`n`nPreferences: $SettingsPath" 26 366 615 100
$script:status=Add-Label $form '' 17 626 670 35
$script:status.ForeColor=[Drawing.Color]::DarkRed
$saveButton=Add-Button $form 'Save' 342 674
$playButton=Add-Button $form 'Save && Play' 454 674 116
$cancelButton=Add-Button $form 'Cancel' 577 674
$cancelButton.DialogResult='Cancel'; $form.CancelButton=$cancelButton

function Update-Status {
    if (!$script:status) { return }
    try {
        $v=Get-Controls; Test-SsxSettings $v
        $script:status.ForeColor=[Drawing.Color]::DimGray
        $script:status.Text='Experimental build. Settings apply at the next launch.'
        $saveButton.Enabled=$true; $playButton.Enabled=$true
    } catch {
        $script:status.ForeColor=[Drawing.Color]::DarkRed; $script:status.Text=$_.Exception.Message
        $saveButton.Enabled=$false; $playButton.Enabled=$false
    }
    $enabled=$script:controls.HDR.Checked
    foreach ($key in @('Peak','PaperWhite','UIWhite','HighlightBoost','Exposure','Calibration')) { $script:controls[$key].Enabled=$enabled }
    $script:controls.DLSSPreset.Enabled=($script:controls.AA.SelectedIndex -ne 0)
}
Set-Controls $script:settings
foreach ($control in $script:controls.Values) {
    if ($control -is [Windows.Forms.CheckBox]) { $control.Add_CheckedChanged({ Update-Status }) }
    elseif ($control -is [Windows.Forms.ComboBox]) { $control.Add_SelectedIndexChanged({ Update-Status }) }
    elseif ($control -is [Windows.Forms.NumericUpDown]) { $control.Add_ValueChanged({ Update-Status }) }
}
$saveButton.Add_Click({
    try { Save-SsxSettings (Get-Controls) $SettingsPath; $script:status.Text='Preferences saved. The running game is unchanged.' }
    catch { [Windows.Forms.MessageBox]::Show($form,$_.Exception.Message,'Could not save','OK','Error') | Out-Null }
})
$playButton.Add_Click({
    try {
        $v=Get-Controls
        $process=Start-SsxGame $v
        Save-SsxSettings $v $SettingsPath
        $form.Close()
    } catch { [Windows.Forms.MessageBox]::Show($form,$_.Exception.Message,'Could not launch SSX','OK','Error') | Out-Null }
})
Update-Status
if ($loadError) { $form.Add_Shown({ [Windows.Forms.MessageBox]::Show($form,$loadError,'Preferences','OK','Warning') | Out-Null }) }
[void]$form.ShowDialog()
$form.Dispose()
