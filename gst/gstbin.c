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

#include <gst/gst.h>

GstElementDetails gst_bin_details = { 
  "Generic bin",
  "Bin",
  "Simple container object",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


void gst_bin_real_destroy(GtkObject *object);

static gboolean gst_bin_change_state(GstElement *element,
                                     GstElementState state);
static gboolean gst_bin_change_state_type(GstBin *bin,
                                          GstElementState state,
                                          GtkType type);
static gboolean gst_bin_change_state_norecurse(GstElement *element,
                                     GstElementState state);

static void gst_bin_create_plan_func(GstBin *bin);
static void gst_bin_iterate_func(GstBin *bin);

static xmlNodePtr gst_bin_save_thyself(GstElement *element,xmlNodePtr parent);

/* Bin signals and args */
enum {
  OBJECT_ADDED,
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_bin_class_init(GstBinClass *klass);
static void gst_bin_init(GstBin *bin);


static GstElementClass *parent_class = NULL;
static guint gst_bin_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_bin_get_type(void) {
  static GtkType bin_type = 0;

  if (!bin_type) {
    static const GtkTypeInfo bin_info = {
      "GstBin",
      sizeof(GstBin),
      sizeof(GstBinClass),
      (GtkClassInitFunc)gst_bin_class_init,
      (GtkObjectInitFunc)gst_bin_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    bin_type = gtk_type_unique(GST_TYPE_ELEMENT,&bin_info);
  }
  return bin_type;
}

static void
gst_bin_class_init(GstBinClass *klass) {
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_ELEMENT);

  gst_bin_signals[OBJECT_ADDED] =
    gtk_signal_new("object_added",GTK_RUN_FIRST,gtkobject_class->type,
                   GTK_SIGNAL_OFFSET(GstBinClass,object_added),
                   gtk_marshal_NONE__POINTER,GTK_TYPE_NONE,1,
                   GTK_TYPE_POINTER);
  gtk_object_class_add_signals(gtkobject_class,gst_bin_signals,LAST_SIGNAL);

  klass->change_state_type = gst_bin_change_state_type;
  klass->create_plan = gst_bin_create_plan_func;
  klass->iterate = gst_bin_iterate_func;

  gstelement_class->change_state = gst_bin_change_state;
  gstelement_class->save_thyself = gst_bin_save_thyself;

  gtkobject_class->destroy = gst_bin_real_destroy;
}

static void gst_bin_init(GstBin *bin) {
  bin->numchildren = 0;
  bin->children = NULL;
}

/**
 * gst_bin_new:
 * @name: name of new bin
 *
 * Create a new bin with given name.
 *
 * Returns: new bin
 */
GstElement *gst_bin_new(gchar *name) {
  GstElement *bin = GST_ELEMENT(gtk_type_new(GST_TYPE_BIN));
  gst_element_set_name(GST_ELEMENT(bin),name);
  return bin;
}

/**
 * gst_bin_add:
 * @bin: #GstBin to add element to
 * @element: #GstElement to add to bin
 *
 * Add the given element to the bin.  Set the elements parent, and thus
 * add a reference.
 */
void gst_bin_add(GstBin *bin,GstElement *element) {
  g_return_if_fail(bin != NULL);
  g_return_if_fail(GST_IS_BIN(bin));
  g_return_if_fail(element != NULL);
  g_return_if_fail(GST_IS_ELEMENT(element));

  bin->children = g_list_append(bin->children,element);
  bin->numchildren++;
  gst_object_set_parent(GST_OBJECT(element),GST_OBJECT(bin));

  /* FIXME: this isn't right, the bin should be complete whether or not
     the children are, I think. */
//  if (GST_STATE_IS_SET(element,GST_STATE_COMPLETE)) {
    if (!GST_STATE_IS_SET(bin,GST_STATE_COMPLETE)) {
      g_print("GstBin: adding complete element - ");
      gst_bin_change_state_norecurse(GST_ELEMENT(bin),GST_STATE_COMPLETE);
    }
//  } else {
//    g_print("GstBin: adding element - ");
//  gst_bin_change_state_norecurse(GST_ELEMENT(bin),~GST_STATE_COMPLETE);
//  }

  gtk_signal_emit(GTK_OBJECT(bin),gst_bin_signals[OBJECT_ADDED],element);
}

/**
 * gst_bin_remove:
 * @bin: #Gstbin to remove element from
 * @element: #GstElement to remove
 *
 * Remove the element from its associated bin, unparenting as well.
 */
void gst_bin_remove(GstBin *bin,GstElement *element) {
  g_return_if_fail(bin != NULL);
  g_return_if_fail(GST_IS_BIN(bin));
  g_return_if_fail(element != NULL);
  g_return_if_fail(GST_IS_ELEMENT(element));
  g_return_if_fail(bin->children != NULL);

  gst_object_unparent(GST_OBJECT(element));
  bin->children = g_list_remove(bin->children,element);
  bin->numchildren--;
}


static gboolean gst_bin_change_state(GstElement *element,
                                     GstElementState state) {
  GstBin *bin;
  GList *children;
  GstElement *child;

//  g_print("gst_bin_change_state(\"%s\",%d);\n",
//          gst_object_get_name(GST_OBJECT(bin)),state);

  g_return_if_fail(GST_IS_BIN(element));
  bin = GST_BIN(element);
  g_return_if_fail(bin->numchildren != 0);

//  g_print("-->\n");
  children = bin->children;
  while (children) {
    child = GST_ELEMENT(children->data);
//    g_print("gst_bin_change_state setting state on \"%s\"\n",
//            gst_object_get_name(GST_OBJECT(child)));
    if (!gst_element_set_state(child,state)) {
      g_print("child %p failed to set state 0x%08x\n",child,state);
      return FALSE;
    }
//    g_print("\n");
    children = g_list_next(children);
  }
//  g_print("<-- \"%s\"\n",gst_object_get_name(GST_OBJECT(bin)));

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element,state);
  return TRUE;
}


static gboolean gst_bin_change_state_norecurse(GstElement *element,
                                     GstElementState state) {
  GstBin *bin;

  g_return_if_fail(GST_IS_BIN(element));
  bin = GST_BIN(element);
  g_return_if_fail(bin->numchildren != 0);

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element,state);
  return TRUE;
}

static gboolean gst_bin_change_state_type(GstBin *bin,
                                          GstElementState state,
                                          GtkType type) {
  GList *children;
  GstElement *child;

//  g_print("gst_bin_change_state_type(\"%s\",%d,%d);\n",
//          gst_object_get_name(GST_OBJECT(bin)),state,type);

  g_return_if_fail(GST_IS_BIN(bin));
  g_return_if_fail(bin->numchildren != 0);

//  g_print("-->\n");
  children = bin->children;
  while (children) {
    child = GST_ELEMENT(children->data);
    if (GST_IS_BIN(child)) {
      if (!gst_bin_set_state_type(GST_BIN(child),state,type))
        return FALSE;
    } else if (GTK_CHECK_TYPE(child,type)) {
      if (!gst_element_set_state(child,state))
        return FALSE;
    }
//    g_print("\n");
    children = g_list_next(children);
  }
//  g_print("<-- \"%s\"\n",gst_object_get_name(GST_OBJECT(bin)));
  if (type == GST_TYPE_BIN)
    gst_element_change_state(GST_ELEMENT(bin),state);

  return TRUE;
}


gboolean gst_bin_set_state_type(GstBin *bin,
                                GstElementState state,
                                GtkType type) {
  GstBinClass *oclass;

//  g_print("gst_bin_set_state_type(\"%s\",%d,%d)\n",
//          gst_object_get_name(GST_OBJECT(bin)),state,type);

  g_return_if_fail(bin != NULL);
  g_return_if_fail(GST_IS_BIN(bin));

  oclass = GST_BIN_CLASS(GTK_OBJECT(bin)->klass);

  if (oclass->change_state_type)
    (oclass->change_state_type)(bin,state,type);
}

void gst_bin_real_destroy(GtkObject *object) {
  GstBin *bin = GST_BIN(object);
  GList *children;
  GstElement *child;

//  g_print("in gst_bin_real_destroy()\n");

  children = bin->children;
  while (children) {
    child = GST_ELEMENT(children->data);
    gst_element_destroy(child);
    children = g_list_next(children);
  }

  g_list_free(bin->children);
}

GstElement *gst_bin_get_by_name(GstBin *bin,gchar *name) {
  GList *children;
  GstElement *child;

  g_return_if_fail(bin != NULL);
  g_return_if_fail(GST_IS_BIN(bin));
  g_return_if_fail(name != NULL);

  children = bin->children;
  while (children) {
    child = GST_ELEMENT(children->data);
    if (!strcmp(child->name,name))
      return child;
    children = g_list_next(children);
  }

  return NULL;
}

GList *gst_bin_get_list(GstBin *bin) {
  g_return_if_fail(bin != NULL);
  g_return_if_fail(GST_IS_BIN(bin));

  return bin->children;
}

static xmlNodePtr gst_bin_save_thyself(GstElement *element,xmlNodePtr parent) {
  GstBin *bin = GST_BIN(element);
  xmlNodePtr childlist;
  GList *children;
  GstElement *child;

  if (GST_ELEMENT_CLASS(parent_class)->save_thyself)
    GST_ELEMENT_CLASS(parent_class)->save_thyself(GST_ELEMENT(bin),parent);

  childlist = xmlNewChild(parent,NULL,"children",NULL);

  children = bin->children;
  while (children) {
    child = GST_ELEMENT(children->data);
    gst_element_save_thyself(child,childlist);
    children = g_list_next(children);
  }
}

void gst_bin_iterate(GstBin *bin) {
  GstBinClass *oclass;

  oclass = GST_BIN_CLASS(GTK_OBJECT(bin)->klass);

  if (oclass->iterate)
    (oclass->iterate)(bin);
}

void gst_bin_create_plan(GstBin *bin) {
  GstBinClass *oclass;

  oclass = GST_BIN_CLASS(GTK_OBJECT(bin)->klass);

  if (oclass->create_plan)
    (oclass->create_plan)(bin);
}

static void gst_bin_create_plan_func(GstBin *bin) {
  GList *elements;
  GstElement *element;
  GList *pads;
  GstPad *pad, *peer;
  GstElement *outside;

  bin->numentries = 0;

  g_print("attempting to create a plan for bin %p\n",bin);

  /* walk through all the elements to figure out all kinds of things */
  elements = GST_BIN(bin)->children;
  while (elements) {
    element = GST_ELEMENT(elements->data);

    // have to use cothreads if any elements use loop functions
    if (element->loopfunc != NULL) {
      if (bin->threadcontext == NULL) {
        g_print("initializing cothread context\n");
        bin->threadcontext = cothread_init();
      }
      if (element->threadstate == NULL) {
        g_print("creating thread state for element\n");
        element->threadstate = cothread_create(bin->threadcontext);
        cothread_setfunc(element->threadstate,gst_element_loopfunc_wrapper, 
                         0,element);
      }
    }

    /* we need to find all the entry points into the bin */
    if (GST_IS_SRC(element)) {
      g_print("element '%s' is a source entry point for the bin\n",
              gst_element_get_name(GST_ELEMENT(element)));
      bin->entries = g_list_prepend(bin->entries,element);
      bin->numentries++;
    } else {
      /* go through the list of pads to see if there's a Connection */
      pads = gst_element_get_pad_list(element);
      while (pads) {
        pad = GST_PAD(pads->data);
        /* we only worry about sink pads */
        if (gst_pad_get_direction(pad) == GST_PAD_SINK) {
          /* get the pad's peer */
          peer = gst_pad_get_peer(pad);
          if (!peer) break;
          /* get the parent of the peer of the pad */
          outside = GST_ELEMENT(gst_pad_get_parent(peer));
          if (!outside) break;
          /* if it's a connection and it's not ours... */
          if (GST_IS_CONNECTION(outside) &&
              (gst_object_get_parent(GST_OBJECT(outside)) != GST_OBJECT(bin))) {
            g_print("element '%s' is the external source Connection \
for internal element '%s'\n",
                    gst_element_get_name(GST_ELEMENT(outside)),
                    gst_element_get_name(GST_ELEMENT(element)));
            bin->entries = g_list_prepend(bin->entries,outside);
            bin->numentries++;
          }
        }
        pads = g_list_next(pads);
      }
    }
    elements = g_list_next(elements);
  }
  g_print("have %d entries into bin\n",bin->numentries);
}

void gst_bin_iterate_func(GstBin *bin) {
  GList *entries;
  GstElement *entry;
   
  g_return_if_fail(bin != NULL);
  g_return_if_fail(GST_IS_BIN(bin));
//  g_return_if_fail(GST_FLAG_IS_SET(thread,GST_STATE_RUNNING));
  g_return_if_fail(bin->numentries > 0);
  
  entries = bin->entries;  

  g_print("iterating\n");

  while (entries) {
    entry = GST_ELEMENT(entries->data);
    if (GST_IS_SRC(entry))
      gst_src_push(GST_SRC(entry));
    else if (GST_IS_CONNECTION(entry))
      gst_connection_push(GST_CONNECTION(entry));
    else
      g_assert_not_reached();
    entries = g_list_next(entries);
  }
//  g_print(",");
}
