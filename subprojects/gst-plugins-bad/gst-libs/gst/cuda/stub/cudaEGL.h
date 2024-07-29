/* CUDA EGL stub header
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_CUDA_EGLSTUB_H__
#define __GST_CUDA_EGLSTUB_H__

#include "cuda.h"

#ifdef CUDA_FORCE_API_VERSION
#error "CUDA_FORCE_API_VERSION is no longer supported."
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CU_EGL_MAX_PLANES 3

typedef enum CUeglFrameType_enum {
  CU_EGL_FRAME_TYPE_ARRAY = 0,
  CU_EGL_FRAME_TYPE_PITCH = 1,
} CUeglFrameType;

typedef enum CUeglColorFormat_enum {
  CU_EGL_COLOR_FORMAT_RGBA = 0x07,
  CU_EGL_COLOR_FORMAT_MAX = 0x72,
} CUeglColorFormat;

typedef struct CUeglFrame {
    union {
        CUarray pArray[CU_EGL_MAX_PLANES];
        void*   pPitch[CU_EGL_MAX_PLANES];
    } frame;
    guint width;
    guint height;
    guint depth;
    guint pitch;
    guint planeCount;
    guint numChannels;
    CUeglFrameType frameType;
    CUeglColorFormat eglColorFormat;
    CUarray_format cuFormat;
} CUeglFrame;

#ifdef __cplusplus
};
#endif

#endif

