"""FLEX 1.0 — Stage 1F.1 Optional Live Integration Tests for Local Ollama.

These tests communicate with a real, local Ollama daemon and are strictly OPT-IN.
They never execute by default in offline CI environments or standard unit-test runs.

Activation:
    Set environment variable `FLEX_RUN_OLLAMA_TESTS=1`.

Behavior when opted in:
    - Bounded health probe against the configured Ollama base URL.
    - Inspects whether the configured model (default: qwen2.5-coder:3b) is present.
    - If Ollama is not running or the model is not found, the test cleanly skips with
      a descriptive skip reason instead of failing.
    - Never automatically starts the Ollama daemon or downloads models.
    - Strictly avoids any network activity during module import or in decorators.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

# Ensure repository root is on sys.path
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))

from backend.ai import LLMResponse, OllamaConfig, OllamaProvider


class TestOllamaLiveIntegration(unittest.TestCase):
    """Opt-in live integration tests against local Ollama service."""

    def setUp(self) -> None:
        # 1. Gate execution on explicit opt-in environment variable
        opt_in = os.environ.get("FLEX_RUN_OLLAMA_TESTS")
        if opt_in != "1":
            self.skipTest(
                "Live Ollama tests are disabled by default. "
                "Set environment variable FLEX_RUN_OLLAMA_TESTS=1 to run live tests."
            )

        # 2. Initialize provider (no network I/O in __init__)
        self.config = OllamaConfig()
        self.provider = OllamaProvider(self.config)

        # 3. Check service health with a bounded timeout
        if not self.provider.check_health():
            self.skipTest(
                f"Local Ollama service is unreachable at {self.config.base_url}. "
                "Ensure Ollama daemon is running to execute live tests."
            )

        # 4. Check whether configured model is pulled locally
        if not self.provider.is_model_available(self.config.model):
            self.skipTest(
                f"Model '{self.config.model}' is not available in local Ollama storage. "
                "Skipping live inference test to prevent automatic downloads."
            )

    def test_live_minimal_inference_round_trip(self) -> None:
        """Executes a short, deterministic inference request against the local Ollama instance."""
        prompt = "Answer in exactly one word: What is 2 + 2?"
        response = self.provider.generate(
            prompt=prompt,
            system_prompt="You are a concise arithmetic calculator. Reply with single words only.",
            timeout=30.0,
        )

        self.assertIsInstance(response, LLMResponse)
        self.assertIsInstance(response.text, str)
        self.assertGreater(len(response.text.strip()), 0, "Response text must not be empty")
        self.assertIn("4", response.text.lower())
        self.assertEqual(response.model, self.config.model)


if __name__ == "__main__":
    unittest.main()
