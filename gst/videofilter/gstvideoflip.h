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


#ifndef __GST_VIDEOFLIP_H__
#define __GST_VIDEOFLIP_H__


#include <gst/gst.h>

#include "gstvideofilter.h"


G_BEGIN_DECLS typedef enum
{
  GST_VIDEOFLIP_METHOD_IDENTITY,
  GST_VIDEOFLIP_METHOD_90R,
  GST_VIDEOFLIP_METHOD_180,
  GST_VIDEOFLIP_METHOD_90L,
  GST_VIDEOFLIP_METHOD_HORIZ,
  GST_VIDEOFLIP_METHOD_VERT,
  GST_VIDEOFLIP_METHOD_TRANS,
  GST_VIDEOFLIP_METHOD_OTHER,
} GstVideoflipMethod;

#define GST_TYPE_VIDEOFLIP \
  (gst_videoflip_get_type())
#define GST_VIDEOFLIP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEOFLIP,GstVideoflip))
#define GST_VIDEOFLIP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEOFLIP,GstVideoflipClass))
#define GST_IS_VIDEOFLIP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEOFLIP))
#define GST_IS_VIDEOFLIP_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEOFLIP))

typedef struct _GstVideoflip GstVideoflip;
typedef struct _GstVideoflipClass GstVideoflipClass;

struct _GstVideoflip
{
  GstVideofilter videofilter;

  GstVideoflipMethod method;
};

struct _GstVideoflipClass
{
  GstVideofilterClass parent_class;
};

GType gst_videoflip_get_type (void);

G_END_DECLS
#endif /* __GST_VIDEOFLIP_H__ */
