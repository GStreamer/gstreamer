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
#include "config.h"
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


static void			gst_bin_real_destroy		(GtkObject *object);

static GstElementStateReturn	gst_bin_change_state		(GstElement *element);
static GstElementStateReturn	gst_bin_change_state_norecurse	(GstBin *bin);
static gboolean			gst_bin_change_state_type	(GstBin *bin,
								 GstElementState state,
								 GtkType type);

static void			gst_bin_create_plan_func	(GstBin *bin);
static gboolean			gst_bin_iterate_func		(GstBin *bin);

static xmlNodePtr		gst_bin_save_thyself		(GstObject *object, xmlNodePtr parent);
static void			gst_bin_restore_thyself		(GstObject *object, xmlNodePtr self);

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
  GstObjectClass *gstobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);

  gst_bin_signals[OBJECT_ADDED] =
    gtk_signal_new ("object_added", GTK_RUN_FIRST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstBinClass, object_added),
                    gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
                    GST_TYPE_ELEMENT);
  gtk_object_class_add_signals (gtkobject_class, gst_bin_signals, LAST_SIGNAL);

  klass->change_state_type =		gst_bin_change_state_type;
  klass->create_plan =			gst_bin_create_plan_func;
  klass->schedule =			gst_bin_schedule_func;
  klass->iterate =			gst_bin_iterate_func;

  gstobject_class->save_thyself =	gst_bin_save_thyself;
  gstobject_class->restore_thyself =	gst_bin_restore_thyself;

  gstelement_class->change_state =	gst_bin_change_state;

  gtkobject_class->destroy =		gst_bin_real_destroy;
}

static void
gst_bin_init (GstBin *bin)
{
  // in general, we prefer to use cothreads for most things
  GST_FLAG_SET (bin, GST_BIN_FLAG_PREFER_COTHREADS);

  bin->numchildren = 0;
  bin->children = NULL;
  bin->eos_providers = NULL;
  bin->num_eos_providers = 0;
  bin->chains = NULL;
  bin->eoscond = g_cond_new ();
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
gst_bin_new (const gchar *name)
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

  GST_INFO_ELEMENT (GST_CAT_PARENTAGE, bin, "added child %s", GST_ELEMENT_NAME (element));

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
    GST_ERROR_OBJECT(bin,element,"no such element in bin");
    return;
  }

  gst_object_unparent (GST_OBJECT (element));
  bin->children = g_list_remove (bin->children, element);
  bin->numchildren--;

  GST_INFO_ELEMENT (GST_CAT_PARENTAGE, bin, "removed child %s", GST_ELEMENT_NAME (element));

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

  GST_DEBUG_ENTER("(\"%s\")",GST_ELEMENT_NAME  (element));

  g_return_val_if_fail (GST_IS_BIN (element), GST_STATE_FAILURE);

  bin = GST_BIN (element);

//  GST_DEBUG (0,"currently %d(%s), %d(%s) pending\n",GST_STATE (element),
//          _gst_print_statename (GST_STATE (element)), GST_STATE_PENDING (element),
//          _gst_print_statename (GST_STATE_PENDING (element)));

  GST_INFO_ELEMENT (GST_CAT_STATES, element, "changing bin's state from %s to %s",
                _gst_print_statename (GST_STATE (element)),
                _gst_print_statename (GST_STATE_PENDING (element)));

//  g_return_val_if_fail(bin->numchildren != 0, GST_STATE_FAILURE);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
    {
      GstObject *parent;

      parent = gst_object_get_parent (GST_OBJECT (element));

      if (!parent || !GST_IS_BIN (parent))
        gst_bin_create_plan (bin);
      else
        GST_DEBUG (0,"not creating plan for '%s'\n",GST_ELEMENT_NAME  (bin));

      break;
    }
    default:
      break;
  }

//  g_print("-->\n");
  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    GST_DEBUG (0,"setting state on '%s'\n",GST_ELEMENT_NAME  (child));
    switch (gst_element_set_state (child, GST_STATE_PENDING (element))) {
      case GST_STATE_FAILURE:
        GST_STATE_PENDING (element) = GST_STATE_NONE_PENDING;
        GST_DEBUG (0,"child '%s' failed to go to state %d(%s)\n", GST_ELEMENT_NAME  (child),
              GST_STATE_PENDING (element), _gst_print_statename (GST_STATE_PENDING (element)));
        return GST_STATE_FAILURE;
        break;
      case GST_STATE_ASYNC:
        GST_DEBUG (0,"child '%s' is changing state asynchronously\n", GST_ELEMENT_NAME  (child));
        break;
    }
//    g_print("\n");
    children = g_list_next (children);
  }
//  g_print("<-- \"%s\"\n",gst_object_get_name(GST_OBJECT(bin)));


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

  GST_DEBUG (0,"gst_bin_set_state_type(\"%s\",%d,%d)\n",
          GST_ELEMENT_NAME (bin), state,type);

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

  GST_DEBUG (0,"in gst_bin_real_destroy()\n");

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
 * Get the element with the given name from this bin.
 *
 * Returns: the element with the given name
 */
GstElement*
gst_bin_get_by_name (GstBin *bin,
		     const gchar *name)
{
  GList *children;
  GstElement *child;

  g_return_val_if_fail (bin != NULL, NULL);
  g_return_val_if_fail (GST_IS_BIN (bin), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  GST_INFO_ELEMENT (GST_CAT_PARENTAGE, bin, "looking up child element %s", name);

  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    if (!strcmp (gst_object_get_name (GST_OBJECT (child)),name))
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
 * gst_bin_get_by_name_recurse_up:
 * @bin: #Gstbin to search
 * @name: the element name to search for
 *
 * Get the element with the given name from this bin. If the
 * element is not found, a recursion is performed on the parent bin.
 *
 * Returns: the element with the given name
 */
GstElement*
gst_bin_get_by_name_recurse_up (GstBin *bin,
		                const gchar *name)
{
  GstElement *result = NULL;
  GstObject *parent;

  g_return_val_if_fail (bin != NULL, NULL);
  g_return_val_if_fail (GST_IS_BIN (bin), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  result = gst_bin_get_by_name (bin, name);

  if (result)
    return result;

  parent = gst_object_get_parent (GST_OBJECT (bin));

  if (parent && GST_IS_BIN (parent)) {
    result = gst_bin_get_by_name_recurse_up (GST_BIN (parent), name);
  }

  return result;
}

/**
 * gst_bin_get_list:
 * @bin: #Gstbin to get the list from
 *
 * Get the list of elements in this bin.
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
gst_bin_save_thyself (GstObject *object,
		      xmlNodePtr parent)
{
  GstBin *bin = GST_BIN (object);
  xmlNodePtr childlist, elementnode;
  GList *children;
  GstElement *child;

  if (GST_OBJECT_CLASS (parent_class)->save_thyself)
    GST_OBJECT_CLASS (parent_class)->save_thyself (GST_OBJECT (bin), parent);

  childlist = xmlNewChild (parent, NULL, "children", NULL);

  GST_INFO_ELEMENT (GST_CAT_XML, bin, "saving %d children", bin->numchildren);

  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    elementnode = xmlNewChild (childlist, NULL, "element", NULL);
    gst_object_save_thyself (GST_OBJECT (child), elementnode);
    children = g_list_next (children);
  }
  return childlist;
}

static void
gst_bin_restore_thyself (GstObject *object,
		         xmlNodePtr self)
{
  GstBin *bin = GST_BIN (object);
  xmlNodePtr field = self->xmlChildrenNode;
  xmlNodePtr childlist;

  while (field) {
    if (!strcmp (field->name, "children")) {
      GST_INFO_ELEMENT (GST_CAT_XML, GST_ELEMENT (object), "loading children");
      childlist = field->xmlChildrenNode;
      while (childlist) {
        if (!strcmp (childlist->name, "element")) {
          GstElement *element = gst_element_load_thyself (childlist, GST_OBJECT (bin));

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
 * Iterates over the elements in this bin.
 *
 * Returns: TRUE if the bin did something usefull. This value
 *          can be used to determine it the bin is in EOS.
 */
gboolean
gst_bin_iterate (GstBin *bin)
{
  GstBinClass *oclass;
  gboolean eos = TRUE;

  GST_DEBUG_ENTER("(\"%s\")",GST_ELEMENT_NAME (bin));

  oclass = GST_BIN_CLASS (GTK_OBJECT (bin)->klass);

  if (oclass->iterate)
    eos = (oclass->iterate) (bin);

  GST_DEBUG_LEAVE("(\"%s\")",GST_ELEMENT_NAME (bin));

  return eos;
}

/**
 * gst_bin_create_plan:
 * @bin: #GstBin to create the plan for
 *
 * Let the bin figure out how to handle its children.
 */
void
gst_bin_create_plan (GstBin *bin)
{
  GstBinClass *oclass;

  oclass = GST_BIN_CLASS (GTK_OBJECT (bin)->klass);

  if (oclass->create_plan)
    (oclass->create_plan) (bin);
}

/* out internal element fired EOS, we decrement the number of pending EOS childs */
static void
gst_bin_received_eos (GstElement *element, GstBin *bin)
{
  GST_INFO_ELEMENT (GST_CAT_PLANNING, bin, "child %s fired eos, pending %d\n", GST_ELEMENT_NAME (element),
		  bin->num_eos_providers);

  GST_LOCK (bin);
  if (bin->num_eos_providers) {
    bin->num_eos_providers--;
    g_cond_signal (bin->eoscond);
  }
  GST_UNLOCK (bin);
}

/**
 * gst_bin_schedule:
 * @bin: #GstBin to schedule
 *
 * Let the bin figure out how to handle its children.
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
#ifdef GST_DEBUG_ENABLED
  const gchar *elementname;
#endif
  GSList *pending = NULL;
  GstBin *pending_bin;

  GST_DEBUG_ENTER("(\"%s\")",GST_ELEMENT_NAME  (bin));

  GST_INFO_ELEMENT (GST_CAT_PLANNING, bin, "creating plan");

  // first figure out which element is the manager of this and all child elements
  // if we're a managing bin ourselves, that'd be us
  if (GST_FLAG_IS_SET (bin, GST_BIN_FLAG_MANAGER)) {
    manager = GST_ELEMENT (bin);
    GST_DEBUG (0,"setting manager to self\n");
  // otherwise, it's what our parent says it is
  } else {
    manager = gst_element_get_manager (GST_ELEMENT (bin));
    if (!manager) {
      GST_DEBUG (0,"manager not set for element \"%s\" assuming manager is self\n", GST_ELEMENT_NAME (bin));
      manager = GST_ELEMENT (bin);
      GST_FLAG_SET (bin, GST_BIN_FLAG_MANAGER);
    }
    GST_DEBUG (0,"setting manager to \"%s\"\n", GST_ELEMENT_NAME (manager));
  }
  gst_element_set_manager (GST_ELEMENT (bin), manager);

  // perform the first recursive pass of plan generation
  // we set the manager of every element but those who manage themselves
  // the need for cothreads is also determined recursively
  GST_DEBUG (0,"performing first-phase recursion\n");
  bin->need_cothreads = bin->use_cothreads;
  if (bin->need_cothreads)
    GST_DEBUG (0,"requiring cothreads because we're forced to\n");

  elements = bin->children;
  while (elements) {
    element = GST_ELEMENT (elements->data);
    elements = g_list_next (elements);
#ifdef GST_DEBUG_ENABLED
    elementname = GST_ELEMENT_NAME  (element);
#endif
    GST_DEBUG (0,"have element \"%s\"\n",elementname);

    // first set their manager
    GST_DEBUG (0,"setting manager of \"%s\" to \"%s\"\n",elementname,GST_ELEMENT_NAME (manager));
    gst_element_set_manager (element, manager);

    // we do recursion and such for Bins
    if (GST_IS_BIN (element)) {
      // recurse into the child Bin
      GST_DEBUG (0,"recursing into child Bin \"%s\" with manager \"%s\"\n",elementname,
		      GST_ELEMENT_NAME (element->manager));
      gst_bin_create_plan (GST_BIN (element));
      GST_DEBUG (0,"after recurse got manager \"%s\"\n",
		      GST_ELEMENT_NAME (element->manager));
      // check to see if it needs cothreads and isn't self-managing
      if (((GST_BIN (element))->need_cothreads) && !GST_FLAG_IS_SET(element,GST_BIN_FLAG_MANAGER)) {
        GST_DEBUG (0,"requiring cothreads because child bin \"%s\" does\n",elementname);
        bin->need_cothreads = TRUE;
      }
    } else {
      // then we need to determine whether they need cothreads
      // if it's a loop-based element, use cothreads
      if (element->loopfunc != NULL) {
        GST_DEBUG (0,"requiring cothreads because \"%s\" is a loop-based element\n",elementname);
        GST_FLAG_SET (element, GST_ELEMENT_USE_COTHREAD);
      // if it's a 'complex' element, use cothreads
      } else if (GST_FLAG_IS_SET (element, GST_ELEMENT_COMPLEX)) {
        GST_DEBUG (0,"requiring cothreads because \"%s\" is complex\n",elementname);
        GST_FLAG_SET (element, GST_ELEMENT_USE_COTHREAD);
      // if the element has more than one sink pad, use cothreads
      } else if (element->numsinkpads > 1) {
        GST_DEBUG (0,"requiring cothreads because \"%s\" has more than one sink pad\n",elementname);
        GST_FLAG_SET (element, GST_ELEMENT_USE_COTHREAD);
      }
      if (GST_FLAG_IS_SET (element, GST_ELEMENT_USE_COTHREAD))
        bin->need_cothreads = TRUE;
    }
  }


  // if we're not a manager thread, we're done.
  if (!GST_FLAG_IS_SET (bin, GST_BIN_FLAG_MANAGER)) {
    GST_DEBUG_LEAVE("(\"%s\")",GST_ELEMENT_NAME (bin));
    return;
  }

  // clear previous plan state
  g_list_free (bin->managed_elements);
  bin->managed_elements = NULL;
  bin->num_managed_elements = 0;

  // find all the managed children
  // here we pull off the trick of walking an entire arbitrary tree without recursion
  GST_DEBUG (0,"attempting to find all the elements to manage\n");
  pending = g_slist_prepend (pending, bin);
  do {
    // retrieve the top of the stack and pop it
    pending_bin = GST_BIN (pending->data);
    pending = g_slist_remove (pending, pending_bin);

    // walk the list of elements, find bins, and do stuff
    GST_DEBUG (0,"checking Bin \"%s\" for managed elements\n",
          GST_ELEMENT_NAME  (pending_bin));
    elements = pending_bin->children;
    while (elements) {
      element = GST_ELEMENT (elements->data);
      elements = g_list_next (elements);
#ifdef GST_DEBUG_ENABLED
      elementname = GST_ELEMENT_NAME  (element);
#endif

      // if it's ours, add it to the list
      if (element->manager == GST_ELEMENT(bin)) {
        // if it's a Bin, add it to the list of Bins to check
        if (GST_IS_BIN (element)) {
          GST_DEBUG (0,"flattened recurse into \"%s\"\n",elementname);
          pending = g_slist_prepend (pending, element);

        // otherwise add it to the list of elements
        } else {
          GST_DEBUG (0,"found element \"%s\" that I manage\n",elementname);
          bin->managed_elements = g_list_prepend (bin->managed_elements, element);
          bin->num_managed_elements++;
        }
      }
      // else it's not ours and we need to wait for EOS notifications
      else {
        gtk_signal_connect (GTK_OBJECT (element), "eos", gst_bin_received_eos, bin);
        bin->eos_providers = g_list_prepend (bin->eos_providers, element);
        bin->num_eos_providers++;
      }
    }
  } while (pending);

  GST_DEBUG (0,"have %d elements to manage, implementing plan\n",bin->num_managed_elements);

  gst_bin_schedule(bin);

//  g_print ("gstbin \"%s\", eos providers:%d\n",
//		  GST_ELEMENT_NAME (bin),
//		  bin->num_eos_providers);

  GST_DEBUG_LEAVE("(\"%s\")",GST_ELEMENT_NAME (bin));
}

static gboolean
gst_bin_iterate_func (GstBin *bin)
{
  GList *chains;
  _GstBinChain *chain;
  GList *entries;
  GstElement *entry;
  GList *pads;
  GstPad *pad;
  GstBuffer *buf = NULL;
  gint num_scheduled = 0;
  gboolean eos = FALSE;

  GST_DEBUG_ENTER("(\"%s\")", GST_ELEMENT_NAME (bin));

  g_return_val_if_fail (bin != NULL, TRUE);
  g_return_val_if_fail (GST_IS_BIN (bin), TRUE);
  g_return_val_if_fail (GST_STATE (bin) == GST_STATE_PLAYING, TRUE);

  // step through all the chains
  chains = bin->chains;
  while (chains) {
    chain = (_GstBinChain *)(chains->data);
    chains = g_list_next (chains);

    if (!chain->need_scheduling) continue;

    if (chain->need_cothreads) {
      GList *entries;

      // all we really have to do is switch to the first child
      // FIXME this should be lots more intelligent about where to start
      GST_DEBUG (0,"starting iteration via cothreads\n");

      entries = chain->elements;
      entry = NULL;

      // find an element with a threadstate to start with
      while (entries) {
        entry = GST_ELEMENT (entries->data);

        if (entry->threadstate)
          break;
        entries = g_list_next (entries);
      }
      // if we couldn't find one, bail out
      if (entries == NULL)
        GST_ERROR(GST_ELEMENT(bin),"no cothreaded elements found!");

      GST_FLAG_SET (entry, GST_ELEMENT_COTHREAD_STOPPING);
      GST_DEBUG (0,"set COTHREAD_STOPPING flag on \"%s\"(@%p)\n",
            GST_ELEMENT_NAME (entry),entry);
      cothread_switch (entry->threadstate);

    } else {
      GST_DEBUG (0,"starting iteration via chain-functions\n");

      entries = chain->entries;

      g_assert (entries != NULL);

      while (entries) {
        entry = GST_ELEMENT (entries->data);
        entries = g_list_next (entries);

        GST_DEBUG (0,"have entry \"%s\"\n",GST_ELEMENT_NAME (entry));

        if (GST_IS_BIN (entry)) {
          gst_bin_iterate (GST_BIN (entry));
        } else {
          pads = entry->pads;
          while (pads) {
            pad = GST_PAD (pads->data);
            if (GST_RPAD_DIRECTION(pad) == GST_PAD_SRC) {
              GST_DEBUG (0,"calling getfunc of %s:%s\n",GST_DEBUG_PAD_NAME(pad));
              if (GST_REAL_PAD(pad)->getfunc == NULL)
                fprintf(stderr, "error, no getfunc in \"%s\"\n", GST_ELEMENT_NAME  (entry));
              else
                buf = (GST_REAL_PAD(pad)->getfunc)(pad);
              if (buf) gst_pad_push(pad,buf);
            }
            pads = g_list_next (pads);
          }
        }
      }
    }
    num_scheduled++;
  }

  // check if nothing was scheduled that was ours..
  if (!num_scheduled) {
    // are there any other elements that are still busy?
    if (bin->num_eos_providers) {
      GST_LOCK (bin);
      GST_DEBUG (0,"waiting for eos providers\n");
      g_cond_wait (bin->eoscond, GST_OBJECT(bin)->lock);
      GST_DEBUG (0,"num eos providers %d\n", bin->num_eos_providers);
      GST_UNLOCK (bin);
    }
    else {
      gst_element_signal_eos (GST_ELEMENT (bin));
      eos = TRUE;
    }
  }

  GST_DEBUG_LEAVE("(%s)", GST_ELEMENT_NAME (bin));
  return !eos;
}

