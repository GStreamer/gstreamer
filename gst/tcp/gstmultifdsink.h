/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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


#ifndef __GST_MULTI_FD_SINK_H__
#define __GST_MULTI_FD_SINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>

#include "gstmultihandlesink.h"

G_BEGIN_DECLS

#define GST_TYPE_MULTI_FD_SINK \
  (gst_multi_fd_sink_get_type())
#define GST_MULTI_FD_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MULTI_FD_SINK,GstMultiFdSink))
#define GST_MULTI_FD_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MULTI_FD_SINK,GstMultiFdSinkClass))
#define GST_IS_MULTI_FD_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MULTI_FD_SINK))
#define GST_IS_MULTI_FD_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MULTI_FD_SINK))
#define GST_MULTI_FD_SINK_GET_CLASS(klass) \
  (G_TYPE_INSTANCE_GET_CLASS ((klass), GST_TYPE_MULTI_FD_SINK, GstMultiFdSinkClass))


typedef struct _GstMultiFdSink GstMultiFdSink;
typedef struct _GstMultiFdSinkClass GstMultiFdSinkClass;


/* structure for a client
 */
typedef struct {
  GstMultiHandleClient client;

  GstPollFD gfd;

  gboolean is_socket;
} GstTCPClient;

/**
 * GstMultiFdSink:
 *
 * The multifdsink object structure.
 */
struct _GstMultiFdSink {
  GstMultiHandleSink element;

  /*< private >*/
  gint mode;
  GstPoll *fdset;

  gboolean handle_read;

  guint8 header_flags;
};

struct _GstMultiFdSinkClass {
  GstMultiHandleSinkClass parent_class;

  /* element methods */
  void          (*add)          (GstMultiFdSink *sink, GstMultiSinkHandle handle);
  void          (*add_full)     (GstMultiFdSink *sink, GstMultiSinkHandle handle, GstSyncMethod sync,
		                 GstFormat format, guint64 value, 
				 GstFormat max_format, guint64 max_value);
  void          (*remove)       (GstMultiFdSink *sink, GstMultiSinkHandle handle);
  void          (*remove_flush) (GstMultiFdSink *sink, GstMultiSinkHandle handle);
  void          (*clear)        (GstMultiFdSink *sink);
  GValueArray*  (*get_stats)    (GstMultiFdSink *sink, GstMultiSinkHandle handle);

  /* vtable */
  gboolean (*wait)   (GstMultiFdSink *sink, GstPoll *set);
  void (*removed) (GstMultiFdSink *sink, GstMultiSinkHandle handle);

  /* signals */
  void (*client_added) (GstElement *element, GstMultiSinkHandle handle);
  void (*client_removed) (GstElement *element, GstMultiSinkHandle handle, GstClientStatus status);
  void (*client_fd_removed) (GstElement *element, GstMultiSinkHandle handle);
};

GType gst_multi_fd_sink_get_type (void);

void          gst_multi_fd_sink_add          (GstMultiFdSink *sink, GstMultiSinkHandle handle);
void          gst_multi_fd_sink_add_full     (GstMultiFdSink *sink, GstMultiSinkHandle handle, GstSyncMethod sync, 
                                              GstFormat min_format, guint64 min_value,
                                              GstFormat max_format, guint64 max_value);
void          gst_multi_fd_sink_remove       (GstMultiFdSink *sink, GstMultiSinkHandle handle);
void          gst_multi_fd_sink_remove_flush (GstMultiFdSink *sink, GstMultiSinkHandle handle);
void          gst_multi_fd_sink_clear        (GstMultiHandleSink *sink);
GValueArray*  gst_multi_fd_sink_get_stats    (GstMultiFdSink *sink, GstMultiSinkHandle handle);

G_END_DECLS

#endif /* __GST_MULTI_FD_SINK_H__ */
