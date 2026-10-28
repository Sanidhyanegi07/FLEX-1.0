[CmdletBinding()]
param(
    [string]$ModelName = "tiny.en",
    [string]$TargetDir = "$env:USERPROFILE\.flex\models\whisper"
)

$ErrorActionPreference = "Stop"

$modelFileName = "ggml-$ModelName.bin"
$modelUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$modelFileName"
$destinationPath = Join-Path $TargetDir $modelFileName

Write-Host "=== FLEX 1.0 Whisper Model Downloader ===" -ForegroundColor Cyan
Write-Host "Model: $modelFileName"
Write-Host "Target Directory: $TargetDir"
Write-Host "Download URL: $modelUrl"

if (-not (Test-Path $TargetDir)) {
    New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
}

if (Test-Path $destinationPath) {
    $existingSize = (Get-Item $destinationPath).Length
    if ($existingSize -gt 50MB) {
        Write-Host "Model file already exists ($([math]::Round($existingSize / 1MB, 2)) MB): $destinationPath" -ForegroundColor Green
        exit 0
    } else {
        Write-Warning "Existing model file is too small ($existingSize bytes). Re-downloading..."
        Remove-Item -Force $destinationPath
    }
}

Write-Host "Downloading $modelFileName (~75 MB)... Please wait." -ForegroundColor Yellow
$curlCmd = Get-Command "curl.exe" -ErrorAction SilentlyContinue
if ($curlCmd) {
    & curl.exe -L --fail --progress-bar -o "$destinationPath" "$modelUrl"
    if ($LASTEXITCODE -ne 0) {
        Write-Error "curl download failed with exit code $LASTEXITCODE."
        exit $LASTEXITCODE
    }
} else {
    Invoke-WebRequest -Uri $modelUrl -OutFile $destinationPath
}

if (Test-Path $destinationPath) {
    $downloadedSize = (Get-Item $destinationPath).Length
    if ($downloadedSize -lt 10MB) {
        Write-Error "Downloaded file is corrupted or too small ($downloadedSize bytes). Check internet connection."
        Remove-Item -Force $destinationPath
        exit 1
    }
    Write-Host "Model downloaded successfully ($([math]::Round($downloadedSize / 1MB, 2)) MB) to:" -ForegroundColor Green
    Write-Host "  $destinationPath" -ForegroundColor Green
} else {
    Write-Error "Failed to save model to destination: $destinationPath"
    exit 1
}

exit 0

