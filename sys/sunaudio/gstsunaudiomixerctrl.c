/*
 * GStreamer - SunAudio mixer interface element
 * Copyright (C) 2005,2006,2008 Sun Microsystems, Inc.,
 *               Brian Cameron <brian.cameron@sun.com>
 * Copyright (C) 2008 Sun Microsystems, Inc.,
 *               Jan Schmidt <jan.schmidt@sun.com> 
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
#include <sys/mixer.h>

#include <gst/gst-i18n-plugin.h>

#include "gstsunaudiomixerctrl.h"
#include "gstsunaudiomixertrack.h"

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

  return TRUE;
}

void
gst_sunaudiomixer_ctrl_build_list (GstSunAudioMixerCtrl * mixer)
{
  GstMixerTrack *track;

  struct audio_info audioinfo;

  /*
   * Do not continue appending the same 3 static tracks onto the list
   */
  if (mixer->tracklist == NULL) {
    g_return_if_fail (mixer->mixer_fd != -1);

    /* Output & should be MASTER when it's the only one. */
    track = gst_sunaudiomixer_track_new (GST_SUNAUDIO_TRACK_OUTPUT,
        2, GST_MIXER_TRACK_OUTPUT | GST_MIXER_TRACK_MASTER);
    mixer->tracklist = g_list_append (mixer->tracklist, track);

    /* Input */
    track = gst_sunaudiomixer_track_new (GST_SUNAUDIO_TRACK_LINE_IN,
        2, GST_MIXER_TRACK_INPUT);

    /* Set whether we are recording from microphone or from line-in */
    if (ioctl (mixer->mixer_fd, AUDIO_GETINFO, &audioinfo) < 0) {
      g_warning ("Error getting audio device volume");
      return;
    }

    /* Set initial RECORD status */
    if (audioinfo.record.port == AUDIO_MICROPHONE) {
      mixer->recdevs |= (1 << GST_SUNAUDIO_TRACK_LINE_IN);
      track->flags |= GST_MIXER_TRACK_RECORD;
    } else {
      mixer->recdevs &= ~(1 << GST_SUNAUDIO_TRACK_LINE_IN);
      track->flags &= ~GST_MIXER_TRACK_RECORD;
    }

    /* Monitor */
    mixer->tracklist = g_list_append (mixer->tracklist, track);
    track = gst_sunaudiomixer_track_new (GST_SUNAUDIO_TRACK_MONITOR,
        2, GST_MIXER_TRACK_INPUT);
    mixer->tracklist = g_list_append (mixer->tracklist, track);
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

  GstSunAudioMixerTrack *sunaudiotrack = GST_SUNAUDIO_MIXER_TRACK (track);

  g_return_if_fail (mixer->mixer_fd != -1);

  if (ioctl (mixer->mixer_fd, AUDIO_GETINFO, &audioinfo) < 0) {
    g_warning ("Error getting audio device volume");
    return;
  }

  switch (sunaudiotrack->track_num) {
    case GST_SUNAUDIO_TRACK_OUTPUT:
      gain = (int) audioinfo.play.gain;
      balance = audioinfo.play.balance;
      break;
    case GST_SUNAUDIO_TRACK_LINE_IN:
      gain = (int) audioinfo.record.gain;
      balance = audioinfo.record.balance;
      break;
    case GST_SUNAUDIO_TRACK_MONITOR:
      gain = (int) audioinfo.monitor_gain;
      balance = audioinfo.record.balance;
      break;
  }

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

  /*
   * Reset whether we are recording from microphone or from line-in.
   * This can change if another program resets the value (such as
   * sdtaudiocontrol), so it is good to update the flag when we
   * get the volume.  The gnome-volume-control program calls this
   * function in a loop so the value will update properly when
   * changed.
   */
  if ((audioinfo.record.port == AUDIO_MICROPHONE &&
          !GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_RECORD)) ||
      (audioinfo.record.port == AUDIO_LINE_IN &&
          GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_RECORD))) {

    if (audioinfo.record.port == AUDIO_MICROPHONE) {
      mixer->recdevs |= (1 << GST_SUNAUDIO_TRACK_LINE_IN);
      track->flags |= GST_MIXER_TRACK_RECORD;
    } else {
      mixer->recdevs &= ~(1 << GST_SUNAUDIO_TRACK_LINE_IN);
      track->flags &= ~GST_MIXER_TRACK_RECORD;
    }
  }

  /* Likewise reset MUTE */
  if ((sunaudiotrack->track_num == GST_SUNAUDIO_TRACK_OUTPUT &&
          audioinfo.output_muted == 1) ||
      (sunaudiotrack->track_num != GST_SUNAUDIO_TRACK_OUTPUT && gain == 0)) {
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

  gchar buf[100];

  struct audio_info audioinfo;

  GstSunAudioMixerTrack *sunaudiotrack = GST_SUNAUDIO_MIXER_TRACK (track);

  gint temp[2];

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
    case GST_SUNAUDIO_TRACK_LINE_IN:
      audioinfo.record.gain = gain;
      audioinfo.record.balance = balance;
      break;
    case GST_SUNAUDIO_TRACK_MONITOR:
      audioinfo.monitor_gain = gain;
      audioinfo.record.balance = balance;
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

  GstSunAudioMixerTrack *sunaudiotrack = GST_SUNAUDIO_MIXER_TRACK (track);

  gint volume, balance;

  AUDIO_INITINFO (&audioinfo);

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
    case GST_SUNAUDIO_TRACK_LINE_IN:
      audioinfo.record.gain = volume;
      audioinfo.record.balance = balance;
      break;
    case GST_SUNAUDIO_TRACK_MONITOR:
      audioinfo.monitor_gain = volume;
      audioinfo.record.balance = balance;
      break;
  }

  g_return_if_fail (mixer->mixer_fd != -1);

  if (ioctl (mixer->mixer_fd, AUDIO_SETINFO, &audioinfo) < 0) {
    g_warning ("Error setting audio device volume");
    return;
  }
}

void
gst_sunaudiomixer_ctrl_set_record (GstSunAudioMixerCtrl * mixer,
    GstMixerTrack * track, gboolean record)
{
  GstSunAudioMixerTrack *sunaudiotrack = GST_SUNAUDIO_MIXER_TRACK (track);

  struct audio_info audioinfo;

  GList *trk;

  /* Don't change the setting */
  if ((record && GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_RECORD)) ||
      (!record && !GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_RECORD)))
    return;

  /*
   * So there is probably no need to look for others, but reset them all
   * to being off.
   */
  for (trk = mixer->tracklist; trk != NULL; trk = trk->next) {
    GstMixerTrack *turn = (GstMixerTrack *) trk->data;

    turn->flags &= ~GST_MIXER_TRACK_RECORD;
  }
  mixer->recdevs = 0;

  /* Set the port */
  AUDIO_INITINFO (&audioinfo);

  if (record) {
    audioinfo.record.port = AUDIO_MICROPHONE;
    mixer->recdevs |= (1 << sunaudiotrack->track_num);
    track->flags |= GST_MIXER_TRACK_RECORD;
  } else {
    audioinfo.record.port = AUDIO_LINE_IN;
    mixer->recdevs &= ~(1 << sunaudiotrack->track_num);
    track->flags &= ~GST_MIXER_TRACK_RECORD;
  }

  g_return_if_fail (mixer->mixer_fd != -1);

  if (ioctl (mixer->mixer_fd, AUDIO_SETINFO, &audioinfo) < 0) {
    g_warning ("Error setting audio device volume");
    return;
  }
}
