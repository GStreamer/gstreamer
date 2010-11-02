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

#ifndef __GST_CEL_API_H__
#define __GST_CEL_API_H__

#include "mtapi.h"

G_BEGIN_DECLS

typedef struct _GstCelApi GstCelApi;
typedef struct _GstCelApiClass GstCelApiClass;

enum
{
  kCelError_ResourceBusy = -12780
};

struct _GstCelApi
{
  GstDynApi parent;

  OSStatus (* FigCreateCaptureDevicesAndStreamsForPreset)
      (CFAllocatorRef allocator, CFStringRef capturePreset,
      CFDictionaryRef audioOptions,
      FigCaptureDeviceRef * outVideoDevice,
      FigCaptureStreamRef * outVideoStream,
      FigCaptureDeviceRef * outAudioDevice,
      FigCaptureStreamRef * outAudioStream);

  CFStringRef * kFigRecorderCapturePreset_AudioRecording;
  CFStringRef * kFigRecorderCapturePreset_VideoRecording;
  CFStringRef * kFigRecorderCapturePreset_AudioVideoRecording;
  CFStringRef * kFigRecorderCapturePreset_PhotoCapture;
};

struct _GstCelApiClass
{
  GstDynApiClass parent_class;
};

GType gst_cel_api_get_type (void);

GstCelApi * gst_cel_api_obtain (GError ** error);

G_END_DECLS

#endif
