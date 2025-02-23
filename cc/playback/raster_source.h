// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_PLAYBACK_RASTER_SOURCE_H_
#define CC_PLAYBACK_RASTER_SOURCE_H_

#include <stddef.h>

#include <memory>
#include <vector>

#include "base/macros.h"
#include "base/threading/thread_checker.h"
#include "base/trace_event/memory_allocator_dump.h"
#include "base/trace_event/memory_dump_provider.h"
#include "cc/base/cc_export.h"
#include "cc/debug/rendering_stats_instrumentation.h"
#include "cc/playback/recording_source.h"
#include "skia/ext/analysis_canvas.h"
#include "third_party/skia/include/core/SkPicture.h"

namespace cc {
class DisplayItemList;
class DrawImage;
class ImageDecodeController;

class CC_EXPORT RasterSource : public base::trace_event::MemoryDumpProvider,
                               public base::RefCountedThreadSafe<RasterSource> {
 public:
  struct CC_EXPORT PlaybackSettings {
    PlaybackSettings();

    // If set to true, this indicates that the canvas has already been
    // rasterized into. This means that the canvas cannot be cleared safely.
    bool playback_to_shared_canvas;

    // If set to true, none of the images will be rasterized.
    bool skip_images;

    // If set to true, we will use an image hijack canvas, which enables
    // compositor image caching.
    bool use_image_hijack_canvas;
  };

  static scoped_refptr<RasterSource> CreateFromRecordingSource(
      const RecordingSource* other,
      bool can_use_lcd_text);

  // Raster a subrect of this RasterSource into the given canvas. It is
  // assumed that contents_scale has already been applied to this canvas.
  // Writes the total number of pixels rasterized and the time spent
  // rasterizing to the stats if the respective pointer is not nullptr.
  // It is assumed that the canvas passed here will only be rasterized by
  // this raster source via this call.
  //
  // Virtual for testing.
  //
  // Note that this should only be called after the image decode controller has
  // been set, which happens during commit.
  virtual void PlaybackToCanvas(SkCanvas* canvas,
                                const gfx::Rect& canvas_bitmap_rect,
                                const gfx::Rect& canvas_playback_rect,
                                float contents_scale,
                                const PlaybackSettings& settings) const;

  // Returns whether the given rect at given scale is of solid color in
  // this raster source, as well as the solid color value.
  bool PerformSolidColorAnalysis(const gfx::Rect& content_rect,
                                 float contents_scale,
                                 SkColor* color) const;

  // Returns true iff the whole raster source is of solid color.
  bool IsSolidColor() const;

  // Returns the color of the raster source if it is solid color. The results
  // are unspecified if IsSolidColor returns false.
  SkColor GetSolidColor() const;

  // Returns the size of this raster source.
  gfx::Size GetSize() const;

  // Populate the given list with all images that may overlap the given
  // rect in layer space. The returned draw images' matrices are modified as if
  // they were being using during raster at scale |raster_scale|.
  void GetDiscardableImagesInRect(const gfx::Rect& layer_rect,
                                  float raster_scale,
                                  std::vector<DrawImage>* images) const;

  // Return true iff this raster source can raster the given rect in layer
  // space.
  bool CoversRect(const gfx::Rect& layer_rect) const;

  // Returns true if this raster source has anything to rasterize.
  virtual bool HasRecordings() const;

  // Valid rectangle in which everything is recorded and can be rastered from.
  virtual gfx::Rect RecordedViewport() const;

  // Informs the raster source that it should attempt to use distance field text
  // during rasterization.
  virtual void SetShouldAttemptToUseDistanceFieldText();

  // Return true iff this raster source would benefit from using distance
  // field text.
  virtual bool ShouldAttemptToUseDistanceFieldText() const;

  // Tracing functionality.
  virtual void DidBeginTracing();
  virtual void AsValueInto(base::trace_event::TracedValue* array) const;
  virtual sk_sp<SkPicture> GetFlattenedPicture();
  virtual size_t GetPictureMemoryUsage() const;

  // Return true if LCD anti-aliasing may be used when rastering text.
  virtual bool CanUseLCDText() const;

  scoped_refptr<RasterSource> CreateCloneWithoutLCDText() const;

  // Image decode controller should be set once. Its lifetime has to exceed that
  // of the raster source, since the raster source will access it during raster.
  void SetImageDecodeController(ImageDecodeController* image_decode_controller);

  // base::trace_event::MemoryDumpProvider implementation
  bool OnMemoryDump(const base::trace_event::MemoryDumpArgs& args,
                    base::trace_event::ProcessMemoryDump* pmd) override;

 protected:
  friend class base::RefCountedThreadSafe<RasterSource>;

  RasterSource(const RecordingSource* other, bool can_use_lcd_text);
  RasterSource(const RasterSource* other, bool can_use_lcd_text);
  ~RasterSource() override;

  // These members are const as this raster source may be in use on another
  // thread and so should not be touched after construction.
  const scoped_refptr<DisplayItemList> display_list_;
  const size_t painter_reported_memory_usage_;
  const SkColor background_color_;
  const bool requires_clear_;
  const bool can_use_lcd_text_;
  const bool is_solid_color_;
  const SkColor solid_color_;
  const gfx::Rect recorded_viewport_;
  const gfx::Size size_;
  const bool clear_canvas_with_debug_color_;
  const int slow_down_raster_scale_factor_for_debug_;
  // TODO(enne/vmiura): this has a read/write race between raster and compositor
  // threads with multi-threaded Ganesh.  Make this const or remove it.
  bool should_attempt_to_use_distance_field_text_;

  // In practice, this is only set once before raster begins, so it's ok with
  // respect to threading.
  ImageDecodeController* image_decode_controller_;

 private:
  void RasterCommon(SkCanvas* canvas,
                    SkPicture::AbortCallback* callback,
                    const gfx::Rect& canvas_bitmap_rect,
                    const gfx::Rect& canvas_playback_rect,
                    float contents_scale) const;

  void PrepareForPlaybackToCanvas(SkCanvas* canvas,
                                  const gfx::Rect& canvas_bitmap_rect,
                                  const gfx::Rect& canvas_playback_rect,
                                  float contents_scale) const;

  // Used to ensure that memory dump logic always happens on the same thread.
  base::ThreadChecker memory_dump_thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(RasterSource);
};

}  // namespace cc

#endif  // CC_PLAYBACK_RASTER_SOURCE_H_
