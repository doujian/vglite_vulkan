package com.example.vglite;

import android.app.NativeActivity;
import android.os.Bundle;
import android.view.WindowManager;

/**
 * Main Activity for VGLite Vulkan Android application.
 * Extends NativeActivity to enable native code rendering via ANativeWindow.
 */
public class MainActivity extends NativeActivity {
    
    static {
        // Load the native library containing VGLite Vulkan implementation
        System.loadLibrary("vglite_vulkan");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        // Keep the screen on while rendering
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        
        // Set full-screen immersive mode
        getWindow().setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN
        );
        
        // Hide navigation and status bars for immersive experience
        hideSystemUI();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemUI();
        }
    }

    /**
     * Hides system UI for immersive full-screen mode.
     */
    private void hideSystemUI() {
        getWindow().getDecorView().setSystemUiVisibility(
            android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
            | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
        );
    }

    /**
     * Native method called from C++ to get the native window.
     * The window pointer is used by Vulkan for rendering.
     * For NativeActivity, the window is accessed via native_app_glue in C++.
     */
    public long getNativeWindow() {
        // The native window handle is obtained via ANativeWindow in C++ code
        // through the android_app structure provided by native_app_glue
        return 0;
    }
}
