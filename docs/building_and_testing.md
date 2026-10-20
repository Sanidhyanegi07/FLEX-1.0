# FLEX 1.0 — Building and Testing Core C

## Prerequisites
- **C Compiler**: GCC with C11 support (e.g. MinGW-w64 or MinGW GCC 6.3.0+)
- **Environment**: Windows PowerShell or standard POSIX Make / MinGW make

## How to Build and Run Tests

### Option 1: PowerShell (Recommended on Windows)
Run the automated build and test runner script from the repository root:

```powershell
.\build_test.ps1
```

To clean previous artifacts before building:
```powershell
.\build_test.ps1 -Clean
```

### Option 2: GNU Make / MinGW Make
From the repository root:

```sh
# Using GNU Make
make test

# Or using MinGW Make
mingw32-make test
```

To clean up built binaries:
```sh
make clean
```

### Option 3: Direct GCC Command
```powershell
# Compile and run test suite:
gcc -std=c11 -Wall -Wextra -Werror -pedantic -I core-c/include core-c/src/command_parser.c core-c/src/state_manager.c core-c/src/orchestrator.c core-c/tests/test_command_parser.c core-c/tests/test_state_manager.c core-c/tests/test_orchestrator.c -o test_command_parser.exe
.\test_command_parser.exe

# Compile bridge executable:
gcc -std=c11 -Wall -Wextra -Werror -pedantic -I core-c/include core-c/src/command_parser.c core-c/src/state_manager.c core-c/src/orchestrator.c core-c/src/bridge_main.c -o flex_bridge.exe

# Run bridge smoke tests:
powershell -ExecutionPolicy Bypass -File .\test_bridge.ps1
```

## Bridge CLI Protocol (Stage 1E.1)
`flex_bridge.exe` is a native C11 line-based IPC bridge designed for communication over stdin/stdout with callers such as Python or automated test suites.

### Line-Based Request Framing
- Requests are fed line-by-line via `stdin`, terminated by `\n` or `\r\n`.
- Supported actions:
  - `DISPATCH<TAB><command>`: Dispatches `<command>` to the orchestrator layer. `<TAB>` is a single literal tab (`\t`) character.
  - `FINISH`: Explicitly finishes the currently active operation.
  - `STATUS`: Reports current lifecycle state, active operation, operation ID, and last error.
  - `RESET_ERROR`: Resets any recoverable state manager error to `FLEX_STATE_OK`.
- Every request produces **exactly one JSON object** on `stdout` followed by `\n` and immediate flush.
- Diagnostics, warnings, and error messages are written exclusively to `stderr`, keeping `stdout` clean and JSON-parseable.
- Overlong input lines exceeding `BRIDGE_BUFFER_SIZE` (2048 bytes) are safely detected, rejected with an error JSON (`REQUEST_TOO_LONG`), and drained until newline without corrupting subsequent requests.

### Graceful Shutdown and Lifecycle
- The bridge process maintains a single caller-owned `FlexStateManager` throughout its lifetime.
- Dispatching `exit` requests shutdown (`shutdown_requested = true`), moving the lifecycle to `STOPPING`.
- In-flight operations are **not** abruptly cancelled; callers must send `FINISH` to complete them.
- New operations are rejected with `FLEX_STATE_ERROR_STOPPING` while in `STOPPING`.
- Clean termination occurs upon standard input `EOF` with exit code 0. If EOF occurs while an operation is still active, a diagnostic warning is emitted to `stderr`.

## Test Coverage
The test suite (`core-c/tests/test_command_parser.c`, `core-c/tests/test_state_manager.c`, `core-c/tests/test_orchestrator.c`, and `test_bridge.ps1`) validates:

### Stage 1A Coverage
1. **Command Recognition**: Exact matching of `chat`, `voice`, `memory`, and `exit`.
2. **Case Insensitivity**: Mixed-case variations (e.g., `CHAT`, `cHaT`, `Voice`, `eXiT`).
3. **Whitespace Resilience**: Leading, trailing, tab, and newline whitespace handling.
4. **Unknown Input Rejection**: Empty strings, invalid words, partial prefix matches, and unsupported trailing text.
5. **Null Safety**: `NULL` pointer input safely returning `FLEX_COMMAND_UNKNOWN`.

### Stage 1B Coverage
1. **Structured Results**: `FlexCommandResult` with normalized `name`, `args`, `args_len`, `has_args`, `type`, and `status`.
2. **Status Codes**: `FLEX_STATUS_OK`, `FLEX_STATUS_UNKNOWN_COMMAND`, `FLEX_STATUS_EMPTY_INPUT`, `FLEX_STATUS_INVALID_INPUT`, and `FLEX_STATUS_INPUT_TOO_LONG`.
3. **Argument Parsing**: Extraction and trimming of command arguments across multiple words (e.g., `chat Explain pointers in C`), preserving internal spaces.
4. **Buffer & Memory Safety**: Explicit limits (`FLEX_MAX_INPUT_LEN = 1024`, `FLEX_MAX_COMMAND_LEN = 64`, `FLEX_MAX_ARGS_LEN = 1024`), bounded string scans, no heap allocation, and safe handling of boundary and oversized inputs.
5. **Backward Compatibility**: Unmodified behavior for legacy `flex_parse_command()` across all Stage 1A assertions.
6. **Extended APIs & Helpers**: `flex_parse_command_line()`, `flex_parse_command_ex()`, `flex_command_type_to_string()`, and `flex_command_status_to_string()`.

### Stage 1C Coverage
1. **Deterministic Initialization**: `flex_state_init()` sets lifecycle to `IDLE`, operation to `NONE`, operation ID to 0, and status to `FLEX_STATE_OK`.
2. **Safe Transitions**: Explicit validation for beginning and finishing `CHAT`, `VOICE`, and `MEMORY` operations.
3. **State Integrity**: Rejected transitions (such as starting while already `PROCESSING`, starting during `STOPPING`, or finishing while `IDLE`) preserve all state fields without corruption.
4. **Graceful Shutdown**: Requesting shutdown from `IDLE` stops immediately; requesting shutdown while `PROCESSING` allows the in-flight operation to finish cleanly before halting further operations.
5. **Monotonic Operation IDs & Overflow Safety**: Operation IDs increment by 1 on each operation start, remain unchanged on finish, and reject transitions at `UINT64_MAX` with `FLEX_STATE_ERROR_ID_OVERFLOW`.
6. **Error Recovery**: `flex_state_reset_error()` resets recoverable error codes without altering lifecycle or operation IDs.
7. **Null Safety & Mapping Helpers**: Guarding all state manager pointers against `NULL` and validating command-to-operation mapping (`flex_operation_from_command()`).

### Stage 1D Coverage
1. **Command Orchestration**: Unified `flex_dispatch_command()` connecting command parsing with state manager transitions.
2. **Structured Dispatch Result**: `FlexDispatchResult` containing `parser_status`, `state_status`, `command_type`, `operation`, `operation_id`, `operation_started`, `shutdown_requested`, and full `command_result`.
3. **Operation Lifecycle Enforcement**: Starting `CHAT`, `VOICE`, `MEMORY` only when input syntax is valid and state is `IDLE`.
4. **Concurrent Operation Rejection**: Rejecting attempts to start an operation while another is active, maintaining state integrity without advancing operation IDs.
5. **Explicit Completion**: `flex_dispatch_finish_operation()` completing operations without auto-finishing on successful parse.
6. **Graceful Shutdown Integration**: Mapping `exit` commands to shutdown requests while preserving in-flight operations until finished.
7. **Error Differentiation**: Distinguishing parser syntax errors (`parser_status`) from state transition errors (`state_status`).
8. **Null & Overflow Safety**: Guarding against NULL inputs/state pointers and cleanly reporting operation ID overflow through `FLEX_STATE_ERROR_ID_OVERFLOW`.

### Stage 1E.1 Coverage
1. **Line Protocol & Framing**: Validation of `DISPATCH<TAB><command>`, `FINISH`, `STATUS`, `RESET_ERROR`, and error responses for malformed framing (`MALFORMED_REQUEST`, `UNKNOWN_ACTION`, `EMPTY_REQUEST`).
2. **Single JSON Line Invariance**: Guaranteed exactly one valid JSON object per request terminated with `\n` and flushed immediately.
3. **JSON Escaping Compliance**: RFC 8259 escaping of quotes (`"`), backslashes (`\`), control characters (`\t`, `\n`, `\r`, `\b`, `\f`, `\u00xx`) verifying safe transmission of arbitrary argument content.
4. **Oversized Input Drainage**: Safe rejection of lines > 2048 bytes with `REQUEST_TOO_LONG` and full drainage of trailing characters to avoid corruption of subsequent commands; enforcement of `FLEX_MAX_INPUT_LEN` (1024) returning `FLEX_STATUS_INPUT_TOO_LONG`.
5. **Operation & Shutdown Lifecycle**: Preservation of in-flight operation upon `exit` dispatch, rejection of new operations during `STOPPING`, and clean `FINISH` completion.
6. **EOF & Stderr Isolation**: Clean exit with code 0 on EOF, nonzero exit code on read errors, and isolation of diagnostics/warnings to `stderr`.



