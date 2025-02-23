// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_MOJO_SERVICES_MOJO_RENDERER_SERVICE_H_
#define MEDIA_MOJO_SERVICES_MOJO_RENDERER_SERVICE_H_

#include <stdint.h>

#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "media/base/buffering_state.h"
#include "media/base/pipeline_status.h"
#include "media/base/renderer_client.h"
#include "media/mojo/interfaces/renderer.mojom.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace media {

class DemuxerStreamProviderShim;
class MediaKeys;
class MojoCdmServiceContext;
class Renderer;

// An interfaces::Renderer implementation that use a media::Renderer to render
// media streams.
class MojoRendererService : public interfaces::Renderer, public RendererClient {
 public:
  // |mojo_cdm_service_context| can be used to find the CDM to support
  // encrypted media. If null, encrypted media is not supported.
  MojoRendererService(
      base::WeakPtr<MojoCdmServiceContext> mojo_cdm_service_context,
      std::unique_ptr<media::Renderer> renderer,
      mojo::InterfaceRequest<interfaces::Renderer> request);
  ~MojoRendererService() final;

  // interfaces::Renderer implementation.
  void Initialize(interfaces::RendererClientPtr client,
                  interfaces::DemuxerStreamPtr audio,
                  interfaces::DemuxerStreamPtr video,
                  const mojo::Callback<void(bool)>& callback) final;
  void Flush(const mojo::Closure& callback) final;
  void StartPlayingFrom(int64_t time_delta_usec) final;
  void SetPlaybackRate(double playback_rate) final;
  void SetVolume(float volume) final;
  void SetCdm(int32_t cdm_id, const mojo::Callback<void(bool)>& callback) final;

 private:
  enum State {
    STATE_UNINITIALIZED,
    STATE_INITIALIZING,
    STATE_FLUSHING,
    STATE_PLAYING,
    STATE_ERROR
  };

  // RendererClient implementation.
  void OnError(PipelineStatus status) final;
  void OnEnded() final;
  void OnStatisticsUpdate(const PipelineStatistics& stats) final;
  void OnBufferingStateChange(BufferingState state) final;
  void OnWaitingForDecryptionKey() final;

  // Called when the DemuxerStreamProviderShim is ready to go (has a config,
  // pipe handle, etc) and can be handed off to a renderer for use.
  void OnStreamReady(const mojo::Callback<void(bool)>& callback);

  // Called when |audio_renderer_| initialization has completed.
  void OnRendererInitializeDone(const mojo::Callback<void(bool)>& callback,
                                PipelineStatus status);

  // Periodically polls the media time from the renderer and notifies the client
  // if the media time has changed since the last update.  If |force| is true,
  // the client is notified even if the time is unchanged.
  void UpdateMediaTime(bool force);
  void CancelPeriodicMediaTimeUpdates();
  void SchedulePeriodicMediaTimeUpdates();

  // Callback executed once Flush() completes.
  void OnFlushCompleted(const mojo::Closure& callback);

  // Callback executed once SetCdm() completes.
  void OnCdmAttached(scoped_refptr<MediaKeys> cdm,
                     const mojo::Callback<void(bool)>& callback,
                     bool success);

  mojo::StrongBinding<interfaces::Renderer> binding_;

  base::WeakPtr<MojoCdmServiceContext> mojo_cdm_service_context_;

  State state_;

  std::unique_ptr<DemuxerStreamProviderShim> stream_provider_;

  base::RepeatingTimer time_update_timer_;
  uint64_t last_media_time_usec_;

  interfaces::RendererClientPtr client_;

  // Hold a reference to the CDM set on the |renderer_| so that the CDM won't be
  // destructed while the |renderer_| is still using it.
  scoped_refptr<MediaKeys> cdm_;

  // Note: Destroy |renderer_| first to avoid access violation into other
  // members, e.g. |stream_provider_| and |cdm_|.
  // Must use "media::" because "Renderer" is ambiguous.
  std::unique_ptr<media::Renderer> renderer_;

  base::WeakPtr<MojoRendererService> weak_this_;
  base::WeakPtrFactory<MojoRendererService> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(MojoRendererService);
};

}  // namespace media

#endif  // MEDIA_MOJO_SERVICES_MOJO_RENDERER_SERVICE_H_
