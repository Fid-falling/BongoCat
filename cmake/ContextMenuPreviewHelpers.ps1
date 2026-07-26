function Send-MenuHighlight([IntPtr]$Owner, [IntPtr]$Menu, [int]$Position) {
    [uint32]$id = [BongoCatNeoMenuNative]::GetMenuItemID($Menu, $Position)
    if ($id -eq [uint32]::MaxValue) { return $false }
    [void][BongoCatNeoMenuNative]::SendMessageW(
        $Owner, 0x011F, [IntPtr]::new([int64]$id), $Menu)
    return $true
}

function Wait-FrameChange([string]$Frame, [datetime]$PreviousWrite) {
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        Start-Sleep -Milliseconds 50
        if ((Test-Path -LiteralPath $Frame) -and
            (Get-Item -LiteralPath $Frame).LastWriteTimeUtc -gt $PreviousWrite) {
            return $true
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    return $false
}

function Copy-MenuFrame([string]$Source, [string]$Destination) {
    for ($attempt = 0; $attempt -lt 40; $attempt++) {
        try {
            Copy-Item -LiteralPath $Source -Destination $Destination -Force -ErrorAction Stop
            return
        } catch { Start-Sleep -Milliseconds 10 }
    }
    throw "Timed out copying menu preview frame: $Source"
}
