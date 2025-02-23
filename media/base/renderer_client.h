// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_RENDERER_CLIENT_H_
#define MEDIA_BASE_RENDERER_CLIENT_H_

namespace media {

// Interface used by Renderer, AudioRenderer, and VideoRenderer implementations
// to notify their clients.
class RendererClient {
 public:
  // Executed if any error was encountered after Renderer initialization.
  virtual void OnError(PipelineStatus status) = 0;

  // Executed when rendering has reached the end of stream.
  virtual void OnEnded() = 0;

  // Executed periodically with rendering statistics.
  virtual void OnStatisticsUpdate(const PipelineStatistics& stats) = 0;

  // Executed when buffering state is changed.
  virtual void OnBufferingStateChange(BufferingState state) = 0;

  // Executed whenever the key needed to decrypt the stream is not available.
  virtual void OnWaitingForDecryptionKey() = 0;
};

}  // namespace media

#endif  // MEDIA_BASE_RENDERER_CLIENT_H_
