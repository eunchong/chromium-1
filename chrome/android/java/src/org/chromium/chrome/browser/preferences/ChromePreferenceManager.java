// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.preferences;

import android.content.Context;
import android.content.SharedPreferences;

import org.chromium.base.ContextUtils;
import org.chromium.base.annotations.SuppressFBWarnings;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.crash.MinidumpUploadService.ProcessType;
import org.chromium.chrome.browser.util.FeatureUtilities;

import java.util.Locale;


/**
 * ChromePreferenceManager stores and retrieves various values in Android shared preferences.
 */
public class ChromePreferenceManager {
    /**
     * Preference that denotes that Chrome has attempted to migrate from tabbed mode to document
     * mode.
     */
    public static final String MIGRATION_ON_UPGRADE_ATTEMPTED = "migration_on_upgrade_attempted";

    private static final String TAG = "preferences";

    private static final String PROMOS_SKIPPED_ON_FIRST_START = "promos_skipped_on_first_start";
    private static final String SIGNIN_PROMO_LAST_SHOWN = "signin_promo_last_timestamp_key";
    private static final String SHOW_SIGNIN_PROMO = "show_signin_promo";
    private static final String ALLOW_LOW_END_DEVICE_UI = "allow_low_end_device_ui";
    private static final String PREF_WEBSITE_SETTINGS_FILTER = "website_settings_filter";
    private static final String CONTEXTUAL_SEARCH_PROMO_OPEN_COUNT =
            "contextual_search_promo_open_count";
    private static final String CONTEXTUAL_SEARCH_TAP_TRIGGERED_PROMO_COUNT =
            "contextual_search_tap_triggered_promo_count";
    private static final String CONTEXTUAL_SEARCH_TAP_COUNT = "contextual_search_tap_count";
    private static final String CONTEXTUAL_SEARCH_PEEK_PROMO_SHOW_COUNT =
            "contextual_search_peek_promo_show_count";
    private static final String CONTEXTUAL_SEARCH_LAST_ANIMATION_TIME =
            "contextual_search_last_animation_time";
    private static final String ENABLE_CUSTOM_TABS = "enable_custom_tabs";
    private static final String HERB_FLAVOR_KEY = "herb_flavor";
    private static final String APP_LINK_KEY = "applink.app_link_enabled";
    private static final String CHROME_DEFAULT_BROWSER = "applink.chrome_default_browser";

    private static final String SUCCESS_UPLOAD_SUFFIX = "_crash_success_upload";
    private static final String FAILURE_UPLOAD_SUFFIX = "_crash_failure_upload";

    private static final int SIGNIN_PROMO_CYCLE_IN_DAYS = 120;
    private static final long MILLISECONDS_IN_DAY = 1000 * 60 * 60 * 24;

    private static ChromePreferenceManager sPrefs;

    private final SharedPreferences mSharedPreferences;
    private final Context mContext;

    private ChromePreferenceManager(Context context) {
        mContext = context.getApplicationContext();
        mSharedPreferences = ContextUtils.getAppSharedPreferences();
    }

    /**
     * Get the static instance of ChromePreferenceManager if exists else create it.
     * @param context
     * @return the ChromePreferenceManager singleton
     */
    @SuppressFBWarnings("CHROMIUM_SYNCHRONIZED_METHOD")
    public static synchronized ChromePreferenceManager getInstance(Context context) {
        if (sPrefs == null) {
            sPrefs = new ChromePreferenceManager(context);
        }
        return sPrefs;
    }

    /**
     * @return Number of times of successful crash upload.
     */
    public int getCrashSuccessUploadCount(@ProcessType String process) {
        // Convention to keep all the key in preference lower case.
        return mSharedPreferences.getInt(successUploadKey(process), 0);
    }

    public void setCrashSuccessUploadCount(@ProcessType String process, int count) {
        SharedPreferences.Editor sharedPreferencesEditor;

        sharedPreferencesEditor = mSharedPreferences.edit();
        // Convention to keep all the key in preference lower case.
        sharedPreferencesEditor.putInt(successUploadKey(process), count);
        sharedPreferencesEditor.apply();
    }

    public void incrementCrashSuccessUploadCount(@ProcessType String process) {
        setCrashSuccessUploadCount(process, getCrashSuccessUploadCount(process) + 1);
    }

    private String successUploadKey(@ProcessType String process) {
        return process.toLowerCase(Locale.US) + SUCCESS_UPLOAD_SUFFIX;
    }

    /**
     * @return Number of times of failure crash upload after reaching the max number of tries.
     */
    public int getCrashFailureUploadCount(@ProcessType String process) {
        return mSharedPreferences.getInt(failureUploadKey(process), 0);
    }

    public void setCrashFailureUploadCount(@ProcessType String process, int count) {
        SharedPreferences.Editor sharedPreferencesEditor;

        sharedPreferencesEditor = mSharedPreferences.edit();
        sharedPreferencesEditor.putInt(failureUploadKey(process), count);
        sharedPreferencesEditor.apply();
    }

    public void incrementCrashFailureUploadCount(@ProcessType String process) {
        setCrashFailureUploadCount(process, getCrashFailureUploadCount(process) + 1);
    }

    private String failureUploadKey(@ProcessType String process) {
        return process.toLowerCase(Locale.US) + FAILURE_UPLOAD_SUFFIX;
    }

    /**
     * @return Whether we have attempted to migrate tabbed state to document mode after OS upgrade.
     */
    public boolean hasAttemptedMigrationOnUpgrade() {
        return mSharedPreferences.getBoolean(MIGRATION_ON_UPGRADE_ATTEMPTED, false);
    }

    /**
     * Mark that we have made an attempt to migrate tabbed state to document mode after OS upgrade.
     */
    public void setAttemptedMigrationOnUpgrade() {
        SharedPreferences.Editor sharedPreferencesEditor = mSharedPreferences.edit();
        sharedPreferencesEditor.putBoolean(MIGRATION_ON_UPGRADE_ATTEMPTED, true);
        sharedPreferencesEditor.apply();
    }

    /**
     * @return Whether the promotion for data reduction has been skipped on first invocation.
     */
    public boolean getPromosSkippedOnFirstStart() {
        return mSharedPreferences.getBoolean(PROMOS_SKIPPED_ON_FIRST_START, false);
    }

    /**
     * Enables custom tabs when true. This will take effect next time an activity is created.
     * @param enabled Whether custom tabs should be enabled.
     */
    public void setCustomTabsEnabled(boolean enabled) {
        SharedPreferences.Editor ed = mSharedPreferences.edit();
        ed.putBoolean(ENABLE_CUSTOM_TABS, enabled);
        ed.apply();
    }

    /**
     * @return Whether custom tabs is enabled. This return value is designed to be used as a kill
     *         switch for the feature, so it returns true by default if the preference is not set.
     */
    public boolean getCustomTabsEnabled() {
        return mSharedPreferences.getBoolean(ENABLE_CUSTOM_TABS, true);
    }

    /**
     * Marks whether the data reduction promotion was skipped on first
     * invocation.
     * @param displayed Whether the promotion was shown.
     */
    public void setPromosSkippedOnFirstStart(boolean displayed) {
        SharedPreferences.Editor ed = mSharedPreferences.edit();
        ed.putBoolean(PROMOS_SKIPPED_ON_FIRST_START, displayed);
        ed.apply();
    }

    /**
     * @return The value for the website settings filter (the one that specifies
     * which sites to show in the list).
     */
    public String getWebsiteSettingsFilterPreference() {
        return mSharedPreferences.getString(
                ChromePreferenceManager.PREF_WEBSITE_SETTINGS_FILTER, "");
    }

    /**
     * Sets the filter value for website settings (which websites to show in the list).
     * @param prefValue The type to restrict the filter to.
     */
    public void setWebsiteSettingsFilterPreference(String prefValue) {
        SharedPreferences.Editor sharedPreferencesEditor = mSharedPreferences.edit();
        sharedPreferencesEditor.putString(
                ChromePreferenceManager.PREF_WEBSITE_SETTINGS_FILTER, prefValue);
        sharedPreferencesEditor.apply();
    }

    /**
     * This value may have been explicitly set to false when we used to keep existing low-end
     * devices on the normal UI rather than the simplified UI. We want to keep the existing device
     * settings. For all new low-end devices they should get the simplified UI by default.
     * @return Whether low end device UI was allowed.
     */
    public boolean getAllowLowEndDeviceUi() {
        return mSharedPreferences.getBoolean(ALLOW_LOW_END_DEVICE_UI, true);
    }

    /**
     * Signin promo could be shown at most once every 12 weeks. This method checks
     * wheter the signin promo has already been shown in the current cycle.
     * @return Whether the signin promo has been shown in the current cycle.
     */
    public boolean getSigninPromoShown() {
        long signinPromoLastShown = mSharedPreferences.getLong(SIGNIN_PROMO_LAST_SHOWN, 0);
        long numDaysElapsed =
                (System.currentTimeMillis() - signinPromoLastShown) / MILLISECONDS_IN_DAY;
        return numDaysElapsed < SIGNIN_PROMO_CYCLE_IN_DAYS;
    }

    /**
     * Sets the preference for tracking when the signin promo was last shown.
     */
    public void setSigninPromoShown() {
        SharedPreferences.Editor sharedPreferencesEditor = mSharedPreferences.edit();
        sharedPreferencesEditor.putLong(SIGNIN_PROMO_LAST_SHOWN, System.currentTimeMillis());
        sharedPreferencesEditor.apply();
    }

    /**
     * @return Whether the signin promo has been marked to be shown on next startup.
     */
    public boolean getShowSigninPromo() {
        return mSharedPreferences.getBoolean(SHOW_SIGNIN_PROMO, false);
    }

    /**
     * Sets the preference to indicate that the signin promo should be shown on next startup.
     * @param shouldShow Whether the signin promo should be shown.
     */
    public void setShowSigninPromo(boolean shouldShow) {
        SharedPreferences.Editor sharedPreferencesEditor = mSharedPreferences.edit();
        sharedPreferencesEditor.putBoolean(SHOW_SIGNIN_PROMO, shouldShow).apply();
    }

    /**
     * @return Number of times the panel was opened with the promo visible.
     */
    public int getContextualSearchPromoOpenCount() {
        return mSharedPreferences.getInt(CONTEXTUAL_SEARCH_PROMO_OPEN_COUNT, 0);
    }

    /**
     * Sets the number of times the panel was opened with the promo visible.
     * @param count Number of times the panel was opened with a promo visible.
     */
    public void setContextualSearchPromoOpenCount(int count) {
        writeInt(CONTEXTUAL_SEARCH_PROMO_OPEN_COUNT, count);
    }

    /**
     * @return Number of times the Peek Promo was shown.
     */
    public int getContextualSearchPeekPromoShowCount() {
        return mSharedPreferences.getInt(CONTEXTUAL_SEARCH_PEEK_PROMO_SHOW_COUNT, 0);
    }

    /**
     * Sets the number of times the Peek Promo was shown.
     * @param count Number of times the Peek Promo was shown.
     */
    public void setContextualSearchPeekPromoShowCount(int count) {
        writeInt(CONTEXTUAL_SEARCH_PEEK_PROMO_SHOW_COUNT, count);
    }

    /**
     * @return The last time the search provider icon was animated on tap.
     */
    public long getContextualSearchLastAnimationTime() {
        return mSharedPreferences.getLong(CONTEXTUAL_SEARCH_LAST_ANIMATION_TIME, 0);
    }

    /**
     * Sets the last time the search provider icon was animated on tap.
     * @param time The last time the search provider icon was animated on tap.
     */
    public void setContextualSearchLastAnimationTime(long time) {
        SharedPreferences.Editor ed = mSharedPreferences.edit();
        ed.putLong(CONTEXTUAL_SEARCH_LAST_ANIMATION_TIME, time);
        ed.apply();
    }

    /**
     * @return Number of times the promo was triggered to peek by a tap gesture, or a negative value
     *         if in the disabled state.
     */
    public int getContextualSearchTapTriggeredPromoCount() {
        return mSharedPreferences.getInt(CONTEXTUAL_SEARCH_TAP_TRIGGERED_PROMO_COUNT, 0);
    }

    /**
     * Sets the number of times the promo was triggered to peek by a tap gesture.
     * Use a negative value to record that the counter has been disabled.
     * @param count Number of times the promo was triggered by a tap gesture, or a negative value
     *        to record that the counter has been disabled.
     */
    public void setContextualSearchTapTriggeredPromoCount(int count) {
        writeInt(CONTEXTUAL_SEARCH_TAP_TRIGGERED_PROMO_COUNT, count);
    }

    /**
     * @return Number of tap gestures that have been received when not waiting for the promo.
     */
    public int getContextualSearchTapCount() {
        return mSharedPreferences.getInt(CONTEXTUAL_SEARCH_TAP_COUNT, 0);
    }

    /**
     * Sets the number of tap gestures that have been received when not waiting for the promo.
     * @param count Number of taps that have been received when not waiting for the promo.
     */
    public void setContextualSearchTapCount(int count) {
        writeInt(CONTEXTUAL_SEARCH_TAP_COUNT, count);
    }

    /**
     * @return Which UI prototype the user is testing. This is cached from native via
     *         {@link FeatureUtilities#cacheHerbFlavor}.
     */
    public String getCachedHerbFlavor() {
        return mSharedPreferences.getString(HERB_FLAVOR_KEY, ChromeSwitches.HERB_FLAVOR_DISABLED);
    }

    /**
     * Caches which UI prototype the user is testing.
     */
    public void setCachedHerbFlavor(String flavor) {
        writeString(HERB_FLAVOR_KEY, flavor);
    }

    /** Checks the cached value for the app link feature. */
    public boolean getCachedAppLinkEnabled() {
        return mSharedPreferences.getBoolean(APP_LINK_KEY, false);
    }

    /** Writes the cached value for whether app link is enabled. */
    public void setCachedAppLinkEnabled(boolean isEnabled) {
        SharedPreferences.Editor ed = mSharedPreferences.edit();
        ed.putBoolean(APP_LINK_KEY, isEnabled);
        ed.apply();
    }

    public boolean getCachedChromeDefaultBrowser() {
        return mSharedPreferences.getBoolean(CHROME_DEFAULT_BROWSER, false);
    }

    public void setCachedChromeDefaultBrowser(boolean isDefault) {
        SharedPreferences.Editor ed = mSharedPreferences.edit();
        ed.putBoolean(CHROME_DEFAULT_BROWSER, isDefault);
        ed.apply();
    }

    /**
     * Writes the given int value to the named shared preference.
     *
     * @param key The name of the preference to modify.
     * @param value The new value for the preference.
     */
    private void writeInt(String key, int value) {
        SharedPreferences.Editor ed = mSharedPreferences.edit();
        ed.putInt(key, value);
        ed.apply();
    }

    /**
     * Writes the given String to the named shared preference.
     *
     * @param key The name of the preference to modify.
     * @param value The new value for the preference.
     */
    private void writeString(String key, String value) {
        SharedPreferences.Editor ed = mSharedPreferences.edit();
        ed.putString(key, value);
        ed.apply();
    }
}
