"""FLEX 1.0 — Local AI Engine Package (Stage 1F.1).

Provides the provider abstraction, Ollama integration, configuration models,
in-memory conversation history buffer, and structured exception hierarchy.
"""

from __future__ import annotations

from .base import ChatMessage, LLMProvider, LLMResponse, VALID_ROLES
from .config import (
    DEFAULT_BASE_URL,
    DEFAULT_MODEL,
    DEFAULT_TIMEOUT,
    OllamaConfig,
)
from .exceptions import (
    AIModelNotFoundError,
    AIRequestError,
    AIResponseError,
    AIServiceUnavailableError,
    AITimeoutError,
    AIValidationError,
    FLEXAIError,
)
from .history import InMemoryConversationHistory
from .ollama_provider import OllamaProvider

__all__ = [
    "AIModelNotFoundError",
    "AIRequestError",
    "AIResponseError",
    "AIServiceUnavailableError",
    "AITimeoutError",
    "AIValidationError",
    "ChatMessage",
    "DEFAULT_BASE_URL",
    "DEFAULT_MODEL",
    "DEFAULT_TIMEOUT",
    "FLEXAIError",
    "InMemoryConversationHistory",
    "LLMProvider",
    "LLMResponse",
    "OllamaConfig",
    "OllamaProvider",
    "VALID_ROLES",
]
