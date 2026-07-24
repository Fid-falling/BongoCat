$multiple = New-Fixture "multiple-manifests"
Copy-Item (Join-Path $multiple "cat.model3.json") `
    (Join-Path $multiple "other.model3.json")
$fixtures += [pscustomobject]@{ Name="multiple-manifests"; Path=$multiple
    Valid=$false; Count=0 }

$missingSound = New-Fixture "optional-missing-motion-sound"
$manifestPath = Join-Path $missingSound "cat.model3.json"
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$motionGroup = $manifest.FileReferences.Motions.psobject.Properties.Value |
    Where-Object { @($_).Count } | Select-Object -First 1
if ($motionGroup) {
    $motionGroup[0] | Add-Member -Force -NotePropertyName Sound `
        -NotePropertyValue "missing-but-optional.flac"
}
$manifest | ConvertTo-Json -Depth 20 | Set-Content $manifestPath
$fixtures += [pscustomobject]@{ Name="optional-missing-motion-sound"
    Path=$missingSound; Valid=$true; Count=1; Cleanup=$true }

$inputModeZero = Join-Path $OutputDir "mver-input-mode-zero"
Copy-Item -LiteralPath $mver -Destination $inputModeZero -Recurse
$configPath = Join-Path $inputModeZero "config.json"
$config = Get-Content $configPath -Raw | ConvertFrom-Json
$config.gamepad.input_mode = 0
$config.gamepad.lefthand = ,@(8)
$config.gamepad.righthand = ,@(9)
$config.gamepad.keyboard = @()
$config.gamepad.face = ,@(17,65)
$config.gamepad.l2d_expression = ,@(8)
$config.gamepad.l2d_motion = ,@(9)
$config | ConvertTo-Json -Depth 20 | Set-Content $configPath
$fixtures += [pscustomobject]@{ Name="mver-input-mode-zero"; Path=$inputModeZero
    Valid=$true; Count=3; NoGamepadShortcuts=$true }

$wrongGroup = Join-Path $OutputDir "mver-wrong-motion-group"
Copy-Item -LiteralPath $mver -Destination $wrongGroup -Recurse
$manifestPath = Get-ChildItem (Join-Path $wrongGroup "img\standard\cat_model") `
    -Filter "*.model3.json" | Select-Object -First 1 -ExpandProperty FullName
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$motions = $manifest.FileReferences.Motions
$lock = $motions.CAT_motion_lock
$motions.psobject.Properties.Remove("CAT_motion_lock")
$motions | Add-Member -NotePropertyName "Unrelated" -NotePropertyValue $lock
$manifest | ConvertTo-Json -Depth 20 | Set-Content $manifestPath
$fixtures += [pscustomobject]@{ Name="mver-wrong-motion-group"; Path=$wrongGroup
    Valid=$false; Count=0 }

$overflow = Join-Path $OutputDir "mver-motion-overflow"
Copy-Item -LiteralPath $mver -Destination $overflow -Recurse
$manifestPath = Get-ChildItem (Join-Path $overflow "img\standard\cat_model") `
    -Filter "*.model3.json" | Select-Object -First 1 -ExpandProperty FullName
$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$manifest.FileReferences.Motions.CAT_motion_lock = @()
$manifest | ConvertTo-Json -Depth 20 | Set-Content $manifestPath
$fixtures += [pscustomobject]@{ Name="mver-motion-overflow"; Path=$overflow
    Valid=$false; Count=0 }

$corruptEffect = Join-Path $OutputDir "mver-corrupt-effect"
Copy-Item -LiteralPath $mver -Destination $corruptEffect -Recurse
[IO.File]::WriteAllBytes((Join-Path $corruptEffect "img\keyboard\face\0.png"),
    [byte[]](0x89,0x50,0x4e,0x47))
$fixtures += [pscustomobject]@{ Name="mver-corrupt-effect"; Path=$corruptEffect
    Valid=$false; Count=0 }

$momentary = Join-Path $OutputDir "mver-momentary-media"
Copy-Item -LiteralPath $mver -Destination $momentary -Recurse
$configPath = Join-Path $momentary "config.json"
$config = Get-Content $configPath -Raw | ConvertFrom-Json
$config.decoration.emoticonKeep = $false
$config.decoration.soundKeep = $false
$config | ConvertTo-Json -Depth 20 | Set-Content $configPath
$fixtures += [pscustomobject]@{ Name="mver-momentary-media"; Path=$momentary
    Valid=$true; Count=3; MomentaryMedia=$true }

$sparse = Join-Path $OutputDir "mver-sparse-assets"
Copy-Item -LiteralPath $mver -Destination $sparse -Recurse
Remove-Item (Join-Path $sparse "img\keyboard\lefthand\1.png")
$fixtures += [pscustomobject]@{ Name="mver-sparse-assets"; Path=$sparse
    Valid=$true; Count=3; SparseAssets=$true }

$badChord = Join-Path $OutputDir "mver-invalid-chord"
Copy-Item -LiteralPath $mver -Destination $badChord -Recurse
$configPath = Join-Path $badChord "config.json"
$config = Get-Content $configPath -Raw | ConvertFrom-Json
$config.keyboard.face = ,@(17,65,66)
$config | ConvertTo-Json -Depth 20 | Set-Content $configPath
$fixtures += [pscustomobject]@{ Name="mver-invalid-chord"; Path=$badChord
    Valid=$false; Count=0 }

function New-PatchTree([string]$Root, [switch]$SecondBase) {
    New-Item -ItemType Directory -Force -Path $Root | Out-Null
    Copy-Item -LiteralPath $mver -Destination (Join-Path $Root "BaseModel") -Recurse
    $container = if ($SecondBase) { Join-Path $Root "inner" } else { $Root }
    if ($SecondBase) {
        New-Item -ItemType Directory -Force -Path $container | Out-Null
        Copy-Item -LiteralPath $mver -Destination (Join-Path $container "BaseModel") -Recurse
    }
    $hand = Join-Path $container "Variant\BaseModel\img\standard\hand"
    New-Item -ItemType Directory -Force -Path $hand | Out-Null
    New-PixelPng (Join-Path $hand "0.png") 7 8 9 255
    return Join-Path $container "Variant"
}

$patchRoot = Join-Path $OutputDir "mver-patch"
$patch = New-PatchTree $patchRoot
$patchHash = (Get-FileHash (Join-Path $patch `
    "BaseModel\img\standard\hand\0.png") -Algorithm SHA256).Hash
$fixtures += [pscustomobject]@{ Name="mver-patch"; Path=$patch
    Valid=$true; Count=3; PatchHash=$patchHash }

$noBaseRoot = Join-Path $OutputDir "mver-patch-no-base"
$noBaseHand = Join-Path $noBaseRoot "Variant\BaseModel\img\standard\hand"
New-Item -ItemType Directory -Force -Path $noBaseHand | Out-Null
New-PixelPng (Join-Path $noBaseHand "0.png") 1 2 3 255
$fixtures += [pscustomobject]@{ Name="mver-patch-no-base"
    Path=(Join-Path $noBaseRoot "Variant"); Valid=$false; Count=0 }

$ambiguousRoot = Join-Path $OutputDir "mver-patch-ambiguous"
$ambiguous = New-PatchTree $ambiguousRoot -SecondBase
$fixtures += [pscustomobject]@{ Name="mver-patch-ambiguous"; Path=$ambiguous
    Valid=$false; Count=0 }
