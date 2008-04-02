/*
 * GStreamer
 * Copyright (C) 2005,2008 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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

#define MASK_BIT_IS_SET(mask, bit) \
  (mask & (1 << bit))

G_DEFINE_TYPE (GstSunAudioMixerTrack, gst_sunaudiomixer_track,
    GST_TYPE_MIXER_TRACK)

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
gst_sunaudiomixer_track_new (GstSunAudioTrackType track_num,
    gint max_chans, gint flags)
{
  const gchar *labels[] = { N_("Volume"), N_("Gain"), N_("Monitor") };

  GstSunAudioMixerTrack *sunaudiotrack;
  GstMixerTrack *track;
  GObjectClass *klass;
  const gchar *untranslated_label;
  gint volume;

  if ((guint) track_num < G_N_ELEMENTS (labels))
    untranslated_label = labels[track_num];
  else
    untranslated_label = NULL;

  /* FIXME: remove this check once we depend on -base >= 0.10.12.1 */
  klass = G_OBJECT_CLASS (g_type_class_ref (GST_TYPE_SUNAUDIO_MIXER_TRACK));
  if (g_object_class_find_property (klass, "untranslated-label")) {
    sunaudiotrack = g_object_new (GST_TYPE_SUNAUDIO_MIXER_TRACK,
        "untranslated-label", untranslated_label, NULL);
  } else {
    sunaudiotrack = g_object_new (GST_TYPE_SUNAUDIO_MIXER_TRACK, NULL);
  }
  g_type_class_unref (klass);

  track = GST_MIXER_TRACK (sunaudiotrack);
  track->label = g_strdup (_(untranslated_label));
  track->num_channels = max_chans;
  track->flags = flags;
  track->min_volume = 0;
  track->max_volume = 255;
  sunaudiotrack->track_num = track_num;
  sunaudiotrack->gain = (0 & 0xff);
  sunaudiotrack->balance = AUDIO_MID_BALANCE;

  return track;
}
