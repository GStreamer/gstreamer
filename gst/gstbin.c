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

#define GST_DEBUG_ENABLED

#include "gstbin.h"
#include "gstdebug.h"
#include "gstsrc.h"
#include "gstconnection.h"

GstElementDetails gst_bin_details = { 
  "Generic bin",
  "Bin",
  "Simple container object",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


static void 			gst_bin_real_destroy		(GtkObject *object);

static GstElementStateReturn 	gst_bin_change_state		(GstElement *element);
static GstElementStateReturn 	gst_bin_change_state_norecurse	(GstBin *bin);
static gboolean 		gst_bin_change_state_type	(GstBin *bin,
                                          			 GstElementState state,
                                          			 GtkType type);

static void 			gst_bin_create_plan_func	(GstBin *bin);
static void 			gst_bin_iterate_func		(GstBin *bin);

static xmlNodePtr 		gst_bin_save_thyself		(GstElement *element, xmlNodePtr parent);
static void 			gst_bin_restore_thyself		(GstElement *element, xmlNodePtr parent, 
								 GHashTable *elements);

/* Bin signals and args */
enum {
  OBJECT_ADDED,
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_bin_class_init	(GstBinClass *klass);
static void gst_bin_init	(GstBin *bin);


static GstElementClass *parent_class = NULL;
static guint gst_bin_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_bin_get_type (void) 
{
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
    bin_type = gtk_type_unique (GST_TYPE_ELEMENT, &bin_info);
  }
  return bin_type;
}

static void
gst_bin_class_init (GstBinClass *klass) 
{
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);

  gst_bin_signals[OBJECT_ADDED] =
    gtk_signal_new ("object_added", GTK_RUN_FIRST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstBinClass, object_added),
                    gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
                    GST_TYPE_ELEMENT);
  gtk_object_class_add_signals (gtkobject_class, gst_bin_signals, LAST_SIGNAL);

  klass->change_state_type = 		gst_bin_change_state_type;
  klass->create_plan = 			gst_bin_create_plan_func;
  klass->iterate = 			gst_bin_iterate_func;

  gstelement_class->change_state = 	gst_bin_change_state;
  gstelement_class->save_thyself = 	gst_bin_save_thyself;
  gstelement_class->restore_thyself = 	gst_bin_restore_thyself;
  gstelement_class->elementfactory = 	gst_elementfactory_find("bin");

  gtkobject_class->destroy = 		gst_bin_real_destroy;
}

static void 
gst_bin_init (GstBin *bin) 
{
  bin->numchildren = 0;
  bin->children = NULL;
// FIXME temporary testing measure
//  bin->use_cothreads = TRUE;
}

/**
 * gst_bin_new:
 * @name: name of new bin
 *
 * Create a new bin with given name.
 *
 * Returns: new bin
 */
GstElement*
gst_bin_new (gchar *name) 
{
  GstElement *bin = GST_ELEMENT (gtk_type_new (GST_TYPE_BIN));
  gst_element_set_name (GST_ELEMENT (bin), name);
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
void 
gst_bin_add (GstBin *bin, 
	     GstElement *element) 
{
  g_return_if_fail (bin != NULL);
  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));

  // must be NULL or PAUSED state in order to modify bin
  g_return_if_fail ((GST_STATE (bin) == GST_STATE_NULL) ||
		    (GST_STATE (bin) == GST_STATE_PAUSED));

  bin->children = g_list_append (bin->children, element);
  bin->numchildren++;
  gst_object_set_parent (GST_OBJECT (element), GST_OBJECT (bin));

  /* we know we have at least one child, we just added one... */
//  if (GST_STATE(element) < GST_STATE_READY)
//    gst_bin_change_state_norecurse(bin,GST_STATE_READY);

  gtk_signal_emit (GTK_OBJECT (bin), gst_bin_signals[OBJECT_ADDED], element);
}

/**
 * gst_bin_remove:
 * @bin: #GstBin to remove element from
 * @element: #GstElement to remove
 *
 * Remove the element from its associated bin, unparenting as well.
 */
void 
gst_bin_remove (GstBin *bin,
		GstElement *element) 
{
  g_return_if_fail (bin != NULL);
  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (bin->children != NULL);

  // must be NULL or PAUSED state in order to modify bin
  g_return_if_fail ((GST_STATE (bin) == GST_STATE_NULL) ||
		    (GST_STATE (bin) == GST_STATE_PAUSED));

  gst_object_unparent (GST_OBJECT (element));
  bin->children = g_list_remove (bin->children, element);
  bin->numchildren--;

  /* if we're down to zero children, force state to NULL */
  if (bin->numchildren == 0)
    gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);
}


static GstElementStateReturn 
gst_bin_change_state (GstElement *element) 
{
  GstBin *bin;
  GList *children;
  GstElement *child;

  g_return_val_if_fail (GST_IS_BIN (element), GST_STATE_FAILURE);

  bin = GST_BIN (element);

  g_print("gst_bin_change_state(\"%s\"): currently %d(%s), %d(%s) pending\n",
          gst_element_get_name (element), GST_STATE (element),
          _gst_print_statename (GST_STATE (element)), GST_STATE_PENDING (element),
          _gst_print_statename (GST_STATE_PENDING (element)));

//  g_return_val_if_fail(bin->numchildren != 0, GST_STATE_FAILURE);

//  g_print("-->\n");
  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    g_print("gst_bin_change_state: setting state on '%s'\n",
            gst_element_get_name (child));
    switch (gst_element_set_state (child, GST_STATE_PENDING (element))) {
      case GST_STATE_FAILURE:
        GST_STATE_PENDING (element) = GST_STATE_NONE_PENDING;
        g_print("gstbin: child '%s' failed to go to state %d(%s)\n", gst_element_get_name (child),
                GST_STATE_PENDING (element), _gst_print_statename (GST_STATE_PENDING (element)));
        return GST_STATE_FAILURE;
        break;
      case GST_STATE_ASYNC:
        g_print("gstbin: child '%s' is changing state asynchronously\n", gst_element_get_name (child));
        break;
    }
//    g_print("\n");
    children = g_list_next (children);
  }
//  g_print("<-- \"%s\"\n",gst_object_get_name(GST_OBJECT(bin)));

  if (GST_STATE_PENDING (element) == GST_STATE_READY) {
    GstObject *parent;

    parent = gst_object_get_parent (GST_OBJECT (element));

    if (!parent || !GST_IS_BIN (parent))
      gst_bin_create_plan (bin);
  }

  return gst_bin_change_state_norecurse (bin);
}


static GstElementStateReturn 
gst_bin_change_state_norecurse (GstBin *bin) 
{

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (GST_ELEMENT (bin));
  else
    return GST_STATE_FAILURE;
}

static gboolean 
gst_bin_change_state_type(GstBin *bin,
                          GstElementState state,
                          GtkType type) 
{
  GList *children;
  GstElement *child;

//  g_print("gst_bin_change_state_type(\"%s\",%d,%d);\n",
//          gst_object_get_name(GST_OBJECT(bin)),state,type);

  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);
  g_return_val_if_fail (bin->numchildren != 0, FALSE);

//  g_print("-->\n");
  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    if (GST_IS_BIN (child)) {
      if (!gst_bin_set_state_type (GST_BIN (child), state,type))
        return FALSE;
    } else if (GTK_CHECK_TYPE (child,type)) {
      if (!gst_element_set_state (child,state))
        return FALSE;
    }
//    g_print("\n");
    children = g_list_next (children);
  }
  if (type == GST_TYPE_BIN)
    gst_element_set_state (GST_ELEMENT (bin),state);

  return TRUE;
}

/**
 * gst_bin_set_state_type:
 * @bin: #GstBin to set the state
 * @state: the new state to set the elements to
 * @type: the type of elements to change
 *
 * Sets the state of only those objects of the given type.
 *
 * Returns: indication if the state change was successfull
 */
gboolean 
gst_bin_set_state_type (GstBin *bin,
                        GstElementState state,
                        GtkType type) 
{
  GstBinClass *oclass;

  DEBUG("gst_bin_set_state_type(\"%s\",%d,%d)\n",
          gst_element_get_name (GST_ELEMENT (bin)), state,type);

  g_return_val_if_fail (bin != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);

  oclass = GST_BIN_CLASS (GTK_OBJECT (bin)->klass);

  if (oclass->change_state_type)
    (oclass->change_state_type) (bin,state,type);
  return TRUE;
}

static void 
gst_bin_real_destroy (GtkObject *object) 
{
  GstBin *bin = GST_BIN (object);
  GList *children;
  GstElement *child;

  DEBUG("in gst_bin_real_destroy()\n");

  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    gst_element_destroy (child);
    children = g_list_next (children);
  }

  g_list_free (bin->children);
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
GstElement*
gst_bin_get_by_name (GstBin *bin,
		     gchar *name) 
{
  GList *children;
  GstElement *child;

  g_return_val_if_fail (bin != NULL, NULL);
  g_return_val_if_fail (GST_IS_BIN (bin), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  g_print("gstbin: lookup element \"%s\" in \"%s\"\n", name, 
		  gst_element_get_name (GST_ELEMENT (bin)));
  
  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    if (!strcmp (child->name,name))
      return child;
    if (GST_IS_BIN (child)) {
      GstElement *res = gst_bin_get_by_name (GST_BIN (child), name);
      if (res) 
        return res;
    }
    children = g_list_next (children);
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
GList*
gst_bin_get_list (GstBin *bin) 
{
  g_return_val_if_fail (bin != NULL, NULL);
  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  return bin->children;
}

static xmlNodePtr 
gst_bin_save_thyself (GstElement *element, 
		      xmlNodePtr parent) 
{
  GstBin *bin = GST_BIN (element);
  xmlNodePtr childlist;
  GList *children;
  GstElement *child;

  if (GST_ELEMENT_CLASS (parent_class)->save_thyself)
    GST_ELEMENT_CLASS (parent_class)->save_thyself (GST_ELEMENT (bin), parent);

  childlist = xmlNewChild (parent,NULL,"children",NULL);

  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    gst_element_save_thyself (child, childlist);
    children = g_list_next (children);
  }
  return childlist;
}

static void 
gst_bin_restore_thyself (GstElement *element, 
		         xmlNodePtr parent, 
			 GHashTable *elements) 
{
  GstBin *bin = GST_BIN (element);
  xmlNodePtr field = parent->childs;
  xmlNodePtr childlist;

  g_print("gstbin: restore \"%s\"\n", gst_element_get_name (element));

  while (field) {
    if (!strcmp (field->name, "children")) {
      childlist = field->childs;
      while (childlist) {
        if (!strcmp (childlist->name, "element")) {
          GstElement *element = gst_element_load_thyself (childlist, elements);

	  gst_bin_add (bin, element);
	}
        childlist = childlist->next;
      }
    }

    field = field->next;
  }
  
}

void 
gst_bin_use_cothreads (GstBin *bin, 
		       gboolean enabled) 
{
  g_return_if_fail (GST_IS_BIN (bin));

  bin->use_cothreads = enabled;
}

/**
 * gst_bin_iterate:
 * @bin: #Gstbin to iterate
 *
 * iterates over the elements in this bin
 */
void 
gst_bin_iterate (GstBin *bin) 
{
  GstBinClass *oclass;

  DEBUG_ENTER("(\"%s\")",gst_element_get_name(GST_ELEMENT(bin)));

  oclass = GST_BIN_CLASS (GTK_OBJECT (bin)->klass);
  
  if (oclass->iterate)
    (oclass->iterate) (bin);

  DEBUG_LEAVE("(\"%s\")",gst_element_get_name(GST_ELEMENT(bin)));
}

/**
 * gst_bin_create_plan:
 * @bin: #Gstbin to create the plan for
 *
 * let the bin figure out how to handle the plugins in it.
 */
void 
gst_bin_create_plan (GstBin *bin) 
{
  GstBinClass *oclass;

  oclass = GST_BIN_CLASS (GTK_OBJECT (bin)->klass);

  if (oclass->create_plan)
    (oclass->create_plan) (bin);
}

typedef struct {
  gulong offset;
  gulong size;
} region_struct; 

static int 
gst_bin_loopfunc_wrapper (int argc,char *argv[]) 
{
  GstElement *element = GST_ELEMENT (argv);
  GList *pads;
  GstPad *pad;
  GstBuffer *buf;
  G_GNUC_UNUSED const gchar *name = gst_element_get_name (element);

  DEBUG_ENTER("(%d,'%s')",argc,name);

//  DEBUG("entering gst_bin_loopfunc_wrapper(%d,\"%s\")\n",
//          argc,gst_element_get_name (element));

  if (element->loopfunc != NULL) {
    DEBUG("element %s has loop function, calling it\n", name);
    (element->loopfunc) (element);
    DEBUG("element %s ended loop function\n", name);
  } else {
    DEBUG("element %s is chain-based\n", name);
    DEBUG("stepping through pads\n");
    do {
      pads = element->pads;
      while (pads) {
        pad = GST_PAD (pads->data);
        if (pad->direction == GST_PAD_SINK) {
          DEBUG("pulling a buffer from %s:%s\n", name, gst_pad_get_name (pad));
          buf = gst_pad_pull (pad);
          DEBUG("calling chain function of %s:%s\n", name, gst_pad_get_name (pad));
          (pad->chainfunc) (pad,buf);
          DEBUG("calling chain function of %s:%s done\n", name, gst_pad_get_name (pad));
        }
        pads = g_list_next (pads);
      }
    } while (!GST_ELEMENT_IS_COTHREAD_STOPPING(element));
  }
  GST_FLAG_UNSET(element,GST_ELEMENT_COTHREAD_STOPPING);

  DEBUG_LEAVE("(%d,'%s')",argc,name);
  return 0;
}

static int
gst_bin_src_wrapper (int argc,char *argv[]) 
{
  GstElement *element = GST_ELEMENT (argv);
  GList *pads;
  GstPad *pad;
  G_GNUC_UNUSED const gchar *name = gst_element_get_name (element);

  DEBUG_ENTER("(%d,\"%s\")",argc,name);

  do {
    pads = element->pads;
    while (pads) {
      pad = GST_PAD (pads->data);
      if (pad->direction == GST_PAD_SRC) {
        region_struct *region = cothread_get_data (element->threadstate, "region");
        DEBUG("calling _getfunc for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
        if (region) {
 	  //gst_src_push_region (GST_SRC (element), region->offset, region->size);
          if (pad->getregionfunc == NULL) 
	    fprintf(stderr,"error, no getregionfunc in \"%s\"\n", name);
          (pad->getregionfunc)(pad, region->offset, region->size);
 	} else {
          if (pad->getfunc == NULL) 
 	    fprintf(stderr,"error, no getfunc in \"%s\"\n", name);
          (pad->getfunc)(pad);
 	}
      }
      pads = g_list_next(pads);
    }
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING(element));
  GST_FLAG_UNSET(element,GST_ELEMENT_COTHREAD_STOPPING);

  DEBUG_LEAVE("");
  return 0;
}

/*
static void 
gst_bin_pullfunc_proxy (GstPad *pad) 
{
  DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));
  cothread_switch (GST_ELEMENT(pad->parent)->threadstate);
}
*/

static void 
gst_bin_pullregionfunc_proxy (GstPad *pad, 
				gulong offset, 
				gulong size) 
{
  region_struct region;
  cothread_state *threadstate;

  DEBUG_ENTER("%s:%s,%ld,%ld",GST_DEBUG_PAD_NAME(pad),offset,size);

  region.offset = offset;
  region.size = size;

  threadstate = GST_ELEMENT(pad->parent)->threadstate;
  cothread_set_data (threadstate, "region", &region);
  cothread_switch (threadstate);
  cothread_set_data (threadstate, "region", NULL);
}

static void 
gst_bin_pushfunc_proxy (GstPad *pad, GstBuffer *buf) 
{
  cothread_state *threadstate = GST_ELEMENT(pad->parent)->threadstate;
  DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));
  DEBUG("putting buffer in peer's pen\n");
  pad->peer->bufpen = buf;
  DEBUG("switching to %p (@%p)\n",threadstate,&(GST_ELEMENT(pad->parent)->threadstate));
  cothread_switch (threadstate);
  DEBUG("done switching\n");
}

static GstBuffer*
gst_bin_pullfunc_proxy (GstPad *pad) 
{
  GstBuffer *buf;

  cothread_state *threadstate = GST_ELEMENT(pad->parent)->threadstate;
  DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));
  if (pad->bufpen == NULL) {
    DEBUG("switching to %p (@%p)\n",threadstate,&(GST_ELEMENT(pad->parent)->threadstate));
    cothread_switch (threadstate);
  }
  DEBUG("done switching\n");
  buf = pad->bufpen;
  pad->bufpen = NULL;
  return buf;
}

static void
gst_bin_pushfunc_fake_proxy (GstPad *pad)
{
}

static void
gst_bin_create_plan_func (GstBin *bin) 
{
  GList *elements;
  GstElement *element;
  int sink_pads;
  GList *pads;
  GstPad *pad, *peer;
  GstElement *outside;

  DEBUG_SET_STRING("(\"%s\")",gst_element_get_name(GST_ELEMENT(bin)));
  DEBUG_ENTER_STRING;

  // first loop through all children to see if we need cothreads
  // we break immediately when we find we need to, why keep searching?
  elements = bin->children;
  while (elements) {
    element = GST_ELEMENT (elements->data);

    DEBUG("found element \"%s\" in bin \"%s\"\n", 
	  gst_element_get_name (element), 
	  gst_element_get_name (GST_ELEMENT (bin)));

    // if it's a loop-based element, use cothreads
    if (element->loopfunc != NULL) {
      DEBUG("loop based element \"%s\" in bin \"%s\"\n", 
            gst_element_get_name (element), 
            gst_element_get_name (GST_ELEMENT (bin)));

      bin->need_cothreads = TRUE;
      DEBUG("NEED COTHREADS, it's \"%s\"'s fault\n",gst_element_get_name(element));
      break;
    }

    // if it's a complex element, use cothreads
    else if (GST_ELEMENT_IS_MULTI_IN (element)) {
      DEBUG("complex element \"%s\" in bin \"%s\"\n", 
            gst_element_get_name (element), 
            gst_element_get_name (GST_ELEMENT (bin)));

      bin->need_cothreads = TRUE;
      DEBUG("NEED COTHREADS, it's \"%s\"'s fault\n",gst_element_get_name(element));
      break;
    }

    // if it has more than one input pad, use cothreads
    sink_pads = 0;
    pads = gst_element_get_pad_list (element);
    while (pads) {
      pad = GST_PAD (pads->data);
      if (pad->direction == GST_PAD_SINK)
        sink_pads++;
      pads = g_list_next (pads);
    }
    if (sink_pads > 1) {
      DEBUG("more than 1 sinkpad for element \"%s\" in bin \"%s\"\n", 
            gst_element_get_name (element),
            gst_element_get_name (GST_ELEMENT (bin)));

      bin->need_cothreads = TRUE;
      DEBUG("NEED COTHREADS, it's \"%s\"'s fault\n",gst_element_get_name(element));
      break;
    }

    elements = g_list_next (elements);
  }

  // FIXME
//  bin->need_cothreads &= bin->use_cothreads;
  // FIXME temporary testing measure
  if (bin->use_cothreads) bin->need_cothreads = TRUE;

  // clear previous plan state
  g_list_free (bin->entries);
  bin->entries = NULL;
  bin->numentries = 0;

  if (bin->need_cothreads) {
    // first create thread context
    if (bin->threadcontext == NULL) {
      bin->threadcontext = cothread_init ();
      DEBUG("initialized cothread context\n");
    }

    // walk through all the children
    elements = bin->children;
    while (elements) {
      element = GST_ELEMENT (elements->data);

      // start by creating thread state for the element
      if (element->threadstate == NULL) {
        element->threadstate = cothread_create (bin->threadcontext);
        cothread_setfunc (element->threadstate, gst_bin_loopfunc_wrapper,
                          0, (char **)element);
        DEBUG("created cothread %p (@%p) for \"%s\"\n",element->threadstate,
              &element->threadstate,gst_element_get_name(element));
      }

      if (GST_IS_BIN (element)) {
        gst_bin_create_plan (GST_BIN (element));

      } else if (GST_IS_SRC (element)) {
        DEBUG("adding '%s' as entry point, because it's a source\n",gst_element_get_name (element));
        bin->entries = g_list_prepend (bin->entries,element);
        bin->numentries++;
        cothread_setfunc(element->threadstate,gst_bin_src_wrapper,0,(char **)element);
      }

      pads = gst_element_get_pad_list (element);
      while (pads) {
        pad = GST_PAD(pads->data);

        if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
          DEBUG("setting push proxy for sinkpad %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          // set the proxy functions
          pad->pushfunc = GST_DEBUG_FUNCPTR(gst_bin_pushfunc_proxy);
          DEBUG("pushfunc %p = gst_bin_pushfunc_proxy %p\n",&pad->pushfunc,gst_bin_pushfunc_proxy);
        } else if (gst_pad_get_direction (pad) == GST_PAD_SRC) {
          DEBUG("setting pull proxies for srcpad %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          // set the proxy functions
          pad->pullfunc = GST_DEBUG_FUNCPTR(gst_bin_pullfunc_proxy);
          DEBUG("pad->pullfunc(@%p) = gst_bin_pullfunc_proxy(@%p)\n",
                &pad->pullfunc,gst_bin_pullfunc_proxy);
          pad->pullregionfunc = GST_DEBUG_FUNCPTR(gst_bin_pullregionfunc_proxy);
        }
        pads = g_list_next (pads);
      }
      elements = g_list_next (elements);

      // if there are no entries, we have to pick one at random
      if (bin->numentries == 0)
        bin->entries = g_list_prepend (bin->entries, GST_ELEMENT(bin->children->data));
    }
  } else {
    DEBUG("don't need cothreads, looking for entry points\n");
    // we have to find which elements will drive an iteration
    elements = bin->children;
    while (elements) {
      element = GST_ELEMENT (elements->data);
      DEBUG("found element \"%s\"\n", gst_element_get_name (element));
      if (GST_IS_BIN (element)) {
        gst_bin_create_plan (GST_BIN (element));
      }
      if (GST_IS_SRC (element)) {
        DEBUG("adding '%s' as entry point, because it's a source\n",gst_element_get_name (element));
        bin->entries = g_list_prepend (bin->entries, element);
        bin->numentries++;
      }

      // go through all the pads, set pointers, and check for connections
      pads = gst_element_get_pad_list (element);
      while (pads) {
        pad = GST_PAD (pads->data);

        if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
	  DEBUG("found SINK pad %s:%s\n", GST_DEBUG_PAD_NAME(pad));

          // copy the peer's chain function, easy enough
          DEBUG("copying peer's chainfunc to %s:%s's pushfunc\n",GST_DEBUG_PAD_NAME(pad));
          pad->pushfunc = pad->peer->chainfunc;

          // need to walk through and check for outside connections
//FIXME need to do this for all pads
          /* get the pad's peer */
          peer = gst_pad_get_peer (pad);
          if (!peer) {
	    DEBUG("found SINK pad %s has no peer\n", gst_pad_get_name (pad));
	    break;
	  }
          /* get the parent of the peer of the pad */
          outside = GST_ELEMENT (gst_pad_get_parent (peer));
          if (!outside) break;
          /* if it's a connection and it's not ours... */
          if (GST_IS_CONNECTION (outside) &&
               (gst_object_get_parent (GST_OBJECT (outside)) != GST_OBJECT (bin))) {
            gst_info("gstbin: element \"%s\" is the external source Connection "
				    "for internal element \"%s\"\n",
	                  gst_element_get_name (GST_ELEMENT (outside)),
	                  gst_element_get_name (GST_ELEMENT (element)));
	    bin->entries = g_list_prepend (bin->entries, outside);
	    bin->numentries++;
	  }
	}
	else {
	  DEBUG("found pad %s\n", gst_pad_get_name (pad));
	}
	pads = g_list_next (pads);

      }
      elements = g_list_next (elements);
    }
  }

  DEBUG_LEAVE("(\"%s\")",gst_element_get_name(GST_ELEMENT(bin)));
}

void 
gst_bin_iterate_func (GstBin *bin) 
{
  GList *entries;
  GstElement *entry;
  GList *pads;
  GstPad *pad;
  _GstBinOutsideSchedule *sched;

  DEBUG_SET_STRING("(\"%s\")", gst_element_get_name (GST_ELEMENT (bin)));
  DEBUG_ENTER_STRING;

  g_return_if_fail (bin != NULL);
  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (GST_STATE (bin) == GST_STATE_PLAYING);

  if (bin->need_cothreads) {
    // all we really have to do is switch to the first child
    // FIXME this should be lots more intelligent about where to start
    DEBUG("starting iteration via cothreads\n");

    if (GST_IS_ELEMENT(bin->entries->data)) {
      entry = GST_ELEMENT (bin->entries->data);
      GST_FLAG_SET (entry, GST_ELEMENT_COTHREAD_STOPPING);
      DEBUG("set COTHREAD_STOPPING flag on \"%s\"(@%p)\n",
            gst_element_get_name(entry),entry);
      cothread_switch (entry->threadstate);
    } else {
      sched = (_GstBinOutsideSchedule *) (bin->entries->data);
      sched->flags |= GST_ELEMENT_COTHREAD_STOPPING;
      DEBUG("set COTHREAD STOPPING flag on sched for \"%s\"(@%p)\n",
            gst_element_get_name(sched->element),sched->element);
      cothread_switch (sched->threadstate);
    }

  } else {
    DEBUG("starting iteration via chain-functions\n");

    if (bin->numentries <= 0) {
      //printf("gstbin: no entries in bin \"%s\" trying children...\n", gst_element_get_name(GST_ELEMENT(bin)));
      // we will try loop over the elements then...
      entries = bin->children;
    }
    else {
      entries = bin->entries;
    }

    g_assert (entries != NULL);

    while (entries) {
      entry = GST_ELEMENT (entries->data);
      if (GST_IS_SRC (entry) || GST_IS_CONNECTION (entry)) {
        pads = entry->pads;
        while (pads) {
          pad = GST_PAD (pads->data);
          if (pad->direction == GST_PAD_SRC) {
            DEBUG("calling getfunc of %s:%s\n",GST_DEBUG_PAD_NAME(pad));
            if (pad->getfunc == NULL) 
	      fprintf(stderr, "error, no getfunc in \"%s\"\n", gst_element_get_name (entry));
	    else
              (pad->getfunc)(pad);
          }
          pads = g_list_next (pads);
        }
//      } else if (GST_IS_CONNECTION (entry)) {
//        gst_connection_push (GST_CONNECTION (entry));
      } else if (GST_IS_BIN (entry))
        gst_bin_iterate (GST_BIN (entry));
      else {
	fprintf(stderr, "gstbin: entry \"%s\" cannot be handled\n", gst_element_get_name (entry));
        g_assert_not_reached ();
      }
      entries = g_list_next (entries);
    }
  }

  DEBUG_LEAVE("(%s)", gst_element_get_name (GST_ELEMENT (bin)));
}



/*
        // ***** check for possible connections outside
        // get the pad's peer
        peer = gst_pad_get_peer (pad);
        // FIXME this should be an error condition, if not disabled
        if (!peer) break;
        // get the parent of the peer of the pad
        outside = GST_ELEMENT (gst_pad_get_parent (peer));
        // FIXME this should *really* be an error condition
        if (!outside) break;
        // if it's a source or connection and it's not ours...
        if ((GST_IS_SRC (outside) || GST_IS_CONNECTION (outside)) &&
            (gst_object_get_parent (GST_OBJECT (outside)) != GST_OBJECT (bin))) {
          if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
            DEBUG("dealing with outside source element %s\n",gst_element_get_name(outside));
//            DEBUG("PUNT: copying pullfunc ptr from %s:%s to %s:%s (@ %p)\n",
//GST_DEBUG_PAD_NAME(pad->peer),GST_DEBUG_PAD_NAME(pad),&pad->pullfunc);
//            pad->pullfunc = pad->peer->pullfunc;
//            DEBUG("PUNT: setting pushfunc proxy to fake proxy on %s:%s\n",GST_DEBUG_PAD_NAME(pad->peer));
//            pad->peer->pushfunc = GST_DEBUG_FUNCPTR(gst_bin_pushfunc_fake_proxy);
            pad->pullfunc = GST_DEBUG_FUNCPTR(gst_bin_pullfunc_proxy);
          }
        } else {
*/
