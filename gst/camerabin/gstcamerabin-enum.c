/*
 * GStreamer
 * Copyright (C) 2009 Nokia Corporation <multimedia@maemo.org>
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

#include "gstcamerabin-enum.h"

#define C_FLAGS(v) ((guint) v)

static void
register_gst_camerabin_flags (GType * id)
{
  static const GFlagsValue values[] = {
    {C_FLAGS (GST_CAMERABIN_FLAG_SOURCE_RESIZE),
        "Enable source crop and scale", "source-resize"},
    {C_FLAGS (GST_CAMERABIN_FLAG_SOURCE_COLOR_CONVERSION),
          "Enable colorspace conversion for video source",
        "source-colorspace-conversion"},
    {C_FLAGS (GST_CAMERABIN_FLAG_VIEWFINDER_COLOR_CONVERSION),
          "Enable colorspace conversion for viewfinder",
        "viewfinder-colorspace-conversion"},
    {C_FLAGS (GST_CAMERABIN_FLAG_VIEWFINDER_SCALE),
        "Enable scale for viewfinder", "viewfinder-scale"},
    {C_FLAGS (GST_CAMERABIN_FLAG_AUDIO_CONVERSION),
        "Enable audio conversion for video capture", "audio-conversion"},
    {C_FLAGS (GST_CAMERABIN_FLAG_DISABLE_AUDIO),
        "Disable audio elements for video capture", "disable-audio"},
    {C_FLAGS (GST_CAMERABIN_FLAG_IMAGE_COLOR_CONVERSION),
          "Enable colorspace conversion for still image",
        "image-colorspace-conversion"},
    {C_FLAGS (GST_CAMERABIN_FLAG_VIDEO_COLOR_CONVERSION),
          "Enable colorspace conversion for video capture",
        "video-colorspace-conversion"},
    {0, NULL, NULL}
  };
  *id = g_flags_register_static ("GstCameraBinFlags", values);
}

GType
gst_camerabin_flags_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_camerabin_flags, &id);
  return id;
}
