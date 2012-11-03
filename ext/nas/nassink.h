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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef __GST_NAS_SINK_H__
#define __GST_NAS_SINK_H__

#include <gst/gst.h>
#include <gst/audio/gstaudiosink.h>

G_BEGIN_DECLS

#define GST_TYPE_NAS_SINK \
  (gst_nas_sink_get_type())
#define GST_NAS_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NAS_SINK,GstNasSink))
#define GST_NAS_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NAS_SINK,GstNasSinkClass))
#define GST_IS_NAS_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NAS_SINK))
#define GST_IS_NAS_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NAS_SINK))

typedef struct _GstNasSink GstNasSink;
typedef struct _GstNasSinkClass GstNasSinkClass;

struct _GstNasSink {
  GstAudioSink audiosink;

  /*< private >*/

  /* instance properties */

  gboolean mute;
  gchar* host;

  /* Server info */

  AuServer *audio;
  AuFlowID flow;
  AuDeviceID device;

  /* buffer */

  AuUint32 need_data;
};

struct _GstNasSinkClass {
  GstAudioSinkClass parent_class;
};

GType gst_nas_sink_get_type(void);

G_END_DECLS

#endif /* __GST_NAS_SINK_H__ */
