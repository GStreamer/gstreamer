/* GStreamer
 * 
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

/* #define GST_DEBUG_ENABLED */
#include "gst_private.h"

#include "gstevent.h"
#include "gstbin.h"
#include "gstxml.h"

#include "gstscheduler.h"

GstElementDetails gst_bin_details = {
  "Generic bin",
  "Generic/Bin",
  "Simple container object",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};

GType _gst_bin_type = 0;

static void 			gst_bin_dispose 		(GObject * object);

static GstElementStateReturn	gst_bin_change_state		(GstElement *element);
static GstElementStateReturn	gst_bin_change_state_norecurse	(GstBin *bin);
static gboolean			gst_bin_change_state_type	(GstBin *bin,
								 GstElementState state,
								 GType type);

static gboolean 		gst_bin_iterate_func 		(GstBin * bin);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr 		gst_bin_save_thyself 		(GstObject * object, xmlNodePtr parent);
static void 			gst_bin_restore_thyself 	(GstObject * object, xmlNodePtr self);
#endif

/* Bin signals and args */
enum
{
  OBJECT_ADDED,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  /* FILL ME */
};

static void 			gst_bin_class_init 		(GstBinClass * klass);
static void 			gst_bin_init 			(GstBin * bin);

static GstElementClass *parent_class = NULL;
static guint gst_bin_signals[LAST_SIGNAL] = { 0 };

GType
gst_bin_get_type (void)
{
  if (!_gst_bin_type) {
    static const GTypeInfo bin_info = {
      sizeof (GstBinClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_bin_class_init,
      NULL,
      NULL,
      sizeof (GstBin),
      8,
      (GInstanceInitFunc) gst_bin_init,
      NULL
    };

    _gst_bin_type = g_type_register_static (GST_TYPE_ELEMENT, "GstBin", &bin_info, 0);
  }
  return _gst_bin_type;
}

static void
gst_bin_class_init (GstBinClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gst_bin_signals[OBJECT_ADDED] =
    g_signal_new ("object_added", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
		  G_STRUCT_OFFSET (GstBinClass, object_added), NULL, NULL,
		  gst_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);

  gobject_class->dispose 		= GST_DEBUG_FUNCPTR (gst_bin_dispose);

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself 	= GST_DEBUG_FUNCPTR (gst_bin_save_thyself);
  gstobject_class->restore_thyself 	= GST_DEBUG_FUNCPTR (gst_bin_restore_thyself);
#endif

  gstelement_class->change_state 	= GST_DEBUG_FUNCPTR (gst_bin_change_state);

  klass->change_state_type 		= GST_DEBUG_FUNCPTR (gst_bin_change_state_type);
  klass->iterate 			= GST_DEBUG_FUNCPTR (gst_bin_iterate_func);
}

static void
gst_bin_init (GstBin * bin)
{
  /* in general, we prefer to use cothreads for most things */
  GST_FLAG_SET (bin, GST_BIN_FLAG_PREFER_COTHREADS);

  bin->numchildren = 0;
  bin->children = NULL;
  
  bin->pre_iterate_func = NULL;
  bin->post_iterate_func = NULL;
  bin->pre_iterate_private = NULL;
  bin->post_iterate_private = NULL;

  bin->iterate_mutex = g_mutex_new ();
  bin->iterate_cond = g_cond_new ();
}

/**
 * gst_bin_new:
 * @name: name of new bin
 *
 * Create a new bin with given name.
 *
 * Returns: new bin
 */
GstElement *
gst_bin_new (const gchar * name)
{
  return gst_element_factory_make ("bin", name);
}

/**
 * gst_bin_get_clock:
 * @bin: a #GstBin to get the clock of
 *
 * Gets the current clock of the (scheduler of the) bin.
 *
 * Returns: the #GstClock of the bin
 */
GstClock*
gst_bin_get_clock (GstBin *bin)
{
  g_return_val_if_fail (bin != NULL, NULL);
  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  if (GST_ELEMENT_SCHED (bin)) 
    return gst_scheduler_get_clock (GST_ELEMENT_SCHED (bin));

  return NULL;
}

/**
 * gst_bin_use_clock:
 * @bin: the bin to set the clock for
 * @clock: the clock to use.
 *
 * Force the bin to use the given clock. Use NULL to 
 * force it to use no clock at all.
 */
void
gst_bin_use_clock (GstBin *bin, GstClock *clock)
{
  g_return_if_fail (bin != NULL);
  g_return_if_fail (GST_IS_BIN (bin));

  if (GST_ELEMENT_SCHED (bin)) 
    gst_scheduler_use_clock (GST_ELEMENT_SCHED (bin), clock);
}

/**
 * gst_bin_auto_clock:
 * @bin: the bin to autoclock
 *
 * Let the bin select a clock automatically.
 */
void
gst_bin_auto_clock (GstBin *bin)
{
  g_return_if_fail (bin != NULL);
  g_return_if_fail (GST_IS_BIN (bin));

  if (GST_ELEMENT_SCHED (bin)) 
    gst_scheduler_auto_clock (GST_ELEMENT_SCHED (bin));
}

static void
gst_bin_set_element_sched (GstElement *element, GstScheduler *sched)
{
  GList *children;
  GstElement *child;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (sched != NULL);
  g_return_if_fail (GST_IS_SCHEDULER (sched));

  GST_INFO (GST_CAT_SCHEDULING, "setting element \"%s\" sched to %p", GST_ELEMENT_NAME (element),
	    sched);

  /* if it's actually a Bin */
  if (GST_IS_BIN (element)) {
    if (GST_FLAG_IS_SET (element, GST_BIN_FLAG_MANAGER)) {
      GST_INFO_ELEMENT (GST_CAT_PARENTAGE, element, "child is already a manager, not resetting");
      gst_scheduler_add_scheduler (sched, GST_ELEMENT_SCHED (element));
      return;
    }

    GST_INFO_ELEMENT (GST_CAT_PARENTAGE, element, "setting children's schedule to parent's");
    gst_scheduler_add_element (sched, element);

    /* set the children's schedule */
    children = GST_BIN (element)->children;
    while (children) {
      child = GST_ELEMENT (children->data);
      children = g_list_next (children);

      gst_bin_set_element_sched (child, sched);
    }
  }
  /* otherwise, if it's just a regular old element */
  else {
    gst_scheduler_add_element (sched, element);
  }
}


static void
gst_bin_unset_element_sched (GstElement *element, GstScheduler *sched)
{
  GList *children;
  GstElement *child;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));

  if (GST_ELEMENT_SCHED (element) == NULL) {
    GST_INFO (GST_CAT_SCHEDULING, "element \"%s\" has no scheduler",
	      GST_ELEMENT_NAME (element));
    return;
  }
  
  GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" from its sched %p",
	    GST_ELEMENT_NAME (element), GST_ELEMENT_SCHED (element));

  /* if it's actually a Bin */
  if (GST_IS_BIN (element)) {

    if (GST_FLAG_IS_SET (element, GST_BIN_FLAG_MANAGER)) {
      GST_INFO_ELEMENT (GST_CAT_PARENTAGE, element,
			"child is already a manager, not unsetting sched");
      if (sched) {
        gst_scheduler_remove_scheduler (sched, GST_ELEMENT_SCHED (element));
      }
      return;
    }
    /* for each child, remove them from their schedule */
    children = GST_BIN (element)->children;
    while (children) {
      child = GST_ELEMENT (children->data);
      children = g_list_next (children);

      gst_bin_unset_element_sched (child, sched);
    }

    gst_scheduler_remove_element (GST_ELEMENT_SCHED (element), element);
  }
  /* otherwise, if it's just a regular old element */
  else {
    gst_scheduler_remove_element (GST_ELEMENT_SCHED (element), element);
  }
}


/**
 * gst_bin_add_many:
 * @bin: the bin to add the elements to
 * @element_1: the first element to add to the bin
 * @...: NULL-terminated list of elements to add to the bin
 * 
 * Add a list of elements to a bin. Uses #gst_bin_add.
 */
void
gst_bin_add_many (GstBin *bin, GstElement *element_1, ...)
{
  va_list args;

  g_return_if_fail (bin != NULL);
  g_return_if_fail (element_1 != NULL);
  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (GST_IS_ELEMENT (element_1));

  va_start (args, element_1);

  while (element_1) {
    gst_bin_add (bin, element_1);
    
    element_1 = va_arg (args, GstElement*);
  }

  va_end (args);
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
gst_bin_add (GstBin *bin, GstElement *element)
{
  gint state_idx = 0;
  GstElementState state;
  GstScheduler *sched;

  g_return_if_fail (bin != NULL);
  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));

  GST_DEBUG (GST_CAT_PARENTAGE, "adding element \"%s\" to bin \"%s\"",
	     GST_ELEMENT_NAME (element), GST_ELEMENT_NAME (bin));

  /* must be not be in PLAYING state in order to modify bin */
  g_return_if_fail (GST_STATE (bin) != GST_STATE_PLAYING);

  /* the element must not already have a parent */
  g_return_if_fail (GST_ELEMENT_PARENT (element) == NULL);

  /* then check to see if the element's name is already taken in the bin */
  if (gst_object_check_uniqueness (bin->children, 
	                           GST_ELEMENT_NAME (element)) == FALSE)
  {
    g_warning ("Name %s is not unique in bin %s, not adding\n",
	       GST_ELEMENT_NAME (element), GST_ELEMENT_NAME (bin));
    return;
  }

  /* set the element's parent and add the element to the bin's list of children */
  gst_object_set_parent (GST_OBJECT (element), GST_OBJECT (bin));

  bin->children = g_list_append (bin->children, element);
  bin->numchildren++;

  /* bump our internal state counter */
  state = GST_STATE (element);
  while (state >>= 1) state_idx++;
  bin->child_states[state_idx]++;

  /* now we have to deal with manager stuff 
   * we can only do this if there's a scheduler: 
   * if we're not a manager, and aren't attached to anything, we have no sched (yet) */
  sched = GST_ELEMENT_SCHED (bin);
  if (sched) {
    gst_bin_set_element_sched (element, sched);
  }

  GST_INFO_ELEMENT (GST_CAT_PARENTAGE, bin, "added child \"%s\"", GST_ELEMENT_NAME (element));

  g_signal_emit (G_OBJECT (bin), gst_bin_signals[OBJECT_ADDED], 0, element);
}

/**
 * gst_bin_remove:
 * @bin: #GstBin to remove element from
 * @element: #GstElement to remove
 *
 * Remove the element from its associated bin, unparenting as well.
 * The element will also be unreferenced so there's no need to call
 * gst_object_unref on it.
 * If you want the element to still exist after removing, you need to call
 * #gst_object_ref before removing it from the bin.
 */
void
gst_bin_remove (GstBin *bin, GstElement *element)
{
  gint state_idx = 0;
  GstElementState state;

  GST_DEBUG_ELEMENT (GST_CAT_PARENTAGE, bin, "trying to remove child %s", GST_ELEMENT_NAME (element));

  g_return_if_fail (bin != NULL);
  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (bin->children != NULL);

  /* must not be in PLAYING state in order to modify bin */
  g_return_if_fail (GST_STATE (bin) != GST_STATE_PLAYING);

  /* the element must have its parent set to the current bin */
  g_return_if_fail (GST_ELEMENT_PARENT (element) == (GstObject *) bin);

  /* the element must be in the bin's list of children */
  if (g_list_find (bin->children, element) == NULL) {
    g_warning ("no element \"%s\" in bin \"%s\"\n", GST_ELEMENT_NAME (element),
	       GST_ELEMENT_NAME (bin));
    return;
  }

  /* remove this element from the list of managed elements */
  gst_bin_unset_element_sched (element, GST_ELEMENT_SCHED (bin));

  /* now remove the element from the list of elements */
  bin->children = g_list_remove (bin->children, element);
  bin->numchildren--;

  /* bump our internal state counter */
  state = GST_STATE (element);
  while (state >>= 1) state_idx++;
  bin->child_states[state_idx]--;

  GST_INFO_ELEMENT (GST_CAT_PARENTAGE, bin, "removed child %s", GST_ELEMENT_NAME (element));

  gst_object_unparent (GST_OBJECT (element));

  /* if we're down to zero children, force state to NULL */
  if (bin->numchildren == 0 && GST_ELEMENT_SCHED (bin) != NULL) {
    GST_STATE_PENDING (bin) = GST_STATE_NULL;
    gst_bin_change_state_norecurse (bin);
  }
}

/**
 * gst_bin_child_state_change:
 * @bin: #GstBin with the child
 * @oldstate: The old child state
 * @newstate: The new child state
 * @child: #GstElement that signaled an changed state
 *
 * An internal function to inform the parent bin about a state change
 * of a child.
 */
void
gst_bin_child_state_change (GstBin *bin, GstElementState oldstate, GstElementState newstate,
			    GstElement *child)
{
  gint old_idx = 0, new_idx = 0, i;

  GST_INFO (GST_CAT_STATES, "child %s changed state in bin %s from %s to %s",
	    GST_ELEMENT_NAME (child), GST_ELEMENT_NAME (bin),
	    gst_element_state_get_name (oldstate), gst_element_state_get_name (newstate));

  while (oldstate >>= 1) old_idx++;
  while (newstate >>= 1) new_idx++;

  GST_LOCK (bin);
  bin->child_states[old_idx]--;
  bin->child_states[new_idx]++;
  
  for (i = GST_NUM_STATES - 1; i >= 0; i--) {
    if (bin->child_states[i] != 0) {
      gint state = (1 << i);
      if (GST_STATE (bin) != state) {
	GST_INFO (GST_CAT_STATES, "bin %s need state change to %s",
		  GST_ELEMENT_NAME (bin), gst_element_state_get_name (state));
	GST_STATE_PENDING (bin) = state;
        GST_UNLOCK (bin);
	gst_bin_change_state_norecurse (bin);
	return;
      }
      break;
    }
  }
  GST_UNLOCK (bin);
}

static GstElementStateReturn
gst_bin_change_state (GstElement * element)
{
  GstBin *bin;
  GList *children;
  GstElement *child;
  GstElementStateReturn ret;
  GstElementState old_state, pending;
  gint transition;
  gboolean have_async = FALSE;

  g_return_val_if_fail (GST_IS_BIN (element), GST_STATE_FAILURE);

  bin = GST_BIN (element);

  old_state = GST_STATE (element);
  pending = GST_STATE_PENDING (element);
  transition = GST_STATE_TRANSITION (element);

  GST_INFO_ELEMENT (GST_CAT_STATES, element, "changing childrens' state from %s to %s",
		    gst_element_state_get_name (old_state), gst_element_state_get_name (pending));

  if (pending == GST_STATE_VOID_PENDING)
    return GST_STATE_SUCCESS;

  children = bin->children;

  while (children) {
    child = GST_ELEMENT (children->data);
    children = g_list_next (children);

    switch (gst_element_set_state (child, pending)) {
      case GST_STATE_FAILURE:
	GST_STATE_PENDING (element) = GST_STATE_VOID_PENDING;
	GST_DEBUG (GST_CAT_STATES, "child '%s' failed to go to state %d(%s)",
		   GST_ELEMENT_NAME (child), pending, gst_element_state_get_name (pending));

	gst_element_set_state (child, old_state);
	if (GST_ELEMENT_SCHED (child) == GST_ELEMENT_SCHED (element)) {
          /* reset to what is was */
          GST_STATE_PENDING (element) = old_state;
          gst_bin_change_state (element);
	  return GST_STATE_FAILURE;
	}
	break;
      case GST_STATE_ASYNC:
	GST_DEBUG (GST_CAT_STATES, "child '%s' is changing state asynchronously",
		   GST_ELEMENT_NAME (child));
	have_async = TRUE;
	break;
    }
  }

  GST_INFO_ELEMENT (GST_CAT_STATES, element, "done changing bin's state from %s to %s, now in %s",
                gst_element_state_get_name (old_state),
                gst_element_state_get_name (pending),
                gst_element_state_get_name (GST_STATE (element)));

  if (have_async)
    ret = GST_STATE_ASYNC;
  else
    ret = GST_STATE_SUCCESS;

  return ret;
}


static GstElementStateReturn
gst_bin_change_state_norecurse (GstBin * bin)
{
  GstElementStateReturn ret;

  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    GST_DEBUG_ELEMENT (GST_CAT_STATES, bin, "setting bin's own state");
    ret = GST_ELEMENT_CLASS (parent_class)->change_state (GST_ELEMENT (bin));

    return ret;
  }
  else
    return GST_STATE_FAILURE;
}

static gboolean
gst_bin_change_state_type (GstBin * bin, GstElementState state, GType type)
{
  GList *children;
  GstElement *child;

  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);
  g_return_val_if_fail (bin->numchildren != 0, FALSE);

  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    if (GST_IS_BIN (child)) {
      if (!gst_bin_set_state_type (GST_BIN (child), state, type))
	return FALSE;
    }
    else if (G_TYPE_CHECK_INSTANCE_TYPE (child, type)) {
      if (!gst_element_set_state (child, state))
	return FALSE;
    }
    children = g_list_next (children);
  }
  if (type == GST_TYPE_BIN)
    gst_element_set_state (GST_ELEMENT (bin), state);

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
gst_bin_set_state_type (GstBin * bin, GstElementState state, GType type)
{
  GstBinClass *oclass;

  GST_DEBUG (GST_CAT_STATES, "gst_bin_set_state_type(\"%s\",%d,%s)",
	     GST_ELEMENT_NAME (bin), state, G_OBJECT_TYPE_NAME (type));

  g_return_val_if_fail (bin != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);

  oclass = GST_BIN_CLASS (G_OBJECT_GET_CLASS (bin));

  if (oclass->change_state_type)
    (oclass->change_state_type) (bin, state, type);
  return TRUE;
}

static void
gst_bin_dispose (GObject * object)
{
  GstBin *bin = GST_BIN (object);
  GList *children, *orig;
  GstElement *child;

  GST_DEBUG (GST_CAT_REFCOUNTING, "dispose");

  if (gst_element_get_state (GST_ELEMENT (object)) == GST_STATE_PLAYING)
    gst_element_set_state (GST_ELEMENT (object), GST_STATE_PAUSED);

  if (bin->children) {
    orig = children = g_list_copy (bin->children);
    while (children) {
      child = GST_ELEMENT (children->data);
      gst_bin_remove (bin, child);
      children = g_list_next (children);
    }
    g_list_free (bin->children);
    g_list_free (orig);
  }
  bin->children = NULL;
  bin->numchildren = 0;

  G_OBJECT_CLASS (parent_class)->dispose (object);
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
GstElement *
gst_bin_get_by_name (GstBin * bin, const gchar * name)
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
    if (!strcmp (GST_OBJECT_NAME (child), name))
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
GstElement *
gst_bin_get_by_name_recurse_up (GstBin * bin, const gchar * name)
{
  GstElement *result = NULL;
  GstObject *parent;

  g_return_val_if_fail (bin != NULL, NULL);
  g_return_val_if_fail (GST_IS_BIN (bin), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  result = gst_bin_get_by_name (bin, name);

  if (!result) {
    parent = gst_object_get_parent (GST_OBJECT (bin));

    if (parent && GST_IS_BIN (parent)) {
      result = gst_bin_get_by_name_recurse_up (GST_BIN (parent), name);
    }
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
const GList *
gst_bin_get_list (GstBin * bin)
{
  g_return_val_if_fail (bin != NULL, NULL);
  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  return bin->children;
}

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr
gst_bin_save_thyself (GstObject * object, xmlNodePtr parent)
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
gst_bin_restore_thyself (GstObject * object, xmlNodePtr self)
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
	  GstElement *element = gst_xml_make_element (childlist, GST_OBJECT (bin));
          
          /* it had to be parented to find the pads, now we ref and unparent so
           * we can add it to the bin */
          gst_object_ref (GST_OBJECT (element));
          gst_object_unparent (GST_OBJECT (element));
          
	  gst_bin_add (bin, element);
	}
	childlist = childlist->next;
      }
    }

    field = field->next;
  }
}
#endif /* GST_DISABLE_LOADSAVE */

static gboolean
gst_bin_iterate_func (GstBin * bin)
{
  /* only iterate if this is the manager bin */
  if (GST_ELEMENT_SCHED (bin) &&
      GST_ELEMENT_SCHED (bin)->parent == GST_ELEMENT (bin)) {
    GstSchedulerState state;

    state = gst_scheduler_iterate (GST_ELEMENT_SCHED (bin));

    if (state == GST_SCHEDULER_STATE_RUNNING) {
      return TRUE;
    }
    else if (state == GST_SCHEDULER_STATE_ERROR) {
      gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);
    }
  }
  else {
    g_warning ("bin \"%s\" can't be iterated on!\n", GST_ELEMENT_NAME (bin));
  }

  return FALSE;
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
gst_bin_iterate (GstBin * bin)
{
  GstBinClass *oclass;
  gboolean running = TRUE;

  GST_DEBUG_ENTER ("(\"%s\")", GST_ELEMENT_NAME (bin));

  g_return_val_if_fail (bin != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);

  oclass = GST_BIN_CLASS (G_OBJECT_GET_CLASS (bin));

  if (bin->pre_iterate_func)
    (bin->pre_iterate_func) (bin, bin->pre_iterate_private);

  if (oclass->iterate)
    running = (oclass->iterate) (bin);

  if (bin->post_iterate_func)
    (bin->post_iterate_func) (bin, bin->post_iterate_private);

  GST_DEBUG_LEAVE ("(\"%s\") %d", GST_ELEMENT_NAME (bin), running);

  if (!running) {
    if (GST_STATE (bin) == GST_STATE_PLAYING && GST_STATE_PENDING (bin) == GST_STATE_VOID_PENDING) {
      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, bin,
			 "polling for child shutdown after useless iteration");
      usleep (1);
      //gst_element_wait_state_change (GST_ELEMENT (bin));
      running = TRUE;
    }
  }

  return running;
}

/**
 * gst_bin_set_pre_iterate_function:
 * @bin: #Gstbin to attach to
 * @func: callback function to call
 * @func_data: private data to put in the function call
 *
 * Attaches a callback which will be run before every iteration of the bin
 *
 */
void
gst_bin_set_pre_iterate_function (GstBin *bin, GstBinPrePostIterateFunction func, gpointer func_data)
{
  g_return_if_fail (GST_IS_BIN (bin));
  bin->pre_iterate_func = func;
  bin->pre_iterate_private = func_data;
}

/**
 * gst_bin_set_post_iterate_function:
 * @bin: #Gstbin to attach to
 * @func: callback function to call
 * @func_data: private data to put in the function call
 *
 * Attaches a callback which will be run after every iteration of the bin
 *
 */
void
gst_bin_set_post_iterate_function (GstBin *bin, GstBinPrePostIterateFunction func, gpointer func_data)
{
  g_return_if_fail (GST_IS_BIN (bin));
  bin->post_iterate_func = func;
  bin->post_iterate_private = func_data;
}

