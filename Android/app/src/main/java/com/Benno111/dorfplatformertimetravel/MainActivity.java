package com.Benno111.dorfplatformertimetravel;

import android.os.Bundle;
import android.content.pm.ActivityInfo;
import android.util.Log;
import android.view.GestureDetector;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.os.Build;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.inputmethod.InputMethodManager;
import android.text.InputType;
import android.content.Context;

import org.libsdl.app.SDLActivity;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

public class MainActivity extends SDLActivity {
    private static final int NATIVE_MOD_IMPORT = 0x4d31;
    private String nativeModImportRoot;

    // Called by the native Mods menu; Android's document picker grants access
    // to the selected ZIP without broad storage permissions.
    public void importNativeMod(String root) {
        nativeModImportRoot = root;
        runOnUiThread(() -> {
            android.content.Intent intent = new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(android.content.Intent.CATEGORY_OPENABLE);
            intent.setType("application/zip");
            intent.putExtra(android.content.Intent.EXTRA_MIME_TYPES, new String[]{"application/zip", "application/octet-stream", "application/x-zip-compressed"});
            try { startActivityForResult(intent, NATIVE_MOD_IMPORT); }
            catch (Exception error) { android.widget.Toast.makeText(this, "Could not open mod picker", android.widget.Toast.LENGTH_LONG).show(); }
        });
    }

    @Override
    protected void onActivityResult(int request, int result, android.content.Intent data) {
        if (request != NATIVE_MOD_IMPORT) { super.onActivityResult(request, result, data); return; }
        if (result != RESULT_OK || data == null || data.getData() == null || nativeModImportRoot == null) return;
        final java.io.File root = new java.io.File(nativeModImportRoot);
        final android.net.Uri uri = data.getData();
        new Thread(() -> {
            java.io.File staging = new java.io.File(root, ".import-" + java.util.UUID.randomUUID());
            String message;
            try {
                if (!staging.mkdirs()) throw new java.io.IOException("Could not create mod staging directory");
                java.io.InputStream input = getContentResolver().openInputStream(uri);
                if (input == null) throw new java.io.IOException("Could not read selected mod");
                NativeModArchive.extract(input, staging);
                java.io.File content = staging;
                java.io.File manifest = new java.io.File(content, "mod.json");
                if (!manifest.isFile()) {
                    java.io.File[] children = staging.listFiles();
                    if (children != null && children.length == 1 && children[0].isDirectory()) {
                        content = children[0]; manifest = new java.io.File(content, "mod.json");
                    }
                }
                if (!manifest.isFile() || manifest.length() > 65536) throw new java.io.IOException("Missing or oversized mod.json");
                java.io.ByteArrayOutputStream text = new java.io.ByteArrayOutputStream();
                try (java.io.InputStream stream = new java.io.FileInputStream(manifest)) {
                    byte[] buffer = new byte[4096]; int read;
                    while ((read = stream.read(buffer)) != -1) text.write(buffer, 0, read);
                }
                org.json.JSONObject json = new org.json.JSONObject(text.toString("UTF-8"));
                String id = json.getString("id");
                if (!id.matches("[A-Za-z0-9_-][A-Za-z0-9._-]{0,79}")) throw new java.io.IOException("Invalid mod ID");
                json.getString("version"); json.getJSONObject("api"); json.getJSONObject("libraries");
                java.io.File destination = new java.io.File(root, id);
                java.io.File backup = new java.io.File(root, ".backup-" + id + "-" + java.util.UUID.randomUUID());
                boolean replaced = destination.exists();
                if (replaced && !destination.renameTo(backup)) throw new java.io.IOException("Could not back up installed mod");
                if (!content.renameTo(destination)) {
                    if (replaced) backup.renameTo(destination);
                    throw new java.io.IOException("Could not install mod");
                }
                message = "Mod installed. Scan for mods, then restart the game.";
            } catch (Exception error) { message = "Mod import failed: " + error.getMessage(); }
            finally { deleteModStaging(staging); }
            final String notice = message;
            runOnUiThread(() -> android.widget.Toast.makeText(this, notice, android.widget.Toast.LENGTH_LONG).show());
        }, "NativeModImport").start();
    }

    private static void deleteModStaging(java.io.File file) {
        if (file.isDirectory()) {
            java.io.File[] children = file.listFiles();
            if (children != null) for (java.io.File child : children) deleteModStaging(child);
        }
        file.delete();
    }

    private static final String TAG = "MainActivity";
    private static volatile boolean softKeyboardActive = false;
    private static volatile boolean preloadAttempted = false;

    private static synchronized void preloadNativeLibraries() {
        if (preloadAttempted) return;
        preloadAttempted = true;
        try {
            System.loadLibrary("platformer");
        } catch (Throwable t) {
            Log.e(TAG, "Failed to preload platformer library", t);
        }
    }

    static {
        preloadNativeLibraries();
    }
    private GestureDetector gestureDetector;
    private final View.OnSystemUiVisibilityChangeListener systemUiListener =
            visibility -> applyImmersiveMode();

    // Called by the native game to enable debugger behavior in debug APKs.
    public static boolean isDebugBuild() {
        return BuildConfig.DEBUG;
    }

    public static Context getAppContext() {
        final SDLActivity activity = mSingleton;
        return activity != null ? activity.getApplicationContext() : null;
    }

    public static String httpGet(String url, int timeoutMs) {
        HttpURLConnection conn = null;
        try {
            URL u = new URL(url);
            conn = (HttpURLConnection) u.openConnection();
            conn.setRequestMethod("GET");
            conn.setConnectTimeout(Math.max(1000, timeoutMs));
            conn.setReadTimeout(Math.max(1000, timeoutMs));
            conn.setInstanceFollowRedirects(true);
            conn.setRequestProperty("User-Agent", "DF-New/" + BuildConfig.VERSION_NAME + " (build/" + BuildConfig.VERSION_CODE + ")");
            int code = conn.getResponseCode();
            if (code < 200 || code >= 300) {
                Log.i(TAG, "NET: Java GET fail code=" + code + " url=" + url);
                return "";
            }
            BufferedInputStream in = new BufferedInputStream(conn.getInputStream());
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) != -1) {
                out.write(buf, 0, n);
            }
            in.close();
            byte[] bytes = out.toByteArray();
            Log.i(TAG, "NET: Java GET ok code=" + code + " bytes=" + bytes.length + " url=" + url);
            return new String(bytes, StandardCharsets.UTF_8);
        } catch (Throwable t) {
            Log.i(TAG, "NET: Java GET exception url=" + url, t);
            return "";
        } finally {
            if (conn != null) {
                try { conn.disconnect(); } catch (Throwable ignored) {}
            }
        }
    }

    private static String jsonEscape(String s) {
        if (s == null) return "";
        StringBuilder out = new StringBuilder(s.length() + 8);
        for (int i = 0; i < s.length(); i++) {
            char ch = s.charAt(i);
            switch (ch) {
                case '\\': out.append("\\\\"); break;
                case '"': out.append("\\\""); break;
                case '\b': out.append("\\b"); break;
                case '\f': out.append("\\f"); break;
                case '\n': out.append("\\n"); break;
                case '\r': out.append("\\r"); break;
                case '\t': out.append("\\t"); break;
                default:
                    if (ch < 0x20) {
                        out.append(String.format("\\u%04x", (int) ch));
                    } else {
                        out.append(ch);
                    }
                    break;
            }
        }
        return out.toString();
    }

    public static String gameServerSignIn(String serverBase, String email, String password, int timeoutMs) {
        HttpURLConnection conn = null;
        try {
            if (serverBase == null || serverBase.isEmpty()) return "";
            String safeEmail = email == null ? "" : email;
            String safePassword = password == null ? "" : password;
            final String url = serverBase.replaceAll("/+$", "") + "/api/auth/login";
            final String body = "{\"email\":\"" + jsonEscape(safeEmail) + "\",\"password\":\"" +
                    jsonEscape(safePassword) + "\",\"returnSecureToken\":true}";

            URL u = new URL(url);
            conn = (HttpURLConnection) u.openConnection();
            conn.setRequestMethod("POST");
            conn.setConnectTimeout(Math.max(1000, timeoutMs));
            conn.setReadTimeout(Math.max(1000, timeoutMs));
            conn.setInstanceFollowRedirects(false);
            conn.setDoOutput(true);
            conn.setRequestProperty("Content-Type", "application/json");
            conn.setRequestProperty("Accept", "application/json");
            conn.setRequestProperty("User-Agent", "DF-New/" + BuildConfig.VERSION_NAME + " (build/" + BuildConfig.VERSION_CODE + ")");
            java.io.OutputStream os = conn.getOutputStream();
            os.write(body.getBytes(StandardCharsets.UTF_8));
            os.flush();
            os.close();

            final int code = conn.getResponseCode();
            BufferedInputStream in;
            try {
                in = new BufferedInputStream(code >= 200 && code < 300 ? conn.getInputStream() : conn.getErrorStream());
            } catch (Throwable ignored) {
                in = null;
            }
            if (in == null) return "";
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) != -1) out.write(buf, 0, n);
            in.close();
            byte[] bytes = out.toByteArray();
            String resp = new String(bytes, StandardCharsets.UTF_8);
            Log.i(TAG, "ACCOUNT: Java signIn code=" + code + " bytes=" + bytes.length);
            return resp;
        } catch (Throwable t) {
            Log.i(TAG, "ACCOUNT: Java signIn exception", t);
            return "";
        } finally {
            if (conn != null) {
                try { conn.disconnect(); } catch (Throwable ignored) {}
            }
        }
    }

    public static String gameServerLookupAccount(String serverBase, String idToken, int timeoutMs) {
        return gameServerAccountRequest(serverBase, idToken, timeoutMs, "lookup");
    }

    public static String gameServerLogout(String serverBase, String idToken, int timeoutMs) {
        return gameServerAccountRequest(serverBase, idToken, timeoutMs, "logout");
    }

    private static String gameServerAccountRequest(String serverBase, String idToken, int timeoutMs, String action) {
        HttpURLConnection conn = null;
        try {
            if (serverBase == null || serverBase.isEmpty() || idToken == null || idToken.isEmpty()) return "";
            final String url = serverBase.replaceAll("/+$", "") + "/api/auth/" + action;
            final String body = "{\"idToken\":\"" + jsonEscape(idToken) + "\"}";
            URL u = new URL(url);
            conn = (HttpURLConnection) u.openConnection();
            conn.setRequestMethod("POST");
            conn.setConnectTimeout(Math.max(1000, timeoutMs));
            conn.setReadTimeout(Math.max(1000, timeoutMs));
            conn.setInstanceFollowRedirects(false);
            conn.setDoOutput(true);
            conn.setRequestProperty("Content-Type", "application/json");
            conn.setRequestProperty("Accept", "application/json");
            conn.setRequestProperty("User-Agent", "DF-New/" + BuildConfig.VERSION_NAME + " (build/" + BuildConfig.VERSION_CODE + ")");
            java.io.OutputStream os = conn.getOutputStream();
            os.write(body.getBytes(StandardCharsets.UTF_8));
            os.flush();
            os.close();
            final int code = conn.getResponseCode();
            BufferedInputStream in;
            try {
                in = new BufferedInputStream(code >= 200 && code < 300 ? conn.getInputStream() : conn.getErrorStream());
            } catch (Throwable ignored) {
                in = null;
            }
            if (in == null) return "";
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) != -1) out.write(buf, 0, n);
            in.close();
            return new String(out.toByteArray(), StandardCharsets.UTF_8);
        } catch (Throwable t) {
            Log.i(TAG, "ACCOUNT: Java lookup exception", t);
            return "";
        } finally {
            if (conn != null) {
                try { conn.disconnect(); } catch (Throwable ignored) {}
            }
        }
    }

    public static int gameServerUploadLevel(String url, String jsonBody, String token, int timeoutMs) {
        HttpURLConnection conn = null;
        try {
            if (url == null || url.isEmpty() || jsonBody == null) return -1;
            URL u = new URL(url);
            conn = (HttpURLConnection) u.openConnection();
            conn.setRequestMethod("PUT");
            conn.setRequestProperty("Authorization", "Bearer " + token);
            conn.setConnectTimeout(Math.max(1000, timeoutMs));
            conn.setReadTimeout(Math.max(1000, timeoutMs));
            conn.setInstanceFollowRedirects(false);
            conn.setDoOutput(true);
            conn.setRequestProperty("Content-Type", "application/json");
            conn.setRequestProperty("Accept", "application/json");
            conn.setRequestProperty("User-Agent", "DF-New/" + BuildConfig.VERSION_NAME + " (build/" + BuildConfig.VERSION_CODE + ")");
            java.io.OutputStream os = conn.getOutputStream();
            os.write(jsonBody.getBytes(StandardCharsets.UTF_8));
            os.flush();
            os.close();
            return conn.getResponseCode();
        } catch (Throwable t) {
            Log.i(TAG, "NET/UI: Java upload exception", t);
            return -1;
        } finally {
            if (conn != null) {
                try { conn.disconnect(); } catch (Throwable ignored) {}
            }
        }
    }

    public static boolean showSoftKeyboard(int x, int y, int w, int h) {
        try {
            final SDLActivity activity = mSingleton;
            if (activity == null) return false;
            softKeyboardActive = true;
            activity.runOnUiThread(() -> {
                try {
                    final int safeW = Math.max(1, w);
                    final int safeH = Math.max(1, h);
                    SDLActivity.showTextInput(
                            InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS,
                            x, y, safeW, safeH);
                    // SDLActivity.showTextInput posts internally; wait until mTextEdit exists.
                    activity.getWindow().getDecorView().postDelayed(
                            () -> requestImeForSdlEdit(activity, 0), 50L);
                } catch (Throwable t) {
                    Log.i(TAG, "showSoftKeyboard ui failed", t);
                }
            });
            return true;
        } catch (Throwable t) {
            Log.i(TAG, "showSoftKeyboard failed", t);
            return false;
        }
    }

    public static void hideSoftKeyboard() {
        try {
            final SDLActivity activity = mSingleton;
            if (activity == null) return;
            softKeyboardActive = false;
            activity.runOnUiThread(() -> {
                try {
                    View focus = mTextEdit != null ? mTextEdit : activity.getCurrentFocus();
                    if (focus == null) focus = activity.getWindow().getDecorView();
                    InputMethodManager imm = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
                    if (imm != null && focus != null) {
                        imm.hideSoftInputFromWindow(focus.getWindowToken(), 0);
                        focus.clearFocus();
                    }
                } catch (Throwable t) {
                    Log.i(TAG, "hideSoftKeyboard ui failed", t);
                }
            });
        } catch (Throwable t) {
            Log.i(TAG, "hideSoftKeyboard failed", t);
        }
    }

    private static void requestImeForSdlEdit(SDLActivity activity, int attempt) {
        if (activity == null) return;
        final View root = activity.getWindow().getDecorView();
        final View focus = mTextEdit;
        if (focus == null) {
            if (attempt < 8) {
                root.postDelayed(() -> requestImeForSdlEdit(activity, attempt + 1), 40L);
            } else {
                Log.i(TAG, "showSoftKeyboard: SDL text edit view not ready");
            }
            return;
        }
        try {
            InputMethodManager imm = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
            focus.setFocusable(true);
            focus.setFocusableInTouchMode(true);
            focus.requestFocusFromTouch();
            focus.requestFocus();
            if (imm != null) {
                imm.restartInput(focus);
                if (!imm.showSoftInput(focus, InputMethodManager.SHOW_IMPLICIT)) {
                    focus.post(() -> {
                        try {
                            if (!imm.showSoftInput(focus, InputMethodManager.SHOW_IMPLICIT)) {
                                imm.toggleSoftInput(InputMethodManager.SHOW_IMPLICIT, 0);
                            }
                        } catch (Throwable ignored) {}
                    });
                }
            }
        } catch (Throwable t) {
            Log.i(TAG, "showSoftKeyboard requestImeForSdlEdit failed", t);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        preloadNativeLibraries();
        // Guard against stale recreate flags before SDL native thread exists.
        if (mSDLThread == null) {
            mActivityCreated = false;
            mSDLMainFinished = false;
        }
        try {
            super.onCreate(savedInstanceState);
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "SDL onCreate failed; retrying after preload/reset", e);
            preloadNativeLibraries();
            if (mSDLThread == null) {
                mActivityCreated = false;
                mSDLMainFinished = false;
            }
            super.onCreate(savedInstanceState);
        }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(systemUiListener);
        applyImmersiveMode();
        setupTouchDebuggerToggle();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            applyImmersiveMode();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        applyImmersiveMode();
    }

    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        // Keep the game in landscape even when SDL requests broader orientation modes.
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    private void applyImmersiveMode() {
        if (softKeyboardActive) return;
        View decorView = getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = decorView.getWindowInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
            }
        } else {
            decorView.setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_FULLSCREEN
            );
        }
    }

    private void setupTouchDebuggerToggle() {
        final int tapWindowMs = 450;
        final int[] tapCount = {0};
        final long[] lastTapAt = {0L};
        final float topBand = 0.12f;
        final float rightBand = 0.88f;
        gestureDetector = new GestureDetector(this, new GestureDetector.SimpleOnGestureListener() {
            @Override
            public boolean onSingleTapUp(MotionEvent e) {
                if (e == null) return false;
                View decor = getWindow().getDecorView();
                int w = decor.getWidth();
                int h = decor.getHeight();
                if (w <= 0 || h <= 0) return false;
                boolean inTopRight = e.getX() >= (w * rightBand) && e.getY() <= (h * topBand);
                if (!inTopRight) {
                    tapCount[0] = 0;
                    return false;
                }
                long now = android.os.SystemClock.uptimeMillis();
                if (now - lastTapAt[0] <= tapWindowMs) {
                    tapCount[0]++;
                } else {
                    tapCount[0] = 1;
                }
                lastTapAt[0] = now;
                if (tapCount[0] >= 2) {
                    // Mirror desktop F5 debugger toggle for touch devices.
                    SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_F5);
                    SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_F5);
                    tapCount[0] = 0;
                    return true;
                }
                return false;
            }
        });
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent ev) {
        if (gestureDetector != null) {
            gestureDetector.onTouchEvent(ev);
        }
        return super.dispatchTouchEvent(ev);
    }

    @Override
    public void onBackPressed() {
        // Route Android back to in-game Escape handling.
        SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_ESCAPE);
        SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_ESCAPE);
        applyImmersiveMode();
    }

    @Override
    protected String getMainFunction() {
        return "main";
    }

    @Override
    protected String[] getLibraries() {
        // Android startup loads only the game library; SDL is embedded.
        return new String[] { "platformer" };
    }
}
