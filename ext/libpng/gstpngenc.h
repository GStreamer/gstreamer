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


#ifndef __GST_PNGENC_H__
#define __GST_PNGENC_H__

#include <gst/gst.h>
#include <png.h>

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GST_TYPE_PNGENC  	(gst_pngenc_get_type())
#define GST_PNGENC(obj)  	(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PNGENC,GstPngEnc))
#define GST_PNGENC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PNGENC,GstPngEnc))
#define GST_IS_PNGENC(obj) 	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PNGENC))
#define GST_IS_PNGENC_CLASS(obj)(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PNGENC))

  typedef struct _GstPngEnc GstPngEnc;
  typedef struct _GstPngEncClass GstPngEncClass;

  extern GstPadTemplate *pngenc_src_template, *pngenc_sink_template;

  struct _GstPngEnc
  {
    GstElement element;

    GstPad *sinkpad, *srcpad;
    GstBuffer *buffer_out;

    png_structp png_struct_ptr;
    png_infop png_info_ptr;

    gint width;
    gint height;
    gint bpp;
  };

  struct _GstPngEncClass
  {
    GstElementClass parent_class;
  };

  GType gst_pngenc_get_type (void);


#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_PNGENC_H__ */
