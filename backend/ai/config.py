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
DEFAULT_TEMPERATURE: float = 0.2
DEFAULT_NUM_CTX: int = 4096
DEFAULT_OPTIONS: Dict[str, Any] = {"temperature": DEFAULT_TEMPERATURE}

MIN_TIMEOUT: float = 1.0
MAX_TIMEOUT: float = 600.0
MIN_TEMPERATURE: float = 0.0
MAX_TEMPERATURE: float = 2.0
MIN_NUM_CTX: int = 512
MAX_NUM_CTX: int = 131072


@dataclass
class OllamaConfig:
    """Configuration for the local Ollama provider.

    Attributes:
        base_url: The HTTP root URL of the Ollama server (e.g. 'http://127.0.0.1:11434').
        model: The model tag to invoke for chat inference (default 'qwen2.5-coder:3b').
        timeout: Request timeout in seconds for inference operations (default 30.0).
        temperature: Inference temperature parameter (default 0.2, range 0.0 to 2.0).
        num_ctx: Context window size in tokens (optional, range 512 to 131072).
        options: Optional generation parameters passed to Ollama (e.g. temperature, num_ctx).
    """

    base_url: str = field(default="")
    model: str = field(default="")
    timeout: float = field(default=0.0)
    temperature: float = field(default=DEFAULT_TEMPERATURE)
    num_ctx: Optional[int] = None
    options: Optional[Dict[str, Any]] = None

    def __init__(
        self,
        base_url: Optional[str] = None,
        model: Optional[str] = None,
        timeout: Optional[float] = None,
        temperature: Optional[float] = None,
        num_ctx: Optional[int] = None,
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

        # 4. Temperature
        if temperature is not None:
            resolved_temp = self._validate_temperature(temperature)
        else:
            env_temp = os.environ.get("FLEX_OLLAMA_TEMPERATURE")
            if env_temp is not None:
                try:
                    resolved_temp = self._validate_temperature(float(env_temp))
                except (ValueError, TypeError) as e:
                    raise AIValidationError(
                        f"Environment variable FLEX_OLLAMA_TEMPERATURE must be a valid number, got: {env_temp!r}"
                    ) from e
            elif options is not None and "temperature" in options:
                resolved_temp = self._validate_temperature(options["temperature"])
            else:
                resolved_temp = DEFAULT_TEMPERATURE

        # 5. Context Window (num_ctx)
        if num_ctx is not None:
            resolved_num_ctx = self._validate_num_ctx(num_ctx)
        else:
            env_ctx = (
                os.environ.get("FLEX_OLLAMA_NUM_CTX")
                or os.environ.get("FLEX_OLLAMA_CONTEXT_WINDOW")
            )
            if env_ctx is not None:
                try:
                    resolved_num_ctx = self._validate_num_ctx(int(env_ctx))
                except (ValueError, TypeError) as e:
                    raise AIValidationError(
                        f"Environment variable for context window must be an integer, got: {env_ctx!r}"
                    ) from e
            elif options is not None and "num_ctx" in options:
                resolved_num_ctx = self._validate_num_ctx(options["num_ctx"])
            else:
                resolved_num_ctx = None

        # 6. Options Assembly
        if options is not None:
            resolved_options = dict(options)
            resolved_options["temperature"] = resolved_temp
            if resolved_num_ctx is not None:
                resolved_options["num_ctx"] = resolved_num_ctx
        else:
            resolved_options = {"temperature": resolved_temp}
            if resolved_num_ctx is not None:
                resolved_options["num_ctx"] = resolved_num_ctx

        # Validate and assign
        self.base_url = self._validate_base_url(resolved_url)
        self.model = self._validate_model(resolved_model)
        self.timeout = self._validate_timeout(resolved_timeout)
        self.temperature = resolved_temp
        self.num_ctx = resolved_num_ctx
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
    def _validate_temperature(temp: Any) -> float:
        if not isinstance(temp, (int, float)) or isinstance(temp, bool):
            raise AIValidationError(f"temperature must be a number, got {type(temp).__name__}")
        val = float(temp)
        if val < MIN_TEMPERATURE or val > MAX_TEMPERATURE:
            raise AIValidationError(
                f"temperature must be between {MIN_TEMPERATURE} and {MAX_TEMPERATURE}, got: {val}"
            )
        return val

    @staticmethod
    def _validate_num_ctx(ctx: Any) -> int:
        if not isinstance(ctx, int) or isinstance(ctx, bool):
            raise AIValidationError(f"num_ctx must be an integer, got {type(ctx).__name__}")
        val = int(ctx)
        if val < MIN_NUM_CTX or val > MAX_NUM_CTX:
            raise AIValidationError(
                f"num_ctx must be between {MIN_NUM_CTX} and {MAX_NUM_CTX}, got: {val}"
            )
        return val

    @staticmethod
    def _validate_options(options: Any) -> Optional[Dict[str, Any]]:
        if options is not None and not isinstance(options, dict):
            raise AIValidationError(f"options must be a dictionary, got {type(options).__name__}")
        return options

    def to_dict(self) -> Dict[str, Any]:
        """Serializes configuration to a JSON-compatible dictionary."""
        data: Dict[str, Any] = {
            "base_url": self.base_url,
            "model": self.model,
            "timeout": self.timeout,
            "temperature": self.temperature,
            "num_ctx": self.num_ctx if self.num_ctx is not None else DEFAULT_NUM_CTX,
        }
        return data

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "OllamaConfig":
        """Reconstructs OllamaConfig from dictionary data with safe defaults."""
        if not isinstance(data, dict):
            return cls()
        return cls(
            base_url=data.get("base_url"),
            model=data.get("model"),
            timeout=data.get("timeout"),
            temperature=data.get("temperature"),
            num_ctx=data.get("num_ctx"),
            options=data.get("options"),
        )
