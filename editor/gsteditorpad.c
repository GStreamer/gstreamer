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

/* class functions */
static void gst_editor_pad_class_init(GstEditorPadClass *klass);
static void gst_editor_pad_init(GstEditorPad *pad);
static void gst_editor_pad_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_editor_pad_get_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_editor_pad_realize(GstEditorPad *pad);

static void gst_editor_pad_position_changed(GstEditorPad *pad, GstEditorElement *element);

/* class implementation functions */
//static void gst_editor_pad_update(GnomeCanvasItem *item,double *affine,
//                                      ArtSVP *clip_path,int flags);
//static gint gst_editor_pad_event(GnomeCanvasItem *item,GdkEvent *event);

/* events fired by items within self */
static gint gst_editor_pad_padbox_event(GnomeCanvasItem *item,
                                        GdkEvent *event,
                                        GstEditorPad *pad);

/* utility functions */
static void gst_editor_pad_resize(GstEditorPad *pad);


enum {
  ARG_0,
  ARG_X,
  ARG_Y,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_PAD,
};

enum {
  LAST_SIGNAL
};

static GtkObjectClass *parent_class;
//static guint gst_editor_pad_signals[LAST_SIGNAL] = { 0 };

GtkType 
gst_editor_pad_get_type (void) 
{
  static GtkType pad_type = 0;

  if (!pad_type) {
    static const GtkTypeInfo pad_info = {
      "GstEditorPad",
      sizeof(GstEditorPad),
      sizeof(GstEditorPadClass),
      (GtkClassInitFunc)gst_editor_pad_class_init,
      (GtkObjectInitFunc)gst_editor_pad_init,
      NULL,
      NULL,
      (GtkClassInitFunc)NULL,
    };
    pad_type = gtk_type_unique(gtk_object_get_type(),&pad_info);
  }
  return pad_type;
}

static void 
gst_editor_pad_class_init (GstEditorPadClass *klass) 
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(gtk_object_get_type());

  gtk_object_add_arg_type("GstEditorPad::x",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE,ARG_X);
  gtk_object_add_arg_type("GstEditorPad::y",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE,ARG_Y);
  gtk_object_add_arg_type("GstEditorPad::width",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE,ARG_WIDTH);
  gtk_object_add_arg_type("GstEditorPad::height",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE,ARG_HEIGHT);
  gtk_object_add_arg_type("GstEditorPad::pad",GTK_TYPE_POINTER,
                          GTK_ARG_READWRITE,ARG_PAD);

  klass->realize = gst_editor_pad_realize;

  object_class->set_arg = gst_editor_pad_set_arg;
  object_class->get_arg = gst_editor_pad_get_arg;
}

static void 
gst_editor_pad_init(GstEditorPad *pad) 
{
}

GstEditorPad*
gst_editor_pad_new(GstEditorElement *parent,GstPad *pad,
                   const gchar *first_arg_name, ...) 
{
  GstEditorPad *editorpad;
  va_list args;

  g_return_val_if_fail(parent != NULL, NULL);
  g_return_val_if_fail(GST_IS_EDITOR_ELEMENT(parent), NULL);
  g_return_val_if_fail(pad != NULL, NULL);
  g_return_val_if_fail(GST_IS_PAD(pad), NULL);

  editorpad = GST_EDITOR_PAD(gtk_type_new(GST_TYPE_EDITOR_PAD));
  editorpad->pad = pad;
  GST_EDITOR_SET_OBJECT(pad, editorpad);

  va_start(args,first_arg_name);
  gst_editor_pad_construct(editorpad,parent,first_arg_name,args);
  va_end(args);

  if (GST_PAD_CONNECTED (pad)) {
    GstPad *peerpad;
    GstEditorPad *peereditorpad;

    // FIXME does this need to check for ghost/real?
    peerpad = GST_PAD_PEER(pad);

    peereditorpad = GST_EDITOR_GET_OBJECT (peerpad);

    if (peereditorpad) { 
      GstEditorConnection *connection;

      connection = gst_editor_connection_new (parent, editorpad);
      gst_editor_connection_set_endpad (connection, peereditorpad);
    }
  }

  gtk_signal_connect_object (GTK_OBJECT (parent), "position_changed", 
		             gst_editor_pad_position_changed, editorpad);

  return editorpad;
}

void 
gst_editor_pad_construct(GstEditorPad *pad,
                         GstEditorElement *parent,
                         const gchar *first_arg_name,va_list args) 
{
  GtkObject *obj = GTK_OBJECT(pad);
  GSList *arg_list = NULL, *info_list = NULL;
  gchar *error;
  GstEditorPadClass *padclass;
  
//  g_print("in gst_editor_pad_construct()\n");
      
  error = gtk_object_args_collect(GTK_OBJECT_TYPE(obj),&arg_list,
                                  &info_list,first_arg_name,args);
  if (error) {
    g_warning("gst_editor_pad_construct(): %s",error);
    g_free(error);
  } else {
    GSList *arg,*info;
//    g_print("setting all the arguments on the pad\n");
    for (arg=arg_list,info=info_list;arg;arg=arg->next,info=info->next)
      gtk_object_arg_set(obj,arg->data,info->data);
    gtk_args_collect_cleanup(arg_list,info_list);
  }

  pad->parent = parent;

  padclass = GST_EDITOR_PAD_CLASS(GTK_OBJECT(pad)->klass);
  if (padclass)
    (padclass->realize)(pad);
}

static void 
gst_editor_pad_set_arg (GtkObject *object,GtkArg *arg,guint id) 
{
  GstEditorPad *pad;

  /* get the major types of this object */
  pad = GST_EDITOR_PAD(object);

  switch (id) {
    case ARG_X:
      pad->x = GTK_VALUE_DOUBLE(*arg);
      break;
    case ARG_Y:
      pad->y = GTK_VALUE_DOUBLE(*arg);
      break;
    case ARG_WIDTH:
      pad->width = GTK_VALUE_DOUBLE(*arg);
      pad->resize = TRUE;
      break;
    case ARG_HEIGHT:
      pad->height = GTK_VALUE_DOUBLE(*arg);
      pad->resize = TRUE;
      break;
    case ARG_PAD:
      /* FIXME: this is very brute force */
      pad->pad = GTK_VALUE_POINTER(*arg);
      break;
    default:
      g_warning("gsteditorpad: unknown arg!");
      break;
  }
}

static void 
gst_editor_pad_get_arg (GtkObject *object,GtkArg *arg,guint id) 
{
  GstEditorPad *pad;

  /* get the major types of this object */
  pad = GST_EDITOR_PAD(object);

  switch (id) {
    case ARG_X:
      GTK_VALUE_INT(*arg) = pad->x;
      break;
    case ARG_Y:
      GTK_VALUE_INT(*arg) = pad->y;
      break;
    case ARG_WIDTH:
      GTK_VALUE_INT(*arg) = pad->width;
      break;
    case ARG_HEIGHT:
      GTK_VALUE_INT(*arg) = pad->height;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

static void 
gst_editor_pad_realize (GstEditorPad *pad) 
{
//  g_print("realizing editor pad %p\n",pad);

  /* we must be attached to an element */
  g_return_if_fail(pad->parent != NULL);

  /* create the group and bounding box */
  pad->group = GNOME_CANVAS_GROUP(gnome_canvas_item_new(pad->parent->group,
    gnome_canvas_group_get_type(),"x",pad->x,"y",pad->y,NULL));
  g_return_if_fail(pad->group != NULL);
  GST_EDITOR_SET_OBJECT(pad->group,pad);

  pad->border = gnome_canvas_item_new(pad->group,
    gnome_canvas_rect_get_type(),
    "width_units",1.0,"fill_color_rgba", 0xCCFFCC00,"outline_color","black", 
    "x1",0.0,"y1",0.0,"x2",pad->width,"y2",pad->height,NULL);
  g_return_if_fail(pad->border != NULL);
  GST_EDITOR_SET_OBJECT(pad->border,pad);

  /* create the pad box on the correct side */
  // FIXME does this need to check for ghost/real?
  pad->issrc = (GST_PAD_DIRECTION(pad->pad) == GST_PAD_SRC);
  if (pad->issrc)
    pad->padbox = gnome_canvas_item_new(pad->group,
      gnome_canvas_rect_get_type(),
      "width_units",1.0,"fill_color","white","outline_color","black",
      "x1",pad->x-4.0,"y1",2.0,"x2",pad->x,"y2",pad->height-2.0,NULL);
  else
    pad->padbox = gnome_canvas_item_new(pad->group,
      gnome_canvas_rect_get_type(),
      "width_units",1.0,"fill_color","white","outline_color","black",
      "x1",0.0,"y1",2.0,"x2",4.0,"y2",pad->height-2.0,NULL);
  g_return_if_fail(pad->padbox != NULL);
  GST_EDITOR_SET_OBJECT(pad->padbox,pad);

  gtk_signal_connect(GTK_OBJECT(pad->group),"event",
    GTK_SIGNAL_FUNC(gst_editor_pad_padbox_event),pad);

  pad->title = gnome_canvas_item_new(pad->group,
    gnome_canvas_text_get_type(),
    "text",gst_pad_get_name(pad->pad),
    "x",0.0,"y",0.0,"anchor",GTK_ANCHOR_NORTH_WEST,
    "font_gdk",gtk_widget_get_default_style()->font,
    NULL);
  g_return_if_fail(pad->title != NULL);
  GST_EDITOR_SET_OBJECT(pad->title,pad);

  pad->realized = TRUE;
  pad->resize = TRUE;
  gst_editor_pad_repack(pad);
}


static void 
gst_editor_pad_resize (GstEditorPad *pad) 
{
  gdouble minwidth,minheight;

//  g_print("resizing pad\n");

  minwidth = 0;minheight = 0;

  /* get the text size and add it into minsize */
  minwidth = gst_util_get_double_arg(GTK_OBJECT(pad->title),
                                     "text_width") + 2.0;
  minheight = gst_util_get_double_arg(GTK_OBJECT(pad->title),
                                      "text_height");

  /* calculate the size of the padbox */
  pad->boxheight = minheight - 4.0;
  pad->boxwidth = pad->boxheight / 2.0;
  minwidth += pad->boxwidth;

  /* force the thing to grow if necessary */
  pad->width = MAX(pad->width,minwidth);
  pad->height = MAX(pad->height,minheight);

  /* update the connection if there is one */
//  g_print("connection is %p\n",pad->connection);
  if (pad->connection != NULL)
    gst_editor_connection_resize(pad->connection);
}

void 
gst_editor_pad_repack (GstEditorPad *pad) 
{
  gdouble x1,y1,x2,y2;

  if (!pad->realized) return;

  gst_editor_pad_resize(pad);

  x1 = 0;y1 = 0;
  x2 = x1 + pad->width;y2 = y1 + pad->height;
//  g_print("repacking pad at %.2fx%.2f - %.2fx%.2f\n",x1,y1,x2,y2);

  /* move the group */
  gtk_object_set(GTK_OBJECT(pad->group),"x",pad->x,"y",pad->y,NULL);

  /* start by resizing the bordering box */
  gtk_object_set(GTK_OBJECT(pad->border),
                 "x1",x1,"y1",y1,"x2",x2,"y2",y2,NULL);

  /* if we're a left-jusified sink */
  if (pad->issrc) {
    /* and move the pad box */
    gtk_object_set(GTK_OBJECT(pad->padbox),
                   "x1",x2-pad->boxwidth,"y1",y1+2.0,
                   "x2",x2,"y2",y2-2.0,NULL);
    /* then move the text to the right place */
    gtk_object_set(GTK_OBJECT(pad->title),
                   "x",x2-pad->boxwidth-1.0,"y",y1,
                   "anchor",GTK_ANCHOR_NORTH_EAST,
                   NULL);
  } else {
    /* and move the pad box */
    gtk_object_set(GTK_OBJECT(pad->padbox),
                   "x1",x1,"y1",y1+2.0,
                   "x2",x1+pad->boxwidth,"y2",y2-2.0,NULL);
    /* then move the text to the right place */
    gtk_object_set(GTK_OBJECT(pad->title),
                   "x",x1+pad->boxwidth+1.0,"y",y1,
                   "anchor",GTK_ANCHOR_NORTH_WEST,
                   NULL);
  }

  if (pad->connection != NULL) {
    pad->connection->resize = TRUE;
    gst_editor_connection_resize(pad->connection);
  }

  pad->resize = FALSE;
}


/*
static gint gst_editor_pad_event(GnomeCanvasItem *item,GdkEvent *event) {
  GstEditorPad *pad = GST_EDITOR_PAD(item);
  gdouble item_x,item_y;
  GdkCursor *fleur;
  gdouble tx,ty;

  item_x = event->button.x;
  item_y = event->button.y;
  gnome_canvas_item_w2i(item->parent,&item_x,&item_y);

  switch(event->type) {
    case GDK_ENTER_NOTIFY:
//      g_print("entered pad\n");
      break;
    case GDK_LEAVE_NOTIFY:
//      g_print("left pad\n");
      break;
    default:
      break;
  }
  return FALSE;
}
*/

/* FIXME FIXME FIXME */
static gint 
gst_editor_pad_padbox_event(GnomeCanvasItem *item,
                            GdkEvent *event,
                            GstEditorPad *pad) 
{
  GstEditorElement *element;
  GstEditorBin *bin;

//  g_print("padbox has event %d\n",event->type);
  g_return_val_if_fail(GST_IS_EDITOR_PAD(pad), FALSE);

  element = pad->parent;
  bin = element->parent;

  switch(event->type) {
    case GDK_ENTER_NOTIFY:
      gtk_object_set(GTK_OBJECT(pad->border),
                 "fill_color_rgba", 0xBBDDBB00, NULL);
      break;
    case GDK_LEAVE_NOTIFY:
      gtk_object_set(GTK_OBJECT(pad->border),
                 "fill_color_rgba", 0xCCFFCC00, NULL);
      break;
    case GDK_BUTTON_PRESS:
//      g_print("have button press in pad '%s'\n",
//              gst_pad_get_name(pad->pad));
      gst_editor_bin_start_banding(bin,pad);
      return TRUE;
      break;
    case GDK_MOTION_NOTIFY:
//      g_print("have motion in pad\n");
      break;
    default:
      break;
  }
  return FALSE;
}

static void
gst_editor_pad_position_changed(GstEditorPad *pad, 
		                GstEditorElement *element)
{
  GList *pads;

  if (pad->connection) {
//      g_print("updating pad's connection\n");
    pad->connection->resize = TRUE;
    gst_editor_connection_resize(pad->connection);
  }
}
