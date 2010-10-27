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

#include "cmapi.h"

#include "dynapi-internal.h"

#define CM_FRAMEWORK_PATH "/System/Library/PrivateFrameworks/" \
    "CoreMedia.framework/CoreMedia"

G_DEFINE_TYPE (GstCMApi, gst_cm_api, GST_TYPE_DYN_API);

static void
gst_cm_api_init (GstCMApi * self)
{
}

static void
gst_cm_api_class_init (GstCMApiClass * klass)
{
}

#define SYM_SPEC(name) GST_DYN_SYM_SPEC (GstCMApi, name)

GstCMApi *
gst_cm_api_obtain (GError ** error)
{
  static const GstDynSymSpec symbols[] = {
    SYM_SPEC (FigBaseObjectGetVTable),

    SYM_SPEC (FigGetAttachment),

    SYM_SPEC (FigFormatDescriptionRelease),
    SYM_SPEC (FigFormatDescriptionRetain),
    SYM_SPEC (FigFormatDescriptionEqual),
    SYM_SPEC (FigFormatDescriptionGetExtension),
    SYM_SPEC (FigFormatDescriptionGetMediaType),
    SYM_SPEC (FigFormatDescriptionGetMediaSubType),

    SYM_SPEC (FigVideoFormatDescriptionCreate),
    SYM_SPEC
        (FigVideoFormatDescriptionCreateWithSampleDescriptionExtensionAtom),
    SYM_SPEC (FigVideoFormatDescriptionGetDimensions),

    SYM_SPEC (FigTimeMake),

    SYM_SPEC (FigSampleBufferCreate),
    SYM_SPEC (FigSampleBufferDataIsReady),
    SYM_SPEC (FigSampleBufferGetDataBuffer),
    SYM_SPEC (FigSampleBufferGetFormatDescription),
    SYM_SPEC (FigSampleBufferGetImageBuffer),
    SYM_SPEC (FigSampleBufferGetNumSamples),
    SYM_SPEC (FigSampleBufferGetSampleAttachmentsArray),
    SYM_SPEC (FigSampleBufferGetSampleSize),
    SYM_SPEC (FigSampleBufferRelease),
    SYM_SPEC (FigSampleBufferRetain),

    SYM_SPEC (FigBlockBufferCreateWithMemoryBlock),
    SYM_SPEC (FigBlockBufferGetDataLength),
    SYM_SPEC (FigBlockBufferGetDataPointer),
    SYM_SPEC (FigBlockBufferRelease),
    SYM_SPEC (FigBlockBufferRetain),

    SYM_SPEC (FigBufferQueueDequeueAndRetain),
    SYM_SPEC (FigBufferQueueGetBufferCount),
    SYM_SPEC (FigBufferQueueIsEmpty),
    SYM_SPEC (FigBufferQueueRelease),
    SYM_SPEC (FigBufferQueueSetValidationCallback),

    SYM_SPEC (kFigFormatDescriptionExtension_SampleDescriptionExtensionAtoms),
    SYM_SPEC (kFigSampleAttachmentKey_DependsOnOthers),
    SYM_SPEC (kFigTimeInvalid),

    {NULL, 0},
  };

  return _gst_dyn_api_new (gst_cm_api_get_type (), CM_FRAMEWORK_PATH, symbols,
      error);
}
