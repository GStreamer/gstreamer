/*
 *  GStreamer pulseaudio plugin
 *
 *  Copyright (c) 2004-2008 Lennart Poettering
 *
 *  gst-pulse is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or (at your option) any later version.
 *
 *  gst-pulse is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with gst-pulse; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "pulsemixertrack.h"

GST_DEBUG_CATEGORY_EXTERN (pulse_debug);
#define GST_CAT_DEFAULT pulse_debug

G_DEFINE_TYPE (GstPulseMixerTrack, gst_pulsemixer_track, GST_TYPE_MIXER_TRACK);

static void
gst_pulsemixer_track_class_init (GstPulseMixerTrackClass * klass)
{
}

static void
gst_pulsemixer_track_init (GstPulseMixerTrack * track)
{
  track->control = NULL;
}

GstMixerTrack *
gst_pulsemixer_track_new (GstPulseMixerCtrl * control)
{
  GstPulseMixerTrack *pulsetrack;
  GstMixerTrack *track;

  pulsetrack = g_object_new (GST_TYPE_PULSEMIXER_TRACK, NULL);
  pulsetrack->control = control;

  track = GST_MIXER_TRACK (pulsetrack);
  track->label = g_strdup ("Master");
  track->num_channels = control->channel_map.channels;
  track->flags =
      (control->type ==
      GST_PULSEMIXER_SINK ? GST_MIXER_TRACK_OUTPUT | GST_MIXER_TRACK_MASTER :
      GST_MIXER_TRACK_INPUT | GST_MIXER_TRACK_RECORD) | (control->muted ?
      GST_MIXER_TRACK_MUTE : 0);
  track->min_volume = PA_VOLUME_MUTED;
  track->max_volume = PA_VOLUME_NORM;

  return track;
}
