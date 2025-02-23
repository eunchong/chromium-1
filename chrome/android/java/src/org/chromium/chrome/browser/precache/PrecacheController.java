// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.precache;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.SharedPreferences.Editor;
import android.net.ConnectivityManager;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.os.PowerManager.WakeLock;

import com.google.android.gms.common.ConnectionResult;
import com.google.android.gms.common.GoogleApiAvailability;
import com.google.android.gms.gcm.GcmNetworkManager;
import com.google.android.gms.gcm.OneoffTask;
import com.google.android.gms.gcm.PeriodicTask;

import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.base.VisibleForTesting;
import org.chromium.base.library_loader.LibraryLoader;
import org.chromium.base.metrics.RecordHistogram;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.chrome.browser.ChromeBackgroundService;
import org.chromium.chrome.browser.ChromeVersionInfo;
import org.chromium.chrome.browser.util.NonThreadSafe;
import org.chromium.components.precache.DeviceState;
import org.chromium.sync.ModelType;

import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.Collections;
import java.util.EnumSet;
import java.util.HashSet;
import java.util.Queue;
import java.util.Set;

/**
 * Singleton responsible for starting and stopping a precache session.
 * Precaching occurs only when the device is connected to power and an
 * un-metered network connection. It holds a wake lock while running. It stops
 * running when power or the un-metered network is disconnected,
 * MAX_PRECACHE_DURATION_SECONDS elapse, or there are no more resources to
 * precache.
 */
public class PrecacheController {
    private static final String TAG = "Precache";

    /**
     * ID of the periodic task. Used here and by
     * {@link ChromeBackgroundService} for dispatch.
     */
    public static final String PERIODIC_TASK_TAG = "precache";

    @VisibleForTesting
    static final String PREF_IS_PRECACHING_ENABLED = "precache.is_precaching_enabled";

    /**
     * ID of the continuation task. Used here and by
     * {@link ChromeBackgroundService} for dispatch.
     */
    public static final String CONTINUATION_TASK_TAG = "precache-continuation";

    static final int WAIT_UNTIL_NEXT_PRECACHE_SECONDS = 6 * 60 * 60;  // 6 hours.
    static final int COMPLETION_TASK_MIN_DELAY_SECONDS = 5 * 60; // 5 minutes.
    static final int COMPLETION_TASK_MAX_DELAY_SECONDS = 60 * 60; // 1 hour.
    static final int MAX_SYNC_SERVICE_INIT_TIMOUT_MS = 5 * 60 * 1000; // 5 minutes
    static final int MAX_PRECACHE_DURATION_SECONDS = 30 * 60;  // 30 minutes.
    static final Set<Integer> SYNC_SERVICE_CONFIGURED_DATATYPES =
            Collections.unmodifiableSet(new HashSet<Integer>(Arrays.asList(ModelType.SESSIONS)));

    /**
     * Singleton instance of the PrecacheController. PrecacheController is a
     * singleton so that there is a single handle by which to determine if
     * precaching is underway, and to cancel it if necessary.
     */
    private static PrecacheController sInstance;

    /**
     * The default task scheduler. Overridden for tests.
     */
    private static PrecacheTaskScheduler sTaskScheduler = new PrecacheTaskScheduler();

    /**
     * Listener for syncservice backend.
     */
    SyncServiceInitializedNotifier mSyncServiceNotifier;

    /** True if a precache session is in progress. Threadsafe. */
    private boolean mIsPrecaching = false;

    /** Wakelock that is held while precaching is in progress. */
    private WakeLock mPrecachingWakeLock;

    private Context mAppContext;
    private Queue<Integer> mFailureReasonsToRecord = new ArrayDeque<Integer>();
    private DeviceState mDeviceState = DeviceState.getInstance();

    /** Receiver that will be notified when conditions become wrong for precaching. */
    private final BroadcastReceiver mDeviceStateReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            Log.v(TAG, "conditions changed: precaching(%s), powered(%s), unmetered(%s)",
                    isPrecaching(), mDeviceState.isPowerConnected(context),
                    mDeviceState.isUnmeteredNetworkAvailable(context));
            if (isPrecaching() && (!mDeviceState.isPowerConnected(context)
                    || !mDeviceState.isUnmeteredNetworkAvailable(context))) {
                recordFailureReasons(context);
                cancelPrecaching();
            }
        }
    };

    Handler mHandler;
    Runnable mTimeoutRunnable = new Runnable() {
        @Override
        public void run() {
            Log.v(TAG, "precache session timed out");
            cancelPrecaching();
        }
    };

    /**
     * Used to ensure this class is always used on the thread on which it
     * is instantiated.
     */
    private final NonThreadSafe mNonThreadSafe;

    /**
     * Returns the singleton PrecacheController instance. Should only be called
     * from the UI thread.
     */
    public static PrecacheController get(Context context) {
        if (sInstance == null) {
            sInstance = new PrecacheController(context);
        }
        return sInstance;
    }

    /**
     * Returns true if the PrecacheController singleton has already been
     * created.
     */
    public static boolean hasInstance() {
        return sInstance != null;
    }

    /** Schedules a periodic task to precache resources. */
    public static void schedulePeriodicPrecacheTask(Context context) {
        if (!canScheduleTasks(context)) return;

        PeriodicTask task = new PeriodicTask.Builder()
                .setPeriod(WAIT_UNTIL_NEXT_PRECACHE_SECONDS)
                .setPersisted(true)
                .setRequiredNetwork(PeriodicTask.NETWORK_STATE_UNMETERED)
                .setRequiresCharging(true)
                .setService(ChromeBackgroundService.class)
                .setTag(PERIODIC_TASK_TAG)
                .build();
        sTaskScheduler.scheduleTask(context, task);
    }

    private static void cancelPeriodicPrecacheTask(Context context) {
        Log.v(TAG, "canceling a periodic precache task");
        sTaskScheduler.cancelTask(context, PERIODIC_TASK_TAG);
    }

    /**
     * Schedules a one-off task to finish precaching the resources that were
     * still outstanding when the last task was interrupted. Interrupting such
     * a one-off task will result in scheduling a new one.
     * @param context The application context.
     */
    private static void schedulePrecacheCompletionTask(Context context) {
        Log.v(TAG, "scheduling a precache completion task");
        if (!canScheduleTasks(context)) return;

        OneoffTask task = new OneoffTask.Builder()
                .setExecutionWindow(COMPLETION_TASK_MIN_DELAY_SECONDS,
                        COMPLETION_TASK_MAX_DELAY_SECONDS)
                .setPersisted(true)
                .setRequiredNetwork(OneoffTask.NETWORK_STATE_UNMETERED)
                .setRequiresCharging(true)
                .setService(ChromeBackgroundService.class)
                .setTag(CONTINUATION_TASK_TAG)
                .setUpdateCurrent(true)
                .build();
        sTaskScheduler.scheduleTask(context, task);
    }

    private static void cancelPrecacheCompletionTask(Context context) {
        Log.v(TAG, "canceling a precache completion task");
        sTaskScheduler.cancelTask(context, CONTINUATION_TASK_TAG);
    }

    public static void rescheduleTasksOnUpgrade(Context context) {
        schedulePeriodicPrecacheTask(context);
    }

    @VisibleForTesting
    static boolean canScheduleTasks(Context context) {
        // This experimental feature should not run on Chrome Stable.
        if (ChromeVersionInfo.isStableBuild()) {
            return false;
        }
        if (GoogleApiAvailability.getInstance().isGooglePlayServicesAvailable(context)
                != ConnectionResult.SUCCESS) {
            return false;
        }
        return true;
    }

    @VisibleForTesting
    PrecacheController(Context context) {
        mNonThreadSafe = new NonThreadSafe();
        mAppContext = context.getApplicationContext();
        mHandler = new Handler(Looper.myLooper());
    }

    /** Returns true if precaching is able to run. */
    @VisibleForTesting
    boolean isPrecachingEnabled() {
        assert mNonThreadSafe.calledOnValidThread();
        SharedPreferences prefs = ContextUtils.getAppSharedPreferences();
        return prefs.getBoolean(PREF_IS_PRECACHING_ENABLED, false);
    }

    private void runOnInstanceThread(final Runnable r) {
        if (mHandler.getLooper() == Looper.myLooper()) {
            r.run();
        } else {
            mHandler.post(r);
        }
    }

    /**
     * Sets whether or not precaching is enabled. If precaching is enabled, a
     * periodic precaching task will be scheduled to run. If disabled, any
     * running precache session will be stopped, and all tasks canceled.
     */
    public static void setIsPrecachingEnabled(Context context, boolean enabled) {
        boolean cancelRequired = !enabled && PrecacheController.hasInstance();
        Context appContext = context.getApplicationContext();

        SharedPreferences sharedPreferences = ContextUtils.getAppSharedPreferences();
        if (sharedPreferences.getBoolean(PREF_IS_PRECACHING_ENABLED, !enabled) == enabled) {
            return;
        }

        Log.v(TAG, "setting precache enabled to %s", enabled);
        Editor editor = sharedPreferences.edit();
        editor.putBoolean(PREF_IS_PRECACHING_ENABLED, enabled);
        editor.apply();

        if (enabled) {
            // Overwrites existing periodic task.
            schedulePeriodicPrecacheTask(appContext);
        } else {
            // If precaching, stop.
            cancelPeriodicPrecacheTask(appContext);
            cancelPrecacheCompletionTask(appContext);
        }
        if (cancelRequired) {
            sInstance.runOnInstanceThread(new Runnable() {
                @Override
                public void run() {
                    sInstance.cancelPrecaching();
                }
            });
        }
    }

    /** Returns true if the precache session in progress. */
    public boolean isPrecaching() {
        assert mNonThreadSafe.calledOnValidThread();
        return mIsPrecaching;
    }

    /**
     * Sets whether or not the precache session is in progress.
     * @return True if this state changed.
     */
    @VisibleForTesting
    boolean setIsPrecaching(boolean isPrecaching) {
        assert mNonThreadSafe.calledOnValidThread();
        if (mIsPrecaching != isPrecaching) {
            mIsPrecaching = isPrecaching;
            return true;
        }
        return false;
    }

    /** Overrides the default DeviceState object, e.g., with a mock for tests. */
    @VisibleForTesting
    void setDeviceState(DeviceState deviceState) {
        assert mNonThreadSafe.calledOnValidThread();
        mDeviceState = deviceState;
    }

    @VisibleForTesting
    Runnable getTimeoutRunnable() {
        assert mNonThreadSafe.calledOnValidThread();
        return mTimeoutRunnable;
    }

    @VisibleForTesting
    BroadcastReceiver getDeviceStateReceiver() {
        assert mNonThreadSafe.calledOnValidThread();
        return mDeviceStateReceiver;
    }

    /**
     * Ends a precache session.
     * @param precachingIncomplete True if the session was interrupted.
     */
    void handlePrecacheCompleted(boolean precachingIncomplete) {
        assert mNonThreadSafe.calledOnValidThread();
        if (setIsPrecaching(false)) {
            shutdownPrecaching(precachingIncomplete);
        }
    }

    /** {@link PrecacheLauncher} used to run a precache session. */
    private PrecacheLauncher mPrecacheLauncher = new PrecacheLauncher() {
        @Override
        protected void onPrecacheCompleted(boolean tryAgainSoon) {
            Log.v(TAG, "precache session completed");
            handlePrecacheCompleted(tryAgainSoon);
        }
    };

    /**
      * Called by {@link ChromeBackgroundService} when a precache task is ready
      * to run.
      */
    public int precache(String tag) {
        assert mNonThreadSafe.calledOnValidThread();
        Log.v(TAG, "precache task (%s) started", tag);
        if (!isPrecachingEnabled()) {
            Log.v(TAG, "precaching isn't enabled");
            cancelPeriodicPrecacheTask(mAppContext);
            cancelPrecacheCompletionTask(mAppContext);
            return GcmNetworkManager.RESULT_SUCCESS;
        }
        if (setIsPrecaching(true)) {
            if (PERIODIC_TASK_TAG.equals(tag)) {
                cancelPrecacheCompletionTask(mAppContext);
            }
            startPrecachingAfterSyncInit();
            return GcmNetworkManager.RESULT_SUCCESS;
        }
        Log.v(TAG, "precache session was already running");
        return GcmNetworkManager.RESULT_FAILURE;
    }

    @VisibleForTesting
    void startPrecachingAfterSyncInit() {
        mSyncServiceNotifier = new SyncServiceInitializedNotifier(
                SYNC_SERVICE_CONFIGURED_DATATYPES, new SyncServiceInitializedNotifier.Listener() {
                    @Override
                    public void onDataTypesActive() {
                        startPrecaching();
                    }

                    @Override
                    public void onFailureOrTimedOut() {
                        // TODO(rajendrant): Add UMA histogram to track this failure.
                        cancelPrecaching();
                    }
                }, MAX_SYNC_SERVICE_INIT_TIMOUT_MS);
    }

    /** Begins a precache session. */
    @VisibleForTesting
    void startPrecaching() {
        Log.v(TAG, "precache session has started");
        registerDeviceStateReceiver();
        acquirePrecachingWakeLock();

        mHandler.postDelayed(mTimeoutRunnable, MAX_PRECACHE_DURATION_SECONDS * 1000);

        // In certain cases, the PrecacheLauncher will skip precaching entirely and call
        // finishPrecaching() before this call to mPrecacheLauncher.start() returns, so the call to
        // mPrecacheLauncher.start() must happen after acquiring the wake lock to ensure that the
        // wake lock is released properly.
        mPrecacheLauncher.start();
    }

    /** Cancels a precache session. */
    private void cancelPrecaching() {
        Log.v(TAG, "canceling precache session");
        if (setIsPrecaching(false)) {
            mPrecacheLauncher.cancel();
            shutdownPrecaching(true);
        }
    }

    /**
     * Updates state to indicate that the precache session is no longer in
     * progress, and stops the service.
     */
    private void shutdownPrecaching(boolean precachingIncomplete) {
        Log.v(TAG, "shutting down precache session");
        if (precachingIncomplete) {
            schedulePrecacheCompletionTask(mAppContext);
        }
        mHandler.removeCallbacks(mTimeoutRunnable);
        mAppContext.unregisterReceiver(mDeviceStateReceiver);
        releasePrecachingWakeLock();
    }

    /**
     * Registers a BroadcastReceiver to detect when conditions become wrong
     * for precaching.
     */
    private void registerDeviceStateReceiver() {
        Log.v(TAG, "registered device state receiver");
        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_POWER_DISCONNECTED);
        filter.addAction(ConnectivityManager.CONNECTIVITY_ACTION);
        mAppContext.registerReceiver(mDeviceStateReceiver, filter);
    }

    /** Acquires the precaching {@link WakeLock}. */
    @VisibleForTesting
    void acquirePrecachingWakeLock() {
        assert mNonThreadSafe.calledOnValidThread();
        Log.v(TAG, "acquiring wake lock");
        if (mPrecachingWakeLock == null) {
            PowerManager pm = (PowerManager) mAppContext.getSystemService(Context.POWER_SERVICE);
            mPrecachingWakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, TAG);
        }
        mPrecachingWakeLock.acquire();
    }

    /** Releases the precaching {@link WakeLock} if it is held. */
    @VisibleForTesting
    void releasePrecachingWakeLock() {
        assert mNonThreadSafe.calledOnValidThread();
        Log.v(TAG, "releasing wake lock");
        if (mPrecachingWakeLock != null && mPrecachingWakeLock.isHeld()) {
            mPrecachingWakeLock.release();
        }
    }

    /**
      * Returns the set of reasons that the last prefetch attempt failed to start.
      *
      * @param context the context passed to onReceive()
      */
    @VisibleForTesting
    EnumSet<FailureReason> interruptionReasons(Context context) {
        assert mNonThreadSafe.calledOnValidThread();
        EnumSet<FailureReason> reasons = EnumSet.noneOf(FailureReason.class);
        reasons.addAll(mPrecacheLauncher.failureReasons());
        if (!mDeviceState.isPowerConnected(context)) reasons.add(FailureReason.NO_POWER);
        if (!mDeviceState.isUnmeteredNetworkAvailable(context)) reasons.add(FailureReason.NO_WIFI);
        if (isPrecaching()) reasons.add(FailureReason.CURRENTLY_PRECACHING);
        return reasons;
    }

    /**
     * Tries to record a histogram enumerating all of the return value of failureReasons().
     *
     * If the native libraries are not already loaded, no histogram is recorded.
     *
     * @param context the context passed to onReceive()
     */
    @VisibleForTesting
    void recordFailureReasons(Context context) {
        assert mNonThreadSafe.calledOnValidThread();
        int reasons = FailureReason.bitValue(interruptionReasons(context));
         // Queue up this failure reason, for the next time we are able to record it in UMA.
        mFailureReasonsToRecord.add(reasons);
         // If native libraries are loaded, then we are able to flush our queue to UMA.
        if (LibraryLoader.isInitialized()) {
            Integer reasonsToRecord;
            while ((reasonsToRecord = mFailureReasonsToRecord.poll()) != null) {
                RecordHistogram.recordSparseSlowlyHistogram(
                        "Precache.Fetch.FailureReasons", reasonsToRecord);
                RecordUserAction.record("Precache.Fetch.IntentReceived");
            }
        }
    }

    @VisibleForTesting
    void setPrecacheLauncher(PrecacheLauncher precacheLauncher) {
        assert mNonThreadSafe.calledOnValidThread();
        mPrecacheLauncher = precacheLauncher;
    }

    @VisibleForTesting
    static void setTaskScheduler(PrecacheTaskScheduler taskScheduler) {
        PrecacheController.sTaskScheduler = taskScheduler;
    }
}
