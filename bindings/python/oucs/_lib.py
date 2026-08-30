"""
CFFI loader for liboucs.
Loads the shared library and exposes the raw C API.
"""
import ctypes
import ctypes.util
import os
import sys
from pathlib import Path


def _find_library() -> str:
    """Locate liboucs shared library."""
    # 1. Check OUCS_LIB_PATH environment variable
    env_path = os.environ.get("OUCS_LIB_PATH")
    if env_path and os.path.isfile(env_path):
        return env_path

    # 2. Check package directory (for wheel installs)
    pkg_dir = Path(__file__).parent
    for name in ("liboucs.so", "liboucs.dylib", "oucs.dll", "liboucs.so.1"):
        candidate = pkg_dir / name
        if candidate.exists():
            return str(candidate)

    # 3. Check adjacent build directories (development mode)
    build_dirs = [
        pkg_dir.parent.parent.parent / "build" / "liboucs.so",
        pkg_dir.parent.parent.parent / "build" / "liboucs.dylib",
        pkg_dir.parent.parent.parent / "build" / "oucs.dll",
        Path("/usr/local/lib/liboucs.so"),
        Path("/usr/local/lib/liboucs.dylib"),
    ]
    for p in build_dirs:
        if p.exists():
            return str(p)

    # 4. ctypes.util.find_library
    found = ctypes.util.find_library("oucs")
    if found:
        return found

    raise OSError(
        "liboucs not found. Either:\n"
        "  - Build the C core: cmake -B build && cmake --build build\n"
        "  - Set OUCS_LIB_PATH=/path/to/liboucs.so\n"
        "  - Install via pip install oucs (pre-built wheel)"
    )


# ── Load library ─────────────────────────────────────────────

_lib_path = _find_library()
_lib = ctypes.CDLL(_lib_path)

# ── Typedefs ─────────────────────────────────────────────────

c_uint8_p  = ctypes.POINTER(ctypes.c_uint8)
c_uint8_pp = ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8))
c_size_p   = ctypes.POINTER(ctypes.c_size_t)

# ── Function signatures ───────────────────────────────────────

# Version
_lib.oucs_version.restype  = ctypes.c_char_p
_lib.oucs_version.argtypes = []

_lib.oucs_strerror.restype  = ctypes.c_char_p
_lib.oucs_strerror.argtypes = [ctypes.c_int]

# Writer
_lib.oucs_writer_create.restype  = ctypes.c_void_p
_lib.oucs_writer_create.argtypes = [ctypes.c_char_p, ctypes.c_uint8]

_lib.oucs_writer_finalize.restype  = ctypes.c_int
_lib.oucs_writer_finalize.argtypes = [ctypes.c_void_p]

_lib.oucs_writer_free.restype  = None
_lib.oucs_writer_free.argtypes = [ctypes.c_void_p]

_lib.oucs_writer_add_song.restype  = ctypes.c_int
_lib.oucs_writer_add_song.argtypes = [
    ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
    ctypes.c_void_p  # OucsIndexEntry* (can be NULL)
]

_lib.oucs_writer_add_song_mem.restype  = ctypes.c_int
_lib.oucs_writer_add_song_mem.argtypes = [
    ctypes.c_void_p, c_uint8_p, ctypes.c_size_t,
    ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
    ctypes.c_void_p
]

_lib.oucs_writer_encrypt_song.restype  = ctypes.c_int
_lib.oucs_writer_encrypt_song.argtypes = [ctypes.c_void_p, ctypes.c_char_p]

_lib.oucs_writer_set_lyrics.restype  = ctypes.c_int
_lib.oucs_writer_set_lyrics.argtypes = [ctypes.c_void_p, ctypes.c_void_p]

_lib.oucs_writer_set_chapters.restype  = ctypes.c_int
_lib.oucs_writer_set_chapters.argtypes = [ctypes.c_void_p, ctypes.c_void_p]

_lib.oucs_writer_set_accessibility.restype  = ctypes.c_int
_lib.oucs_writer_set_accessibility.argtypes = [ctypes.c_void_p, ctypes.c_void_p]

# Reader
_lib.oucs_reader_open.restype  = ctypes.c_void_p
_lib.oucs_reader_open.argtypes = [ctypes.c_char_p]

_lib.oucs_reader_open_url.restype  = ctypes.c_void_p
_lib.oucs_reader_open_url.argtypes = [ctypes.c_char_p]

_lib.oucs_reader_song_count.restype  = ctypes.c_int
_lib.oucs_reader_song_count.argtypes = [ctypes.c_void_p]

_lib.oucs_reader_extract_song_mem.restype  = ctypes.c_int
_lib.oucs_reader_extract_song_mem.argtypes = [
    ctypes.c_void_p, ctypes.c_uint32, c_uint8_pp,
    c_size_p, ctypes.c_char_p
]

_lib.oucs_reader_extract_song.restype  = ctypes.c_int
_lib.oucs_reader_extract_song.argtypes = [
    ctypes.c_void_p, ctypes.c_uint32, ctypes.c_char_p, ctypes.c_char_p
]

_lib.oucs_reader_free.restype  = None
_lib.oucs_reader_free.argtypes = [ctypes.c_void_p]

# Stream
_lib.oucs_stream_open.restype  = ctypes.c_void_p
_lib.oucs_stream_open.argtypes = [
    ctypes.c_void_p, ctypes.c_uint32, ctypes.c_size_t, ctypes.c_char_p
]

_lib.oucs_stream_read_chunk.restype  = ctypes.c_int
_lib.oucs_stream_read_chunk.argtypes = [
    ctypes.c_void_p, c_uint8_p, ctypes.c_size_t, c_size_p
]

_lib.oucs_stream_seek.restype  = ctypes.c_int
_lib.oucs_stream_seek.argtypes = [ctypes.c_void_p, ctypes.c_uint64]

_lib.oucs_stream_seek_chapter.restype  = ctypes.c_int
_lib.oucs_stream_seek_chapter.argtypes = [ctypes.c_void_p, ctypes.c_uint32]

_lib.oucs_stream_tell.restype  = ctypes.c_uint64
_lib.oucs_stream_tell.argtypes = [ctypes.c_void_p]

_lib.oucs_stream_free.restype  = None
_lib.oucs_stream_free.argtypes = [ctypes.c_void_p]

# Utils
_lib.oucs_uuid_generate.restype  = None
_lib.oucs_uuid_generate.argtypes = [c_uint8_p]

_lib.oucs_uuid_to_str.restype  = None
_lib.oucs_uuid_to_str.argtypes = [c_uint8_p, ctypes.c_char_p]

# Merge/split
_lib.oucs_merge.restype  = ctypes.c_int
_lib.oucs_merge.argtypes = [
    ctypes.POINTER(ctypes.c_char_p), ctypes.c_uint32, ctypes.c_char_p
]

_lib.oucs_split.restype  = ctypes.c_int
_lib.oucs_split.argtypes = [
    ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_char_p
]
