// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ui/zoom/zoom_event_manager.h"

#include "components/ui/zoom/zoom_event_manager_observer.h"
#include "content/public/browser/browser_context.h"

namespace {
static const char kBrowserZoomEventManager[] = "browser_zoom_event_manager";
}

namespace ui_zoom {

ZoomEventManager* ZoomEventManager::GetForBrowserContext(
    content::BrowserContext* context) {
  if (!context->GetUserData(kBrowserZoomEventManager))
    context->SetUserData(kBrowserZoomEventManager, new ZoomEventManager);
  return static_cast<ZoomEventManager*>(
      context->GetUserData(kBrowserZoomEventManager));
}

ZoomEventManager::ZoomEventManager() : weak_ptr_factory_(this) {
}

ZoomEventManager::~ZoomEventManager() {
}

void ZoomEventManager::OnZoomLevelChanged(
    const content::HostZoomMap::ZoomLevelChange& change) {
  zoom_level_changed_callbacks_.Notify(change);
}

std::unique_ptr<content::HostZoomMap::Subscription>
ZoomEventManager::AddZoomLevelChangedCallback(
    const content::HostZoomMap::ZoomLevelChangedCallback& callback) {
  return zoom_level_changed_callbacks_.Add(callback);
}

void ZoomEventManager::OnDefaultZoomLevelChanged() {
  FOR_EACH_OBSERVER(ZoomEventManagerObserver, observers_,
                    OnDefaultZoomLevelChanged());
}

void ZoomEventManager::AddZoomEventManagerObserver(
    ZoomEventManagerObserver* observer) {
  observers_.AddObserver(observer);
}

void ZoomEventManager::RemoveZoomEventManagerObserver(
    ZoomEventManagerObserver* observer) {
  observers_.RemoveObserver(observer);
}

}  // namespace ui_zoom
