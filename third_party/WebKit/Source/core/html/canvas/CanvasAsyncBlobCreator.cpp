// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/html/canvas/CanvasAsyncBlobCreator.h"

#include "core/fileapi/Blob.h"
#include "platform/ThreadSafeFunctional.h"
#include "platform/graphics/ImageBuffer.h"
#include "platform/image-encoders/JPEGImageEncoder.h"
#include "platform/image-encoders/PNGImageEncoder.h"
#include "platform/threading/BackgroundTaskRunner.h"
#include "public/platform/Platform.h"
#include "public/platform/WebScheduler.h"
#include "public/platform/WebTaskRunner.h"
#include "public/platform/WebThread.h"
#include "public/platform/WebTraceLocation.h"
#include "wtf/CurrentTime.h"
#include "wtf/Functional.h"

namespace blink {

namespace {

const double SlackBeforeDeadline = 0.001; // a small slack period between deadline and current time for safety
const int NumChannelsPng = 4;
const int LongTaskImageSizeThreshold = 1000 * 1000; // The max image size we expect to encode in 14ms on Linux in PNG format

// The encoding task is highly likely to switch from idle task to alternative
// code path when the startTimeoutDelay is set to be below 150ms. As we want the
// majority of encoding tasks to take the usual async idle task, we set a
// lenient limit -- 200ms here. This limit still needs to be short enough for
// the latency to be negligible to the user.
const double IdleTaskStartTimeoutDelay = 200.0;
// We should be more lenient on completion timeout delay to ensure that the
// switch from idle to main thread only happens to a minority of toBlob calls
#if !OS(ANDROID)
// Png image encoding on 4k by 4k canvas on Mac HDD takes 5.7+ seconds
const double IdleTaskCompleteTimeoutDelay = 6700.0;
#else
// Png image encoding on 4k by 4k canvas on Android One takes 9.0+ seconds
const double IdleTaskCompleteTimeoutDelay = 10000.0;
#endif

bool isDeadlineNearOrPassed(double deadlineSeconds)
{
    return (deadlineSeconds - SlackBeforeDeadline - monotonicallyIncreasingTime() <= 0);
}

String convertMimeTypeEnumToString(CanvasAsyncBlobCreator::MimeType mimeTypeEnum)
{
    switch (mimeTypeEnum) {
    case CanvasAsyncBlobCreator::MimeTypePng:
        return "image/png";
    case CanvasAsyncBlobCreator::MimeTypeJpeg:
        return "image/jpeg";
    case CanvasAsyncBlobCreator::MimeTypeWebp:
        return "image/webp";
    default:
        return "image/unknown";
    }
}

CanvasAsyncBlobCreator::MimeType convertMimeTypeStringToEnum(const String& mimeType)
{
    CanvasAsyncBlobCreator::MimeType mimeTypeEnum;
    if (mimeType == "image/png") {
        mimeTypeEnum = CanvasAsyncBlobCreator::MimeTypePng;
    } else if (mimeType == "image/jpeg") {
        mimeTypeEnum = CanvasAsyncBlobCreator::MimeTypeJpeg;
    } else if (mimeType == "image/webp") {
        mimeTypeEnum = CanvasAsyncBlobCreator::MimeTypeWebp;
    } else {
        mimeTypeEnum = CanvasAsyncBlobCreator::NumberOfMimeTypeSupported;
    }
    return mimeTypeEnum;
}

} // anonymous namespace

CanvasAsyncBlobCreator* CanvasAsyncBlobCreator::create(DOMUint8ClampedArray* unpremultipliedRGBAImageData, const String& mimeType, const IntSize& size, BlobCallback* callback)
{
    return new CanvasAsyncBlobCreator(unpremultipliedRGBAImageData, convertMimeTypeStringToEnum(mimeType), size, callback);
}

CanvasAsyncBlobCreator::CanvasAsyncBlobCreator(DOMUint8ClampedArray* data, MimeType mimeType, const IntSize& size, BlobCallback* callback)
    : m_data(data)
    , m_size(size)
    , m_mimeType(mimeType)
    , m_callback(callback)
{
    ASSERT(m_data->length() == (unsigned) (size.height() * size.width() * 4));
    m_encodedImage = adoptPtr(new Vector<unsigned char>());
    m_pixelRowStride = size.width() * NumChannelsPng;
    m_idleTaskStatus = IdleTaskNotSupported;
    m_numRowsCompleted = 0;
}

CanvasAsyncBlobCreator::~CanvasAsyncBlobCreator()
{
}

void CanvasAsyncBlobCreator::scheduleAsyncBlobCreation(bool canUseIdlePeriodScheduling, const double& quality)
{
    ASSERT(isMainThread());

    if (canUseIdlePeriodScheduling) {
        m_idleTaskStatus = IdleTaskNotStarted;
        if (m_mimeType == MimeTypePng) {
            this->scheduleInitiatePngEncoding();
        } else if (m_mimeType == MimeTypeJpeg) {
            this->scheduleInitiateJpegEncoding(quality);
        } else {
            // Progressive encoding is only applicable to png and jpeg image format,
            // and thus idle tasks scheduling can only be applied to these image formats.
            // TODO(xlai): Progressive encoding on webp image formats (crbug.com/571399)
            ASSERT_NOT_REACHED();
        }
        // We post the below task to check if the above idle task isn't late.
        // There's no risk of concurrency as both tasks are on main thread.
        this->postDelayedTaskToMainThread(BLINK_FROM_HERE, new SameThreadTask(bind(&CanvasAsyncBlobCreator::idleTaskStartTimeoutEvent, this, quality)), IdleTaskStartTimeoutDelay);
    } else if (m_mimeType == MimeTypeWebp) {
        BackgroundTaskRunner::TaskSize taskSize = (m_size.height() * m_size.width() >= LongTaskImageSizeThreshold) ? BackgroundTaskRunner::TaskSizeLongRunningTask : BackgroundTaskRunner::TaskSizeShortRunningTask;
        BackgroundTaskRunner::postOnBackgroundThread(BLINK_FROM_HERE, threadSafeBind(&CanvasAsyncBlobCreator::encodeImageOnEncoderThread, AllowCrossThreadAccess(this), quality), taskSize);
    }
}

void CanvasAsyncBlobCreator::scheduleInitiateJpegEncoding(const double& quality)
{
    Platform::current()->mainThread()->scheduler()->postIdleTask(BLINK_FROM_HERE, bind<double>(&CanvasAsyncBlobCreator::initiateJpegEncoding, this, quality));
}

void CanvasAsyncBlobCreator::initiateJpegEncoding(const double& quality, double deadlineSeconds)
{
    ASSERT(isMainThread());
    if (m_idleTaskStatus == IdleTaskSwitchedToMainThreadTask) {
        return;
    }

    ASSERT(m_idleTaskStatus == IdleTaskNotStarted);
    m_idleTaskStatus = IdleTaskStarted;

    if (!initializeJpegStruct(quality)) {
        m_idleTaskStatus = IdleTaskFailed;
        return;
    }
    this->idleEncodeRowsJpeg(deadlineSeconds);
}

void CanvasAsyncBlobCreator::scheduleInitiatePngEncoding()
{
    Platform::current()->mainThread()->scheduler()->postIdleTask(BLINK_FROM_HERE, bind<double>(&CanvasAsyncBlobCreator::initiatePngEncoding, this));
}

void CanvasAsyncBlobCreator::initiatePngEncoding(double deadlineSeconds)
{
    ASSERT(isMainThread());
    if (m_idleTaskStatus == IdleTaskSwitchedToMainThreadTask) {
        return;
    }

    ASSERT(m_idleTaskStatus == IdleTaskNotStarted);
    m_idleTaskStatus = IdleTaskStarted;

    if (!initializePngStruct()) {
        m_idleTaskStatus = IdleTaskFailed;
        return;
    }
    this->idleEncodeRowsPng(deadlineSeconds);
}

void CanvasAsyncBlobCreator::idleEncodeRowsPng(double deadlineSeconds)
{
    ASSERT(isMainThread());
    if (m_idleTaskStatus == IdleTaskSwitchedToMainThreadTask) {
        return;
    }

    unsigned char* inputPixels = m_data->data() + m_pixelRowStride * m_numRowsCompleted;
    for (int y = m_numRowsCompleted; y < m_size.height(); ++y) {
        if (isDeadlineNearOrPassed(deadlineSeconds)) {
            m_numRowsCompleted = y;
            Platform::current()->currentThread()->scheduler()->postIdleTask(BLINK_FROM_HERE, bind<double>(&CanvasAsyncBlobCreator::idleEncodeRowsPng, this));
            return;
        }
        PNGImageEncoder::writeOneRowToPng(inputPixels, m_pngEncoderState.get());
        inputPixels += m_pixelRowStride;
    }
    m_numRowsCompleted = m_size.height();
    PNGImageEncoder::finalizePng(m_pngEncoderState.get());

    m_idleTaskStatus = IdleTaskCompleted;

    if (isDeadlineNearOrPassed(deadlineSeconds)) {
        Platform::current()->mainThread()->getWebTaskRunner()->postTask(BLINK_FROM_HERE, bind(&CanvasAsyncBlobCreator::createBlobAndInvokeCallback, this));
    } else {
        this->createBlobAndInvokeCallback();
    }
}

void CanvasAsyncBlobCreator::idleEncodeRowsJpeg(double deadlineSeconds)
{
    ASSERT(isMainThread());
    if (m_idleTaskStatus == IdleTaskSwitchedToMainThreadTask) {
        return;
    }

    m_numRowsCompleted = JPEGImageEncoder::progressiveEncodeRowsJpegHelper(m_jpegEncoderState.get(), m_data->data(), m_numRowsCompleted, SlackBeforeDeadline, deadlineSeconds);
    if (m_numRowsCompleted == m_size.height()) {
        m_idleTaskStatus = IdleTaskCompleted;
        if (isDeadlineNearOrPassed(deadlineSeconds)) {
            Platform::current()->mainThread()->getWebTaskRunner()->postTask(BLINK_FROM_HERE, bind(&CanvasAsyncBlobCreator::createBlobAndInvokeCallback, this));
        } else {
            this->createBlobAndInvokeCallback();
        }
    } else if (m_numRowsCompleted == JPEGImageEncoder::ProgressiveEncodeFailed) {
        m_idleTaskStatus = IdleTaskFailed;
        this->createNullAndInvokeCallback();
    } else {
        Platform::current()->currentThread()->scheduler()->postIdleTask(BLINK_FROM_HERE, bind<double>(&CanvasAsyncBlobCreator::idleEncodeRowsJpeg, this));
    }
}

void CanvasAsyncBlobCreator::encodeRowsPngOnMainThread()
{
    ASSERT(m_idleTaskStatus == IdleTaskSwitchedToMainThreadTask);

    // Continue encoding from the last completed row
    unsigned char* inputPixels = m_data->data() + m_pixelRowStride * m_numRowsCompleted;
    for (int y = m_numRowsCompleted; y < m_size.height(); ++y) {
        PNGImageEncoder::writeOneRowToPng(inputPixels, m_pngEncoderState.get());
        inputPixels += m_pixelRowStride;
    }
    PNGImageEncoder::finalizePng(m_pngEncoderState.get());
    this->createBlobAndInvokeCallback();

    this->signalAlternativeCodePathFinishedForTesting();
}

void CanvasAsyncBlobCreator::encodeRowsJpegOnMainThread()
{
    ASSERT(m_idleTaskStatus == IdleTaskSwitchedToMainThreadTask);

    // Continue encoding from the last completed row
    if (JPEGImageEncoder::encodeWithPreInitializedState(std::move(m_jpegEncoderState), m_data->data(), m_numRowsCompleted)) {
        this->createBlobAndInvokeCallback();
    } else {
        this->createNullAndInvokeCallback();
    }

    this->signalAlternativeCodePathFinishedForTesting();
}

void CanvasAsyncBlobCreator::createBlobAndInvokeCallback()
{
    ASSERT(isMainThread());
    Blob* resultBlob = Blob::create(m_encodedImage->data(), m_encodedImage->size(), convertMimeTypeEnumToString(m_mimeType));
    Platform::current()->mainThread()->getWebTaskRunner()->postTask(BLINK_FROM_HERE, bind(&BlobCallback::handleEvent, m_callback, resultBlob));
    // Since toBlob is done, timeout events are no longer needed. So we clear
    // non-GC members to allow teardown of CanvasAsyncBlobCreator.
    m_data.clear();
    m_callback.clear();
}

void CanvasAsyncBlobCreator::createNullAndInvokeCallback()
{
    ASSERT(isMainThread());
    Platform::current()->mainThread()->getWebTaskRunner()->postTask(BLINK_FROM_HERE, bind(&BlobCallback::handleEvent, m_callback, nullptr));
    // Since toBlob is done (failed), timeout events are no longer needed. So we
    // clear non-GC members to allow teardown of CanvasAsyncBlobCreator.
    m_data.clear();
    m_callback.clear();
}

void CanvasAsyncBlobCreator::encodeImageOnEncoderThread(double quality)
{
    ASSERT(!isMainThread());
    ASSERT(m_mimeType == MimeTypeWebp);

    if (!ImageDataBuffer(m_size, m_data->data()).encodeImage("image/webp", quality, m_encodedImage.get())) {
        Platform::current()->mainThread()->getWebTaskRunner()->postTask(BLINK_FROM_HERE, threadSafeBind(&BlobCallback::handleEvent, wrapCrossThreadPersistent(m_callback.get()), nullptr));
        return;
    }

    Platform::current()->mainThread()->getWebTaskRunner()->postTask(BLINK_FROM_HERE, threadSafeBind(&CanvasAsyncBlobCreator::createBlobAndInvokeCallback, AllowCrossThreadAccess(this)));
}

bool CanvasAsyncBlobCreator::initializePngStruct()
{
    m_pngEncoderState = PNGImageEncoderState::create(m_size, m_encodedImage.get());
    if (!m_pngEncoderState) {
        this->createNullAndInvokeCallback();
        return false;
    }
    return true;
}

bool CanvasAsyncBlobCreator::initializeJpegStruct(double quality)
{
    m_jpegEncoderState = JPEGImageEncoderState::create(m_size, quality, m_encodedImage.get());
    if (!m_jpegEncoderState) {
        this->createNullAndInvokeCallback();
        return false;
    }
    return true;
}

void CanvasAsyncBlobCreator::idleTaskStartTimeoutEvent(double quality)
{
    if (m_idleTaskStatus == IdleTaskStarted) {
        // Even if the task started quickly, we still want to ensure completion
        this->postDelayedTaskToMainThread(BLINK_FROM_HERE, new SameThreadTask(bind(&CanvasAsyncBlobCreator::idleTaskCompleteTimeoutEvent, this)), IdleTaskCompleteTimeoutDelay);
    } else if (m_idleTaskStatus == IdleTaskNotStarted) {
        // If the idle task does not start after a delay threshold, we will
        // force it to happen on main thread (even though it may cause more
        // janks) to prevent toBlob being postponed forever in extreme cases.
        m_idleTaskStatus = IdleTaskSwitchedToMainThreadTask;
        signalTaskSwitchInStartTimeoutEventForTesting();

        if (m_mimeType == MimeTypePng) {
            if (initializePngStruct()) {
                Platform::current()->mainThread()->getWebTaskRunner()->postTask(BLINK_FROM_HERE, bind(&CanvasAsyncBlobCreator::encodeRowsPngOnMainThread, this));
            } else {
                // Failing in initialization of png struct
                this->signalAlternativeCodePathFinishedForTesting();
            }
        } else {
            ASSERT(m_mimeType == MimeTypeJpeg);
            if (initializeJpegStruct(quality)) {
                Platform::current()->mainThread()->getWebTaskRunner()->postTask(BLINK_FROM_HERE, bind(&CanvasAsyncBlobCreator::encodeRowsJpegOnMainThread, this));
            } else {
                // Failing in initialization of jpeg struct
                this->signalAlternativeCodePathFinishedForTesting();
            }
        }
    } else {
        ASSERT(m_idleTaskStatus == IdleTaskFailed || m_idleTaskStatus == IdleTaskCompleted);
        this->signalAlternativeCodePathFinishedForTesting();
    }

}

void CanvasAsyncBlobCreator::idleTaskCompleteTimeoutEvent()
{
    ASSERT(m_idleTaskStatus != IdleTaskNotStarted);

    if (m_idleTaskStatus == IdleTaskStarted) {
        // It has taken too long to complete for the idle task.
        m_idleTaskStatus = IdleTaskSwitchedToMainThreadTask;
        signalTaskSwitchInCompleteTimeoutEventForTesting();

        if (m_mimeType == MimeTypePng) {
            Platform::current()->mainThread()->getWebTaskRunner()->postTask(BLINK_FROM_HERE, bind(&CanvasAsyncBlobCreator::encodeRowsPngOnMainThread, this));
        } else {
            ASSERT(m_mimeType == MimeTypeJpeg);
            Platform::current()->mainThread()->getWebTaskRunner()->postTask(BLINK_FROM_HERE, bind(&CanvasAsyncBlobCreator::encodeRowsJpegOnMainThread, this));
        }
    } else {
        ASSERT(m_idleTaskStatus == IdleTaskFailed || m_idleTaskStatus == IdleTaskCompleted);
        this->signalAlternativeCodePathFinishedForTesting();
    }
}

void CanvasAsyncBlobCreator::postDelayedTaskToMainThread(const WebTraceLocation& location, SameThreadTask* task, double delayMs)
{
    Platform::current()->mainThread()->getWebTaskRunner()->postDelayedTask(location, task, delayMs);
}

} // namespace blink
