/* G-Streamer generic V4L element - generic V4L calls handling
 * Copyright (C) 2001 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __V4L_CALLS_H__
#define __V4L_CALLS_H__

#include "gstv4lelement.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* simple check whether the device is open */
#define GST_V4L_IS_OPEN(v4lelement) \
  (v4lelement->video_fd > 0)

/* check whether the device is 'active' */
#define GST_V4L_IS_ACTIVE(v4lelement) \
  (v4lelement->buffer != NULL)

/* checks whether the current v4lelement has already been open()'ed or not */
#define GST_V4L_CHECK_OPEN(v4lelement) \
  if (v4lelement->video_fd <= 0)               \
  {                                            \
    gst_element_error(GST_ELEMENT(v4lelement), \
      "Device is not open");                   \
    return FALSE;                              \
  }

/* checks whether the current v4lelement is close()'ed or whether it is still open */
#define GST_V4L_CHECK_NOT_OPEN(v4lelement) \
  if (v4lelement->video_fd != -1)              \
  {                                            \
    gst_element_error(GST_ELEMENT(v4lelement), \
      "Device is open");                       \
    return FALSE;                              \
  }

/* checks whether we're in capture mode or not */
#define GST_V4L_CHECK_ACTIVE(v4lelement) \
  if (v4lelement->buffer == NULL)              \
  {                                            \
    gst_element_error(GST_ELEMENT(v4lelement), \
      "Device is not in streaming mode");      \
    return FALSE;                              \
  }

/* checks whether we're out of capture mode or not */
#define GST_V4L_CHECK_NOT_ACTIVE(v4lelement) \
  if (v4lelement->buffer != NULL)              \
  {                                            \
    gst_element_error(GST_ELEMENT(v4lelement), \
      "Device is in streaming mode");          \
    return FALSE;                              \
  }


typedef enum {
  V4L_PICTURE_HUE,
  V4L_PICTURE_BRIGHTNESS,
  V4L_PICTURE_CONTRAST,
  V4L_PICTURE_SATURATION,
} GstV4lPictureType;

extern char *picture_name[];

typedef enum {
  V4L_AUDIO_VOLUME,
  V4L_AUDIO_MUTE,
  V4L_AUDIO_MODE, /* stereo, mono, ... (see videodev.h) */
} GstV4lAudioType;

extern char *audio_name[];

extern char *norm_name[];


/* open/close the device */
gboolean gst_v4l_open           (GstV4lElement *v4lelement);
gboolean gst_v4l_close          (GstV4lElement *v4lelement);

/* norm control (norm = VIDEO_MODE_{PAL|NTSC|SECAM|AUTO}) */
gint     gst_v4l_get_num_chans  (GstV4lElement *v4lelement);
gboolean gst_v4l_get_chan_norm  (GstV4lElement *v4lelement, gint *channel,          gint *norm);
gboolean gst_v4l_set_chan_norm  (GstV4lElement *v4lelement, gint  channel,          gint  norm);

/* frequency control */
gboolean gst_v4l_has_tuner      (GstV4lElement *v4lelement);
gboolean gst_v4l_get_frequency  (GstV4lElement *v4lelement, gulong *frequency);
gboolean gst_v4l_set_frequency  (GstV4lElement *v4lelement, gulong  frequency);

/* picture control */
gboolean gst_v4l_get_picture    (GstV4lElement *v4lelement, GstV4lPictureType type, gint *value);
gboolean gst_v4l_set_picture    (GstV4lElement *v4lelement, GstV4lPictureType type, gint  value);

/* audio control */
gboolean gst_v4l_has_audio      (GstV4lElement *v4lelement);
gboolean gst_v4l_get_audio      (GstV4lElement *v4lelement, GstV4lAudioType type,   gint *value);
gboolean gst_v4l_set_audio      (GstV4lElement *v4lelement, GstV4lAudioType type,   gint  value);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __V4L_CALLS_H__ */
