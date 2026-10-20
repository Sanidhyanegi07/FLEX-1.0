[CmdletBinding()]
param(
    [switch]$Clean,
    [string]$Compiler = "gcc"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir

$target = "test_command_parser.exe"

if ($Clean) {
    if (Test-Path $target) {
        Remove-Item $target -Force
        Write-Host "Cleaned $target" -ForegroundColor Cyan
    }
}

Write-Host "Checking C compiler ($Compiler)..." -ForegroundColor Cyan
$compilerCmd = Get-Command $Compiler -ErrorAction SilentlyContinue
if (-not $compilerCmd) {
    Write-Error "Compiler '$Compiler' not found in PATH. Please ensure GCC is installed and on PATH."
    exit 1
}

$cflags = @(
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-pedantic",
    "-Icore-c/include",
    "core-c/src/command_parser.c",
    "core-c/src/state_manager.c",
    "core-c/src/orchestrator.c",
    "core-c/tests/test_command_parser.c",
    "core-c/tests/test_state_manager.c",
    "core-c/tests/test_orchestrator.c",
    "-o",
    $target
)



Write-Host "Compiling test suite with: $Compiler $cflags" -ForegroundColor Cyan
& $Compiler @cflags
if ($LASTEXITCODE -ne 0) {
    Write-Error "Compilation failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "Running $target..." -ForegroundColor Cyan
& ".\$target"
$testExitCode = $LASTEXITCODE

if ($testExitCode -ne 0) {
    Write-Error "Tests failed with exit code $testExitCode."
    exit $testExitCode
}

Write-Host "All tests completed successfully." -ForegroundColor Green
exit 0
