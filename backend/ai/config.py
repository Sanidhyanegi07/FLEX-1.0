"""FLEX 1.0 — Local AI Provider Configuration.

Manages runtime parameters for the Ollama inference provider.
Supports explicit constructor configuration with fallback to environment variables
and conservative built-in defaults.

Precedence order (highest to lowest):
    1. Explicit constructor arguments passed by caller.
    2. Environment variables (FLEX_OLLAMA_BASE_URL, FLEX_OLLAMA_MODEL, FLEX_OLLAMA_TIMEOUT).
    3. Built-in defaults (http://127.0.0.1:11434, qwen2.5-coder:3b, 30.0s).
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from typing import Any, Dict, Optional

from .exceptions import AIValidationError

DEFAULT_BASE_URL: str = "http://127.0.0.1:11434"
DEFAULT_MODEL: str = "qwen2.5-coder:3b"
DEFAULT_TIMEOUT: float = 30.0
DEFAULT_OPTIONS: Dict[str, Any] = {"temperature": 0.2}


@dataclass
class OllamaConfig:
    """Configuration for the local Ollama provider.

    Attributes:
        base_url: The HTTP root URL of the Ollama server (e.g. 'http://127.0.0.1:11434').
        model: The model tag to invoke for chat inference (default 'qwen2.5-coder:3b').
        timeout: Request timeout in seconds for inference operations (default 30.0).
        options: Optional generation parameters passed to Ollama (e.g. temperature).
    """

    base_url: str = field(default="")
    model: str = field(default="")
    timeout: float = field(default=0.0)
    options: Optional[Dict[str, Any]] = None

    def __init__(
        self,
        base_url: Optional[str] = None,
        model: Optional[str] = None,
        timeout: Optional[float] = None,
        options: Optional[Dict[str, Any]] = None,
    ) -> None:
        """Initializes configuration with precedence: explicit > env > default."""
        # 1. Base URL
        if base_url is not None:
            resolved_url = base_url
        else:
            resolved_url = (
                os.environ.get("FLEX_OLLAMA_BASE_URL")
                or os.environ.get("FLEX_OLLAMA_HOST")
                or os.environ.get("FLEX_OLLAMA_URL")
                or DEFAULT_BASE_URL
            )

        # 2. Model
        if model is not None:
            resolved_model = model
        else:
            resolved_model = os.environ.get("FLEX_OLLAMA_MODEL") or DEFAULT_MODEL

        # 3. Timeout
        if timeout is not None:
            resolved_timeout = timeout
        else:
            env_timeout = os.environ.get("FLEX_OLLAMA_TIMEOUT")
            if env_timeout is not None:
                try:
                    resolved_timeout = float(env_timeout)
                except (ValueError, TypeError) as e:
                    raise AIValidationError(
                        f"Environment variable FLEX_OLLAMA_TIMEOUT must be a valid number, got: {env_timeout!r}"
                    ) from e
            else:
                resolved_timeout = DEFAULT_TIMEOUT

        # 4. Options
        resolved_options = options if options is not None else dict(DEFAULT_OPTIONS)

        # Validate and assign
        self.base_url = self._validate_base_url(resolved_url)
        self.model = self._validate_model(resolved_model)
        self.timeout = self._validate_timeout(resolved_timeout)
        self.options = self._validate_options(resolved_options)

    @staticmethod
    def _validate_base_url(url: Any) -> str:
        if not isinstance(url, str):
            raise AIValidationError(f"base_url must be a string, got {type(url).__name__}")
        trimmed = url.strip().rstrip("/")
        if not (trimmed.startswith("http://") or trimmed.startswith("https://")):
            raise AIValidationError(
                f"base_url must start with 'http://' or 'https://', got: '{url}'"
            )
        return trimmed

    @staticmethod
    def _validate_model(model: Any) -> str:
        if not isinstance(model, str):
            raise AIValidationError(f"model must be a string, got {type(model).__name__}")
        trimmed = model.strip()
        if not trimmed:
            raise AIValidationError("model name must not be empty")
        return trimmed

    @staticmethod
    def _validate_timeout(timeout: Any) -> float:
        if not isinstance(timeout, (int, float)) or isinstance(timeout, bool):
            raise AIValidationError(f"timeout must be a number, got {type(timeout).__name__}")
        val = float(timeout)
        if val <= 0.0:
            raise AIValidationError(f"timeout must be greater than zero, got: {val}")
        return val

    @staticmethod
    def _validate_options(options: Any) -> Optional[Dict[str, Any]]:
        if options is not None and not isinstance(options, dict):
            raise AIValidationError(f"options must be a dictionary, got {type(options).__name__}")
        return options
