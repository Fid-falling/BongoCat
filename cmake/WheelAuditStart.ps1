$env:BONGO_CAT_NEO_ALLOW_TEST_INSTANCES = "1"
$env:BONGO_CAT_NEO_TEST_INSTANCE_ID = "wheel-audit-$PID"
$data = if ($DataRoot) { [IO.Path]::GetFullPath($DataRoot) } else {
    Join-Path $OutputDir ("data-" + [DateTime]::UtcNow.Ticks)
}
$arguments = @("--ci-smoke", "--ci-frame-series", "--ci-exit-ms=9000",
    "--data-root=$data")
if ($ControlOpacity) {
    $config = Join-Path $OutputDir "opacity-settings.json"
    $json = @{ schemaVersion=2; window=@{ opacity=$InitialOpacity } } |
        ConvertTo-Json -Compress
    [IO.File]::WriteAllText($config, $json, [Text.UTF8Encoding]::new($false))
    $arguments += "--config=$config"
}
$frameSeries = Join-Path $data "frame-series.csv"
for ($attempt = 0; $attempt -lt 20; $attempt++) {
    try {
        if ([IO.File]::Exists($frameSeries)) { [IO.File]::Delete($frameSeries) }
        break
    } catch { Start-Sleep -Milliseconds 10 }
}
$process = Start-Process $Exe -ArgumentList $arguments `
    -WorkingDirectory (Split-Path $Exe) -PassThru
$globalControlDown = $false
