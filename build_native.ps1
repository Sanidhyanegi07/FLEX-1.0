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

if ((Test-Path $qtMinGW) -and (Test-Path $qtCMake)) {
    $env:PATH = "$qtMinGW;$qtCMake;$qtNinja;" + $env:PATH
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

Write-Host "Configuring CMake project (64-bit MinGW-w64 / Ninja)..." -ForegroundColor Cyan
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

Write-Host "Running native C++ wrapper test suite directly..." -ForegroundColor Cyan
& "$BuildDir\flex_cpp_tests.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Error "C++ wrapper tests failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "All native builds and tests completed successfully." -ForegroundColor Green
exit 0

