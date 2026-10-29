<#
.SYNOPSIS
    Calculates first-party C and C++ production source lines of code (LOC).
.DESCRIPTION
    Counts raw lines and source lines of code (SLOC excluding comments and blank lines).
    Excludes third-party (whisper.cpp), test suites, generated files, and build directories.
#>
[CmdletBinding()]
param(
    [switch]$IncludeHeaders
)

function Get-CodeStats {
    param([string]$FilePath)
    $lines = Get-Content -LiteralPath $FilePath
    $raw = $lines.Count
    $sloc = 0
    $inBlockComment = $false
    foreach ($line in $lines) {
        $trimmed = $line.Trim()
        if ($inBlockComment) {
            if ($trimmed -match '\*/') {
                $inBlockComment = $false
                $postComment = $trimmed -replace '^.*?\*/', ''
                if ($postComment.Trim().Length -gt 0) { $sloc++ }
            }
            continue
        }
        if ($trimmed.Length -eq 0) { continue }
        if ($trimmed.StartsWith('/*')) {
            if ($trimmed -match '\*/') {
                $postComment = $trimmed -replace '^/\*.*?\*/', ''
                if ($postComment.Trim().Length -gt 0) { $sloc++ }
            } else {
                $inBlockComment = $true
            }
            continue
        }
        if ($trimmed.StartsWith('//')) { continue }
        $sloc++
    }
    return [PSCustomObject]@{ Raw = $raw; SLOC = $sloc }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host " FLEX 1.0 - FIRST-PARTY PRODUCTION CODE MEASUREMENT " -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan

# C production source files
$cFiles = Get-ChildItem -Path (Join-Path $repoRoot "core-c\src") -Filter "*.c"

Write-Host "`n[Production C Source Files (.c)]" -ForegroundColor Yellow
$cRaw = 0; $cSLOC = 0
foreach ($f in $cFiles) {
    $stats = Get-CodeStats $f.FullName
    $cRaw += $stats.Raw
    $cSLOC += $stats.SLOC
    $lineStr = '  {0,-35} Raw: {1,5} | SLOC: {2,5}' -f $f.Name, $stats.Raw, $stats.SLOC
    Write-Host $lineStr
}
$totCStr = '  {0,-35} Raw: {1,5} | SLOC: {2,5}' -f "TOTAL C PRODUCTION SOURCES", $cRaw, $cSLOC
Write-Host $totCStr -ForegroundColor Green

# C++ production source files
$cppFiles = Get-ChildItem -Path (Join-Path $repoRoot "src-cpp\src") -Recurse -Filter "*.cpp"

Write-Host "`n[Production C++ Source Files (.cpp)]" -ForegroundColor Yellow
$cppRaw = 0; $cppSLOC = 0
foreach ($f in $cppFiles) {
    $rel = $f.FullName.Substring($repoRoot.Length + 1)
    $stats = Get-CodeStats $f.FullName
    $cppRaw += $stats.Raw
    $cppSLOC += $stats.SLOC
    $lineStr = '  {0,-45} Raw: {1,5} | SLOC: {2,5}' -f $rel, $stats.Raw, $stats.SLOC
    Write-Host $lineStr
}
$totCppStr = '  {0,-45} Raw: {1,5} | SLOC: {2,5}' -f "TOTAL C++ PRODUCTION SOURCES", $cppRaw, $cppSLOC
Write-Host $totCppStr -ForegroundColor Green

$totalSLOC = $cSLOC + $cppSLOC
$cSLOCPercent = if ($totalSLOC -gt 0) { [math]::Round(($cSLOC / $totalSLOC) * 100, 2) } else { 0 }
$cppSLOCPercent = if ($totalSLOC -gt 0) { [math]::Round(($cppSLOC / $totalSLOC) * 100, 2) } else { 0 }

$totalRaw = $cRaw + $cppRaw
$cRawPercent = if ($totalRaw -gt 0) { [math]::Round(($cRaw / $totalRaw) * 100, 2) } else { 0 }
$cppRawPercent = if ($totalRaw -gt 0) { [math]::Round(($cppRaw / $totalRaw) * 100, 2) } else { 0 }

Write-Host "`n=================================================================" -ForegroundColor Cyan
Write-Host " PRODUCTION RATIO SUMMARY" -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host ('  Total Executable SLOC: {0}' -f $totalSLOC)
$cColor = if ($cSLOCPercent -ge 50) { "Green" } else { "Red" }
$s1 = '  C Production SLOC:     {0,5} ({1,5}%)' -f $cSLOC, $cSLOCPercent
Write-Host $s1 -ForegroundColor $cColor
$s2 = '  C++ Production SLOC:   {0,5} ({1,5}%)' -f $cppSLOC, $cppSLOCPercent
Write-Host $s2
Write-Host "-----------------------------------------------------------------"
Write-Host ('  Total Raw Lines:       {0}' -f $totalRaw)
$s3 = '  C Raw Lines:           {0,5} ({1,5}%)' -f $cRaw, $cRawPercent
Write-Host $s3
$s4 = '  C++ Raw Lines:         {0,5} ({1,5}%)' -f $cppRaw, $cppRawPercent
Write-Host $s4
Write-Host "=================================================================`n"

if ($IncludeHeaders) {
    Write-Host "[C Headers (.h)]" -ForegroundColor Yellow
    $chFiles = Get-ChildItem -Path (Join-Path $repoRoot "core-c\include") -Filter "*.h"
    $chRaw = 0; $chSLOC = 0
    foreach ($f in $chFiles) {
        $stats = Get-CodeStats $f.FullName
        $chRaw += $stats.Raw; $chSLOC += $stats.SLOC
        $hStr = '  {0,-35} Raw: {1,5} | SLOC: {2,5}' -f $f.Name, $stats.Raw, $stats.SLOC
        Write-Host $hStr
    }
    $hTot = '  TOTAL C HEADERS: Raw: {0,5} | SLOC: {1,5}' -f $chRaw, $chSLOC
    Write-Host $hTot

    Write-Host "`n[C++ Headers (.hpp)]" -ForegroundColor Yellow
    $cpphFiles = Get-ChildItem -Path (Join-Path $repoRoot "src-cpp\include") -Recurse -Filter "*.hpp"
    $cpphRaw = 0; $cpphSLOC = 0
    foreach ($f in $cpphFiles) {
        $rel = $f.FullName.Substring($repoRoot.Length + 1)
        $stats = Get-CodeStats $f.FullName
        $cpphRaw += $stats.Raw; $cpphSLOC += $stats.SLOC
        $hppStr = '  {0,-45} Raw: {1,5} | SLOC: {2,5}' -f $rel, $stats.Raw, $stats.SLOC
        Write-Host $hppStr
    }
    $hppTot = '  TOTAL C++ HEADERS: Raw: {0,5} | SLOC: {1,5}' -f $cpphRaw, $cpphSLOC
    Write-Host $hppTot
}
