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

#ifndef __GST_CM_API_H__
#define __GST_CM_API_H__

#include "dynapi.h"

#include <CoreFoundation/CoreFoundation.h>
#include "cvapi.h"

G_BEGIN_DECLS

typedef struct _GstCMApi GstCMApi;
typedef struct _GstCMApiClass GstCMApiClass;

typedef CFTypeRef FigBaseObjectRef;
typedef struct _FigBaseVTable FigBaseVTable;
typedef struct _FigBaseIface FigBaseIface;

typedef CFTypeRef CMFormatDescriptionRef;
typedef struct _CMVideoDimensions CMVideoDimensions;
typedef struct _CMTime CMTime;

typedef CFTypeRef CMBufferQueueRef;
typedef SInt32 CMBufferQueueTriggerCondition;
typedef struct _CMBufferQueueTriggerToken *CMBufferQueueTriggerToken;
typedef CFTypeRef CMSampleBufferRef;
typedef CFTypeRef CMBlockBufferRef;

typedef void (* CMBufferQueueTriggerCallback) (void *triggerRefcon,
    CMBufferQueueTriggerToken triggerToken);
typedef Boolean (* CMBufferQueueValidationCallback) (CMBufferQueueRef queue,
    CMSampleBufferRef buf, void *refCon);

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

enum _CMBufferQueueTriggerCondition
{
  kCMBufferQueueTrigger_WhenDurationBecomesLessThan             = 1,
  kCMBufferQueueTrigger_WhenDurationBecomesLessThanOrEqualTo    = 2,
  kCMBufferQueueTrigger_WhenDurationBecomesGreaterThan          = 3,
  kCMBufferQueueTrigger_WhenDurationBecomesGreaterThanOrEqualTo = 4,
  kCMBufferQueueTrigger_WhenMinPresentationTimeStampChanges     = 5,
  kCMBufferQueueTrigger_WhenMaxPresentationTimeStampChanges     = 6,
  kCMBufferQueueTrigger_WhenDataBecomesReady                    = 7,
  kCMBufferQueueTrigger_WhenEndOfDataReached                    = 8,
  kCMBufferQueueTrigger_WhenReset                               = 9,
  kCMBufferQueueTrigger_WhenBufferCountBecomesLessThan          = 10,
  kCMBufferQueueTrigger_WhenBufferCountBecomesGreaterThan       = 11
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
  OSStatus (* Invalidate) (FigBaseObjectRef obj);
  OSStatus (* Finalize) (FigBaseObjectRef obj);
  gpointer unk4;
  OSStatus (* CopyProperty) (FigBaseObjectRef obj, CFTypeRef key, void *unk,
      CFTypeRef * value);
  OSStatus (* SetProperty) (FigBaseObjectRef obj, CFTypeRef key,
      CFTypeRef value);
};

struct _CMVideoDimensions
{
  UInt32 width;
  UInt32 height;
};

struct _CMTime
{
  UInt8 data[24];
};

struct _GstCMApi
{
  GstDynApi parent;

  FigBaseVTable * (* FigBaseObjectGetVTable) (FigBaseObjectRef obj);

  void * (* CMGetAttachment) (CFTypeRef obj, CFStringRef attachmentKey,
      UInt32 * foundWherePtr);

  void (* FigFormatDescriptionRelease) (CMFormatDescriptionRef desc);
  CMFormatDescriptionRef (* FigFormatDescriptionRetain) (
      CMFormatDescriptionRef desc);
  Boolean (* CMFormatDescriptionEqual) (CMFormatDescriptionRef desc1,
      CMFormatDescriptionRef desc2);
  CFTypeRef (* CMFormatDescriptionGetExtension) (
      const CMFormatDescriptionRef desc, CFStringRef extensionKey);
  UInt32 (* CMFormatDescriptionGetMediaType) (
      const CMFormatDescriptionRef desc);
  UInt32 (* CMFormatDescriptionGetMediaSubType) (
      const CMFormatDescriptionRef desc);

  OSStatus (* CMVideoFormatDescriptionCreate) (
      CFAllocatorRef allocator, UInt32 formatId, UInt32 width, UInt32 height,
      CFDictionaryRef extensions, CMFormatDescriptionRef * desc);
  OSStatus (* FigVideoFormatDescriptionCreateWithSampleDescriptionExtensionAtom)
      (CFAllocatorRef allocator, UInt32 formatId, UInt32 width, UInt32 height,
      UInt32 atomId, const UInt8 * data, CFIndex len, void *unk1,
      CMFormatDescriptionRef * formatDesc);
  CMVideoDimensions (* CMVideoFormatDescriptionGetDimensions) (
      const CMFormatDescriptionRef desc);

  CMTime (* CMTimeMake) (int64_t value, int32_t timescale);

  OSStatus (* CMSampleBufferCreate) (CFAllocatorRef allocator,
      CMBlockBufferRef blockBuf, Boolean dataReady,
      void *makeDataReadyCallback,
      void *makeDataReadyRefcon,
      CMFormatDescriptionRef fmtDesc, size_t numSamples,
      size_t numSampleTimingEntries,
      const void *sampleTimingArray,
      size_t numSampleSizeEntries, const size_t *sampleSizeArray,
      CMSampleBufferRef * sampleBuffer);
  Boolean (* CMSampleBufferDataIsReady) (
      const CMSampleBufferRef buf);
  CMBlockBufferRef (* CMSampleBufferGetDataBuffer) (
      const CMSampleBufferRef buf);
  CMFormatDescriptionRef (* CMSampleBufferGetFormatDescription) (
      const CMSampleBufferRef buf);
  CVImageBufferRef (* CMSampleBufferGetImageBuffer) (
      const CMSampleBufferRef buf);
  SInt32 (* CMSampleBufferGetNumSamples) (
      const CMSampleBufferRef buf);
  CFArrayRef (* CMSampleBufferGetSampleAttachmentsArray) (
      const CMSampleBufferRef buf, SInt32 sampleIndex);
  SInt32 (* CMSampleBufferGetSampleSize) (
      const CMSampleBufferRef buf, SInt32 sampleIndex);
  void (* FigSampleBufferRelease) (CMSampleBufferRef buf);
  CMSampleBufferRef (* FigSampleBufferRetain) (CMSampleBufferRef buf);

  OSStatus (* CMBlockBufferCreateWithMemoryBlock)
      (CFAllocatorRef allocator, void * memoryBlock, size_t blockLength,
      CFAllocatorRef dataAllocator, void *customBlockSource,
      size_t offsetToData, size_t dataLength,
      int flags, CMBlockBufferRef * blockBuffer);
  SInt32 (* CMBlockBufferGetDataLength) (const CMBlockBufferRef buf);
  OSStatus (* CMBlockBufferGetDataPointer) (
      const CMBlockBufferRef buf, UInt32 unk1, UInt32 unk2, UInt32 unk3,
      Byte ** dataPtr);
  void (* FigBlockBufferRelease) (CMBlockBufferRef buf);
  CMBlockBufferRef (* FigBlockBufferRetain) (CMBlockBufferRef buf);

  CMSampleBufferRef (* CMBufferQueueDequeueAndRetain)
      (CMBufferQueueRef queue);
  CFIndex (* CMBufferQueueGetBufferCount) (CMBufferQueueRef queue);
  OSStatus (* CMBufferQueueInstallTrigger) (CMBufferQueueRef queue,
      CMBufferQueueTriggerCallback triggerCallback, void * triggerRefCon,
      CMBufferQueueTriggerCondition triggerCondition, CMTime triggerTime,
      CMBufferQueueTriggerToken * triggerTokenOut);
  Boolean (* CMBufferQueueIsEmpty) (CMBufferQueueRef queue);
  void (* FigBufferQueueRelease) (CMBufferQueueRef queue);
  OSStatus (* CMBufferQueueRemoveTrigger) (CMBufferQueueRef queue,
      CMBufferQueueTriggerToken triggerToken);
  OSStatus (* CMBufferQueueSetValidationCallback) (CMBufferQueueRef queue,
      CMBufferQueueValidationCallback func, void *refCon);

  CFStringRef * kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms;
  CFStringRef * kCMSampleAttachmentKey_DependsOnOthers;
  CMTime * kCMTimeInvalid;
};

struct _GstCMApiClass
{
  GstDynApiClass parent_class;
};

GType gst_cm_api_get_type (void);

GstCMApi * gst_cm_api_obtain (GError ** error);

G_END_DECLS

#endif
