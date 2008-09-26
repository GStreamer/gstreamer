/* GStreamer Adaptive Multi-Rate Narrow-Band (AMR-NB) plugin
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_AMRNBPARSE_H__
#define __GST_AMRNBPARSE_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_AMRNBPARSE \
  (gst_amrnbparse_get_type())
#define GST_AMRNBPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AMRNBPARSE, GstAmrnbParse))
#define GST_AMRNBPARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AMRNBPARSE, GstAmrnbParseClass))
#define GST_IS_AMRNBPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AMRNBPARSE))
#define GST_IS_AMRNBPARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AMRNBPARSE))

typedef struct _GstAmrnbParse GstAmrnbParse;
typedef struct _GstAmrnbParseClass GstAmrnbParseClass;

typedef gboolean (*GstAmrnbSeekHandler) (GstAmrnbParse * amrnbparse, GstPad * pad,
		    GstEvent * event);


struct _GstAmrnbParse {
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;

  GstAdapter *adapter;

  gboolean seekable;
  gboolean need_header;
  gint64 offset;
  gint block;

  GstAmrnbSeekHandler seek_handler;

  guint64 ts;

  /* for seeking etc */
  GstSegment segment;
};

struct _GstAmrnbParseClass {
  GstElementClass parent_class;
};

GType gst_amrnbparse_get_type (void);

G_END_DECLS

#endif /* __GST_AMRNBPARSE_H__ */
