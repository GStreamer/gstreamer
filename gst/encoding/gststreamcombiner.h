/* GStreamer Stream Combiner
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

#ifndef __GST_STREAMCOMBINER_H__
#define __GST_STREAMCOMBINER_H__

#include <gst/gst.h>

#define GST_TYPE_STREAM_COMBINER               (gst_stream_combiner_get_type())
#define GST_STREAM_COMBINER(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_STREAM_COMBINER,GstStreamCombiner))
#define GST_STREAM_COMBINER_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_STREAM_COMBINER,GstStreamCombinerClass))
#define GST_IS_STREAM_COMBINER(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_STREAM_COMBINER))
#define GST_IS_STREAM_COMBINER_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_STREAM_COMBINER))

typedef struct _GstStreamCombiner GstStreamCombiner;
typedef struct _GstStreamCombinerClass GstStreamCombinerClass;

struct _GstStreamCombiner {
  GstElement parent;

  GstPad *srcpad;

  /* lock protects:
   * * the current pad
   * * the list of srcpads
   */
  GMutex lock;
  /* Currently activated srcpad */
  GstPad *current;
  GList *sinkpads;
  guint32 cookie;

};

struct _GstStreamCombinerClass {
  GstElementClass parent;
};

GType gst_stream_combiner_get_type(void);

GstElement *gst_stream_combiner_new (gchar *name);

#endif /* __GST_STREAMCOMBINER_H__ */
