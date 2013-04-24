/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@soundrop.com>
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

#include "cvapi.h"

#include "dynapi-internal.h"

#define CV_FRAMEWORK_PATH "/System/Library/Frameworks/CoreVideo.framework/" \
    "CoreVideo"

G_DEFINE_TYPE (GstCVApi, gst_cv_api, GST_TYPE_DYN_API);

static void
gst_cv_api_init (GstCVApi * self)
{
}

static void
gst_cv_api_class_init (GstCVApiClass * klass)
{
}

#define SYM_SPEC(name) GST_DYN_SYM_SPEC (GstCVApi, name)
#define SYM_SPEC_OPTIONAL(name) GST_DYN_SYM_SPEC_OPTIONAL (GstCVApi, name)

GstCVApi *
gst_cv_api_obtain (GError ** error)
{
  static const GstDynSymSpec symbols[] = {
    SYM_SPEC (CVBufferRelease),
    SYM_SPEC (CVBufferRetain),

    SYM_SPEC (CVPixelBufferCreateWithBytes),
    SYM_SPEC (CVPixelBufferCreateWithPlanarBytes),
    SYM_SPEC (CVPixelBufferGetBaseAddress),
    SYM_SPEC (CVPixelBufferGetBaseAddressOfPlane),
    SYM_SPEC (CVPixelBufferGetBytesPerRow),
    SYM_SPEC (CVPixelBufferGetBytesPerRowOfPlane),
    SYM_SPEC (CVPixelBufferGetHeight),
    SYM_SPEC (CVPixelBufferGetHeightOfPlane),
    SYM_SPEC_OPTIONAL (CVPixelBufferGetIOSurface),
    SYM_SPEC (CVPixelBufferGetPlaneCount),
    SYM_SPEC (CVPixelBufferGetTypeID),
    SYM_SPEC (CVPixelBufferIsPlanar),
    SYM_SPEC (CVPixelBufferLockBaseAddress),
    SYM_SPEC (CVPixelBufferRelease),
    SYM_SPEC (CVPixelBufferRetain),
    SYM_SPEC (CVPixelBufferUnlockBaseAddress),

    SYM_SPEC (kCVPixelBufferPixelFormatTypeKey),
    SYM_SPEC (kCVPixelBufferWidthKey),
    SYM_SPEC (kCVPixelBufferHeightKey),
    SYM_SPEC (kCVPixelBufferBytesPerRowAlignmentKey),
    SYM_SPEC (kCVPixelBufferPlaneAlignmentKey),

    {NULL, 0},
  };

  return _gst_dyn_api_new (gst_cv_api_get_type (), CV_FRAMEWORK_PATH, symbols,
      error);
}
