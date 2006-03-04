/*
 * GStreamer
 * Copyright (C) 2005 Brian Cameron <brian.cameron@sun.com>
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

#include <gst/gst-i18n-plugin.h>

#include "gstsunaudiomixertrack.h"

#define MIXER_DEVICES 3
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
  track->vol = 0;
  track->track_num = 0;
}

static const gchar **labels = NULL;

static void
fill_labels (void)
{
  int i;
  struct
  {
    gchar *given, *wanted;
  }
  cases[] = {
    {
    "Vol  ", N_("Volume")}
    , {
    "Gain ", N_("Gain")}
    , {
    "Mon  ", N_("Monitor")}
    , {
    NULL, NULL}
  };

  labels = g_malloc (sizeof (gchar *) * MIXER_DEVICES);

  for (i = 0; i < MIXER_DEVICES; i++) {
    labels[i] = g_strdup (cases[i].wanted);
  }
}

GstMixerTrack *
gst_sunaudiomixer_track_new (gint track_num, gint max_chans, gint flags)
{
  GstSunAudioMixerTrack *sunaudiotrack;
  GstMixerTrack *track;

  if (!labels)
    fill_labels ();

  sunaudiotrack = g_object_new (GST_TYPE_SUNAUDIO_MIXER_TRACK, NULL);
  track = GST_MIXER_TRACK (sunaudiotrack);
  track->label = g_strdup (labels[track_num]);
  track->num_channels = max_chans;
  track->flags = flags;
  track->min_volume = 0;
  track->max_volume = 100;
  sunaudiotrack->track_num = track_num;

  sunaudiotrack->vol = (0 & 0xff);

  return track;
}
