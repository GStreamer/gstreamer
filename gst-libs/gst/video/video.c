/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Library       <2002> Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#  include "config.h"
#endif

#include "video.h"

/* This is simply a convenience function, nothing more or less */

gdouble
gst_video_frame_rate (GstPad *pad)
{
  gdouble fps = 0.;
  const GstCaps *caps = NULL;
  GstStructure *structure;

  /* get pad caps */
  caps = GST_PAD_CAPS (pad);
  if (caps == NULL) {
    g_warning ("gstvideo: failed to get caps of pad %s:%s",
               GST_ELEMENT_NAME (gst_pad_get_parent (pad)),
	       GST_PAD_NAME(pad));
    return 0.;
  }

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_double (structure, "framerate", &fps)){
    g_warning ("gstvideo: failed to get framerate property of pad %s:%s",
               GST_ELEMENT_NAME (gst_pad_get_parent (pad)),
	       GST_PAD_NAME (pad));
    return 0.;
  }

  GST_DEBUG ("Framerate request on pad %s:%s: %f",
             GST_ELEMENT_NAME (gst_pad_get_parent (pad)),
	     GST_PAD_NAME(pad), fps);

  return fps;
}

gboolean
gst_video_get_size (GstPad *pad,
                    gint   *width,
                    gint   *height)
{
  const GstCaps *caps = NULL;
  GstStructure *structure;
  gboolean ret;

  g_return_val_if_fail (pad != NULL, FALSE);
  g_return_val_if_fail (width != NULL, FALSE);
  g_return_val_if_fail (height != NULL, FALSE);

  caps = GST_PAD_CAPS (pad);

  if (caps == NULL) {
    g_warning ("gstvideo: failed to get caps of pad %s:%s",
               GST_ELEMENT_NAME (gst_pad_get_parent (pad)),
	       GST_PAD_NAME(pad));
    return FALSE;
  }

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", width);
  ret &= gst_structure_get_int (structure, "height", height);

  if (!ret) {
    g_warning ("gstvideo: failed to get size properties on pad %s:%s",
               GST_ELEMENT_NAME (gst_pad_get_parent (pad)),
	       GST_PAD_NAME(pad));
    return FALSE;
  }

  GST_DEBUG ("size request on pad %s:%s: %dx%d",
	     GST_ELEMENT_NAME (gst_pad_get_parent (pad)),
	     GST_PAD_NAME (pad), 
             width  ? *width  : -1,
	     height ? *height : -1);

  return TRUE;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstvideo",
  "Convenience routines for video plugins",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
