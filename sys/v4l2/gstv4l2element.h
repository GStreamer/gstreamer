/* G-Streamer generic V4L2 element
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

#ifndef __GST_V4L2ELEMENT_H__
#define __GST_V4L2ELEMENT_H__

#include <gst/gst.h>
#include <sys/types.h>
#include <linux/types.h>
#include <linux/videodev2.h>


#define GST_TYPE_V4L2ELEMENT \
		(gst_v4l2element_get_type())
#define GST_V4L2ELEMENT(obj) \
		(G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_V4L2ELEMENT, GstV4l2Element))
#define GST_V4L2ELEMENT_CLASS(klass) \
		(G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_V4L2ELEMENT, GstV4l2ElementClass))
#define GST_IS_V4L2ELEMENT(obj) \
		(G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_V4L2ELEMENT))
#define GST_IS_V4L2ELEMENT_CLASS(obj) \
		(G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_V4L2ELEMENT))


typedef	struct _GstV4l2Element		GstV4l2Element;
typedef	struct _GstV4l2ElementClass	GstV4l2ElementClass;

typedef struct _GstV4l2Rect {
	gint x, y, w, h;
} GstV4l2Rect;

typedef enum {
	GST_V4L2_ATTRIBUTE_VALUE_TYPE_INT,
	GST_V4L2_ATTRIBUTE_VALUE_TYPE_BOOLEAN,
	GST_V4L2_ATTRIBUTE_VALUE_TYPE_BUTTON,
	GST_V4L2_ATTRIBUTE_VALUE_TYPE_LIST,
} GstV4l2AttributeValueType;

typedef enum {
	GST_V4L2_ATTRIBUTE_TYPE_VIDEO,
	GST_V4L2_ATTRIBUTE_TYPE_AUDIO,
	GST_V4L2_ATTRIBUTE_TYPE_EFFECT,
} GstV4l2AttributeType;

typedef struct _GstV4l2Attribute {
	gint index;
	gchar *name;
	GstV4l2AttributeType type;
	GstV4l2AttributeValueType val_type;
	gint min, max, value;
	GList *list_items; /* in case of 'list' */
} GstV4l2Attribute;

struct _GstV4l2Element {
	GstElement element;

	/* the video device */
	char *device;

	/* the video-device's file descriptor */
	gint video_fd;

	/* the video buffer (mmap()'ed) */
	guint8 **buffer;

	/* the video-device's capabilities */
	struct v4l2_capability vcap;

	/* the toys available to us */
	GList /*v4l2_fmtdesc*/ *formats; /* list of available capture formats */
	GList /*v4l2_input*/ *inputs;
	GList /*v4l2_output*/ *outputs;
	GList /*v4l2_enumstd*/ *norms;
	GList /*v4l2_queryctrl*/ *controls;
	GList /*GList:v4l2_querymenu*/ *menus;

	/* caching values */
	gint channel;
	gint output;
	gint norm;
	gulong frequency;
};

struct _GstV4l2ElementClass {
	GstElementClass parent_class;
};


GType gst_v4l2element_get_type (void);

#endif /* __GST_V4L2ELEMENT_H__ */
