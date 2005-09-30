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
/**
 * SECTION:gstbin
 * @short_description: Base class for elements that contain other elements
 *
 * GstBin is the simplest of the container elements, allowing elements to
 * become children of itself.  Pads from the child elements can be ghosted to
 * the bin, making the bin itself look transparently like any other element,
 * allowing for deep nesting of predefined sub-pipelines.
 *
 * A new GstBin is created with gst_bin_new(). Use a #GstPipeline instead if you
 * want to create a toplevel bin because a normal bin doesn't have a bus or
 * handle clock distribution of its own.
 * 
 * After the bin has been created you will typically add elements to it with
 * gst_bin_add(). You can remove elements with gst_bin_remove().
 *
 * An element can be retrieved from a bin with gst_bin_get_by_name(), using the
 * elements name. gst_bin_get_by_name_recurse_up() is mainly used for internal
 * purposes and will query the parent bins when the element is not found in the
 * current bin.
 * 
 * An iterator of elements in a bin can be retrieved with 
 * gst_bin_iterate_elements(). Various other iterators exist to retrieve the
 * elements in a bin.
 * 
 * The "element_added" signal is fired whenever a new element is added to the
 * bin. Likewise the "element_removed" signal is fired whenever an element is
 * removed from the bin.
 *
 * gst_bin_unref() is used to destroy the bin. 
 */

#include "gst_private.h"

#include "gstevent.h"
#include "gstbin.h"
#include "gstmarshal.h"
#include "gstxml.h"
#include "gstinfo.h"
#include "gsterror.h"

#include "gstindex.h"
#include "gstindexfactory.h"
#include "gstutils.h"
#include "gstchildproxy.h"

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

static GstStateChangeReturn gst_bin_change_state (GstElement * element,
    GstStateChange transition);
static GstStateChangeReturn gst_bin_get_state (GstElement * element,
    GstState * state, GstState * pending, GTimeVal * timeout);

static gboolean gst_bin_add_func (GstBin * bin, GstElement * element);
static gboolean gst_bin_remove_func (GstBin * bin, GstElement * element);

#ifndef GST_DISABLE_INDEX
static void gst_bin_set_index_func (GstElement * element, GstIndex * index);
#endif
static GstClock *gst_bin_provide_clock_func (GstElement * element);
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
static void gst_bin_child_proxy_init (gpointer g_iface, gpointer iface_data);

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
    static const GInterfaceInfo child_proxy_info = {
      gst_bin_child_proxy_init,
      NULL,
      NULL
    };

    _gst_bin_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstBin", &bin_info, 0);

    g_type_add_interface_static (_gst_bin_type, GST_TYPE_CHILD_PROXY,
        &child_proxy_info);

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

static GstObject *
gst_bin_child_proxy_get_child_by_index (GstChildProxy * child_proxy,
    guint index)
{
  return g_list_nth_data (GST_BIN (child_proxy)->children, index);
}

guint
gst_bin_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
  return GST_BIN (child_proxy)->numchildren;
}

static void
gst_bin_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  iface->get_children_count = gst_bin_child_proxy_get_children_count;
  iface->get_child_by_index = gst_bin_child_proxy_get_child_by_index;
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

  parent_class = g_type_class_peek_parent (klass);

  /**
   * GstBin::element-added:
   * @bin: the object which emitted the signal.
   * @element: the element that was added to the bin
   *
   * Will be emitted if a new element was removed/added to this bin.
   */
  gst_bin_signals[ELEMENT_ADDED] =
      g_signal_new ("element-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GstBinClass, element_added), NULL,
      NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);
  /**
   * GstBin::element-removed:
   * @bin: the object which emitted the signal.
   * @element: the element that was removed from the bin
   *
   * Will be emitted if an element was removed from this bin.
   */
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
  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_bin_provide_clock_func);
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

  /* Set up a bus for listening to child elements */
  bus = g_object_new (gst_bus_get_type (), NULL);
  bin->child_bus = bus;
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bin_bus_handler, bin);
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
 * The ref of the returned clock in increased so unref after usage.
 *
 * MT safe
 *
 * FIXME, clock selection is not correct here.
 */
static GstClock *
gst_bin_provide_clock_func (GstElement * element)
{
  GstClock *result = NULL;
  GstBin *bin;
  GList *children;

  bin = GST_BIN (element);

  GST_LOCK (bin);
  for (children = bin->children; children; children = g_list_next (children)) {
    GstElement *child = GST_ELEMENT (children->data);

    result = gst_element_provide_clock (child);
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
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_IS_SINK)) {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PARENTAGE, bin, "element \"%s\" was sink",
        elem_name);
    GST_FLAG_SET (bin, GST_ELEMENT_IS_SINK);
  }

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

  GST_LOCK (element);
  /* Check if the element is already being removed and immediately
   * return */
  if (G_UNLIKELY (GST_FLAG_IS_SET (element, GST_ELEMENT_UNPARENTING)))
    goto already_removing;

  GST_FLAG_SET (element, GST_ELEMENT_UNPARENTING);
  /* grab element name so we can print it */
  elem_name = g_strdup (GST_ELEMENT_NAME (element));
  GST_UNLOCK (element);

  /* unlink all linked pads */
  it = gst_element_iterate_pads (element);
  gst_iterator_foreach (it, (GFunc) unlink_pads, element);
  gst_iterator_free (it);

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

  gst_element_set_bus (element, NULL);

  /* unlock any waiters for the state change. It is possible that
   * we are waiting for an ASYNC state change on this element. The
   * element cannot be added to another bin yet as it is not yet
   * unparented. */
  GST_STATE_LOCK (element);
  GST_STATE_BROADCAST (element);
  GST_STATE_UNLOCK (element);

  /* we ref here because after the _unparent() the element can be disposed
   * and we still need it to reset the UNPARENTING flag and fire a signal. */
  gst_object_ref (element);
  gst_object_unparent (GST_OBJECT_CAST (element));

  GST_LOCK (element);
  GST_FLAG_UNSET (element, GST_ELEMENT_UNPARENTING);
  GST_UNLOCK (element);

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
already_removing:
  {
    GST_UNLOCK (element);
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
 * gst_object_ref() before removing it from the bin.
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
static GstStateChangeReturn
gst_bin_get_state (GstElement * element, GstState * state,
    GstState * pending, GTimeVal * timeout)
{
  GstBin *bin = GST_BIN (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
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
        case GST_STATE_CHANGE_FAILURE:
          goto done;
        case GST_STATE_CHANGE_NO_PREROLL:
          /* we have to continue scanning as there might be
           * ERRORS too */
          have_no_preroll = TRUE;
          break;
        case GST_STATE_CHANGE_ASYNC:
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
      ret = GST_STATE_CHANGE_NO_PREROLL;
      goto done;
    }

    /* if we get here, no NO_PREROLL elements are in the pipeline */
    GST_CAT_INFO_OBJECT (GST_CAT_STATES, element, "no NO_PREROLL elements");

    /* if no ASYNC elements exist we don't even have to poll with a
     * timeout again */
    if (!have_async) {
      ret = GST_STATE_CHANGE_SUCCESS;
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
      case GST_STATE_CHANGE_SUCCESS:
        break;
      case GST_STATE_CHANGE_FAILURE:
      case GST_STATE_CHANGE_NO_PREROLL:
        /* report FAILURE and NO_PREROLL immediatly */
        goto done;
      case GST_STATE_CHANGE_ASYNC:
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
    case GST_STATE_CHANGE_SUCCESS:
      /* we can commit the state */
      gst_element_commit_state (element);
      break;
    case GST_STATE_CHANGE_FAILURE:
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

/***********************************************
 * Topologically sorted iterator 
 * see http://en.wikipedia.org/wiki/Topological_sorting
 *
 * For each element in the graph, an entry is kept in a HashTable
 * with its number of srcpad connections (degree). 
 * We then change state of all elements without dependencies 
 * (degree 0) and decrement the degree of all elements connected
 * on the sinkpads. When an element reaches degree 0, its state is
 * changed next.
 * When all elements are handled the algorithm stops.
 */
typedef struct _GstBinSortIterator
{
  GstIterator it;
  GQueue *queue;                /* elements queued for state change */
  GstBin *bin;                  /* bin we iterate */
  gint mode;                    /* adding or removing dependency */
  GstElement *best;             /* next element with least dependencies */
  gint best_deg;                /* best degree */
  GHashTable *hash;             /* has table with element dependencies */
} GstBinSortIterator;

/* we add and subtract 1 to make sure we don't confuse NULL and 0 */
#define HASH_SET_DEGREE(bit, elem, deg) \
    g_hash_table_replace (bit->hash, elem, GINT_TO_POINTER(deg+1))
#define HASH_GET_DEGREE(bit, elem) \
    (GPOINTER_TO_INT(g_hash_table_lookup (bit->hash, elem))-1)

/* add element to queue of next elements in the iterator.
 * We push at the tail to give higher priority elements a
 * chance first */
static void
add_to_queue (GstBinSortIterator * bit, GstElement * element)
{
  GST_DEBUG ("%s add to queue", GST_ELEMENT_NAME (element));
  gst_object_ref (element);
  g_queue_push_tail (bit->queue, element);
  HASH_SET_DEGREE (bit, element, -1);
}

/* clear the queue, unref all objects as we took a ref when
 * we added them to the queue */
static void
clear_queue (GQueue * queue)
{
  gpointer p;

  while ((p = g_queue_pop_head (queue)))
    gst_object_unref (p);
}

/* set all degrees to 0. Elements marked as a sink are
 * added to the queue immediatly. */
static void
reset_degree (GstElement * element, GstBinSortIterator * bit)
{
  /* sinks are added right away */
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_IS_SINK)) {
    add_to_queue (bit, element);
  } else {
    /* others are marked with 0 and handled when sinks are done */
    HASH_SET_DEGREE (bit, element, 0);
  }
}

/* adjust the degree of all elements connected to the given
 * element. If an degree of an element drops to 0, it is
 * added to the queue of elements to schedule next.
 *
 * We have to make sure not to cross the bin boundary this element
 * belongs to.
 */
static void
update_degree (GstElement * element, GstBinSortIterator * bit)
{
  gboolean linked = FALSE;

  GST_LOCK (element);
  /* don't touch degree is element has no sourcepads */
  if (element->numsinkpads != 0) {
    /* loop over all sinkpads, decrement degree for all connected
     * elements in this bin */
    GList *pads;

    for (pads = element->sinkpads; pads; pads = g_list_next (pads)) {
      GstPad *peer;

      if ((peer = gst_pad_get_peer (GST_PAD_CAST (pads->data)))) {
        GstElement *peer_element;

        if ((peer_element = gst_pad_get_parent_element (peer))) {
          GST_LOCK (peer_element);
          if (GST_OBJECT_CAST (peer_element)->parent ==
              GST_OBJECT_CAST (bit->bin)) {
            gint old_deg, new_deg;

            old_deg = HASH_GET_DEGREE (bit, peer_element);
            new_deg = old_deg + bit->mode;

            GST_DEBUG ("change element %s, degree %d->%d, linked to %s",
                GST_ELEMENT_NAME (peer_element),
                old_deg, new_deg, GST_ELEMENT_NAME (element));

            /* update degree */
            if (new_deg == 0) {
              /* degree hit 0, add to queue */
              add_to_queue (bit, peer_element);
            } else {
              HASH_SET_DEGREE (bit, peer_element, new_deg);
            }
            linked = TRUE;
          }
          GST_UNLOCK (peer_element);
          gst_object_unref (peer_element);
        }
        gst_object_unref (peer);
      }
    }
  }
  if (!linked) {
    GST_DEBUG ("element %s not linked to anything", GST_ELEMENT_NAME (element));
  }
  GST_UNLOCK (element);
}

/* find the next best element not handled yet. This is the one
 * with the lowest non-negative degree */
static void
find_element (GstElement * element, GstBinSortIterator * bit)
{
  gint degree;

  /* element is already handled */
  if ((degree = HASH_GET_DEGREE (bit, element)) < 0)
    return;

  /* first element or element with smaller degree */
  if (bit->best == NULL || bit->best_deg > degree) {
    bit->best = element;
    bit->best_deg = degree;
  }
}

/* get next element in iterator. the returned element has the
 * refcount increased */
static GstIteratorResult
gst_bin_sort_iterator_next (GstBinSortIterator * bit, gpointer * result)
{
  /* empty queue, we have to find a next best element */
  if (g_queue_is_empty (bit->queue)) {
    bit->best = NULL;
    bit->best_deg = G_MAXINT;
    g_list_foreach (bit->bin->children, (GFunc) find_element, bit);
    if (bit->best) {
      if (bit->best_deg != 0) {
        /* we don't fail on this one yet */
        g_warning ("loop detected in the graph !!");
      }
      /* best unhandled element, schedule as next element */
      GST_DEBUG ("queue empty, next best: %s", GST_ELEMENT_NAME (bit->best));
      gst_object_ref (bit->best);
      HASH_SET_DEGREE (bit, bit->best, -1);
      *result = bit->best;
    } else {
      GST_DEBUG ("queue empty, elements exhausted");
      /* no more unhandled elements, we are done */
      return GST_ITERATOR_DONE;
    }
  } else {
    /* everything added to the queue got reffed */
    *result = g_queue_pop_head (bit->queue);
  }

  GST_DEBUG ("queue head gives %s", GST_ELEMENT_NAME (*result));
  /* update degrees of linked elements */
  update_degree (GST_ELEMENT_CAST (*result), bit);

  return GST_ITERATOR_OK;
}

/* clear queues, recalculate the degrees and restart. */
static void
gst_bin_sort_iterator_resync (GstBinSortIterator * bit)
{
  clear_queue (bit->queue);
  /* reset degrees */
  g_list_foreach (bit->bin->children, (GFunc) reset_degree, bit);
  /* calc degrees, incrementing */
  bit->mode = 1;
  g_list_foreach (bit->bin->children, (GFunc) update_degree, bit);
  /* for the rest of the function we decrement the degrees */
  bit->mode = -1;
}

/* clear queues, unref bin and free iterator. */
static void
gst_bin_sort_iterator_free (GstBinSortIterator * bit)
{
  clear_queue (bit->queue);
  g_queue_free (bit->queue);
  g_hash_table_destroy (bit->hash);
  gst_object_unref (bit->bin);
  g_free (bit);
}

/**
 * gst_bin_iterate_sorted:
 * @bin: #Gstbin to iterate on
 *
 * Get an iterator for the elements in this bin in topologically
 * sorted order. This means that the elements are returned from
 * the most downstream elements (sinks) to the sources.
 *
 * This function is used internally to perform the state changes 
 * of the bin elements.
 *
 * Each element will have its refcount increased, so unref
 * after use.
 *
 * MT safe. 
 *
 * Returns: a #GstIterator of #GstElements. gst_iterator_free after use.
 */
GstIterator *
gst_bin_iterate_sorted (GstBin * bin)
{
  GstBinSortIterator *result;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  GST_LOCK (bin);
  gst_object_ref (bin);
  /* we don't need a NextFunction because we ref the items in the _next
   * method already */
  result = (GstBinSortIterator *)
      gst_iterator_new (sizeof (GstBinSortIterator),
      GST_GET_LOCK (bin),
      &bin->children_cookie,
      (GstIteratorNextFunction) gst_bin_sort_iterator_next,
      (GstIteratorItemFunction) NULL,
      (GstIteratorResyncFunction) gst_bin_sort_iterator_resync,
      (GstIteratorFreeFunction) gst_bin_sort_iterator_free);
  result->queue = g_queue_new ();
  result->hash = g_hash_table_new (NULL, NULL);
  result->bin = bin;
  gst_bin_sort_iterator_resync (result);
  GST_UNLOCK (bin);

  return (GstIterator *) result;
}

static GstStateChangeReturn
gst_bin_element_set_state (GstBin * bin, GstElement * element, GstState pending)
{
  GstStateChangeReturn ret;
  gboolean locked;

  /* peel off the locked flag */
  GST_LOCK (element);
  locked = GST_FLAG_IS_SET (element, GST_ELEMENT_LOCKED_STATE);
  GST_UNLOCK (element);

  /* skip locked elements */
  if (G_UNLIKELY (locked)) {
    ret = GST_STATE_CHANGE_SUCCESS;
    goto done;
  }

  /* change state */
  ret = gst_element_set_state (element, pending);

done:
  return ret;
}

static GstStateChangeReturn
gst_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstBin *bin;
  GstStateChangeReturn ret;
  GstState old_state, pending;
  gboolean have_async;
  gboolean have_no_preroll;
  GstClockTime base_time;
  GstIterator *it;
  gboolean done;

  /* we don't need to take the STATE_LOCK, it is already taken */
  old_state = GST_STATE (element);
  pending = GST_STATE_PENDING (element);

  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
      "changing state of children from %s to %s",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (pending));

  if (pending == GST_STATE_VOID_PENDING)
    return GST_STATE_CHANGE_SUCCESS;

  bin = GST_BIN_CAST (element);

  /* Clear eosed element list on READY-> PAUSED */
  if (transition == GST_STATE_CHANGE_READY_TO_PAUSED) {
    g_list_free (bin->eosed);
    bin->eosed = NULL;
  }

  /* iterate in state change order */
  it = gst_bin_iterate_sorted (bin);

restart:
  /* take base time */
  base_time = element->base_time;

  have_async = FALSE;
  have_no_preroll = FALSE;

  done = FALSE;
  while (!done) {
    gpointer data;

    switch (gst_iterator_next (it, &data)) {
      case GST_ITERATOR_OK:
      {
        GstElement *element;

        element = GST_ELEMENT_CAST (data);

        /* set base time on element */
        gst_element_set_base_time (element, base_time);

        /* set state now */
        ret = gst_bin_element_set_state (bin, element, pending);

        switch (ret) {
          case GST_STATE_CHANGE_SUCCESS:
            GST_CAT_DEBUG (GST_CAT_STATES,
                "child '%s' changed state to %d(%s) successfully",
                GST_ELEMENT_NAME (element), pending,
                gst_element_state_get_name (pending));
            break;
          case GST_STATE_CHANGE_ASYNC:
            GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
                "child '%s' is changing state asynchronously",
                GST_ELEMENT_NAME (element));
            have_async = TRUE;
            break;
          case GST_STATE_CHANGE_FAILURE:
            GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
                "child '%s' failed to go to state %d(%s)",
                GST_ELEMENT_NAME (element),
                pending, gst_element_state_get_name (pending));
            gst_object_unref (element);
            goto done;
          case GST_STATE_CHANGE_NO_PREROLL:
            GST_CAT_DEBUG (GST_CAT_STATES,
                "child '%s' changed state to %d(%s) successfully without preroll",
                GST_ELEMENT_NAME (element), pending,
                gst_element_state_get_name (pending));
            have_no_preroll = TRUE;
            break;
          default:
            g_assert_not_reached ();
            break;
        }
        gst_object_unref (element);
        break;
      }
      case GST_ITERATOR_RESYNC:
        GST_CAT_DEBUG (GST_CAT_STATES, "iterator doing resync");
        gst_iterator_resync (it);
        goto restart;
        break;
      default:
      case GST_ITERATOR_DONE:
        GST_CAT_DEBUG (GST_CAT_STATES, "iterator done");
        done = TRUE;
        break;
    }
  }

  if (have_no_preroll) {
    ret = GST_STATE_CHANGE_NO_PREROLL;
  } else if (have_async) {
    ret = GST_STATE_CHANGE_ASYNC;
  } else {
    ret = parent_class->change_state (element, transition);
  }

done:
  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
      "done changing bin's state from %s to %s, now in %s, ret %d",
      gst_element_state_get_name (old_state),
      gst_element_state_get_name (pending),
      gst_element_state_get_name (GST_STATE (element)), ret);

  gst_iterator_free (it);

  return ret;
}

static void
gst_bin_dispose (GObject * object)
{
  GstBin *bin = GST_BIN (object);

  GST_CAT_DEBUG_OBJECT (GST_CAT_REFCOUNTING, object, "dispose");

  g_list_free (bin->eosed);
  bin->eosed = NULL;
  gst_object_unref (bin->child_bus);
  bin->child_bus = NULL;

  while (bin->children) {
    gst_bin_remove (bin, GST_ELEMENT (bin->children->data));
  }
  if (G_UNLIKELY (bin->children != NULL)) {
    g_critical ("could not remove elements from bin %s",
        GST_STR_NULL (GST_OBJECT_NAME (object)));
  }

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
  GST_DEBUG_OBJECT (bin, "[msg %p] handling child message of type %s",
      message, gst_message_type_get_name (GST_MESSAGE_TYPE (message)));

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:{
      GstObject *src = GST_MESSAGE_SRC (message);

      if (src) {
        gchar *name;

        name = gst_object_get_name (src);
        GST_DEBUG_OBJECT (bin, "got EOS message from %s", name);
        g_free (name);

        /* collect all eos messages from the children */
        GST_LOCK (bin->child_bus);
        bin->eosed = g_list_prepend (bin->eosed, src);
        GST_UNLOCK (bin->child_bus);

        /* if we are completely EOS, we forward an EOS message */
        if (is_eos (bin)) {
          GST_DEBUG_OBJECT (bin, "all sinks posted EOS");
          gst_element_post_message (GST_ELEMENT (bin),
              gst_message_new_eos (GST_OBJECT (bin)));
        }
      } else {
        GST_DEBUG_OBJECT (bin, "got EOS message from (NULL), not processing");
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
 * gst_bin_iterate_all_by_interface:
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
