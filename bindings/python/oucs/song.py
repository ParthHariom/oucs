"""OucsSong — represents a single song inside a .oucs file."""
import ctypes
from typing import Optional, TYPE_CHECKING
from .meta import OucsSongMeta
from .stream import OucsStream

if TYPE_CHECKING:
    from .file import OucsFile


class OucsSong:
    """
    Represents a single song entry inside a .oucs container.

    Obtained via ``OucsFile.songs()`` or ``OucsFile.song(index)``.
    Does NOT load audio data until explicitly requested.
    """

    def __init__(self, parent: "OucsFile", index: int, meta: OucsSongMeta):
        self._parent = parent
        self._index  = index
        self.meta    = meta

    @property
    def index(self) -> int:
        return self._index

    @property
    def name(self) -> str:
        return self.meta.name

    @property
    def uuid(self) -> str:
        return self.meta.uuid

    @property
    def bpm(self) -> float:
        return self.meta.bpm

    @property
    def key(self) -> str:
        return self.meta.key_name

    @property
    def mood(self) -> str:
        return self.meta.mood_str

    @property
    def duration_ms(self) -> int:
        return self.meta.duration_ms

    @property
    def encrypted(self) -> bool:
        return self.meta.encrypted

    def stream(self, chunk_size: int = 4096,
               password: Optional[str] = None) -> OucsStream:
        """
        Open a chunk-by-chunk stream for this song.

        Only the current chunk is in memory at any time — device load is
        near zero regardless of song file size.

        Args:
            chunk_size: Bytes per chunk (default 4096).
            password:   Required if song is encrypted.

        Returns:
            OucsStream — iterable, seekable, context-manager compatible.
        """
        return self._parent._open_stream(self._index, chunk_size, password)

    def extract(self, output_path: str,
                password: Optional[str] = None) -> None:
        """Extract this song to a file."""
        self._parent.extract(self._index, output_path, password)

    def extract_bytes(self, password: Optional[str] = None) -> bytes:
        """Extract this song to memory. Returns raw audio bytes."""
        return self._parent.extract_bytes(self._index, password)

    def __repr__(self) -> str:
        return (f"OucsSong(index={self._index}, name={self.meta.name!r}, "
                f"format={self.meta.audio_format}, bpm={self.meta.bpm:.1f})")

    def __str__(self) -> str:
        return str(self.meta)
