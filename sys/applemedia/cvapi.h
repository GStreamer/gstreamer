/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oravnas@cisco.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_CV_API_H__
#define __GST_CV_API_H__

#include "dynapi.h"

#include <CoreFoundation/CoreFoundation.h>

G_BEGIN_DECLS

typedef struct _GstCVApi GstCVApi;
typedef struct _GstCVApiClass GstCVApiClass;

typedef int32_t CVReturn;

typedef uint64_t CVOptionFlags;

typedef struct _CVBuffer * CVBufferRef;
typedef CVBufferRef CVImageBufferRef;
typedef CVImageBufferRef CVPixelBufferRef;

typedef void (* CVPixelBufferReleaseBytesCallback) (void * releaseRefCon,
    const void * baseAddress);

enum _CVReturn
{
  kCVReturnSuccess = 0
};

enum _CVPixelFormatType
{
  kCVPixelFormatType_422YpCbCr8Deprecated   = 'yuvs',
  kCVPixelFormatType_422YpCbCr8             = '2vuy'
};

struct _GstCVApi
{
  GstDynApi parent;

  void (* CVBufferRelease) (CVBufferRef buffer);
  CVBufferRef (* CVBufferRetain) (CVBufferRef buffer);

  CVReturn (* CVPixelBufferCreateWithBytes)
      (CFAllocatorRef allocator, size_t width, size_t height,
      OSType pixelFormatType, void * baseAddress, size_t bytesPerRow,
      CVPixelBufferReleaseBytesCallback releaseCallback,
      void * releaseRefCon, CFDictionaryRef pixelBufferAttributes,
      CVPixelBufferRef * pixelBufferOut);
  void * (* CVPixelBufferGetBaseAddress)
      (CVPixelBufferRef pixelBuffer);
  void * (* CVPixelBufferGetBaseAddressOfPlane)
      (CVPixelBufferRef pixelBuffer, size_t planeIndex);
  size_t (* CVPixelBufferGetBytesPerRow)
      (CVPixelBufferRef pixelBuffer);
  size_t (* CVPixelBufferGetBytesPerRowOfPlane)
      (CVPixelBufferRef pixelBuffer, size_t planeIndex);
  size_t (* CVPixelBufferGetHeight) (CVPixelBufferRef pixelBuffer);
  size_t (* CVPixelBufferGetHeightOfPlane)
      (CVPixelBufferRef pixelBuffer, size_t planeIndex);
  size_t (* CVPixelBufferGetPlaneCount)
      (CVPixelBufferRef pixelBuffer);
  CFTypeID (* CVPixelBufferGetTypeID) (void);
  Boolean (* CVPixelBufferIsPlanar) (CVPixelBufferRef pixelBuffer);
  CVReturn (* CVPixelBufferLockBaseAddress)
      (CVPixelBufferRef pixelBuffer, CVOptionFlags lockFlags);
  void (* CVPixelBufferRelease) (CVPixelBufferRef pixelBuffer);
  CVPixelBufferRef (* CVPixelBufferRetain)
      (CVPixelBufferRef pixelBuffer);
  CVReturn (* CVPixelBufferUnlockBaseAddress)
      (CVPixelBufferRef pixelBuffer, CVOptionFlags unlockFlags);

  CFStringRef * kCVPixelBufferPixelFormatTypeKey;
  CFStringRef * kCVPixelBufferWidthKey;
  CFStringRef * kCVPixelBufferHeightKey;
  CFStringRef * kCVPixelBufferBytesPerRowAlignmentKey;
};

struct _GstCVApiClass
{
  GstDynApiClass parent_class;
};

GType gst_cv_api_get_type (void);

GstCVApi * gst_cv_api_obtain (GError ** error);

G_END_DECLS

#endif
