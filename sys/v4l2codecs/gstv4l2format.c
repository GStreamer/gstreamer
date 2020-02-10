/* GStreamer
 * Copyright (C) 2020 Nicolas Dufresne <nicolas.dufresne@collabora.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "gstv4l2format.h"

#define GST_CAT_DEFAULT gstv4l2codecs_debug
GST_DEBUG_CATEGORY_EXTERN (gstv4l2codecs_debug);

struct FormatEntry
{
  guint32 v4l2_pix_fmt;
  gint num_planes;
  GstVideoFormat gst_fmt;
  guint bitdepth;
  gint subsampling;
};

static struct FormatEntry format_map[] = {
  {V4L2_PIX_FMT_NV12, 1, GST_VIDEO_FORMAT_NV12, 8, 420},
  {0,}
};

static struct FormatEntry *
lookup_v4l2_fmt (guint v4l2_pix_fmt)
{
  gint i;
  struct FormatEntry *ret = NULL;

  for (i = 0; format_map[i].v4l2_pix_fmt; i++) {
    if (format_map[i].v4l2_pix_fmt == v4l2_pix_fmt) {
      ret = format_map + i;
      break;
    }
  }

  return ret;
}

gboolean
gst_v4l2_format_to_video_info (struct v4l2_format * fmt,
    GstVideoInfo * out_info)
{
  struct FormatEntry *entry = lookup_v4l2_fmt (fmt->fmt.pix_mp.pixelformat);

  if (!entry)
    return FALSE;

  if (entry->num_planes != 1) {
    GST_FIXME ("Multi allocation formats are not supported yet");
    return FALSE;
  }

  if (!gst_video_info_set_format (out_info, entry->gst_fmt,
          fmt->fmt.pix_mp.width, fmt->fmt.pix_mp.height))
    return FALSE;

  /* FIXME play the extrapolation danse for single FDs formats, and copy over
   * stride/offsets/size for the other formats */

  return TRUE;
}
