import com.Benno111.dorfplatformertimetravel.LogcatRecorder;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class LogcatRecorderTest {
    static class Child extends Process {
        final PipedInputStream input = new PipedInputStream();
        final PipedOutputStream writer;
        boolean destroyed;
        Child() throws IOException { writer = new PipedOutputStream(input); }
        public InputStream getInputStream() { return input; }
        public InputStream getErrorStream() { return new ByteArrayInputStream(new byte[0]); }
        public OutputStream getOutputStream() { return new ByteArrayOutputStream(); }
        public int waitFor() { return 0; }
        public int exitValue() { if (!destroyed) throw new IllegalThreadStateException(); return 0; }
        public void destroy() { destroyed = true; try { writer.close(); } catch (IOException ignored) {} }
    }
    static void awaitText(File file, String expected) throws Exception {
        long deadline = System.nanoTime() + 2000000000L;
        while (System.nanoTime() < deadline) {
            if (file.isFile() && new String(Files.readAllBytes(file.toPath()), "UTF-8").contains(expected)) return;
            Thread.sleep(10);
        }
        throw new AssertionError("Missing saved logcat output: " + expected);
    }
    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("df-logcat-test").toFile();
        List<Child> children = new ArrayList<>();
        LogcatRecorder recorder = new LogcatRecorder(pid -> {
            if (!pid.equals("1234")) throw new AssertionError("Must use the application's PID");
            Child child = new Child(); children.add(child); return child;
        });
        File first = new File(root, "logs/logcat.txt");
        if (!recorder.setEnabled(true, first, "1234")) throw new AssertionError();
        if (!recorder.setEnabled(true, first, "1234") || children.size() != 1) throw new AssertionError("Duplicate child");
        children.get(0).writer.write("first log line\n".getBytes("UTF-8")); children.get(0).writer.flush();
        awaitText(first, "first log line");
        File second = new File(root, "logs/next.txt");
        if (!recorder.setEnabled(true, second, "1234") || !children.get(0).destroyed) throw new AssertionError("Old capture must stop");
        children.get(1).writer.write("second log line\n".getBytes("UTF-8")); children.get(1).writer.flush();
        awaitText(second, "second log line");
        if (!recorder.setEnabled(false, second, "1234") || !children.get(1).destroyed) throw new AssertionError("Disable must destroy child");
        recorder.stop();
        LogcatRecorder failing = new LogcatRecorder(pid -> { throw new IOException("unavailable"); });
        if (failing.setEnabled(true, first, "1234")) throw new AssertionError("Report launch failure");
        // All streams are closed; remove only this test's own temporary directory.
        Thread.sleep(20);
        try (java.util.stream.Stream<Path> files = Files.walk(root.toPath())) {
            files.sorted(Comparator.reverseOrder()).forEach(path -> { try { Files.delete(path); } catch(IOException e) { throw new RuntimeException(e); } });
        }
        System.out.println("Logcat writer lifecycle, PID filter and file capture checks passed");
    }
}
