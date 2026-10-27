"""FLEX 1.0 — Persistent SQLite Conversation Storage (Stage 2C).

Provides durable, local-first conversation and message persistence using Python's
standard-library `sqlite3`.

Design Guarantees:
    - Zero third-party dependencies; strictly standard-library compliant.
    - Local-first privacy: Default database stored in %LOCALAPPDATA%/FLEX (outside Git).
    - Foreign-key constraints and atomic transactions for referential integrity.
    - Write-Ahead Logging (WAL) and bounded busy timeouts for safe multi-thread concurrency.
    - Deterministic message ordering via monotonic sequence numbers.
    - Safe corruption quarantine: corrupt files are preserved, falling back to in-memory mode.
    - Idempotent message saving preventing duplicate writes during retries.
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
import logging
import os
import shutil
import sqlite3
import sys
import threading
from typing import Any, Dict, List, Optional
import uuid

logger = logging.getLogger(__name__)

# Permitted message roles in the storage model (matching backend.ai.base)
VALID_STORAGE_ROLES = frozenset({"system", "user", "assistant"})

CURRENT_SCHEMA_VERSION = 1
DEFAULT_BUSY_TIMEOUT_MS = 5000
DEFAULT_CONVERSATION_TITLE = "New Chat"


class StorageError(Exception):
    """Base exception for persistent storage errors."""
    pass


class StorageInitError(StorageError):
    """Raised when database initialization or schema migration fails."""
    pass


class StorageWriteError(StorageError):
    """Raised when a database write operation fails."""
    pass


class StorageReadError(StorageError):
    """Raised when a database read operation fails."""
    pass


class ConversationNotFoundError(StorageError):
    """Raised when an operation targets a non-existent conversation ID."""
    pass


def get_default_database_path() -> str:
    """Resolves the default persistent database location.

    Resolution order:
        1. FLEX_DB_PATH environment variable (if set).
        2. %LOCALAPPDATA%/FLEX/conversations.db (on Windows).
        3. ~/.flex/conversations.db (fallback when LOCALAPPDATA is absent).

    Returns:
        str: Absolute or designated path to the SQLite database file.
    """
    env_override = os.environ.get("FLEX_DB_PATH")
    if env_override and env_override.strip():
        return os.path.abspath(env_override.strip())

    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data and local_app_data.strip():
        base_dir = os.path.join(local_app_data.strip(), "FLEX")
    else:
        base_dir = os.path.expanduser(os.path.join("~", ".flex"))

    return os.path.abspath(os.path.join(base_dir, "conversations.db"))


def utc_now_iso() -> str:
    """Returns current UTC timestamp in ISO-8601 format."""
    return datetime.now(timezone.utc).isoformat()


@dataclass(frozen=True)
class ConversationRecord:
    """Immutable representation of a stored conversation."""

    id: str
    title: str
    created_at: str
    updated_at: str

    def to_dict(self) -> Dict[str, Any]:
        return {
            "id": self.id,
            "title": self.title,
            "created_at": self.created_at,
            "updated_at": self.updated_at,
        }


@dataclass(frozen=True)
class MessageRecord:
    """Immutable representation of a stored conversation message."""

    id: str
    conversation_id: str
    role: str
    content: str
    model: Optional[str]
    sequence_num: int
    created_at: str

    def to_dict(self) -> Dict[str, Any]:
        return {
            "id": self.id,
            "conversation_id": self.conversation_id,
            "role": self.role,
            "content": self.content,
            "model": self.model,
            "sequence_num": self.sequence_num,
            "created_at": self.created_at,
        }


class SQLiteConversationStorage:
    """Thread-safe persistent SQLite storage for conversations and messages.

    Attributes:
        db_path: Path to SQLite database file or ':memory:'.
        is_degraded: True if operating in in-memory fallback mode due to file corruption/error.
        storage_error: Diagnostic error message if initialization fell back to in-memory mode.
    """

    def __init__(self, db_path: Optional[str] = None) -> None:
        """Initializes conversation storage, creating tables and migrations.

        Args:
            db_path: Optional explicit database path. If None, resolves default location.
        """
        self._raw_path = db_path or get_default_database_path()
        self.is_degraded: bool = False
        self.storage_error: Optional[str] = None
        self._lock = threading.RLock()
        self._conn: Optional[sqlite3.Connection] = None

        self._init_storage(self._raw_path)

    @property
    def db_path(self) -> str:
        """Returns the actual database path currently in use."""
        return self._current_path

    def _init_storage(self, target_path: str) -> None:
        """Establishes connection and runs schema migrations with safe recovery."""
        self._current_path = target_path

        if target_path != ":memory:":
            try:
                db_dir = os.path.dirname(target_path)
                if db_dir:
                    os.makedirs(db_dir, exist_ok=True)
            except Exception as exc:
                logger.error("Failed to create storage directory for %s: %s", target_path, exc)
                self._fallback_to_memory(f"Directory creation failed: {exc}")
                return

        try:
            self._conn = self._create_connection(self._current_path)
            self._apply_migrations(self._conn)
        except sqlite3.DatabaseError as db_err:
            logger.error("Database initialization error for %s: %s", target_path, db_err)
            # Safe corruption quarantine: preserve original corrupt file and switch to in-memory
            self._quarantine_and_fallback(target_path, db_err)
        except Exception as exc:
            logger.error("Unexpected storage initialization error: %s", exc)
            self._fallback_to_memory(f"Unexpected error: {exc}")

    def _create_connection(self, path: str) -> sqlite3.Connection:
        """Creates and configures a SQLite connection with WAL and foreign keys."""
        conn = sqlite3.connect(
            path,
            timeout=DEFAULT_BUSY_TIMEOUT_MS / 1000.0,
            check_same_thread=False,
            isolation_level=None,  # Autocommit mode; transactions managed explicitly via BEGIN/COMMIT
        )
        conn.row_factory = sqlite3.Row

        # Enforce foreign key constraints
        conn.execute("PRAGMA foreign_keys = ON;")

        # Enable Write-Ahead Logging for file databases
        if path != ":memory:":
            try:
                conn.execute("PRAGMA journal_mode = WAL;")
                conn.execute(f"PRAGMA busy_timeout = {DEFAULT_BUSY_TIMEOUT_MS};")
                conn.execute("PRAGMA synchronous = NORMAL;")
            except sqlite3.Error as pragma_err:
                logger.warning("Could not set WAL pragma: %s", pragma_err)

        return conn

    def _quarantine_and_fallback(self, corrupt_path: str, error: Exception) -> None:
        """Preserves a corrupt database by copying it aside and activates in-memory fallback."""
        if self._conn is not None:
            try:
                self._conn.close()
            except Exception:
                pass
            self._conn = None

        quarantine_path: Optional[str] = None
        if corrupt_path != ":memory:" and os.path.exists(corrupt_path):
            timestamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
            quarantine_path = f"{corrupt_path}.corrupt.{timestamp}"
            try:
                shutil.copy2(corrupt_path, quarantine_path)
                logger.warning(
                    "Corrupt database preserved to quarantine: %s", quarantine_path
                )
            except Exception as copy_err:
                logger.error("Could not quarantine corrupt database: %s", copy_err)

        msg = f"Database corrupted ({error}). Preserved original. Operating in temporary in-memory mode."
        self._fallback_to_memory(msg)

    def _fallback_to_memory(self, reason: str) -> None:
        """Activates safe in-memory fallback mode."""
        self.is_degraded = True
        self.storage_error = reason
        self._current_path = ":memory:"
        try:
            self._conn = self._create_connection(":memory:")
            self._apply_migrations(self._conn)
            logger.info("Storage operating in degraded in-memory mode: %s", reason)
        except Exception as exc:
            logger.critical("Fatal: In-memory fallback initialization failed: %s", exc)
            raise StorageInitError(f"Fatal storage failure: {exc}") from exc

    def _apply_migrations(self, conn: sqlite3.Connection) -> None:
        """Applies schema creation and migrations up to CURRENT_SCHEMA_VERSION."""
        with self._lock:
            conn.execute("BEGIN IMMEDIATE;")
            try:
                conn.execute(
                    """
                    CREATE TABLE IF NOT EXISTS schema_version (
                        version INTEGER PRIMARY KEY,
                        applied_at TEXT NOT NULL
                    );
                    """
                )

                cur = conn.execute("SELECT MAX(version) FROM schema_version;")
                row = cur.fetchone()
                current_ver = row[0] if (row and row[0] is not None) else 0

                if current_ver < 1:
                    # Version 1 schema: conversations & messages
                    conn.execute(
                        """
                        CREATE TABLE IF NOT EXISTS conversations (
                            id TEXT PRIMARY KEY,
                            title TEXT NOT NULL,
                            created_at TEXT NOT NULL,
                            updated_at TEXT NOT NULL
                        );
                        """
                    )
                    conn.execute(
                        """
                        CREATE TABLE IF NOT EXISTS messages (
                            id TEXT PRIMARY KEY,
                            conversation_id TEXT NOT NULL,
                            role TEXT NOT NULL CHECK(role IN ('system', 'user', 'assistant')),
                            content TEXT NOT NULL,
                            model TEXT,
                            sequence_num INTEGER NOT NULL,
                            created_at TEXT NOT NULL,
                            FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
                        );
                        """
                    )
                    conn.execute(
                        """
                        CREATE INDEX IF NOT EXISTS idx_messages_conversation_seq 
                            ON messages(conversation_id, sequence_num ASC);
                        """
                    )
                    conn.execute(
                        """
                        CREATE INDEX IF NOT EXISTS idx_conversations_updated 
                            ON conversations(updated_at DESC);
                        """
                    )
                    conn.execute(
                        "INSERT INTO schema_version (version, applied_at) VALUES (1, ?);",
                        (utc_now_iso(),),
                    )

                conn.execute("COMMIT;")
            except Exception as exc:
                conn.execute("ROLLBACK;")
                raise StorageInitError(f"Migration failed: {exc}") from exc

    def create_conversation(
        self,
        title: str = DEFAULT_CONVERSATION_TITLE,
        conversation_id: Optional[str] = None,
    ) -> str:
        """Creates a new conversation record.

        Args:
            title: Initial display title for conversation.
            conversation_id: Optional caller-provided UUID. If None, generates UUIDv4.

        Returns:
            str: The conversation ID.

        Raises:
            StorageWriteError: If database write fails.
        """
        cid = conversation_id or str(uuid.uuid4())
        now = utc_now_iso()
        clean_title = title.strip() if title and title.strip() else DEFAULT_CONVERSATION_TITLE

        with self._lock:
            if self._conn is None:
                raise StorageWriteError("Storage connection is not open.")
            try:
                self._conn.execute("BEGIN IMMEDIATE;")
                self._conn.execute(
                    """
                    INSERT INTO conversations (id, title, created_at, updated_at)
                    VALUES (?, ?, ?, ?);
                    """,
                    (cid, clean_title, now, now),
                )
                self._conn.execute("COMMIT;")
                return cid
            except Exception as exc:
                try:
                    self._conn.execute("ROLLBACK;")
                except Exception:
                    pass
                raise StorageWriteError(f"Failed to create conversation: {exc}") from exc

    def list_conversations(self, limit: int = 30) -> List[ConversationRecord]:
        """Lists recent conversations ordered by updated_at DESC.

        Args:
            limit: Maximum number of conversation records to return.

        Returns:
            List[ConversationRecord]: Chronologically recent conversations.

        Raises:
            StorageReadError: If database query fails.
        """
        with self._lock:
            if self._conn is None:
                raise StorageReadError("Storage connection is not open.")
            try:
                cur = self._conn.execute(
                    """
                    SELECT id, title, created_at, updated_at
                    FROM conversations
                    ORDER BY updated_at DESC
                    LIMIT ?;
                    """,
                    (max(1, limit),),
                )
                rows = cur.fetchall()
                return [
                    ConversationRecord(
                        id=row["id"],
                        title=row["title"],
                        created_at=row["created_at"],
                        updated_at=row["updated_at"],
                    )
                    for row in rows
                ]
            except Exception as exc:
                raise StorageReadError(f"Failed to list conversations: {exc}") from exc

    def get_conversation(self, conversation_id: str) -> Optional[ConversationRecord]:
        """Retrieves metadata for a specific conversation by ID.

        Args:
            conversation_id: Conversation identifier.

        Returns:
            Optional[ConversationRecord]: Record if found, None otherwise.

        Raises:
            StorageReadError: If database query fails.
        """
        if not conversation_id or not isinstance(conversation_id, str):
            return None

        with self._lock:
            if self._conn is None:
                raise StorageReadError("Storage connection is not open.")
            try:
                cur = self._conn.execute(
                    """
                    SELECT id, title, created_at, updated_at
                    FROM conversations
                    WHERE id = ?;
                    """,
                    (conversation_id,),
                )
                row = cur.fetchone()
                if not row:
                    return None
                return ConversationRecord(
                    id=row["id"],
                    title=row["title"],
                    created_at=row["created_at"],
                    updated_at=row["updated_at"],
                )
            except Exception as exc:
                raise StorageReadError(f"Failed to get conversation: {exc}") from exc

    def update_conversation_title(self, conversation_id: str, title: str) -> None:
        """Updates the title of an existing conversation.

        Args:
            conversation_id: Target conversation ID.
            title: New display title.

        Raises:
            ConversationNotFoundError: If conversation does not exist.
            StorageWriteError: If database write fails.
        """
        clean_title = title.strip() if title and title.strip() else DEFAULT_CONVERSATION_TITLE
        now = utc_now_iso()

        with self._lock:
            if self._conn is None:
                raise StorageWriteError("Storage connection is not open.")
            try:
                self._conn.execute("BEGIN IMMEDIATE;")
                cur = self._conn.execute(
                    "UPDATE conversations SET title = ?, updated_at = ? WHERE id = ?;",
                    (clean_title, now, conversation_id),
                )
                if cur.rowcount == 0:
                    self._conn.execute("ROLLBACK;")
                    raise ConversationNotFoundError(f"Conversation {conversation_id} not found")
                self._conn.execute("COMMIT;")
            except ConversationNotFoundError:
                raise
            except Exception as exc:
                try:
                    self._conn.execute("ROLLBACK;")
                except Exception:
                    pass
                raise StorageWriteError(f"Failed to update conversation title: {exc}") from exc

    def delete_conversation(self, conversation_id: str) -> bool:
        """Deletes a conversation and its messages via cascade.

        Args:
            conversation_id: Target conversation ID.

        Returns:
            bool: True if a conversation was deleted, False if ID was not found.

        Raises:
            StorageWriteError: If database operation fails.
        """
        with self._lock:
            if self._conn is None:
                raise StorageWriteError("Storage connection is not open.")
            try:
                self._conn.execute("BEGIN IMMEDIATE;")
                cur = self._conn.execute(
                    "DELETE FROM conversations WHERE id = ?;",
                    (conversation_id,),
                )
                deleted = cur.rowcount > 0
                self._conn.execute("COMMIT;")
                return deleted
            except Exception as exc:
                try:
                    self._conn.execute("ROLLBACK;")
                except Exception:
                    pass
                raise StorageWriteError(f"Failed to delete conversation: {exc}") from exc

    def save_message(
        self,
        conversation_id: str,
        role: str,
        content: str,
        model: Optional[str] = None,
        message_id: Optional[str] = None,
        auto_title: bool = True,
    ) -> str:
        """Persists a chat message atomically within a conversation.

        Guarantees:
            - Validates role strictly against VALID_STORAGE_ROLES.
            - Assigns next monotonic sequence_num atomically.
            - Updates conversation updated_at timestamp.
            - If message_id is provided and already exists, returns existing ID (idempotent).
            - If conversation title is default and role is 'user', auto-titles from prompt.

        Args:
            conversation_id: Target conversation ID.
            role: Participant role ('system', 'user', or 'assistant').
            content: Message text body.
            model: Optional model identifier string.
            message_id: Optional caller-specified stable message UUID.
            auto_title: If True, updates conversation title from first user prompt.

        Returns:
            str: Message ID.

        Raises:
            ValueError: If role is invalid or content is not a string.
            ConversationNotFoundError: If conversation_id does not exist.
            StorageWriteError: If database write fails.
        """
        if not isinstance(role, str):
            raise ValueError(f"Role must be a string, got {type(role).__name__}")
        norm_role = role.strip().lower()
        if norm_role not in VALID_STORAGE_ROLES:
            raise ValueError(
                f"Invalid role '{role}'. Permitted roles are: {sorted(VALID_STORAGE_ROLES)}"
            )

        if not isinstance(content, str):
            raise ValueError(f"Content must be a string, got {type(content).__name__}")

        now = utc_now_iso()
        mid = message_id or str(uuid.uuid4())

        with self._lock:
            if self._conn is None:
                raise StorageWriteError("Storage connection is not open.")

            try:
                self._conn.execute("BEGIN IMMEDIATE;")

                # 1. Verify conversation exists and fetch current title
                cur = self._conn.execute(
                    "SELECT title FROM conversations WHERE id = ?;", (conversation_id,)
                )
                conv_row = cur.fetchone()
                if not conv_row:
                    self._conn.execute("ROLLBACK;")
                    raise ConversationNotFoundError(
                        f"Cannot save message: conversation {conversation_id} not found"
                    )
                current_title = conv_row["title"]

                # 2. Duplicate prevention check
                if message_id:
                    cur_dup = self._conn.execute(
                        "SELECT id FROM messages WHERE id = ?;", (mid,)
                    )
                    if cur_dup.fetchone():
                        # Already exists: idempotent return without duplicate sequence increment
                        self._conn.execute("COMMIT;")
                        return mid

                # 3. Determine next sequence number atomically
                cur_seq = self._conn.execute(
                    "SELECT COALESCE(MAX(sequence_num) + 1, 0) AS next_seq FROM messages WHERE conversation_id = ?;",
                    (conversation_id,),
                )
                next_seq = cur_seq.fetchone()["next_seq"]

                # 4. Insert message
                self._conn.execute(
                    """
                    INSERT INTO messages (id, conversation_id, role, content, model, sequence_num, created_at)
                    VALUES (?, ?, ?, ?, ?, ?, ?);
                    """,
                    (mid, conversation_id, norm_role, content, model, next_seq, now),
                )

                # 5. Update conversation updated_at and optionally auto-title
                new_title = current_title
                if auto_title and norm_role == "user" and (current_title == DEFAULT_CONVERSATION_TITLE or not current_title):
                    clean_excerpt = content.strip().replace("\n", " ").replace("\r", " ")
                    if clean_excerpt:
                        new_title = clean_excerpt[:35] + ("..." if len(clean_excerpt) > 35 else "")

                self._conn.execute(
                    "UPDATE conversations SET title = ?, updated_at = ? WHERE id = ?;",
                    (new_title, now, conversation_id),
                )

                self._conn.execute("COMMIT;")
                return mid
            except (ValueError, ConversationNotFoundError):
                raise
            except Exception as exc:
                try:
                    self._conn.execute("ROLLBACK;")
                except Exception:
                    pass
                raise StorageWriteError(f"Failed to save message: {exc}") from exc

    def get_messages(
        self,
        conversation_id: str,
        limit: Optional[int] = None,
    ) -> List[MessageRecord]:
        """Retrieves messages for a conversation ordered chronologically (sequence_num ASC).

        If limit is specified, returns the most recent `limit` messages in chronological order.

        Args:
            conversation_id: Target conversation ID.
            limit: Optional maximum number of recent messages to return.

        Returns:
            List[MessageRecord]: Messages in chronological order.

        Raises:
            StorageReadError: If database read fails.
        """
        with self._lock:
            if self._conn is None:
                raise StorageReadError("Storage connection is not open.")

            try:
                if limit is not None and limit > 0:
                    # Select most recent 'limit' messages, ordered ascending
                    sql = """
                        SELECT id, conversation_id, role, content, model, sequence_num, created_at
                        FROM (
                            SELECT id, conversation_id, role, content, model, sequence_num, created_at
                            FROM messages
                            WHERE conversation_id = ?
                            ORDER BY sequence_num DESC
                            LIMIT ?
                        )
                        ORDER BY sequence_num ASC;
                    """
                    params: tuple = (conversation_id, limit)
                else:
                    sql = """
                        SELECT id, conversation_id, role, content, model, sequence_num, created_at
                        FROM messages
                        WHERE conversation_id = ?
                        ORDER BY sequence_num ASC;
                    """
                    params = (conversation_id,)

                cur = self._conn.execute(sql, params)
                rows = cur.fetchall()
                return [
                    MessageRecord(
                        id=row["id"],
                        conversation_id=row["conversation_id"],
                        role=row["role"],
                        content=row["content"],
                        model=row["model"],
                        sequence_num=row["sequence_num"],
                        created_at=row["created_at"],
                    )
                    for row in rows
                ]
            except Exception as exc:
                raise StorageReadError(f"Failed to get messages: {exc}") from exc

    def get_message_count(self, conversation_id: str) -> int:
        """Returns total message count for a conversation."""
        with self._lock:
            if self._conn is None:
                raise StorageReadError("Storage connection is not open.")
            try:
                cur = self._conn.execute(
                    "SELECT COUNT(*) AS cnt FROM messages WHERE conversation_id = ?;",
                    (conversation_id,),
                )
                return cur.fetchone()["cnt"]
            except Exception as exc:
                raise StorageReadError(f"Failed to count messages: {exc}") from exc

    def close(self) -> None:
        """Closes the underlying database connection cleanly."""
        with self._lock:
            if self._conn is not None:
                try:
                    self._conn.close()
                except Exception as exc:
                    logger.warning("Error closing database connection: %s", exc)
                finally:
                    self._conn = None
