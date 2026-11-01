# FLEX 1.0 — User Manual & Operations Guide
## Complete Guide to Installing, Operating, and Customizing Your Native AI Assistant
**Version:** 1.0 Production Release  
**Platform:** Microsoft Windows 10 / Windows 11 (64-bit)  
**Architecture:** 100% Local-First Native C11 & C++17 Application  

---

## 1. Quick Start & Installation

### 1.1 System Requirements
- **Operating System:** Windows 10 or Windows 11 (64-bit).
- **Processor:** Any modern 64-bit x86-64 processor (Intel Core i3/i5/i7/i9 or AMD Ryzen).
- **RAM:** 4 GB minimum (8 GB recommended when running 3B/7B LLMs locally).
- **Storage:** 300 MB for FLEX 1.0 and offline Whisper model; 2–4 GB for local Ollama models.
- **Audio:** Any standard Windows-compatible microphone and audio speaker/headset.

### 1.2 Launching the Application
FLEX 1.0 is completely portable and requires no installer or administrator privileges.
1. Download or locate `release/FLEX-1.0-Windows-x64.zip`.
2. Extract the archive to any folder (e.g. `C:\Users\<YourUsername>\FLEX-1.0`).
3. Inside the extracted folder, double-click **`FLEX-1.0.bat`** or **`flex_desktop.exe`**.
4. The application opens in under 300 milliseconds.

---

## 2. Setting Up Local AI (Ollama)

FLEX 1.0 connects to your local Ollama instance for generative text responses.

### 2.1 Installing Ollama
1. Download Ollama from the official website: **https://ollama.com/download/windows**
2. Run the Ollama installer. Ollama will start automatically in your Windows system tray.

### 2.2 Pulling Recommended Models
Open your Windows Terminal or PowerShell and run:
```powershell
# Recommended for fast, high-quality coding & chat (approx. 2.0 GB)
ollama run qwen2.5-coder:3b

# General conversational model alternative (approx. 2.0 GB)
ollama run llama3.2:3b
```
Once the model responds in the terminal, you can close the terminal. The model will remain available to FLEX.

### 2.3 Verifying AI Connectivity in FLEX
Look at the **Right Sidebar** inside FLEX 1.0:
- **`● AI Engine — Connected (qwen2.5-coder:3b)`** in green indicates you are ready to chat.
- If it shows **`○ AI Engine — Offline`**, verify that Ollama is running, or navigate to **⚙️ Settings** to check your port (`11434`) and host (`127.0.0.1`).

---

## 3. Screen-by-Screen User Guide

### 3.1 Home / Chat View
The **Home** view is your conversational workspace for programming assistance, document analysis, and reasoning.
- **Chat History:** Click any previous conversation in the left sidebar to resume it.
- **Message Input Deck:**
  - Type your prompt in the bottom text box.
  - Press **`Enter`** to send.
  - Press **`Shift + Enter`** to insert a new line for code blocks or multi-paragraph messages.
- **Streaming Responses:** Assistant replies stream in real-time token by token.
- **Cancel Active Generation:** While the assistant is responding, the **Send** button transforms into a red **`⏹ Stop`** button. Clicking it interrupts generation safely without corrupting the chat history.
- **Quick Voice Controls:**
  - Click **`🎙 Record Audio`** to speak directly into the chat.
  - Click **`🔊 Voice Output: ON/OFF`** to toggle spoken responses.
  - Click **`🔊 Test Voice`** to test your Windows SAPI text-to-speech engine.

---

### 3.2 Voice Studio
**Voice Studio** provides a dedicated, distraction-free environment for voice interaction and transcript editing.
- **Audio Device Selection:** Choose your physical microphone from the dropdown selector. FLEX automatically filters out loopback devices (like Stereo Mix) to prevent audio feedback.
- **Recording Lifecycle:**
  - Click **`Start Recording`** and speak clearly into your microphone.
  - Watch the live **Audio Level (VU)** indicator bar reflect your voice volume.
  - Click **`Stop Recording`** when finished.
- **Transcript Review & Editing:**
  - The local Whisper speech model transcribes your recording locally in seconds.
  - The text appears in the editable **Spoken Transcript** box.
  - You can correct words, add punctuation, or expand on your thoughts before sending.
- **Confirm & Send:** Click **`Confirm & Send Prompt`**.
  - FLEX creates a dedicated **`Voice Session`** conversation.
  - Your transcript is sent to Ollama, and the AI's response is both displayed in chat and read aloud via your speakers.

---

### 3.3 Long-Term Memory
The **Memory** view allows FLEX to remember persistent personal facts, preferences, and guidelines across all chats.
- **Adding a New Fact:**
  - **Key:** Enter a descriptive label (e.g. `My Name`, `Coding Style`, `Current Project`).
  - **Content:** Enter the specific detail (e.g. `Sanidhya Negi`, `Always write pure C11 code with strict bounds checks`).
  - **Category:** Select a category (`General`, `Preference`, `Work`, or `System`).
  - Click **`Remember Fact`**.
- **Searching Facts:** Type in the search box to filter your memories dynamically.
- **Editing & Deleting Facts:** Click on any fact card to update its content or click the trash icon to delete it permanently.
- **How It Works in Chat:** When you ask questions like *"What is my name?"* or *"What coding style should we use?"*, FLEX automatically matches the relevant memory facts and injects them into the AI's context.

---

### 3.4 Knowledge Base (RAG)
The **Knowledge Base** gives the AI access to your private documents for question-answering without uploading files to the cloud.
- **Importing Documents:**
  - Click **`Import Document (.txt / .md)`**.
  - Choose any plain text or Markdown document (e.g. `spec.md`, `meeting_notes.txt`).
  - FLEX reads the file, splits it into semantic chunks with overlapping boundaries, and stores them in your local SQLite database.
- **Searching Chunks:** Enter a query in the chunk search bar to view matching passages and their relevance scores.
- **Asking Questions in Chat:** Switch to Home Chat and ask questions about your documents (e.g. *"What are the TrailBeacon sensor specifications according to the lab notes?"*). The relevant chunks will be provided to the AI automatically as factual context.
- **Deleting Documents:** Select a document in the list and click **`Delete Document`** to remove it and its associated chunks completely.

---

### 3.5 Projects & Files
The **Projects** view provides a built-in file manager for your development workspace.
- **Browsing Workspace Files:** The tree view displays files and folders in your project directory.
- **File Preview:** Click on any supported source file (`.c`, `.h`, `.cpp`, `.hpp`, `.txt`, `.md`, `CMakeLists.txt`, etc.) to view line numbers, file size, and complete file contents.
- **Safe Sandboxing:** The workspace viewer is confined strictly to your project folder to prevent accidental modifications to system files.

---

### 3.6 Native Tools
The **Tools** view offers native utility engines that run deterministically with zero AI hallucination:
1. **Expression Calculator:**
   - Evaluates standard arithmetic expressions with parentheses (e.g. `(125.5 + 74.5) * 4 / 2`).
   - Runs in pure C11 with exact decimal precision.
   - Shortcut in Chat: Type `/calc <expression>`.
2. **Workspace Reader & External File Authorizer:**
   - Reads project files safely.
   - **External File Authorization:** Need to inspect a file outside the workspace (e.g. `C:\Users\...\CMakeLists.txt`)? Click **`Browse...`** to authorize that single file.
   - Click **`Attach File Content to Home Chat`** to format the file's text directly into your active chat prompt.
3. **System Diagnostics:**
   - Displays real-time host architecture, CPU model, total RAM, OS build, system uptime, and active C11 core state machine status.

---

### 3.7 Settings & Subsystem Telemetry
The **Settings** view allows you to customize the assistant:
- **AI Engine (Ollama):** Configure the host IP (`127.0.0.1`), port (`11434`), and active model (`qwen2.5-coder:3b`).
- **Generation Parameters:** Adjust LLM temperature (creativity vs determinism) and context window limits.
- **Voice Preferences:** Configure Whisper model path and default speech playback voice.
- **Persistence:** Click **`Save Settings`** to persist your preferences across application restarts.

---

## 4. Data Storage & User Privacy

### 4.1 Where Is My Data Stored?
All conversations, memory facts, imported documents, and user preferences are saved locally on your physical machine:

1. **Relational Database (`conversations.db`):**
   ```text
   %LOCALAPPDATA%\FLEX\conversations.db
   (e.g., C:\Users\<YourUsername>\AppData\Local\FLEX\conversations.db)
   ```
   *(Fallback path if LOCALAPPDATA is undefined: `%USERPROFILE%\.flex\conversations.db`)*

2. **Application Settings (`config.ini`):**
   ```text
   %LOCALAPPDATA%\FLEX\config.ini
   (e.g., C:\Users\<YourUsername>\AppData\Local\FLEX\config.ini)
   ```

3. **Offline Whisper Speech Models:**
   - Pre-bundled portable release path: `<ApplicationDirectory>\models\whisper\ggml-tiny.en.bin`
   - Default user directory: `%USERPROFILE%\.flex\models\whisper\ggml-tiny.en.bin`

### 4.2 Backing Up Your Data
To back up your entire chat history, knowledge base, and memories:
1. Close FLEX 1.0.
2. Copy the `conversations.db` and `config.ini` files from `%LOCALAPPDATA%\FLEX\` to your backup drive or USB stick.
3. To restore, simply place the files back into that folder before launching FLEX.

---

## 5. Troubleshooting Guide

| Issue / Symptom | Possible Cause | Resolution |
|---|---|---|
| **`○ AI Engine — Offline`** | Ollama daemon is not running or blocked by firewall. | Open Windows Terminal and run `ollama serve`. In FLEX Settings, verify the host is `127.0.0.1` and port is `11434`. |
| **No microphone audio in Voice Studio** | Microphone muted or loopback device selected. | Open Voice Studio and select your physical microphone (e.g. "Microphone Array (Realtek)") from the dropdown. Check Windows Microphone Privacy Settings. |
| **Speech recognition fails or takes too long** | Missing Whisper model or slow CPU. | Check that `models/whisper/ggml-tiny.en.bin` exists. Use the pre-bundled `FLEX-1.0.bat` launcher, which sets the model path automatically. |
| **"System is currently processing another operation"** | User clicked Send multiple times rapidly. | FLEX prevents overlapping operations. Requests from Voice Studio queue automatically. Wait for the active response to finish or click `⏹ Stop`. |
| **Directory traversal outside workspace forbidden** | Typed an absolute path in Workspace Reader. | Click the **`Browse...`** button instead. Selecting a file through the Windows file dialog explicitly authorizes one-time read access. |

---

## 6. Built-in Slash Commands

You can type these commands directly into the Home Chat box:
- **`/calc <expression>`** — Calculate math expressions (e.g. `/calc (25 * 4) + 18`).
- **`/read <path>`** — Read a file from your project workspace into chat.
- **`/sysinfo`** — Display host CPU, RAM, and system architecture.
- **`/status`** — Display C11 core state machine status and memory metrics.
- **`/clear`** — Clear the current conversation message display.
- **`/help`** — Show the built-in command cheat sheet.

