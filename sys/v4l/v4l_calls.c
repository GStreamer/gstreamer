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

//#define DEBUG

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "v4l_calls.h"


char *picture_name[] = { "Hue", "Brightness", "Contrast", "Saturation" };

char *audio_name[] = { "Volume", "Mute", "Mode" };

char *norm_name[] = { "PAL", "NTSC", "SECAM", "Autodetect" };

/******************************************************
 * gst_v4l_get_capabilities():
 *   get the device's capturing capabilities
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4l_get_capabilities (GstV4lElement *v4lelement)
{
#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_get_capabilities()\n");
#endif

  GST_V4L_CHECK_OPEN(v4lelement);

  if (ioctl(v4lelement->video_fd, VIDIOCGCAP, &(v4lelement->vcap)) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting \'%s\' capabilities: %s",
      v4lelement->videodev, sys_errlist[errno]);
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
#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_open()\n");
#endif

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
      v4lelement->videodev, sys_errlist[errno]);
    return FALSE;
  }

  /* get capabilities */
  if (!gst_v4l_get_capabilities(v4lelement))
  {
    close(v4lelement->video_fd);
    v4lelement->video_fd = -1;
    return FALSE;
  }

  g_message("Opened device \'%s\' (\'%s\') successfully\n",
    v4lelement->vcap.name, v4lelement->videodev);

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
#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_close()\n");
#endif

  GST_V4L_CHECK_OPEN(v4lelement);
  GST_V4L_CHECK_NOT_ACTIVE(v4lelement);

  close(v4lelement->video_fd);
  v4lelement->video_fd = -1;

  return TRUE;
}


/******************************************************
 * gst_v4l_get_num_chans()
 * return value: the numver of video input channels
 ******************************************************/

gint
gst_v4l_get_num_chans (GstV4lElement *v4lelement)
{
#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_get_num_chans()\n");
#endif

  GST_V4L_CHECK_OPEN(v4lelement);

  return v4lelement->vcap.channels;
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
#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_get_chan_norm()\n");
#endif

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
#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_set_chan_norm(), channel = %d, norm = %d (%s)\n",
    channel, norm, norm_name[norm]);
#endif

  GST_V4L_CHECK_OPEN(v4lelement);
  GST_V4L_CHECK_NOT_ACTIVE(v4lelement);

  v4lelement->vchan.channel = channel;
  v4lelement->vchan.norm = norm;

  if (ioctl(v4lelement->video_fd, VIDIOCSCHAN, &(v4lelement->vchan)) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error setting the channel/norm settings: %s",
      sys_errlist[errno]);
    return FALSE;
  }

  if (ioctl(v4lelement->video_fd, VIDIOCGCHAN, &(v4lelement->vchan)) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting the channel/norm settings: %s",
      sys_errlist[errno]);
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
#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_has_tuner()\n");
#endif

  GST_V4L_CHECK_OPEN(v4lelement);

  return (v4lelement->vcap.type & VID_TYPE_TUNER &&
          v4lelement->vchan.flags & VIDEO_VC_TUNER);
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
#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_get_frequency()\n");
#endif

  GST_V4L_CHECK_OPEN(v4lelement);

  if (!gst_v4l_has_tuner(v4lelement))
    return FALSE;

  if (ioctl(v4lelement->video_fd, VIDIOCGFREQ, frequency) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting tuner frequency: %s",
      sys_errlist[errno]);
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
#ifdef DEBUG
  fprintf(stderr, "gst_v4l_set_frequency(), frequency = %ul\n",
    frequency);
#endif

  GST_V4L_CHECK_OPEN(v4lelement);
  GST_V4L_CHECK_NOT_ACTIVE(v4lelement);

  if (!gst_v4l_has_tuner(v4lelement))
    return FALSE;

  if (ioctl(v4lelement->video_fd, VIDIOCSFREQ, &frequency) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error setting tuner frequency: %s",
      sys_errlist[errno]);
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

#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_get_picture(), type = %d (%s)\n",
    type, picture_name[type]);
#endif

  GST_V4L_CHECK_OPEN(v4lelement);

  if (ioctl(v4lelement->video_fd, VIDIOCGPICT, &vpic) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting picture parameters: %s",
      sys_errlist[errno]);
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

#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_set_picture(), type = %d (%s), value = %d\n",
    type, picture_name[type], value);
#endif

  GST_V4L_CHECK_OPEN(v4lelement);

  if (ioctl(v4lelement->video_fd, VIDIOCGPICT, &vpic) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting picture parameters: %s",
      sys_errlist[errno]);
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
      sys_errlist[errno]);
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
#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_has_audio()\n");
#endif

  GST_V4L_CHECK_OPEN(v4lelement);

  return (v4lelement->vcap.audios > 0 &&
          v4lelement->vchan.flags & VIDEO_VC_AUDIO);
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

#ifdef DEBUG
  fprintf(stderr, "V4L: v4l_gst_get_audio(), type = %d (%s)\n",
    type, audio_name[type]);
#endif

  GST_V4L_CHECK_OPEN(v4lelement);

  if (!gst_v4l_has_audio(v4lelement))
    return FALSE;

  if (ioctl(v4lelement->video_fd, VIDIOCGAUDIO, &vau) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting audio parameters: %s",
      sys_errlist[errno]);
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

#ifdef DEBUG
  fprintf(stderr, "V4L: v4l_gst_set_audio(), type = %d (%s), value = %d\n",
    type, audio_name[type], value);
#endif

  GST_V4L_CHECK_OPEN(v4lelement);

  if (!gst_v4l_has_audio(v4lelement))
    return FALSE;

  if (ioctl(v4lelement->video_fd, VIDIOCGAUDIO, &vau) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Error getting audio parameters: %s",
      sys_errlist[errno]);
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
      sys_errlist[errno]);
    return FALSE;
  }

  return TRUE;
}
