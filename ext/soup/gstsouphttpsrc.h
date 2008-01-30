/* GStreamer
 * Copyright (C) <2007> Wouter Cloetens <wouter@mind.be>
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

#ifndef __GST_SOUPHTTP_SRC_H__
#define __GST_SOUPHTTP_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <glib.h>

G_BEGIN_DECLS

#include <libsoup/soup.h>

#define GST_TYPE_SOUPHTTP_SRC \
  (gst_souphttp_src_get_type())
#define GST_SOUPHTTP_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SOUPHTTP_SRC,GstSouphttpSrc))
#define GST_SOUPHTTP_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SOUPHTTP_SRC,GstSouphttpSrcClass))
#define GST_IS_SOUPHTTP_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SOUPHTTP_SRC))
#define GST_IS_SOUPHTTP_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SOUPHTTP_SRC))

typedef struct _GstSouphttpSrc GstSouphttpSrc;
typedef struct _GstSouphttpSrcClass GstSouphttpSrcClass;

typedef enum {
  GST_SOUPHTTP_SRC_SESSION_IO_STATUS_IDLE,
  GST_SOUPHTTP_SRC_SESSION_IO_STATUS_QUEUED,
  GST_SOUPHTTP_SRC_SESSION_IO_STATUS_RUNNING,
  GST_SOUPHTTP_SRC_SESSION_IO_STATUS_FINISHED,
} GstSouphttpSrcSessionIOStatus;

struct _GstSouphttpSrc {
  GstPushSrc element;

  gchar * location;                     /* Full URI. */
  gchar * user_agent;                   /* User-Agent HTTP header. */
  gboolean automatic_redirect;          /* Follow redirects. */
  SoupURI * proxy;                      /* HTTP proxy URI. */
  GMainContext * context;               /* I/O context. */
  GMainLoop * loop;                     /* Event loop. */
  SoupSession * session;                /* Async context. */
  GstSouphttpSrcSessionIOStatus session_io_status;
                                        /* Async I/O status. */
  SoupMessage * msg;                    /* Request message. */
  GstFlowReturn ret;                    /* Return code from callback. */
  GstBuffer ** outbuf;                  /* Return buffer allocated by callback. */
  gboolean interrupted;                 /* Signal unlock(). */

  gboolean have_size;                   /* Received and parsed Content-Length header. */
  guint64 content_size;                 /* Value of Content-Length header. */
  guint64 read_position;                /* Current position. */
  gboolean seekable;                    /* FALSE if the server does not support Range. */
  guint64 request_position;             /* Seek to this position. */

  /* Shoutcast/icecast metadata extraction handling. */
  gboolean iradio_mode;
  GstCaps * icy_caps;
  gchar * iradio_name;
  gchar * iradio_genre;
  gchar * iradio_url;
  gchar * iradio_title;
};

struct _GstSouphttpSrcClass {
  GstPushSrcClass parent_class;
};

GType gst_souphttp_src_get_type (void);

G_END_DECLS

#endif /* __GST_SOUPHTTP_SRC_H__ */

