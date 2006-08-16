/* GStreamer OSS Mixer implementation
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstossmixer.c: mixer interface implementation for OSS
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

#ifdef HAVE_OSS_INCLUDE_IN_SYS
# include <sys/soundcard.h>
#else
# ifdef HAVE_OSS_INCLUDE_IN_ROOT
#  include <soundcard.h>
# else
#  ifdef HAVE_OSS_INCLUDE_IN_MACHINE
#   include <machine/soundcard.h>
#  else
#   error "What to include?"
#  endif /* HAVE_OSS_INCLUDE_IN_MACHINE */
# endif /* HAVE_OSS_INCLUDE_IN_ROOT */
#endif /* HAVE_OSS_INCLUDE_IN_SYS */

#include <gst/gst-i18n-plugin.h>

#include "gstossmixer.h"
#include "gstossmixertrack.h"

GST_DEBUG_CATEGORY_EXTERN (oss_debug);
#define GST_CAT_DEFAULT oss_debug

#define MASK_BIT_IS_SET(mask, bit) \
  (mask & (1 << bit))

static gboolean
gst_ossmixer_open (GstOssMixer * mixer)
{
#ifdef SOUND_MIXER_INFO
  struct mixer_info minfo;
#endif

  g_return_val_if_fail (mixer->mixer_fd == -1, FALSE);

  mixer->mixer_fd = open (mixer->device, O_RDWR);
  if (mixer->mixer_fd == -1)
    goto open_failed;

  /* get masks */
  if (ioctl (mixer->mixer_fd, SOUND_MIXER_READ_RECMASK, &mixer->recmask) < 0
      || ioctl (mixer->mixer_fd, SOUND_MIXER_READ_RECSRC, &mixer->recdevs) < 0
      || ioctl (mixer->mixer_fd, SOUND_MIXER_READ_STEREODEVS,
          &mixer->stereomask) < 0
      || ioctl (mixer->mixer_fd, SOUND_MIXER_READ_DEVMASK, &mixer->devmask) < 0
      || ioctl (mixer->mixer_fd, SOUND_MIXER_READ_CAPS, &mixer->mixcaps) < 0)
    goto masks_failed;

  /* get name, not fatal */
  g_free (mixer->cardname);
#ifdef SOUND_MIXER_INFO
  if (ioctl (mixer->mixer_fd, SOUND_MIXER_INFO, &minfo) == 0) {
    mixer->cardname = g_strdup (minfo.name);
    GST_INFO ("Card name = %s", GST_STR_NULL (mixer->cardname));
  } else
#endif
  {
    mixer->cardname = g_strdup ("Unknown");
    GST_INFO ("Unknown card name");
  }
  GST_INFO ("Opened mixer for device %s", mixer->device);

  return TRUE;

  /* ERRORS */
open_failed:
  {
    /* this is valid. OSS devices don't need to expose a mixer */
    GST_DEBUG ("Failed to open mixer device %s, mixing disabled: %s",
        mixer->device, strerror (errno));
    return FALSE;
  }
masks_failed:
  {
    GST_DEBUG ("Failed to get device masks");
    close (mixer->mixer_fd);
    mixer->mixer_fd = -1;
    return FALSE;
  }
}

static void
gst_ossmixer_ensure_track_list (GstOssMixer * mixer)
{
  gint i, master = -1;

  g_return_if_fail (mixer->mixer_fd != -1);

  if (mixer->tracklist)
    return;

  /* find master volume */
  if (mixer->devmask & SOUND_MASK_VOLUME)
    master = SOUND_MIXER_VOLUME;
  else if (mixer->devmask & SOUND_MASK_PCM)
    master = SOUND_MIXER_PCM;
  else if (mixer->devmask & SOUND_MASK_SPEAKER)
    master = SOUND_MIXER_SPEAKER;       /* doubtful... */
  /* else: no master, so we won't set any */

  /* build track list */
  for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
    if (mixer->devmask & (1 << i)) {
      GstMixerTrack *track;
      gboolean input = FALSE, stereo = FALSE, record = FALSE;

      /* track exists, make up capabilities */
      if (MASK_BIT_IS_SET (mixer->stereomask, i))
        stereo = TRUE;
      if (MASK_BIT_IS_SET (mixer->recmask, i))
        input = TRUE;
      if (MASK_BIT_IS_SET (mixer->recdevs, i))
        record = TRUE;

      /* do we want this in our list? */
      if (!((mixer->dir & GST_OSS_MIXER_CAPTURE && input == TRUE) ||
              (mixer->dir & GST_OSS_MIXER_PLAYBACK && i != SOUND_MIXER_PCM)))
        /* the PLAYBACK case seems hacky, but that's how 0.8 had it */
        continue;

      /* add track to list */
      track = gst_ossmixer_track_new (mixer->mixer_fd, i, stereo ? 2 : 1,
          (record ? GST_MIXER_TRACK_RECORD : 0) |
          (input ? GST_MIXER_TRACK_INPUT :
              GST_MIXER_TRACK_OUTPUT) |
          ((master != i) ? 0 : GST_MIXER_TRACK_MASTER));
      mixer->tracklist = g_list_append (mixer->tracklist, track);
    }
  }
}

GstOssMixer *
gst_ossmixer_new (const char *device, GstOssMixerDirection dir)
{
  GstOssMixer *ret = NULL;

  g_return_val_if_fail (device != NULL, NULL);

  ret = g_new0 (GstOssMixer, 1);

  ret->device = g_strdup (device);
  ret->dir = dir;
  ret->mixer_fd = -1;

  if (!gst_ossmixer_open (ret))
    goto error;

  return ret;

  /* ERRORS */
error:
  {
    gst_ossmixer_free (ret);
    return NULL;
  }
}

void
gst_ossmixer_free (GstOssMixer * mixer)
{
  g_return_if_fail (mixer != NULL);

  if (mixer->device) {
    g_free (mixer->device);
    mixer->device = NULL;
  }

  if (mixer->cardname) {
    g_free (mixer->cardname);
    mixer->cardname = NULL;
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

/* unused with G_DISABLE_* */
static G_GNUC_UNUSED gboolean
gst_ossmixer_contains_track (GstOssMixer * mixer, GstOssMixerTrack * osstrack)
{
  const GList *item;

  for (item = mixer->tracklist; item != NULL; item = item->next)
    if (item->data == osstrack)
      return TRUE;

  return FALSE;
}

const GList *
gst_ossmixer_list_tracks (GstOssMixer * mixer)
{
  gst_ossmixer_ensure_track_list (mixer);

  return (const GList *) mixer->tracklist;
}

void
gst_ossmixer_get_volume (GstOssMixer * mixer,
    GstMixerTrack * track, gint * volumes)
{
  gint volume;
  GstOssMixerTrack *osstrack = GST_OSSMIXER_TRACK (track);

  g_return_if_fail (mixer->mixer_fd != -1);
  g_return_if_fail (gst_ossmixer_contains_track (mixer, osstrack));

  if (track->flags & GST_MIXER_TRACK_MUTE) {
    volumes[0] = osstrack->lvol;
    if (track->num_channels == 2) {
      volumes[1] = osstrack->rvol;
    }
  } else {
    /* get */
    if (ioctl (mixer->mixer_fd, MIXER_READ (osstrack->track_num), &volume) < 0) {
      g_warning ("Error getting recording device (%d) volume: %s",
          osstrack->track_num, strerror (errno));
      volume = 0;
    }

    osstrack->lvol = volumes[0] = (volume & 0xff);
    if (track->num_channels == 2) {
      osstrack->rvol = volumes[1] = ((volume >> 8) & 0xff);
    }
  }
}

void
gst_ossmixer_set_volume (GstOssMixer * mixer,
    GstMixerTrack * track, gint * volumes)
{
  gint volume;
  GstOssMixerTrack *osstrack = GST_OSSMIXER_TRACK (track);

  g_return_if_fail (mixer->mixer_fd != -1);
  g_return_if_fail (gst_ossmixer_contains_track (mixer, osstrack));

  /* prepare the value for ioctl() */
  if (!(track->flags & GST_MIXER_TRACK_MUTE)) {
    volume = (volumes[0] & 0xff);
    if (track->num_channels == 2) {
      volume |= ((volumes[1] & 0xff) << 8);
    }

    /* set */
    if (ioctl (mixer->mixer_fd, MIXER_WRITE (osstrack->track_num), &volume) < 0) {
      g_warning ("Error setting recording device (%d) volume (0x%x): %s",
          osstrack->track_num, volume, strerror (errno));
      return;
    }
  }

  osstrack->lvol = volumes[0];
  if (track->num_channels == 2) {
    osstrack->rvol = volumes[1];
  }
}

void
gst_ossmixer_set_mute (GstOssMixer * mixer, GstMixerTrack * track,
    gboolean mute)
{
  int volume;
  GstOssMixerTrack *osstrack = GST_OSSMIXER_TRACK (track);

  g_return_if_fail (mixer->mixer_fd != -1);
  g_return_if_fail (gst_ossmixer_contains_track (mixer, osstrack));

  if (mute) {
    volume = 0;
  } else {
    volume = (osstrack->lvol & 0xff);
    if (MASK_BIT_IS_SET (mixer->stereomask, osstrack->track_num)) {
      volume |= ((osstrack->rvol & 0xff) << 8);
    }
  }

  if (ioctl (mixer->mixer_fd, MIXER_WRITE (osstrack->track_num), &volume) < 0) {
    g_warning ("Error setting mixer recording device volume (0x%x): %s",
        volume, strerror (errno));
    return;
  }

  if (mute) {
    track->flags |= GST_MIXER_TRACK_MUTE;
  } else {
    track->flags &= ~GST_MIXER_TRACK_MUTE;
  }
}

void
gst_ossmixer_set_record (GstOssMixer * mixer,
    GstMixerTrack * track, gboolean record)
{
  GstOssMixerTrack *osstrack = GST_OSSMIXER_TRACK (track);

  g_return_if_fail (mixer->mixer_fd != -1);
  g_return_if_fail (gst_ossmixer_contains_track (mixer, osstrack));

  /* if there's nothing to do... */
  if ((record && GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_RECORD)) ||
      (!record && !GST_MIXER_TRACK_HAS_FLAG (track, GST_MIXER_TRACK_RECORD)))
    return;

  /* if we're exclusive, then we need to unset the current one(s) */
  if (mixer->mixcaps & SOUND_CAP_EXCL_INPUT) {
    GList *track;

    for (track = mixer->tracklist; track != NULL; track = track->next) {
      GstMixerTrack *turn = (GstMixerTrack *) track->data;

      turn->flags &= ~GST_MIXER_TRACK_RECORD;
    }
    mixer->recdevs = 0;
  }

  /* set new record bit, if needed */
  if (record) {
    mixer->recdevs |= (1 << osstrack->track_num);
  } else {
    mixer->recdevs &= ~(1 << osstrack->track_num);
  }

  /* set it to the device */
  if (ioctl (mixer->mixer_fd, SOUND_MIXER_WRITE_RECSRC, &mixer->recdevs) < 0) {
    g_warning ("Error setting mixer recording devices (0x%x): %s",
        mixer->recdevs, strerror (errno));
    return;
  }

  if (record) {
    track->flags |= GST_MIXER_TRACK_RECORD;
  } else {
    track->flags &= ~GST_MIXER_TRACK_RECORD;
  }
}
