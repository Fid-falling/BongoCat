function Wait-WindowVisibility([IntPtr]$Window, [bool]$Visible,
    [int]$TimeoutMilliseconds = 3000) {
    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        if ([WindowProbe]::IsWindowVisible($Window) -eq $Visible) { return }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Window visibility did not become $Visible"
}

function Get-FrameRows([string]$DataRoot) {
    $path = Join-Path $DataRoot "frame-series.csv"
    if (-not (Test-Path -LiteralPath $path)) { return @() }
    return @(Import-Csv -LiteralPath $path)
}

function Assert-NewVisibleModelFrame([string]$DataRoot, [int]$PreviousCount) {
    $deadline = [DateTime]::UtcNow.AddSeconds(4)
    do {
        $rows = @(Get-FrameRows $DataRoot)
        if ($rows.Count -gt $PreviousCount) {
            $newRows = @($rows | Select-Object -Skip $PreviousCount)
            $valid = @($newRows | Where-Object {
                [int]$_.alpha_pixels -gt 0 -and
                [int]$_.window_config_visible -eq 1 -and
                [int]$_.window_os_visible -eq 1 -and
                [int]$_.model_state_consistent -eq 1
            })
            if ($valid.Count) { return }
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Window reveal did not produce a new non-transparent model frame"
}
