"""
OUCS Python Bindings
====================
Pythonic wrapper over liboucs using CFFI.

Usage::

    from oucs import OucsFile, OucsStream

    # Pack songs
    with OucsFile.create("playlist.oucs") as f:
        f.set_meta(theme="My Playlist", description="Weekend vibes")
        f.add_song("song1.mp3", name="Song One")
        f.add_song("song2.flac", name="Song Two")

    # Read & stream
    with OucsFile.open("playlist.oucs") as f:
        print(f.info())
        for song in f.songs():
            print(song)
        stream = f.stream(0, chunk_size=4096)
        for chunk in stream:
            audio_player.feed(chunk)

License: MIT
"""

from .file import OucsFile
from .song import OucsSong
from .stream import OucsStream
from .meta import OucsContainerMeta, OucsSongMeta
from .exceptions import OucsError, OucsNotFoundError, OucsCorruptError, OucsCryptoError
from ._version import __version__

__all__ = [
    "OucsFile",
    "OucsSong",
    "OucsStream",
    "OucsContainerMeta",
    "OucsSongMeta",
    "OucsError",
    "OucsNotFoundError",
    "OucsCorruptError",
    "OucsCryptoError",
    "__version__",
]
