package com.Benno111.dorfplatformertimetravel;

import java.io.*;

/** App-process-only logcat capture. No shell commands or broad READ_LOGS permission. */
public final class LogcatRecorder {
    public interface Launcher { Process launch(String pid) throws IOException; }
    private final Launcher launcher;
    private Process process;
    private Thread reader;
    private File destination;
    public LogcatRecorder() {
        this(pid -> new ProcessBuilder("logcat", "--pid=" + pid, "-v", "threadtime", "-T", "1")
            .redirectErrorStream(true).start());
    }
    public LogcatRecorder(Launcher launcher) { this.launcher = launcher; }
    public synchronized boolean setEnabled(boolean enabled, File file, String pid) {
        if (!enabled) { stop(); return true; }
        if (process != null && file.equals(destination)) return true;
        stop();
        try {
            File parent = file.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) return false;
            if (file.length() > 8 * 1024 * 1024) rotate(file);
            new FileOutputStream(file, true).close(); // Report an unwritable destination before spawning.
            final Process started = launcher.launch(pid);
            process = started; destination = file;
            reader = new Thread(() -> {
                try (InputStream in = started.getInputStream()) {
                    OutputStream out = new FileOutputStream(file, true);
                    try {
                        byte[] bytes = new byte[4096]; int count;
                        while ((count = in.read(bytes)) != -1) {
                            if (file.length() + count > 8 * 1024 * 1024) {
                                out.close(); rotate(file); out = new FileOutputStream(file, true);
                            }
                            out.write(bytes, 0, count); out.flush();
                        }
                    } finally { out.close(); }
                } catch (IOException ignored) {
                } finally {
                    synchronized (LogcatRecorder.this) {
                        if (process == started) { process = null; reader = null; destination = null; }
                    }
                    started.destroy();
                }
            }, "GameLogcatWriter");
            reader.setDaemon(true); reader.start(); return true;
        } catch (IOException error) { stop(); return false; }
    }
    private static void rotate(File file) {
        File previous = new File(file.getPath() + ".previous");
        if (previous.exists()) previous.delete();
        file.renameTo(previous);
    }
    public synchronized void stop() {
        Process old = process; process = null; destination = null; reader = null;
        if (old != null) {
            old.destroy();
            try { old.getInputStream().close(); } catch (IOException ignored) {}
        }
    }
}
