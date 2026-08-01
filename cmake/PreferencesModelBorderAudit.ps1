param(
    [string]$Exe = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-final\Release\BongoCat.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-final\model-border" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$data = Join-Path $OutputDir ("data-$PID-" + [DateTime]::UtcNow.Ticks)
New-Item -ItemType Directory -Force -Path $data | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatModelBorderNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr handle, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr handle, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll", EntryPoint="ClipCursor")] public static extern bool ClipCursorRect(ref Rect rect);
    [DllImport("user32.dll", EntryPoint="ClipCursor")] public static extern bool ReleaseCursor(IntPtr rect);
    [DllImport("user32.dll")] public static extern bool ReleaseCapture();
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr handle, uint message, UIntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][BongoCatModelBorderNative]::SetProcessDPIAware()
$script:UiScale = 1.0

function Wait-Preferences([int]$ProcessId) {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    $path = Join-Path $data "preferences-window.txt"
    do {
        $text = Get-Content -Raw -LiteralPath $path -ErrorAction SilentlyContinue
        if ($text -match 'handle=(\d+)') {
            $handle = [IntPtr][long]$Matches[1]
            [uint32]$owner = 0
            [void][BongoCatModelBorderNative]::GetWindowThreadProcessId(
                $handle, [ref]$owner)
            $rect = [BongoCatModelBorderNative+Rect]::new()
            if ($owner -eq $ProcessId -and
                [BongoCatModelBorderNative]::GetClientRect($handle, [ref]$rect)) {
                return $handle
            }
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    $evidence = Get-Content -Raw -LiteralPath $path -ErrorAction SilentlyContinue
    $startup = Get-Content -Tail 8 (Join-Path $data "startup.log") -ErrorAction SilentlyContinue
    if ($startup -match 'Missing OpenGL function:' -and $startup -match 'Preferences failed') {
        Write-Output "Preferences audit skipped: OpenGL shader APIs unavailable"
        exit 77
    }
    throw "Preferences window was not created: handle=[$evidence] startup=[$startup]"
}

function Move-Client([IntPtr]$Window, [int]$X, [int]$Y) {
    $X = [int][Math]::Round($X * $script:UiScale)
    $Y = [int][Math]::Round($Y * $script:UiScale)
    $point = [BongoCatModelBorderNative+Point]::new()
    $point.X = $X; $point.Y = $Y
    [void][BongoCatModelBorderNative]::ClientToScreen($Window, [ref]$point)
    [void][BongoCatModelBorderNative]::SetCursorPos($point.X, $point.Y)
    $position = (($Y -band 0xffff) -shl 16) -bor ($X -band 0xffff)
    [void][BongoCatModelBorderNative]::SendMessageW($Window, 0x0200,
        [UIntPtr]::Zero, [IntPtr]$position)
}

function Release-Pointer([IntPtr]$Window) {
    [void][BongoCatModelBorderNative]::ReleaseCapture()
    [void][BongoCatModelBorderNative]::SendMessageW(
        $Window, 0x001F, [UIntPtr]::Zero, [IntPtr]::Zero)
    [void][BongoCatModelBorderNative]::SendMessageW(
        $Window, 0x0202, [UIntPtr]::Zero, [IntPtr]::Zero)
}

function Click-Client([IntPtr]$Window, [int]$X, [int]$Y) {
    Move-Client $Window $X $Y
    $nativeX = [int][Math]::Round($X * $script:UiScale)
    $nativeY = [int][Math]::Round($Y * $script:UiScale)
    $point = [BongoCatModelBorderNative+Point]::new()
    $point.X = $nativeX; $point.Y = $nativeY
    [void][BongoCatModelBorderNative]::ClientToScreen($Window, [ref]$point)
    $clip = [BongoCatModelBorderNative+Rect]::new()
    $clip.L = $point.X; $clip.T = $point.Y
    $clip.R = $point.X + 1; $clip.B = $point.Y + 1
    $packed = [IntPtr](($nativeX -band 0xffff) -bor
        (($nativeY -band 0xffff) -shl 16))
    [void][BongoCatModelBorderNative]::ClipCursorRect([ref]$clip)
    try {
        [void][BongoCatModelBorderNative]::SendMessageW(
            $Window, 0x0201, [UIntPtr]::new(1), $packed)
        Start-Sleep -Milliseconds 5
        [void][BongoCatModelBorderNative]::SendMessageW(
            $Window, 0x0202, [UIntPtr]::Zero, $packed)
    } finally {
        [void][BongoCatModelBorderNative]::ReleaseCursor([IntPtr]::Zero)
    }
}

function Select-ModelPage([IntPtr]$Window, [string]$Frame) {
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        Release-Pointer $Window
        Click-Client $Window 82 358
        $deadline = [DateTime]::UtcNow.AddSeconds(1)
        do {
            $content = Get-Content -Raw -LiteralPath $Frame `
                -ErrorAction SilentlyContinue
            if ($content -match 'page=(\d+)' -and [int]$Matches[1] -eq 2) {
                return
            }
            Start-Sleep -Milliseconds 20
        } while ([DateTime]::UtcNow -lt $deadline)
    }
    throw "Preferences did not remain on the model page"
}

function Save-Client([IntPtr]$Window, [string]$Name) {
    $rect = [BongoCatModelBorderNative+Rect]::new()
    [void][BongoCatModelBorderNative]::GetClientRect($Window, [ref]$rect)
    $origin = [BongoCatModelBorderNative+Point]::new()
    [void][BongoCatModelBorderNative]::ClientToScreen($Window, [ref]$origin)
    $bitmap = [Drawing.Bitmap]::new($rect.R, $rect.B)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, $bitmap.Size)
    $path = Join-Path $OutputDir $Name
    $bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    return $path
}

function Measure-Outline([string]$Path, [string]$Color,
    [int]$Left, [int]$Top, [int]$Right, [int]$Bottom, [double]$Scale) {
    $Left = [int][Math]::Floor($Left * $Scale)
    $Top = [int][Math]::Floor($Top * $Scale)
    $Right = [int][Math]::Ceiling($Right * $Scale)
    $Bottom = [int][Math]::Ceiling($Bottom * $Scale)
    $bitmap = [Drawing.Bitmap]::new($Path)
    try {
        $points = [Collections.Generic.List[object]]::new()
        for ($y = $Top; $y -lt [Math]::Min($Bottom, $bitmap.Height); $y++) {
            for ($x = $Left; $x -lt [Math]::Min($Right, $bitmap.Width); $x++) {
                $pixel = $bitmap.GetPixel($x, $y)
                $match = if ($Color -eq "pink") {
                    $pixel.R -ge 225 -and $pixel.G -ge 85 -and
                    $pixel.G -le 165 -and $pixel.B -ge 135 -and $pixel.B -le 205
                } else {
                    $pixel.R -ge 40 -and $pixel.R -le 200 -and
                    $pixel.G -ge 145 -and $pixel.G -le 240 -and
                    $pixel.B -ge 230 -and $pixel.B - $pixel.R -ge 40
                }
                if ($match) { $points.Add([pscustomobject]@{ X=$x; Y=$y }) }
            }
        }
        if (-not $points.Count) { return [pscustomobject]@{ Pixels=0; Passed=$false } }
        $minX = ($points.X | Measure-Object -Minimum).Minimum
        $maxX = ($points.X | Measure-Object -Maximum).Maximum
        $minY = ($points.Y | Measure-Object -Minimum).Minimum
        $maxY = ($points.Y | Measure-Object -Maximum).Maximum
        $leftCount = @($points | Where-Object { $_.X -le $minX + 3 }).Count
        $rightCount = @($points | Where-Object { $_.X -ge $maxX - 3 }).Count
        $bottomCount = @($points | Where-Object { $_.Y -ge $maxY - 3 }).Count
        $bottomLeft = @($points | Where-Object {
            $_.X -le $minX + 18 -and $_.Y -ge $maxY - 18 }).Count
        $bottomRight = @($points | Where-Object {
            $_.X -ge $maxX - 18 -and $_.Y -ge $maxY - 18 }).Count
        $passed = $maxX - $minX -ge 200 * $Scale -and
            $maxY - $minY -ge 200 * $Scale -and
            $leftCount -ge 70 * $Scale -and $rightCount -ge 70 * $Scale -and
            $bottomCount -ge 130 * $Scale -and
            $bottomLeft -ge 30 * $Scale -and $bottomRight -ge 30 * $Scale
        return [pscustomobject]@{ Pixels=$points.Count; MinX=$minX; MaxX=$maxX;
            MinY=$minY; MaxY=$maxY; Left=$leftCount; Right=$rightCount;
            Bottom=$bottomCount; BottomLeft=$bottomLeft;
            BottomRight=$bottomRight; Passed=$passed }
    } finally { $bitmap.Dispose() }
}

$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_TEST_INSTANCE_ID = "preferences-model-border-audit-$PID"
$arguments = @("--ci-smoke", "--ci-preferences", "--ci-preference-page=2",
    "--ci-language=zh-CN", "--ci-theme=light", "--ci-exit-ms=15000",
    "--ci-ignore-global-input", "--data-root=$data")
$process = Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -PassThru
try {
    $window = Wait-Preferences $process.Id
    Start-Sleep -Milliseconds 500
    $window = Wait-Preferences $process.Id
    $client = [BongoCatModelBorderNative+Rect]::new()
    [void][BongoCatModelBorderNative]::GetClientRect($window, [ref]$client)
    $script:UiScale = $client.R / 900.0
    [void][BongoCatModelBorderNative]::SetWindowPos($window,
        [IntPtr](-1), 40, 40, [int](900 * $script:UiScale),
        [int](680 * $script:UiScale), 0x0040)
    [void][BongoCatModelBorderNative]::SetForegroundWindow($window)
    $frame = Join-Path $data "ui-frame.txt"
    Select-ModelPage $window $frame
    Move-Client $window 100 500
    Start-Sleep -Milliseconds 120
    Move-Client $window 750 200
    Start-Sleep -Milliseconds 600
    $hoverPath = Save-Client $window "hover-default.png"
    $hover = Measure-Outline $hoverPath "blue" 620 100 875 350 $script:UiScale
    [void][BongoCatModelBorderNative]::SetWindowPos($window,
        [IntPtr](-1), 40, 40, [int](720 * $script:UiScale),
        [int](560 * $script:UiScale), 0x0040)
    Select-ModelPage $window $frame
    Move-Client $window 100 500
    Start-Sleep -Milliseconds 600
    $selectedPath = Save-Client $window "selected-minimum.png"
    $selected = Measure-Outline $selectedPath "pink" 350 90 705 355 $script:UiScale
    $passed = $hover.Passed -and $selected.Passed
    $result = [ordered]@{ HoverBlue=$hover; SelectedPink=$selected; Passed=$passed }
    $result | ConvertTo-Json -Depth 3 | Set-Content -Encoding utf8 `
        (Join-Path $OutputDir "result.json")
    $hover | Format-List; $selected | Format-List
    if (-not $passed) { exit 1 }
} finally {
    if ($process -and -not $process.HasExited) {
        $process.Kill(); $process.WaitForExit()
    }
}
