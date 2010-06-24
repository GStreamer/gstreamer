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

#ifndef _GST_BASE_VIDEO_UTILS_H_
#define _GST_BASE_VIDEO_UTILS_H_

#define GST_USE_UNSTABLE_API 1

#ifndef GST_USE_UNSTABLE_API
#warning "The base video utils API is unstable and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

typedef struct _GstVideoState GstVideoState;

struct _GstVideoState
{
	gint width, height;
  gint fps_n, fps_d;
  gint par_n, par_d;

	gboolean interlaced;
	
  gint clean_width, clean_height;
  gint clean_offset_left, clean_offset_top;

  gint bytes_per_picture;

  GstBuffer *codec_data;

};

#endif /* _GST_BASE_VIDEO_UTILS_H_ */