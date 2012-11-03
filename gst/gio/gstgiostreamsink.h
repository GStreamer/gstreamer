/* GStreamer
 *
 * Copyright (C) 2007 Rene Stadler <mail@renestadler.de>
 * Copyright (C) 2007-2009 Sebastian Dröge <slomo@circular-chaos.org>
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

#ifndef __GST_GIO_STREAM_SINK_H__
#define __GST_GIO_STREAM_SINK_H__

#include "gstgio.h"
#include "gstgiobasesink.h"

#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

#define GST_TYPE_GIO_STREAM_SINK \
  (gst_gio_stream_sink_get_type())
#define GST_GIO_STREAM_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GIO_STREAM_SINK,GstGioStreamSink))
#define GST_GIO_STREAM_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GIO_STREAM_SINK,GstGioStreamSinkClass))
#define GST_IS_GIO_STREAM_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GIO_STREAM_SINK))
#define GST_IS_GIO_STREAM_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GIO_STREAM_SINK))

typedef struct _GstGioStreamSink      GstGioStreamSink;
typedef struct _GstGioStreamSinkClass GstGioStreamSinkClass;

/**
 * GstGioStreamSink:
 *
 * Opaque data structure.
 */
struct _GstGioStreamSink
{
  GstGioBaseSink sink;
  
  /* < private > */
  GOutputStream *stream;
};

struct _GstGioStreamSinkClass 
{
  GstGioBaseSinkClass parent_class;
};

GType gst_gio_stream_sink_get_type (void);

G_END_DECLS

#endif /* __GST_GIO_STREAM_SINK_H__ */
