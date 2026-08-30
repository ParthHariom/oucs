package io.oucs;

/**
 * OucsWriter — creates a new .oucs container file.
 *
 * <pre>{@code
 * try (OucsWriter w = OucsEngine.create("playlist.oucs")) {
 *     w.setMeta("Summer Hits", "Hot tracks 2025", null, "https://example.com/logo.png");
 *     w.addSong("song1.mp3", "Song One", "An upbeat track");
 *     w.addSong("song2.flac", "Song Two", "A chill track", "secretpassword");
 *     w.save();
 * }
 * }</pre>
 */
public class OucsWriter implements AutoCloseable {

    private long _ptr;  // native OucsWriter* pointer

    OucsWriter(String path, int flags) {
        _ptr = nativeCreate(path, flags);
        if (_ptr == 0) throw new OucsException("Cannot create .oucs file: " + path);
    }

    /**
     * Set container-level metadata.
     *
     * @param theme       Playlist/container theme name
     * @param description Short description
     * @param logoBytes   Raw logo image bytes (PNG/JPEG), or null
     * @param logoUrl     External logo URL, or empty string
     */
    public void setMeta(String theme, String description,
                        byte[] logoBytes, String logoUrl) {
        nativeSetMeta(_ptr, theme, description, logoBytes, logoUrl);
    }

    /**
     * Add a song from a file path.
     *
     * @param filePath    Path to audio file
     * @param name        Song display name
     * @param description Song description
     */
    public void addSong(String filePath, String name, String description) {
        int ret = nativeAddSong(_ptr, filePath, name, description);
        OucsException.checkCode(ret, "addSong(" + filePath + ")");
    }

    /**
     * Add an encrypted song. Password is required to extract/stream later.
     */
    public void addSong(String filePath, String name, String description, String password) {
        addSong(filePath, name, description);
        if (password != null && !password.isEmpty()) {
            int ret = nativeEncryptSong(_ptr, password);
            OucsException.checkCode(ret, "encryptSong");
        }
    }

    /**
     * Add a song from raw bytes.
     */
    public void addSongBytes(byte[] data, String audioFormat,
                              String name, String description) {
        int ret = nativeAddSongBytes(_ptr, data, audioFormat, name, description);
        OucsException.checkCode(ret, "addSongBytes");
    }

    /**
     * Finalize and write the .oucs file to disk.
     * Must be called before closing.
     */
    public void save() {
        int ret = nativeFinalize(_ptr);
        OucsException.checkCode(ret, "save/finalize");
    }

    @Override
    public void close() {
        if (_ptr != 0) {
            nativeFree(_ptr);
            _ptr = 0;
        }
    }

    // ── Native declarations ───────────────────────────────────
    private static native long nativeCreate(String path, int flags);
    private static native void nativeSetMeta(long ptr, String theme, String description,
                                              byte[] logoBytes, String logoUrl);
    private static native int  nativeAddSong(long ptr, String filePath,
                                              String name, String description);
    private static native int  nativeAddSongBytes(long ptr, byte[] data, String format,
                                                   String name, String description);
    private static native int  nativeEncryptSong(long ptr, String password);
    private static native int  nativeFinalize(long ptr);
    private static native void nativeFree(long ptr);
}
