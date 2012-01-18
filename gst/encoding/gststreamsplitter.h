/* GStreamer Stream Splitter
 * Copyright (C) 2010 Edward Hervey <edward.hervey@collabora.co.uk>
 *           (C) 2009 Nokia Corporation
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

#ifndef __GST_STREAMSPLITTER_H__
#define __GST_STREAMSPLITTER_H__

#include <gst/gst.h>

#define GST_TYPE_STREAM_SPLITTER               (gst_stream_splitter_get_type())
#define GST_STREAM_SPLITTER(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_STREAM_SPLITTER,GstStreamSplitter))
#define GST_STREAM_SPLITTER_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_STREAM_SPLITTER,GstStreamSplitterClass))
#define GST_IS_STREAM_SPLITTER(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_STREAM_SPLITTER))
#define GST_IS_STREAM_SPLITTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_STREAM_SPLITTER))

typedef struct _GstStreamSplitter GstStreamSplitter;
typedef struct _GstStreamSplitterClass GstStreamSplitterClass;

struct _GstStreamSplitter {
  GstElement parent;

  GstPad *sinkpad;

  /* lock protects:
   * * the current pad
   * * the list of srcpads
   */
  GMutex lock;
  /* Currently activated srcpad */
  GstPad *current;
  GList *srcpads;
  guint32 cookie;

  /* List of pending in-band events */
  GList *pending_events;
};

struct _GstStreamSplitterClass {
  GstElementClass parent;
};

GType gst_stream_splitter_get_type(void);

GstElement *gst_stream_splitter_new (gchar *name);

#endif /* __GST_STREAMSPLITTER_H__ */
