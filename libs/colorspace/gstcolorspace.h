/* Gnome-Streamer
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


#ifndef __GST_COLORSPACE_H__
#define __GST_COLORSPACE_H__

#include <gdk/gdk.h>
#include <gst/gstbuffer.h>
#include <gst/gstplugin.h>

#include "yuv2rgb.h"

typedef enum {
#define GST_COLORSPACE_RGB_FIRST GST_COLORSPACE_RGB555
  GST_COLORSPACE_RGB555,
  GST_COLORSPACE_BGR555,
  GST_COLORSPACE_RGB565,
  GST_COLORSPACE_BGR565,
  GST_COLORSPACE_RGB24,                   // RGB
  GST_COLORSPACE_BGR24,                   // RGB
  GST_COLORSPACE_RGB32,
  GST_COLORSPACE_BGR32,
#define GST_COLORSPACE_RGB_LAST GST_COLORSPACE_BGR32

#define GST_COLORSPACE_YUV_FIRST GST_COLORSPACE_YUV420
  GST_COLORSPACE_YUV420,                  // YUV 
  GST_COLORSPACE_YUV420P,                 // YUV planar
  GST_COLORSPACE_YUV422,
  GST_COLORSPACE_YUV422P,
#define GST_COLORSPACE_YUV_LAST GST_COLORSPACE_YUV422P

} GstColorSpaceType;

typedef struct _GstColorSpace GstColorSpace;
typedef void (*GstColorSpaceConverter) (GstColorSpace *space, unsigned char *src, unsigned char *dest);

struct _GstColorSpace {
  guint width;
  guint height;
  GstColorSpaceType srcspace;
  GstColorSpaceType destspace;
  GdkVisual *visual;
  guint insize;
  guint outsize;
  /* private */
  GstColorSpaceYUVTables *color_tables;
  GstColorSpaceConverter convert;
};


#define GST_COLORSPACE_IS_RGB_TYPE(type) ((type)>=GST_COLORSPACE_RGB_FIRST && \
		                          (type)<=GST_COLORSPACE_RGB_LAST)
#define GST_COLORSPACE_IS_YUV_TYPE(type) ((type)>=GST_COLORSPACE_YUV_FIRST && \
		                          (type)<=GST_COLORSPACE_YUV_LAST)

GstColorSpace *gst_colorspace_new(int width, int height, GstColorSpaceType srcspace, GstColorSpaceType destspace, GdkVisual *destvisual);
void gst_colorspace_destroy(GstColorSpace *space);

#endif /* __GST_COLORSPACE_H__ */
