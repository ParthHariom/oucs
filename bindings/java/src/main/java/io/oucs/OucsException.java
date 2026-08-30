package io.oucs;

/** Exception thrown by OUCS operations. */
public class OucsException extends RuntimeException {
    private final int code;

    public OucsException(String message) { super(message); this.code = 0; }
    public OucsException(String message, int code) { super(message); this.code = code; }

    public int getCode() { return code; }

    static void checkCode(int code, String context) {
        if (code == 0) return;
        String base = context + ": error code " + code;
        if (code == -6) throw new OucsNotFoundException(base);
        if (code == -5 || code == -3 || code == -10) throw new OucsCorruptException(base);
        if (code == -8 || code == -9) throw new OucsCryptoException(base);
        throw new OucsException(base, code);
    }

    public static class OucsNotFoundException  extends OucsException { OucsNotFoundException(String m) { super(m, -6); } }
    public static class OucsCorruptException   extends OucsException { OucsCorruptException(String m)  { super(m, -5); } }
    public static class OucsCryptoException    extends OucsException { OucsCryptoException(String m)   { super(m, -9); } }
}
