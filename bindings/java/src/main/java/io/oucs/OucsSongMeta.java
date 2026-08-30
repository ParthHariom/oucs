package io.oucs;

/** Metadata for a single song in a .oucs container. */
public class OucsSongMeta {
    public int    index;
    public String uuid;
    public String name;
    public String description;
    public String audioFormat;
    public long   byteSize;
    public long   crc32;
    public float  bpm;
    public int    musicalKey;
    public int    moodFlags;
    public String language;
    public long   durationMs;
    public int    sampleRate;
    public int    channels;
    public int    bitrateKbps;
    public int    trackNumber;
    public boolean encrypted;
    public boolean hasLyrics;
    public boolean hasChapters;
    public boolean hasWaveform;
    public boolean hasAccessibility;

    private static final String[] KEY_NAMES =
        {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

    public String getKeyName() {
        return (musicalKey >= 0 && musicalKey < 12) ? KEY_NAMES[musicalKey] : "Unknown";
    }

    public String getMoodStr() {
        if (moodFlags == 0) return "Unknown";
        StringBuilder sb = new StringBuilder();
        if ((moodFlags & 1)  != 0) sb.append("Happy ");
        if ((moodFlags & 2)  != 0) sb.append("Sad ");
        if ((moodFlags & 4)  != 0) sb.append("Energetic ");
        if ((moodFlags & 8)  != 0) sb.append("Calm ");
        if ((moodFlags & 16) != 0) sb.append("Romantic ");
        if ((moodFlags & 32) != 0) sb.append("Angry ");
        return sb.toString().trim();
    }

    public String getDurationStr() {
        long s = durationMs / 1000;
        return String.format("%d:%02d", s / 60, s % 60);
    }

    @Override
    public String toString() {
        return String.format(
            "[%2d] %s%s\n     Format: %s  Duration: %s  BPM: %.1f  Key: %s  Mood: %s\n     UUID: %s  Size: %d bytes",
            index, name != null ? name : "(untitled)", encrypted ? " [encrypted]" : "",
            audioFormat, getDurationStr(), bpm, getKeyName(), getMoodStr(),
            uuid, byteSize
        );
    }
}
