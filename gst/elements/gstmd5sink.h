/* GStreamer
 * Copyright (C) 2002 Erik Walthinsen <omega@cse.ogi.edu>
 *               2002 Wim Taymans <wtay@chello.be>
 *
 * gstmd5sink.h: 
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


#ifndef __GST_MD5SINK_H__
#define __GST_MD5SINK_H__


#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_MD5SINK \
  (gst_md5sink_get_type())
#define GST_MD5SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MD5SINK,GstMD5Sink))
#define GST_MD5SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MD5SINK,GstMD5SinkClass))
#define GST_IS_MD5SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MD5SINK))
#define GST_IS_MD5SINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MD5SINK))
typedef struct _GstMD5Sink GstMD5Sink;
typedef struct _GstMD5SinkClass GstMD5SinkClass;

struct _GstMD5Sink
{
  GstElement element;

  /* md5 information */
  guint32 A;
  guint32 B;
  guint32 C;
  guint32 D;

  guint32 total[2];
  guint32 buflen;
  gchar buffer[128];

  /* latest md5 */
  guchar md5[16];

};

struct _GstMD5SinkClass
{
  GstElementClass parent_class;

};

GType gst_md5sink_get_type (void);

gboolean gst_md5sink_factory_init (GstElementFactory * factory);

G_END_DECLS
#endif /* __GST_MD5SINK_H__ */
