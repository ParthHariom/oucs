package io.oucs;

/**
 * OucsEngine — main entry point for the OUCS Java bindings.
 *
 * Wraps liboucs via JNI. Load the native library before use:
 *
 * <pre>{@code
 * OucsEngine.loadLibrary(); // or System.loadLibrary("oucs")
 *
 * // Write
 * try (OucsWriter writer = OucsEngine.create("playlist.oucs")) {
 *     writer.setMeta("My Playlist", "Weekend vibes", null, "");
 *     writer.addSong("track1.mp3", "Track One", "");
 *     writer.addSong("track2.flac", "Track Two", "");
 *     writer.save();
 * }
 *
 * // Read
 * try (OucsReader reader = OucsEngine.open("playlist.oucs")) {
 *     System.out.println(reader.getSongCount() + " songs");
 *     OucsStream stream = reader.stream(0, 4096);
 *     byte[] chunk;
 *     while ((chunk = stream.readChunk()) != null) {
 *         audioPlayer.feed(chunk);
 *     }
 * }
 * }</pre>
 *
 * License: MIT
 */
public class OucsEngine {

    private static boolean _loaded = false;

    /** Load the native liboucs library. */
    public static synchronized void loadLibrary() {
        if (_loaded) return;
        // Try OUCS_LIB_PATH env first
        String envPath = System.getenv("OUCS_LIB_PATH");
        if (envPath != null) {
            System.load(envPath);
        } else {
            System.loadLibrary("oucs");
        }
        _loaded = true;
    }

    /** Create a new .oucs file for writing. */
    public static OucsWriter create(String path) {
        return create(path, 0);
    }

    /** Create a new .oucs file for writing with flags. */
    public static OucsWriter create(String path, int flags) {
        return new OucsWriter(path, flags);
    }

    /** Open an existing .oucs file for reading. */
    public static OucsReader open(String path) {
        return new OucsReader(path);
    }

    /** Open a .oucs file via URL (HTTP Range streaming). */
    public static OucsReader openUrl(String url) {
        return OucsReader.fromUrl(url);
    }

    /** Merge multiple .oucs files into one. */
    public static void merge(String[] inputs, String output) {
        nativeMerge(inputs, output);
    }

    /** Split a .oucs file into a subset. */
    public static void split(String input, int fromIdx, int toIdx, String output) {
        nativeSplit(input, fromIdx, toIdx, output);
    }

    /** Get liboucs version string. */
    public static native String version();

    /** Get error string for a native error code. */
    public static native String strerror(int code);

    // Native method declarations
    static native void nativeMerge(String[] inputs, String output);
    static native void nativeSplit(String input, int fromIdx, int toIdx, String output);
}
