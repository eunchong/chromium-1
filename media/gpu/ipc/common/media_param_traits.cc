// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Get basic type definitions.
#include "media/gpu/ipc/common/media_param_traits.h"

// Generate param traits size methods.
#include "ipc/param_traits_size_macros.h"
namespace IPC {
#undef MEDIA_GPU_IPC_COMMON_MEDIA_PARAM_TRAITS_MACROS_H_
#include "media/gpu/ipc/common/media_param_traits_macros.h"
}

// Generate param traits write methods.
#include "ipc/param_traits_write_macros.h"
namespace IPC {
#undef MEDIA_GPU_IPC_COMMON_MEDIA_PARAM_TRAITS_MACROS_H_
#include "media/gpu/ipc/common/media_param_traits_macros.h"
}  // namespace IPC

// Generate param traits read methods.
#include "ipc/param_traits_read_macros.h"
namespace IPC {
#undef MEDIA_GPU_IPC_COMMON_MEDIA_PARAM_TRAITS_MACROS_H_
#include "media/gpu/ipc/common/media_param_traits_macros.h"
}  // namespace IPC

// Generate param traits log methods.
#include "ipc/param_traits_log_macros.h"
namespace IPC {
#undef MEDIA_GPU_IPC_COMMON_MEDIA_PARAM_TRAITS_MACROS_H_
#include "media/gpu/ipc/common/media_param_traits_macros.h"
}  // namespace IPC
