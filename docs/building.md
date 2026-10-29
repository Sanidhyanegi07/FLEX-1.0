# FLEX 1.0 — Building and Compiling Guide

This guide covers building the native C/C++ Qt 6 desktop application, libraries, and test suites on Windows.

---

## 1. Prerequisites and Toolchain

FLEX 1.0 is built natively for 64-bit Windows. The verified toolchain components are:

| Component | Minimum Version | Verified Version | Notes |
|---|---|---|---|
| **OS** | Windows 10 (x64) | Windows 11 (x64) | Native Win32 / WASAPI / SAPI |
| **C Compiler** | GCC with C11 support | MinGW-w64 GCC 13.1.0 | Portable strict C11 core |
| **C++ Compiler** | GCC with C++17 support | MinGW-w64 G++ 13.1.0 | C++17 standard required |
| **CMake** | 3.20+ | 3.30.5 | Native build system generator |
| **Generator** | Ninja 1.10+ | Ninja 1.12.1 | Fast parallel compilation |
| **Qt 6 Framework** | Qt 6.5+ | Qt 6.11.1 (MinGW 64-bit) | Core, Sql, Network, Widgets, Multimedia |
| **Whisper Model** | `ggml-tiny.en.bin` | ~77.7 MB | Auto-downloaded via script |
| **Ollama** | 0.3.0+ | Latest | Local daemon running at `127.0.0.1:11434` |

---

## 2. Setting Up the Environment

If Qt and MinGW are installed in standard locations (e.g. `C:\Qt\6.11.1\mingw_64` and `C:\Qt\Tools\mingw1310_64\bin`), the build scripts (`build_native.ps1`, `scripts/build.ps1`) automatically discover and prepend them to `PATH`.

To manually set up your terminal environment:
```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.11.1\mingw_64\bin;" + $env:PATH
```

---

## 3. Downloading the Speech Recognition Model

Before running the voice subsystem or end-to-end voice tests, download the lightweight Whisper speech model:

```powershell
.\scripts\download_whisper_model.ps1
```

This verifies and places `ggml-tiny.en.bin` (~77.7 MB) into:
`%USERPROFILE%\.flex\models\whisper\ggml-tiny.en.bin`

---

## 4. Building the Project

### Option A: Quick One-Step Build & Test (Recommended)
From the repository root:
```powershell
.\build_native.ps1
```
To perform a complete clean build and run all tests:
```powershell
.\build_native.ps1 -Clean
```

### Option B: Using the Modular Build Script (`scripts/build.ps1`)
The modular build script offers fine-grained control:
```powershell
# Standard Debug build
.\scripts\build.ps1

# Release build
.\scripts\build.ps1 -Config Release -BuildDir "build-release"

# Clean previous build before compiling
.\scripts\build.ps1 -Clean

# Custom Qt path if installed in non-standard location
.\scripts\build.ps1 -QtDir "D:\CustomQt\6.11.1\mingw_64"

# Build only core libraries and tests (skip desktop UI executable)
.\scripts\build.ps1 -NoDesktop
```

### Option C: Using CMake Presets
`CMakePresets.json` defines standard configurations:
```powershell
# Configure using the Ninja MinGW Debug preset
cmake --preset ninja-mingw-debug

# Build the preset
cmake --build --preset ninja-mingw-debug

# Run tests via preset
ctest --preset ninja-mingw-debug
```

### Option D: Direct CMake & Ninja Commands
```powershell
# 1. Configure
cmake -B build -G "Ninja" `
    -DCMAKE_BUILD_TYPE=Debug `
    -DCMAKE_C_COMPILER=gcc `
    -DCMAKE_CXX_COMPILER=g++ `
    -DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/mingw_64"

# 2. Build
cmake --build build
```

---

## 5. CMake Build Options

The authoritative `CMakeLists.txt` provides the following user-configurable switches:

| Option | Default | Purpose |
|---|---|---|
| `FLEX_BUILD_DESKTOP` | `ON` | Builds the native Qt 6 desktop application (`flex_desktop.exe`) and UI library. |
| `FLEX_BUILD_TESTS` | `ON` | Compiles all 7 native test suites and enables CTest. |
| `FLEX_BUILD_BRIDGE` | `ON` | Builds the standalone C Core bridge CLI (`flex_bridge.exe`). |

To toggle options:
```powershell
cmake -B build -G "Ninja" -DFLEX_BUILD_DESKTOP=OFF -DFLEX_BUILD_TESTS=ON
```

---

## 6. Compiled Executable Artifacts

After a successful build, the following binaries are produced inside the build directory:

| Executable | Description |
|---|---|
| `build/flex_desktop.exe` | **Native Qt 6 Desktop Application** |
| `build/flex_bridge.exe` | Standalone C Core Bridge CLI |
| `build/flex_c_tests.exe` | C Core Test Runner (541 assertions) |
| `build/flex_cpp_tests.exe` | C++ Core Wrapper Test Runner (214 assertions) |
| `build/flex_storage_tests.exe` | Native SQLite Storage Test Runner (98 assertions) |
| `build/flex_ollama_tests.exe` | Native Ollama Client Test Runner (67 assertions) |
| `build/flex_backend_tests.exe` | Backend Coordinator Integration Test Runner |
| `build/flex_ui_tests.exe` | Native Qt Desktop UI Test Runner (72 assertions) |
| `build/flex_voice_tests.exe` | Native Voice Subsystem Test Runner (65,371+ assertions) |

---

## 7. Running the Application

To start the desktop assistant:
```powershell
.\build\flex_desktop.exe
```

*Note: Ensure Ollama is running in the background (`ollama serve`) with the desired model (e.g. `ollama run qwen2.5-coder:3b`) for live AI responses.*

