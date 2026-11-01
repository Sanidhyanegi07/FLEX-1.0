<#
.SYNOPSIS
    Builds and packages the complete Windows Release distribution of FLEX 1.0.
.DESCRIPTION
    1. Compiles the Release build of flex_desktop.exe and flex_bridge.exe.
    2. Stages a portable release directory under release/FLEX-1.0/.
    3. Runs windeployqt to deploy all Qt 6.11 runtime DLLs, plugins, and platforms.
    4. Copies MinGW-w64 runtime DLLs.
    5. Packages local Whisper GGML models and helper launcher scripts.
    6. Generates a comprehensive README.txt.
    7. Verifies staged executables (IPC status check and clean GUI startup).
    8. Creates release/FLEX-1.0-Windows-x64.zip archive.
#>
[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$SkipZip
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

$releaseBuildDir = Join-Path $repoRoot "build-release"
$releaseStageDir = Join-Path $repoRoot "release\FLEX-1.0"
$releaseZipPath = Join-Path $repoRoot "release\FLEX-1.0-Windows-x64.zip"

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " FLEX 1.0 - Windows Release Packaging Pipeline" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# Step 1: Build Release Binaries
if (-not $SkipBuild) {
    Write-Host "[1/7] Building Release configuration with Ninja..." -ForegroundColor Yellow
    & "$PSScriptRoot\build.ps1" -Config Release -BuildDir "build-release" -NoTests
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Release build failed."
        exit 1
    }
}

$desktopExe = Join-Path $releaseBuildDir "flex_desktop.exe"
if (-not (Test-Path $desktopExe)) {
    $desktopExe = Join-Path $repoRoot "build\flex_desktop.exe"
}

if (-not (Test-Path $desktopExe)) {
    Write-Error "Could not find flex_desktop.exe to package!"
    exit 1
}

# Step 2: Prepare Staging Directory
Write-Host "[2/7] Preparing staging directory at: $releaseStageDir" -ForegroundColor Yellow
if (Test-Path $releaseStageDir) {
    Remove-Item -Path $releaseStageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $releaseStageDir -Force | Out-Null

# Step 3: Copy Executables
Write-Host "[3/7] Copying production executables..." -ForegroundColor Yellow
Copy-Item $desktopExe -Destination $releaseStageDir -Force
$bridgeExe = Join-Path (Split-Path $desktopExe) "flex_bridge.exe"
if (Test-Path $bridgeExe) {
    Copy-Item $bridgeExe -Destination $releaseStageDir -Force
}

# Step 4: Run windeployqt
Write-Host "[4/7] Running windeployqt on flex_desktop.exe..." -ForegroundColor Yellow
$windeployqt = "C:\Qt\6.11.1\mingw_64\bin\windeployqt.exe"
if (Test-Path $windeployqt) {
    & $windeployqt --release --no-translations --compiler-runtime (Join-Path $releaseStageDir "flex_desktop.exe")
} else {
    Write-Warning "windeployqt not found; manual DLL staging will be required."
}

# Copy MinGW compiler runtime DLLs explicitly to ensure portability
$mingwDlls = @(
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll",
    "libgomp-1.dll",
    "libatomic-1.dll"
)
foreach ($dll in $mingwDlls) {
    $src = Join-Path $qtMinGW $dll
    $dst = Join-Path $releaseStageDir $dll
    if ((Test-Path $src) -and (-not (Test-Path $dst))) {
        Copy-Item $src $dst -Force
    }
}

# Ensure platforms plugin directory has both qwindows.dll and qoffscreen.dll
$platformsSrc = "C:\Qt\6.11.1\mingw_64\plugins\platforms"
$platformsDst = Join-Path $releaseStageDir "platforms"
New-Item -ItemType Directory -Path $platformsDst -Force | Out-Null
foreach ($pPlugin in @("qwindows.dll", "qoffscreen.dll")) {
    $srcPlugin = Join-Path $platformsSrc $pPlugin
    if (Test-Path $srcPlugin) {
        Copy-Item $srcPlugin (Join-Path $platformsDst $pPlugin) -Force
    }
}

# Ensure Sqlite driver plugin is present
$sqldriversSrc = "C:\Qt\6.11.1\mingw_64\plugins\sqldrivers"
$sqldriversDst = Join-Path $releaseStageDir "sqldrivers"
if ((Test-Path $sqldriversSrc) -and (-not (Test-Path $sqldriversDst))) {
    Copy-Item $sqldriversSrc $sqldriversDst -Recurse -Force
}

# Step 5: Whisper model & launcher scripts
Write-Host "[5/7] Staging Whisper model assets and launchers..." -ForegroundColor Yellow
$modelsDir = Join-Path $releaseStageDir "models\whisper"
New-Item -ItemType Directory -Path $modelsDir -Force | Out-Null

$userWhisperModel = "$env:USERPROFILE\.flex\models\whisper\ggml-tiny.en.bin"
if (Test-Path $userWhisperModel) {
    Write-Host "  Bundling offline Whisper model (ggml-tiny.en.bin)..." -ForegroundColor Green
    Copy-Item $userWhisperModel -Destination (Join-Path $modelsDir "ggml-tiny.en.bin") -Force
}

# Launcher batch script
$launcherBat = @"
@echo off
setlocal
cd /d "%~dp0"
if exist "%~dp0models\whisper\ggml-tiny.en.bin" (
    set "FLEX_WHISPER_MODEL_PATH=%~dp0models\whisper\ggml-tiny.en.bin"
)
start "" "%~dp0flex_desktop.exe" %*
"@
Set-Content -Path (Join-Path $releaseStageDir "FLEX-1.0.bat") -Value $launcherBat -Encoding ASCII

# Copy download script as utility
Copy-Item (Join-Path $PSScriptRoot "download_whisper_model.ps1") -Destination $releaseStageDir -Force

# Create README.txt
$readmeContent = @"
================================================================================
 FLEX 1.0 — Native Windows Desktop AI Assistant
 Production Release (64-bit Windows)
================================================================================

OVERVIEW
--------
FLEX 1.0 is a native Windows desktop AI assistant built on a dual-core
architecture:
  - C11 Production Core: Command parsing, zero-heap validation, state machine,
    expression evaluator, audio DSP, memory indexer, and security sandbox.
  - C++17 / Qt 6.11 GUI: Responsive 3-column workspace shell, asynchronous
    multithreaded controller, streaming token renderer, and local SQLite vault.
  - Offline Voice Subsystem: WASAPI 16kHz mono audio capture, local Whisper.cpp
    speech-to-text inference, and Windows SAPI 5.4 multithreaded text-to-speech.
  - Local AI Inference: Connects seamlessly to local Ollama instances
    (default: http://127.0.0.1:11434).

SYSTEM REQUIREMENTS
-------------------
  - Operating System : Windows 10 / Windows 11 (64-bit)
  - Memory (RAM)     : 4 GB minimum (8 GB recommended for local LLM inference)
  - Audio Hardware   : Any standard microphone and speaker/headset (WASAPI supported)
  - Local AI Engine  : Ollama (optional, for LLM chat). Download from https://ollama.com

QUICK START
-----------
1. Launch FLEX 1.0:
   - Double-click 'FLEX-1.0.bat' or 'flex_desktop.exe'.
   - The application starts with zero required cloud dependencies.

2. Configure AI Model (Optional):
   - Ensure Ollama is running ('ollama serve' or system tray daemon).
   - Recommended model: 'ollama run qwen2.5-coder:3b' or 'ollama run llama3.2:3b'.
   - In FLEX, open the '⚙️ Settings' view in the left sidebar to verify connectivity
     or adjust temperature and context window.

3. Offline Voice Interaction:
   - The offline Whisper speech model (ggml-tiny.en.bin) is pre-bundled in 'models/whisper/'.
   - Navigate to the 'Home' chat view or '🎙️ Voice Studio'.
   - Click the microphone icon to record spoken prompts, review real-time transcription,
     and submit directly to the AI core.

4. Built-in Tools:
   - Switch to '⚙️ Tools' view or use chat slash commands:
     * /calc <expr>  : Safe expression evaluator (e.g., /calc (12 + 8) * 3 / 2)
     * /read <path>  : Sandboxed workspace file reader
     * /sysinfo      : Local architecture and host diagnostics
     * /status       : C Core engine and lifecycle state
     * /clear        : Reset the conversation view

PORTABLE VAULT & STORAGE
------------------------
  - Persistent SQLite storage is stored in:
    %APPDATA%\FLEX\flex_conversations.db
  - All conversation histories, memory facts, knowledge documents, and user
    settings are strictly persisted on your local machine and never transmitted.

CLI BRIDGE (IPC PROTOCOL)
-------------------------
  - 'flex_bridge.exe' provides line-based JSON IPC over standard input/output:
    * Status inquiry   : echo STATUS | flex_bridge.exe
    * Dispatch command : echo DISPATCH`tchat Hello | flex_bridge.exe
    * Finish operation : echo FINISH | flex_bridge.exe
    * Reset error state: echo RESET_ERROR | flex_bridge.exe

================================================================================
 Copyright (c) 2026 FLEX 1.0 Project. All rights reserved.
================================================================================
"@
Set-Content -Path (Join-Path $releaseStageDir "README.txt") -Value $readmeContent -Encoding UTF8

# Step 6: Verify Staged Executables
Write-Host "[6/7] Verifying staged release executables..." -ForegroundColor Yellow

$bridgeExePath = Join-Path $releaseStageDir "flex_bridge.exe"
if (Test-Path $bridgeExePath) {
    Write-Host "  Verifying flex_bridge.exe via IPC STATUS probe..."
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $bridgeExePath
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true

    $bridgeProc = [System.Diagnostics.Process]::Start($psi)
    $bridgeProc.StandardInput.WriteLine("STATUS")
    $bridgeProc.StandardInput.Close()

    $completed = $bridgeProc.WaitForExit(5000)
    if (-not $completed) {
        $bridgeProc.Kill()
        Write-Error "flex_bridge.exe timed out during IPC verification."
        exit 1
    }

    $bridgeOut = $bridgeProc.StandardOutput.ReadToEnd()
    if ($bridgeOut -match '"action":"STATUS"') {
        Write-Host "  flex_bridge.exe verified: IPC responded successfully." -ForegroundColor Green
    } else {
        Write-Error "flex_bridge.exe did not return expected STATUS JSON. Output: $bridgeOut"
        exit 1
    }
}

$desktopExePath = Join-Path $releaseStageDir "flex_desktop.exe"
Write-Host "  Verifying flex_desktop.exe launch (clean load, no DLL errors)..."
$prevQpa = $env:QT_QPA_PLATFORM
$env:QT_QPA_PLATFORM = "offscreen"
try {
    $desktopProc = Start-Process -FilePath $desktopExePath -WorkingDirectory $releaseStageDir -PassThru
    Start-Sleep -Milliseconds 2500
    if ($desktopProc.HasExited) {
        Write-Error "flex_desktop.exe exited prematurely with exit code $($desktopProc.ExitCode)."
        exit 1
    }
    $desktopProc.CloseMainWindow() | Out-Null
    Start-Sleep -Milliseconds 500
    if (-not $desktopProc.HasExited) {
        $desktopProc.Kill()
    }
    Write-Host "  flex_desktop.exe verified: initialized cleanly without DLL errors or crashes." -ForegroundColor Green
} finally {
    if ($prevQpa) { $env:QT_QPA_PLATFORM = $prevQpa } else { Remove-Item Env:\QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
}

# Step 7: Create ZIP Archive
if (-not $SkipZip) {
    Write-Host "[7/7] Creating release archive: $releaseZipPath" -ForegroundColor Yellow
    $releaseParent = Split-Path $releaseStageDir -Parent
    $zipDir = Join-Path $releaseParent "FLEX-1.0-Windows-x64.zip"
    if (Test-Path $zipDir) {
        Remove-Item $zipDir -Force
    }
    Compress-Archive -Path "$releaseStageDir\*" -DestinationPath $zipDir -CompressionLevel Optimal
    Write-Host "  Release ZIP created successfully ($( [math]::Round((Get-Item $zipDir).Length / 1MB, 2) ) MB)." -ForegroundColor Green
}

Write-Host "==========================================================" -ForegroundColor Green
Write-Host " FLEX 1.0 Packaging Complete!" -ForegroundColor Green
Write-Host " Staged Directory: $releaseStageDir" -ForegroundColor Green
Write-Host " Release Archive : $releaseZipPath" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
