/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfakesink.h: 
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


#ifndef __GST_FAKESINK_H__
#define __GST_FAKESINK_H__


#include <config.h>
#include <gst/gst.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


GstElementDetails gst_fakesink_details;


#define GST_TYPE_FAKESINK \
  (gst_fakesink_get_type())
#define GST_FAKESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FAKESINK,GstFakeSink))
#define GST_FAKESINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FAKESINK,GstFakeSinkClass))
#define GST_IS_FAKESINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FAKESINK))
#define GST_IS_FAKESINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FAKESINK))

typedef struct _GstFakeSink GstFakeSink;
typedef struct _GstFakeSinkClass GstFakeSinkClass;

struct _GstFakeSink {
  GstElement 	 element;

  gboolean 	 silent;
  gboolean 	 dump;
  gboolean 	 sync;
  GstClock 	*clock;

  gchar 	*last_message;
};

struct _GstFakeSinkClass {
  GstElementClass parent_class;

  /* signals */
  void (*handoff) (GstElement *element, GstBuffer *buf, GstPad *pad);
};

GType gst_fakesink_get_type(void);

gboolean gst_fakesink_factory_init (GstElementFactory *factory);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_FAKESINK_H__ */
