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

static gboolean		gst_ossmixer_supported	   (GstInterface     *iface,
						    GType             iface_type);

static const GList *	gst_ossmixer_list_channels (GstMixer         *ossmixer);

static void		gst_ossmixer_set_volume	   (GstMixer         *ossmixer,
						    GstMixerChannel  *channel,
						    gint             *volumes);
static void		gst_ossmixer_get_volume	   (GstMixer         *ossmixer,
						    GstMixerChannel  *channel,
						    gint             *volumes);

static void		gst_ossmixer_set_record	   (GstMixer         *ossmixer,
						    GstMixerChannel  *channel,
						    gboolean          record);
static void		gst_ossmixer_set_mute	   (GstMixer         *ossmixer,
						    GstMixerChannel  *channel,
						    gboolean          mute);

static const gchar *labels[SOUND_MIXER_NRDEVICES] = SOUND_DEVICE_LABELS;

GstMixerChannel *
gst_ossmixer_channel_new (GstOssElement *oss,
			  gint channel_num,
			  gint max_chans,
			  gint flags)
{
  GstMixerChannel *channel = (GstMixerChannel *) g_new (GstOssMixerChannel, 1);
  gint volumes[2];

  channel->label = g_strdup (labels[channel_num]);
  channel->num_channels = max_chans;
  channel->flags = flags;
  channel->min_volume = 0;
  channel->max_volume = 100;
  ((GstOssMixerChannel *) channel)->channel_num = channel_num;

  /* volume */
  gst_ossmixer_get_volume (GST_MIXER (oss),
			   channel, volumes);
  if (max_chans == 1) {
    volumes[1] = 0;
  }
  ((GstOssMixerChannel *) channel)->lvol = volumes[0];
  ((GstOssMixerChannel *) channel)->rvol = volumes[1];

  return channel;
}

void
gst_ossmixer_channel_free (GstMixerChannel *channel)
{
  g_free (channel->label);
  g_free (channel);
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
  klass->list_channels = gst_ossmixer_list_channels;
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
gst_ossmixer_list_channels (GstMixer *mixer)
{
  GstOssElement *oss = GST_OSSELEMENT (mixer);

  g_return_val_if_fail (oss->mixer_fd != -1, NULL);

  return (const GList *) GST_OSSELEMENT (mixer)->channellist;
}

static void
gst_ossmixer_get_volume (GstMixer        *mixer,
			 GstMixerChannel *channel,
			 gint            *volumes)
{
  gint volume;
  GstOssElement *oss = GST_OSSELEMENT (mixer);
  GstOssMixerChannel *osschannel = (GstOssMixerChannel *) channel;

  g_return_if_fail (oss->mixer_fd != -1);

  if (channel->flags & GST_MIXER_CHANNEL_MUTE) {
    volumes[0] = osschannel->lvol;
    if (channel->num_channels == 2) {
      volumes[1] = osschannel->rvol;
    }
  } else {
    /* get */
    if (ioctl(oss->mixer_fd, MIXER_READ (osschannel->channel_num), &volume) < 0) {
      g_warning("Error getting recording device (%d) volume (0x%x): %s\n",
	        osschannel->channel_num, volume, strerror(errno));
      volume = 0;
    }

    osschannel->lvol = volumes[0] = (volume & 0xff);
    if (channel->num_channels == 2) {
      osschannel->rvol = volumes[1] = ((volume >> 8) & 0xff);
    }
  }
}

static void
gst_ossmixer_set_volume (GstMixer        *mixer,
			 GstMixerChannel *channel,
			 gint            *volumes)
{
  gint volume;
  GstOssElement *oss = GST_OSSELEMENT (mixer);
  GstOssMixerChannel *osschannel = (GstOssMixerChannel *) channel;

  g_return_if_fail (oss->mixer_fd != -1);

  /* prepare the value for ioctl() */
  if (!(channel->flags & GST_MIXER_CHANNEL_MUTE)) {
    volume = (volumes[0] & 0xff);
    if (channel->num_channels == 2) {
      volume |= ((volumes[1] & 0xff) << 8);
    }

    /* set */
    if (ioctl(oss->mixer_fd, MIXER_WRITE (osschannel->channel_num), &volume) < 0) {
      g_warning("Error setting recording device (%d) volume (0x%x): %s\n",
	        osschannel->channel_num, volume, strerror(errno));
      return;
    }
  }

  osschannel->lvol = volumes[0];
  if (channel->num_channels == 2) {
    osschannel->rvol = volumes[1];
  }
}

static void
gst_ossmixer_set_mute (GstMixer        *mixer,
		       GstMixerChannel *channel,
		       gboolean         mute)
{
  int volume;
  GstOssElement *oss = GST_OSSELEMENT (mixer);
  GstOssMixerChannel *osschannel = (GstOssMixerChannel *) channel;

  g_return_if_fail (oss->mixer_fd != -1);

  if (mute) {
    volume = 0;
  } else {
    volume = (osschannel->lvol & 0xff);
    if (MASK_BIT_IS_SET (oss->stereomask, osschannel->channel_num)) {
      volume |= ((osschannel->rvol & 0xff) << 8);
    }
  }

  if (ioctl(oss->mixer_fd, MIXER_WRITE(osschannel->channel_num), &volume) < 0) {
    g_warning("Error setting mixer recording device volume (0x%x): %s",
	      volume, strerror(errno));
    return;
  }

  if (mute) {
    channel->flags |= GST_MIXER_CHANNEL_MUTE;
  } else {
    channel->flags &= ~GST_MIXER_CHANNEL_MUTE;
  }
}

static void
gst_ossmixer_set_record (GstMixer        *mixer,
			 GstMixerChannel *channel,
			 gboolean         record)
{
  GstOssElement *oss = GST_OSSELEMENT (mixer);
  GstOssMixerChannel *osschannel = (GstOssMixerChannel *) channel;

  g_return_if_fail (oss->mixer_fd != -1);

  /* if we're exclusive, then we need to unset the current one(s) */
  if (oss->mixcaps & SOUND_CAP_EXCL_INPUT) {
    GList *channel;
    for (channel = oss->channellist; channel != NULL; channel = channel->next) {
      GstMixerChannel *turn = (GstMixerChannel *) channel->data;
      turn->flags &= ~GST_MIXER_CHANNEL_RECORD;
    }
    oss->recdevs = 0;
  }

  /* set new record bit, if needed */
  if (record) {
    oss->recdevs |= (1 << osschannel->channel_num);
  } else {
    oss->recdevs &= ~(1 << osschannel->channel_num);
  }

  /* set it to the device */
  if (ioctl(oss->mixer_fd, SOUND_MIXER_WRITE_RECSRC, &oss->recdevs) < 0) {
    g_warning("Error setting mixer recording devices (0x%x): %s",
	      oss->recdevs, strerror(errno));
    return;
  }

  if (record) {
    channel->flags |= GST_MIXER_CHANNEL_RECORD;
  } else {
    channel->flags &= ~GST_MIXER_CHANNEL_RECORD;
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

  /* build channel list */
  for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
    if (devmask & (1 << i)) {
      GstMixerChannel *channel;
      gboolean input = FALSE, stereo = FALSE, record = FALSE;

      /* channel exists, make up capabilities */
      if (MASK_BIT_IS_SET (oss->stereomask, i))
        stereo = TRUE;
      if (MASK_BIT_IS_SET (oss->recmask, i))
        input = TRUE;
      if (MASK_BIT_IS_SET (oss->recdevs, i))
        record = TRUE;

      /* add channel to list */
      channel = gst_ossmixer_channel_new (oss, i, stereo ? 2 : 1,
					  (record ? GST_MIXER_CHANNEL_RECORD : 0) |
					  (input ? GST_MIXER_CHANNEL_INPUT :
						   GST_MIXER_CHANNEL_OUTPUT));
      oss->channellist = g_list_append (oss->channellist, channel);
    }
  }
}

void
gst_ossmixer_free_list (GstOssElement *oss)
{
  g_return_if_fail (oss->mixer_fd != -1);

  g_list_foreach (oss->channellist, (GFunc) gst_ossmixer_channel_free, NULL);
  g_list_free (oss->channellist);
  oss->channellist = NULL;

  close (oss->mixer_fd);
  oss->mixer_fd = -1;
}
