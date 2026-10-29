<#
.SYNOPSIS
    Builds the native FLEX 1.0 C/C++ Qt 6 application, libraries, and tests.
.DESCRIPTION
    Configures CMake using Ninja or Makefiles with MinGW-w64 GCC and builds all targets.
.PARAMETER Clean
    Removes the build directory before configuring.
.PARAMETER Config
    Build configuration (Debug or Release). Default is Debug.
.PARAMETER BuildDir
    The build directory name or path. Default is "build".
.PARAMETER Generator
    CMake generator to use (e.g., "Ninja"). Default is "Ninja".
.PARAMETER Compiler
    C compiler to use. Default is "gcc".
.PARAMETER CxxCompiler
    C++ compiler to use. Default is "g++".
.PARAMETER QtDir
    Optional custom Qt prefix path.
.PARAMETER NoDesktop
    Skip building the Qt desktop application.
.PARAMETER NoTests
    Skip building test executables.
.PARAMETER NoBridge
    Skip building the C core bridge CLI.
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Config = "Debug",
    [string]$BuildDir = "build",
    [string]$Generator = "Ninja",
    [string]$Compiler = "gcc",
    [string]$CxxCompiler = "g++",
    [string]$QtDir = "",
    [switch]$NoDesktop,
    [switch]$NoTests,
    [switch]$NoBridge
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

# Discover Qt / MinGW toolchains in standard installation locations if not already in PATH
$qtMinGW = "C:\Qt\Tools\mingw1310_64\bin"
$qtCMake = "C:\Qt\Tools\CMake_64\bin"
$qtNinja = "C:\Qt\Tools\Ninja"
$qtBin = "C:\Qt\6.11.1\mingw_64\bin"

if ((Test-Path $qtMinGW) -and (Test-Path $qtCMake)) {
    $env:PATH = "$qtMinGW;$qtCMake;$qtNinja;$qtBin;" + $env:PATH
}

$cmakeCmd = Get-Command "cmake" -ErrorAction SilentlyContinue
if (-not $cmakeCmd) {
    Write-Error "CMake was not found in PATH or standard Qt tool directories."
    exit 1
}

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning build directory '$BuildDir'..." -ForegroundColor Cyan
    Remove-Item -Recurse -Force $BuildDir
}

Write-Host "=== FLEX 1.0 Native Build ===" -ForegroundColor Cyan
Write-Host "Configuration: $Config"
Write-Host "Build Directory: $BuildDir"
Write-Host "Generator: $Generator"

$cmakeArgs = @(
    "-B", $BuildDir,
    "-G", $Generator,
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_C_COMPILER=$Compiler",
    "-DCMAKE_CXX_COMPILER=$CxxCompiler"
)

if ($QtDir -and (Test-Path $QtDir)) {
    $cmakeArgs += "-DCMAKE_PREFIX_PATH=$QtDir"
}

if ($NoDesktop) {
    $cmakeArgs += "-DFLEX_BUILD_DESKTOP=OFF"
} else {
    $cmakeArgs += "-DFLEX_BUILD_DESKTOP=ON"
}

if ($NoTests) {
    $cmakeArgs += "-DFLEX_BUILD_TESTS=OFF"
} else {
    $cmakeArgs += "-DFLEX_BUILD_TESTS=ON"
}

if ($NoBridge) {
    $cmakeArgs += "-DFLEX_BUILD_BRIDGE=OFF"
} else {
    $cmakeArgs += "-DFLEX_BUILD_BRIDGE=ON"
}

Write-Host "Configuring CMake project..." -ForegroundColor Cyan
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "Building all targets..." -ForegroundColor Cyan
& cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake build failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "Build completed successfully." -ForegroundColor Green
exit 0

