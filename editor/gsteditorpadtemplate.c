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
static void gst_editor_padtemplate_class_init(GstEditorPadTemplateClass *klass);
static void gst_editor_padtemplate_init(GstEditorPadTemplate *padtemplate);
static void gst_editor_padtemplate_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_editor_padtemplate_get_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_editor_padtemplate_realize(GstEditorPadTemplate *padtemplate);

/* class implementation functions */
//static void gst_editor_pad_update(GnomeCanvasItem *item,double *affine,
//                                      ArtSVP *clip_path,int flags);
//static gint gst_editor_pad_event(GnomeCanvasItem *item,GdkEvent *event);

/* events fired by items within self */
static gint gst_editor_padtemplate_padbox_event(GnomeCanvasItem *item,
                                        GdkEvent *event,
                                        GstEditorPadTemplate *padtemplate);

/* utility functions */
static void gst_editor_padtemplate_resize(GstEditorPadTemplate *padtemplate);


enum {
  ARG_0,
  ARG_X,
  ARG_Y,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_PADTEMPLATE,
};

enum {
  LAST_SIGNAL
};

static GtkObjectClass *parent_class;
//static guint gst_editor_padtemplate_signals[LAST_SIGNAL] = { 0 };

GtkType 
gst_editor_padtemplate_get_type (void) 
{
  static GtkType padtemplate_type = 0;

  if (!padtemplate_type) {
    static const GtkTypeInfo padtemplate_info = {
      "GstEditorPadTemplate",
      sizeof(GstEditorPadTemplate),
      sizeof(GstEditorPadTemplateClass),
      (GtkClassInitFunc)gst_editor_padtemplate_class_init,
      (GtkObjectInitFunc)gst_editor_padtemplate_init,
      NULL,
      NULL,
      (GtkClassInitFunc)NULL,
    };
    padtemplate_type = gtk_type_unique(gtk_object_get_type(),&padtemplate_info);
  }
  return padtemplate_type;
}

static void 
gst_editor_padtemplate_class_init (GstEditorPadTemplateClass *klass) 
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(gtk_object_get_type());

  gtk_object_add_arg_type("GstEditorPadTemplate::x",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE,ARG_X);
  gtk_object_add_arg_type("GstEditorPadTemplate::y",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE,ARG_Y);
  gtk_object_add_arg_type("GstEditorPadTemplate::width",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE,ARG_WIDTH);
  gtk_object_add_arg_type("GstEditorPadTemplate::height",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE,ARG_HEIGHT);
  gtk_object_add_arg_type("GstEditorPadTemplate::padtemplate",GTK_TYPE_POINTER,
                          GTK_ARG_READWRITE,ARG_PADTEMPLATE);

  klass->realize = gst_editor_padtemplate_realize;

  object_class->set_arg = gst_editor_padtemplate_set_arg;
  object_class->get_arg = gst_editor_padtemplate_get_arg;
}

static void 
gst_editor_padtemplate_init(GstEditorPadTemplate *padtemplate) 
{
}

GstEditorPadTemplate*
gst_editor_padtemplate_new (GstEditorElement *parent,
		            GstPadTemplate *padtemplate,
                            const gchar *first_arg_name, ...) 
{
  GstEditorPadTemplate *editorpadtemplate;
  va_list args;

  g_return_val_if_fail(parent != NULL, NULL);
  g_return_val_if_fail(GST_IS_EDITOR_ELEMENT(parent), NULL);
  g_return_val_if_fail(padtemplate != NULL, NULL);

  editorpadtemplate = GST_EDITOR_PADTEMPLATE(gtk_type_new(GST_TYPE_EDITOR_PADTEMPLATE));
  editorpadtemplate->padtemplate = padtemplate;
  //GST_EDITOR_SET_OBJECT(padtemplate, editorpadtemplate);

  va_start(args,first_arg_name);
  gst_editor_padtemplate_construct(editorpadtemplate,parent,first_arg_name,args);
  va_end(args);

  return editorpadtemplate;
}

void 
gst_editor_padtemplate_construct (GstEditorPadTemplate *padtemplate,
                                  GstEditorElement *parent,
                                  const gchar *first_arg_name, va_list args) 
{
  GtkObject *obj = GTK_OBJECT(padtemplate);
  GSList *arg_list = NULL, *info_list = NULL;
  gchar *error;
  GstEditorPadTemplateClass *padtemplateclass;
  
//  g_print("in gst_editor_padtemplate_construct()\n");
      
  error = gtk_object_args_collect(GTK_OBJECT_TYPE(obj),&arg_list,
                                  &info_list,first_arg_name,args);
  if (error) {
    g_warning("gst_editor_padtemplate_construct(): %s",error);
    g_free(error);
  } else {
    GSList *arg,*info;
//    g_print("setting all the arguments on the padtemplate\n");
    for (arg=arg_list,info=info_list;arg;arg=arg->next,info=info->next)
      gtk_object_arg_set(obj,arg->data,info->data);
    gtk_args_collect_cleanup(arg_list,info_list);
  }

  padtemplate->parent = parent;

  padtemplateclass = GST_EDITOR_PADTEMPLATE_CLASS(GTK_OBJECT(padtemplate)->klass);
  if (padtemplateclass)
    (padtemplateclass->realize)(padtemplate);
}

void 
gst_editor_padtemplate_add_pad (GstEditorPadTemplate *padtemplate,
                                GstPad *pad) 
{
  GstEditorPad *editorpad;
  g_print ("gsteditorpadtemplate: add pad\n"); 

  editorpad = gst_editor_pad_new (padtemplate->parent, pad, NULL);

  padtemplate->pads = g_list_prepend (padtemplate->pads, editorpad);
}

static void 
gst_editor_padtemplate_set_arg (GtkObject *object,GtkArg *arg,guint id) 
{
  GstEditorPadTemplate *padtemplate;

  /* get the major types of this object */
  padtemplate = GST_EDITOR_PADTEMPLATE(object);

  switch (id) {
    case ARG_X:
      padtemplate->x = GTK_VALUE_DOUBLE(*arg);
      break;
    case ARG_Y:
      padtemplate->y = GTK_VALUE_DOUBLE(*arg);
      break;
    case ARG_WIDTH:
      padtemplate->width = GTK_VALUE_DOUBLE(*arg);
      padtemplate->resize = TRUE;
      break;
    case ARG_HEIGHT:
      padtemplate->height = GTK_VALUE_DOUBLE(*arg);
      padtemplate->resize = TRUE;
      break;
    case ARG_PADTEMPLATE:
      /* FIXME: this is very brute force */
      padtemplate->padtemplate = GTK_VALUE_POINTER(*arg);
      break;
    default:
      g_warning("gsteditorpadtemplate: unknown arg!");
      break;
  }
}

static void 
gst_editor_padtemplate_get_arg (GtkObject *object,GtkArg *arg,guint id) 
{
  GstEditorPadTemplate *padtemplate;

  /* get the major types of this object */
  padtemplate = GST_EDITOR_PADTEMPLATE(object);

  switch (id) {
    case ARG_X:
      GTK_VALUE_INT(*arg) = padtemplate->x;
      break;
    case ARG_Y:
      GTK_VALUE_INT(*arg) = padtemplate->y;
      break;
    case ARG_WIDTH:
      GTK_VALUE_INT(*arg) = padtemplate->width;
      break;
    case ARG_HEIGHT:
      GTK_VALUE_INT(*arg) = padtemplate->height;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

static void 
gst_editor_padtemplate_realize (GstEditorPadTemplate *padtemplate) 
{
  g_print("realizing editor padtemplate %p\n",padtemplate);

  /* we must be attached to an element */
  g_return_if_fail(padtemplate->parent != NULL);

  /* create the group and bounding box */
  padtemplate->group = GNOME_CANVAS_GROUP(gnome_canvas_item_new(padtemplate->parent->group,
    gnome_canvas_group_get_type(),"x",padtemplate->x,"y",padtemplate->y,NULL));
  g_return_if_fail(padtemplate->group != NULL);
  GST_EDITOR_SET_OBJECT(padtemplate->group,padtemplate);

  padtemplate->border = gnome_canvas_item_new(padtemplate->group,
    gnome_canvas_rect_get_type(),
    "width_units",1.0,"fill_color_rgba", 0xFFCCCC00,"outline_color","black", 
    "x1",0.0,"y1",0.0,"x2",padtemplate->width,"y2",padtemplate->height,NULL);
  g_return_if_fail(padtemplate->border != NULL);
  GST_EDITOR_SET_OBJECT(padtemplate->border,padtemplate);

  /* create the padtemplate box on the correct side */
  padtemplate->issrc = (padtemplate->padtemplate->direction == GST_PAD_SRC);
  if (padtemplate->issrc)
    padtemplate->padtemplatebox = gnome_canvas_item_new(padtemplate->group,
      gnome_canvas_rect_get_type(),
      "width_units",1.0,"fill_color","white","outline_color","black",
      "x1",padtemplate->x-4.0,"y1",2.0,"x2",padtemplate->x,"y2",padtemplate->height-2.0,NULL);
  else
    padtemplate->padtemplatebox = gnome_canvas_item_new(padtemplate->group,
      gnome_canvas_rect_get_type(),
      "width_units",1.0,"fill_color","white","outline_color","black",
      "x1",0.0,"y1",2.0,"x2",4.0,"y2",padtemplate->height-2.0,NULL);
  g_return_if_fail(padtemplate->padtemplatebox != NULL);
  GST_EDITOR_SET_OBJECT(padtemplate->padtemplatebox,padtemplate);

  gtk_signal_connect(GTK_OBJECT(padtemplate->group),"event",
    GTK_SIGNAL_FUNC(gst_editor_padtemplate_padbox_event),padtemplate);

  padtemplate->title = gnome_canvas_item_new(padtemplate->group,
    gnome_canvas_text_get_type(),
    "text",padtemplate->padtemplate->name_template,
    "x",0.0,"y",0.0,"anchor",GTK_ANCHOR_NORTH_WEST,
    "font_gdk",gtk_widget_get_default_style()->font,
    NULL);
  g_return_if_fail(padtemplate->title != NULL);
  GST_EDITOR_SET_OBJECT(padtemplate->title,padtemplate);

  padtemplate->realized = TRUE;
  padtemplate->resize = TRUE;
  gst_editor_padtemplate_repack(padtemplate);
}


static void 
gst_editor_padtemplate_resize (GstEditorPadTemplate *padtemplate) 
{
  gdouble minwidth,minheight;

//  g_print("resizing padtemplate\n");

  minwidth = 0;minheight = 0;

  /* get the text size and add it into minsize */
  minwidth = gst_util_get_double_arg(GTK_OBJECT(padtemplate->title),
                                     "text_width") + 2.0;
  minheight = gst_util_get_double_arg(GTK_OBJECT(padtemplate->title),
                                      "text_height");

  /* calculate the size of the padtemplatebox */
  padtemplate->boxheight = minheight - 4.0;
  padtemplate->boxwidth = padtemplate->boxheight / 2.0;
  minwidth += padtemplate->boxwidth;

  /* force the thing to grow if necessary */
  padtemplate->width = MAX(padtemplate->width,minwidth);
  padtemplate->height = MAX(padtemplate->height,minheight);

  /* update the connection if there is one */
//  g_print("connection is %p\n",padtemplate->connection);
  if (padtemplate->connection != NULL)
    gst_editor_connection_resize(padtemplate->connection);
}

void 
gst_editor_padtemplate_repack (GstEditorPadTemplate *padtemplate) 
{
  gdouble x1,y1,x2,y2;
  GList *pads;

  if (!padtemplate->realized) return;

  gst_editor_padtemplate_resize(padtemplate);

  x1 = 0;y1 = 0;
  x2 = x1 + padtemplate->width;y2 = y1 + padtemplate->height;
//  g_print("repacking padtemplate at %.2fx%.2f %.2fx%.2f - %.2fx%.2f\n",padtemplate->x, padtemplate->y,x1,y1,x2,y2);

  /* move the group */
  gtk_object_set(GTK_OBJECT(padtemplate->group),"x",padtemplate->x,"y",padtemplate->y,NULL);

  /* start by resizing the bordering box */
  gtk_object_set(GTK_OBJECT(padtemplate->border),
                 "x1",x1,"y1",y1,"x2",x2,"y2",y2,NULL);

  /* if we're a left-jusified sink */
  if (padtemplate->issrc) {
    /* and move the padtemplate box */
    gtk_object_set(GTK_OBJECT(padtemplate->padtemplatebox),
                   "x1",x2-padtemplate->boxwidth,"y1",y1+2.0,
                   "x2",x2,"y2",y2-2.0,NULL);
    /* then move the text to the right place */
    gtk_object_set(GTK_OBJECT(padtemplate->title),
                   "x",x2-padtemplate->boxwidth-1.0,"y",y1,
                   "anchor",GTK_ANCHOR_NORTH_EAST,
                   NULL);
  } else {
    /* and move the padtemplate box */
    gtk_object_set(GTK_OBJECT(padtemplate->padtemplatebox),
                   "x1",x1,"y1",y1+2.0,
                   "x2",x1+padtemplate->boxwidth,"y2",y2-2.0,NULL);
    /* then move the text to the right place */
    gtk_object_set(GTK_OBJECT(padtemplate->title),
                   "x",x1+padtemplate->boxwidth+1.0,"y",y1,
                   "anchor",GTK_ANCHOR_NORTH_WEST,
                   NULL);
  }

  pads = padtemplate->pads;
  while (pads) {
    GstEditorPad *pad = GST_EDITOR_PAD(pads->data);

    if (!strcmp (gst_pad_get_name(pad->pad), padtemplate->padtemplate->name_template)) {
      gtk_object_set(GTK_OBJECT(pad),"x",padtemplate->x,"y",padtemplate->y,NULL);
    }
    else {
      gtk_object_set(GTK_OBJECT(pad),"x",padtemplate->x,"y",padtemplate->y+y2,NULL);
    }
    gst_editor_pad_repack (pad);
    
    pads = g_list_next (pads);
  }

  if (padtemplate->connection != NULL) {
    padtemplate->connection->resize = TRUE;
    gst_editor_connection_resize(padtemplate->connection);
  }

  padtemplate->resize = FALSE;
}


/*
static gint gst_editor_padtemplate_event(GnomeCanvasItem *item,GdkEvent *event) {
  GstEditorPadTemplate *padtemplate = GST_EDITOR_PAD(item);
  gdouble item_x,item_y;
  GdkCursor *fleur;
  gdouble tx,ty;

  item_x = event->button.x;
  item_y = event->button.y;
  gnome_canvas_item_w2i(item->parent,&item_x,&item_y);

  switch(event->type) {
    case GDK_ENTER_NOTIFY:
//      g_print("entered padtemplate\n");
      break;
    case GDK_LEAVE_NOTIFY:
//      g_print("left padtemplate\n");
      break;
    default:
      break;
  }
  return FALSE;
}
*/

/* FIXME FIXME FIXME */
static gint 
gst_editor_padtemplate_padbox_event(GnomeCanvasItem *item,
                            GdkEvent *event,
                            GstEditorPadTemplate *padtemplate) 
{
  GstEditorElement *element;
  GstEditorBin *bin;

//  g_print("padtemplatebox has event %d\n",event->type);
  g_return_val_if_fail(GST_IS_EDITOR_PADTEMPLATE(padtemplate), FALSE);

  element = padtemplate->parent;
  bin = element->parent;

  switch(event->type) {
    case GDK_ENTER_NOTIFY:
      gtk_object_set(GTK_OBJECT(padtemplate->border),
                 "fill_color_rgba", 0xDDBBBB00, NULL);
//      g_print("entered padtemplate '%s'\n",
//              gst_padtemplate_get_name(padtemplate->padtemplate));
      break;
    case GDK_LEAVE_NOTIFY:
      gtk_object_set(GTK_OBJECT(padtemplate->border),
                 "fill_color_rgba", 0xFFCCCC00, NULL);
//      g_print("left padtemplate '%s'\n",
//              gst_padtemplate_get_name(padtemplate->padtemplate));
      break;
    case GDK_BUTTON_PRESS:
//      g_print("have button press in padtemplate '%s'\n",
//              gst_padtemplate_get_name(padtemplate->padtemplate));
      //gst_editor_bin_start_banding(bin,padtemplate);
      return TRUE;
      break;
    case GDK_MOTION_NOTIFY:
//      g_print("have motion in padtemplate\n");
      break;
    default:
      break;
  }
  return FALSE;
}
