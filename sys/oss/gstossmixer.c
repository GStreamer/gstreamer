/* GStreamer OSS Mixer implementation
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstossmixer.h: mixer interface implementation for OSS
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
#include <sys/soundcard.h>

#include "gstossmixer.h"

#define MASK_BIT_IS_SET(mask, bit) \
  (mask & (1 << bit))

static gboolean		gst_ossmixer_supported	   (GstInterface   *iface,
						    GType           iface_type);

static const GList *	gst_ossmixer_list_tracks   (GstMixer       *ossmixer);

static void		gst_ossmixer_set_volume	   (GstMixer       *ossmixer,
						    GstMixerTrack  *track,
						    gint           *volumes);
static void		gst_ossmixer_get_volume	   (GstMixer       *ossmixer,
						    GstMixerTrack  *track,
						    gint           *volumes);

static void		gst_ossmixer_set_record	   (GstMixer       *ossmixer,
						    GstMixerTrack  *track,
						    gboolean        record);
static void		gst_ossmixer_set_mute	   (GstMixer       *ossmixer,
						    GstMixerTrack  *track,
						    gboolean        mute);

static const gchar *labels[SOUND_MIXER_NRDEVICES] = SOUND_DEVICE_LABELS;

GstMixerTrack *
gst_ossmixer_track_new (GstOssElement *oss,
                        gint track_num,
                        gint max_chans,
                        gint flags)
{
  GstMixerTrack *track = (GstMixerTrack *) g_new (GstOssMixerTrack, 1);
  gint volumes[2];

  track->label = g_strdup (labels[track_num]);
  track->num_channels = max_chans;
  track->flags = flags;
  track->min_volume = 0;
  track->max_volume = 100;
  ((GstOssMixerTrack *) track)->track_num = track_num;

  /* volume */
  gst_ossmixer_get_volume (GST_MIXER (oss), track, volumes);
  if (max_chans == 1) {
    volumes[1] = 0;
  }
  ((GstOssMixerTrack *) track)->lvol = volumes[0];
  ((GstOssMixerTrack *) track)->rvol = volumes[1];

  return track;
}

void
gst_ossmixer_track_free (GstMixerTrack *track)
{
  g_free (track->label);
  g_free (track);
}

void
gst_oss_interface_init (GstInterfaceClass *klass)
{
  /* default virtual functions */
  klass->supported = gst_ossmixer_supported;
}

void
gst_ossmixer_interface_init (GstMixerClass *klass)
{
  /* default virtual functions */
  klass->list_tracks = gst_ossmixer_list_tracks;
  klass->set_volume = gst_ossmixer_set_volume;
  klass->get_volume = gst_ossmixer_get_volume;
  klass->set_mute = gst_ossmixer_set_mute;
  klass->set_record = gst_ossmixer_set_record;
}

static gboolean
gst_ossmixer_supported (GstInterface *iface,
			GType         iface_type)
{
  g_assert (iface_type == GST_TYPE_MIXER);

  return (GST_OSSELEMENT (iface)->mixer_fd != -1);
}

static const GList *
gst_ossmixer_list_tracks (GstMixer *mixer)
{
  GstOssElement *oss = GST_OSSELEMENT (mixer);

  g_return_val_if_fail (oss->mixer_fd != -1, NULL);

  return (const GList *) GST_OSSELEMENT (mixer)->tracklist;
}

static void
gst_ossmixer_get_volume (GstMixer      *mixer,
			 GstMixerTrack *track,
			 gint          *volumes)
{
  gint volume;
  GstOssElement *oss = GST_OSSELEMENT (mixer);
  GstOssMixerTrack *osstrack = (GstOssMixerTrack *) track;

  g_return_if_fail (oss->mixer_fd != -1);

  if (track->flags & GST_MIXER_TRACK_MUTE) {
    volumes[0] = osstrack->lvol;
    if (track->num_channels == 2) {
      volumes[1] = osstrack->rvol;
    }
  } else {
    /* get */
    if (ioctl(oss->mixer_fd, MIXER_READ (osstrack->track_num), &volume) < 0) {
      g_warning("Error getting recording device (%d) volume (0x%x): %s\n",
	        osstrack->track_num, volume, strerror(errno));
      volume = 0;
    }

    osstrack->lvol = volumes[0] = (volume & 0xff);
    if (track->num_channels == 2) {
      osstrack->rvol = volumes[1] = ((volume >> 8) & 0xff);
    }
  }
}

static void
gst_ossmixer_set_volume (GstMixer      *mixer,
			 GstMixerTrack *track,
			 gint          *volumes)
{
  gint volume;
  GstOssElement *oss = GST_OSSELEMENT (mixer);
  GstOssMixerTrack *osstrack = (GstOssMixerTrack *) track;

  g_return_if_fail (oss->mixer_fd != -1);

  /* prepare the value for ioctl() */
  if (!(track->flags & GST_MIXER_TRACK_MUTE)) {
    volume = (volumes[0] & 0xff);
    if (track->num_channels == 2) {
      volume |= ((volumes[1] & 0xff) << 8);
    }

    /* set */
    if (ioctl(oss->mixer_fd, MIXER_WRITE (osstrack->track_num), &volume) < 0) {
      g_warning("Error setting recording device (%d) volume (0x%x): %s\n",
	        osstrack->track_num, volume, strerror(errno));
      return;
    }
  }

  osstrack->lvol = volumes[0];
  if (track->num_channels == 2) {
    osstrack->rvol = volumes[1];
  }
}

static void
gst_ossmixer_set_mute (GstMixer      *mixer,
		       GstMixerTrack *track,
		       gboolean       mute)
{
  int volume;
  GstOssElement *oss = GST_OSSELEMENT (mixer);
  GstOssMixerTrack *osstrack = (GstOssMixerTrack *) track;

  g_return_if_fail (oss->mixer_fd != -1);

  if (mute) {
    volume = 0;
  } else {
    volume = (osstrack->lvol & 0xff);
    if (MASK_BIT_IS_SET (oss->stereomask, osstrack->track_num)) {
      volume |= ((osstrack->rvol & 0xff) << 8);
    }
  }

  if (ioctl(oss->mixer_fd, MIXER_WRITE(osstrack->track_num), &volume) < 0) {
    g_warning("Error setting mixer recording device volume (0x%x): %s",
	      volume, strerror(errno));
    return;
  }

  if (mute) {
    track->flags |= GST_MIXER_TRACK_MUTE;
  } else {
    track->flags &= ~GST_MIXER_TRACK_MUTE;
  }
}

static void
gst_ossmixer_set_record (GstMixer      *mixer,
			 GstMixerTrack *track,
			 gboolean       record)
{
  GstOssElement *oss = GST_OSSELEMENT (mixer);
  GstOssMixerTrack *osstrack = (GstOssMixerTrack *) track;

  g_return_if_fail (oss->mixer_fd != -1);

  /* if we're exclusive, then we need to unset the current one(s) */
  if (oss->mixcaps & SOUND_CAP_EXCL_INPUT) {
    GList *track;
    for (track = oss->tracklist; track != NULL; track = track->next) {
      GstMixerTrack *turn = (GstMixerTrack *) track->data;
      turn->flags &= ~GST_MIXER_TRACK_RECORD;
    }
    oss->recdevs = 0;
  }

  /* set new record bit, if needed */
  if (record) {
    oss->recdevs |= (1 << osstrack->track_num);
  } else {
    oss->recdevs &= ~(1 << osstrack->track_num);
  }

  /* set it to the device */
  if (ioctl(oss->mixer_fd, SOUND_MIXER_WRITE_RECSRC, &oss->recdevs) < 0) {
    g_warning("Error setting mixer recording devices (0x%x): %s",
	      oss->recdevs, strerror(errno));
    return;
  }

  if (record) {
    track->flags |= GST_MIXER_TRACK_RECORD;
  } else {
    track->flags &= ~GST_MIXER_TRACK_RECORD;
  }
}

void
gst_ossmixer_build_list (GstOssElement *oss)
{
  gint i, devmask;

  g_return_if_fail (oss->mixer_fd == -1);

  oss->mixer_fd = open (oss->mixer_dev, O_RDWR);
  if (oss->mixer_fd == -1) {
    g_warning ("Failed to open mixer device %s, mixing disabled: %s",
	       oss->mixer_dev, strerror (errno));
    return;
  }

  /* get masks */
  ioctl (oss->mixer_fd, SOUND_MIXER_READ_RECMASK, &oss->recmask);
  ioctl (oss->mixer_fd, SOUND_MIXER_READ_RECSRC, &oss->recdevs);
  ioctl (oss->mixer_fd, SOUND_MIXER_READ_STEREODEVS, &oss->stereomask);
  ioctl (oss->mixer_fd, SOUND_MIXER_READ_DEVMASK, &devmask);
  ioctl (oss->mixer_fd, SOUND_MIXER_READ_CAPS, &oss->mixcaps);

  /* build track list */
  for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
    if (devmask & (1 << i)) {
      GstMixerTrack *track;
      gboolean input = FALSE, stereo = FALSE, record = FALSE;

      /* track exists, make up capabilities */
      if (MASK_BIT_IS_SET (oss->stereomask, i))
        stereo = TRUE;
      if (MASK_BIT_IS_SET (oss->recmask, i))
        input = TRUE;
      if (MASK_BIT_IS_SET (oss->recdevs, i))
        record = TRUE;

      /* add track to list */
      track = gst_ossmixer_track_new (oss, i, stereo ? 2 : 1,
                                        (record ? GST_MIXER_TRACK_RECORD : 0) |
                                        (input ? GST_MIXER_TRACK_INPUT :
                                                 GST_MIXER_TRACK_OUTPUT));
      oss->tracklist = g_list_append (oss->tracklist, track);
    }
  }
}

void
gst_ossmixer_free_list (GstOssElement *oss)
{
  g_return_if_fail (oss->mixer_fd != -1);

  g_list_foreach (oss->tracklist, (GFunc) gst_ossmixer_track_free, NULL);
  g_list_free (oss->tracklist);
  oss->tracklist = NULL;

  close (oss->mixer_fd);
  oss->mixer_fd = -1;
}
