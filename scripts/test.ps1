<#
.SYNOPSIS
    Runs the native FLEX 1.0 test suites via CTest and individual test runners.
.DESCRIPTION
    Executes CTest across all native suites or runs a specified test binary.
.PARAMETER BuildDir
    The build directory containing test executables. Default is "build".
.PARAMETER Suite
    Optional name or pattern of a specific test suite to run.
.PARAMETER Verbose
    Show detailed verbose test output.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = "build",
    [string]$Suite = "",
    [switch]$VerboseOutput
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

$qtMinGW = "C:\Qt\Tools\mingw1310_64\bin"
$qtCMake = "C:\Qt\Tools\CMake_64\bin"
$qtNinja = "C:\Qt\Tools\Ninja"
$qtBin = "C:\Qt\6.11.1\mingw_64\bin"

if ((Test-Path $qtMinGW) -and (Test-Path $qtCMake)) {
    $env:PATH = "$qtMinGW;$qtCMake;$qtNinja;$qtBin;" + $env:PATH
}

if (-not (Test-Path $BuildDir)) {
    Write-Error "Build directory '$BuildDir' does not exist. Please run scripts/build.ps1 first."
    exit 1
}

Write-Host "=== FLEX 1.0 Native Test Suite Runner ===" -ForegroundColor Cyan
Write-Host "Build Directory: $BuildDir"

$ctestArgs = @(
    "--test-dir", $BuildDir,
    "--output-on-failure"
)

if ($VerboseOutput) {
    $ctestArgs += "-V"
}

if ($Suite) {
    $ctestArgs += @("-R", $Suite)
    Write-Host "Running tests matching pattern '$Suite'..." -ForegroundColor Cyan
} else {
    Write-Host "Running all test suites via CTest..." -ForegroundColor Cyan
}

& ctest @ctestArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "CTest run failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "All specified test suites passed successfully." -ForegroundColor Green
exit 0

