"""FLEX 1.0 — Centralized Application Settings & Configuration Manager (Stage 2E).

Provides thread-safe, atomic, persistent configuration storage for:
    - Ollama server connection (base_url, timeout).
    - Model selection and discovery (model tag).
    - Inference parameters (temperature, num_ctx).
    - Workspace folder persistence (last_workspace_path).

Safeguards:
    - Single unified settings store at `%LOCALAPPDATA%/FLEX/settings.json`.
    - Atomic file persistence via temporary file replacement (`os.replace`).
    - Non-destructive updates: updating AI settings strictly preserves `last_workspace_path`.
    - Corruption resilience: malformed or unreadable files trigger graceful fallback to defaults
      without crashing the desktop GUI.
"""

from __future__ import annotations

import json
import logging
import os
import threading
from typing import Any, Dict, List, Optional, Tuple

from .ai.config import (
    DEFAULT_BASE_URL,
    DEFAULT_MODEL,
    DEFAULT_NUM_CTX,
    DEFAULT_TEMPERATURE,
    DEFAULT_TIMEOUT,
    MAX_NUM_CTX,
    MAX_TEMPERATURE,
    MAX_TIMEOUT,
    MIN_NUM_CTX,
    MIN_TEMPERATURE,
    MIN_TIMEOUT,
    OllamaConfig,
)
from .ai.exceptions import AIValidationError

logger = logging.getLogger(__name__)


def get_default_settings_path() -> str:
    """Resolves the canonical persistent settings.json file path outside repository.

    Precedence:
        1. FLEX_SETTINGS_PATH environment variable (if non-empty).
        2. %LOCALAPPDATA%/FLEX/settings.json (Windows).
        3. ~/.flex/settings.json (Unix / user directory fallback).
    """
    env_override = os.environ.get("FLEX_SETTINGS_PATH")
    if env_override and env_override.strip():
        return os.path.abspath(env_override.strip())

    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data and local_app_data.strip():
        base_dir = os.path.join(local_app_data.strip(), "FLEX")
    else:
        base_dir = os.path.expanduser(os.path.join("~", ".flex"))

    return os.path.abspath(os.path.join(base_dir, "settings.json"))


class SettingsManager:
    """Thread-safe persistent settings storage for FLEX 1.0."""

    def __init__(
        self,
        settings_path: Optional[Any] = None,
        settings_file: Optional[Any] = None,
    ) -> None:
        """Initializes SettingsManager with specified or default JSON file path."""
        target = settings_path if settings_path is not None else settings_file
        if target is not None:
            self.settings_path: str = os.path.abspath(str(target))
        else:
            self.settings_path = get_default_settings_path()
        self._lock: threading.RLock = threading.RLock()

    # ── Low-Level I/O & Atomic Storage ────────────────────────────────────────

    def read_settings(self) -> Dict[str, Any]:
        """Reads raw settings dictionary from disk with corruption safeguards.

        Returns:
            Dict[str, Any]: Parsed settings, or empty dict on file absence or parse error.
        """
        with self._lock:
            if not os.path.exists(self.settings_path):
                return {}

            try:
                with open(self.settings_path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                if isinstance(data, dict):
                    return data
                logger.warning("Settings file %s contains non-dictionary root; resetting.", self.settings_path)
                return {}
            except Exception as exc:
                logger.warning("Failed to parse settings from %s (%s); using defaults.", self.settings_path, exc)
                return {}

    def write_settings(self, data: Dict[str, Any]) -> None:
        """Atomically writes settings dictionary to disk using a temporary file.

        Args:
            data: Complete settings dictionary to serialize.
        """
        with self._lock:
            try:
                target_dir = os.path.dirname(self.settings_path)
                if target_dir:
                    os.makedirs(target_dir, exist_ok=True)

                temp_path = f"{self.settings_path}.tmp"
                with open(temp_path, "w", encoding="utf-8") as f:
                    json.dump(data, f, indent=2)

                os.replace(temp_path, self.settings_path)
            except Exception as exc:
                logger.error("Failed to atomically write settings to %s: %s", self.settings_path, exc)
                raise

    # Aliases for backward compatibility
    _read_settings = read_settings
    _write_settings = write_settings

    # ── Ollama AI Configuration Operations ────────────────────────────────────

    def get_ollama_config(self) -> OllamaConfig:
        """Loads and constructs the active OllamaConfig from settings.

        Falls back cleanly to environment variables and built-in defaults.
        """
        with self._lock:
            data = self.read_settings()
            ollama_dict = data.get("ollama")

            if isinstance(ollama_dict, dict):
                try:
                    return OllamaConfig.from_dict(ollama_dict)
                except Exception as exc:
                    logger.warning("Invalid stored Ollama configuration (%s); falling back to default.", exc)

            # Default configuration handles env vars and defaults
            return OllamaConfig()

    def set_ollama_config(self, config: OllamaConfig) -> None:
        """Saves Ollama configuration while strictly preserving workspace settings."""
        with self._lock:
            data = self.read_settings()
            data["ollama"] = config.to_dict()
            self.write_settings(data)
            logger.info("Saved Ollama configuration to %s", self.settings_path)

    def reset_ollama_config(self) -> OllamaConfig:
        """Resets Ollama configuration to defaults while strictly preserving workspace settings."""
        with self._lock:
            default_cfg = OllamaConfig(
                base_url=DEFAULT_BASE_URL,
                model=DEFAULT_MODEL,
                timeout=DEFAULT_TIMEOUT,
                temperature=DEFAULT_TEMPERATURE,
                num_ctx=DEFAULT_NUM_CTX,
            )
            data = self.read_settings()
            data["ollama"] = default_cfg.to_dict()
            self.write_settings(data)
            logger.info("Reset Ollama settings to default values in %s", self.settings_path)
            return default_cfg

    # ── Workspace Configuration Operations ───────────────────────────────────

    def get_last_workspace(self) -> Optional[str]:
        """Retrieves the last selected workspace folder path."""
        with self._lock:
            data = self.read_settings()
            path = data.get("last_workspace_path")
            if isinstance(path, str) and path.strip():
                return path.strip()
            return None

    def set_last_workspace(self, workspace_path: Optional[str]) -> None:
        """Saves the last selected workspace path while strictly preserving AI settings."""
        with self._lock:
            data = self.read_settings()
            if workspace_path is None:
                data.pop("last_workspace_path", None)
            else:
                data["last_workspace_path"] = str(workspace_path).strip()
            self.write_settings(data)

    get_last_workspace_path = get_last_workspace
    set_last_workspace_path = set_last_workspace

    # ── Validation Utilities ──────────────────────────────────────────────────

    @staticmethod
    def validate_ollama_settings(
        base_url: str = "",
        model: str = "",
        timeout_str: Any = "",
        temperature_str: Any = "",
        num_ctx_str: Any = "",
        **kwargs: Any,
    ) -> Tuple[bool, Optional[OllamaConfig], List[str]]:
        """Validates raw string inputs from GUI form fields.

        Returns:
            Tuple[bool, Optional[OllamaConfig], List[str]]:
                - is_valid: True if all fields valid, False otherwise.
                - config: Resulting OllamaConfig if valid, None if invalid.
                - errors: List of human-readable error messages.
        """
        # Resolve aliases from kwargs if provided
        if "url" in kwargs and not base_url:
            base_url = str(kwargs["url"])
        if "timeout" in kwargs and not timeout_str:
            timeout_str = kwargs["timeout"]
        if "temperature" in kwargs and not temperature_str:
            temperature_str = kwargs["temperature"]
        if "num_ctx" in kwargs and not num_ctx_str:
            num_ctx_str = kwargs["num_ctx"]

        errors: List[str] = []

        # 1. Base URL
        trimmed_url = str(base_url).strip().rstrip("/")
        if not (trimmed_url.startswith("http://") or trimmed_url.startswith("https://")):
            errors.append("Server URL must begin with 'http://' or 'https://'")
        elif trimmed_url in ("http://", "https://") or len(trimmed_url) < 9:
            errors.append("Server URL must include a host name or IP address (e.g. http://127.0.0.1:11434)")

        # 2. Model
        trimmed_model = str(model).strip()
        if not trimmed_model:
            errors.append("Model name cannot be empty")

        # 3. Timeout
        val_timeout = DEFAULT_TIMEOUT
        raw_timeout = str(timeout_str).strip()
        if not raw_timeout:
            errors.append("Timeout must be a valid positive number")
        else:
            try:
                val_timeout = float(raw_timeout)
                if val_timeout < MIN_TIMEOUT or val_timeout > MAX_TIMEOUT:
                    errors.append(f"Timeout must be between {MIN_TIMEOUT:.0f}s and {MAX_TIMEOUT:.0f}s")
            except (ValueError, TypeError):
                errors.append("Timeout must be a valid positive number")

        # 4. Temperature
        val_temp = DEFAULT_TEMPERATURE
        raw_temp = str(temperature_str).strip()
        if not raw_temp:
            errors.append("Temperature must be a valid number (e.g. 0.2)")
        else:
            try:
                val_temp = float(raw_temp)
                if val_temp < MIN_TEMPERATURE or val_temp > MAX_TEMPERATURE:
                    errors.append(f"Temperature must be between {MIN_TEMPERATURE:.1f} and {MAX_TEMPERATURE:.1f}")
            except (ValueError, TypeError):
                errors.append("Temperature must be a valid number (e.g. 0.2)")

        # 5. Context Window (num_ctx) — optional; if omitted, Ollama default is used
        val_ctx: Optional[int] = None
        raw_ctx = str(num_ctx_str).strip() if num_ctx_str is not None else ""
        if raw_ctx:
            try:
                val_ctx = int(raw_ctx)
                if val_ctx < MIN_NUM_CTX or val_ctx > MAX_NUM_CTX:
                    errors.append(f"Context window must be between {MIN_NUM_CTX} and {MAX_NUM_CTX} tokens")
            except (ValueError, TypeError):
                errors.append("Context window must be an integer token count (e.g. 4096)")

        if errors:
            return False, None, errors

        try:
            config = OllamaConfig(
                base_url=trimmed_url,
                model=trimmed_model,
                timeout=val_timeout,
                temperature=val_temp,
                num_ctx=val_ctx,
            )
            return True, config, []
        except AIValidationError as exc:
            return False, None, [str(exc)]
