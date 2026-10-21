"""FLEX 1.0 — AI Engine Exception Hierarchy.

Provides structured, descriptive exceptions for the AI engine foundation.
Distinguishes service unreachability, missing models, request timeouts,
and protocol/response validation errors.
"""

from __future__ import annotations


class FLEXAIError(Exception):
    """Base exception for all FLEX AI Engine errors."""
    pass


class AIValidationError(FLEXAIError, ValueError):
    """Raised when configuration values, message roles, or input parameters fail validation."""
    pass


class AIServiceUnavailableError(FLEXAIError):
    """Raised when the AI service daemon (e.g. Ollama) is unreachable or not running."""
    pass


class AIModelNotFoundError(FLEXAIError):
    """Raised when the requested model is not installed or found on the service host."""
    pass


class AITimeoutError(FLEXAIError, TimeoutError):
    """Raised when an AI inference or service request exceeds the configured timeout."""
    pass


class AIResponseError(FLEXAIError):
    """Raised when the AI service returns malformed JSON, missing fields, or an invalid structure."""
    pass


class AIRequestError(FLEXAIError):
    """Raised when an HTTP or API request fails with a client or server error (e.g. HTTP 4xx/5xx)."""
    pass
