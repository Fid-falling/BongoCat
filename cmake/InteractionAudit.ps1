param(
    [string]$Exe = "",
    [string]$DataRoot = "$env:APPDATA\bongo_cat\bongo_cat",
    [string]$Model = "model-32027d288-standard-1",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-cubism\Release\BongoCat.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-cubism\interaction-audit" }
$Exe = [IO.Path]::GetFullPath($Exe)
$DataRoot = [IO.Path]::GetFullPath($DataRoot)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$frame = Join-Path $DataRoot "frame.bmp"
$inputAudit = Join-Path $DataRoot "input-audit.txt"
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
New-Item -ItemType Directory -Force -Path $DataRoot | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatInteractionNative {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")]
    public static extern IntPtr GetWindowLongPtr(IntPtr h, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void keybd_event(
        byte key, byte scan, uint flags, UIntPtr extra);
}
'@

function Wait-Window([Diagnostics.Process]$Process) {
    for ($index = 0; $index -lt 100; $index++) {
        $Process.Refresh()
        if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
            return $Process.MainWindowHandle
        }
        Start-Sleep -Milliseconds 40
    }
    throw "BongoCat window was not created"
}

function Wait-Frame {
    for ($index = 0; $index -lt 100 -and -not (Test-Path $frame); $index++) {
        Start-Sleep -Milliseconds 30
    }
    if (-not (Test-Path $frame)) { throw "bongo_cat frame audit was not created" }
}

function Copy-Frame([string]$Destination) {
    for ($index = 0; $index -lt 40; $index++) {
        $bitmap = $null
        try {
            Copy-Item $frame $Destination -Force
            $bitmap = [Drawing.Bitmap]::new($Destination)
            if ($bitmap.Width -lt 2 -or $bitmap.Height -lt 2) {
                throw "Copied frame has invalid dimensions"
            }
            [void]$bitmap.GetPixel($bitmap.Width - 1, $bitmap.Height - 1)
            return
        }
        catch { Start-Sleep -Milliseconds 25 }
        finally { if ($null -ne $bitmap) { $bitmap.Dispose() } }
    }
    throw "Cannot copy the current frame"
}

function Send-VisibilityShortcut {
    [BongoCatInteractionNative]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
    [BongoCatInteractionNative]::keybd_event(0x42, 0, 0, [UIntPtr]::Zero)
    [BongoCatInteractionNative]::keybd_event(0x42, 0, 2, [UIntPtr]::Zero)
    [BongoCatInteractionNative]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)
}

function Wait-Visibility([IntPtr]$Window, [bool]$Expected, [int]$TimeoutMs) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    do {
        if ([BongoCatInteractionNative]::IsWindowVisible($Window) -eq $Expected) {
            return $watch.ElapsedMilliseconds
        }
        Start-Sleep -Milliseconds 5
    } while ($watch.ElapsedMilliseconds -le $TimeoutMs)
    return -1
}

function Wait-RestoredFrame([IntPtr]$Window, [datetime]$Previous, [int]$TimeoutMs) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    do {
        if ([BongoCatInteractionNative]::IsWindowVisible($Window) -and
            [IO.File]::GetLastWriteTimeUtc($frame) -gt $Previous) {
            return $watch.ElapsedMilliseconds
        }
        Start-Sleep -Milliseconds 5
    } while ($watch.ElapsedMilliseconds -le $TimeoutMs)
    return -1
}

function Find-AlphaPoints([string]$Path) {
    $bitmap = [Drawing.Bitmap]::new($Path)
    try {
        $transparent = $null; $opaque = $null
        function Stable-Transparent([Drawing.Bitmap]$Image, [int]$X, [int]$Y) {
            for ($ny = $Y - 10; $ny -le $Y + 10; $ny += 5) {
                for ($nx = $X - 10; $nx -le $X + 10; $nx += 5) {
                    $sample = $Image.GetPixel($nx, $ny)
                    if ($sample.A -ge 32 -and
                        ($sample.R + $sample.G + $sample.B) -ge 100) { return $false }
                }
            }
            return $true
        }
        function Stable-Opaque([Drawing.Bitmap]$Image, [int]$X, [int]$Y) {
            for ($ny = $Y - 10; $ny -le $Y + 10; $ny += 5) {
                for ($nx = $X - 10; $nx -le $X + 10; $nx += 5) {
                    $sample = $Image.GetPixel($nx, $ny)
                    if ($sample.A -le 220 -or ($sample.R + $sample.G + $sample.B) -le 420) {
                        return $false
                    }
                }
            }
            return $true
        }
        for ($y = 4; $y -lt $bitmap.Height - 4; $y += 4) {
            for ($x = 4; $x -lt $bitmap.Width - 4; $x += 4) {
                $pixel = $bitmap.GetPixel($x, $y)
                $interior = $x -gt $bitmap.Width * 0.05 -and
                    $x -lt $bitmap.Width * 0.95 -and $y -gt $bitmap.Height * 0.05 -and
                    $y -lt $bitmap.Height * 0.95 -and $x -gt 14 -and
                    $x -lt $bitmap.Width - 14 -and $y -gt 14 -and
                    $y -lt $bitmap.Height - 14
                $dark = $pixel.A -lt 8 -or ($pixel.R + $pixel.G + $pixel.B) -lt 30
                if ($null -eq $transparent -and $interior -and $dark -and
                    (Stable-Transparent $bitmap $x $y)) {
                    $transparent = @($x, $y)
                }
                $central = $x -gt $bitmap.Width * 0.25 -and $x -lt $bitmap.Width * 0.75 -and
                    $y -gt $bitmap.Height * 0.35 -and $y -lt $bitmap.Height * 0.85
                if ($null -eq $opaque -and $central -and $pixel.A -gt 240 -and
                    ($pixel.R + $pixel.G + $pixel.B) -gt 500 -and
                    (Stable-Opaque $bitmap $x $y)) { $opaque = @($x, $y) }
                if ($null -ne $transparent -and $null -ne $opaque) {
                    return @($transparent, $opaque, @($bitmap.Width, $bitmap.Height))
                }
            }
        }
        throw "Frame does not contain transparent and opaque pixels"
    } finally { $bitmap.Dispose() }
}

function Measure-WatermarkInk([string]$Path) {
    $bitmap = [Drawing.Bitmap]::new($Path)
    try {
        $white = 0; $samples = 0
        for ($y = 15; $y -lt [Math]::Min(300, $bitmap.Height); $y += 3) {
            for ($x = 90; $x -lt [Math]::Min(500, $bitmap.Width); $x += 3) {
                $pixel = $bitmap.GetPixel($x, $y)
                if ($pixel.A -gt 220 -and $pixel.R -gt 220 -and
                    $pixel.G -gt 220 -and $pixel.B -gt 220) { $white++ }
                $samples++
            }
        }
        return $white / [double]$samples
    } finally { $bitmap.Dispose() }
}

function Test-ClickThrough([IntPtr]$Window, [int]$X, [int]$Y) {
    [void][BongoCatInteractionNative]::SetCursorPos($X, $Y)
    Start-Sleep -Milliseconds 350
    $style = [BongoCatInteractionNative]::GetWindowLongPtr($Window, -20).ToInt64()
    return ($style -band 0x20) -ne 0
}

$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_TEST_INSTANCE_ID = "interaction-audit-$PID"
Remove-Item $frame -Force -ErrorAction SilentlyContinue
Remove-Item $inputAudit -Force -ErrorAction SilentlyContinue
$settings = Join-Path $DataRoot "preferences.json"
Set-Content -LiteralPath $settings -Value '{"format":"bongo-cat/preferences","version":2,"model":{"ignoreMouse":true},"shortcuts":{"visibleCat":"Control+B"}}' -NoNewline
$arguments = @("--ci-smoke", "--ci-input-audit", "--ci-exit-ms=14000",
    "--ci-model=$Model", "--data-root=$DataRoot")
$process = Start-Process $Exe -ArgumentList $arguments -WorkingDirectory `
    (Split-Path $Exe) -PassThru
try {
    $window = Wait-Window $process
    $rect = [BongoCatInteractionNative+Rect]::new()
    [void][BongoCatInteractionNative]::GetWindowRect($window, [ref]$rect)
    Wait-Frame; Start-Sleep -Milliseconds 250
    $baseline = Join-Path $OutputDir "toggle-0.bmp"; Copy-Frame $baseline
    $points = Find-AlphaPoints $baseline
    $width = $rect.R - $rect.L; $height = $rect.B - $rect.T; $size = $points[2]
    $transparentX = $rect.L + [int](($points[0][0] + 0.5) * $width / $size[0])
    $transparentY = $rect.T + [int](($points[0][1] + 0.5) * $height / $size[1])
    $opaqueX = $rect.L + [int](($points[1][0] + 0.5) * $width / $size[0])
    $opaqueY = $rect.T + [int](($points[1][1] + 0.5) * $height / $size[1])
    $transparentPasses = Test-ClickThrough $window $transparentX $transparentY
    $opaquePasses = Test-ClickThrough $window $opaqueX $opaqueY
    Send-VisibilityShortcut
    $firstHideLatency = Wait-Visibility $window $false 300
    $previousFrame = [IO.File]::GetLastWriteTimeUtc($frame)
    Send-VisibilityShortcut
    $restoreLatency = Wait-RestoredFrame $window $previousFrame 300
    $restored = Join-Path $OutputDir "toggle-2-restored.bmp"; Copy-Frame $restored
    $baselineInk = Measure-WatermarkInk $baseline
    $restoredInk = Measure-WatermarkInk $restored
    $inkRatio = if ($baselineInk -gt 0) { $restoredInk / $baselineInk } else { 0 }
    Send-VisibilityShortcut
    $secondHideLatency = Wait-Visibility $window $false 300
    $process.Refresh()
    $result = [ordered]@{
        FirstHideLatencyMs = $firstHideLatency
        RestoreLatencyMs = $restoreLatency
        SecondHideLatencyMs = $secondHideLatency
        BaselineWatermarkInk = $baselineInk
        RestoredWatermarkInk = $restoredInk
        RestoredInkRatio = $inkRatio
        TransparentPixelPassThrough = $transparentPasses
        OpaquePixelPassThrough = $opaquePasses
        TransparentPoint = "$transparentX,$transparentY"
        OpaquePoint = "$opaqueX,$opaqueY"
        WindowRect = "$($rect.L),$($rect.T),$($rect.R),$($rect.B)"
        WorkingSetMiB = [Math]::Round($process.WorkingSet64 / 1MB, 2)
        MouseAuditLines = if (Test-Path $inputAudit) {
            @(Get-Content $inputAudit | Where-Object { $_ -like "mouse*" }).Count
        } else { 0 }
        ShortcutInputLines = if (Test-Path $inputAudit) {
            @(Get-Content $inputAudit | Where-Object {
                $_ -match "name=(ControlLeft|ControlRight|KeyB)" }).Count
        } else { 0 }
    }
    $result | ConvertTo-Json | Set-Content (Join-Path $OutputDir "result.json")
    [pscustomobject]$result | Format-List
    $passed = $firstHideLatency -ge 0 -and $firstHideLatency -le 300 -and
        $restoreLatency -ge 0 -and $restoreLatency -le 300 -and
        $secondHideLatency -ge 0 -and $secondHideLatency -le 300 -and
        $inkRatio -ge 0.7 -and $inkRatio -le 1.3 -and
        $result.ShortcutInputLines -ge 12 -and $transparentPasses -and -not $opaquePasses
    if (-not $passed) { exit 1 }
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
