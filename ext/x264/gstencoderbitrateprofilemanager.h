/* GStreamer
 * Copyright (C) 2019 Thibault Saunier <tsaunier@igalia.com>
 *
 * gstencoderbitrateprofilemanager.h
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>

typedef struct _GstEncoderBitrateProfileManager GstEncoderBitrateProfileManager;

typedef struct _GstEncoderBitrateTargetForPixelsMap
{
  guint n_pixels;
  guint low_framerate_bitrate;
  guint high_framerate_bitrate;

  gpointer _gst_reserved[GST_PADDING_LARGE];
} GstEncoderBitrateTargetForPixelsMap;

void
gst_encoder_bitrate_profile_manager_add_profile(GstEncoderBitrateProfileManager* self,
    const gchar* profile_name, const GstEncoderBitrateTargetForPixelsMap* map);
guint gst_encoder_bitrate_profile_manager_get_bitrate(GstEncoderBitrateProfileManager* self, GstVideoInfo* info);
void gst_encoder_bitrate_profile_manager_start_loading_preset (GstEncoderBitrateProfileManager* self);
void gst_encoder_bitrate_profile_manager_end_loading_preset(GstEncoderBitrateProfileManager* self, const gchar* preset);
void gst_encoder_bitrate_profile_manager_set_bitrate(GstEncoderBitrateProfileManager* self, guint bitrate);
GstEncoderBitrateProfileManager* gst_encoder_bitrate_profile_manager_new(guint default_bitrate);
void gst_encoder_bitrate_profile_manager_free(GstEncoderBitrateProfileManager* self);