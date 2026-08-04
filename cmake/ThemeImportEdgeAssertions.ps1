$edgeMatches = $reports -eq $installed
if ($fixture.NoGamepadShortcuts) {
    foreach ($model in $layouts) {
        $metadata = Get-Content (Join-Path $model.Adapter ".bongo-cat-mver.json") `
            -Raw | ConvertFrom-Json
        $edgeMatches = $edgeMatches -and
            @($metadata.bindings | Where-Object shortcut -like "Gamepad:*").Count -eq 0
    }
}
if ($fixture.PatchHash) {
    $standard = $layouts | Where-Object Mode -eq "standard" | Select-Object -First 1
    $edgeMatches = $edgeMatches -and $null -ne $standard -and
        (Get-FileHash (Join-Path $standard.Adapter "resources\left-keys\KeyA.png") `
            -Algorithm SHA256).Hash -eq $fixture.PatchHash
}
if ($fixture.MomentaryMedia) {
    $mediaCount = 0
    foreach ($model in $layouts) {
        $metadata = Get-Content (Join-Path $model.Adapter ".bongo-cat-mver.json") `
            -Raw | ConvertFrom-Json
        $media = @($metadata.bindings | Where-Object kind -in "sound","effect")
        $mediaCount += $media.Count
        $edgeMatches = $edgeMatches -and
            @($media | Where-Object { -not $_.momentary }).Count -eq 0 -and
            @($metadata.bindings | Where-Object kind -like "*-clear").Count -eq 0
    }
    $edgeMatches = $edgeMatches -and $mediaCount -ge 5
}
if ($fixture.SparseAssets) {
    $keyboard = $layouts | Where-Object Mode -eq "keyboard" | Select-Object -First 1
    $report = Get-Content (Join-Path $keyboard.Adapter `
        ".bongo-cat-import-report.json") -Raw | ConvertFrom-Json
    $edgeMatches = $edgeMatches -and $report.assets.lefthand.declared -eq 2 -and
        $report.assets.lefthand.available -eq 1 -and
        $report.assets.lefthand.missing -eq 1
}
if ($fixture.MotionOverflow) {
    $standard = $layouts | Where-Object Mode -eq "standard" | Select-Object -First 1
    $metadata = Get-Content (Join-Path $standard.Adapter ".bongo-cat-mver.json") `
        -Raw | ConvertFrom-Json
    $edgeMatches = $edgeMatches -and @($metadata.bindings | Where-Object {
        $_.kind -eq "motion" -and $_.group -eq "CAT_motion_lock"
    }).Count -eq 0
}
if ($fixture.Cleanup) {
    $custom = Join-Path $data "custom-models"
    $edgeMatches = $edgeMatches -and
        -not (Test-Path (Join-Path $custom ".import-deadbeef-1.tmp")) -and
        (Test-Path (Join-Path $custom ".import-not-owned.tmp"))
}
