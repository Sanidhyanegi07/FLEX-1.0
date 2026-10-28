#include "flex/core_session.hpp"

namespace flex {

CoreSession::CoreSession()
{
    flex_state_init(&m_state);
}

CoreSession::CoreSession(CoreSession&& other) noexcept
    : m_state(other.m_state)
{
    flex_state_init(&other.m_state);
}

CoreSession& CoreSession::operator=(CoreSession&& other) noexcept
{
    if (this != &other) {
        m_state = other.m_state;
        flex_state_init(&other.m_state);
    }
    return *this;
}

DispatchResult CoreSession::dispatch(const char* commandLine)
{
    DispatchResult result;
    result.raw = flex_dispatch_command(&m_state, commandLine);
    return result;
}

DispatchResult CoreSession::dispatch(const std::string& commandLine)
{
    return dispatch(commandLine.c_str());
}

FlexStateStatus CoreSession::beginOperation(FlexOperation operation)
{
    return flex_state_begin_operation(&m_state, operation);
}

FlexStateStatus CoreSession::finishOperation()
{
    return flex_dispatch_finish_operation(&m_state);
}

FlexStateStatus CoreSession::requestShutdown()
{
    return flex_state_request_shutdown(&m_state);
}

FlexStateStatus CoreSession::resetError()
{
    return flex_state_reset_error(&m_state);
}

ScopedOperation CoreSession::scopedOperation(FlexOperation operation)
{
    return ScopedOperation(*this, operation);
}

std::pair<DispatchResult, ScopedOperation> CoreSession::dispatchScoped(const char* commandLine)
{
    auto result = dispatch(commandLine);
    if (result.operationStarted()) {
        return {result, ScopedOperation(*this, result.operationId(), true)};
    }
    return {result, ScopedOperation(*this, 0, false)};
}

std::pair<DispatchResult, ScopedOperation> CoreSession::dispatchScoped(const std::string& commandLine)
{
    return dispatchScoped(commandLine.c_str());
}

bool CoreSession::isIdle() const noexcept
{
    return flex_state_is_idle(&m_state);
}

bool CoreSession::isProcessing() const noexcept
{
    return flex_state_is_processing(&m_state);
}

bool CoreSession::isStopping() const noexcept
{
    return flex_state_is_stopping(&m_state);
}

FlexLifecycleState CoreSession::lifecycle() const noexcept
{
    return m_state.lifecycle;
}

FlexOperation CoreSession::currentOperation() const noexcept
{
    return m_state.current_operation;
}

uint64_t CoreSession::currentOperationId() const noexcept
{
    return m_state.current_operation_id;
}

FlexStateStatus CoreSession::lastError() const noexcept
{
    return m_state.last_error;
}

const FlexStateManager& CoreSession::rawState() const noexcept
{
    return m_state;
}

const char* CoreSession::lifecycleToString(FlexLifecycleState lifecycle) noexcept
{
    return flex_lifecycle_to_string(lifecycle);
}

const char* CoreSession::operationToString(FlexOperation operation) noexcept
{
    return flex_operation_to_string(operation);
}

const char* CoreSession::stateStatusToString(FlexStateStatus status) noexcept
{
    return flex_state_status_to_string(status);
}

const char* CoreSession::commandTypeToString(FlexCommandType type) noexcept
{
    return flex_command_type_to_string(type);
}

const char* CoreSession::commandStatusToString(FlexCommandStatus status) noexcept
{
    return flex_command_status_to_string(status);
}

FlexOperation CoreSession::operationFromCommand(FlexCommandType cmd) noexcept
{
    return flex_operation_from_command(cmd);
}

// -----------------------------------------------------------------------------
// ScopedOperation Implementation
// -----------------------------------------------------------------------------

ScopedOperation::ScopedOperation(CoreSession& session, FlexOperation operation)
    : m_session(&session), m_operationId(0), m_status(FLEX_STATE_OK), m_active(false)
{
    m_status = m_session->beginOperation(operation);
    if (m_status == FLEX_STATE_OK) {
        m_active = true;
        m_operationId = m_session->currentOperationId();
    }
}

ScopedOperation::ScopedOperation(CoreSession& session, uint64_t operationId, bool active)
    : m_session(&session), m_operationId(operationId), m_status(FLEX_STATE_OK), m_active(active)
{
}

ScopedOperation::~ScopedOperation()
{
    if (m_active && m_session != nullptr) {
        m_session->finishOperation();
    }
}

ScopedOperation::ScopedOperation(ScopedOperation&& other) noexcept
    : m_session(other.m_session)
    , m_operationId(other.m_operationId)
    , m_status(other.m_status)
    , m_active(other.m_active)
{
    other.m_session = nullptr;
    other.m_operationId = 0;
    other.m_active = false;
}

ScopedOperation& ScopedOperation::operator=(ScopedOperation&& other) noexcept
{
    if (this != &other) {
        if (m_active && m_session != nullptr) {
            m_session->finishOperation();
        }
        m_session = other.m_session;
        m_operationId = other.m_operationId;
        m_status = other.m_status;
        m_active = other.m_active;

        other.m_session = nullptr;
        other.m_operationId = 0;
        other.m_active = false;
    }
    return *this;
}

FlexStateStatus ScopedOperation::finish()
{
    if (!m_active || m_session == nullptr) {
        return FLEX_STATE_ERROR_NOT_PROCESSING;
    }
    m_active = false;
    m_status = m_session->finishOperation();
    return m_status;
}

void ScopedOperation::dismiss() noexcept
{
    m_active = false;
}

} // namespace flex

