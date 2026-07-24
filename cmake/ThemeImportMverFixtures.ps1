$mver = Join-Path $OutputDir "mver-package"
$mverImage = Join-Path $mver "img"
New-Item -ItemType Directory -Force -Path $mverImage | Out-Null
@{
    decoration = @{ emoticonKeep=$true; emoticonClear=@(17,67)
        soundKeep=$true; soundClear=@(17,68); window_size=@(1400,1400)
        offsetX=@(0,11); offsetY=@(0,0); scalar=@(1.0,1.0) }
    standard = @{ hand = @(@(65, 66), (,16), (,16)); face=@()
        l2d_expression = @(,@(18, 49)); l2d_motion = @(,@(18, 50))
        l2d_motion_lockhand = @(,@(18, 51)); sounds = @(,@(18, 52)) }
    keyboard = @{ lefthand = @(@(65, 66), (,16)); righthand = @((,37), (,16))
        keyboard = @((,66), (,37)); l2d_expression = @(,@(17, 65))
        face=@(@(17,65),@(17,66)); sounds=@(); l2d_motion=@(); l2d_motion_lockhand=@() }
    gamepad = @{ input_mode=1; lefthand = ,@(10, 11); righthand = ,@(0)
        keyboard = @((,0), (,11)); l2d_expression = @(,@(0)); l2d_motion = @(,@(1))
        l2d_motion_lockhand=@(); face=@(@(17,65),@(0)); sounds=@() }
} | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $mver "config.json")
foreach ($mode in @("standard", "keyboard", "gamepad")) {
    $modeSource = Join-Path $root "resources\assets\models\$mode"
    $modeRoot = Join-Path $mverImage $mode
    $modelRoot = Join-Path $modeRoot "cat_model"
    New-Item -ItemType Directory -Force -Path $modeRoot | Out-Null
    Copy-Item -LiteralPath $modeSource -Destination $modelRoot -Recurse
    Remove-Item -LiteralPath (Join-Path $modelRoot "resources") -Recurse -Force
    Copy-Item (Join-Path $modeSource "resources\cover.png") (Join-Path $modeRoot "cat.png")
    $backgroundName = if ($mode -eq "standard") { "mousebg.png" } else { "bg.png" }
    Copy-Item (Join-Path $modeSource "resources\background.png") `
        (Join-Path $modeRoot $backgroundName)
    if ($mode -eq "standard") {
        New-Item -ItemType Directory -Force -Path (Join-Path $modeRoot "hand") | Out-Null
        Copy-Item (Join-Path $modeSource "resources\left-keys\KeyA.png") `
            (Join-Path $modeRoot "hand\0.png")
        New-PixelPng (Join-Path $modeRoot "hand\1.png") 255 0 0 255
        New-PixelPng (Join-Path $modeRoot "hand\2.png") 0 255 0 255
        New-Item -ItemType Directory -Force -Path (Join-Path $modeRoot "sounds") | Out-Null
        Copy-Item (Join-Path $modeSource "live2d_motion1.flac") `
            (Join-Path $modeRoot "sounds\0.flac")
        continue
    }
    foreach ($directory in @("keyboard", "lefthand", "righthand", "face")) {
        New-Item -ItemType Directory -Force -Path (Join-Path $modeRoot $directory) | Out-Null
    }
    $leftName = if ($mode -eq "gamepad") { "DPadLeft.png" } else { "KeyA.png" }
    $rightName = if ($mode -eq "gamepad") { "South.png" } else { "LeftArrow.png" }
    Copy-Item (Join-Path $modeSource "resources\left-keys\$leftName") `
        (Join-Path $modeRoot "lefthand\0.png")
    Copy-Item (Join-Path $modeSource "resources\right-keys\$rightName") `
        (Join-Path $modeRoot "righthand\0.png")
    $leftKeyboard = if ($mode -eq "gamepad") {
        "resources\right-keys\North.png"
    } else { "resources\left-keys\KeyB.png" }
    $rightKeyboard = if ($mode -eq "gamepad") {
        "resources\left-keys\DPadUp.png"
    } else { "resources\right-keys\UpArrow.png" }
    Copy-Item (Join-Path $modeSource $leftKeyboard) (Join-Path $modeRoot "keyboard\0.png")
    Copy-Item (Join-Path $modeSource $rightKeyboard) (Join-Path $modeRoot "keyboard\1.png")
    New-PixelPng (Join-Path $modeRoot "face\0.png") 255 128 0 255
    New-PixelPng (Join-Path $modeRoot "face\1.png") 0 128 255 255
    if ($mode -eq "keyboard") {
        New-PixelPng (Join-Path $modeRoot "lefthand\0.png") 255 0 0 128 1 2
        New-PixelPng (Join-Path $modeRoot "keyboard\0.png") 0 0 255 128 2 1
        New-PixelPng (Join-Path $modeRoot "lefthand\1.png") 255 64 0 255
        New-PixelPng (Join-Path $modeRoot "righthand\1.png") 0 255 64 255
    }
}
$hash = { param($Path) (Get-FileHash $Path -Algorithm SHA256).Hash }
$mverBackgroundHashes = @{
    standard = &$hash (Join-Path $mverImage "standard\mousebg.png")
    keyboard = &$hash (Join-Path $mverImage "keyboard\bg.png")
    gamepad = &$hash (Join-Path $mverImage "gamepad\bg.png") }
$mverCoverHashes = @{
    standard = &$hash (Join-Path $mverImage "standard\cat.png")
    keyboard = &$hash (Join-Path $mverImage "keyboard\cat.png")
    gamepad = &$hash (Join-Path $mverImage "gamepad\cat.png") }
$fixtures += [pscustomobject]@{ Name="mver-package"; Path=$mver; Valid=$true; Count=3
    Mver=$true; StandardKeyHash=(&$hash (Join-Path $mverImage "standard\hand\0.png"))
    ShiftLeftHash=(&$hash (Join-Path $mverImage "standard\hand\1.png"))
    ShiftRightHash=(&$hash (Join-Path $mverImage "standard\hand\2.png"))
    KeyboardShiftLeftHash=(&$hash (Join-Path $mverImage "keyboard\lefthand\1.png"))
    KeyboardShiftRightHash=(&$hash (Join-Path $mverImage "keyboard\righthand\1.png"))
    KeyboardDirectHash=(&$hash (Join-Path $mverImage "keyboard\lefthand\0.png"))
    GamepadDirectHash=(&$hash (Join-Path $mverImage "gamepad\lefthand\0.png"))
    BackgroundHashes=$mverBackgroundHashes; CoverHashes=$mverCoverHashes }
$fixtures += [pscustomobject]@{ Name="mver-subdirectory"
    Path=(Join-Path $mver "img\standard\cat_model"); Valid=$true; Count=3 }

$mverMalformed = Join-Path $OutputDir "mver-malformed-mode"
Copy-Item -LiteralPath $mver -Destination $mverMalformed -Recurse
@{ standard = @{ hand = "broken" } } | ConvertTo-Json -Depth 4 | Set-Content `
    (Join-Path $mverMalformed "config.json")
$fixtures += [pscustomobject]@{ Name="mver-malformed-mode"; Path=$mverMalformed
    Valid=$false; Count=0 }
$mverInvalid = Join-Path $OutputDir "mver-invalid-json"
Copy-Item -LiteralPath $mver -Destination $mverInvalid -Recurse
Set-Content -LiteralPath (Join-Path $mverInvalid "config.json") -Value "{" -NoNewline
$fixtures += [pscustomobject]@{ Name="mver-invalid-json"; Path=$mverInvalid
    Valid=$false; Count=0 }
foreach ($case in @(@("mver-corrupt-input","standard\hand\0.png"),
    @("mver-corrupt-second-model","keyboard\lefthand\0.png"))) {
    $path = Join-Path $OutputDir $case[0]
    Copy-Item -LiteralPath $mver -Destination $path -Recurse
    [IO.File]::WriteAllBytes((Join-Path $path "img\$($case[1])"),
        [byte[]](0x89, 0x50, 0x4e, 0x47))
    $fixtures += [pscustomobject]@{ Name=$case[0]; Path=$path; Valid=$false; Count=0 }
}
