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


#ifndef __GST_CONNECTION_H__
#define __GST_CONNECTION_H__


#include <gst/gstelement.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_CONNECTION \
  (gst_connection_get_type())
#define GST_CONNECTION(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_CONNECTION,GstConnection))
#define GST_CONNECTION_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_CONNECTION,GstConnectionClass))
#define GST_IS_CONNECTION(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_CONNECTION))
#define GST_IS_CONNECTION_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_CONNECTION))

typedef struct _GstConnection GstConnection;
typedef struct _GstConnectionClass GstConnectionClass;

struct _GstConnection {
  GstElement element;
};

struct _GstConnectionClass {
  GstElementClass parent_class;

  /* push function */
  void (*push) (GstConnection *connection);
};

GtkType 	gst_connection_get_type		(void);
GstElement*	gst_connection_new		(gchar *name);

void 		gst_connection_push		(GstConnection *connection);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_CONNECTION_H__ */
