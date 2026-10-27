"""FLEX 1.0 — Workspace and Project Files Manager (Stage 2D).

Provides local-first, safe workspace filesystem binding and file context attachment
for the FLEX desktop AI assistant.

Guarantees & Constraints:
    - Zero third-party dependencies; strictly standard-library compliant.
    - Security: Strict path containment verification preventing directory traversal
      (e.g. `..` escapes, junctions, and symlinks escaping the workspace root).
    - Privacy: No remote uploads, no cloud syncing; all processing occurs locally.
    - Safety: Files are opened strictly in read-only mode. Never executes files or commands.
    - Robustness: Handles missing, moved, inaccessible, binary, or oversized files gracefully.
    - UI Protection: Lazy-loading directory structures and capped preview sizes prevent Tkinter freezing.
    - Context Limits: Strict bounds on attached file count (max 5) and context size (max 64 KB).
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
import json
import logging
import os
from pathlib import Path
import threading
from typing import Any, Dict, List, Optional, Set, Tuple

logger = logging.getLogger(__name__)

# Default limits
DEFAULT_MAX_ATTACHED_FILES: int = 5
DEFAULT_MAX_CONTEXT_BYTES: int = 64 * 1024       # 64 KB total context for chat attachment
DEFAULT_MAX_PREVIEW_BYTES: int = 256 * 1024      # 256 KB preview limit in GUI
DEFAULT_BINARY_CHECK_BYTES: int = 1024           # 1 KB sample for null byte check

# Common binary file extensions to reject immediately from text preview
KNOWN_BINARY_EXTENSIONS: Set[str] = {
    ".exe", ".dll", ".so", ".dylib", ".bin", ".obj", ".o", ".a", ".lib",
    ".pyc", ".pyo", ".pyd", ".class", ".jar",
    ".db", ".sqlite", ".sqlite3",
    ".zip", ".tar", ".gz", ".bz2", ".7z", ".rar",
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".webp",
    ".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx",
    ".mp3", ".wav", ".ogg", ".mp4", ".avi", ".mkv", ".mov",
    ".iso", ".img", ".dmg",
}

# Directories to ignore during workspace listing for performance & noise reduction
IGNORED_DIRECTORIES: Set[str] = {
    ".git", ".svn", ".hg", "__pycache__", ".pytest_cache",
    ".vscode", ".idea", "node_modules", "build", "dist",
    ".venv", "venv", "env",
}


class WorkspaceError(Exception):
    """Base exception for workspace filesystem operations."""
    pass


class WorkspaceSecurityError(WorkspaceError):
    """Raised when an operation attempts to access paths outside the workspace root."""
    pass


class WorkspaceNotFoundError(WorkspaceError):
    """Raised when the specified workspace directory does not exist or is not a directory."""
    pass


class WorkspacePathError(WorkspaceError):
    """Raised when a file path within the workspace is invalid or inaccessible."""
    pass


def get_default_settings_path() -> str:
    """Resolves the persistent settings.json location outside the repository.

    Precedence:
        1. FLEX_SETTINGS_PATH environment variable (if set).
        2. %LOCALAPPDATA%/FLEX/settings.json (on Windows).
        3. ~/.flex/settings.json (fallback).
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


def is_path_contained(base_dir: str, target_path: str) -> bool:
    """Verifies that target_path resolves strictly within base_dir.

    Resolves symbolic links, Windows junctions, and relative parent operators ('..')
    to ensure zero path escape.

    Args:
        base_dir: Canonical root directory.
        target_path: Path to validate.

    Returns:
        bool: True if target_path is within base_dir, False otherwise.
    """
    try:
        real_base = os.path.normcase(os.path.realpath(os.path.abspath(base_dir)))
        real_target = os.path.normcase(os.path.realpath(os.path.abspath(target_path)))

        if real_base == real_target:
            return True

        # Common path must be exactly real_base
        common = os.path.commonpath([real_base, real_target])
        return os.path.normcase(common) == real_base
    except (ValueError, OSError):
        return False


def is_binary_file(file_path: str, check_bytes: int = DEFAULT_BINARY_CHECK_BYTES) -> bool:
    """Determines if a file is binary using extension heuristics and null-byte sampling.

    Args:
        file_path: Absolute or relative path to the file.
        check_bytes: Number of bytes to read for null-byte detection.

    Returns:
        bool: True if file is binary, False if text.
    """
    ext = os.path.splitext(file_path)[1].lower()
    if ext in KNOWN_BINARY_EXTENSIONS:
        return True

    try:
        with open(file_path, "rb") as f:
            chunk = f.read(check_bytes)
            if b"\x00" in chunk:
                return True
            # Check for excessive unprintable non-whitespace characters
            text_characters = (
                b"".join(bytes([i]) for i in range(32, 127)) +
                b"\n\r\t\b\f"
            )
            if chunk:
                nontext = chunk.translate(None, text_characters)
                if len(nontext) / len(chunk) > 0.30:
                    return True
        return False
    except (OSError, PermissionError):
        return False


def read_file_preview(
    file_path: str,
    max_bytes: int = DEFAULT_MAX_PREVIEW_BYTES,
) -> Tuple[str, bool, bool]:
    """Safely reads up to max_bytes from a file for UI preview.

    Args:
        file_path: Path to the target file.
        max_bytes: Maximum number of bytes to read.

    Returns:
        Tuple[str, bool, bool]: (content, is_truncated, is_binary)
    """
    if is_binary_file(file_path):
        return ("[Binary file cannot be previewed]", False, True)

    try:
        file_size = os.path.getsize(file_path)
        is_truncated = file_size > max_bytes

        with open(file_path, "rb") as f:
            raw_bytes = f.read(max_bytes)

        # Attempt UTF-8 decoding with fallback
        try:
            content = raw_bytes.decode("utf-8")
        except UnicodeDecodeError:
            content = raw_bytes.decode("latin-1", errors="replace")

        return (content, is_truncated, False)
    except PermissionError:
        return ("[Permission Denied: Unable to read file]", False, False)
    except FileNotFoundError:
        return ("[File Not Found: File may have been moved or deleted]", False, False)
    except Exception as exc:
        return (f"[Error reading file: {exc}]", False, False)


@dataclass(frozen=True)
class WorkspaceFileInfo:
    """Immutable representation of a file or directory within a workspace."""

    name: str
    rel_path: str
    abs_path: str
    is_dir: bool
    size_bytes: int
    extension: str
    modified_at: str
    is_text: bool

    def to_dict(self) -> Dict[str, Any]:
        return {
            "name": self.name,
            "rel_path": self.rel_path,
            "abs_path": self.abs_path,
            "is_dir": self.is_dir,
            "size_bytes": self.size_bytes,
            "extension": self.extension,
            "modified_at": self.modified_at,
            "is_text": self.is_text,
        }


class WorkspaceSettings:
    """Thread-safe persistent settings storage for workspace configuration."""

    def __init__(self, settings_path: Optional[str] = None) -> None:
        self.settings_path: str = settings_path or get_default_settings_path()
        self._lock = threading.RLock()

    def get_last_workspace(self) -> Optional[str]:
        """Loads the last selected workspace path from settings."""
        with self._lock:
            data = self._read_settings()
            path = data.get("last_workspace_path")
            if isinstance(path, str) and path.strip():
                return path.strip()
            return None

    def set_last_workspace(self, workspace_path: Optional[str]) -> None:
        """Saves the last selected workspace path to settings."""
        with self._lock:
            data = self._read_settings()
            if workspace_path is None:
                data.pop("last_workspace_path", None)
            else:
                data["last_workspace_path"] = str(workspace_path).strip()
            self._write_settings(data)

    def _read_settings(self) -> Dict[str, Any]:
        if not os.path.exists(self.settings_path):
            return {}
        try:
            with open(self.settings_path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception as exc:
            logger.warning("Failed to load settings from %s: %s", self.settings_path, exc)
            return {}

    def _write_settings(self, data: Dict[str, Any]) -> None:
        try:
            settings_dir = os.path.dirname(self.settings_path)
            if settings_dir:
                os.makedirs(settings_dir, exist_ok=True)
            temp_path = f"{self.settings_path}.tmp"
            with open(temp_path, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2)
            os.replace(temp_path, self.settings_path)
        except Exception as exc:
            logger.error("Failed to write settings to %s: %s", self.settings_path, exc)


class WorkspaceManager:
    """Manages the active workspace directory, file navigation, preview, and chat context attachments."""

    def __init__(
        self,
        workspace_root: Optional[str] = None,
        settings: Optional[WorkspaceSettings] = None,
        settings_path: Optional[str] = None,
        settings_file: Optional[str] = None,
        max_attached_files: int = DEFAULT_MAX_ATTACHED_FILES,
        max_context_bytes: int = DEFAULT_MAX_CONTEXT_BYTES,
        max_preview_bytes: int = DEFAULT_MAX_PREVIEW_BYTES,
    ) -> None:
        effective_settings_path = settings_path or settings_file
        if settings is not None:
            self.settings = settings
        elif effective_settings_path is not None:
            self.settings = WorkspaceSettings(settings_path=str(effective_settings_path))
        else:
            self.settings = WorkspaceSettings()
        self.max_attached_files: int = max_attached_files
        self.max_context_bytes: int = max_context_bytes
        self.max_preview_bytes: int = max_preview_bytes

        self._lock = threading.RLock()
        self._workspace_root: Optional[str] = None
        self._attached_rel_paths: List[str] = []

        # Initialize workspace: explicit argument > stored setting > None
        initial_path = workspace_root or self.settings.get_last_workspace()
        if initial_path:
            try:
                self.set_workspace(initial_path)
            except Exception as exc:
                logger.warning("Could not restore workspace from %s: %s", initial_path, exc)
                self._workspace_root = None

    @property
    def workspace_root(self) -> Optional[str]:
        """Returns the absolute canonical path of the active workspace, or None."""
        with self._lock:
            return self._workspace_root

    @property
    def workspace_name(self) -> str:
        """Returns the basename of the active workspace, or 'No Workspace'."""
        with self._lock:
            if self._workspace_root:
                return os.path.basename(self._workspace_root) or self._workspace_root
            return "No Workspace"

    def is_workspace_valid(self) -> bool:
        """Returns True if the current workspace directory exists and is accessible."""
        with self._lock:
            if not self._workspace_root:
                return False
            try:
                return os.path.isdir(self._workspace_root)
            except OSError:
                return False

    def set_workspace(self, path: str) -> str:
        """Sets and validates the active workspace directory.

        Args:
            path: Target directory path.

        Returns:
            str: Canonicalized workspace root path.

        Raises:
            WorkspaceNotFoundError: If path does not exist or is not a directory.
            WorkspaceSecurityError: If path cannot be safely resolved.
        """
        if not path or not isinstance(path, str) or not path.strip():
            raise WorkspaceNotFoundError("Workspace path cannot be empty.")

        clean_path = os.path.abspath(path.strip())
        if not os.path.exists(clean_path):
            raise WorkspaceNotFoundError(f"Workspace directory does not exist: {clean_path}")
        if not os.path.isdir(clean_path):
            raise WorkspaceNotFoundError(f"Workspace path is not a directory: {clean_path}")

        real_path = os.path.realpath(clean_path)

        with self._lock:
            self._workspace_root = real_path
            self._attached_rel_paths.clear()
            self.settings.set_last_workspace(real_path)

        return real_path

    def clear_workspace(self) -> None:
        """Clears the active workspace and removes saved setting."""
        with self._lock:
            self._workspace_root = None
            self._attached_rel_paths.clear()
            self.settings.set_last_workspace(None)

    def list_directory(self, rel_path: str = "") -> List[WorkspaceFileInfo]:
        """Lists files and folders within a subdirectory of the workspace.

        Guarantees:
            - Validates path containment (rejects directory traversal).
            - Excludes ignored noisy folders (.git, __pycache__, etc.).
            - Does not follow symlinks pointing outside the workspace.
            - Sorts directories first, then files alphabetically.

        Args:
            rel_path: Subdirectory path relative to the workspace root.

        Returns:
            List[WorkspaceFileInfo]: Sorted child entries.

        Raises:
            WorkspaceNotFoundError: If workspace is not set or target directory does not exist.
            WorkspaceSecurityError: If rel_path escapes the workspace root.
        """
        with self._lock:
            if not self._workspace_root or not os.path.isdir(self._workspace_root):
                raise WorkspaceNotFoundError("No active workspace configured.")

            target_dir = os.path.join(self._workspace_root, rel_path)
            if not is_path_contained(self._workspace_root, target_dir):
                raise WorkspaceSecurityError(
                    f"Path escape detected: {rel_path} is outside workspace root."
                )

            if not os.path.exists(target_dir):
                raise WorkspaceNotFoundError(f"Directory not found: {rel_path}")
            if not os.path.isdir(target_dir):
                raise WorkspacePathError(f"Target path is not a directory: {rel_path}")

            results: List[WorkspaceFileInfo] = []
            try:
                with os.scandir(target_dir) as entries:
                    for entry in entries:
                        # Skip ignored directories
                        if entry.name in IGNORED_DIRECTORIES:
                            continue

                        # Check symlink destination safety
                        if entry.is_symlink():
                            try:
                                resolved = os.path.realpath(entry.path)
                                if not is_path_contained(self._workspace_root, resolved):
                                    logger.warning(
                                        "Skipping symlink escaping workspace: %s -> %s",
                                        entry.path,
                                        resolved,
                                    )
                                    continue
                            except OSError:
                                continue

                        try:
                            stat_res = entry.stat(follow_symlinks=True)
                            size_bytes = stat_res.st_size
                            mtime = datetime.fromtimestamp(
                                stat_res.st_mtime, timezone.utc
                            ).strftime("%Y-%m-%d %H:%M")
                        except (OSError, PermissionError):
                            size_bytes = 0
                            mtime = "Unknown"

                        entry_rel = os.path.relpath(entry.path, self._workspace_root)
                        is_dir = entry.is_dir(follow_symlinks=True)
                        ext = os.path.splitext(entry.name)[1].lower() if not is_dir else ""
                        is_txt = not is_dir and not is_binary_file(entry.path)

                        results.append(
                            WorkspaceFileInfo(
                                name=entry.name,
                                rel_path=entry_rel,
                                abs_path=os.path.abspath(entry.path),
                                is_dir=is_dir,
                                size_bytes=size_bytes,
                                extension=ext,
                                modified_at=mtime,
                                is_text=is_txt,
                            )
                        )
            except PermissionError as perm_err:
                raise WorkspacePathError(f"Permission denied accessing directory: {rel_path}") from perm_err
            except Exception as exc:
                raise WorkspacePathError(f"Failed to list directory {rel_path}: {exc}") from exc

            # Sort: directories first, then alphabetical by name
            results.sort(key=lambda x: (not x.is_dir, x.name.lower()))
            return results

    def get_file_preview(self, rel_path: str) -> Tuple[str, bool, bool]:
        """Loads a safe preview of a file within the workspace.

        Args:
            rel_path: File path relative to workspace root.

        Returns:
            Tuple[str, bool, bool]: (content, is_truncated, is_binary)
        """
        with self._lock:
            if not self._workspace_root:
                return ("[No active workspace]", False, False)

            target_file = os.path.join(self._workspace_root, rel_path)
            if not is_path_contained(self._workspace_root, target_file):
                return ("[Security Error: Path escapes workspace root]", False, False)

            return read_file_preview(target_file, max_bytes=self.max_preview_bytes)

    # ── Context Attachment Operations for Chat ────────────────────────────────

    def attach_file(self, rel_path: str) -> None:
        """Attaches a workspace file to be sent as context with the next chat message.

        Args:
            rel_path: File path relative to the workspace root.

        Raises:
            WorkspaceNotFoundError: If workspace is not set.
            WorkspaceSecurityError: If path escapes the workspace root.
            WorkspacePathError: If file is missing, a directory, binary, or limits exceeded.
        """
        with self._lock:
            if not self._workspace_root:
                raise WorkspaceNotFoundError("No workspace active.")

            target_file = os.path.join(self._workspace_root, rel_path)
            if not is_path_contained(self._workspace_root, target_file):
                raise WorkspaceSecurityError("Cannot attach file outside workspace.")

            if not os.path.exists(target_file):
                raise WorkspacePathError(f"File not found: {rel_path}")
            if os.path.isdir(target_file):
                raise WorkspacePathError("Cannot attach a directory. Select a file.")

            if is_binary_file(target_file):
                raise WorkspacePathError("Cannot attach binary file as text context.")

            norm_rel = os.path.normpath(rel_path)
            if norm_rel in self._attached_rel_paths:
                return  # Idempotent

            if len(self._attached_rel_paths) >= self.max_attached_files:
                raise WorkspacePathError(
                    f"Attachment limit reached (maximum {self.max_attached_files} files)."
                )

            # Check individual and cumulative context size
            file_size = os.path.getsize(target_file)
            current_total = self._calculate_current_attachment_bytes()
            if (current_total + file_size) > self.max_context_bytes:
                max_kb = self.max_context_bytes // 1024
                raise WorkspacePathError(
                    f"Attaching '{os.path.basename(rel_path)}' exceeds context limit of {max_kb} KB."
                )

            self._attached_rel_paths.append(norm_rel)

    def detach_file(self, rel_path: str) -> None:
        """Removes an attached file from the pending chat context list."""
        with self._lock:
            norm_rel = os.path.normpath(rel_path)
            if norm_rel in self._attached_rel_paths:
                self._attached_rel_paths.remove(norm_rel)

    def clear_attachments(self) -> None:
        """Clears all attached files."""
        with self._lock:
            self._attached_rel_paths.clear()

    def get_attached_files(self) -> List[str]:
        """Returns a copy of the currently attached relative file paths."""
        with self._lock:
            return list(self._attached_rel_paths)

    @property
    def attached_files(self) -> List[str]:
        """Returns a copy of currently attached relative file paths."""
        return self.get_attached_files()

    def _calculate_current_attachment_bytes(self) -> int:
        if not self._workspace_root:
            return 0
        total = 0
        for rel in self._attached_rel_paths:
            p = os.path.join(self._workspace_root, rel)
            try:
                if os.path.isfile(p):
                    total += os.path.getsize(p)
            except OSError:
                pass
        return total

    def build_context_block(self) -> Tuple[str, List[str]]:
        """Assembles the structured context block from attached workspace files for the LLM.

        Returns:
            Tuple[str, List[str]]:
                - Markdown-formatted context block string.
                - List of successfully read relative file paths.
        """
        with self._lock:
            if not self._workspace_root or not self._attached_rel_paths:
                return ("", [])

            lines: List[str] = [
                f"### WORKSPACE CONTEXT: {self.workspace_name}",
                "The user has attached the following project file(s) for reference:\n",
            ]
            read_files: List[str] = []

            for rel in self._attached_rel_paths:
                abs_p = os.path.join(self._workspace_root, rel)
                norm_rel = rel.replace("\\", "/")

                if not is_path_contained(self._workspace_root, abs_p):
                    lines.append(f"--- File: {norm_rel} [Error: Path escapes workspace] ---\n")
                    continue

                if not os.path.exists(abs_p):
                    lines.append(f"--- File: {norm_rel} [Warning: File missing or deleted] ---\n")
                    continue

                content, is_trunc, is_bin = read_file_preview(
                    abs_p, max_bytes=self.max_context_bytes
                )
                if is_bin:
                    lines.append(f"--- File: {norm_rel} [Notice: Binary file omitted] ---\n")
                    continue

                ext = os.path.splitext(rel)[1].lstrip(".").lower()
                lang_tag = ext if ext in {"c", "h", "py", "json", "md", "txt", "sh", "ps1", "sql"} else ""

                trunc_note = " (truncated)" if is_trunc else ""
                lines.append(f"--- File: {norm_rel}{trunc_note} ---")
                lines.append(f"```{lang_tag}")
                lines.append(content)
                lines.append("```\n")
                read_files.append(rel)

            lines.append("=========================================\n")
            return ("\n".join(lines), read_files)
