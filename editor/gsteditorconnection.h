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


#ifndef __GST_EDITOR_CONNECTION_H__
#define __GST_EDITOR_CONNECTION_H__

#include <gnome.h>
#include <gst.h>

#define GST_TYPE_EDITOR_CONNECTION \
  (gst_editor_connection_get_type())
#define GST_EDITOR_CONNECTION(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_EDITOR_CONNECTION,GstEditorConnection))
#define GST_EDITOR_CONNECTION_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_EDITOR_CONNECTION,GstEditorConnection))
#define GST_IS_EDITOR_CONNECTION(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_EDITOR_CONNECTION))
#define GST_IS_EDITOR_CONNECTION_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_EDITOR_CONNECTION)))

typedef struct _GstEditorConnection GstEditorConnection;
typedef struct _GstEditorConnectionClass GstEditorConnectionClass;

struct _GstEditorConnection {
  GnomeCanvasLine line;

  /* the two pads we're connecting */
  GstEditorPad *pad1, *pad2;
  gdouble fromsrc;

  /* visual stuff */
  gdouble x,y;					// terminating point
  GnomeCanvasPoints *points;
  gboolean created;				// has it been created?
  gboolean resized;				// does it need resizing?
};

struct _GstEditorConnectionClass {
  GnomeCanvasGroupClass parent_class;
};

GtkType gst_editor_connection_get_type();

#endif /* __GST_EDITOR_CONNECTION_H__ */
