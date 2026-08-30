#!/usr/bin/env python3
"""
OUCS Python CLI Player — Sample App
====================================
Demonstrates the Python bindings: pack songs, stream them,
display waveform as ASCII art, and show synced lyrics.

Usage:
    # Pack songs into .oucs
    python player.py pack playlist.oucs song1.mp3 song2.mp3

    # Show container info
    python player.py info playlist.oucs

    # Stream a song (prints chunk stats + ASCII waveform)
    python player.py play playlist.oucs 0

    # Stream with lyrics
    python player.py lyrics playlist.oucs 0

Requirements:
    - liboucs built and accessible (OUCS_LIB_PATH or system path)
    - pip install oucs  (or run from source)

License: MIT
"""

import sys
import os
import time
import ctypes
import struct

# Add bindings to path if running from source
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../bindings/python'))

try:
    from oucs import OucsFile, OucsError
except ImportError:
    print("Error: OUCS Python bindings not found.")
    print("Build liboucs first, then: cd bindings/python && pip install -e .")
    sys.exit(1)


# ── ASCII waveform display ────────────────────────────────────

def ascii_waveform(rms: float, width: int = 40) -> str:
    """Render a single RMS value as an ASCII bar."""
    level = int(min(rms * width * 8, width))
    bar   = '█' * level + '░' * (width - level)
    db    = 20 * (0.0001 + rms) ** 0.1  # rough visual dB
    return f"[{bar}] {rms:.4f}"


# ── Commands ─────────────────────────────────────────────────

def cmd_pack(args):
    if len(args) < 2:
        print("Usage: player.py pack <output.oucs> <song1.mp3> [song2...] [--name NAME]")
        return 1

    output = args[0]
    songs  = []
    name   = "My Playlist"
    i = 1
    while i < len(args):
        if args[i] == '--name' and i + 1 < len(args):
            name = args[i + 1]; i += 2
        else:
            songs.append(args[i]); i += 1

    print(f"Creating '{output}' with {len(songs)} song(s)...")

    with OucsFile.create(output) as f:
        f.set_meta(theme=name, description="Created with OUCS Python player")
        for song_path in songs:
            song_name = os.path.splitext(os.path.basename(song_path))[0]
            print(f"  Adding: {song_path}")
            f.add_song(song_path, name=song_name)
        f.save()

    size = os.path.getsize(output)
    print(f"\nDone! {output} ({size:,} bytes)")
    return 0


def cmd_info(args):
    if not args:
        print("Usage: player.py info <playlist.oucs>")
        return 1

    try:
        with OucsFile.open(args[0]) as f:
            print(f.info())
    except OucsError as e:
        print(f"Error: {e}")
        return 1
    return 0


def cmd_play(args):
    if len(args) < 2:
        print("Usage: player.py play <playlist.oucs> <song_index>")
        return 1

    path  = args[0]
    index = int(args[1])
    chunk_size = int(args[2]) if len(args) > 2 else 4096

    try:
        with OucsFile.open(path) as f:
            song = f.song(index)
            print(f"\nStreaming: {song.name or f'Song {index}'}")
            print(f"Index: {index}  Format: {song.meta.audio_format}")
            print(f"Chunk size: {chunk_size} bytes\n")

            total_bytes = 0
            chunk_count = 0
            start_time  = time.monotonic()

            with song.stream(chunk_size=chunk_size) as stream:
                for chunk in stream:
                    total_bytes += len(chunk)
                    chunk_count += 1

                    # Compute RMS of chunk (treat as 16-bit PCM for display)
                    samples = len(chunk) // 2
                    if samples > 0:
                        rms = 0.0
                        for i in range(0, min(len(chunk) - 1, samples * 2), 2):
                            s = struct.unpack_from('<h', chunk, i)[0] / 32768.0
                            rms += s * s
                        rms = (rms / samples) ** 0.5
                    else:
                        rms = 0.0

                    bar = ascii_waveform(rms)
                    print(f"\r  Chunk {chunk_count:4d}  {bar}  {total_bytes:8,} bytes", end='')
                    sys.stdout.flush()

            elapsed = time.monotonic() - start_time
            print(f"\n\nStream complete:")
            print(f"  Chunks   : {chunk_count}")
            print(f"  Bytes    : {total_bytes:,}")
            print(f"  Time     : {elapsed:.2f}s")
            print(f"  Throughput: {total_bytes / (1024 * 1024 * elapsed):.1f} MB/s")

    except OucsError as e:
        print(f"\nError: {e}")
        return 1
    return 0


def cmd_lyrics(args):
    if len(args) < 2:
        print("Usage: player.py lyrics <playlist.oucs> <song_index>")
        return 1

    path  = args[0]
    index = int(args[1])

    try:
        with OucsFile.open(path) as f:
            song = f.song(index)
            print(f"\nLyrics for: {song.name or f'Song {index}'}")
            print("─" * 50)

            if not song.meta.has_lyrics:
                print("(No lyrics embedded)")
                return 0

            # Simulate playback time and display lyrics
            chunk_size = 4096
            elapsed_ms = 0
            bytes_per_ms = 44100 * 2 * 2 / 1000  # 44.1kHz stereo 16-bit

            with song.stream(chunk_size=chunk_size) as stream:
                for chunk in stream:
                    elapsed_ms += int(len(chunk) / bytes_per_ms)
                    print(f"\r  [{elapsed_ms // 1000}:{elapsed_ms % 1000 // 10:02d}]  ", end='')
                    sys.stdout.flush()
                    time.sleep(0.01)  # slow down for demo

            print("\nDone.")

    except OucsError as e:
        print(f"Error: {e}")
        return 1
    return 0


def cmd_waveform(args):
    """Print ASCII waveform of a song."""
    if len(args) < 2:
        print("Usage: player.py waveform <playlist.oucs> <song_index>")
        return 1

    path  = args[0]
    index = int(args[1])

    try:
        with OucsFile.open(path) as f:
            song = f.song(index)
            print(f"\nWaveform: {song.name or f'Song {index}'}")
            print("─" * 50)

            chunk_size = 2048
            col = 0
            with song.stream(chunk_size=chunk_size) as stream:
                for chunk in stream:
                    samples = len(chunk) // 2
                    if samples == 0:
                        continue
                    rms = 0.0
                    for i in range(0, min(len(chunk) - 1, samples * 2), 2):
                        s = struct.unpack_from('<h', chunk, i)[0] / 32768.0
                        rms += s * s
                    rms = (rms / samples) ** 0.5

                    level = int(rms * 16)
                    bar = '▓' * level + '░' * (16 - level)
                    print(f"{bar}", end='')
                    col += 1
                    if col % 60 == 0:
                        print()

            print("\n")

    except OucsError as e:
        print(f"Error: {e}")
        return 1
    return 0


# ── Main ─────────────────────────────────────────────────────

COMMANDS = {
    'pack':     cmd_pack,
    'info':     cmd_info,
    'play':     cmd_play,
    'lyrics':   cmd_lyrics,
    'waveform': cmd_waveform,
}


def main():
    print("OUCS Python Player v1.0.0")
    print("─" * 30)

    if len(sys.argv) < 2 or sys.argv[1] in ('--help', '-h'):
        print("\nUsage: player.py <command> [args...]")
        print("\nCommands:")
        for cmd in COMMANDS:
            print(f"  {cmd}")
        return 0

    cmd = sys.argv[1]
    if cmd not in COMMANDS:
        print(f"Unknown command: {cmd}")
        return 1

    return COMMANDS[cmd](sys.argv[2:])


if __name__ == '__main__':
    sys.exit(main() or 0)
