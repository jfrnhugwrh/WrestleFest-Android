package com.mfm.wrestlefest;

import android.app.AlertDialog;
import android.content.Context;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;

import com.google.androidgamesdk.GameActivity;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

public final class MainActivity extends GameActivity {
    private static final String TAG = "WrestleFest";
    private static final String PACKAGE_NAME = "com.mfm.wrestlefest";

    static {
        System.loadLibrary("wrestlefest");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        CrashLogHandler.collectPreviousCrashReports(this);
        CrashLogHandler.install(this);
        super.onCreate(savedInstanceState);
        if (findObbFile().isEmpty()) {
            showMissingObbDialog();
        }
    }

    private void showMissingObbDialog() {
        new AlertDialog.Builder(this)
                .setTitle("Game data not found")
                .setMessage(
                        "The required OBB expansion file was not found. Copy the .obb file "
                                + "prepared from your own copy of the game into either "
                                + "obb/com.mfm.wrestlefest/ or "
                                + "Android/obb/com.mfm.wrestlefest/ on shared storage, "
                                + "then restart WrestleFest.")
                .setPositiveButton(android.R.string.ok, null)
                .show();
    }

    /**
     * Locate the OBB expansion file across standard Android expansion directories,
     * including obb/com.mfm.wrestlefest and Android/obb/com.mfm.wrestlefest.
     */
    public String findObbFile() {
        return findObbFile(this);
    }

    public static String findObbFile(Context context) {
        List<File> searchDirs = new ArrayList<>();

        // 1. Android standard getObbDirs()
        File[] obbDirs = context.getObbDirs();
        if (obbDirs != null) {
            for (File dir : obbDirs) {
                if (dir != null) {
                    searchDirs.add(dir);
                }
            }
        }

        // 2. External storage obb/com.mfm.wrestlefest and Android/obb/com.mfm.wrestlefest
        try {
            File extStorage = Environment.getExternalStorageDirectory();
            if (extStorage != null) {
                searchDirs.add(new File(extStorage, "Android/obb/" + PACKAGE_NAME));
                searchDirs.add(new File(extStorage, "obb/" + PACKAGE_NAME));
            }
        } catch (Exception ignored) {
        }

        // 3. Fallback explicit common storage paths
        searchDirs.add(new File("/sdcard/Android/obb/" + PACKAGE_NAME));
        searchDirs.add(new File("/sdcard/obb/" + PACKAGE_NAME));
        searchDirs.add(new File("/storage/emulated/0/Android/obb/" + PACKAGE_NAME));
        searchDirs.add(new File("/storage/emulated/0/obb/" + PACKAGE_NAME));

        // 4. App-specific storage
        try {
            File extFiles = context.getExternalFilesDir(null);
            if (extFiles != null) {
                searchDirs.add(new File(extFiles, "obb/" + PACKAGE_NAME));
            }
            File intFiles = context.getFilesDir();
            if (intFiles != null) {
                searchDirs.add(new File(intFiles, "obb/" + PACKAGE_NAME));
            }
        } catch (Exception ignored) {
        }

        for (File dir : searchDirs) {
            if (dir.exists() && dir.isDirectory()) {
                // Check primary expected file name
                File primary = new File(dir, "main.1." + PACKAGE_NAME + ".obb");
                if (primary.isFile() && primary.canRead()) {
                    Log.i(TAG, "Located primary OBB: " + primary.getAbsolutePath());
                    return primary.getAbsolutePath();
                }

                // Check any *.obb file in the directory
                File[] files = dir.listFiles();
                if (files != null) {
                    for (File f : files) {
                        if (f.isFile() && f.getName().toLowerCase().endsWith(".obb") && f.canRead()) {
                            Log.i(TAG, "Located matching OBB: " + f.getAbsolutePath());
                            return f.getAbsolutePath();
                        }
                    }
                }
            }
        }

        Log.i(TAG, "No OBB file found in search directories; falling back to APK assets");
        return "";
    }
}
