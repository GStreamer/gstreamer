/* G-Streamer generic V4L element - generic V4L overlay handling
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "v4l_calls.h"

#define DEBUG(format, args...) \
	GST_DEBUG_OBJECT (\
		GST_ELEMENT(v4lelement), \
		"V4L-overlay: " format, ##args)


/******************************************************
 * gst_v4l_set_overlay():
 *   calls v4l-conf
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_set_overlay (GstV4lElement *v4lelement)
{
  gchar *buff;

  if (v4lelement->display)
    g_free(v4lelement->display);
  v4lelement->display = g_strdup(g_getenv("DISPLAY"));

  DEBUG("setting display to '%s'", v4lelement->display);
  GST_V4L_CHECK_NOT_OPEN(v4lelement);

  if (!v4lelement->display || v4lelement->display[0] != ':')
    return FALSE;

  /* start v4l-conf */
  buff = g_strdup_printf("v4l-conf -q -c %s -d %s",
    v4lelement->videodev, v4lelement->display);

  switch (system(buff))
  {
    case -1:
      GST_ELEMENT_ERROR (v4lelement, RESOURCE, FAILED,
                         (_("Could not start v4l-conf")), GST_ERROR_SYSTEM);
      g_free(buff);
      return FALSE;
    case 0:
      break;
    default:
      GST_ELEMENT_ERROR (v4lelement, RESOURCE, FAILED,
                         (_("Executing v4l-conf failed")), GST_ERROR_SYSTEM);
      g_free(buff);
      return FALSE;
  }

  g_free(buff);
  return TRUE;
}


/******************************************************
 * gst_v4l_set_window():
 *   sets the window where to display the video overlay
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_set_window (GstElement        *element,
                    gint               x,
                    gint               y,
                    gint               w,
                    gint               h,
                    struct video_clip *clips,
                    gint               num_clips)
{
  GstV4lElement *v4lelement = GST_V4LELEMENT(element);
  struct video_window vwin;

  DEBUG("setting video window to position (x,y/wxh) = %d,%d/%dx%d",
    x, y, w, h);
  GST_V4L_CHECK_OPEN(v4lelement);
  GST_V4L_CHECK_OVERLAY(v4lelement);

  vwin.x = x;
  vwin.y = y;
  vwin.width = w;
  vwin.height = h;
  vwin.flags = 0;

  if (clips && !(v4lelement->vcap.type & VID_TYPE_CLIPPING))
  {
    DEBUG("Device \'%s\' doesn't do clipping",
      v4lelement->videodev?v4lelement->videodev:"/dev/video");
    vwin.clips = 0;
  }
  else
  {
    vwin.clips = clips;
    vwin.clipcount = num_clips;
  }

  if (ioctl(v4lelement->video_fd, VIDIOCSWIN, &vwin) < 0)
  {
    GST_ELEMENT_ERROR (v4lelement, RESOURCE, TOO_LAZY, NULL,
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
gst_v4l_enable_overlay (GstV4lElement *v4lelement,
                        gboolean       enable)
{
  gint doit = enable?1:0;

  DEBUG("%s overlay", enable?"enabling":"disabling");
  GST_V4L_CHECK_OPEN(v4lelement);
  GST_V4L_CHECK_OVERLAY(v4lelement);

  if (ioctl(v4lelement->video_fd, VIDIOCCAPTURE, &doit) < 0)
  {
    GST_ELEMENT_ERROR (v4lelement, RESOURCE, TOO_LAZY, NULL,
      ("Failed to %s overlay display: %s",
      enable?"enable":"disable", g_strerror (errno)));
    return FALSE;
  }

  return TRUE;
}
