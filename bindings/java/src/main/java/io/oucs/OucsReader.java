package io.oucs;

/**
 * OucsReader — reads an existing .oucs container.
 */
public class OucsReader implements AutoCloseable {

    private long _ptr;

    OucsReader(String path) {
        _ptr = nativeOpen(path);
        if (_ptr == 0) throw new OucsException("Cannot open: " + path);
    }

    private OucsReader(long ptr) {
        if (ptr == 0) throw new OucsException("Cannot open URL");
        _ptr = ptr;
    }

    static OucsReader fromUrl(String url) {
        return new OucsReader(nativeOpenUrl(url));
    }

    /** Number of songs. */
    public int getSongCount() { return nativeSongCount(_ptr); }

    /**
     * Get metadata for a song by index.
     * @return OucsSongMeta populated from the index entry
     */
    public OucsSongMeta getSongMeta(int index) {
        checkIndex(index);
        return nativeGetSongMeta(_ptr, index);
    }

    /**
     * Get all song metadata.
     */
    public OucsSongMeta[] getAllSongMeta() {
        int n = getSongCount();
        OucsSongMeta[] arr = new OucsSongMeta[n];
        for (int i = 0; i < n; i++) arr[i] = getSongMeta(i);
        return arr;
    }

    /**
     * Get container-level metadata.
     */
    public OucsContainerMeta getContainerMeta() {
        return nativeGetContainerMeta(_ptr);
    }

    /**
     * Extract a song to a file.
     * @param index    Song index
     * @param outPath  Output file path
     * @param password Decryption password (null if not encrypted)
     */
    public void extractSong(int index, String outPath, String password) {
        checkIndex(index);
        int ret = nativeExtractSong(_ptr, index, outPath, password);
        OucsException.checkCode(ret, "extractSong(" + index + ")");
    }

    /**
     * Extract a song to a byte array.
     */
    public byte[] extractSongBytes(int index, String password) {
        checkIndex(index);
        byte[] data = nativeExtractSongBytes(_ptr, index, password);
        if (data == null) throw new OucsException("Extraction failed for song " + index);
        return data;
    }

    /**
     * Open a chunk-by-chunk stream for a song.
     * @param index     Song index
     * @param chunkSize Bytes per chunk (e.g. 4096)
     * @return OucsStream — call readChunk() repeatedly
     */
    public OucsStream stream(int index, int chunkSize) {
        return stream(index, chunkSize, null);
    }

    public OucsStream stream(int index, int chunkSize, String password) {
        checkIndex(index);
        return new OucsStream(_ptr, index, chunkSize, password);
    }

    /**
     * Find duplicate songs by CRC32.
     * @return Array of duplicate index pairs [[a,b], ...]
     */
    public int[][] findDuplicates() {
        return nativeFindDuplicates(_ptr);
    }

    @Override
    public void close() {
        if (_ptr != 0) { nativeFree(_ptr); _ptr = 0; }
    }

    private void checkIndex(int i) {
        if (i < 0 || i >= getSongCount())
            throw new IndexOutOfBoundsException("Song index " + i + " out of range");
    }

    // ── Native declarations ───────────────────────────────────
    private static native long          nativeOpen(String path);
    private static native long          nativeOpenUrl(String url);
    private static native int           nativeSongCount(long ptr);
    private static native OucsSongMeta  nativeGetSongMeta(long ptr, int index);
    private static native OucsContainerMeta nativeGetContainerMeta(long ptr);
    private static native int           nativeExtractSong(long ptr, int index,
                                                           String outPath, String password);
    private static native byte[]        nativeExtractSongBytes(long ptr, int index,
                                                                String password);
    private static native int[][]       nativeFindDuplicates(long ptr);
    private static native void          nativeFree(long ptr);
}
