"""FLEX 1.0 — Stage 1F.1 Unit Tests for AI Provider Foundation.

Tests:
    - Default, explicit, and environment-variable configuration precedence.
    - Configuration and message validation rules.
    - Bounded in-memory conversation history, FIFO eviction, and defensive copies.
    - Offline OllamaProvider generation with mocked standard library urllib responses.
    - Correct request URL, headers, and JSON body construction (stream=False, model, options).
    - System prompt and history message serialization.
    - Assistant text and execution metric extraction.
    - Error translation: service unavailable, timeout, missing model (404), HTTP errors,
      malformed JSON, and missing/invalid response fields.
    - Health check and model availability discovery.
    - Verification that initialization performs zero network calls.
"""

from __future__ import annotations

import io
import json
import os
import socket
import sys
import unittest
from pathlib import Path
from typing import Any, Dict
from unittest.mock import MagicMock, patch
from urllib.error import HTTPError, URLError

# Ensure repository root is on sys.path
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))

from backend.ai import (
    DEFAULT_BASE_URL,
    DEFAULT_MODEL,
    DEFAULT_TIMEOUT,
    AIModelNotFoundError,
    AIRequestError,
    AIResponseError,
    AIServiceUnavailableError,
    AITimeoutError,
    AIValidationError,
    ChatMessage,
    InMemoryConversationHistory,
    LLMResponse,
    OllamaConfig,
    OllamaProvider,
)


class FakeHTTPResponse:
    """Mock standard library HTTP response supporting context manager and read()."""

    def __init__(self, body_bytes: bytes, status: int = 200) -> None:
        self.body_bytes = body_bytes
        self.status = status

    def read(self) -> bytes:
        return self.body_bytes

    def __enter__(self) -> "FakeHTTPResponse":
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        pass


class TestAIConfiguration(unittest.TestCase):
    """Validates OllamaConfig precedence, defaults, and validation."""

    def setUp(self) -> None:
        self._saved_env = {
            "FLEX_OLLAMA_BASE_URL": os.environ.get("FLEX_OLLAMA_BASE_URL"),
            "FLEX_OLLAMA_HOST": os.environ.get("FLEX_OLLAMA_HOST"),
            "FLEX_OLLAMA_URL": os.environ.get("FLEX_OLLAMA_URL"),
            "FLEX_OLLAMA_MODEL": os.environ.get("FLEX_OLLAMA_MODEL"),
            "FLEX_OLLAMA_TIMEOUT": os.environ.get("FLEX_OLLAMA_TIMEOUT"),
        }
        for k in self._saved_env:
            os.environ.pop(k, None)

    def tearDown(self) -> None:
        for k, v in self._saved_env.items():
            if v is not None:
                os.environ[k] = v
            else:
                os.environ.pop(k, None)

    def test_default_configuration(self) -> None:
        cfg = OllamaConfig()
        self.assertEqual(cfg.base_url, DEFAULT_BASE_URL)
        self.assertEqual(cfg.model, DEFAULT_MODEL)
        self.assertEqual(cfg.timeout, DEFAULT_TIMEOUT)
        self.assertEqual(cfg.options, {"temperature": 0.2})

    def test_explicit_configuration_overrides_defaults(self) -> None:
        cfg = OllamaConfig(
            base_url="http://192.168.1.100:11434/",
            model="custom-model:latest",
            timeout=15.5,
            options={"temperature": 0.7, "top_k": 40},
        )
        self.assertEqual(cfg.base_url, "http://192.168.1.100:11434")
        self.assertEqual(cfg.model, "custom-model:latest")
        self.assertEqual(cfg.timeout, 15.5)
        self.assertEqual(cfg.options, {"temperature": 0.7, "top_k": 40})

    def test_environment_variable_configuration(self) -> None:
        os.environ["FLEX_OLLAMA_BASE_URL"] = "http://localhost:8080"
        os.environ["FLEX_OLLAMA_MODEL"] = "llama3:8b"
        os.environ["FLEX_OLLAMA_TIMEOUT"] = "45.0"

        cfg = OllamaConfig()
        self.assertEqual(cfg.base_url, "http://localhost:8080")
        self.assertEqual(cfg.model, "llama3:8b")
        self.assertEqual(cfg.timeout, 45.0)

    def test_explicit_overrides_environment_variables(self) -> None:
        os.environ["FLEX_OLLAMA_BASE_URL"] = "http://env-host:11434"
        os.environ["FLEX_OLLAMA_MODEL"] = "env-model"
        os.environ["FLEX_OLLAMA_TIMEOUT"] = "60.0"

        cfg = OllamaConfig(
            base_url="http://explicit-host:11434",
            model="explicit-model",
            timeout=10.0,
        )
        self.assertEqual(cfg.base_url, "http://explicit-host:11434")
        self.assertEqual(cfg.model, "explicit-model")
        self.assertEqual(cfg.timeout, 10.0)

    def test_invalid_base_url_scheme_raises(self) -> None:
        with self.assertRaises(AIValidationError):
            OllamaConfig(base_url="ftp://localhost:11434")

        with self.assertRaises(AIValidationError):
            OllamaConfig(base_url="invalid-no-scheme")

        with self.assertRaises(AIValidationError):
            OllamaConfig(base_url=12345)  # type: ignore

    def test_empty_model_raises(self) -> None:
        with self.assertRaises(AIValidationError):
            OllamaConfig(model="")

        with self.assertRaises(AIValidationError):
            OllamaConfig(model="   ")

    def test_invalid_timeout_raises(self) -> None:
        with self.assertRaises(AIValidationError):
            OllamaConfig(timeout=0)

        with self.assertRaises(AIValidationError):
            OllamaConfig(timeout=-5.0)

        with self.assertRaises(AIValidationError):
            OllamaConfig(timeout="not-a-number")  # type: ignore

    def test_invalid_env_timeout_raises(self) -> None:
        os.environ["FLEX_OLLAMA_TIMEOUT"] = "invalid_number"
        with self.assertRaises(AIValidationError):
            OllamaConfig()


class TestChatMessageAndDataModels(unittest.TestCase):
    """Validates ChatMessage and LLMResponse validation rules."""

    def test_valid_roles_and_case_normalization(self) -> None:
        m1 = ChatMessage(role="User", content="Hello")
        self.assertEqual(m1.role, "user")
        self.assertEqual(m1.content, "Hello")

        m2 = ChatMessage(role="ASSISTANT", content="Hi")
        self.assertEqual(m2.role, "assistant")

        m3 = ChatMessage(role="system", content="Instructions")
        self.assertEqual(m3.role, "system")

    def test_invalid_role_raises(self) -> None:
        with self.assertRaises(AIValidationError):
            ChatMessage(role="moderator", content="Stop")

        with self.assertRaises(AIValidationError):
            ChatMessage(role="", content="Stop")

        with self.assertRaises(AIValidationError):
            ChatMessage(role=123, content="Stop")  # type: ignore

    def test_invalid_content_type_raises(self) -> None:
        with self.assertRaises(AIValidationError):
            ChatMessage(role="user", content=None)  # type: ignore

    def test_to_dict_serialization(self) -> None:
        msg = ChatMessage(role="user", content="Explain pointers")
        self.assertEqual(msg.to_dict(), {"role": "user", "content": "Explain pointers"})

    def test_llm_response_validation(self) -> None:
        res = LLMResponse(text="Pointers store memory addresses", model="qwen2.5-coder:3b")
        self.assertEqual(res.text, "Pointers store memory addresses")
        self.assertEqual(res.model, "qwen2.5-coder:3b")

        with self.assertRaises(AIValidationError):
            LLMResponse(text=123, model="model")  # type: ignore

        with self.assertRaises(AIValidationError):
            LLMResponse(text="Valid", model="")


class TestInMemoryConversationHistory(unittest.TestCase):
    """Validates bounded in-memory conversation history and eviction."""

    def test_add_and_retrieve_messages(self) -> None:
        history = InMemoryConversationHistory(max_messages=10)
        self.assertEqual(len(history), 0)

        history.add_user_message("Hello")
        history.add_assistant_message("Hi there!")

        msgs = history.get_messages()
        self.assertEqual(len(msgs), 2)
        self.assertEqual(msgs[0].role, "user")
        self.assertEqual(msgs[0].content, "Hello")
        self.assertEqual(msgs[1].role, "assistant")
        self.assertEqual(msgs[1].content, "Hi there!")

    def test_fifo_eviction_when_max_exceeded(self) -> None:
        history = InMemoryConversationHistory(max_messages=3)
        history.add_user_message("msg 1")
        history.add_assistant_message("msg 2")
        history.add_user_message("msg 3")
        self.assertEqual(len(history), 3)

        # Adding 4th message should evict 'msg 1'
        history.add_assistant_message("msg 4")
        self.assertEqual(len(history), 3)
        contents = [m.content for m in history.get_messages()]
        self.assertEqual(contents, ["msg 2", "msg 3", "msg 4"])

    def test_clear_history(self) -> None:
        history = InMemoryConversationHistory(max_messages=5)
        history.add_user_message("msg 1")
        history.add_assistant_message("msg 2")
        self.assertEqual(len(history), 2)

        history.clear()
        self.assertEqual(len(history), 0)
        self.assertEqual(history.get_messages(), [])

    def test_defensive_copying_prevents_external_mutation(self) -> None:
        history = InMemoryConversationHistory(max_messages=5)
        history.add_user_message("Original")

        exported = history.get_messages()
        exported.clear()

        # Internal buffer must remain unchanged
        self.assertEqual(len(history), 1)
        self.assertEqual(history.get_messages()[0].content, "Original")

    def test_invalid_max_messages_raises(self) -> None:
        with self.assertRaises(AIValidationError):
            InMemoryConversationHistory(max_messages=0)

        with self.assertRaises(AIValidationError):
            InMemoryConversationHistory(max_messages=-1)

        with self.assertRaises(AIValidationError):
            InMemoryConversationHistory(max_messages="10")  # type: ignore

    def test_invalid_message_type_raises(self) -> None:
        history = InMemoryConversationHistory(max_messages=5)
        with self.assertRaises(AIValidationError):
            history.add_message("Not a ChatMessage")  # type: ignore


class TestOllamaProviderOfflineGeneration(unittest.TestCase):
    """Validates OllamaProvider request framing and response parsing with mocked HTTP."""

    def setUp(self) -> None:
        self.config = OllamaConfig(
            base_url="http://127.0.0.1:11434",
            model="qwen2.5-coder:3b",
            timeout=10.0,
        )
        self.provider = OllamaProvider(self.config)

    @patch("backend.ai.ollama_provider.urlopen")
    def test_successful_generation_default_payload(self, mock_urlopen: MagicMock) -> None:
        mock_response_data = {
            "model": "qwen2.5-coder:3b",
            "message": {"role": "assistant", "content": "Pointers hold addresses."},
            "done": True,
            "done_reason": "stop",
            "total_duration": 150000000,
            "load_duration": 50000000,
            "prompt_eval_count": 8,
            "eval_count": 12,
            "eval_duration": 100000000,
        }
        mock_urlopen.return_value = FakeHTTPResponse(
            json.dumps(mock_response_data).encode("utf-8"), status=200
        )

        response = self.provider.generate(prompt="What are pointers?")

        self.assertIsInstance(response, LLMResponse)
        self.assertEqual(response.text, "Pointers hold addresses.")
        self.assertEqual(response.model, "qwen2.5-coder:3b")
        self.assertEqual(response.eval_count, 12)
        self.assertEqual(response.total_duration_ns, 150000000)

        # Verify request structure sent to urlopen
        self.assertEqual(mock_urlopen.call_count, 1)
        req, kwargs = mock_urlopen.call_args
        request_obj = req[0]
        self.assertEqual(request_obj.full_url, "http://127.0.0.1:11434/api/chat")
        self.assertEqual(request_obj.get_method(), "POST")
        self.assertEqual(kwargs.get("timeout"), 10.0)

        sent_body = json.loads(request_obj.data.decode("utf-8"))
        self.assertEqual(sent_body["model"], "qwen2.5-coder:3b")
        self.assertIs(sent_body["stream"], False)
        self.assertEqual(sent_body["messages"], [{"role": "user", "content": "What are pointers?"}])

    @patch("backend.ai.ollama_provider.urlopen")
    def test_generation_with_system_prompt_and_history(self, mock_urlopen: MagicMock) -> None:
        mock_response_data = {
            "model": "qwen2.5-coder:3b",
            "message": {"role": "assistant", "content": "You dereference with *."},
            "done": True,
        }
        mock_urlopen.return_value = FakeHTTPResponse(
            json.dumps(mock_response_data).encode("utf-8")
        )

        history = [
            ChatMessage(role="user", content="How do I declare a pointer?"),
            ChatMessage(role="assistant", content="int *p;"),
        ]

        response = self.provider.generate(
            prompt="How do I read its value?",
            system_prompt="You are a strict C11 mentor.",
            history=history,
            timeout=5.0,
        )

        self.assertEqual(response.text, "You dereference with *.")

        req, kwargs = mock_urlopen.call_args
        self.assertEqual(kwargs.get("timeout"), 5.0)
        sent_body = json.loads(req[0].data.decode("utf-8"))
        expected_messages = [
            {"role": "system", "content": "You are a strict C11 mentor."},
            {"role": "user", "content": "How do I declare a pointer?"},
            {"role": "assistant", "content": "int *p;"},
            {"role": "user", "content": "How do I read its value?"},
        ]
        self.assertEqual(sent_body["messages"], expected_messages)

    @patch("backend.ai.ollama_provider.urlopen")
    def test_generation_with_in_memory_history_instance(self, mock_urlopen: MagicMock) -> None:
        mock_response_data = {
            "model": "qwen2.5-coder:3b",
            "message": {"role": "assistant", "content": "Sure thing."},
        }
        mock_urlopen.return_value = FakeHTTPResponse(
            json.dumps(mock_response_data).encode("utf-8")
        )

        hist = InMemoryConversationHistory(max_messages=10)
        hist.add_user_message("Prior question")
        hist.add_assistant_message("Prior answer")

        self.provider.generate(prompt="Follow-up question", history=hist)

        req, _ = mock_urlopen.call_args
        sent_body = json.loads(req[0].data.decode("utf-8"))
        expected_roles = [m["role"] for m in sent_body["messages"]]
        self.assertEqual(expected_roles, ["user", "assistant", "user"])

    def test_empty_prompt_raises_validation_error(self) -> None:
        with self.assertRaises(AIValidationError):
            self.provider.generate(prompt="")

        with self.assertRaises(AIValidationError):
            self.provider.generate(prompt="   ")

        with self.assertRaises(AIValidationError):
            self.provider.generate(prompt=None)  # type: ignore

    def test_invalid_timeout_override_raises(self) -> None:
        with self.assertRaises(AIValidationError):
            self.provider.generate(prompt="Valid", timeout=-1.0)


class TestOllamaProviderErrorHandling(unittest.TestCase):
    """Validates robust translation of network, HTTP, and API errors."""

    def setUp(self) -> None:
        self.provider = OllamaProvider()

    @patch("backend.ai.ollama_provider.urlopen")
    def test_connection_refused_raises_service_unavailable(self, mock_urlopen: MagicMock) -> None:
        mock_urlopen.side_effect = URLError(ConnectionRefusedError("Connection refused"))

        with self.assertRaises(AIServiceUnavailableError) as ctx:
            self.provider.generate(prompt="Hello")

        self.assertIn("unreachable", str(ctx.exception).lower())

    @patch("backend.ai.ollama_provider.urlopen")
    def test_socket_timeout_raises_ai_timeout_error(self, mock_urlopen: MagicMock) -> None:
        mock_urlopen.side_effect = socket.timeout("timed out")

        with self.assertRaises(AITimeoutError) as ctx:
            self.provider.generate(prompt="Hello")

        self.assertIn("timed out", str(ctx.exception).lower())

    @patch("backend.ai.ollama_provider.urlopen")
    def test_url_error_timeout_raises_ai_timeout_error(self, mock_urlopen: MagicMock) -> None:
        mock_urlopen.side_effect = URLError(socket.timeout("The read operation timed out"))

        with self.assertRaises(AITimeoutError):
            self.provider.generate(prompt="Hello")

    @patch("backend.ai.ollama_provider.urlopen")
    def test_http_404_model_not_found_raises_model_not_found_error(
        self, mock_urlopen: MagicMock
    ) -> None:
        fp = io.BytesIO(b'{"error":"model \'qwen2.5-coder:3b\' not found"}')
        mock_urlopen.side_effect = HTTPError(
            url="http://127.0.0.1:11434/api/chat",
            code=404,
            msg="Not Found",
            hdrs={},  # type: ignore
            fp=fp,
        )

        with self.assertRaises(AIModelNotFoundError) as ctx:
            self.provider.generate(prompt="Hello")

        self.assertIn("not found", str(ctx.exception).lower())

    @patch("backend.ai.ollama_provider.urlopen")
    def test_http_500_raises_ai_request_error(self, mock_urlopen: MagicMock) -> None:
        fp = io.BytesIO(b'{"error":"Internal Server Error"}')
        mock_urlopen.side_effect = HTTPError(
            url="http://127.0.0.1:11434/api/chat",
            code=500,
            msg="Internal Error",
            hdrs={},  # type: ignore
            fp=fp,
        )

        with self.assertRaises(AIRequestError) as ctx:
            self.provider.generate(prompt="Hello")

        self.assertIn("500", str(ctx.exception))

    @patch("backend.ai.ollama_provider.urlopen")
    def test_malformed_json_raises_ai_response_error(self, mock_urlopen: MagicMock) -> None:
        mock_urlopen.return_value = FakeHTTPResponse(b"NOT_JSON_BODY_<<<>>>")

        with self.assertRaises(AIResponseError):
            self.provider.generate(prompt="Hello")

    @patch("backend.ai.ollama_provider.urlopen")
    def test_missing_message_field_raises_ai_response_error(self, mock_urlopen: MagicMock) -> None:
        mock_urlopen.return_value = FakeHTTPResponse(b'{"model":"qwen2.5-coder:3b"}')

        with self.assertRaises(AIResponseError) as ctx:
            self.provider.generate(prompt="Hello")

        self.assertIn("message", str(ctx.exception))

    @patch("backend.ai.ollama_provider.urlopen")
    def test_missing_content_field_raises_ai_response_error(self, mock_urlopen: MagicMock) -> None:
        mock_urlopen.return_value = FakeHTTPResponse(
            b'{"model":"qwen2.5-coder:3b","message":{"role":"assistant"}}'
        )

        with self.assertRaises(AIResponseError):
            self.provider.generate(prompt="Hello")


class TestOllamaProviderHealthAndDiscovery(unittest.TestCase):
    """Validates health check and model availability discovery without side effects."""

    def setUp(self) -> None:
        self.provider = OllamaProvider()

    def test_no_network_on_init(self) -> None:
        # Creating OllamaProvider must not trigger any socket or URL calls
        with patch("backend.ai.ollama_provider.urlopen") as mock_open:
            prov = OllamaProvider()
            self.assertIsNotNone(prov)
            mock_open.assert_not_called()

    @patch("backend.ai.ollama_provider.urlopen")
    def test_check_health_success(self, mock_urlopen: MagicMock) -> None:
        mock_urlopen.return_value = FakeHTTPResponse(b'{"version":"0.40.6"}', status=200)
        self.assertTrue(self.provider.check_health())

    @patch("backend.ai.ollama_provider.urlopen")
    def test_check_health_failure_on_connection_error(self, mock_urlopen: MagicMock) -> None:
        mock_urlopen.side_effect = URLError(ConnectionRefusedError("Refused"))
        self.assertFalse(self.provider.check_health())

    @patch("backend.ai.ollama_provider.urlopen")
    def test_check_health_failure_on_bad_json(self, mock_urlopen: MagicMock) -> None:
        mock_urlopen.return_value = FakeHTTPResponse(b"MALFORMED_DATA", status=200)
        self.assertFalse(self.provider.check_health())

    @patch("backend.ai.ollama_provider.urlopen")
    def test_is_model_available_exact_match(self, mock_urlopen: MagicMock) -> None:
        tags_response = {
            "models": [
                {"name": "qwen2.5-coder:3b", "size": 1900000000},
                {"name": "llama3:8b", "size": 4000000000},
            ]
        }
        mock_urlopen.return_value = FakeHTTPResponse(
            json.dumps(tags_response).encode("utf-8")
        )

        self.assertTrue(self.provider.is_model_available("qwen2.5-coder:3b"))
        self.assertTrue(self.provider.is_model_available("llama3:8b"))
        self.assertFalse(self.provider.is_model_available("mistral:7b"))

    @patch("backend.ai.ollama_provider.urlopen")
    def test_is_model_available_latest_tag_match(self, mock_urlopen: MagicMock) -> None:
        tags_response = {
            "models": [
                {"name": "qwen2.5-coder:latest"},
            ]
        }
        mock_urlopen.return_value = FakeHTTPResponse(
            json.dumps(tags_response).encode("utf-8")
        )

        # Checking bare name should match :latest
        self.assertTrue(self.provider.is_model_available("qwen2.5-coder"))
        self.assertFalse(self.provider.is_model_available("other-model"))

    @patch("backend.ai.ollama_provider.urlopen")
    def test_is_model_available_failure_on_service_down(self, mock_urlopen: MagicMock) -> None:
        mock_urlopen.side_effect = URLError(ConnectionRefusedError("Refused"))
        self.assertFalse(self.provider.is_model_available("qwen2.5-coder:3b"))


if __name__ == "__main__":
    unittest.main()
