/* 
 * GStreamer
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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
 
#ifndef __GST_HDVPARSE_H__
#define __GST_HDVPARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_HDVPARSE \
  (gst_hdvparse_get_type())
#define GST_HDVPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HDVPARSE,GstHDVParse))
#define GST_HDVPARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HDVPARSE,GstHDVParseClass))
#define GST_IS_HDVPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HDVPARSE))
#define GST_IS_HDVPARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HDVPARSE))

typedef struct _GstHDVParse      GstHDVParse;
typedef struct _GstHDVParseClass GstHDVParseClass;

struct _GstHDVParse {
  GstBaseTransform element;

};

struct _GstHDVParseClass {
  GstBaseTransformClass parent_class;
};

GType gst_hdvparse_get_type (void);

G_END_DECLS

#endif /* __GST_HDVPARSE_H__ */
