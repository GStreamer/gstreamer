/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstmultifilesink.h: 
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


#ifndef __GST_MULTIFILESINK_H__
#define __GST_MULTIFILESINK_H__

#include <gst/gst.h>

G_BEGIN_DECLS


#define GST_TYPE_MULTIFILESINK \
  (gst_multifilesink_get_type())
#define GST_MULTIFILESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MULTIFILESINK,GstMultiFileSink))
#define GST_FILESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MULTIFILESINK,GstMultiFileSinkClass))
#define GST_IS_MULTIFILESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MULTIFILESINK))
#define GST_IS_MULTIFILESINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MULTIFILESINK))

typedef struct _GstMultiFileSink GstMultiFileSink;
typedef struct _GstMultiFileSinkClass GstMultiFileSinkClass;

typedef enum {
  GST_MULTIFILESINK_OPEN             = GST_ELEMENT_FLAG_LAST,
  GST_MULTIFILESINK_NEWFILE = GST_ELEMENT_FLAG_LAST +2,

  GST_MULTIFILESINK_FLAG_LAST 	= GST_ELEMENT_FLAG_LAST + 4
} GstMultiFileSinkFlags;

struct _GstMultiFileSink {
  GstElement element;

  gchar *filename;
  gchar *uri;
  gint curfileindex;
  gchar* curfilename;
  gint numfiles;
  FILE *file;

  guint64 data_written;
};

struct _GstMultiFileSinkClass {
  GstElementClass parent_class;

  /* signals */
  void (*handoff) (GstElement *element, GstPad *pad);
  void (*newfile) (GstElement *element);
};

GType gst_multifilesink_get_type(void);

G_END_DECLS

#endif /* __GST_MULTIFILESINK_H__ */
