// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/media/webrtc/mock_peer_connection_dependency_factory.h"

#include <stddef.h>

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "content/renderer/media/mock_peer_connection_impl.h"
#include "content/renderer/media/webrtc/webrtc_video_capturer_adapter.h"
#include "third_party/WebKit/public/platform/WebMediaStreamTrack.h"
#include "third_party/webrtc/api/mediastreaminterface.h"
#include "third_party/webrtc/base/scoped_ref_ptr.h"
#include "third_party/webrtc/media/base/videocapturer.h"

using webrtc::AudioSourceInterface;
using webrtc::AudioTrackInterface;
using webrtc::AudioTrackVector;
using webrtc::IceCandidateCollection;
using webrtc::IceCandidateInterface;
using webrtc::MediaStreamInterface;
using webrtc::ObserverInterface;
using webrtc::SessionDescriptionInterface;
using webrtc::VideoTrackSourceInterface;
using webrtc::VideoTrackInterface;
using webrtc::VideoTrackVector;

namespace content {

template <class V>
static typename V::iterator FindTrack(V* vector,
                                      const std::string& track_id) {
  typename V::iterator it = vector->begin();
  for (; it != vector->end(); ++it) {
    if ((*it)->id() == track_id) {
      break;
    }
  }
  return it;
};

MockMediaStream::MockMediaStream(const std::string& label) : label_(label) {}

bool MockMediaStream::AddTrack(AudioTrackInterface* track) {
  audio_track_vector_.push_back(track);
  NotifyObservers();
  return true;
}

bool MockMediaStream::AddTrack(VideoTrackInterface* track) {
  video_track_vector_.push_back(track);
  NotifyObservers();
  return true;
}

bool MockMediaStream::RemoveTrack(AudioTrackInterface* track) {
  AudioTrackVector::iterator it = FindTrack(&audio_track_vector_,
                                            track->id());
  if (it == audio_track_vector_.end())
    return false;
  audio_track_vector_.erase(it);
  NotifyObservers();
  return true;
}

bool MockMediaStream::RemoveTrack(VideoTrackInterface* track) {
  VideoTrackVector::iterator it = FindTrack(&video_track_vector_,
                                            track->id());
  if (it == video_track_vector_.end())
    return false;
  video_track_vector_.erase(it);
  NotifyObservers();
  return true;
}

std::string MockMediaStream::label() const {
  return label_;
}

AudioTrackVector MockMediaStream::GetAudioTracks() {
  return audio_track_vector_;
}

VideoTrackVector MockMediaStream::GetVideoTracks() {
  return video_track_vector_;
}

rtc::scoped_refptr<AudioTrackInterface> MockMediaStream::FindAudioTrack(
    const std::string& track_id) {
  AudioTrackVector::iterator it = FindTrack(&audio_track_vector_, track_id);
  return it == audio_track_vector_.end() ? NULL : *it;
}

rtc::scoped_refptr<VideoTrackInterface> MockMediaStream::FindVideoTrack(
    const std::string& track_id) {
  VideoTrackVector::iterator it = FindTrack(&video_track_vector_, track_id);
  return it == video_track_vector_.end() ? NULL : *it;
}

void MockMediaStream::RegisterObserver(ObserverInterface* observer) {
  DCHECK(observers_.find(observer) == observers_.end());
  observers_.insert(observer);
}

void MockMediaStream::UnregisterObserver(ObserverInterface* observer) {
  ObserverSet::iterator it = observers_.find(observer);
  DCHECK(it != observers_.end());
  observers_.erase(it);
}

void MockMediaStream::NotifyObservers() {
  for (ObserverSet::iterator it = observers_.begin(); it != observers_.end();
       ++it) {
    (*it)->OnChanged();
  }
}

MockMediaStream::~MockMediaStream() {}

class MockRtcVideoCapturer : public WebRtcVideoCapturerAdapter {
 public:
  explicit MockRtcVideoCapturer(bool is_screencast)
      : WebRtcVideoCapturerAdapter(is_screencast),
        number_of_capturered_frames_(0),
        width_(0),
        height_(0) {
  }

  void OnFrameCaptured(const scoped_refptr<media::VideoFrame>& frame) override {
    ++number_of_capturered_frames_;
    width_ = frame->visible_rect().width();
    height_ = frame->visible_rect().height();
  }

  int GetLastFrameWidth() const {
    return width_;
  }

  int GetLastFrameHeight() const {
    return height_;
  }

  int GetFrameNum() const {
    return number_of_capturered_frames_;
  }

 private:
  int number_of_capturered_frames_;
  int width_;
  int height_;
};

MockAudioSource::MockAudioSource(const cricket::AudioOptions& options,
                                 bool remote)
    : remote_(remote), state_(MediaSourceInterface::kLive) {}

MockAudioSource::~MockAudioSource() {}

void MockAudioSource::RegisterObserver(webrtc::ObserverInterface* observer) {
  DCHECK(observers_.find(observer) == observers_.end());
  observers_.insert(observer);
}

void MockAudioSource::UnregisterObserver(webrtc::ObserverInterface* observer) {
  DCHECK(observers_.find(observer) != observers_.end());
  observers_.erase(observer);
}

webrtc::MediaSourceInterface::SourceState MockAudioSource::state() const {
  return state_;
}

bool MockAudioSource::remote() const {
  return remote_;
}

scoped_refptr<MockWebRtcAudioTrack> MockWebRtcAudioTrack::Create(
    const std::string& id) {
  return new rtc::RefCountedObject<MockWebRtcAudioTrack>(id);
}

MockWebRtcAudioTrack::MockWebRtcAudioTrack(const std::string& id)
    : id_(id),
      enabled_(true),
      state_(webrtc::MediaStreamTrackInterface::kLive) {}

MockWebRtcAudioTrack::~MockWebRtcAudioTrack() {}

std::string MockWebRtcAudioTrack::kind() const {
  return kAudioKind;
}

webrtc::AudioSourceInterface* MockWebRtcAudioTrack::GetSource() const {
  NOTREACHED();
  return nullptr;
}

std::string MockWebRtcAudioTrack::id() const {
  return id_;
}

bool MockWebRtcAudioTrack::enabled() const {
  return enabled_;
}

MockWebRtcVideoTrack::TrackState MockWebRtcAudioTrack::state() const {
  return state_;
}

bool MockWebRtcAudioTrack::set_enabled(bool enable) {
  enabled_ = enable;
  return true;
}

void MockWebRtcAudioTrack::RegisterObserver(ObserverInterface* observer) {
  DCHECK(observers_.find(observer) == observers_.end());
  observers_.insert(observer);
}

void MockWebRtcAudioTrack::UnregisterObserver(ObserverInterface* observer) {
  DCHECK(observers_.find(observer) != observers_.end());
  observers_.erase(observer);
}

void MockWebRtcAudioTrack::SetEnded() {
  DCHECK_EQ(webrtc::MediaStreamTrackInterface::kLive, state_);
  state_ = webrtc::MediaStreamTrackInterface::kEnded;
  for (auto& o : observers_)
    o->OnChanged();
}

MockWebRtcVideoTrack::MockWebRtcVideoTrack(
    const std::string& id,
    webrtc::VideoTrackSourceInterface* source)
    : id_(id),
      source_(source),
      enabled_(true),
      state_(webrtc::MediaStreamTrackInterface::kLive),
      sink_(NULL) {}

MockWebRtcVideoTrack::~MockWebRtcVideoTrack() {}

scoped_refptr<MockWebRtcVideoTrack> MockWebRtcVideoTrack::Create(
    const std::string& id) {
  return new rtc::RefCountedObject<MockWebRtcVideoTrack>(id, nullptr);
}

void MockWebRtcVideoTrack::AddOrUpdateSink(
    rtc::VideoSinkInterface<cricket::VideoFrame>* sink,
    const rtc::VideoSinkWants& wants) {
  DCHECK(!sink_);
  sink_ = sink;
}

void MockWebRtcVideoTrack::RemoveSink(
    rtc::VideoSinkInterface<cricket::VideoFrame>* sink) {
  DCHECK(sink_ == sink);
  sink_ = NULL;
}

VideoTrackSourceInterface* MockWebRtcVideoTrack::GetSource() const {
  return source_.get();
}

std::string MockWebRtcVideoTrack::kind() const {
  return kVideoKind;
}

std::string MockWebRtcVideoTrack::id() const { return id_; }

bool MockWebRtcVideoTrack::enabled() const { return enabled_; }

MockWebRtcVideoTrack::TrackState MockWebRtcVideoTrack::state() const {
  return state_;
}

bool MockWebRtcVideoTrack::set_enabled(bool enable) {
  enabled_ = enable;
  return true;
}

void MockWebRtcVideoTrack::RegisterObserver(ObserverInterface* observer) {
  DCHECK(observers_.find(observer) == observers_.end());
  observers_.insert(observer);
}

void MockWebRtcVideoTrack::UnregisterObserver(ObserverInterface* observer) {
  DCHECK(observers_.find(observer) != observers_.end());
  observers_.erase(observer);
}

void MockWebRtcVideoTrack::SetEnded() {
  DCHECK_EQ(webrtc::MediaStreamTrackInterface::kLive, state_);
  state_ = webrtc::MediaStreamTrackInterface::kEnded;
  for (auto& o : observers_)
    o->OnChanged();
}

class MockSessionDescription : public SessionDescriptionInterface {
 public:
  MockSessionDescription(const std::string& type,
                         const std::string& sdp)
      : type_(type),
        sdp_(sdp) {
  }
  ~MockSessionDescription() override {}
  cricket::SessionDescription* description() override {
    NOTIMPLEMENTED();
    return NULL;
  }
  const cricket::SessionDescription* description() const override {
    NOTIMPLEMENTED();
    return NULL;
  }
  std::string session_id() const override {
    NOTIMPLEMENTED();
    return std::string();
  }
  std::string session_version() const override {
    NOTIMPLEMENTED();
    return std::string();
  }
  std::string type() const override { return type_; }
  bool AddCandidate(const IceCandidateInterface* candidate) override {
    NOTIMPLEMENTED();
    return false;
  }
  size_t number_of_mediasections() const override {
    NOTIMPLEMENTED();
    return 0;
  }
  const IceCandidateCollection* candidates(
      size_t mediasection_index) const override {
    NOTIMPLEMENTED();
    return NULL;
  }

  bool ToString(std::string* out) const override {
    *out = sdp_;
    return true;
  }

 private:
  std::string type_;
  std::string sdp_;
};

class MockIceCandidate : public IceCandidateInterface {
 public:
  MockIceCandidate(const std::string& sdp_mid,
                   int sdp_mline_index,
                   const std::string& sdp)
      : sdp_mid_(sdp_mid),
        sdp_mline_index_(sdp_mline_index),
        sdp_(sdp) {
    // Assign an valid address to |candidate_| to pass assert in code.
    candidate_.set_address(rtc::SocketAddress("127.0.0.1", 5000));
  }
  ~MockIceCandidate() override {}
  std::string sdp_mid() const override { return sdp_mid_; }
  int sdp_mline_index() const override { return sdp_mline_index_; }
  const cricket::Candidate& candidate() const override { return candidate_; }
  bool ToString(std::string* out) const override {
    *out = sdp_;
    return true;
  }

 private:
  std::string sdp_mid_;
  int sdp_mline_index_;
  std::string sdp_;
  cricket::Candidate candidate_;
};

MockPeerConnectionDependencyFactory::MockPeerConnectionDependencyFactory()
    : PeerConnectionDependencyFactory(NULL),
      signaling_thread_("MockPCFactory WebRtc Signaling Thread") {
  EnsureWebRtcAudioDeviceImpl();
  CHECK(signaling_thread_.Start());
}

MockPeerConnectionDependencyFactory::~MockPeerConnectionDependencyFactory() {}

scoped_refptr<webrtc::PeerConnectionInterface>
MockPeerConnectionDependencyFactory::CreatePeerConnection(
    const webrtc::PeerConnectionInterface::RTCConfiguration& config,
    blink::WebFrame* frame,
    webrtc::PeerConnectionObserver* observer) {
  return new rtc::RefCountedObject<MockPeerConnectionImpl>(this, observer);
}

scoped_refptr<webrtc::AudioSourceInterface>
MockPeerConnectionDependencyFactory::CreateLocalAudioSource(
    const cricket::AudioOptions& options) {
  last_audio_source_ =
      new rtc::RefCountedObject<MockAudioSource>(options, false);
  return last_audio_source_;
}

WebRtcVideoCapturerAdapter*
MockPeerConnectionDependencyFactory::CreateVideoCapturer(
    bool is_screen_capture) {
  return new MockRtcVideoCapturer(is_screen_capture);
}

scoped_refptr<webrtc::VideoTrackSourceInterface>
MockPeerConnectionDependencyFactory::CreateVideoSource(
    cricket::VideoCapturer* capturer) {
  // Video source normally take ownership of |capturer|.
  delete capturer;
  NOTIMPLEMENTED();
  return nullptr;
}

scoped_refptr<webrtc::MediaStreamInterface>
MockPeerConnectionDependencyFactory::CreateLocalMediaStream(
    const std::string& label) {
  return new rtc::RefCountedObject<MockMediaStream>(label);
}

scoped_refptr<webrtc::VideoTrackInterface>
MockPeerConnectionDependencyFactory::CreateLocalVideoTrack(
    const std::string& id,
    webrtc::VideoTrackSourceInterface* source) {
  scoped_refptr<webrtc::VideoTrackInterface> track(
      new rtc::RefCountedObject<MockWebRtcVideoTrack>(
          id, source));
  return track;
}

scoped_refptr<webrtc::VideoTrackInterface>
MockPeerConnectionDependencyFactory::CreateLocalVideoTrack(
    const std::string& id,
    cricket::VideoCapturer* capturer) {
  return new rtc::RefCountedObject<MockWebRtcVideoTrack>(id, nullptr);
}

SessionDescriptionInterface*
MockPeerConnectionDependencyFactory::CreateSessionDescription(
    const std::string& type,
    const std::string& sdp,
    webrtc::SdpParseError* error) {
  return new MockSessionDescription(type, sdp);
}

webrtc::IceCandidateInterface*
MockPeerConnectionDependencyFactory::CreateIceCandidate(
    const std::string& sdp_mid,
    int sdp_mline_index,
    const std::string& sdp) {
  return new MockIceCandidate(sdp_mid, sdp_mline_index, sdp);
}

scoped_refptr<base::SingleThreadTaskRunner>
MockPeerConnectionDependencyFactory::GetWebRtcSignalingThread() const {
  return signaling_thread_.task_runner();
}

}  // namespace content
