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
	GST_DEBUG_ELEMENT(GST_CAT_PLUGIN_INFO, \
		GST_ELEMENT(v4l2element), \
		"V4L2-overlay: " format, ##args)


/******************************************************
 * gst_v4l2_set_display():
 *   calls v4l-conf
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_set_display (GstV4l2Element *v4l2element,
                      const gchar    *display)
{
	gchar *buff;

	DEBUG("trying to set overlay to '%s'", display);

	/* start v4l-conf */
	buff = g_strdup_printf("v4l-conf -q -c %s -d %s",
		v4l2element->device?v4l2element->device:"/dev/video", display);

	switch (system(buff)) {
		case -1:
			gst_element_error(GST_ELEMENT(v4l2element),
				"Could not start v4l-conf: %s", sys_errlist[errno]);
			g_free(buff);
			return FALSE;
		case 0:
			break;
		default:
			gst_element_error(GST_ELEMENT(v4l2element),
				"v4l-conf failed to run correctly: %s", sys_errlist[errno]);
			g_free(buff);
			return FALSE;
	}

	g_free(buff);
	return TRUE;
}


/******************************************************
 * gst_v4l2_set_vwin():
 *   does the VIDIOC_S_WIN ioctl()
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4l2_set_vwin (GstV4l2Element *v4l2element)
{
	if (ioctl(v4l2element->video_fd, VIDIOC_S_WIN, &(v4l2element->vwin)) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to set the video window on device %s: %s",
			v4l2element->device, sys_errlist[errno]);
		return FALSE;
	}

	return TRUE;
}


/******************************************************
 * gst_v4l2_set_window():
 *   sets the window where to display the video overlay
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_set_window (GstV4l2Element *v4l2element,
                     gint x,         gint y,
                     gint w,         gint h)
{
	DEBUG("trying to set video window to %dx%d,%d,%d", x,y,w,h);
	GST_V4L2_CHECK_OVERLAY(v4l2element);
	GST_V4L2_CHECK_OPEN(v4l2element);

	v4l2element->vwin.clipcount = 0;
	v4l2element->vwin.x = x;
	v4l2element->vwin.y = y;
	v4l2element->vwin.width = w;
	v4l2element->vwin.height = h;

	return gst_v4l2_set_vwin(v4l2element);
}


/******************************************************
 * gst_v4l_set_clips():
 *   sets video overlay clips
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_set_clips (GstV4l2Element     *v4l2element,
                    struct v4l2_clip   *clips,
                    gint                num_clips)
{
	DEBUG("trying to set video clipping information");
	GST_V4L2_CHECK_OPEN(v4l2element);
	GST_V4L2_CHECK_OVERLAY(v4l2element);

	v4l2element->vwin.clips = clips;
	v4l2element->vwin.clipcount = num_clips;

	return gst_v4l2_set_vwin(v4l2element);
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

	if (ioctl(v4l2element->video_fd, VIDIOC_PREVIEW, &doit) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to %s overlay display for device %s: %s",
			enable?"enable":"disable", v4l2element->device, sys_errlist[errno]);
		return FALSE;
	}

	return TRUE;
}
