/* GStreamer OSS4 mixer on/off switch control
 * Copyright (C) 2007-2008 Tim-Philipp Müller <tim centricular net>
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

/* A simple ON/OFF 'switch' in gnome-volume-control / GstMixer is represented
 * by a GstMixerTrack with no channels.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>

#define NO_LEGACY_MIXER
#include "oss4-mixer-switch.h"
#include "oss4-soundcard.h"

GST_DEBUG_CATEGORY_EXTERN (oss4mixer_debug);
#define GST_CAT_DEFAULT oss4mixer_debug

/* GstMixerTrack is a plain GObject, so let's just use the GLib macro here */
G_DEFINE_TYPE (GstOss4MixerSwitch, gst_oss4_mixer_switch, GST_TYPE_MIXER_TRACK);

static void
gst_oss4_mixer_switch_class_init (GstOss4MixerSwitchClass * klass)
{
  /* nothing to do here */
}

static void
gst_oss4_mixer_switch_init (GstOss4MixerSwitch * s)
{
  /* nothing to do here */
}

static GstMixerTrackFlags
gst_oss4_mixer_switch_get_switch_flag (GstMixerTrack * track)
{
  if ((track->flags & GST_MIXER_TRACK_INPUT)) {
    return GST_MIXER_TRACK_RECORD;
  } else if ((track->flags & GST_MIXER_TRACK_OUTPUT)) {
    return GST_MIXER_TRACK_MUTE;
  } else {
    GST_ERROR_OBJECT (track, "switch neither input nor output track!?");
  }
  return 0;
}

gboolean
gst_oss4_mixer_switch_set (GstOss4MixerSwitch * s, gboolean enabled)
{
  GstMixerTrackFlags switch_flag;
  GstMixerTrack *track;
  int newval;

  track = GST_MIXER_TRACK (s);
  switch_flag = gst_oss4_mixer_switch_get_switch_flag (track);

  newval = (enabled) ? 1 : 0;

  if (!!newval == !!(track->flags & switch_flag)) {
    GST_LOG_OBJECT (s, "switch is already %d, doing nothing", newval);
    return TRUE;
  }

  if (!gst_oss4_mixer_set_control_val (s->mixer, s->mc, newval)) {
    GST_WARNING_OBJECT (s, "could not set switch to %d", newval);
    return FALSE;
  }

  if (newval) {
    track->flags |= switch_flag;
  } else {
    track->flags &= ~switch_flag;
  }

  GST_LOG_OBJECT (s, "set switch to %d", newval);

  return TRUE;
}

gboolean
gst_oss4_mixer_switch_get (GstOss4MixerSwitch * s, gboolean * enabled)
{
  GstMixerTrackFlags switch_flag;
  GstMixerTrack *track;
  int val = -1;

  track = GST_MIXER_TRACK (s);
  switch_flag = gst_oss4_mixer_switch_get_switch_flag (track);

  if (!gst_oss4_mixer_get_control_val (s->mixer, s->mc, &val) || val < 0) {
    GST_WARNING_OBJECT (s, "could not get switch state");
    return FALSE;
  }

  *enabled = (val != 0);

  if (!!val != !!(track->flags & switch_flag)) {
    GST_INFO_OBJECT (s, "updating inconsistent switch state to %d", !!val);
    if (*enabled) {
      track->flags |= switch_flag;
    } else {
      track->flags &= ~switch_flag;
    }
  }


  return TRUE;
}

GstMixerTrack *
gst_oss4_mixer_switch_new (GstOss4Mixer * mixer, GstOss4MixerControl * mc)
{
  GstOss4MixerSwitch *s;
  GstMixerTrack *track;
  int cur = -1;

  s = g_object_new (GST_TYPE_OSS4_MIXER_SWITCH, "untranslated-label",
      mc->mixext.extname, NULL);

  s->mixer = mixer;
  s->mc = mc;

  track = GST_MIXER_TRACK (s);

  /* caller will set track->label and track->flags */

  track->num_channels = 0;
  track->min_volume = 0;
  track->max_volume = 0;

  if (!gst_oss4_mixer_get_control_val (s->mixer, s->mc, &cur) || cur < 0)
    return NULL;

  return track;
}

/* This is called from the watch thread */
void
gst_oss4_mixer_switch_process_change_unlocked (GstMixerTrack * track)
{
  GstOss4MixerSwitch *s = GST_OSS4_MIXER_SWITCH_CAST (track);

  if (!s->mc->changed)
    return;

  if ((track->flags & GST_MIXER_TRACK_INPUT)) {
    gst_mixer_record_toggled (GST_MIXER (s->mixer), track, !!s->mc->last_val);
  } else {
    gst_mixer_mute_toggled (GST_MIXER (s->mixer), track, !!s->mc->last_val);
  }
}
