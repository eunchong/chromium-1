// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/webrtc/webrtc_audio_sink.h"

#include <algorithm>
#include <limits>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"

namespace content {

WebRtcAudioSink::WebRtcAudioSink(
    const std::string& label,
    scoped_refptr<webrtc::AudioSourceInterface> track_source,
    scoped_refptr<base::SingleThreadTaskRunner> signaling_task_runner)
    : adapter_(new rtc::RefCountedObject<Adapter>(
          label, std::move(track_source), std::move(signaling_task_runner))),
      fifo_(base::Bind(&WebRtcAudioSink::DeliverRebufferedAudio,
                       base::Unretained(this))) {
  DVLOG(1) << "WebRtcAudioSink::WebRtcAudioSink()";
}

WebRtcAudioSink::~WebRtcAudioSink() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DVLOG(1) << "WebRtcAudioSink::~WebRtcAudioSink()";
}

void WebRtcAudioSink::SetAudioProcessor(
    scoped_refptr<MediaStreamAudioProcessor> processor) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(processor.get());
  adapter_->set_processor(std::move(processor));
}

void WebRtcAudioSink::SetLevel(
    scoped_refptr<MediaStreamAudioLevelCalculator::Level> level) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(level.get());
  adapter_->set_level(std::move(level));
}

void WebRtcAudioSink::OnEnabledChanged(bool enabled) {
  DCHECK(thread_checker_.CalledOnValidThread());
  adapter_->signaling_task_runner()->PostTask(
      FROM_HERE,
      base::Bind(
          base::IgnoreResult(&WebRtcAudioSink::Adapter::set_enabled),
          adapter_, enabled));
}

void WebRtcAudioSink::OnData(const media::AudioBus& audio_bus,
                             base::TimeTicks estimated_capture_time) {
  DCHECK(audio_thread_checker_.CalledOnValidThread());
  // The following will result in zero, one, or multiple synchronous calls to
  // DeliverRebufferedAudio().
  fifo_.Push(audio_bus);
}

void WebRtcAudioSink::OnSetFormat(const media::AudioParameters& params) {
  // On a format change, the thread delivering audio might have also changed.
  audio_thread_checker_.DetachFromThread();
  DCHECK(audio_thread_checker_.CalledOnValidThread());

  DCHECK(params.IsValid());
  params_ = params;
  fifo_.Reset(params_.frames_per_buffer());
  const int num_pcm16_data_elements =
      params_.frames_per_buffer() * params_.channels();
  interleaved_data_.reset(new int16_t[num_pcm16_data_elements]);
}

void WebRtcAudioSink::DeliverRebufferedAudio(const media::AudioBus& audio_bus,
                                             int frame_delay) {
  DCHECK(audio_thread_checker_.CalledOnValidThread());
  DCHECK(params_.IsValid());

  // TODO(miu): Why doesn't a WebRTC sink care about reference time passed to
  // OnData(), and the |frame_delay| here?  How is AV sync achieved otherwise?

  // TODO(henrika): Remove this conversion once the interface in libjingle
  // supports float vectors.
  audio_bus.ToInterleaved(audio_bus.frames(),
                          sizeof(interleaved_data_[0]),
                          interleaved_data_.get());
  adapter_->DeliverPCMToWebRtcSinks(interleaved_data_.get(),
                                    params_.sample_rate(),
                                    audio_bus.channels(),
                                    audio_bus.frames());
}

namespace {
// TODO(miu): MediaStreamAudioProcessor destructor requires this nonsense.
void DereferenceOnMainThread(
    const scoped_refptr<MediaStreamAudioProcessor>& processor) {}
}  // namespace

WebRtcAudioSink::Adapter::Adapter(
    const std::string& label,
    scoped_refptr<webrtc::AudioSourceInterface> source,
    scoped_refptr<base::SingleThreadTaskRunner> signaling_task_runner)
    : webrtc::MediaStreamTrack<webrtc::AudioTrackInterface>(label),
      source_(std::move(source)),
      signaling_task_runner_(std::move(signaling_task_runner)),
      main_task_runner_(base::MessageLoop::current()->task_runner()) {
  DCHECK(signaling_task_runner_);
}

WebRtcAudioSink::Adapter::~Adapter() {
  if (audio_processor_) {
    main_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&DereferenceOnMainThread, std::move(audio_processor_)));
  }
}

void WebRtcAudioSink::Adapter::DeliverPCMToWebRtcSinks(
    const int16_t* audio_data,
    int sample_rate,
    size_t number_of_channels,
    size_t number_of_frames) {
  base::AutoLock auto_lock(lock_);
  for (webrtc::AudioTrackSinkInterface* sink : sinks_) {
    sink->OnData(audio_data, sizeof(int16_t) * 8, sample_rate,
                 number_of_channels, number_of_frames);
  }
}

std::string WebRtcAudioSink::Adapter::kind() const {
  return webrtc::MediaStreamTrackInterface::kAudioKind;
}

bool WebRtcAudioSink::Adapter::set_enabled(bool enable) {
  DCHECK(!signaling_task_runner_ ||
         signaling_task_runner_->RunsTasksOnCurrentThread());
  return webrtc::MediaStreamTrack<webrtc::AudioTrackInterface>::
      set_enabled(enable);
}

void WebRtcAudioSink::Adapter::AddSink(webrtc::AudioTrackSinkInterface* sink) {
  DCHECK(!signaling_task_runner_ ||
         signaling_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(sink);
  base::AutoLock auto_lock(lock_);
  DCHECK(std::find(sinks_.begin(), sinks_.end(), sink) == sinks_.end());
  sinks_.push_back(sink);
}

void WebRtcAudioSink::Adapter::RemoveSink(
    webrtc::AudioTrackSinkInterface* sink) {
  DCHECK(!signaling_task_runner_ ||
         signaling_task_runner_->RunsTasksOnCurrentThread());
  base::AutoLock auto_lock(lock_);
  const auto it = std::find(sinks_.begin(), sinks_.end(), sink);
  if (it != sinks_.end())
    sinks_.erase(it);
}

bool WebRtcAudioSink::Adapter::GetSignalLevel(int* level) {
  DCHECK(!signaling_task_runner_ ||
         signaling_task_runner_->RunsTasksOnCurrentThread());

  // |level_| is only set once, so it's safe to read without first acquiring a
  // mutex.
  if (!level_)
    return false;
  const float signal_level = level_->GetCurrent();
  DCHECK_GE(signal_level, 0.0f);
  DCHECK_LE(signal_level, 1.0f);
  // Convert from float in range [0.0,1.0] to an int in range [0,32767].
  *level = static_cast<int>(signal_level * std::numeric_limits<int16_t>::max() +
                            0.5f /* rounding to nearest int */);
  return true;
}

rtc::scoped_refptr<webrtc::AudioProcessorInterface>
WebRtcAudioSink::Adapter::GetAudioProcessor() {
  DCHECK(!signaling_task_runner_ ||
         signaling_task_runner_->RunsTasksOnCurrentThread());
  return audio_processor_.get();
}

webrtc::AudioSourceInterface* WebRtcAudioSink::Adapter::GetSource() const {
  DCHECK(!signaling_task_runner_ ||
         signaling_task_runner_->RunsTasksOnCurrentThread());
  return source_.get();
}

}  // namespace content
