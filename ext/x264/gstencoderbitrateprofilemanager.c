/* GStreamer
 * Copyright (C) 2019 Thibault Saunier <tsaunier@igalia.com>
 *
 * gstencoderbitrateprofilemanager.c
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

#include "gstencoderbitrateprofilemanager.h"

GST_DEBUG_CATEGORY_STATIC (encoderbitratemanager_debug);
#define GST_CAT_DEFAULT encoderbitratemanager_debug

typedef struct
{
  gchar *name;
  gsize n_vals;
  GstEncoderBitrateTargetForPixelsMap *map;
} GstEncoderBitrateProfile;

struct _GstEncoderBitrateProfileManager
{
  GList *profiles;
  gchar *preset;
  guint bitrate;

  gboolean setting_preset;
  gboolean user_bitrate;
};

/* *INDENT-OFF* */
/* Copied from https://support.google.com/youtube/answer/1722171?hl=en */
static const GstEncoderBitrateTargetForPixelsMap youtube_bitrate_profiles[] = {
  {
        .n_pixels = 3840 * 2160,
        .low_framerate_bitrate = 40000,
        .high_framerate_bitrate = 60000,
  },
  {
        .n_pixels = 2560 * 1440,
        .low_framerate_bitrate = 16000,
        .high_framerate_bitrate = 24000,
  },
  {
        .n_pixels = 1920 * 1080,
        .low_framerate_bitrate = 8000,
        .high_framerate_bitrate = 12000,
  },
  {
        .n_pixels = 1080 * 720,
        .low_framerate_bitrate = 5000,
        .high_framerate_bitrate = 7500,
      },
  {
        .n_pixels = 640 * 480,
        .low_framerate_bitrate = 2500,
        .high_framerate_bitrate = 4000,
  },
  {
        .n_pixels = 0,
        .low_framerate_bitrate = 2500,
        .high_framerate_bitrate = 4000,
  },
  {
        .n_pixels = 0,
        .low_framerate_bitrate = 0,
        .high_framerate_bitrate = 0,
  },
};
/* *INDENT-ON* */

static void
gst_encoder_bitrate_profile_free (GstEncoderBitrateProfile * profile)
{
  g_free (profile->name);
  g_free (profile->map);
  g_free (profile);
}

void
gst_encoder_bitrate_profile_manager_add_profile (GstEncoderBitrateProfileManager
    * self, const gchar * profile_name,
    const GstEncoderBitrateTargetForPixelsMap * map)
{
  gint n_vals;
  GstEncoderBitrateProfile *profile;

  for (n_vals = 0;
      map[n_vals].low_framerate_bitrate != 0
      && map[n_vals].high_framerate_bitrate != 0; n_vals++);
  n_vals++;

  profile = g_new0 (GstEncoderBitrateProfile, 1);
  profile->name = g_strdup (profile_name);
  profile->n_vals = n_vals;
  profile->map
      = g_memdup (map, sizeof (GstEncoderBitrateTargetForPixelsMap) * n_vals);
  self->profiles = g_list_prepend (self->profiles, profile);
}

guint
gst_encoder_bitrate_profile_manager_get_bitrate (GstEncoderBitrateProfileManager
    * self, GstVideoInfo * info)
{
  gint i;
  gboolean high_fps;
  guint num_pix;
  GList *tmp;

  GstEncoderBitrateProfile *profile = NULL;

  g_return_val_if_fail (self != NULL, -1);

  if (!info || info->finfo == NULL
      || info->finfo->format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_INFO ("Video info %p not usable, returning current bitrate", info);
    return self->bitrate;
  }

  if (!self->preset) {
    GST_INFO ("No preset used, returning current bitrate");
    return self->bitrate;

  }

  for (tmp = self->profiles; tmp; tmp = tmp->next) {
    GstEncoderBitrateProfile *tmpprof = tmp->data;
    if (!g_strcmp0 (tmpprof->name, self->preset)) {
      profile = tmpprof;
      break;
    }
  }

  if (!profile) {
    GST_INFO ("Could not find map for profile: %s", self->preset);

    return self->bitrate;
  }

  high_fps = GST_VIDEO_INFO_FPS_N (info) / GST_VIDEO_INFO_FPS_D (info) > 30.0;
  num_pix = GST_VIDEO_INFO_WIDTH (info) * GST_VIDEO_INFO_HEIGHT (info);
  for (i = 0; i < profile->n_vals; i++) {
    GstEncoderBitrateTargetForPixelsMap *bitrate_values = &profile->map[i];

    if (num_pix < bitrate_values->n_pixels)
      continue;

    self->bitrate =
        high_fps ? bitrate_values->
        high_framerate_bitrate : bitrate_values->low_framerate_bitrate;
    GST_INFO ("Using %s bitrate! %d", self->preset, self->bitrate);
    return self->bitrate;
  }

  return -1;
}

void gst_encoder_bitrate_profile_manager_start_loading_preset
    (GstEncoderBitrateProfileManager * self)
{
  self->setting_preset = TRUE;
}

void gst_encoder_bitrate_profile_manager_end_loading_preset
    (GstEncoderBitrateProfileManager * self, const gchar * preset)
{
  self->setting_preset = FALSE;
  g_free (self->preset);
  self->preset = g_strdup (preset);
}

void
gst_encoder_bitrate_profile_manager_set_bitrate (GstEncoderBitrateProfileManager
    * self, guint bitrate)
{
  self->bitrate = bitrate;
  self->user_bitrate = !self->setting_preset;
}

void
gst_encoder_bitrate_profile_manager_free (GstEncoderBitrateProfileManager *
    self)
{
  g_free (self->preset);
  g_list_free_full (self->profiles,
      (GDestroyNotify) gst_encoder_bitrate_profile_free);
  g_free (self);
}

GstEncoderBitrateProfileManager *
gst_encoder_bitrate_profile_manager_new (guint default_bitrate)
{
  GstEncoderBitrateProfileManager *self =
      g_new0 (GstEncoderBitrateProfileManager, 1);
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "encoderbitratemanager", 0,
        "Encoder bitrate manager");
    g_once_init_leave (&_init, 1);
  }

  self->bitrate = default_bitrate;
  gst_encoder_bitrate_profile_manager_add_profile (self,
      "Profile YouTube", youtube_bitrate_profiles);

  return self;
}
