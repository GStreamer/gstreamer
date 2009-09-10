/* GStreamer
 * Copyright (C) <2009> Руслан Ижбулатов <lrn1986 _at_ gmail _dot_ com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#ifndef __GST_MEASURE_COLLECTOR_H__
#define __GST_MEASURE_COLLECTOR_H__

#include "gstvideomeasure.h"
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

typedef struct _GstMeasureCollector GstMeasureCollector;
typedef struct _GstMeasureCollectorClass GstMeasureCollectorClass;

#define GST_TYPE_MEASURE_COLLECTOR            (gst_measure_collector_get_type())
#define GST_MEASURE_COLLECTOR(obj)                                             \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MEASURE_COLLECTOR,              \
    GstMeasureCollector))
#define GST_IS_MEASURE_COLLECTOR(obj)         \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_MEASURE_COLLECTOR))
#define GST_MEASURE_COLLECTOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),\
    GST_TYPE_MEASURE_COLLECTOR, GstMeasureCollectorClass))
#define GST_IS_MEASURE_COLLECTOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),\
    GST_TYPE_MEASURE_COLLECTOR))
#define GST_MEASURE_COLLECTOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),\
    GST_TYPE_MEASURE_COLLECTOR, GstMeasureCollectorClass))

typedef enum {
  GST_MEASURE_COLLECTOR_0 = 0,
  GST_MEASURE_COLLECTOR_WRITE_CSV = 0x1,
  GST_MEASURE_COLLECTOR_EMIT_MESSAGE = 0x1 << 1,
  GST_MEASURE_COLLECTOR_ALL =
      GST_MEASURE_COLLECTOR_WRITE_CSV |
      GST_MEASURE_COLLECTOR_EMIT_MESSAGE
} GstMeasureCollectorFlags;

struct _GstMeasureCollector {
  GstBaseTransform element;
  
  guint64 flags;

  gchar *filename;

  /* Array of pointers to GstStructure */
  GPtrArray *measurements;

  GValue *result;

  guint64 nextoffset;
  
  gchar *metric;

  gboolean inited;
};

struct _GstMeasureCollectorClass {
  GstBaseTransformClass parent_class;
};

GType gst_measure_collector_get_type (void);

G_END_DECLS

#endif /* __GST_MEASURE_COLLECTOR_H__ */
