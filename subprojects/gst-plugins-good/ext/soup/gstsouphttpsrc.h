/* GStreamer
 * Copyright (C) 2007-2008 Wouter Cloetens <wouter@mind.be>
 * Copyright (C) 2021 Igalia S.L.
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

#include "gstsouputils.h"
#include "gstsouploader.h"
#include <gio/gio.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS

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

typedef struct {
  gint max;
  gint count;            /* Number of retries since we received data */

  gdouble backoff_factor;
  gdouble backoff_max;
  GMutex lock;
  GCond cond;
} Retry;

/* opaque from here, implementation detail */
typedef struct _GstSoupSession GstSoupSession;

struct _GstSoupHTTPSrc {
  GstPushSrc element;

  gchar *location;             /* Full URI. */
  gchar *redirection_uri;      /* Full URI after redirections. */
  gboolean redirection_permanent; /* Permanent or temporary redirect? */
  gchar *user_agent;           /* User-Agent HTTP header. */
  gboolean automatic_redirect; /* Follow redirects. */
  GstSoupUri *proxy;           /* HTTP proxy URI. */
  gchar *user_id;              /* Authentication user id for location URI. */
  gchar *user_pw;              /* Authentication user password for location URI. */
  gchar *proxy_id;             /* Authentication user id for proxy URI. */
  gchar *proxy_pw;             /* Authentication user password for proxy URI. */
  gchar **cookies;             /* HTTP request cookies. */
  GstSoupSession *session;     /* Libsoup session wrapper. */
  gboolean session_is_shared;
  GstSoupSession *external_session; /* Shared via GstContext */
  SoupMessage *msg;            /* Request message. */
  gchar *method;               /* HTTP method */

  GstFlowReturn headers_ret;
  gboolean got_headers;        /* Already received headers from the server */
  gboolean have_size;          /* Received and parsed Content-Length
                                  header. */
  guint64 content_size;        /* Value of Content-Length header. */
  guint64 read_position;       /* Current position. */
  gboolean seekable;           /* FALSE if the server does not support
                                  Range. */
  guint64 request_position;    /* Seek to this position. */
  guint64 stop_position;       /* Stop at this position. */
  gboolean have_body;          /* Indicates if it has just been signaled the
                                * end of the message body. This is used to
                                * decide if an out of range request should be
                                * handled as an error or EOS when the content
                                * size is unknown */
  gboolean keep_alive;         /* Use keep-alive sessions */
  gboolean ssl_strict;
  gchar *ssl_ca_file;
  gboolean ssl_use_system_ca_file;
  GTlsDatabase *tls_database;
  GTlsInteraction *tls_interaction;

  GCancellable *cancellable;
  GInputStream *input_stream;

  gint reduce_blocksize_count;
  gint increase_blocksize_count;
  guint minimum_blocksize;

  /* Shoutcast/icecast metadata extraction handling. */
  gboolean iradio_mode;
  GstCaps *src_caps;
  gchar *iradio_name;
  gchar *iradio_genre;
  gchar *iradio_url;

  GstStructure *extra_headers;

  SoupLoggerLogLevel log_level;/* Soup HTTP session logger level */

  gboolean compress;

  guint timeout;

  /* This mutex-cond pair is used to talk to the soup session thread; it is
   * per src to allow concurrent access to shared sessions (if it was inside
   * the shared session structure, it would be effectively global)
   */
  GMutex session_mutex;
  GCond session_cond;

  GstEvent *http_headers_event;

  gint64 last_socket_read_time;

  Retry retry;
};

struct _GstSoupHTTPSrcClass {
  GstPushSrcClass parent_class;
};

GType gst_soup_http_src_get_type (void);

G_END_DECLS

#endif /* __GST_SOUP_HTTP_SRC_H__ */

