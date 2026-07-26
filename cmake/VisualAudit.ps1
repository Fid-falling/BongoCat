param(
    [string]$Exe = "",
    [string]$OutputDir = "",
    [string[]]$Languages = @("en-US", "zh-CN", "zh-TW", "pt-BR", "vi-VN"),
    [string[]]$Themes = @("light", "dark"),
    [int[]]$Pages = @(0, 1, 2, 3, 4),
    [string[]]$Live2DScenarios = @(),
    [string[]]$ExternalKeys = @(),
    [string]$PreferenceDataRoot = "",
    [int]$PreferenceWidth = 0,
    [int]$PreferenceHeight = 0,
    [switch]$SkipPreferences,
    [switch]$SkipMain
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-desktop\BongoCatNeo.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build\visual-audit" }
if (-not [IO.Path]::IsPathRooted($Exe)) { $Exe = Join-Path (Get-Location) $Exe }
if (-not [IO.Path]::IsPathRooted($OutputDir)) {
    $OutputDir = Join-Path (Get-Location) $OutputDir
}
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
Get-Process BongoCatNeo -ErrorAction SilentlyContinue | Stop-Process -Force
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatNeoVisualNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr handle, int command);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr handle, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern uint MapVirtualKeyW(uint code, uint mapType);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    public struct Rect { public int Left, Top, Right, Bottom; }
}
'@
[void][BongoCatNeoVisualNative]::SetProcessDPIAware()

function Get-AppWindows([int]$ProcessId) {
    $windows = [Collections.Generic.List[object]]::new()
    [BongoCatNeoVisualNative]::EnumWindows({
        param($handle, $data)
        [uint32]$owner = 0
        [void][BongoCatNeoVisualNative]::GetWindowThreadProcessId($handle, [ref]$owner)
        if ($owner -eq $ProcessId -and [BongoCatNeoVisualNative]::IsWindowVisible($handle)) {
            $rect = [BongoCatNeoVisualNative+Rect]::new()
            if ([BongoCatNeoVisualNative]::GetWindowRect($handle, [ref]$rect)) {
                $width = $rect.Right - $rect.Left
                $height = $rect.Bottom - $rect.Top
                if ($width -gt 20 -and $height -gt 20) {
                    $windows.Add([pscustomobject]@{ Handle=$handle; Width=$width; Height=$height; Area=$width*$height })
                }
            }
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null
    return $windows
}

function Wait-AppWindow([int]$ProcessId, [bool]$Largest) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    $required = 1
    if ($Largest) { $required = 2 }
    do {
        $windows = @(Get-AppWindows $ProcessId)
        if ($windows.Count -ge $required) {
            if ($Largest) {
                return $windows | Sort-Object Area -Descending | Select-Object -First 1
            }
            return $windows | Sort-Object Area | Select-Object -First 1
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out waiting for process $ProcessId window"
}

function Save-Window([object]$Window, [string]$Path) {
    [void][BongoCatNeoVisualNative]::ShowWindow($Window.Handle, 9)
    if ($Window.Width -gt 700) {
        [void][BongoCatNeoVisualNative]::SetWindowPos($Window.Handle, [IntPtr](-1),
            40, 40, 0, 0, 0x0041)
    }
    [void][BongoCatNeoVisualNative]::SetForegroundWindow($Window.Handle)
    Start-Sleep -Milliseconds $(if ($Window.Width -gt 700) { 650 } else { 80 })
    $rect = [BongoCatNeoVisualNative+Rect]::new()
    [void][BongoCatNeoVisualNative]::GetWindowRect($Window.Handle, [ref]$rect)
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bitmap = [Drawing.Bitmap]::new($width, $height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $printed = $false
    if ($width -gt 700) {
        $dc = $graphics.GetHdc()
        try { $printed = [BongoCatNeoVisualNative]::PrintWindow($Window.Handle, $dc, 2) }
        finally { $graphics.ReleaseHdc($dc) }
    }
    if (-not $printed) {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    }
    $bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    $colors = [Collections.Generic.HashSet[int]]::new()
    for ($y = 0; $y -lt $height; $y += 12) {
        for ($x = 0; $x -lt $width; $x += 12) { [void]$colors.Add($bitmap.GetPixel($x, $y).ToArgb()) }
    }
    $graphics.Dispose()
    $bitmap.Dispose()
    [void][BongoCatNeoVisualNative]::SetWindowPos($Window.Handle, [IntPtr](-2), 0, 0, 0, 0, 0x0013)
    return [pscustomobject]@{ Width=$width; Height=$height; SampleColors=$colors.Count }
}

. (Join-Path $PSScriptRoot "VisualAuditHelpers.ps1")

if (($Live2DScenarios.Count -or $ExternalKeys.Count) -and $SkipMain) {
    throw "Input scenarios require same-run main idle baselines; remove -SkipMain"
}

$results = [Collections.Generic.List[object]]::new()
$auditData = if ($PreferenceDataRoot) {
    [IO.Path]::GetFullPath($PreferenceDataRoot)
} else { Join-Path $OutputDir ("data-" + [DateTime]::UtcNow.Ticks) }
if (-not $SkipPreferences) {
    foreach ($theme in $Themes) {
        foreach ($language in $Languages) {
            foreach ($page in $Pages) {
            $arguments = @("--ci-smoke", "--ci-preferences", "--ci-preference-page=$page",
                "--ci-language=$language", "--ci-theme=$theme", "--ci-exit-ms=8000",
                "--data-root=$auditData")
            $uiFrame = Join-Path $auditData "ui-frame.txt"
            Remove-Item -LiteralPath $uiFrame -Force -ErrorAction SilentlyContinue
            $process = Start-Process -FilePath $Exe -ArgumentList $arguments -WorkingDirectory (Split-Path $Exe) -PassThru
            try {
                $window = Wait-AppWindow $process.Id $true
                if ($PreferenceWidth -gt 0 -and $PreferenceHeight -gt 0) {
                    [void][BongoCatNeoVisualNative]::SetWindowPos($window.Handle,
                        [IntPtr](-1), 40, 40, $PreferenceWidth,
                        $PreferenceHeight, 0x0040)
                }
                Wait-Frame $uiFrame
                $path = Join-Path $OutputDir "preferences-$theme-$language-page$page.png"
                $audit = Save-Window $window $path
                $results.Add([pscustomobject]@{ View="preferences"; Theme=$theme; Language=$language;
                    Page=$page; Model=""; Scenario=""; Width=$audit.Width; Height=$audit.Height;
                    SampleColors=$audit.SampleColors; Difference=0.0;
                    Passed=($audit.SampleColors -ge 16); Path=$path })
            } finally { Stop-AuditProcess $process }
            }
        }
    }
}

if (-not $SkipMain) {
    foreach ($model in @("standard", "keyboard", "gamepad")) {
        $runData = Join-Path $OutputDir ("data-main-$model-" + [DateTime]::UtcNow.Ticks)
        $arguments = @("--ci-smoke", "--ci-model=$model", "--ci-live2d-scenario=idle",
            "--ci-ignore-global-input", "--ci-exit-ms=8000", "--data-root=$runData")
        $process = Start-Process -FilePath $Exe -ArgumentList $arguments -WorkingDirectory (Split-Path $Exe) -PassThru
        try {
            $window = Wait-AppWindow $process.Id $false
            $frame = Join-Path $runData "frame.bmp"
            Wait-Frame $frame
            $path = Join-Path $OutputDir "main-$model.png"
            $audit = Save-Window $window $path
            $internal = Join-Path $OutputDir "internal-main-$model.bmp"
            if (Test-Path $frame) {
                Copy-Frame $frame $internal
            }
            $log = Join-Path $runData "live2d-audit.txt"
            if (Test-Path $log) { Copy-Item $log (Join-Path $OutputDir "main-$model.txt") -Force }
            $modelBright = Measure-ModelAppearance $internal
            $results.Add([pscustomobject]@{ View="main"; Theme=""; Language=""; Page="";
                Model=$model; Scenario="idle"; Width=$audit.Width; Height=$audit.Height;
                SampleColors=$audit.SampleColors; Difference=0.0;
                ModelBrightPixels=$modelBright;
                Passed=($audit.SampleColors -ge 8 -and
                    $modelBright -ge 500 -and
                    (Test-Live2DLog $log)); Path=$path })
        } finally { Stop-AuditProcess $process }
    }
}

foreach ($specification in $Live2DScenarios) {
    $parts = $specification.Split(':', 2)
    if ($parts.Count -ne 2) { throw "Live2D scenario must be model:scenario: $specification" }
    $model = $parts[0]
    $scenario = $parts[1]
    $runData = Join-Path $OutputDir ("data-$model-$scenario-" + [DateTime]::UtcNow.Ticks)
    $arguments = @("--ci-smoke", "--ci-model=$model",
        "--ci-live2d-scenario=$scenario", "--ci-ignore-global-input",
        "--ci-exit-ms=8000", "--data-root=$runData")
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments -WorkingDirectory (Split-Path $Exe) -PassThru
    try {
        $window = Wait-AppWindow $process.Id $false
        $frame = Join-Path $runData "frame.bmp"
        Wait-Frame $frame
        $path = Join-Path $OutputDir "live2d-$model-$scenario.png"
        $audit = Save-Window $window $path
        $internal = Join-Path $OutputDir "internal-live2d-$model-$scenario.bmp"
        if (Test-Path $frame) { Copy-Frame $frame $internal }
        $log = Join-Path $runData "live2d-audit.txt"
        if (Test-Path $log) { Copy-Item $log (Join-Path $OutputDir "live2d-$model-$scenario.txt") -Force }
        $difference = Measure-ImageDifference `
            (Join-Path $OutputDir "internal-main-$model.bmp") $internal
        $released = $scenario.EndsWith("-release") -or $scenario -eq "key-stress"
        $changed = $scenario -eq "idle" -or ($released -and $difference -le 0.02) -or
            (-not $released -and $difference -ge 0.0005)
        $results.Add([pscustomobject]@{ View="live2d"; Theme=""; Language=""; Page="";
            Model=$model; Scenario=$scenario; Width=$audit.Width; Height=$audit.Height;
            SampleColors=$audit.SampleColors; Difference=$difference;
            Passed=($audit.SampleColors -ge 8 -and $changed -and
                (Test-Live2DLog $log)); Path=$path })
    } finally { Stop-AuditProcess $process }
}

foreach ($specification in $ExternalKeys) {
    $parts = $specification.Split(':')
    if ($parts.Count -ne 3) { throw "External key must be model:hex-vk:scenario" }
    $model = $parts[0]
    $virtualKey = [Convert]::ToByte($parts[1], 16)
    $scanCode = [byte]([BongoCatNeoVisualNative]::MapVirtualKeyW($virtualKey, 0) -band 0xff)
    $extended = if ($virtualKey -in 0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,
        0x2d,0x2e,0x5b,0x5c,0xa3,0xa5) { 1 } else { 0 }
    $scenario = $parts[2]
    $runData = Join-Path $OutputDir ("data-key-$model-$scenario-" + [DateTime]::UtcNow.Ticks)
    $arguments = @("--ci-smoke", "--ci-model=$model", "--ci-exit-ms=8000",
        "--data-root=$runData")
    $process = Start-Process -FilePath $Exe -ArgumentList $arguments `
        -WorkingDirectory (Split-Path $Exe) -PassThru
    $pressed = $false
    try {
        $window = Wait-AppWindow $process.Id $false
        [BongoCatNeoVisualNative]::keybd_event($virtualKey, $scanCode, $extended, [UIntPtr]::Zero)
        $pressed = $true
        Start-Sleep -Milliseconds 80
        $path = Join-Path $OutputDir "external-key-$model-$scenario.png"
        $audit = Save-Window $window $path
        $frame = Join-Path $runData "frame.bmp"
        Wait-Frame $frame
        $internal = Join-Path $OutputDir "internal-key-$model-$scenario.bmp"
        if (Test-Path $frame) { Copy-Frame $frame $internal }
        $difference = Measure-ImageDifference `
            (Join-Path $OutputDir "internal-main-$model.bmp") $internal
        $results.Add([pscustomobject]@{ View="external-key"; Theme=""; Language="";
            Page=""; Model=$model; Scenario=$scenario; Width=$audit.Width; Height=$audit.Height;
            SampleColors=$audit.SampleColors; Difference=$difference;
            Passed=($audit.SampleColors -ge 8 -and $difference -ge 0.0005); Path=$path })
    } finally {
        if ($pressed) {
            [BongoCatNeoVisualNative]::keybd_event($virtualKey, $scanCode,
                ($extended -bor 2), [UIntPtr]::Zero)
        }
        Stop-AuditProcess $process
    }
}

$report = Join-Path $OutputDir "audit.csv"
$results | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $report
Save-ContactSheet @($results | Where-Object { $_.View -eq "preferences" -and
    $_.Theme -eq "dark" -and $_.Language -eq "en-US" } | Sort-Object Page) `
    (Join-Path $OutputDir "contact-pages-dark-en-US.png")
Save-ContactSheet @($results | Where-Object { $_.View -eq "preferences" -and
    $_.Page -eq 1 } | Sort-Object Language,Theme) `
    (Join-Path $OutputDir "contact-languages-page1.png") 2
Save-ContactSheet @($results | Where-Object { $_.View -eq "main" } | Sort-Object Model) `
    (Join-Path $OutputDir "contact-main.png") 3
Save-ContactSheet @($results | Where-Object { $_.View -eq "live2d" } |
    Sort-Object Model,Scenario) (Join-Path $OutputDir "contact-live2d.png") 3
Save-ContactSheet @($results | Where-Object { $_.View -eq "external-key" } |
    Sort-Object Model,Scenario) (Join-Path $OutputDir "contact-external-keys.png") 3
$failed = @($results | Where-Object { -not $_.Passed })
Write-Output "Captured $($results.Count) views; failed visual/runtime checks: $($failed.Count); report: $report"
if ($failed.Count) { $failed | Format-Table View,Theme,Language,Page,Model,SampleColors,Path; exit 1 }
