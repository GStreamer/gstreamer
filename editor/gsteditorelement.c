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
#include <gst/gstutils.h>

#include "gsteditor.h"
#include "gsteditorproject.h"
#include "gsteditorproperty.h"

/* class functions */
static void gst_editor_element_class_init(GstEditorElementClass *klass);
static void gst_editor_element_init(GstEditorElement *element);
static void gst_editor_element_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_editor_element_get_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_editor_element_realize(GstEditorElement *element);
static gint gst_editor_element_event(GnomeCanvasItem *item,
                                     GdkEvent *event,
                                     GstEditorElement *element);

/* events fired by items within self */
static gint gst_editor_element_resizebox_event(GnomeCanvasItem *item,
                                               GdkEvent *event,
                                               GstEditorElement *element);
static gint gst_editor_element_group_event(GnomeCanvasItem *item,
                                           GdkEvent *event,
                                           GstEditorElement *element);
static gint gst_editor_element_state_event(GnomeCanvasItem *item,
                                           GdkEvent *event,
                                           gpointer data);

/* external events (from GstElement) */
static void gst_editor_element_state_change(GstElement *element,
                                            gint state,
                                            GstEditorElement *editorelement);

/* utility functions */
static void gst_editor_element_resize(GstEditorElement *element);
static void gst_editor_element_set_state(GstEditorElement *element,
                                         gint id,gboolean set);
static void gst_editor_element_sync_state(GstEditorElement *element);
static void gst_editor_element_move(GstEditorElement *element,
                                    gdouble dx,gdouble dy);


static gchar *_gst_editor_element_states[] = { "S","R","P","F" };

static GstElementState _gst_element_states[] = {
  GST_STATE_NULL,
  GST_STATE_READY,
  GST_STATE_PLAYING,
  GST_STATE_PAUSED,
};

enum {
  ARG_0,
  ARG_X,
  ARG_Y,
  ARG_WIDTH,  
  ARG_HEIGHT,
  ARG_X1,
  ARG_Y1,
  ARG_X2,
  ARG_Y2,
  ARG_ELEMENT,
};

enum {
  NAME_CHANGED,
  LAST_SIGNAL
};

static GtkObjectClass *parent_class;
static guint gst_editor_element_signals[LAST_SIGNAL] = { 0 };

GtkType gst_editor_element_get_type() {
  static GtkType element_type = 0;

  if (!element_type) {
    static const GtkTypeInfo element_info = {
      "GstEditorElement",
      sizeof(GstEditorElement),
      sizeof(GstEditorElementClass),
      (GtkClassInitFunc)gst_editor_element_class_init,
      (GtkObjectInitFunc)gst_editor_element_init,
      NULL,
      NULL,
      (GtkClassInitFunc)NULL,
    };
    element_type = gtk_type_unique(gtk_object_get_type(),&element_info);
  }
  return element_type;
}

static void gst_editor_element_class_init(GstEditorElementClass *klass) {
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(gtk_object_get_type());

  gst_editor_element_signals[NAME_CHANGED] =
    gtk_signal_new("name_changed",GTK_RUN_FIRST,object_class->type,
                   GTK_SIGNAL_OFFSET(GstEditorElementClass,name_changed),
                   gtk_marshal_NONE__POINTER,GTK_TYPE_NONE,1,
                   GST_TYPE_EDITOR_ELEMENT);

  gtk_object_class_add_signals(object_class,gst_editor_element_signals,LAST_SIGNAL);

  gtk_object_add_arg_type("GstEditorElement::x",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE|GTK_ARG_CONSTRUCT_ONLY,
                          ARG_X);
  gtk_object_add_arg_type("GstEditorElement::y",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE|GTK_ARG_CONSTRUCT_ONLY,
                          ARG_Y);
  gtk_object_add_arg_type("GstEditorElement::width",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE|GTK_ARG_CONSTRUCT_ONLY,
                          ARG_WIDTH);
  gtk_object_add_arg_type("GstEditorElement::height",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE|GTK_ARG_CONSTRUCT_ONLY,
                          ARG_HEIGHT);
  gtk_object_add_arg_type("GstEditorElement::x1",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE,ARG_X1);
  gtk_object_add_arg_type("GstEditorElement::y1",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE,ARG_Y1);
  gtk_object_add_arg_type("GstEditorElement::x2",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE,ARG_X2);
  gtk_object_add_arg_type("GstEditorElement::y2",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE,ARG_Y2);
  gtk_object_add_arg_type("GstEditorElement::element",GTK_TYPE_POINTER,
                          GTK_ARG_READABLE,ARG_ELEMENT);

  klass->realize = gst_editor_element_realize;
  klass->event = gst_editor_element_event;

  object_class->set_arg = gst_editor_element_set_arg;
  object_class->get_arg = gst_editor_element_get_arg;
}

static void gst_editor_element_init(GstEditorElement *element) {
}

GstEditorElement *gst_editor_element_new(GstEditorBin *parent,
                                         GstElement *element,
                                         const gchar *first_arg_name, ...) {
  GstEditorElement *editorelement;
  va_list args;

  g_return_val_if_fail(parent != NULL, NULL);
  g_return_val_if_fail(GST_IS_EDITOR_BIN(parent), NULL);
  g_return_val_if_fail(element != NULL, NULL);
  g_return_val_if_fail(GST_IS_ELEMENT(element), NULL);

  editorelement = GST_EDITOR_ELEMENT(gtk_type_new(GST_TYPE_EDITOR_ELEMENT));
  editorelement->element = element;

  va_start(args,first_arg_name);
  gst_editor_element_construct(editorelement,parent,first_arg_name,args);
  va_end(args);

  return editorelement;
}

void gst_editor_element_set_name(GstEditorElement *element,
                                  const gchar *name) {
  g_return_if_fail(GST_IS_EDITOR_ELEMENT(element));
  g_return_if_fail(name != NULL);

  gst_element_set_name(element->element, (gchar *)name);
  gnome_canvas_item_set(element->title, "text", name, NULL);
  gtk_signal_emit(GTK_OBJECT(element),gst_editor_element_signals[NAME_CHANGED], element);
}

const gchar *gst_editor_element_get_name(GstEditorElement *element) {
  g_return_val_if_fail(GST_IS_EDITOR_ELEMENT(element), NULL);

  return gst_element_get_name(element->element);
}

void gst_editor_element_construct(GstEditorElement *element,
                                  GstEditorBin *parent,
                                  const gchar *first_arg_name,
                                  va_list args) {
  GtkObject *obj = GTK_OBJECT(element);
  GSList *arg_list = NULL, *info_list = NULL;
  gchar *error;
  GstEditorElementClass *elementclass;

//  g_print("in gst_editor_element_construct()\n");

  error = gtk_object_args_collect(GTK_OBJECT_TYPE(obj),&arg_list,
                                  &info_list,first_arg_name,args);
  if (error) {
    g_warning("gst_editor_element_construct(): %s",error);
    g_free(error);
  } else {
    GSList *arg,*info;
//    g_print("setting all the arguments on the element\n");
    for (arg=arg_list,info=info_list;arg;arg=arg->next,info=info->next)
      gtk_object_arg_set(obj,arg->data,info->data);
    gtk_args_collect_cleanup(arg_list,info_list);
  }

  if (parent)
    gst_editor_bin_add(parent,element);
  else if (!GST_IS_EDITOR_BIN(element))
    g_warning("floating element...\n");

  elementclass = GST_EDITOR_ELEMENT_CLASS(GTK_OBJECT(element)->klass);
  if (elementclass->realize)
    (elementclass->realize)(element);
}

static void gst_editor_element_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstEditorElement *element;

  /* get the major types of this object */
  element = GST_EDITOR_ELEMENT(object);

  switch (id) {
    case ARG_X:
      element->x = GTK_VALUE_DOUBLE(*arg);
      break;
    case ARG_Y:
      element->y = GTK_VALUE_DOUBLE(*arg);
      break;
    case ARG_WIDTH:
      element->width = GTK_VALUE_DOUBLE(*arg);
      element->resize = TRUE;
      break;
    case ARG_HEIGHT:
      element->height = GTK_VALUE_DOUBLE(*arg);
      element->resize = TRUE;
      break;
    case ARG_X1:
      element->x = GTK_VALUE_DOUBLE(*arg);
      element->resize = TRUE;
      break;
    case ARG_Y1:
      element->y = GTK_VALUE_DOUBLE(*arg);
      element->resize = TRUE;
      break;
    case ARG_X2:
      // make sure it's big enough, grow if not
      element->width = MAX(GTK_VALUE_DOUBLE(*arg),element->minwidth);
      element->resize = TRUE;
      break;
    case ARG_Y2:
      // make sure it's big enough, grow if not
      element->height = MAX(GTK_VALUE_DOUBLE(*arg),element->minheight);
      element->resize = TRUE;
      break;
    default:
      g_warning("gsteditorelement: unknown arg!");
      break;
  }
}

static void gst_editor_element_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstEditorElement *element;

  /* get the major types of this object */
  element = GST_EDITOR_ELEMENT(object);

  switch (id) {
    case ARG_X:
      GTK_VALUE_INT(*arg) = element->x + (element->width / 2.0);
      break;
    case ARG_Y:
      GTK_VALUE_INT(*arg) = element->y + (element->height / 2.0);
      break;
    case ARG_WIDTH:
      GTK_VALUE_INT(*arg) = element->width;
      break;
    case ARG_HEIGHT:
      GTK_VALUE_INT(*arg) = element->height;
      break;
    case ARG_X1:
      GTK_VALUE_INT(*arg) = element->x;
      break;
    case ARG_Y1:
      GTK_VALUE_INT(*arg) = element->y;
      break;
    case ARG_X2:
      GTK_VALUE_INT(*arg) = element->x + element->width;
      break;
    case ARG_Y2:
      GTK_VALUE_INT(*arg) = element->y + element->height;
      break;
    case ARG_ELEMENT:
      GTK_VALUE_POINTER(*arg) = element->element;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

static void gst_editor_element_realize(GstEditorElement *element) {
  GnomeCanvasGroup *parentgroup;
  gint i;
  gdouble x1,y1,x2,y2;
  GList *pads;
  GstPad *pad;

//  g_print("realizing editor element %p\n",element);

  /* we have to have a parent by this point */
  g_return_if_fail(element->parent != NULL);

  // set the state signal of the actual element
  gtk_signal_connect(GTK_OBJECT(element->element),"state_change",
                     GTK_SIGNAL_FUNC(gst_editor_element_state_change),
                     element);

  // create the bounds if we haven't had them set
//  g_print("centering element at %.2fx%.2f (%.2fx%.2f)\n",
//          element->x,element->y,element->width,element->height);

  /* create the group holding all the stuff for this element */
  parentgroup = GST_EDITOR_ELEMENT(element->parent)->group;
  element->group = GNOME_CANVAS_GROUP(gnome_canvas_item_new(parentgroup,
    gnome_canvas_group_get_type(),
    "x",element->x - (element->width / 2.0),
    "y",element->y - (element->height / 2.0),NULL));
//  g_print("origin of group is %.2fx%.2f\n",
//          element->x - (element->width / 2.0),
//          element->y - (element->height / 2.0));
  g_return_if_fail(element->group != NULL);
  GST_EDITOR_SET_OBJECT(element->group,element);
  gtk_signal_connect(GTK_OBJECT(element->group),"event",
    GTK_SIGNAL_FUNC(gst_editor_element_group_event),element);

  // calculate the inter-group coords (x1,y1,x2,y2 are convenience vars)
  x1 = 0.0;y1 = 0.0;
  x2 = element->width;y2 = element->height;

  /* create bordering box */
  element->border = gnome_canvas_item_new(element->group,
    gnome_canvas_rect_get_type(),
    "width_units",2.0,"fill_color","white","outline_color","black", 
    "x1",x1,"y1",y1,"x2",x2,"y2",y2,NULL);
  g_return_if_fail(element->border != NULL);
  GST_EDITOR_SET_OBJECT(element->border,element);

  /* create resizing box */
  element->resizebox = gnome_canvas_item_new(element->group,
    gnome_canvas_rect_get_type(),
    "width_units",1.0,"fill_color","white","outline_color","black",
    "x1",x2-4.0,"y1",y2-4.0,"x2",x2,"y2",y2,NULL);
  g_return_if_fail(element->resizebox != NULL);
  GST_EDITOR_SET_OBJECT(element->resizebox,element);
  gtk_signal_connect(GTK_OBJECT(element->resizebox),"event",
    GTK_SIGNAL_FUNC(gst_editor_element_resizebox_event),element);

  /* create the title */
  element->title = gnome_canvas_item_new(element->group,
    gnome_canvas_text_get_type(),
    "text",gst_element_get_name(GST_ELEMENT(element->element)),
    "x",x1+1.0,"y",y1+1.0,"anchor",GTK_ANCHOR_NORTH_WEST,
    "font_gdk",gtk_widget_get_default_style()->font,
    NULL);
  g_return_if_fail(element->title != NULL);
  GST_EDITOR_SET_OBJECT(element->title,element);

  /* create the state boxen */
  for (i=0;i<4;i++) {
    element->statebox[i] = gnome_canvas_item_new(element->group,
      gnome_canvas_rect_get_type(),
      "width_units",1.0,"fill_color","white","outline_color","black",
      "x1",0.0,"y1",0.0,"x2",0.0,"y2",0.0,
      NULL);
    g_return_if_fail(element->statebox[i] != NULL);

    GST_EDITOR_SET_OBJECT(element->statebox[i],element);
    gtk_signal_connect(GTK_OBJECT(element->statebox[i]),"event",
                       GTK_SIGNAL_FUNC(gst_editor_element_state_event),
                       GINT_TO_POINTER(i));
    element->statetext[i] = gnome_canvas_item_new(element->group,
      gnome_canvas_text_get_type(),
      "text",_gst_editor_element_states[i],
      "x",0.0,"y",0.0,"anchor",GTK_ANCHOR_NORTH_WEST,
      "font","-*-*-*-*-*-*-6-*-*-*-*-*-*-*",
      NULL);
    g_return_if_fail(element->statetext[i] != NULL);
    GST_EDITOR_SET_OBJECT(element->statetext[i],element);
    gtk_signal_connect(GTK_OBJECT(element->statetext[i]),"event",
                       GTK_SIGNAL_FUNC(gst_editor_element_state_event),
                       GINT_TO_POINTER(i));
  }
  // get all the pads
  pads = gst_element_get_pad_list(element->element);
  while (pads) {
    pad = GST_PAD(pads->data);
    gst_editor_element_add_pad(element,pad);
    pads = g_list_next(pads);
  }

  element->realized = TRUE;

  // force a resize
  element->resize = TRUE;
  gst_editor_element_resize(element);

  // recenter things on the supposed center
//  g_print("recentering element at %.2fx%.2f (%.2fx%.2f)\n",
//          element->x,element->y,element->width,element->height);
  element->x -= (element->width / 2.0);
  element->y -= (element->height / 2.0);
  gnome_canvas_item_set(GNOME_CANVAS_ITEM(element->group),
    "x",element->x,"y",element->y,NULL);
//  g_print("origin of group is %.2fx%.2f\n",element->x,element->y);

  gst_editor_element_repack(element);
}


static void gst_editor_element_resize(GstEditorElement *element) {
  gdouble itemwidth,itemheight;
  gdouble groupwidth,groupheight;
  GList *pads;
  GstEditorPad *editorpad;
  gint i;

  if (element->resize != TRUE) return;
  element->resize = FALSE;

//  g_print("resizing element\n");

  element->minwidth = element->insidewidth;
  element->minheight = element->insideheight;

  // get the text size and add it into minsize
  g_return_if_fail(element->title != NULL);
  itemwidth = gst_util_get_double_arg(GTK_OBJECT(element->title),
                                      "text_width") + 2.0;
  itemheight = gst_util_get_double_arg(GTK_OBJECT(element->title),
                                       "text_height") + 2.0;

  element->titlewidth = itemwidth;
  element->titleheight = itemheight;
  element->minwidth = MAX(element->minwidth,itemwidth);
  element->minheight += itemheight;

  // now do the bottom bar
  // find the biggest of the state chars
  element->statewidth = 0.0;element->stateheight = 0.0;
  for (i=0;i<4;i++) {
    g_return_if_fail(element->statetext[i] != NULL);

    itemwidth = 16.0;
    itemheight = 16.0;

    element->statewidth = MAX(element->statewidth,itemwidth);
    element->stateheight = MAX(element->stateheight,itemheight);
  }
  // calculate the size of the primary group
  groupwidth = element->statewidth * 5;	// 4 states plus playstate
  groupheight = element->stateheight;
  // add in the resize box
  groupwidth += 7.0;		// 2.0 for buffer, 5.0 for actual size
  groupheight = MAX(groupheight,5.0);
  // update the minsize
  element->minwidth = MAX(element->minwidth,groupwidth);
  element->minheight += groupheight;

  // now go and try to calculate necessary space for the pads
  element->sinkwidth = 10.0;element->sinkheight = 0.0;element->sinks = 0;
  pads = element->sinkpads;
  while (pads) {
    editorpad = GST_EDITOR_PAD(pads->data);
    element->sinkwidth = MAX(element->sinkwidth,editorpad->width);
    element->sinkheight = MAX(element->sinkheight,editorpad->height);
    element->sinks++;
    pads = g_list_next(pads);
  }
  element->srcwidth = 10.0;element->srcheight = 0.0;element->srcs = 0;
  pads = element->srcpads;
  while (pads) {
    editorpad = GST_EDITOR_PAD(pads->data);
    element->srcwidth = MAX(element->srcwidth,editorpad->width);
    element->srcheight = MAX(element->srcheight,editorpad->height);
    element->srcs++;
    pads = g_list_next(pads);
  }
  // add in the needed space
  element->minheight += MAX((element->sinkheight*element->sinks),
                            (element->srcheight*element->srcs)) + 4.0;
  element->minwidth = MAX(element->minwidth,
                          ((element->sinkwidth*element->sinks) +
                           (element->srcwidth*element->srcs) + 4.0));
//  g_print("have %d sinks (%.2fx%.2f) and %d srcs (%.2fx%.2f)\n",
//          element->sinks,element->sinkwidth,element->sinkheight,
//          element->srcs,element->srcwidth,element->srcheight);

  // grow the element to hold all the stuff
//  g_print("minsize is %.2fx%.2f,
//",element->minwidth,element->minheight);
//  g_print("size was %.2fx%.2f, ",element->width,element->height);
  element->width = MAX(element->width,element->minwidth);
  element->height = MAX(element->height,element->minheight);
//  g_print("is now %.2fx%.2f\n",element->width,element->height);
}

void gst_editor_element_repack(GstEditorElement *element) {
  GList *pads;
  GstEditorPad *editorpad;
  gint sinks;
  gint srcs;
  gdouble x1,y1,x2,y2;
  gint i;

  if (!element->realized) return;

  gst_editor_element_resize(element);

  // still use x1,y1,x2,y2 so we can change around later
  x1 = 0.0;y1 = 0.0;
  x2 = element->width;y2 = element->height;

//  g_print("repacking element at %.2fx%.2f + %.2fx%.2f\n",
//          element->x,element->y,x2,y2);

  // move the element group to match
  gnome_canvas_item_set(GNOME_CANVAS_ITEM(element->group),
                        "x",element->x,"y",element->y,NULL);

  // start by resizing the bordering box
  g_return_if_fail(element->border != NULL);
  gtk_object_set(GTK_OBJECT(element->border),
                 "x1",x1,"y1",y1,"x2",x2,"y2",y2,NULL);

  // then move the text to the new top left
  g_return_if_fail(element->title != NULL);
  gtk_object_set(GTK_OBJECT(element->title),
                 "x",x1+1.0,"y",y1+1.0,
                 "anchor",GTK_ANCHOR_NORTH_WEST,
                 NULL);

  // and move the resize box
  g_return_if_fail(element->resizebox != NULL);
  gtk_object_set(GTK_OBJECT(element->resizebox),
                 "x1",x2-5.0,"y1",y2-5.0,"x2",x2,"y2",y2,NULL);

  // now place the state boxes
  for (i=0;i<4;i++) {
    g_return_if_fail(element->statebox[i] != NULL);
    gtk_object_set(GTK_OBJECT(element->statebox[i]),
                   "x1",x1+(element->statewidth*i),
                   "y1",y2-element->stateheight,
                   "x2",x1+(element->statewidth*(i+1)),"y2",y2,NULL);
    g_return_if_fail(element->statetext[i] != NULL);
    gtk_object_set(GTK_OBJECT(element->statetext[i]),
                   "x",x1+(element->statewidth*i)+2.0,
                   "y",y2-element->stateheight+1.0,
                   "anchor",GTK_ANCHOR_NORTH_WEST,NULL);
  }
  gst_editor_element_sync_state(element);

  // now we try to place all the pads
  sinks = element->sinks;
  pads = element->sinkpads;
  while (pads) {
    editorpad = GST_EDITOR_PAD(pads->data);
    gtk_object_set(GTK_OBJECT(editorpad),
                   "x",x1,
                   "y",y2 - 2.0 - element->stateheight - 
                       (element->sinkheight * sinks),
                   NULL);
    gst_editor_pad_repack(editorpad);
    sinks--;
    pads = g_list_next(pads);
  }

  srcs = element->srcs;
  pads = element->srcpads;
  while (pads) {
    editorpad = GST_EDITOR_PAD(pads->data);
    gtk_object_set(GTK_OBJECT(editorpad),
                   "x",x2 - element->srcwidth,
                   "y",y2 - 2.0 - element->stateheight -
                       (element->srcheight * srcs),
                   NULL);
    gst_editor_pad_repack(editorpad);
    srcs--;
    pads = g_list_next(pads);
  }

//  g_print("done resizing element\n");
}


GstEditorPad *gst_editor_element_add_pad(GstEditorElement *element,
                                         GstPad *pad) {
  GstEditorPad *editorpad;

  editorpad = gst_editor_pad_new(element,pad,NULL);
  if (pad->direction == GST_PAD_SINK) {
    element->sinkpads = g_list_prepend(element->sinkpads,editorpad);
    element->sinks++;
//    g_print("added 'new' pad to sink list\n");
  } else if (pad->direction == GST_PAD_SRC) {
    element->srcpads = g_list_prepend(element->srcpads,editorpad);
    element->srcs++;
//    g_print("added 'new' pad to src list\n");
  } else
    g_print("HUH?!?  Don't know which direction this pad is...\n");

  element->padlistchange = TRUE;
  gst_editor_element_repack(element);
  return editorpad;
}


static gint gst_editor_element_group_event(GnomeCanvasItem *item,
                                           GdkEvent *event,
                                           GstEditorElement *element) {
  switch(event->type) {
    case GDK_BUTTON_PRESS:
      gst_editor_property_show(gst_editor_property_get(), element);
      break;
    default:
      break;
  }

  if (GST_EDITOR_ELEMENT_CLASS(GTK_OBJECT(element)->klass)->event)
    return (GST_EDITOR_ELEMENT_CLASS(GTK_OBJECT(element)->klass)->event)(
      item,event,element);
  return FALSE;
}


static gint gst_editor_element_event(GnomeCanvasItem *item,GdkEvent *event,
                                     GstEditorElement *element) {
  gdouble dx,dy;
  GdkCursor *fleur;

//  g_print("element in event, type %d\n",event->type);

  switch(event->type) {
    case GDK_ENTER_NOTIFY:
      break;
    case GDK_LEAVE_NOTIFY:
      break;
    case GDK_BUTTON_PRESS:
      // dragxy coords are world coords of button press
      element->dragx = event->button.x;
      element->dragy = event->button.y;
      // set some flags
      element->dragging = TRUE;
      element->moved = FALSE;
      fleur = gdk_cursor_new(GDK_FLEUR);
      gnome_canvas_item_grab(item,
                             GDK_POINTER_MOTION_MASK |
//                             GDK_ENTER_NOTIFY_MASK |
//                             GDK_LEAVE_NOTIFY_MASK |
                             GDK_BUTTON_RELEASE_MASK,
                             fleur,event->button.time);
      return TRUE;
      break;
    case GDK_MOTION_NOTIFY:
      if (element->dragging) {
        dx = event->button.x - element->dragx;
        dy = event->button.y - element->dragy;
        gst_editor_element_move(element,dx,dy);
        element->dragx = event->button.x;
        element->dragy = event->button.y;
        element->moved = TRUE;
      }
      return TRUE;
      break;
    case GDK_BUTTON_RELEASE:
      if (element->dragging) {
        element->dragging = FALSE;
        gnome_canvas_item_ungrab(item,event->button.time);
      }
      if (!element->moved) {
        GstEditorElementClass *elementclass;
        elementclass = GST_EDITOR_ELEMENT_CLASS(GTK_OBJECT(element)->klass);
        if (elementclass->button_event)
          (elementclass->button_event)(item,event,element);
      }
//g_print("in element group_event, setting inchild");
      element->canvas->inchild = TRUE;
      return TRUE;
      break;

    default:
      break;
  }
  return FALSE;
}


static gint gst_editor_element_resizebox_event(GnomeCanvasItem *item,
                                               GdkEvent *event,
                                               GstEditorElement *element) {
  GdkCursor *bottomright;
  gdouble item_x,item_y;

//  g_print("in resizebox_event...\n");

  // calculate coords relative to the group, not the box
  item_x = event->button.x;
  item_y = event->button.y;
  gnome_canvas_item_w2i(item->parent,&item_x,&item_y);

  switch(event->type) {
    case GDK_ENTER_NOTIFY:
      break;
    case GDK_LEAVE_NOTIFY:
      element->hesitating = FALSE;
      break;
    case GDK_BUTTON_PRESS:
      element->dragx = event->button.x;
      element->dragy = event->button.y;
      element->resizing = TRUE;
      element->hesitating = TRUE;
      bottomright = gdk_cursor_new(GDK_BOTTOM_RIGHT_CORNER);
      gnome_canvas_item_grab(item,
                             GDK_POINTER_MOTION_MASK |
                             GDK_ENTER_NOTIFY_MASK |
                             GDK_LEAVE_NOTIFY_MASK |
                             GDK_BUTTON_RELEASE_MASK,
                             bottomright,event->button.time);
      return TRUE;
      break;
    case GDK_MOTION_NOTIFY:
      if (element->resizing) {
        // doing a set because the code is in the arg set code
//        g_print("resizing to x2,y2 of %.2f,%.2f\n",item_x,item_y);
        gtk_object_set(GTK_OBJECT(element),"x2",item_x,"y2",item_y,NULL);
        element->resize = TRUE;
        gst_editor_element_repack(element);
        return TRUE;
      }
      break;
    case GDK_BUTTON_RELEASE:
      if (element->resizing) {
        element->resizing = FALSE;
        gnome_canvas_item_ungrab(item,event->button.time);
//g_print("in element resizebox_event, setting inchild");
        element->canvas->inchild = TRUE;
        return TRUE;
      }
      break;
    default:
      break;
  }
  return FALSE;
}


static gint gst_editor_element_state_event(GnomeCanvasItem *item,
                                           GdkEvent *event,
                                           gpointer data) {
  GstEditorElement *element;
  gint id = GPOINTER_TO_INT(data);
  GdkCursor *uparrow;

  element = GST_EDTIOR_GET_OBJECT(item);

  switch (event->type) {
    case GDK_ENTER_NOTIFY:
      uparrow = gdk_cursor_new(GDK_SB_UP_ARROW);
      gnome_canvas_item_grab(item,
                             GDK_POINTER_MOTION_MASK |
                             GDK_BUTTON_RELEASE_MASK |
                             GDK_LEAVE_NOTIFY_MASK,
                             uparrow,event->button.time);
      /* NOTE: when grabbing canvas item, always get pointer_motion,
         this will allow you to actually get all the other synth events */
      break;
    case GDK_LEAVE_NOTIFY:
      gnome_canvas_item_ungrab(item,event->button.time);
      break;
    case GDK_BUTTON_PRESS:
      return TRUE;
      break;
    case GDK_BUTTON_RELEASE:
      if (id < 4) {
        gst_editor_element_set_state(element,id,TRUE);
      } else
        g_warning("Uh, shouldn't have gotten here, unknown state\n");
//g_print("in element statebox_event, setting inchild");
      element->canvas->inchild = TRUE;
      return TRUE;
      break;
    default:
      break;
  }
  return FALSE;
}


static void gst_editor_element_set_state(GstEditorElement *element,
                                         gint id,gboolean set) {
  gboolean stateset = TRUE;	/* if we have no element, set anyway */
  //g_print("element set state %d\n", id);
  if (element->element)
    stateset = gst_element_set_state(element->element,_gst_element_states[id]);
}


static void gst_editor_element_state_change(GstElement *element,
                                            gint state,
                                            GstEditorElement *editorelement) {
  g_return_if_fail(editorelement != NULL);

  //g_print("gst_editor_element_state_change got state 0x%08x\n",state);
  gst_editor_element_sync_state(editorelement);
}

static void gst_editor_element_sync_state(GstEditorElement *element) {
  gint id;
  GstElementState state = GST_STATE(element->element);

  for (id=0;id<4;id++) {
    if (_gst_element_states[id] == state) {
      gtk_object_set(GTK_OBJECT(element->statebox[id]),
                     "fill_color","black",NULL);
      gtk_object_set(GTK_OBJECT(element->statetext[id]),
                     "fill_color","white",NULL);
    }
    else {
      gtk_object_set(GTK_OBJECT(element->statebox[id]),
                     "fill_color","white",NULL);
      gtk_object_set(GTK_OBJECT(element->statetext[id]),
                     "fill_color","black",NULL);
    }
  }
}

static void gst_editor_element_move(GstEditorElement *element,
                                    gdouble dx,gdouble dy) {
  GList *pads;
  GstEditorPad *pad;

  // this is a 'little' trick to keep from repacking the whole thing...
  element->x += dx;element->y += dy;
  gnome_canvas_item_move(GNOME_CANVAS_ITEM(element->group),dx,dy);

  pads = element->srcpads;
  while (pads) {
    pad = GST_EDITOR_PAD(pads->data);
    if (pad->connection) {
//      g_print("updating pad's connection\n");
      pad->connection->resize = TRUE;
      gst_editor_connection_resize(pad->connection);
    }
    pads = g_list_next(pads);
  }
  pads = element->sinkpads;
  while (pads) {
    pad = GST_EDITOR_PAD(pads->data);
    if (pad->connection) {
//      g_print("updating pad's connection\n");
      pad->connection->resize = TRUE;
      gst_editor_connection_resize(pad->connection);
    }
    pads = g_list_next(pads);
  }
}
