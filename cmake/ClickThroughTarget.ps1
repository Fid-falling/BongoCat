param(
    [string]$Events,
    [string]$Ready,
    [int]$X,
    [int]$Y,
    [int]$Width,
    [int]$Height
)

Add-Type @'
using System.Runtime.InteropServices;
public static class BongoCatClickTargetDpi {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][BongoCatClickTargetDpi]::SetProcessDPIAware()
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

function Write-Event([string]$Name) {
    [IO.File]::AppendAllText($Events, "$Name`r`n")
}

$form = [Windows.Forms.Form]::new()
$form.Text = "BongoCat Click Through Target"
$form.StartPosition = [Windows.Forms.FormStartPosition]::Manual
$form.FormBorderStyle = [Windows.Forms.FormBorderStyle]::None
$form.Bounds = [Drawing.Rectangle]::new($X, $Y, $Width, $Height)
$form.TopMost = $true
$form.BackColor = [Drawing.Color]::FromArgb(245, 80, 80)
$form.Add_MouseDown({ Write-Event ("down:" + $_.Button) })
$form.Add_MouseUp({ Write-Event ("up:" + $_.Button) })
$form.Add_Shown({ [IO.File]::WriteAllText($Ready, "ready") })
[Windows.Forms.Application]::Run($form)
