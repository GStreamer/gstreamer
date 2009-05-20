/* GStreamer
 * Copyright (C) 2007-2008 Wouter Cloetens <wouter@mind.be>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more 
 */

#ifndef __GST_SOUP_HTTP_SRC_H__
#define __GST_SOUP_HTTP_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <glib.h>

G_BEGIN_DECLS

#include <libsoup/soup.h>

#define GST_TYPE_SOUP_HTTP_SRC \
  (gst_soup_http_src_get_type())
#define GST_SOUP_HTTP_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SOUP_HTTP_SRC,GstSoupHTTPSrc))
#define GST_SOUP_HTTP_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), \
      GST_TYPE_SOUP_HTTP_SRC,GstSoupHTTPSrcClass))
#define GST_IS_SOUP_HTTP_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SOUP_HTTP_SRC))
#define GST_IS_SOUP_HTTP_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SOUP_HTTP_SRC))

typedef struct _GstSoupHTTPSrc GstSoupHTTPSrc;
typedef struct _GstSoupHTTPSrcClass GstSoupHTTPSrcClass;

typedef enum {
  GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_IDLE,
  GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_QUEUED,
  GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING,
  GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_CANCELLED,
} GstSoupHTTPSrcSessionIOStatus;

struct _GstSoupHTTPSrc {
  GstPushSrc element;

  gchar *location;             /* Full URI. */
  gchar *user_agent;           /* User-Agent HTTP header. */
  gboolean automatic_redirect; /* Follow redirects. */
  SoupURI *proxy;              /* HTTP proxy URI. */
  gchar *user_id;              /* Authentication user id for location URI. */
  gchar *user_pw;              /* Authentication user password for location URI. */
  gchar *proxy_id;             /* Authentication user id for proxy URI. */
  gchar *proxy_pw;             /* Authentication user password for proxy URI. */
  gchar **cookies;             /* HTTP request cookies. */
  GMainContext *context;       /* I/O context. */
  GMainLoop *loop;             /* Event loop. */
  SoupSession *session;        /* Async context. */
  GstSoupHTTPSrcSessionIOStatus session_io_status;
                               /* Async I/O status. */
  SoupMessage *msg;            /* Request message. */
  GstFlowReturn ret;           /* Return code from callback. */
  GstBuffer **outbuf;          /* Return buffer allocated by callback. */
  gboolean interrupted;        /* Signal unlock(). */
  gboolean retry;              /* Should attempt to reconnect. */

  gboolean have_size;          /* Received and parsed Content-Length
                                  header. */
  guint64 content_size;        /* Value of Content-Length header. */
  guint64 read_position;       /* Current position. */
  gboolean seekable;           /* FALSE if the server does not support
                                  Range. */
  guint64 request_position;    /* Seek to this position. */

  /* Shoutcast/icecast metadata extraction handling. */
  gboolean iradio_mode;
  GstCaps *src_caps;
  gchar *iradio_name;
  gchar *iradio_genre;
  gchar *iradio_url;
  gchar *iradio_title;

  GstStructure *extra_headers;

  guint timeout;
};

struct _GstSoupHTTPSrcClass {
  GstPushSrcClass parent_class;
};

GType gst_soup_http_src_get_type (void);

G_END_DECLS

#endif /* __GST_SOUP_HTTP_SRC_H__ */

