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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "v4l_calls.h"

#define DEBUG(format, args...) \
	GST_DEBUG_ELEMENT(GST_CAT_PLUGIN_INFO, \
		GST_ELEMENT(v4lelement), \
		"V4L: " format, ##args)


const char *picture_name[] = {
  "Hue",
  "Brightness",
  "Contrast",
  "Saturation",
  NULL
};

const char *audio_name[] = {
  "Volume",
  "Mute",
  "Mode",
  NULL
};

const char *norm_name[] = {
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
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting \'%s\' capabilities: %s",
      v4lelement->videodev, g_strerror(errno));
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
  GParamSpec *spec;

  DEBUG("opening device %s", v4lelement->videodev);
  GST_V4L_CHECK_NOT_OPEN(v4lelement);
  GST_V4L_CHECK_NOT_ACTIVE(v4lelement);

  /* be sure we have a device */
  if (!v4lelement->videodev)
    v4lelement->videodev = g_strdup("/dev/video");

  /* open the device */
  v4lelement->video_fd = open(v4lelement->videodev, O_RDWR);
  if (!GST_V4L_IS_OPEN(v4lelement))
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Failed to open device (\'%s\'): %s",
      v4lelement->videodev, g_strerror(errno));
    return FALSE;
  }

  /* get capabilities */
  if (!gst_v4l_get_capabilities(v4lelement))
  {
    close(v4lelement->video_fd);
    v4lelement->video_fd = -1;
    return FALSE;
  }

  gst_info("Opened device \'%s\' (\'%s\') successfully\n",
    v4lelement->vcap.name, v4lelement->videodev);

  for (num=0;norm_name[num]!=NULL;num++)
    v4lelement->norm_names = g_list_append(v4lelement->norm_names, (gpointer)norm_name[num]);
  v4lelement->input_names = gst_v4l_get_chan_names(v4lelement);

  for (num=0;picture_name[num]!=NULL;num++)
  {
    spec = g_param_spec_int(picture_name[num], picture_name[num],
                            picture_name[num], 0, 65535, 32768,
                            G_PARAM_READWRITE);
    v4lelement->control_specs = g_list_append(v4lelement->control_specs, spec);
  }
  spec = g_param_spec_boolean("mute", "mute", "mute", TRUE, G_PARAM_READWRITE);
  v4lelement->control_specs = g_list_append(v4lelement->control_specs, spec);
  spec = g_param_spec_int("volume", "volume", "volume",
                          0, 65535, 0, G_PARAM_READWRITE);
  v4lelement->control_specs = g_list_append(v4lelement->control_specs, spec);

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

  while (g_list_length(v4lelement->input_names) > 0)
  {
    gpointer data = g_list_nth_data(v4lelement->input_names, 0);
    v4lelement->input_names = g_list_remove(v4lelement->input_names, data);
    g_free(data);
  }
  g_list_free(v4lelement->norm_names);
  v4lelement->norm_names = NULL;
  while (g_list_length(v4lelement->control_specs) > 0)
  {
    gpointer data = g_list_nth_data(v4lelement->control_specs, 0);
    v4lelement->control_specs = g_list_remove(v4lelement->control_specs, data);
    g_param_spec_unref(G_PARAM_SPEC(data));
  }

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
  GList *list = NULL;
  gint i;

  DEBUG("getting channel names");

  if (!GST_V4L_IS_OPEN(v4lelement))
    return NULL;

  for (i=0;i<gst_v4l_get_num_chans(v4lelement);i++)
  {
    vchan.channel = i;
    if (ioctl(v4lelement->video_fd, VIDIOCGCHAN, &vchan) < 0)
      return NULL;
    list = g_list_append(list, (gpointer)g_strdup(vchan.name));
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
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error setting the channel/norm settings: %s",
      g_strerror(errno));
    return FALSE;
  }

  if (ioctl(v4lelement->video_fd, VIDIOCGCHAN, &(v4lelement->vchan)) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting the channel/norm settings: %s",
      g_strerror(errno));
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4l_has_tuner():
 * return value: TRUE if it has a tuner, else FALSE
 ******************************************************/

gboolean
gst_v4l_has_tuner (GstV4lElement *v4lelement)
{
  DEBUG("checking whether device has a tuner");
  GST_V4L_CHECK_OPEN(v4lelement);

  return v4lelement->vcap.type & VID_TYPE_TUNER;
}


/******************************************************
 * gst_v4l_get_signal():
 *   get the current signal
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_get_signal (GstV4lElement *v4lelement,
                       guint        *signal)
{
	struct video_tuner tuner;

  DEBUG("getting tuner signal");
  GST_V4L_CHECK_OPEN(v4lelement);

  if (!gst_v4l_has_tuner(v4lelement))
    return FALSE;

  tuner.tuner = 0;
  if (ioctl(v4lelement->video_fd, VIDIOCGTUNER, &tuner) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting tuner signal: %s",
      sys_errlist[errno]);
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
                       gulong        *frequency)
{
  DEBUG("getting tuner frequency");
  GST_V4L_CHECK_OPEN(v4lelement);

  if (!gst_v4l_has_tuner(v4lelement))
    return FALSE;

  if (ioctl(v4lelement->video_fd, VIDIOCGFREQ, frequency) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting tuner frequency: %s",
      g_strerror(errno));
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
                       gulong        frequency)
{
  DEBUG("setting tuner frequency to %lu", frequency);
  GST_V4L_CHECK_OPEN(v4lelement);
  
  if (!gst_v4l_has_tuner(v4lelement))
    return FALSE;

  if (ioctl(v4lelement->video_fd, VIDIOCSFREQ, &frequency) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error setting tuner frequency: %s",
      g_strerror(errno));
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
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting picture parameters: %s",
      g_strerror(errno));
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
      gst_element_error(GST_ELEMENT(v4lelement),
        "Error getting picture parameters: unknown type %d",
        type);
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
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting picture parameters: %s",
      g_strerror(errno));
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
      gst_element_error(GST_ELEMENT(v4lelement),
        "Error setting picture parameters: unknown type %d",
        type);
      return FALSE;
  }

  if (ioctl(v4lelement->video_fd, VIDIOCSPICT, &vpic) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error setting picture parameters: %s",
      g_strerror(errno));
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4l_has_audio():
 * return value: TRUE if it can do audio, else FALSE
 ******************************************************/

gboolean
gst_v4l_has_audio (GstV4lElement *v4lelement)
{
  DEBUG("checking whether device has audio");
  GST_V4L_CHECK_OPEN(v4lelement);

  return v4lelement->vcap.audios > 0;
}


/******************************************************
 * gst_v4l_get_audio():
 *   get some audio value
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_get_audio (GstV4lElement   *v4lelement,
                   GstV4lAudioType type,
                   gint            *value)
{
  struct video_audio vau;

  DEBUG("getting audio parameter type %d (%s)",
    type, audio_name[type]);
  GST_V4L_CHECK_OPEN(v4lelement);

  if (!gst_v4l_has_audio(v4lelement))
    return FALSE;

  if (ioctl(v4lelement->video_fd, VIDIOCGAUDIO, &vau) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting audio parameters: %s",
      g_strerror(errno));
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
      gst_element_error(GST_ELEMENT(v4lelement),
        "Error getting audio parameters: unknown type %d",
        type);
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
                   GstV4lAudioType type,
                   gint            value)
{
  struct video_audio vau;

  DEBUG("setting audio parameter type %d (%s) to value %d",
    type, audio_name[type], value);
  GST_V4L_CHECK_OPEN(v4lelement);

  if (!gst_v4l_has_audio(v4lelement))
    return FALSE;

  if (ioctl(v4lelement->video_fd, VIDIOCGAUDIO, &vau) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting audio parameters: %s",
      g_strerror(errno));
    return FALSE;
  }

  switch (type)
  {
    case V4L_AUDIO_MUTE:
      if (!(vau.flags & VIDEO_AUDIO_MUTABLE))
      {
        gst_element_error(GST_ELEMENT(v4lelement),
          "Error setting audio mute: (un)setting mute is not supported");
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
        gst_element_error(GST_ELEMENT(v4lelement),
          "Error setting audio volume: setting volume is not supported");
        return FALSE;
      }
      vau.volume = value;
      break;
    case V4L_AUDIO_MODE:
      vau.mode = value;
      break;
    default:
      gst_element_error(GST_ELEMENT(v4lelement),
        "Error setting audio parameters: unknown type %d",
        type);
      return FALSE;
  }

  if (ioctl(v4lelement->video_fd, VIDIOCSAUDIO, &vau) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error setting audio parameters: %s",
      g_strerror(errno));
    return FALSE;
  }

  return TRUE;
}
