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

struct _GstSouphttpSrc {
  GstPushSrc element;

  gchar *location;                      /* Full URI. */
  GMainLoop *loop;                      /* Event loop. */
  SoupSession *session;                 /* Async context. */
  SoupMessage *msg;                     /* Request message. */
  GstFlowReturn ret;                    /* Return code from callback. */
  GstBuffer **outbuf;                   /* Return buffer allocated by callback. */
  gboolean interrupted;                 /* Signal unlock(). */
};

struct _GstSouphttpSrcClass {
  GstPushSrcClass parent_class;
};

GType gst_souphttp_src_get_type (void);

G_END_DECLS

#endif /* __GST_SOUPHTTP_SRC_H__ */

