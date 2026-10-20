"""FLEX 1.0 Backend Package."""

from .core_bridge import (
    BRIDGE_MAX_LINE_BYTES,
    DEFAULT_TIMEOUT,
    FLEX_MAX_COMMAND_LEN,
    FLEX_MAX_INPUT_BYTES,
    FlexBridgeClient,
    FlexBridgeError,
    FlexBridgeNotFoundError,
    FlexBridgeProcessError,
    FlexBridgeProtocolError,
    FlexBridgeTimeoutError,
    resolve_bridge_path,
    utf8_byte_length,
)

__all__ = [
    "BRIDGE_MAX_LINE_BYTES",
    "DEFAULT_TIMEOUT",
    "FLEX_MAX_COMMAND_LEN",
    "FLEX_MAX_INPUT_BYTES",
    "FlexBridgeClient",
    "FlexBridgeError",
    "FlexBridgeNotFoundError",
    "FlexBridgeProcessError",
    "FlexBridgeProtocolError",
    "FlexBridgeTimeoutError",
    "resolve_bridge_path",
    "utf8_byte_length",
]
