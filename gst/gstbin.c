/* GStreamer
 * 
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2004 Wim Taymans <wim@fluendo.com>
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
 *
 * MT safe.
 */

#include "gst_private.h"

#include "gstevent.h"
#include "gstbin.h"
#include "gstmarshal.h"
#include "gstxml.h"
#include "gstinfo.h"
#include "gsterror.h"

#include "gstscheduler.h"
#include "gstindex.h"
#include "gstutils.h"

GST_DEBUG_CATEGORY_STATIC (bin_debug);
#define GST_CAT_DEFAULT bin_debug
#define GST_LOG_BIN_CONTENTS(bin, text) GST_LOG_OBJECT ((bin), \
	text ": %d elements: %u PLAYING, %u PAUSED, %u READY, %u NULL, own state: %s", \
	(bin)->numchildren, (guint) (bin)->child_states[3], \
	(guint) (bin)->child_states[2], (bin)->child_states[1], \
	(bin)->child_states[0], gst_element_state_get_name (GST_STATE (bin)))


static GstElementDetails gst_bin_details = GST_ELEMENT_DETAILS ("Generic bin",
    "Generic/Bin",
    "Simple container object",
    "Erik Walthinsen <omega@cse.ogi.edu>," "Wim Taymans <wim@fluendo.com>");

GType _gst_bin_type = 0;

static gboolean _gst_boolean_did_something_accumulator (GSignalInvocationHint *
    ihint, GValue * return_accu, const GValue * handler_return, gpointer dummy);

static void gst_bin_dispose (GObject * object);

static GstElementStateReturn gst_bin_change_state (GstElement * element);
static GstElementStateReturn gst_bin_change_state_norecurse (GstBin * bin);

static gboolean gst_bin_add_func (GstBin * bin, GstElement * element);
static gboolean gst_bin_remove_func (GstBin * bin, GstElement * element);

#ifndef GST_DISABLE_INDEX
static void gst_bin_set_index_func (GstElement * element, GstIndex * index);
#endif
static GstClock *gst_bin_get_clock_func (GstElement * element);
static void gst_bin_set_clock_func (GstElement * element, GstClock * clock);

static void gst_bin_child_state_change_func (GstBin * bin,
    GstElementState oldstate, GstElementState newstate, GstElement * child);
GstElementStateReturn gst_bin_set_state (GstElement * element,
    GstElementState state);

static gboolean gst_bin_iterate_func (GstBin * bin);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr gst_bin_save_thyself (GstObject * object, xmlNodePtr parent);
static void gst_bin_restore_thyself (GstObject * object, xmlNodePtr self);
#endif

/* Bin signals and args */
enum
{
  ELEMENT_ADDED,
  ELEMENT_REMOVED,
  ITERATE,
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

static void gst_bin_base_init (gpointer g_class);
static void gst_bin_class_init (GstBinClass * klass);
static void gst_bin_init (GstBin * bin);

static GstElementClass *parent_class = NULL;
static guint gst_bin_signals[LAST_SIGNAL] = { 0 };

/**
 * gst_bin_get_type:
 *
 * Returns: the type of #GstBin
 */
GType
gst_bin_get_type (void)
{
  if (!_gst_bin_type) {
    static const GTypeInfo bin_info = {
      sizeof (GstBinClass),
      gst_bin_base_init,
      NULL,
      (GClassInitFunc) gst_bin_class_init,
      NULL,
      NULL,
      sizeof (GstBin),
      0,
      (GInstanceInitFunc) gst_bin_init,
      NULL
    };

    _gst_bin_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstBin", &bin_info, 0);

    GST_DEBUG_CATEGORY_INIT (bin_debug, "bin", GST_DEBUG_BOLD,
        "debugging info for the 'bin' container element");
  }
  return _gst_bin_type;
}

static void
gst_bin_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_bin_details);
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

  gst_bin_signals[ELEMENT_ADDED] =
      g_signal_new ("element-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GstBinClass, element_added), NULL,
      NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);
  gst_bin_signals[ELEMENT_REMOVED] =
      g_signal_new ("element-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GstBinClass, element_removed), NULL,
      NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);
  gst_bin_signals[ITERATE] =
      g_signal_new ("iterate", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstBinClass, iterate),
      _gst_boolean_did_something_accumulator, NULL, gst_marshal_BOOLEAN__VOID,
      G_TYPE_BOOLEAN, 0);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_bin_dispose);

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself = GST_DEBUG_FUNCPTR (gst_bin_save_thyself);
  gstobject_class->restore_thyself =
      GST_DEBUG_FUNCPTR (gst_bin_restore_thyself);
#endif

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_bin_change_state);
  gstelement_class->set_state = GST_DEBUG_FUNCPTR (gst_bin_set_state);
#ifndef GST_DISABLE_INDEX
  gstelement_class->set_index = GST_DEBUG_FUNCPTR (gst_bin_set_index_func);
#endif
  gstelement_class->get_clock = GST_DEBUG_FUNCPTR (gst_bin_get_clock_func);
  gstelement_class->set_clock = GST_DEBUG_FUNCPTR (gst_bin_set_clock_func);

  klass->add_element = GST_DEBUG_FUNCPTR (gst_bin_add_func);
  klass->remove_element = GST_DEBUG_FUNCPTR (gst_bin_remove_func);

  klass->child_state_change =
      GST_DEBUG_FUNCPTR (gst_bin_child_state_change_func);
  klass->iterate = GST_DEBUG_FUNCPTR (gst_bin_iterate_func);
}

static gboolean
_gst_boolean_did_something_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  gboolean did_something;

  did_something = g_value_get_boolean (handler_return);
  if (did_something) {
    g_value_set_boolean (return_accu, TRUE);
  }

  /* always continue emission */
  return TRUE;
}

static void
gst_bin_init (GstBin * bin)
{
  bin->numchildren = 0;
  bin->children = NULL;
  bin->children_cookie = 0;
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

/* set the index on all elements in this bin
 *
 * MT safe 
 */
#ifndef GST_DISABLE_INDEX
static void
gst_bin_set_index_func (GstElement * element, GstIndex * index)
{
  GstBin *bin;
  GList *children;

  bin = GST_BIN (element);

  GST_LOCK (bin);
  for (children = bin->children; children; children = g_list_next (children)) {
    GstElement *child = GST_ELEMENT (children->data);

    gst_element_set_index (child, index);
  }
  GST_UNLOCK (bin);
}
#endif

/* set the clock on all elements in this bin
 *
 * MT safe 
 */
static void
gst_bin_set_clock_func (GstElement * element, GstClock * clock)
{
  GList *children;
  GstBin *bin;

  bin = GST_BIN (element);

  GST_LOCK (bin);
  for (children = bin->children; children; children = g_list_next (children)) {
    GstElement *child = GST_ELEMENT (children->data);

    gst_element_set_clock (child, clock);
  }
  GST_UNLOCK (bin);
}

/* get the clock for this bin by asking all of the children in this bin
 *
 * MT safe 
 */
static GstClock *
gst_bin_get_clock_func (GstElement * element)
{
  GstClock *result = NULL;
  GstBin *bin;
  GList *children;

  bin = GST_BIN (element);

  GST_LOCK (bin);
  for (children = bin->children; children; children = g_list_next (children)) {
    GstElement *child = GST_ELEMENT (children->data);

    result = gst_element_get_clock (child);
    if (result)
      break;
  }
  GST_UNLOCK (bin);

  return result;
}

/* will be removed */
static void
gst_bin_set_element_sched (GstElement * element, GstScheduler * sched)
{
  GST_CAT_LOG (GST_CAT_SCHEDULING, "setting element \"%s\" sched to %p",
      GST_ELEMENT_NAME (element), sched);

  /* if it's actually a Bin */
  if (GST_IS_BIN (element)) {
    if (GST_FLAG_IS_SET (element, GST_BIN_FLAG_MANAGER)) {
      GST_CAT_DEBUG_OBJECT (GST_CAT_PARENTAGE, element,
          "child is already a manager, not resetting sched");
      if (GST_ELEMENT_SCHEDULER (element))
        gst_scheduler_add_scheduler (sched, GST_ELEMENT_SCHEDULER (element));
      return;
    }

    GST_CAT_DEBUG_OBJECT (GST_CAT_PARENTAGE, element,
        "setting child bin's scheduler to be the same as the parent's");
    gst_scheduler_add_element (sched, element);

    /* set the children's schedule */
    g_list_foreach (GST_BIN (element)->children,
        (GFunc) gst_bin_set_element_sched, sched);
  } else {
    /* otherwise, if it's just a regular old element */
    GList *pads;

    gst_scheduler_add_element (sched, element);

    if (!GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
      /* set the sched pointer in all the pads */
      pads = element->pads;
      while (pads) {
        GstPad *pad;

        pad = GST_PAD (pads->data);
        pads = g_list_next (pads);

        /* we only operate on real pads */
        if (!GST_IS_REAL_PAD (pad))
          continue;

        /* if the peer element exists and is a candidate */
        if (GST_PAD_PEER (pad)) {
          if (gst_pad_get_scheduler (GST_PAD_PEER (pad)) == sched) {
            GST_CAT_LOG (GST_CAT_SCHEDULING,
                "peer is in same scheduler, telling scheduler");

            if (GST_PAD_IS_SRC (pad))
              gst_scheduler_pad_link (sched, pad, GST_PAD_PEER (pad));
            else
              gst_scheduler_pad_link (sched, GST_PAD_PEER (pad), pad);
          }
        }
      }
    }
  }
}

/* will be removed */
static void
gst_bin_unset_element_sched (GstElement * element, GstScheduler * sched)
{
  if (GST_ELEMENT_SCHEDULER (element) == NULL) {
    GST_CAT_DEBUG (GST_CAT_SCHEDULING, "element \"%s\" has no scheduler",
        GST_ELEMENT_NAME (element));
    return;
  }

  GST_CAT_DEBUG (GST_CAT_SCHEDULING,
      "removing element \"%s\" from its sched %p", GST_ELEMENT_NAME (element),
      GST_ELEMENT_SCHEDULER (element));

  /* if it's actually a Bin */
  if (GST_IS_BIN (element)) {

    if (GST_FLAG_IS_SET (element, GST_BIN_FLAG_MANAGER)) {
      GST_CAT_DEBUG_OBJECT (GST_CAT_PARENTAGE, element,
          "child is already a manager, not unsetting sched");
      if (sched) {
        gst_scheduler_remove_scheduler (sched, GST_ELEMENT_SCHEDULER (element));
      }
      return;
    }
    /* for each child, remove them from their schedule */
    g_list_foreach (GST_BIN (element)->children,
        (GFunc) gst_bin_unset_element_sched, sched);

    gst_scheduler_remove_element (GST_ELEMENT_SCHEDULER (element), element);
  } else {
    /* otherwise, if it's just a regular old element */
    GList *pads;

    if (!GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
      /* unset the sched pointer in all the pads */
      pads = element->pads;
      while (pads) {
        GstPad *pad;

        pad = GST_PAD (pads->data);
        pads = g_list_next (pads);

        /* we only operate on real pads */
        if (!GST_IS_REAL_PAD (pad))
          continue;

        /* if the peer element exists and is a candidate */
        if (GST_PAD_PEER (pad)) {
          if (gst_pad_get_scheduler (GST_PAD_PEER (pad)) == sched) {
            GST_CAT_LOG (GST_CAT_SCHEDULING,
                "peer is in same scheduler, telling scheduler");

            if (GST_PAD_IS_SRC (pad))
              gst_scheduler_pad_unlink (sched, pad, GST_PAD_PEER (pad));
            else
              gst_scheduler_pad_unlink (sched, GST_PAD_PEER (pad), pad);
          }
        }
      }
    }

    gst_scheduler_remove_element (GST_ELEMENT_SCHEDULER (element), element);
  }
}

/* add an element to this bin
 *
 * MT safe 
 */
static gboolean
gst_bin_add_func (GstBin * bin, GstElement * element)
{
  gchar *elem_name;

  /* we obviously can't add ourself to ourself */
  if (G_UNLIKELY (GST_ELEMENT_CAST (element) == GST_ELEMENT_CAST (bin)))
    goto adding_itself;

  /* get the element name to make sure it is unique in this bin, FIXME, another
   * thread can change the name after the unlock. */
  GST_LOCK (element);
  elem_name = g_strdup (GST_ELEMENT_NAME (element));
  GST_UNLOCK (element);

  GST_LOCK (bin);
  /* then check to see if the element's name is already taken in the bin,
   * we can safely take the lock here. This check is probably bogus because
   * you can safely change the element name after adding it to the bin. */
  if (G_UNLIKELY (gst_object_check_uniqueness (bin->children,
              elem_name) == FALSE))
    goto duplicate_name;

  /* set the element's parent and add the element to the bin's list of children */
  if (G_UNLIKELY (!gst_object_set_parent (GST_OBJECT_CAST (element),
              GST_OBJECT_CAST (bin))))
    goto had_parent;

  bin->children = g_list_prepend (bin->children, element);
  bin->numchildren++;
  bin->children_cookie++;

  gst_element_set_scheduler (element, GST_ELEMENT_SCHEDULER (bin));

  GST_UNLOCK (bin);

  GST_CAT_DEBUG_OBJECT (GST_CAT_PARENTAGE, bin, "added element \"%s\"",
      elem_name);
  g_free (elem_name);

  g_signal_emit (G_OBJECT (bin), gst_bin_signals[ELEMENT_ADDED], 0, element);

  return TRUE;

  /* ERROR handling here */
adding_itself:
  {
    GST_LOCK (bin);
    g_warning ("Cannot add bin %s to itself", GST_ELEMENT_NAME (bin));
    GST_UNLOCK (bin);
    return FALSE;
  }
duplicate_name:
  {
    g_warning ("Name %s is not unique in bin %s, not adding",
        elem_name, GST_ELEMENT_NAME (bin));
    GST_UNLOCK (bin);
    g_free (elem_name);
    return FALSE;
  }
had_parent:
  {
    g_warning ("Element %s already has parent", elem_name);
    GST_UNLOCK (bin);
    g_free (elem_name);
    return FALSE;
  }
}

/**
 * gst_bin_add:
 * @bin: #GstBin to add element to
 * @element: #GstElement to add to bin
 *
 * Adds the given element to the bin.  Sets the element's parent, and thus
 * takes ownership of the element. An element can only be added to one bin.
 *
 * Returns: TRUE if the element could be added, FALSE on wrong parameters or
 * the bin does not want to accept the element.
 *
 * MT safe.
 */
gboolean
gst_bin_add (GstBin * bin, GstElement * element)
{
  GstBinClass *bclass;
  gboolean result;

  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  bclass = GST_BIN_GET_CLASS (bin);

  if (G_UNLIKELY (bclass->add_element == NULL))
    goto no_function;

  GST_CAT_DEBUG (GST_CAT_PARENTAGE, "adding element %s to bin %s",
      GST_ELEMENT_NAME (element), GST_ELEMENT_NAME (bin));

  result = bclass->add_element (bin, element);

  return result;

  /* ERROR handling */
no_function:
  {
    g_warning ("adding elements to bin %s is not supported",
        GST_ELEMENT_NAME (bin));
    return FALSE;
  }
}

/* remove an element from the bin
 *
 * MT safe
 */
static gboolean
gst_bin_remove_func (GstBin * bin, GstElement * element)
{
  gchar *elem_name;

  /* grab element name so we can print it */
  GST_LOCK (element);
  elem_name = g_strdup (GST_ELEMENT_NAME (element));
  GST_UNLOCK (element);

  GST_LOCK (bin);
  /* the element must be in the bin's list of children */
  if (G_UNLIKELY (g_list_find (bin->children, element) == NULL))
    goto not_in_bin;

  /* now remove the element from the list of elements */
  bin->children = g_list_remove (bin->children, element);
  bin->numchildren--;
  bin->children_cookie++;
  GST_UNLOCK (bin);

  GST_CAT_INFO_OBJECT (GST_CAT_PARENTAGE, bin, "removed child \"%s\"",
      elem_name);
  g_free (elem_name);

  gst_element_set_scheduler (element, NULL);

  /* we ref here because after the _unparent() the element can be disposed
   * and we still need it to fire a signal. */
  gst_object_ref (GST_OBJECT_CAST (element));
  gst_object_unparent (GST_OBJECT_CAST (element));

  g_signal_emit (G_OBJECT (bin), gst_bin_signals[ELEMENT_REMOVED], 0, element);
  /* element is really out of our control now */
  gst_object_unref (GST_OBJECT_CAST (element));

  return TRUE;

  /* ERROR handling */
not_in_bin:
  {
    g_warning ("Element %s is not in bin %s", elem_name,
        GST_ELEMENT_NAME (bin));
    GST_UNLOCK (bin);
    g_free (elem_name);
    return FALSE;
  }
}

/**
 * gst_bin_remove:
 * @bin: #GstBin to remove element from
 * @element: #GstElement to remove
 *
 * Remove the element from its associated bin, unparenting it as well.
 * Unparenting the element means that the element will be dereferenced,
 * so if the bin holds the only reference to the element, the element
 * will be freed in the process of removing it from the bin.  If you
 * want the element to still exist after removing, you need to call
 * #gst_object_ref before removing it from the bin.
 *
 * Returns: TRUE if the element could be removed, FALSE on wrong parameters or
 * the bin does not want to remove the element.
 *
 * MT safe.
 */
gboolean
gst_bin_remove (GstBin * bin, GstElement * element)
{
  GstBinClass *bclass;
  gboolean result;

  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  bclass = GST_BIN_GET_CLASS (bin);

  if (G_UNLIKELY (bclass->remove_element == NULL))
    goto no_function;

  GST_CAT_DEBUG (GST_CAT_PARENTAGE, "removing element %s from bin %s",
      GST_ELEMENT_NAME (element), GST_ELEMENT_NAME (bin));

  result = bclass->remove_element (bin, element);

  return result;

  /* ERROR handling */
no_function:
  {
    g_warning ("removing elements from bin %s is not supported",
        GST_ELEMENT_NAME (bin));
    return FALSE;
  }
}

static GstIteratorItem
iterate_child (GstIterator * it, GstElement * child)
{
  gst_object_ref (GST_OBJECT (child));
  return GST_ITERATOR_ITEM_PASS;
}

/**
 * gst_bin_iterate_elements:
 * @bin: #Gstbin to iterate the elements of
 *
 * Get an iterator for the elements in this bin. 
 * Each element will have its refcount increased, so unref 
 * after usage.
 *
 * Returns: a #GstIterator of #GstElements. gst_iterator_free after
 * use. returns NULL when passing bad parameters.
 *
 * MT safe.
 */
GstIterator *
gst_bin_iterate_elements (GstBin * bin)
{
  GstIterator *result;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  GST_LOCK (bin);
  /* add ref because the iterator refs the bin. When the iterator
   * is freed it will unref the bin again using the provided dispose 
   * function. */
  gst_object_ref (GST_OBJECT (bin));
  result = gst_iterator_new_list (GST_GET_LOCK (bin),
      &bin->children_cookie,
      &bin->children,
      bin,
      (GstIteratorItemFunction) iterate_child,
      (GstIteratorDisposeFunction) gst_object_unref);
  GST_UNLOCK (bin);

  return result;
}

static GstIteratorItem
iterate_child_recurse (GstIterator * it, GstElement * child)
{
  gst_object_ref (GST_OBJECT (child));
  if (GST_IS_BIN (child)) {
    GstIterator *other = gst_bin_iterate_recurse (GST_BIN (child));

    gst_iterator_push (it, other);
  }
  return GST_ITERATOR_ITEM_PASS;
}

/**
 * gst_bin_iterate_recurse:
 * @bin: #Gstbin to iterate the elements of
 *
 * Get an iterator for the elements in this bin. 
 * Each element will have its refcount increased, so unref 
 * after usage. This iterator recurses into GstBin children.
 *
 * Returns: a #GstIterator of #GstElements. gst_iterator_free after
 * use. returns NULL when passing bad parameters.
 *
 * MT safe.
 */
GstIterator *
gst_bin_iterate_recurse (GstBin * bin)
{
  GstIterator *result;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  GST_LOCK (bin);
  /* add ref because the iterator refs the bin. When the iterator
   * is freed it will unref the bin again using the provided dispose 
   * function. */
  gst_object_ref (GST_OBJECT (bin));
  result = gst_iterator_new_list (GST_GET_LOCK (bin),
      &bin->children_cookie,
      &bin->children,
      bin,
      (GstIteratorItemFunction) iterate_child_recurse,
      (GstIteratorDisposeFunction) gst_object_unref);
  GST_UNLOCK (bin);

  return result;
}

/* returns 0 if the element is a sink, this is made so that
 * we can use this function as a filter 
 *
 * MT safe
 */
static gint
bin_element_is_sink (GstElement * child, GstBin * bin)
{
  gint ret = 1;

  /* we lock the child here for the remainder of the function to
   * get its pads and name safely. */
  GST_LOCK (child);

  /* check if this is a sink element, these are the elements
   * without (linked) source pads. */
  if (child->numsrcpads == 0) {
    /* shortcut */
    GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, bin,
        "adding child %s as sink", GST_OBJECT_NAME (child));
    ret = 0;
  } else {
    /* loop over all pads, try to figure out if this element
     * is a sink because it has no linked source pads */
    GList *pads;
    gboolean connected_src = FALSE;

    for (pads = child->srcpads; pads; pads = g_list_next (pads)) {
      GstPad *peer;

      peer = gst_pad_get_peer (GST_PAD_CAST (pads->data));
      if (peer) {
        GstElement *parent;

        parent = gst_pad_get_parent (peer);
        if (parent) {
          GstObject *grandparent;

          grandparent = gst_object_get_parent (GST_OBJECT_CAST (parent));
          if (grandparent == GST_OBJECT_CAST (bin)) {
            connected_src = TRUE;
          }
          if (grandparent) {
            gst_object_unref (GST_OBJECT_CAST (grandparent));
          }
          gst_object_unref (GST_OBJECT_CAST (parent));
        }
        gst_object_unref (GST_OBJECT_CAST (peer));
        if (connected_src) {
          break;
        }
      }
    }

    if (connected_src) {
      GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, bin,
          "not adding child %s as sink: linked source pads",
          GST_OBJECT_NAME (child));
    } else {
      GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, bin,
          "adding child %s as sink since it has unlinked source pads in this bin",
          GST_OBJECT_NAME (child));
      ret = 0;
    }
  }
  GST_UNLOCK (child);

  /* we did not find the element, need to release the ref
   * added by the iterator */
  if (ret == 1)
    gst_object_unref (GST_OBJECT (child));

  return ret;
}

/**
 * gst_bin_iterate_sinks:
 * @bin: #Gstbin to iterate on
 *
 * Get an iterator for the sink elements in this bin.
 * Each element will have its refcount increased, so unref 
 * after usage.
 *
 * The sink elements are those without any linked srcpads.
 *
 * Returns: a #GstIterator of #GstElements. gst_iterator_free after use.
 *
 * MT safe.
 */
GstIterator *
gst_bin_iterate_sinks (GstBin * bin)
{
  GstIterator *children;
  GstIterator *result;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  children = gst_bin_iterate_elements (bin);
  result = gst_iterator_filter (children,
      (GCompareFunc) bin_element_is_sink, bin);

  return result;
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
 *
 * Marked for removal.
 */
void
gst_bin_child_state_change (GstBin * bin, GstElementState oldstate,
    GstElementState newstate, GstElement * child)
{
  GstBinClass *bclass;

  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (GST_IS_ELEMENT (child));

  GST_CAT_LOG (GST_CAT_STATES, "child %s changed state in bin %s from %s to %s",
      GST_ELEMENT_NAME (child), GST_ELEMENT_NAME (bin),
      gst_element_state_get_name (oldstate),
      gst_element_state_get_name (newstate));

  bclass = GST_BIN_GET_CLASS (bin);

  if (bclass->child_state_change) {
    bclass->child_state_change (bin, oldstate, newstate, child);
  } else {
    g_warning ("cannot signal state change of child %s to bin %s\n",
        GST_ELEMENT_NAME (child), GST_ELEMENT_NAME (bin));
  }
}

/* will be removed */
static void
gst_bin_child_state_change_func (GstBin * bin, GstElementState oldstate,
    GstElementState newstate, GstElement * child)
{
  GstElementState old = 0, new = 0;
  gint old_idx = 0, new_idx = 0, i;

  old = oldstate;
  new = newstate;
  while ((old >>= 1) != 0)
    old_idx++;
  while ((new >>= 1) != 0)
    new_idx++;

  GST_LOCK (bin);
  GST_LOG_BIN_CONTENTS (bin, "before child state change");
  bin->child_states[old_idx]--;
  bin->child_states[new_idx]++;

  for (i = GST_NUM_STATES - 1; i >= 0; i--) {
    if (bin->child_states[i] != 0) {
      gint state = (1 << i);

      /* We only change state on the parent if the state is not locked.
       * State locking can occur if the bin itself set state on children,
       * which should not recurse since it leads to infinite loops. */
      if (GST_STATE (bin) != state &&
          !GST_FLAG_IS_SET (bin, GST_BIN_STATE_LOCKED)) {
        GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, bin,
            "highest child state is %s, changing bin state accordingly",
            gst_element_state_get_name (state));
        GST_STATE_PENDING (bin) = state;
        GST_UNLOCK (bin);
        gst_bin_change_state_norecurse (bin);
        if (state != GST_STATE (bin)) {
          g_warning ("%s: state change in callback %d %d",
              GST_ELEMENT_NAME (bin), state, GST_STATE (bin));
        }
        GST_LOG_BIN_CONTENTS (bin, "after child state change");
        return;
      }
      break;
    }
  }
  GST_LOG_BIN_CONTENTS (bin, "after child state change");
  GST_UNLOCK (bin);
}

typedef gboolean (*GstBinForeachFunc) (GstBin * bin, GstElement * element,
    gpointer data);

/*
 * gst_bin_foreach:
 * @bin: bin to traverse
 * @func: function to call on each child
 * @data: user data handed to each function call
 *
 * Calls @func on each child of the bin. If @func returns FALSE, 
 * gst_bin_foreach() immediately returns.
 * It is assumed that calling @func may alter the set of @bin's children. @func
 * will only be called on children that were in @bin when gst_bin_foreach() was
 * called, and that are still in @bin when the child is reached.
 *
 * Returns: TRUE if @func always returned TRUE, FALSE otherwise
 *
 * Marked for removal.
 **/
static gboolean
gst_bin_foreach (GstBin * bin, GstBinForeachFunc func, gpointer data)
{
  GList *kids, *walk;

  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);
  g_return_val_if_fail (func != NULL, FALSE);

  kids = g_list_copy (bin->children);

  for (walk = kids; walk; walk = g_list_next (walk)) {
    GstElement *element = (GstElement *) walk->data;

    if (g_list_find (bin->children, element)) {
      gboolean res = func (bin, element, data);

      if (!res) {
        g_list_free (kids);
        return FALSE;
      }
    }
  }

  g_list_free (kids);
  return TRUE;
}

typedef struct
{
  GstElementState pending;
  GstElementStateReturn result;
}
SetKidStateData;
static int
set_kid_state_func (GstBin * bin, GstElement * child, gpointer user_data)
{
  GstElementState old_child_state;
  SetKidStateData *data = user_data;

  if (GST_FLAG_IS_SET (child, GST_ELEMENT_LOCKED_STATE)) {
    return TRUE;
  }

  old_child_state = GST_STATE (child);

  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, bin,
      "changing state of child %s from current %s to pending %s",
      GST_ELEMENT_NAME (child), gst_element_state_get_name (old_child_state),
      gst_element_state_get_name (data->pending));

  switch (gst_element_set_state (child, data->pending)) {
    case GST_STATE_FAILURE:
      GST_CAT_INFO_OBJECT (GST_CAT_STATES, bin,
          "child '%s' failed to go to state %d(%s)",
          GST_ELEMENT_NAME (child),
          data->pending, gst_element_state_get_name (data->pending));

      gst_element_set_state (child, old_child_state);
      return FALSE;             /* error out to the caller */

    case GST_STATE_ASYNC:
      GST_CAT_INFO_OBJECT (GST_CAT_STATES, bin,
          "child '%s' is changing state asynchronously",
          GST_ELEMENT_NAME (child));
      data->result = GST_STATE_ASYNC;
      return TRUE;

    case GST_STATE_SUCCESS:
      GST_CAT_DEBUG (GST_CAT_STATES,
          "child '%s' changed state to %d(%s) successfully",
          GST_ELEMENT_NAME (child), data->pending,
          gst_element_state_get_name (data->pending));
      return TRUE;

    default:
      g_assert_not_reached ();
      return FALSE;             /* satisfy gcc */
  }
}

static GstElementStateReturn
gst_bin_change_state (GstElement * element)
{
  GstBin *bin;
  GstElementStateReturn ret;
  GstElementState old_state, pending;

  g_return_val_if_fail (GST_IS_BIN (element), GST_STATE_FAILURE);

  bin = GST_BIN (element);

  old_state = GST_STATE (element);
  pending = GST_STATE_PENDING (element);

  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
      "changing state of children from %s to %s",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (pending));

  if (pending == GST_STATE_VOID_PENDING)
    return GST_STATE_SUCCESS;

  /* If we're changing state non-recursively (see _norecurse()),
   * this flag is already set and we should not set children states. */
  if (!GST_FLAG_IS_SET (bin, GST_BIN_STATE_LOCKED)) {
    SetKidStateData data;

    /* So now we use this flag to make sure that kids don't re-set our
     * state, which would lead to infinite loops. */
    GST_FLAG_SET (bin, GST_BIN_STATE_LOCKED);
    data.pending = pending;
    data.result = GST_STATE_SUCCESS;
    if (!gst_bin_foreach (bin, set_kid_state_func, &data)) {
      GST_FLAG_UNSET (bin, GST_BIN_STATE_LOCKED);
      GST_STATE_PENDING (element) = old_state;
      return GST_STATE_FAILURE;
    }
    GST_FLAG_UNSET (bin, GST_BIN_STATE_LOCKED);

    GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
        "done changing bin's state from %s to %s, now in %s",
        gst_element_state_get_name (old_state),
        gst_element_state_get_name (pending),
        gst_element_state_get_name (GST_STATE (element)));

    /* if we're async, the kids will change state later (when the
     * lock-state flag is no longer held) and all will be fine. */
    if (data.result == GST_STATE_ASYNC)
      return GST_STATE_ASYNC;
  } else {
    GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
        "Not recursing state change onto children");
  }

  /* FIXME: this should have been done by the children already, no? */
  if (parent_class->change_state) {
    ret = parent_class->change_state (element);
  } else {
    ret = GST_STATE_SUCCESS;
  }
  return ret;
}

GstElementStateReturn
gst_bin_set_state (GstElement * element, GstElementState state)
{
  GstBin *bin = GST_BIN (element);

  if (GST_STATE (bin) == state) {
    SetKidStateData data;

    data.pending = state;
    data.result = GST_STATE_SUCCESS;
    if (!gst_bin_foreach (bin, set_kid_state_func, &data)) {
      return GST_STATE_FAILURE;
    } else {
      return data.result;
    }
  } else {
    return GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, set_state, (element,
            state), GST_STATE_FAILURE);
  }
}

static GstElementStateReturn
gst_bin_change_state_norecurse (GstBin * bin)
{
  GstElementStateReturn ret;

  if (GST_ELEMENT_GET_CLASS (bin)->change_state) {
    GST_CAT_LOG_OBJECT (GST_CAT_STATES, bin, "setting bin's own state");

    /* Non-recursive state change flag */
    GST_FLAG_SET (bin, GST_BIN_STATE_LOCKED);
    ret = GST_ELEMENT_GET_CLASS (bin)->change_state (GST_ELEMENT (bin));
    GST_FLAG_UNSET (bin, GST_BIN_STATE_LOCKED);

    return ret;
  } else
    return GST_STATE_FAILURE;
}

static void
gst_bin_dispose (GObject * object)
{
  GstBin *bin = GST_BIN (object);

  GST_CAT_DEBUG_OBJECT (GST_CAT_REFCOUNTING, object, "dispose");

  /* ref to not hit 0 again */
  gst_object_ref (GST_OBJECT (object));

  while (bin->children) {
    gst_bin_remove (bin, GST_ELEMENT (bin->children->data));
  }
  GST_CAT_DEBUG_OBJECT (GST_CAT_REFCOUNTING, object, "dispose no children");
  g_assert (bin->children == NULL);
  g_assert (bin->numchildren == 0);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gint
compare_name (GstElement * element, const gchar * name)
{
  gint eq;

  GST_LOCK (element);
  eq = strcmp (GST_ELEMENT_NAME (element), name) == 0;
  GST_UNLOCK (element);

  if (eq != 0) {
    gst_object_unref (GST_OBJECT (element));
  }
  return eq;
}

/**
 * gst_bin_get_by_name:
 * @bin: #Gstbin to search
 * @name: the element name to search for
 *
 * Get the element with the given name from this bin. This
 * function recurses into subbins.
 *
 * Returns: the element with the given name. Returns NULL if the
 * element is not found or when bad parameters were given. Unref after
 * usage.
 *
 * MT safe.
 */
GstElement *
gst_bin_get_by_name (GstBin * bin, const gchar * name)
{
  GstIterator *children;
  GstIterator *result;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  children = gst_bin_iterate_recurse (bin);
  result = gst_iterator_find_custom (children,
      (GCompareFunc) compare_name, (gpointer) name);
  gst_iterator_free (children);

  return GST_ELEMENT_CAST (result);
}

/**
 * gst_bin_get_by_name_recurse_up:
 * @bin: #Gstbin to search
 * @name: the element name to search for
 *
 * Get the element with the given name from this bin. If the
 * element is not found, a recursion is performed on the parent bin.
 *
 * Returns: the element with the given name or NULL when the element
 * was not found or bad parameters were given. Unref after usage.
 *
 * MT safe.
 */
GstElement *
gst_bin_get_by_name_recurse_up (GstBin * bin, const gchar * name)
{
  GstElement *result;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  result = gst_bin_get_by_name (bin, name);

  if (!result) {
    GstObject *parent;

    parent = gst_object_get_parent (GST_OBJECT_CAST (bin));
    if (parent) {
      if (GST_IS_BIN (parent)) {
        result = gst_bin_get_by_name_recurse_up (GST_BIN_CAST (parent), name);
      }
      gst_object_unref (parent);
    }
  }

  return result;
}

static gint
compare_interface (GstElement * element, gpointer interface)
{
  gint ret;

  if (G_TYPE_CHECK_INSTANCE_TYPE (element, GPOINTER_TO_INT (interface))) {
    ret = 0;
  } else {
    /* we did not find the element, need to release the ref
     * added by the iterator */
    gst_object_unref (GST_OBJECT (element));
    ret = 1;
  }
  return ret;
}

/**
 * gst_bin_get_by_interface:
 * @bin: bin to find element in
 * @interface: interface to be implemented by interface
 *
 * Looks for the first element inside the bin that implements the given
 * interface. If such an element is found, it returns the element. You can
 * cast this element to the given interface afterwards.
 * If you want all elements that implement the interface, use
 * gst_bin_iterate_all_by_interface(). The function recurses bins inside bins.
 *
 * Returns: An element inside the bin implementing the interface. Unref after
 *          usage.
 *
 * MT safe.
 */
GstElement *
gst_bin_get_by_interface (GstBin * bin, GType interface)
{
  GstIterator *children;
  GstIterator *result;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  children = gst_bin_iterate_recurse (bin);
  result = gst_iterator_find_custom (children, (GCompareFunc) compare_interface,
      GINT_TO_POINTER (interface));
  gst_iterator_free (children);

  return GST_ELEMENT_CAST (result);
}

/**
 * gst_bin_get_all_by_interface:
 * @bin: bin to find elements in
 * @interface: interface to be implemented by interface
 *
 * Looks for all elements inside the bin that implements the given
 * interface. You can safely cast all returned elements to the given interface.
 * The function recurses bins inside bins. The iterator will return a series
 * of #GstElement that should be unreffed after usage.
 *
 * Returns: An iterator for the  elements inside the bin implementing the interface.
 *
 * MT safe.
 */
GstIterator *
gst_bin_iterate_all_by_interface (GstBin * bin, GType interface)
{
  GstIterator *children;
  GstIterator *result;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  children = gst_bin_iterate_recurse (bin);
  result = gst_iterator_filter (children, (GCompareFunc) compare_interface,
      GINT_TO_POINTER (interface));

  return result;
}

/**
 * gst_bin_sync_children_state:
 * @bin: #Gstbin to sync state
 *
 * Tries to set the state of the children of this bin to the same state of the
 * bin by calling gst_element_set_state for each child not already having a
 * synchronized state. 
 *
 * Returns: The worst return value of any gst_element_set_state. So if one child
 *          returns #GST_STATE_FAILURE while all others return #GST_STATE_SUCCESS
 *          this function returns #GST_STATE_FAILURE.
 */
GstElementStateReturn
gst_bin_sync_children_state (GstBin * bin)
{
  GList *children;
  GstElement *element;
  GstElementState state;
  GstElementStateReturn ret = GST_STATE_SUCCESS;

  g_return_val_if_fail (GST_IS_BIN (bin), GST_STATE_FAILURE);

  state = GST_STATE (bin);
  children = bin->children;
  GST_CAT_INFO (GST_CAT_STATES,
      "syncing state of children with bin \"%s\"'s state %s",
      GST_ELEMENT_NAME (bin), gst_element_state_get_name (state));

  while (children) {
    element = GST_ELEMENT (children->data);
    children = children->next;
    if (GST_STATE (element) != state) {
      switch (gst_element_set_state (element, state)) {
        case GST_STATE_SUCCESS:
          break;
        case GST_STATE_ASYNC:
          if (ret == GST_STATE_SUCCESS)
            ret = GST_STATE_ASYNC;
          break;
        case GST_STATE_FAILURE:
          ret = GST_STATE_FAILURE;
          break;
        default:
          /* make sure gst_element_set_state never returns this */
          g_assert_not_reached ();
      }
    }
  }

  return ret;
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

  GST_CAT_INFO (GST_CAT_XML, "[%s]: saving %d children",
      GST_ELEMENT_NAME (bin), bin->numchildren);

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
      GST_CAT_INFO (GST_CAT_XML, "[%s]: loading children",
          GST_ELEMENT_NAME (object));
      childlist = field->xmlChildrenNode;
      while (childlist) {
        if (!strcmp (childlist->name, "element")) {
          GstElement *element =
              gst_xml_make_element (childlist, GST_OBJECT (bin));

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
  if (GST_OBJECT_CLASS (parent_class)->restore_thyself)
    (GST_OBJECT_CLASS (parent_class)->restore_thyself) (object, self);
}
#endif /* GST_DISABLE_LOADSAVE */

static GStaticRecMutex iterate_lock = G_STATIC_REC_MUTEX_INIT;

static gboolean
gst_bin_iterate_func (GstBin * bin)
{
  GstScheduler *sched = GST_ELEMENT_SCHEDULER (bin);

  g_static_rec_mutex_unlock (&iterate_lock);

  /* only iterate if this is the manager bin */
  if (sched && sched->parent == GST_ELEMENT (bin)) {
    GstSchedulerState state;

    state = gst_scheduler_iterate (sched);

    if (state == GST_SCHEDULER_STATE_RUNNING) {
      goto done;
    } else if (state == GST_SCHEDULER_STATE_ERROR) {
      gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PAUSED);
    } else if (state == GST_SCHEDULER_STATE_STOPPED) {
      /* check if we have children scheds that are still running */
      /* FIXME: remove in 0.9? autouseless because iterations gone? */
      GList *walk;

      for (walk = sched->schedulers; walk; walk = g_list_next (walk)) {
        GstScheduler *test = walk->data;

        g_return_val_if_fail (test->parent, FALSE);
        if (GST_STATE (test->parent) == GST_STATE_PLAYING) {
          GST_CAT_DEBUG_OBJECT (GST_CAT_SCHEDULING, bin,
              "current bin is not iterating, but children are, "
              "so returning TRUE anyway...");
          g_usleep (1);
          goto done;
        }
      }
    }
  } else {
    g_warning ("bin \"%s\" is not the managing bin, can't be iterated on!\n",
        GST_ELEMENT_NAME (bin));
  }

  g_static_rec_mutex_lock (&iterate_lock);

  return FALSE;

done:
  g_static_rec_mutex_lock (&iterate_lock);
  return TRUE;
}

/**
 * gst_bin_iterate:
 * @bin: a#GstBin to iterate.
 *
 * Iterates over the elements in this bin.
 *
 * Returns: TRUE if the bin did something useful. This value
 *          can be used to determine it the bin is in EOS.
 */
gboolean
gst_bin_iterate (GstBin * bin)
{
  gboolean running;

  g_return_val_if_fail (bin != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BIN (bin), FALSE);

  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, bin, "starting iteration");
  gst_object_ref (GST_OBJECT (bin));

  g_static_rec_mutex_lock (&iterate_lock);
  running = FALSE;
  g_signal_emit (G_OBJECT (bin), gst_bin_signals[ITERATE], 0, &running);
  g_static_rec_mutex_unlock (&iterate_lock);

  gst_object_unref (GST_OBJECT (bin));
  GST_CAT_LOG_OBJECT (GST_CAT_SCHEDULING, bin, "finished iteration");

  return running;
}
