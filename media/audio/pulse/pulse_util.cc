// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/audio/pulse/pulse_util.h"

#include <stdint.h>

#include "base/logging.h"
#include "base/macros.h"
#include "base/time/time.h"
#include "media/audio/audio_device_description.h"
#include "media/base/audio_parameters.h"

namespace media {

namespace pulse {

namespace {

#if defined(GOOGLE_CHROME_BUILD)
static const char kBrowserDisplayName[] = "google-chrome";
#else
static const char kBrowserDisplayName[] = "chromium-browser";
#endif

pa_channel_position ChromiumToPAChannelPosition(Channels channel) {
  switch (channel) {
    // PulseAudio does not differentiate between left/right and
    // stereo-left/stereo-right, both translate to front-left/front-right.
    case LEFT:
      return PA_CHANNEL_POSITION_FRONT_LEFT;
    case RIGHT:
      return PA_CHANNEL_POSITION_FRONT_RIGHT;
    case CENTER:
      return PA_CHANNEL_POSITION_FRONT_CENTER;
    case LFE:
      return PA_CHANNEL_POSITION_LFE;
    case BACK_LEFT:
      return PA_CHANNEL_POSITION_REAR_LEFT;
    case BACK_RIGHT:
      return PA_CHANNEL_POSITION_REAR_RIGHT;
    case LEFT_OF_CENTER:
      return PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER;
    case RIGHT_OF_CENTER:
      return PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER;
    case BACK_CENTER:
      return PA_CHANNEL_POSITION_REAR_CENTER;
    case SIDE_LEFT:
      return PA_CHANNEL_POSITION_SIDE_LEFT;
    case SIDE_RIGHT:
      return PA_CHANNEL_POSITION_SIDE_RIGHT;
    default:
      NOTREACHED() << "Invalid channel: " << channel;
      return PA_CHANNEL_POSITION_INVALID;
  }
}

class ScopedPropertyList {
 public:
  ScopedPropertyList() : property_list_(pa_proplist_new()) {}
  ~ScopedPropertyList() { pa_proplist_free(property_list_); }

  pa_proplist* get() const { return property_list_; }

 private:
  pa_proplist* property_list_;
  DISALLOW_COPY_AND_ASSIGN(ScopedPropertyList);
};

}  // namespace

// static, pa_stream_success_cb_t
void StreamSuccessCallback(pa_stream* s, int error, void* mainloop) {
  pa_threaded_mainloop* pa_mainloop =
      static_cast<pa_threaded_mainloop*>(mainloop);
  pa_threaded_mainloop_signal(pa_mainloop, 0);
}

// |pa_context| and |pa_stream| state changed cb.
void ContextStateCallback(pa_context* context, void* mainloop) {
  pa_threaded_mainloop* pa_mainloop =
      static_cast<pa_threaded_mainloop*>(mainloop);
  pa_threaded_mainloop_signal(pa_mainloop, 0);
}

pa_sample_format_t BitsToPASampleFormat(int bits_per_sample) {
  switch (bits_per_sample) {
    case 8:
      return PA_SAMPLE_U8;
    case 16:
      return PA_SAMPLE_S16LE;
    case 24:
      return PA_SAMPLE_S24LE;
    case 32:
      return PA_SAMPLE_S32LE;
    default:
      NOTREACHED() << "Invalid bits per sample: " << bits_per_sample;
      return PA_SAMPLE_INVALID;
  }
}

pa_channel_map ChannelLayoutToPAChannelMap(ChannelLayout channel_layout) {
  pa_channel_map channel_map;
  if (channel_layout == CHANNEL_LAYOUT_MONO) {
    // CHANNEL_LAYOUT_MONO only specifies audio on the C channel, but we
    // want PulseAudio to play single-channel audio on more than just that.
    pa_channel_map_init_mono(&channel_map);
  } else {
    pa_channel_map_init(&channel_map);

    channel_map.channels = ChannelLayoutToChannelCount(channel_layout);
    for (Channels ch = LEFT; ch <= CHANNELS_MAX;
         ch = static_cast<Channels>(ch + 1)) {
      int channel_index = ChannelOrder(channel_layout, ch);
      if (channel_index < 0)
        continue;

      channel_map.map[channel_index] = ChromiumToPAChannelPosition(ch);
    }
  }

  return channel_map;
}

void WaitForOperationCompletion(pa_threaded_mainloop* pa_mainloop,
                                pa_operation* operation) {
  if (!operation) {
    DLOG(WARNING) << "Operation is NULL";
    return;
  }

  while (pa_operation_get_state(operation) == PA_OPERATION_RUNNING)
    pa_threaded_mainloop_wait(pa_mainloop);

  pa_operation_unref(operation);
}

int GetHardwareLatencyInBytes(pa_stream* stream,
                              int sample_rate,
                              int bytes_per_frame) {
  DCHECK(stream);
  int negative = 0;
  pa_usec_t latency_micros = 0;
  if (pa_stream_get_latency(stream, &latency_micros, &negative) != 0)
    return 0;

  if (negative)
    return 0;

  return latency_micros * sample_rate * bytes_per_frame /
      base::Time::kMicrosecondsPerSecond;
}

// Helper macro for CreateInput/OutputStream() to avoid code spam and
// string bloat.
#define RETURN_ON_FAILURE(expression, message) do { \
  if (!(expression)) { \
    DLOG(ERROR) << message; \
    return false; \
  } \
} while (0)

bool CreateInputStream(pa_threaded_mainloop* mainloop,
                       pa_context* context,
                       pa_stream** stream,
                       const AudioParameters& params,
                       const std::string& device_id,
                       pa_stream_notify_cb_t stream_callback,
                       void* user_data) {
  DCHECK(mainloop);
  DCHECK(context);
  DCHECK_NE(device_id, AudioDeviceDescription::kDefaultDeviceId);

  // Set sample specifications.
  pa_sample_spec sample_specifications;
  sample_specifications.format = BitsToPASampleFormat(
      params.bits_per_sample());
  sample_specifications.rate = params.sample_rate();
  sample_specifications.channels = params.channels();

  // Get channel mapping and open recording stream.
  pa_channel_map source_channel_map = ChannelLayoutToPAChannelMap(
      params.channel_layout());
  pa_channel_map* map = (source_channel_map.channels != 0) ?
      &source_channel_map : NULL;

  // Create a new recording stream and
  // tells PulseAudio what the stream icon should be.
  ScopedPropertyList property_list;
  pa_proplist_sets(property_list.get(), PA_PROP_APPLICATION_ICON_NAME,
                   kBrowserDisplayName);
  *stream = pa_stream_new_with_proplist(context, "RecordStream",
                                        &sample_specifications, map,
                                        property_list.get());
  RETURN_ON_FAILURE(*stream, "failed to create PA recording stream");

  pa_stream_set_state_callback(*stream, stream_callback, user_data);

  // Set server-side capture buffer metrics. Detailed documentation on what
  // values should be chosen can be found at
  // freedesktop.org/software/pulseaudio/doxygen/structpa__buffer__attr.html.
  pa_buffer_attr buffer_attributes;
  const unsigned int buffer_size = params.GetBytesPerBuffer();
  buffer_attributes.maxlength = static_cast<uint32_t>(-1);
  buffer_attributes.tlength = buffer_size;
  buffer_attributes.minreq = buffer_size;
  buffer_attributes.prebuf = static_cast<uint32_t>(-1);
  buffer_attributes.fragsize = buffer_size;
  int flags = PA_STREAM_AUTO_TIMING_UPDATE |
              PA_STREAM_INTERPOLATE_TIMING |
              PA_STREAM_ADJUST_LATENCY |
              PA_STREAM_START_CORKED;
  RETURN_ON_FAILURE(
      pa_stream_connect_record(*stream, device_id.c_str(), &buffer_attributes,
                               static_cast<pa_stream_flags_t>(flags)) == 0,
      "pa_stream_connect_record FAILED ");

  // Wait for the stream to be ready.
  while (true) {
    pa_stream_state_t stream_state = pa_stream_get_state(*stream);
    RETURN_ON_FAILURE(
        PA_STREAM_IS_GOOD(stream_state), "Invalid PulseAudio stream state");
    if (stream_state == PA_STREAM_READY)
        break;
    pa_threaded_mainloop_wait(mainloop);
  }

  return true;
}

bool CreateOutputStream(pa_threaded_mainloop* mainloop,
                        pa_context* context,
                        pa_stream** stream,
                        const AudioParameters& params,
                        const std::string& device_id,
                        const std::string& app_name,
                        pa_stream_notify_cb_t stream_callback,
                        pa_stream_request_cb_t write_callback,
                        void* user_data) {
  DCHECK(mainloop);
  DCHECK(context);
  DCHECK(device_id != AudioDeviceDescription::kDefaultDeviceId);

  // Set sample specifications.
  pa_sample_spec sample_specifications;
  sample_specifications.format = BitsToPASampleFormat(
      params.bits_per_sample());
  sample_specifications.rate = params.sample_rate();
  sample_specifications.channels = params.channels();

  // Get channel mapping.
  pa_channel_map* map = NULL;
  pa_channel_map source_channel_map = ChannelLayoutToPAChannelMap(
      params.channel_layout());
  if (source_channel_map.channels != 0) {
    // The source data uses a supported channel map so we will use it rather
    // than the default channel map (NULL).
    map = &source_channel_map;
  }

  // Open playback stream and
  // tell PulseAudio what the stream icon should be.
  ScopedPropertyList property_list;
  pa_proplist_sets(property_list.get(), PA_PROP_APPLICATION_ICON_NAME,
                   kBrowserDisplayName);
  *stream = pa_stream_new_with_proplist(
      context, "Playback", &sample_specifications, map, property_list.get());
  RETURN_ON_FAILURE(*stream, "failed to create PA playback stream");

  pa_stream_set_state_callback(*stream, stream_callback, user_data);

  // Even though we start the stream corked above, PulseAudio will issue one
  // stream request after setup.  write_callback() must fulfill the write.
  pa_stream_set_write_callback(*stream, write_callback, user_data);

  // Pulse is very finicky with the small buffer sizes used by Chrome.  The
  // settings below are mostly found through trial and error.  Essentially we
  // want Pulse to auto size its internal buffers, but call us back nearly every
  // |minreq| bytes.  |tlength| should be a multiple of |minreq|; too low and
  // Pulse will issue callbacks way too fast, too high and we don't get
  // callbacks frequently enough.
  //
  // Setting |minreq| to the exact buffer size leads to more callbacks than
  // necessary, so we've clipped it to half the buffer size.  Regardless of the
  // requested amount, we'll always fill |params.GetBytesPerBuffer()| though.
  pa_buffer_attr pa_buffer_attributes;
  pa_buffer_attributes.maxlength = static_cast<uint32_t>(-1);
  pa_buffer_attributes.minreq = params.GetBytesPerBuffer() / 2;
  pa_buffer_attributes.prebuf = static_cast<uint32_t>(-1);
  pa_buffer_attributes.tlength = params.GetBytesPerBuffer() * 3;
  pa_buffer_attributes.fragsize = static_cast<uint32_t>(-1);

  // Connect playback stream.  Like pa_buffer_attr, the pa_stream_flags have a
  // huge impact on the performance of the stream and were chosen through trial
  // and error.
  RETURN_ON_FAILURE(
      pa_stream_connect_playback(
          *stream, device_id.c_str(), &pa_buffer_attributes,
          static_cast<pa_stream_flags_t>(
              PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_ADJUST_LATENCY |
              PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_NOT_MONOTONIC |
              PA_STREAM_START_CORKED),
          NULL, NULL) == 0,
      "pa_stream_connect_playback FAILED ");

  // Wait for the stream to be ready.
  while (true) {
    pa_stream_state_t stream_state = pa_stream_get_state(*stream);
    RETURN_ON_FAILURE(
        PA_STREAM_IS_GOOD(stream_state), "Invalid PulseAudio stream state");
    if (stream_state == PA_STREAM_READY)
      break;
    pa_threaded_mainloop_wait(mainloop);
  }

  return true;
}

#undef RETURN_ON_FAILURE

}  // namespace pulse

}  // namespace media
