"""OUCS exception hierarchy."""


class OucsError(Exception):
    """Base exception for all OUCS errors."""
    def __init__(self, message: str, code: int = 0):
        super().__init__(message)
        self.code = code


class OucsNotFoundError(OucsError):
    """Raised when a song or file is not found."""


class OucsCorruptError(OucsError):
    """Raised when file corruption is detected and unrecoverable."""


class OucsCryptoError(OucsError):
    """Raised on encryption/decryption failure (wrong password etc.)."""


class OucsIOError(OucsError):
    """Raised on I/O failure."""


class OucsMemoryError(OucsError):
    """Raised when memory allocation fails in the C core."""


# Map C error codes → Python exceptions
_CODE_MAP = {
    -1:  OucsError,
    -2:  OucsIOError,
    -3:  OucsCorruptError,   # invalid magic
    -4:  OucsError,          # version
    -5:  OucsCorruptError,
    -6:  OucsNotFoundError,
    -7:  OucsMemoryError,
    -8:  OucsCryptoError,
    -9:  OucsCryptoError,    # wrong password
    -10: OucsCorruptError,   # ECC fail
    -11: OucsError,          # invalid arg
    -12: OucsError,          # overflow
    -13: OucsError,          # already exists
    -14: OucsError,          # network
    -15: OucsError,          # unsupported
}


def raise_for_code(code: int, context: str = "") -> None:
    """Raise the appropriate exception for a C error code."""
    if code == 0:
        return
    exc_class = _CODE_MAP.get(code, OucsError)
    msg = f"{context}: error code {code}" if context else f"error code {code}"
    raise exc_class(msg, code=code)
