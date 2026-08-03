param([string]$Exe = "", [string]$OutputDir = "")

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build-final\Release\BongoCat.exe" }
if (-not $OutputDir) {
    $OutputDir = Join-Path $root "build-final\expression-menu-audit"
}
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force $OutputDir | Out-Null

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BongoCatExpressionMenuNative {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(
        IntPtr parent, IntPtr childAfter, string className, string title);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(
        IntPtr handle, out uint process);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(
        IntPtr handle, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32.dll")] public static extern IntPtr GetSubMenu(IntPtr menu, int position);
    [DllImport("user32.dll")] public static extern uint GetMenuItemID(IntPtr menu, int position);
    [DllImport("user32.dll")] public static extern uint GetMenuState(
        IntPtr menu, uint item, uint flags);
}
'@

function Wait-RootMenu([int]$ProcessId) {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $after = [IntPtr]::Zero
        while ($true) {
            $handle = [BongoCatExpressionMenuNative]::FindWindowExW(
                [IntPtr]::Zero, $after, "#32768", $null)
            if ($handle -eq [IntPtr]::Zero) { break }
            $after = $handle
            [uint32]$owner = 0
            [void][BongoCatExpressionMenuNative]::GetWindowThreadProcessId(
                $handle, [ref]$owner)
            if ($owner -ne $ProcessId -or
                -not [BongoCatExpressionMenuNative]::IsWindowVisible($handle)) { continue }
            $menu = [BongoCatExpressionMenuNative]::SendMessageW(
                $handle, 0x01E1, [IntPtr]::Zero, [IntPtr]::Zero)
            if ($menu -ne [IntPtr]::Zero -and
                [BongoCatExpressionMenuNative]::GetMenuItemCount($menu) -ge 9) {
                return $menu
            }
        }
        Start-Sleep -Milliseconds 25
    } while ([DateTime]::UtcNow -lt $deadline)
    return [IntPtr]::Zero
}

$data = Join-Path $OutputDir ("data-" + [DateTime]::UtcNow.Ticks)
New-Item -ItemType Directory -Force $data | Out-Null
$env:BONGO_CAT_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_TEST_INSTANCE_ID = "expression-menu-$PID"
$process = Start-Process $Exe -WorkingDirectory (Split-Path $Exe) -PassThru `
    -ArgumentList @("--ci-smoke", "--ci-model=standard", "--ci-context-menu",
        "--ci-live2d-scenario=expression-2", "--ci-exit-ms=15000", "--data-root=$data")
$rows = @()
$menuFound = $false
$auditPassed = $false
try {
    $rootMenu = Wait-RootMenu $process.Id
    $menuFound = $rootMenu -ne [IntPtr]::Zero
    $expressions = if ($menuFound) {
        [BongoCatExpressionMenuNative]::GetSubMenu($rootMenu, 8)
    } else { [IntPtr]::Zero }
    if ($expressions -ne [IntPtr]::Zero) {
        $count = [BongoCatExpressionMenuNative]::GetMenuItemCount($expressions)
        for ($index = 0; $index -lt $count; ++$index) {
            $state = [BongoCatExpressionMenuNative]::GetMenuState(
                $expressions, $index, 0x400)
            $rows += [pscustomobject]@{
                Position = $index
                Command = [BongoCatExpressionMenuNative]::GetMenuItemID(
                    $expressions, $index)
                Checked = ($state -band 8) -ne 0
            }
        }
    }
    $auditPath = Join-Path $data "live2d-audit.txt"
    $auditPassed = (Test-Path $auditPath) -and
        ((Get-Content -Raw $auditPath) -match "assertions=passed")
} finally {
    if ($process -and -not $process.HasExited) {
        $process.Kill()
        $process.WaitForExit()
    }
    Remove-Item Env:BONGO_CAT_ALLOW_TEST_INSTANCES -ErrorAction SilentlyContinue
    Remove-Item Env:BONGO_CAT_TEST_INSTANCE_ID -ErrorAction SilentlyContinue
}

$commandsMatch = ($rows.Command -join ",") -eq "3000,3001,3002"
$checkedMatch = $rows.Count -eq 3 -and -not $rows[0].Checked -and
    -not $rows[1].Checked -and $rows[2].Checked
$result = [pscustomobject]@{
    MenuFound = $menuFound
    ExpressionItems = $rows
    CommandsMatch = $commandsMatch
    CheckedExpression2 = $checkedMatch
    Live2DAuditPassed = $auditPassed
    Passed = $menuFound -and $commandsMatch -and $checkedMatch -and $auditPassed
}
$result | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 `
    (Join-Path $OutputDir "result.json")
$result | ConvertTo-Json -Depth 4
if (-not $result.Passed) { exit 1 }
