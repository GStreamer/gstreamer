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
#include "gst/gst-i18n-plugin.h"


/* simple check whether the device is open */
#define GST_V4L2_IS_OPEN(element) \
  (element->video_fd > 0)

/* check whether the device is 'active' */
#define GST_V4L2_IS_ACTIVE(element) \
  (element->buffer != NULL)

#define GST_V4L2_IS_OVERLAY(element) \
  (element->vcap.capabilities & V4L2_CAP_VIDEO_OVERLAY)

/* checks whether the current v4lelement has already been open()'ed or not */
#define GST_V4L2_CHECK_OPEN(element)				\
  if (!GST_V4L2_IS_OPEN(element))				\
  {								\
    gst_element_error (element, RESOURCE, TOO_LAZY,		\
      (_("Device is not open")), NULL);                         \
    return FALSE;						\
  }

/* checks whether the current v4lelement is close()'ed or whether it is still open */
#define GST_V4L2_CHECK_NOT_OPEN(element)			\
  if (GST_V4L2_IS_OPEN(element))				\
  {								\
    gst_element_error (element, RESOURCE, TOO_LAZY,		\
      (_("Device is open")), NULL);                             \
    return FALSE;						\
  }

/* checks whether the current v4lelement does video overlay */
#define GST_V4L2_CHECK_OVERLAY(element)				\
  if (!GST_V4L2_IS_OVERLAY(element))				\
  {								\
    gst_element_error (element, RESOURCE, TOO_LAZY,             \
      NULL, ("Device cannot handle overlay"));                  \
    return FALSE;						\
  }

/* checks whether we're in capture mode or not */
#define GST_V4L2_CHECK_ACTIVE(element)				\
  if (!GST_V4L2_IS_ACTIVE(element))				\
  {								\
    gst_element_error (element, RESOURCE, SETTINGS,             \
      NULL, ("Device is not in streaming mode"));               \
    return FALSE;						\
  }

/* checks whether we're out of capture mode or not */
#define GST_V4L2_CHECK_NOT_ACTIVE(element)			\
  if (GST_V4L2_IS_ACTIVE(element))				\
  {								\
    gst_element_error (element, RESOURCE, SETTINGS,             \
      NULL, ("Device is in streaming mode"));                   \
    return FALSE;						\
  }


/* open/close the device */
gboolean	gst_v4l2_open			(GstV4l2Element *v4l2element);
gboolean	gst_v4l2_close			(GstV4l2Element *v4l2element);

/* norm/input/output */
gboolean	gst_v4l2_get_norm		(GstV4l2Element *v4l2element,
						 v4l2_std_id    *norm);
gboolean	gst_v4l2_set_norm		(GstV4l2Element *v4l2element,
						 v4l2_std_id     norm);
gboolean	gst_v4l2_get_input		(GstV4l2Element *v4l2element,
						 gint           *input);
gboolean	gst_v4l2_set_input		(GstV4l2Element *v4l2element,
						 gint            input);
gboolean	gst_v4l2_get_output		(GstV4l2Element *v4l2element,
						 gint           *output);
gboolean	gst_v4l2_set_output		(GstV4l2Element *v4l2element,
						 gint            output);

/* frequency control */
gboolean	gst_v4l2_get_frequency		(GstV4l2Element *v4l2element,
						 gint            tunernum,
						 gulong         *frequency);
gboolean	gst_v4l2_set_frequency		(GstV4l2Element *v4l2element,
						 gint            tunernum,
					 	 gulong          frequency);
gboolean	gst_v4l2_signal_strength	(GstV4l2Element *v4l2element,
						 gint            tunernum,
						 gulong         *signal);

/* attribute control */
gboolean	gst_v4l2_get_attribute		(GstV4l2Element *v4l2element,
						 int             attribute,
						 int            *value);
gboolean	gst_v4l2_set_attribute		(GstV4l2Element *v4l2element,
						 int             attribute,
						 const int       value);

/* overlay */
gboolean	gst_v4l2_set_display		(GstV4l2Element *v4l2element);
gboolean	gst_v4l2_set_window		(GstElement     *element,
						 gint x,         gint y,
						 gint w,         gint h,
						 struct v4l2_clip *clips,
						 gint            num_clips);
gboolean	gst_v4l2_enable_overlay		(GstV4l2Element *v4l2element,
						 gboolean        enable);

#endif /* __V4L2_CALLS_H__ */
