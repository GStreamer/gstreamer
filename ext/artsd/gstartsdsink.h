/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_ARTSDSINK_H__
#define __GST_ARTSDSINK_H__

#include <gst/gst.h>
#include <artsc.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_ARTSDSINK \
  (gst_artsdsink_get_type())
#define GST_ARTSDSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ARTSDSINK,GstArtsdsink))
#define GST_ARTSDSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ARTSDSINK,GstArtsdsink))
#define GST_IS_ARTSDSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ARTSDSINK))
#define GST_IS_ARTSDSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ARTSDSINK))

typedef enum {
  GST_ARTSDSINK_OPEN            = GST_ELEMENT_FLAG_LAST,
  GST_ARTSDSINK_FLAG_LAST       = GST_ELEMENT_FLAG_LAST+2,
} GstArtsdSinkFlags;

typedef struct _GstArtsdsink GstArtsdsink;
typedef struct _GstArtsdsinkClass GstArtsdsinkClass;

struct _GstArtsdsink {
  GstElement element;

  GstPad *sinkpad;

  gboolean connected;
  arts_stream_t stream;
  gboolean mute;
  gboolean signd;
  gint     depth;
  gint     channels;
  gint     frequency;
  gchar*   connect_name;
};

struct _GstArtsdsinkClass {
  GstElementClass parent_class;
};

GType gst_artsdsink_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_ARTSDSINK_H__ */
