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

#ifndef __GST_CM_API_H__
#define __GST_CM_API_H__

#include "dynapi.h"

#include <CoreFoundation/CoreFoundation.h>
#include "cvapi.h"

G_BEGIN_DECLS

typedef struct _GstCMApi GstCMApi;
typedef struct _GstCMApiClass GstCMApiClass;

typedef enum _FigStatus FigStatus;

typedef CFTypeRef FigBaseObjectRef;
typedef struct _FigBaseVTable FigBaseVTable;
typedef struct _FigBaseIface FigBaseIface;

typedef struct _FigFormatDescription FigFormatDescription;
typedef struct _FigVideoDimensions FigVideoDimensions;
typedef struct _FigTime FigTime;

typedef CFTypeRef FigBufferQueueRef;

typedef struct _FigSampleBuffer FigSampleBuffer;
typedef struct _FigDataBuffer FigDataBuffer;
typedef struct _FigBlockBuffer FigBlockBuffer;

typedef Boolean (* FigBufferQueueValidateFunc) (FigBufferQueueRef queue,
    FigSampleBuffer *buf, void *refCon);

enum _FigStatus
{
  kFigSuccess = 0,

  kFigResourceBusy = -12780
};

enum _FigMediaType
{
  kFigMediaTypeVideo = 'vide'
};

enum _FigCodecType
{
  kComponentVideoUnsigned           = 'yuvs',
  kFigVideoCodecType_JPEG_OpenDML   = 'dmb1',
  kYUV420vCodecType                 = '420v'
};

struct _FigBaseVTable
{
  gsize unk;
  FigBaseIface * base;
  void * derived;
};

struct _FigBaseIface
{
  gsize unk1;
  gsize unk2;
  gsize unk3;
  FigStatus (* Invalidate) (FigBaseObjectRef obj);
  FigStatus (* Finalize) (FigBaseObjectRef obj);
  gpointer unk4;
  FigStatus (* CopyProperty) (FigBaseObjectRef obj, CFTypeRef key, void *unk,
      CFTypeRef * value);
  FigStatus (* SetProperty) (FigBaseObjectRef obj, CFTypeRef key,
      CFTypeRef value);
};

struct _FigVideoDimensions
{
  UInt32 width;
  UInt32 height;
};

struct _FigTime
{
  UInt8 data[24];
};

struct _GstCMApi
{
  GstDynApi parent;

  FigBaseVTable * (* FigBaseObjectGetVTable) (FigBaseObjectRef obj);

  void * (* FigGetAttachment) (void * obj, CFStringRef attachmentKey,
      UInt32 * foundWherePtr);

  void (* FigFormatDescriptionRelease) (FigFormatDescription * desc);
  FigFormatDescription * (* FigFormatDescriptionRetain) (
      FigFormatDescription * desc);
  Boolean (* FigFormatDescriptionEqual) (FigFormatDescription * desc1,
      FigFormatDescription * desc2);
  CFTypeRef (* FigFormatDescriptionGetExtension) (
      const FigFormatDescription * desc, CFStringRef extensionKey);
  UInt32 (* FigFormatDescriptionGetMediaType) (
      const FigFormatDescription * desc);
  UInt32 (* FigFormatDescriptionGetMediaSubType) (
      const FigFormatDescription * desc);

  FigStatus (* FigVideoFormatDescriptionCreate) (
      CFAllocatorRef allocator, UInt32 formatId, UInt32 width, UInt32 height,
      CFDictionaryRef extensions, FigFormatDescription ** desc);
  FigStatus (* FigVideoFormatDescriptionCreateWithSampleDescriptionExtensionAtom)
      (CFAllocatorRef allocator, UInt32 formatId, UInt32 width, UInt32 height,
      UInt32 atomId, const UInt8 * data, CFIndex len,
      FigFormatDescription ** formatDesc);
  FigVideoDimensions (* FigVideoFormatDescriptionGetDimensions) (
      const FigFormatDescription * desc);

  FigTime (* FigTimeMake) (UInt64 numerator, UInt32 denominator);

  FigStatus (* FigSampleBufferCreate) (CFAllocatorRef allocator,
      FigBlockBuffer * blockBuf, Boolean unkBool, UInt32 unkDW1, UInt32 unkDW2,
      FigFormatDescription * fmtDesc, UInt32 unkCountA, UInt32 unkCountB,
      const void * unkTimeData, UInt32 unkCountC, const void * unkDWordData,
      FigSampleBuffer ** sampleBuffer);
  Boolean (* FigSampleBufferDataIsReady) (
      const FigSampleBuffer * buf);
  FigBlockBuffer * (* FigSampleBufferGetDataBuffer) (
      const FigSampleBuffer * buf);
  FigFormatDescription * (* FigSampleBufferGetFormatDescription) (
      const FigSampleBuffer * buf);
  CVImageBufferRef (* FigSampleBufferGetImageBuffer) (
      const FigSampleBuffer * buf);
  SInt32 (* FigSampleBufferGetNumSamples) (
      const FigSampleBuffer * buf);
  CFArrayRef (* FigSampleBufferGetSampleAttachmentsArray) (
      const FigSampleBuffer * buf, SInt32 sampleIndex);
  SInt32 (* FigSampleBufferGetSampleSize) (
      const FigSampleBuffer * buf, SInt32 sampleIndex);
  void (* FigSampleBufferRelease) (FigSampleBuffer * buf);
  FigSampleBuffer * (* FigSampleBufferRetain) (FigSampleBuffer * buf);

  FigStatus (* FigBlockBufferCreateWithMemoryBlock)
      (CFAllocatorRef allocator, Byte * data, UInt32 size,
      CFAllocatorRef dataAllocator, void *unk1, UInt32 sizeA, UInt32 sizeB,
      Boolean unkBool, FigBlockBuffer ** blockBuffer);
  SInt32 (* FigBlockBufferGetDataLength) (const FigBlockBuffer * buf);
  FigStatus (* FigBlockBufferGetDataPointer) (
      const FigBlockBuffer * buf, UInt32 unk1, UInt32 unk2, UInt32 unk3,
      Byte ** dataPtr);
  void (* FigBlockBufferRelease) (FigBlockBuffer * buf);
  FigBlockBuffer * (* FigBlockBufferRetain) (FigBlockBuffer * buf);

  FigSampleBuffer * (* FigBufferQueueDequeueAndRetain)
      (FigBufferQueueRef queue);
  CFIndex (* FigBufferQueueGetBufferCount) (FigBufferQueueRef queue);
  Boolean (* FigBufferQueueIsEmpty) (FigBufferQueueRef queue);
  void (* FigBufferQueueRelease) (FigBufferQueueRef queue);
  FigStatus (* FigBufferQueueSetValidationCallback)
      (FigBufferQueueRef queue, FigBufferQueueValidateFunc func, void *refCon);

  CFStringRef * kFigFormatDescriptionExtension_SampleDescriptionExtensionAtoms;
  CFStringRef * kFigSampleAttachmentKey_DependsOnOthers;
  FigTime * kFigTimeInvalid;
};

struct _GstCMApiClass
{
  GstDynApiClass parent_class;
};

GType gst_cm_api_get_type (void);

GstCMApi * gst_cm_api_obtain (GError ** error);

G_END_DECLS

#endif
