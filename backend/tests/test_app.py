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

import os
import shutil
import sys
import tempfile
import tkinter as tk
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))

from backend.ai.storage import SQLiteConversationStorage
from backend.app import AICoreVisualizer, DiagnosticsDrawer, FlexDesktopApp, MemoryView
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
        # Note: memory is no longer a prototype view in Stage 2C; it is a functional MemoryView.
        prototype_keys = ["voice", "knowledge", "projects", "tools", "settings"]

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

    def test_ai_core_initialization(self) -> None:
        """Verifies that the AI Core visualizer is initialized with IDLE state."""
        self.assertIsNotNone(self.app.ai_core)
        self.assertIsInstance(self.app.ai_core, AICoreVisualizer)
        self.assertEqual(self.app.ai_core.state, AICoreVisualizer.STATE_IDLE)
        self.assertTrue(self.app.ai_core._is_active)

    def test_ai_core_attentive_on_typing(self) -> None:
        """Verifies that typing triggers ATTENTIVE state and schedules idle reset."""
        self.app.chat_view.ai_core.on_user_typing()
        self.assertEqual(self.app.ai_core.state, AICoreVisualizer.STATE_ATTENTIVE)
        self.assertIsNotNone(self.app.chat_view.ai_core._attentive_reset_timer)

        # Triggering timeout handler returns to IDLE
        self.app.chat_view.ai_core._on_attentive_timeout()
        self.assertEqual(self.app.ai_core.state, AICoreVisualizer.STATE_IDLE)

    def test_ai_core_thinking_on_send_message(self) -> None:
        """Verifies that sending a message transitions the AI Core to THINKING state."""
        self.app.txt_input.insert("1.0", "Execute test command")
        with patch("threading.Thread"):
            self.app.send_message()
            self.assertEqual(self.app.ai_core.state, AICoreVisualizer.STATE_THINKING)

    def test_ai_core_responding_on_chat_success(self) -> None:
        """Verifies that a successful response transitions the AI Core to RESPONDING."""
        self.app._busy = True
        self.app.ai_core.set_state(AICoreVisualizer.STATE_THINKING)

        result = CoordinatorChatResult(
            text="AI answer text",
            operation_id=42,
            command_name="chat",
            parser_status="FLEX_STATUS_OK",
            state_status="FLEX_STATE_OK",
            model="qwen2.5-coder:3b",
        )
        self.app._on_chat_success(result, elapsed=0.35)

        self.assertEqual(self.app.ai_core.state, AICoreVisualizer.STATE_RESPONDING)
        self.assertTrue(self.app.ai_core._ripple_active)

        # Timeout reverts to IDLE
        self.app.ai_core._on_responding_timeout()
        self.assertEqual(self.app.ai_core.state, AICoreVisualizer.STATE_IDLE)

    def test_ai_core_error_on_chat_error(self) -> None:
        """Verifies that an error callback transitions the AI Core to ERROR."""
        self.app._busy = True
        self.app.ai_core.set_state(AICoreVisualizer.STATE_THINKING)

        exc = CoordinatorDispatchError("Input rejected by C parser")
        self.app._on_chat_error(exc, elapsed=0.10)

        self.assertEqual(self.app.ai_core.state, AICoreVisualizer.STATE_ERROR)

        # Timeout reverts to IDLE
        self.app.ai_core._on_error_timeout()
        self.assertEqual(self.app.ai_core.state, AICoreVisualizer.STATE_IDLE)

    def test_ai_core_idle_on_clear_conversation(self) -> None:
        """Verifies that clearing conversation resets the AI Core to IDLE."""
        self.app.ai_core.set_state(AICoreVisualizer.STATE_ERROR)
        self.app.clear_conversation()
        self.assertEqual(self.app.ai_core.state, AICoreVisualizer.STATE_IDLE)

    def test_ai_core_animation_pause_and_resume_on_view_switch(self) -> None:
        """Verifies that switching away from Home pauses animation, and returning resumes."""
        self.assertTrue(self.app.ai_core._is_active)

        # Switch to Voice Studio
        self.app.switch_view("voice")
        self.assertFalse(self.app.ai_core._is_active)
        self.assertIsNone(self.app.ai_core._anim_timer_id)

        # Switch back to Home
        self.app.switch_view("home")
        self.assertTrue(self.app.ai_core._is_active)
        self.assertIsNotNone(self.app.ai_core._anim_timer_id)

    def test_ai_core_shutdown_cancels_all_timers(self) -> None:
        """Verifies that application shutdown cancels all animation and state timers."""
        self.app.ai_core.on_user_typing()
        self.app.on_closing()

        self.assertTrue(self.app.ai_core._closing)
        self.assertIsNone(self.app.ai_core._anim_timer_id)
        self.assertIsNone(self.app.ai_core._attentive_reset_timer)

    def test_no_duplicate_animation_loops_on_repeated_switching(self) -> None:
        """Verifies that rapid view switching never creates multiple concurrent timer loops."""
        for _ in range(8):
            self.app.switch_view("voice")
            self.app.switch_view("home")

        self.assertTrue(self.app.ai_core._is_active)
        timer_id = self.app.ai_core._anim_timer_id
        self.assertIsNotNone(timer_id)

    def test_ai_core_safe_when_unmapped_or_closing(self) -> None:
        """Verifies that _animate safely no-ops without exception when unmapped or closing."""
        self.app.ai_core._closing = True
        self.app.ai_core._animate()
        self.assertIsNone(self.app.ai_core._anim_timer_id)

    # ── Stage 2B: System Diagnostics Drawer Tests ────────────────────────────

    def test_diagnostics_drawer_initialization(self) -> None:
        """Verifies that the diagnostics drawer is initialized and initially closed."""
        self.assertIsNotNone(self.app.diagnostics_drawer)
        self.assertIsInstance(self.app.diagnostics_drawer, DiagnosticsDrawer)
        self.assertFalse(self.app._diagnostics_open)
        self.assertFalse(self.app.diagnostics_drawer.winfo_ismapped())
        self.assertIn("ai", self.app.status_pills)
        self.assertIn("voice", self.app.status_pills)
        self.assertIn("knowledge", self.app.status_pills)

    def test_diagnostics_drawer_open_and_close(self) -> None:
        """Verifies drawer open, close, and toggle behavior and button label updates."""
        self.assertFalse(self.app._diagnostics_open)

        # Open drawer
        self.app.open_diagnostics_drawer()
        self.assertTrue(self.app._diagnostics_open)
        self.assertEqual(self.app.btn_toggle_diagnostics["text"], "▲ Close Diagnostics")

        # Close drawer
        self.app.close_diagnostics_drawer()
        self.assertFalse(self.app._diagnostics_open)
        self.assertEqual(self.app.btn_toggle_diagnostics["text"], "🔍 Diagnostics Drawer ▾")

        # Toggle drawer
        self.app.toggle_diagnostics_drawer()
        self.assertTrue(self.app._diagnostics_open)
        self.app.toggle_diagnostics_drawer()
        self.assertFalse(self.app._diagnostics_open)

    def test_calculate_overall_health_rules(self) -> None:
        """Verifies overall health calculation for all required and edge scenarios."""
        # Scenario 1: Nominal Healthy (bridge running, Ollama healthy, model exists, internet ok)
        healthy_diag = {
            "bridge_running": True,
            "bridge": {"lifecycle": "FLEX_LIFECYCLE_IDLE", "last_error": "FLEX_STATE_OK"},
            "ai_health": True,
            "model_available": True,
            "internet": True,
        }
        state, fg, bg = FlexDesktopApp.calculate_overall_health(healthy_diag)
        self.assertEqual(state, "HEALTHY")

        # Scenario 2: Local components healthy, but internet offline (local-first design)
        # Internet unavailability alone MUST NOT mark the local application degraded.
        offline_net = dict(healthy_diag, internet=False)
        state, fg, bg = FlexDesktopApp.calculate_overall_health(offline_net)
        self.assertEqual(state, "HEALTHY")

        # Scenario 3: Ollama is unavailable while C bridge is operational
        degraded_ollama = dict(healthy_diag, ai_health=False)
        state, _, _ = FlexDesktopApp.calculate_overall_health(degraded_ollama)
        self.assertEqual(state, "DEGRADED")

        # Scenario 4: Configured model is missing
        degraded_model = dict(healthy_diag, model_available=False)
        state, _, _ = FlexDesktopApp.calculate_overall_health(degraded_model)
        self.assertEqual(state, "DEGRADED")

        # Scenario 5: C bridge process has terminated
        offline_bridge = dict(healthy_diag, bridge_running=False)
        state, _, _ = FlexDesktopApp.calculate_overall_health(offline_bridge)
        self.assertEqual(state, "OFFLINE")

        # Scenario 6: Diagnostic information is incomplete or empty
        state_empty, _, _ = FlexDesktopApp.calculate_overall_health({})
        self.assertEqual(state_empty, "OFFLINE")

        # Scenario 7: C core state error
        degraded_c_err = dict(
            healthy_diag,
            bridge={"lifecycle": "FLEX_LIFECYCLE_ERROR", "last_error": "FLEX_STATE_INVALID"},
        )
        state, _, _ = FlexDesktopApp.calculate_overall_health(degraded_c_err)
        self.assertEqual(state, "DEGRADED")

        # Scenario 8: Missing optional features (voice, knowledge base, persistent DB)
        # Unimplemented future capabilities must NOT degrade the operational core.
        optional_unimpl = dict(
            healthy_diag,
            voice_implemented=False,
            knowledge_base_connected=False,
            persistent_db=False,
        )
        state_opt, _, _ = FlexDesktopApp.calculate_overall_health(optional_unimpl)
        self.assertEqual(state_opt, "HEALTHY")

    def test_subsystem_status_mapping_and_honest_indicators(self) -> None:
        """Verifies that right sidebar pills accurately report honest statuses."""
        diag = {
            "bridge_running": True,
            "bridge": {"lifecycle": "FLEX_LIFECYCLE_IDLE", "last_error": "FLEX_STATE_OK"},
            "bridge_pid": 4321,
            "ai_health": True,
            "model": "qwen2.5-coder:3b",
            "model_available": True,
            "history_count": 5,
            "history_max": 50,
            "internet": True,
            "health_state": "HEALTHY",
            "health_fg": "#34D399",
            "health_bg": "#064E3B",
        }
        self.app._apply_diagnostics(diag)

        # Check Voice pill is honestly NOT IMPLEMENTED (muted color, not green)
        voice_text = self.app.status_pills["voice"].cget("text")
        self.assertIn("Not Implemented", voice_text)
        self.assertNotEqual(str(self.app.status_pills["voice"].cget("fg")), "#34D399")

        # Check Knowledge Base pill is honestly PLANNED (muted color, not green)
        kb_text = self.app.status_pills["knowledge"].cget("text")
        self.assertIn("Planned", kb_text)
        self.assertNotEqual(str(self.app.status_pills["knowledge"].cget("fg")), "#34D399")

        # Check Memory pill reflects session buffer turn count
        mem_text = self.app.status_pills["memory"].cget("text")
        self.assertIn("Session Buffer (5/50)", mem_text)

        # Check AI Engine pill reflects model ready
        ai_text = self.app.status_pills["ai"].cget("text")
        self.assertIn("Local Model Ready", ai_text)

        # Check Overall Health badge
        health_text = self.app.badge_overall_health.cget("text")
        self.assertIn("HEALTHY", health_text)

    def test_diagnostics_drawer_telemetry_fields_update(self) -> None:
        """Verifies that the diagnostics drawer cards update correctly from live data."""
        self.app.open_diagnostics_drawer()

        diag = {
            "bridge_running": True,
            "bridge": {
                "lifecycle": "FLEX_LIFECYCLE_IDLE",
                "active_operation": "FLEX_OPERATION_NONE",
                "last_error": "FLEX_STATE_OK",
            },
            "bridge_pid": 9876,
            "ai_health": True,
            "model": "qwen2.5-coder:3b",
            "model_available": True,
            "is_inferring": False,
            "history_count": 8,
            "history_max": 50,
            "internet": True,
            "health_state": "HEALTHY",
            "health_fg": "#34D399",
            "health_bg": "#064E3B",
            "timestamp": "12:34:56",
        }
        self.app.diagnostics_drawer.update_diagnostics(diag)

        drawer = self.app.diagnostics_drawer
        self.assertIn("PID 9876", drawer.lbl_c_status.cget("text"))
        self.assertEqual(drawer.lbl_c_lifecycle.cget("text"), "FLEX_LIFECYCLE_IDLE")
        self.assertEqual(drawer.lbl_c_op.cget("text"), "FLEX_OPERATION_NONE")
        self.assertIn("ONLINE", drawer.lbl_ai_daemon.cget("text"))
        self.assertEqual(drawer.lbl_ai_model.cget("text"), "qwen2.5-coder:3b")
        self.assertIn("8 / 50", drawer.lbl_mem_buffer.cget("text"))
        self.assertIn("Online", drawer.lbl_net_internet.cget("text"))
        self.assertIn("NOT IMPLEMENTED", drawer.lbl_mod_voice.cget("text"))
        self.assertIn("NOT IMPLEMENTED", drawer.lbl_mod_kb.cget("text"))
        self.assertIn("HEALTHY", drawer.badge_health.cget("text"))
        self.assertIn("12:34:56", drawer.lbl_timestamp.cget("text"))

    def test_diagnostic_failures_and_missing_fields_handled_gracefully(self) -> None:
        """Verifies that empty, malformed, or incomplete diagnostic data do not raise exceptions."""
        # 1. Empty dict to _apply_diagnostics
        self.app._apply_diagnostics({})
        self.assertIn("OFFLINE", self.app.badge_overall_health.cget("text"))

        # 2. Empty dict to legacy _apply_status
        self.app._apply_status({})
        self.assertIsNotNone(self.app.last_diagnostics)

        # 3. None inputs safely ignored without exceptions
        self.app._apply_diagnostics(None)  # type: ignore[arg-type]
        self.app._apply_status(None)       # type: ignore[arg-type]
        state_none, _, _ = FlexDesktopApp.calculate_overall_health(None)  # type: ignore[arg-type]
        self.assertEqual(state_none, "OFFLINE")

        # 4. Malformed bridge=None in diagnostics
        malformed_bridge = {
            "bridge_running": True,
            "bridge": None,
            "ai_health": True,
            "model_available": True,
        }
        state_malformed, _, _ = FlexDesktopApp.calculate_overall_health(malformed_bridge)
        self.assertEqual(state_malformed, "OFFLINE")
        self.app._apply_diagnostics(malformed_bridge)
        self.app.diagnostics_drawer.update_diagnostics(malformed_bridge)

        # 5. Non-dict bridge string
        str_bridge = {"bridge_running": True, "bridge": "invalid_string"}
        state_str, _, _ = FlexDesktopApp.calculate_overall_health(str_bridge)
        self.assertEqual(state_str, "OFFLINE")
        self.app._apply_diagnostics(str_bridge)

    def test_no_conflicting_bridge_requests_during_active_chat(self) -> None:
        """Verifies that background telemetry refresh does not send bridge commands while locked."""
        mock_bridge = MagicMock()
        mock_bridge.is_running = True
        mock_bridge.pid = 5555
        mock_bridge._lifecycle = "FLEX_LIFECYCLE_PROCESSING"
        mock_bridge._active_operation = "FLEX_OPERATION_CHAT"

        self.mock_coordinator.bridge = mock_bridge
        self.mock_coordinator.is_running = True

        # Simulate lock held by active chat turn (mock lock acquire returns False)
        mock_lock = MagicMock()
        mock_lock.acquire.return_value = False
        orig_lock = self.mock_coordinator._lock
        self.mock_coordinator._lock = mock_lock
        try:
            with patch.object(self.app.root, "after") as mock_after:
                self.app._worker_refresh_status()

                # bridge.get_status() MUST NOT be called while lock is held
                mock_bridge.get_status.assert_not_called()
                mock_after.assert_called_once()
                args, _ = mock_after.call_args
                self.assertEqual(args[0], 0)
                self.assertEqual(args[1], self.app._apply_diagnostics)
                diag = args[2]
                self.assertTrue(diag["bridge"].get("busy", False))
                self.assertEqual(diag["bridge"]["active_operation"], "FLEX_OPERATION_CHAT")
        finally:
            self.mock_coordinator._lock = orig_lock

    def test_safe_refresh_scheduling_and_shutdown(self) -> None:
        """Verifies that recurring diagnostics polling timer is tracked and cancelled on close."""
        self.assertIsNotNone(self.app._diagnostics_poll_id)

        # Triggering scheduled poll re-arms timer when not closing
        with patch.object(self.app, "refresh_status"):
            self.app._schedule_status_poll()
            self.assertIsNotNone(self.app._diagnostics_poll_id)

        # Shutdown cancels timer
        self.app.on_closing()
        self.assertIsNone(self.app._diagnostics_poll_id)
        self.assertTrue(self.app._closing)


class TestFlexDesktopAppPersistence(unittest.TestCase):
    """Unit tests for persistent conversation memory in FlexDesktopApp."""

    def setUp(self) -> None:
        self.root = tk.Tk()
        self.root.withdraw()

        from backend.ai.storage import SQLiteConversationStorage
        from backend.ai.history import InMemoryConversationHistory

        self.storage = SQLiteConversationStorage(db_path=":memory:")
        self.mock_coordinator = MagicMock(spec=AICoordinator)
        self.mock_coordinator.is_running = True
        self.mock_coordinator.storage = self.storage
        self.mock_coordinator.history = InMemoryConversationHistory()
        self.mock_coordinator.active_conversation_id = None
        self.mock_coordinator.get_status.return_value = {
            "bridge": {"lifecycle": "FLEX_LIFECYCLE_IDLE"},
            "ai_health": True,
            "model_available": True,
            "model": "qwen2.5-coder:3b",
            "history_count": 0,
            "storage_enabled": True,
            "storage_degraded": False,
            "storage_status": "PERSISTED",
        }

    def tearDown(self) -> None:
        if hasattr(self, "app") and not self.app._closing:
            self.app.on_closing()
        elif hasattr(self, "root"):
            try:
                self.root.destroy()
            except Exception:
                pass
        if hasattr(self, "storage") and self.storage is not None:
            self.storage.close()

    def test_startup_with_empty_database(self) -> None:
        """Verifies startup with empty database initializes cleanly and displays empty state."""
        self.app = FlexDesktopApp(self.root, coordinator=self.mock_coordinator, storage=self.storage)
        self.assertIsNotNone(self.app.recent_chats_container)
        self.assertEqual(len(self.app.recent_chat_labels), 0)

    def test_startup_restores_existing_conversation(self) -> None:
        """Verifies that on startup, messages from the active conversation are restored into chat_display."""
        cid = self.storage.create_conversation("Historical Chat")
        self.storage.save_message(cid, role="user", content="Past question")
        self.storage.save_message(
            cid, role="assistant", content="Past answer", model="qwen2.5-coder:3b"
        )
        self.mock_coordinator.active_conversation_id = cid

        self.app = FlexDesktopApp(self.root, coordinator=self.mock_coordinator, storage=self.storage)

        rendered_text = self.app.chat_display.get("1.0", tk.END)
        self.assertIn("Past question", rendered_text)
        self.assertIn("Past answer", rendered_text)
        self.assertIn("Restored Session", rendered_text)

    def test_switching_between_conversations_in_gui(self) -> None:
        """Verifies clicking or loading another conversation switches active transcript."""
        c1 = self.storage.create_conversation("Topic 1")
        self.storage.save_message(c1, role="user", content="Question 1")
        self.storage.save_message(c1, role="assistant", content="Answer 1")

        c2 = self.storage.create_conversation("Topic 2")
        self.storage.save_message(c2, role="user", content="Question 2")
        self.storage.save_message(c2, role="assistant", content="Answer 2")

        def mock_set_active(conv_id: str, sync_history: bool = True) -> None:
            self.mock_coordinator.active_conversation_id = conv_id

        self.mock_coordinator.set_active_conversation.side_effect = mock_set_active
        self.mock_coordinator.active_conversation_id = c1

        self.app = FlexDesktopApp(self.root, coordinator=self.mock_coordinator, storage=self.storage)

        # Switch to Topic 2
        self.app.load_conversation(c2)
        rendered_text = self.app.chat_display.get("1.0", tk.END)
        self.assertIn("Question 2", rendered_text)
        self.assertIn("Answer 2", rendered_text)
        self.assertNotIn("Question 1", rendered_text)

    def test_start_new_conversation_in_gui(self) -> None:
        """Verifies new_chat creates a fresh conversation and clears transcript."""
        self.app = FlexDesktopApp(self.root, coordinator=self.mock_coordinator, storage=self.storage)
        self.app.new_chat()

        self.mock_coordinator.create_new_conversation.assert_called_once()
        self.mock_coordinator.clear_history.assert_called_once()
        self.assertEqual(self.app.current_view_name, "home")

    def test_recent_chats_rendering(self) -> None:
        """Verifies sidebar renders recent chat titles from persistent storage."""
        self.storage.create_conversation("First Project")
        self.storage.create_conversation("Second Project")

        self.app = FlexDesktopApp(self.root, coordinator=self.mock_coordinator, storage=self.storage)
        self.assertEqual(len(self.app.recent_chat_labels), 2)

    def test_database_unavailable_at_startup_fails_safely(self) -> None:
        """Verifies that if storage fails or is None, the app still launches in ephemeral mode."""
        self.mock_coordinator.storage = None
        self.app = FlexDesktopApp(self.root, coordinator=self.mock_coordinator, storage=None)
        self.assertIsNone(self.app.storage)
        self.assertEqual(self.app.current_view_name, "home")

    def test_clean_shutdown_closes_storage(self) -> None:
        """Verifies on_closing calls storage.close()."""
        mock_storage = MagicMock()
        self.app = FlexDesktopApp(self.root, coordinator=self.mock_coordinator, storage=mock_storage)
        self.app.on_closing()
        mock_storage.close.assert_called_once()

    def test_new_chat_and_load_ignored_when_busy(self) -> None:
        """Verifies new_chat and load_conversation no-op when app._busy is True."""
        self.app = FlexDesktopApp(self.root, coordinator=self.mock_coordinator, storage=self.storage)
        self.app._busy = True

        self.app.new_chat()
        self.mock_coordinator.create_new_conversation.assert_not_called()

        self.app.load_conversation("some-cid")
        self.mock_coordinator.set_active_conversation.assert_not_called()

    def test_switching_to_empty_conversation_clears_transcript(self) -> None:
        """Verifies switching from a conversation with messages to an empty conversation clears the transcript."""
        c1 = self.storage.create_conversation("Topic 1")
        self.storage.save_message(c1, role="user", content="Question 1")
        c2 = self.storage.create_conversation("Empty Topic 2")

        def mock_set_active(conv_id: str, sync_history: bool = True) -> None:
            self.mock_coordinator.active_conversation_id = conv_id

        self.mock_coordinator.set_active_conversation.side_effect = mock_set_active
        self.mock_coordinator.active_conversation_id = c1

        self.app = FlexDesktopApp(self.root, coordinator=self.mock_coordinator, storage=self.storage)

        # Confirm Question 1 is visible initially
        rendered_initial = self.app.chat_display.get("1.0", tk.END)
        self.assertIn("Question 1", rendered_initial)

        # Switch to Empty Topic 2
        self.app.load_conversation(c2)
        rendered_after = self.app.chat_display.get("1.0", tk.END)
        self.assertNotIn("Question 1", rendered_after)
        self.assertIn("FLEX 1.0 System Initialized", rendered_after)

    def test_on_closing_while_busy_terminates_cleanly(self) -> None:
        """Verifies on_closing handles a running worker cleanly without crashing."""
        self.app = FlexDesktopApp(self.root, coordinator=self.mock_coordinator, storage=self.storage)
        self.app._busy = True
        self.app.on_closing()
        self.assertTrue(self.app._closing)

    def test_chat_success_for_non_active_conversation_does_not_append_to_ui(self) -> None:
        """Verifies that if a chat success response arrives for an inactive conversation, UI does not append."""
        from backend.coordinator import CoordinatorChatResult
        self.app = FlexDesktopApp(self.root, coordinator=self.mock_coordinator, storage=self.storage)
        self.mock_coordinator.active_conversation_id = "active-cid-123"

        stale_result = CoordinatorChatResult(
            text="Stale response text that should not appear.",
            operation_id=99,
            command_name="chat",
            parser_status="FLEX_STATUS_OK",
            state_status="FLEX_STATE_OK",
            model="qwen2.5-coder:3b",
            conversation_id="other-cid-456",
            storage_status="PERSISTED",
        )

        self.app.chat_view._on_chat_success(stale_result, 1.0)
        rendered = self.app.chat_display.get("1.0", tk.END)
        self.assertNotIn("Stale response text that should not appear.", rendered)


class TestFlexDesktopAppStage2C1UIStabilization(unittest.TestCase):
    """Stage 2C.1 regression tests for UI stabilization and functional recovery."""

    def setUp(self) -> None:
        self.root = tk.Tk()
        self.root.withdraw()
        self.temp_dir = tempfile.mkdtemp()
        self.db_path = Path(self.temp_dir) / "test_stage2c1.db"
        self.storage = SQLiteConversationStorage(self.db_path)

        self.mock_coordinator = MagicMock(spec=AICoordinator)
        self.mock_coordinator.is_running = True
        self.mock_coordinator.storage = self.storage
        self.mock_coordinator.active_conversation_id = self.storage.create_conversation("Active Chat")
        self.mock_coordinator.get_status.return_value = {
            "bridge": {"is_running": True, "lifecycle": "FLEX_LIFECYCLE_IDLE"},
            "ai_health": True,
            "model_available": True,
            "model": "qwen2.5-coder:3b",
        }

        self.app = FlexDesktopApp(
            self.root, coordinator=self.mock_coordinator, storage=self.storage
        )

    def tearDown(self) -> None:
        if hasattr(self, "app") and not self.app._closing:
            self.app.on_closing()
        elif hasattr(self, "root"):
            try:
                self.root.destroy()
            except Exception:
                pass
        if hasattr(self, "storage") and self.storage is not None:
            self.storage.close()
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_composer_mapped_and_visible_in_layout(self) -> None:
        """Verifies that the chat composer Text widget and Send button are packed and visible in layout."""
        self.assertEqual(self.app.txt_input.winfo_manager(), "pack")
        self.assertGreater(self.app.txt_input.winfo_reqheight(), 0)
        self.assertEqual(self.app.btn_send.winfo_manager(), "pack")
        self.assertGreater(self.app.btn_send.winfo_reqheight(), 0)

        # In active desktop display, deiconify and update to verify winfo_ismapped
        try:
            self.root.deiconify()
            self.root.update()
            self.assertEqual(self.app.txt_input.winfo_ismapped(), 1)
            self.assertGreater(self.app.txt_input.winfo_height(), 0)
            self.assertEqual(self.app.btn_send.winfo_ismapped(), 1)
            self.assertGreater(self.app.btn_send.winfo_height(), 0)
        finally:
            self.root.withdraw()

    def test_composer_placeholder_and_focus_behavior(self) -> None:
        """Verifies placeholder appearance, clearing on focus/keypress, and restoration on empty blur."""
        chat_view = self.app.chat_view
        self.assertTrue(chat_view._has_placeholder)
        self.assertEqual(chat_view.txt_input.get("1.0", tk.END).strip(), chat_view.PLACEHOLDER_TEXT)

        # FocusIn clears placeholder
        chat_view._on_input_focus_in()
        self.assertFalse(chat_view._has_placeholder)
        self.assertEqual(chat_view.txt_input.get("1.0", tk.END).strip(), "")

        # FocusOut with empty text restores placeholder
        chat_view._on_input_focus_out()
        self.assertTrue(chat_view._has_placeholder)
        self.assertEqual(chat_view.txt_input.get("1.0", tk.END).strip(), chat_view.PLACEHOLDER_TEXT)

    def test_enter_and_shift_enter_key_bindings(self) -> None:
        """Verifies Shift+Enter inserts newline and Enter submits the message."""
        chat_view = self.app.chat_view
        chat_view._on_input_focus_in()
        chat_view.txt_input.insert("1.0", "Line 1")

        # Shift+Enter inserts newline
        res_shift = chat_view._handle_shift_enter()
        self.assertEqual(res_shift, "break")
        chat_view.txt_input.insert(tk.INSERT, "Line 2")
        self.assertIn("Line 1\nLine 2", chat_view.txt_input.get("1.0", tk.END))

        # Enter submits message
        event_mock = MagicMock()
        event_mock.state = 0  # No Shift modifier
        with patch.object(chat_view, "send_message") as mock_send:
            res_enter = chat_view._handle_enter_key(event_mock)
            self.assertEqual(res_enter, "break")
            mock_send.assert_called_once()

    def test_empty_or_placeholder_message_not_submitted(self) -> None:
        """Verifies empty or placeholder-only messages are never submitted."""
        with patch("threading.Thread") as mock_thread:
            # Placeholder text only
            self.app.send_message()
            mock_thread.assert_not_called()
            self.assertFalse(self.app._busy)

            # Whitespace only
            self.app.txt_input.delete("1.0", tk.END)
            self.app.txt_input.insert("1.0", "   \n\t  ")
            self.app.send_message()
            mock_thread.assert_not_called()
            self.assertFalse(self.app._busy)

    def test_submit_handler_receives_actual_message_and_prevents_duplicate(self) -> None:
        """Verifies message is dispatched and concurrent submissions while busy are safely blocked."""
        self.app.txt_input.delete("1.0", tk.END)
        self.app.txt_input.insert("1.0", "Perform C core operation")

        with patch("threading.Thread") as mock_thread:
            self.app.send_message()
            self.assertTrue(self.app._busy)
            self.assertEqual(self.app.btn_send["text"], "BUSY...")
            self.assertEqual(str(self.app.btn_send["state"]), "disabled")
            mock_thread.assert_called_once()

            # Attempt duplicate submission while busy
            self.app.txt_input.insert("1.0", "Duplicate message")
            self.app.send_message()
            self.assertEqual(mock_thread.call_count, 1)

    def test_error_does_not_permanently_disable_composer(self) -> None:
        """Verifies that error recovery resets busy state, restores Send button, and re-enables composer."""
        self.app._busy = True
        self.app.btn_send.configure(state=tk.DISABLED, text="BUSY...")

        self.app._on_chat_error(Exception("Inference timeout"), 2.5)

        self.assertFalse(self.app._busy)
        self.assertEqual(str(self.app.btn_send["state"]), "normal")
        self.assertEqual(self.app.btn_send["text"], "➤")
        self.assertIn("Error encountered", self.app.status_lbl.cget("text"))

    def test_new_chat_with_empty_conversation_deduplicates_recent_chats(self) -> None:
        """Verifies creating new chats without messages does not create duplicate sidebar entries."""
        # Clean storage with only default empty conversation
        empty_dir = tempfile.mkdtemp()
        try:
            empty_storage = SQLiteConversationStorage(Path(empty_dir) / "empty.db")
            coord = MagicMock(spec=AICoordinator)
            coord.storage = empty_storage
            coord.active_conversation_id = empty_storage.create_conversation("New Chat")
            app = FlexDesktopApp(self.root, coordinator=coord, storage=empty_storage)

            # Trigger new_chat
            app.new_chat()
            app._refresh_recent_chats()

            # Empty database / chats with 0 messages must not generate duplicate sidebar labels
            self.assertEqual(len(app.recent_chat_labels), 0)
            empty_storage.close()
        finally:
            shutil.rmtree(empty_dir, ignore_errors=True)

    def test_subsystem_diagnostics_shows_sqlite_operational_status(self) -> None:
        """Verifies diagnostics correctly classifies SQLite persistence as Active and healthy."""
        diag = {
            "bridge": {"is_running": True, "lifecycle": "FLEX_LIFECYCLE_IDLE"},
            "bridge_running": True,
            "ai_health": True,
            "model_available": True,
            "storage_enabled": True,
            "storage_degraded": False,
            "total_conversations": 4,
            "history_count": 5,
            "history_max": 50,
            "internet": True,
        }
        self.app._apply_diagnostics(diag)
        mem_text = self.app.status_pills["memory"].cget("text")
        self.assertIn("SQLite Active (4 chats)", mem_text)

        health_state, _, _ = FlexDesktopApp.calculate_overall_health(diag)
        self.assertEqual(health_state, "HEALTHY")

    def test_subsystem_diagnostics_shows_degraded_when_storage_fails(self) -> None:
        """Verifies diagnostics classifies overall health as DEGRADED when storage is degraded."""
        diag = {
            "bridge": {"is_running": True, "lifecycle": "FLEX_LIFECYCLE_IDLE"},
            "bridge_running": True,
            "ai_health": True,
            "model_available": True,
            "storage_enabled": True,
            "storage_degraded": True,
            "history_count": 5,
            "history_max": 50,
            "internet": True,
        }
        self.app._apply_diagnostics(diag)
        mem_text = self.app.status_pills["memory"].cget("text")
        self.assertIn("SQLite Degraded", mem_text)

        health_state, _, _ = FlexDesktopApp.calculate_overall_health(diag)
        self.assertEqual(health_state, "DEGRADED")

    def test_auth_notice_displays_honestly_without_crash(self) -> None:
        """Verifies clicking authentication controls informs the user about local-first architecture."""
        with patch("tkinter.messagebox.showinfo") as mock_info:
            self.app._show_auth_notice()
            mock_info.assert_called_once()
            self.assertIn("local-first", self.app.chat_view.status_lbl.cget("text").lower())


class TestFlexDesktopAppStage2CMemoryView(unittest.TestCase):
    """Unit and GUI tests for Stage 2C functional MemoryView interface."""

    def setUp(self) -> None:
        self.root = tk.Tk()
        self.root.withdraw()

        self.temp_dir = tempfile.mkdtemp()
        self.db_path = os.path.join(self.temp_dir, "test_memory_view.db")
        self.storage = SQLiteConversationStorage(db_path=self.db_path)

        self.mock_coordinator = MagicMock(spec=AICoordinator)
        self.mock_coordinator.is_running = True
        self.mock_coordinator.storage = self.storage
        self.mock_coordinator.active_conversation_id = None
        self.mock_coordinator.get_status.return_value = {
            "bridge": {"is_running": True, "lifecycle": "FLEX_LIFECYCLE_IDLE"},
            "ai_health": True,
            "model_available": True,
            "model": "qwen2.5-coder:3b",
        }

        self.app = FlexDesktopApp(
            self.root, coordinator=self.mock_coordinator, storage=self.storage
        )

    def tearDown(self) -> None:
        if hasattr(self, "app") and self.app is not None:
            self.app.on_closing()
        if hasattr(self, "storage") and self.storage is not None:
            self.storage.close()
        shutil.rmtree(self.temp_dir, ignore_errors=True)
        try:
            self.root.destroy()
        except Exception:
            pass

    def test_memory_view_is_functional_not_prototype(self) -> None:
        """Verifies that 'memory' is an interactive MemoryView instance with real widgets."""
        memory_view = self.app.views.get("memory")
        self.assertIsInstance(memory_view, MemoryView)
        self.assertTrue(hasattr(memory_view, "search_entry"))
        self.assertTrue(hasattr(memory_view, "btn_open"))
        self.assertTrue(hasattr(memory_view, "btn_rename"))
        self.assertTrue(hasattr(memory_view, "btn_delete"))

    def test_persisted_conversation_appears_in_memory_view(self) -> None:
        """Verifies that saved conversations in SQLite are rendered in the MemoryView list."""
        cid = self.storage.create_conversation("C Memory Architecture")
        self.storage.save_message(cid, role="user", content="Explain heap vs stack")
        self.storage.save_message(cid, role="assistant", content="Heap is dynamic; stack is fixed.")

        self.app.switch_view("memory")
        mem_view: MemoryView = self.app.views["memory"]
        mem_view.refresh()

        self.assertIn(cid, mem_view._card_widgets)
        self.assertEqual(mem_view.selected_cid, cid)
        self.assertEqual(mem_view.lbl_detail_title.cget("text"), "C Memory Architecture")

    def test_messages_load_in_chronological_order(self) -> None:
        """Verifies conversation messages are loaded and formatted in chronological order."""
        cid = self.storage.create_conversation("Pointers in C")
        self.storage.save_message(cid, role="user", content="What is a pointer?")
        self.storage.save_message(cid, role="assistant", content="A variable holding memory address.")
        self.storage.save_message(cid, role="user", content="What is a dereference?")

        mem_view: MemoryView = self.app.views["memory"]
        mem_view.refresh()
        mem_view.select_conversation(cid)

        transcript_content = mem_view.txt_transcript.get("1.0", tk.END)
        self.assertIn("What is a pointer?", transcript_content)
        self.assertIn("A variable holding memory address.", transcript_content)
        self.assertIn("What is a dereference?", transcript_content)

        idx_1 = transcript_content.index("What is a pointer?")
        idx_2 = transcript_content.index("A variable holding memory address.")
        idx_3 = transcript_content.index("What is a dereference?")
        self.assertTrue(idx_1 < idx_2 < idx_3)

    def test_recent_chats_and_memory_consistent_counts(self) -> None:
        """Verifies Recent Chats and MemoryView agree on saved conversation count."""
        # 1. Start with 1 conversation with messages and 1 unstarted empty conversation
        c_active = self.storage.create_conversation("Pointers Discussion")
        self.storage.save_message(c_active, role="user", content="Explain NULL pointer")
        self.storage.create_conversation("New Chat")  # Empty placeholder

        self.app._refresh_recent_chats()
        mem_view: MemoryView = self.app.views["memory"]
        mem_view.refresh()

        # Both views should filter out unstarted empty 'New Chat' drafts
        self.assertEqual(len(self.app.recent_chat_labels), 1)
        self.assertEqual(mem_view.lbl_list_count.cget("text"), "1")
        self.assertEqual(self.storage.get_conversation_count(filter_empty=True), 1)

    def test_search_conversations_in_memory_view(self) -> None:
        """Verifies searching conversations by title or message content."""
        c1 = self.storage.create_conversation("Operating Systems")
        self.storage.save_message(c1, role="user", content="Explain scheduling algorithms")

        c2 = self.storage.create_conversation("Networking Basics")
        self.storage.save_message(c2, role="user", content="Explain TCP handshake")

        mem_view: MemoryView = self.app.views["memory"]
        mem_view.refresh()
        self.assertEqual(len(mem_view._card_widgets), 2)

        # Search by title
        mem_view._search_query = "Operating"
        mem_view._populate_conversation_list()
        self.assertEqual(len(mem_view._card_widgets), 1)
        self.assertIn(c1, mem_view._card_widgets)

        # Search by message content
        mem_view._search_query = "handshake"
        mem_view._populate_conversation_list()
        self.assertEqual(len(mem_view._card_widgets), 1)
        self.assertIn(c2, mem_view._card_widgets)

        # Clear search
        mem_view._clear_search()
        self.assertEqual(len(mem_view._card_widgets), 2)

    def test_open_in_chat_action_switches_to_home(self) -> None:
        """Verifies 'Open in Chat' loads the conversation and switches to Home view."""
        cid = self.storage.create_conversation("Algorithms")
        self.storage.save_message(cid, role="user", content="Binary search complexity?")

        self.app.switch_view("memory")
        mem_view: MemoryView = self.app.views["memory"]
        mem_view.refresh()
        mem_view.select_conversation(cid)

        mem_view._on_open_clicked()
        self.assertEqual(self.app.current_view_name, "home")
        self.mock_coordinator.set_active_conversation.assert_called_with(cid, sync_history=True)

    def test_rename_conversation_updates_memory_and_recent_chats(self) -> None:
        """Verifies renaming a conversation updates SQLite, MemoryView, and Recent Chats."""
        cid = self.storage.create_conversation("Old Title")
        self.storage.save_message(cid, role="user", content="Hello world")
        self.app._refresh_recent_chats()

        mem_view: MemoryView = self.app.views["memory"]
        mem_view.refresh()
        mem_view.select_conversation(cid)

        with patch("tkinter.simpledialog.askstring", return_value="Renamed Space Topic"):
            mem_view._on_rename_clicked()

        updated_conv = self.storage.get_conversation(cid)
        self.assertEqual(updated_conv.title, "Renamed Space Topic")
        self.assertEqual(mem_view.lbl_detail_title.cget("text"), "Renamed Space Topic")
        self.assertIn("Renamed Space Topic", [lbl.cget("text") for lbl in self.app.recent_chat_labels.values()])

    def test_delete_conversation_with_confirmation_updates_both_views(self) -> None:
        """Verifies deleting a conversation cascades and updates all views."""
        cid = self.storage.create_conversation("To Be Deleted")
        self.storage.save_message(cid, role="user", content="Temporary message")
        self.app._refresh_recent_chats()

        mem_view: MemoryView = self.app.views["memory"]
        mem_view.refresh()
        mem_view.select_conversation(cid)

        with patch("tkinter.messagebox.askyesno", return_value=True):
            mem_view._on_delete_clicked()

        self.assertIsNone(self.storage.get_conversation(cid))
        self.assertNotIn(cid, mem_view._card_widgets)
        self.assertNotIn(cid, self.app.recent_chat_labels)

    def test_empty_database_shows_empty_state(self) -> None:
        """Verifies that an empty database displays a clean empty state in MemoryView."""
        mem_view: MemoryView = self.app.views["memory"]
        mem_view.refresh()

        self.assertEqual(mem_view.lbl_list_count.cget("text"), "0")
        self.assertEqual(len(mem_view._card_widgets), 0)
        self.assertIsNone(mem_view.selected_cid)
        self.assertEqual(mem_view.btn_open.cget("state"), tk.DISABLED)


if __name__ == "__main__":
    unittest.main()
