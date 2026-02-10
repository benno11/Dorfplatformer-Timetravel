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

import org.libsdl.app.SDLActivity;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

public class MainActivity extends SDLActivity {
    private static final String TAG = "MainActivity";
    private GestureDetector gestureDetector;
    private final View.OnSystemUiVisibilityChangeListener systemUiListener =
            visibility -> applyImmersiveMode();

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

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        try {
            Class.forName("org.libsdl.app.SDLInputConnection");
            Log.i(TAG, "SDLInputConnection class visible before SDL init");
        } catch (Throwable t) {
            Log.e(TAG, "SDLInputConnection class NOT visible before SDL init", t);
        }
        super.onCreate(savedInstanceState);
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
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
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
        return new String[] {
                "SDL3",
                "SDL3_image",
                "SDL3_ttf",
                "SDL3_mixer",
                "platformer"
        };
    }
}
