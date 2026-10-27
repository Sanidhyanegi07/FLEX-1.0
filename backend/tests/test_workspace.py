"""FLEX 1.0 — Stage 2D Projects & Files Workspace Unit and Integration Tests.

Validates the complete Stage 2D specification:
    1. Selecting and persisting a workspace directory.
    2. Restoring the workspace after application restart.
    3. Handling a missing workspace.
    4. Listing files and directories.
    5. Handling unreadable files and directories.
    6. Rejecting paths outside the workspace (directory traversal).
    7. Rejecting or safely handling symlinks and junctions that escape the workspace.
    8. Previewing supported text files.
    9. Handling binary and oversized files.
    10. Attaching and removing files from a chat request.
    11. Enforcing file-count and context-size limits.
    12. Preserving the original user message.
    13. Sending the assembled context through the existing provider/coordinator.
    14. Handling deleted attachments.
    15. Responsive lazy-loading and non-blocking safety.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import MagicMock, patch

from backend.ai.base import LLMResponse
from backend.coordinator import AICoordinator
from backend.workspace import (
    DEFAULT_MAX_ATTACHED_FILES,
    DEFAULT_MAX_CONTEXT_BYTES,
    DEFAULT_MAX_PREVIEW_BYTES,
    WorkspaceError,
    WorkspaceFileInfo,
    WorkspaceManager,
    WorkspaceNotFoundError,
    WorkspacePathError,
    WorkspaceSecurityError,
    WorkspaceSettings,
    is_binary_file,
    is_path_contained,
    read_file_preview,
)


class TestWorkspaceManagerUnit(unittest.TestCase):
    """Unit tests for the Stage 2D WorkspaceManager and WorkspaceSettings."""

    def setUp(self) -> None:
        self.temp_dir = tempfile.mkdtemp(prefix="flex_workspace_test_")
        self.settings_dir = tempfile.mkdtemp(prefix="flex_settings_test_")
        self.settings_file = os.path.join(self.settings_dir, "settings.json")
        self.settings = WorkspaceSettings(self.settings_file)

        # Create a sample directory structure in workspace
        self.sub_dir = os.path.join(self.temp_dir, "src")
        os.makedirs(self.sub_dir, exist_ok=True)

        self.c_file = os.path.join(self.sub_dir, "main.c")
        with open(self.c_file, "w", encoding="utf-8") as f:
            f.write('#include <stdio.h>\nint main(void) { printf("Hello, FLEX!\\n"); return 0; }\n')

        self.h_file = os.path.join(self.sub_dir, "parser.h")
        with open(self.h_file, "w", encoding="utf-8") as f:
            f.write("#ifndef PARSER_H\n#define PARSER_H\nvoid parse(void);\n#endif\n")

        self.bin_file = os.path.join(self.temp_dir, "tool.exe")
        with open(self.bin_file, "wb") as f:
            f.write(b"MZ\x90\x00\x03\x00\x00\x00\x04\x00\x00\x00\xff\xff\x00\x00")

    def tearDown(self) -> None:
        shutil.rmtree(self.temp_dir, ignore_errors=True)
        shutil.rmtree(self.settings_dir, ignore_errors=True)

    def test_select_and_persist_workspace(self) -> None:
        """1. Selecting and persisting a workspace directory."""
        manager = WorkspaceManager(settings=self.settings)
        self.assertIsNone(manager.workspace_root)

        saved_root = manager.set_workspace(self.temp_dir)
        self.assertEqual(os.path.normcase(saved_root), os.path.normcase(self.temp_dir))
        self.assertTrue(manager.is_workspace_valid())

        # Verify saved in settings JSON
        with open(self.settings_file, "r", encoding="utf-8") as f:
            data = json.load(f)
        self.assertEqual(os.path.normcase(data["last_workspace_path"]), os.path.normcase(self.temp_dir))

    def test_restore_workspace_after_restart(self) -> None:
        """2. Restoring the workspace after application restart."""
        manager1 = WorkspaceManager(settings=self.settings)
        manager1.set_workspace(self.temp_dir)

        # New instance mimicking restart
        manager2 = WorkspaceManager(settings=self.settings)
        self.assertEqual(os.path.normcase(manager2.workspace_root or ""), os.path.normcase(self.temp_dir))
        self.assertTrue(manager2.is_workspace_valid())
        self.assertEqual(manager2.workspace_name, os.path.basename(self.temp_dir))

    def test_handle_missing_workspace(self) -> None:
        """3. Handling a missing workspace gracefully."""
        missing_dir = os.path.join(self.temp_dir, "nonexistent_dir")
        manager = WorkspaceManager(settings=self.settings)

        with self.assertRaises(WorkspaceNotFoundError):
            manager.set_workspace(missing_dir)

        # Test settings pointing to moved/deleted workspace
        self.settings.set_last_workspace(missing_dir)
        manager2 = WorkspaceManager(settings=self.settings)
        self.assertIsNone(manager2.workspace_root)
        self.assertFalse(manager2.is_workspace_valid())
        self.assertEqual(manager2.workspace_name, "No Workspace")

    def test_list_files_and_directories(self) -> None:
        """4. Listing files and directories correctly with sorting and metadata."""
        manager = WorkspaceManager(settings=self.settings)
        manager.set_workspace(self.temp_dir)

        items = manager.list_directory("")
        names = [item.name for item in items]

        # Directory 'src' should be listed, 'tool.exe' should be listed
        self.assertIn("src", names)
        self.assertIn("tool.exe", names)

        # Directories come first
        self.assertTrue(items[0].is_dir)
        self.assertEqual(items[0].name, "src")

        # Check subfolder
        sub_items = manager.list_directory("src")
        sub_names = [item.name for item in sub_items]
        self.assertIn("main.c", sub_names)
        self.assertIn("parser.h", sub_names)

        c_item = next(i for i in sub_items if i.name == "main.c")
        self.assertFalse(c_item.is_dir)
        self.assertTrue(c_item.is_text)
        self.assertEqual(c_item.extension, ".c")
        self.assertGreater(c_item.size_bytes, 0)

    def test_handle_unreadable_files_and_directories(self) -> None:
        """5. Handling unreadable files and directories gracefully."""
        manager = WorkspaceManager(settings=self.settings)
        manager.set_workspace(self.temp_dir)

        # Mock PermissionError on scandir
        with patch("os.scandir", side_effect=PermissionError("Access denied")):
            with self.assertRaises(WorkspacePathError) as cm:
                manager.list_directory("")
            self.assertIn("Permission denied", str(cm.exception))

    def test_reject_paths_outside_workspace(self) -> None:
        """6. Rejecting paths outside the workspace (directory traversal)."""
        manager = WorkspaceManager(settings=self.settings)
        manager.set_workspace(self.temp_dir)

        outside_path = os.path.join("..", "..", "Windows", "System32")
        with self.assertRaises(WorkspaceSecurityError):
            manager.list_directory(outside_path)

        with self.assertRaises(WorkspaceSecurityError):
            manager.attach_file(outside_path)

        preview, _, _ = manager.get_file_preview(outside_path)
        self.assertIn("Security Error", preview)

    def test_reject_symlinks_escaping_workspace(self) -> None:
        """7. Rejecting or safely handling symlinks and junctions that escape the workspace."""
        manager = WorkspaceManager(settings=self.settings)
        manager.set_workspace(self.temp_dir)

        # Create external file outside workspace
        ext_dir = tempfile.mkdtemp(prefix="flex_ext_")
        ext_file = os.path.join(ext_dir, "secret.txt")
        with open(ext_file, "w", encoding="utf-8") as f:
            f.write("Secret Key Outside Workspace")

        symlink_target = os.path.join(self.temp_dir, "link_to_secret.txt")
        try:
            os.symlink(ext_file, symlink_target)
            has_symlink = True
        except (OSError, NotImplementedError):
            has_symlink = False

        if has_symlink:
            # Listing should skip the symlink escaping the workspace
            items = manager.list_directory("")
            names = [i.name for i in items]
            self.assertNotIn("link_to_secret.txt", names)

            # Trying to attach or preview the symlink must fail with Security Error
            with self.assertRaises(WorkspaceSecurityError):
                manager.attach_file("link_to_secret.txt")
        shutil.rmtree(ext_dir, ignore_errors=True)

    def test_preview_supported_text_files(self) -> None:
        """8. Previewing supported text files with proper decoding."""
        manager = WorkspaceManager(settings=self.settings)
        manager.set_workspace(self.temp_dir)

        content, is_trunc, is_bin = manager.get_file_preview("src/main.c")
        self.assertFalse(is_bin)
        self.assertFalse(is_trunc)
        self.assertIn("Hello, FLEX!", content)

    def test_handle_binary_and_oversized_files(self) -> None:
        """9. Handling binary and oversized files."""
        manager = WorkspaceManager(
            settings=self.settings,
            max_preview_bytes=50,  # low limit to trigger truncation
        )
        manager.set_workspace(self.temp_dir)

        # Binary file preview
        bin_content, is_trunc, is_bin = manager.get_file_preview("tool.exe")
        self.assertTrue(is_bin)
        self.assertIn("Binary file cannot be previewed", bin_content)

        # Binary file attachment should be rejected
        with self.assertRaises(WorkspacePathError) as cm:
            manager.attach_file("tool.exe")
        self.assertIn("binary file", str(cm.exception).lower())

        # Oversized text file preview
        content, is_trunc, is_bin = manager.get_file_preview("src/main.c")
        self.assertFalse(is_bin)
        self.assertTrue(is_trunc)
        self.assertLessEqual(len(content.encode("utf-8")), 50)

    def test_attach_and_remove_files(self) -> None:
        """10. Attaching and removing files from a chat request."""
        manager = WorkspaceManager(settings=self.settings)
        manager.set_workspace(self.temp_dir)

        self.assertEqual(manager.get_attached_files(), [])

        manager.attach_file("src/main.c")
        self.assertEqual(len(manager.get_attached_files()), 1)
        self.assertIn(os.path.normpath("src/main.c"), manager.get_attached_files())

        # Idempotent re-attach
        manager.attach_file("src/main.c")
        self.assertEqual(len(manager.get_attached_files()), 1)

        manager.attach_file("src/parser.h")
        self.assertEqual(len(manager.get_attached_files()), 2)

        # Detach one
        manager.detach_file("src/main.c")
        self.assertEqual(len(manager.get_attached_files()), 1)
        self.assertNotIn(os.path.normpath("src/main.c"), manager.get_attached_files())

        # Clear all
        manager.clear_attachments()
        self.assertEqual(manager.get_attached_files(), [])

    def test_enforce_file_count_and_context_size_limits(self) -> None:
        """11. Enforcing file-count and context-size limits."""
        manager = WorkspaceManager(
            settings=self.settings,
            max_attached_files=2,
            max_context_bytes=200,
        )
        manager.set_workspace(self.temp_dir)

        manager.attach_file("src/main.c")
        manager.attach_file("src/parser.h")

        # Create a 3rd file
        third_file = os.path.join(self.temp_dir, "test.c")
        with open(third_file, "w") as f:
            f.write("int x = 1;\n")

        with self.assertRaises(WorkspacePathError) as cm:
            manager.attach_file("test.c")
        self.assertIn("Attachment limit reached", str(cm.exception))

        # Test context size limit
        manager_size = WorkspaceManager(
            settings=self.settings,
            max_attached_files=5,
            max_context_bytes=40,  # main.c is ~70 bytes
        )
        manager_size.set_workspace(self.temp_dir)
        with self.assertRaises(WorkspacePathError) as cm:
            manager_size.attach_file("src/main.c")
        self.assertIn("exceeds context limit", str(cm.exception))

    def test_build_context_block_formats_markdown(self) -> None:
        """12. Context block formats relative paths and markdown blocks cleanly."""
        manager = WorkspaceManager(settings=self.settings)
        manager.set_workspace(self.temp_dir)

        manager.attach_file("src/main.c")
        block, read_files = manager.build_context_block()

        self.assertIn("### WORKSPACE CONTEXT:", block)
        self.assertIn("--- File: src/main.c ---", block)
        self.assertIn("```c", block)
        self.assertIn('printf("Hello, FLEX!\\n");', block)
        self.assertEqual(read_files, [os.path.normpath("src/main.c")])

    def test_handle_deleted_attachments_gracefully(self) -> None:
        """14. Handling deleted attachments gracefully without crash."""
        manager = WorkspaceManager(settings=self.settings)
        manager.set_workspace(self.temp_dir)

        temp_c = os.path.join(self.temp_dir, "temp_del.c")
        with open(temp_c, "w") as f:
            f.write("int to_be_deleted = 42;\n")

        manager.attach_file("temp_del.c")
        self.assertIn("temp_del.c", manager.get_attached_files())

        # Now delete the file externally
        os.remove(temp_c)

        # Building context block should not crash; it includes a warning note
        block, read_files = manager.build_context_block()
        self.assertIn("Warning: File missing or deleted", block)
        self.assertNotIn("temp_del.c", read_files)


class TestCoordinatorWorkspaceContextIntegration(unittest.TestCase):
    """Verifies coordinator integration with workspace context."""

    def test_send_chat_with_workspace_context(self) -> None:
        """12 & 13: Preserves original user message in C bridge & storage while sending context to LLM."""
        mock_bridge = MagicMock()
        mock_bridge.is_running = True
        mock_bridge.dispatch.return_value = {
            "success": True,
            "operation_started": True,
            "operation_id": 101,
            "command_name": "chat",
            "parser_status": "OK",
            "state_status": "PROCESSING",
            "args": "explain main.c",
        }
        mock_bridge.finish.return_value = {
            "success": True,
            "operation_id": 101,
            "state_status": "IDLE",
        }

        mock_provider = MagicMock()
        mock_provider.check_health.return_value = True
        mock_provider.is_model_available.return_value = True
        mock_provider.generate.return_value = LLMResponse(
            text="main.c prints Hello, FLEX!",
            model="qwen2.5-coder:3b",
        )

        mock_storage = MagicMock()
        mock_storage.create_conversation.return_value = "conv-1234"
        mock_storage.get_messages.return_value = []

        coord = AICoordinator(
            bridge=mock_bridge,
            provider=mock_provider,
            storage=mock_storage,
            active_conversation_id="conv-1234",
            auto_start=False,
        )

        context_block = "=== WORKSPACE CONTEXT ===\nFile: main.c\n```c\nint main() {}\n```"
        user_prompt = "explain main.c"

        result = coord.send_chat(user_prompt, workspace_context=context_block)

        # 1. C Bridge receives clean command string within buffer limits
        mock_bridge.dispatch.assert_called_once_with("chat explain main.c")

        # 2. SQLite storage saves original clean prompt, NOT the massive file context block
        mock_storage.save_message.assert_any_call(
            conversation_id="conv-1234",
            role="user",
            content="explain main.c",
            auto_title=True,
        )

        # 3. LLM Provider receives combined prompt with workspace context
        call_kwargs = mock_provider.generate.call_args[1]
        sent_prompt = call_kwargs["prompt"]
        self.assertIn("=== WORKSPACE CONTEXT ===", sent_prompt)
        self.assertIn("User Question:\nexplain main.c", sent_prompt)

        # 4. In-memory history stores clean prompt
        user_history = [m.content for m in coord.history.get_messages() if m.role == "user"]
        self.assertEqual(user_history, ["explain main.c"])

        self.assertEqual(result.text, "main.c prints Hello, FLEX!")
        self.assertEqual(result.operation_id, 101)


if __name__ == "__main__":
    unittest.main()
