/*
 * Copyright (C) 2006 Apple Computer, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PNGImageDecoder_h
#define PNGImageDecoder_h

#include "platform/image-decoders/ImageDecoder.h"

namespace blink {

class PNGImageReader;

class PLATFORM_EXPORT PNGImageDecoder final : public ImageDecoder {
    WTF_MAKE_NONCOPYABLE(PNGImageDecoder);
public:
    PNGImageDecoder(AlphaOption, GammaAndColorProfileOption, size_t maxDecodedBytes, size_t offset = 0);
    ~PNGImageDecoder() override;

    // ImageDecoder:
    String filenameExtension() const override { return "png"; }

    // Callbacks from libpng
    void headerAvailable();
    void rowAvailable(unsigned char* row, unsigned rowIndex, int);
    void complete();

private:
    // ImageDecoder:
    void decodeSize() override { decode(true); }
    void decode(size_t) override { decode(false); }

    // Decodes the image.  If |onlySize| is true, stops decoding after
    // calculating the image size.  If decoding fails but there is no more
    // data coming, sets the "decode failure" flag.
    void decode(bool onlySize);

    OwnPtr<PNGImageReader> m_reader;
    const unsigned m_offset;
};

} // namespace blink

#endif
