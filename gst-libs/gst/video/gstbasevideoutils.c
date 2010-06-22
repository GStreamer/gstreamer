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

GST_DEBUG_CATEGORY_EXTERN (basevideocodec_debug);
#define GST_CAT_DEFAULT basevideocodec_debug


GstClockTime
gst_video_state_get_timestamp (const GstVideoState * state,
    GstSegment * segment, int frame_number)
{
  if (frame_number < 0) {
    return segment->start -
        (gint64) gst_util_uint64_scale (-frame_number,
        state->fps_d * GST_SECOND, state->fps_n);
  } else {
    return segment->start +
        gst_util_uint64_scale (frame_number,
        state->fps_d * GST_SECOND, state->fps_n);
  }
}
