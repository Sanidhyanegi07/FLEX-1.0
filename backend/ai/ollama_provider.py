"""FLEX 1.0 — Ollama Local AI Provider.

Implements the LLMProvider interface for local Ollama instances using Python's
standard-library HTTP and JSON modules (urllib.request, urllib.error, json, socket).

Features:
    - Structured chat completions via Ollama's `/api/chat` endpoint (stream=False).
    - Support for system messages, user prompts, and multi-turn conversation history.
    - Health inspection via `/api/version` and model discovery via `/api/tags`.
    - Strict error classification: service unreachability, missing models, timeouts,
      and malformed response handling.
    - Zero third-party dependencies; strictly adheres to Python 3 standard library.
"""

from __future__ import annotations

import json
import logging
import socket
from typing import Any, Dict, List, Optional, Union
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from .base import ChatMessage, LLMProvider, LLMResponse
from .config import OllamaConfig
from .exceptions import (
    AIModelNotFoundError,
    AIRequestError,
    AIResponseError,
    AIServiceUnavailableError,
    AITimeoutError,
    AIValidationError,
)
from .history import InMemoryConversationHistory

logger = logging.getLogger(__name__)


class OllamaProvider(LLMProvider):
    """Local inference provider communicating with an Ollama daemon over HTTP.

    Attributes:
        config: The OllamaConfig instance governing connection and model settings.
    """

    def __init__(self, config: Optional[OllamaConfig] = None) -> None:
        """Initializes the Ollama provider.

        Notice:
            Object construction performs no network calls or service checks.
            All I/O operations occur explicitly during method calls.

        Args:
            config: Optional configuration instance. If None, default configuration is resolved.
        """
        self.config: OllamaConfig = config if config is not None else OllamaConfig()

    def generate(
        self,
        prompt: str,
        system_prompt: Optional[str] = None,
        history: Optional[Union[List[ChatMessage], InMemoryConversationHistory]] = None,
        timeout: Optional[float] = None,
    ) -> LLMResponse:
        """Generates a chat completion from the local Ollama instance.

        Args:
            prompt: User prompt string. Must be non-empty.
            system_prompt: Optional instructions directing assistant behavior.
            history: Optional conversation history (either a list of ChatMessage or
                     an InMemoryConversationHistory instance).
            timeout: Optional override for the request timeout in seconds.

        Returns:
            LLMResponse: Structured assistant text and performance metrics.

        Raises:
            AIValidationError: If input parameters fail validation.
            AIServiceUnavailableError: If Ollama daemon is unreachable.
            AIModelNotFoundError: If the configured model is missing on the host.
            AITimeoutError: If inference exceeds the timeout.
            AIResponseError: If the server returns malformed or invalid response data.
            AIRequestError: If an HTTP 4xx/5xx status is returned.
        """
        # 1. Input validation
        if not isinstance(prompt, str) or not prompt.strip():
            raise AIValidationError("User prompt must be a non-empty string")

        if system_prompt is not None and not isinstance(system_prompt, str):
            raise AIValidationError(
                f"system_prompt must be a string if provided, got {type(system_prompt).__name__}"
            )

        effective_timeout = self._resolve_timeout(timeout)

        # 2. Build structured messages payload
        messages_payload: List[Dict[str, str]] = []

        if system_prompt and system_prompt.strip():
            messages_payload.append({"role": "system", "content": system_prompt.strip()})

        if history is not None:
            if isinstance(history, InMemoryConversationHistory):
                history_items = history.get_messages()
            elif isinstance(history, list):
                history_items = history
            else:
                raise AIValidationError(
                    f"history must be a list of ChatMessage or an InMemoryConversationHistory instance, "
                    f"got {type(history).__name__}"
                )

            for idx, item in enumerate(history_items):
                if not isinstance(item, ChatMessage):
                    raise AIValidationError(
                        f"History item at index {idx} is not a ChatMessage instance: {item!r}"
                    )
                messages_payload.append(item.to_dict())

        # Append current user prompt
        messages_payload.append({"role": "user", "content": prompt.strip()})

        # 3. Construct HTTP request body
        request_body: Dict[str, Any] = {
            "model": self.config.model,
            "messages": messages_payload,
            "stream": False,
        }
        if self.config.options:
            request_body["options"] = self.config.options

        url = f"{self.config.base_url}/api/chat"
        payload_bytes = json.dumps(request_body).encode("utf-8")
        req = Request(
            url,
            data=payload_bytes,
            headers={
                "Content-Type": "application/json",
                "Accept": "application/json",
            },
            method="POST",
        )

        logger.debug("Dispatching chat request to %s (model: %s)", url, self.config.model)

        # 4. Perform HTTP request
        try:
            with urlopen(req, timeout=effective_timeout) as response:
                status_code = getattr(response, "status", 200)
                raw_bytes = response.read()
        except HTTPError as e:
            err_body = ""
            try:
                err_body = e.read().decode("utf-8", errors="replace")
            except Exception:
                pass
            finally:
                try:
                    e.close()
                except Exception:
                    pass

            if e.code == 404:
                raise AIModelNotFoundError(
                    f"Model '{self.config.model}' was not found on Ollama server at {self.config.base_url} "
                    f"(HTTP 404): {err_body.strip()}"
                ) from e
            raise AIRequestError(
                f"Ollama API request failed with HTTP {e.code} ({e.reason}): {err_body.strip()}"
            ) from e
        except URLError as e:
            if isinstance(e.reason, socket.timeout) or "timed out" in str(e.reason).lower():
                raise AITimeoutError(
                    f"Inference request timed out after {effective_timeout:.1f}s waiting for Ollama"
                ) from e
            raise AIServiceUnavailableError(
                f"Ollama service is unreachable at {self.config.base_url}: {e.reason}"
            ) from e
        except (socket.timeout, TimeoutError) as e:
            raise AITimeoutError(
                f"Inference request timed out after {effective_timeout:.1f}s waiting for Ollama"
            ) from e
        except OSError as e:
            raise AIServiceUnavailableError(
                f"Network error communicating with Ollama at {self.config.base_url}: {e}"
            ) from e

        # 5. Decode and validate JSON response
        try:
            decoded_text = raw_bytes.decode("utf-8")
            data = json.loads(decoded_text)
        except (UnicodeDecodeError, json.JSONDecodeError) as e:
            raise AIResponseError(
                f"Failed to decode JSON response from Ollama: {e}"
            ) from e

        if not isinstance(data, dict):
            raise AIResponseError(
                f"Expected JSON object from Ollama API, got {type(data).__name__}"
            )

        message_obj = data.get("message")
        if not isinstance(message_obj, dict) or "content" not in message_obj:
            raise AIResponseError(
                "Ollama response is missing required 'message.content' field in payload"
            )

        content = message_obj.get("content")
        if not isinstance(content, str):
            raise AIResponseError(
                f"Ollama response 'content' must be a string, got {type(content).__name__}"
            )

        returned_model = data.get("model") or self.config.model

        return LLMResponse(
            text=content,
            model=returned_model,
            total_duration_ns=data.get("total_duration"),
            load_duration_ns=data.get("load_duration"),
            prompt_eval_count=data.get("prompt_eval_count"),
            eval_count=data.get("eval_count"),
            eval_duration_ns=data.get("eval_duration"),
            done_reason=data.get("done_reason"),
            raw_response=data,
        )

    def check_health(self) -> bool:
        """Pings the Ollama service to check reachability.

        Queries the `/api/version` endpoint with a conservative 2.0-second timeout.
        Never raises an exception.

        Returns:
            bool: True if the service is running and returns HTTP 200, False otherwise.
        """
        url = f"{self.config.base_url}/api/version"
        req = Request(url, headers={"Accept": "application/json"}, method="GET")

        try:
            with urlopen(req, timeout=2.0) as resp:
                status_code = getattr(resp, "status", 200)
                if status_code == 200:
                    raw_data = resp.read()
                    data = json.loads(raw_data.decode("utf-8"))
                    return isinstance(data, dict) and "version" in data
                return False
        except Exception as e:
            logger.debug("Ollama health check failed against %s: %s", url, e)
            return False

    def is_model_available(self, model_name: str) -> bool:
        """Inspects the list of locally pulled models via `/api/tags`.

        Never raises an exception.

        Args:
            model_name: Target model name or tag to locate (e.g. 'qwen2.5-coder:3b').

        Returns:
            bool: True if the model is found in local storage, False otherwise.
        """
        if not isinstance(model_name, str) or not model_name.strip():
            return False

        target = model_name.strip().lower()
        url = f"{self.config.base_url}/api/tags"
        req = Request(url, headers={"Accept": "application/json"}, method="GET")

        try:
            with urlopen(req, timeout=3.0) as resp:
                status_code = getattr(resp, "status", 200)
                if status_code != 200:
                    return False
                raw_data = resp.read()
                data = json.loads(raw_data.decode("utf-8"))
        except Exception as e:
            logger.debug("Ollama model check failed for '%s' against %s: %s", model_name, url, e)
            return False

        if not isinstance(data, dict):
            return False

        models = data.get("models")
        if not isinstance(models, list):
            return False

        for item in models:
            if not isinstance(item, dict):
                continue
            name = item.get("name", "").strip().lower()
            # Exact match (e.g. 'qwen2.5-coder:3b')
            if name == target:
                return True
            # Match bare name with default :latest tag (e.g. target='qwen2.5-coder', name='qwen2.5-coder:latest')
            if ":" not in target and name == f"{target}:latest":
                return True

        return False

    def _resolve_timeout(self, timeout_override: Optional[float]) -> float:
        """Resolves request timeout, validating explicit override if provided."""
        if timeout_override is not None:
            if (
                not isinstance(timeout_override, (int, float))
                or isinstance(timeout_override, bool)
                or timeout_override <= 0.0
            ):
                raise AIValidationError(
                    f"timeout must be a positive number, got: {timeout_override!r}"
                )
            return float(timeout_override)
        return self.config.timeout
