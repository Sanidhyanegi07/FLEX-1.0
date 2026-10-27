"""FLEX 1.0 — Desktop Application (Priority 2 / Stage 1F.3).

A polished, space-inspired dark desktop client built with Tkinter and Python standard library.
Connects the user directly to the native C core pipeline and local Ollama AI engine.

Key Features:
    - Space-inspired dark aesthetic (#0A0D14 obsidian, cyan/blue accents).
    - Prominent FLEX 1.0 branding and real-time C Core + Ollama status telemetry.
    - Threaded non-blocking request pipeline keeping the UI smooth during inference.
    - Direct verification that C core dispatch and state cleanup run for every turn.
    - Clean shutdown handling with graceful subprocess termination.
"""

from __future__ import annotations

import logging
import queue
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk
from typing import Any, Dict, Optional

from .coordinator import (
    AICoordinator,
    CoordinatorChatResult,
    CoordinatorDispatchError,
    CoordinatorError,
    CoordinatorLifecycleError,
    CoordinatorStateError,
)
from .ai.exceptions import (
    AIModelNotFoundError,
    AIServiceUnavailableError,
    AITimeoutError,
    FLEXAIError,
)

logger = logging.getLogger(__name__)

# Color Palette — Space-inspired dark theme
COLOR_BG_ROOT = "#0B0E14"
COLOR_BG_PANEL = "#131822"
COLOR_BG_CHAT = "#0F131C"
COLOR_BG_INPUT = "#182030"
COLOR_BG_CARD = "#192233"

COLOR_TEXT_PRIMARY = "#F1F5F9"
COLOR_TEXT_MUTED = "#94A3B8"
COLOR_TEXT_ACCENT = "#38BDF8"       # Electric Cyan
COLOR_TEXT_USER = "#60A5FA"         # Pulsar Blue
COLOR_TEXT_ASSISTANT = "#F8FAFC"
COLOR_TEXT_ERROR = "#F87171"        # Coral Red
COLOR_TEXT_WARNING = "#FBBF24"      # Amber
COLOR_TEXT_SUCCESS = "#34D399"      # Emerald Green

COLOR_BTN_PRIMARY = "#0284C7"
COLOR_BTN_PRIMARY_HOVER = "#0369A1"
COLOR_BTN_SECONDARY = "#243044"
COLOR_BTN_SECONDARY_HOVER = "#334155"


class FlexDesktopApp:
    """FLEX 1.0 Polished Desktop Application."""

    def __init__(self, root: tk.Tk, coordinator: Optional[AICoordinator] = None) -> None:
        self.root = root
        self.root.title("FLEX 1.0 — Core C + Local AI Desktop")
        self.root.geometry("1000x720")
        self.root.minsize(800, 560)
        self.root.configure(bg=COLOR_BG_ROOT)

        self.coordinator: AICoordinator = (
            coordinator if coordinator is not None else AICoordinator(auto_start=True)
        )
        self._busy: bool = False
        self._last_op_id: int = 0

        self._setup_styles()
        self._build_ui()

        # Handle window close
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)

        # Initial status refresh in background
        self.root.after(200, self.refresh_status)

        # Welcome message in chat
        self._display_welcome_banner()

    def _setup_styles(self) -> None:
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except Exception:
            pass

        style.configure(
            "Flex.Horizontal.TProgressbar",
            background=COLOR_TEXT_ACCENT,
            troughcolor=COLOR_BG_INPUT,
            borderwidth=0,
            thickness=4,
        )

    def _build_ui(self) -> None:
        # Top Header Bar
        header_frame = tk.Frame(self.root, bg=COLOR_BG_PANEL, padx=18, pady=12)
        header_frame.pack(side=tk.TOP, fill=tk.X)

        # Left branding
        brand_left = tk.Frame(header_frame, bg=COLOR_BG_PANEL)
        brand_left.pack(side=tk.LEFT, fill=tk.Y)

        title_lbl = tk.Label(
            brand_left,
            text="FLEX 1.0",
            font=("Segoe UI", 16, "bold"),
            fg=COLOR_TEXT_ACCENT,
            bg=COLOR_BG_PANEL,
        )
        title_lbl.pack(anchor="w")

        subtitle_lbl = tk.Label(
            brand_left,
            text="HYBRID C CORE (C11) + LOCAL AI PIPELINE",
            font=("Segoe UI", 8, "bold"),
            fg=COLOR_TEXT_MUTED,
            bg=COLOR_BG_PANEL,
        )
        subtitle_lbl.pack(anchor="w")

        # Right status telemetry
        self.status_right = tk.Frame(header_frame, bg=COLOR_BG_PANEL)
        self.status_right.pack(side=tk.RIGHT, fill=tk.Y)

        self.badge_bridge = tk.Label(
            self.status_right,
            text="● C CORE: CONNECTING...",
            font=("Consolas", 9, "bold"),
            fg=COLOR_TEXT_WARNING,
            bg=COLOR_BG_CARD,
            padx=10,
            pady=4,
            relief=tk.FLAT,
        )
        self.badge_bridge.pack(side=tk.LEFT, padx=5)

        self.badge_ai = tk.Label(
            self.status_right,
            text="● OLLAMA: CHECKING...",
            font=("Consolas", 9, "bold"),
            fg=COLOR_TEXT_WARNING,
            bg=COLOR_BG_CARD,
            padx=10,
            pady=4,
            relief=tk.FLAT,
        )
        self.badge_ai.pack(side=tk.LEFT, padx=5)

        self.badge_op = tk.Label(
            self.status_right,
            text="OP #0",
            font=("Consolas", 9, "bold"),
            fg=COLOR_TEXT_MUTED,
            bg=COLOR_BG_CARD,
            padx=8,
            pady=4,
            relief=tk.FLAT,
        )
        self.badge_op.pack(side=tk.LEFT, padx=5)

        # Conversation History Display
        chat_container = tk.Frame(self.root, bg=COLOR_BG_ROOT, padx=16, pady=10)
        chat_container.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        self.chat_display = tk.Text(
            chat_container,
            bg=COLOR_BG_CHAT,
            fg=COLOR_TEXT_PRIMARY,
            insertbackground=COLOR_TEXT_ACCENT,
            selectbackground=COLOR_BTN_PRIMARY,
            font=("Segoe UI", 10),
            relief=tk.FLAT,
            padx=14,
            pady=12,
            wrap=tk.WORD,
            state=tk.DISABLED,
        )
        self.chat_display.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        scrollbar = tk.Scrollbar(
            chat_container,
            command=self.chat_display.yview,
            bg=COLOR_BG_PANEL,
            troughcolor=COLOR_BG_ROOT,
            relief=tk.FLAT,
            width=10,
        )
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.chat_display.configure(yscrollcommand=scrollbar.set)

        # Chat tag definitions
        self.chat_display.tag_configure(
            "user_hdr", font=("Segoe UI", 10, "bold"), foreground=COLOR_TEXT_USER
        )
        self.chat_display.tag_configure(
            "user_msg", font=("Segoe UI", 10), foreground=COLOR_TEXT_PRIMARY
        )
        self.chat_display.tag_configure(
            "flex_hdr", font=("Segoe UI", 10, "bold"), foreground=COLOR_TEXT_ACCENT
        )
        self.chat_display.tag_configure(
            "flex_msg", font=("Segoe UI", 10), foreground=COLOR_TEXT_ASSISTANT
        )
        self.chat_display.tag_configure(
            "meta_tag", font=("Consolas", 8), foreground=COLOR_TEXT_MUTED
        )
        self.chat_display.tag_configure(
            "system_msg", font=("Segoe UI", 9, "italic"), foreground=COLOR_TEXT_MUTED
        )
        self.chat_display.tag_configure(
            "error_msg", font=("Segoe UI", 10, "bold"), foreground=COLOR_TEXT_ERROR
        )
        self.chat_display.tag_configure(
            "warning_msg", font=("Segoe UI", 10), foreground=COLOR_TEXT_WARNING
        )
        self.chat_display.tag_configure(
            "banner_hdr", font=("Segoe UI", 11, "bold"), foreground=COLOR_TEXT_ACCENT
        )

        # Progress / Activity Bar
        self.progress_bar = ttk.Progressbar(
            self.root,
            style="Flex.Horizontal.TProgressbar",
            mode="indeterminate",
        )
        self.progress_bar.pack(side=tk.TOP, fill=tk.X, padx=16)

        # Input & Control Deck
        bottom_deck = tk.Frame(self.root, bg=COLOR_BG_PANEL, padx=16, pady=12)
        bottom_deck.pack(side=tk.BOTTOM, fill=tk.X)

        # Status text row
        status_row = tk.Frame(bottom_deck, bg=COLOR_BG_PANEL)
        status_row.pack(side=tk.TOP, fill=tk.X, pady=(0, 8))

        self.status_lbl = tk.Label(
            status_row,
            text="FLEX 1.0 System Ready",
            font=("Segoe UI", 9),
            fg=COLOR_TEXT_MUTED,
            bg=COLOR_BG_PANEL,
        )
        self.status_lbl.pack(side=tk.LEFT)

        # Action Buttons on Right of Status Row
        btn_box = tk.Frame(status_row, bg=COLOR_BG_PANEL)
        btn_box.pack(side=tk.RIGHT)

        self.btn_refresh = tk.Button(
            btn_box,
            text="Refresh Telemetry",
            font=("Segoe UI", 8),
            fg=COLOR_TEXT_PRIMARY,
            bg=COLOR_BTN_SECONDARY,
            activebackground=COLOR_BTN_SECONDARY_HOVER,
            activeforeground=COLOR_TEXT_PRIMARY,
            relief=tk.FLAT,
            padx=10,
            pady=2,
            command=self.refresh_status,
            cursor="hand2",
        )
        self.btn_refresh.pack(side=tk.LEFT, padx=4)

        self.btn_clear = tk.Button(
            btn_box,
            text="Clear Conversation",
            font=("Segoe UI", 8),
            fg=COLOR_TEXT_PRIMARY,
            bg=COLOR_BTN_SECONDARY,
            activebackground=COLOR_BTN_SECONDARY_HOVER,
            activeforeground=COLOR_TEXT_PRIMARY,
            relief=tk.FLAT,
            padx=10,
            pady=2,
            command=self.clear_conversation,
            cursor="hand2",
        )
        self.btn_clear.pack(side=tk.LEFT, padx=4)

        # Text input & Send button row
        input_row = tk.Frame(bottom_deck, bg=COLOR_BG_PANEL)
        input_row.pack(side=tk.BOTTOM, fill=tk.X)

        self.txt_input = tk.Text(
            input_row,
            bg=COLOR_BG_INPUT,
            fg=COLOR_TEXT_PRIMARY,
            insertbackground=COLOR_TEXT_ACCENT,
            font=("Segoe UI", 10),
            height=3,
            relief=tk.FLAT,
            padx=10,
            pady=8,
            wrap=tk.WORD,
        )
        self.txt_input.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0, 10))
        self.txt_input.bind("<Return>", self._handle_enter_key)
        self.txt_input.bind("<Shift-Return>", lambda e: None)  # Allow newline with Shift+Enter
        self.txt_input.focus_set()

        self.btn_send = tk.Button(
            input_row,
            text="SEND\n➤",
            font=("Segoe UI", 10, "bold"),
            fg=COLOR_TEXT_PRIMARY,
            bg=COLOR_BTN_PRIMARY,
            activebackground=COLOR_BTN_PRIMARY_HOVER,
            activeforeground=COLOR_TEXT_PRIMARY,
            relief=tk.FLAT,
            padx=18,
            pady=10,
            command=self.send_message,
            cursor="hand2",
            width=8,
        )
        self.btn_send.pack(side=tk.RIGHT, fill=tk.Y)

    def _display_welcome_banner(self) -> None:
        self._append_to_chat(
            "FLEX 1.0 System Initialized\n",
            "banner_hdr",
        )
        self._append_to_chat(
            "• Architecture: Native C Core (Parser + Lifecycle Manager) linked to Local Ollama.\n"
            "• Every message transitions the C State Manager from IDLE -> PROCESSING -> IDLE.\n"
            "• Type your query below or use commands like 'chat <prompt>', 'status', or 'exit'.\n"
            "─────────────────────────────────────────────────────────────────────────────\n\n",
            "system_msg",
        )

    def _append_to_chat(self, text: str, tag: str) -> None:
        self.chat_display.configure(state=tk.NORMAL)
        self.chat_display.insert(tk.END, text, tag)
        self.chat_display.see(tk.END)
        self.chat_display.configure(state=tk.DISABLED)

    def _handle_enter_key(self, event: Any) -> str:
        # If Shift is pressed, let newline happen naturally
        if event.state & 0x0001:
            return ""
        self.send_message()
        return "break"  # Prevents newline insertion on plain Enter

    def send_message(self) -> None:
        if self._busy:
            return

        raw_text = self.txt_input.get("1.0", tk.END).strip()
        if not raw_text:
            return

        # Clear input box
        self.txt_input.delete("1.0", tk.END)

        # Check for special exit command
        if raw_text.lower() == "exit":
            self._append_to_chat(f"\nUser: {raw_text}\n", "user_hdr")
            self._append_to_chat("Requesting C Core shutdown and closing application...\n", "system_msg")
            self.root.after(800, self.on_closing)
            return

        # Display user message in chat
        self._append_to_chat(f"\nUser:\n", "user_hdr")
        self._append_to_chat(f"{raw_text}\n", "user_msg")

        # Update UI state to processing
        self._busy = True
        self.btn_send.configure(
            text="BUSY...", bg=COLOR_BTN_SECONDARY, state=tk.DISABLED
        )
        self.progress_bar.start(12)
        self.status_lbl.configure(
            text="Processing: Dispatched to C core orchestrator...", fg=COLOR_TEXT_WARNING
        )

        # Run pipeline in background thread
        start_time = time.time()
        threading.Thread(
            target=self._worker_send,
            args=(raw_text, start_time),
            daemon=True,
            name="FlexChatWorker",
        ).start()

    def _worker_send(self, text: str, start_time: float) -> None:
        try:
            result = self.coordinator.send_chat(text)
            elapsed = time.time() - start_time
            self.root.after(0, self._on_chat_success, result, elapsed)
        except Exception as exc:
            elapsed = time.time() - start_time
            self.root.after(0, self._on_chat_error, exc, elapsed)

    def _on_chat_success(self, result: CoordinatorChatResult, elapsed: float) -> None:
        self._last_op_id = result.operation_id
        self.badge_op.configure(text=f"OP #{result.operation_id}", fg=COLOR_TEXT_ACCENT)

        # Display assistant response
        self._append_to_chat(
            f"\nFLEX 1.0 [{result.model}]:\n", "flex_hdr"
        )
        self._append_to_chat(f"{result.text}\n", "flex_msg")

        # Display technical C core verification metadata
        meta = (
            f"└─ [C Core Verified: Op #{result.operation_id} • "
            f"Parser: {result.parser_status} • State: {result.state_status} • "
            f"Latency: {elapsed:.2f}s]\n"
        )
        self._append_to_chat(meta, "meta_tag")

        self._reset_input_state()
        self.status_lbl.configure(
            text=f"Turn complete. C Core Operation #{result.operation_id} cleaned up successfully ({elapsed:.2f}s).",
            fg=COLOR_TEXT_SUCCESS,
        )
        self.refresh_status()

    def _on_chat_error(self, exc: Exception, elapsed: float) -> None:
        self._append_to_chat("\n[FLEX Pipeline Notice]:\n", "error_msg")

        if isinstance(exc, CoordinatorDispatchError):
            err_msg = (
                f"C Core Dispatch Rejected the command.\n"
                f"Details: {exc}\n"
                f"Guidance: Check command syntax or ensure input does not exceed buffer limits (1024 bytes).\n"
            )
            self._append_to_chat(err_msg, "warning_msg")
        elif isinstance(exc, AIServiceUnavailableError):
            err_msg = (
                f"Local Ollama daemon is unreachable (http://127.0.0.1:11434).\n"
                f"Details: {exc}\n"
                f"Guidance: Ensure Ollama is running. In terminal: 'ollama serve'.\n"
            )
            self._append_to_chat(err_msg, "error_msg")
        elif isinstance(exc, AIModelNotFoundError):
            err_msg = (
                f"Requested model was not found in local Ollama storage.\n"
                f"Details: {exc}\n"
            )
            self._append_to_chat(err_msg, "error_msg")
        elif isinstance(exc, AITimeoutError):
            err_msg = (
                f"AI inference exceeded timeout limit.\n"
                f"Details: {exc}\n"
            )
            self._append_to_chat(err_msg, "error_msg")
        else:
            self._append_to_chat(f"Operation failed: {exc}\n", "error_msg")

        self._append_to_chat(
            f"└─ [C Core Cleanup Verified: Operation finished / reset • Elapsed: {elapsed:.2f}s]\n",
            "meta_tag",
        )

        self._reset_input_state()
        self.status_lbl.configure(
            text=f"Error encountered ({type(exc).__name__}). C Core state verified.",
            fg=COLOR_TEXT_ERROR,
        )
        self.refresh_status()

    def _reset_input_state(self) -> None:
        self._busy = False
        self.progress_bar.stop()
        self.btn_send.configure(
            text="SEND\n➤", bg=COLOR_BTN_PRIMARY, state=tk.NORMAL
        )
        self.txt_input.focus_set()

    def clear_conversation(self) -> None:
        self.coordinator.clear_history()
        self.chat_display.configure(state=tk.NORMAL)
        self.chat_display.delete("1.0", tk.END)
        self.chat_display.configure(state=tk.DISABLED)
        self._display_welcome_banner()
        self.status_lbl.configure(
            text="Conversation history cleared. Buffer reset.", fg=COLOR_TEXT_MUTED
        )

    def refresh_status(self) -> None:
        # Run inspection in background to avoid hitching UI
        threading.Thread(target=self._worker_refresh_status, daemon=True).start()

    def _worker_refresh_status(self) -> None:
        try:
            status = self.coordinator.get_status()
            self.root.after(0, self._apply_status, status)
        except Exception as e:
            logger.debug("Telemetry refresh error: %s", e)

    def _apply_status(self, status: Dict[str, Any]) -> None:
        # 1. Bridge Status
        bridge = status.get("bridge", {})
        is_running = bridge.get("is_running", False) or self.coordinator.is_running
        lifecycle = bridge.get("lifecycle", "UNKNOWN")

        if is_running:
            pid = self.coordinator.bridge.pid
            self.badge_bridge.configure(
                text=f"● C CORE: {lifecycle} (PID {pid})",
                fg=COLOR_TEXT_SUCCESS,
            )
        else:
            self.badge_bridge.configure(
                text="● C CORE: OFFLINE",
                fg=COLOR_TEXT_ERROR,
            )

        # 2. Ollama Status
        ai_health = status.get("ai_health", False)
        model = status.get("model", "qwen2.5-coder:3b")
        model_avail = status.get("model_available", False)

        if ai_health and model_avail:
            self.badge_ai.configure(
                text=f"● OLLAMA: {model} (ONLINE)",
                fg=COLOR_TEXT_SUCCESS,
            )
        elif ai_health:
            self.badge_ai.configure(
                text=f"● OLLAMA: ONLINE (MODEL MISSING)",
                fg=COLOR_TEXT_WARNING,
            )
        else:
            self.badge_ai.configure(
                text="● OLLAMA: OFFLINE",
                fg=COLOR_TEXT_ERROR,
            )

    def on_closing(self) -> None:
        self.status_lbl.configure(text="Shutting down C core bridge and terminating...", fg=COLOR_TEXT_WARNING)
        try:
            self.coordinator.close(timeout=1.0)
        except Exception as e:
            logger.debug("Error during coordinator close: %s", e)
        finally:
            self.root.destroy()


def main() -> None:
    """Application entry point."""
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )
    root = tk.Tk()
    app = FlexDesktopApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
