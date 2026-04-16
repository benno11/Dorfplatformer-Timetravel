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
            conn.setRequestProperty("User-Agent", "DF-New/1.0-android");
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

    public static String firebaseSignIn(String apiKey, String email, String password, int timeoutMs) {
        HttpURLConnection conn = null;
        try {
            if (apiKey == null || apiKey.isEmpty()) return "";
            String safeEmail = email == null ? "" : email;
            String safePassword = password == null ? "" : password;
            final String url = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" + apiKey;
            final String body = "{\"email\":\"" + jsonEscape(safeEmail) + "\",\"password\":\"" +
                    jsonEscape(safePassword) + "\",\"returnSecureToken\":true}";

            URL u = new URL(url);
            conn = (HttpURLConnection) u.openConnection();
            conn.setRequestMethod("POST");
            conn.setConnectTimeout(Math.max(1000, timeoutMs));
            conn.setReadTimeout(Math.max(1000, timeoutMs));
            conn.setDoOutput(true);
            conn.setRequestProperty("Content-Type", "application/json");
            conn.setRequestProperty("Accept", "application/json");
            conn.setRequestProperty("User-Agent", "DF-New/1.0-android");
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

    public static String firebaseLookupAccount(String apiKey, String idToken, int timeoutMs) {
        HttpURLConnection conn = null;
        try {
            if (apiKey == null || apiKey.isEmpty() || idToken == null || idToken.isEmpty()) return "";
            final String url = "https://identitytoolkit.googleapis.com/v1/accounts:lookup?key=" + apiKey;
            final String body = "{\"idToken\":\"" + jsonEscape(idToken) + "\"}";
            URL u = new URL(url);
            conn = (HttpURLConnection) u.openConnection();
            conn.setRequestMethod("POST");
            conn.setConnectTimeout(Math.max(1000, timeoutMs));
            conn.setReadTimeout(Math.max(1000, timeoutMs));
            conn.setDoOutput(true);
            conn.setRequestProperty("Content-Type", "application/json");
            conn.setRequestProperty("Accept", "application/json");
            conn.setRequestProperty("User-Agent", "DF-New/1.0-android");
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

    public static int firebaseUploadLevel(String url, String jsonBody, int timeoutMs) {
        HttpURLConnection conn = null;
        try {
            if (url == null || url.isEmpty() || jsonBody == null) return -1;
            URL u = new URL(url);
            conn = (HttpURLConnection) u.openConnection();
            conn.setRequestMethod("PUT");
            conn.setConnectTimeout(Math.max(1000, timeoutMs));
            conn.setReadTimeout(Math.max(1000, timeoutMs));
            conn.setDoOutput(true);
            conn.setRequestProperty("Content-Type", "application/json");
            conn.setRequestProperty("Accept", "application/json");
            conn.setRequestProperty("User-Agent", "DF-New/1.0-android");
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
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
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
