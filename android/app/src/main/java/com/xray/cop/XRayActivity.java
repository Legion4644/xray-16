package com.xray.cop;

import android.Manifest;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.Log;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/**
 * Android host for the standalone X-Ray engine.
 *
 * It reuses SDL's {@link SDLActivity} to provide the native window and thread,
 * loads the engine (libxr_3da.so) and shows an on-screen overlay with the tail
 * of the engine log, so the log can be read directly on the device.
 */
public class XRayActivity extends SDLActivity {
    private static final String TAG = "xray-cop";
    private static final int REQUEST_STORAGE = 1001;
    private static final long POLL_INTERVAL_MS = 1500;
    private static final String FALLBACK_LOG_DIR = "/sdcard/XRay/logs";

    private static native String nativeGetLogDir();

    private TextView mLogView;
    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private boolean mRequestedStorage = false;

    private final Runnable mLogPoll = new Runnable() {
        @Override
        public void run() {
            try {
                updateLogView();
            } catch (Throwable t) {
                Log.w(TAG, "log poll failed", t);
            }
            mHandler.postDelayed(this, POLL_INTERVAL_MS);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        installJavaCrashHandler();
        attachLogOverlay();
        ensureStorageAccess();
        mHandler.post(mLogPoll);
    }

    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint) {
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
    }

    @Override
    protected String[] getLibraries() {
        // SDL2 first (provides the JNI entry points), then the engine, whose
        // SDL_main is the actual entry point.
        return new String[] { "SDL2", "xr_3da" };
    }

    @Override
    protected String[] getArguments() {
        // Flush the engine log after every line so a crash never loses it.
        // Marco 3 uses the GLES stub renderer only: no game, no sound, no
        // console, no splash, no gamepad.
        return new String[] {
                "-force_flushlog",
                "-nogame",
                "-nosound",
                "-nosplash",
                "-no_gamepad",
                "-no_console",
                "-no_imgui"
        };
    }

    // ------------------------------------------------------------------
    // Storage permission
    // ------------------------------------------------------------------

    private boolean hasStorageAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        }
        return checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED;
    }

    private void ensureStorageAccess() {
        if (hasStorageAccess() || mRequestedStorage) {
            return;
        }
        mRequestedStorage = true;

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.parse("package:" + getPackageName()));
                startActivityForResult(intent, REQUEST_STORAGE);
            } catch (Exception e) {
                Log.w(TAG, "app-specific all-files intent failed, falling back", e);
                try {
                    startActivityForResult(
                            new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION),
                            REQUEST_STORAGE);
                } catch (Exception e2) {
                    Log.w(TAG, "cannot open all-files settings", e2);
                }
            }
        } else {
            requestPermissions(new String[] {
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE }, REQUEST_STORAGE);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_STORAGE && hasStorageAccess()) {
            recreate();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_STORAGE && hasStorageAccess()) {
            recreate();
        }
    }

    // ------------------------------------------------------------------
    // On-screen log overlay
    // ------------------------------------------------------------------

    private void attachLogOverlay() {
        if (mLogView != null) {
            return;
        }
        mLogView = new TextView(this);
        mLogView.setTextColor(Color.rgb(0, 255, 0));
        mLogView.setBackgroundColor(0x99000000);
        mLogView.setTextSize(9f);
        mLogView.setPadding(16, 16, 16, 16);
        mLogView.setText("XRay starting...");

        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        params.gravity = Gravity.TOP | Gravity.START;
        addContentView(mLogView, params);
    }

    private File getLogDir() {
        String dir = null;
        try {
            dir = nativeGetLogDir();
        } catch (Throwable t) {
            Log.w(TAG, "nativeGetLogDir failed: " + t);
        }
        if (dir == null || dir.isEmpty()) {
            dir = FALLBACK_LOG_DIR;
        }
        return new File(dir);
    }

    private void updateLogView() {
        if (mLogView == null) {
            return;
        }
        File dir = getLogDir();
        File newest = newestLog(dir);

        StringBuilder text = new StringBuilder();
        text.append("logs: ").append(dir.getAbsolutePath()).append('\n');
        if (!hasStorageAccess()) {
            text.append("! grant \"All files access\" and reopen the app\n");
        }
        if (newest == null) {
            text.append("(waiting for engine log...)");
        } else {
            text.append(newest.getName()).append('\n');
            text.append(tail(newest, 14));
        }
        mLogView.setText(text.toString());
    }

    private File newestLog(File dir) {
        File[] files = dir.listFiles();
        if (files == null || files.length == 0) {
            return null;
        }
        List<File> logs = new ArrayList<>();
        for (File file : files) {
            if (file.isFile() && file.getName().endsWith(".log")) {
                logs.add(file);
            }
        }
        if (logs.isEmpty()) {
            return null;
        }
        // A crash log always takes priority, otherwise show the freshest file.
        for (File file : logs) {
            if ("native_crash.log".equals(file.getName())) {
                return file;
            }
        }
        return Collections.max(logs, new Comparator<File>() {
            @Override
            public int compare(File a, File b) {
                return Long.compare(a.lastModified(), b.lastModified());
            }
        });
    }

    private String tail(File file, int maxLines) {
        List<String> lines = new ArrayList<>();
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(new FileInputStream(file)))) {
            String line;
            while ((line = reader.readLine()) != null) {
                lines.add(line);
                if (lines.size() > maxLines * 4) {
                    lines = new ArrayList<>(lines.subList(lines.size() - maxLines * 2, lines.size()));
                }
            }
        } catch (Exception e) {
            return "cannot read log: " + e;
        }
        int from = Math.max(0, lines.size() - maxLines);
        StringBuilder sb = new StringBuilder();
        for (int i = from; i < lines.size(); i++) {
            sb.append(lines.get(i)).append('\n');
        }
        return sb.toString();
    }

    // ------------------------------------------------------------------
    // Java crash log
    // ------------------------------------------------------------------

    private void installJavaCrashHandler() {
        final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread thread, Throwable throwable) {
                try {
                    File dir = getLogDir();
                    //noinspection ResultOfMethodCallIgnored
                    dir.mkdirs();
                    File crash = new File(dir, "java_crash.log");
                    try (PrintWriter writer = new PrintWriter(new OutputStreamWriter(
                            new FileOutputStream(crash, true)))) {
                        writer.println("===== JAVA CRASH " + new java.util.Date() + " =====");
                        writer.println("Thread: " + thread.getName());
                        throwable.printStackTrace(writer);
                        writer.println("===== END JAVA CRASH =====");
                    }
                } catch (Throwable ignored) {
                    // Nothing else we can do here.
                }
                if (previous != null) {
                    previous.uncaughtException(thread, throwable);
                }
            }
        });
    }
}
