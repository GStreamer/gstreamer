/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstbin.c: GstBin container object and support code
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

//#define GST_DEBUG_ENABLED
#include "gst_private.h"

#include "gstbin.h"

#include "gstscheduler.h"


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
  klass->schedule = 			gst_bin_schedule_func;
  klass->iterate = 			gst_bin_iterate_func;

  gstelement_class->change_state = 	gst_bin_change_state;
  gstelement_class->save_thyself = 	gst_bin_save_thyself;
  gstelement_class->restore_thyself = 	gst_bin_restore_thyself;

  gtkobject_class->destroy = 		gst_bin_real_destroy;
}

static void 
gst_bin_init (GstBin *bin) 
{
  // in general, we prefer to use cothreads for most things
  GST_FLAG_SET (bin, GST_BIN_FLAG_PREFER_COTHREADS);

  bin->numchildren = 0;
  bin->children = NULL;
  bin->chains = NULL;
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
  return gst_elementfactory_make ("bin", name);
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

  INFO_ELEMENT (GST_INFO_PARENTAGE, bin, "added child %s", gst_element_get_name(element));

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

  if (g_list_find(bin->children, element) == NULL) {
    // FIXME this should be a warning!!!
    ERROR_OBJECT(bin,element,"no such element in bin");
    return;
  }

  gst_object_unparent (GST_OBJECT (element));
  bin->children = g_list_remove (bin->children, element);
  bin->numchildren--;

  INFO_ELEMENT (GST_INFO_PARENTAGE, bin, "removed child %s", gst_element_get_name(element));

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

  DEBUG_ENTER("(\"%s\")",gst_element_get_name (element));

  g_return_val_if_fail (GST_IS_BIN (element), GST_STATE_FAILURE);

  bin = GST_BIN (element);

//  DEBUG("currently %d(%s), %d(%s) pending\n",GST_STATE (element),
//          _gst_print_statename (GST_STATE (element)), GST_STATE_PENDING (element),
//          _gst_print_statename (GST_STATE_PENDING (element)));

  INFO_ELEMENT (GST_INFO_STATES, element, "changing bin's state from %s to %s",
                _gst_print_statename (GST_STATE (element)),
                _gst_print_statename (GST_STATE_PENDING (element)));

//  g_return_val_if_fail(bin->numchildren != 0, GST_STATE_FAILURE);

//  g_print("-->\n");
  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    DEBUG("setting state on '%s'\n",gst_element_get_name (child));
    switch (gst_element_set_state (child, GST_STATE_PENDING (element))) {
      case GST_STATE_FAILURE:
        GST_STATE_PENDING (element) = GST_STATE_NONE_PENDING;
        DEBUG("child '%s' failed to go to state %d(%s)\n", gst_element_get_name (child),
              GST_STATE_PENDING (element), _gst_print_statename (GST_STATE_PENDING (element)));
        return GST_STATE_FAILURE;
        break;
      case GST_STATE_ASYNC:
        DEBUG("child '%s' is changing state asynchronously\n", gst_element_get_name (child));
        break;
    }
//    g_print("\n");
    children = g_list_next (children);
  }
//  g_print("<-- \"%s\"\n",gst_object_get_name(GST_OBJECT(bin)));

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:	  
    {
      GstObject *parent;

      parent = gst_object_get_parent (GST_OBJECT (element));

      if (!parent || !GST_IS_BIN (parent))
        gst_bin_create_plan (bin);

      break;
    }
    default:
      break;
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

  INFO_ELEMENT (GST_INFO_PARENTAGE, bin, "looking up child element %s", name);

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

  INFO_ELEMENT (GST_INFO_XML, bin, "saving %d children", bin->numchildren);

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

//  g_print("gstbin: restore \"%s\"\n", gst_element_get_name (element));

  while (field) {
    if (!strcmp (field->name, "children")) {
      INFO_ELEMENT (GST_INFO_XML, element, "loading children");
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
 * @bin: #GstBin to create the plan for
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

/**
 * gst_bin_schedule:
 * @bin: #GstBin to schedule
 *
 * let the bin figure out how to handle the plugins in it.
 */
void 
gst_bin_schedule (GstBin *bin) 
{
  GstBinClass *oclass;

  oclass = GST_BIN_CLASS (GTK_OBJECT (bin)->klass);

  if (oclass->schedule)
    (oclass->schedule) (bin);
}

typedef struct {
  gulong offset;
  gulong size;
} region_struct; 


static void
gst_bin_create_plan_func (GstBin *bin) 
{
  GstElement *manager;
  GList *elements;
  GstElement *element;
  const gchar *elementname;
  GSList *pending = NULL;
  GstBin *pending_bin;

  DEBUG_SET_STRING("(\"%s\")",gst_element_get_name (GST_ELEMENT (bin)));
  DEBUG_ENTER_STRING;

  INFO_ELEMENT (GST_INFO_PLANNING, bin, "creating plan");

  // first figure out which element is the manager of this and all child elements
  // if we're a managing bin ourselves, that'd be us
  if (GST_FLAG_IS_SET (bin, GST_BIN_FLAG_MANAGER)) {
    manager = GST_ELEMENT (bin);
    DEBUG("setting manager to self\n");
  // otherwise, it's what our parent says it is
  } else {
    manager = gst_element_get_manager (GST_ELEMENT (bin));
    if (!manager) {
      DEBUG("manager not set for element \"%s\" assuming manager is self\n", gst_element_get_name (GST_ELEMENT (bin)));
      manager = GST_ELEMENT (bin);
      GST_FLAG_SET (bin, GST_BIN_FLAG_MANAGER);
    }
    DEBUG("setting manager to \"%s\"\n", gst_element_get_name (manager));
  }

  // perform the first recursive pass of plan generation
  // we set the manager of every element but those who manage themselves
  // the need for cothreads is also determined recursively
  DEBUG("performing first-phase recursion\n");
  bin->need_cothreads = bin->use_cothreads;
  if (bin->need_cothreads)
    DEBUG("requiring cothreads because we're forced to\n");

  elements = bin->children;
  while (elements) {
    element = GST_ELEMENT (elements->data);
    elements = g_list_next (elements);
#ifdef GST_DEBUG_ENABLED
    elementname = gst_element_get_name (element);
#endif
    DEBUG("have element \"%s\"\n",elementname);

    // first set their manager
    DEBUG("setting manager of \"%s\" to \"%s\"\n",elementname,gst_element_get_name(manager));
    gst_element_set_manager (element, manager);

    // we do recursion and such for Bins
    if (GST_IS_BIN (element)) {
      // recurse into the child Bin
      DEBUG("recursing into child Bin \"%s\"\n",elementname);
      gst_bin_create_plan (GST_BIN (element));
      // check to see if it needs cothreads and isn't self-managing
      if (((GST_BIN (element))->need_cothreads) && !GST_FLAG_IS_SET(element,GST_BIN_FLAG_MANAGER)) {
        DEBUG("requiring cothreads because child bin \"%s\" does\n",elementname);
        bin->need_cothreads = TRUE;
      }
    } else {
      // then we need to determine whether they need cothreads
      // if it's a loop-based element, use cothreads
      if (element->loopfunc != NULL) {
        DEBUG("requiring cothreads because \"%s\" is a loop-based element\n",elementname);
        GST_FLAG_SET (element, GST_ELEMENT_USE_COTHREAD);
      // if it's a 'complex' element, use cothreads
      } else if (GST_FLAG_IS_SET (element, GST_ELEMENT_COMPLEX)) {
        DEBUG("requiring cothreads because \"%s\" is complex\n",elementname);
        GST_FLAG_SET (element, GST_ELEMENT_USE_COTHREAD);
      // if the element has more than one sink pad, use cothreads
      } else if (element->numsinkpads > 1) {
        DEBUG("requiring cothreads because \"%s\" has more than one sink pad\n",elementname);
        GST_FLAG_SET (element, GST_ELEMENT_USE_COTHREAD);
      }
      if (GST_FLAG_IS_SET (element, GST_ELEMENT_USE_COTHREAD))
        bin->need_cothreads = TRUE;
    }
  }


  // if we're not a manager thread, we're done.
  if (!GST_FLAG_IS_SET (bin, GST_BIN_FLAG_MANAGER)) {
    DEBUG_LEAVE("(\"%s\")",gst_element_get_name(GST_ELEMENT(bin)));
    return;
  }

  // clear previous plan state
  g_list_free (bin->managed_elements);
  bin->managed_elements = NULL;
  bin->num_managed_elements = 0;

  // find all the managed children
  // here we pull off the trick of walking an entire arbitrary tree without recursion
  DEBUG("attempting to find all the elements to manage\n");
  pending = g_slist_prepend (pending, bin);
  do {
    // retrieve the top of the stack and pop it
    pending_bin = GST_BIN (pending->data);
    pending = g_slist_remove (pending, pending_bin);

    // walk the list of elements, find bins, and do stuff
    DEBUG("checking Bin \"%s\" for managed elements\n",
          gst_element_get_name (GST_ELEMENT (pending_bin)));
    elements = pending_bin->children;
    while (elements) {
      element = GST_ELEMENT (elements->data);
      elements = g_list_next (elements);
#ifdef GST_DEBUG_ENABLED
      elementname = gst_element_get_name (element);
#endif

      // if it's ours, add it to the list
      if (element->manager == GST_ELEMENT(bin)) {
        // if it's a Bin, add it to the list of Bins to check
        if (GST_IS_BIN (element)) {
          DEBUG("flattened recurse into \"%s\"\n",elementname);
          pending = g_slist_prepend (pending, element);
        // otherwise add it to the list of elements
        } else {
          DEBUG("found element \"%s\" that I manage\n",elementname);
          bin->managed_elements = g_list_prepend (bin->managed_elements, element);
          bin->num_managed_elements++;
        }
      }
    }
  } while (pending);

  DEBUG("have %d elements to manage, implementing plan\n",bin->num_managed_elements);

  gst_bin_schedule(bin);

  DEBUG_LEAVE("(\"%s\")",gst_element_get_name(GST_ELEMENT(bin)));
}

static void 
gst_bin_iterate_func (GstBin *bin) 
{
  GList *chains;
  _GstBinChain *chain;
  GList *entries;
  GstElement *entry;
  GList *pads;
  GstPad *pad;
  GstBuffer *buf;

  DEBUG_SET_STRING("(\"%s\")", gst_element_get_name (GST_ELEMENT (bin)));
  DEBUG_ENTER_STRING;

  g_return_if_fail (bin != NULL);
  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (GST_STATE (bin) == GST_STATE_PLAYING);

  // step through all the chains
  chains = bin->chains;
  while (chains) {
    chain = (_GstBinChain *)(chains->data);
    chains = g_list_next (chains);

    if (chain->need_cothreads) {
      // all we really have to do is switch to the first child
      // FIXME this should be lots more intelligent about where to start
      DEBUG("starting iteration via cothreads\n");

      entry = GST_ELEMENT (chain->elements->data);
      GST_FLAG_SET (entry, GST_ELEMENT_COTHREAD_STOPPING);
      DEBUG("set COTHREAD_STOPPING flag on \"%s\"(@%p)\n",
            gst_element_get_name(entry),entry);
      cothread_switch (entry->threadstate);

    } else {
      DEBUG("starting iteration via chain-functions\n");

      entries = chain->entries;

      g_assert (entries != NULL);

      while (entries) {
        entry = GST_ELEMENT (entries->data);
        entries = g_list_next (entries);

        DEBUG("have entry \"%s\"\n",gst_element_get_name(entry));

        if (GST_IS_BIN (entry)) {
          gst_bin_iterate (GST_BIN (entry));
        } else {
          pads = entry->pads;
          while (pads) {
            pad = GST_PAD (pads->data);
            if (pad->direction == GST_PAD_SRC) {
              DEBUG("calling getfunc of %s:%s\n",GST_DEBUG_PAD_NAME(pad));
              if (pad->getfunc == NULL) 
                fprintf(stderr, "error, no getfunc in \"%s\"\n", gst_element_get_name (entry));
              else
                buf = (pad->getfunc)(pad);
              gst_pad_push(pad,buf);
            }
            pads = g_list_next (pads);
          }
        }
      }
    }
  }

  DEBUG_LEAVE("(%s)", gst_element_get_name (GST_ELEMENT (bin)));
}
