"""FLEX 1.0 — AI Pipeline Coordinator (Stage 1F.2).

Integrates the native C core bridge (`FlexBridgeClient`), local AI inference provider
(`OllamaProvider`), and in-memory conversation history (`InMemoryConversationHistory`).

Architectural Workflow:
    User message
    -> C command parsing and state transition (`flex_bridge.exe`)
    -> Python AI coordinator serialization and dispatch verification
    -> Local Ollama inference (`OllamaProvider.generate()`)
    -> Actual model response extraction
    -> Guaranteed C operation cleanup (`finish()`)
    -> Delivery to UI or consumer.

Strict Guarantees:
    - Thread-safe serialization across bridge operations (`threading.RLock`).
    - C core dispatch must succeed and begin operation before AI inference starts.
    - Guaranteed cleanup: `bridge.finish()` is always attempted if an operation started.
    - Never finish an operation that did not start.
    - Preserves primary inference error if cleanup also encounters an error.
    - Never fabricates model responses.
    - Zero third-party dependencies; strictly standard-library compliant.
"""

from __future__ import annotations

import logging
from dataclasses import dataclass
import threading
from typing import Any, Dict, Optional, Union
import uuid

from .ai.base import LLMProvider, LLMResponse
from .ai.config import OllamaConfig
from .ai.history import InMemoryConversationHistory
from .ai.ollama_provider import OllamaProvider
from .ai.storage import SQLiteConversationStorage
from .core_bridge import FlexBridgeClient, FlexBridgeError

logger = logging.getLogger(__name__)


class CoordinatorError(Exception):
    """Base exception for AI Coordinator errors."""
    pass


class CoordinatorDispatchError(CoordinatorError):
    """Raised when C core dispatch rejects command or fails state transition."""

    def __init__(self, message: str, dispatch_response: Optional[Dict[str, Any]] = None) -> None:
        super().__init__(message)
        self.dispatch_response = dispatch_response or {}


class CoordinatorStateError(CoordinatorError):
    """Raised when C state manager operation cleanup or state transition fails."""
    pass


class CoordinatorLifecycleError(CoordinatorError):
    """Raised when the bridge process is not running or bridge lifecycle is invalid."""
    pass


@dataclass
class CoordinatorChatResult:
    """Encapsulates the successful execution of an end-to-end chat turn.

    Attributes:
        text: The actual generated text from the local LLM.
        operation_id: The monotonic operation ID assigned by the C state manager.
        command_name: The command verb parsed by the C parser (e.g. 'chat').
        parser_status: The C command parser status string (e.g. 'FLEX_STATUS_OK').
        state_status: The C state manager status string (e.g. 'FLEX_STATE_OK').
        model: The model tag used for inference.
        raw_response: Raw response dictionary returned by the AI provider.
        total_duration_ns: Inference total duration in nanoseconds if available.
    """

    text: str
    operation_id: int
    command_name: str
    parser_status: str
    state_status: str
    model: str
    raw_response: Optional[Dict[str, Any]] = None
    total_duration_ns: Optional[int] = None
    conversation_id: Optional[str] = None
    storage_status: str = "MEMORY_ONLY"


DEFAULT_SYSTEM_PROMPT: str = (
    "You are FLEX 1.0, an advanced, concise AI desktop assistant for engineering and system control. "
    "Provide clear, accurate, and direct responses."
)


class AICoordinator:
    """Coordinates command dispatch through the native C core and executes local AI inference.

    Attributes:
        bridge: The FlexBridgeClient instance managing `flex_bridge.exe`.
        provider: The LLMProvider instance managing local Ollama calls.
        history: The in-memory bounded conversation history buffer.
        system_prompt: System prompt instructing the assistant behavior.
    """

    def __init__(
        self,
        bridge: Optional[FlexBridgeClient] = None,
        provider: Optional[LLMProvider] = None,
        history: Optional[InMemoryConversationHistory] = None,
        system_prompt: Optional[str] = None,
        auto_start: bool = True,
        storage: Optional[SQLiteConversationStorage] = None,
        active_conversation_id: Optional[str] = None,
        config: Optional[OllamaConfig] = None,
    ) -> None:
        """Initializes the coordinator with required subsystems.

        Args:
            bridge: Optional FlexBridgeClient. If None, instantiates default client.
            provider: Optional LLMProvider. If None, instantiates OllamaProvider with default config.
            history: Optional conversation history buffer. If None, creates 50-message buffer.
            system_prompt: System prompt string. If None, uses DEFAULT_SYSTEM_PROMPT.
            auto_start: If True, launches the bridge subprocess immediately if not already running.
            storage: Optional SQLiteConversationStorage for persistent conversation memory.
            active_conversation_id: Optional initial conversation ID to activate.
            config: Optional OllamaConfig to initialize provider with when provider is None.
        """
        self.bridge: FlexBridgeClient = bridge if bridge is not None else FlexBridgeClient()
        if provider is not None:
            self.provider: LLMProvider = provider
        elif config is not None:
            self.provider = OllamaProvider(config=config)
        else:
            self.provider = OllamaProvider()

        self.history: InMemoryConversationHistory = (
            history if history is not None else InMemoryConversationHistory()
        )
        self.system_prompt: str = (
            system_prompt if system_prompt is not None else DEFAULT_SYSTEM_PROMPT
        )
        self.storage: Optional[SQLiteConversationStorage] = storage
        self._active_conversation_id: Optional[str] = active_conversation_id
        self.last_storage_status: str = "MEMORY_ONLY"
        self.last_storage_error: Optional[str] = None
        self._config: Optional[OllamaConfig] = config or getattr(self.provider, "config", None)

        self._lock: threading.RLock = threading.RLock()

        if self.storage is not None:
            if self._active_conversation_id is not None:
                self.sync_history_from_storage(self._active_conversation_id)
            else:
                try:
                    recent = self.storage.list_conversations(limit=1)
                    if recent:
                        self._active_conversation_id = recent[0].id
                        self.sync_history_from_storage(recent[0].id)
                    else:
                        self._active_conversation_id = self.storage.create_conversation()
                except Exception as exc:
                    logger.warning("Error initializing active conversation from storage: %s", exc)
                    self.last_storage_error = str(exc)

        if auto_start and not self.bridge.is_running:
            self.start()

    @property
    def config(self) -> Optional[OllamaConfig]:
        """Returns the active provider configuration if available."""
        return getattr(self.provider, "config", None) or self._config

    @property
    def is_running(self) -> bool:
        """Returns True if the underlying C bridge process is active."""
        return self.bridge.is_running

    def start(self) -> "AICoordinator":
        """Starts the underlying C bridge subprocess if not already running."""
        with self._lock:
            if not self.bridge.is_running:
                self.bridge.start()
            return self

    def close(self, timeout: float = 2.0) -> None:
        """Closes the underlying C bridge subprocess and persistent storage cleanly."""
        acquired = self._lock.acquire(timeout=min(timeout, 1.0))
        try:
            if self.bridge.is_running:
                self.bridge.close(timeout=timeout)
            if self.storage is not None:
                try:
                    self.storage.close()
                except Exception as e:
                    logger.debug("Error closing storage in coordinator: %s", e)
        finally:
            if acquired:
                self._lock.release()

    def __enter__(self) -> "AICoordinator":
        self.start()
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.close()

    def clear_history(self) -> None:
        """Clears all in-memory conversation history messages."""
        self.history.clear()

    @property
    def active_conversation_id(self) -> Optional[str]:
        """Returns the current active conversation ID."""
        return self._active_conversation_id

    def set_active_conversation(
        self, conversation_id: str, sync_history: bool = True
    ) -> None:
        """Switches the active conversation and optionally synchronizes in-memory history.

        Args:
            conversation_id: Target conversation ID.
            sync_history: If True, populates in-memory history from storage.
        """
        with self._lock:
            self._active_conversation_id = conversation_id
            if sync_history and self.storage is not None:
                self.sync_history_from_storage(conversation_id)

    def sync_history_from_storage(self, conversation_id: str) -> None:
        """Populates in-memory history buffer with recent messages from storage."""
        with self._lock:
            self.history.clear()
            if self.storage is None:
                return
            try:
                records = self.storage.get_messages(
                    conversation_id, limit=self.history.max_messages
                )
                for rec in records:
                    if rec.role == "user":
                        self.history.add_user_message(rec.content)
                    elif rec.role == "assistant":
                        self.history.add_assistant_message(rec.content)
                    elif rec.role == "system":
                        self.history.add_system_message(rec.content)
            except Exception as exc:
                logger.warning("Failed to sync history from storage for %s: %s", conversation_id, exc)
                self.last_storage_error = str(exc)

    def create_new_conversation(self, title: str = "New Chat") -> str:
        """Creates a new conversation in storage and sets it active.

        Returns:
            str: The newly created conversation ID.
        """
        with self._lock:
            if self.storage is not None:
                cid = self.storage.create_conversation(title=title)
            else:
                cid = str(uuid.uuid4())
            self._active_conversation_id = cid
            self.history.clear()
            return cid

    def check_ollama_health(self) -> bool:
        """Pings the local Ollama service. Returns True if healthy, False otherwise."""
        return self.provider.check_health()

    def check_model_available(self, model_name: Optional[str] = None) -> bool:
        """Checks if the configured model is installed locally in Ollama."""
        cfg_model = getattr(self.provider, "config", None)
        target = model_name or (getattr(cfg_model, "model", None) or "qwen2.5-coder:3b")
        return self.provider.is_model_available(target)

    def get_status(self) -> Dict[str, Any]:
        """Queries the combined status of the C core bridge, AI provider, and history buffer.

        Returns:
            dict: Comprehensive operational status.
        """
        with self._lock:
            bridge_status: Dict[str, Any] = {}
            if self.bridge.is_running:
                try:
                    bridge_status = self.bridge.get_status()
                except Exception as e:
                    bridge_status = {"error": str(e), "is_running": False}
            else:
                bridge_status = {
                    "is_running": False,
                    "lifecycle": "FLEX_LIFECYCLE_UNKNOWN",
                    "active_operation": "FLEX_OPERATION_NONE",
                }

            ai_health = self.check_ollama_health()
            cfg = getattr(self.provider, "config", None)
            model_name = getattr(cfg, "model", "qwen2.5-coder:3b")
            base_url = getattr(cfg, "base_url", "http://127.0.0.1:11434")
            timeout_sec = getattr(cfg, "timeout", 30.0)
            temperature = getattr(cfg, "temperature", 0.2)
            num_ctx = getattr(cfg, "num_ctx", None)
            model_avail = self.check_model_available(model_name) if ai_health else False

            storage_enabled = self.storage is not None
            storage_degraded = getattr(self.storage, "is_degraded", False)

            return {
                "bridge": bridge_status,
                "ai_health": ai_health,
                "model_available": model_avail,
                "model": model_name,
                "base_url": base_url,
                "timeout": timeout_sec,
                "temperature": temperature,
                "num_ctx": num_ctx,
                "history_count": len(self.history.get_messages()),
                "active_conversation_id": self._active_conversation_id,
                "storage_enabled": storage_enabled,
                "storage_degraded": storage_degraded,
                "storage_status": self.last_storage_status,
            }

    def update_ai_config(self, new_config: OllamaConfig) -> None:
        """Updates the AI provider configuration at runtime in a thread-safe manner.

        Applies immediately to subsequent inference requests without restarting the C bridge.
        """
        with self._lock:
            self._config = new_config
            try:
                self.provider.config = new_config
            except (AttributeError, TypeError):
                pass
            logger.info(
                "Updated AI configuration: model=%s, url=%s, timeout=%.1f, temp=%.2f",
                new_config.model,
                new_config.base_url,
                new_config.timeout,
                getattr(new_config, "temperature", 0.2),
            )

    def dispatch_command(self, command: str, timeout: Optional[float] = None) -> Dict[str, Any]:
        """Dispatches an arbitrary raw command string directly to the C bridge.

        Serialized by coordinator lock.

        Args:
            command: The exact command string to pass to the C orchestrator.
            timeout: Optional read timeout.

        Returns:
            dict: The bridge dispatch response.
        """
        with self._lock:
            if not self.bridge.is_running:
                self.bridge.start()
            return self.bridge.dispatch(command, timeout=timeout)

    def finish_operation(self, timeout: Optional[float] = None) -> Dict[str, Any]:
        """Explicitly finishes any active C operation.

        Serialized by coordinator lock.
        """
        with self._lock:
            if not self.bridge.is_running:
                raise CoordinatorLifecycleError("Bridge process is not running.")
            return self.bridge.finish(timeout=timeout)

    def send_chat(
        self,
        user_message: str,
        timeout: Optional[float] = None,
        workspace_context: Optional[str] = None,
    ) -> CoordinatorChatResult:
        """Executes the complete end-to-end request pipeline for a user chat turn.

        Pipeline stages:
            1. Serialize access to bridge via lock.
            2. Validate and format command for C parser (`chat <message>`).
            3. Dispatch to C bridge and verify C state transition (`PROCESSING`).
            4. If dispatch fails, never finish and abort without AI inference.
            5. Execute local AI inference (`OllamaProvider.generate()`).
            6. Guaranteed valid C operation cleanup (`finish()`) even if inference fails.
            7. Preserve primary inference error if cleanup also fails.
            8. Record turn in conversation history and return structured result.

        Args:
            user_message: The user's input text prompt.
            timeout: Optional override for inference request timeout.
            workspace_context: Optional structured file context block from attached workspace files.

        Returns:
            CoordinatorChatResult: The assistant response and C core metadata.

        Raises:
            CoordinatorDispatchError: If C parser or state manager rejects dispatch.
            CoordinatorStateError: If C state cleanup fails after successful inference.
            FLEXAIError: If AI inference fails (service unreachable, timeout, etc.).
        """
        with self._lock:
            # 1. Ensure bridge process is active
            if not self.bridge.is_running:
                self.bridge.start()

            # 2. Format command string for C parser
            trimmed = user_message.strip() if isinstance(user_message, str) else ""
            if not trimmed:
                command_line = "chat"
            elif trimmed.lower().startswith("chat ") or trimmed.lower().startswith("chat\t"):
                command_line = trimmed
            else:
                command_line = f"chat {trimmed}"

            # 3. Dispatch to C Core (Command Parsing & State Transition)
            try:
                dispatch_res = self.bridge.dispatch(command_line)
            except FlexBridgeError as e:
                raise CoordinatorDispatchError(f"Bridge communication error during dispatch: {e}") from e

            parser_status = dispatch_res.get("parser_status", "UNKNOWN")
            state_status = dispatch_res.get("state_status", "UNKNOWN")
            operation_started = bool(dispatch_res.get("operation_started", False))
            dispatch_success = bool(dispatch_res.get("success", False))
            op_id = int(dispatch_res.get("operation_id", 0))
            cmd_name = str(dispatch_res.get("command_name", "chat"))
            extracted_args = str(dispatch_res.get("args", ""))

            # REQUIREMENT: Dispatch must succeed before AI inference begins.
            # REQUIREMENT: Never finish an operation that did not start.
            if not dispatch_success or not operation_started:
                raise CoordinatorDispatchError(
                    f"C core rejected dispatch: parser={parser_status}, state={state_status}",
                    dispatch_response=dispatch_res,
                )

            # Prompt to feed to LLM is the extracted arguments, or original trimmed input
            prompt_for_ai = extracted_args if extracted_args else trimmed
            if not prompt_for_ai:
                prompt_for_ai = "Hello"

            # 4. If storage is enabled, persist user message once C core accepted dispatch
            if self.storage is not None and not self._active_conversation_id:
                try:
                    self._active_conversation_id = self.storage.create_conversation()
                except Exception as exc:
                    logger.warning("Error creating conversation in storage: %s", exc)

            # Pin target_cid to ensure assistant response writes to the exact same conversation
            target_cid = self._active_conversation_id
            user_saved = False
            if self.storage is not None and target_cid:
                try:
                    self.storage.save_message(
                        conversation_id=target_cid,
                        role="user",
                        content=prompt_for_ai,
                        auto_title=True,
                    )
                    user_saved = True
                    self.last_storage_status = "PERSISTED"
                except Exception as s_exc:
                    logger.warning("Storage failure saving user message: %s", s_exc)
                    self.last_storage_status = "MEMORY_ONLY"
                    self.last_storage_error = str(s_exc)

            # 5. AI Inference & Guaranteed Cleanup
            inference_error: Optional[Exception] = None
            cleanup_error: Optional[Exception] = None
            llm_response: Optional[LLMResponse] = None

            # Incorporate workspace context into prompt sent to Ollama
            effective_prompt = prompt_for_ai
            if workspace_context and workspace_context.strip():
                effective_prompt = f"{workspace_context.strip()}\n\nUser Question:\n{prompt_for_ai}"

            try:
                # Actual local Ollama inference — never fabricate a response
                llm_response = self.provider.generate(
                    prompt=effective_prompt,
                    system_prompt=self.system_prompt,
                    history=self.history,
                    timeout=timeout,
                )
            except Exception as exc:
                inference_error = exc
                if user_saved and self._active_conversation_id == target_cid:
                    self.history.add_user_message(prompt_for_ai)
                raise
            finally:
                # REQUIREMENT: Cleanup must be attempted when an operation has started, even if inference fails.
                # Only finish if operation_started is True.
                if operation_started:
                    try:
                        finish_res = self.bridge.finish()
                        if not finish_res.get("success", False):
                            cleanup_error = CoordinatorStateError(
                                f"C core finish failed: state={finish_res.get('state_status')}"
                            )
                    except Exception as exc:
                        cleanup_error = exc

                    # REQUIREMENT: Preserve the primary error if cleanup also fails.
                    if cleanup_error is not None:
                        if inference_error is not None:
                            logger.error(
                                "Operation #%d cleanup failed (%s) after primary inference error (%s)",
                                op_id,
                                cleanup_error,
                                inference_error,
                            )
                            # Do NOT raise cleanup_error; the active 'raise' in try block will propagate inference_error!
                        else:
                            # Inference succeeded, but cleanup failed. Raise cleanup error.
                            raise cleanup_error from None

            # 6. Inference and cleanup both succeeded: record to history and return result
            assert llm_response is not None
            if self._active_conversation_id == target_cid:
                self.history.add_user_message(prompt_for_ai)
                self.history.add_assistant_message(llm_response.text)

            if self.storage is not None and target_cid:
                try:
                    self.storage.save_message(
                        conversation_id=target_cid,
                        role="assistant",
                        content=llm_response.text,
                        model=llm_response.model,
                    )
                    self.last_storage_status = "PERSISTED"
                except Exception as s_exc:
                    logger.warning("Storage failure saving assistant response: %s", s_exc)
                    self.last_storage_status = "MEMORY_ONLY"
                    self.last_storage_error = str(s_exc)
            else:
                self.last_storage_status = "MEMORY_ONLY"

            return CoordinatorChatResult(
                text=llm_response.text,
                operation_id=op_id,
                command_name=cmd_name,
                parser_status=parser_status,
                state_status=state_status,
                model=llm_response.model,
                raw_response=llm_response.raw_response,
                total_duration_ns=llm_response.total_duration_ns,
                conversation_id=target_cid,
                storage_status=self.last_storage_status,
            )
