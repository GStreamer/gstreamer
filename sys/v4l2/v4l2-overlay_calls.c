/* G-Streamer generic V4L2 element - generic V4L2 overlay handling
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
#include "v4l2_calls.h"

#define DEBUG(format, args...) \
	GST_DEBUG_OBJECT (\
		GST_ELEMENT(v4l2element), \
		"V4L2-overlay: " format, ##args)


/******************************************************
 * gst_v4l2_set_display():
 *   calls v4l-conf
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_set_display (GstV4l2Element *v4l2element)
{
	gchar *buff;

	if (v4l2element->display)
		g_free(v4l2element->display);
	v4l2element->display = g_strdup(g_getenv("DISPLAY"));

	DEBUG("trying to set overlay to '%s'", v4l2element->display);

	/* start v4l-conf */
	buff = g_strdup_printf("v4l-conf -q -c %s -d %s",
		v4l2element->device, v4l2element->display);

	switch (system(buff)) {
		case -1:
			gst_element_error (v4l2element, RESOURCE, FAILED,
                                           (_("Could not start v4l-conf")),
                                           GST_ERROR_SYSTEM);
			g_free(buff);
			return FALSE;
		case 0:
			break;
		default:
			gst_element_error (v4l2element, RESOURCE, FAILED,
                                           (_("Executing v4l-conf failed")),
                                           GST_ERROR_SYSTEM);
			g_free(buff);
			return FALSE;
	}

	g_free(buff);
	return TRUE;
}


/******************************************************
 * gst_v4l2_set_window():
 *   sets the window where to display the video overlay
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_set_window (GstElement       *element,
                     gint              x,
                     gint              y,
                     gint              w,
                     gint              h,
                     struct v4l2_clip *clips,
                     gint              num_clips)
{
	struct v4l2_format fmt;
	GstV4l2Element *v4l2element = GST_V4L2ELEMENT(element);

	DEBUG("trying to set video window to %dx%d,%d,%d", x,y,w,h);
	GST_V4L2_CHECK_OVERLAY(v4l2element);
	GST_V4L2_CHECK_OPEN(v4l2element);

	fmt.type = V4L2_CAP_VIDEO_OVERLAY;
	fmt.fmt.win.clipcount = 0;
	fmt.fmt.win.w.left = x;
	fmt.fmt.win.w.top = y;
	fmt.fmt.win.w.width = w;
	fmt.fmt.win.w.height = h;
	fmt.fmt.win.clips = clips;
	fmt.fmt.win.clipcount = num_clips;
	fmt.fmt.win.bitmap = NULL;

	if (ioctl(v4l2element->video_fd, VIDIOC_S_FMT, &fmt) < 0) {
		gst_element_error (v4l2element, RESOURCE, TOO_LAZY, NULL,
			("Failed to set the video window: %s", g_strerror (errno)));
		return FALSE;
	}

	return TRUE;
}


/******************************************************
 * gst_v4l_set_overlay():
 *   enables/disables actual video overlay display
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_enable_overlay (GstV4l2Element *v4l2element,
                         gboolean        enable)
{
	gint doit = enable?1:0;

	DEBUG("trying to %s overlay display", enable?"enable":"disable");
	GST_V4L2_CHECK_OPEN(v4l2element);
	GST_V4L2_CHECK_OVERLAY(v4l2element);

	if (ioctl(v4l2element->video_fd, VIDIOC_OVERLAY, &doit) < 0) {
		gst_element_error (v4l2element, RESOURCE, TOO_LAZY, NULL,
			("Failed to %s overlay display: %s",
			 enable?"enable":"disable", g_strerror (errno)));
		return FALSE;
	}

	return TRUE;
}
