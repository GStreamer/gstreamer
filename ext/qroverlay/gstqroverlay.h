/* 
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) 2015 anthony <<user@hostname.org>>
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
 
#ifndef __GST_QROVERLAY_H__
#define __GST_QROVERLAY_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_QROVERLAY \
  (gst_qroverlay_get_type())
#define GST_QROVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QROVERLAY,Gstqroverlay))
#define GST_QROVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QROVERLAY,GstqroverlayClass))
#define GST_IS_QROVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QROVERLAY))
#define GST_IS_QROVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QROVERLAY))

typedef struct _Gstqroverlay      Gstqroverlay;
typedef struct _GstqroverlayClass GstqroverlayClass;

struct _Gstqroverlay {
  GstBaseTransform element;

  guint32	frame_number;
  guint32	width;
  guint32	height;
  gfloat	qrcode_size;
  guint 	qrcode_quality;
  guint 	array_counter;
  guint 	array_size;
  guint 	span_frame;
  guint64	extra_data_interval_buffers;
  guint64	extra_data_span_buffers;
  QRecLevel level;
  gchar		*framerate_string;
  const gchar 	*extra_data_name;
  const gchar		*extra_data_array;
  gfloat	x_percent;
  gfloat	y_percent;
  gboolean	silent;
  gboolean	extra_data_enabled;
};

struct _GstqroverlayClass {
  GstBaseTransformClass parent_class;
};

GType gst_qroverlay_get_type (void);

G_END_DECLS

#endif /* __GST_QROVERLAY_H__ */
