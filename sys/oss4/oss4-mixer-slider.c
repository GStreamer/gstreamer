/* GStreamer OSS4 mixer slider control
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

/* A 'slider' in gnome-volume-control / GstMixer is represented by a
 * GstMixerTrack with one or more channels.
 *
 * A slider should be either flagged as INPUT or OUTPUT (mostly because of
 * gnome-volume-control being littered with g_asserts for everything it doesn't
 * expect).
 *
 * From mixertrack.h:
 * "Input tracks can have 'recording' enabled, which means that any input will
 * be hearable into the speakers that are attached to the output. Mute is
 * obvious."
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

#define NO_LEGACY_MIXER
#include "oss4-mixer-slider.h"

GST_DEBUG_CATEGORY_EXTERN (oss4mixer_debug);
#define GST_CAT_DEFAULT oss4mixer_debug

/* GstMixerTrack is a plain GObject, so let's just use the GLib macro here */
G_DEFINE_TYPE (GstOss4MixerSlider, gst_oss4_mixer_slider, GST_TYPE_MIXER_TRACK);

static void
gst_oss4_mixer_slider_class_init (GstOss4MixerSliderClass * klass)
{
  /* nothing to do here */
}

static void
gst_oss4_mixer_slider_init (GstOss4MixerSlider * s)
{
  /* nothing to do here */
}

static int
gst_oss4_mixer_slider_pack_volume (GstOss4MixerSlider * s, const gint * volumes)
{
  int val = 0;

  switch (s->mc->mixext.type) {
    case MIXT_MONOSLIDER:
    case MIXT_MONOSLIDER16:
    case MIXT_SLIDER:
      val = volumes[0];
      break;
    case MIXT_STEREOSLIDER:
      val = ((volumes[1] & 0xff) << 8) | (volumes[0] & 0xff);
      break;
    case MIXT_STEREOSLIDER16:
      val = ((volumes[1] & 0xffff) << 16) | (volumes[0] & 0xffff);
      break;
    default:
      g_return_val_if_reached (0);
  }
  return val;
}

static void
gst_oss4_mixer_slider_unpack_volume (GstOss4MixerSlider * s, int v,
    gint * volumes)
{
  guint32 val;                  /* use uint so bitshifting the highest bit works right */

  val = (guint32) v;
  switch (s->mc->mixext.type) {
    case MIXT_SLIDER:
      volumes[0] = val;
      break;
    case MIXT_MONOSLIDER:
      /* oss repeats the value in the upper bits, as if it was stereo */
      volumes[0] = val & 0x00ff;
      break;
    case MIXT_MONOSLIDER16:
      /* oss repeats the value in the upper bits, as if it was stereo */
      volumes[0] = val & 0x0000ffff;
      break;
    case MIXT_STEREOSLIDER:
      volumes[0] = (val & 0x00ff);
      volumes[1] = (val & 0xff00) >> 8;
      break;
    case MIXT_STEREOSLIDER16:
      volumes[0] = (val & 0x0000ffff);
      volumes[1] = (val & 0xffff0000) >> 16;
      break;
    default:
      g_return_if_reached ();
  }
}

gboolean
gst_oss4_mixer_slider_get_volume (GstOss4MixerSlider * s, gint * volumes)
{
  GstMixerTrack *track = GST_MIXER_TRACK (s);
  int v = 0;

  /* if we're supposed to be muted, and don't have an actual mute control
   * (ie. 'simulate' the mute), then just return the volume as saved, not
   * the actually set volume which is most likely 0 */
  if (GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_MUTE) && !s->mc->mute) {
    volumes[0] = s->volumes[0];
    if (track->num_channels == 2)
      volumes[1] = s->volumes[1];
    return TRUE;
  }

  if (!gst_oss4_mixer_get_control_val (s->mixer, s->mc, &v))
    return FALSE;

  gst_oss4_mixer_slider_unpack_volume (s, v, volumes);

  if (track->num_channels > 1) {
    GST_LOG_OBJECT (s, "volume: left=%d, right=%d", volumes[0], volumes[1]);
  } else {
    GST_LOG_OBJECT (s, "volume: mono=%d", volumes[0]);
  }

  return TRUE;
}

gboolean
gst_oss4_mixer_slider_set_volume (GstOss4MixerSlider * s, const gint * volumes)
{
  GstMixerTrack *track = GST_MIXER_TRACK (s);
  int val = 0;

  /* if we're supposed to be muted, and are 'simulating' the mute because
   * we don't have a mute control, don't actually change the volume, just
   * save it as the new desired volume for later when we get unmuted again */
  if (!GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_NO_MUTE)) {
    if (GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_MUTE) && !s->mc->mute)
      goto done;
  }

  val = gst_oss4_mixer_slider_pack_volume (s, volumes);

  if (track->num_channels > 1) {
    GST_LOG_OBJECT (s, "left=%d, right=%d", volumes[0], volumes[1]);
  } else {
    GST_LOG_OBJECT (s, "mono=%d", volumes[0]);
  }

  if (!gst_oss4_mixer_set_control_val (s->mixer, s->mc, val))
    return FALSE;

done:

  s->volumes[0] = volumes[0];
  if (track->num_channels == 2)
    s->volumes[1] = volumes[1];

  return TRUE;
}

gboolean
gst_oss4_mixer_slider_set_record (GstOss4MixerSlider * s, gboolean record)
{
  /* There doesn't seem to be a way to do this using the OSS4 mixer API, so
   * just do nothing here for now. */
  return FALSE;
}

gboolean
gst_oss4_mixer_slider_set_mute (GstOss4MixerSlider * s, gboolean mute)
{
  GstMixerTrack *track = GST_MIXER_TRACK (s);
  gboolean ret;

  /* if the control does not support muting, then do not do anything */
  if (GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_NO_MUTE)) {
    return TRUE;
  }

  /* If we do not have a mute control, simulate mute (which is a bit broken,
   * since we can not differentiate between capture/playback volume etc., so
   * we just assume that setting the volume to 0 would be the same as muting
   * this control) */
  if (s->mc->mute == NULL) {
    int volume;

    if (mute) {
      /* make sure the current volume values get saved. */
      gst_oss4_mixer_slider_get_volume (s, s->volumes);
      volume = 0;
    } else {
      volume = gst_oss4_mixer_slider_pack_volume (s, s->volumes);
    }
    ret = gst_oss4_mixer_set_control_val (s->mixer, s->mc, volume);
  } else {
    ret = gst_oss4_mixer_set_control_val (s->mixer, s->mc->mute, ! !mute);
  }

  if (mute) {
    track->flags |= GST_MIXER_TRACK_MUTE;
  } else {
    track->flags &= ~GST_MIXER_TRACK_MUTE;
  }

  return ret;
}

GstMixerTrack *
gst_oss4_mixer_slider_new (GstOss4Mixer * mixer, GstOss4MixerControl * mc)
{
  GstOss4MixerSlider *s;
  GstMixerTrack *track;
  gint volumes[2] = { 0, };

  s = g_object_new (GST_TYPE_OSS4_MIXER_SLIDER, "untranslated-label",
      mc->mixext.extname, NULL);

  track = GST_MIXER_TRACK (s);

  /* caller will set track->label and track->flags */

  s->mc = mc;
  s->mixer = mixer;

  /* we don't do value scaling but just present a scale of 0-maxvalue */
  track->min_volume = 0;
  track->max_volume = mc->mixext.maxvalue;

  switch (mc->mixext.type) {
    case MIXT_MONOSLIDER:
    case MIXT_MONOSLIDER16:
    case MIXT_SLIDER:
      track->num_channels = 1;
      break;
    case MIXT_STEREOSLIDER:
    case MIXT_STEREOSLIDER16:
      track->num_channels = 2;
      break;
    default:
      g_return_val_if_reached (NULL);
  }

  GST_LOG_OBJECT (track, "min=%d, max=%d, channels=%d", track->min_volume,
      track->max_volume, track->num_channels);

  if (!gst_oss4_mixer_slider_get_volume (s, volumes)) {
    GST_WARNING_OBJECT (track, "failed to read volume, returning NULL");
    g_object_unref (track);
    track = NULL;
  }

  return track;
}

/* This is called from the watch thread */
void
gst_oss4_mixer_slider_process_change_unlocked (GstMixerTrack * track)
{
  GstOss4MixerSlider *s = GST_OSS4_MIXER_SLIDER_CAST (track);

  if (s->mc->mute != NULL && s->mc->mute->changed) {
    gst_mixer_mute_toggled (GST_MIXER (s->mixer), track,
        ! !s->mc->mute->last_val);
  } else {
    /* nothing to do here, since we don't/can't easily implement the record
     * flag */
  }

  if (s->mc->changed) {
    gint volumes[2] = { 0, 0 };

    gst_oss4_mixer_slider_unpack_volume (s, s->mc->last_val, volumes);

    /* if we 'simulate' the mute, update flag when the volume changes */
    if (s->mc->mute == NULL) {
      if (volumes[0] == 0 && volumes[1] == 0) {
        track->flags |= GST_MIXER_TRACK_MUTE;
      } else {
        track->flags &= ~GST_MIXER_TRACK_MUTE;
      }
    }

    gst_mixer_volume_changed (GST_MIXER (s->mixer), track, volumes);
  }
}
