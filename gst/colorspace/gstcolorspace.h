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

#ifndef _GST_COLORSPACE_H_
#define _GST_COLORSPACE_H_

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_COLORSPACE \
  (gst_colorspace_get_type())
#define GST_COLORSPACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_COLORSPACE,GstColorspace))
#define GST_COLORSPACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstColorspace))
#define GST_IS_COLORSPACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_COLORSPACE))
#define GST_IS_COLORSPACE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_COLORSPACE))
typedef struct _GstColorspace GstColorspace;
typedef struct _GstColorspaceClass GstColorspaceClass;

typedef enum
{
  GST_COLORSPACE_NONE,
  GST_COLORSPACE_HERMES,
  GST_COLORSPACE_YUV_RGB,
  GST_COLORSPACE_YUY2_I420,
  GST_COLORSPACE_RGB32_I420,
  GST_COLORSPACE_RGB32_YV12,
  GST_COLORSPACE_420_SWAP,
} GstColorSpaceConverterType;

struct _GstColorspace
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  int converter_index;

  int src_format_index;
  int sink_format_index;

  int src_size;
  int sink_size;

  int src_stride;
  int sink_stride;

  gint width, height;
  gdouble fps;
};

struct _GstColorspaceClass
{
  GstElementClass parent_class;
};

GType gst_colorspace_get_type (void);

typedef struct _GstColorspaceFormat
{
  GstStaticCaps caps;

} GstColorspaceFormat;

typedef enum
{
  GST_COLORSPACE_I420,
  GST_COLORSPACE_YV12,
  GST_COLORSPACE_RGB32,
  GST_COLORSPACE_RGB24,
  GST_COLORSPACE_RGB16,
} GstColorSpaceFormatType;

typedef struct _GstColorspaceConverter
{
  GstColorSpaceFormatType from;
  GstColorSpaceFormatType to;
  void (*convert) (GstColorspace * colorspace, unsigned char *dest,
      unsigned char *src);
} GstColorspaceConverter;

G_END_DECLS
#endif
