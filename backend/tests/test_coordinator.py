"""FLEX 1.0 — Unit and Integration Tests for AI Pipeline Coordinator (Stage 1F.2).

Validates:
    - End-to-end coordinator chat turn: dispatch -> inference -> cleanup -> history update.
    - C core dispatch failure prevents AI inference from starting.
    - An operation that never started is never finished.
    - Guaranteed cleanup: C core finish is attempted when an operation started, even if AI fails.
    - Primary inference error preservation when cleanup also encounters an error.
    - Handling of inference success when cleanup fails.
    - Thread serialization lock presence.
    - Conversation history buffer updates and clearing.
    - Live integration with real flex_bridge.exe and real Ollama (when opted in).
"""

from __future__ import annotations

import os
import sys
import threading
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

# Ensure repository root is on sys.path
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))

from backend.ai import (
    AIServiceUnavailableError,
    AITimeoutError,
    AIValidationError,
    ChatMessage,
    InMemoryConversationHistory,
    LLMProvider,
    LLMResponse,
    OllamaConfig,
    OllamaProvider,
)
from backend.coordinator import (
    AICoordinator,
    CoordinatorChatResult,
    CoordinatorDispatchError,
    CoordinatorError,
    CoordinatorLifecycleError,
    CoordinatorStateError,
)
from backend.core_bridge import FlexBridgeClient, FlexBridgeProcessError


class TestAICoordinatorUnit(unittest.TestCase):
    """Unit tests for AICoordinator using mocked bridge and AI provider."""

    def setUp(self) -> None:
        self.mock_bridge = MagicMock(spec=FlexBridgeClient)
        self.mock_bridge.is_running = True

        self.mock_provider = MagicMock(spec=LLMProvider)
        self.history = InMemoryConversationHistory()

        self.coordinator = AICoordinator(
            bridge=self.mock_bridge,
            provider=self.mock_provider,
            history=self.history,
            system_prompt="Test System Prompt",
            auto_start=False,
        )

    def test_successful_chat_pipeline(self) -> None:
        """Verifies full flow: dispatch -> inference -> finish -> history update."""
        # Setup dispatch response
        self.mock_bridge.dispatch.return_value = {
            "action": "DISPATCH",
            "success": True,
            "parser_status": "FLEX_STATUS_OK",
            "state_status": "FLEX_STATE_OK",
            "command_type": "FLEX_COMMAND_CHAT",
            "operation": "FLEX_OPERATION_CHAT",
            "operation_id": 42,
            "operation_started": True,
            "command_name": "chat",
            "args": "Explain pointers in C",
            "lifecycle": "FLEX_LIFECYCLE_PROCESSING",
        }

        # Setup AI response
        self.mock_provider.generate.return_value = LLMResponse(
            text="Pointers store memory addresses.",
            model="qwen2.5-coder:3b",
            total_duration_ns=12345678,
            raw_response={"message": {"content": "Pointers store memory addresses."}},
        )

        # Setup finish response
        self.mock_bridge.finish.return_value = {
            "action": "FINISH",
            "success": True,
            "state_status": "FLEX_STATE_OK",
            "lifecycle": "FLEX_LIFECYCLE_IDLE",
        }

        result = self.coordinator.send_chat("Explain pointers in C")

        # 1. Dispatch was called with formatted command
        self.mock_bridge.dispatch.assert_called_once_with("chat Explain pointers in C")

        # 2. Provider generate was called with extracted args and history
        self.mock_provider.generate.assert_called_once_with(
            prompt="Explain pointers in C",
            system_prompt="Test System Prompt",
            history=self.history,
            timeout=None,
        )

        # 3. Bridge finish was called for cleanup
        self.mock_bridge.finish.assert_called_once()

        # 4. Result contains model output and C core metadata
        self.assertIsInstance(result, CoordinatorChatResult)
        self.assertEqual(result.text, "Pointers store memory addresses.")
        self.assertEqual(result.operation_id, 42)
        self.assertEqual(result.command_name, "chat")
        self.assertEqual(result.parser_status, "FLEX_STATUS_OK")
        self.assertEqual(result.state_status, "FLEX_STATE_OK")
        self.assertEqual(result.model, "qwen2.5-coder:3b")

        # 5. History recorded both user and assistant turns
        messages = self.history.get_messages()
        self.assertEqual(len(messages), 2)
        self.assertEqual(messages[0].role, "user")
        self.assertEqual(messages[0].content, "Explain pointers in C")
        self.assertEqual(messages[1].role, "assistant")
        self.assertEqual(messages[1].content, "Pointers store memory addresses.")

    def test_dispatch_failure_prevents_inference_and_never_finishes(self) -> None:
        """Dispatch rejection must never start inference and never call finish."""
        self.mock_bridge.dispatch.return_value = {
            "action": "DISPATCH",
            "success": False,
            "parser_status": "FLEX_STATUS_UNKNOWN_COMMAND",
            "state_status": "FLEX_STATE_OK",
            "operation_started": False,
        }

        with self.assertRaises(CoordinatorDispatchError) as ctx:
            self.coordinator.send_chat("bad_command")

        self.assertIn("C core rejected dispatch", str(ctx.exception))
        # Inference must NOT be called
        self.mock_provider.generate.assert_not_called()
        # Finish must NEVER be called
        self.mock_bridge.finish.assert_not_called()
        # History must NOT be modified
        self.assertEqual(len(self.history.get_messages()), 0)

    def test_inference_failure_triggers_guaranteed_cleanup(self) -> None:
        """When inference raises an error, C core finish must still be attempted."""
        self.mock_bridge.dispatch.return_value = {
            "action": "DISPATCH",
            "success": True,
            "parser_status": "FLEX_STATUS_OK",
            "state_status": "FLEX_STATE_OK",
            "operation_started": True,
            "operation_id": 10,
            "args": "Test prompt",
        }

        # Provider fails
        self.mock_provider.generate.side_effect = AIServiceUnavailableError("Ollama is down")

        # Bridge finish succeeds
        self.mock_bridge.finish.return_value = {
            "action": "FINISH",
            "success": True,
            "state_status": "FLEX_STATE_OK",
        }

        with self.assertRaises(AIServiceUnavailableError) as ctx:
            self.coordinator.send_chat("Test prompt")

        self.assertEqual(str(ctx.exception), "Ollama is down")
        # Finish was still called for cleanup
        self.mock_bridge.finish.assert_called_once()
        # Incomplete turn must NOT be stored in history
        self.assertEqual(len(self.history.get_messages()), 0)

    def test_preserves_primary_inference_error_when_cleanup_also_fails(self) -> None:
        """If both inference and cleanup fail, primary inference error must be preserved."""
        self.mock_bridge.dispatch.return_value = {
            "action": "DISPATCH",
            "success": True,
            "parser_status": "FLEX_STATUS_OK",
            "state_status": "FLEX_STATE_OK",
            "operation_started": True,
            "operation_id": 11,
            "args": "Test prompt",
        }

        # Provider fails with primary error
        primary_exc = AITimeoutError("Inference timed out after 30s")
        self.mock_provider.generate.side_effect = primary_exc

        # Bridge finish ALSO fails (crashed bridge / broken pipe)
        self.mock_bridge.finish.side_effect = FlexBridgeProcessError("Bridge process dead")

        with self.assertRaises(AITimeoutError) as ctx:
            self.coordinator.send_chat("Test prompt")

        # Primary error is preserved
        self.assertIs(ctx.exception, primary_exc)
        self.assertEqual(len(self.history.get_messages()), 0)

    def test_cleanup_failure_after_successful_inference(self) -> None:
        """If inference succeeds but cleanup fails, a CoordinatorStateError is raised."""
        self.mock_bridge.dispatch.return_value = {
            "action": "DISPATCH",
            "success": True,
            "parser_status": "FLEX_STATUS_OK",
            "state_status": "FLEX_STATE_OK",
            "operation_started": True,
            "operation_id": 12,
            "args": "Test prompt",
        }

        self.mock_provider.generate.return_value = LLMResponse(
            text="Response text",
            model="qwen2.5-coder:3b",
        )

        # Bridge finish returns unsuccessful state status
        self.mock_bridge.finish.return_value = {
            "action": "FINISH",
            "success": False,
            "state_status": "FLEX_STATE_ERROR_CORRUPTED",
        }

        with self.assertRaises(CoordinatorStateError) as ctx:
            self.coordinator.send_chat("Test prompt")

        self.assertIn("FLEX_STATE_ERROR_CORRUPTED", str(ctx.exception))

    def test_clear_history(self) -> None:
        """Verifies clearing conversation history."""
        self.history.add_user_message("Hello")
        self.history.add_assistant_message("Hi")
        self.assertEqual(len(self.history.get_messages()), 2)

        self.coordinator.clear_history()
        self.assertEqual(len(self.history.get_messages()), 0)

    def test_serialization_lock_is_present(self) -> None:
        """Verifies that AICoordinator uses an RLock for serialization."""
        self.assertIsInstance(self.coordinator._lock, type(threading.RLock()))

    def test_status_reporting(self) -> None:
        """Verifies comprehensive status dictionary."""
        self.mock_bridge.get_status.return_value = {
            "action": "STATUS",
            "success": True,
            "lifecycle": "FLEX_LIFECYCLE_IDLE",
            "active_operation": "FLEX_OPERATION_NONE",
            "operation_id": 5,
        }
        self.mock_provider.check_health.return_value = True
        self.mock_provider.is_model_available.return_value = True
        self.mock_provider.config = MagicMock(model="qwen2.5-coder:3b")

        status = self.coordinator.get_status()
        self.assertTrue(status["ai_health"])
        self.assertTrue(status["model_available"])
        self.assertEqual(status["model"], "qwen2.5-coder:3b")
        self.assertEqual(status["bridge"]["lifecycle"], "FLEX_LIFECYCLE_IDLE")


class TestAICoordinatorLiveIntegration(unittest.TestCase):
    """End-to-end integration test with real flex_bridge.exe and live local Ollama."""

    def setUp(self) -> None:
        if os.environ.get("FLEX_RUN_OLLAMA_TESTS") != "1":
            self.skipTest(
                "Live coordinator tests are disabled by default. "
                "Set environment variable FLEX_RUN_OLLAMA_TESTS=1 to run."
            )

        # Check bridge binary existence
        try:
            self.bridge = FlexBridgeClient()
        except Exception as e:
            self.skipTest(f"C bridge binary unavailable: {e}")

        self.provider = OllamaProvider()
        if not self.provider.check_health():
            self.skipTest("Local Ollama daemon is unreachable.")
        if not self.provider.is_model_available("qwen2.5-coder:3b"):
            self.skipTest("Model 'qwen2.5-coder:3b' is not pulled locally.")

        self.coordinator = AICoordinator(
            bridge=self.bridge,
            provider=self.provider,
            auto_start=True,
        )

    def tearDown(self) -> None:
        if hasattr(self, "coordinator"):
            self.coordinator.close()

    def test_live_coordinator_round_trip(self) -> None:
        """Runs a complete live request: C bridge dispatch -> Ollama -> C bridge finish."""
        result = self.coordinator.send_chat("Say 'FLEX LIVE CONFIRMED' and nothing else.")

        self.assertIsInstance(result, CoordinatorChatResult)
        self.assertGreater(len(result.text.strip()), 0)
        self.assertGreater(result.operation_id, 0)
        self.assertEqual(result.parser_status, "FLEX_STATUS_OK")
        self.assertEqual(result.state_status, "FLEX_STATE_OK")

        # Verify C bridge returned to IDLE
        bridge_status = self.coordinator.bridge.get_status()
        self.assertEqual(bridge_status["lifecycle"], "FLEX_LIFECYCLE_IDLE")
        self.assertEqual(bridge_status["active_operation"], "FLEX_OPERATION_NONE")


class TestAICoordinatorPersistence(unittest.TestCase):
    """Unit tests for AICoordinator SQLite persistence integration."""

    def setUp(self) -> None:
        self.mock_bridge = MagicMock(spec=FlexBridgeClient)
        self.mock_bridge.is_running = True
        def mock_dispatch(cmd: str, timeout: Optional[float] = None) -> dict:
            args = cmd[5:] if cmd.startswith("chat ") else cmd
            return {
                "action": "DISPATCH",
                "success": True,
                "parser_status": "FLEX_STATUS_OK",
                "state_status": "FLEX_STATE_OK",
                "operation_started": True,
                "operation_id": 100,
                "command_name": "chat",
                "args": args,
            }
        self.mock_bridge.dispatch.side_effect = mock_dispatch
        self.mock_bridge.finish.return_value = {
            "action": "FINISH",
            "success": True,
            "state_status": "FLEX_STATE_OK",
            "lifecycle": "FLEX_LIFECYCLE_IDLE",
        }

        self.mock_provider = MagicMock(spec=LLMProvider)
        self.mock_provider.generate.return_value = LLMResponse(
            text="Persistence stores messages to SQLite.",
            model="qwen2.5-coder:3b",
            total_duration_ns=1000000,
        )

        from backend.ai.storage import SQLiteConversationStorage
        self.storage = SQLiteConversationStorage(db_path=":memory:")
        self.coordinator = AICoordinator(
            bridge=self.mock_bridge,
            provider=self.mock_provider,
            storage=self.storage,
            auto_start=False,
        )

    def tearDown(self) -> None:
        if hasattr(self, "storage") and self.storage is not None:
            self.storage.close()

    def test_persisted_chat_turn_saves_both_messages(self) -> None:
        """Verifies full turn saves user prompt and assistant response to SQLite."""
        cid = self.coordinator.active_conversation_id
        self.assertIsNotNone(cid)

        result = self.coordinator.send_chat("How does persistence work?")

        self.assertEqual(result.storage_status, "PERSISTED")
        self.assertEqual(result.conversation_id, cid)

        # Check messages in storage
        messages = self.storage.get_messages(cid)
        self.assertEqual(len(messages), 2)
        self.assertEqual(messages[0].role, "user")
        self.assertEqual(messages[0].content, "How does persistence work?")
        self.assertEqual(messages[1].role, "assistant")
        self.assertEqual(messages[1].content, "Persistence stores messages to SQLite.")

        # Check messages in in-memory history
        hist = self.coordinator.history.get_messages()
        self.assertEqual(len(hist), 2)
        self.assertEqual(hist[0].content, "How does persistence work?")
        self.assertEqual(hist[1].content, "Persistence stores messages to SQLite.")

    def test_user_message_persisted_when_inference_fails(self) -> None:
        """Verifies user message is preserved in SQLite and history when Ollama inference fails."""
        cid = self.coordinator.active_conversation_id
        self.mock_provider.generate.side_effect = AIServiceUnavailableError("Daemon down")

        with self.assertRaises(AIServiceUnavailableError):
            self.coordinator.send_chat("Preserve my question")

        # C core finish must still be called
        self.mock_bridge.finish.assert_called_once()

        # User message was preserved in SQLite!
        stored_msgs = self.storage.get_messages(cid)
        self.assertEqual(len(stored_msgs), 1)
        self.assertEqual(stored_msgs[0].role, "user")
        self.assertEqual(stored_msgs[0].content, "Preserve my question")

        # In-memory history is synchronized with the saved user message
        hist = self.coordinator.history.get_messages()
        self.assertEqual(len(hist), 1)
        self.assertEqual(hist[0].content, "Preserve my question")

    def test_storage_write_failure_falls_back_to_memory_only(self) -> None:
        """Verifies failure in storage write does not crash coordinator and reports MEMORY_ONLY."""
        self.storage.save_message = MagicMock(side_effect=Exception("Disk full"))

        result = self.coordinator.send_chat("Test write failure")

        self.assertEqual(result.storage_status, "MEMORY_ONLY")
        self.assertIn("Disk full", self.coordinator.last_storage_error or "")
        # Turn still completed in memory
        self.assertEqual(result.text, "Persistence stores messages to SQLite.")
        self.assertEqual(len(self.coordinator.history.get_messages()), 2)

    def test_switching_conversations_syncs_in_memory_history(self) -> None:
        """Verifies switching conversations swaps active conversation ID and in-memory history."""
        # Conv 1
        c1 = self.coordinator.create_new_conversation("Conversation 1")
        self.storage.save_message(c1, role="user", content="C1 User Msg")
        self.storage.save_message(c1, role="assistant", content="C1 Assistant Msg")

        # Conv 2
        c2 = self.coordinator.create_new_conversation("Conversation 2")
        self.storage.save_message(c2, role="user", content="C2 User Msg")

        # Switch to C1
        self.coordinator.set_active_conversation(c1, sync_history=True)
        self.assertEqual(self.coordinator.active_conversation_id, c1)
        c1_hist = self.coordinator.history.get_messages()
        self.assertEqual(len(c1_hist), 2)
        self.assertEqual(c1_hist[0].content, "C1 User Msg")
        self.assertEqual(c1_hist[1].content, "C1 Assistant Msg")

        # Switch to C2
        self.coordinator.set_active_conversation(c2, sync_history=True)
        self.assertEqual(self.coordinator.active_conversation_id, c2)
        c2_hist = self.coordinator.history.get_messages()
        self.assertEqual(len(c2_hist), 1)
        self.assertEqual(c2_hist[0].content, "C2 User Msg")

    def test_storage_assistant_persistence_failure_falls_back_to_memory_only(self) -> None:
        """Verifies storage failure on assistant message does not abort request and reports MEMORY_ONLY."""
        real_save = self.storage.save_message
        call_count = 0

        def failing_save(*args: Any, **kwargs: Any) -> str:
            nonlocal call_count
            call_count += 1
            if kwargs.get("role") == "assistant" or (len(args) > 1 and args[1] == "assistant"):
                raise Exception("Simulated disk error on assistant message")
            return real_save(*args, **kwargs)

        self.storage.save_message = MagicMock(side_effect=failing_save)

        result = self.coordinator.send_chat("Test assistant write failure")

        self.assertEqual(result.storage_status, "MEMORY_ONLY")
        self.assertIn("Simulated disk error", self.coordinator.last_storage_error or "")
        self.assertEqual(result.text, "Persistence stores messages to SQLite.")
        # User message was saved to storage before failure
        self.assertEqual(call_count, 2)

    def test_conversation_switching_while_request_running_pins_conversation(self) -> None:
        """Verifies that if active conversation is changed during inference, the turn remains pinned to the starting conversation."""
        c1 = self.coordinator.active_conversation_id
        c2 = self.storage.create_conversation("Conversation 2")

        # Simulate conversation switch happening inside generate()
        def switch_mid_flight(*args: Any, **kwargs: Any) -> LLMResponse:
            # Switch the active conversation on the coordinator directly
            self.coordinator._active_conversation_id = c2
            return LLMResponse(
                text="Response meant for C1.",
                model="qwen2.5-coder:3b",
                total_duration_ns=1000,
            )

        self.mock_provider.generate.side_effect = switch_mid_flight

        result = self.coordinator.send_chat("Question for C1")

        # Result is pinned to C1, not C2
        self.assertEqual(result.conversation_id, c1)

        # Both user and assistant messages for the turn are saved in C1
        c1_msgs = self.storage.get_messages(c1)
        self.assertEqual(len(c1_msgs), 2)
        self.assertEqual(c1_msgs[0].content, "Question for C1")
        self.assertEqual(c1_msgs[1].content, "Response meant for C1.")

        # C2 has NO messages (no contamination!)
        c2_msgs = self.storage.get_messages(c2)
        self.assertEqual(len(c2_msgs), 0)

    def test_create_new_chat_after_failed_request(self) -> None:
        """Verifies creating a new chat after an inference failure functions without leftover error state."""
        self.mock_provider.generate.side_effect = AIServiceUnavailableError("Server down")
        with self.assertRaises(AIServiceUnavailableError):
            self.coordinator.send_chat("Failed message")

        # Create new chat
        new_cid = self.coordinator.create_new_conversation("Recovery Chat")
        self.assertEqual(self.coordinator.active_conversation_id, new_cid)
        self.assertEqual(len(self.coordinator.history.get_messages()), 0)

        # Now configure provider to succeed
        self.mock_provider.generate.side_effect = None
        self.mock_provider.generate.return_value = LLMResponse(
            text="Recovered successfully.",
            model="qwen2.5-coder:3b",
            total_duration_ns=1000,
        )

        result = self.coordinator.send_chat("Are you working?")
        self.assertEqual(result.conversation_id, new_cid)
        self.assertEqual(result.text, "Recovered successfully.")

    def test_coordinator_close_closes_storage(self) -> None:
        """Verifies coordinator.close() cleanly closes storage connection."""
        self.assertIsNotNone(self.storage._conn)
        self.coordinator.close()
        self.assertIsNone(self.storage._conn)


if __name__ == "__main__":
    unittest.main()
