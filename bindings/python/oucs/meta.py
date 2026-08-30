"""Container and per-song metadata dataclasses."""
from dataclasses import dataclass, field
from typing import Optional, List
import datetime


@dataclass
class OucsContainerMeta:
    """Container-level metadata for a .oucs file."""
    theme: str = ""
    description: str = ""
    logo_bytes: Optional[bytes] = None
    logo_url: str = ""
    created_at: Optional[datetime.datetime] = None
    author_name: str = ""

    def __str__(self) -> str:
        lines = [
            f"Theme      : {self.theme or '(none)'}",
            f"Description: {self.description or '(none)'}",
            f"Logo       : {self.logo_url or ('embedded' if self.logo_bytes else 'none')} "
            f"({len(self.logo_bytes) if self.logo_bytes else 0} bytes)",
        ]
        if self.created_at:
            lines.append(f"Created    : {self.created_at.isoformat()}")
        return "\n".join(lines)


@dataclass
class LyricLine:
    timestamp_ms: int
    text: str


@dataclass
class Chapter:
    offset_ms: int
    name: str


@dataclass
class OucsSongMeta:
    """Per-song metadata for a single song in a .oucs file."""
    index: int = 0
    uuid: str = ""
    name: str = ""
    description: str = ""
    audio_format: str = ""
    byte_size: int = 0
    crc32: int = 0
    bpm: float = 0.0
    musical_key: int = 255
    mood_flags: int = 0
    language: str = ""
    duration_ms: int = 0
    sample_rate: int = 0
    channels: int = 0
    bitrate_kbps: int = 0
    track_number: int = 0
    encrypted: bool = False
    has_lyrics: bool = False
    has_chapters: bool = False
    has_waveform: bool = False
    has_fingerprint: bool = False
    has_accessibility: bool = False
    chapters: List[Chapter] = field(default_factory=list)
    lyrics: List[LyricLine] = field(default_factory=list)

    KEY_NAMES = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]
    MOOD_NAMES = {1: "Happy", 2: "Sad", 4: "Energetic", 8: "Calm", 16: "Romantic", 32: "Angry"}

    @property
    def key_name(self) -> str:
        if self.musical_key < 12:
            return self.KEY_NAMES[self.musical_key]
        return "Unknown"

    @property
    def mood_str(self) -> str:
        if not self.mood_flags:
            return "Unknown"
        return " ".join(n for v, n in self.MOOD_NAMES.items() if self.mood_flags & v)

    @property
    def duration_str(self) -> str:
        s = self.duration_ms // 1000
        return f"{s // 60}:{s % 60:02d}"

    def __str__(self) -> str:
        enc = " [encrypted]" if self.encrypted else ""
        return (
            f"[{self.index:2d}] {self.name or '(untitled)'}{enc}\n"
            f"     Format: {self.audio_format}  Duration: {self.duration_str}  "
            f"BPM: {self.bpm:.1f}  Key: {self.key_name}  Mood: {self.mood_str}\n"
            f"     UUID: {self.uuid}  Size: {self.byte_size} bytes"
        )
