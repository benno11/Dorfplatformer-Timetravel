package com.Benno111.dorfplatformertimetravel;

import android.os.Bundle;
import android.content.pm.ActivityInfo;
import android.view.GestureDetector;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

public class MainActivity extends SDLActivity {
    private GestureDetector gestureDetector;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        enterImmersiveFullscreen();
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        setupTouchDebuggerToggle();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            enterImmersiveFullscreen();
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
        }
    }

    private void enterImmersiveFullscreen() {
        View decorView = getWindow().getDecorView();
        decorView.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
        );
        decorView.setOnSystemUiVisibilityChangeListener(visibility -> enterImmersiveFullscreen());
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
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
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
                "sdl3",
                "sdl3_image",
                "sdl3_ttf",
                "sdl3_mixer",
                "platformer"
        };
    }
}
