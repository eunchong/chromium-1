// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/test/simple_test_tick_clock.h"
#include "media/base/null_video_sink.h"
#include "media/base/test_helpers.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::Return;

namespace media {

ACTION_P(RunClosure, closure) {
  closure.Run();
}

class NullVideoSinkTest : public testing::Test,
                          public VideoRendererSink::RenderCallback {
 public:
  NullVideoSinkTest() {
    // Never use null TimeTicks since they have special connotations.
    tick_clock_.Advance(base::TimeDelta::FromMicroseconds(12345));
  }
  ~NullVideoSinkTest() override {}

  std::unique_ptr<NullVideoSink> ConstructSink(bool clockless,
                                               base::TimeDelta interval) {
    std::unique_ptr<NullVideoSink> new_sink(new NullVideoSink(
        clockless, interval,
        base::Bind(&NullVideoSinkTest::FrameReceived, base::Unretained(this)),
        message_loop_.task_runner()));
    new_sink->set_tick_clock_for_testing(&tick_clock_);
    return new_sink;
  }

  scoped_refptr<VideoFrame> CreateFrame(base::TimeDelta timestamp) {
    const gfx::Size natural_size(8, 8);
    return VideoFrame::CreateFrame(PIXEL_FORMAT_YV12, natural_size,
                                   gfx::Rect(natural_size), natural_size,
                                   timestamp);
  }

  // VideoRendererSink::RenderCallback implementation.
  MOCK_METHOD3(Render,
               scoped_refptr<VideoFrame>(base::TimeTicks,
                                         base::TimeTicks,
                                         bool));
  MOCK_METHOD0(OnFrameDropped, void());

  MOCK_METHOD1(FrameReceived, void(const scoped_refptr<VideoFrame>&));

 protected:
  base::MessageLoop message_loop_;
  base::SimpleTestTickClock tick_clock_;

  DISALLOW_COPY_AND_ASSIGN(NullVideoSinkTest);
};

TEST_F(NullVideoSinkTest, BasicFunctionality) {
  const base::TimeDelta kInterval = base::TimeDelta::FromMilliseconds(25);

  std::unique_ptr<NullVideoSink> sink = ConstructSink(false, kInterval);
  scoped_refptr<VideoFrame> test_frame = CreateFrame(base::TimeDelta());

  // The sink shouldn't have to be started to use the paint method.
  EXPECT_CALL(*this, FrameReceived(test_frame));
  sink->PaintFrameUsingOldRenderingPath(test_frame);

  {
    SCOPED_TRACE("Waiting for sink startup.");
    sink->Start(this);
    const base::TimeTicks current_time = tick_clock_.NowTicks();
    const base::TimeTicks current_interval_end = current_time + kInterval;
    EXPECT_CALL(*this, Render(current_time, current_interval_end, false))
        .WillOnce(Return(test_frame));
    WaitableMessageLoopEvent event;
    EXPECT_CALL(*this, FrameReceived(test_frame))
        .WillOnce(RunClosure(event.GetClosure()));
    event.RunAndWait();
  }

  // Verify that toggling background rendering mode issues the right bit to
  // each Render() call.
  sink->set_background_render(true);

  // A second call returning the same frame should not result in a new call to
  // FrameReceived().
  {
    SCOPED_TRACE("Waiting for second render call.");
    WaitableMessageLoopEvent event;
    scoped_refptr<VideoFrame> test_frame_2 = CreateFrame(kInterval);
    EXPECT_CALL(*this, Render(_, _, true))
        .WillOnce(Return(test_frame))
        .WillOnce(Return(test_frame_2));
    EXPECT_CALL(*this, FrameReceived(test_frame)).Times(0);
    EXPECT_CALL(*this, FrameReceived(test_frame_2))
        .WillOnce(RunClosure(event.GetClosure()));
    event.RunAndWait();
  }

  {
    SCOPED_TRACE("Waiting for stop event.");
    WaitableMessageLoopEvent event;
    sink->set_stop_cb(event.GetClosure());
    sink->Stop();
    event.RunAndWait();
  }
}

TEST_F(NullVideoSinkTest, ClocklessFunctionality) {
  // Construct the sink with a huge interval, it should still complete quickly.
  const base::TimeDelta interval = base::TimeDelta::FromSeconds(10);
  std::unique_ptr<NullVideoSink> sink = ConstructSink(true, interval);

  scoped_refptr<VideoFrame> test_frame = CreateFrame(base::TimeDelta());
  scoped_refptr<VideoFrame> test_frame_2 = CreateFrame(interval);
  sink->Start(this);

  EXPECT_CALL(*this, FrameReceived(test_frame)).Times(1);
  EXPECT_CALL(*this, FrameReceived(test_frame_2)).Times(1);

  const int kTestRuns = 6;
  const base::TimeTicks now = base::TimeTicks::Now();
  const base::TimeTicks current_time = tick_clock_.NowTicks();

  SCOPED_TRACE("Waiting for multiple render callbacks");
  WaitableMessageLoopEvent event;
  for (int i = 0; i < kTestRuns; ++i) {
    if (i < kTestRuns - 1) {
      EXPECT_CALL(*this, Render(current_time + i * interval,
                                current_time + (i + 1) * interval, false))
          .WillOnce(Return(test_frame));
    } else {
      EXPECT_CALL(*this, Render(current_time + i * interval,
                                current_time + (i + 1) * interval, false))
          .WillOnce(
              DoAll(RunClosure(event.GetClosure()), Return(test_frame_2)));
    }
  }
  event.RunAndWait();
  ASSERT_LT(base::TimeTicks::Now() - now, kTestRuns * interval);
  sink->Stop();
}

}  // namespace media
