/* GStreamer
 * Copyright (C) <2005> Philippe Khalaf <burger@speedy.org>
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

#ifndef __GST_DYNUDPSINK_H__
#define __GST_DYNUDPSINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

#include "gstudpnetutils.h"

#include "gstudp.h"

#define GST_TYPE_DYNUDPSINK             (gst_dynudpsink_get_type())
#define GST_DYNUDPSINK(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DYNUDPSINK,GstDynUDPSink))
#define GST_DYNUDPSINK_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DYNUDPSINK,GstDynUDPSinkClass))
#define GST_IS_DYNUDPSINK(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DYNUDPSINK))
#define GST_IS_DYNUDPSINK_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DYNUDPSINK))

typedef struct _GstDynUDPSink GstDynUDPSink;
typedef struct _GstDynUDPSinkClass GstDynUDPSinkClass;


/* sends udp packets to host/port pairs contained in the GstNetBuffer received.
 */
struct _GstDynUDPSink {
  GstBaseSink parent;

  /* properties */
  gint sockfd;
  gboolean closefd;

  /* the socket in use */
  int sock;
  gboolean externalfd;
};

struct _GstDynUDPSinkClass {
  GstBaseSinkClass parent_class;

  /* element methods */
  GValueArray*  (*get_stats)    (GstDynUDPSink *sink, const gchar *host, gint port);

  /* signals */
};

GType gst_dynudpsink_get_type(void);

GValueArray*    gst_dynudpsink_get_stats        (GstDynUDPSink *sink, const gchar *host, gint port);

G_END_DECLS

#endif /* __GST_DYNUDPSINK_H__ */
