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


#include <gnome.h>
#include <gst/gst.h>

#include "gsteditor.h"

/* signals and args */
enum {
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_CANVAS,
};

static void gst_editor_canvas_class_init(GstEditorCanvasClass *klass);
static void gst_editor_canvas_init(GstEditorCanvas *editorcanvas);
static void gst_editor_canvas_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_editor_canvas_get_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_editor_canvas_realize(GstEditorElement *element);


static gint gst_editor_canvas_button_release(GtkWidget *widget,
                                             GdkEvent *event,
                                             GstEditorCanvas *canvas);
static gint gst_editor_canvas_event(GnomeCanvasItem *item,
                                    GdkEvent *event,
                                    GstEditorElement *element);

//gint gst_editor_canvas_verbose_event(GtkWidget *widget,GdkEvent *event);


static GstEditorBinClass *parent_class = NULL;

GtkType gst_editor_canvas_get_type(void) {
  static GtkType editor_canvas_type = 0;

  if (!editor_canvas_type) {
    static const GtkTypeInfo editor_canvas_info = {
      "GstEditorCanvas",
      sizeof(GstEditorCanvas),
      sizeof(GstEditorCanvasClass),
      (GtkClassInitFunc)gst_editor_canvas_class_init,
      (GtkObjectInitFunc)gst_editor_canvas_init,
      NULL,
      NULL,
      (GtkClassInitFunc)NULL,
    };
    editor_canvas_type = gtk_type_unique(gst_editor_bin_get_type(),&editor_canvas_info);
  }
  return editor_canvas_type;
}

static void gst_editor_canvas_class_init(GstEditorCanvasClass *klass) {
  GstEditorElementClass *element_class;

  element_class = (GstEditorElementClass*)klass;

  parent_class = gtk_type_class(gst_editor_bin_get_type());

  gtk_object_add_arg_type("GstEditorCanvas::canvas",GTK_TYPE_POINTER,
                          GTK_ARG_READABLE,ARG_CANVAS);

  element_class->realize = gst_editor_canvas_realize;
}

static void gst_editor_canvas_init(GstEditorCanvas *editorcanvas) {
}

GstEditorCanvas *gst_editor_canvas_new(GstBin *bin,
                                       const gchar *first_arg_name,...) {
  GstEditorCanvas *editorcanvas;
  GstEditorBin *bin2;
  va_list args;

  g_return_if_fail(bin != NULL);
  g_return_if_fail(GST_IS_BIN(bin));

  editorcanvas = GST_EDITOR_CANVAS(gtk_type_new(GST_TYPE_EDITOR_CANVAS));
  GST_EDITOR_ELEMENT(editorcanvas)->element = GST_ELEMENT(bin);
  GST_EDITOR_ELEMENT(editorcanvas)->parent = editorcanvas;

  va_start(args,first_arg_name);
  gst_editor_element_construct(GST_EDITOR_ELEMENT(editorcanvas),NULL,
                               first_arg_name,args);
  va_end(args);

  return editorcanvas;
}

static void gst_editor_canvas_realize(GstEditorElement *element) {
  GstEditorCanvas *canvas = GST_EDITOR_CANVAS(element);

  canvas->canvas = GNOME_CANVAS(gnome_canvas_new());
  element->canvas = canvas;
  gtk_signal_connect(GTK_OBJECT(canvas->canvas),
                     "event",
                     GTK_SIGNAL_FUNC(gst_editor_canvas_event),
                     canvas);
  gtk_signal_connect_after(GTK_OBJECT(canvas->canvas),
                     "button_release_event",
                     GTK_SIGNAL_FUNC(gst_editor_canvas_button_release),
                     canvas);
  GST_EDITOR_SET_OBJECT(canvas->canvas,canvas);

  element->group = gnome_canvas_root(canvas->canvas);

  if (GST_EDITOR_ELEMENT_CLASS(parent_class)->realize) {
    GST_EDITOR_ELEMENT_CLASS(parent_class)->realize(element);
  }
}

static void gst_editor_canvas_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstEditorCanvas *canvas;

  canvas = GST_EDITOR_CANVAS(object);

  switch (id) {
    default:
      g_warning("gsteditorcanvas: unknown arg!");
      break;
  }
}

static void gst_editor_canvas_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstEditorCanvas *canvas;

  canvas = GST_EDITOR_CANVAS(object);

  switch (id) {
    case ARG_CANVAS:
      GTK_VALUE_POINTER(*arg) = canvas->canvas;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

GtkWidget *gst_editor_canvas_get_canvas(GstEditorCanvas *canvas) {
  return GTK_WIDGET(canvas->canvas);
}


static gint gst_editor_canvas_button_release(GtkWidget *widget,
                                             GdkEvent *event,
                                             GstEditorCanvas *canvas) {
  GstEditorBin *bin = GST_EDITOR_BIN(canvas);
  gdouble x,y;
  GstEditorElement *element;

//  g_print("canvas got button press at %.2fx%.2f\n",
//          event->button.x,event->button.y);
  if (event->type != GDK_BUTTON_RELEASE) return FALSE;

  // if we're connecting a pair of objects in the canvas, fall through
//  if (bin->connection) {
//    g_print("we're in a connection, not handling\n");
//    return FALSE;
//  }

  if (canvas->inchild) {
//    g_print("inchild, not responding to button_release\n");
    canvas->inchild = FALSE;
    return FALSE;
  }

  gnome_canvas_window_to_world(GNOME_CANVAS(widget),
                               event->button.x,event->button.y,&x,&y);
//  g_print("calling gst_editor_create_item()\n");
  if (element = gst_editor_create_item(GST_EDITOR_BIN(canvas),x,y))
    return TRUE;
  return FALSE;
}


/* FIXME: guerilla prototype... */
void gst_editor_bin_connection_drag(GstEditorBin *bin,
                                    gdouble wx,gdouble wy);

static gint gst_editor_canvas_event(GnomeCanvasItem *item,
                                    GdkEvent *event,
                                    GstEditorElement *element) {
//  if (GST_EDITOR_ELEMENT_CLASS(parent_class)->event)
//    return (*GST_EDITOR_ELEMENT_CLASS(parent_class)->event)(
//      element->group,event,element);

  GstEditorBin *bin = GST_EDITOR_BIN(element);
  GstEditorCanvas *canvas = GST_EDITOR_CANVAS(element);

  //g_print("canvas got event %d at %.2fx%.2f\n",event->type,
  //        event->button.x,event->button.y);

  switch (event->type) {
    case GDK_BUTTON_RELEASE:
      if (bin->connecting) {
//        g_print("canvas got button release during drag\n");
        gnome_canvas_item_ungrab(
          GNOME_CANVAS_ITEM(element->group),
          event->button.time);
        if (bin->connection->topad)
          gst_editor_connection_connect(bin->connection);
        else
          gtk_object_destroy(GTK_OBJECT(bin->connection));
        bin->connecting = FALSE;
//g_print("finished dragging connection on canvas, setting inchild\n");
        element->canvas->inchild = TRUE;
        return TRUE;
      } else {
//        g_print("got release, calling button_release()\n");
//        gst_editor_canvas_button_release(canvas->canvas,event,canvas);
        return FALSE;
      }
      break;
    case GDK_MOTION_NOTIFY:
      if (bin->connecting) {
        gdouble x,y;
        x = event->button.x;y = event->button.y;
        gnome_canvas_window_to_world(canvas->canvas,
                               event->button.x,event->button.y,&x,&y);
//        g_print("canvas has motion during connection draw at
//%.2fx%.2f\n",
//                x,y);
        gst_editor_bin_connection_drag(bin,x,y);
        return TRUE;
      }
      break;
    default:
      break;
  }
  return FALSE;
}
