/* G-Streamer generic V4L element - generic V4L calls handling
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <gst/gst.h>
#include <gst/tuner/tuner.h>
#include <gst/colorbalance/colorbalance.h>

#include "v4l_calls.h"
#include "gstv4ltuner.h"
#include "gstv4lcolorbalance.h"

#include "gstv4lsrc.h"
#include "gstv4lmjpegsrc.h"
#include "gstv4lmjpegsink.h"

#define DEBUG(format, args...) \
	GST_DEBUG_OBJECT (\
		GST_ELEMENT(v4lelement), \
		"V4L: " format, ##args)


static const char *picture_name[] = {
  "Hue",
  "Brightness",
  "Contrast",
  "Saturation",
  NULL
};

G_GNUC_UNUSED static const char *audio_name[] = {
  "Volume",
  "Mute",
  "Mode",
  NULL
};

static const char *norm_name[] = {
  "PAL",
  "NTSC",
  "SECAM",
  NULL
};

/******************************************************
 * gst_v4l_get_capabilities():
 *   get the device's capturing capabilities
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4l_get_capabilities (GstV4lElement *v4lelement)
{
  DEBUG("getting capabilities");
  GST_V4L_CHECK_OPEN(v4lelement);

  if (ioctl(v4lelement->video_fd, VIDIOCGCAP, &(v4lelement->vcap)) < 0)
  {
    gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
                       ("error getting capabilities %s of from device %s",
                        g_strerror (errno), v4lelement->videodev));
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4l_open():
 *   open the video device (v4lelement->videodev)
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_open (GstV4lElement *v4lelement)
{
  int num;

  DEBUG("opening device %s", v4lelement->videodev);
  GST_V4L_CHECK_NOT_OPEN(v4lelement);
  GST_V4L_CHECK_NOT_ACTIVE(v4lelement);

  /* be sure we have a device */
  if (!v4lelement->videodev) {
    gst_element_error (v4lelement, RESOURCE, NOT_FOUND,
		       (_("No device specified")), NULL);
    return FALSE;
  }

  /* open the device */
  v4lelement->video_fd = open(v4lelement->videodev, O_RDWR);
  if (!GST_V4L_IS_OPEN(v4lelement))
  {
    gst_element_error (v4lelement, RESOURCE, OPEN_READ_WRITE,
                       (_("Could not open device \"%s\" for reading and writing"), v4lelement->videodev),
                       GST_ERROR_SYSTEM);
    return FALSE;
  }

  /* get capabilities */
  if (!gst_v4l_get_capabilities(v4lelement))
  {
    close(v4lelement->video_fd);
    v4lelement->video_fd = -1;
    return FALSE;
  }

  /* device type check */
  if ((GST_IS_V4LSRC(v4lelement) &&
       !(v4lelement->vcap.type & VID_TYPE_CAPTURE)) ||
      (GST_IS_V4LMJPEGSRC(v4lelement) &&
       !(v4lelement->vcap.type & VID_TYPE_MJPEG_ENCODER)) ||
      (GST_IS_V4LMJPEGSINK(v4lelement) &&
       !(v4lelement->vcap.type & VID_TYPE_MJPEG_DECODER))) {
    gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
		      ("Device opened, but wrong type (0x%x)",
		      v4lelement->vcap.type));
    close(v4lelement->video_fd);
    v4lelement->video_fd = -1;
    return FALSE;
  }

  gst_info("Opened device \'%s\' (\'%s\') successfully\n",
    v4lelement->vcap.name, v4lelement->videodev);

  /* norms + inputs, for the tuner interface */
  for (num=0;norm_name[num]!=NULL;num++) {
    GstV4lTunerNorm *v4lnorm = g_object_new (GST_TYPE_V4L_TUNER_NORM,
					     NULL);
    GstTunerNorm *norm = GST_TUNER_NORM (v4lnorm);

    norm->label = g_strdup (norm_name[num]);
    norm->fps = (num == 1) ? (30000./1001) : 25.;
    v4lnorm->index = num;
    v4lelement->norms = g_list_append(v4lelement->norms,
				      (gpointer) norm);
  }
  v4lelement->channels = gst_v4l_get_chan_names(v4lelement);

  for (num=0;picture_name[num]!=NULL;num++)
  {
    GstV4lColorBalanceChannel *v4lchannel =
	g_object_new (GST_TYPE_V4L_COLOR_BALANCE_CHANNEL, NULL);
    GstColorBalanceChannel *channel = GST_COLOR_BALANCE_CHANNEL (v4lchannel);
    channel->label = g_strdup (picture_name[num]);
    channel->min_value = 0;
    channel->max_value = 65535;
    v4lchannel->index = num;
    v4lelement->colors = g_list_append(v4lelement->colors, channel);
  }

  DEBUG("Setting default norm/input");
  gst_v4l_set_chan_norm (v4lelement, 0, 0);

  return TRUE;
}


/******************************************************
 * gst_v4l_close():
 *   close the video device (v4lelement->video_fd)
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_close (GstV4lElement *v4lelement)
{
  DEBUG("closing device");
  GST_V4L_CHECK_OPEN(v4lelement);
  GST_V4L_CHECK_NOT_ACTIVE(v4lelement);

  close(v4lelement->video_fd);
  v4lelement->video_fd = -1;

  g_list_foreach (v4lelement->channels, (GFunc) g_object_unref, NULL);
  g_list_free (v4lelement->channels);
  v4lelement->channels = NULL;

  g_list_foreach (v4lelement->norms, (GFunc) g_object_unref, NULL);
  g_list_free (v4lelement->norms);
  v4lelement->norms = NULL;

  g_list_foreach (v4lelement->colors, (GFunc) g_object_unref, NULL);
  g_list_free (v4lelement->colors);
  v4lelement->colors = NULL;

  return TRUE;
}


/******************************************************
 * gst_v4l_get_num_chans()
 * return value: the numver of video input channels
 ******************************************************/

gint
gst_v4l_get_num_chans (GstV4lElement *v4lelement)
{
  DEBUG("getting number of channels");
  GST_V4L_CHECK_OPEN(v4lelement);

  return v4lelement->vcap.channels;
}


/******************************************************
 * gst_v4l_get_chan_names()
 * return value: a GList containing the channel names
 ******************************************************/

GList *
gst_v4l_get_chan_names (GstV4lElement *v4lelement)
{
  struct video_channel vchan;
  const GList *pads = gst_element_get_pad_list (GST_ELEMENT (v4lelement));
  GList *list = NULL;
  gint i;

  DEBUG("getting channel names");

  if (!GST_V4L_IS_OPEN(v4lelement))
    return NULL;

  /* sinks don't have inputs in v4l */
  if (pads && g_list_length ((GList *) pads) == 1)
    if (GST_PAD_DIRECTION (GST_PAD (pads->data)) == GST_PAD_SINK)
      return NULL;

  for (i=0;i<gst_v4l_get_num_chans(v4lelement);i++)
  {
    GstV4lTunerChannel *v4lchannel = g_object_new (GST_TYPE_V4L_TUNER_CHANNEL,
						   NULL);
    GstTunerChannel *channel = GST_TUNER_CHANNEL (v4lchannel);

    vchan.channel = i;
    if (ioctl(v4lelement->video_fd, VIDIOCGCHAN, &vchan) < 0)
      return NULL; /* memleak... */
    channel->label = g_strdup (vchan.name);
    channel->flags = GST_TUNER_CHANNEL_INPUT;
    v4lchannel->index = i;
    if (vchan.flags & VIDEO_VC_TUNER) {
      struct video_tuner vtun;
      gint n;

      for (n = 0; ; n++) {
        vtun.tuner = n;
        if (ioctl(v4lelement->video_fd, VIDIOCGTUNER, &vtun) < 0)
          break; /* no more tuners */
        if (!strcmp(vtun.name, vchan.name)) {
          v4lchannel->tuner = n;
          channel->flags |= GST_TUNER_CHANNEL_FREQUENCY;
          channel->min_frequency = vtun.rangelow;
          channel->max_frequency = vtun.rangehigh;
          channel->min_signal = 0;
          channel->max_signal = 0xffff;
          break;
        }
      }
    }
    if (vchan.flags & VIDEO_VC_AUDIO) {
      struct video_audio vaud;
      gint n;

      for (n = 0; n < v4lelement->vcap.audios; n++) {
        vaud.audio = n;
        if (ioctl(v4lelement->video_fd, VIDIOCGAUDIO, &vaud) < 0)
          continue;
        if (!strcmp(vaud.name, vchan.name)) {
          v4lchannel->audio = n;
          channel->flags |= GST_TUNER_CHANNEL_AUDIO;
          break;
        }
      }
    }
    list = g_list_append(list, (gpointer) channel);
  }

  return list;
}


/******************************************************
 * gst_v4l_get_chan_norm():
 *   get the currently active video-channel and it's
 *   norm (VIDEO_MODE_{PAL|NTSC|SECAM|AUTO})
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_get_chan_norm (GstV4lElement *v4lelement,
                       gint          *channel,
                       gint          *norm)
{
  DEBUG("getting current channel and norm");
  GST_V4L_CHECK_OPEN(v4lelement);

  if (channel)
    *channel = v4lelement->vchan.channel;
  if (norm)
    *norm = v4lelement->vchan.norm;

  return TRUE;
}


/******************************************************
 * gst_v4l_set_chan_norm():
 *   set a new active channel and it's norm
 *   (VIDEO_MODE_{PAL|NTSC|SECAM|AUTO})
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_set_chan_norm (GstV4lElement *v4lelement,
                       gint          channel,
                       gint          norm)
{
  DEBUG("setting channel = %d, norm = %d (%s)",
    channel, norm, norm_name[norm]);
  GST_V4L_CHECK_OPEN(v4lelement);
  GST_V4L_CHECK_NOT_ACTIVE(v4lelement);

  v4lelement->vchan.channel = channel;
  v4lelement->vchan.norm = norm;

  if (ioctl(v4lelement->video_fd, VIDIOCSCHAN, &(v4lelement->vchan)) < 0)
  {
    gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
      ("Error setting the channel/norm settings: %s",
      g_strerror(errno)));
    return FALSE;
  }

  if (ioctl(v4lelement->video_fd, VIDIOCGCHAN, &(v4lelement->vchan)) < 0)
  {
    gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
      ("Error getting the channel/norm settings: %s",
      g_strerror(errno)));
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4l_get_signal():
 *   get the current signal
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_get_signal (GstV4lElement *v4lelement,
                    gint           tunernum,
                    guint         *signal)
{
  struct video_tuner tuner;

  DEBUG("getting tuner signal");
  GST_V4L_CHECK_OPEN(v4lelement);

  tuner.tuner = tunernum;
  if (ioctl(v4lelement->video_fd, VIDIOCGTUNER, &tuner) < 0)
  {
    gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
      ("Error getting tuner signal: %s", g_strerror (errno)));
    return FALSE;
  }

  *signal = tuner.signal;

  return TRUE;
}


/******************************************************
 * gst_v4l_get_frequency():
 *   get the current frequency
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_get_frequency (GstV4lElement *v4lelement,
                       gint           tunernum,
                       gulong        *frequency)
{
  struct video_tuner vtun;

  DEBUG("getting tuner frequency");
  GST_V4L_CHECK_OPEN(v4lelement);

  /* check that this is the current input */
  vtun.tuner = tunernum;
  if (ioctl (v4lelement->video_fd, VIDIOCGTUNER, &vtun) < 0)
    return FALSE;
  if (strcmp(vtun.name, v4lelement->vchan.name))
    return FALSE;

  if (ioctl(v4lelement->video_fd, VIDIOCGFREQ, frequency) < 0)
  {
    gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
      ("Error getting tuner frequency: %s", g_strerror (errno)));
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4l_set_frequency():
 *   set frequency
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_set_frequency (GstV4lElement *v4lelement,
                       gint           tunernum,
                       gulong         frequency)
{
  struct video_tuner vtun;

  DEBUG("setting tuner frequency to %lu", frequency);
  GST_V4L_CHECK_OPEN(v4lelement);

  /* check that this is the current input */
  vtun.tuner = tunernum;
  if (ioctl (v4lelement->video_fd, VIDIOCGTUNER, &vtun) < 0)
    return FALSE;
  if (strcmp(vtun.name, v4lelement->vchan.name))
    return FALSE;

  if (ioctl(v4lelement->video_fd, VIDIOCSFREQ, &frequency) < 0)
  {
    gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
      ("Error setting tuner frequency: %s", g_strerror (errno)));
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4l_get_picture():
 *   get a picture value
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_get_picture (GstV4lElement     *v4lelement,
                     GstV4lPictureType type,
                     gint              *value)
{
  struct video_picture vpic;

  DEBUG("getting picture property type %d (%s)",
    type, picture_name[type]);
  GST_V4L_CHECK_OPEN(v4lelement);

  if (ioctl(v4lelement->video_fd, VIDIOCGPICT, &vpic) < 0)
  {
    gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
      ("Error getting picture parameters: %s", g_strerror (errno)));
    return FALSE;
  }

  switch (type)
  {
    case V4L_PICTURE_HUE:
      *value = vpic.hue;
      break;
    case V4L_PICTURE_BRIGHTNESS:
      *value = vpic.brightness;
      break;
    case V4L_PICTURE_CONTRAST:
      *value = vpic.contrast;
      break;
    case V4L_PICTURE_SATURATION:
      *value = vpic.colour;
      break;
    default:
      gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
        ("Error getting picture parameters: unknown type %d", type));
      return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4l_set_picture():
 *   set a picture value
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_set_picture (GstV4lElement     *v4lelement,
                     GstV4lPictureType type,
                     gint              value)
{
  struct video_picture vpic;

  DEBUG("setting picture type %d (%s) to value %d",
    type, picture_name[type], value);
  GST_V4L_CHECK_OPEN(v4lelement);

  if (ioctl(v4lelement->video_fd, VIDIOCGPICT, &vpic) < 0)
  {
    gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
      ("Error getting picture parameters: %s", g_strerror (errno)));
    return FALSE;
  }

  switch (type)
  {
    case V4L_PICTURE_HUE:
      vpic.hue = value;
      break;
    case V4L_PICTURE_BRIGHTNESS:
      vpic.brightness = value;
      break;
    case V4L_PICTURE_CONTRAST:
      vpic.contrast = value;
      break;
    case V4L_PICTURE_SATURATION:
      vpic.colour = value;
      break;
    default:
      gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
        ("Error setting picture parameters: unknown type %d", type));
      return FALSE;
  }

  if (ioctl(v4lelement->video_fd, VIDIOCSPICT, &vpic) < 0)
  {
    gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
      ("Error setting picture parameters: %s", g_strerror (errno)));
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4l_get_audio():
 *   get some audio value
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_get_audio (GstV4lElement   *v4lelement,
                   gint            audionum,
                   GstV4lAudioType type,
                   gint            *value)
{
  struct video_audio vau;

  DEBUG("getting audio parameter type %d (%s)",
    type, audio_name[type]);
  GST_V4L_CHECK_OPEN(v4lelement);

  vau.audio = audionum;
  if (ioctl(v4lelement->video_fd, VIDIOCGAUDIO, &vau) < 0)
  {
    gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
      ("Error getting audio parameters: %s", g_strerror (errno)));
    return FALSE;
  }

  switch (type)
  {
    case V4L_AUDIO_MUTE:
      *value = (vau.flags & VIDEO_AUDIO_MUTE);
      break;
    case V4L_AUDIO_VOLUME:
      *value = vau.volume;
      break;
    case V4L_AUDIO_MODE:
      *value = vau.mode;
      break;
    default:
    gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
        ("Error getting audio parameters: unknown type %d", type));
      return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4l_set_audio():
 *   set some audio value
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_set_audio (GstV4lElement   *v4lelement,
                   gint            audionum,
                   GstV4lAudioType type,
                   gint            value)
{
  struct video_audio vau;

  DEBUG("setting audio parameter type %d (%s) to value %d",
    type, audio_name[type], value);
  GST_V4L_CHECK_OPEN(v4lelement);

  vau.audio = audionum;
  if (ioctl(v4lelement->video_fd, VIDIOCGAUDIO, &vau) < 0)
  {
    gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
      ("Error getting audio parameters: %s", g_strerror (errno)));
    return FALSE;
  }

  switch (type)
  {
    case V4L_AUDIO_MUTE:
      if (!(vau.flags & VIDEO_AUDIO_MUTABLE))
      {
        gst_element_error (v4lelement, CORE, NOT_IMPLEMENTED, NULL,
          ("Error setting audio mute: (un)setting mute is not supported"));
        return FALSE;
      }
      if (value)
        vau.flags |= VIDEO_AUDIO_MUTE;
      else
        vau.flags &= ~VIDEO_AUDIO_MUTE;
      break;
    case V4L_AUDIO_VOLUME:
      if (!(vau.flags & VIDEO_AUDIO_VOLUME))
      {
        gst_element_error (v4lelement, CORE, NOT_IMPLEMENTED, NULL,
          ("Error setting audio volume: setting volume is not supported"));
        return FALSE;
      }
      vau.volume = value;
      break;
    case V4L_AUDIO_MODE:
      vau.mode = value;
      break;
    default:
      gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
        ("Error setting audio parameters: unknown type %d", type));
      return FALSE;
  }

  if (ioctl(v4lelement->video_fd, VIDIOCSAUDIO, &vau) < 0)
  {
    gst_element_error (v4lelement, RESOURCE, SETTINGS, NULL,
      ("Error setting audio parameters: %s", g_strerror (errno)));
    return FALSE;
  }

  return TRUE;
}
