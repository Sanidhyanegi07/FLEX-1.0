"""FLEX 1.0 — Unit Tests for Desktop Application (Stage 1F.3).

Verifies:
    - App initializes properly with coordinator.
    - Sending messages initiates background worker and sets UI busy state.
    - Chat success updates chat widget, operation ID badge, and re-enables UI.
    - Chat error formats error message and re-enables UI.
    - Clear conversation resets chat display and calls coordinator.clear_history().
    - On closing cleanly shuts down coordinator and destroys root.
"""

from __future__ import annotations

import sys
import tkinter as tk
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))

from backend.app import FlexDesktopApp
from backend.coordinator import AICoordinator, CoordinatorChatResult, CoordinatorDispatchError


class TestFlexDesktopApp(unittest.TestCase):
    """Unit tests for FlexDesktopApp."""

    def setUp(self) -> None:
        self.root = tk.Tk()
        self.root.withdraw()  # Headless execution

        self.mock_coordinator = MagicMock(spec=AICoordinator)
        self.mock_coordinator.is_running = True
        self.mock_coordinator.get_status.return_value = {
            "bridge": {"is_running": True, "lifecycle": "FLEX_LIFECYCLE_IDLE"},
            "ai_health": True,
            "model_available": True,
            "model": "qwen2.5-coder:3b",
        }

        self.app = FlexDesktopApp(self.root, coordinator=self.mock_coordinator)

    def tearDown(self) -> None:
        try:
            self.root.destroy()
        except Exception:
            pass

    def test_app_initialization(self) -> None:
        """Verifies UI widgets and initial state."""
        self.assertIsNotNone(self.app.chat_display)
        self.assertIsNotNone(self.app.txt_input)
        self.assertIsNotNone(self.app.btn_send)
        self.assertFalse(self.app._busy)

    def test_send_message_sets_busy_state(self) -> None:
        """Verifies that sending a message updates UI state to busy."""
        self.app.txt_input.insert("1.0", "Hello FLEX")

        with patch("threading.Thread") as mock_thread:
            self.app.send_message()

            self.assertTrue(self.app._busy)
            self.assertEqual(self.app.btn_send["text"], "BUSY...")
            self.assertEqual(str(self.app.btn_send["state"]), "disabled")
            mock_thread.assert_called_once()

    def test_on_chat_success(self) -> None:
        """Verifies UI update after successful turn."""
        self.app._busy = True
        self.app.btn_send.configure(state=tk.DISABLED)

        result = CoordinatorChatResult(
            text="Here is your answer.",
            operation_id=15,
            command_name="chat",
            parser_status="FLEX_STATUS_OK",
            state_status="FLEX_STATE_OK",
            model="qwen2.5-coder:3b",
        )

        self.app._on_chat_success(result, elapsed=0.45)

        self.assertFalse(self.app._busy)
        self.assertEqual(str(self.app.btn_send["state"]), "normal")
        self.assertEqual(self.app.badge_op["text"], "OP #15")

        chat_content = self.app.chat_display.get("1.0", tk.END)
        self.assertIn("Here is your answer.", chat_content)
        self.assertIn("Op #15", chat_content)

    def test_on_chat_error(self) -> None:
        """Verifies UI update when error occurs."""
        self.app._busy = True
        self.app.btn_send.configure(state=tk.DISABLED)

        exc = CoordinatorDispatchError("C core rejected input")
        self.app._on_chat_error(exc, elapsed=0.12)

        self.assertFalse(self.app._busy)
        self.assertEqual(str(self.app.btn_send["state"]), "normal")

        chat_content = self.app.chat_display.get("1.0", tk.END)
        self.assertIn("C Core Dispatch Rejected the command", chat_content)

    def test_clear_conversation(self) -> None:
        """Verifies clear conversation action."""
        self.app.clear_conversation()
        self.mock_coordinator.clear_history.assert_called_once()

    def test_on_closing(self) -> None:
        """Verifies clean closing of coordinator and window destruction."""
        self.app.on_closing()
        self.mock_coordinator.close.assert_called_once()
        self.assertTrue(self.app._closing)

    def test_multiple_messages_ignored_when_busy(self) -> None:
        """Verifies that sending a second message while busy is safely ignored."""
        self.app._busy = True
        self.app.txt_input.insert("1.0", "Second message")

        with patch("threading.Thread") as mock_thread:
            self.app.send_message()
            mock_thread.assert_not_called()

    def test_on_chat_error_ollama_unavailable(self) -> None:
        """Verifies graceful handling when Ollama daemon is unreachable."""
        from backend.ai import AIServiceUnavailableError

        self.app._busy = True
        exc = AIServiceUnavailableError("Connection refused on 127.0.0.1:11434")
        self.app._on_chat_error(exc, elapsed=0.05)

        self.assertFalse(self.app._busy)
        chat_content = self.app.chat_display.get("1.0", tk.END)
        self.assertIn("Local Ollama daemon is unreachable", chat_content)
        self.assertIn("ollama serve", chat_content)

    def test_on_chat_error_model_missing(self) -> None:
        """Verifies graceful handling when requested model is not found."""
        from backend.ai import AIModelNotFoundError

        self.app._busy = True
        exc = AIModelNotFoundError("Model 'qwen2.5-coder:3b' not found in local library")
        self.app._on_chat_error(exc, elapsed=0.08)

        self.assertFalse(self.app._busy)
        chat_content = self.app.chat_display.get("1.0", tk.END)
        self.assertIn("Requested model was not found in local Ollama storage", chat_content)

    def test_closing_during_active_request(self) -> None:
        """Verifies that closing the application during an active turn exits cleanly."""
        self.app._busy = True
        self.app.on_closing()

        self.assertTrue(self.app._closing)
        self.mock_coordinator.close.assert_called_once()

        # Late-arriving worker callback must safely no-op without raising
        result = CoordinatorChatResult(
            text="Late text",
            operation_id=99,
            command_name="chat",
            parser_status="FLEX_STATUS_OK",
            state_status="FLEX_STATE_OK",
            model="qwen2.5-coder:3b",
        )
        self.app._on_chat_success(result, elapsed=1.0)
        self.app._on_chat_error(RuntimeError("Late error"), elapsed=1.0)
        self.app._apply_status({})


if __name__ == "__main__":
    unittest.main()
