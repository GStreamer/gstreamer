/* G-Streamer generic V4L2 element - generic V4L2 calls handling
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __V4L2_CALLS_H__
#define __V4L2_CALLS_H__

#include "gstv4l2element.h"


/* simple check whether the device is open */
#define GST_V4L2_IS_OPEN(v4l2element) \
  (v4l2element->video_fd > 0)

/* check whether the device is 'active' */
#define GST_V4L2_IS_ACTIVE(v4l2element) \
  (v4l2element->buffer != NULL)

#define GST_V4L2_IS_OVERLAY(v4l2element) \
  (v4l2element->vcap.flags & V4L2_FLAG_PREVIEW)

/* checks whether the current v4lelement has already been open()'ed or not */
#define GST_V4L2_CHECK_OPEN(v4l2element) \
  if (v4l2element->video_fd <= 0)               \
  {                                             \
    gst_element_error(GST_ELEMENT(v4l2element), \
      "Device is not open");                    \
    return FALSE;                               \
  }

/* checks whether the current v4lelement is close()'ed or whether it is still open */
#define GST_V4L2_CHECK_NOT_OPEN(v4l2element) \
  if (v4l2element->video_fd != -1)              \
  {                                             \
    gst_element_error(GST_ELEMENT(v4l2element), \
      "Device is open");                        \
    return FALSE;                               \
  }

/* checks whether the current v4lelement does video overlay */
#define GST_V4L2_CHECK_OVERLAY(v4l2element) \
  if (!(v4l2element->vcap.flags & V4L2_FLAG_PREVIEW)) \
  {                                                   \
    gst_element_error(GST_ELEMENT(v4l2element),       \
      "Device doesn't do overlay");                   \
    return FALSE;                                     \
  }

/* checks whether we're in capture mode or not */
#define GST_V4L2_CHECK_ACTIVE(v4l2element) \
  if (v4l2element->buffer == NULL)              \
  {                                             \
    gst_element_error(GST_ELEMENT(v4l2element), \
      "Device is not in streaming mode");       \
    return FALSE;                               \
  }

/* checks whether we're out of capture mode or not */
#define GST_V4L2_CHECK_NOT_ACTIVE(v4l2element) \
  if (v4l2element->buffer != NULL)              \
  {                                             \
    gst_element_error(GST_ELEMENT(v4l2element), \
      "Device is in streaming mode");           \
    return FALSE;                               \
  }


/* open/close the device */
gboolean	gst_v4l2_open			(GstV4l2Element *v4l2element);
gboolean	gst_v4l2_close			(GstV4l2Element *v4l2element);

/* norm/input/output */
gboolean	gst_v4l2_get_norm		(GstV4l2Element *v4l2element,
						 gint           *norm);
gboolean	gst_v4l2_set_norm		(GstV4l2Element *v4l2element,
						 gint            norm);
GList *		gst_v4l2_get_norm_names		(GstV4l2Element *v4l2element);
gboolean	gst_v4l2_get_input		(GstV4l2Element *v4l2element,
						 gint           *input);
gboolean	gst_v4l2_set_input		(GstV4l2Element *v4l2element,
						 gint            input);
GList *		gst_v4l2_get_input_names	(GstV4l2Element *v4l2element);
gboolean	gst_v4l2_get_output		(GstV4l2Element *v4l2element,
						 gint           *output);
gboolean	gst_v4l2_set_output		(GstV4l2Element *v4l2element,
						 gint            output);
GList *		gst_v4l2_get_output_names	(GstV4l2Element *v4l2element);

/* frequency control */
gboolean	gst_v4l2_has_tuner		(GstV4l2Element *v4l2element);
gboolean	gst_v4l2_get_frequency		(GstV4l2Element *v4l2element,
						 gulong         *frequency);
gboolean	gst_v4l2_set_frequency		(GstV4l2Element *v4l2element,
					 	gulong          frequency);
gboolean	gst_v4l2_signal_strength	(GstV4l2Element *v4l2element,
						 gulong         *signal_strength);

/* attribute control */
gboolean	gst_v4l2_has_audio		(GstV4l2Element *v4l2element);
GList *		gst_v4l2_get_attributes		(GstV4l2Element *v4l2element);
gboolean	gst_v4l2_get_attribute		(GstV4l2Element *v4l2element,
						 gint            attribute_num,
						 gint           *value);
gboolean	gst_v4l2_set_attribute		(GstV4l2Element *v4l2element,
						 gint            attribute_num,
						 gint            value);

/* overlay */
gboolean	gst_v4l2_set_display		(GstV4l2Element *v4l2element,
						 const gchar    *display);
gboolean	gst_v4l2_set_window		(GstV4l2Element *v4l2element,
						 gint x,         gint y,
						 gint w,         gint h);
gboolean	gst_v4l2_set_clips		(GstV4l2Element *v4l2element,
						 struct v4l2_clip *clips,
						 gint            num_clips);
gboolean	gst_v4l2_enable_overlay		(GstV4l2Element *v4l2element,
						 gboolean        enable);

#endif /* __V4L2_CALLS_H__ */
