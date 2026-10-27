"""Tests for FLEX 1.0 centralized SettingsManager and configuration persistence (Stage 2E).

Covers:
    - Default settings creation and resolution (%LOCALAPPDATA%\\FLEX\\settings.json)
    - Thread-safe, atomic disk persistence (JSON serialization)
    - Preservation of last_workspace_path across AI settings save, update, and reset
    - Graceful fallback upon missing or corrupted JSON files
    - Strict input validation via validate_ollama_settings()
"""

import json
import os
import shutil
import tempfile
import unittest
from pathlib import Path
from typing import Any, Dict

from backend.ai.config import (
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
from backend.config import SettingsManager


class TestSettingsManagerDefaults(unittest.TestCase):
    """Tests for SettingsManager initialization and default values."""

    def setUp(self) -> None:
        self.temp_dir = tempfile.mkdtemp()
        self.settings_file = Path(self.temp_dir) / "settings.json"
        self.mgr = SettingsManager(settings_file=self.settings_file)

    def tearDown(self) -> None:
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_default_ollama_config_when_file_missing(self) -> None:
        """When no file exists, get_ollama_config() returns built-in defaults."""
        self.assertFalse(self.settings_file.exists())
        cfg = self.mgr.get_ollama_config()
        self.assertEqual(cfg.base_url, DEFAULT_BASE_URL)
        self.assertEqual(cfg.model, DEFAULT_MODEL)
        self.assertEqual(cfg.timeout, DEFAULT_TIMEOUT)
        self.assertAlmostEqual(cfg.temperature, DEFAULT_TEMPERATURE)
        self.assertIsNone(cfg.num_ctx)

    def test_default_workspace_path_is_none(self) -> None:
        """When no file exists, get_last_workspace_path() returns None."""
        self.assertIsNone(self.mgr.get_last_workspace_path())


class TestSettingsManagerPersistence(unittest.TestCase):
    """Tests for atomic file writes and round-trip configuration persistence."""

    def setUp(self) -> None:
        self.temp_dir = tempfile.mkdtemp()
        self.settings_file = Path(self.temp_dir) / "settings.json"
        self.mgr = SettingsManager(settings_file=self.settings_file)

    def tearDown(self) -> None:
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_set_and_reload_ollama_config(self) -> None:
        """Verifies saving an OllamaConfig and reloading it across instances."""
        new_cfg = OllamaConfig(
            base_url="http://192.168.1.50:11434",
            model="mistral:7b",
            timeout=45.0,
            temperature=0.7,
            num_ctx=8192,
        )
        self.mgr.set_ollama_config(new_cfg)
        self.assertTrue(self.settings_file.exists())

        # Create a new manager instance pointing to the same file
        reloaded_mgr = SettingsManager(settings_file=self.settings_file)
        loaded_cfg = reloaded_mgr.get_ollama_config()

        self.assertEqual(loaded_cfg.base_url, "http://192.168.1.50:11434")
        self.assertEqual(loaded_cfg.model, "mistral:7b")
        self.assertEqual(loaded_cfg.timeout, 45.0)
        self.assertAlmostEqual(loaded_cfg.temperature, 0.7)
        self.assertEqual(loaded_cfg.num_ctx, 8192)

    def test_preserve_last_workspace_path_on_ollama_update(self) -> None:
        """CRITICAL SAFEGUARD: last_workspace_path must be preserved when saving Ollama settings."""
        self.mgr.set_last_workspace_path(r"C:\Projects\MyWorkspace")
        self.assertEqual(self.mgr.get_last_workspace_path(), r"C:\Projects\MyWorkspace")

        # Update Ollama config
        new_cfg = OllamaConfig(model="codellama:13b", temperature=0.5)
        self.mgr.set_ollama_config(new_cfg)

        # Verify last_workspace_path is still present and unmodified
        reloaded_mgr = SettingsManager(settings_file=self.settings_file)
        self.assertEqual(reloaded_mgr.get_last_workspace_path(), r"C:\Projects\MyWorkspace")
        self.assertEqual(reloaded_mgr.get_ollama_config().model, "codellama:13b")

    def test_preserve_last_workspace_path_on_ollama_reset(self) -> None:
        """CRITICAL SAFEGUARD: last_workspace_path must be preserved when resetting Ollama config."""
        self.mgr.set_last_workspace_path(r"D:\Data\Workspace")
        new_cfg = OllamaConfig(model="custom-model:latest", temperature=0.9, num_ctx=16384)
        self.mgr.set_ollama_config(new_cfg)

        # Reset Ollama config to defaults
        reset_cfg = self.mgr.reset_ollama_config()
        self.assertEqual(reset_cfg.model, DEFAULT_MODEL)
        self.assertAlmostEqual(reset_cfg.temperature, DEFAULT_TEMPERATURE)

        # Workspace path must remain intact
        reloaded_mgr = SettingsManager(settings_file=self.settings_file)
        self.assertEqual(reloaded_mgr.get_last_workspace_path(), r"D:\Data\Workspace")
        self.assertEqual(reloaded_mgr.get_ollama_config().model, DEFAULT_MODEL)

    def test_preserve_ollama_config_on_workspace_update(self) -> None:
        """Updating workspace path does not alter saved Ollama settings."""
        custom_cfg = OllamaConfig(model="phi3:mini", timeout=25.0)
        self.mgr.set_ollama_config(custom_cfg)

        self.mgr.set_last_workspace_path(r"E:\NewWorkspace")

        reloaded = SettingsManager(settings_file=self.settings_file)
        self.assertEqual(reloaded.get_last_workspace_path(), r"E:\NewWorkspace")
        self.assertEqual(reloaded.get_ollama_config().model, "phi3:mini")
        self.assertEqual(reloaded.get_ollama_config().timeout, 25.0)

    def test_corrupted_json_file_falls_back_gracefully(self) -> None:
        """Corrupted/malformed settings file triggers safe fallback to default values."""
        self.settings_file.write_text("{{corrupted json syntax! --", encoding="utf-8")

        mgr = SettingsManager(settings_file=self.settings_file)
        cfg = mgr.get_ollama_config()
        self.assertEqual(cfg.model, DEFAULT_MODEL)
        self.assertIsNone(mgr.get_last_workspace_path())


class TestSettingsValidation(unittest.TestCase):
    """Tests for SettingsManager.validate_ollama_settings()."""

    def test_valid_inputs(self) -> None:
        """Valid parameters return (True, OllamaConfig, [])."""
        is_valid, cfg, errors = SettingsManager.validate_ollama_settings(
            url="http://127.0.0.1:11434",
            model="qwen2.5-coder:3b",
            timeout="30",
            temperature="0.2",
            num_ctx="4096",
        )
        self.assertTrue(is_valid)
        self.assertIsNotNone(cfg)
        self.assertEqual(errors, [])
        self.assertEqual(cfg.base_url, "http://127.0.0.1:11434")
        self.assertEqual(cfg.model, "qwen2.5-coder:3b")
        self.assertEqual(cfg.timeout, 30.0)
        self.assertAlmostEqual(cfg.temperature, 0.2)
        self.assertEqual(cfg.num_ctx, 4096)

    def test_valid_inputs_with_empty_num_ctx(self) -> None:
        """Empty or whitespace num_ctx is optional and returns num_ctx=None."""
        is_valid, cfg, errors = SettingsManager.validate_ollama_settings(
            url="http://localhost:11434",
            model="llama3.2:1b",
            timeout="15",
            temperature="0.7",
            num_ctx="",
        )
        self.assertTrue(is_valid)
        self.assertIsNotNone(cfg)
        self.assertIsNone(cfg.num_ctx)

    def test_invalid_url_schemes(self) -> None:
        """URL must start with http:// or https://."""
        for bad_url in ["ftp://localhost", "127.0.0.1:11434", "", "   ", "http://"]:
            is_valid, cfg, errors = SettingsManager.validate_ollama_settings(
                url=bad_url,
                model="test-model",
                timeout="30",
                temperature="0.2",
            )
            self.assertFalse(is_valid)
            self.assertIsNone(cfg)
            self.assertTrue(any("URL" in e for e in errors))

    def test_invalid_model_name(self) -> None:
        """Empty or whitespace model name is rejected."""
        for bad_model in ["", "   "]:
            is_valid, cfg, errors = SettingsManager.validate_ollama_settings(
                url="http://localhost:11434",
                model=bad_model,
                timeout="30",
                temperature="0.2",
            )
            self.assertFalse(is_valid)
            self.assertIsNone(cfg)
            self.assertTrue(any("Model" in e for e in errors))

    def test_invalid_timeout(self) -> None:
        """Non-numeric, negative, or out-of-range timeouts are rejected."""
        for bad_timeout in ["abc", "-5", "0", "700", ""]:
            is_valid, cfg, errors = SettingsManager.validate_ollama_settings(
                url="http://localhost:11434",
                model="test-model",
                timeout=bad_timeout,
                temperature="0.2",
            )
            self.assertFalse(is_valid)
            self.assertIsNone(cfg)
            self.assertTrue(any("Timeout" in e for e in errors))

    def test_invalid_temperature(self) -> None:
        """Non-numeric, negative, or > 2.0 temperatures are rejected."""
        for bad_temp in ["abc", "-0.1", "2.1", "99", ""]:
            is_valid, cfg, errors = SettingsManager.validate_ollama_settings(
                url="http://localhost:11434",
                model="test-model",
                timeout="30",
                temperature=bad_temp,
            )
            self.assertFalse(is_valid)
            self.assertIsNone(cfg)
            self.assertTrue(any("Temperature" in e for e in errors))

    def test_invalid_num_ctx(self) -> None:
        """Non-integer, < 256, or > 131072 num_ctx are rejected."""
        for bad_ctx in ["abc", "100", "200000", "-50"]:
            is_valid, cfg, errors = SettingsManager.validate_ollama_settings(
                url="http://localhost:11434",
                model="test-model",
                timeout="30",
                temperature="0.2",
                num_ctx=bad_ctx,
            )
            self.assertFalse(is_valid)
            self.assertIsNone(cfg)
            self.assertTrue(any("Context window" in e for e in errors))


if __name__ == "__main__":
    unittest.main()
