[CmdletBinding()]
param(
    [switch]$Clean,
    [switch]$SkipBridge,
    [switch]$SkipPython,
    [string]$Compiler = "gcc"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir

$target = "test_command_parser.exe"
$bridgeTarget = "flex_bridge.exe"

if ($Clean) {
    if (Test-Path $target) {
        Remove-Item $target -Force
        Write-Host "Cleaned $target" -ForegroundColor Cyan
    }
    if (Test-Path $bridgeTarget) {
        Remove-Item $bridgeTarget -Force
        Write-Host "Cleaned $bridgeTarget" -ForegroundColor Cyan
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
    Write-Error "Compilation of test suite failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

$bridgeCflags = @(
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-pedantic",
    "-Icore-c/include",
    "core-c/src/command_parser.c",
    "core-c/src/state_manager.c",
    "core-c/src/orchestrator.c",
    "core-c/src/bridge_main.c",
    "-o",
    $bridgeTarget
)

Write-Host "Compiling bridge executable with: $Compiler $bridgeCflags" -ForegroundColor Cyan
& $Compiler @bridgeCflags
if ($LASTEXITCODE -ne 0) {
    Write-Error "Compilation of bridge executable failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Host "Running $target..." -ForegroundColor Cyan
& ".\$target"
$testExitCode = $LASTEXITCODE

if ($testExitCode -ne 0) {
    Write-Error "Unit tests failed with exit code $testExitCode."
    exit $testExitCode
}

if (-not $SkipBridge) {
    Write-Host "Running bridge smoke tests (test_bridge.ps1)..." -ForegroundColor Cyan
    & powershell -ExecutionPolicy Bypass -File ".\test_bridge.ps1" -BridgeExe ".\$bridgeTarget"
    $bridgeExitCode = $LASTEXITCODE
    if ($bridgeExitCode -ne 0) {
        Write-Error "Bridge smoke tests failed with exit code $bridgeExitCode."
        exit $bridgeExitCode
    }
}

if (-not $SkipPython) {
    Write-Host "Running Python unit & integration tests (backend/tests)..." -ForegroundColor Cyan
    $pythonCmd = Get-Command "python" -ErrorAction SilentlyContinue
    if ($pythonCmd) {
        & python -m unittest discover -s backend/tests -v
        $pythonExitCode = $LASTEXITCODE
        if ($pythonExitCode -ne 0) {
            Write-Error "Python tests failed with exit code $pythonExitCode."
            exit $pythonExitCode
        }
    } else {
        Write-Warning "Python interpreter not found in PATH. Skipping Python tests."
    }
}

Write-Host "All builds and tests completed successfully." -ForegroundColor Green
exit 0
