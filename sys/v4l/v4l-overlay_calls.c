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

//#define DEBUG

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "v4l_calls.h"


/******************************************************
 * gst_v4l_set_overlay():
 *   calls v4l-conf
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_set_overlay (GstV4lElement *v4lelement,
                     gchar         *display)
{
  gchar *buff;

#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_set_overlay(), display = \'%s\'\n", display);
#endif

  GST_V4L_CHECK_NOT_OPEN(v4lelement);

  /* start v4l-conf */
  buff = g_strdup_printf("v4l-conf -q -c %s -d %s",
    v4lelement->videodev?v4lelement->videodev:"/dev/video", display);

  switch (system(buff))
  {
    case -1:
      gst_element_error(GST_ELEMENT(v4lelement),
        "Could not start v4l-conf: %s", sys_errlist[errno]);
      g_free(buff);
      return FALSE;
    case 0:
      break;
    default:
      gst_element_error(GST_ELEMENT(v4lelement),
        "v4l-conf failed to run correctly: %s", sys_errlist[errno]);
      g_free(buff);
      return FALSE;
  }

  g_free(buff);
  return TRUE;
}


/******************************************************
 * gst_v4l_set_vwin():
 *   does the VIDIOCSVWIN ioctl()
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4l_set_vwin (GstV4lElement *v4lelement)
{
  if (ioctl(v4lelement->video_fd, VIDIOCSWIN, &(v4lelement->vwin)) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Failed to set the video window: %s",
      sys_errlist[errno]);
    return FALSE;
  }
  return TRUE;
}


/******************************************************
 * gst_v4l_set_window():
 *   sets the window where to display the video overlay
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_set_window (GstV4lElement *v4lelement,
                    gint x,        gint y,
                    gint w,        gint h)
{
#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_set_window(), position (x,y/wxh) = %d,%d/%dx%d\n",
    x, y, w, h);
#endif

  GST_V4L_CHECK_OVERLAY(v4lelement);

  v4lelement->vwin.clipcount = 0;
  v4lelement->vwin.x = x;
  v4lelement->vwin.y = y;
  v4lelement->vwin.width = w;
  v4lelement->vwin.height = h;
  v4lelement->vwin.flags = 0;

  return gst_v4l_set_vwin(v4lelement);
}


/******************************************************
 * gst_v4l_set_clips():
 *   sets video overlay clips
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l_set_clips (GstV4lElement     *v4lelement,
                   struct video_clip *clips,
                   gint               num_clips)
{
#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_set_clips()\n");
#endif

  GST_V4L_CHECK_OPEN(v4lelement);
  GST_V4L_CHECK_OVERLAY(v4lelement);

  if (!(v4lelement->vcap.type & VID_TYPE_CLIPPING))
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Device \'%s\' doesn't do clipping",
      v4lelement->videodev?v4lelement->videodev:"/dev/video");
    return FALSE;
  }

  v4lelement->vwin.clips = clips;
  v4lelement->vwin.clipcount = num_clips;

  return gst_v4l_set_vwin(v4lelement);
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

#ifdef DEBUG
  fprintf(stderr, "V4L: gst_v4l_enable_overlay(), enable = %s\n",
    enable?"true":"false");
#endif

  GST_V4L_CHECK_OPEN(v4lelement);
  GST_V4L_CHECK_OVERLAY(v4lelement);

  if (ioctl(v4lelement->video_fd, VIDIOCCAPTURE, &doit) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lelement),
      "Failed to %s overlay display: %s",
      enable?"enable":"disable", sys_errlist[errno]);
    return FALSE;
  }

  return TRUE;
}
