/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * This file:
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include "colorspace.h"

G_BEGIN_DECLS

#define GST_TYPE_CSP 	      (gst_csp_get_type())
#define GST_CSP(obj) 	      (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CSP,GstCsp))
#define GST_CSP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CSP,GstCspClass))
#define GST_IS_CSP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CSP))
#define GST_IS_CSP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CSP))

typedef struct _GstCsp GstCsp;
typedef struct _GstCspClass GstCspClass;

/**
 * GstCsp:
 *
 * Opaque object data structure.
 */
struct _GstCsp {
  GstVideoFilter element;

  gint width, height;
  gboolean interlaced;
  gfloat fps;

  GstVideoFormat from_format;
  ColorSpaceColorSpec from_spec;
  GstVideoFormat to_format;
  ColorSpaceColorSpec to_spec;

  ColorspaceConvert *convert;
  gboolean dither;
};

struct _GstCspClass
{
  GstVideoFilterClass parent_class;
};

G_END_DECLS

#endif /* __GST_COLORSPACE_H__ */
