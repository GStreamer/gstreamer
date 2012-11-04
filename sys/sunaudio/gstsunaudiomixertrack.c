/*
 * GStreamer
 * Copyright (C) 2005,2008, 2009 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
 * Copyright (C) 2009 Sun Microsystems, Inc.,
 *               Garrett D'Amore <garrett.damore@sun.com>
 *
 * gstsunaudiomixer.c: mixer interface implementation for OSS
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>

#include <gst/gst-i18n-plugin.h>

#include "gstsunaudiomixertrack.h"

GST_DEBUG_CATEGORY_EXTERN (sunaudio_debug);
#define GST_CAT_DEFAULT sunaudio_debug

#define MASK_BIT_IS_SET(mask, bit) \
  (mask & (1 << bit))

G_DEFINE_TYPE (GstSunAudioMixerTrack, gst_sunaudiomixer_track,
    GST_TYPE_MIXER_TRACK);

static void
gst_sunaudiomixer_track_class_init (GstSunAudioMixerTrackClass * klass)
{
  /* nop */
}

static void
gst_sunaudiomixer_track_init (GstSunAudioMixerTrack * track)
{
  track->gain = 0;
  track->balance = AUDIO_MID_BALANCE;
  track->track_num = 0;
}

GstMixerTrack *
gst_sunaudiomixer_track_new (GstSunAudioTrackType track_num)
{
  const gchar *labels[] = { N_("Volume"),
    N_("Gain"),
    N_("Monitor"),
    N_("Built-in Speaker"),
    N_("Headphone"),
    N_("Line Out"),
    N_("SPDIF Out"),
    N_("AUX 1 Out"),
    N_("AUX 2 Out"),
  };


  GstSunAudioMixerTrack *sunaudiotrack;
  GstMixerTrack *track;
  const gchar *untranslated_label;

  if ((guint) track_num < G_N_ELEMENTS (labels))
    untranslated_label = labels[track_num];
  else
    untranslated_label = NULL;

  sunaudiotrack = g_object_new (GST_TYPE_SUNAUDIO_MIXER_TRACK,
      "untranslated-label", untranslated_label, NULL);

  GST_DEBUG_OBJECT (sunaudiotrack, "Creating new mixer track of type %d: %s",
      track_num, GST_STR_NULL (untranslated_label));

  switch (track_num) {
    case GST_SUNAUDIO_TRACK_OUTPUT:
      /* these are sliders */
      track = GST_MIXER_TRACK (sunaudiotrack);
      track->label = g_strdup (_(untranslated_label));
      track->num_channels = 2;
      track->flags = GST_MIXER_TRACK_OUTPUT | GST_MIXER_TRACK_WHITELIST |
          GST_MIXER_TRACK_MASTER;
      track->min_volume = 0;
      track->max_volume = 255;
      sunaudiotrack->track_num = track_num;
      sunaudiotrack->gain = (0 & 0xff);
      sunaudiotrack->balance = AUDIO_MID_BALANCE;
      break;
    case GST_SUNAUDIO_TRACK_RECORD:
      /* these are sliders */
      track = GST_MIXER_TRACK (sunaudiotrack);
      track->label = g_strdup (_(untranslated_label));
      track->num_channels = 2;
      track->flags = GST_MIXER_TRACK_INPUT | GST_MIXER_TRACK_NO_RECORD |
          GST_MIXER_TRACK_WHITELIST;
      track->min_volume = 0;
      track->max_volume = 255;
      sunaudiotrack->track_num = track_num;
      sunaudiotrack->gain = (0 & 0xff);
      sunaudiotrack->balance = AUDIO_MID_BALANCE;
      break;
    case GST_SUNAUDIO_TRACK_MONITOR:
      /* these are sliders */
      track = GST_MIXER_TRACK (sunaudiotrack);
      track->label = g_strdup (_(untranslated_label));
      track->num_channels = 2;
      track->flags = GST_MIXER_TRACK_INPUT | GST_MIXER_TRACK_NO_RECORD;
      track->min_volume = 0;
      track->max_volume = 255;
      sunaudiotrack->track_num = track_num;
      sunaudiotrack->gain = (0 & 0xff);
      sunaudiotrack->balance = AUDIO_MID_BALANCE;
      break;
    case GST_SUNAUDIO_TRACK_SPEAKER:
    case GST_SUNAUDIO_TRACK_HP:
    case GST_SUNAUDIO_TRACK_LINEOUT:
    case GST_SUNAUDIO_TRACK_SPDIFOUT:
    case GST_SUNAUDIO_TRACK_AUX1OUT:
    case GST_SUNAUDIO_TRACK_AUX2OUT:
      /* these are switches */
      track = GST_MIXER_TRACK (sunaudiotrack);
      track->label = g_strdup (_(untranslated_label));
      track->num_channels = 0;
      track->flags = GST_MIXER_TRACK_OUTPUT | GST_MIXER_TRACK_WHITELIST;
      track->min_volume = 0;
      track->max_volume = 255;
      sunaudiotrack->track_num = track_num;
      sunaudiotrack->gain = (0 & 0xff);
      sunaudiotrack->balance = AUDIO_MID_BALANCE;
      break;
    default:
      g_warning ("Unknown sun audio track num %d", track_num);
      track = NULL;
  }

  return track;
}
