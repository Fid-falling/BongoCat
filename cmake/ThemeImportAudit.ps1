param([string]$Exe = "", [string]$OutputDir = "")
$ErrorActionPreference = "Stop"
$env:BONGO_CAT_NEO_ALLOW_TEST_INSTANCES = "1"
$root = Split-Path $PSScriptRoot -Parent
if (-not $Exe) { $Exe = Join-Path $root "build\BongoCatNeo.exe" }
if (-not $OutputDir) { $OutputDir = Join-Path $root "build\theme-import-audit" }
$Exe = [IO.Path]::GetFullPath($Exe)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$source = Join-Path $root "resources\assets\models\standard"
. (Join-Path $PSScriptRoot "ThemeImportLayout.ps1")
Add-Type -AssemblyName System.Drawing
Remove-Item -LiteralPath $OutputDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

function New-Fixture([string]$Name) {
    $path = Join-Path $OutputDir $Name
    Copy-Item -LiteralPath $source -Destination $path -Recurse
    return $path
}

function New-PixelPng([string]$Path, [int]$Red, [int]$Green, [int]$Blue,
    [int]$Alpha, [int]$Width = 1, [int]$Height = 1) {
    $bitmap = [Drawing.Bitmap]::new($Width, $Height,
        [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt $Height; $y++) { for ($x = 0; $x -lt $Width; $x++) {
        $bitmap.SetPixel($x, $y, [Drawing.Color]::FromArgb($Alpha, $Red, $Green, $Blue))
    } }
    $bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}

$fixtures = @()
$fixtures += [pscustomobject]@{ Name="valid"; Path=(New-Fixture "valid"); Valid=$true; Count=1 }
$malformed = New-Fixture "malformed-json"
Set-Content -LiteralPath (Join-Path $malformed "cat.model3.json") -Value "{" -NoNewline
$fixtures += [pscustomobject]@{ Name="malformed-json"; Path=$malformed; Valid=$false; Count=0 }
$missingCover = New-Fixture "missing-cover"
Remove-Item -LiteralPath (Join-Path $missingCover "resources\cover.png")
$fixtures += [pscustomobject]@{ Name="missing-cover"; Path=$missingCover; Valid=$true; Count=1 }
$missingBackground = New-Fixture "missing-background"
Remove-Item -LiteralPath (Join-Path $missingBackground "resources\background.png")
$fixtures += [pscustomobject]@{ Name="missing-background"; Path=$missingBackground; Valid=$true; Count=1 }
$nativeConfig = New-Fixture "native-unrelated-config"
New-Item -ItemType Directory -Path (Join-Path $nativeConfig "hand") | Out-Null
Set-Content -LiteralPath (Join-Path $nativeConfig "config.json") -Value "{}" -NoNewline
$fixtures += [pscustomobject]@{ Name="native-unrelated-config"; Path=$nativeConfig
    Valid=$true; Count=1 }
$nativeInvalid = New-Fixture "native-invalid-config"
New-Item -ItemType Directory -Path (Join-Path $nativeInvalid "hand") | Out-Null
Set-Content -LiteralPath (Join-Path $nativeInvalid "config.json") -Value "{" -NoNewline
$fixtures += [pscustomobject]@{ Name="native-invalid-config"; Path=$nativeInvalid
    Valid=$true; Count=1 }
$nativeImage = New-Fixture "native-unrelated-image-tree"
New-Item -ItemType Directory -Force -Path (Join-Path $nativeImage "img\hand") | Out-Null
Set-Content -LiteralPath (Join-Path $nativeImage "config.json") -Value "{" -NoNewline
$fixtures += [pscustomobject]@{ Name="native-unrelated-image-tree"; Path=$nativeImage
    Valid=$true; Count=1 }
$missingTexture = New-Fixture "missing-texture"
Remove-Item -LiteralPath (Join-Path $missingTexture "demomodel.1024\texture_00.png")
$fixtures += [pscustomobject]@{ Name="missing-texture"; Path=$missingTexture; Valid=$false; Count=0 }
$corruptTexture = New-Fixture "corrupt-texture"
[IO.File]::WriteAllBytes((Join-Path $corruptTexture "demomodel.1024\texture_00.png"),
    [byte[]](0x89, 0x50, 0x4e, 0x47))
$fixtures += [pscustomobject]@{ Name="corrupt-texture"; Path=$corruptTexture
    Valid=$false; Count=0 }
$traversal = New-Fixture "path-traversal"
$manifest = Get-Content (Join-Path $traversal "cat.model3.json") -Raw |
    ConvertFrom-Json
$manifest.FileReferences.Moc = "../outside.moc3"
$manifest | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $traversal "cat.model3.json")
$fixtures += [pscustomobject]@{ Name="path-traversal"; Path=$traversal; Valid=$false; Count=0 }

$package = Join-Path $OutputDir "nested-package"
$imageRoot = Join-Path $package "img"
New-Item -ItemType Directory -Force -Path $imageRoot | Out-Null
Copy-Item (Join-Path $source "cat.model3.json") (Join-Path $imageRoot "cat.model3.json")
foreach ($mode in @("standard", "keyboard", "gamepad")) {
    $modeSource = Join-Path $root "resources\assets\models\$mode"
    $modeRoot = Join-Path $imageRoot $mode
    $modelRoot = Join-Path $modeRoot "cat_model"
    New-Item -ItemType Directory -Force -Path $modeRoot | Out-Null
    Copy-Item -LiteralPath $modeSource -Destination $modelRoot -Recurse
    Remove-Item -LiteralPath (Join-Path $modelRoot "resources") -Recurse -Force
    Copy-Item (Join-Path $modeSource "resources\cover.png") (Join-Path $modeRoot "cat.png")
    $backgroundName = if ($mode -eq "standard") { "mousebg.png" } else { "bg.png" }
    Copy-Item (Join-Path $modeSource "resources\background.png") `
        (Join-Path $modeRoot $backgroundName)
}
$fixtures += [pscustomobject]@{ Name="nested-package"; Path=$package; Valid=$true; Count=3 }

. (Join-Path $PSScriptRoot "ThemeImportMverFixtures.ps1")

$localPreview = Join-Path $OutputDir "local-preview-precedence"
$localModel = Join-Path $localPreview "model"
New-Item -ItemType Directory -Force -Path $localPreview | Out-Null
Copy-Item -LiteralPath $source -Destination $localModel -Recurse
$parentResources = Join-Path $localPreview "resources"
New-Item -ItemType Directory -Force -Path $parentResources | Out-Null
Copy-Item (Join-Path $root "resources\assets\models\keyboard\resources\cover.png") `
    (Join-Path $parentResources "cover.png")
$expectedCoverHash = (Get-FileHash (Join-Path $source "resources\cover.png") -Algorithm SHA256).Hash
$fixtures += [pscustomobject]@{ Name="local-preview-precedence"; Path=$localPreview
    Valid=$true; Count=1; CoverHash=$expectedCoverHash }

$missingLocalCover = Join-Path $OutputDir "missing-local-cover"
$missingLocalModel = Join-Path $missingLocalCover "model"
New-Item -ItemType Directory -Force -Path $missingLocalCover | Out-Null
Copy-Item -LiteralPath $source -Destination $missingLocalModel -Recurse
Remove-Item -LiteralPath (Join-Path $missingLocalModel "resources\cover.png")
$fallbackResources = Join-Path $missingLocalCover "resources"
New-Item -ItemType Directory -Force -Path $fallbackResources | Out-Null
$fallbackCover = Join-Path $root "resources\assets\models\keyboard\resources\cover.png"
Copy-Item $fallbackCover (Join-Path $fallbackResources "cover.png")
$fallbackCoverHash = (Get-FileHash $fallbackCover -Algorithm SHA256).Hash
$fixtures += [pscustomobject]@{ Name="missing-local-cover"; Path=$missingLocalCover
    Valid=$true; Count=1; CoverHash=$fallbackCoverHash }

$mixedPreview = Join-Path $OutputDir "mixed-preview-layout"
$mixedModel = Join-Path $mixedPreview "model"
New-Item -ItemType Directory -Force -Path $mixedPreview | Out-Null
Copy-Item -LiteralPath $source -Destination $mixedModel -Recurse
Remove-Item -LiteralPath (Join-Path $mixedModel "resources\cover.png")
$mixedResources = Join-Path $mixedPreview "resources"
New-Item -ItemType Directory -Force -Path $mixedResources | Out-Null
Set-Content -LiteralPath (Join-Path $mixedResources "metadata.txt") -Value "preview"
Copy-Item $fallbackCover (Join-Path $mixedPreview "cat.png")
$fixtures += [pscustomobject]@{ Name="mixed-preview-layout"; Path=$mixedPreview
    Valid=$true; Count=1; CoverHash=$fallbackCoverHash }
. (Join-Path $PSScriptRoot "ThemeImportEdgeFixtures.ps1")

$results = foreach ($fixture in $fixtures) {
    $data = Join-Path $OutputDir ("data-" + $fixture.Name)
    if ($fixture.Cleanup) {
        $custom = Join-Path $data "custom-models"
        New-Item -ItemType Directory -Force -Path `
            (Join-Path $custom ".import-deadbeef-1.tmp") | Out-Null
        New-Item -ItemType Directory -Force -Path `
            (Join-Path $custom ".import-not-owned.tmp") | Out-Null
    }
    Write-Host "CASE $($fixture.Name)"
    $process = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru `
        -WindowStyle Hidden -ArgumentList @("--ci-smoke", "--ci-exit-ms=1200",
            "--data-root=$data", "--ci-import=$($fixture.Path)")
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id -Force
        throw "Import case timed out: $($fixture.Name)"
    }
    $models = @(Get-ChildItem (Join-Path $data "custom-models") -Directory `
        -ErrorAction SilentlyContinue | Where-Object Name -NotLike ".*")
    $layouts = @($models | ForEach-Object { Get-InstalledModelLayout $_ })
    $installed = $models.Count
    $reports = @($layouts | Where-Object {
        Test-Path (Join-Path $_.Adapter ".bongo-cat-neo-import-report.json")
    }).Count
    $covers = @($layouts | Where-Object {
        Test-Path (Join-Path $_.Adapter "resources\cover.png")
    }).Count
    $actualCoverHash = if ($models.Count -eq 1 -and $covers -eq 1) {
        (Get-FileHash (Join-Path $layouts[0].Adapter "resources\cover.png") `
            -Algorithm SHA256).Hash
    } else { "" }
    $coverMatches = -not $fixture.CoverHash -or $actualCoverHash -eq $fixture.CoverHash
    $payloadMatches = $layouts.Count -eq $models.Count
    foreach ($layout in $layouts) {
        $sourcePayload = if ($fixture.PSObject.Properties["PayloadSource"]) {
            $fixture.PayloadSource
        } else { $fixture.Path }
        $targetPayload = if ($layout.Format -eq "bongo-cat-mver-patch") {
            Join-Path $layout.Payload "patch"
        } else { $layout.Payload }
        $payloadMatches = $payloadMatches -and $layout.Preserved -and
            (Test-TreeEqual $sourcePayload $targetPayload)
        if ($layout.Format -eq "bongo-cat-mver-patch" -and
            $fixture.PSObject.Properties["BaseSource"]) {
            $payloadMatches = $payloadMatches -and (Test-TreeEqual `
                $fixture.BaseSource (Join-Path $layout.Payload "base"))
        }
    }
    $mverKeys = 0
    $mverMatches = $true
    if ($fixture.Mver) {
        $expected = @{
            standard = "resources\left-keys\KeyA.png", "resources\left-keys\KeyB.png", `
                "resources\left-keys\ShiftLeft.png", "resources\left-keys\ShiftRight.png"
            keyboard = "resources\left-keys\KeyA.png", "resources\left-keys\KeyB.png", `
                "resources\left-keys\ShiftLeft.png", "resources\right-keys\LeftArrow.png", `
                "resources\right-keys\ShiftRight.png"
            gamepad = "resources\left-keys\DPadLeft.png", "resources\left-keys\DPadRight.png", "resources\right-keys\South.png"
        }
        $direct = @{
            standard = "resources\left-keys\KeyA.png", "resources\left-keys\KeyB.png"
            keyboard = "resources\left-keys\KeyA.png"
            gamepad = "resources\left-keys\DPadLeft.png"
        }
        foreach ($model in $layouts) {
            $mode = $model.Mode
            $backgroundPath = Join-Path $model.Adapter "resources\background.png"
            $coverPath = Join-Path $model.Adapter "resources\cover.png"
            $mverMatches = $mverMatches -and (Test-Path $backgroundPath) -and
                (Get-FileHash $backgroundPath -Algorithm SHA256).Hash -eq
                    $fixture.BackgroundHashes[$mode]
            $mverMatches = $mverMatches -and (Test-Path $coverPath) -and
                (Get-FileHash $coverPath -Algorithm SHA256).Hash -eq $fixture.CoverHashes[$mode]
            $metadataPath = Join-Path $model.Adapter ".bongo-cat-neo-mver.json"
            $mverMatches = $mverMatches -and (Test-Path $metadataPath)
            $metadata = if (Test-Path $metadataPath) { Get-Content $metadataPath -Raw |
                ConvertFrom-Json } else { $null }
            $reportPath = Join-Path $model.Adapter ".bongo-cat-neo-import-report.json"
            $report = if (Test-Path $reportPath) { Get-Content $reportPath -Raw |
                ConvertFrom-Json } else { $null }
            $mverMatches = $mverMatches -and $report.schemaVersion -eq 1 -and
                $report.status -eq "imported-with-documented-degradations" -and
                $report.capabilities.sourceStructurePreserved -and
                $report.capabilities.adapterIsolation
            if ($mode -eq "keyboard") {
                $mverMatches = $mverMatches -and
                    -not (Test-Path (Join-Path $model.Adapter "resources\left-keys\ShiftRight.png")) -and
                    -not (Test-Path (Join-Path $model.Adapter "resources\right-keys\ShiftLeft.png"))
            }
            if ($mode -eq "standard") {
                $mverMatches = $mverMatches -and
                    (Test-Path (Join-Path $model.Adapter "resources\sounds\0.flac")) -and
                    @($metadata.bindings).Count -eq 5 -and
                    @($metadata.bindings | Where-Object kind -eq "sound-clear").Count -eq 1
            }
            if ($mode -eq "keyboard") {
                $mverMatches = $mverMatches -and @($metadata.bindings).Count -eq 4 -and
                    @(Get-ChildItem (Join-Path $model.Adapter "resources\effects") `
                        -Filter "*.png").Count -eq 2
            }
            if ($mode -eq "gamepad") {
                $mverMatches = $mverMatches -and
                    @($metadata.bindings | Where-Object { $_.shortcut -like "Gamepad:*" }).Count -eq 3
            }
            foreach ($relative in @($expected[$mode])) {
                $keyPath = Join-Path $model.Adapter $relative
                if (Test-Path $keyPath) { $mverKeys++ } else { $mverMatches = $false }
                if ($mode -eq "standard" -and (Test-Path $keyPath)) {
                    $expectedHash = if ($relative -like "*ShiftLeft.png") {
                        $fixture.ShiftLeftHash
                    } elseif ($relative -like "*ShiftRight.png") {
                        $fixture.ShiftRightHash
                    } else { $fixture.StandardKeyHash }
                    $mverMatches = $mverMatches -and (Get-FileHash $keyPath `
                        -Algorithm SHA256).Hash -eq $expectedHash
                }
                if ($relative -in @($direct[$mode]) -and (Test-Path $keyPath)) {
                    $expectedHash = if ($mode -eq "keyboard") { $fixture.KeyboardDirectHash } `
                        elseif ($mode -eq "gamepad") { $fixture.GamepadDirectHash } `
                        else { $fixture.StandardKeyHash }
                    $mverMatches = $mverMatches -and
                        (Get-FileHash $keyPath -Algorithm SHA256).Hash -eq $expectedHash
                }
                if ($mode -eq "keyboard" -and $relative -like "*KeyB.png" -and
                    (Test-Path $keyPath)) {
                    $bitmap = [Drawing.Bitmap]::new($keyPath)
                    $pixel = $bitmap.GetPixel(0, 0)
                    $empty = $bitmap.GetPixel($bitmap.Width - 1, $bitmap.Height - 1)
                    $sizeMatches = $bitmap.Width -eq 2 -and $bitmap.Height -eq 2
                    $bitmap.Dispose()
                    $mverMatches = $mverMatches -and $pixel.R -eq 170 -and
                        $pixel.G -eq 0 -and $pixel.B -eq 85 -and $pixel.A -eq 192 -and
                        $sizeMatches -and $empty.A -eq 0
                }
                if ($mode -eq "keyboard" -and $relative -like "*Shift*.png" -and
                    (Test-Path $keyPath)) {
                    $hash = if ($relative -like "*ShiftLeft.png") {
                        $fixture.KeyboardShiftLeftHash
                    } else { $fixture.KeyboardShiftRightHash }
                    $mverMatches = $mverMatches -and
                        (Get-FileHash $keyPath -Algorithm SHA256).Hash -eq $hash
                }
            }
        }
    }
    . (Join-Path $PSScriptRoot "ThemeImportEdgeAssertions.ps1")
    $passed = if ($fixture.Valid) {
        $process.ExitCode -eq 0 -and $installed -eq $fixture.Count -and
            ($fixture.Name -ne "nested-package" -or $covers -eq 3) -and
            $coverMatches -and $payloadMatches -and $edgeMatches -and
            (-not $fixture.Mver -or ($mverMatches -and $mverKeys -eq 12))
    } else {
        $process.ExitCode -ne 0 -and $installed -eq 0
    }
    [pscustomobject]@{ Case=$fixture.Name; ExpectedValid=$fixture.Valid
        ExitCode=$process.ExitCode; Installed=$installed; Covers=$covers
        Reports=$reports; CoverMatches=$coverMatches; PayloadMatches=$payloadMatches
        Passed=$passed }
}
$results | Export-Csv (Join-Path $OutputDir "audit.csv") -NoTypeInformation -Encoding UTF8
$results | Format-Table -AutoSize
if (@($results | Where-Object { -not $_.Passed }).Count) { exit 1 }
