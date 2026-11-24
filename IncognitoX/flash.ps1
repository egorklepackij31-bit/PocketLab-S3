param([string]$Port = "")

$pio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\pio.exe"
if (-not (Test-Path $pio)) { $pio = "pio" }

if ($Port) {
    & $pio run -e incognitox -t upload --upload-port $Port
} else {
    & $pio run -e incognitox -t upload
}
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $pio run -e incognitox -t size
