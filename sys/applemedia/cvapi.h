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
  kCVPixelFormatType_420YpCbCr8Planar             = 'y420',
  kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange = '420v',
  kCVPixelFormatType_422YpCbCr8Deprecated         = 'yuvs',
  kCVPixelFormatType_422YpCbCr8                   = '2vuy'
};

enum _CVPixelBufferLockFlags
{
  kCVPixelBufferLock_ReadOnly = 0x00000001
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
  CVReturn (* CVPixelBufferCreateWithPlanarBytes)
      (CFAllocatorRef allocator, size_t width, size_t height,
      OSType pixelFormatType, void * dataPtr, size_t dataSize,
      size_t numberOfPlanes, void *planeBaseAddress[],
      size_t planeWidth[], size_t planeHeight[],
      size_t planeBytesPerRow[],
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
  void * (* CVPixelBufferGetIOSurface)
      (CVPixelBufferRef pixelBuffer);
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
  CFStringRef * kCVPixelBufferPlaneAlignmentKey;
};

struct _GstCVApiClass
{
  GstDynApiClass parent_class;
};

GType gst_cv_api_get_type (void);

GstCVApi * gst_cv_api_obtain (GError ** error);

G_END_DECLS

#endif
