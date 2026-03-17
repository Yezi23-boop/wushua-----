$adcPath = Join-Path $PSScriptRoot "..\project\user\ADC.c"
$content = Get-Content -Path $adcPath -Raw

if (-not ($content -match 'static uint8 adc_measure_enable = 1;')) {
    Write-Error "ADC max tracking is not enabled by default."
    exit 1
}

if (-not ($content -match 'if \(RAW\[i\] > MA\[i\]\) MA\[i\] = RAW\[i\];')) {
    Write-Error "ADC max tracking logic is missing."
    exit 1
}

Write-Host "ADC max tracking check passed."
