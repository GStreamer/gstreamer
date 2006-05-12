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

/**
 * SECTION:gstvideo
 * @short_description: Support library for video operations
 *
 * <refsect2>
 * <para>
 * This library contains some helper functions and includes the 
 * videosink and videofilter base classes.
 * </para>
 * </refsect2>
 */

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
        GST_DEBUG_PAD_NAME (pad));
    return NULL;
  }

  structure = gst_caps_get_structure (caps, 0);
  if ((fps = gst_structure_get_value (structure, "framerate")) == NULL) {
    g_warning ("gstvideo: failed to get framerate property of pad %s:%s",
        GST_DEBUG_PAD_NAME (pad));
    return NULL;
  }
  if (!GST_VALUE_HOLDS_FRACTION (fps)) {
    g_warning
        ("gstvideo: framerate property of pad %s:%s is not of type Fraction",
        GST_DEBUG_PAD_NAME (pad));
    return NULL;
  }

  fps_string = gst_value_serialize (fps);
  GST_DEBUG ("Framerate request on pad %s:%s: %s",
      GST_DEBUG_PAD_NAME (pad), fps_string);
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
        GST_DEBUG_PAD_NAME (pad));
    return FALSE;
  }

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", width);
  ret &= gst_structure_get_int (structure, "height", height);

  if (!ret) {
    g_warning ("gstvideo: failed to get size properties on pad %s:%s",
        GST_DEBUG_PAD_NAME (pad));
    return FALSE;
  }

  GST_DEBUG ("size request on pad %s:%s: %dx%d",
      GST_DEBUG_PAD_NAME (pad), width ? *width : -1, height ? *height : -1);

  return TRUE;
}

/**
 * gst_video_calculate_display_ratio:
 * @dar_n: Numerator of the calculated display_ratio
 * @dar_d: Denominator of the calculated display_ratio
 * @video_width: Width of the video frame in pixels
 * @video_height: Height of the video frame in pixels
 * @video_par_n: Numerator of the pixel aspect ratio of the input video.
 * @video_par_d: Denominator of the pixel aspect ratio of the input video.
 * @display_par_n: Numerator of the pixel aspect ratio of the display device
 * @display_par_d: Denominator of the pixel aspect ratio of the display device
 *
 * Given the Pixel Aspect Ratio and size of an input video frame, and the 
 * pixel aspect ratio of the intended display device, calculates the actual 
 * display ratio the video will be rendered with.
 *
 * Returns: A boolean indicating success and a calculated Display Ratio in the 
 * dar_n and dar_d parameters. 
 * The return value is FALSE in the case of integer overflow or other error. 
 *
 * Since: 0.10.7
 */
gboolean
gst_video_calculate_display_ratio (guint * dar_n, guint * dar_d,
    guint video_width, guint video_height,
    guint video_par_n, guint video_par_d,
    guint display_par_n, guint display_par_d)
{
  gint num, den;

  GValue display_ratio = { 0, };
  GValue tmp = { 0, };
  GValue tmp2 = { 0, };

  g_return_val_if_fail (dar_n != NULL, FALSE);
  g_return_val_if_fail (dar_d != NULL, FALSE);

  g_value_init (&display_ratio, GST_TYPE_FRACTION);
  g_value_init (&tmp, GST_TYPE_FRACTION);
  g_value_init (&tmp2, GST_TYPE_FRACTION);

  /* Calculate (video_width * video_par_n * display_par_d) /
   * (video_height * video_par_d * display_par_n) */
  gst_value_set_fraction (&display_ratio, video_width, video_height);
  gst_value_set_fraction (&tmp, video_par_n, video_par_d);

  if (!gst_value_fraction_multiply (&tmp2, &display_ratio, &tmp))
    goto error_overflow;

  gst_value_set_fraction (&tmp, display_par_d, display_par_n);

  if (!gst_value_fraction_multiply (&display_ratio, &tmp2, &tmp))
    goto error_overflow;

  num = gst_value_get_fraction_numerator (&display_ratio);
  den = gst_value_get_fraction_denominator (&display_ratio);

  g_value_unset (&display_ratio);
  g_value_unset (&tmp);
  g_value_unset (&tmp2);

  g_return_val_if_fail (num > 0, FALSE);
  g_return_val_if_fail (den > 0, FALSE);

  *dar_n = num;
  *dar_d = den;

  return TRUE;
error_overflow:
  g_value_unset (&display_ratio);
  g_value_unset (&tmp);
  g_value_unset (&tmp2);
  return FALSE;
}
