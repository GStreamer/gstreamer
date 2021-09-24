/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2012 Collabora Ltd.
 *	Author : Edward Hervey <edward@collabora.com>
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


#ifndef __GST_PNGDEC_H__
#define __GST_PNGDEC_H__

#include <gst/gst.h>
#include <gst/video/gstvideodecoder.h>
#include <png.h>

G_BEGIN_DECLS

#define GST_TYPE_PNGDEC (gst_pngdec_get_type())
G_DECLARE_FINAL_TYPE (GstPngDec, gst_pngdec, GST, PNGDEC, GstVideoDecoder)

struct _GstPngDec
{
  GstVideoDecoder parent;

  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;
  GstMapInfo current_frame_map;
  GstVideoCodecFrame *current_frame;

  GstFlowReturn ret;

  png_structp png;
  png_infop info;
  png_infop endinfo;

  gint color_type;

  gboolean image_ready;
  gsize read_data;
};

GST_ELEMENT_REGISTER_DECLARE (pngdec);

G_END_DECLS

#endif /* __GST_PNGDEC_H__ */
