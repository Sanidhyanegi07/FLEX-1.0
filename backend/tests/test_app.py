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

    def test_sidebar_construction_and_actions(self) -> None:
        """Verifies that the navigation sidebar is constructed with all 8 expected actions."""
        expected_keys = [
            "home",
            "new_chat",
            "voice",
            "memory",
            "knowledge",
            "projects",
            "tools",
            "settings",
        ]
        self.assertIsNotNone(self.app.sidebar_frame)
        for key in expected_keys:
            self.assertIn(key, self.app.nav_buttons)
            btn = self.app.nav_buttons[key]
            self.assertIsInstance(btn, tk.Button)
            self.assertTrue(btn.winfo_exists())

    def test_initial_active_view_is_home(self) -> None:
        """Verifies that Home (ChatView) is the initial active view on startup."""
        self.assertEqual(self.app.current_view_name, "home")
        self.assertIn("home", self.app.views)
        self.assertIs(self.app.views["home"], self.app.chat_view)

    def test_switch_view_to_prototype(self) -> None:
        """Verifies switching from Home to a prototype view (e.g. Voice Studio)."""
        self.app.switch_view("voice")
        self.assertEqual(self.app.current_view_name, "voice")
        self.assertTrue(self.app.views["voice"].winfo_exists())

    def test_switch_view_back_to_home_preserves_content(self) -> None:
        """Verifies switching away from Home and returning preserves the conversation transcript."""
        # Insert unique text into transcript
        self.app.chat_display.configure(state=tk.NORMAL)
        self.app.chat_display.insert(tk.END, "Preserved session turn 12345\n")
        self.app.chat_display.configure(state=tk.DISABLED)

        # Switch to memory prototype
        self.app.switch_view("memory")
        self.assertEqual(self.app.current_view_name, "memory")

        # Switch back to home
        self.app.switch_view("home")
        self.assertEqual(self.app.current_view_name, "home")

        # Transcript must be completely preserved
        content = self.app.chat_display.get("1.0", tk.END)
        self.assertIn("Preserved session turn 12345", content)

    def test_new_chat_clears_conversation_and_returns_home(self) -> None:
        """Verifies that New Chat clears conversation history and returns immediately to Home."""
        # Start in settings prototype
        self.app.switch_view("settings")
        self.assertEqual(self.app.current_view_name, "settings")

        # Trigger new chat via sidebar action
        self.app.switch_view("new_chat")

        self.mock_coordinator.clear_history.assert_called_once()
        self.assertEqual(self.app.current_view_name, "home")

    def test_single_coordinator_instance(self) -> None:
        """Verifies that one shared coordinator instance is used across the shell and ChatView."""
        self.assertIs(self.app.coordinator, self.mock_coordinator)
        self.assertIs(self.app.chat_view.coordinator, self.mock_coordinator)

    def test_prototype_views_have_no_fake_interactive_controls(self) -> None:
        """Verifies that prototype views contain no fake interactive buttons or inputs."""
        prototype_keys = ["voice", "memory", "knowledge", "projects", "tools", "settings"]

        def check_widget_for_fake_controls(widget: tk.Widget) -> None:
            self.assertNotIsInstance(
                widget,
                (tk.Button, tk.Entry),
                f"Found unexpected interactive control {widget} in prototype view",
            )
            for child in widget.winfo_children():
                check_widget_for_fake_controls(child)

        for key in prototype_keys:
            self.assertIn(key, self.app.views)
            view = self.app.views[key]
            check_widget_for_fake_controls(view)

    def test_local_profile_displays_offline_status(self) -> None:
        """Verifies the Local Profile panel accurately and honestly displays offline status."""
        self.assertIsNotNone(self.app.profile_frame)
        labels = [
            child.cget("text")
            for child in self.app.profile_frame.winfo_children()
            if isinstance(child, tk.Label)
        ]
        combined = " ".join(labels)
        self.assertIn("Offline", combined)
        self.assertIn("Not Implemented", combined)

    def test_closing_from_prototype_view(self) -> None:
        """Verifies safe shutdown when closing while viewing a prototype panel."""
        self.app.switch_view("voice")
        self.assertEqual(self.app.current_view_name, "voice")

        self.app.on_closing()

        self.assertTrue(self.app._closing)
        self.assertTrue(self.app.chat_view._closing)
        self.mock_coordinator.close.assert_called_once()

    def test_unknown_view_name_handled_safely(self) -> None:
        """Verifies that passing an unknown view name fails safely without corrupting layout."""
        initial_view = self.app.current_view_name
        self.app.switch_view("non_existent_future_panel_99")
        self.assertEqual(self.app.current_view_name, initial_view)

    def test_repeated_view_switching_does_not_accumulate_widgets(self) -> None:
        """Verifies that switching between views repeatedly does not create duplicate widgets."""
        initial_child_count = len(self.app.view_container.winfo_children())

        for _ in range(5):
            self.app.switch_view("voice")
            self.app.switch_view("memory")
            self.app.switch_view("tools")
            self.app.switch_view("home")

        final_child_count = len(self.app.view_container.winfo_children())
        self.assertEqual(initial_child_count, final_child_count)


if __name__ == "__main__":
    unittest.main()
