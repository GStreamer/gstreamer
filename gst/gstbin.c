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

static GstElementStateReturn gst_bin_change_state(GstElement *element);
static GstElementStateReturn gst_bin_change_state_norecurse(GstBin *bin);
static gboolean gst_bin_change_state_type(GstBin *bin,
                                          GstElementState state,
                                          GtkType type);

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
                   GST_TYPE_ELEMENT);
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

  // must be NULL or PAUSED state in order to modify bin
  g_return_if_fail((GST_STATE(bin) == GST_STATE_NULL) ||
		   (GST_STATE(bin) == GST_STATE_PAUSED));

  bin->children = g_list_append(bin->children,element);
  bin->numchildren++;
  gst_object_set_parent(GST_OBJECT(element),GST_OBJECT(bin));

#ifdef OLDSTATE
  /* FIXME: this isn't right, the bin should be complete whether or not
     the children are, I think. */
//  if (GST_STATE_IS_SET(element,GST_STATE_COMPLETE)) {
    if (!GST_STATE_IS_SET(bin,GST_STATE_COMPLETE)) {
      g_print("GstBin: adding complete element - ");
      gst_bin_change_state_norecurse(GST_ELEMENT(bin));
    }
//  } else {
//    g_print("GstBin: adding element - ");
//  gst_bin_change_state_norecurse(GST_ELEMENT(bin),~GST_STATE_COMPLETE);
//  }
#else
  /* we know we have at least one child, we just added one... */
//  if (GST_STATE(element) < GST_STATE_READY)
//    gst_bin_change_state_norecurse(bin,GST_STATE_READY);
#endif

  gtk_signal_emit(GTK_OBJECT(bin),gst_bin_signals[OBJECT_ADDED],element);
}

/**
 * gst_bin_remove:
 * @bin: #GstBin to remove element from
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

  // must be NULL or PAUSED state in order to modify bin
  g_return_if_fail((GST_STATE(bin) == GST_STATE_NULL) ||
		   (GST_STATE(bin) == GST_STATE_PAUSED));

  gst_object_unparent(GST_OBJECT(element));
  bin->children = g_list_remove(bin->children,element);
  bin->numchildren--;

  /* if we're down to zero children, force state to NULL */
  if (bin->numchildren == 0)
    gst_element_set_state(GST_ELEMENT(bin),GST_STATE_NULL);
}


static GstElementStateReturn gst_bin_change_state(GstElement *element) {
  GstBin *bin;
  GList *children;
  GstElement *child;

  g_return_val_if_fail(GST_IS_BIN(element), GST_STATE_FAILURE);
  bin = GST_BIN(element);

  g_print("gst_bin_change_state(\"%s\"): currently %d(%s), %d(%s) pending\n",
          gst_element_get_name(element),GST_STATE(element),
          _gst_print_statename(GST_STATE(element)),GST_STATE_PENDING(element),
          _gst_print_statename(GST_STATE_PENDING(element)));

//  g_return_val_if_fail(bin->numchildren != 0, GST_STATE_FAILURE);

//  g_print("-->\n");
  children = bin->children;
  while (children) {
    child = GST_ELEMENT(children->data);
    g_print("gst_bin_change_state: setting state on '%s'\n",
            gst_element_get_name(child));
    switch (gst_element_set_state(child,GST_STATE_PENDING(element))) {
      case GST_STATE_FAILURE:
        GST_STATE_PENDING(element) = GST_STATE_NONE_PENDING;
        g_print("child '%s' failed to go to state %d(%s)\n",gst_element_get_name(child),
                GST_STATE_PENDING(element),_gst_print_statename(GST_STATE_PENDING(element)));
        return GST_STATE_FAILURE;
        break;
      case GST_STATE_ASYNC:
        g_print("child '%s' is changing state asynchronously\n",gst_element_get_name(child));
        break;
    }
//    g_print("\n");
    children = g_list_next(children);
  }
//  g_print("<-- \"%s\"\n",gst_object_get_name(GST_OBJECT(bin)));

//  if (GST_STATE_PENDING(element),

  return gst_bin_change_state_norecurse(bin);
}


static GstElementStateReturn gst_bin_change_state_norecurse(GstBin *bin) {
/*
  if ((state == GST_STATE_READY) && (GST_STATE(bin) < GST_STATE_READY)) {
//    gst_bin_create_plan(
  }
*/

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(bin);
  else
    return GST_STATE_FAILURE;
}

static gboolean gst_bin_change_state_type(GstBin *bin,
                                          GstElementState state,
                                          GtkType type) {
  GList *children;
  GstElement *child;

//  g_print("gst_bin_change_state_type(\"%s\",%d,%d);\n",
//          gst_object_get_name(GST_OBJECT(bin)),state,type);

  g_return_val_if_fail(GST_IS_BIN(bin), FALSE);
  g_return_val_if_fail(bin->numchildren != 0, FALSE);

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

  g_return_val_if_fail(bin != NULL, FALSE);
  g_return_val_if_fail(GST_IS_BIN(bin), FALSE);

  oclass = GST_BIN_CLASS(GTK_OBJECT(bin)->klass);

  if (oclass->change_state_type)
    (oclass->change_state_type)(bin,state,type);
	return TRUE;
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

/**
 * gst_bin_get_by_name:
 * @bin: #Gstbin to search
 * @name: the element name to search for
 *
 * get the element with the given name from this bin
 *
 * Returns: the element with the given name
 */
GstElement *gst_bin_get_by_name(GstBin *bin,gchar *name) {
  GList *children;
  GstElement *child;

  g_return_val_if_fail(bin != NULL, NULL);
  g_return_val_if_fail(GST_IS_BIN(bin), NULL);
  g_return_val_if_fail(name != NULL, NULL);

  children = bin->children;
  while (children) {
    child = GST_ELEMENT(children->data);
    if (!strcmp(child->name,name))
      return child;
    children = g_list_next(children);
  }

  return NULL;
}

/**
 * gst_bin_get_list:
 * @bin: #Gstbin to get the list from
 *
 * get the list of elements in this bin
 *
 * Returns: a GList of elements
 */
GList *gst_bin_get_list(GstBin *bin) {
  g_return_val_if_fail(bin != NULL, NULL);
  g_return_val_if_fail(GST_IS_BIN(bin), NULL);

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
	return childlist;
}

/**
 * gst_bin_iterate:
 * @bin: #Gstbin to iterate
 *
 * iterates over the elements in this bin
 */
void gst_bin_iterate(GstBin *bin) {
  GstBinClass *oclass;

  oclass = GST_BIN_CLASS(GTK_OBJECT(bin)->klass);

g_print("gst_bin_iterate()\n");
  if (oclass->iterate)
    (oclass->iterate)(bin);
}

void gst_bin_create_plan(GstBin *bin) {
  GstBinClass *oclass;

  oclass = GST_BIN_CLASS(GTK_OBJECT(bin)->klass);

  if (oclass->create_plan)
    (oclass->create_plan)(bin);
}

#ifdef OLD_STUFF
static void gst_bin_create_plan_func(GstBin *bin) {
  GList *elements;
  GstElement *element;
  GList *pads;
  GstPad *pad, *peer;
  GstElement *outside;

  bin->numentries = 0;

  g_print("GstBin: attempting to create a plan for bin %p\n",bin);

  /* walk through all the elements to figure out all kinds of things */
  elements = GST_BIN(bin)->children;
  while (elements) {
    element = GST_ELEMENT(elements->data);

    // have to use cothreads if any elements use loop functions, or if any
    // of them have nontrivial chain functions
    if (element->loopfunc != NULL) {
      if (bin->threadcontext == NULL) {
        g_print("GstBin: initializing cothread context\n");
        bin->threadcontext = cothread_init();
      }
      if (element->threadstate == NULL) {
        g_print("GstBin: creating thread state for element\n");
        element->threadstate = cothread_create(bin->threadcontext);
        cothread_setfunc(element->threadstate,gst_element_loopfunc_wrapper, 
                         0,element);
      }
    }

    // we need to find all the entry points into the bin
    if (GST_IS_SRC(element)) {
      g_print("GstBin: element '%s' is a source entry point for the bin\n",
              gst_element_get_name(GST_ELEMENT(element)));
      bin->entries = g_list_prepend(bin->entries,element);
      bin->numentries++;
    } else {
      // go through the list of pads to see if there's a Connection
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
            g_print("GstBin: element '%s' is the external source Connection \
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
#endif

static int gst_bin_loopfunc_wrapper(int argc,char *argv[]) {
  GstElement *element = GST_ELEMENT(argv);
  GList *pads;
  GstPad *pad;
  GstBuffer *buf;

//  g_print("** gst_bin_loopfunc_wrapper(%d,\"%s\")\n",
//          argc,gst_element_get_name(element));

  if (element->loopfunc != NULL) {
//    g_print("** gst_bin_loopfunc_wrapper(): element has loop function, calling it\n");
    while (1) {
      (element->loopfunc)(element);
    }
  } else {
//    g_print("** gst_bin_loopfunc_wrapper(): element is chain-based, calling in infinite loop\n");
    if (GST_IS_SRC(element)) {
      while (1) {
//        g_print("** gst_bin_loopfunc_wrapper(): calling push function of source\n");
        gst_src_push(GST_SRC(element));
      }
    } else {
      while (1) {
        pads = element->pads;
        while (pads) {
          pad = GST_PAD(pads->data);
          if (pad->direction == GST_PAD_SINK) {
//            g_print("** gst_bin_loopfunc_wrapper(): pulling a buffer\n");
            buf = gst_pad_pull(pad);
//            g_print("** gst_bin_loopfunc_wrapper(): calling chain function\n");
            (pad->chainfunc)(pad,buf);
          }
          pads = g_list_next(pads);
        }
      }
    }
  }
}

static void gst_bin_pullfunc_wrapper(GstPad *pad) {
//  g_print("** in gst_bin_pullfunc_wrapper()============================= %s\n",
//          gst_element_get_name(GST_ELEMENT(pad->parent)));
  cothread_switch(GST_ELEMENT(pad->parent)->threadstate);
}

static void gst_bin_pushfunc_wrapper(GstPad *pad) {
//  g_print("** in gst_bin_pushfunc_wrapper()============================= %s\n",
//          gst_element_get_name(GST_ELEMENT(pad->parent)));
  cothread_switch(GST_ELEMENT(pad->parent)->threadstate);
}

static void gst_bin_create_plan_func(GstBin *bin) {
  GList *elements;
  GstElement *element;
  int sink_pads;
  GList *pads;
  GstPad *pad, *peer;
  GstElement *outside;

  g_print("creating plan for bin\n");

  // first loop through all children to see if we need cothreads
  // we break immediately when we find we need to, why keep searching?
  elements = bin->children;
  while (elements) {
    element = GST_ELEMENT(elements->data);
    // if it's a loop-based element, use cothreads
    if (element->loopfunc != NULL) {
      bin->need_cothreads = TRUE;
      break;
    }
    // if it's a complex element, use cothreads
    if (GST_ELEMENT_IS_MULTI_IN(element)) {
      bin->need_cothreads = TRUE;
      break;
    }
    // if it has more than one input pad, use cothreads
    sink_pads = 0;
    pads = gst_element_get_pad_list(element);
    while (pads) {
      pad = GST_PAD(pads->data);
      if (pad->direction == GST_PAD_SINK)
        sink_pads++;
      pads = g_list_next(pads);
    }
    if (sink_pads > 1) {
      bin->need_cothreads = TRUE;
      break;
    }
    elements = g_list_next(elements);
  }

  if (bin->need_cothreads) {
    g_print("BIN: need cothreads\n");

    // first create thread context
    if (bin->threadcontext == NULL) {
      bin->threadcontext = cothread_init();
      g_print("initialized cothread context\n");
    }

    // walk through all the children
    elements = bin->children;
    while (elements) {
      element = GST_ELEMENT(elements->data);

      // start by creating thread state for the element
      if (element->threadstate == NULL) {
        element->threadstate = cothread_create(bin->threadcontext);
        cothread_setfunc(element->threadstate,gst_bin_loopfunc_wrapper,
                         0,(char **)element);
      }

      pads = gst_element_get_pad_list(element);
      while (pads) {
        pad = GST_PAD(pads->data);
g_print("setting push&pull handlers for %s:%s\n",
gst_element_get_name(element),gst_pad_get_name(pad));
//        if (pad->direction == GST_PAD_SRC)
          pad->pushfunc = gst_bin_pushfunc_wrapper;
//        else
          pad->pullfunc = gst_bin_pullfunc_wrapper;
        pads = g_list_next(pads);
      }
      elements = g_list_next(elements);
   }
  } else {
    g_print("BIN: don't need cothreads, looking for entry points\n");
    // clear previous plan state
    g_list_free(bin->entries);
    bin->numentries = 0;
    // we have to find which elements will drive an iteration
    elements = bin->children;
    while (elements) {
      element = GST_ELEMENT(elements->data);
      if (GST_IS_SRC(element)) {
        g_print("adding '%s' as entry point\n",gst_element_get_name(element));
        bin->entries = g_list_prepend(bin->entries,element);
        bin->numentries++;
      }
      elements = g_list_next(elements);
    }
  }
}

void gst_bin_iterate_func(GstBin *bin) {
  GList *entries;
  GstElement *entry;

  g_print("gst_bin_iterate_func()\n");

  g_return_if_fail(bin != NULL);
  g_return_if_fail(GST_IS_BIN(bin));
  g_return_if_fail(GST_STATE(bin) == GST_STATE_PLAYING);
  g_return_if_fail(bin->numentries > 0);

  g_print("GstBin: iterating\n");

  if (bin->need_cothreads) {
    // all we really have to do is switch to the first child
    // FIXME this should be lots more intelligent about where to start
//  g_print("** in gst_bin_iterate_func()==================================%s\n",
//          gst_element_get_name(GST_ELEMENT(bin->children->data)));
    cothread_switch(GST_ELEMENT(bin->children->data)->threadstate);
  } else {
    entries = bin->entries;

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
  }
//  g_print(",");
}
