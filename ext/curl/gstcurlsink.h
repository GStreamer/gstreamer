/* GStreamer
 * Copyright (C) 2011 Axis Communications <dev-gstreamer@axis.com>
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

#ifndef __GST_CURL_SINK__
#define __GST_CURL_SINK__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <curl/curl.h>

G_BEGIN_DECLS

#define GST_TYPE_CURL_SINK \
  (gst_curl_sink_get_type())
#define GST_CURL_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CURL_SINK, GstCurlSink))
#define GST_CURL_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CURL_SINK, GstCurlSinkClass))
#define GST_IS_CURL_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CURL_SINK))
#define GST_IS_CURL_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CURL_SINK))

typedef struct _GstCurlSink GstCurlSink;
typedef struct _GstCurlSinkClass GstCurlSinkClass;

typedef struct _TransferBuffer TransferBuffer;
typedef struct _TransferCondition TransferCondition;

struct _TransferBuffer {
  guint8 *ptr;
  size_t len;
  size_t offset;
};

struct _TransferCondition {
  GCond *cond;
  gboolean data_sent;
  gboolean data_available;
};

struct _GstCurlSink
{
  GstBaseSink parent;

  /*< private >*/
  CURLM *multi_handle;
  CURL *curl;
  struct curl_slist *header_list;
  GstPollFD fd;
  GstPoll *fdset;
  GThread *transfer_thread;
  GstFlowReturn flow_ret;
  TransferBuffer *transfer_buf;
  TransferCondition *transfer_cond;
  gint num_buffers_per_packet;
  gint timeout;
  gchar *url;
  gchar *user;
  gchar *passwd;
  gchar *proxy;
  guint proxy_port;
  gchar *proxy_user;
  gchar *proxy_passwd;
  gchar *file_name;
  guint qos_dscp;
  gboolean accept_self_signed;
  gboolean use_content_length;
  gboolean transfer_thread_close;
  gboolean new_file;
  gchar *content_type;
  gboolean proxy_headers_set;
};

struct _GstCurlSinkClass
{
  GstBaseSinkClass parent_class;
};

GType gst_curl_sink_get_type (void);

G_END_DECLS

#endif
