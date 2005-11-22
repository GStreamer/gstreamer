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
const GValue *
gst_video_frame_rate (GstPad * pad)
{
  const GValue *fps;
  gchar *fps_string;

  const GstCaps *caps = NULL;
  GstStructure *structure;

  /* get pad caps */
  caps = GST_PAD_CAPS (pad);
  if (caps == NULL) {
    g_warning ("gstvideo: failed to get caps of pad %s:%s",
        GST_ELEMENT_NAME (gst_pad_get_parent (pad)), GST_PAD_NAME (pad));
    return NULL;
  }

  structure = gst_caps_get_structure (caps, 0);
  if ((fps = gst_structure_get_value (structure, "framerate")) == NULL) {
    g_warning ("gstvideo: failed to get framerate property of pad %s:%s",
        GST_ELEMENT_NAME (gst_pad_get_parent (pad)), GST_PAD_NAME (pad));
    return NULL;
  }
  if (!GST_VALUE_HOLDS_FRACTION (fps)) {
    g_warning
        ("gstvideo: framerate property of pad %s:%s is not of type Fraction",
        GST_ELEMENT_NAME (gst_pad_get_parent (pad)), GST_PAD_NAME (pad));
    return NULL;
  }

  fps_string = gst_value_serialize (fps);
  GST_DEBUG ("Framerate request on pad %s:%s: %s",
      GST_ELEMENT_NAME (gst_pad_get_parent (pad)), GST_PAD_NAME (pad),
      fps_string);
  g_free (fps_string);

  return fps;
}

gboolean
gst_video_get_size (GstPad * pad, gint * width, gint * height)
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
        GST_ELEMENT_NAME (gst_pad_get_parent (pad)), GST_PAD_NAME (pad));
    return FALSE;
  }

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", width);
  ret &= gst_structure_get_int (structure, "height", height);

  if (!ret) {
    g_warning ("gstvideo: failed to get size properties on pad %s:%s",
        GST_ELEMENT_NAME (gst_pad_get_parent (pad)), GST_PAD_NAME (pad));
    return FALSE;
  }

  GST_DEBUG ("size request on pad %s:%s: %dx%d",
      GST_ELEMENT_NAME (gst_pad_get_parent (pad)),
      GST_PAD_NAME (pad), width ? *width : -1, height ? *height : -1);

  return TRUE;
}
