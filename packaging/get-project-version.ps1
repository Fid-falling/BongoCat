[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path $PSScriptRoot -Parent
$cmakeLists = Get-Content -LiteralPath (
    Join-Path $repositoryRoot 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch
    '(?m)^project\(BongoCat VERSION ([0-9]+)\.([0-9]+)\.([0-9]+)\b') {
    throw 'Unable to read the BongoCat version from CMakeLists.txt.'
}

$parts = @([int]$Matches[1], [int]$Matches[2], [int]$Matches[3])
if ($parts | Where-Object { $_ -gt 65535 }) {
    throw 'Version components must be between 0 and 65535.'
}
if ($parts[0] -ge 65535) {
    throw 'The major version is too large for the Microsoft Store mapping.'
}

[PSCustomObject]@{
    AppVersion = $parts -join '.'
    StorePackageVersion = "$($parts[0] + 1).$($parts[1]).$($parts[2]).0"
}
