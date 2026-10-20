"""FLEX 1.0 — Stage 1E.3: End-to-End Integration Tests.

Validates the complete path:
    Python application -> FlexBridgeClient -> persistent flex_bridge.exe
    -> C orchestrator -> C state manager -> JSON response -> Python application.

All tests in this suite use the real compiled native binary `flex_bridge.exe` and
the production `FlexBridgeClient` for full end-to-end integration testing.
"""

from __future__ import annotations

import os
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

# Ensure repository root is on sys.path
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))

from backend.core_bridge import (
    BRIDGE_MAX_LINE_BYTES,
    FLEX_MAX_INPUT_BYTES,
    FlexBridgeClient,
    FlexBridgeError,
    FlexBridgeProcessError,
    FlexBridgeTimeoutError,
    resolve_bridge_path,
    utf8_byte_length,
)


class TestEndToEndStartupAndInitialState(unittest.TestCase):
    """Scenario 1: Startup, subprocess viability, and initial state schema."""

    def setUp(self) -> None:
        self.client = FlexBridgeClient(timeout=3.0)
        self.addCleanup(self.client.close)

    def test_startup_and_initial_status_schema(self) -> None:
        self.client.start()
        self.assertTrue(self.client.is_running, "Subprocess must be running after start()")
        self.assertIsInstance(self.client.pid, int)
        self.assertGreater(self.client.pid, 0)

        # Query initial status
        st = self.client.get_status()

        # Schema assertion
        expected_keys = {"action", "success", "lifecycle", "active_operation", "operation_id", "last_error"}
        self.assertTrue(expected_keys.issubset(st.keys()), f"Status schema missing keys: {expected_keys - st.keys()}")

        self.assertEqual(st["action"], "STATUS")
        self.assertIs(st["success"], True)
        self.assertEqual(st["lifecycle"], "FLEX_LIFECYCLE_IDLE")
        self.assertEqual(st["active_operation"], "FLEX_OPERATION_NONE")
        self.assertEqual(st["operation_id"], 0)
        self.assertEqual(st["last_error"], "FLEX_STATE_OK")

        # Client property sync
        self.assertEqual(self.client.lifecycle, "FLEX_LIFECYCLE_IDLE")
        self.assertEqual(self.client.active_operation, "FLEX_OPERATION_NONE")


class TestEndToEndOperationLifecycle(unittest.TestCase):
    """Scenario 2: Complete operation lifecycle for chat, voice, and memory."""

    def setUp(self) -> None:
        self.client = FlexBridgeClient(timeout=3.0)
        self.client.start()
        self.addCleanup(self.client.close)

    def test_complete_chat_lifecycle(self) -> None:
        # 1. Dispatch valid chat command
        res = self.client.dispatch("chat Explain pointers and arrays in C")

        # Verify dispatch response
        self.assertEqual(res["action"], "DISPATCH")
        self.assertIs(res["success"], True)
        self.assertEqual(res["parser_status"], "FLEX_STATUS_OK")
        self.assertEqual(res["state_status"], "FLEX_STATE_OK")
        self.assertEqual(res["command_type"], "FLEX_COMMAND_CHAT")
        self.assertEqual(res["operation"], "FLEX_OPERATION_CHAT")
        self.assertEqual(res["operation_id"], 1)
        self.assertIs(res["operation_started"], True)
        self.assertIs(res["shutdown_requested"], False)
        self.assertEqual(res["command_name"], "chat")
        self.assertEqual(res["args"], "Explain pointers and arrays in C")
        self.assertEqual(res["lifecycle"], "FLEX_LIFECYCLE_PROCESSING")
        self.assertEqual(res["active_operation"], "FLEX_OPERATION_CHAT")

        # 2. Verify status reflects active operation
        st_mid = self.client.get_status()
        self.assertEqual(st_mid["lifecycle"], "FLEX_LIFECYCLE_PROCESSING")
        self.assertEqual(st_mid["active_operation"], "FLEX_OPERATION_CHAT")
        self.assertEqual(st_mid["operation_id"], 1)

        # 3. Explicitly finish operation
        fin = self.client.finish()
        self.assertEqual(fin["action"], "FINISH")
        self.assertIs(fin["success"], True)
        self.assertEqual(fin["state_status"], "FLEX_STATE_OK")
        self.assertEqual(fin["lifecycle"], "FLEX_LIFECYCLE_IDLE")
        self.assertEqual(fin["active_operation"], "FLEX_OPERATION_NONE")
        self.assertEqual(fin["operation_id"], 1)

        # 4. Verify status returns to idle
        st_end = self.client.get_status()
        self.assertEqual(st_end["lifecycle"], "FLEX_LIFECYCLE_IDLE")
        self.assertEqual(st_end["active_operation"], "FLEX_OPERATION_NONE")
        self.assertEqual(st_end["operation_id"], 1)

    def test_voice_and_memory_lifecycle_with_monotonic_ids(self) -> None:
        # Step 1: Voice operation (ID = 1)
        res_voice = self.client.dispatch("voice")
        self.assertIs(res_voice["success"], True)
        self.assertEqual(res_voice["operation"], "FLEX_OPERATION_VOICE")
        self.assertEqual(res_voice["operation_id"], 1)
        self.assertEqual(res_voice["active_operation"], "FLEX_OPERATION_VOICE")

        fin_voice = self.client.finish()
        self.assertIs(fin_voice["success"], True)
        self.assertEqual(fin_voice["lifecycle"], "FLEX_LIFECYCLE_IDLE")

        # Step 2: Memory operation (ID = 2)
        res_mem = self.client.dispatch("memory Store session context")
        self.assertIs(res_mem["success"], True)
        self.assertEqual(res_mem["operation"], "FLEX_OPERATION_MEMORY")
        self.assertEqual(res_mem["operation_id"], 2)
        self.assertEqual(res_mem["args"], "Store session context")
        self.assertEqual(res_mem["active_operation"], "FLEX_OPERATION_MEMORY")

        fin_mem = self.client.finish()
        self.assertIs(fin_mem["success"], True)
        self.assertEqual(fin_mem["lifecycle"], "FLEX_LIFECYCLE_IDLE")

        # Step 3: Chat operation (ID = 3)
        res_chat = self.client.dispatch("chat Third consecutive task")
        self.assertIs(res_chat["success"], True)
        self.assertEqual(res_chat["operation_id"], 3)
        self.client.finish()

        # Step 4: Verify monotonic operation_id persistence
        st = self.client.get_status()
        self.assertEqual(st["operation_id"], 3)
        self.assertEqual(st["lifecycle"], "FLEX_LIFECYCLE_IDLE")


class TestEndToEndStateAndErrorHandling(unittest.TestCase):
    """Scenario 3: Unknown commands, malformed input, concurrent operations, and error reset."""

    def setUp(self) -> None:
        self.client = FlexBridgeClient(timeout=3.0)
        self.client.start()
        self.addCleanup(self.client.close)

    def test_unknown_command_does_not_corrupt_state(self) -> None:
        res = self.client.dispatch("nonexistent_command_verb with args")
        self.assertEqual(res["action"], "DISPATCH")
        self.assertIs(res["success"], False)
        self.assertEqual(res["parser_status"], "FLEX_STATUS_UNKNOWN_COMMAND")
        self.assertEqual(res["state_status"], "FLEX_STATE_OK")
        self.assertEqual(res["command_type"], "FLEX_COMMAND_UNKNOWN")
        self.assertEqual(res["operation"], "FLEX_OPERATION_NONE")
        self.assertEqual(res["operation_id"], 0)
        self.assertIs(res["operation_started"], False)

        # Confirm state manager remained completely idle
        st = self.client.get_status()
        self.assertEqual(st["lifecycle"], "FLEX_LIFECYCLE_IDLE")
        self.assertEqual(st["active_operation"], "FLEX_OPERATION_NONE")
        self.assertEqual(st["operation_id"], 0)

    def test_empty_command_handling(self) -> None:
        res = self.client.dispatch("")
        self.assertIs(res["success"], False)
        self.assertEqual(res["parser_status"], "FLEX_STATUS_EMPTY_INPUT")
        self.assertEqual(res["state_status"], "FLEX_STATE_OK")
        self.assertIs(res["operation_started"], False)

        # State must remain intact
        st = self.client.get_status()
        self.assertEqual(st["lifecycle"], "FLEX_LIFECYCLE_IDLE")

    def test_concurrent_operation_rejection_preserves_active_operation(self) -> None:
        # Start initial CHAT operation
        res1 = self.client.dispatch("chat Primary in-flight operation")
        self.assertIs(res1["success"], True)
        self.assertEqual(res1["active_operation"], "FLEX_OPERATION_CHAT")
        self.assertEqual(res1["operation_id"], 1)

        # Attempt to start concurrent VOICE operation
        res2 = self.client.dispatch("voice")
        self.assertIs(res2["success"], False)
        self.assertEqual(res2["parser_status"], "FLEX_STATUS_OK")
        self.assertEqual(res2["state_status"], "FLEX_STATE_ERROR_ALREADY_PROCESSING")
        self.assertIs(res2["operation_started"], False)
        self.assertEqual(res2["operation_id"], 0)

        # Attempt to start concurrent MEMORY operation
        res3 = self.client.dispatch("memory Concurrent task")
        self.assertIs(res3["success"], False)
        self.assertEqual(res3["state_status"], "FLEX_STATE_ERROR_ALREADY_PROCESSING")

        # Crucial check: Active operation must NOT be corrupted or falsely finished!
        st = self.client.get_status()
        self.assertEqual(st["lifecycle"], "FLEX_LIFECYCLE_PROCESSING")
        self.assertEqual(st["active_operation"], "FLEX_OPERATION_CHAT")
        self.assertEqual(st["operation_id"], 1)

        # Finish original operation cleanly
        fin = self.client.finish()
        self.assertIs(fin["success"], True)
        self.assertEqual(fin["active_operation"], "FLEX_OPERATION_NONE")

    def test_error_state_and_reset_error(self) -> None:
        # Call finish while idle -> causes state manager error
        fin = self.client.finish()
        self.assertIs(fin["success"], False)
        self.assertEqual(fin["state_status"], "FLEX_STATE_ERROR_NOT_PROCESSING")

        # Status must reflect the last error
        st_err = self.client.get_status()
        self.assertEqual(st_err["last_error"], "FLEX_STATE_ERROR_NOT_PROCESSING")

        # Call reset_error()
        rst = self.client.reset_error()
        self.assertEqual(rst["action"], "RESET_ERROR")
        self.assertIs(rst["success"], True)
        self.assertEqual(rst["state_status"], "FLEX_STATE_OK")
        self.assertEqual(rst["last_error"], "FLEX_STATE_OK")

        # Status confirms last error is cleared
        st_clean = self.client.get_status()
        self.assertEqual(st_clean["last_error"], "FLEX_STATE_OK")


class TestEndToEndShutdown(unittest.TestCase):
    """Scenario 4: Graceful shutdown, in-flight operation drain, and context manager cleanup."""

    def test_shutdown_from_idle(self) -> None:
        with FlexBridgeClient() as client:
            res = client.dispatch("exit")
            self.assertIs(res["success"], True)
            self.assertIs(res["shutdown_requested"], True)
            self.assertEqual(res["lifecycle"], "FLEX_LIFECYCLE_STOPPING")
            self.assertEqual(res["active_operation"], "FLEX_OPERATION_NONE")

            # Subsequent commands must be rejected by state manager
            res_after = client.dispatch("chat Attempt after exit")
            self.assertIs(res_after["success"], False)
            self.assertEqual(res_after["state_status"], "FLEX_STATE_ERROR_STOPPING")

    def test_shutdown_with_in_flight_operation_drain(self) -> None:
        with FlexBridgeClient() as client:
            # 1. Start long-running VOICE operation
            res_v = client.dispatch("voice")
            self.assertIs(res_v["operation_started"], True)
            self.assertEqual(res_v["active_operation"], "FLEX_OPERATION_VOICE")

            # 2. Dispatch exit while operation is active
            res_exit = client.dispatch("exit")
            self.assertIs(res_exit["shutdown_requested"], True)
            self.assertEqual(res_exit["lifecycle"], "FLEX_LIFECYCLE_STOPPING")
            # In-flight operation must be preserved, NOT abandoned!
            self.assertEqual(res_exit["active_operation"], "FLEX_OPERATION_VOICE")

            # 3. New commands are rejected
            res_rej = client.dispatch("chat Blocked")
            self.assertIs(res_rej["success"], False)
            self.assertEqual(res_rej["state_status"], "FLEX_STATE_ERROR_STOPPING")

            # 4. In-flight operation is permitted to complete
            fin = client.finish()
            self.assertIs(fin["success"], True)
            self.assertEqual(fin["state_status"], "FLEX_STATE_OK")
            self.assertEqual(fin["lifecycle"], "FLEX_LIFECYCLE_STOPPING")
            self.assertEqual(fin["active_operation"], "FLEX_OPERATION_NONE")

    def test_context_manager_cleans_up_child_process(self) -> None:
        client_ref = None
        saved_proc = None
        with FlexBridgeClient() as client:
            client_ref = client
            saved_proc = client._process
            self.assertTrue(client.is_running)
            client.dispatch("chat In context")
            client.finish()

        self.assertFalse(client_ref.is_running)
        self.assertIsNone(client_ref.pid)
        self.assertIsNotNone(saved_proc)
        self.assertIsNotNone(saved_proc.poll(), "Child process must have terminated")
        self.assertEqual(saved_proc.returncode, 0)


class TestEndToEndProtocolAndInputBoundaries(unittest.TestCase):
    """Scenario 5: Escaping, boundary lengths, multi-byte Unicode, and oversized drainage."""

    def setUp(self) -> None:
        self.client = FlexBridgeClient(timeout=3.0)
        self.client.start()
        self.addCleanup(self.client.close)

    def test_complex_escaping_and_unicode_round_trip(self) -> None:
        payload = 'chat Payload with "double quotes", \'singles\', \\backslashes\\, \ttabs, 🚀 emojis, and हिंदी'
        res = self.client.dispatch(payload)
        self.assertIs(res["success"], True)
        self.assertEqual(
            res["args"],
            'Payload with "double quotes", \'singles\', \\backslashes\\, \ttabs, 🚀 emojis, and हिंदी',
        )
        self.client.finish()

    def test_exact_1024_byte_command_accepted(self) -> None:
        # 'chat ' is 5 bytes. 5 + 1019 'A's = exactly 1024 UTF-8 bytes
        boundary_cmd = "chat " + ("A" * (FLEX_MAX_INPUT_BYTES - 5))
        self.assertEqual(utf8_byte_length(boundary_cmd), FLEX_MAX_INPUT_BYTES)

        res = self.client.dispatch(boundary_cmd)
        self.assertIs(res["success"], True)
        self.assertEqual(res["parser_status"], "FLEX_STATUS_OK")
        self.assertEqual(len(res["args"]), 1019)
        self.client.finish()

    def test_1025_byte_command_rejected_by_parser(self) -> None:
        # 'chat ' is 5 bytes. 5 + 1020 'B's = exactly 1025 UTF-8 bytes (exceeds 1024)
        over_cmd = "chat " + ("B" * (FLEX_MAX_INPUT_BYTES - 4))
        self.assertEqual(utf8_byte_length(over_cmd), FLEX_MAX_INPUT_BYTES + 1)

        res = self.client.dispatch(over_cmd)
        self.assertIs(res["success"], False)
        self.assertEqual(res["parser_status"], "FLEX_STATUS_INPUT_TOO_LONG")
        self.assertEqual(res["state_status"], "FLEX_STATE_OK")

    def test_multibyte_unicode_distinction(self) -> None:
        # Each '€' is 3 UTF-8 bytes. 400 '€'s = 400 characters, but 1200 UTF-8 bytes!
        # Python character count is 405 (well below 1024 characters),
        # but UTF-8 byte length is 5 + 1200 = 1205 bytes (exceeds C limit 1024).
        multibyte_cmd = "chat " + ("€" * 400)
        self.assertEqual(len(multibyte_cmd), 405)
        self.assertGreater(utf8_byte_length(multibyte_cmd), FLEX_MAX_INPUT_BYTES)

        res = self.client.dispatch(multibyte_cmd)
        self.assertIs(res["success"], False)
        self.assertEqual(res["parser_status"], "FLEX_STATUS_INPUT_TOO_LONG")
        self.assertEqual(res["state_status"], "FLEX_STATE_OK")

    def test_oversized_line_rejected_and_drainage_verified(self) -> None:
        # Buffer limit in C bridge is BRIDGE_MAX_LINE_BYTES (2048 bytes).
        # Send a 3000-character line.
        oversized = "chat " + ("Z" * 3000)
        res = self.client.dispatch(oversized)

        self.assertEqual(res["action"], "ERROR")
        self.assertIs(res["success"], False)
        self.assertEqual(res["error"], "REQUEST_TOO_LONG")

        # Crucial drainage test: Subsequent request must succeed immediately
        # without encountering corrupted leftover characters from the oversized line.
        st = self.client.get_status()
        self.assertEqual(st["action"], "STATUS")
        self.assertIs(st["success"], True)
        self.assertEqual(st["lifecycle"], "FLEX_LIFECYCLE_IDLE")


class TestEndToEndProcessFailureAndRecovery(unittest.TestCase):
    """Scenario 6: Process failure, timeout, and explicit state recovery."""

    def test_external_process_kill_raises_process_error(self) -> None:
        client = FlexBridgeClient(auto_start=True)
        try:
            # Kill process externally
            assert client._process is not None
            client._process.kill()
            client._process.wait()

            # Calling dispatch must raise FlexBridgeProcessError
            with self.assertRaises(FlexBridgeProcessError):
                client.dispatch("chat Command after kill")

            # Must NOT silently restart: process must remain dead
            self.assertFalse(client.is_running)
        finally:
            client.close()

    def test_explicit_restart_resets_state(self) -> None:
        client = FlexBridgeClient(auto_start=True)
        try:
            old_pid = client.pid
            client.dispatch("chat Task before restart")
            self.assertEqual(client.active_operation, "FLEX_OPERATION_CHAT")

            # Attempting unforced restart while operation is active must be refused
            with self.assertRaises(FlexBridgeProcessError):
                client.restart(force=False)

            # Explicit force=True restart cleanly reinitializes state
            client.restart(force=True)
            self.assertTrue(client.is_running)
            self.assertNotEqual(client.pid, old_pid)
            self.assertEqual(client.active_operation, "FLEX_OPERATION_NONE")

            st = client.get_status()
            self.assertEqual(st["lifecycle"], "FLEX_LIFECYCLE_IDLE")
            self.assertEqual(st["active_operation"], "FLEX_OPERATION_NONE")
            self.assertEqual(st["operation_id"], 0)
        finally:
            client.close()

    def test_timeout_does_not_silently_claim_success(self) -> None:
        # Controlled test double: a script that sleeps 2.0s
        with tempfile.TemporaryDirectory() as tmpdir:
            script_path = Path(tmpdir) / "fake_slow_bridge.py"
            script_path.write_text(
                textwrap.dedent("""\
                import sys, time
                for line in sys.stdin:
                    time.sleep(2.0)
                    sys.stdout.write('{"action":"STATUS","success":true}\\n')
                    sys.stdout.flush()
                """),
                encoding="utf-8",
            )

            client = FlexBridgeClient(
                executable_path=Path(sys.executable),
                timeout=0.15,
                auto_start=False,
            )
            client._process = __import__("subprocess").Popen(
                [sys.executable, str(script_path)],
                stdin=__import__("subprocess").PIPE,
                stdout=__import__("subprocess").PIPE,
                stderr=__import__("subprocess").PIPE,
            )
            client._stdout_queue = __import__("queue").Queue()
            t_out = __import__("threading").Thread(
                target=lambda: [client._stdout_queue.put(l) for l in iter(client._process.stdout.readline, b"")],
                daemon=True,
            )
            t_out.start()

            try:
                with self.assertRaises(FlexBridgeTimeoutError):
                    client.get_status(timeout=0.15)
            finally:
                client.close()


class TestEndToEndIsolationAndCleanup(unittest.TestCase):
    """Scenario 7: Test isolation, sequential sessions, and clean process teardown."""

    def test_sequential_independent_sessions(self) -> None:
        # Session 1
        with FlexBridgeClient() as c1:
            res1 = c1.dispatch("chat Session 1 task")
            self.assertEqual(res1["operation_id"], 1)
            c1.finish()
            pid1 = c1.pid

        # Session 2
        with FlexBridgeClient() as c2:
            res2 = c2.dispatch("voice")
            # Operation ID in fresh session must start at 1 again
            self.assertEqual(res2["operation_id"], 1)
            c2.finish()
            pid2 = c2.pid

        self.assertNotEqual(pid1, pid2, "Sequential clients must run in distinct processes")

    def test_cleanup_on_exception_inside_context(self) -> None:
        client_ref = None
        try:
            with FlexBridgeClient() as client:
                client_ref = client
                client.dispatch("voice")
                raise RuntimeError("Simulated failure inside caller application")
        except RuntimeError:
            pass

        self.assertIsNotNone(client_ref)
        self.assertFalse(client_ref.is_running, "Process must be terminated even after unhandled exception")
        self.assertTrue(client_ref.closed_with_active_operation)


if __name__ == "__main__":
    unittest.main(verbosity=2)
