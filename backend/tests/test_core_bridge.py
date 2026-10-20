"""FLEX 1.0 — Stage 1E.2 Unit Tests for FlexBridgeClient.

Tests:
    - Executable path resolution and environment variable overrides.
    - Live C bridge integration: dispatch, argument preservation, finish, status, reset_error.
    - Rejection of concurrent operations while one is active.
    - Distinguishing parser syntax errors from state manager errors.
    - Shutdown and drain behavior (preserving in-flight operations).
    - Unicode arguments and command-length boundaries (Python chars vs UTF-8 bytes).
    - Client close, active operation warnings, and context-manager cleanup.
    - Controlled failure modes: malformed JSON, premature EOF, read timeouts, broken pipe,
      and verification that the client never silently restarts.
"""

from __future__ import annotations

import os
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

# Ensure repo root and backend/ are importable
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))

from backend.core_bridge import (
    DEFAULT_TIMEOUT,
    FLEX_MAX_INPUT_BYTES,
    FlexBridgeClient,
    FlexBridgeError,
    FlexBridgeNotFoundError,
    FlexBridgeProcessError,
    FlexBridgeProtocolError,
    FlexBridgeTimeoutError,
    resolve_bridge_path,
    utf8_byte_length,
)


class TestExecutablePathResolution(unittest.TestCase):
    """Tests for bridge executable path discovery and resolution."""

    def test_default_resolution_finds_exe(self) -> None:
        path = resolve_bridge_path()
        self.assertIsInstance(path, Path)
        self.assertTrue(path.is_file(), f"Resolved path '{path}' is not a file")
        self.assertIn("flex_bridge", path.name.lower())

    def test_explicit_valid_path(self) -> None:
        real_path = resolve_bridge_path()
        resolved = resolve_bridge_path(custom_path=real_path)
        self.assertEqual(real_path, resolved)

    def test_explicit_nonexistent_path_raises(self) -> None:
        bogus = REPO_ROOT / "nonexistent_bridge_binary_xyz.exe"
        with self.assertRaises(FlexBridgeNotFoundError):
            resolve_bridge_path(custom_path=bogus)

    def test_environment_variable_override(self) -> None:
        real_path = resolve_bridge_path()
        old_env = os.environ.get("FLEX_BRIDGE_EXE")
        try:
            os.environ["FLEX_BRIDGE_EXE"] = str(real_path)
            resolved = resolve_bridge_path()
            self.assertEqual(real_path, resolved)
        finally:
            if old_env is not None:
                os.environ["FLEX_BRIDGE_EXE"] = old_env
            else:
                os.environ.pop("FLEX_BRIDGE_EXE", None)

    def test_environment_variable_invalid_raises(self) -> None:
        old_env = os.environ.get("FLEX_BRIDGE_EXE")
        try:
            os.environ["FLEX_BRIDGE_EXE"] = "C:\\definitely_not_a_valid_bridge_exe.exe"
            with self.assertRaises(FlexBridgeNotFoundError):
                resolve_bridge_path()
        finally:
            if old_env is not None:
                os.environ["FLEX_BRIDGE_EXE"] = old_env
            else:
                os.environ.pop("FLEX_BRIDGE_EXE", None)


class TestLiveBridgeOperations(unittest.TestCase):
    """Integration tests with the live compiled `flex_bridge.exe`."""

    def setUp(self) -> None:
        self.client = FlexBridgeClient(timeout=3.0)
        self.client.start()

    def tearDown(self) -> None:
        self.client.close(timeout=1.0)

    def test_initial_status(self) -> None:
        st = self.client.get_status()
        self.assertEqual(st.get("action"), "STATUS")
        self.assertTrue(st.get("success"))
        self.assertEqual(st.get("lifecycle"), "FLEX_LIFECYCLE_IDLE")
        self.assertEqual(st.get("active_operation"), "FLEX_OPERATION_NONE")
        self.assertEqual(st.get("operation_id"), 0)
        self.assertEqual(st.get("last_error"), "FLEX_STATE_OK")

    def test_successful_dispatch_and_argument_preservation(self) -> None:
        res = self.client.dispatch("chat Explain pointers in C")
        self.assertEqual(res.get("action"), "DISPATCH")
        self.assertTrue(res.get("success"))
        self.assertEqual(res.get("parser_status"), "FLEX_STATUS_OK")
        self.assertEqual(res.get("state_status"), "FLEX_STATE_OK")
        self.assertEqual(res.get("command_type"), "FLEX_COMMAND_CHAT")
        self.assertEqual(res.get("operation"), "FLEX_OPERATION_CHAT")
        self.assertEqual(res.get("operation_id"), 1)
        self.assertTrue(res.get("operation_started"))
        self.assertFalse(res.get("shutdown_requested"))
        self.assertEqual(res.get("command_name"), "chat")
        self.assertEqual(res.get("args"), "Explain pointers in C")
        self.assertEqual(res.get("lifecycle"), "FLEX_LIFECYCLE_PROCESSING")
        self.assertEqual(res.get("active_operation"), "FLEX_OPERATION_CHAT")

        # Verify client cached state
        self.assertEqual(self.client.active_operation, "FLEX_OPERATION_CHAT")
        self.assertEqual(self.client.lifecycle, "FLEX_LIFECYCLE_PROCESSING")

        # Finish operation cleanly
        fin = self.client.finish()
        self.assertTrue(fin.get("success"))
        self.assertEqual(fin.get("lifecycle"), "FLEX_LIFECYCLE_IDLE")
        self.assertEqual(fin.get("active_operation"), "FLEX_OPERATION_NONE")

    def test_argument_preservation_with_quotes_and_slashes(self) -> None:
        cmd = 'chat "arg with quotes" and \\backslashes\\ and \ttab'
        res = self.client.dispatch(cmd)
        self.assertTrue(res.get("success"))
        self.assertEqual(res.get("args"), '"arg with quotes" and \\backslashes\\ and \ttab')
        self.client.finish()

    def test_explicit_finish_and_status(self) -> None:
        self.client.dispatch("voice")
        st_mid = self.client.get_status()
        self.assertEqual(st_mid.get("active_operation"), "FLEX_OPERATION_VOICE")
        self.assertEqual(st_mid.get("lifecycle"), "FLEX_LIFECYCLE_PROCESSING")

        fin = self.client.finish()
        self.assertTrue(fin.get("success"))
        self.assertEqual(fin.get("state_status"), "FLEX_STATE_OK")
        self.assertEqual(fin.get("active_operation"), "FLEX_OPERATION_NONE")
        self.assertEqual(fin.get("lifecycle"), "FLEX_LIFECYCLE_IDLE")

        st_after = self.client.get_status()
        self.assertEqual(st_after.get("active_operation"), "FLEX_OPERATION_NONE")
        self.assertEqual(st_after.get("lifecycle"), "FLEX_LIFECYCLE_IDLE")

    def test_rejection_of_second_operation_while_active(self) -> None:
        res1 = self.client.dispatch("chat First Operation")
        self.assertTrue(res1.get("success"))

        # Attempting second operation must be rejected by state manager
        res2 = self.client.dispatch("voice")
        self.assertFalse(res2.get("success"))
        self.assertEqual(res2.get("state_status"), "FLEX_STATE_ERROR_ALREADY_PROCESSING")
        self.assertFalse(res2.get("operation_started"))
        self.assertEqual(res2.get("active_operation"), "FLEX_OPERATION_CHAT")

        # Finish original operation cleanly
        self.client.finish()

    def test_double_finish_rejection(self) -> None:
        # Client starts in IDLE, so finish must fail
        fin = self.client.finish()
        self.assertFalse(fin.get("success"))
        self.assertEqual(fin.get("state_status"), "FLEX_STATE_ERROR_NOT_PROCESSING")

    def test_reset_error(self) -> None:
        # Trigger an error in state manager via double finish
        self.client.finish()
        st_before = self.client.get_status()
        self.assertEqual(st_before.get("last_error"), "FLEX_STATE_ERROR_NOT_PROCESSING")

        # Reset error
        rst = self.client.reset_error()
        self.assertTrue(rst.get("success"))
        self.assertEqual(rst.get("state_status"), "FLEX_STATE_OK")
        self.assertEqual(rst.get("last_error"), "FLEX_STATE_OK")

        st_after = self.client.get_status()
        self.assertEqual(st_after.get("last_error"), "FLEX_STATE_OK")

    def test_unknown_command_handling(self) -> None:
        res = self.client.dispatch("unrecognized_action_verb")
        self.assertFalse(res.get("success"))
        self.assertEqual(res.get("parser_status"), "FLEX_STATUS_UNKNOWN_COMMAND")
        self.assertEqual(res.get("state_status"), "FLEX_STATE_OK")

    def test_empty_command_handling(self) -> None:
        res = self.client.dispatch("")
        self.assertFalse(res.get("success"))
        self.assertEqual(res.get("parser_status"), "FLEX_STATUS_EMPTY_INPUT")
        self.assertEqual(res.get("state_status"), "FLEX_STATE_OK")

    def test_newline_in_command_rejected_by_client(self) -> None:
        with self.assertRaises(ValueError):
            self.client.dispatch("chat line1\nline2")
        with self.assertRaises(ValueError):
            self.client.dispatch("chat line1\rline2")

    def test_type_error_on_non_string_command(self) -> None:
        with self.assertRaises(TypeError):
            self.client.dispatch(12345)  # type: ignore


class TestShutdownAndDrainBehavior(unittest.TestCase):
    """Tests shutdown sequencing and active operation drain."""

    def test_shutdown_with_in_flight_operation(self) -> None:
        with FlexBridgeClient() as client:
            # 1. Start operation
            res_op = client.dispatch("voice")
            self.assertTrue(res_op.get("operation_started"))
            self.assertEqual(res_op.get("active_operation"), "FLEX_OPERATION_VOICE")

            # 2. Dispatch exit while operation is in flight
            res_exit = client.dispatch("exit")
            self.assertTrue(res_exit.get("shutdown_requested"))
            self.assertEqual(res_exit.get("lifecycle"), "FLEX_LIFECYCLE_STOPPING")
            # In-flight operation must NOT be abandoned silently!
            self.assertEqual(res_exit.get("active_operation"), "FLEX_OPERATION_VOICE")

            # 3. New operation while STOPPING must be rejected
            res_new = client.dispatch("chat new_task")
            self.assertFalse(res_new.get("success"))
            self.assertEqual(res_new.get("state_status"), "FLEX_STATE_ERROR_STOPPING")

            # 4. Caller finishes in-flight operation explicitly
            res_fin = client.finish()
            self.assertTrue(res_fin.get("success"))
            self.assertEqual(res_fin.get("lifecycle"), "FLEX_LIFECYCLE_STOPPING")
            self.assertEqual(res_fin.get("active_operation"), "FLEX_OPERATION_NONE")

            # 5. Final status verifies shutdown state
            st = client.get_status()
            self.assertEqual(st.get("lifecycle"), "FLEX_LIFECYCLE_STOPPING")
            self.assertEqual(st.get("active_operation"), "FLEX_OPERATION_NONE")


class TestUnicodeAndBoundaries(unittest.TestCase):
    """Tests Unicode encoding differences and command length boundaries."""

    def test_utf8_byte_length_vs_char_length(self) -> None:
        # ASCII: 1 char = 1 byte
        self.assertEqual(utf8_byte_length("hello"), 5)
        self.assertEqual(len("hello"), 5)

        # Multi-byte: 1 emoji = 4 bytes, len = 1 char
        rocket = "🚀"
        self.assertEqual(len(rocket), 1)
        self.assertEqual(utf8_byte_length(rocket), 4)

        # Multi-byte Indian Devanagari script: 1 char = 3 bytes
        namaste = "नमस्ते"
        self.assertGreater(utf8_byte_length(namaste), len(namaste))

    def test_unicode_arguments_preserved(self) -> None:
        with FlexBridgeClient() as client:
            unicode_str = "Bonjour le monde 🌍 and नमस्ते and こんにちは"
            res = client.dispatch(f"chat {unicode_str}")
            self.assertTrue(res.get("success"))
            self.assertEqual(res.get("args"), unicode_str)
            client.finish()

    def test_overlong_command_rejected_by_c_parser(self) -> None:
        # C command limit is FLEX_MAX_INPUT_BYTES (1024 bytes)
        with FlexBridgeClient() as client:
            # Construct command exceeding 1024 bytes
            long_args = "X" * (FLEX_MAX_INPUT_BYTES - 2)
            long_cmd = f"chat {long_args}"  # 5 + 1022 = 1027 bytes
            self.assertGreater(utf8_byte_length(long_cmd), FLEX_MAX_INPUT_BYTES)

            res = client.dispatch(long_cmd)
            self.assertFalse(res.get("success"))
            self.assertEqual(res.get("parser_status"), "FLEX_STATUS_INPUT_TOO_LONG")
            self.assertEqual(res.get("state_status"), "FLEX_STATE_OK")

    def test_oversized_line_rejected_by_bridge_buffer(self) -> None:
        # Bridge line buffer is BRIDGE_MAX_LINE_BYTES (2048 bytes)
        with FlexBridgeClient() as client:
            oversized_cmd = "chat " + ("Y" * 2500)
            res = client.dispatch(oversized_cmd)
            self.assertEqual(res.get("action"), "ERROR")
            self.assertEqual(res.get("error"), "REQUEST_TOO_LONG")

            # Crucial: verify subsequent request is processed cleanly (drainage test)
            st = client.get_status()
            self.assertEqual(st.get("action"), "STATUS")
            self.assertTrue(st.get("success"))


class TestLifecycleAndContextManager(unittest.TestCase):
    """Tests lifecycle management, close semantics, and context managers."""

    def test_context_manager_lifecycle(self) -> None:
        with FlexBridgeClient() as client:
            self.assertTrue(client.is_running)
            self.assertIsNotNone(client.pid)
            res = client.dispatch("chat In context")
            self.assertTrue(res.get("success"))
            client.finish()

        self.assertFalse(client.is_running)
        self.assertIsNone(client.pid)

    def test_close_with_active_operation(self) -> None:
        client = FlexBridgeClient(auto_start=True)
        try:
            client.dispatch("memory Start active operation")
            self.assertEqual(client.active_operation, "FLEX_OPERATION_MEMORY")
            # Close while active
            client.close()
            self.assertTrue(client.closed_with_active_operation)
            self.assertFalse(client.is_running)
        finally:
            client.close()

    def test_explicit_restart_and_no_silent_restart(self) -> None:
        client = FlexBridgeClient(auto_start=True)
        try:
            old_pid = client.pid
            client.get_status()

            # Clean restart when idle
            client.restart()
            self.assertTrue(client.is_running)
            self.assertNotEqual(old_pid, client.pid)

            # Attempt restart while operation active without force must fail
            client.dispatch("chat Active task")
            with self.assertRaises(FlexBridgeProcessError):
                client.restart(force=False)

            # Force restart allows explicit discard
            client.restart(force=True)
            self.assertEqual(client.active_operation, "FLEX_OPERATION_NONE")
        finally:
            client.close()

    def test_send_when_not_started_raises(self) -> None:
        client = FlexBridgeClient(auto_start=False)
        with self.assertRaises(FlexBridgeProcessError):
            client.get_status()
        with self.assertRaises(FlexBridgeProcessError):
            client.dispatch("chat test")
        with self.assertRaises(FlexBridgeProcessError):
            client.finish()
        with self.assertRaises(FlexBridgeProcessError):
            client.reset_error()


class TestControlledFailureModes(unittest.TestCase):
    """Deterministic failure tests using controlled fake subprocesses."""

    def test_malformed_json_response_raises_protocol_error(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            script_path = Path(tmpdir) / "fake_bridge_bad_json.py"
            script_path.write_text(
                textwrap.dedent("""\
                import sys
                for line in sys.stdin:
                    sys.stdout.write("THIS_IS_DEFINITELY_NOT_JSON\\n")
                    sys.stdout.flush()
                """),
                encoding="utf-8",
            )

            # Wrapper script or executable
            client = FlexBridgeClient(
                executable_path=Path(sys.executable),
                timeout=2.0,
                auto_start=False,
            )
            # Override process launch to run Python fake bridge script
            client._executable_path = Path(sys.executable)
            client.start()
            # Replace args in running process
            client.close()
            # Launch via subprocess wrapper
            client._process = __import__("subprocess").Popen(
                [sys.executable, str(script_path)],
                stdin=__import__("subprocess").PIPE,
                stdout=__import__("subprocess").PIPE,
                stderr=__import__("subprocess").PIPE,
            )
            # Reconnect reader threads
            client._stdout_queue = __import__("queue").Queue()
            t_out = __import__("threading").Thread(
                target=lambda: [client._stdout_queue.put(l) for l in iter(client._process.stdout.readline, b"")],
                daemon=True,
            )
            t_out.start()

            try:
                with self.assertRaises(FlexBridgeProtocolError):
                    client.get_status()
            finally:
                client.close()

    def test_premature_eof_raises_protocol_error(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            script_path = Path(tmpdir) / "fake_bridge_eof.py"
            script_path.write_text(
                textwrap.dedent("""\
                import sys
                # Read one line and immediately exit without outputting
                sys.stdin.readline()
                sys.exit(0)
                """),
                encoding="utf-8",
            )

            client = FlexBridgeClient(
                executable_path=Path(sys.executable),
                timeout=2.0,
                auto_start=False,
            )
            client._process = __import__("subprocess").Popen(
                [sys.executable, str(script_path)],
                stdin=__import__("subprocess").PIPE,
                stdout=__import__("subprocess").PIPE,
                stderr=__import__("subprocess").PIPE,
            )
            client._stdout_queue = __import__("queue").Queue()
            def _reader() -> None:
                try:
                    for l in iter(client._process.stdout.readline, b""):
                        client._stdout_queue.put(l)
                finally:
                    client._stdout_queue.put(None)

            t_out = __import__("threading").Thread(target=_reader, daemon=True)
            t_out.start()

            try:
                with self.assertRaises(FlexBridgeProtocolError):
                    client.get_status()
            finally:
                client.close()

    def test_timeout_handling_raises_timeout_error(self) -> None:
        with tempfile.TemporaryDirectory() as tmpdir:
            script_path = Path(tmpdir) / "fake_bridge_sleep.py"
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
                timeout=0.2,  # Bounded short timeout
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
                    client.get_status(timeout=0.2)
            finally:
                client.close()

    def test_broken_pipe_and_no_silent_restart(self) -> None:
        client = FlexBridgeClient(auto_start=True)
        try:
            # Force kill the process externally to simulate a crash/broken pipe
            assert client._process is not None
            client._process.kill()
            client._process.wait()

            # Attempting to dispatch must raise FlexBridgeProcessError
            with self.assertRaises(FlexBridgeProcessError):
                client.dispatch("chat Test after crash")

            # Must NOT have silently restarted: is_running remains False
            self.assertFalse(client.is_running)
        finally:
            client.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
