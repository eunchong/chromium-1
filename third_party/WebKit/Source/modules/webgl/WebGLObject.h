/*
 * Copyright (C) 2009 Apple Inc. All rights reserved.
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

#ifndef WebGLObject_h
#define WebGLObject_h

#include "bindings/core/v8/ScriptWrappable.h"
#include "platform/heap/Handle.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "wtf/Assertions.h"

namespace gpu {
namespace gles2 {
class GLES2Interface;
}
}

namespace blink {

class WebGLContextGroup;
class WebGLRenderingContextBase;

template <typename T>
GLuint objectOrZero(const T* object)
{
    return object ? object->object() : 0;
}

template <typename T>
GLuint objectNonZero(const T* object)
{
    GLuint result = object->object();
    DCHECK(result);
    return result;
}

// TODO(kbr): v8::Persistent doesn't auto-reset in its destructor,
// which wreaks havoc when they're embedded in Oilpan objects even if
// they use GarbageCollectedFinalized. The first V8 GC after the
// Oilpan object is reclaimed will reset the cell in the
// v8::Persistent, scribbling over the Oilpan heap. If v8::Persistents
// are ever embedded in Oilpan objects, they must use
// CopyablePersistentTraits to pick up the kResetInDestructor = true
// setting. This alias template ensures correct usage, but ideally,
// these persistent caches would not be necessary. crbug.com/611864
template <typename T>
using V8CopyablePersistent = v8::Persistent<T, v8::CopyablePersistentTraits<T>>;

class WebGLObject : public GarbageCollectedFinalized<WebGLObject>, public ScriptWrappable {
public:
    virtual ~WebGLObject();

    // deleteObject may not always delete the OpenGL resource.  For programs and
    // shaders, deletion is delayed until they are no longer attached.
    // FIXME: revisit this when resource sharing between contexts are implemented.
    void deleteObject(gpu::gles2::GLES2Interface*);

    void onAttached() { ++m_attachmentCount; }
    void onDetached(gpu::gles2::GLES2Interface*);

    // This indicates whether the client side issue a delete call already, not
    // whether the OpenGL resource is deleted.
    // object()==0 indicates the OpenGL resource is deleted.
    bool isDeleted() { return m_deleted; }

    // True if this object belongs to the group or context.
    virtual bool validate(const WebGLContextGroup*, const WebGLRenderingContextBase*) const = 0;
    virtual bool hasObject() const = 0;

    DEFINE_INLINE_VIRTUAL_TRACE() { }

protected:
    explicit WebGLObject(WebGLRenderingContextBase*);

    // deleteObjectImpl should be only called once to delete the OpenGL resource.
    // After calling deleteObjectImpl, hasObject() should return false.
    virtual void deleteObjectImpl(gpu::gles2::GLES2Interface*) = 0;

    virtual bool hasGroupOrContext() const = 0;

    void detach();
    void detachAndDeleteObject();

    virtual gpu::gles2::GLES2Interface* getAGLInterface() const = 0;

private:
    unsigned m_attachmentCount;
    bool m_deleted;
};

} // namespace blink

#endif // WebGLObject_h
