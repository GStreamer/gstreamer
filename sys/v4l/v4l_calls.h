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

#ifndef __V4L_CALLS_H__
#define __V4L_CALLS_H__

#include "gstv4lelement.h"
#include "gst/gst-i18n-plugin.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* simple check whether the device is open */
#define GST_V4L_IS_OPEN(element) \
  (element->video_fd > 0)

/* check whether the device is 'active' */
#define GST_V4L_IS_ACTIVE(element) \
  (element->buffer != NULL)

#define GST_V4L_IS_OVERLAY(element) \
  (element->vcap.type & VID_TYPE_OVERLAY)

/* checks whether the current v4lelement has already been open()'ed or not */
#define GST_V4L_CHECK_OPEN(element)				\
  if (element->video_fd <= 0)					\
  {								\
    GST_ELEMENT_ERROR (element, RESOURCE, TOO_LAZY,		\
      (_("Device is not open")), NULL);				\
    return FALSE;						\
  }

/* checks whether the current v4lelement is close()'ed or whether it is still open */
#define GST_V4L_CHECK_NOT_OPEN(element)				\
  if (element->video_fd != -1)					\
  {								\
    GST_ELEMENT_ERROR (element, RESOURCE, TOO_LAZY,		\
      (_("Device is open")), NULL);				\
    return FALSE;						\
  }

/* checks whether the current v4lelement does video overlay */
#define GST_V4L_CHECK_OVERLAY(element)				\
  if (!(element->vcap.type & VID_TYPE_OVERLAY))			\
  {								\
    GST_ELEMENT_ERROR (element, RESOURCE, TOO_LAZY,		\
      NULL, ("Device cannot handle overlay"));			\
    return FALSE;						\
  }

/* checks whether we're in capture mode or not */
#define GST_V4L_CHECK_ACTIVE(element)				\
  if (element->buffer == NULL)					\
  {								\
    GST_ELEMENT_ERROR (element, RESOURCE, SETTINGS,		\
      NULL, ("Device is not in streaming mode"));		\
    return FALSE;						\
  }

/* checks whether we're out of capture mode or not */
#define GST_V4L_CHECK_NOT_ACTIVE(element)			\
  if (element->buffer != NULL)					\
  {								\
    GST_ELEMENT_ERROR (element, RESOURCE, SETTINGS,		\
      NULL, ("Device is in streaming mode"));			\
    return FALSE;						\
  }


typedef enum {
  V4L_PICTURE_HUE = 0,
  V4L_PICTURE_BRIGHTNESS,
  V4L_PICTURE_CONTRAST,
  V4L_PICTURE_SATURATION,
} GstV4lPictureType;

typedef enum {
  V4L_AUDIO_VOLUME = 0,
  V4L_AUDIO_MUTE,
  V4L_AUDIO_MODE, /* stereo, mono, ... (see videodev.h) */
} GstV4lAudioType;


/* open/close the device */
gboolean gst_v4l_open           (GstV4lElement *v4lelement);
gboolean gst_v4l_close          (GstV4lElement *v4lelement);

/* norm control (norm = VIDEO_MODE_{PAL|NTSC|SECAM|AUTO}) */
gboolean gst_v4l_get_chan_norm  (GstV4lElement *v4lelement,
				 gint          *channel,
				 gint          *norm);
gboolean gst_v4l_set_chan_norm  (GstV4lElement *v4lelement,
				 gint           channel,
				 gint           norm);
GList   *gst_v4l_get_chan_names (GstV4lElement *v4lelement);

/* frequency control */
gboolean gst_v4l_get_signal     (GstV4lElement *v4lelement,
				 gint           tunernum,
				 guint         *signal);
gboolean gst_v4l_get_frequency  (GstV4lElement *v4lelement,
				 gint           tunernum,
				 gulong        *frequency);
gboolean gst_v4l_set_frequency  (GstV4lElement *v4lelement,
				 gint           tunernum,
				 gulong         frequency);

/* picture control */
gboolean gst_v4l_get_picture    (GstV4lElement *v4lelement,
				 GstV4lPictureType type,
				 gint          *value);
gboolean gst_v4l_set_picture    (GstV4lElement *v4lelement,	
				 GstV4lPictureType type,
				 gint           value);

/* audio control */
gboolean gst_v4l_get_audio      (GstV4lElement *v4lelement,
				 gint           audionum,
				 GstV4lAudioType type,
				 gint          *value);
gboolean gst_v4l_set_audio      (GstV4lElement *v4lelement,
				 gint           audionum,
				 GstV4lAudioType type,
				 gint           value);

/* overlay */
gboolean gst_v4l_set_overlay    (GstV4lElement *v4lelement);
gboolean gst_v4l_set_window     (GstElement    *element,
				 gint x,        gint y,
				 gint w,        gint h,
				 struct video_clip *clips,
				 gint           num_clips);
gboolean gst_v4l_enable_overlay (GstV4lElement *v4lelement,
				 gboolean       enable);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __V4L_CALLS_H__ */
