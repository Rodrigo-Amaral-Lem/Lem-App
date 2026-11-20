// SPDX-License-Identifier: GPL-3.0-only
package pt.lem.app;

import android.Manifest;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.app.job.JobService;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.PersistableBundle;
import android.os.SystemClock;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/** Poll the durable per-user inbox even after Android has reclaimed the Qt activity. */
public final class WorkOrderJob extends JobService {
    private static final int PERIODIC = 2710, IMMEDIATE = 2711;
    private static final String CHANNEL = "work-orders";
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private final java.util.Map<Integer, Future<?>> active = new java.util.HashMap<>();

    public static void configure(Context context, String root, boolean enabled) {
        JobScheduler jobs = context.getSystemService(JobScheduler.class);
        if (!enabled) {
            jobs.cancel(IMMEDIATE);
            jobs.cancel(PERIODIC);
            NotificationManager manager = context.getSystemService(NotificationManager.class);
            for (android.service.notification.StatusBarNotification posted : manager.getActiveNotifications())
                if (CHANNEL.equals(posted.getNotification().getChannelId())) manager.cancel(posted.getTag(), posted.getId());
            return;
        }
        PersistableBundle extras = new PersistableBundle();
        extras.putString("root", root);
        ComponentName service = new ComponentName(context, WorkOrderJob.class);
        jobs.schedule(new JobInfo.Builder(PERIODIC, service).setExtras(extras)
                .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY).setPersisted(true)
                .setPeriodic(15 * 60_000L).build());
        jobs.schedule(new JobInfo.Builder(IMMEDIATE, service).setExtras(extras)
                .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY).setOverrideDeadline(5_000).build());
    }

    @Override public boolean onStartJob(JobParameters params) {
        active.put(params.getJobId(), executor.submit(() -> {
            boolean retry = false;
            try {
                poll(new File(params.getExtras().getString("root")));
            } catch (Exception error) {
                retry = true;
            }
            if (!Thread.currentThread().isInterrupted()) jobFinished(params, retry);
        }));
        return true;
    }

    @Override public boolean onStopJob(JobParameters params) {
        Future<?> job = active.remove(params.getJobId());
        if (job != null) job.cancel(true);
        return true;
    }

    @Override public void onDestroy() {
        executor.shutdownNow();
        super.onDestroy();
    }

    private JSONObject session(File root) throws Exception {
        File sealed = new File(root, ".session.dat"), identityFile = new File(root, "last-user.json");
        if (!sealed.isFile() || sealed.length() > 16384 || !identityFile.isFile() || identityFile.length() > 16384)
            return null;
        byte[] bytes = SessionCrypto.unseal(Files.readAllBytes(sealed.toPath()));
        if (bytes == null) return null;
        JSONObject record = new JSONObject(new String(bytes, StandardCharsets.UTF_8));
        JSONObject identity = new JSONObject(new String(Files.readAllBytes(identityFile.toPath()), StandardCharsets.UTF_8));
        StringBuilder digest = new StringBuilder();
        for (byte b : MessageDigest.getInstance("SHA-256").digest(record.getString("token").getBytes(StandardCharsets.UTF_8)))
            digest.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
        return record.getString("server").equals(identity.optString("server")) &&
                record.getString("user_id").equals(identity.getJSONObject("user").optString("id")) &&
                digest.toString().equals(identity.optString("session_digest")) ? record : null;
    }

    private String request(JSONObject session, String method, String path) throws Exception {
        URL url = new URL(session.getString("server") + "/api/work-order-notices/" + path);
        boolean loopback = "localhost".equals(url.getHost()) || "127.0.0.1".equals(url.getHost());
        if (url.getUserInfo() != null || !("https".equals(url.getProtocol()) ||
                ("http".equals(url.getProtocol()) && loopback))) throw new java.io.IOException();
        HttpURLConnection http = (HttpURLConnection) url.openConnection();
        try {
            http.setInstanceFollowRedirects(false);
            http.setConnectTimeout(8000);
            http.setReadTimeout(10000);
            http.setRequestMethod(method);
            http.setRequestProperty("Authorization", "Bearer " + session.getString("token"));
            http.setRequestProperty("X-Lem-Client-Version", getPackageManager().getPackageInfo(getPackageName(), 0).versionName);
            http.setRequestProperty("X-Lem-Client-Platform", "android");
            if (http.getResponseCode() != 200) throw new java.io.IOException();
            try (InputStream input = http.getInputStream(); ByteArrayOutputStream output = new ByteArrayOutputStream()) {
                byte[] block = new byte[4096];
                int length;
                while ((length = input.read(block)) != -1) {
                    if (output.size() + length > 512 * 1024) throw new java.io.IOException();
                    output.write(block, 0, length);
                }
                return output.toString("UTF-8");
            }
        } finally {
            http.disconnect();
        }
    }

    private void poll(File root) throws Exception {
        JSONObject record = session(root);
        if (record == null) return;
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(new NotificationChannel(CHANNEL, "Ordens de trabalho", NotificationManager.IMPORTANCE_DEFAULT));
        if (!manager.areNotificationsEnabled() ||
                manager.getNotificationChannel(CHANNEL).getImportance() == NotificationManager.IMPORTANCE_NONE ||
                (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)) return;
        android.content.SharedPreferences prefs = getSharedPreferences(CHANNEL, MODE_PRIVATE);
        String device = prefs.getString("device", "");
        if (device.isEmpty()) {
            device = UUID.randomUUID().toString();
            if (!prefs.edit().putString("device", device).commit()) return;
        }
        long sent = SystemClock.elapsedRealtime();
        JSONArray notices = new JSONArray(request(record, "GET", device));
        Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        PendingIntent open = PendingIntent.getActivity(this, 0, launch, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        for (int n = 0; n < notices.length() && !Thread.currentThread().isInterrupted(); ++n) {
            JSONObject event = notices.getJSONObject(n);
            JSONObject current = session(root);
            if (current == null || !record.getString("token").equals(current.getString("token"))) return;
            // Subtract the entire round trip using a monotonic clock. Device time zones and
            // incorrect handset clocks cannot push a start notice past the server's 20:00 cutoff.
            if ("start".equals(event.getString("kind")) && SystemClock.elapsedRealtime() - sent >= event.optLong("valid_ms")) continue;
            String id = UUID.fromString(event.getString("id")).toString();
            manager.notify(id, 0, new Notification.Builder(this, CHANNEL)
                    .setSmallIcon(android.R.drawable.ic_dialog_info).setContentTitle("Ordens de trabalho")
                    .setContentText(event.getString("message")).setStyle(new Notification.BigTextStyle().bigText(event.getString("message")))
                    .setContentIntent(open).setAutoCancel(true).setOnlyAlertOnce(true).build());
            request(record, "PUT", device + "/" + id);
        }
    }
}
