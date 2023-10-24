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


#ifndef __GST_PNGENC_H__
#define __GST_PNGENC_H__

#include <gst/gst.h>
#include <gst/video/gstvideoencoder.h>
#include <png.h>

G_BEGIN_DECLS

#define GST_TYPE_PNGENC (gst_pngenc_get_type())
G_DECLARE_FINAL_TYPE (GstPngEnc, gst_pngenc, GST, PNGENC, GstVideoEncoder)

struct _GstPngEnc
{
  GstVideoEncoder parent;

  GstVideoCodecState *input_state;

  GstMemory *output_mem;
  GstMapInfo output_map;
  gsize output_mem_pos;

  png_structp png_struct_ptr;
  png_infop png_info_ptr;

  gint png_color_type;
  gint depth;
  guint compression_level;

  gboolean snapshot;
  guint    frame_count;
};

GST_ELEMENT_REGISTER_DECLARE (pngenc);

G_END_DECLS

#endif /* __GST_PNGENC_H__ */
