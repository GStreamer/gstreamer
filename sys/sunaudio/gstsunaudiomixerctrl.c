/*
 * GStreamer - SunAudio mixer interface element
 * Copyright (C) 2005,2006,2008,2009 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
 * Copyright (C) 2008 Sun Microsystems, Inc.,
 *               Jan Schmidt <jan.schmidt@sun.com> 
 * Copyright (C) 2009 Sun Microsystems, Inc.,
 *               Garrett D'Amore <garrett.damore@sun.com>
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
#include <sys/audio.h>
#include <sys/mixer.h>

#include <gst/gst-i18n-plugin.h>

#include "gstsunaudiomixerctrl.h"
#include "gstsunaudiomixertrack.h"
#include "gstsunaudiomixeroptions.h"

GST_DEBUG_CATEGORY_EXTERN (sunaudio_debug);
#define GST_CAT_DEFAULT sunaudio_debug

static gboolean
gst_sunaudiomixer_ctrl_open (GstSunAudioMixerCtrl * mixer)
{
  int fd;

  /* First try to open non-blocking */
  fd = open (mixer->device, O_RDWR | O_NONBLOCK);

  if (fd >= 0) {
    close (fd);
    fd = open (mixer->device, O_WRONLY);
  }

  if (fd == -1) {
    GST_DEBUG_OBJECT (mixer,
        "Failed to open mixer device %s, mixing disabled: %s", mixer->device,
        strerror (errno));

    return FALSE;
  }
  mixer->mixer_fd = fd;

  /* Try to set the multiple open flag if we can, but ignore errors */
  ioctl (mixer->mixer_fd, AUDIO_MIXER_MULTIPLE_OPEN);

  GST_DEBUG_OBJECT (mixer, "Opened mixer device %s", mixer->device);

  return TRUE;
}

void
gst_sunaudiomixer_ctrl_build_list (GstSunAudioMixerCtrl * mixer)
{
  GstMixerTrack *track;
  GstMixerOptions *options;

  struct audio_info audioinfo;

  /*
   * Do not continue appending the same 3 static tracks onto the list
   */
  if (mixer->tracklist == NULL) {
    g_return_if_fail (mixer->mixer_fd != -1);

    /* query available ports */
    if (ioctl (mixer->mixer_fd, AUDIO_GETINFO, &audioinfo) < 0) {
      g_warning ("Error getting audio device volume");
      return;
    }

    /* Output & should be MASTER when it's the only one. */
    track = gst_sunaudiomixer_track_new (GST_SUNAUDIO_TRACK_OUTPUT);
    mixer->tracklist = g_list_append (mixer->tracklist, track);

    /* Input */
    track = gst_sunaudiomixer_track_new (GST_SUNAUDIO_TRACK_RECORD);
    mixer->tracklist = g_list_append (mixer->tracklist, track);

    /* Monitor */
    track = gst_sunaudiomixer_track_new (GST_SUNAUDIO_TRACK_MONITOR);
    mixer->tracklist = g_list_append (mixer->tracklist, track);

    if (audioinfo.play.avail_ports & AUDIO_SPEAKER) {
      track = gst_sunaudiomixer_track_new (GST_SUNAUDIO_TRACK_SPEAKER);
      mixer->tracklist = g_list_append (mixer->tracklist, track);
    }
    if (audioinfo.play.avail_ports & AUDIO_HEADPHONE) {
      track = gst_sunaudiomixer_track_new (GST_SUNAUDIO_TRACK_HP);
      mixer->tracklist = g_list_append (mixer->tracklist, track);
    }
    if (audioinfo.play.avail_ports & AUDIO_LINE_OUT) {
      track = gst_sunaudiomixer_track_new (GST_SUNAUDIO_TRACK_LINEOUT);
      mixer->tracklist = g_list_append (mixer->tracklist, track);
    }
    if (audioinfo.play.avail_ports & AUDIO_SPDIF_OUT) {
      track = gst_sunaudiomixer_track_new (GST_SUNAUDIO_TRACK_SPDIFOUT);
      mixer->tracklist = g_list_append (mixer->tracklist, track);
    }
    if (audioinfo.play.avail_ports & AUDIO_AUX1_OUT) {
      track = gst_sunaudiomixer_track_new (GST_SUNAUDIO_TRACK_AUX1OUT);
      mixer->tracklist = g_list_append (mixer->tracklist, track);
    }
    if (audioinfo.play.avail_ports & AUDIO_AUX2_OUT) {
      track = gst_sunaudiomixer_track_new (GST_SUNAUDIO_TRACK_AUX2OUT);
      mixer->tracklist = g_list_append (mixer->tracklist, track);
    }

    if (audioinfo.record.avail_ports != AUDIO_NONE) {
      options =
          gst_sunaudiomixer_options_new (mixer, GST_SUNAUDIO_TRACK_RECSRC);
      mixer->tracklist = g_list_append (mixer->tracklist, options);
    }
  }
}

GstSunAudioMixerCtrl *
gst_sunaudiomixer_ctrl_new (const char *device)
{
  GstSunAudioMixerCtrl *ret = NULL;

  g_return_val_if_fail (device != NULL, NULL);

  ret = g_new0 (GstSunAudioMixerCtrl, 1);

  ret->device = g_strdup (device);
  ret->mixer_fd = -1;
  ret->tracklist = NULL;

  if (!gst_sunaudiomixer_ctrl_open (ret))
    goto error;

  return ret;

error:
  if (ret)
    gst_sunaudiomixer_ctrl_free (ret);

  return NULL;
}

void
gst_sunaudiomixer_ctrl_free (GstSunAudioMixerCtrl * mixer)
{
  g_return_if_fail (mixer != NULL);

  if (mixer->device) {
    g_free (mixer->device);
    mixer->device = NULL;
  }

  if (mixer->tracklist) {
    g_list_foreach (mixer->tracklist, (GFunc) g_object_unref, NULL);
    g_list_free (mixer->tracklist);
    mixer->tracklist = NULL;
  }

  if (mixer->mixer_fd != -1) {
    close (mixer->mixer_fd);
    mixer->mixer_fd = -1;
  }

  g_free (mixer);
}

GstMixerFlags
gst_sunaudiomixer_ctrl_get_mixer_flags (GstSunAudioMixerCtrl * mixer)
{
  return GST_MIXER_FLAG_HAS_WHITELIST | GST_MIXER_FLAG_GROUPING;
}

const GList *
gst_sunaudiomixer_ctrl_list_tracks (GstSunAudioMixerCtrl * mixer)
{
  gst_sunaudiomixer_ctrl_build_list (mixer);

  return (const GList *) mixer->tracklist;
}

void
gst_sunaudiomixer_ctrl_get_volume (GstSunAudioMixerCtrl * mixer,
    GstMixerTrack * track, gint * volumes)
{
  gint gain, balance;
  float ratio;
  struct audio_info audioinfo;
  GstSunAudioMixerTrack *sunaudiotrack;

  g_return_if_fail (GST_IS_SUNAUDIO_MIXER_TRACK (track));
  sunaudiotrack = GST_SUNAUDIO_MIXER_TRACK (track);

  g_return_if_fail (mixer->mixer_fd != -1);

  if (ioctl (mixer->mixer_fd, AUDIO_GETINFO, &audioinfo) < 0) {
    g_warning ("Error getting audio device volume");
    return;
  }

  balance = AUDIO_MID_BALANCE;
  gain = 0;

  switch (sunaudiotrack->track_num) {
    case GST_SUNAUDIO_TRACK_OUTPUT:
      gain = (int) audioinfo.play.gain;
      balance = audioinfo.play.balance;
      break;
    case GST_SUNAUDIO_TRACK_RECORD:
      gain = (int) audioinfo.record.gain;
      balance = audioinfo.record.balance;
      break;
    case GST_SUNAUDIO_TRACK_MONITOR:
      gain = (int) audioinfo.monitor_gain;
      balance = audioinfo.record.balance;
      break;
    case GST_SUNAUDIO_TRACK_SPEAKER:
      if (audioinfo.play.port & AUDIO_SPEAKER)
        gain = AUDIO_MAX_GAIN;
      break;
    case GST_SUNAUDIO_TRACK_HP:
      if (audioinfo.play.port & AUDIO_HEADPHONE)
        gain = AUDIO_MAX_GAIN;
      break;
    case GST_SUNAUDIO_TRACK_LINEOUT:
      if (audioinfo.play.port & AUDIO_LINE_OUT)
        gain = AUDIO_MAX_GAIN;
      break;
    case GST_SUNAUDIO_TRACK_SPDIFOUT:
      if (audioinfo.play.port & AUDIO_SPDIF_OUT)
        gain = AUDIO_MAX_GAIN;
      break;
    case GST_SUNAUDIO_TRACK_AUX1OUT:
      if (audioinfo.play.port & AUDIO_AUX1_OUT)
        gain = AUDIO_MAX_GAIN;
      break;
    case GST_SUNAUDIO_TRACK_AUX2OUT:
      if (audioinfo.play.port & AUDIO_AUX2_OUT)
        gain = AUDIO_MAX_GAIN;
      break;
    default:
      break;
  }

  switch (track->num_channels) {
    case 2:
      if (balance == AUDIO_MID_BALANCE) {
        volumes[0] = gain;
        volumes[1] = gain;
      } else if (balance < AUDIO_MID_BALANCE) {
        volumes[0] = gain;
        ratio = 1 - (float) (AUDIO_MID_BALANCE - balance) /
            (float) AUDIO_MID_BALANCE;
        volumes[1] = (int) ((float) gain * ratio + 0.5);
      } else {
        volumes[1] = gain;
        ratio = 1 - (float) (balance - AUDIO_MID_BALANCE) /
            (float) AUDIO_MID_BALANCE;
        volumes[0] = (int) ((float) gain * ratio + 0.5);
      }
      break;
    case 1:
      volumes[0] = gain;
      break;
  }

  /* Likewise reset MUTE */
  if ((sunaudiotrack->track_num == GST_SUNAUDIO_TRACK_OUTPUT
          && audioinfo.output_muted == 1)
      || (sunaudiotrack->track_num != GST_SUNAUDIO_TRACK_OUTPUT && gain == 0)) {
    /*
     * If MUTE is set, then gain is always 0, so don't bother
     * resetting our internal value.
     */
    track->flags |= GST_MIXER_TRACK_MUTE;
  } else {
    sunaudiotrack->gain = gain;
    sunaudiotrack->balance = balance;
    track->flags &= ~GST_MIXER_TRACK_MUTE;
  }
}

void
gst_sunaudiomixer_ctrl_set_volume (GstSunAudioMixerCtrl * mixer,
    GstMixerTrack * track, gint * volumes)
{
  gint gain;
  gint balance;
  gint l_real_gain;
  gint r_real_gain;
  float ratio;
  struct audio_info audioinfo;
  GstSunAudioMixerTrack *sunaudiotrack = GST_SUNAUDIO_MIXER_TRACK (track);

  l_real_gain = volumes[0];
  r_real_gain = volumes[1];

  if (l_real_gain == r_real_gain) {
    gain = l_real_gain;
    balance = AUDIO_MID_BALANCE;
  } else if (l_real_gain < r_real_gain) {
    gain = r_real_gain;
    ratio = (float) l_real_gain / (float) r_real_gain;
    balance =
        AUDIO_RIGHT_BALANCE - (int) (ratio * (float) AUDIO_MID_BALANCE + 0.5);
  } else {
    gain = l_real_gain;
    ratio = (float) r_real_gain / (float) l_real_gain;
    balance =
        AUDIO_LEFT_BALANCE + (int) (ratio * (float) AUDIO_MID_BALANCE + 0.5);
  }

  sunaudiotrack->gain = gain;
  sunaudiotrack->balance = balance;

  if (GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_MUTE)) {
    if (sunaudiotrack->track_num == GST_SUNAUDIO_TRACK_OUTPUT) {
      return;
    } else if (gain == 0) {
      return;
    } else {
      /*
       * If the volume is set to a non-zero value for LINE_IN
       * or MONITOR, then unset MUTE.
       */
      track->flags &= ~GST_MIXER_TRACK_MUTE;
    }
  }

  /* Set the volume */
  AUDIO_INITINFO (&audioinfo);

  switch (sunaudiotrack->track_num) {
    case GST_SUNAUDIO_TRACK_OUTPUT:
      audioinfo.play.gain = gain;
      audioinfo.play.balance = balance;
      break;
    case GST_SUNAUDIO_TRACK_RECORD:
      audioinfo.record.gain = gain;
      audioinfo.record.balance = balance;
      break;
    case GST_SUNAUDIO_TRACK_MONITOR:
      audioinfo.monitor_gain = gain;
      audioinfo.record.balance = balance;
      break;
    default:
      break;
  }

  g_return_if_fail (mixer->mixer_fd != -1);

  if (ioctl (mixer->mixer_fd, AUDIO_SETINFO, &audioinfo) < 0) {
    g_warning ("Error setting audio device volume");
    return;
  }
}

void
gst_sunaudiomixer_ctrl_set_mute (GstSunAudioMixerCtrl * mixer,
    GstMixerTrack * track, gboolean mute)
{
  struct audio_info audioinfo;
  struct audio_info oldinfo;
  GstSunAudioMixerTrack *sunaudiotrack = GST_SUNAUDIO_MIXER_TRACK (track);
  gint volume, balance;

  AUDIO_INITINFO (&audioinfo);

  if (ioctl (mixer->mixer_fd, AUDIO_GETINFO, &oldinfo) < 0) {
    g_warning ("Error getting audio device volume");
    return;
  }

  if (mute) {
    volume = 0;
    track->flags |= GST_MIXER_TRACK_MUTE;
  } else {
    volume = sunaudiotrack->gain;
    track->flags &= ~GST_MIXER_TRACK_MUTE;
  }

  balance = sunaudiotrack->balance;

  switch (sunaudiotrack->track_num) {
    case GST_SUNAUDIO_TRACK_OUTPUT:
      if (mute)
        audioinfo.output_muted = 1;
      else
        audioinfo.output_muted = 0;

      audioinfo.play.gain = volume;
      audioinfo.play.balance = balance;
      break;
    case GST_SUNAUDIO_TRACK_RECORD:
      audioinfo.record.gain = volume;
      audioinfo.record.balance = balance;
      break;
    case GST_SUNAUDIO_TRACK_MONITOR:
      audioinfo.monitor_gain = volume;
      audioinfo.record.balance = balance;
      break;
    case GST_SUNAUDIO_TRACK_SPEAKER:
      if (mute) {
        audioinfo.play.port = oldinfo.play.port & ~AUDIO_SPEAKER;
      } else {
        audioinfo.play.port = oldinfo.play.port | AUDIO_SPEAKER;
      }
      break;
    case GST_SUNAUDIO_TRACK_HP:
      if (mute) {
        audioinfo.play.port = oldinfo.play.port & ~AUDIO_HEADPHONE;
      } else {
        audioinfo.play.port = oldinfo.play.port | AUDIO_HEADPHONE;
      }
      break;
    case GST_SUNAUDIO_TRACK_LINEOUT:
      if (mute) {
        audioinfo.play.port = oldinfo.play.port & ~AUDIO_LINE_OUT;
      } else {
        audioinfo.play.port = oldinfo.play.port | AUDIO_LINE_OUT;
      }
      break;
    case GST_SUNAUDIO_TRACK_SPDIFOUT:
      if (mute) {
        audioinfo.play.port = oldinfo.play.port & ~AUDIO_SPDIF_OUT;
      } else {
        audioinfo.play.port = oldinfo.play.port | AUDIO_SPDIF_OUT;
      }
      break;
    case GST_SUNAUDIO_TRACK_AUX1OUT:
      if (mute) {
        audioinfo.play.port = oldinfo.play.port & ~AUDIO_AUX1_OUT;
      } else {
        audioinfo.play.port = oldinfo.play.port | AUDIO_AUX1_OUT;
      }
      break;
    case GST_SUNAUDIO_TRACK_AUX2OUT:
      if (mute) {
        audioinfo.play.port = oldinfo.play.port & ~AUDIO_AUX2_OUT;
      } else {
        audioinfo.play.port = oldinfo.play.port | AUDIO_AUX2_OUT;
      }
      break;
    default:
      break;
  }

  if (audioinfo.play.port != ((unsigned) ~0)) {
    /* mask off ports we can't modify. Hack for broken drivers where mod_ports == 0 */
    if (oldinfo.play.mod_ports != 0) {
      audioinfo.play.port &= oldinfo.play.mod_ports;
      /* and add in any that are forced to be on */
      audioinfo.play.port |= (oldinfo.play.port & ~oldinfo.play.mod_ports);
    }
  }
  g_return_if_fail (mixer->mixer_fd != -1);

  if (audioinfo.play.port != (guint) (-1) &&
      audioinfo.play.port != oldinfo.play.port)
    GST_LOG_OBJECT (mixer, "Changing play port mask to 0x%08x",
        audioinfo.play.port);

  if (ioctl (mixer->mixer_fd, AUDIO_SETINFO, &audioinfo) < 0) {
    g_warning ("Error setting audio settings");
    return;
  }
}

void
gst_sunaudiomixer_ctrl_set_record (GstSunAudioMixerCtrl * mixer,
    GstMixerTrack * track, gboolean record)
{
}

void
gst_sunaudiomixer_ctrl_set_option (GstSunAudioMixerCtrl * mixer,
    GstMixerOptions * options, gchar * value)
{
  struct audio_info audioinfo;
  GstMixerTrack *track;
  GstSunAudioMixerOptions *opts;
  GQuark q;
  int i;

  g_return_if_fail (mixer != NULL);
  g_return_if_fail (mixer->mixer_fd != -1);
  g_return_if_fail (value != NULL);
  g_return_if_fail (GST_IS_SUNAUDIO_MIXER_OPTIONS (options));

  track = GST_MIXER_TRACK (options);
  opts = GST_SUNAUDIO_MIXER_OPTIONS (options);

  if (opts->track_num != GST_SUNAUDIO_TRACK_RECSRC) {
    g_warning ("set_option not supported on track %s", track->label);
    return;
  }

  q = g_quark_try_string (value);
  if (q == 0) {
    g_warning ("unknown option '%s'", value);
    return;
  }

  for (i = 0; i < 8; i++) {
    if (opts->names[i] == q) {
      break;
    }
  }

  if (((1 << (i)) & opts->avail) == 0) {
    g_warning ("Record port %s not available", g_quark_to_string (q));
    return;
  }

  AUDIO_INITINFO (&audioinfo);
  audioinfo.record.port = (1 << (i));

  if (ioctl (mixer->mixer_fd, AUDIO_SETINFO, &audioinfo) < 0) {
    g_warning ("Error setting audio record port");
  }
}

const gchar *
gst_sunaudiomixer_ctrl_get_option (GstSunAudioMixerCtrl * mixer,
    GstMixerOptions * options)
{
  GstMixerTrack *track;
  GstSunAudioMixerOptions *opts;
  struct audio_info audioinfo;
  int i;

  g_return_val_if_fail (mixer != NULL, NULL);
  g_return_val_if_fail (mixer->fd != -1, NULL);
  g_return_val_if_fail (GST_IS_SUNAUDIO_MIXER_OPTIONS (options), NULL);

  track = GST_MIXER_TRACK (options);
  opts = GST_SUNAUDIO_MIXER_OPTIONS (options);

  g_return_val_if_fail (opts->track_num == GST_SUNAUDIO_TRACK_RECSRC, NULL);

  if (ioctl (mixer->mixer_fd, AUDIO_GETINFO, &audioinfo) < 0) {
    g_warning ("Error getting audio device settings");
    return (NULL);
  }

  for (i = 0; i < 8; i++) {
    if ((1 << i) == audioinfo.record.port) {
      const gchar *s = g_quark_to_string (opts->names[i]);
      GST_DEBUG_OBJECT (mixer, "Getting value for option %d: %s",
          opts->track_num, s);
      return (s);
    }
  }

  GST_DEBUG_OBJECT (mixer, "Unable to get value for option %d",
      opts->track_num);

  g_warning ("Record port value %d seems illegal", audioinfo.record.port);
  return (NULL);
}
