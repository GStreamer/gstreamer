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

static void gst_bin_dispose (GObject * object);

static GstElementStateReturn gst_bin_change_state (GstElement * element);
static gboolean gst_bin_get_state (GstElement * element,
    GstElementState * state, GstElementState * pending, GTimeVal * timeout);

#ifndef GST_DISABLE_INDEX
static void gst_bin_set_index (GstElement * element, GstIndex * index);
#endif
static void gst_bin_set_clock (GstElement * element, GstClock * clock);
static GstClock *gst_bin_get_clock (GstElement * element);
static void gst_bin_set_bus (GstElement * element, GstBus * bus);
static void gst_bin_set_scheduler (GstElement * element, GstScheduler * sched);

static gboolean gst_bin_add_func (GstBin * bin, GstElement * element);
static gboolean gst_bin_remove_func (GstBin * bin, GstElement * element);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr gst_bin_save_thyself (GstObject * object, xmlNodePtr parent);
static void gst_bin_restore_thyself (GstObject * object, xmlNodePtr self);
#endif

/* Bin signals and args */
enum
{
  ELEMENT_ADDED,
  ELEMENT_REMOVED,
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

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_bin_dispose);

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself = GST_DEBUG_FUNCPTR (gst_bin_save_thyself);
  gstobject_class->restore_thyself =
      GST_DEBUG_FUNCPTR (gst_bin_restore_thyself);
#endif

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_bin_change_state);
  gstelement_class->get_state = GST_DEBUG_FUNCPTR (gst_bin_get_state);
#ifndef GST_DISABLE_INDEX
  gstelement_class->set_index = GST_DEBUG_FUNCPTR (gst_bin_set_index);
#endif
  gstelement_class->get_clock = GST_DEBUG_FUNCPTR (gst_bin_get_clock);
  gstelement_class->set_clock = GST_DEBUG_FUNCPTR (gst_bin_set_clock);
  gstelement_class->set_bus = GST_DEBUG_FUNCPTR (gst_bin_set_bus);
  gstelement_class->set_scheduler = GST_DEBUG_FUNCPTR (gst_bin_set_scheduler);

  klass->add_element = GST_DEBUG_FUNCPTR (gst_bin_add_func);
  klass->remove_element = GST_DEBUG_FUNCPTR (gst_bin_remove_func);
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

#ifndef GST_DISABLE_INDEX
static void
gst_bin_set_index (GstElement * element, GstIndex * index)
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

static void
gst_bin_set_clock (GstElement * element, GstClock * clock)
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

static GstClock *
gst_bin_get_clock (GstElement * element)
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

static void
gst_bin_set_bus (GstElement * element, GstBus * bus)
{
  GList *children;
  GstBin *bin;

  bin = GST_BIN (element);

  parent_class->set_bus (element, bus);

  GST_LOCK (bin);
  for (children = bin->children; children; children = g_list_next (children)) {
    GstElement *child = GST_ELEMENT (children->data);

    gst_element_set_bus (child, bus);
  }
  GST_UNLOCK (bin);
}

static void
gst_bin_set_scheduler (GstElement * element, GstScheduler * sched)
{
  GList *children;
  GstBin *bin;

  bin = GST_BIN (element);

  parent_class->set_scheduler (element, sched);

  GST_LOCK (bin);
  for (children = bin->children; children; children = g_list_next (children)) {
    GstElement *child = GST_ELEMENT (children->data);

    gst_element_set_scheduler (child, sched);
  }
  GST_UNLOCK (bin);
}

static gboolean
gst_bin_add_func (GstBin * bin, GstElement * element)
{
  gchar *elem_name;

  /* we obviously can't add ourself to ourself */
  if (G_UNLIKELY (GST_ELEMENT_CAST (element) == GST_ELEMENT_CAST (bin)))
    goto adding_itself;

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
  if (G_UNLIKELY (!gst_object_set_parent (GST_OBJECT (element),
              GST_OBJECT (bin))))
    goto had_parent;

  bin->children = g_list_prepend (bin->children, element);
  bin->numchildren++;
  bin->children_cookie++;

  gst_element_set_manager (element, GST_ELEMENT (bin)->manager);
  gst_element_set_bus (element, GST_ELEMENT (bin)->bus);
  gst_element_set_scheduler (element, GST_ELEMENT (bin)->scheduler);

  GST_UNLOCK (bin);

  GST_CAT_DEBUG_OBJECT (GST_CAT_PARENTAGE, bin, "added element \"%s\"",
      elem_name);
  g_free (elem_name);

  g_signal_emit (G_OBJECT (bin), gst_bin_signals[ELEMENT_ADDED], 0, element);

  return TRUE;

  /* ERROR handling here */
adding_itself:
  GST_LOCK (bin);
  g_warning ("Cannot add bin %s to itself", GST_ELEMENT_NAME (bin));
  GST_UNLOCK (bin);
  return FALSE;

duplicate_name:
  g_warning ("Name %s is not unique in bin %s, not adding",
      elem_name, GST_ELEMENT_NAME (bin));
  GST_UNLOCK (bin);
  g_free (elem_name);
  return FALSE;

had_parent:
  g_warning ("Element %s already has parent", elem_name);
  GST_UNLOCK (bin);
  g_free (elem_name);
  return FALSE;
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

no_function:
  g_warning ("adding elements to bin %s is not supported",
      GST_ELEMENT_NAME (bin));
  return FALSE;
}

static gboolean
gst_bin_remove_func (GstBin * bin, GstElement * element)
{
  gchar *elem_name;

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

  gst_element_set_manager (element, NULL);

  /* we ref here because after the _unparent() the element can be disposed
   * and we still need it to fire a signal. */
  gst_object_ref (GST_OBJECT (element));
  gst_object_unparent (GST_OBJECT (element));

  g_signal_emit (G_OBJECT (bin), gst_bin_signals[ELEMENT_REMOVED], 0, element);
  gst_object_unref (GST_OBJECT (element));

  return TRUE;

not_in_bin:
  g_warning ("Element %s is not in bin %s", elem_name, GST_ELEMENT_NAME (bin));
  GST_UNLOCK (bin);
  g_free (elem_name);
  return FALSE;
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

no_function:
  g_warning ("removing elements from bin %s is not supported",
      GST_ELEMENT_NAME (bin));
  return FALSE;
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
  gst_object_ref (GST_OBJECT (bin));
  result = gst_iterator_new_list (GST_GET_LOCK (bin),
      &bin->children_cookie,
      &bin->children,
      bin,
      (GstIteratorRefFunction) gst_object_ref,
      (GstIteratorUnrefFunction) gst_object_unref,
      (GstIteratorDisposeFunction) gst_object_unref);
  GST_UNLOCK (bin);

  return result;
}

/* returns 0 if the element is a sink, this is made so that
 * we can use this function as a filter */
static gint
bin_element_is_sink (GstElement * child, GstBin * bin)
{
  gint ret = 1;

  /* check if this is a sink element, these are the elements
   * without (linked) source pads. */
  if (child->numsrcpads == 0) {
    /* shortcut */
    GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, bin,
        "adding child %s as sink", gst_element_get_name (child));
    ret = 0;
  } else {
    /* loop over all pads, try to figure out if this element
     * is a sink because it has no linked source pads */
    GList *pads;
    gboolean connected_src = FALSE;

    /* FIXME not MT safe */
    for (pads = child->srcpads; pads; pads = g_list_next (pads)) {
      GstPad *pad = GST_PAD (pads->data);

      if (GST_PAD_IS_LINKED (pad)) {
        connected_src = TRUE;
        break;
      }
    }
    if (connected_src) {
      GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, bin,
          "not adding child %s as sink: linked source pads",
          gst_element_get_name (child));
    } else {
      GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, bin,
          "adding child %s as sink since it has unlinked source pads",
          gst_element_get_name (child));
      ret = 0;
    }
  }
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
  result = gst_iterator_filter (children, bin,
      (GCompareFunc) bin_element_is_sink);

  return result;
}

static gint
bin_find_pending_child (GstElement * child, GTimeVal * timeout)
{
  gboolean ret;

  ret = gst_element_get_state (GST_ELEMENT (child), NULL, NULL, timeout);
  /* ret is false if some child is still performing the state change */
  gst_object_unref (GST_OBJECT (child));

  return (ret == FALSE ? 0 : 1);
}

/* this functions loops over all children, as soon as one is
 * still performing the state change, FALSE is returned. */
static gboolean
gst_bin_get_state (GstElement * element, GstElementState * state,
    GstElementState * pending, GTimeVal * timeout)
{
  gboolean ret = TRUE;
  GstBin *bin = GST_BIN (element);
  GstIterator *children;
  gboolean have_async = FALSE;
  gpointer child;

  /* we cannot take the state lock yet as we might block when querying
   * the children, holding the lock too long for no reason */
  /* FIXME, we can loop the list ourselves instead of creating the
   * iterator */
  children = gst_bin_iterate_sinks (bin);
  child = gst_iterator_find_custom (children, timeout,
      (GCompareFunc) bin_find_pending_child);
  gst_iterator_free (children);
  /* we unreffed the child in the comparefunc */
  if (child) {
    have_async = TRUE;
    ret = FALSE;
  }

  /* now we can take the state lock */
  GST_STATE_LOCK (bin);
  if (!have_async) {
    /* no async children, we can commit the state */
    gst_element_commit_state (element);
  }

  /* and report the state */
  if (state)
    *state = GST_STATE (element);
  if (pending)
    *pending = GST_STATE_PENDING (element);

  GST_STATE_UNLOCK (bin);

  return ret;
}

/* this function is called with the STATE_LOCK held */
static GstElementStateReturn
gst_bin_change_state (GstElement * element)
{
  GstBin *bin;
  GstElementStateReturn ret;
  GstElementState old_state, pending;
  gboolean have_async = FALSE;
  GstIterator *sinks;
  gboolean done = FALSE;

  GQueue *elem_queue;           /* list of elements waiting for a state change */

  bin = GST_BIN (element);

  old_state = GST_STATE (element);
  pending = GST_STATE_PENDING (element);

  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
      "changing state of children from %s to %s",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (pending));

  if (pending == GST_STATE_VOID_PENDING)
    return GST_STATE_SUCCESS;

  elem_queue = g_queue_new ();

  /* first step, find all sink elements, these are the elements
   * without (linked) source pads. */
  /* FIXME, we can iterate the list ourselves */
  sinks = gst_bin_iterate_sinks (bin);
  while (!done) {
    gpointer child;

    switch (gst_iterator_next (sinks, &child)) {
      case GST_ITERATOR_OK:
        /* this also keeps the refcount on the element */
        g_queue_push_tail (elem_queue, child);
        break;
      case GST_ITERATOR_RESYNC:
        /* undo what we had */
        g_queue_foreach (elem_queue, (GFunc) gst_object_unref, NULL);
        while (g_queue_pop_head (elem_queue));
        gst_iterator_resync (sinks);
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }
  gst_iterator_free (sinks);

  /* second step, change state of elements in the queue */
  while (!g_queue_is_empty (elem_queue)) {
    GstElement *qelement = g_queue_pop_head (elem_queue);
    GList *pads;

    /* queue all elements connected to the sinkpads of this element */
    /* FIXME, not MT safe !! */
    for (pads = qelement->sinkpads; pads; pads = g_list_next (pads)) {
      GstPad *pad = GST_PAD (pads->data);
      GstPad *peer;

      GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
          "found sinkpad %s:%s", GST_DEBUG_PAD_NAME (pad));

      peer = gst_pad_get_peer (pad);
      if (peer) {
        GstElement *peer_elem;

        /* FIXME does not work for bins etc */
        peer_elem = GST_ELEMENT (gst_object_get_parent (GST_OBJECT (peer)));

        if (peer_elem) {
          GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
              "adding element %s to queue", gst_element_get_name (peer_elem));

          /* is reffed before pushing on the queue */
          g_queue_push_tail (elem_queue, peer_elem);
        }
        gst_object_unref (GST_OBJECT (peer));
      } else {
        GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
            "pad %s:%s does not have a peer", GST_DEBUG_PAD_NAME (pad));
      }
    }

    if (GST_FLAG_IS_SET (qelement, GST_ELEMENT_LOCKED_STATE))
      goto next_element;

    /* FIXME handle delayed elements like src and loop based
     * elements */
    ret = gst_element_set_state (qelement, pending);
    switch (ret) {
      case GST_STATE_SUCCESS:
        GST_CAT_DEBUG (GST_CAT_STATES,
            "child '%s' changed state to %d(%s) successfully",
            GST_ELEMENT_NAME (qelement), pending,
            gst_element_state_get_name (pending));
        break;
      case GST_STATE_ASYNC:
        GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
            "child '%s' is changing state asynchronously",
            GST_ELEMENT_NAME (qelement));
        have_async = TRUE;
        break;
      case GST_STATE_FAILURE:
        GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
            "child '%s' failed to go to state %d(%s)",
            GST_ELEMENT_NAME (qelement),
            pending, gst_element_state_get_name (pending));
        ret = GST_STATE_FAILURE;
        /* release refcount of element we popped off the queue */
        gst_object_unref (GST_OBJECT (qelement));
        goto exit;
      default:
        g_assert_not_reached ();
        break;
    }
  next_element:
    gst_object_unref (GST_OBJECT (qelement));
  }

  if (have_async) {
    ret = GST_STATE_ASYNC;
  } else {
    if (parent_class->change_state) {
      ret = parent_class->change_state (element);
    } else {
      ret = GST_STATE_SUCCESS;
    }
    if (ret == GST_STATE_SUCCESS) {
      /* we can commit the state change now */
      gst_element_commit_state (element);
    }
  }

  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
      "done changing bin's state from %s to %s, now in %s",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (pending),
      gst_element_state_get_name (GST_STATE (element)));

exit:
  /* release refcounts in queue, should normally be empty */
  g_queue_foreach (elem_queue, (GFunc) gst_object_unref, NULL);
  g_queue_free (elem_queue);

  return ret;
}


static void
gst_bin_dispose (GObject * object)
{
  GstBin *bin = GST_BIN (object);

  GST_CAT_DEBUG_OBJECT (GST_CAT_REFCOUNTING, object, "dispose");

  /* ref to not hit 0 again */
  gst_object_ref (GST_OBJECT (object));
  gst_element_set_state (GST_ELEMENT (object), GST_STATE_NULL);

  while (bin->children) {
    gst_bin_remove (bin, GST_ELEMENT (bin->children->data));
  }
  GST_CAT_DEBUG_OBJECT (GST_CAT_REFCOUNTING, object, "dispose no children");
  g_assert (bin->children == NULL);
  g_assert (bin->numchildren == 0);

  G_OBJECT_CLASS (parent_class)->dispose (object);
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
 * element is not found or when bad parameters were given.
 *
 * MT safe.
 */
GstElement *
gst_bin_get_by_name (GstBin * bin, const gchar * name)
{
  GList *children;
  GstElement *result = NULL;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  GST_CAT_INFO (GST_CAT_PARENTAGE, "[%s]: looking up child element %s",
      GST_ELEMENT_NAME (bin), name);

  GST_LOCK (bin);
  for (children = bin->children; children; children = g_list_next (children)) {
    GstElement *child = GST_ELEMENT_CAST (children->data);
    gboolean eq;

    GST_LOCK (child);
    eq = strcmp (GST_ELEMENT_NAME (child), name) == 0;
    GST_UNLOCK (child);
    if (eq) {
      result = child;
      break;
    }
    if (GST_IS_BIN (child)) {
      result = gst_bin_get_by_name (GST_BIN (child), name);
      if (result) {
        break;
      }
    }
  }
  GST_UNLOCK (bin);

  return result;
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
 * was not found or bad parameters were given.
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

    if (parent && GST_IS_BIN (parent)) {
      result = gst_bin_get_by_name_recurse_up (GST_BIN_CAST (parent), name);
    }
    gst_object_unref (parent);
  }

  return result;
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
 * gst_bin_get_all_by_interface(). The function recurses bins inside bins.
 *
 * Returns: An element inside the bin implementing the interface.
 *
 * MT safe.
 */
GstElement *
gst_bin_get_by_interface (GstBin * bin, GType interface)
{
  GList *walk;
  GstElement *result = NULL;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);
  g_return_val_if_fail (G_TYPE_IS_INTERFACE (interface), NULL);

  GST_LOCK (bin);
  for (walk = bin->children; walk; walk = g_list_next (walk)) {
    if (G_TYPE_CHECK_INSTANCE_TYPE (walk->data, interface)) {
      result = GST_ELEMENT_CAST (walk->data);
      break;
    }
    if (GST_IS_BIN (walk->data)) {
      result = gst_bin_get_by_interface (GST_BIN (walk->data), interface);
      if (result)
        break;
    }
  }
  GST_UNLOCK (bin);

  return result;
}

/**
 * gst_bin_get_all_by_interface:
 * @bin: bin to find elements in
 * @interface: interface to be implemented by interface
 *
 * Looks for all elements inside the bin that implements the given
 * interface. You can safely cast all returned elements to the given interface.
 * The function recurses bins inside bins. You need to free the list using
 * g_list_free() after use.
 *
 * Returns: The elements inside the bin implementing the interface.
 *
 * MT safe.
 */
GList *
gst_bin_get_all_by_interface (GstBin * bin, GType interface)
{
  GList *walk;
  GList *ret = NULL;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);
  g_return_val_if_fail (G_TYPE_IS_INTERFACE (interface), NULL);

  GST_LOCK (bin);
  for (walk = bin->children; walk; walk = g_list_next (walk)) {
    if (G_TYPE_CHECK_INSTANCE_TYPE (walk->data, interface)) {
      ret = g_list_prepend (ret, walk->data);
    }
    if (GST_IS_BIN (walk->data)) {
      ret = g_list_concat (ret,
          gst_bin_get_all_by_interface (GST_BIN (walk->data), interface));
    }
  }
  GST_UNLOCK (bin);

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
