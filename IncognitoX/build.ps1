$pio = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\pio.exe"
if (-not (Test-Path $pio)) { $pio = "pio" }

& $pio run -e incognitox
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $pio run -e incognitox -t size
