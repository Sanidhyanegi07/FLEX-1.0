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

from backend.app import AICoreVisualizer, DiagnosticsDrawer, FlexDesktopApp
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


if __name__ == "__main__":
    unittest.main()
