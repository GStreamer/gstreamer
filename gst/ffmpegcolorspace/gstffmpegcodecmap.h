/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifndef __GST_FFMPEG_CODECMAP_H__
#define __GST_FFMPEG_CODECMAP_H__

#include <avcodec.h>
#include <gst/gst.h>

/* Template caps */

GstCaps *
gst_ffmpeg_pix_fmt_to_caps (void);

/* Disect a GstCaps */

enum PixelFormat
gst_ffmpeg_caps_to_pix_fmt (const GstCaps *caps,
			    int *width, int *height,
			    double *fps);

#endif /* __GST_FFMPEG_CODECMAP_H__ */
