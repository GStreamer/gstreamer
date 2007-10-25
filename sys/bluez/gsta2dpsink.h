/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2007  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

#define GST_TYPE_A2DP_SINK \
	(gst_a2dp_sink_get_type())
#define GST_A2DP_SINK(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_A2DP_SINK,GstA2dpSink))
#define GST_A2DP_SINK_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_A2DP_SINK,GstA2dpSinkClass))
#define GST_IS_A2DP_SINK(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_A2DP_SINK))
#define GST_IS_A2DP_SINK_CLASS(obj) \
	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_A2DP_SINK))

typedef struct _GstA2dpSink GstA2dpSink;
typedef struct _GstA2dpSinkClass GstA2dpSinkClass;

struct bluetooth_data;

struct _GstA2dpSink {
	GstBaseSink sink;

	gchar *device;
	GIOChannel *stream;

	struct bluetooth_data *data;
	GIOChannel *server;

	gint con_state;

	GCond *con_conf_end;
	gboolean waiting_con_conf;
	GMutex *sink_lock;

	guint watch_id;
};

struct _GstA2dpSinkClass {
	GstBaseSinkClass parent_class;
};

GType gst_a2dp_sink_get_type(void);

G_END_DECLS
