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

#include <gst/gstelement.h>


/* Element signals and args */
enum {
  STATE_CHANGE,
  NEW_PAD,
  NEW_GHOST_PAD,
  ERROR,
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_element_class_init(GstElementClass *klass);
static void gst_element_init(GstElement *element);
static void gst_element_real_destroy(GtkObject *object);


static GstObjectClass *parent_class = NULL;
static guint gst_element_signals[LAST_SIGNAL] = { 0 };

GtkType gst_element_get_type(void) {
  static GtkType element_type = 0;

  if (!element_type) {
    static const GtkTypeInfo element_info = {
      "GstElement",
      sizeof(GstElement),
      sizeof(GstElementClass),
      (GtkClassInitFunc)gst_element_class_init,
      (GtkObjectInitFunc)gst_element_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    element_type = gtk_type_unique(GST_TYPE_OBJECT,&element_info);
  }
  return element_type;
}

static void gst_element_class_init(GstElementClass *klass) {
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_OBJECT);

  gst_element_signals[STATE_CHANGE] =
    gtk_signal_new("state_change",GTK_RUN_LAST,gtkobject_class->type,
                   GTK_SIGNAL_OFFSET(GstElementClass,state_change),
                   gtk_marshal_NONE__INT,GTK_TYPE_NONE,1,
                   GTK_TYPE_INT);
  gst_element_signals[NEW_PAD] =
    gtk_signal_new("new_pad",GTK_RUN_LAST,gtkobject_class->type,
                   GTK_SIGNAL_OFFSET(GstElementClass,new_pad),
                   gtk_marshal_NONE__POINTER,GTK_TYPE_NONE,1,
                   GTK_TYPE_POINTER);
  gst_element_signals[NEW_GHOST_PAD] =
    gtk_signal_new("new_ghost_pad",GTK_RUN_LAST,gtkobject_class->type,
                   GTK_SIGNAL_OFFSET(GstElementClass,new_ghost_pad),
                   gtk_marshal_NONE__POINTER,GTK_TYPE_NONE,1,
                   GTK_TYPE_POINTER);
  gst_element_signals[ERROR] =
    gtk_signal_new("error",GTK_RUN_LAST,gtkobject_class->type,
                   GTK_SIGNAL_OFFSET(GstElementClass,error),
                   gtk_marshal_NONE__POINTER,GTK_TYPE_NONE,1,
                   GTK_TYPE_POINTER);


  gtk_object_class_add_signals(gtkobject_class,gst_element_signals,LAST_SIGNAL);

  klass->start = NULL;
  klass->stop = NULL;
  klass->change_state = gst_element_change_state;

  gtkobject_class->destroy = gst_element_real_destroy;
}

static void gst_element_init(GstElement *element) {
  element->state = 0;
  element->numpads = 0;
  element->pads = NULL;
  element->loopfunc = NULL;
}

/**
 * gst_element_new:
 *
 * Create a new element.
 *
 * Returns: new element
 */
GstElement *gst_element_new() {
  return GST_ELEMENT(gtk_type_new(GST_TYPE_ELEMENT));
}

/**
 * gst_element_add_pad:
 * @element: element to add pad to
 * @pad: pad to add
 *
 * Add a pad (connection point) to the element, setting the parent of the
 * pad to the element (and thus adding a reference).
 */
void gst_element_add_pad(GstElement *element,GstPad *pad) {
  g_return_if_fail(element != NULL);
  g_return_if_fail(GST_IS_ELEMENT(element));
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

  /* set the pad's parent */
  gst_pad_set_parent(pad,GST_OBJECT(element));

  /* add it to the list */
  element->pads = g_list_append(element->pads,pad);
  element->numpads++;

  /* emit the NEW_PAD signal */
//  g_print("emitting NEW_PAD signal, \"%s\"!\n",gst_pad_get_name(pad));
  gtk_signal_emit(GTK_OBJECT(element),gst_element_signals[NEW_PAD],pad);
}

/**
 * gst_element_add_ghost_pad:
 * @element: element to add ghost pad to
 * @pad: ghost pad to add
 *
 * Add a ghost pad to the element, setting the ghost parent of the pad to
 * the element (and thus adding a reference).
 */
void gst_element_add_ghost_pad(GstElement *element,GstPad *pad) {
  g_return_if_fail(element != NULL);
  g_return_if_fail(GST_IS_ELEMENT(element));
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

  /* set the pad's parent */
  gst_pad_add_ghost_parent(pad,GST_OBJECT(element));

  /* add it to the list */
  element->pads = g_list_append(element->pads,pad);
  element->numpads++;

  /* emit the NEW_PAD signal */
  gtk_signal_emit(GTK_OBJECT(element),gst_element_signals[NEW_GHOST_PAD],pad);
}

/**
 * gst_element_get_pad:
 * @element: element to find pad of
 * @name: name of pad to retrieve
 *
 * Retrieve a pad from the element by name.
 *
 * Returns: requested pad if found, otherwise NULL.
 */
GstPad *gst_element_get_pad(GstElement *element,gchar *name) {
  GList *walk;

  g_return_if_fail(element != NULL);
  g_return_if_fail(GST_IS_ELEMENT(element));
  if (name == NULL)
    return NULL;
  if (!element->numpads)
    return NULL;

  walk = element->pads;
  while (walk) {
    if (!strcmp(((GstPad *)(walk->data))->name,name))
      return (GstPad *)(walk->data);
    walk = g_list_next(walk);
  }

  return NULL;
}

/**
 * gst_element_get_pad_list:
 * @element: element to get pads of
 *
 * Retrieve a list of the pads associated with the element.
 *
 * Returns: <type>GList</type> of pads
 */
GList *gst_element_get_pad_list(GstElement *element) {
  g_return_if_fail(element != NULL);
  g_return_if_fail(GST_IS_ELEMENT(element));

  return element->pads;
}

/**
 * gst_element_connect:
 * @src: element containing source pad
 * @srcpadname: name of pad in source element
 * @dest: element containing destination pad
 * @destpadname: name of pad in destination element
 *
 * Connect the two named pads of the source and destination elements.
 * Side effect is that if one of the pads has no parent, it becomes a
 * child of the parent of the other element.  If they have different
 * parents, the connection fails.
 */
void gst_element_connect(GstElement *src,gchar *srcpadname,
                         GstElement *dest,gchar *destpadname) {
  GstPad *srcpad,*destpad;
  GstObject *srcparent,*destparent;

  g_return_if_fail(src != NULL);
  g_return_if_fail(GST_IS_ELEMENT(src));
  g_return_if_fail(srcpadname != NULL);
  g_return_if_fail(dest != NULL);
  g_return_if_fail(GST_IS_ELEMENT(dest));
  g_return_if_fail(destpadname != NULL);

  srcpad = gst_element_get_pad(src,srcpadname);
  destpad = gst_element_get_pad(dest,destpadname);

  g_return_if_fail(srcpad != NULL);
  g_return_if_fail(destpad != NULL);

  srcparent = gst_object_get_parent(GST_OBJECT(src));
  destparent = gst_object_get_parent(GST_OBJECT(dest));
  /* we can't do anything if neither have parents */
  if ((srcparent == NULL) && (destparent == NULL))
    return;

  /* and we have to make sure that they have the same parents... */
  if ((srcparent == NULL) && (destparent == NULL)) {
    if (srcparent != destparent)
      return;
  }
}

void gst_element_error(GstElement *element,gchar *error) {
  g_error("error in element '%s': %s\n",element->name,error);

  gtk_signal_emit(GTK_OBJECT(element),gst_element_signals[ERROR],error);
}


/**
 * gst_element_set_state:
 * @element: element to change state of
 * @state: new element state
 *
 * Sets the state of the element, but more importantly fires off a signal
 * indicating the new state.  You can clear state by simply prefixing the
 * GstElementState value with ~, it will be detected and used to turn off
 * that bit.
 *
 * Returns: whether or not the state was successfully set.
 */
gboolean gst_element_set_state(GstElement *element,GstElementState state) {
  GstElementClass *oclass;
  gboolean stateset = FALSE;

//  g_print("gst_element_set_state(\"%s\",%08lx)\n",
//          element->name,state);

  g_return_if_fail(element != NULL);
  g_return_if_fail(GST_IS_ELEMENT(element));

  oclass = GST_ELEMENT_CLASS(GTK_OBJECT(element)->klass);

  if (oclass->change_state)
    stateset = (oclass->change_state)(element,state);

  /* if a state *set* failed, unset it immediately */
/*
  if (!(state & GST_STATE_MAX) && !stateset) {
    g_print("set state failed miserably, forcing unset\n");
    if (oclass->change_state)
      stateset = (oclass->change_state)(element,~state);
    return FALSE;
  }*/

  return stateset;
}

/* class function to set the state of a simple element */
gboolean gst_element_change_state(GstElement *element,
                                  GstElementState state) {
  g_return_if_fail(element != NULL);
  g_return_if_fail(GST_IS_ELEMENT(element));

//  g_print("gst_element_change_state(\"%s\",%d)\n",
//          element->name,state);

  /* deal with the inverted state */
//  g_print("changing element state, was %08lx",GST_STATE(element));
  if (state & GST_STATE_MAX)
    GST_STATE_UNSET(element,~state);
  else
    GST_STATE_SET(element,state);
//  g_print(", is now %08lx\n",GST_STATE(element));
  gtk_signal_emit(GTK_OBJECT(element),gst_element_signals[STATE_CHANGE],
                  state);
}

/**
 * gst_element_set_name:
 * @element: GstElement to set name of
 * @name: new name of element
 *
 * Set the name of the element, getting rid of the old name if there was
 * one.
 */
void gst_element_set_name(GstElement *element,gchar *name) {
  g_return_if_fail(element != NULL);
  g_return_if_fail(GST_IS_ELEMENT(element));
  g_return_if_fail(name != NULL);

  if (element->name != NULL)
    g_free(element->name);

  element->name = g_strdup(name);
}

/**
 * gst_element_get_name:
 * @element: GstElement to set name of
 *
 * Get the name of the element.
 *
 * Returns: name of the element
 */
gchar *gst_element_get_name(GstElement *element) {
  g_return_if_fail(element != NULL);
  g_return_if_fail(GST_IS_ELEMENT(element));

  return element->name;
}

static void gst_element_real_destroy(GtkObject *object) {
  GstElement *element = GST_ELEMENT(object);
  GList *pads;
  GstPad *pad;

//  g_print("in gst_element_real_destroy()\n");

  if (element->name)
    g_free(element->name);
  
  pads = element->pads;
  while (pads) {
    pad = GST_PAD(pads->data);
    gst_pad_destroy(pad);
    pads = g_list_next(pads);
  }

  g_list_free(element->pads);
}


static gchar *_gst_element_type_names[] = {
  "invalid",
  "none",
  "char",
  "uchar",
  "bool",
  "int",
  "uint",
  "long",
  "ulong",
  "float",
  "double",
  "string",
};

xmlNodePtr gst_element_save_thyself(GstElement *element,xmlNodePtr parent) {
  xmlNodePtr self, arglist;
  GList *pads;
  GstPad *pad;
  GstElementClass *oclass;
  GstElementFactory *factory;
  GtkType type;

  oclass = GST_ELEMENT_CLASS(GTK_OBJECT(element)->klass);

  self = xmlNewChild(parent,NULL,"element",NULL);
  xmlNewChild(self,NULL,"name",element->name);
  if (oclass->elementfactory != NULL) {
    factory = (GstElementFactory *)oclass->elementfactory;
    xmlNewChild(self,NULL,"type",factory->name);
    xmlNewChild(self,NULL,"version",factory->details->version);
  }

  pads = element->pads;
  while (pads) {
    pad = GST_PAD(pads->data);
    // figure out if it's a direct pad or a ghostpad
    if (pad->parent == element)
      gst_pad_save_thyself(pad,self);
    pads = g_list_next(pads);
  }

  // output all args to the element
  arglist = xmlNewChild(self,NULL,"args",NULL);
  type = GTK_OBJECT_TYPE(element);
  while (type != GTK_TYPE_INVALID) {
    GtkArg *args;
    guint32 *flags;
    guint num_args,i;

    args = gtk_object_query_args(type,&flags,&num_args);
    for (i=0;i<num_args;i++) {
      if ((args[i].type > GTK_TYPE_NONE) &&
          (args[i].type <= GTK_TYPE_STRING) &&
          (flags && GTK_ARG_READABLE)) {
        xmlNodePtr arg;
        gtk_object_getv(GTK_OBJECT(element),1,&args[i]);
        arg = xmlNewChild(arglist,NULL,"arg",NULL);
        xmlNewChild(arg,NULL,"name",args[i].name);
        switch (args[i].type) {
          case GTK_TYPE_CHAR:
            xmlNewChild(arg,NULL,"value",
                        g_strdup_printf("%c",GTK_VALUE_CHAR(args[i])));
            break;
          case GTK_TYPE_UCHAR:
            xmlNewChild(arg,NULL,"value",
                        g_strdup_printf("%d",GTK_VALUE_UCHAR(args[i])));
            break;
          case GTK_TYPE_BOOL:
            xmlNewChild(arg,NULL,"value",
                        GTK_VALUE_BOOL(args[1])?"true":"false");
            break;
          case GTK_TYPE_INT:
            xmlNewChild(arg,NULL,"value",
                        g_strdup_printf("%d",GTK_VALUE_INT(args[i])));
            break;
          case GTK_TYPE_LONG:
            xmlNewChild(arg,NULL,"value",
                        g_strdup_printf("%ld",GTK_VALUE_LONG(args[i])));
            break;
          case GTK_TYPE_ULONG:
            xmlNewChild(arg,NULL,"value",
                        g_strdup_printf("%ld",GTK_VALUE_ULONG(args[i])));
            break;
          case GTK_TYPE_FLOAT:
            xmlNewChild(arg,NULL,"value",
                        g_strdup_printf("%f",GTK_VALUE_FLOAT(args[i])));
            break;
          case GTK_TYPE_DOUBLE:
            xmlNewChild(arg,NULL,"value",
                        g_strdup_printf("%lf",GTK_VALUE_DOUBLE(args[i])));
            break;
          case GTK_TYPE_STRING:
            xmlNewChild(arg,NULL,"value",GTK_VALUE_STRING(args[i]));
            break;
        }
      }
    }
    type = gtk_type_parent(type);
  }

  if (oclass->save_thyself)
    (oclass->save_thyself)(element,self);

  return self;
}

void gst_element_set_manager(GstElement *element,GstElement *manager) {
  element->manager = manager;
}

GstElement *gst_element_get_manager(GstElement *element) {
  return element->manager;
}

// note that this casts a char ** to a GstElement *.  Ick.
int gst_element_loopfunc_wrapper(int argc,char **argv) {
  GstElement *element = GST_ELEMENT(argv);
  element->loopfunc(element);
}

void gst_element_set_loop_function(GstElement *element,
                                   GstElementLoopFunction loop) {
  element->loopfunc = loop;
  if (element->threadstate != NULL)
    cothread_setfunc(element->threadstate,gst_element_loopfunc_wrapper,
                     0,element);
}
