<#
.SYNOPSIS
    Convenience wrapper to build and test the native FLEX 1.0 desktop assistant.
.DESCRIPTION
    Delegates to scripts/build.ps1 and scripts/test.ps1.
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Config = "Debug",
    [string]$BuildDir = "build",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir

$buildArgs = @{
    BuildDir = $BuildDir
    Config   = $Config
}
if ($Clean) { $buildArgs["Clean"] = $true }

& "$scriptDir\scripts\build.ps1" @buildArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

if (-not $SkipTests) {
    & "$scriptDir\scripts\test.ps1" -BuildDir $BuildDir
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Test suite execution failed with exit code $LASTEXITCODE."
        exit $LASTEXITCODE
    }
}

Write-Host "All native builds and tests completed successfully." -ForegroundColor Green
exit 0
