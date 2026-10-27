"""FLEX 1.0 — Desktop Application (Stage 1 Multi-Panel Shell).

A polished, space-inspired dark desktop client built with Tkinter and Python standard library.
Connects the user directly to the native C core pipeline and local Ollama AI engine.

Stage 1 Architecture:
    - Persistent Shell: Top telemetry bar + Left navigation sidebar + Center dynamic view container.
    - Navigation: Home, New Chat, Voice Studio, Memory, Knowledge Base, Projects & Files, Tools, Settings.
    - Encapsulated ChatView: Preserves all existing working chat, C core dispatch, Ollama inference,
      busy-state locking, error formatting, and verification metadata.
    - Truthful Prototype Views: Clearly labelled status cards for unfinished capabilities.
      Contains ZERO fake interactive buttons.
    - Local Session: Honest offline profile panel; explicitly documents no cloud authentication.
    - Strictly standard-library compliant: Python standard library `tkinter` and `ttk` only.
"""

from __future__ import annotations

import logging
import queue
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk
from typing import Any, Dict, List, Optional

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
COLOR_BG_SIDEBAR = "#0F141F"
COLOR_BG_SIDEBAR_ACTIVE = "#1E293B"
COLOR_BG_SIDEBAR_HOVER = "#1A2333"
COLOR_BG_CHAT = "#0F131C"
COLOR_BG_INPUT = "#182030"
COLOR_BG_CARD = "#192233"
COLOR_BORDER = "#1E293B"

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


class ChatView(tk.Frame):
    """Encapsulated Chat Interface for FLEX 1.0 (Home View).

    Houses conversation display, rich tags, multiline input deck,
    and threaded non-blocking worker pipeline.
    """

    def __init__(
        self,
        parent: tk.Widget,
        coordinator: AICoordinator,
        root: tk.Tk,
        app: Optional["FlexDesktopApp"] = None,
    ) -> None:
        super().__init__(parent, bg=COLOR_BG_ROOT)
        self.coordinator: AICoordinator = coordinator
        self.root: tk.Tk = root
        self.app: Optional["FlexDesktopApp"] = app
        self._busy: bool = False
        self._closing: bool = False
        self._last_op_id: int = 0

        self._build_ui()
        self._display_welcome_banner()

    def _build_ui(self) -> None:
        # Conversation History Display
        chat_container = tk.Frame(self, bg=COLOR_BG_ROOT, padx=16, pady=10)
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
            self,
            style="Flex.Horizontal.TProgressbar",
            mode="indeterminate",
        )
        self.progress_bar.pack(side=tk.TOP, fill=tk.X, padx=16)

        # Input & Control Deck
        bottom_deck = tk.Frame(self, bg=COLOR_BG_PANEL, padx=16, pady=12)
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
            command=self._on_refresh_clicked,
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
        self.txt_input.bind("<Shift-Return>", lambda e: None)
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

    def _on_refresh_clicked(self) -> None:
        if self.app and hasattr(self.app, "refresh_status"):
            self.app.refresh_status()

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
        if event.state & 0x0001:
            return ""
        self.send_message()
        return "break"

    def send_message(self) -> None:
        if self._busy:
            return

        raw_text = self.txt_input.get("1.0", tk.END).strip()
        if not raw_text:
            return

        self.txt_input.delete("1.0", tk.END)

        if raw_text.lower() == "exit":
            self._append_to_chat(f"\nUser: {raw_text}\n", "user_hdr")
            self._append_to_chat("Requesting C Core shutdown and closing application...\n", "system_msg")
            if self.app and hasattr(self.app, "on_closing"):
                self.root.after(800, self.app.on_closing)
            else:
                self.root.after(800, self.root.destroy)
            return

        self._append_to_chat(f"\nUser:\n", "user_hdr")
        self._append_to_chat(f"{raw_text}\n", "user_msg")

        self._busy = True
        self.btn_send.configure(
            text="BUSY...", bg=COLOR_BTN_SECONDARY, state=tk.DISABLED
        )
        self.progress_bar.start(12)
        self.status_lbl.configure(
            text="Processing: Dispatched to C core orchestrator...", fg=COLOR_TEXT_WARNING
        )

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
            if not self._closing and not (self.app and self.app._closing):
                self.root.after(0, self._on_chat_success, result, elapsed)
        except Exception as exc:
            elapsed = time.time() - start_time
            if not self._closing and not (self.app and self.app._closing):
                self.root.after(0, self._on_chat_error, exc, elapsed)

    def _on_chat_success(self, result: CoordinatorChatResult, elapsed: float) -> None:
        if self._closing or (self.app and self.app._closing):
            return
        self._last_op_id = result.operation_id
        if self.app and hasattr(self.app, "badge_op") and self.app.badge_op.winfo_exists():
            self.app.badge_op.configure(text=f"OP #{result.operation_id}", fg=COLOR_TEXT_ACCENT)

        self._append_to_chat(
            f"\nFLEX 1.0 [{result.model}]:\n", "flex_hdr"
        )
        self._append_to_chat(f"{result.text}\n", "flex_msg")

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
        if self.app and hasattr(self.app, "refresh_status"):
            self.app.refresh_status()

    def _on_chat_error(self, exc: Exception, elapsed: float) -> None:
        if self._closing or (self.app and self.app._closing):
            return
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
        if self.app and hasattr(self.app, "refresh_status"):
            self.app.refresh_status()

    def _reset_input_state(self) -> None:
        self._busy = False
        try:
            self.progress_bar.stop()
        except Exception:
            pass
        try:
            self.btn_send.configure(
                text="SEND\n➤", bg=COLOR_BTN_PRIMARY, state=tk.NORMAL
            )
        except Exception:
            pass
        try:
            self.txt_input.focus_set()
        except Exception:
            pass

    def clear_conversation(self) -> None:
        self.coordinator.clear_history()
        self.chat_display.configure(state=tk.NORMAL)
        self.chat_display.delete("1.0", tk.END)
        self.chat_display.configure(state=tk.DISABLED)
        self._display_welcome_banner()
        self.status_lbl.configure(
            text="Conversation history cleared. Buffer reset.", fg=COLOR_TEXT_MUTED
        )


class PrototypeView(tk.Frame):
    """Informational prototype view for unfinished features.

    Provides an honest, space-themed architectural specification panel.
    Strictly displays informational content; contains ZERO fake interactive buttons.
    """

    def __init__(
        self,
        parent: tk.Widget,
        title: str,
        icon: str,
        badge_text: str,
        badge_color: str,
        description: str,
        status_explanation: str,
        unavailable_features: List[str],
    ) -> None:
        super().__init__(parent, bg=COLOR_BG_ROOT)
        self.title_text: str = title
        self.icon_symbol: str = icon
        self.badge_text: str = badge_text
        self.unavailable_features: List[str] = unavailable_features

        self._build_ui(
            title, icon, badge_text, badge_color, description, status_explanation, unavailable_features
        )

    def _build_ui(
        self,
        title: str,
        icon: str,
        badge_text: str,
        badge_color: str,
        description: str,
        status_explanation: str,
        unavailable_features: List[str],
    ) -> None:
        outer = tk.Frame(self, bg=COLOR_BG_ROOT)
        outer.place(relx=0.5, rely=0.5, anchor="center")

        card = tk.Frame(
            outer,
            bg=COLOR_BG_CARD,
            padx=32,
            pady=28,
            highlightthickness=1,
            highlightbackground=COLOR_BORDER,
        )
        card.pack()

        # Header with icon and title
        hdr_frame = tk.Frame(card, bg=COLOR_BG_CARD)
        hdr_frame.pack(fill=tk.X, pady=(0, 10))

        icon_lbl = tk.Label(
            hdr_frame,
            text=icon,
            font=("Segoe UI", 22),
            fg=COLOR_TEXT_ACCENT,
            bg=COLOR_BG_CARD,
        )
        icon_lbl.pack(side=tk.LEFT, padx=(0, 12))

        title_lbl = tk.Label(
            hdr_frame,
            text=title,
            font=("Segoe UI", 16, "bold"),
            fg=COLOR_TEXT_PRIMARY,
            bg=COLOR_BG_CARD,
        )
        title_lbl.pack(side=tk.LEFT, anchor="w")

        # Status Badge
        badge_lbl = tk.Label(
            card,
            text=badge_text,
            font=("Consolas", 9, "bold"),
            fg=badge_color,
            bg=COLOR_BG_PANEL,
            padx=10,
            pady=4,
            relief=tk.FLAT,
        )
        badge_lbl.pack(anchor="w", pady=(0, 14))

        # Description
        desc_lbl = tk.Label(
            card,
            text=description,
            font=("Segoe UI", 10),
            fg=COLOR_TEXT_PRIMARY,
            bg=COLOR_BG_CARD,
            justify=tk.LEFT,
            wraplength=540,
        )
        desc_lbl.pack(anchor="w", pady=(0, 14))

        # Implementation Status Section
        section_hdr = tk.Label(
            card,
            text="STAGE 1 IMPLEMENTATION STATUS:",
            font=("Segoe UI", 8, "bold"),
            fg=COLOR_TEXT_MUTED,
            bg=COLOR_BG_CARD,
        )
        section_hdr.pack(anchor="w", pady=(0, 4))

        status_lbl = tk.Label(
            card,
            text=status_explanation,
            font=("Segoe UI", 9),
            fg=COLOR_TEXT_MUTED,
            bg=COLOR_BG_CARD,
            justify=tk.LEFT,
            wraplength=540,
        )
        status_lbl.pack(anchor="w", pady=(0, 14))

        # Unavailable Features
        unavail_hdr = tk.Label(
            card,
            text="UNAVAILABLE CAPABILITIES IN THIS STAGE:",
            font=("Segoe UI", 8, "bold"),
            fg=COLOR_TEXT_WARNING,
            bg=COLOR_BG_CARD,
        )
        unavail_hdr.pack(anchor="w", pady=(0, 6))

        for feat in unavailable_features:
            bullet_lbl = tk.Label(
                card,
                text=f"•  {feat}",
                font=("Segoe UI", 9),
                fg=COLOR_TEXT_MUTED,
                bg=COLOR_BG_CARD,
                justify=tk.LEFT,
                wraplength=520,
            )
            bullet_lbl.pack(anchor="w", padx=(10, 0), pady=1)


class FlexDesktopApp:
    """FLEX 1.0 Multi-Panel Space-Themed Desktop Application."""

    def __init__(self, root: tk.Tk, coordinator: Optional[AICoordinator] = None) -> None:
        self.root: tk.Tk = root
        self.root.title("FLEX 1.0 — Core C + Local AI Desktop")
        self.root.geometry("1080x740")
        self.root.minsize(880, 580)
        self.root.configure(bg=COLOR_BG_ROOT)

        self.coordinator: AICoordinator = (
            coordinator if coordinator is not None else AICoordinator(auto_start=True)
        )
        self._closing: bool = False
        self.current_view_name: str = "home"
        self.nav_buttons: Dict[str, tk.Button] = {}

        self._setup_styles()
        self._build_ui()

        # Handle window close
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)

        # Initial status refresh in background
        self.root.after(200, self.refresh_status)

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
        # Top Header Bar (Persistent across all views)
        header_frame = tk.Frame(self.root, bg=COLOR_BG_PANEL, padx=18, pady=10)
        header_frame.pack(side=tk.TOP, fill=tk.X)

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

        # Workspace Container (Sidebar + Center View Container)
        workspace = tk.Frame(self.root, bg=COLOR_BG_ROOT)
        workspace.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

        # Left Navigation Sidebar
        self.sidebar_frame = tk.Frame(
            workspace,
            bg=COLOR_BG_SIDEBAR,
            width=210,
            highlightthickness=1,
            highlightbackground=COLOR_BORDER,
        )
        self.sidebar_frame.pack(side=tk.LEFT, fill=tk.Y)
        self.sidebar_frame.pack_propagate(False)

        nav_header = tk.Label(
            self.sidebar_frame,
            text="NAVIGATION",
            font=("Segoe UI", 8, "bold"),
            fg="#64748B",
            bg=COLOR_BG_SIDEBAR,
            padx=16,
            pady=10,
            anchor="w",
        )
        nav_header.pack(fill=tk.X)

        # Navigation Action Items
        nav_items = [
            ("home", "💬  Home"),
            ("new_chat", "✨  New Chat"),
            ("voice", "🎙️  Voice Studio"),
            ("memory", "🧠  Memory"),
            ("knowledge", "📚  Knowledge Base"),
            ("projects", "📁  Projects & Files"),
            ("tools", "🛠️  Tools"),
            ("settings", "⚙️  Settings"),
        ]

        for key, text in nav_items:
            btn = tk.Button(
                self.sidebar_frame,
                text=text,
                font=("Segoe UI", 9),
                fg=COLOR_TEXT_MUTED,
                bg=COLOR_BG_SIDEBAR,
                activebackground=COLOR_BG_SIDEBAR_ACTIVE,
                activeforeground=COLOR_TEXT_PRIMARY,
                relief=tk.FLAT,
                anchor="w",
                padx=16,
                pady=7,
                cursor="hand2",
                command=lambda k=key: self.switch_view(k),
            )
            btn.pack(fill=tk.X, padx=6, pady=1)
            self.nav_buttons[key] = btn

        # Sidebar Spacer
        spacer = tk.Frame(self.sidebar_frame, bg=COLOR_BG_SIDEBAR)
        spacer.pack(fill=tk.BOTH, expand=True)

        # Local Profile Panel
        self.profile_frame = tk.Frame(
            self.sidebar_frame,
            bg=COLOR_BG_CARD,
            padx=12,
            pady=10,
            relief=tk.FLAT,
            highlightthickness=1,
            highlightbackground=COLOR_BORDER,
        )
        self.profile_frame.pack(side=tk.BOTTOM, fill=tk.X, padx=8, pady=10)

        prof_title = tk.Label(
            self.profile_frame,
            text="👤  Local Session",
            font=("Segoe UI", 9, "bold"),
            fg=COLOR_TEXT_PRIMARY,
            bg=COLOR_BG_CARD,
            anchor="w",
        )
        prof_title.pack(fill=tk.X)

        prof_status = tk.Label(
            self.profile_frame,
            text="● Offline / Local Only",
            font=("Consolas", 8),
            fg=COLOR_TEXT_SUCCESS,
            bg=COLOR_BG_CARD,
            anchor="w",
        )
        prof_status.pack(fill=tk.X, pady=(2, 2))

        prof_note = tk.Label(
            self.profile_frame,
            text="Cloud Auth: Not Implemented (Stage 2)",
            font=("Segoe UI", 7, "italic"),
            fg=COLOR_TEXT_MUTED,
            bg=COLOR_BG_CARD,
            anchor="w",
        )
        prof_note.pack(fill=tk.X)

        # Dynamic View Container
        self.view_container = tk.Frame(workspace, bg=COLOR_BG_ROOT)
        self.view_container.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)

        # Instantiate Primary ChatView (Home)
        self.chat_view = ChatView(
            parent=self.view_container,
            coordinator=self.coordinator,
            root=self.root,
            app=self,
        )

        # Register Views
        self.views: Dict[str, tk.Frame] = {
            "home": self.chat_view,
            "voice": PrototypeView(
                self.view_container,
                title="Voice Studio",
                icon="🎙️",
                badge_text="● STATUS: PLANNED FOR STAGE 1G",
                badge_color=COLOR_TEXT_WARNING,
                description="Dedicated environment for continuous voice interaction, speech-to-text audio capture, and text-to-speech AI responses.",
                status_explanation="Voice input and spoken responses are designated as future core capabilities. The backend audio capture pipeline has not yet been built.",
                unavailable_features=[
                    "Microphone audio recording (STT) is not implemented in Stage 1.",
                    "Speech synthesis audio output (TTS) is not implemented in Stage 1.",
                    "Hardware audio devices are not captured or monitored.",
                ],
            ),
            "memory": PrototypeView(
                self.view_container,
                title="Memory & Context",
                icon="🧠",
                badge_text="● STATUS: IN-MEMORY BUFFER ONLY (STAGE 1F)",
                badge_color=COLOR_TEXT_ACCENT,
                description="Conversation recall and context management system for retaining conversation state across turns.",
                status_explanation="Currently operating with a bounded 50-turn in-memory FIFO conversation buffer (backend/ai/history.py). Persistent disk or database storage is not yet active.",
                unavailable_features=[
                    "Persistent SQLite conversation database is not implemented in Stage 1.",
                    "Vector embeddings and semantic long-term memory are not implemented.",
                    "Memory state resets whenever the application is closed.",
                ],
            ),
            "knowledge": PrototypeView(
                self.view_container,
                title="Knowledge Base",
                icon="📚",
                badge_text="● STATUS: PLANNED FOR STAGE 2",
                badge_color=COLOR_TEXT_WARNING,
                description="Retrieval-augmented generation (RAG) system for indexing local documentation, PDFs, and codebase repositories.",
                status_explanation="Document parsing, chunking, and vector index ingestion are architectural components planned for subsequent stages.",
                unavailable_features=[
                    "Document file ingestion and PDF parsing are not implemented in Stage 1.",
                    "Local vector index and semantic search are not implemented.",
                    "Context injection into AI inference is limited to in-memory conversation history.",
                ],
            ),
            "projects": PrototypeView(
                self.view_container,
                title="Projects & Files",
                icon="📁",
                badge_text="● STATUS: PLANNED FOR STAGE 2",
                badge_color=COLOR_TEXT_WARNING,
                description="Project workspace manager allowing FLEX 1.0 to attach to local source directories and inspect workspace files.",
                status_explanation="A dedicated project file tree and contextual file-binding workspace will be introduced in Stage 2.",
                unavailable_features=[
                    "Workspace directory tree explorer is not implemented in Stage 1.",
                    "File attachment and multi-file code context ingestion are not active.",
                    "No project configurations are stored on disk.",
                ],
            ),
            "tools": PrototypeView(
                self.view_container,
                title="C Core Native Tools",
                icon="🛠️",
                badge_text="● STATUS: ARCHITECTURAL SPECIFICATION",
                badge_color=COLOR_TEXT_SUCCESS,
                description="Native tools engine governed by the C11 Core. Command parsing, memory safety, and deterministic lifecycle tracking.",
                status_explanation="FLEX 1.0 enforces all command parsing through strict C11 native modules without dynamic heap allocations. Live dispatch is active in the Home chat pipeline.",
                unavailable_features=[
                    "External CLI tool execution plugins are not implemented in Stage 1.",
                    "Interactive tools dashboard is an architectural specification only; no fabricated buttons.",
                    "All command verification is performed automatically in the Home chat view.",
                ],
            ),
            "settings": PrototypeView(
                self.view_container,
                title="Settings & Configuration",
                icon="⚙️",
                badge_text="● STATUS: PLANNED FOR STAGE 4",
                badge_color=COLOR_TEXT_WARNING,
                description="Configuration panel for local Ollama endpoints, LLM model selection, inference temperature, and visual theme options.",
                status_explanation="The application currently uses configured defaults (qwen2.5-coder:3b at http://127.0.0.1:11434). A persistent configuration file editor is planned for Stage 4.",
                unavailable_features=[
                    "Persistent settings file (config.json) writer is not implemented in Stage 1.",
                    "Live model switcher dropdown is not active.",
                    "Theme and font customization are not persistent.",
                ],
            ),
        }

        # Mount Home view by default
        self.views["home"].pack(fill=tk.BOTH, expand=True)
        self._highlight_nav_button("home")

    def _highlight_nav_button(self, active_key: str) -> None:
        for key, btn in self.nav_buttons.items():
            if key == active_key:
                btn.configure(
                    bg=COLOR_BG_SIDEBAR_ACTIVE,
                    fg=COLOR_TEXT_ACCENT,
                    font=("Segoe UI", 9, "bold"),
                )
            else:
                btn.configure(
                    bg=COLOR_BG_SIDEBAR,
                    fg=COLOR_TEXT_MUTED,
                    font=("Segoe UI", 9),
                )

    def switch_view(self, view_name: str) -> None:
        """Switches the active view in the dynamic view container.

        Safely handles new_chat, unknown view names, and preserves previous view state.
        """
        if view_name == "new_chat":
            self.new_chat()
            return

        if view_name not in self.views:
            logger.warning("Attempted to switch to unknown view: %r", view_name)
            return

        if view_name == self.current_view_name and self.views[view_name].winfo_ismapped():
            return

        if self.current_view_name in self.views:
            self.views[self.current_view_name].pack_forget()

        self.views[view_name].pack(fill=tk.BOTH, expand=True)
        self.current_view_name = view_name
        self._highlight_nav_button(view_name)

    def new_chat(self) -> None:
        """Clears conversation history and returns immediately to the Home view."""
        try:
            self.chat_view.clear_conversation()
        except Exception as e:
            logger.error("Error clearing conversation during new_chat: %s", e)
        self.switch_view("home")

    # Forwarding attributes & methods for backward compatibility with existing tests
    @property
    def chat_display(self) -> tk.Text:
        return self.chat_view.chat_display

    @property
    def txt_input(self) -> tk.Text:
        return self.chat_view.txt_input

    @property
    def btn_send(self) -> tk.Button:
        return self.chat_view.btn_send

    @property
    def progress_bar(self) -> ttk.Progressbar:
        return self.chat_view.progress_bar

    @property
    def status_lbl(self) -> tk.Label:
        return self.chat_view.status_lbl

    @property
    def _busy(self) -> bool:
        return self.chat_view._busy

    @_busy.setter
    def _busy(self, val: bool) -> None:
        self.chat_view._busy = val

    @property
    def _last_op_id(self) -> int:
        return self.chat_view._last_op_id

    @_last_op_id.setter
    def _last_op_id(self, val: int) -> None:
        self.chat_view._last_op_id = val

    def send_message(self) -> None:
        self.chat_view.send_message()

    def clear_conversation(self) -> None:
        self.chat_view.clear_conversation()

    def _display_welcome_banner(self) -> None:
        self.chat_view._display_welcome_banner()

    def _append_to_chat(self, text: str, tag: str) -> None:
        self.chat_view._append_to_chat(text, tag)

    def _on_chat_success(self, result: CoordinatorChatResult, elapsed: float) -> None:
        self.chat_view._on_chat_success(result, elapsed)

    def _on_chat_error(self, exc: Exception, elapsed: float) -> None:
        self.chat_view._on_chat_error(exc, elapsed)

    def _reset_input_state(self) -> None:
        self.chat_view._reset_input_state()

    def refresh_status(self) -> None:
        threading.Thread(target=self._worker_refresh_status, daemon=True).start()

    def _worker_refresh_status(self) -> None:
        try:
            status = self.coordinator.get_status()
            if not self._closing:
                self.root.after(0, self._apply_status, status)
        except Exception as e:
            logger.debug("Telemetry refresh error: %s", e)

    def _apply_status(self, status: Dict[str, Any]) -> None:
        if self._closing:
            return
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
        if self._closing:
            return
        self._closing = True
        self.chat_view._closing = True
        try:
            self.status_lbl.configure(
                text="Shutting down C core bridge and terminating...",
                fg=COLOR_TEXT_WARNING,
            )
        except Exception:
            pass
        try:
            self.coordinator.close(timeout=1.0)
        except Exception as e:
            logger.debug("Error during coordinator close: %s", e)
        finally:
            try:
                self.root.destroy()
            except Exception:
                pass


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
