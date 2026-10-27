"""FLEX 1.0 — Unit Tests for SQLite Conversation Storage (Stage 2C Phase 1).

Validates SQLiteConversationStorage against:
    - Schema creation, indexing, and versioning
    - Conversation CRUD, listing, auto-titling, and manual renaming
    - Message insertion, sequence assignment, and chronological ordering
    - Unicode, multiline, code blocks, and emoji content
    - Foreign-key constraints and cascade deletion
    - Duplicate message prevention / idempotency
    - Thread safety and safe connection lifecycle
    - Corruption quarantine and in-memory degradation
    - Restart persistence using real temporary directories
"""

from __future__ import annotations

import os
import shutil
import sqlite3
import tempfile
import threading
import unittest

from backend.ai.storage import (
    CURRENT_SCHEMA_VERSION,
    DEFAULT_CONVERSATION_TITLE,
    ConversationNotFoundError,
    ConversationRecord,
    MessageRecord,
    SQLiteConversationStorage,
    StorageError,
    StorageInitError,
    StorageReadError,
    StorageWriteError,
    get_default_database_path,
)


class TestSQLiteStorageUnit(unittest.TestCase):
    """Unit tests for SQLiteConversationStorage using isolated temporary files and :memory:."""

    def setUp(self) -> None:
        self.temp_dir = tempfile.mkdtemp()
        self.db_path = os.path.join(self.temp_dir, "test_conversations.db")
        self.storage = SQLiteConversationStorage(db_path=self.db_path)

    def tearDown(self) -> None:
        if hasattr(self, "storage") and self.storage is not None:
            self.storage.close()
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_schema_creation_and_version(self) -> None:
        """Verifies tables and schema_version table are initialized."""
        self.assertFalse(self.storage.is_degraded)
        self.assertIsNone(self.storage.storage_error)

        with self.storage._lock:
            cur = self.storage._conn.execute("SELECT version FROM schema_version;")
            row = cur.fetchone()
            self.assertIsNotNone(row)
            self.assertEqual(row["version"], CURRENT_SCHEMA_VERSION)

    def test_create_and_retrieve_conversation(self) -> None:
        """Verifies creating a conversation record and retrieving it."""
        conv_id = self.storage.create_conversation("Project Planning")
        self.assertTrue(bool(conv_id))

        conv = self.storage.get_conversation(conv_id)
        self.assertIsNotNone(conv)
        self.assertIsInstance(conv, ConversationRecord)
        self.assertEqual(conv.id, conv_id)
        self.assertEqual(conv.title, "Project Planning")
        self.assertTrue("T" in conv.created_at)
        self.assertEqual(conv.created_at, conv.updated_at)

    def test_create_conversation_with_explicit_id(self) -> None:
        """Verifies creating a conversation with a caller-supplied UUID."""
        explicit_id = "test-custom-uuid-12345"
        returned_id = self.storage.create_conversation("Custom ID Conv", conversation_id=explicit_id)
        self.assertEqual(returned_id, explicit_id)

        conv = self.storage.get_conversation(explicit_id)
        self.assertIsNotNone(conv)
        self.assertEqual(conv.id, explicit_id)

    def test_list_conversations_ordering(self) -> None:
        """Verifies list_conversations returns conversations ordered by updated_at DESC."""
        id1 = self.storage.create_conversation("Chat 1")
        id2 = self.storage.create_conversation("Chat 2")
        id3 = self.storage.create_conversation("Chat 3")

        # Save a message to id1 to bump its updated_at
        self.storage.save_message(id1, role="user", content="Hello in chat 1")

        recent = self.storage.list_conversations(limit=10)
        self.assertEqual(len(recent), 3)
        # id1 should be first because it was updated most recently
        self.assertEqual(recent[0].id, id1)

    def test_save_and_retrieve_messages_chronological(self) -> None:
        """Verifies message insertion, sequence assignment, and chronological retrieval."""
        cid = self.storage.create_conversation("C11 Pointers")

        m1_id = self.storage.save_message(cid, role="user", content="What is a pointer in C?")
        m2_id = self.storage.save_message(
            cid,
            role="assistant",
            content="A pointer stores a memory address.",
            model="qwen2.5-coder:3b",
        )
        m3_id = self.storage.save_message(cid, role="user", content="How do I dereference it?")

        messages = self.storage.get_messages(cid)
        self.assertEqual(len(messages), 3)

        self.assertEqual(messages[0].id, m1_id)
        self.assertEqual(messages[0].sequence_num, 0)
        self.assertEqual(messages[0].role, "user")
        self.assertEqual(messages[0].content, "What is a pointer in C?")

        self.assertEqual(messages[1].id, m2_id)
        self.assertEqual(messages[1].sequence_num, 1)
        self.assertEqual(messages[1].role, "assistant")
        self.assertEqual(messages[1].model, "qwen2.5-coder:3b")

        self.assertEqual(messages[2].id, m3_id)
        self.assertEqual(messages[2].sequence_num, 2)
        self.assertEqual(messages[2].role, "user")

    def test_auto_title_from_first_user_prompt(self) -> None:
        """Verifies default conversation title is automatically updated from first user prompt."""
        cid = self.storage.create_conversation()  # Uses DEFAULT_CONVERSATION_TITLE = "New Chat"
        conv = self.storage.get_conversation(cid)
        self.assertEqual(conv.title, DEFAULT_CONVERSATION_TITLE)

        prompt = "Explain memory management in modern C11"
        self.storage.save_message(cid, role="user", content=prompt, auto_title=True)

        updated_conv = self.storage.get_conversation(cid)
        expected_title = prompt[:35] + "..."
        self.assertEqual(updated_conv.title, expected_title)

    def test_auto_title_does_not_override_custom_title(self) -> None:
        """Verifies custom title provided at creation is preserved."""
        cid = self.storage.create_conversation("My Custom Project")
        self.storage.save_message(cid, role="user", content="Hello there", auto_title=True)

        conv = self.storage.get_conversation(cid)
        self.assertEqual(conv.title, "My Custom Project")

    def test_manual_update_conversation_title(self) -> None:
        """Verifies explicit updating of conversation title."""
        cid = self.storage.create_conversation("Initial Title")
        self.storage.update_conversation_title(cid, "Renamed Title")

        conv = self.storage.get_conversation(cid)
        self.assertEqual(conv.title, "Renamed Title")

    def test_update_title_nonexistent_raises_error(self) -> None:
        """Verifies updating title for a non-existent conversation raises ConversationNotFoundError."""
        with self.assertRaises(ConversationNotFoundError):
            self.storage.update_conversation_title("non-existent-cid-999", "New Title")

    def test_delete_conversation_cascades_to_messages(self) -> None:
        """Verifies deleting conversation deletes its messages via foreign-key cascade."""
        cid = self.storage.create_conversation("To Delete")
        self.storage.save_message(cid, role="user", content="Message 1")
        self.storage.save_message(cid, role="assistant", content="Message 2")

        self.assertEqual(self.storage.get_message_count(cid), 2)

        deleted = self.storage.delete_conversation(cid)
        self.assertTrue(deleted)
        self.assertIsNone(self.storage.get_conversation(cid))
        self.assertEqual(len(self.storage.get_messages(cid)), 0)

        # Deleting non-existent conversation returns False
        self.assertFalse(self.storage.delete_conversation("unknown-cid"))

    def test_unicode_multiline_and_code_blocks(self) -> None:
        """Verifies support for UTF-8 Unicode emojis, multiline strings, and code snippets."""
        cid = self.storage.create_conversation("Unicode & C Code")

        code_content = (
            '#include <stdio.h>\n'
            'int main(void) {\n'
            '    printf("Hello, World! 🚀\\n");\n'
            '    return 0;\n'
            '}'
        )
        self.storage.save_message(cid, role="user", content=code_content)

        japanese_content = "こんにちは、FLEX 1.0！ 🌌 宇宙の深淵"
        self.storage.save_message(cid, role="assistant", content=japanese_content)

        messages = self.storage.get_messages(cid)
        self.assertEqual(len(messages), 2)
        self.assertEqual(messages[0].content, code_content)
        self.assertEqual(messages[1].content, japanese_content)

    def test_save_message_invalid_role_raises_value_error(self) -> None:
        """Verifies saving a message with an invalid role raises ValueError."""
        cid = self.storage.create_conversation("Test Roles")
        with self.assertRaises(ValueError):
            self.storage.save_message(cid, role="moderator", content="Invalid")

    def test_save_message_nonexistent_conversation_raises_error(self) -> None:
        """Verifies saving to a non-existent conversation raises ConversationNotFoundError."""
        with self.assertRaises(ConversationNotFoundError):
            self.storage.save_message("invalid-cid-404", role="user", content="Hello")

    def test_duplicate_message_prevention_idempotency(self) -> None:
        """Verifies saving with an existing message_id does not duplicate or create extra rows."""
        cid = self.storage.create_conversation("Idempotency")
        stable_msg_id = "msg-uuid-stable-1234"

        id1 = self.storage.save_message(
            cid, role="user", content="First attempt", message_id=stable_msg_id
        )
        self.assertEqual(id1, stable_msg_id)

        # Retry with same message_id
        id2 = self.storage.save_message(
            cid, role="user", content="Second attempt", message_id=stable_msg_id
        )
        self.assertEqual(id2, stable_msg_id)

        messages = self.storage.get_messages(cid)
        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0].content, "First attempt")

    def test_restart_persistence_from_file(self) -> None:
        """Verifies data survives closing storage and reopening from file."""
        cid = self.storage.create_conversation("Persistent Conv")
        self.storage.save_message(cid, role="user", content="Save this across restart")
        self.storage.save_message(
            cid, role="assistant", content="Confirmed preserved", model="qwen2.5-coder:3b"
        )

        # Close existing storage connection
        self.storage.close()

        # Reopen new storage instance pointing to same file
        reopened = SQLiteConversationStorage(db_path=self.db_path)
        try:
            conv = reopened.get_conversation(cid)
            self.assertIsNotNone(conv)
            self.assertEqual(conv.title, "Persistent Conv")

            messages = reopened.get_messages(cid)
            self.assertEqual(len(messages), 2)
            self.assertEqual(messages[0].content, "Save this across restart")
            self.assertEqual(messages[1].content, "Confirmed preserved")
            self.assertEqual(messages[1].model, "qwen2.5-coder:3b")
        finally:
            reopened.close()

    def test_get_messages_with_limit(self) -> None:
        """Verifies limit parameter returns the most recent messages in chronological order."""
        cid = self.storage.create_conversation("Limited Messages")
        for i in range(10):
            self.storage.save_message(cid, role="user", content=f"Message {i}")

        all_msgs = self.storage.get_messages(cid)
        self.assertEqual(len(all_msgs), 10)

        # Request last 3 messages
        last_3 = self.storage.get_messages(cid, limit=3)
        self.assertEqual(len(last_3), 3)
        self.assertEqual(last_3[0].content, "Message 7")
        self.assertEqual(last_3[1].content, "Message 8")
        self.assertEqual(last_3[2].content, "Message 9")

    def test_corruption_quarantine_and_in_memory_fallback(self) -> None:
        """Verifies that a corrupt database file is quarantined and fallback to in-memory mode occurs."""
        corrupt_file = os.path.join(self.temp_dir, "corrupt.db")
        # Write invalid garbage bytes to simulate file corruption
        with open(corrupt_file, "wb") as f:
            f.write(b"NOT A SQLITE DATABASE FILE - CORRUPT DATA 1234567890")

        storage = SQLiteConversationStorage(db_path=corrupt_file)
        try:
            self.assertTrue(storage.is_degraded)
            self.assertIsNotNone(storage.storage_error)
            self.assertEqual(storage.db_path, ":memory:")

            # Verify original file was NOT deleted
            self.assertTrue(os.path.exists(corrupt_file))

            # Verify quarantined file was created
            quarantine_files = [f for f in os.listdir(self.temp_dir) if "corrupt" in f and f != "corrupt.db"]
            self.assertTrue(len(quarantine_files) >= 1)

            # In-memory storage is still fully functional
            cid = storage.create_conversation("Degraded Session")
            storage.save_message(cid, role="user", content="Temporary message")
            msgs = storage.get_messages(cid)
            self.assertEqual(len(msgs), 1)
        finally:
            storage.close()

    def test_thread_safety_concurrent_writes(self) -> None:
        """Verifies concurrent writes from multiple threads do not cause database locked errors."""
        cid = self.storage.create_conversation("Multi-thread Test")
        errors: list = []

        def worker(idx: int) -> None:
            try:
                for i in range(15):
                    self.storage.save_message(
                        cid, role="user", content=f"Worker {idx} msg {i}"
                    )
            except Exception as e:
                errors.append(e)

        threads = [threading.Thread(target=worker, args=(t,)) for t in range(4)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        self.assertEqual(len(errors), 0, f"Thread errors encountered: {errors}")
        msgs = self.storage.get_messages(cid)
        self.assertEqual(len(msgs), 60)
        # Sequence numbers must be strictly 0 to 59
        seqs = [m.sequence_num for m in msgs]
        self.assertEqual(seqs, list(range(60)))

    def test_default_database_path_resolution(self) -> None:
        """Verifies default database path resolution."""
        # Unset env override if any
        orig_env = os.environ.get("FLEX_DB_PATH")
        try:
            if "FLEX_DB_PATH" in os.environ:
                del os.environ["FLEX_DB_PATH"]
            resolved = get_default_database_path()
            self.assertTrue(resolved.endswith("conversations.db"))
            # Must NOT be inside the repository directory
            repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
            self.assertFalse(resolved.startswith(repo_root))
        finally:
            if orig_env is not None:
                os.environ["FLEX_DB_PATH"] = orig_env

    def test_unsupported_future_schema_version_falls_back(self) -> None:
        """Verifies that a database with a future unsupported schema version falls back to memory mode."""
        future_db = os.path.join(self.temp_dir, "future_schema.db")
        conn = sqlite3.connect(future_db)
        conn.execute("CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at TEXT);")
        conn.execute("INSERT INTO schema_version (version, applied_at) VALUES (999, '2099-01-01T00:00:00Z');")
        conn.commit()
        conn.close()

        storage = SQLiteConversationStorage(db_path=future_db)
        try:
            self.assertTrue(storage.is_degraded)
            self.assertIsNotNone(storage.storage_error)
            self.assertIn("newer than supported version", storage.storage_error or "")
            self.assertEqual(storage.db_path, ":memory:")
            # Functional in memory
            cid = storage.create_conversation("Safe In Memory")
            self.assertTrue(bool(cid))
        finally:
            storage.close()

    def test_corruption_quarantines_wal_and_shm_sidecars(self) -> None:
        """Verifies that WAL and SHM sidecars are quarantined alongside the corrupt database file."""
        corrupt_db = os.path.join(self.temp_dir, "corrupt_with_wal.db")
        wal_file = f"{corrupt_db}-wal"
        shm_file = f"{corrupt_db}-shm"

        with open(corrupt_db, "wb") as f:
            f.write(b"CORRUPT DB FILE HEADER")
        with open(wal_file, "wb") as f:
            f.write(b"WAL FILE DATA")
        with open(shm_file, "wb") as f:
            f.write(b"SHM FILE DATA")

        storage = SQLiteConversationStorage(db_path=corrupt_db)
        try:
            self.assertTrue(storage.is_degraded)
            self.assertEqual(storage.db_path, ":memory:")

            # Check that quarantine file and its sidecars exist
            quarantines = [
                f for f in os.listdir(self.temp_dir)
                if "corrupt_with_wal.db.corrupt" in f
            ]
            self.assertTrue(len(quarantines) >= 1)
            quarantine_wal = [f for f in quarantines if f.endswith("-wal")]
            quarantine_shm = [f for f in quarantines if f.endswith("-shm")]
            self.assertEqual(len(quarantine_wal), 1)
            self.assertEqual(len(quarantine_shm), 1)
        finally:
            storage.close()

    def test_busy_timeout_and_wal_pragma_configured(self) -> None:
        """Verifies that WAL mode and busy timeout are set on file-backed database."""
        with self.storage._lock:
            cur = self.storage._conn.execute("PRAGMA journal_mode;")
            mode = cur.fetchone()[0]
            self.assertEqual(mode.lower(), "wal")

            cur = self.storage._conn.execute("PRAGMA busy_timeout;")
            timeout = cur.fetchone()[0]
            self.assertEqual(timeout, 5000)


if __name__ == "__main__":
    unittest.main()
