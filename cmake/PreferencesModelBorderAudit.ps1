param(
    [string]$Exe = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-final\Release\BongoCatNeo.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build-final\model-border" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$data = Join-Path $OutputDir ("data-$PID-" + [DateTime]::UtcNow.Ticks)
New-Item -ItemType Directory -Force -Path $data | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatNeoModelBorderNative {
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
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
[void][BongoCatNeoModelBorderNative]::SetProcessDPIAware()

function Wait-Preferences([int]$ProcessId) {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        $found = [Collections.Generic.List[IntPtr]]::new()
        [BongoCatNeoModelBorderNative]::EnumWindows({
            param($handle, $unused)
            [uint32]$owner = 0
            [void][BongoCatNeoModelBorderNative]::GetWindowThreadProcessId(
                $handle, [ref]$owner)
            $rect = [BongoCatNeoModelBorderNative+Rect]::new()
            if ($owner -eq $ProcessId -and
                [BongoCatNeoModelBorderNative]::IsWindowVisible($handle) -and
                [BongoCatNeoModelBorderNative]::GetClientRect($handle, [ref]$rect) -and
                $rect.R -ge 700 -and $rect.B -ge 550) { $found.Add($handle) }
            return $true
        }, [IntPtr]::Zero) | Out-Null
        if ($found.Count) { return $found[0] }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Preferences window was not created"
}

function Move-Client([IntPtr]$Window, [int]$X, [int]$Y) {
    $point = [BongoCatNeoModelBorderNative+Point]::new()
    $point.X = $X; $point.Y = $Y
    [void][BongoCatNeoModelBorderNative]::ClientToScreen($Window, [ref]$point)
    [void][BongoCatNeoModelBorderNative]::SetCursorPos($point.X, $point.Y)
}

function Save-Client([IntPtr]$Window, [string]$Name) {
    $rect = [BongoCatNeoModelBorderNative+Rect]::new()
    [void][BongoCatNeoModelBorderNative]::GetClientRect($Window, [ref]$rect)
    $origin = [BongoCatNeoModelBorderNative+Point]::new()
    [void][BongoCatNeoModelBorderNative]::ClientToScreen($Window, [ref]$origin)
    $bitmap = [Drawing.Bitmap]::new($rect.R, $rect.B)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, $bitmap.Size)
    $path = Join-Path $OutputDir $Name
    $bitmap.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    return $path
}

function Measure-Outline([string]$Path, [string]$Color,
    [int]$Left, [int]$Top, [int]$Right, [int]$Bottom) {
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
        $passed = $maxX - $minX -ge 200 -and $maxY - $minY -ge 200 -and
            $leftCount -ge 80 -and $rightCount -ge 80 -and
            $bottomCount -ge 150 -and $bottomLeft -ge 35 -and $bottomRight -ge 35
        return [pscustomobject]@{ Pixels=$points.Count; MinX=$minX; MaxX=$maxX;
            MinY=$minY; MaxY=$maxY; Left=$leftCount; Right=$rightCount;
            Bottom=$bottomCount; BottomLeft=$bottomLeft;
            BottomRight=$bottomRight; Passed=$passed }
    } finally { $bitmap.Dispose() }
}

$env:BONGO_CAT_NEO_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_NEO_TEST_INSTANCE_ID = "preferences-model-border-audit-$PID"
$arguments = @("--ci-preferences", "--ci-preference-page=2",
    "--ci-language=zh-CN", "--ci-theme=light", "--ci-exit-ms=15000",
    "--data-root=$data")
$process = Start-Process -FilePath $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -PassThru
try {
    $window = Wait-Preferences $process.Id
    [void][BongoCatNeoModelBorderNative]::SetWindowPos($window,
        [IntPtr](-1), 40, 40, 900, 680, 0x0040)
    [void][BongoCatNeoModelBorderNative]::SetForegroundWindow($window)
    Move-Client $window 750 200
    Start-Sleep -Milliseconds 600
    $hoverPath = Save-Client $window "hover-default.png"
    $hover = Measure-Outline $hoverPath "blue" 620 100 875 350
    [void][BongoCatNeoModelBorderNative]::SetWindowPos($window,
        [IntPtr](-1), 40, 40, 720, 560, 0x0040)
    Move-Client $window 100 500
    Start-Sleep -Milliseconds 600
    $selectedPath = Save-Client $window "selected-minimum.png"
    $selected = Measure-Outline $selectedPath "pink" 350 90 705 355
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
