// SPDX-License-Identifier: GPL-3.0-only
package pt.lem.app;

import android.app.Notification;
import android.app.Activity;
import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Build;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;

/** Protect a transfer started while the activity is visible, until the Qt queue drains. */
public final class TransferService extends Service {
    private static final String CHANNEL = "lem-transfers";
    private static final Handler main = new Handler(Looper.getMainLooper());
    private static boolean wanted;
    private static TransferService promoted;
    private PowerManager.WakeLock wakeLock;
    private Notification pendingNotification;

    public static void permissionChanged() {
        main.post(() -> {
            // Android can discard a foreground notification posted before the
            // user grants permission during the first download. Publish it again
            // when the permission dialog returns to the running activity.
            TransferService service = promoted;
            if (wanted && service != null && service.pendingNotification != null &&
                service.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                    == PackageManager.PERMISSION_GRANTED) {
                service.startForeground(2702, service.pendingNotification);
                service.pendingNotification = null;
            }
        });
    }

    public static void offerNotification(Context context) {
        if (Build.VERSION.SDK_INT < 33 || !(context instanceof Activity)) return;
        main.post(() -> {
            Activity activity = (Activity) context;
            if (activity.isFinishing() || activity.isDestroyed() ||
                activity.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                    == PackageManager.PERMISSION_GRANTED) return;
            // A declined notification does not prevent the download. Offer it
            // once. Later changes belong to the application's system settings.
            android.content.SharedPreferences prefs =
                activity.getSharedPreferences("transfers", MODE_PRIVATE);
            if (prefs.getBoolean("offered", false)) return;
            prefs.edit().putBoolean("offered", true).apply();
            activity.requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 2702);
        });
    }

    public static void setNeeded(Context context, boolean needed) {
        main.post(() -> {
            wanted = needed;
            if (needed) {
                try {
                    context.startForegroundService(new Intent(context, TransferService.class));
                } catch (IllegalStateException | SecurityException e) {
                    // The activity can lose visibility before Android handles the start.
                    Log.w("Lem", "Android não autorizou o serviço de transferência.", e);
                }
            } else if (promoted != null) {
                promoted.stopSelf();
            }
        });
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        getSystemService(NotificationManager.class).createNotificationChannel(
            new NotificationChannel(CHANNEL, "Sincronização", NotificationManager.IMPORTANCE_LOW));
        Intent open = getPackageManager().getLaunchIntentForPackage(getPackageName());
        PendingIntent show = PendingIntent.getActivity(this, 0, open,
            PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder notification = new Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setContentTitle("Lem").setContentText("A sincronizar dados em segundo plano")
            .setContentIntent(show).setOnlyAlertOnce(true).setOngoing(true);
        if (Build.VERSION.SDK_INT >= 31)
            notification.setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE);
        // Always promote a pending start, even if its request has already completed.
        Notification notice = notification.build();
        pendingNotification = Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
            ? notice : null;
        startForeground(2702, notice);
        promoted = this;
        if (wanted) {
            if (wakeLock == null) {
                wakeLock = getSystemService(PowerManager.class).newWakeLock(
                    PowerManager.PARTIAL_WAKE_LOCK, "Lem:transfer");
                wakeLock.setReferenceCounted(false);
            }
            wakeLock.acquire(30 * 60 * 1000L);
        } else {
            stopSelf(startId);
        }
        return START_NOT_STICKY;
    }

    @Override public void onTimeout(int startId, int type) { stopSelf(startId); }

    @Override public void onDestroy() {
        if (promoted == this) promoted = null;
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        stopForeground(STOP_FOREGROUND_REMOVE);
        super.onDestroy();
    }

    @Override public IBinder onBind(Intent intent) { return null; }
}
