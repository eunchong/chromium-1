// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_SHELL_RENDERER_SHELL_CONTENT_RENDERER_CLIENT_H_
#define CONTENT_SHELL_RENDERER_SHELL_CONTENT_RENDERER_CLIENT_H_

#include <memory>

#include "base/compiler_specific.h"
#include "content/public/renderer/content_renderer_client.h"

namespace web_cache {
class WebCacheImpl;
}

namespace content {

class ShellContentRendererClient : public ContentRendererClient {
 public:
  ShellContentRendererClient();
  ~ShellContentRendererClient() override;

  // ContentRendererClient implementation.
  void RenderThreadStarted() override;
  void RenderViewCreated(RenderView* render_view) override;

  // TODO(mkwst): These toggle based on the kEnablePepperTesting flag. Do we
  // need that outside of layout tests?
  bool IsPluginAllowedToUseCompositorAPI(const GURL& url) override;
  bool IsPluginAllowedToUseDevChannelAPIs() override;

  void DidInitializeWorkerContextOnWorkerThread(
      v8::Local<v8::Context> context) override;

 private:
  std::unique_ptr<web_cache::WebCacheImpl> web_cache_impl_;
};

}  // namespace content

#endif  // CONTENT_SHELL_RENDERER_SHELL_CONTENT_RENDERER_CLIENT_H_
