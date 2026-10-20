package com.mfm.wrestlefest;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;
import android.os.Process;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

final class CrashLogHandler implements Thread.UncaughtExceptionHandler {
    private static final String TAG = "WrestleFestCrash";
    private static final String FILE_NAME = "crash-logcat.txt";
    private static final String PREFERENCES_NAME = "crash-logcat";
    private static final String LAST_EXIT_TIMESTAMP_KEY = "last_exit_timestamp";
    private static boolean installed;

    private final Context context;
    private final Thread.UncaughtExceptionHandler previousHandler;

    private CrashLogHandler(Context context, Thread.UncaughtExceptionHandler previousHandler) {
        this.context = context;
        this.previousHandler = previousHandler;
    }

    static synchronized void install(Context context) {
        if (installed) {
            return;
        }
        Thread.setDefaultUncaughtExceptionHandler(
                new CrashLogHandler(
                        context.getApplicationContext(),
                        Thread.getDefaultUncaughtExceptionHandler()));
        installed = true;
    }

    static void collectPreviousCrashReports(Context context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) {
            return;
        }

        ActivityManager activityManager =
                (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        if (activityManager == null) {
            return;
        }

        SharedPreferences preferences =
                context.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE);
        long lastReportedTimestamp = preferences.getLong(LAST_EXIT_TIMESTAMP_KEY, 0L);
        long newestReportedTimestamp = lastReportedTimestamp;
        List<ApplicationExitInfo> exits =
                activityManager.getHistoricalProcessExitReasons(
                        context.getPackageName(), 0, 10);
        for (ApplicationExitInfo exit : exits) {
            if (exit.getTimestamp() <= lastReportedTimestamp || !isCrash(exit.getReason())) {
                continue;
            }
            try (InputStream trace = exit.getTraceInputStream()) {
                appendSystemCrashReport(context, exit, trace);
                newestReportedTimestamp = Math.max(newestReportedTimestamp, exit.getTimestamp());
            } catch (IOException | RuntimeException error) {
                Log.e(TAG, "Could not save previous system crash report", error);
            }
        }
        if (newestReportedTimestamp > lastReportedTimestamp) {
            preferences.edit()
                    .putLong(LAST_EXIT_TIMESTAMP_KEY, newestReportedTimestamp)
                    .apply();
        }
    }

    @Override
    public void uncaughtException(Thread thread, Throwable error) {
        try {
            appendCrashReport(thread, error);
        } catch (IOException | RuntimeException writeError) {
            Log.e(TAG, "Could not write crash report", writeError);
        }

        if (previousHandler != null) {
            previousHandler.uncaughtException(thread, error);
        } else {
            Process.killProcess(Process.myPid());
            System.exit(10);
        }
    }

    private static boolean isCrash(int reason) {
        return reason == ApplicationExitInfo.REASON_CRASH
                || reason == ApplicationExitInfo.REASON_CRASH_NATIVE
                || reason == ApplicationExitInfo.REASON_ANR;
    }

    private static void appendSystemCrashReport(
            Context context, ApplicationExitInfo exit, InputStream trace) throws IOException {
        File report = new File(context.getFilesDir(), FILE_NAME);
        try (FileOutputStream output = new FileOutputStream(report, true)) {
            String reportText =
                    "\n\n----- SYSTEM-REPORTED PROCESS FAILURE -----\n"
                            + "Timestamp: " + exit.getTimestamp() + "\n"
                            + "Process ID: " + exit.getPid() + "\n"
                            + "Reason: " + exit.getReason() + "\n"
                            + "Description: " + exit.getDescription() + "\n"
                            + "Report file: " + report.getAbsolutePath() + "\n\n";
            output.write(reportText.getBytes(StandardCharsets.UTF_8));
            if (trace == null) {
                output.write("(No system trace was available.)\n"
                        .getBytes(StandardCharsets.UTF_8));
            } else {
                byte[] buffer = new byte[8192];
                int bytesRead;
                while ((bytesRead = trace.read(buffer)) != -1) {
                    output.write(buffer, 0, bytesRead);
                }
            }
            output.write("\n----- END SYSTEM-REPORTED PROCESS FAILURE -----\n"
                    .getBytes(StandardCharsets.UTF_8));
            output.getFD().sync();
        }
    }

    private void appendCrashReport(Thread thread, Throwable error) throws IOException {
        File report = new File(context.getFilesDir(), FILE_NAME);
        try (FileOutputStream output = new FileOutputStream(report, true)) {
            StringWriter stackTrace = new StringWriter();
            error.printStackTrace(new PrintWriter(stackTrace));

            SimpleDateFormat timestampFormat =
                    new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'", Locale.US);
            timestampFormat.setTimeZone(TimeZone.getTimeZone("UTC"));
            String reportText =
                    "\n\n----- FATAL EXCEPTION -----\n"
                            + "Timestamp: " + timestampFormat.format(new Date()) + "\n"
                            + "Process ID: " + Process.myPid() + "\n"
                            + "Thread: " + thread.getName() + " (id=" + thread.getId() + ")\n"
                            + "Package: " + context.getPackageName() + "\n"
                            + "Android: " + Build.VERSION.RELEASE
                            + " (API " + Build.VERSION.SDK_INT + ")\n"
                            + "Device: " + Build.MANUFACTURER + " " + Build.MODEL + "\n"
                            + "Build fingerprint: " + Build.FINGERPRINT + "\n\n"
                            + "Report file: " + report.getAbsolutePath() + "\n\n"
                            + stackTrace
                            + "----- END FATAL EXCEPTION -----\n";
            output.write(reportText.getBytes(StandardCharsets.UTF_8));
            output.getFD().sync();
        }
    }
}
