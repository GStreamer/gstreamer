/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstdisksink.h: 
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


#ifndef __GST_DISKSINK_H__
#define __GST_DISKSINK_H__


#include <config.h>
#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


GstElementDetails gst_disksink_details;


#define GST_TYPE_DISKSINK \
  (gst_disksink_get_type())
#define GST_DISKSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DISKSINK,GstDiskSink))
#define GST_DISKSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DISKSINK,GstDiskSinkClass))
#define GST_IS_DISKSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DISKSINK))
#define GST_IS_DISKSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DISKSINK))

typedef struct _GstDiskSink GstDiskSink;
typedef struct _GstDiskSinkClass GstDiskSinkClass;

typedef enum {
  GST_DISKSINK_OPEN             = GST_ELEMENT_FLAG_LAST,

  GST_DISKSINK_FLAG_LAST 	= GST_ELEMENT_FLAG_LAST + 2,
} GstDiskSinkFlags;

struct _GstDiskSink {
  GstElement element;

  gchar *filename;
  FILE *file;

  gint filenum;

  guint64 data_written;
  gint maxfilesize;
};

struct _GstDiskSinkClass {
  GstElementClass parent_class;

  /* signals */
  void (*handoff) (GstElement *element,GstPad *pad);
};

GType gst_disksink_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_DISKSINK_H__ */
