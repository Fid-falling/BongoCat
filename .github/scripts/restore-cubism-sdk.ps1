[CmdletBinding()]
param(
    [string]$ArchiveUrl = $env:CUBISM_SDK_ARCHIVE_URL,
    [string]$ExpectedSha256 = $env:CUBISM_SDK_ARCHIVE_SHA256,
    [string]$Destination
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $Destination) {
    $Destination = Join-Path $repositoryRoot 'vendor\CubismSdkForNative'
}

if ([string]::IsNullOrWhiteSpace($ArchiveUrl) -or
    [string]::IsNullOrWhiteSpace($ExpectedSha256)) {
    throw @"
The Store workflow needs the licensed Cubism SDK archive. Configure the
CUBISM_SDK_ARCHIVE_URL and CUBISM_SDK_ARCHIVE_SHA256 repository secrets with a
private URL and SHA-256 for CubismSdkForNative 5 r.5. The SDK is intentionally
not committed to this repository.
"@
}

$ExpectedSha256 = $ExpectedSha256.Trim().ToUpperInvariant()
if ($ExpectedSha256 -notmatch '^[0-9A-F]{64}$') {
    throw "CUBISM_SDK_ARCHIVE_SHA256 must be a 64-character SHA-256 digest."
}

$Destination = [System.IO.Path]::GetFullPath($Destination)
$repositoryPrefix = $repositoryRoot.TrimEnd('\', '/') +
    [IO.Path]::DirectorySeparatorChar
if (-not $Destination.StartsWith($repositoryPrefix,
        [StringComparison]::OrdinalIgnoreCase) -or
    (Split-Path $Destination -Leaf) -ne 'CubismSdkForNative') {
    throw "Refusing to replace an unexpected Cubism SDK destination: $Destination"
}
$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("BongoCatCubism_" + [guid]::NewGuid())
$archivePath = Join-Path $temporaryRoot "cubism-sdk.zip"
$extractPath = Join-Path $temporaryRoot "extracted"

try {
    New-Item -ItemType Directory -Force -Path $temporaryRoot, $extractPath | Out-Null
    Write-Host "Downloading the private Cubism SDK archive..."
    Invoke-WebRequest -Uri $ArchiveUrl -OutFile $archivePath -UseBasicParsing

    $actualSha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actualSha256 -ne $ExpectedSha256) {
        throw "Cubism SDK archive hash mismatch: expected $ExpectedSha256, got $actualSha256."
    }

    Expand-Archive -LiteralPath $archivePath -DestinationPath $extractPath -Force
    $rootCandidate = Get-Item -LiteralPath $extractPath
    if (-not (Test-Path (Join-Path $rootCandidate.FullName "Core\include\Live2DCubismCore.h"))) {
        $rootCandidate = Get-ChildItem -LiteralPath $extractPath -Directory -Recurse |
            Where-Object { Test-Path (Join-Path $_.FullName "Core\include\Live2DCubismCore.h") } |
            Select-Object -First 1
    }
    if (-not $rootCandidate) {
        throw "The archive does not contain a CubismSdkForNative Core/include tree."
    }

    $required = @(
        (Join-Path $rootCandidate.FullName "Core\include\Live2DCubismCore.h"),
        (Join-Path $rootCandidate.FullName "Core\lib\windows\x86_64\143\Live2DCubismCore_MT.lib"),
        (Join-Path $rootCandidate.FullName "Framework\CMakeLists.txt"),
        (Join-Path $rootCandidate.FullName "Samples\OpenGL\thirdParty\glew\build\cmake\CMakeLists.txt")
    )
    foreach ($path in $required) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "The Cubism SDK archive is incomplete; missing $path."
        }
    }

    Remove-Item -LiteralPath $Destination -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $rootCandidate.FullName -Force | Copy-Item -Destination $Destination -Recurse -Force
    Write-Host "Cubism SDK restored to $Destination"
}
finally {
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
}
