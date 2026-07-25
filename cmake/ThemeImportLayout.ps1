if (-not (Get-Command Get-FileHash -ErrorAction SilentlyContinue)) {
    function Get-FileHash {
        [CmdletBinding(DefaultParameterSetName = "Path")]
        param(
            [Parameter(Mandatory = $true, Position = 0, ParameterSetName = "Path")]
            [string[]]$Path,
            [Parameter(Mandatory = $true, ParameterSetName = "LiteralPath")]
            [string[]]$LiteralPath,
            [string]$Algorithm = "SHA256"
        )

        $paths = if ($PSCmdlet.ParameterSetName -eq "LiteralPath") {
            $LiteralPath
        } else {
            $Path | ForEach-Object {
                (Resolve-Path -Path $_ -ErrorAction Stop).Path
            }
        }
        foreach ($filePath in $paths) {
            $resolved = if ($PSCmdlet.ParameterSetName -eq "LiteralPath") {
                (Resolve-Path -LiteralPath $filePath -ErrorAction Stop).Path
            } else {
                $filePath
            }
            $hasher = [Security.Cryptography.HashAlgorithm]::Create($Algorithm)
            if ($null -eq $hasher) {
                throw "Unsupported hash algorithm: $Algorithm"
            }
            try {
                $stream = [IO.File]::OpenRead($resolved)
                try {
                    $bytes = $hasher.ComputeHash($stream)
                } finally {
                    $stream.Dispose()
                }
            } finally {
                $hasher.Dispose()
            }
            [pscustomobject]@{
                Algorithm = $Algorithm.ToUpperInvariant()
                Hash = ([BitConverter]::ToString($bytes) -replace "-", "")
                Path = $resolved
            }
        }
    }
}

function Get-InstalledModelLayout([IO.DirectoryInfo]$Model) {
    $descriptorPath = Join-Path $Model.FullName ".bongo-cat-neo-package.json"
    if (-not (Test-Path -LiteralPath $descriptorPath)) {
        $modePath = Join-Path $Model.FullName ".bongo-cat-neo-mode"
        $mode = if (Test-Path -LiteralPath $modePath) {
            (Get-Content -LiteralPath $modePath -Raw).Trim()
        } else { "" }
        return [pscustomobject]@{ Root=$Model.FullName; Adapter=$Model.FullName
            Directory=$Model.FullName; Payload=$Model.FullName; Mode=$mode
            Preserved=$false; Format="legacy" }
    }
    $descriptor = Get-Content -LiteralPath $descriptorPath -Raw | ConvertFrom-Json
    $adapter = Join-Path $Model.FullName $descriptor.adapter
    $directory = Join-Path $Model.FullName $descriptor.directory
    [pscustomobject]@{ Root=$Model.FullName; Adapter=$adapter; Directory=$directory
        Payload=(Join-Path $Model.FullName "payload"); Mode=$descriptor.mode
        Preserved=($descriptor.schemaVersion -eq 1 -and
            $descriptor.layout -eq "preserved-payload" -and
            (Test-Path -LiteralPath $adapter -PathType Container) -and
            (Test-Path -LiteralPath $directory -PathType Container))
        Format=$descriptor.format }
}

function Get-TreeSignature([string]$Root) {
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\','/')
    $items = [Collections.Generic.List[string]]::new()
    Get-ChildItem -LiteralPath $rootPath -Recurse -Force -Directory | ForEach-Object {
        $relative = $_.FullName.Substring($rootPath.Length).TrimStart('\','/')
        $items.Add("D|" + $relative.Replace('\','/'))
    }
    Get-ChildItem -LiteralPath $rootPath -Recurse -Force -File | ForEach-Object {
        $relative = $_.FullName.Substring($rootPath.Length).TrimStart('\','/')
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        $items.Add("F|" + $relative.Replace('\','/') + "|" + $hash)
    }
    return @($items | Sort-Object)
}

function Test-TreeEqual([string]$Source, [string]$Target) {
    if (-not (Test-Path -LiteralPath $Source -PathType Container) -or
        -not (Test-Path -LiteralPath $Target -PathType Container)) { return $false }
    $left = @(Get-TreeSignature $Source)
    $right = @(Get-TreeSignature $Target)
    return $left.Count -eq $right.Count -and
        -not (Compare-Object $left $right)
}
