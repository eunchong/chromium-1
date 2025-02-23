// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/aw_render_thread_context_provider.h"

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/lazy_instance.h"
#include "base/trace_event/trace_event.h"
#include "cc/output/managed_memory_policy.h"
#include "gpu/command_buffer/client/gl_in_process_context.h"
#include "gpu/command_buffer/client/gles2_implementation.h"
#include "gpu/command_buffer/client/gles2_lib.h"
#include "gpu/command_buffer/client/shared_memory_limits.h"
#include "gpu/skia_bindings/gl_bindings_skia_cmd_buffer.h"
#include "third_party/skia/include/gpu/GrContext.h"
#include "third_party/skia/include/gpu/gl/GrGLInterface.h"

namespace android_webview {

// static
scoped_refptr<AwRenderThreadContextProvider>
AwRenderThreadContextProvider::Create(
    scoped_refptr<gfx::GLSurface> surface,
    scoped_refptr<gpu::InProcessCommandBuffer::Service> service) {
  return new AwRenderThreadContextProvider(surface, service);
}

AwRenderThreadContextProvider::AwRenderThreadContextProvider(
    scoped_refptr<gfx::GLSurface> surface,
    scoped_refptr<gpu::InProcessCommandBuffer::Service> service) {
  DCHECK(main_thread_checker_.CalledOnValidThread());

  // This is an onscreen context, wrapping the GLSurface given to us from
  // the Android OS. The widget we pass here will be ignored since we're
  // providing the GLSurface to the context already.
  DCHECK(!surface->IsOffscreen());
  gpu::gles2::ContextCreationAttribHelper attributes;
  // The context is wrapping an already allocated surface, so we can't control
  // what buffers it has from these attributes. We do expect an alpha and
  // stencil buffer to exist for webview, as the display compositor requires
  // having them both in order to integrate its output with the content behind
  // it.
  attributes.alpha_size = 8;
  attributes.stencil_size = 8;
  // The depth buffer may exist due to having a stencil buffer, but we don't
  // need one, so use -1 for it.
  attributes.depth_size = -1;
  attributes.samples = 0;
  attributes.sample_buffers = 0;
  attributes.bind_generates_resource = false;

  gpu::SharedMemoryLimits limits;
  // This context is only used for the display compositor, and there are no
  // uploads done with it at all. We choose a small transfer buffer limit
  // here, the minimums match the display compositor context for the android
  // browser. We don't set the max since we expect the transfer buffer to be
  // relatively unused.
  limits.start_transfer_buffer_size = 64 * 1024;
  limits.min_transfer_buffer_size = 64 * 1024;

  context_.reset(gpu::GLInProcessContext::Create(
      service, surface, surface->IsOffscreen(), gfx::kNullAcceleratedWidget,
      surface->GetSize(), nullptr /* share_context */, attributes,
      gfx::PreferDiscreteGpu, limits, nullptr, nullptr));

  context_->GetImplementation()->SetLostContextCallback(base::Bind(
      &AwRenderThreadContextProvider::OnLostContext, base::Unretained(this)));
}

AwRenderThreadContextProvider::~AwRenderThreadContextProvider() {
  DCHECK(main_thread_checker_.CalledOnValidThread());
  if (gr_context_)
    gr_context_->releaseResourcesAndAbandonContext();
}

bool AwRenderThreadContextProvider::BindToCurrentThread() {
  // This is called on the thread the context will be used.
  DCHECK(main_thread_checker_.CalledOnValidThread());

  return true;
}

gpu::Capabilities AwRenderThreadContextProvider::ContextCapabilities() {
  DCHECK(main_thread_checker_.CalledOnValidThread());
  return context_->GetImplementation()->capabilities();
}

gpu::gles2::GLES2Interface* AwRenderThreadContextProvider::ContextGL() {
  DCHECK(main_thread_checker_.CalledOnValidThread());

  return context_->GetImplementation();
}

gpu::ContextSupport* AwRenderThreadContextProvider::ContextSupport() {
  DCHECK(main_thread_checker_.CalledOnValidThread());

  return context_->GetImplementation();
}

class GrContext* AwRenderThreadContextProvider::GrContext() {
  DCHECK(main_thread_checker_.CalledOnValidThread());

  if (gr_context_)
    return gr_context_.get();

  sk_sp<GrGLInterface> interface(
      skia_bindings::CreateGLES2InterfaceBindings(ContextGL()));
  gr_context_ = sk_sp<::GrContext>(GrContext::Create(
      // GrContext takes ownership of |interface|.
      kOpenGL_GrBackend, reinterpret_cast<GrBackendContext>(interface.get())));
  return gr_context_.get();
}

void AwRenderThreadContextProvider::InvalidateGrContext(uint32_t state) {
  DCHECK(main_thread_checker_.CalledOnValidThread());

  if (gr_context_)
    gr_context_->resetContext(state);
}

base::Lock* AwRenderThreadContextProvider::GetLock() {
  // This context provider is not used on multiple threads.
  NOTREACHED();
  return nullptr;
}

void AwRenderThreadContextProvider::DeleteCachedResources() {
  DCHECK(main_thread_checker_.CalledOnValidThread());

  if (gr_context_) {
    TRACE_EVENT_INSTANT0("gpu", "GrContext::freeGpuResources",
                         TRACE_EVENT_SCOPE_THREAD);
    gr_context_->freeGpuResources();
  }
}

void AwRenderThreadContextProvider::SetLostContextCallback(
    const LostContextCallback& lost_context_callback) {
  lost_context_callback_ = lost_context_callback;
}

void AwRenderThreadContextProvider::OnLostContext() {
  DCHECK(main_thread_checker_.CalledOnValidThread());

  if (!lost_context_callback_.is_null())
    lost_context_callback_.Run();
  if (gr_context_)
    gr_context_->abandonContext();
}

}  // namespace android_webview
