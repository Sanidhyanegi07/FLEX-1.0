[CmdletBinding()]
param(
    [switch]$Clean,
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir

$qtMinGW = "C:\Qt\Tools\mingw1310_64\bin"
$qtCMake = "C:\Qt\Tools\CMake_64\bin"
$qtNinja = "C:\Qt\Tools\Ninja"
$qtBin = "C:\Qt\6.11.1\mingw_64\bin"

if ((Test-Path $qtMinGW) -and (Test-Path $qtCMake)) {
    $env:PATH = "$qtMinGW;$qtCMake;$qtNinja;$qtBin;" + $env:PATH
}

$cmakeCmd = Get-Command "cmake" -ErrorAction SilentlyContinue
if (-not $cmakeCmd) {
    Write-Error "CMake not found in PATH or Qt directory: $qtCMake"
    exit 1
}

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning build directory '$BuildDir'..." -ForegroundColor Cyan
    Remove-Item -Recurse -Force $BuildDir
}

Write-Host "Configuring CMake project (64-bit MinGW-w64 / Qt 6.11.1 / Ninja)..." -ForegroundColor Cyan
& cmake -B $BuildDir -G "Ninja" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "Building all targets..." -ForegroundColor Cyan
& cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake build failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "Running CTest..." -ForegroundColor Cyan
& ctest --test-dir $BuildDir --output-on-failure
if ($LASTEXITCODE -ne 0) {
    Write-Error "CTest execution failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "Running native C++ wrapper test suite (Stage 1)..." -ForegroundColor Cyan
& "$BuildDir\flex_cpp_tests.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Error "C++ wrapper tests failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "Running native C++ storage test suite (Stage 2)..." -ForegroundColor Cyan
& "$BuildDir\flex_storage_tests.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Error "C++ storage tests failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "Running native C++ Ollama client test suite (Stage 3)..." -ForegroundColor Cyan
& "$BuildDir\flex_ollama_tests.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Error "C++ Ollama client tests failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "Running native C++ backend integration test suite (Stage 4)..." -ForegroundColor Cyan
& "$BuildDir\flex_backend_tests.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Error "C++ backend integration tests failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "Running native Qt desktop UI test suite (Stage 5)..." -ForegroundColor Cyan
& "$BuildDir\flex_ui_tests.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Error "Qt desktop UI tests failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "Running native voice subsystem test suite (Stage 6B - Step 1)..." -ForegroundColor Cyan
& "$BuildDir\flex_voice_tests.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Error "Voice subsystem tests failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "All native builds and tests completed successfully." -ForegroundColor Green
exit 0
