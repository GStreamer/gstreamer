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

#include "celapi.h"

#include "dynapi-internal.h"

#define CELESTIAL_FRAMEWORK_PATH "/System/Library/PrivateFrameworks/" \
    "Celestial.framework/Celestial"

G_DEFINE_TYPE (GstCelApi, gst_cel_api, GST_TYPE_DYN_API);

static void
gst_cel_api_init (GstCelApi * self)
{
}

static void
gst_cel_api_class_init (GstCelApiClass * klass)
{
}

#define SYM_SPEC(name) GST_DYN_SYM_SPEC (GstCelApi, name)

GstCelApi *
gst_cel_api_obtain (GError ** error)
{
  static const GstDynSymSpec symbols[] = {
    SYM_SPEC (FigCreateCaptureDevicesAndStreamsForPreset),

    SYM_SPEC (kFigRecorderCapturePreset_AudioRecording),
    SYM_SPEC (kFigRecorderCapturePreset_VideoRecording),
    SYM_SPEC (kFigRecorderCapturePreset_AudioVideoRecording),
    SYM_SPEC (kFigRecorderCapturePreset_PhotoCapture),

    {NULL, 0},
  };

  return _gst_dyn_api_new (gst_cel_api_get_type (), CELESTIAL_FRAMEWORK_PATH,
      symbols, error);
}
