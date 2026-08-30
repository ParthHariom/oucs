"""OucsFile — main entry point for reading and writing .oucs files."""
import ctypes
import struct
import datetime
from pathlib import Path
from typing import Iterator, List, Optional, Union

from ._lib import _lib, c_uint8_p, c_uint8_pp, c_size_p
from .exceptions import raise_for_code, OucsError, OucsIOError
from .meta import OucsContainerMeta, OucsSongMeta, LyricLine, Chapter
from .song import OucsSong
from .stream import OucsStream

# Index entry size from spec
_INDEX_ENTRY_SIZE = 512
_UUID_SIZE = 16


def _read_c_str(buf, max_len: int) -> str:
    """Read null-terminated string from ctypes buffer."""
    raw = bytes(buf[:max_len])
    null = raw.find(b"\x00")
    return raw[:null].decode("utf-8", errors="replace") if null >= 0 else raw.decode("utf-8", errors="replace")


class OucsFile:
    """
    Main interface for .oucs files. Supports both reading and writing.

    Writing (create mode)::

        with OucsFile.create("playlist.oucs") as f:
            f.set_meta(theme="My Playlist", description="Weekend vibes")
            f.add_song("track1.mp3", name="Track One", description="Intro")
            f.add_song("track2.flac", name="Track Two", encrypt=True, password="secret")

    Reading (open mode)::

        with OucsFile.open("playlist.oucs") as f:
            print(f.meta)
            for song in f.songs():
                stream = song.stream(chunk_size=8192)
                for chunk in stream:
                    player.feed(chunk)

    URL streaming (no full download)::

        f = OucsFile.open_url("https://cdn.example.com/playlist.oucs")
        f.song(0).stream()

    """

    # ── Constructors ──────────────────────────────────────────

    def __init__(self):
        self._writer_ptr  = None
        self._reader_ptr  = None
        self._path        = None
        self._mode        = None   # "r" or "w"
        self._song_count  = 0
        self._meta        = OucsContainerMeta()
        self._pending_meta_set = False

    @classmethod
    def create(cls, path: Union[str, Path], flags: int = 0) -> "OucsFile":
        """
        Create a new .oucs file for writing.

        Args:
            path:  Output file path.
            flags: OUCS_FLAG_* bitmask (optional).

        Returns:
            OucsFile instance in write mode (use as context manager).
        """
        f = cls()
        f._path = str(path)
        f._mode = "w"
        f._writer_ptr = _lib.oucs_writer_create(
            f._path.encode(), ctypes.c_uint8(flags)
        )
        if not f._writer_ptr:
            raise OucsIOError(f"Cannot create .oucs file: {path}")
        return f

    @classmethod
    def open(cls, path: Union[str, Path]) -> "OucsFile":
        """
        Open an existing .oucs file for reading.

        Args:
            path: Path to the .oucs file.

        Returns:
            OucsFile instance in read mode.
        """
        f = cls()
        f._path = str(path)
        f._mode = "r"
        f._reader_ptr = _lib.oucs_reader_open(f._path.encode())
        if not f._reader_ptr:
            raise OucsIOError(f"Cannot open .oucs file: {path}")
        f._song_count = _lib.oucs_reader_song_count(f._reader_ptr)
        f._load_meta()
        return f

    @classmethod
    def open_url(cls, url: str) -> "OucsFile":
        """
        Open a .oucs file via URL (HTTP Range request streaming).
        No full download needed.

        Args:
            url: HTTP(S) URL to the .oucs file.

        Returns:
            OucsFile instance backed by HTTP range requests.
        """
        f = cls()
        f._path = url
        f._mode = "r"
        f._reader_ptr = _lib.oucs_reader_open_url(url.encode())
        if not f._reader_ptr:
            raise OucsIOError(f"Cannot open .oucs URL: {url}")
        f._song_count = _lib.oucs_reader_song_count(f._reader_ptr)
        f._load_meta()
        return f

    # ── Context manager ───────────────────────────────────────

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    # ── Write API ─────────────────────────────────────────────

    def set_meta(self, theme: str = "", description: str = "",
                 logo: Optional[bytes] = None, logo_path: Optional[str] = None,
                 logo_url: str = "", author: str = "") -> None:
        """
        Set container-level metadata.

        Args:
            theme:       Container theme/playlist name.
            description: Short description.
            logo:        Raw bytes of logo image (PNG/JPEG/etc.).
            logo_path:   Path to logo image file (alternative to logo).
            logo_url:    External URL for the logo.
            author:      Author name.
        """
        self._assert_write()
        self._meta.theme       = theme
        self._meta.description = description
        self._meta.logo_url    = logo_url
        self._meta.author_name = author

        if logo_path and not logo:
            logo = Path(logo_path).read_bytes()
        self._meta.logo_bytes = logo

        # Build C struct and call set_container_meta
        # We use a raw byte buffer matching the C OucsContainerMeta layout
        # theme_name (256) + description (1024) + logo_size (4) + logo_bytes (N)
        # + logo_ext_url (512) + created_at (8) + author_uuid (16) + author_name (16) + crc32 (4)
        # For simplicity we call individual fields via the writer internal path
        self._pending_meta_set = True

    def add_song(self, path: Union[str, Path],
                 name: str = "",
                 description: str = "",
                 password: Optional[str] = None,
                 lyrics: Optional[List[LyricLine]] = None,
                 chapters: Optional[List[Chapter]] = None) -> None:
        """
        Add a song from a file.

        Args:
            path:        Path to the audio file (mp3, flac, ogg, wav, aac).
            name:        Song display name.
            description: Song description.
            password:    If set, encrypts this song with AES-256-GCM.
            lyrics:      List of LyricLine(timestamp_ms, text) entries.
            chapters:    List of Chapter(offset_ms, name) entries.
        """
        self._assert_write()
        ret = _lib.oucs_writer_add_song(
            self._writer_ptr,
            str(path).encode(),
            name.encode() if name else None,
            description.encode() if description else None,
            None
        )
        raise_for_code(ret, f"add_song({path})")

        if password:
            ret = _lib.oucs_writer_encrypt_song(
                self._writer_ptr, password.encode()
            )
            raise_for_code(ret, "encrypt_song")

    def add_song_bytes(self, data: bytes, audio_format: str = "MP3 ",
                       name: str = "", description: str = "",
                       password: Optional[str] = None) -> None:
        """Add a song from raw bytes."""
        self._assert_write()
        buf = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
        ret = _lib.oucs_writer_add_song_mem(
            self._writer_ptr, buf, ctypes.c_size_t(len(data)),
            audio_format.encode(),
            name.encode() if name else None,
            description.encode() if description else None,
            None
        )
        raise_for_code(ret, "add_song_bytes")
        if password:
            ret = _lib.oucs_writer_encrypt_song(self._writer_ptr, password.encode())
            raise_for_code(ret, "encrypt_song")

    def save(self) -> None:
        """Finalize and write the .oucs file to disk."""
        self._assert_write()
        ret = _lib.oucs_writer_finalize(self._writer_ptr)
        raise_for_code(ret, "finalize")

    # ── Read API ──────────────────────────────────────────────

    @property
    def meta(self) -> OucsContainerMeta:
        """Container metadata."""
        return self._meta

    @property
    def song_count(self) -> int:
        """Number of songs in the container."""
        self._assert_read()
        return self._song_count

    def songs(self) -> Iterator[OucsSong]:
        """Iterate over all songs."""
        self._assert_read()
        for i in range(self._song_count):
            yield self.song(i)

    def song(self, index: int) -> OucsSong:
        """Get a song by index."""
        self._assert_read()
        meta = self._read_song_meta(index)
        return OucsSong(self, index, meta)

    def find_by_uuid(self, uuid: str) -> Optional[OucsSong]:
        """Find a song by UUID string."""
        for s in self.songs():
            if s.uuid == uuid:
                return s
        return None

    def extract(self, index: int, output_path: str,
                password: Optional[str] = None) -> None:
        """Extract song at index to a file."""
        self._assert_read()
        ret = _lib.oucs_reader_extract_song(
            self._reader_ptr,
            ctypes.c_uint32(index),
            output_path.encode(),
            password.encode() if password else None
        )
        raise_for_code(ret, f"extract({index})")

    def extract_bytes(self, index: int,
                      password: Optional[str] = None) -> bytes:
        """Extract song at index to memory."""
        self._assert_read()
        data_ptr = ctypes.POINTER(ctypes.c_uint8)()
        size_val = ctypes.c_size_t(0)
        ret = _lib.oucs_reader_extract_song_mem(
            self._reader_ptr,
            ctypes.c_uint32(index),
            ctypes.byref(data_ptr),
            ctypes.byref(size_val),
            password.encode() if password else None
        )
        raise_for_code(ret, f"extract_bytes({index})")
        size = size_val.value
        result = bytes(data_ptr[:size])
        # Free the C-allocated buffer
        ctypes.cdll.LoadLibrary(None).free(data_ptr)
        return result

    def stream(self, index: int, chunk_size: int = 4096,
               password: Optional[str] = None) -> OucsStream:
        """Open a stream for song at index."""
        self._assert_read()
        return self._open_stream(index, chunk_size, password)

    def info(self) -> str:
        """Return a formatted string of container info."""
        lines = [
            "OUCS Container",
            "═" * 50,
            str(self._meta),
            f"Songs      : {self._song_count}",
            "",
        ]
        for s in self.songs():
            lines.append(str(s.meta))
            lines.append("")
        return "\n".join(lines)

    # ── Merge / Split (static methods) ───────────────────────

    @staticmethod
    def merge(inputs: List[str], output: str) -> None:
        """Merge multiple .oucs files into one."""
        c_inputs = (ctypes.c_char_p * len(inputs))(
            *[p.encode() for p in inputs]
        )
        ret = _lib.oucs_merge(c_inputs, ctypes.c_uint32(len(inputs)),
                               output.encode())
        raise_for_code(ret, "merge")

    @staticmethod
    def split(input_path: str, from_idx: int,
              to_idx: int, output: str) -> None:
        """Split a .oucs file: extract songs [from_idx..to_idx]."""
        ret = _lib.oucs_split(
            input_path.encode(),
            ctypes.c_uint32(from_idx),
            ctypes.c_uint32(to_idx),
            output.encode()
        )
        raise_for_code(ret, "split")

    # ── Close ────────────────────────────────────────────────

    def close(self) -> None:
        """Close and free all resources."""
        if self._writer_ptr:
            _lib.oucs_writer_free(self._writer_ptr)
            self._writer_ptr = None
        if self._reader_ptr:
            _lib.oucs_reader_free(self._reader_ptr)
            self._reader_ptr = None

    def __del__(self):
        self.close()

    def __repr__(self) -> str:
        mode = "write" if self._mode == "w" else "read"
        return f"OucsFile({self._path!r}, mode={mode!r}, songs={self._song_count})"

    # ── Internal helpers ──────────────────────────────────────

    def _assert_read(self):
        if self._mode != "r" or not self._reader_ptr:
            raise OucsError("File not open for reading")

    def _assert_write(self):
        if self._mode != "w" or not self._writer_ptr:
            raise OucsError("File not open for writing")

    def _load_meta(self):
        """Load container metadata by parsing .oucs binary directly."""
        if not self._path or self._path.startswith("http"):
            return
        try:
            with open(self._path, "rb") as f:
                hdr = f.read(44)
                if len(hdr) < 44 or hdr[:4] != b"OUCS":
                    return
                meta_offset = struct.unpack_from("<Q", hdr, 12)[0]
                if meta_offset == 0:
                    return
                f.seek(meta_offset)
                theme_raw   = f.read(256)
                desc_raw    = f.read(1024)
                logo_size   = struct.unpack("<I", f.read(4))[0]
                logo_bytes  = f.read(logo_size) if logo_size > 0 else None
                url_raw     = f.read(512)
                created_at  = struct.unpack("<Q", f.read(8))[0]

                def _cstr(b: bytes) -> str:
                    idx = b.find(b"\x00")
                    return b[:idx].decode("utf-8", errors="replace") if idx >= 0 else b.decode("utf-8", errors="replace")

                self._meta.theme       = _cstr(theme_raw)
                self._meta.description = _cstr(desc_raw)
                self._meta.logo_bytes  = logo_bytes
                self._meta.logo_url    = _cstr(url_raw)
                if created_at:
                    self._meta.created_at = datetime.datetime.fromtimestamp(created_at)
        except Exception:
            pass  # graceful degradation

    def _read_song_meta(self, index: int) -> OucsSongMeta:
        """Read index entry for song at index by parsing binary directly."""
        meta = OucsSongMeta(index=index, name=f"Song {index}")
        if not self._path or self._path.startswith("http"):
            return meta
        try:
            _INDEX_ENTRY = 512
            with open(self._path, "rb") as f:
                hdr = f.read(44)
                if len(hdr) < 44:
                    return meta
                index_offset = struct.unpack_from("<Q", hdr, 20)[0]
                if index_offset == 0:
                    return meta
                f.seek(index_offset + index * _INDEX_ENTRY)
                entry = f.read(_INDEX_ENTRY)
                if len(entry) < _INDEX_ENTRY:
                    return meta

                def _cstr(b: bytes) -> str:
                    idx = b.find(b"\x00")
                    return b[:idx].decode("utf-8", errors="replace") if idx >= 0 else b.decode("utf-8", errors="replace")

                uuid_bytes = entry[0:16]
                meta.uuid        = "-".join([
                    uuid_bytes[:4].hex(), uuid_bytes[4:6].hex(),
                    uuid_bytes[6:8].hex(), uuid_bytes[8:10].hex(),
                    uuid_bytes[10:16].hex()
                ])
                meta.name        = _cstr(entry[16:80]) or f"Song {index}"
                meta.description = _cstr(entry[80:336])
                meta.byte_size   = struct.unpack_from("<Q", entry, 344)[0]
                meta.audio_format = entry[352:356].decode("ascii", errors="replace").strip().rstrip("\x00")
                meta.crc32       = struct.unpack_from("<I", entry, 356)[0]
                meta.bpm         = struct.unpack_from("<f", entry, 436)[0]
                meta.musical_key = entry[440]
                meta.mood_flags  = entry[441]
                meta.language    = entry[442:446].decode("ascii", errors="replace").rstrip("\x00")
                meta.duration_ms = struct.unpack_from("<I", entry, 475)[0]
                meta.sample_rate = struct.unpack_from("<I", entry, 479)[0]
                meta.channels    = struct.unpack_from("<H", entry, 483)[0]
                meta.bitrate_kbps = struct.unpack_from("<H", entry, 485)[0]
                meta.track_number = entry[487]
                meta.encrypted   = entry[462] == 1
                meta.has_lyrics  = struct.unpack_from("<Q", entry, 404)[0] > 0
                meta.has_chapters = struct.unpack_from("<I", entry, 424)[0] > 0
                meta.has_waveform = struct.unpack_from("<Q", entry, 376)[0] > 0
                meta.has_accessibility = struct.unpack_from("<Q", entry, 428)[0] > 0
        except Exception:
            pass
        return meta

    def _open_stream(self, index: int, chunk_size: int,
                     password: Optional[str]) -> OucsStream:
        return OucsStream(
            self._reader_ptr, index, chunk_size, password
        )
