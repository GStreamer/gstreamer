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
static GstElementStateReturn gst_bin_get_state (GstElement * element,
    GstElementState * state, GstElementState * pending, GTimeVal * timeout);

static gboolean gst_bin_add_func (GstBin * bin, GstElement * element);
static gboolean gst_bin_remove_func (GstBin * bin, GstElement * element);

#ifndef GST_DISABLE_INDEX
static void gst_bin_set_index_func (GstElement * element, GstIndex * index);
#endif
static GstClock *gst_bin_get_clock_func (GstElement * element);
static void gst_bin_set_clock_func (GstElement * element, GstClock * clock);

static gboolean gst_bin_send_event (GstElement * element, GstEvent * event);
static GstBusSyncReply bin_bus_handler (GstBus * bus,
    GstMessage * message, GstBin * bin);
static gboolean gst_bin_query (GstElement * element, GstQuery * query);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr gst_bin_save_thyself (GstObject * object, xmlNodePtr parent);
static void gst_bin_restore_thyself (GstObject * object, xmlNodePtr self);
#endif

static gint bin_element_is_sink (GstElement * child, GstBin * bin);

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
  gstelement_class->set_index = GST_DEBUG_FUNCPTR (gst_bin_set_index_func);
#endif
  gstelement_class->get_clock = GST_DEBUG_FUNCPTR (gst_bin_get_clock_func);
  gstelement_class->set_clock = GST_DEBUG_FUNCPTR (gst_bin_set_clock_func);

  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_bin_send_event);
  gstelement_class->query = GST_DEBUG_FUNCPTR (gst_bin_query);

  klass->add_element = GST_DEBUG_FUNCPTR (gst_bin_add_func);
  klass->remove_element = GST_DEBUG_FUNCPTR (gst_bin_remove_func);
}

static void
gst_bin_init (GstBin * bin)
{
  GstBus *bus;

  bin->numchildren = 0;
  bin->children = NULL;
  bin->children_cookie = 0;
  bin->eosed = NULL;

  /* Set up a bus for listening to child elements,
   * and one for sending messages up the hierarchy */
  bus = g_object_new (gst_bus_get_type (), NULL);
  bin->child_bus = bus;
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bin_bus_handler, bin);

  bus = g_object_new (gst_bus_get_type (), NULL);
  gst_element_set_bus (GST_ELEMENT (bin), bus);
  /* set_bus refs the bus via gst_object_replace, we drop our ref */
  gst_object_unref (bus);
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

static gboolean
is_eos (GstBin * bin)
{
  GstIterator *sinks;
  gboolean result = TRUE;
  gboolean done = FALSE;

  sinks = gst_bin_iterate_sinks (bin);
  while (!done) {
    gpointer data;

    switch (gst_iterator_next (sinks, &data)) {
      case GST_ITERATOR_OK:
      {
        GstElement *element = GST_ELEMENT (data);
        GList *eosed;
        gchar *name;

        name = gst_element_get_name (element);
        eosed = g_list_find (bin->eosed, element);
        if (!eosed) {
          GST_DEBUG ("element %s did not post EOS yet", name);
          result = FALSE;
          done = TRUE;
        } else {
          GST_DEBUG ("element %s posted EOS", name);
        }
        g_free (name);
        gst_object_unref (element);
        break;
      }
      case GST_ITERATOR_RESYNC:
        result = TRUE;
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
  return result;
}

static void
unlink_pads (GstPad * pad)
{
  GstPad *peer;

  if ((peer = gst_pad_get_peer (pad))) {
    if (gst_pad_get_direction (pad) == GST_PAD_SRC)
      gst_pad_unlink (pad, peer);
    else
      gst_pad_unlink (peer, pad);
    gst_object_unref (peer);
  }
  gst_object_unref (pad);
}

/* add an element to this bin
 *
 * MT safe
 */
static gboolean
gst_bin_add_func (GstBin * bin, GstElement * element)
{
  gchar *elem_name;
  GstIterator *it;

  /* we obviously can't add ourself to ourself */
  if (G_UNLIKELY (GST_ELEMENT_CAST (element) == GST_ELEMENT_CAST (bin)))
    goto adding_itself;

  /* get the element name to make sure it is unique in this bin. */
  GST_LOCK (element);
  elem_name = g_strdup (GST_ELEMENT_NAME (element));
  GST_UNLOCK (element);

  GST_LOCK (bin);

  /* then check to see if the element's name is already taken in the bin,
   * we can safely take the lock here. This check is probably bogus because
   * you can safely change the element name after this check and before setting
   * the object parent. The window is very small though... */
  if (G_UNLIKELY (!gst_object_check_uniqueness (bin->children, elem_name)))
    goto duplicate_name;

  /* set the element's parent and add the element to the bin's list of children */
  if (G_UNLIKELY (!gst_object_set_parent (GST_OBJECT_CAST (element),
              GST_OBJECT_CAST (bin))))
    goto had_parent;

  /* if we add a sink we become a sink */
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_IS_SINK))
    GST_FLAG_SET (bin, GST_ELEMENT_IS_SINK);

  bin->children = g_list_prepend (bin->children, element);
  bin->numchildren++;
  bin->children_cookie++;

  /* distribute the bus */
  gst_element_set_bus (element, bin->child_bus);

  /* propagate the current base time and clock */
  gst_element_set_base_time (element, GST_ELEMENT (bin)->base_time);
  gst_element_set_clock (element, GST_ELEMENT_CLOCK (bin));

  GST_UNLOCK (bin);

  /* unlink all linked pads */
  it = gst_element_iterate_pads (element);
  gst_iterator_foreach (it, (GFunc) unlink_pads, element);
  gst_iterator_free (it);

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
 * If the element's pads are linked to other pads, the pads will be unlinked
 * before the element is added to the bin.
 *
 * MT safe.
 *
 * Returns: TRUE if the element could be added, FALSE on wrong parameters or
 * the bin does not want to accept the element.
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
  GstIterator *it;

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

  /* check if we removed a sink */
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_IS_SINK)) {
    GList *other_sink;

    /* check if we removed the last sink */
    other_sink = g_list_find_custom (bin->children,
        bin, (GCompareFunc) bin_element_is_sink);
    if (!other_sink) {
      /* yups, we're not a sink anymore */
      GST_FLAG_UNSET (bin, GST_ELEMENT_IS_SINK);
    }
  }
  GST_UNLOCK (bin);

  GST_CAT_INFO_OBJECT (GST_CAT_PARENTAGE, bin, "removed child \"%s\"",
      elem_name);
  g_free (elem_name);

  /* unlink all linked pads */
  it = gst_element_iterate_pads (element);
  gst_iterator_foreach (it, (GFunc) unlink_pads, element);
  gst_iterator_free (it);

  gst_element_set_bus (element, NULL);

  /* unlock any waiters for the state change. It is possible that
   * we are waiting for an ASYNC state change on this element. The
   * element cannot be added to another bin yet as it is not yet
   * unparented. */
  GST_STATE_LOCK (element);
  GST_STATE_BROADCAST (element);
  GST_STATE_UNLOCK (element);

  /* we ref here because after the _unparent() the element can be disposed
   * and we still need it to fire a signal. */
  gst_object_ref (element);
  gst_object_unparent (GST_OBJECT_CAST (element));

  g_signal_emit (G_OBJECT (bin), gst_bin_signals[ELEMENT_REMOVED], 0, element);
  /* element is really out of our control now */
  gst_object_unref (element);

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
 * If the element's pads are linked to other pads, the pads will be unlinked
 * before the element is removed from the bin.
 *
 * MT safe.
 *
 * Returns: TRUE if the element could be removed, FALSE on wrong parameters or
 * the bin does not want to remove the element.
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
  gst_object_ref (child);
  return GST_ITERATOR_ITEM_PASS;
}

/**
 * gst_bin_iterate_elements:
 * @bin: #Gstbin to iterate the elements of
 *
 * Get an iterator for the elements in this bin.
 * Each element will have its refcount increased, so unref
 * after use.
 *
 * MT safe.
 *
 * Returns: a #GstIterator of #GstElements. gst_iterator_free after
 * use. returns NULL when passing bad parameters.
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
  gst_object_ref (bin);
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
  gst_object_ref (child);
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
 * after use. This iterator recurses into GstBin children.
 *
 * MT safe.
 *
 * Returns: a #GstIterator of #GstElements. gst_iterator_free after
 * use. returns NULL when passing bad parameters.
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
  gst_object_ref (bin);
  result = gst_iterator_new_list (GST_GET_LOCK (bin),
      &bin->children_cookie,
      &bin->children,
      bin,
      (GstIteratorItemFunction) iterate_child_recurse,
      (GstIteratorDisposeFunction) gst_object_unref);
  GST_UNLOCK (bin);

  return result;
}

/* returns 0 when TRUE because this is a GCompareFunc */
/* MT safe */
static gint
bin_element_is_sink (GstElement * child, GstBin * bin)
{
  gboolean is_sink;

  /* we lock the child here for the remainder of the function to
   * get its name safely. */
  GST_LOCK (child);
  is_sink = GST_FLAG_IS_SET (child, GST_ELEMENT_IS_SINK);

  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, bin,
      "child %s %s sink", GST_OBJECT_NAME (child), is_sink ? "is" : "is not");

  GST_UNLOCK (child);
  return is_sink ? 0 : 1;
}

/* check if object has the given ancestor somewhere up in
 * the hierarchy
 */
static gboolean
has_ancestor (GstObject * object, GstObject * ancestor)
{
  GstObject *parent;
  gboolean result = FALSE;

  if (object == NULL)
    return FALSE;

  if (object == ancestor)
    return TRUE;

  parent = gst_object_get_parent (object);
  result = has_ancestor (parent, ancestor);
  if (parent)
    gst_object_unref (parent);

  return result;
}

/* returns 0 when TRUE because this is a GCompareFunc.
 * This function returns elements that have no connected srcpads and
 * are therefore not reachable from a real sink. */
/* MT safe */
static gint
bin_element_is_semi_sink (GstElement * child, GstBin * bin)
{
  int ret = 1;

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
     * is a semi sink because it has no linked source pads */
    GList *pads;
    gboolean connected_src = FALSE;

    for (pads = child->srcpads; pads; pads = g_list_next (pads)) {
      GstPad *peer;

      if ((peer = gst_pad_get_peer (GST_PAD_CAST (pads->data)))) {
        connected_src =
            has_ancestor (GST_OBJECT_CAST (peer), GST_OBJECT_CAST (bin));
        gst_object_unref (peer);
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

  return ret;
}

static gint
sink_iterator_filter (GstElement * child, GstBin * bin)
{
  if (bin_element_is_sink (child, bin) == 0) {
    /* returns 0 because this is a GCompareFunc */
    return 0;
  } else {
    /* child carries a ref from gst_bin_iterate_elements -- drop if not passing
       through */
    gst_object_unref ((GstObject *) child);
    return 1;
  }
}

/**
 * gst_bin_iterate_sinks:
 * @bin: #Gstbin to iterate on
 *
 * Get an iterator for the sink elements in this bin.
 * Each element will have its refcount increased, so unref
 * after use.
 *
 * The sink elements are those without any linked srcpads.
 *
 * MT safe.
 *
 * Returns: a #GstIterator of #GstElements. gst_iterator_free after use.
 */
GstIterator *
gst_bin_iterate_sinks (GstBin * bin)
{
  GstIterator *children;
  GstIterator *result;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  children = gst_bin_iterate_elements (bin);
  result = gst_iterator_filter (children,
      (GCompareFunc) sink_iterator_filter, bin);

  return result;
}

/* 2 phases:
 *  1) check state of all children with 0 timeout to find ERROR and
 *     NO_PREROLL elements. return if found.
 *  2) perform full blocking wait with requested timeout.
 *
 * 2) cannot be performed when 1) returns results as the sinks might
 *    not be able to complete the state change making 2) block forever.
 *
 * MT safe
 */
static GstElementStateReturn
gst_bin_get_state (GstElement * element, GstElementState * state,
    GstElementState * pending, GTimeVal * timeout)
{
  GstBin *bin = GST_BIN (element);
  GstElementStateReturn ret = GST_STATE_SUCCESS;
  GList *children;
  guint32 children_cookie;
  gboolean have_no_preroll;

  GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "getting state");

  /* lock bin, no element can be added or removed between going into
   * the quick scan and the blocking wait. */
  GST_LOCK (bin);

restart:
  have_no_preroll = FALSE;

  /* first we need to poll with a non zero timeout to make sure we don't block
   * on the sinks when we have NO_PREROLL elements. This is why we do
   * a quick check if there are still NO_PREROLL elements. We also
   * catch the error elements this way. */
  {
    GTimeVal tv;
    gboolean have_async = FALSE;

    GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "checking for NO_PREROLL");
    /* use 0 timeout so we don't block on the sinks */
    GST_TIME_TO_TIMEVAL (0, tv);
    children = bin->children;
    children_cookie = bin->children_cookie;
    while (children) {
      GstElement *child = GST_ELEMENT_CAST (children->data);

      gst_object_ref (child);
      /* now we release the lock to enter a non blocking wait. We 
       * release the lock anyway since we can. */
      GST_UNLOCK (bin);

      ret = gst_element_get_state (child, NULL, NULL, &tv);

      gst_object_unref (child);

      /* now grab the lock to iterate to the next child */
      GST_LOCK (bin);
      if (G_UNLIKELY (children_cookie != bin->children_cookie)) {
        /* child added/removed during state change, restart. We need
         * to restart with the quick check as a no-preroll element could
         * have been added here and we don't want to block on sinks then.*/
        goto restart;
      }

      switch (ret) {
          /* report FAILURE  immediatly */
        case GST_STATE_FAILURE:
          goto done;
        case GST_STATE_NO_PREROLL:
          /* we have to continue scanning as there might be
           * ERRORS too */
          have_no_preroll = TRUE;
          break;
        case GST_STATE_ASYNC:
          have_async = TRUE;
          break;
        default:
          break;
      }
      children = g_list_next (children);
    }
    /* if we get here, we have no FAILURES, check for any NO_PREROLL
     * elements then. */
    if (have_no_preroll) {
      ret = GST_STATE_NO_PREROLL;
      goto done;
    }

    /* if we get here, no NO_PREROLL elements are in the pipeline */
    GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "no NO_PREROLL elements");

    /* if no ASYNC elements exist we don't even have to poll with a
     * timeout again */
    if (!have_async) {
      ret = GST_STATE_SUCCESS;
      goto done;
    }
  }

  /* next we poll all children for their state to see if one of them
   * is still busy with its state change. We did not release the bin lock
   * yet so the elements are the same as the ones from the quick scan. */
  children = bin->children;
  children_cookie = bin->children_cookie;
  while (children) {
    GstElement *child = GST_ELEMENT_CAST (children->data);

    gst_object_ref (child);
    /* now we release the lock to enter the potentialy blocking wait */
    GST_UNLOCK (bin);

    /* ret is ASYNC if some child is still performing the state change
     * ater the timeout. */
    ret = gst_element_get_state (child, NULL, NULL, timeout);

    gst_object_unref (child);

    /* now grab the lock to iterate to the next child */
    GST_LOCK (bin);
    if (G_UNLIKELY (children_cookie != bin->children_cookie)) {
      /* child added/removed during state change, restart. We need
       * to restart with the quick check as a no-preroll element could
       * have been added here and we don't want to block on sinks then.*/
      goto restart;
    }

    switch (ret) {
      case GST_STATE_SUCCESS:
        break;
      case GST_STATE_FAILURE:
      case GST_STATE_NO_PREROLL:
        /* report FAILURE and NO_PREROLL immediatly */
        goto done;
      case GST_STATE_ASYNC:
        goto done;
      default:
        g_assert_not_reached ();
    }
    children = g_list_next (children);
  }
  /* if we got here, all elements can do preroll */
  have_no_preroll = FALSE;

done:
  GST_UNLOCK (bin);

  /* now we can take the state lock, it is possible that new elements
   * are added now and we still report the old state. No problem though as
   * the return is still consistent, the effect is as if the element was
   * added after this function completed. */
  GST_STATE_LOCK (bin);
  switch (ret) {
    case GST_STATE_SUCCESS:
      /* we can commit the state */
      gst_element_commit_state (element);
      break;
    case GST_STATE_FAILURE:
      /* some element failed, abort the state change */
      gst_element_abort_state (element);
      break;
    default:
      /* other cases are just passed along */
      break;
  }

  /* and report the state if needed */
  if (state)
    *state = GST_STATE (element);
  if (pending)
    *pending = GST_STATE_PENDING (element);

  GST_STATE_NO_PREROLL (element) = have_no_preroll;

  GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
      "state current: %s, pending: %s, error: %d, no_preroll: %d, result: %d",
      gst_element_state_get_name (GST_STATE (element)),
      gst_element_state_get_name (GST_STATE_PENDING (element)),
      GST_STATE_ERROR (element), GST_STATE_NO_PREROLL (element), ret);

  GST_STATE_UNLOCK (bin);

  return ret;
}

static void
append_child (gpointer child, GQueue * queue)
{
  g_queue_push_tail (queue, child);
}

/**
 * gst_bin_iterate_state_order:
 * @bin: #Gstbin to iterate on
 *
 * Get an iterator for the elements in this bin in the order
 * in which a state change should be performed on them. This 
 * means that first the sinks and then the other elements will
 * be returned.
 * Each element will have its refcount increased, so unref
 * after use.
 *
 * Not implemented yet.
 *
 * MT safe.
 *
 * Returns: a #GstIterator of #GstElements. gst_iterator_free after use.
 */
GstIterator *
gst_bin_iterate_state_order (GstBin * bin)
{
  GstIterator *result;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  result = NULL;

  return result;
}

static void
clear_queue (GQueue * queue, gboolean unref)
{
  gpointer p;

  while ((p = g_queue_pop_head (queue)))
    if (unref)
      gst_object_unref (p);
}

static void
remove_all_from_queue (GQueue * queue, gpointer elem, gboolean unref)
{
  gpointer p;

  while ((p = g_queue_find (queue, elem))) {
    if (unref)
      gst_object_unref (elem);
    g_queue_delete_link (queue, p);
  }
}

/* this function is called with the STATE_LOCK held. It works
 * as follows:
 *
 * 1) put all sink elements on the queue.
 * 2) put all semisink elements on the queue.
 * 3) change state of elements in queue, put linked elements to queue.
 * 4) while queue not empty goto 3)
 *
 * This will effectively change the state of all elements in the bin
 * from the sinks to the sources. We have to change the states this
 * way so that when a source element pushes data, the downstream element
 * is in the right state to receive the data.
 *
 * MT safe.
 */
/* FIXME,  make me more elegant, want to use a topological sort algorithm
 * based on indegrees (or outdegrees in our case) */
static GstElementStateReturn
gst_bin_change_state (GstElement * element)
{
  GstBin *bin;
  GstElementStateReturn ret;
  GstElementState old_state, pending;
  gboolean have_async = FALSE;
  gboolean have_no_preroll = FALSE;
  GList *children;
  guint32 children_cookie;
  GQueue *elem_queue;           /* list of elements waiting for a state change */
  GQueue *semi_queue;           /* list of elements with no connected srcpads */
  GQueue *temp;                 /* queue of leftovers */
  GstClockTime base_time;

  bin = GST_BIN (element);

  /* we don't need to take the STATE_LOCK, it is already taken */
  old_state = GST_STATE (element);
  pending = GST_STATE_PENDING (element);

  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
      "changing state of children from %s to %s",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (pending));

  if (pending == GST_STATE_VOID_PENDING)
    return GST_STATE_SUCCESS;

  /* Clear eosed element list on READY-> PAUSED */
  if (GST_STATE_TRANSITION (element) == GST_STATE_READY_TO_PAUSED) {
    g_list_free (bin->eosed);
    bin->eosed = NULL;
  }

  /* all elements added to these queues should have their refcount
   * incremented */
  elem_queue = g_queue_new ();
  semi_queue = g_queue_new ();
  temp = g_queue_new ();

  /* first step, find all sink elements, these are the elements
   * without (linked) source pads. */
  GST_LOCK (bin);

restart:
  /* take base time */
  base_time = element->base_time;

  /* make sure queues are empty, they could be filled when 
   * restarting. */
  clear_queue (elem_queue, TRUE);
  clear_queue (semi_queue, TRUE);
  clear_queue (temp, TRUE);

  children = bin->children;
  children_cookie = bin->children_cookie;
  while (children) {
    GstElement *child = GST_ELEMENT_CAST (children->data);

    gst_object_ref (child);
    GST_UNLOCK (bin);

    if (bin_element_is_sink (child, bin) == 0) {
      g_queue_push_tail (elem_queue, child);
    } else if (bin_element_is_semi_sink (child, bin) == 0) {
      g_queue_push_tail (semi_queue, child);
    } else {
      g_queue_push_tail (temp, child);
    }

    GST_LOCK (bin);
    if (G_UNLIKELY (children_cookie != bin->children_cookie)) {
      GST_INFO_OBJECT (bin, "bin->children_cookie changed, restarting");
      /* restart will unref the children in the queues so that we don't
       * leak refcounts. */
      goto restart;
    }
    children = g_list_next (children);
  }
  GST_UNLOCK (bin);
  /* after this point new elements can be added/removed from the
   * bin. We operate on the snapshot taken above. Applications
   * should serialize their add/remove and set_state. */

  /* now change state for semi sink elements first so add them in
   * front of the other elements */
  g_queue_foreach (temp, (GFunc) append_child, semi_queue);
  clear_queue (temp, FALSE);

  /* if we don't have real sinks, we continue with the other elements */
  if (g_queue_is_empty (elem_queue) && !g_queue_is_empty (semi_queue)) {
    GQueue *q = elem_queue;

    /* we swap the queues as oposed to copy them over */
    elem_queue = semi_queue;
    semi_queue = q;
  }

  /* second step, change state of elements in the queue */
  while (!g_queue_is_empty (elem_queue)) {
    GstElement *qelement;
    GList *pads;
    gboolean locked;

    /* take element */
    qelement = g_queue_pop_head (elem_queue);
    /* we don't need any duplicates in the other queue anymore */
    remove_all_from_queue (semi_queue, qelement, TRUE);

    /* queue all elements connected to the sinkpads of this element */
    GST_LOCK (qelement);
    pads = qelement->sinkpads;
    while (pads) {
      GstPad *pad = GST_PAD_CAST (pads->data);
      GstPad *peer;

      GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
          "found sinkpad %s:%s", GST_DEBUG_PAD_NAME (pad));

      peer = gst_pad_get_peer (pad);
      if (peer) {
        GstObject *peer_parent;

        /*  get parent */
        peer_parent = gst_object_get_parent (GST_OBJECT (peer));

        /* if we have an element parent, follow it */
        if (peer_parent && GST_IS_ELEMENT (peer_parent)) {
          GstObject *parent;

          /* see if this element is in the bin we are currently handling */
          parent = gst_object_get_parent (peer_parent);
          if (parent) {
            if (parent == GST_OBJECT_CAST (bin)) {
              GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
                  "adding element %s to queue", GST_ELEMENT_NAME (peer_parent));

              /* make sure we don't have duplicates */
              remove_all_from_queue (semi_queue, peer_parent, TRUE);
              remove_all_from_queue (elem_queue, peer_parent, TRUE);

              /* was reffed before pushing on the queue by the
               * gst_object_get_parent() call we used to get the element. */
              g_queue_push_tail (elem_queue, peer_parent);
              /* so that we don't unref it */
              peer_parent = NULL;
            } else {
              GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
                  "not adding element %s to queue, it is in another bin",
                  GST_ELEMENT_NAME (peer_parent));
            }
            gst_object_unref (parent);
          }
        }
        if (peer_parent)
          gst_object_unref (peer_parent);

        gst_object_unref (peer);
      } else {
        GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
            "pad %s:%s does not have a peer", GST_DEBUG_PAD_NAME (pad));
      }
      pads = g_list_next (pads);
    }
    /* peel off the locked flag and release the element lock */
    locked = GST_FLAG_IS_SET (qelement, GST_ELEMENT_LOCKED_STATE);
    GST_UNLOCK (qelement);

    /* skip locked elements */
    if (G_UNLIKELY (locked))
      goto next_element;

    /* set base time on element */
    gst_element_set_base_time (qelement, base_time);

    /* then change state */
    ret = gst_element_set_state (qelement, pending);

    /* the set state could have cause elements to be added/removed,
     * we support that. */
    GST_LOCK (bin);
    if (G_UNLIKELY (children_cookie != bin->children_cookie)) {
      gst_object_unref (qelement);
      goto restart;
    }
    GST_UNLOCK (bin);

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
        gst_object_unref (qelement);
        goto exit;
      case GST_STATE_NO_PREROLL:
        GST_CAT_DEBUG (GST_CAT_STATES,
            "child '%s' changed state to %d(%s) successfully without preroll",
            GST_ELEMENT_NAME (qelement), pending,
            gst_element_state_get_name (pending));
        have_no_preroll = TRUE;
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  next_element:
    gst_object_unref (qelement);

    /* if queue is empty now, continue with a non-sink */
    if (g_queue_is_empty (elem_queue)) {
      GstElement *non_sink;

      GST_DEBUG ("sinks and upstream elements exhausted");
      non_sink = g_queue_pop_head (semi_queue);
      if (non_sink) {
        GST_DEBUG ("found lefover non-sink %s", GST_OBJECT_NAME (non_sink));
        g_queue_push_tail (elem_queue, non_sink);
      }
    }
  }

  if (have_no_preroll) {
    ret = GST_STATE_NO_PREROLL;
  } else if (have_async) {
    ret = GST_STATE_ASYNC;
  } else {
    ret = parent_class->change_state (element);
  }

  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
      "done changing bin's state from %s to %s, now in %s, ret %d",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (pending),
      gst_element_state_get_name (GST_STATE (element)), ret);

exit:
  /* release refcounts in queue, should normally be empty unless we
   * had an error. */
  clear_queue (elem_queue, TRUE);
  clear_queue (semi_queue, TRUE);
  g_queue_free (elem_queue);
  g_queue_free (semi_queue);
  g_queue_free (temp);

  return ret;
}

static void
gst_bin_dispose (GObject * object)
{
  GstBin *bin = GST_BIN (object);

  GST_CAT_DEBUG_OBJECT (GST_CAT_REFCOUNTING, object, "dispose");

  /* ref to not hit 0 again */
  gst_object_ref (object);

  g_list_free (bin->eosed);
  bin->eosed = NULL;
  gst_object_unref (bin->child_bus);
  bin->child_bus = NULL;
  gst_element_set_bus (GST_ELEMENT (bin), NULL);

  while (bin->children) {
    gst_bin_remove (bin, GST_ELEMENT (bin->children->data));
  }
  GST_CAT_DEBUG_OBJECT (GST_CAT_REFCOUNTING, object, "dispose no children");
  g_assert (bin->children == NULL);
  g_assert (bin->numchildren == 0);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/*
 * This function is a utility event handler for seek events.
 * It will send the event to all sinks.
 * Applications are free to override this behaviour and
 * implement their own seek handler, but this will work for
 * pretty much all cases in practice.
 */
static gboolean
gst_bin_send_event (GstElement * element, GstEvent * event)
{
  GstBin *bin = GST_BIN (element);
  GstIterator *iter;
  gboolean res = TRUE;
  gboolean done = FALSE;

  iter = gst_bin_iterate_sinks (bin);
  GST_DEBUG_OBJECT (bin, "Sending event to sink children");

  while (!done) {
    gpointer data;

    switch (gst_iterator_next (iter, &data)) {
      case GST_ITERATOR_OK:
      {
        GstElement *sink;

        gst_event_ref (event);
        sink = GST_ELEMENT_CAST (data);
        res &= gst_element_send_event (sink, event);
        gst_object_unref (sink);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        res = TRUE;
        break;
      default:
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
  gst_event_unref (event);

  return res;
}

static GstBusSyncReply
bin_bus_handler (GstBus * bus, GstMessage * message, GstBin * bin)
{
  GST_DEBUG_OBJECT (bin, "[msg %p] handling child message of type %d",
      message, GST_MESSAGE_TYPE (message));

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:{
      gchar *name = gst_object_get_name (GST_MESSAGE_SRC (message));

      GST_DEBUG_OBJECT (bin, "got EOS message from %s", name);
      g_free (name);

      /* collect all eos messages from the children */
      GST_LOCK (bin->child_bus);
      bin->eosed = g_list_prepend (bin->eosed, GST_MESSAGE_SRC (message));
      GST_UNLOCK (bin->child_bus);

      /* if we are completely EOS, we forward an EOS message */
      if (is_eos (bin)) {
        GST_DEBUG_OBJECT (bin, "all sinks posted EOS");
        gst_element_post_message (GST_ELEMENT (bin),
            gst_message_new_eos (GST_OBJECT (bin)));
      }

      /* we drop all EOS messages */
      gst_message_unref (message);
      break;
    }
    default:
      /* Send all other messages upward */
      GST_DEBUG_OBJECT (bin, "posting message upward");
      gst_element_post_message (GST_ELEMENT (bin), message);
      break;
  }

  return GST_BUS_DROP;
}

static gboolean
gst_bin_query (GstElement * element, GstQuery * query)
{
  GstBin *bin = GST_BIN (element);
  GstIterator *iter;
  gboolean res = FALSE, done = FALSE;

  iter = gst_bin_iterate_sinks (bin);
  GST_DEBUG_OBJECT (bin, "Sending event to sink children");

  while (!(res || done)) {
    gpointer data;

    switch (gst_iterator_next (iter, &data)) {
      case GST_ITERATOR_OK:
      {
        GstElement *sink;

        sink = GST_ELEMENT_CAST (data);
        res = gst_element_query (sink, query);
        gst_object_unref (sink);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      default:
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);

  return res;
}

static gint
compare_name (GstElement * element, const gchar * name)
{
  gint eq;

  GST_LOCK (element);
  eq = strcmp (GST_ELEMENT_NAME (element), name);
  GST_UNLOCK (element);

  if (eq != 0) {
    gst_object_unref (element);
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
 * MT safe.
 *
 * Returns: the element with the given name. Returns NULL if the
 * element is not found or when bad parameters were given. Unref after
 * use.
 */
GstElement *
gst_bin_get_by_name (GstBin * bin, const gchar * name)
{
  GstIterator *children;
  GstIterator *result;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  GST_CAT_INFO (GST_CAT_PARENTAGE, "[%s]: looking up child element %s",
      GST_ELEMENT_NAME (bin), name);

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
 * MT safe.
 *
 * Get the element with the given name from this bin. If the
 * element is not found, a recursion is performed on the parent bin.
 *
 * Returns: the element with the given name or NULL when the element
 * was not found or bad parameters were given. Unref after use.
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
    gst_object_unref (element);
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
 * gst_bin_iterate_all_by_interface(). The function recurses inside bins.
 *
 * MT safe.
 *
 * Returns: An #GstElement inside the bin implementing the interface.
 *          Unref after use.
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
 * of #GstElement that should be unreffed after use.
 *
 * MT safe.
 *
 * Returns: A #GstIterator for the elements inside the bin implementing the
 *          given interface.
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

  childlist = xmlNewChild (parent, NULL, (xmlChar *) "children", NULL);

  GST_CAT_INFO (GST_CAT_XML, "[%s]: saving %d children",
      GST_ELEMENT_NAME (bin), bin->numchildren);

  children = bin->children;
  while (children) {
    child = GST_ELEMENT (children->data);
    elementnode = xmlNewChild (childlist, NULL, (xmlChar *) "element", NULL);
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
    if (!strcmp ((char *) field->name, "children")) {
      GST_CAT_INFO (GST_CAT_XML, "[%s]: loading children",
          GST_ELEMENT_NAME (object));
      childlist = field->xmlChildrenNode;
      while (childlist) {
        if (!strcmp ((char *) childlist->name, "element")) {
          GstElement *element =
              gst_xml_make_element (childlist, GST_OBJECT (bin));

          /* it had to be parented to find the pads, now we ref and unparent so
           * we can add it to the bin */
          gst_object_ref (element);
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
