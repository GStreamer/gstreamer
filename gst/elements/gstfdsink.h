/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfdsink.h: 
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


#ifndef __GST_FDSINK_H__
#define __GST_FDSINK_H__


#include <config.h>
#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


GstElementDetails gst_fdsink_details;


#define GST_TYPE_FDSINK \
  (gst_fdsink_get_type())
#define GST_FDSINK(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_FDSINK,GstFdSink))
#define GST_FDSINK_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_FDSINK,GstFdSinkClass))
#define GST_IS_FDSINK(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_FDSINK))
#define GST_IS_FDSINK_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_FDSINK))

typedef struct _GstFdSink GstFdSink;
typedef struct _GstFdSinkClass GstFdSinkClass;

struct _GstFdSink {
  GstElement element;

  GstPad *sinkpad;

  int fd;
};

struct _GstFdSinkClass {
  GstElementClass parent_class;
};

GtkType gst_fdsink_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_FDSINK_H__ */
