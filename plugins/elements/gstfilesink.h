/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfilesink.h: 
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


#ifndef __GST_FILESINK_H__
#define __GST_FILESINK_H__

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_FILESINK \
  (gst_filesink_get_type())
#define GST_FILESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FILESINK,GstFileSink))
#define GST_FILESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FILESINK,GstFileSinkClass))
#define GST_IS_FILESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FILESINK))
#define GST_IS_FILESINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FILESINK))
typedef struct _GstFileSink GstFileSink;
typedef struct _GstFileSinkClass GstFileSinkClass;

typedef enum
{
  GST_FILESINK_OPEN = GST_ELEMENT_FLAG_LAST,

  GST_FILESINK_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2
} GstFileSinkFlags;

struct _GstFileSink
{
  GstElement element;

  gchar *filename;
  gchar *uri;
  FILE *file;

  guint64 data_written;
};

struct _GstFileSinkClass
{
  GstElementClass parent_class;

  /* signals */
  void (*handoff) (GstElement * element, GstPad * pad);
};

GType gst_filesink_get_type (void);

G_END_DECLS
#endif /* __GST_FILESINK_H__ */
