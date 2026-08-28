[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackagePath,
    [Parameter(Mandatory = $true)][string]$ExecutableName
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) {
    throw "Portable package is missing: $PackagePath"
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $PackagePath))
try {
    $entries = @($archive.Entries)
    if ($entries.Count -ne 1 -or $entries[0].FullName -ne $ExecutableName -or
        $entries[0].FullName -match '/$') {
        $actual = ($entries | ForEach-Object FullName) -join ', '
        throw "Portable package must contain only $ExecutableName; found: $actual"
    }
}
finally {
    $archive.Dispose()
}

Write-Host "Portable package contents validated: $ExecutableName"
