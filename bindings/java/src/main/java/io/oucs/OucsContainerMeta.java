package io.oucs;

/** Container-level metadata for a .oucs file. */
public class OucsContainerMeta {
    public String theme;
    public String description;
    public byte[] logoBytes;
    public String logoUrl;
    public long   createdAt;
    public String authorName;

    @Override
    public String toString() {
        return String.format(
            "Theme: %s\nDescription: %s\nLogo: %s (%d bytes)\nCreated: %d",
            theme != null ? theme : "(none)",
            description != null ? description : "(none)",
            logoUrl != null && !logoUrl.isEmpty() ? logoUrl : "(embedded)",
            logoBytes != null ? logoBytes.length : 0,
            createdAt
        );
    }
}
