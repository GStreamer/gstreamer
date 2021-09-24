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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef __GST_DVDEC_H__
#define __GST_DVDEC_H__


#include <gst/gst.h>
#include <gst/video/video.h>

#include <libdv/dv.h>


G_BEGIN_DECLS

#define GST_TYPE_DVDEC (gst_dvdec_get_type())
G_DECLARE_FINAL_TYPE (GstDVDec, gst_dvdec, GST, DVDEC, GstElement)

struct _GstDVDec {
  GstElement     element;

  GstPad        *sinkpad;
  GstPad        *srcpad;

  dv_decoder_t  *decoder;
  gboolean       clamp_luma;
  gboolean       clamp_chroma;
  gint           quality;

  gboolean       PAL;
  gboolean       interlaced;
  gboolean       wide;

  /* input caps */
  gboolean       sink_negotiated;
  GstVideoInfo   vinfo;
  gint           framerate_numerator;
  gint           framerate_denominator;
  gint           height;
  gint           par_x;
  gint           par_y;
  gboolean       need_par;

  /* negotiated output */
  gint           bpp;
  gboolean       src_negotiated;
  
  gint           video_offset;
  gint           drop_factor;

  GstBufferPool *pool;
  GstSegment     segment;
  gboolean       need_segment;
};

G_END_DECLS


#endif /* __GST_DVDEC_H__ */
