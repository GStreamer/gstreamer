/* G-Streamer Video4linux2 video-capture plugin
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

#ifndef __GST_V4L2SRC_H__
#define __GST_V4L2SRC_H__

#include <gstv4l2element.h>


#define GST_TYPE_V4L2SRC \
		(gst_v4l2src_get_type())
#define GST_V4L2SRC(obj) \
		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_V4L2SRC,GstV4l2Src))
#define GST_V4L2SRC_CLASS(klass) \
		(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_V4L2SRC,GstV4l2SrcClass))
#define GST_IS_V4L2SRC(obj) \
		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_V4L2SRC))
#define GST_IS_V4L2SRC_CLASS(obj) \
		(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_V4L2SRC))


typedef	struct _GstV4l2Src	GstV4l2Src;
typedef	struct _GstV4l2SrcClass	GstV4l2SrcClass;

struct _GstV4l2Src {
	GstV4l2Element v4l2element;

	/* pads */
	GstPad *srcpad;

	/* internal lists */
	GList /*v4l2_fmtdesc*/ *formats; /* list of available capture formats */

	/* buffer properties */
	struct v4l2_buffer bufsettings;
	struct v4l2_requestbuffers breq;
	struct v4l2_format format;
	guint64 first_timestamp;

	/* bufferpool for the buffers we're gonna use */
	GstBufferPool *bufferpool;

	/* caching values */
	gint width;
	gint height;
	gint palette;
};

struct _GstV4l2SrcClass {
	GstV4l2ElementClass parent_class;
};


GType gst_v4l2src_get_type(void);

#endif /* __GST_V4L2SRC_H__ */
