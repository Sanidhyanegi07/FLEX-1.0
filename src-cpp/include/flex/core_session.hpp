#ifndef FLEX_CORE_SESSION_HPP
#define FLEX_CORE_SESSION_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

extern "C" {
#include "command_parser.h"
#include "state_manager.h"
#include "orchestrator.h"
}

namespace flex {

/**
 * Result structure representing the outcome of dispatching a command.
 * Wraps FlexDispatchResult with C++ friendly types and accessors.
 */
struct DispatchResult {
    FlexDispatchResult raw{};

    [[nodiscard]] FlexCommandStatus parserStatus() const noexcept { return raw.parser_status; }
    [[nodiscard]] FlexStateStatus stateStatus() const noexcept { return raw.state_status; }
    [[nodiscard]] FlexCommandType commandType() const noexcept { return raw.command_type; }
    [[nodiscard]] FlexOperation operation() const noexcept { return raw.operation; }
    [[nodiscard]] uint64_t operationId() const noexcept { return raw.operation_id; }
    [[nodiscard]] bool operationStarted() const noexcept { return raw.operation_started; }
    [[nodiscard]] bool shutdownRequested() const noexcept { return raw.shutdown_requested; }
    [[nodiscard]] std::string_view commandName() const noexcept { return raw.command_result.name; }
    [[nodiscard]] std::string_view commandArgs() const noexcept { return raw.command_result.args; }
    [[nodiscard]] bool hasArgs() const noexcept { return raw.command_result.has_args; }

    [[nodiscard]] bool isOk() const noexcept {
        return raw.parser_status == FLEX_STATUS_OK && raw.state_status == FLEX_STATE_OK;
    }
};

class ScopedOperation;

/**
 * RAII wrapper around the C11 FlexStateManager and orchestrator dispatch pipeline.
 *
 * Guarantees:
 * - Owns an isolated FlexStateManager instance; no shared mutable state.
 * - Non-copyable to prevent silent state divergence.
 * - Movable to support standard container usage and transfer of ownership.
 * - Strictly obeys C core lifecycle transitions (IDLE -> PROCESSING -> IDLE/STOPPING).
 */
class CoreSession {
public:
    CoreSession();
    ~CoreSession() = default;

    // Non-copyable
    CoreSession(const CoreSession&) = delete;
    CoreSession& operator=(const CoreSession&) = delete;

    // Movable
    CoreSession(CoreSession&& other) noexcept;
    CoreSession& operator=(CoreSession&& other) noexcept;

    // Command Dispatch
    [[nodiscard]] DispatchResult dispatch(const char* commandLine);
    [[nodiscard]] DispatchResult dispatch(const std::string& commandLine);

    // Lifecycle Operations
    FlexStateStatus beginOperation(FlexOperation operation);
    FlexStateStatus finishOperation();
    FlexStateStatus requestShutdown();
    FlexStateStatus resetError();

    // RAII Operation Creation
    [[nodiscard]] ScopedOperation scopedOperation(FlexOperation operation);
    [[nodiscard]] std::pair<DispatchResult, ScopedOperation> dispatchScoped(const char* commandLine);
    [[nodiscard]] std::pair<DispatchResult, ScopedOperation> dispatchScoped(const std::string& commandLine);

    // State Inspection
    [[nodiscard]] bool isIdle() const noexcept;
    [[nodiscard]] bool isProcessing() const noexcept;
    [[nodiscard]] bool isStopping() const noexcept;

    [[nodiscard]] FlexLifecycleState lifecycle() const noexcept;
    [[nodiscard]] FlexOperation currentOperation() const noexcept;
    [[nodiscard]] uint64_t currentOperationId() const noexcept;
    [[nodiscard]] FlexStateStatus lastError() const noexcept;
    [[nodiscard]] const FlexStateManager& rawState() const noexcept;

    // Static Helpers
    static const char* lifecycleToString(FlexLifecycleState lifecycle) noexcept;
    static const char* operationToString(FlexOperation operation) noexcept;
    static const char* stateStatusToString(FlexStateStatus status) noexcept;
    static const char* commandTypeToString(FlexCommandType type) noexcept;
    static const char* commandStatusToString(FlexCommandStatus status) noexcept;
    static FlexOperation operationFromCommand(FlexCommandType cmd) noexcept;

private:
    FlexStateManager m_state{};
};

/**
 * RAII guard for an active Flex operation.
 *
 * Ensures that an operation begun on a CoreSession is completed when the guard
 * falls out of scope, preventing state leaks in error/exception branches.
 * Explicitly supports single completion and rejects repeated completion.
 */
class ScopedOperation {
public:
    ScopedOperation() noexcept = default;
    ScopedOperation(CoreSession& session, FlexOperation operation);
    ScopedOperation(CoreSession& session, uint64_t operationId, bool active);
    ~ScopedOperation();

    // Non-copyable
    ScopedOperation(const ScopedOperation&) = delete;
    ScopedOperation& operator=(const ScopedOperation&) = delete;

    // Movable
    ScopedOperation(ScopedOperation&& other) noexcept;
    ScopedOperation& operator=(ScopedOperation&& other) noexcept;

    [[nodiscard]] bool isActive() const noexcept { return m_active; }
    [[nodiscard]] uint64_t operationId() const noexcept { return m_operationId; }
    [[nodiscard]] FlexStateStatus status() const noexcept { return m_status; }

    /**
     * Explicitly finish the active operation.
     * If already finished or inactive, returns FLEX_STATE_ERROR_NOT_PROCESSING.
     */
    FlexStateStatus finish();

    /**
     * Release ownership of the operation without finishing it on the session.
     */
    void dismiss() noexcept;

private:
    CoreSession* m_session{nullptr};
    uint64_t m_operationId{0};
    FlexStateStatus m_status{FLEX_STATE_OK};
    bool m_active{false};
};

} // namespace flex

#endif // FLEX_CORE_SESSION_HPP

