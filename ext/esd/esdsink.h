/* Gnome-Streamer
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


#ifndef __GST_ESDSINK_H__
#define __GST_ESDSINK_H__

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_ESDSINK \
  (gst_esdsink_get_type())
#define GST_ESDSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ESDSINK,GstEsdsink))
#define GST_ESDSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ESDSINK,GstEsdsink))
#define GST_IS_ESDSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ESDSINK))
#define GST_IS_ESDSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ESDSINK))

typedef enum {
  GST_ESDSINK_OPEN            = GST_ELEMENT_FLAG_LAST,
  GST_ESDSINK_FLAG_LAST       = GST_ELEMENT_FLAG_LAST+2,
} GstEsdSinkFlags;

typedef struct _GstEsdsink GstEsdsink;
typedef struct _GstEsdsinkClass GstEsdsinkClass;

struct _GstEsdsink {
  GstElement element;

  GstPad *sinkpad;

  gboolean mute;
  int fd;
  gint format;
  gint depth;
  gint channels;
  gint frequency;
  gchar* host;
};

struct _GstEsdsinkClass {
  GstElementClass parent_class;
};

GType gst_esdsink_get_type(void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_ESDSINK_H__ */
