"""OucsStream — chunk-by-chunk audio streaming."""
import ctypes
from typing import Iterator, Optional
from ._lib import _lib
from .exceptions import raise_for_code


class OucsStream:
    """
    Low-memory chunk-by-chunk stream of a single song from a .oucs file.

    Only one chunk is ever in memory at a time.

    Usage::

        stream = file.stream(song_index=0, chunk_size=4096)
        for chunk in stream:
            audio_player.feed(chunk)

        # Or use as context manager:
        with file.stream(0) as stream:
            for chunk in stream:
                process(chunk)
    """

    def __init__(self, reader_ptr, song_idx: int,
                 chunk_size: int = 4096, password: Optional[str] = None):
        self._reader_ptr = reader_ptr
        self._song_idx   = song_idx
        self._chunk_size = chunk_size
        self._password   = password.encode() if password else None
        self._ptr        = None
        self._buf        = (ctypes.c_uint8 * chunk_size)()
        self._open()

    def _open(self):
        self._ptr = _lib.oucs_stream_open(
            self._reader_ptr,
            ctypes.c_uint32(self._song_idx),
            ctypes.c_size_t(self._chunk_size),
            self._password
        )
        if not self._ptr:
            raise RuntimeError(f"Cannot open stream for song index {self._song_idx}")

    def read_chunk(self) -> bytes:
        """Read the next chunk. Returns b'' at end of stream."""
        if not self._ptr:
            return b""
        bytes_read = ctypes.c_size_t(0)
        ret = _lib.oucs_stream_read_chunk(
            self._ptr,
            self._buf,
            ctypes.c_size_t(self._chunk_size),
            ctypes.byref(bytes_read)
        )
        raise_for_code(ret, "stream_read_chunk")
        n = bytes_read.value
        if n == 0:
            return b""
        return bytes(self._buf[:n])

    def seek(self, byte_offset: int) -> None:
        """Seek to a byte position within the song."""
        ret = _lib.oucs_stream_seek(self._ptr, ctypes.c_uint64(byte_offset))
        raise_for_code(ret, "stream_seek")

    def seek_chapter(self, chapter_index: int) -> None:
        """Seek to a chapter by index."""
        ret = _lib.oucs_stream_seek_chapter(self._ptr, ctypes.c_uint32(chapter_index))
        raise_for_code(ret, "stream_seek_chapter")

    @property
    def position(self) -> int:
        """Current byte position within the song."""
        if not self._ptr:
            return 0
        return int(_lib.oucs_stream_tell(self._ptr))

    def close(self) -> None:
        """Close stream and free resources."""
        if self._ptr:
            _lib.oucs_stream_free(self._ptr)
            self._ptr = None

    def __iter__(self) -> Iterator[bytes]:
        """Iterate over chunks until end of stream."""
        while True:
            chunk = self.read_chunk()
            if not chunk:
                break
            yield chunk

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def __del__(self):
        self.close()

    def read_all(self) -> bytes:
        """Read the entire song into memory. Use with care for large files."""
        parts = []
        for chunk in self:
            parts.append(chunk)
        return b"".join(parts)
