package com.Benno111.dorfplatformertimetravel;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.HashSet;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Streaming ZIP extraction shared with the native-mod import tests. */
public final class NativeModArchive {
    public static void extract(InputStream input, File directory) throws IOException {
        final String prefix = directory.getCanonicalPath() + File.separator;
        Set<String> names = new HashSet<>();
        long total = 0;
        int count = 0;
        byte[] buffer = new byte[16384];
        try (ZipInputStream zip = new ZipInputStream(input)) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                if (++count > 512) throw new IOException("Mod archive has too many files");
                String name = entry.getName();
                File target = new File(directory, name);
                String canonical = target.getCanonicalPath();
                if (name.startsWith("/") || name.contains("\\") || !canonical.startsWith(prefix) || !names.add(canonical))
                    throw new IOException("Invalid or duplicate mod archive path");
                if (entry.isDirectory()) {
                    if (!target.isDirectory() && !target.mkdirs()) throw new IOException("Cannot create mod directory");
                } else {
                    File parent = target.getParentFile();
                    if (!parent.isDirectory() && !parent.mkdirs()) throw new IOException("Cannot create mod directory");
                    try (FileOutputStream out = new FileOutputStream(target)) {
                        int read;
                        while ((read = zip.read(buffer)) != -1) {
                            total += read;
                            if (total > 128L * 1024 * 1024) throw new IOException("Mod archive exceeds 128 MB");
                            out.write(buffer, 0, read);
                        }
                    }
                }
            }
        }
    }
    private NativeModArchive() {}
}
