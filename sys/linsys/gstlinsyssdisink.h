/* GStreamer
 * Copyright (C) 2010 FIXME <fixme@example.com>
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

#ifndef _GST_LINSYS_SDI_SINK_H_
#define _GST_LINSYS_SDI_SINK_H_

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>


G_BEGIN_DECLS

#define GST_TYPE_LINSYS_SDI_SINK   (gst_linsys_sdi_sink_get_type())
#define GST_LINSYS_SDI_SINK(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LINSYS_SDI_SINK,GstLinsysSdiSink))
#define GST_LINSYS_SDI_SINK_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_LINSYS_SDI_SINK,GstLinsysSdiSinkClass))
#define GST_IS_LINSYS_SDI_SINK(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LINSYS_SDI_SINK))
#define GST_IS_LINSYS_SDI_SINK_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_LINSYS_SDI_SINK))

typedef struct _GstLinsysSdiSink GstLinsysSdiSink;
typedef struct _GstLinsysSdiSinkClass GstLinsysSdiSinkClass;

struct _GstLinsysSdiSink
{
  GstBaseSink base_linsyssdisink;

  /* properties */
  gchar *device;

  /* state */
  int fd;
  guint8 *tmpdata;
};

struct _GstLinsysSdiSinkClass
{
  GstBaseSinkClass base_linsyssdisink_class;
};

GType gst_linsys_sdi_sink_get_type (void);

G_END_DECLS

#endif
