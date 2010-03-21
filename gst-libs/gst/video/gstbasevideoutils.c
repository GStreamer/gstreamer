/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
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
#include "config.h"
#endif

#include "gstbasevideoutils.h"

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (basevideo_debug);
#define GST_CAT_DEFAULT basevideo_debug


gboolean
gst_base_video_rawvideo_convert (GstVideoState * state,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = FALSE;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  if (src_format == GST_FORMAT_BYTES &&
      *dest_format == GST_FORMAT_DEFAULT && state->bytes_per_picture != 0) {
    /* convert bytes to frames */
    *dest_value = gst_util_uint64_scale_int (src_value, 1,
        state->bytes_per_picture);
    res = TRUE;
  } else if (src_format == GST_FORMAT_DEFAULT &&
      *dest_format == GST_FORMAT_BYTES && state->bytes_per_picture != 0) {
    /* convert bytes to frames */
    *dest_value = src_value * state->bytes_per_picture;
    res = TRUE;
  } else if (src_format == GST_FORMAT_DEFAULT &&
      *dest_format == GST_FORMAT_TIME && state->fps_n != 0) {
    /* convert frames to time */
    /* FIXME add segment time? */
    *dest_value = gst_util_uint64_scale (src_value,
        GST_SECOND * state->fps_d, state->fps_n);
    res = TRUE;
  } else if (src_format == GST_FORMAT_TIME &&
      *dest_format == GST_FORMAT_DEFAULT && state->fps_d != 0) {
    /* convert time to frames */
    /* FIXME subtract segment time? */
    *dest_value = gst_util_uint64_scale (src_value, state->fps_n,
        GST_SECOND * state->fps_d);
    res = TRUE;
  }

  /* FIXME add bytes <--> time */

  return res;
}

gboolean
gst_base_video_encoded_video_convert (GstVideoState * state,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = FALSE;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  GST_DEBUG ("src convert");

#if 0
  if (src_format == GST_FORMAT_DEFAULT && *dest_format == GST_FORMAT_TIME) {
    if (dec->fps_d != 0) {
      *dest_value = gst_util_uint64_scale (granulepos_to_frame (src_value),
          dec->fps_d * GST_SECOND, dec->fps_n);
      res = TRUE;
    } else {
      res = FALSE;
    }
  } else {
    GST_WARNING ("unhandled conversion from %d to %d", src_format,
        *dest_format);
    res = FALSE;
  }
#endif

  return res;
}

gboolean
gst_base_video_state_from_caps (GstVideoState * state, GstCaps * caps)
{

  gst_video_format_parse_caps (caps, &state->format,
      &state->width, &state->height);

  if (!gst_video_parse_caps_framerate (caps, &state->fps_n, &state->fps_d))
    return FALSE;

  state->par_n = 1;
  state->par_d = 1;
  gst_video_parse_caps_pixel_aspect_ratio (caps, &state->par_n, &state->par_d);

  {
    GstStructure *structure = gst_caps_get_structure (caps, 0);
    state->interlaced = FALSE;
    gst_structure_get_boolean (structure, "interlaced", &state->interlaced);
  }

  state->clean_width = state->width;
  state->clean_height = state->height;
  state->clean_offset_left = 0;
  state->clean_offset_top = 0;

  /* FIXME need better error handling */
  return TRUE;
}

GstClockTime
gst_video_state_get_timestamp (const GstVideoState * state, int frame_number)
{
  if (frame_number < 0) {
    return state->segment.start -
        (gint64) gst_util_uint64_scale (-frame_number,
        state->fps_d * GST_SECOND, state->fps_n);
  } else {
    return state->segment.start +
        gst_util_uint64_scale (frame_number,
        state->fps_d * GST_SECOND, state->fps_n);
  }
}
