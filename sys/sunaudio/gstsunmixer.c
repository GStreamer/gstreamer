/*
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2004 David A. Schleef <ds@schleef.org>
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

#include "gst/gst-i18n-plugin.h"
#include "gstsunmixer.h"

static void gst_sunaudiomixer_track_class_init (GstSunAudioMixerTrackClass *
    klass);
static void gst_sunaudiomixer_track_init (GstSunAudioMixerTrack * track);

static gboolean gst_sunaudiomixer_supported (GstImplementsInterface * iface,
    GType iface_type);
static const GList *gst_sunaudiomixer_list_tracks (GstMixer * sunaudiomixer);
static void gst_sunaudiomixer_set_volume (GstMixer * sunaudiomixer,
    GstMixerTrack * track, gint * volumes);
static void gst_sunaudiomixer_get_volume (GstMixer * sunaudiomixer,
    GstMixerTrack * track, gint * volumes);
static void gst_sunaudiomixer_set_mute (GstMixer * sunaudiomixer,
    GstMixerTrack * track, gboolean mute);
static void gst_sunaudiomixer_set_record (GstMixer * sunaudiomixer,
    GstMixerTrack * track, gboolean record);

#define MIXER_DEVICES 3
static gchar **labels = NULL;
static GstMixerTrackClass *parent_class = NULL;

GType
gst_sunaudiomixer_track_get_type (void)
{
  static GType gst_sunaudiomixer_track_type = 0;

  if (!gst_sunaudiomixer_track_type) {
    static const GTypeInfo sunaudiomixer_track_info = {
      sizeof (GstSunAudioMixerTrackClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_sunaudiomixer_track_class_init,
      NULL,
      NULL,
      sizeof (GstSunAudioMixerTrack),
      0,
      (GInstanceInitFunc) gst_sunaudiomixer_track_init,
      NULL
    };

    gst_sunaudiomixer_track_type = g_type_register_static (GST_TYPE_MIXER_TRACK,
        "GstSunAudioMixerTrack", &sunaudiomixer_track_info, 0);

  }

  return gst_sunaudiomixer_track_type;
}

static void
gst_sunaudiomixer_track_class_init (GstSunAudioMixerTrackClass * klass)
{
  parent_class = g_type_class_ref (GST_TYPE_MIXER_TRACK);
}

static void
gst_sunaudiomixer_track_init (GstSunAudioMixerTrack * track)
{
  track->lvol = track->rvol = 0;
  track->track_num = 0;
}

void
gst_sunaudio_interface_init (GstImplementsInterfaceClass * klass)
{
  /* default virtual functions */
  klass->supported = gst_sunaudiomixer_supported;
}

static gboolean
gst_sunaudiomixer_supported (GstImplementsInterface * iface, GType iface_type)
{
  g_assert (iface_type == GST_TYPE_MIXER);

  return (GST_SUNAUDIOELEMENT (iface)->mixer_fd != -1);
}

void
gst_sunaudiomixer_interface_init (GstMixerClass * klass)
{
  GST_MIXER_TYPE (klass) = GST_MIXER_HARDWARE;

  klass->list_tracks = gst_sunaudiomixer_list_tracks;
  klass->set_volume = gst_sunaudiomixer_set_volume;
  klass->get_volume = gst_sunaudiomixer_get_volume;
  klass->set_mute = gst_sunaudiomixer_set_mute;
  klass->set_record = gst_sunaudiomixer_set_record;
}

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
gst_sunaudiomixer_track_new (GstSunAudioElement * sunaudio,
    gint track_num, gint max_chans, gint flags)
{
  GstSunAudioMixerTrack *sunaudiotrack;
  GstMixerTrack *track;
  gint volume;

  if (!labels)
    fill_labels ();

  sunaudiotrack = g_object_new (GST_TYPE_SUNAUDIOMIXER_TRACK, NULL);
  track = GST_MIXER_TRACK (sunaudiotrack);
  track->label = g_strdup (labels[track_num]);
  track->num_channels = max_chans;
  track->flags = flags;
  track->min_volume = 0;
  track->max_volume = 100;
  sunaudiotrack->track_num = track_num;

  sunaudiotrack->lvol = (0 & 0xff);

  return track;
}

void
gst_sunaudiomixer_build_list (GstSunAudioElement * sunaudio)
{
  GstMixerTrack *track;

  sunaudio->mixer_fd = open (sunaudio->mixer_dev, O_RDWR);

  if (sunaudio->mixer_fd == -1) {
    return;
  }

  sunaudio->device_name = g_strdup ("Unknown");

  track = gst_sunaudiomixer_track_new (sunaudio, 0, 1, GST_MIXER_TRACK_OUTPUT);
  sunaudio->tracklist = g_list_append (sunaudio->tracklist, track);
  track = gst_sunaudiomixer_track_new (sunaudio, 1, 1, 0);
  sunaudio->tracklist = g_list_append (sunaudio->tracklist, track);
  track = gst_sunaudiomixer_track_new (sunaudio, 2, 1, 0);
  sunaudio->tracklist = g_list_append (sunaudio->tracklist, track);
}

static const GList *
gst_sunaudiomixer_list_tracks (GstMixer * mixer)
{
  return (const GList *) GST_SUNAUDIOELEMENT (mixer)->tracklist;
}

static void
gst_sunaudiomixer_set_volume (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes)
{
  gint volume;
  gchar buf[100];
  struct audio_info audioinfo;
  GstSunAudioElement *sunaudio = GST_SUNAUDIOELEMENT (mixer);
  GstSunAudioMixerTrack *sunaudiotrack = GST_SUNAUDIOMIXER_TRACK (track);

  g_return_if_fail (sunaudio->mixer_fd != -1);
  volume = (volumes[0] * 255) / 100;

  /* Set the volume */
  AUDIO_INITINFO (&audioinfo);

  switch (sunaudiotrack->track_num) {
    case 0:
      audioinfo.play.gain = volume;
      break;
    case 1:
      audioinfo.record.gain = volume;
      break;
    case 2:
      audioinfo.monitor_gain = volume;
      break;
  }


  if (ioctl (sunaudio->mixer_fd, AUDIO_SETINFO, &audioinfo) < 0) {
    g_warning ("Error setting volume");
    return;
  }
}

static void
gst_sunaudiomixer_get_volume (GstMixer * mixer,
    GstMixerTrack * track, gint * volumes)
{
  gint volume;
  struct audio_info audioinfo;
  GstSunAudioElement *sunaudio = GST_SUNAUDIOELEMENT (mixer);
  GstSunAudioMixerTrack *sunaudiotrack = GST_SUNAUDIOMIXER_TRACK (track);

  g_return_if_fail (sunaudio->mixer_fd != -1);

  if (ioctl (sunaudio->mixer_fd, AUDIO_GETINFO, &audioinfo) < 0) {
    g_warning ("Error setting volume device");
    return;
  }

  switch (sunaudiotrack->track_num) {
    case 0:
      sunaudiotrack->lvol = volumes[0] = (audioinfo.play.gain * 100) / 255;
      break;
    case 1:
      sunaudiotrack->lvol = volumes[0] = (audioinfo.record.gain * 100) / 255;
      break;
    case 2:
      sunaudiotrack->lvol = volumes[0] = (audioinfo.monitor_gain * 100) / 255;
      break;
  }

}

static void
gst_sunaudiomixer_set_mute (GstMixer * mixer,
    GstMixerTrack * track, gboolean mute)
{
  struct audio_info audioinfo;
  GstSunAudioElement *sunaudio = GST_SUNAUDIOELEMENT (mixer);
  GstSunAudioMixerTrack *sunaudiotrack = GST_SUNAUDIOMIXER_TRACK (track);

  g_return_if_fail (sunaudio->mixer_fd != -1);
  if (sunaudiotrack->track_num != 0)
    return;

  AUDIO_INITINFO (&audioinfo);

  if (mute) {
    audioinfo.output_muted = 1;
  } else {
    audioinfo.output_muted = 0;
  }

  if (ioctl (sunaudio->mixer_fd, AUDIO_SETINFO, &audioinfo) < 0) {
    g_warning ("Error setting volume device");
    return;
  }
}

static void
gst_sunaudiomixer_set_record (GstMixer * mixer,
    GstMixerTrack * track, gboolean record)
{

  /* Implementation Pending */

}
