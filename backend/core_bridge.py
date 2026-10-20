"""FLEX 1.0 — Python Client for the C Bridge (Stage 1E.2).

Provides `FlexBridgeClient`, a beginner-friendly client that launches and communicates
with the native C bridge (`flex_bridge.exe`) using a line-based request protocol over
standard input/output pipes.

Line Protocol Specification:
    Requests sent to stdin (one line terminated with newline):
        - DISPATCH<TAB><command> : Dispatches <command> to the orchestrator layer.
        - FINISH                 : Explicitly completes the active operation.
        - STATUS                 : Queries lifecycle and active operation status.
        - RESET_ERROR            : Resets recoverable state manager error to OK.

    Responses received on stdout (one JSON object per line terminated with newline):
        - DISPATCH response: {"action":"DISPATCH","success":bool,"parser_status":str,...}
        - FINISH response:   {"action":"FINISH","success":bool,"state_status":str,...}
        - STATUS response:   {"action":"STATUS","success":bool,"lifecycle":str,...}
        - RESET_ERROR resp:  {"action":"RESET_ERROR","success":bool,"state_status":str,...}
        - ERROR response:    {"action":"ERROR","success":false,"error":str,"message":str}

Diagnostics, warnings, and non-protocol outputs are written strictly to stderr by C.

Unicode and Input Length Limits:
    In Python, string length (`len(s)`) measures Unicode code points (characters).
    In C, `command_parser.c` measures UTF-8 encoded byte length against `FLEX_MAX_INPUT_LEN` (1024 bytes).
    Multi-byte characters (such as emojis or accented characters) consume 2 to 4 bytes per character.
    For example:
        - 'A' * 1024 is 1024 characters and 1024 UTF-8 bytes (fits in limit).
        - '€' * 500 is 500 characters, but 1500 UTF-8 bytes (exceeds 1024 byte limit).
    The bridge executable buffer limit (`BRIDGE_BUFFER_SIZE`) is 2048 bytes per line.
    Any command exceeding 1024 UTF-8 bytes will be safely rejected by the C parser with
    `parser_status: "FLEX_STATUS_INPUT_TOO_LONG"`.
"""

from __future__ import annotations

import json
import logging
import os
import queue
import shutil
import subprocess
import threading
from pathlib import Path
from typing import Any, Dict, Optional, Union

logger = logging.getLogger(__name__)

# Protocol & buffer constants from C headers
FLEX_MAX_COMMAND_LEN: int = 64
FLEX_MAX_INPUT_BYTES: int = 1024  # FLEX_MAX_INPUT_LEN in command_parser.h
BRIDGE_MAX_LINE_BYTES: int = 2048  # BRIDGE_BUFFER_SIZE in bridge_main.c
DEFAULT_TIMEOUT: float = 5.0


class FlexBridgeError(Exception):
    """Base exception for all FLEX bridge errors."""
    pass


class FlexBridgeNotFoundError(FlexBridgeError, FileNotFoundError):
    """Raised when the native bridge executable cannot be located."""
    pass


class FlexBridgeProcessError(FlexBridgeError):
    """Raised when process startup, runtime process state, or termination fails."""
    pass


class FlexBridgeTimeoutError(FlexBridgeError, TimeoutError):
    """Raised when an operation or read times out."""
    pass


class FlexBridgeProtocolError(FlexBridgeError):
    """Raised when the bridge returns malformed JSON, premature EOF, or invalid response data."""
    pass


def utf8_byte_length(text: str) -> int:
    """Returns the UTF-8 encoded byte length of a string.

    Note:
        In Python, `len(text)` returns the character (code-point) count.
        The C core enforces a limit of 1024 UTF-8 bytes (`FLEX_MAX_INPUT_LEN`).
        Use this helper to verify byte length for multi-byte Unicode strings.
    """
    return len(text.encode("utf-8"))


def resolve_bridge_path(custom_path: Optional[Union[str, Path]] = None) -> Path:
    """Resolves the absolute path to the `flex_bridge.exe` executable.

    Search order:
        1. Explicitly provided `custom_path`.
        2. Environment variable `FLEX_BRIDGE_EXE`.
        3. Repository root directory (`<repo>/flex_bridge.exe`).
        4. Core C directory (`<repo>/core-c/flex_bridge.exe`).
        5. System PATH lookup for `flex_bridge.exe` or `flex_bridge`.

    Args:
        custom_path: Optional explicit file path to verify and use.

    Returns:
        Path: Resolved absolute path to the executable.

    Raises:
        FlexBridgeNotFoundError: If the executable cannot be located.
    """
    if custom_path is not None:
        p = Path(custom_path).resolve()
        if p.is_file():
            return p
        raise FlexBridgeNotFoundError(
            f"Configured bridge executable not found at specified path: '{p}'"
        )

    env_path = os.environ.get("FLEX_BRIDGE_EXE")
    if env_path:
        p = Path(env_path).resolve()
        if p.is_file():
            return p
        raise FlexBridgeNotFoundError(
            f"Bridge executable specified in FLEX_BRIDGE_EXE not found: '{p}'"
        )

    # Search relative to backend/ directory (repo root is parent of backend)
    repo_root = Path(__file__).resolve().parent.parent

    candidates = [
        repo_root / "flex_bridge.exe",
        repo_root / "flex_bridge",
        repo_root / "core-c" / "flex_bridge.exe",
        repo_root / "core-c" / "flex_bridge",
        repo_root / "build" / "flex_bridge.exe",
    ]

    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()

    # Fallback to PATH search
    which_path = shutil.which("flex_bridge.exe") or shutil.which("flex_bridge")
    if which_path:
        return Path(which_path).resolve()

    raise FlexBridgeNotFoundError(
        f"Could not locate 'flex_bridge.exe'. Searched repository root ({repo_root}) "
        "and system PATH. Please build the bridge using '.\\build_test.ps1' or 'make bridge', "
        "or specify 'executable_path' explicitly."
    )


class FlexBridgeClient:
    """Client for communicating with the native C bridge (`flex_bridge.exe`).

    Maintains a persistent subprocess and communicates over standard input/output
    using the documented line-based protocol. Captures diagnostics from stderr separately.

    Example:
        >>> with FlexBridgeClient() as client:
        ...     res = client.dispatch("chat Hello FLEX")
        ...     print(res["success"], res["args"])
        ...     client.finish()
    """

    def __init__(
        self,
        executable_path: Optional[Union[str, Path]] = None,
        timeout: float = DEFAULT_TIMEOUT,
        auto_start: bool = False,
    ) -> None:
        """Initializes the bridge client.

        Args:
            executable_path: Optional path to `flex_bridge.exe`. If None, automatically resolved.
            timeout: Default read timeout in seconds for bridge responses.
            auto_start: If True, automatically launches the bridge subprocess during initialization.
        """
        self._executable_path = resolve_bridge_path(executable_path)
        self._timeout = float(timeout)

        self._process: Optional[subprocess.Popen[bytes]] = None
        self._stdout_queue: queue.Queue[Optional[bytes]] = queue.Queue()
        self._stderr_lines: list[str] = []
        self._stderr_lock = threading.Lock()
        self._stdout_thread: Optional[threading.Thread] = None
        self._stderr_thread: Optional[threading.Thread] = None

        self._active_operation: Optional[str] = "FLEX_OPERATION_NONE"
        self._lifecycle: Optional[str] = "FLEX_LIFECYCLE_IDLE"
        self._last_status: Optional[Dict[str, Any]] = None
        self._closed_with_active_operation: bool = False

        if auto_start:
            self.start()

    @property
    def executable_path(self) -> Path:
        """Returns the configured executable path."""
        return self._executable_path

    @property
    def timeout(self) -> float:
        """Returns the default request timeout in seconds."""
        return self._timeout

    @property
    def is_running(self) -> bool:
        """Returns True if the bridge subprocess is active and running."""
        return self._process is not None and self._process.poll() is None

    @property
    def pid(self) -> Optional[int]:
        """Returns the PID of the bridge subprocess if running, else None."""
        return self._process.pid if self.is_running else None

    @property
    def active_operation(self) -> Optional[str]:
        """Returns the last known active operation enum name (e.g. 'FLEX_OPERATION_CHAT')."""
        return self._active_operation

    @property
    def lifecycle(self) -> Optional[str]:
        """Returns the last known lifecycle enum name (e.g. 'FLEX_LIFECYCLE_IDLE')."""
        return self._lifecycle

    @property
    def last_status(self) -> Optional[Dict[str, Any]]:
        """Returns the most recent parsed response dictionary from the bridge."""
        return self._last_status

    @property
    def closed_with_active_operation(self) -> bool:
        """Returns True if close() was invoked while an operation was active."""
        return self._closed_with_active_operation

    def get_stderr(self) -> str:
        """Returns all captured diagnostic lines from stderr as a single string."""
        with self._stderr_lock:
            return "".join(self._stderr_lines)

    def start(self) -> "FlexBridgeClient":
        """Launches the persistent `flex_bridge.exe` subprocess.

        Returns:
            self: The client instance.

        Raises:
            FlexBridgeProcessError: If process launch fails or the executable is not executable.
        """
        if self.is_running:
            logger.debug("Bridge client is already running (PID %s).", self.pid)
            return self

        if not self._executable_path.is_file():
            raise FlexBridgeNotFoundError(
                f"Bridge executable does not exist: '{self._executable_path}'"
            )

        try:
            self._process = subprocess.Popen(
                [str(self._executable_path)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=str(self._executable_path.parent),
            )
        except OSError as e:
            raise FlexBridgeProcessError(
                f"Failed to start bridge process '{self._executable_path}': {e}"
            ) from e

        # Clear queues and buffers
        self._stdout_queue = queue.Queue()
        with self._stderr_lock:
            self._stderr_lines = []

        # Start background reader threads
        def _stdout_reader() -> None:
            proc = self._process
            if proc is None or proc.stdout is None:
                return
            try:
                for line in iter(proc.stdout.readline, b""):
                    self._stdout_queue.put(line)
            except Exception:
                pass
            finally:
                self._stdout_queue.put(None)  # Sentinel for EOF

        def _stderr_reader() -> None:
            proc = self._process
            if proc is None or proc.stderr is None:
                return
            try:
                for line in iter(proc.stderr.readline, b""):
                    try:
                        decoded = line.decode("utf-8", errors="replace")
                    except Exception:
                        decoded = str(line)
                    with self._stderr_lock:
                        self._stderr_lines.append(decoded)
            except Exception:
                pass

        self._stdout_thread = threading.Thread(
            target=_stdout_reader, daemon=True, name=f"FlexBridge-StdoutReader-{self.pid}"
        )
        self._stderr_thread = threading.Thread(
            target=_stderr_reader, daemon=True, name=f"FlexBridge-StderrReader-{self.pid}"
        )
        self._stdout_thread.start()
        self._stderr_thread.start()

        # Check immediate crash
        if self._process.poll() is not None:
            stderr_out = self.get_stderr()
            raise FlexBridgeProcessError(
                f"Bridge process terminated immediately with exit code {self._process.returncode}: {stderr_out}"
            )

        self._active_operation = "FLEX_OPERATION_NONE"
        self._lifecycle = "FLEX_LIFECYCLE_IDLE"
        self._closed_with_active_operation = False
        logger.info("Started bridge subprocess with PID %d", self._process.pid)
        return self

    def _send_and_receive(self, payload_str: str, timeout: Optional[float] = None) -> Dict[str, Any]:
        """Internal helper to write a line to stdin and read the JSON response.

        Args:
            payload_str: The protocol line without trailing newline.
            timeout: Optional override for read timeout.

        Returns:
            dict: The parsed JSON response object from the bridge.

        Raises:
            FlexBridgeProcessError: If the process is dead or write fails.
            FlexBridgeTimeoutError: If read times out.
            FlexBridgeProtocolError: If EOF, malformed JSON, or non-dict response is received.
        """
        if not self.is_running:
            raise FlexBridgeProcessError(
                "Bridge process is not running. Call start() before sending commands."
            )

        proc = self._process
        assert proc is not None and proc.stdin is not None

        effective_timeout = timeout if timeout is not None else self._timeout

        # Ensure newline termination
        payload_bytes = payload_str.encode("utf-8")
        if not payload_bytes.endswith(b"\n"):
            payload_bytes += b"\n"

        # 1. Write request to stdin
        try:
            proc.stdin.write(payload_bytes)
            proc.stdin.flush()
        except (BrokenPipeError, OSError) as e:
            exit_code = proc.poll()
            stderr_info = self.get_stderr()
            raise FlexBridgeProcessError(
                f"Failed to write to bridge process (broken pipe, exit code {exit_code}): {stderr_info}"
            ) from e

        # 2. Read response from stdout queue with timeout
        try:
            raw_line = self._stdout_queue.get(timeout=effective_timeout)
        except queue.Empty as e:
            exit_code = proc.poll()
            stderr_info = self.get_stderr()
            if exit_code is not None:
                raise FlexBridgeProcessError(
                    f"Bridge process terminated unexpectedly (exit code {exit_code}) while waiting for response: {stderr_info}"
                ) from e
            raise FlexBridgeTimeoutError(
                f"Timed out after {effective_timeout:.2f}s waiting for response from bridge."
            ) from e

        # 3. Check for EOF
        if raw_line is None or len(raw_line) == 0:
            exit_code = proc.poll()
            stderr_info = self.get_stderr()
            raise FlexBridgeProtocolError(
                f"Received unexpected EOF from bridge process (exit code {exit_code}): {stderr_info}"
            )

        # 4. Decode UTF-8 and parse JSON
        try:
            decoded = raw_line.decode("utf-8").strip()
            data = json.loads(decoded)
        except (UnicodeDecodeError, json.JSONDecodeError) as e:
            raise FlexBridgeProtocolError(
                f"Failed to decode bridge JSON response: {raw_line!r}"
            ) from e

        if not isinstance(data, dict):
            raise FlexBridgeProtocolError(
                f"Expected JSON object (dict) from bridge, got {type(data).__name__}: {data!r}"
            )

        # 5. Update cached state
        if "active_operation" in data:
            self._active_operation = data["active_operation"]
        if "lifecycle" in data:
            self._lifecycle = data["lifecycle"]
        self._last_status = data

        return data

    def dispatch(self, command: str, timeout: Optional[float] = None) -> Dict[str, Any]:
        """Dispatches a command line to the bridge orchestrator.

        Protocol framing:
            `DISPATCH<TAB><command>`

        Args:
            command: The command string (e.g. 'chat Explain pointers in C', 'voice', 'exit').
                     Must not contain newline characters ('\\n' or '\\r').
            timeout: Optional read timeout in seconds.

        Returns:
            dict: The parsed JSON response object from the bridge.
                  Keys typically include 'action', 'success', 'parser_status', 'state_status',
                  'command_type', 'operation', 'operation_id', 'operation_started',
                  'shutdown_requested', 'command_name', 'args', 'lifecycle', 'active_operation'.

        Raises:
            TypeError: If command is not a string.
            ValueError: If command contains newline characters.
            FlexBridgeProcessError: If the bridge process is not running or crashed.
            FlexBridgeTimeoutError: If response read times out.
            FlexBridgeProtocolError: If response cannot be parsed.
        """
        if not isinstance(command, str):
            raise TypeError(f"Command must be a str, got {type(command).__name__}")
        if "\n" in command or "\r" in command:
            raise ValueError("Command must not contain newline characters ('\\n' or '\\r').")

        return self._send_and_receive(f"DISPATCH\t{command}", timeout=timeout)

    def finish(self, timeout: Optional[float] = None) -> Dict[str, Any]:
        """Explicitly finishes the currently active operation.

        Protocol framing:
            `FINISH`

        Returns:
            dict: The parsed JSON response object.
                  Keys include 'action', 'success', 'state_status', 'lifecycle',
                  'active_operation', 'operation_id'.
        """
        return self._send_and_receive("FINISH", timeout=timeout)

    def get_status(self, timeout: Optional[float] = None) -> Dict[str, Any]:
        """Queries the current lifecycle state, active operation, and last error.

        Protocol framing:
            `STATUS`

        Returns:
            dict: The parsed JSON response object.
                  Keys include 'action', 'success', 'lifecycle', 'active_operation',
                  'operation_id', 'last_error'.
        """
        return self._send_and_receive("STATUS", timeout=timeout)

    def reset_error(self, timeout: Optional[float] = None) -> Dict[str, Any]:
        """Resets recoverable state manager error to FLEX_STATE_OK.

        Protocol framing:
            `RESET_ERROR`

        Returns:
            dict: The parsed JSON response object.
                  Keys include 'action', 'success', 'state_status', 'last_error'.
        """
        return self._send_and_receive("RESET_ERROR", timeout=timeout)

    def restart(self, force: bool = False, timeout: Optional[float] = None) -> "FlexBridgeClient":
        """Explicitly restarts the bridge process.

        WARNING:
            Restarting terminates the C process and permanently loses all
            C lifecycle state, active operation context, and operation IDs.
            This method is intentionally explicit; silent auto-restarts are
            never performed.

        Args:
            force: If False and an operation is known to be active, raises
                   FlexBridgeProcessError to prevent accidental state loss.
                   If True, forces restart regardless of active operation.
            timeout: Optional shutdown timeout.

        Returns:
            self: The restarted client instance.

        Raises:
            FlexBridgeProcessError: If restart is refused because an operation is active.
        """
        if not force and self.is_running and self._active_operation not in (None, "FLEX_OPERATION_NONE"):
            raise FlexBridgeProcessError(
                f"Cannot restart bridge while operation '{self._active_operation}' is active. "
                "Call finish() first, or pass force=True to abandon the operation and reset state."
            )

        logger.warning(
            "Explicit bridge restart invoked. Resetting C lifecycle state and operation context."
        )
        self.close(timeout=timeout if timeout is not None else 2.0)
        return self.start()

    def close(self, timeout: float = 2.0) -> None:
        """Closes the bridge client and terminates the subprocess.

        Lifecycle and Active Operation Behavior:
            - If an operation is currently active (`active_operation != 'FLEX_OPERATION_NONE'`),
              calling `close()` does NOT pretend the operation finished.
              It logs a warning, sets `closed_with_active_operation = True`, and closes `stdin`,
              which triggers the C bridge's clean EOF warning.
            - Uses a bounded timeout (`timeout`) to wait for process termination.
            - If the process fails to exit within `timeout`, `terminate()` and `kill()`
              are invoked to guarantee that `close()` never hangs indefinitely.

        Args:
            timeout: Maximum seconds to wait for graceful process exit before forceful termination.
        """
        if self._process is None:
            return

        proc = self._process

        # Record active operation status
        if self._active_operation not in (None, "FLEX_OPERATION_NONE"):
            self._closed_with_active_operation = True
            logger.warning(
                "Closing bridge client while operation '%s' was active. Operation was not finished.",
                self._active_operation,
            )

        # 1. Close stdin to signal EOF to the C process
        if proc.stdin and not proc.stdin.closed:
            try:
                proc.stdin.close()
            except Exception as e:
                logger.debug("Error closing stdin: %s", e)

        # 2. Wait bounded timeout for process to exit
        try:
            proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            logger.warning("Bridge process did not terminate within %.2fs; killing.", timeout)
            try:
                proc.terminate()
                proc.wait(timeout=1.0)
            except (subprocess.TimeoutExpired, OSError):
                try:
                    proc.kill()
                    proc.wait(timeout=1.0)
                except OSError:
                    pass

        # 3. Wait for reader threads to finish
        if self._stdout_thread and self._stdout_thread.is_alive():
            self._stdout_thread.join(timeout=1.0)
        if self._stderr_thread and self._stderr_thread.is_alive():
            self._stderr_thread.join(timeout=1.0)

        # 4. Close pipes
        if proc.stdout and not proc.stdout.closed:
            try:
                proc.stdout.close()
            except Exception:
                pass
        if proc.stderr and not proc.stderr.closed:
            try:
                proc.stderr.close()
            except Exception:
                pass

        logger.info("Bridge client closed (PID %s, exit code %s).", proc.pid, proc.returncode)
        self._process = None

    def __enter__(self) -> "FlexBridgeClient":
        """Context-manager entry: ensures bridge is started and returns client."""
        if not self.is_running:
            self.start()
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        """Context-manager exit: cleanly closes the bridge client."""
        self.close()
