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


#ifndef __GST_EDITOR_PAD_H__
#define __GST_EDITOR_PAD_H__

#include <gst/gst.h>

#define GST_TYPE_EDITOR_PAD \
  (gst_editor_pad_get_type())
#define GST_EDITOR_PAD(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_EDITOR_PAD,GstEditorPad))
#define GST_EDITOR_PAD_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_EDITOR_PAD,GstEditorPad))
#define GST_IS_EDITOR_PAD(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_EDITOR_PAD))
#define GST_IS_EDITOR_PAD_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_EDITOR_PAD))

typedef struct _GstEditorPad GstEditorPad;
typedef struct _GstEditorPadClass GstEditorPadClass;

struct _GstEditorPad {
  GtkObject object;

  /* parent object */
  GtkObject *parent;		// FIXME!!!

  /* the pad we're associated with */
  GstPad *pad;
  gboolean issink;

  /* visual stuff */
  GnomeCanvasGroup *group;
  GnomeCanvasItem *border,*title,*padbox;
  gboolean sinkpad;				// is this a sink pad?
  gdouble x,y;					// location
  gdouble width,height;				// actual size
  gdouble boxwidth,boxheight;			// size of pad box
  gboolean resize;				// does it need resizing?

  /* interaction state */
  gboolean dragging,resizing,moved;
  gdouble dragx,dragy;

  /* connection */
//  GnomeCanvasItem *connection;		// can't use
//GstEditorConnection
};

struct _GstEditorPadClass {
  GtkObjectClass parent_class;
};

GtkType gst_editor_pad_get_type();
GstEditorPad *gst_editor_pad_new(GstEditorElement *parent,GstPad *pad,
                                 const gchar *first_arg_name, ...);
void gst_editor_pad_construct(GstEditorPad *element,
                              GstEditorElement *parent,
                              const gchar *first_arg_name,va_list args);

#endif /* __GST_EDITOR_PAD_H__ */
