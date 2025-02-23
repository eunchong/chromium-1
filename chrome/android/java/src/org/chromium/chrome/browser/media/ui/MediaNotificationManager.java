// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.media.ui;

import android.app.Notification;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.media.AudioManager;
import android.os.Build;
import android.os.IBinder;
import android.support.v4.app.NotificationManagerCompat;
import android.support.v4.media.MediaMetadataCompat;
import android.support.v4.media.session.MediaSessionCompat;
import android.support.v4.media.session.PlaybackStateCompat;
import android.support.v7.app.NotificationCompat;
import android.support.v7.media.MediaRouter;
import android.text.TextUtils;
import android.util.SparseArray;
import android.view.KeyEvent;
import android.view.View;
import android.widget.RemoteViews;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.VisibleForTesting;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeFeatureList;
import org.chromium.content_public.common.MediaMetadata;

import javax.annotation.Nullable;

/**
 * A class for notifications that provide information and optional media controls for a given media.
 * Internally implements a Service for transforming notification Intents into
 * {@link MediaNotificationListener} calls for all registered listeners.
 * There's one service started for a distinct notification id.
 */
public class MediaNotificationManager {
    private static final String TAG = "MediaNotification";

    // We're always used on the UI thread but the LOCK is required by lint when creating the
    // singleton.
    private static final Object LOCK = new Object();

    // Maps the notification ids to their corresponding notification managers.
    private static SparseArray<MediaNotificationManager> sManagers;

    /**
     * Service used to transform intent requests triggered from the notification into
     * {@code MediaNotificationListener} callbacks. We have to create a separate derived class for
     * each type of notification since one class corresponds to one instance of the service only.
     */
    private abstract static class ListenerService extends Service {
        private static final String ACTION_PLAY =
                "MediaNotificationManager.ListenerService.PLAY";
        private static final String ACTION_PAUSE =
                "MediaNotificationManager.ListenerService.PAUSE";
        private static final String ACTION_STOP =
                "MediaNotificationManager.ListenerService.STOP";

        @Override
        public IBinder onBind(Intent intent) {
            return null;
        }

        @Override
        public void onDestroy() {
            super.onDestroy();

            MediaNotificationManager manager = getManager();
            if (manager == null) return;

            manager.onServiceDestroyed();
        }

        @Override
        public int onStartCommand(Intent intent, int flags, int startId) {
            if (!processIntent(intent)) stopSelf();

            return START_NOT_STICKY;
        }

        @Nullable
        protected abstract MediaNotificationManager getManager();

        private boolean processIntent(Intent intent) {
            if (intent == null) return false;

            MediaNotificationManager manager = getManager();
            if (manager == null || manager.mMediaNotificationInfo == null) return false;

            manager.onServiceStarted(this);

            processAction(intent, manager);
            return true;
        }

        private void processAction(Intent intent, MediaNotificationManager manager) {
            String action = intent.getAction();

            // Before Android L, instead of using the MediaSession callback, the system will fire
            // ACTION_MEDIA_BUTTON intents which stores the information about the key event.
            if (Intent.ACTION_MEDIA_BUTTON.equals(action)) {
                assert Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP;

                KeyEvent event = (KeyEvent) intent.getParcelableExtra(Intent.EXTRA_KEY_EVENT);
                if (event == null) return;
                if (event.getAction() != KeyEvent.ACTION_DOWN) return;

                switch (event.getKeyCode()) {
                    case KeyEvent.KEYCODE_MEDIA_PLAY:
                        manager.onPlay(
                                MediaNotificationListener.ACTION_SOURCE_MEDIA_SESSION);
                        break;
                    case KeyEvent.KEYCODE_MEDIA_PAUSE:
                        manager.onPause(
                                MediaNotificationListener.ACTION_SOURCE_MEDIA_SESSION);
                        break;
                    case KeyEvent.KEYCODE_HEADSETHOOK:
                    case KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE:
                        if (manager.mMediaNotificationInfo.isPaused) {
                            manager.onPlay(MediaNotificationListener.ACTION_SOURCE_MEDIA_SESSION);
                        } else {
                            manager.onPause(
                                    MediaNotificationListener.ACTION_SOURCE_MEDIA_SESSION);
                        }
                        break;
                    default:
                        break;
                }
            } else if (ACTION_STOP.equals(action)) {
                manager.onStop(
                        MediaNotificationListener.ACTION_SOURCE_MEDIA_NOTIFICATION);
                stopSelf();
            } else if (ACTION_PLAY.equals(action)) {
                manager.onPlay(MediaNotificationListener.ACTION_SOURCE_MEDIA_NOTIFICATION);
            } else if (ACTION_PAUSE.equals(action)) {
                manager.onPause(MediaNotificationListener.ACTION_SOURCE_MEDIA_NOTIFICATION);
            } else if (AudioManager.ACTION_AUDIO_BECOMING_NOISY.equals(action)) {
                manager.onPause(MediaNotificationListener.ACTION_SOURCE_HEADSET_UNPLUG);
            }
        }
    }

    /**
     * This class is used internally but have to be public to be able to launch the service.
     */
    public static final class PlaybackListenerService extends ListenerService {
        private static final int NOTIFICATION_ID = R.id.media_playback_notification;

        @Override
        public void onCreate() {
            super.onCreate();
            IntentFilter filter = new IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY);
            registerReceiver(mAudioBecomingNoisyReceiver, filter);
        }

        @Override
        public void onDestroy() {
            unregisterReceiver(mAudioBecomingNoisyReceiver);
            super.onDestroy();
        }

        @Override
        @Nullable
        protected MediaNotificationManager getManager() {
            return MediaNotificationManager.getManager(NOTIFICATION_ID);
        }

        private BroadcastReceiver mAudioBecomingNoisyReceiver = new BroadcastReceiver() {
                @Override
                public void onReceive(Context context, Intent intent) {
                    if (!AudioManager.ACTION_AUDIO_BECOMING_NOISY.equals(intent.getAction())) {
                        return;
                    }

                    Intent i = new Intent(context, PlaybackListenerService.class);
                    i.setAction(intent.getAction());
                    context.startService(i);
                }
            };
    }

    /**
     * This class is used internally but have to be public to be able to launch the service.
     */
    public static final class PresentationListenerService extends ListenerService {
        private static final int NOTIFICATION_ID = R.id.presentation_notification;

        @Override
        @Nullable
        protected MediaNotificationManager getManager() {
            return MediaNotificationManager.getManager(NOTIFICATION_ID);
        }
    }

    /**
     * This class is used internally but have to be public to be able to launch the service.
     */
    public static final class CastListenerService extends ListenerService {
        private static final int NOTIFICATION_ID = R.id.remote_notification;

        @Override
        @Nullable
        protected MediaNotificationManager getManager() {
            return MediaNotificationManager.getManager(NOTIFICATION_ID);
        }
    }

    // Three classes to specify the right notification id in the intent.

    /**
     * This class is used internally but have to be public to be able to launch the service.
     */
    public static final class PlaybackMediaButtonReceiver extends MediaButtonReceiver {
        @Override
        public String getServiceClassName() {
            return PlaybackListenerService.class.getName();
        }
    }

    /**
     * This class is used internally but have to be public to be able to launch the service.
     */
    public static final class PresentationMediaButtonReceiver extends MediaButtonReceiver {
        @Override
        public String getServiceClassName() {
            return PresentationListenerService.class.getName();
        }
    }

    /**
     * This class is used internally but have to be public to be able to launch the service.
     */
    public static final class CastMediaButtonReceiver extends MediaButtonReceiver {
        @Override
        public String getServiceClassName() {
            return CastListenerService.class.getName();
        }
    }

    private Intent createIntent(Context context) {
        Intent intent = null;
        if (mMediaNotificationInfo.id == PlaybackListenerService.NOTIFICATION_ID) {
            intent = new Intent(context, PlaybackListenerService.class);
        } else if (mMediaNotificationInfo.id == PresentationListenerService.NOTIFICATION_ID) {
            intent = new Intent(context, PresentationListenerService.class);
        }  else if (mMediaNotificationInfo.id == CastListenerService.NOTIFICATION_ID) {
            intent = new Intent(context, CastListenerService.class);
        }
        return intent;
    }

    private PendingIntent createPendingIntent(String action) {
        assert mService != null;
        Intent intent = createIntent(mService).setAction(action);
        return PendingIntent.getService(mService, 0, intent, PendingIntent.FLAG_CANCEL_CURRENT);
    }

    private String getButtonReceiverClassName() {
        if (mMediaNotificationInfo.id == PlaybackListenerService.NOTIFICATION_ID) {
            return PlaybackMediaButtonReceiver.class.getName();
        }

        if (mMediaNotificationInfo.id == PresentationListenerService.NOTIFICATION_ID) {
            return PresentationMediaButtonReceiver.class.getName();
        }

        if (mMediaNotificationInfo.id == CastListenerService.NOTIFICATION_ID) {
            return CastMediaButtonReceiver.class.getName();
        }

        assert false;
        return null;
    }

    /**
     * Shows the notification with media controls with the specified media info. Replaces/updates
     * the current notification if already showing. Does nothing if |mediaNotificationInfo| hasn't
     * changed from the last one.
     *
     * @param applicationContext context to create the notification with
     * @param notificationInfo information to show in the notification
     */
    public static void show(Context applicationContext,
                            MediaNotificationInfo notificationInfo) {
        synchronized (LOCK) {
            if (sManagers == null) {
                sManagers = new SparseArray<MediaNotificationManager>();
            }
        }

        MediaNotificationManager manager = sManagers.get(notificationInfo.id);
        if (manager == null) {
            manager = new MediaNotificationManager(applicationContext, notificationInfo.id);
            sManagers.put(notificationInfo.id, manager);
        }

        manager.showNotification(notificationInfo);
    }

    /**
     * Hides the notification for the specified tabId and notificationId
     *
     * @param tabId the id of the tab that showed the notification or invalid tab id.
     * @param notificationId the id of the notification to hide for this tab.
     */
    public static void hide(int tabId, int notificationId) {
        MediaNotificationManager manager = getManager(notificationId);
        if (manager == null) return;

        manager.hideNotification(tabId);
    }

    /**
     * Hides notifications with the specified id for all tabs if shown.
     *
     * @param notificationId the id of the notification to hide for all tabs.
     */
    public static void clear(int notificationId) {
        MediaNotificationManager manager = getManager(notificationId);
        if (manager == null) return;

        manager.clearNotification();
        sManagers.remove(notificationId);
    }

    /**
     * Hides notifications with all known ids for all tabs if shown.
     */
    public static void clearAll() {
        if (sManagers == null) return;

        for (int i = 0; i < sManagers.size(); ++i) {
            MediaNotificationManager manager = sManagers.valueAt(i);
            manager.clearNotification();
        }
        sManagers.clear();
    }

    private static MediaNotificationManager getManager(int notificationId) {
        if (sManagers == null) return null;

        return sManagers.get(notificationId);
    }

    @VisibleForTesting
    static boolean hasManagerForTesting(int notificationId) {
        return getManager(notificationId) != null;
    }

    @VisibleForTesting
    @Nullable
    static NotificationCompat.Builder getNotificationBuilderForTesting(
            int notificationId) {
        MediaNotificationManager manager = getManager(notificationId);
        if (manager == null) return null;

        return manager.mNotificationBuilder;
    }

    private final Context mContext;

    // ListenerService running for the notification. Only non-null when showing.
    private ListenerService mService;

    private final String mPlayDescription;
    private final String mPauseDescription;
    private final String mStopDescription;

    private NotificationCompat.Builder mNotificationBuilder;

    private Bitmap mNotificationIcon;

    private final Bitmap mDefaultLargeIcon;

    // |mMediaNotificationInfo| should be not null if and only if the notification is showing.
    private MediaNotificationInfo mMediaNotificationInfo;

    private MediaSessionCompat mMediaSession;

    private final MediaSessionCompat.Callback mMediaSessionCallback =
            new MediaSessionCompat.Callback() {
                @Override
                public void onPlay() {
                    MediaNotificationManager.this.onPlay(
                            MediaNotificationListener.ACTION_SOURCE_MEDIA_SESSION);
                }

                @Override
                public void onPause() {
                    MediaNotificationManager.this.onPause(
                            MediaNotificationListener.ACTION_SOURCE_MEDIA_SESSION);
                }
            };

    private MediaNotificationManager(Context context, int notificationId) {
        mContext = context;
        mPlayDescription = context.getResources().getString(R.string.accessibility_play);
        mPauseDescription = context.getResources().getString(R.string.accessibility_pause);
        mStopDescription = context.getResources().getString(R.string.accessibility_stop);

        mDefaultLargeIcon = BitmapFactory.decodeResource(
                context.getResources(), R.drawable.ic_music_video);
    }

    /**
     * Registers the started {@link Service} with the manager and creates the notification.
     *
     * @param service the service that was started
     */
    private void onServiceStarted(ListenerService service) {
        mService = service;
        updateNotification();
    }

    /**
     * Handles the service destruction destruction.
     */
    private void onServiceDestroyed() {
        // Service already detached
        if (mService == null) return;
        // Notification is not showing
        if (mMediaNotificationInfo == null) return;

        clear(mMediaNotificationInfo.id);

        mNotificationBuilder = null;
        mService = null;
    }

    private void onPlay(int actionSource) {
        mMediaNotificationInfo.listener.onPlay(actionSource);
    }

    private void onPause(int actionSource) {
        mMediaNotificationInfo.listener.onPause(actionSource);
    }

    private void onStop(int actionSource) {
        mMediaNotificationInfo.listener.onStop(actionSource);
    }

    private void showNotification(MediaNotificationInfo mediaNotificationInfo) {
        if (mediaNotificationInfo.equals(mMediaNotificationInfo)) return;

        mMediaNotificationInfo = mediaNotificationInfo;
        mContext.startService(createIntent(mContext));

        updateNotification();
    }

    private void clearNotification() {
        if (mMediaNotificationInfo == null) return;

        NotificationManagerCompat manager = NotificationManagerCompat.from(mContext);
        manager.cancel(mMediaNotificationInfo.id);

        if (mMediaSession != null) {
            mMediaSession.setCallback(null);
            mMediaSession.setActive(false);
            mMediaSession.release();
            mMediaSession = null;
        }
        mContext.stopService(createIntent(mContext));
        mMediaNotificationInfo = null;
    }

    private void hideNotification(int tabId) {
        if (mMediaNotificationInfo == null || tabId != mMediaNotificationInfo.tabId) return;
        clearNotification();
    }

    private MediaMetadataCompat createMetadata() {
        MediaMetadataCompat.Builder metadataBuilder = new MediaMetadataCompat.Builder();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            metadataBuilder.putString(MediaMetadataCompat.METADATA_KEY_DISPLAY_TITLE,
                    mMediaNotificationInfo.metadata.getTitle());
            metadataBuilder.putString(MediaMetadataCompat.METADATA_KEY_DISPLAY_SUBTITLE,
                    mMediaNotificationInfo.origin);
        } else {
            metadataBuilder.putString(MediaMetadataCompat.METADATA_KEY_TITLE,
                    mMediaNotificationInfo.metadata.getTitle());
            metadataBuilder.putString(MediaMetadataCompat.METADATA_KEY_ARTIST,
                    mMediaNotificationInfo.origin);
        }
        if (!TextUtils.isEmpty(mMediaNotificationInfo.metadata.getArtist())) {
            metadataBuilder.putString(MediaMetadataCompat.METADATA_KEY_ARTIST,
                    mMediaNotificationInfo.metadata.getArtist());
        }
        if (!TextUtils.isEmpty(mMediaNotificationInfo.metadata.getAlbum())) {
            metadataBuilder.putString(MediaMetadataCompat.METADATA_KEY_ALBUM,
                    mMediaNotificationInfo.metadata.getAlbum());
        }

        return metadataBuilder.build();
    }

    private void updateNotification() {
        if (mService == null) return;

        if (mMediaNotificationInfo == null) return;

        updateMediaSession();

        mNotificationBuilder = new NotificationCompat.Builder(mContext);
        if (ChromeFeatureList.isEnabled(ChromeFeatureList.MEDIA_STYLE_NOTIFICATION)) {
            setMediaStyleLayoutForNotificationBuilder(mNotificationBuilder);
        } else {
            setCustomLayoutForNotificationBuilder(mNotificationBuilder);
        }
        mNotificationBuilder.setSmallIcon(mMediaNotificationInfo.icon);
        mNotificationBuilder.setAutoCancel(false);
        mNotificationBuilder.setLocalOnly(true);

        if (mMediaNotificationInfo.supportsSwipeAway()) {
            mNotificationBuilder.setOngoing(!mMediaNotificationInfo.isPaused);
        }

        // The intent will currently only be null when using a custom tab.
        // TODO(avayvod) work out what we should do in this case. See https://crbug.com/585395.
        if (mMediaNotificationInfo.contentIntent != null) {
            mNotificationBuilder.setContentIntent(PendingIntent.getActivity(mContext,
                    mMediaNotificationInfo.tabId, mMediaNotificationInfo.contentIntent,
                    PendingIntent.FLAG_UPDATE_CURRENT));
            // Set FLAG_UPDATE_CURRENT so that the intent extras is updated, otherwise the
            // intent extras will stay the same for the same tab.
        }

        mNotificationBuilder.setVisibility(
                mMediaNotificationInfo.isPrivate ? NotificationCompat.VISIBILITY_PRIVATE
                                                 : NotificationCompat.VISIBILITY_PUBLIC);

        Notification notification = mNotificationBuilder.build();

        // We keep the service as a foreground service while the media is playing. When it is not,
        // the service isn't stopped but is no longer in foreground, thus at a lower priority.
        // While the service is in foreground, the associated notification can't be swipped away.
        // Moving it back to background allows the user to remove the notification.
        if (mMediaNotificationInfo.supportsSwipeAway() && mMediaNotificationInfo.isPaused) {
            mService.stopForeground(false /* removeNotification */);

            NotificationManagerCompat manager = NotificationManagerCompat.from(mContext);
            manager.notify(mMediaNotificationInfo.id, notification);
        } else {
            mService.startForeground(mMediaNotificationInfo.id, notification);
        }
    }

    private void updateMediaSession() {
        if (!mMediaNotificationInfo.supportsPlayPause()) return;

        if (mMediaSession == null) mMediaSession = createMediaSession();

        try {
            // Tell the MediaRouter about the session, so that Chrome can control the volume
            // on the remote cast device (if any).
            // Pre-MR1 versions of JB do not have the complete MediaRouter APIs,
            // so getting the MediaRouter instance will throw an exception.
            MediaRouter.getInstance(mContext).setMediaSessionCompat(mMediaSession);
        } catch (NoSuchMethodError e) {
            // Do nothing. Chrome can't be casting without a MediaRouter, so there is nothing
            // to do here.
        }

        mMediaSession.setMetadata(createMetadata());

        PlaybackStateCompat.Builder playbackStateBuilder =
                new PlaybackStateCompat.Builder().setActions(
                        PlaybackStateCompat.ACTION_PLAY | PlaybackStateCompat.ACTION_PAUSE);
        if (mMediaNotificationInfo.isPaused) {
            playbackStateBuilder.setState(PlaybackStateCompat.STATE_PAUSED,
                    PlaybackStateCompat.PLAYBACK_POSITION_UNKNOWN, 1.0f);
        } else {
            // If notification only supports stop, still pretend
            playbackStateBuilder.setState(PlaybackStateCompat.STATE_PLAYING,
                    PlaybackStateCompat.PLAYBACK_POSITION_UNKNOWN, 1.0f);
        }
        mMediaSession.setPlaybackState(playbackStateBuilder.build());
    }

    private MediaSessionCompat createMediaSession() {
        MediaSessionCompat mediaSession = new MediaSessionCompat(
                mContext,
                mContext.getString(R.string.app_name),
                new ComponentName(mContext.getPackageName(),
                        getButtonReceiverClassName()),
                null);
        mediaSession.setFlags(MediaSessionCompat.FLAG_HANDLES_MEDIA_BUTTONS
                | MediaSessionCompat.FLAG_HANDLES_TRANSPORT_CONTROLS);
        mediaSession.setCallback(mMediaSessionCallback);

        // TODO(mlamouri): the following code is to work around a bug that hopefully
        // MediaSessionCompat will handle directly. see b/24051980.
        try {
            mediaSession.setActive(true);
        } catch (NullPointerException e) {
            // Some versions of KitKat do not support AudioManager.registerMediaButtonIntent
            // with a PendingIntent. They will throw a NullPointerException, in which case
            // they should be able to activate a MediaSessionCompat with only transport
            // controls.
            mediaSession.setActive(false);
            mediaSession.setFlags(MediaSessionCompat.FLAG_HANDLES_TRANSPORT_CONTROLS);
            mediaSession.setActive(true);
        }
        return mediaSession;
    }

    private void setMediaStyleLayoutForNotificationBuilder(NotificationCompat.Builder builder) {
        setMediaStyleNotificationText(builder);
        if (mMediaNotificationInfo.largeIcon != null) {
            builder.setLargeIcon(mMediaNotificationInfo.largeIcon);
        } else if (!TextUtils.equals(Build.VERSION.CODENAME, "N")
                && mMediaNotificationInfo.supportsPlayPause()) {
            builder.setLargeIcon(mDefaultLargeIcon);
        }
        // TODO(zqzhang): It's weird that setShowWhen() don't work on K. Calling setWhen() to force
        // removing the time.
        builder.setShowWhen(false).setWhen(0);

        // Only apply MediaStyle when NotificationInfo supports play/pause.
        if (mMediaNotificationInfo.supportsPlayPause()) {
            NotificationCompat.MediaStyle style = new NotificationCompat.MediaStyle();
            style.setMediaSession(mMediaSession.getSessionToken());

            if (mMediaNotificationInfo.isPaused) {
                builder.addAction(R.drawable.ic_vidcontrol_play, mPlayDescription,
                        createPendingIntent(ListenerService.ACTION_PLAY));
            } else {
                // If we're here, the notification supports play/pause button and is playing.
                builder.addAction(R.drawable.ic_vidcontrol_pause, mPauseDescription,
                        createPendingIntent(ListenerService.ACTION_PAUSE));
            }
            style.setShowActionsInCompactView(0);
            style.setCancelButtonIntent(createPendingIntent(ListenerService.ACTION_STOP));
            style.setShowCancelButton(true);
            builder.setStyle(style);
        }

        if (mMediaNotificationInfo.supportsStop()) {
            builder.addAction(R.drawable.ic_vidcontrol_stop, mStopDescription,
                    createPendingIntent(ListenerService.ACTION_STOP));
        }
    }

    private void setCustomLayoutForNotificationBuilder(NotificationCompat.Builder builder) {
        builder.setContent(createContentView());
    }

    private RemoteViews createContentView() {
        RemoteViews contentView =
                new RemoteViews(mContext.getPackageName(), R.layout.playback_notification_bar);

        // By default, play/pause button is the only one.
        int playPauseButtonId = R.id.button1;
        // On Android pre-L, dismissing the notification when the service is no longer in foreground
        // doesn't work. Instead, a STOP button is shown.
        if (mMediaNotificationInfo.supportsSwipeAway()
                        && Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP
                || mMediaNotificationInfo.supportsStop()) {
            contentView.setOnClickPendingIntent(
                    R.id.button1, createPendingIntent(ListenerService.ACTION_STOP));
            contentView.setContentDescription(R.id.button1, mStopDescription);

            // If the play/pause needs to be shown, it moves over to the second button from the end.
            playPauseButtonId = R.id.button2;
        }

        contentView.setTextViewText(R.id.title, mMediaNotificationInfo.metadata.getTitle());
        contentView.setTextViewText(R.id.status, mMediaNotificationInfo.origin);

        // Android doesn't badge the icons for RemoteViews automatically when
        // running the app under the Work profile.
        if (mNotificationIcon == null) {
            Drawable notificationIconDrawable =
                    ApiCompatibilityUtils.getUserBadgedIcon(mContext, mMediaNotificationInfo.icon);
            mNotificationIcon = drawableToBitmap(notificationIconDrawable);
        }

        if (mNotificationIcon != null) {
            contentView.setImageViewBitmap(R.id.icon, mNotificationIcon);
        } else {
            contentView.setImageViewResource(R.id.icon, mMediaNotificationInfo.icon);
        }

        if (mMediaNotificationInfo.supportsPlayPause()) {
            if (mMediaNotificationInfo.isPaused) {
                contentView.setImageViewResource(playPauseButtonId, R.drawable.ic_vidcontrol_play);
                contentView.setContentDescription(playPauseButtonId, mPlayDescription);
                contentView.setOnClickPendingIntent(
                        playPauseButtonId, createPendingIntent(ListenerService.ACTION_PLAY));
            } else {
                // If we're here, the notification supports play/pause button and is playing.
                contentView.setImageViewResource(playPauseButtonId, R.drawable.ic_vidcontrol_pause);
                contentView.setContentDescription(playPauseButtonId, mPauseDescription);
                contentView.setOnClickPendingIntent(
                        playPauseButtonId, createPendingIntent(ListenerService.ACTION_PAUSE));
            }

            contentView.setViewVisibility(playPauseButtonId, View.VISIBLE);
        } else {
            contentView.setViewVisibility(playPauseButtonId, View.GONE);
        }

        return contentView;
    }

    private Bitmap drawableToBitmap(Drawable drawable) {
        if (!(drawable instanceof BitmapDrawable)) return null;

        BitmapDrawable bitmapDrawable = (BitmapDrawable) drawable;
        return bitmapDrawable.getBitmap();
    }

    private void setMediaStyleNotificationText(NotificationCompat.Builder builder) {
        builder.setContentTitle(mMediaNotificationInfo.metadata.getTitle());
        String artistAndAlbumText = getArtistAndAlbumText(mMediaNotificationInfo.metadata);
        // TODO(zqzhang): update this when N is released.
        if (TextUtils.equals(Build.VERSION.CODENAME, "N") || !artistAndAlbumText.isEmpty()) {
            builder.setContentText(artistAndAlbumText);
            builder.setSubText(mMediaNotificationInfo.origin);
        } else {
            // Leaving ContentText empty looks bad, so move origin up to the ContentText.
            builder.setContentText(mMediaNotificationInfo.origin);
        }
    }

    private String getArtistAndAlbumText(MediaMetadata metadata) {
        String artist = (metadata.getArtist() == null) ? "" : metadata.getArtist();
        String album = (metadata.getAlbum() == null) ? "" : metadata.getAlbum();
        if (artist.isEmpty() || album.isEmpty()) {
            return artist + album;
        }
        return artist + " - " + album;
    }
}
