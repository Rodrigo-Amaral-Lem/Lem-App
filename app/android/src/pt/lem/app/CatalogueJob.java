// SPDX-License-Identifier: GPL-3.0-only
package pt.lem.app;

import android.app.job.JobInfo;
import android.app.job.JobParameters;
import android.app.job.JobScheduler;
import android.app.job.JobService;
import android.content.ComponentName;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

/** The scheduler grants CPU/network time to the same controller that owns the cache. */
public final class CatalogueJob extends JobService {
    private static final int JOB = 2701;
    private static final Handler main = new Handler(Looper.getMainLooper());
    private static boolean liveSession;
    private static CatalogueJob running;
    private static int generation;
    private JobParameters parameters;

    public static void setEnabled(Context context, boolean enabled) {
        main.post(() -> {
            liveSession = enabled;
            JobScheduler scheduler = context.getSystemService(JobScheduler.class);
            if (!enabled) {
                scheduler.cancel(JOB);
                return;
            }
            JobInfo info = new JobInfo.Builder(JOB, new ComponentName(context, CatalogueJob.class))
                .setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY)
                .setPeriodic(15 * 60 * 1000L).build();
            if (scheduler.schedule(info) != JobScheduler.RESULT_SUCCESS)
                Log.w("Lem", "Android adiou a atualização periódica.");
        });
    }

    private static native void jobChanged(int ticket, boolean started);

    @Override public boolean onStartJob(JobParameters params) {
        // Android may have reclaimed the Qt process. Only the live session can
        // write this cache. Authentication on reopening schedules a fresh job.
        if (!liveSession) {
            getSystemService(JobScheduler.class).cancel(JOB);
            return false;
        }
        running = this;
        parameters = params;
        jobChanged(++generation, true);
        return true;
    }

    @Override public boolean onStopJob(JobParameters params) {
        if (running == this) {
            running = null;
            jobChanged(generation, false);
        }
        return false;
    }

    public static void finish(int ticket) {
        main.post(() -> {
            if (running != null && generation == ticket) {
                running.jobFinished(running.parameters, false);
                running = null;
            }
        });
    }
}
