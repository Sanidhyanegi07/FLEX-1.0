"""FLEX 1.0 — AI Provider Interface and Data Models.

Defines the core data contracts for the AI engine:
    - ChatMessage: A validated conversational message with role and content.
    - LLMResponse: Structured response containing generated text and execution metrics.
    - LLMProvider: Abstract base class decoupling high-level callers from specific LLM runtimes.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional

from .exceptions import AIValidationError

# Permitted message roles in the conversation model
VALID_ROLES = frozenset({"system", "user", "assistant"})


@dataclass(frozen=True)
class ChatMessage:
    """Represents a single chat message with a validated role and content.

    Attributes:
        role: The participant role. Must be 'system', 'user', or 'assistant'.
        content: The text content of the message.
    """

    role: str
    content: str

    def __post_init__(self) -> None:
        if not isinstance(self.role, str):
            raise AIValidationError(
                f"ChatMessage role must be a string, got {type(self.role).__name__}"
            )
        normalized_role = self.role.strip().lower()
        if normalized_role not in VALID_ROLES:
            raise AIValidationError(
                f"Invalid message role '{self.role}'. Permitted roles are: {sorted(VALID_ROLES)}"
            )
        # Use object.__setattr__ because the dataclass is frozen
        object.__setattr__(self, "role", normalized_role)

        if not isinstance(self.content, str):
            raise AIValidationError(
                f"ChatMessage content must be a string, got {type(self.content).__name__}"
            )

    def to_dict(self) -> Dict[str, str]:
        """Serializes the message to a role/content dictionary."""
        return {"role": self.role, "content": self.content}


@dataclass(frozen=True)
class LLMResponse:
    """Structured response from an LLM provider.

    Attributes:
        text: The generated assistant message text.
        model: The model name that performed the inference.
        total_duration_ns: Optional total processing time in nanoseconds.
        load_duration_ns: Optional model load time in nanoseconds.
        prompt_eval_count: Optional number of tokens evaluated in the prompt.
        eval_count: Optional number of tokens generated in the completion.
        eval_duration_ns: Optional duration spent generating tokens in nanoseconds.
        done_reason: Optional termination reason (e.g. 'stop', 'length').
        raw_response: The complete unprocessed dictionary returned by the provider API.
    """

    text: str
    model: str
    total_duration_ns: Optional[int] = None
    load_duration_ns: Optional[int] = None
    prompt_eval_count: Optional[int] = None
    eval_count: Optional[int] = None
    eval_duration_ns: Optional[int] = None
    done_reason: Optional[str] = None
    raw_response: Dict[str, Any] = field(default_factory=dict)

    def __post_init__(self) -> None:
        if not isinstance(self.text, str):
            raise AIValidationError(
                f"LLMResponse text must be a string, got {type(self.text).__name__}"
            )
        if not isinstance(self.model, str) or not self.model.strip():
            raise AIValidationError("LLMResponse model must be a non-empty string")


class LLMProvider(ABC):
    """Abstract provider interface for local or remote LLM runtimes."""

    config: Optional[Any] = None

    @abstractmethod
    def generate(
        self,
        prompt: str,
        system_prompt: Optional[str] = None,
        history: Optional[List[ChatMessage]] = None,
        timeout: Optional[float] = None,
    ) -> LLMResponse:
        """Generates a text completion for the given prompt and conversation context.

        Args:
            prompt: The user input prompt. Must be a non-empty string.
            system_prompt: Optional instructions directing the assistant's behavior.
            history: Optional list of previous ChatMessage instances.
            timeout: Optional override for the request timeout in seconds.

        Returns:
            LLMResponse: The structured completion response.

        Raises:
            AIValidationError: If input arguments fail validation.
            AIServiceUnavailableError: If the inference service is unreachable.
            AIModelNotFoundError: If the requested model is not found on the host.
            AITimeoutError: If generation exceeds the timeout.
            AIResponseError: If the provider returns malformed or invalid response data.
            AIRequestError: If an HTTP error occurs.
        """
        pass

    @abstractmethod
    def check_health(self) -> bool:
        """Checks whether the underlying inference service is alive and reachable.

        Returns:
            bool: True if the service responds successfully, False otherwise.
                  Must never raise an exception.
        """
        pass

    @abstractmethod
    def is_model_available(self, model_name: str) -> bool:
        """Checks whether the specified model is installed and ready for inference.

        Args:
            model_name: The name or tag of the model to check.

        Returns:
            bool: True if the model is available, False otherwise.
                  Must never raise an exception.
        """
        pass

    @abstractmethod
    def list_models(self) -> List[str]:
        """Queries and returns a list of installed models available on the host.

        Returns:
            List[str]: List of model tags. Must never raise an exception; returns [] on failure.
        """
        pass

    def test_connection(self, url: Optional[str] = None, timeout: float = 3.0) -> Tuple[bool, str, Optional[str]]:
        """Tests reachability to the inference server and reports diagnostic details.

        Args:
            url: Optional endpoint URL override.
            timeout: Network timeout in seconds.

        Returns:
            Tuple[bool, str, Optional[str]]: (connected, message, version_or_details)
        """
        healthy = self.check_health()
        return (healthy, "Service reachable" if healthy else "Service unreachable", None)
