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

GType _gst_bin_type = 0;

static void			gst_bin_dispose		(GObject *object);

static GstElementStateReturn	gst_bin_change_state		(GstElement *element);
static GstElementStateReturn	gst_bin_change_state_norecurse	(GstBin *bin);
static gboolean			gst_bin_change_state_type	(GstBin *bin,
								 GstElementState state,
								 GType type);
static void 			gst_bin_child_state_change 	(GstBin *bin, GstElementState old, 
								 GstElementState new, GstElement *child);

static gboolean			gst_bin_iterate_func		(GstBin *bin);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr		gst_bin_save_thyself		(GstObject *object, xmlNodePtr parent);
static void			gst_bin_restore_thyself		(GstObject *object, xmlNodePtr self);
#endif

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

GType
gst_bin_get_type (void)
{
  if (!_gst_bin_type) {
    static const GTypeInfo bin_info = {
      sizeof(GstBinClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_bin_class_init,
      NULL,
      NULL,
      sizeof(GstBin),
      8,
      (GInstanceInitFunc)gst_bin_init,
      NULL
    };
    _gst_bin_type = g_type_register_static (GST_TYPE_ELEMENT, "GstBin", &bin_info, 0);
  }
  return _gst_bin_type;
}

static void
gst_bin_class_init (GstBinClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gst_bin_signals[OBJECT_ADDED] =
    g_signal_new ("object_added", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST,
                    G_STRUCT_OFFSET (GstBinClass, object_added), NULL, NULL,
                    gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
                    GST_TYPE_ELEMENT);

  klass->change_state_type =		GST_DEBUG_FUNCPTR (gst_bin_change_state_type);
  klass->iterate =			GST_DEBUG_FUNCPTR (gst_bin_iterate_func);

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself =	GST_DEBUG_FUNCPTR (gst_bin_save_thyself);
  gstobject_class->restore_thyself =	GST_DEBUG_FUNCPTR (gst_bin_restore_thyself);
#endif

  gstelement_class->change_state =	GST_DEBUG_FUNCPTR (gst_bin_change_state);

  gobject_class->dispose =		GST_DEBUG_FUNCPTR (gst_bin_dispose);
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
  bin->eoscond = g_cond_new ();
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

static inline void
gst_bin_reset_element_sched (GstElement *element, GstScheduler *sched)
{
  GST_INFO_ELEMENT (GST_CAT_PARENTAGE, element, "resetting element's scheduler");

  gst_element_set_sched (element,sched);
}

static void
gst_bin_set_element_sched (GstElement *element,GstScheduler *sched)
{
  GList *children;
  GstElement *child;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT(element));
  g_return_if_fail (sched != NULL);
  g_return_if_fail (GST_IS_SCHEDULER(sched));

  GST_INFO (GST_CAT_SCHEDULING, "setting element \"%s\" sched to %p",GST_ELEMENT_NAME(element),
            sched);

  // if it's actually a Bin
  if (GST_IS_BIN(element)) {
    if (GST_FLAG_IS_SET (element, GST_BIN_FLAG_MANAGER)) {
      GST_INFO_ELEMENT (GST_CAT_PARENTAGE, element, "child is already a manager, not resetting");
      return;
    }

    GST_INFO_ELEMENT (GST_CAT_PARENTAGE, element, "setting children's schedule to parent's");
    gst_scheduler_add_element (sched, element);

    // set the children's schedule
    children = GST_BIN(element)->children;
    while (children) {
      child = GST_ELEMENT (children->data);
      children = g_list_next(children);

      gst_bin_set_element_sched (child, sched);
    }

  // otherwise, if it's just a regular old element
  } else {
    gst_scheduler_add_element (sched, element);
  }
}


static void
gst_bin_unset_element_sched (GstElement *element)
{
  GList *children;
  GstElement *child;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT(element));

  GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" from it sched %p",
            GST_ELEMENT_NAME(element),GST_ELEMENT_SCHED(element));

  // if it's actually a Bin
  if (GST_IS_BIN(element)) {

    if (GST_FLAG_IS_SET(element,GST_BIN_FLAG_MANAGER)) {
      GST_INFO_ELEMENT (GST_CAT_PARENTAGE, element, "child is already a manager, not unsetting sched");
      return;
    }

    // FIXME this check should be irrelevant
    if (GST_ELEMENT_SCHED (element))
      gst_scheduler_remove_element (GST_ELEMENT_SCHED(element), element);

    // for each child, remove them from their schedule
    children = GST_BIN(element)->children;
    while (children) {
      child = GST_ELEMENT (children->data);
      children = g_list_next(children);

      gst_bin_unset_element_sched (child);
    }

  // otherwise, if it's just a regular old element
  } else {
    // FIXME this check should be irrelevant
    if (GST_ELEMENT_SCHED (element))
      gst_scheduler_remove_element (GST_ELEMENT_SCHED(element), element);
  }
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
  gint state_idx = 0;
  GstElementState state;

  g_return_if_fail (bin != NULL);
  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));

  GST_DEBUG (GST_CAT_PARENTAGE, "adding element \"%s\" to bin \"%s\"\n",
             GST_ELEMENT_NAME (element), GST_ELEMENT_NAME (bin));

  // must be not be in PLAYING state in order to modify bin
  g_return_if_fail (GST_STATE (bin) != GST_STATE_PLAYING);

  // the element must not already have a parent
  g_return_if_fail (GST_ELEMENT_PARENT (element) == NULL);

  // then check to see if the element's name is already taken in the bin
  g_return_if_fail (gst_object_check_uniqueness (bin->children, GST_ELEMENT_NAME (element)) == TRUE);

  // set the element's parent and add the element to the bin's list of children
  gst_object_set_parent (GST_OBJECT (element), GST_OBJECT (bin));
  g_signal_connect_object (G_OBJECT (element), "state_change", gst_bin_child_state_change, G_OBJECT (bin));

  bin->children = g_list_append (bin->children, element);
  bin->numchildren++;

  // bump our internal state counter
  state = GST_STATE (element);
  while (state>>=1) state_idx++;
  bin->child_states[state_idx]++;

  ///// now we have to deal with manager stuff
  // we can only do this if there's a scheduler:
  // if we're not a manager, and aren't attached to anything, we have no sched (yet)
  if (GST_IS_BIN(element) && GST_FLAG_IS_SET (element, GST_BIN_FLAG_MANAGER)) {
    GST_INFO_ELEMENT (GST_CAT_PARENTAGE, element, "child is a manager");
  }
  else if (GST_ELEMENT_SCHED (bin) != NULL) {
    gst_bin_set_element_sched (element, GST_ELEMENT_SCHED (bin));
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
 */
void
gst_bin_remove (GstBin *bin,
		GstElement *element)
{
  gint state_idx = 0;
  GstElementState state;

  g_return_if_fail (bin != NULL);
  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element));
  g_return_if_fail (bin->children != NULL);

  // must not be in PLAYING state in order to modify bin
  g_return_if_fail (GST_STATE (bin) != GST_STATE_PLAYING);

  // the element must have its parent set to the current bin
  g_return_if_fail (GST_ELEMENT_PARENT(element) == (GstObject *)bin);

  // the element must be in the bin's list of children
  if (g_list_find(bin->children, element) == NULL) {
    // FIXME this should be a warning!!!
    GST_ERROR_OBJECT(bin,element,"no such element in bin");
    return;
  }

  // remove this element from the list of managed elements
  gst_bin_unset_element_sched (element);

  // now remove the element from the list of elements
  bin->children = g_list_remove (bin->children, element);
  bin->numchildren--;

  // bump our internal state counter
  state = GST_STATE (element);
  while (state>>=1) state_idx++;
  bin->child_states[state_idx]--;

  GST_INFO_ELEMENT (GST_CAT_PARENTAGE, bin, "removed child %s", GST_ELEMENT_NAME (element));

  gst_object_unparent (GST_OBJECT (element));

  /* if we're down to zero children, force state to NULL */
  if (bin->numchildren == 0 && GST_ELEMENT_SCHED (bin) != NULL)
    gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);
}

static void
gst_bin_child_state_change (GstBin *bin, GstElementState old, GstElementState new, GstElement *child)
{
  gint old_idx = 0, new_idx = 0, i;

  GST_INFO (GST_CAT_STATES, "child %s changed state in bin %s from %s to %s", 
		  GST_ELEMENT_NAME (child), GST_ELEMENT_NAME (bin),
		  gst_element_statename (old), gst_element_statename (new));

  while (old>>=1) old_idx++;
  while (new>>=1) new_idx++;

  GST_LOCK (bin);
  bin->child_states[old_idx]--;
  bin->child_states[new_idx]++;

  for (i = GST_NUM_STATES-1; i >= 0; i--) {
    if (bin->child_states[i] != 0) {
      if (GST_STATE (bin) != (1 << i)) {
        GST_INFO (GST_CAT_STATES, "bin %s need state change to %s", 
			GST_ELEMENT_NAME (bin), gst_element_statename (1<<i));
        GST_STATE_PENDING (bin) = (1<<i);
	gst_bin_change_state_norecurse (bin);
      }
      break;
    }
  }
  // FIXME, need to setup this array at add/remove time
  if (i<0) {
    GST_STATE_PENDING (bin) = GST_STATE_NULL;
    gst_bin_change_state_norecurse (bin);
  }
	  
  GST_UNLOCK (bin);
}

static GstElementStateReturn
gst_bin_change_state (GstElement *element)
{
  GstBin *bin;
  GList *children;
  GstElement *child;
  GstElementStateReturn ret;
  GstElementState old_state, pending;
  gboolean have_async = FALSE;

  g_return_val_if_fail (GST_IS_BIN (element), GST_STATE_FAILURE);

  bin = GST_BIN (element);

  old_state = GST_STATE (element);
  pending = GST_STATE_PENDING (element);

  GST_INFO_ELEMENT (GST_CAT_STATES, element, "changing childrens' state from %s to %s",
                gst_element_statename (old_state),
                gst_element_statename (pending));

  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    children = g_list_next (children);

    switch (gst_element_set_state (child, pending)) {
      case GST_STATE_FAILURE:
        GST_STATE_PENDING (element) = GST_STATE_VOID_PENDING;
        GST_DEBUG (GST_CAT_STATES,"child '%s' failed to go to state %d(%s)\n", GST_ELEMENT_NAME (child),
              pending, gst_element_statename (pending));

	gst_element_set_state (child, old_state);
	if (GST_ELEMENT_SCHED (child) == GST_ELEMENT_SCHED (element)) {
          return GST_STATE_FAILURE;
	}
        break;
      case GST_STATE_ASYNC:
        GST_DEBUG (GST_CAT_STATES,"child '%s' is changing state asynchronously\n", GST_ELEMENT_NAME (child));
	have_async = TRUE;
        break;
    }
  }

  GST_INFO_ELEMENT (GST_CAT_STATES, element, "done changing bin's state from %s to %s",
                gst_element_statename (old_state),
                gst_element_statename (pending));

  if (have_async)
    ret = GST_STATE_ASYNC;
  else
    ret = GST_STATE_SUCCESS;

  return ret;
}


static GstElementStateReturn
gst_bin_change_state_norecurse (GstBin *bin)
{
  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    GST_DEBUG_ELEMENT (GST_CAT_STATES, bin, "setting bin's own state\n");
    return GST_ELEMENT_CLASS (parent_class)->change_state (GST_ELEMENT (bin));
  } else
    return GST_STATE_FAILURE;
}

static gboolean
gst_bin_change_state_type (GstBin *bin,
                           GstElementState state,
                           GType type)
{
  GList *children;
  GstElement *child;

  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);
  g_return_val_if_fail (bin->numchildren != 0, FALSE);

  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    if (GST_IS_BIN (child)) {
      if (!gst_bin_set_state_type (GST_BIN (child), state,type))
        return FALSE;
    } else if (G_TYPE_CHECK_INSTANCE_TYPE (child,type)) {
      if (!gst_element_set_state (child,state))
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
gst_bin_set_state_type (GstBin *bin,
                        GstElementState state,
                        GType type)
{
  GstBinClass *oclass;

  GST_DEBUG (GST_CAT_STATES,"gst_bin_set_state_type(\"%s\",%d,%d)\n",
          GST_ELEMENT_NAME (bin), state,type);

  g_return_val_if_fail (bin != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);

  oclass = GST_BIN_CLASS (G_OBJECT_GET_CLASS(bin));

  if (oclass->change_state_type)
    (oclass->change_state_type) (bin,state,type);
  return TRUE;
}

static void
gst_bin_dispose (GObject *object)
{
  GstBin *bin = GST_BIN (object);
  GList *children, *orig;
  GstElement *child;

  GST_DEBUG (GST_CAT_REFCOUNTING,"dispose\n");

  if (bin->children) {
    orig = children = g_list_copy (bin->children);
    while (children) {
      child = GST_ELEMENT (children->data);
      //gst_object_unref (GST_OBJECT (child));
      //gst_object_unparent (GST_OBJECT (child));
      gst_bin_remove (bin, child);
      children = g_list_next (children);
    }
    g_list_free (orig);
    g_list_free (bin->children);
  }
  bin->children = NULL;
  bin->numchildren = 0;

  g_cond_free (bin->eoscond);

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
    if (!strcmp (GST_OBJECT_NAME(child),name))
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

#ifndef GST_DISABLE_LOADSAVE
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
          GstElement *element = gst_element_restore_thyself (childlist, GST_OBJECT (bin));

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
gst_bin_iterate_func (GstBin *bin)
{
  // only iterate if this is the manager bin
  if (GST_ELEMENT_SCHED(bin)->parent == GST_ELEMENT (bin)) {
    return gst_scheduler_iterate (GST_ELEMENT_SCHED(bin));
  } else {
    g_warning ("bin \"%d\" can't be iterated on!\n", GST_ELEMENT_NAME (bin));
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
gst_bin_iterate (GstBin *bin)
{
  GstBinClass *oclass;
  gboolean running = TRUE;

  GST_DEBUG_ENTER("(\"%s\")",GST_ELEMENT_NAME (bin));

  oclass = GST_BIN_CLASS (G_OBJECT_GET_CLASS(bin));

  if (oclass->iterate)
    running = (oclass->iterate) (bin);

  GST_DEBUG_LEAVE("(\"%s\") %d",GST_ELEMENT_NAME (bin), running);

  if (!running) {
    if (GST_STATE (bin) == GST_STATE_PLAYING && GST_STATE_PENDING (bin) == GST_STATE_VOID_PENDING) {
      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, bin, "waiting for child shutdown after useless iteration\n");
      //gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);
      gst_element_wait_state_change (GST_ELEMENT (bin));
      GST_DEBUG_ELEMENT (GST_CAT_DATAFLOW, bin, "child shutdown\n");
    }
  }

  return running;
}

/* out internal element fired EOS, we decrement the number of pending EOS childs */
G_GNUC_UNUSED static void
gst_bin_received_eos (GstElement *element, GstBin *bin)
{
  GST_INFO_ELEMENT (GST_CAT_PLANNING, bin, "child %s fired eos, pending %d", GST_ELEMENT_NAME (element),
		  bin->num_eos_providers);

  GST_LOCK (bin);
  if (bin->num_eos_providers) {
    bin->num_eos_providers--;
    g_cond_signal (bin->eoscond);
  }
  GST_UNLOCK (bin);
}

