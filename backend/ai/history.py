"""FLEX 1.0 — In-Memory Conversation History.

Provides a bounded, in-memory conversation history buffer for multi-turn chat sessions.

Architectural Notice:
    This class maintains an ephemeral sequence of ChatMessage objects in process memory.
    It does NOT provide persistent storage, database backends (SQLite/PostgreSQL),
    vector embeddings, user profile management, or cross-session memory.
    Persistent memory capabilities are strictly reserved for future Stage 1G.
"""

from __future__ import annotations

from typing import List, Optional

from .base import ChatMessage
from .exceptions import AIValidationError

DEFAULT_MAX_MESSAGES: int = 50


class InMemoryConversationHistory:
    """A bounded FIFO buffer for in-memory chat messages.

    Attributes:
        max_messages: Maximum number of messages preserved before oldest messages are evicted.
    """

    def __init__(self, max_messages: int = DEFAULT_MAX_MESSAGES) -> None:
        """Initializes the conversation history buffer.

        Args:
            max_messages: Positive integer specifying the capacity limit. Default is 50.

        Raises:
            AIValidationError: If max_messages is not an integer >= 1.
        """
        if not isinstance(max_messages, int) or isinstance(max_messages, bool) or max_messages < 1:
            raise AIValidationError(
                f"max_messages must be an integer greater than or equal to 1, got: {max_messages!r}"
            )
        self._max_messages: int = max_messages
        self._messages: List[ChatMessage] = []

    @property
    def max_messages(self) -> int:
        """Returns the configured maximum message limit."""
        return self._max_messages

    def add_message(self, message: ChatMessage) -> None:
        """Appends a validated ChatMessage, evicting the oldest message if capacity is exceeded.

        Args:
            message: The ChatMessage instance to add.

        Raises:
            AIValidationError: If message is not an instance of ChatMessage.
        """
        if not isinstance(message, ChatMessage):
            raise AIValidationError(
                f"Expected ChatMessage instance, got {type(message).__name__}"
            )
        self._messages.append(message)
        while len(self._messages) > self._max_messages:
            self._messages.pop(0)

    def add_user_message(self, content: str) -> ChatMessage:
        """Helper to create and add a user message.

        Args:
            content: The text content of the user message.

        Returns:
            ChatMessage: The newly created and stored message.
        """
        msg = ChatMessage(role="user", content=content)
        self.add_message(msg)
        return msg

    def add_assistant_message(self, content: str) -> ChatMessage:
        """Helper to create and add an assistant message.

        Args:
            content: The text content of the assistant message.

        Returns:
            ChatMessage: The newly created and stored message.
        """
        msg = ChatMessage(role="assistant", content=content)
        self.add_message(msg)
        return msg

    def add_system_message(self, content: str) -> ChatMessage:
        """Helper to create and add a system message.

        Args:
            content: The text instructions for the system.

        Returns:
            ChatMessage: The newly created and stored message.
        """
        msg = ChatMessage(role="system", content=content)
        self.add_message(msg)
        return msg

    def get_messages(self) -> List[ChatMessage]:
        """Returns a defensive shallow copy of all messages currently in memory.

        Callers cannot mutate the internal buffer through the returned list.
        """
        return list(self._messages)

    def clear(self) -> None:
        """Clears all messages from memory."""
        self._messages.clear()

    def __len__(self) -> int:
        """Returns the current number of messages in memory."""
        return len(self._messages)

    def __repr__(self) -> str:
        return (
            f"InMemoryConversationHistory(count={len(self._messages)}, max={self._max_messages})"
        )
