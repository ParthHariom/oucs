package io.oucs;

/**
 * OucsStream — chunk-by-chunk low-memory audio streaming.
 *
 * <pre>{@code
 * try (OucsStream s = reader.stream(0, 4096)) {
 *     byte[] chunk;
 *     while ((chunk = s.readChunk()) != null) {
 *         audioPlayer.feed(chunk);
 *     }
 * }
 * }</pre>
 */
public class OucsStream implements AutoCloseable {

    private long _ptr;
    private final int _chunkSize;

    OucsStream(long readerPtr, int songIndex, int chunkSize, String password) {
        _chunkSize = chunkSize;
        _ptr = nativeOpen(readerPtr, songIndex, chunkSize, password);
        if (_ptr == 0) throw new OucsException("Cannot open stream for song " + songIndex);
    }

    /**
     * Read the next chunk. Returns null at end of stream.
     * Only this chunk is in memory — device load is near zero.
     */
    public byte[] readChunk() {
        if (_ptr == 0) return null;
        return nativeReadChunk(_ptr, _chunkSize);
    }

    /**
     * Seek to a byte offset within the song's audio data.
     */
    public void seek(long byteOffset) {
        OucsException.checkCode(nativeSeek(_ptr, byteOffset), "seek");
    }

    /**
     * Seek to a chapter by index.
     */
    public void seekChapter(int chapterIndex) {
        OucsException.checkCode(nativeSeekChapter(_ptr, chapterIndex), "seekChapter");
    }

    /**
     * Current byte position within the song.
     */
    public long getPosition() {
        return _ptr != 0 ? nativeTell(_ptr) : 0;
    }

    /**
     * Read all audio data into a single byte array.
     * Use with care for large files.
     */
    public byte[] readAll() {
        java.io.ByteArrayOutputStream baos = new java.io.ByteArrayOutputStream();
        byte[] chunk;
        while ((chunk = readChunk()) != null) {
            baos.write(chunk, 0, chunk.length);
        }
        return baos.toByteArray();
    }

    @Override
    public void close() {
        if (_ptr != 0) { nativeFree(_ptr); _ptr = 0; }
    }

    // ── Native declarations ───────────────────────────────────
    private static native long   nativeOpen(long readerPtr, int songIndex,
                                             int chunkSize, String password);
    private static native byte[] nativeReadChunk(long ptr, int chunkSize);
    private static native int    nativeSeek(long ptr, long offset);
    private static native int    nativeSeekChapter(long ptr, int chapterIndex);
    private static native long   nativeTell(long ptr);
    private static native void   nativeFree(long ptr);
}
