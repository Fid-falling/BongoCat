param(
    [string]$Exe = "",
    [string]$OutputDir = "",
    [string]$Language = "zh-CN",
    [switch]$BehaviorDisabled,
    [switch]$PreviewBehaviors,
    [switch]$AllowDynamicBehaviorLabels
)

$ErrorActionPreference = "Stop"
$env:BONGO_CAT_NEO_ALLOW_TEST_INSTANCES = "1"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build\BongoCatNeo.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build\context-menu-audit" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class BongoCatNeoMenuNative {
    public delegate bool EnumProc(IntPtr handle, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr handle, uint command);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr handle, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32.dll")] public static extern uint GetMenuItemID(IntPtr menu, int position);
    [DllImport("user32.dll")] public static extern IntPtr GetSubMenu(IntPtr menu, int position);
    [DllImport("user32.dll")] public static extern bool GetMenuItemRect(
        IntPtr window, IntPtr menu, uint item, out Rect rect);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetMenuStringW(
        IntPtr menu, uint item, StringBuilder text, int max, uint flags);
    [DllImport("user32.dll")] public static extern int GetClassNameW(IntPtr handle, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string className, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(
        IntPtr parent, IntPtr childAfter, string className, string title);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr handle, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(
        byte key, byte scan, uint flags, UIntPtr extra);
}
'@
Add-Type -AssemblyName System.Drawing

function Get-Windows([int]$ProcessId) {
    $rows = [Collections.Generic.List[object]]::new()
    [BongoCatNeoMenuNative]::EnumWindows({
        param($handle, $data)
        [uint32]$owner = 0
        [void][BongoCatNeoMenuNative]::GetWindowThreadProcessId($handle, [ref]$owner)
        if ($owner -eq $ProcessId -and [BongoCatNeoMenuNative]::IsWindowVisible($handle)) {
            $rect = [BongoCatNeoMenuNative+Rect]::new()
            $class = [Text.StringBuilder]::new(64)
            [void][BongoCatNeoMenuNative]::GetClassNameW($handle, $class, 64)
            if ([BongoCatNeoMenuNative]::GetWindowRect($handle, [ref]$rect)) {
                $rows.Add([pscustomobject]@{Handle=$handle; Class=$class.ToString();
                    Rect=$rect; Area=($rect.R-$rect.L)*($rect.B-$rect.T)})
            }
        }
        return $true
    }, [IntPtr]::Zero) | Out-Null
    return $rows
}

function Get-MenuWindow([string]$ExpectedFirstLabel, [int]$ProcessId) {
    $after = [IntPtr]::Zero
    while ($true) {
        $handle = [BongoCatNeoMenuNative]::FindWindowExW(
            [IntPtr]::Zero, $after, "#32768", $null)
        if ($handle -eq [IntPtr]::Zero) { break }
        $after = $handle
        if (-not [BongoCatNeoMenuNative]::IsWindowVisible($handle)) { continue }
        [uint32]$ownerProcess = 0
        [void][BongoCatNeoMenuNative]::GetWindowThreadProcessId(
            $handle, [ref]$ownerProcess)
        if ($ownerProcess -ne $ProcessId) { continue }
        $menu = [BongoCatNeoMenuNative]::SendMessageW($handle, 0x01E1,
            [IntPtr]::Zero, [IntPtr]::Zero)
        if ($menu -eq [IntPtr]::Zero -or
            [BongoCatNeoMenuNative]::GetMenuItemCount($menu) -lt 8) { continue }
        $text = [Text.StringBuilder]::new(128)
        [void][BongoCatNeoMenuNative]::GetMenuStringW($menu, 0, $text, 128, 0x400)
        if ($text.ToString() -eq $ExpectedFirstLabel) {
            return $handle
        }
    }
    return [IntPtr]::Zero
}

function Get-MenuLabels([IntPtr]$Menu) {
    $labels = [Collections.Generic.List[string]]::new()
    if ($Menu -eq [IntPtr]::Zero) { return $labels }
    $count = [BongoCatNeoMenuNative]::GetMenuItemCount($Menu)
    for ($index = 0; $index -lt $count; $index++) {
        $text = [Text.StringBuilder]::new(128)
        [void][BongoCatNeoMenuNative]::GetMenuStringW($Menu, $index, $text, 128, 0x400)
        if ($text.Length) { $labels.Add($text.ToString()) }
    }
    return $labels
}

. (Join-Path $PSScriptRoot "ContextMenuPreviewHelpers.ps1")

$localePath = Join-Path $root "resources\assets\locales\$Language.json"
if (-not (Test-Path $localePath)) { $localePath = Join-Path $root "resources\assets\locales\en-US.json" }
$locale = Get-Content -Raw -Encoding utf8 $localePath | ConvertFrom-Json
$menuLabels = $locale.composables.useAppMenu.labels
$expected = @(
    if ($menuLabels.preference) { $menuLabels.preference } else { "Preferences..." }
    if ($menuLabels.hideCat) { $menuLabels.hideCat } else { "Hide Cat" }
    if ($menuLabels.passThrough) { $menuLabels.passThrough } else { "Pass Through" }
    if ($menuLabels.alwaysOnTop) { $menuLabels.alwaysOnTop } else { "Always on Top" }
    if ($menuLabels.windowSize) { $menuLabels.windowSize } else { "Window Size" }
    if ($menuLabels.opacity) { $menuLabels.opacity } else { "Opacity" }
    if (-not $BehaviorDisabled) {
        if ($menuLabels.motion) { $menuLabels.motion } else { "Motions" }
        if ($menuLabels.expression) { $menuLabels.expression } else { "Expressions" }
    }
    if ($menuLabels.model) { $menuLabels.model } else { "Model" }
    if ($menuLabels.quitApp) { $menuLabels.quitApp } else { "Quit App" }
)
$wheelSizeHint = if ($menuLabels.wheelSizeHint) { $menuLabels.wheelSizeHint } else { "Wheel: resize" }
$wheelOpacityHint = if ($menuLabels.wheelOpacityHint) { $menuLabels.wheelOpacityHint } else { "Ctrl+Wheel: opacity" }
$expectedSize = [Collections.Generic.List[string]]::new()
for ($index = 0; $index -lt 16; $index++) { $expectedSize.Add("$((50 + $index * 10))%") }
$expectedSize.Add($wheelSizeHint)
$expectedOpacity = [Collections.Generic.List[string]]::new()
for ($index = 0; $index -lt 10; $index++) { $expectedOpacity.Add("$((10 + $index * 10))%") }
$expectedOpacity.Add($wheelOpacityHint)
$expectedMotions = [Collections.Generic.List[string]]::new()
$expectedExpressions = [Collections.Generic.List[string]]::new()
if (-not $BehaviorDisabled) {
    $manifestPath = Join-Path $root "resources\assets\models\gamepad\cat.model3.json"
    $manifest = Get-Content -Raw -Encoding utf8 $manifestPath | ConvertFrom-Json
    foreach ($group in $manifest.FileReferences.Motions.PSObject.Properties) {
        for ($index = 0; $index -lt @($group.Value).Count; $index++) {
            $expectedMotions.Add("$($group.Name) $($index + 1)")
        }
    }
    foreach ($expression in $manifest.FileReferences.Expressions) {
        $expectedExpressions.Add($(if ($expression.Name) { $expression.Name } else { "Expression" }))
    }
}
$data = Join-Path $OutputDir ("data-" + [DateTime]::UtcNow.Ticks)
New-Item -ItemType Directory -Force -Path $data | Out-Null
if ($BehaviorDisabled) {
    $settings = @{format="bongo-cat-neo/preferences"; version=1;
        model=@{behavior=$false}} | ConvertTo-Json -Depth 4
    [IO.File]::WriteAllText((Join-Path $data "preferences.json"), $settings,
        [Text.UTF8Encoding]::new($false))
}
$process = Start-Process -FilePath $Exe -ArgumentList @("--ci-smoke", "--ci-context-menu",
    "--ci-language=$Language", "--ci-exit-ms=15000", "--data-root=$data") -WorkingDirectory (Split-Path $Exe) `
    -WindowStyle Normal -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds($(if ($AllowDynamicBehaviorLabels) { 30 } else { 10 }))
do {
    Start-Sleep -Milliseconds 50
    $menuHandle = Get-MenuWindow $expected[0] $process.Id
} while ($menuHandle -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
if ($menuHandle -ne [IntPtr]::Zero) {
    $menuRect = [BongoCatNeoMenuNative+Rect]::new()
    [void][BongoCatNeoMenuNative]::GetWindowRect($menuHandle, [ref]$menuRect)
    $bitmap = [Drawing.Bitmap]::new($menuRect.R - $menuRect.L, $menuRect.B - $menuRect.T)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($menuRect.L, $menuRect.T, 0, 0, $bitmap.Size)
    $bitmap.Save((Join-Path $OutputDir "context-menu.png"), [Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    $nativeMenu = [BongoCatNeoMenuNative]::SendMessageW($menuHandle, 0x01E1,
        [IntPtr]::Zero, [IntPtr]::Zero)
    $labels = Get-MenuLabels $nativeMenu
    # Top-level positions include separators: size=5 and opacity=6.
    $sizeMenu = [BongoCatNeoMenuNative]::GetSubMenu($nativeMenu, 5)
    $opacityMenu = [BongoCatNeoMenuNative]::GetSubMenu($nativeMenu, 6)
    $sizeLabels = Get-MenuLabels $sizeMenu
    $opacityLabels = Get-MenuLabels $opacityMenu
    if (-not $BehaviorDisabled) {
        $motionMenu = [BongoCatNeoMenuNative]::GetSubMenu($nativeMenu, 7)
        $expressionMenu = [BongoCatNeoMenuNative]::GetSubMenu($nativeMenu, 8)
        $motionLabels = Get-MenuLabels $motionMenu
        $expressionLabels = Get-MenuLabels $expressionMenu
    }
    $owner = [BongoCatNeoMenuNative]::GetWindow($menuHandle, 4)
    $previewOwner = @(Get-Windows $process.Id | Where-Object Class -ne "#32768" |
        Sort-Object Area -Descending | Select-Object -First 1).Handle
    if ($PreviewBehaviors -and -not $BehaviorDisabled) {
        $frame = Join-Path $data "frame.bmp"
        $baseline = Join-Path $OutputDir "preview-baseline.bmp"
        $baselineHighlighted = Send-MenuHighlight $previewOwner $sizeMenu 5
        $baselineRendered = $baselineHighlighted -and
            (Wait-FrameChange $frame ([datetime]::MinValue))
        if ($baselineRendered) { Copy-MenuFrame $frame $baseline }
        $before = if ($baselineRendered) {
            (Get-Item -LiteralPath $frame).LastWriteTimeUtc
        } else { [datetime]::MinValue }
        $menuAnimationRendered = $baselineRendered -and
            (Wait-FrameChange $frame $before)
        $menuAnimationFrame = Join-Path $OutputDir "preview-menu-animation.bmp"
        if ($menuAnimationRendered) { Copy-MenuFrame $frame $menuAnimationFrame }
        $menuAnimationContinued = $menuAnimationRendered -and
            (Get-FileHash $baseline).Hash -ne (Get-FileHash $menuAnimationFrame).Hash
        $before = if ($baselineRendered) {
            (Get-Item -LiteralPath $frame).LastWriteTimeUtc
        } else { [datetime]::MinValue }
        $motionHighlighted = Send-MenuHighlight $previewOwner $motionMenu 0
        $motionRendered = $motionHighlighted -and (Wait-FrameChange $frame $before)
        $motionFrame = Join-Path $OutputDir "preview-motion.bmp"
        if ($motionRendered) { Copy-MenuFrame $frame $motionFrame }
        $before = if ($motionRendered) {
            (Get-Item -LiteralPath $frame).LastWriteTimeUtc
        } else { [datetime]::MinValue }
        $motionAdvanced = $motionRendered -and (Wait-FrameChange $frame $before)
        $motionAdvancedFrame = Join-Path $OutputDir "preview-motion-advanced.bmp"
        if ($motionAdvanced) { Copy-MenuFrame $frame $motionAdvancedFrame }
        $motionPreviewAnimated = $motionAdvanced -and
            (Get-FileHash $motionFrame).Hash -ne (Get-FileHash $motionAdvancedFrame).Hash
        $before = if (Test-Path -LiteralPath $frame) {
            (Get-Item -LiteralPath $frame).LastWriteTimeUtc
        } else { [datetime]::MinValue }
        $expressionPosition = if ($expressionLabels.Count -gt 1) { 1 } else { 0 }
        $expressionHighlighted = Send-MenuHighlight $previewOwner $expressionMenu $expressionPosition
        $expressionRendered = $expressionHighlighted -and (Wait-FrameChange $frame $before)
        $expressionFrame = Join-Path $OutputDir "preview-expression.bmp"
        if ($expressionRendered) { Copy-MenuFrame $frame $expressionFrame }
        $motionPreviewChanged = $menuAnimationRendered -and $motionRendered -and
            (Get-FileHash $menuAnimationFrame).Hash -ne (Get-FileHash $motionFrame).Hash
        $expressionPreviewChanged = $expressionRendered -and
            (Get-FileHash $motionAdvancedFrame).Hash -ne
                (Get-FileHash $expressionFrame).Hash
    }
    $itemRect = [BongoCatNeoMenuNative+Rect]::new()
    $itemKnown = [BongoCatNeoMenuNative]::GetMenuItemRect(
        $owner, $nativeMenu, 0, [ref]$itemRect)
    if (-not $itemKnown -or $itemRect.L -lt $menuRect.L -or
        $itemRect.R -gt $menuRect.R -or $itemRect.T -lt $menuRect.T -or
        $itemRect.B -gt $menuRect.B) {
        $itemRect = [BongoCatNeoMenuNative+Rect]::new()
        $itemRect.L = $menuRect.L; $itemRect.R = $menuRect.R
        $itemRect.T = $menuRect.T + 8; $itemRect.B = $menuRect.T + 30
    }
    [void][BongoCatNeoMenuNative]::SetCursorPos(
        [int](($itemRect.L + $itemRect.R) / 2),
        [int](($itemRect.T + $itemRect.B) / 2))
    [BongoCatNeoMenuNative]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
    [BongoCatNeoMenuNative]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
}
$windowDeadline = [DateTime]::UtcNow.AddMilliseconds(2500)
do {
    Start-Sleep -Milliseconds 100
    $visible = @(Get-Windows $process.Id | Where-Object Class -ne "#32768")
} while ($visible.Count -lt 2 -and [DateTime]::UtcNow -lt $windowDeadline)
$preferencesOpened = $visible.Count -ge 2
$exited = $process.WaitForExit(30000)
if (-not $exited) { Stop-Process -Id $process.Id -Force }
$exitCode = if ($exited) { $process.ExitCode } else { -1 }
$labelsMatch = ($labels -join "|") -eq ($expected -join "|")
$sizeLabelsMatch = ($sizeLabels -join "|") -eq ($expectedSize -join "|")
$opacityLabelsMatch = ($opacityLabels -join "|") -eq ($expectedOpacity -join "|")
$motionLabelsMatch = $BehaviorDisabled -or $(if ($AllowDynamicBehaviorLabels) {
    $motionLabels.Count -gt 0
} else { ($motionLabels -join "|") -eq ($expectedMotions -join "|") })
$expressionLabelsMatch = $BehaviorDisabled -or $(if ($AllowDynamicBehaviorLabels) {
    $expressionLabels.Count -gt 0
} else { ($expressionLabels -join "|") -eq ($expectedExpressions -join "|") })
$behaviorPreviewMatch = -not $PreviewBehaviors -or $BehaviorDisabled -or
    ($menuAnimationContinued -and $motionPreviewChanged -and
        $motionPreviewAnimated -and $expressionPreviewChanged)
$preferencesMatch = $PreviewBehaviors -or $preferencesOpened
$passed = $menuHandle -ne [IntPtr]::Zero -and $preferencesMatch -and $exitCode -eq 0 -and
    $labelsMatch -and $sizeLabelsMatch -and $opacityLabelsMatch -and
    $motionLabelsMatch -and $expressionLabelsMatch -and $behaviorPreviewMatch
$result = [pscustomobject]@{MenuFound=($menuHandle-ne[IntPtr]::Zero); Labels=$labels
    LabelsMatch=$labelsMatch; SizeLabels=$sizeLabels; SizeLabelsMatch=$sizeLabelsMatch
    OpacityLabels=$opacityLabels; OpacityLabelsMatch=$opacityLabelsMatch
    MotionLabels=$motionLabels; MotionLabelsMatch=$motionLabelsMatch
    ExpressionLabels=$expressionLabels; ExpressionLabelsMatch=$expressionLabelsMatch
    BaselineHighlighted=[bool]$baselineHighlighted
    MotionHighlighted=[bool]$motionHighlighted
    ExpressionHighlighted=[bool]$expressionHighlighted
    MotionPreviewChanged=[bool]$motionPreviewChanged
    MotionPreviewAnimated=[bool]$motionPreviewAnimated
    ExpressionPreviewChanged=[bool]$expressionPreviewChanged
    MenuAnimationContinued=[bool]$menuAnimationContinued
    BehaviorPreviewMatch=$behaviorPreviewMatch
    AllowDynamicBehaviorLabels=[bool]$AllowDynamicBehaviorLabels
    BehaviorDisabled=[bool]$BehaviorDisabled
    PreferencesOpened=$preferencesOpened; VisibleWindows=$visible.Count
    ExitCode=$exitCode; Passed=$passed}
$result | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $OutputDir "result.json")
$result | Format-List
if (-not $passed) { exit 1 }
