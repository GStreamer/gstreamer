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
#include <gst/xwindowlistener/xwindowlistener.h>

/* Because of some really cool feature in video4linux1, also known as
 * 'not including sys/types.h and sys/time.h', we had to include it
 * ourselves. In all their intelligence, these people decided to fix
 * this in the next version (video4linux2) in such a cool way that it
 * breaks all compilations of old stuff...
 * The real problem is actually that linux/time.h doesn't use proper
 * macro checks before defining types like struct timeval. The proper
 * fix here is to either fuck the kernel header (which is what we do
 * by defining _LINUX_TIME_H, an innocent little hack) or by fixing it
 * upstream, which I'll consider doing later on. If you get compiler
 * errors here, check your linux/time.h && sys/time.h header setup.
 */
#include <sys/types.h>
#include <linux/types.h>
#define _LINUX_TIME_H
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
#define GST_V4L2ELEMENT_GET_CLASS(obj) \
		(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_V4L2ELEMENT, GstV4l2ElementClass))


typedef	struct _GstV4l2Element		GstV4l2Element;
typedef	struct _GstV4l2ElementClass	GstV4l2ElementClass;

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
	GList *channels;
	GList *norms;
	GList *colors;

	/* X-overlay */
	GstXWindowListener *overlay;
	XID xwindow_id;

	/* properties */
	gchar *norm;
	gchar *channel;
	gulong frequency;

	/* caching values */
	gchar *display;
};

struct _GstV4l2ElementClass {
	GstElementClass parent_class;

	/* probed devices */
	GList *devices;

	/* signals */
	void     (*open)            (GstElement  *element,
	                             const gchar *device);
	void     (*close)           (GstElement  *element,
	                             const gchar *device);
};


GType gst_v4l2element_get_type (void);

#endif /* __GST_V4L2ELEMENT_H__ */
