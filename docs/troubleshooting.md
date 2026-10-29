# FLEX 1.0 — Troubleshooting & Diagnostics

This guide addresses common configuration, compilation, and runtime issues.

---

## 1. Build and Compilation Issues

### Issue 1.1: CMake or Compiler Not Recognized
**Symptom**: `cmake : The term 'cmake' is not recognized...` or `gcc : The term 'gcc' is not recognized...`  
**Cause**: The MinGW-w64 and CMake toolchain paths are not in your active PowerShell session's `$env:PATH`.  
**Resolution**:
Run `build_native.ps1` or prepend the Qt tools directory to your session:
```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.11.1\mingw_64\bin;" + $env:PATH
```

### Issue 1.2: Dirty Build Cache After Source Changes
**Symptom**: CMake configure errors mentioning missing files, stale target names, or cache inconsistencies.  
**Resolution**:
Perform a clean build:
```powershell
.\build_native.ps1 -Clean
```

---

## 2. Speech and Voice Issues

### Issue 2.1: "Whisper speech model not found"
**Symptom**: The UI status bar reports that the Whisper model cannot be located.  
**Cause**: The model binary `ggml-tiny.en.bin` has not been downloaded to the user profile directory.  
**Resolution**:
Run the automated model downloader:
```powershell
.\scripts\download_whisper_model.ps1
```
Verify that `%USERPROFILE%\.flex\models\whisper\ggml-tiny.en.bin` exists and is ~77.7 MB.

### Issue 2.2: Microphone Capture Fails / Audio Level Stays at Zero
**Symptom**: Microphone level indicator does not move during speech recording.  
**Cause**:
1. Windows microphone privacy permissions are blocking the application.
2. The default audio recording device is muted or disabled.  
**Resolution**:
1. Open Windows **Settings** > **Privacy & security** > **Microphone**.
2. Ensure **Microphone access** is toggled **On**.
3. Ensure **Let desktop apps access your microphone** is toggled **On**.
4. Check **Sound Settings** to confirm your default input device is active.

### Issue 2.3: Windows SAPI Voice Speech Fails
**Symptom**: SAPI text-to-speech produces no audible sound.  
**Cause**: Windows SAPI voices may be uninitialized or volume set to 0.  
**Resolution**:
Open **SettingsView** in the application and test the speech synthesis engine. Verify that the system volume is enabled and the default Windows TTS voice (e.g. "Microsoft David" or "Microsoft Zira") is installed.

---

## 3. Ollama AI Connectivity Issues

### Issue 3.1: "AI Unavailable" / HTTP Connection Refused
**Symptom**: Sending a message yields a red error banner: `Connection error: Ollama is unreachable at http://127.0.0.1:11434`.  
**Cause**: The local Ollama daemon is not active.  
**Resolution**:
1. Open a separate terminal window and launch Ollama:
   ```powershell
   ollama serve
   ```
2. Verify Ollama is listening by opening `http://127.0.0.1:11434` in your browser.

### Issue 3.2: Model Not Found on Ollama Daemon
**Symptom**: Ollama returns HTTP 404 or an error indicating the model `qwen2.5-coder:3b` is not installed.  
**Resolution**:
Pull the required model locally:
```powershell
ollama pull qwen2.5-coder:3b
```
You can verify installed models with:
```powershell
ollama list
```

---

## 4. SQLite Storage and User Data

### Issue 4.1: Database File Location & Schema
**Location**: `%LOCALAPPDATA%\FLEX\conversations.db`  
**Settings**: `%LOCALAPPDATA%\FLEX\settings.json`  

FLEX 1.0 automatically creates and migrates this database. If you need to inspect the database schema or contents directly, you can use any standard SQLite browser:
```powershell
sqlite3 "$env:LOCALAPPDATA\FLEX\conversations.db" ".schema"
```

### Issue 4.2: Database Locked Error
**Symptom**: Error indicating SQLite database is locked.  
**Cause**: Another process (e.g. an external SQLite viewer) has an exclusive lock on the file.  
**Resolution**:
Close any external SQLite viewers or previous instances of `flex_desktop.exe`. FLEX 1.0 uses a 5,000 ms busy timeout to automatically handle concurrent access.

