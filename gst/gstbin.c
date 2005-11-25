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
 * @short_description: Base class and element that can contain other elements
 *
 * #GstBin is an element that can contain other #GstElement, allowing them to be
 * managed as a group.
 * Pads from the child elements can be ghosted to the bin, see #GstGhostPad.
 * This makes the bin look like any other elements and enables creation of
 * higher-level abstraction elements.
 *
 * A new #GstBin is created with gst_bin_new(). Use a #GstPipeline instead if you
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
 * gst_object_unref() is used to drop your reference to the bin.
 *
 * The <link linkend="GstBin-element-added">element-added</link> signal is
 * fired whenever a new element is added to the bin. Likewise the <link
 * linkend="GstBin-element-removed">element-removed</link> signal is fired
 * whenever an element is removed from the bin.
 *
 * <refsect2><title>Notes</title>
 * <para>
 * A #GstBin internally intercepts every #GstMessage posted by its children and
 * implements the following default behaviour for each of them:
 * <variablelist>
 *   <varlistentry>
 *     <term>GST_MESSAGE_EOS</term>
 *     <listitem><para>This message is only posted by sinks in the PLAYING
 *     state. If all sinks posted the EOS message, this bin will post and EOS
 *     message upwards.</para></listitem>
 *   </varlistentry>
 *   <varlistentry>
 *     <term>GST_MESSAGE_SEGMENT_START</term>
 *     <listitem><para>just collected and never forwarded upwards.
 *     The messages are used to decide when all elements have completed playback
 *     of their segment.</para></listitem>
 *   </varlistentry>
 *   <varlistentry>
 *     <term>GST_MESSAGE_SEGMENT_DONE</term>
 *     <listitem><para> Is posted by #GstBin when all elements that posted
 *     a SEGMENT_START have posted a SEGMENT_DONE.</para></listitem>
 *   </varlistentry>
 *   <varlistentry>
 *     <term>OTHERS</term>
 *     <listitem><para> posted upwards.</para></listitem>
 *   </varlistentry>
 * </variablelist>
 *
 * A #GstBin implements the following default behaviour for answering to a
 * #GstQuery:
 * <variablelist>
 *   <varlistentry>
 *     <term>GST_QUERY_DURATION</term>
 *     <listitem><para>If the query has been asked before with the same format,
 *     use the cached previous value. If no previous value was cached, the
 *     query is sent to all sink elements in the bin and the MAXIMUM of all
 *     values is returned and cached. If no sinks are available in the bin, the
 *     query fails.</para></listitem>
 *   </varlistentry>
 *   <varlistentry>
 *     <term>OTHERS</term>
 *     <listitem><para>the query is forwarded to all sink elements, the result
 *     of the first sink that answers the query successfully is returned. If no
 *     sink is in the bin, the query fails.</para></listitem>
 *   </varlistentry>
 * </variablelist>
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2005-11-23 (0.9.5)
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

static GstElementDetails gst_bin_details = GST_ELEMENT_DETAILS ("Generic bin",
    "Generic/Bin",
    "Simple container object",
    "Erik Walthinsen <omega@cse.ogi.edu>," "Wim Taymans <wim@fluendo.com>");

static void gst_bin_dispose (GObject * object);

static void gst_bin_recalc_state (GstBin * bin, gboolean force);
static GstStateChangeReturn gst_bin_change_state_func (GstElement * element,
    GstStateChange transition);
static GstStateChangeReturn gst_bin_get_state_func (GstElement * element,
    GstState * state, GstState * pending, GstClockTime timeout);

static gboolean gst_bin_add_func (GstBin * bin, GstElement * element);
static gboolean gst_bin_remove_func (GstBin * bin, GstElement * element);

#ifndef GST_DISABLE_INDEX
static void gst_bin_set_index_func (GstElement * element, GstIndex * index);
#endif
static GstClock *gst_bin_provide_clock_func (GstElement * element);
static gboolean gst_bin_set_clock_func (GstElement * element, GstClock * clock);

static void gst_bin_handle_message_func (GstBin * bin, GstMessage * message);
static gboolean gst_bin_send_event (GstElement * element, GstEvent * event);
static GstBusSyncReply bin_bus_handler (GstBus * bus,
    GstMessage * message, GstBin * bin);
static gboolean gst_bin_query (GstElement * element, GstQuery * query);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr gst_bin_save_thyself (GstObject * object, xmlNodePtr parent);
static void gst_bin_restore_thyself (GstObject * object, xmlNodePtr self);
#endif

static void bin_remove_messages (GstBin * bin, GstObject * src,
    GstMessageType types);
static void gst_bin_recalc_func (GstBin * child, gpointer data);
static gint bin_element_is_sink (GstElement * child, GstBin * bin);

static GstIterator *gst_bin_sort_iterator_new (GstBin * bin);

/* Bin signals and properties */
enum
{
  ELEMENT_ADDED,
  ELEMENT_REMOVED,
  LAST_SIGNAL
};

enum
{
  PROP_0
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
  static GType gst_bin_type = 0;

  if (!gst_bin_type) {
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

    gst_bin_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstBin", &bin_info, 0);

    g_type_add_interface_static (gst_bin_type, GST_TYPE_CHILD_PROXY,
        &child_proxy_info);

    GST_DEBUG_CATEGORY_INIT (bin_debug, "bin", GST_DEBUG_BOLD,
        "debugging info for the 'bin' container element");
  }
  return gst_bin_type;
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
  GstObject *res;
  GstBin *bin;

  bin = GST_BIN_CAST (child_proxy);

  GST_OBJECT_LOCK (bin);
  if ((res = g_list_nth_data (bin->children, index)))
    gst_object_ref (res);
  GST_OBJECT_UNLOCK (bin);

  return res;
}

guint
gst_bin_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
  guint num;
  GstBin *bin;

  bin = GST_BIN_CAST (child_proxy);

  GST_OBJECT_LOCK (bin);
  num = bin->numchildren;
  GST_OBJECT_UNLOCK (bin);

  return num;
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
  GError *err;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  /**
   * GstBin::element-added:
   * @bin: the #GstBin
   * @element: the #GstElement that was added to the bin
   *
   * Will be emitted after the element was added to the bin.
   */
  gst_bin_signals[ELEMENT_ADDED] =
      g_signal_new ("element-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GstBinClass, element_added), NULL,
      NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_ELEMENT);
  /**
   * GstBin::element-removed:
   * @bin: the #GstBin
   * @element: the #GstElement that was removed from the bin
   *
   * Will be emitted after the element was removed from the bin.
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

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_bin_change_state_func);
  gstelement_class->get_state = GST_DEBUG_FUNCPTR (gst_bin_get_state_func);
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
  klass->handle_message = GST_DEBUG_FUNCPTR (gst_bin_handle_message_func);

  GST_DEBUG ("creating bin thread pool");
  err = NULL;
  klass->pool =
      g_thread_pool_new ((GFunc) gst_bin_recalc_func, NULL, 1, FALSE, &err);
  if (err != NULL) {
    g_critical ("could alloc threadpool %s", err->message);
  }
}

static void
gst_bin_init (GstBin * bin)
{
  GstBus *bus;

  bin->numchildren = 0;
  bin->children = NULL;
  bin->children_cookie = 0;
  bin->messages = NULL;
  bin->polling = FALSE;
  bin->state_dirty = FALSE;
  bin->provided_clock = NULL;
  bin->clock_dirty = FALSE;

  /* Set up a bus for listening to child elements */
  bus = g_object_new (gst_bus_get_type (), NULL);
  bin->child_bus = bus;
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bin_bus_handler, bin);
}

static void
gst_bin_dispose (GObject * object)
{
  GstBin *bin = GST_BIN (object);

  GST_CAT_DEBUG_OBJECT (GST_CAT_REFCOUNTING, object, "dispose");

  bin_remove_messages (bin, NULL, GST_MESSAGE_ANY);
  gst_object_unref (bin->child_bus);
  bin->child_bus = NULL;
  gst_object_replace ((GstObject **) & bin->provided_clock, NULL);

  while (bin->children) {
    gst_bin_remove (bin, GST_ELEMENT_CAST (bin->children->data));
  }
  if (G_UNLIKELY (bin->children != NULL)) {
    g_critical ("could not remove elements from bin %s",
        GST_STR_NULL (GST_OBJECT_NAME (object)));
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/**
 * gst_bin_new:
 * @name: the name of the new bin
 *
 * Creates a new bin with the given name.
 *
 * Returns: a new #GstBin
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

  GST_OBJECT_LOCK (bin);
  for (children = bin->children; children; children = g_list_next (children)) {
    GstElement *child = GST_ELEMENT (children->data);

    gst_element_set_index (child, index);
  }
  GST_OBJECT_UNLOCK (bin);
}
#endif

/* set the clock on all elements in this bin
 *
 * MT safe
 */
static gboolean
gst_bin_set_clock_func (GstElement * element, GstClock * clock)
{
  GList *children;
  GstBin *bin;
  gboolean res = TRUE;

  bin = GST_BIN (element);

  GST_OBJECT_LOCK (bin);
  if (element->clock != clock) {
    for (children = bin->children; children; children = g_list_next (children)) {
      GstElement *child = GST_ELEMENT (children->data);

      res &= gst_element_set_clock (child, clock);
    }
  }
  GST_OBJECT_UNLOCK (bin);

  return res;
}

/* get the clock for this bin by asking all of the children in this bin
 *
 * The ref of the returned clock in increased so unref after usage.
 *
 * We loop the elements in state order and pick the last clock we can 
 * get. This makes sure we get a clock from the source.
 *
 * MT safe
 */
static GstClock *
gst_bin_provide_clock_func (GstElement * element)
{
  GstClock *result = NULL;
  GstElement *provider = NULL;
  GstBin *bin;
  GstIterator *it;
  gpointer val;

  bin = GST_BIN (element);

  GST_OBJECT_LOCK (bin);
  if (!bin->clock_dirty)
    goto not_dirty;

  GST_DEBUG_OBJECT (bin, "finding new clock");

  it = gst_bin_sort_iterator_new (bin);

  while (it->next (it, &val) == GST_ITERATOR_OK) {
    GstElement *child = GST_ELEMENT_CAST (val);
    GstClock *clock;

    clock = gst_element_provide_clock (child);
    if (clock) {
      GST_DEBUG_OBJECT (bin, "found candidate clock %p by element %s",
          clock, GST_ELEMENT_NAME (child));
      if (result) {
        gst_object_unref (result);
        gst_object_unref (provider);
      }
      result = clock;
      provider = child;
    } else {
      gst_object_unref (child);
    }
  }
  gst_object_replace ((GstObject **) & bin->provided_clock,
      (GstObject *) result);
  gst_object_replace ((GstObject **) & bin->clock_provider,
      (GstObject *) provider);
  bin->clock_dirty = FALSE;
  GST_DEBUG_OBJECT (bin, "provided new clock %p", result);
  GST_OBJECT_UNLOCK (bin);

  gst_iterator_free (it);

  return result;

not_dirty:
  {
    if ((result = bin->provided_clock))
      gst_object_ref (result);
    GST_DEBUG_OBJECT (bin, "returning old clock %p", result);
    GST_OBJECT_UNLOCK (bin);

    return result;
  }
}

/*
 * functions for manipulating cached messages
 */
typedef struct
{
  GstObject *src;
  GstMessageType types;
} MessageFind;

/* check if a message is of given src and type */
static gint
message_check (GstMessage * message, MessageFind * target)
{
  gboolean eq = TRUE;

  if (target->src)
    eq &= GST_MESSAGE_SRC (message) == target->src;
  if (target->types)
    eq &= (GST_MESSAGE_TYPE (message) & target->types) != 0;

  return (eq ? 0 : 1);
}

/* with LOCK, returns TRUE if message had a valid SRC, takes ref on
 * the message. 
 *
 * A message that is cached and has the same SRC and type is replaced
 * by the given message.
 */
static gboolean
bin_replace_message (GstBin * bin, GstMessage * message, GstMessageType types)
{
  GList *previous;
  GstObject *src;
  gboolean res = TRUE;
  const gchar *name;

  name = gst_message_type_get_name (GST_MESSAGE_TYPE (message));

  if ((src = GST_MESSAGE_SRC (message))) {
    MessageFind find;

    find.src = src;
    find.types = types;

    /* first find the previous message posted by this element */
    previous = g_list_find_custom (bin->messages, &find,
        (GCompareFunc) message_check);
    if (previous) {
      /* if we found a previous message, replace it */
      gst_message_unref (previous->data);
      previous->data = message;

      GST_DEBUG_OBJECT (bin, "replace old message %s from %s",
          name, GST_ELEMENT_NAME (src));
    } else {
      /* keep new message */
      bin->messages = g_list_prepend (bin->messages, message);

      GST_DEBUG_OBJECT (bin, "got new message %s from %s",
          name, GST_ELEMENT_NAME (src));
    }
  } else {
    GST_DEBUG_OBJECT (bin, "got message %s from (NULL), not processing", name);
    res = FALSE;
    gst_message_unref (message);
  }
  return res;
}

/* with LOCK. Remove all messages of given types */
static void
bin_remove_messages (GstBin * bin, GstObject * src, GstMessageType types)
{
  MessageFind find;
  GList *walk, *next;

  find.src = src;
  find.types = types;

  for (walk = bin->messages; walk; walk = next) {
    GstMessage *message = (GstMessage *) walk->data;

    next = g_list_next (walk);

    if (message_check (message, &find) == 0) {
      GST_DEBUG_OBJECT (GST_MESSAGE_SRC (message),
          "deleting message of types %d", types);
      bin->messages = g_list_delete_link (bin->messages, walk);
      gst_message_unref (message);
    } else {
      GST_DEBUG_OBJECT (GST_MESSAGE_SRC (message),
          "not deleting message of types %d", types);
    }
  }
}


/* Check if the bin is EOS. We do this by scanning all sinks and
 * checking if they posted an EOS message.
 *
 * call with bin LOCK */
static gboolean
is_eos (GstBin * bin)
{
  gboolean result;
  GList *walk;

  result = TRUE;
  for (walk = bin->children; walk; walk = g_list_next (walk)) {
    GstElement *element;

    element = GST_ELEMENT_CAST (walk->data);
    if (bin_element_is_sink (element, bin) == 0) {
      MessageFind find;

      /* check if element posted EOS */
      find.src = GST_OBJECT_CAST (element);
      find.types = GST_MESSAGE_EOS;

      if (g_list_find_custom (bin->messages, &find,
              (GCompareFunc) message_check)) {
        GST_DEBUG ("element posted EOS");
      } else {
        GST_DEBUG ("element did not post EOS yet");
        result = FALSE;
        break;
      }
    }
  }
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

/* vmethod that adds an element to a bin
 *
 * MT safe
 */
static gboolean
gst_bin_add_func (GstBin * bin, GstElement * element)
{
  gchar *elem_name;
  GstIterator *it;
  gboolean is_sink;
  GstMessage *clock_message = NULL;

  /* we obviously can't add ourself to ourself */
  if (G_UNLIKELY (GST_ELEMENT_CAST (element) == GST_ELEMENT_CAST (bin)))
    goto adding_itself;

  /* get the element name to make sure it is unique in this bin. */
  GST_OBJECT_LOCK (element);
  elem_name = g_strdup (GST_ELEMENT_NAME (element));
  is_sink = GST_OBJECT_FLAG_IS_SET (element, GST_ELEMENT_IS_SINK);
  GST_OBJECT_UNLOCK (element);

  GST_OBJECT_LOCK (bin);

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
  if (is_sink) {
    GST_CAT_DEBUG_OBJECT (GST_CAT_PARENTAGE, bin, "element \"%s\" was sink",
        elem_name);
    GST_OBJECT_FLAG_SET (bin, GST_ELEMENT_IS_SINK);
  }
  if (gst_element_provides_clock (element)) {
    GST_DEBUG_OBJECT (bin, "element \"%s\" can provide a clock", elem_name);
    bin->clock_dirty = TRUE;
    clock_message =
        gst_message_new_clock_provide (GST_OBJECT_CAST (bin), NULL, TRUE);
  }

  bin->children = g_list_prepend (bin->children, element);
  bin->numchildren++;
  bin->children_cookie++;

  /* distribute the bus */
  gst_element_set_bus (element, bin->child_bus);

  /* propagate the current base time and clock */
  gst_element_set_base_time (element, GST_ELEMENT (bin)->base_time);
  /* it's possible that the element did not accept the clock but
   * that is not important right now. When the pipeline goes to PLAYING,
   * a new clock will be selected */
  gst_element_set_clock (element, GST_ELEMENT_CLOCK (bin));
  bin->state_dirty = TRUE;
  GST_OBJECT_UNLOCK (bin);

  if (clock_message) {
    gst_element_post_message (GST_ELEMENT_CAST (bin), clock_message);
  }

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
    GST_OBJECT_LOCK (bin);
    g_warning ("Cannot add bin %s to itself", GST_ELEMENT_NAME (bin));
    GST_OBJECT_UNLOCK (bin);
    return FALSE;
  }
duplicate_name:
  {
    g_warning ("Name %s is not unique in bin %s, not adding",
        elem_name, GST_ELEMENT_NAME (bin));
    GST_OBJECT_UNLOCK (bin);
    g_free (elem_name);
    return FALSE;
  }
had_parent:
  {
    g_warning ("Element %s already has parent", elem_name);
    GST_OBJECT_UNLOCK (bin);
    g_free (elem_name);
    return FALSE;
  }
}


/**
 * gst_bin_add:
 * @bin: a #GstBin
 * @element: the #GstElement to add
 *
 * Adds the given element to the bin.  Sets the element's parent, and thus
 * takes ownership of the element. An element can only be added to one bin.
 *
 * If the element's pads are linked to other pads, the pads will be unlinked
 * before the element is added to the bin.
 *
 * MT safe.
 *
 * Returns: TRUE if the element could be added, FALSE if
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
  gboolean is_sink;
  GstMessage *clock_message = NULL;

  GST_OBJECT_LOCK (element);
  /* Check if the element is already being removed and immediately
   * return */
  if (G_UNLIKELY (GST_OBJECT_FLAG_IS_SET (element, GST_ELEMENT_UNPARENTING)))
    goto already_removing;

  GST_OBJECT_FLAG_SET (element, GST_ELEMENT_UNPARENTING);
  /* grab element name so we can print it */
  elem_name = g_strdup (GST_ELEMENT_NAME (element));
  is_sink = GST_OBJECT_FLAG_IS_SET (element, GST_ELEMENT_IS_SINK);
  GST_OBJECT_UNLOCK (element);

  /* unlink all linked pads */
  it = gst_element_iterate_pads (element);
  gst_iterator_foreach (it, (GFunc) unlink_pads, element);
  gst_iterator_free (it);

  GST_OBJECT_LOCK (bin);
  /* the element must be in the bin's list of children */
  if (G_UNLIKELY (g_list_find (bin->children, element) == NULL))
    goto not_in_bin;

  /* now remove the element from the list of elements */
  bin->children = g_list_remove (bin->children, element);
  bin->numchildren--;
  bin->children_cookie++;

  /* check if we removed a sink */
  if (is_sink) {
    GList *other_sink;

    /* check if we removed the last sink */
    other_sink = g_list_find_custom (bin->children,
        bin, (GCompareFunc) bin_element_is_sink);
    if (!other_sink) {
      /* yups, we're not a sink anymore */
      GST_OBJECT_FLAG_UNSET (bin, GST_ELEMENT_IS_SINK);
    }
  }
  /* if the clock provider for this element is removed, we lost
   * the clock as well, we need to inform the parent of this
   * so that it can select a new clock */
  if (bin->clock_provider == element) {
    GST_DEBUG_OBJECT (bin, "element \"%s\" provided the clock", elem_name);
    bin->clock_dirty = TRUE;
    clock_message =
        gst_message_new_clock_lost (GST_OBJECT_CAST (bin), bin->provided_clock);
  }
  bin->state_dirty = TRUE;
  GST_OBJECT_UNLOCK (bin);

  if (clock_message) {
    gst_element_post_message (GST_ELEMENT_CAST (bin), clock_message);
  }

  GST_CAT_INFO_OBJECT (GST_CAT_PARENTAGE, bin, "removed child \"%s\"",
      elem_name);
  g_free (elem_name);

  gst_element_set_bus (element, NULL);

  /* we ref here because after the _unparent() the element can be disposed
   * and we still need it to reset the UNPARENTING flag and fire a signal. */
  gst_object_ref (element);
  gst_object_unparent (GST_OBJECT_CAST (element));

  GST_OBJECT_LOCK (element);
  GST_OBJECT_FLAG_UNSET (element, GST_ELEMENT_UNPARENTING);
  GST_OBJECT_UNLOCK (element);

  g_signal_emit (G_OBJECT (bin), gst_bin_signals[ELEMENT_REMOVED], 0, element);

  /* element is really out of our control now */
  gst_object_unref (element);

  return TRUE;

  /* ERROR handling */
not_in_bin:
  {
    g_warning ("Element %s is not in bin %s", elem_name,
        GST_ELEMENT_NAME (bin));
    GST_OBJECT_UNLOCK (bin);
    g_free (elem_name);
    return FALSE;
  }
already_removing:
  {
    GST_OBJECT_UNLOCK (element);
    return FALSE;
  }
}

/**
 * gst_bin_remove:
 * @bin: a #GstBin
 * @element: the #GstElement to remove
 *
 * Removes the element from the bin, unparenting it as well.
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
 * Returns: TRUE if the element could be removed, FALSE if
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
 * @bin: a #GstBin
 *
 * Gets an iterator for the elements in this bin.
 *
 * Each element yielded by the iterator will have its refcount increased, so
 * unref after use.
 *
 * MT safe.  Caller owns returned value.
 *
 * Returns: a #GstIterator of #GstElement, or NULL
 */
GstIterator *
gst_bin_iterate_elements (GstBin * bin)
{
  GstIterator *result;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  GST_OBJECT_LOCK (bin);
  /* add ref because the iterator refs the bin. When the iterator
   * is freed it will unref the bin again using the provided dispose
   * function. */
  gst_object_ref (bin);
  result = gst_iterator_new_list (GST_TYPE_ELEMENT,
      GST_OBJECT_GET_LOCK (bin),
      &bin->children_cookie,
      &bin->children,
      bin,
      (GstIteratorItemFunction) iterate_child,
      (GstIteratorDisposeFunction) gst_object_unref);
  GST_OBJECT_UNLOCK (bin);

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
 * @bin: a #GstBin
 *
 * Gets an iterator for the elements in this bin.
 * This iterator recurses into GstBin children.
 *
 * Each element yielded by the iterator will have its refcount increased, so
 * unref after use.
 *
 * MT safe.  Caller owns returned value.
 *
 * Returns: a #GstIterator of #GstElement, or NULL
 */
GstIterator *
gst_bin_iterate_recurse (GstBin * bin)
{
  GstIterator *result;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  GST_OBJECT_LOCK (bin);
  /* add ref because the iterator refs the bin. When the iterator
   * is freed it will unref the bin again using the provided dispose
   * function. */
  gst_object_ref (bin);
  result = gst_iterator_new_list (GST_TYPE_ELEMENT,
      GST_OBJECT_GET_LOCK (bin),
      &bin->children_cookie,
      &bin->children,
      bin,
      (GstIteratorItemFunction) iterate_child_recurse,
      (GstIteratorDisposeFunction) gst_object_unref);
  GST_OBJECT_UNLOCK (bin);

  return result;
}

/* returns 0 when TRUE because this is a GCompareFunc */
/* MT safe */
static gint
bin_element_is_sink (GstElement * child, GstBin * bin)
{
  gboolean is_sink;

  /* we lock the child here for the remainder of the function to
   * get its name and flag safely. */
  GST_OBJECT_LOCK (child);
  is_sink = GST_OBJECT_FLAG_IS_SET (child, GST_ELEMENT_IS_SINK);

  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, bin,
      "child %s %s sink", GST_OBJECT_NAME (child), is_sink ? "is" : "is not");

  GST_OBJECT_UNLOCK (child);
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
    gst_object_unref (child);
    return 1;
  }
}

/**
 * gst_bin_iterate_sinks:
 * @bin: a #GstBin
 *
 * Gets an iterator for all elements in the bin that have the
 * #GST_ELEMENT_IS_SINK flag set.
 *
 * Each element yielded by the iterator will have its refcount increased, so
 * unref after use.
 *
 * MT safe.  Caller owns returned value.
 *
 * Returns: a #GstIterator of #GstElement, or NULL
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

/*
 * MT safe
 */
static GstStateChangeReturn
gst_bin_get_state_func (GstElement * element, GstState * state,
    GstState * pending, GstClockTime timeout)
{
  GstBin *bin = GST_BIN (element);
  GstStateChangeReturn ret;

  GST_CAT_INFO_OBJECT (GST_CAT_STATES, bin, "getting state");

  /* do a non forced recalculation of the state */
  gst_bin_recalc_state (bin, FALSE);

  ret = parent_class->get_state (element, state, pending, timeout);

  return ret;
}

static void
gst_bin_recalc_state (GstBin * bin, gboolean force)
{
  GstStateChangeReturn ret;
  GList *children;
  guint32 children_cookie;
  guint32 state_cookie;
  gboolean have_no_preroll;
  gboolean have_async;

  ret = GST_STATE_CHANGE_SUCCESS;

  /* lock bin, no element can be added or removed while we have this lock */
  GST_OBJECT_LOCK (bin);
  /* forced recalc, make state dirty again */
  if (force)
    bin->state_dirty = TRUE;

  /* no point in scanning if nothing changed and it's no forced recalc */
  if (!bin->state_dirty)
    goto not_dirty;

  /* no point in having two scans run concurrently */
  if (bin->polling)
    goto was_polling;

  bin->polling = TRUE;
  state_cookie = GST_ELEMENT_CAST (bin)->state_cookie;

  GST_CAT_INFO_OBJECT (GST_CAT_STATES, bin, "recalc state");

restart:
  /* when we leave this function, the state must not be dirty, whenever
   * we are scanning and the state bemoces dirty again, we restart. */
  bin->state_dirty = FALSE;

  have_no_preroll = FALSE;
  have_async = FALSE;

  GST_CAT_INFO_OBJECT (GST_CAT_STATES, bin, "checking element states");

  /* scan all element states with a zero timeout so we don't block on
   * anything */
  children = bin->children;
  children_cookie = bin->children_cookie;
  while (children) {
    GstElement *child = GST_ELEMENT_CAST (children->data);

    gst_object_ref (child);
    /* now we release the lock to enter a non blocking wait. We
     * release the lock anyway since we can. */
    GST_OBJECT_UNLOCK (bin);

    ret = gst_element_get_state (child, NULL, NULL, 0);

    gst_object_unref (child);

    /* now grab the lock to iterate to the next child */
    GST_OBJECT_LOCK (bin);
    if (G_UNLIKELY (children_cookie != bin->children_cookie)) {
      /* child added/removed during state change, restart. We need
       * to restart with the quick check as a no-preroll element could
       * have been added here and we don't want to block on sinks then.*/
      GST_DEBUG_OBJECT (bin, "children added or removed, restarting recalc");
      goto restart;
    }
    if (state_cookie != GST_ELEMENT_CAST (bin)->state_cookie) {
      GST_DEBUG_OBJECT (bin, "concurrent state change");
      goto concurrent_state;
    }
    if (bin->state_dirty) {
      GST_DEBUG_OBJECT (bin, "state dirty again, restarting recalc");
      goto restart;
    }

    switch (ret) {
      case GST_STATE_CHANGE_FAILURE:
        /* report FAILURE  immediatly */
        goto done;
      case GST_STATE_CHANGE_NO_PREROLL:
        /* we have to continue scanning as there might be
         * ERRORS too */
        have_no_preroll = TRUE;
        break;
      case GST_STATE_CHANGE_ASYNC:
        /* we have to continue scanning as there might be
         * ERRORS too */
        have_async = TRUE;
        break;
      default:
        break;
    }
    children = g_list_next (children);
  }
  /* if we get here, we have no FAILURES */

  /* if we have NO_PREROLL, return that */
  if (have_no_preroll) {
    ret = GST_STATE_CHANGE_NO_PREROLL;
  }
  /* else return ASYNC if async elements where found. */
  else if (have_async) {
    ret = GST_STATE_CHANGE_ASYNC;
  }

done:
  bin->polling = FALSE;
  GST_OBJECT_UNLOCK (bin);

  /* now we can take the state lock, it is possible that new elements
   * are added now and we still report the old state. No problem though as
   * the return is still consistent, the effect is as if the element was
   * added after this function completed. */
  switch (ret) {
    case GST_STATE_CHANGE_SUCCESS:
    case GST_STATE_CHANGE_NO_PREROLL:
      ret = gst_element_continue_state (GST_ELEMENT_CAST (bin), ret);
      break;
    case GST_STATE_CHANGE_ASYNC:
      gst_element_lost_state (GST_ELEMENT_CAST (bin));
      break;
    case GST_STATE_CHANGE_FAILURE:
      gst_element_abort_state (GST_ELEMENT_CAST (bin));
      break;
    default:
      goto unknown_state;
  }

  GST_CAT_INFO_OBJECT (GST_CAT_STATES, bin, "bin RETURN is now %d", ret);

  return;

not_dirty:
  {
    GST_CAT_INFO_OBJECT (GST_CAT_STATES, bin, "not dirty");
    GST_OBJECT_UNLOCK (bin);
    return;
  }
was_polling:
  {
    GST_CAT_INFO_OBJECT (GST_CAT_STATES, bin, "was polling");
    GST_OBJECT_UNLOCK (bin);
    return;
  }
concurrent_state:
  {
    GST_CAT_INFO_OBJECT (GST_CAT_STATES, bin, "concurrent_state");
    bin->polling = FALSE;
    GST_OBJECT_UNLOCK (bin);
    return;
  }
unknown_state:
  {
    /* somebody added a GST_STATE_ and forgot to do stuff here ! */
    GST_CAT_INFO_OBJECT (GST_CAT_STATES, bin,
        "unknown return value %d from a state change function", ret);
    g_critical ("unknown return value %d from a state change function", ret);
    GST_STATE_RETURN (bin) = GST_STATE_CHANGE_FAILURE;
    GST_STATE_UNLOCK (bin);
    return;
  }
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
  GHashTable *hash;             /* hashtable with element dependencies */
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
  GST_DEBUG_OBJECT (bit->bin, "%s add to queue", GST_ELEMENT_NAME (element));
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
  gboolean is_sink;

  /* sinks are added right away */
  GST_OBJECT_LOCK (element);
  is_sink = GST_OBJECT_FLAG_IS_SET (element, GST_ELEMENT_IS_SINK);
  GST_OBJECT_UNLOCK (element);

  if (is_sink) {
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

  GST_OBJECT_LOCK (element);
  /* don't touch degree if element has no sourcepads */
  if (element->numsinkpads != 0) {
    /* loop over all sinkpads, decrement degree for all connected
     * elements in this bin */
    GList *pads;

    for (pads = element->sinkpads; pads; pads = g_list_next (pads)) {
      GstPad *peer;

      if ((peer = gst_pad_get_peer (GST_PAD_CAST (pads->data)))) {
        GstElement *peer_element;

        if ((peer_element = gst_pad_get_parent_element (peer))) {
          GST_OBJECT_LOCK (peer_element);
          /* check that we don't go outside of this bin */
          if (GST_OBJECT_CAST (peer_element)->parent ==
              GST_OBJECT_CAST (bit->bin)) {
            gint old_deg, new_deg;

            old_deg = HASH_GET_DEGREE (bit, peer_element);
            new_deg = old_deg + bit->mode;

            GST_DEBUG_OBJECT (bit->bin,
                "change element %s, degree %d->%d, linked to %s",
                GST_ELEMENT_NAME (peer_element), old_deg, new_deg,
                GST_ELEMENT_NAME (element));

            /* update degree */
            if (new_deg == 0) {
              /* degree hit 0, add to queue */
              add_to_queue (bit, peer_element);
            } else {
              HASH_SET_DEGREE (bit, peer_element, new_deg);
            }
            linked = TRUE;
          }
          GST_OBJECT_UNLOCK (peer_element);
          gst_object_unref (peer_element);
        }
        gst_object_unref (peer);
      }
    }
  }
  if (!linked) {
    GST_DEBUG_OBJECT (bit->bin, "element %s not linked on any sinkpads",
        GST_ELEMENT_NAME (element));
  }
  GST_OBJECT_UNLOCK (element);
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
  GstBin *bin = bit->bin;

  /* empty queue, we have to find a next best element */
  if (g_queue_is_empty (bit->queue)) {
    GstElement *best;

    bit->best = NULL;
    bit->best_deg = G_MAXINT;
    g_list_foreach (bin->children, (GFunc) find_element, bit);
    if ((best = bit->best)) {
      if (bit->best_deg != 0) {
        /* we don't fail on this one yet */
        g_warning ("loop detected in the graph !!");
      }
      /* best unhandled element, schedule as next element */
      GST_DEBUG_OBJECT (bin, "queue empty, next best: %s",
          GST_ELEMENT_NAME (best));
      gst_object_ref (best);
      HASH_SET_DEGREE (bit, best, -1);
      *result = best;
    } else {
      GST_DEBUG_OBJECT (bin, "queue empty, elements exhausted");
      /* no more unhandled elements, we are done */
      return GST_ITERATOR_DONE;
    }
  } else {
    /* everything added to the queue got reffed */
    *result = g_queue_pop_head (bit->queue);
  }

  GST_DEBUG_OBJECT (bin, "queue head gives %s", GST_ELEMENT_NAME (*result));
  /* update degrees of linked elements */
  update_degree (GST_ELEMENT_CAST (*result), bit);

  return GST_ITERATOR_OK;
}

/* clear queues, recalculate the degrees and restart. */
static void
gst_bin_sort_iterator_resync (GstBinSortIterator * bit)
{
  GstBin *bin = bit->bin;

  GST_DEBUG_OBJECT (bin, "resync");
  clear_queue (bit->queue);
  /* reset degrees */
  g_list_foreach (bin->children, (GFunc) reset_degree, bit);
  /* calc degrees, incrementing */
  bit->mode = 1;
  g_list_foreach (bin->children, (GFunc) update_degree, bit);
  /* for the rest of the function we decrement the degrees */
  bit->mode = -1;
}

/* clear queues, unref bin and free iterator. */
static void
gst_bin_sort_iterator_free (GstBinSortIterator * bit)
{
  GstBin *bin = bit->bin;

  GST_DEBUG_OBJECT (bin, "free");
  clear_queue (bit->queue);
  g_queue_free (bit->queue);
  g_hash_table_destroy (bit->hash);
  gst_object_unref (bin);
  g_free (bit);
}

/* should be called with the bin LOCK held */
static GstIterator *
gst_bin_sort_iterator_new (GstBin * bin)
{
  GstBinSortIterator *result;

  /* we don't need an ItemFunction because we ref the items in the _next
   * method already */
  result = (GstBinSortIterator *)
      gst_iterator_new (sizeof (GstBinSortIterator),
      GST_TYPE_ELEMENT,
      GST_OBJECT_GET_LOCK (bin),
      &bin->children_cookie,
      (GstIteratorNextFunction) gst_bin_sort_iterator_next,
      (GstIteratorItemFunction) NULL,
      (GstIteratorResyncFunction) gst_bin_sort_iterator_resync,
      (GstIteratorFreeFunction) gst_bin_sort_iterator_free);
  result->queue = g_queue_new ();
  result->hash = g_hash_table_new (NULL, NULL);
  gst_object_ref (bin);
  result->bin = bin;
  gst_bin_sort_iterator_resync (result);

  return (GstIterator *) result;
}

/**
 * gst_bin_iterate_sorted:
 * @bin: a #GstBin
 *
 * Gets an iterator for the elements in this bin in topologically
 * sorted order. This means that the elements are returned from
 * the most downstream elements (sinks) to the sources.
 *
 * This function is used internally to perform the state changes
 * of the bin elements.
 *
 * Each element yielded by the iterator will have its refcount increased, so
 * unref after use.
 *
 * MT safe.  Caller owns returned value.
 *
 * Returns: a #GstIterator of #GstElement, or NULL
 */
GstIterator *
gst_bin_iterate_sorted (GstBin * bin)
{
  GstIterator *result;

  g_return_val_if_fail (GST_IS_BIN (bin), NULL);

  GST_OBJECT_LOCK (bin);
  result = gst_bin_sort_iterator_new (bin);
  GST_OBJECT_UNLOCK (bin);

  return result;
}

static GstStateChangeReturn
gst_bin_element_set_state (GstBin * bin, GstElement * element, GstState pending)
{
  GstStateChangeReturn ret;
  gboolean locked;

  /* peel off the locked flag */
  GST_OBJECT_LOCK (element);
  locked = GST_OBJECT_FLAG_IS_SET (element, GST_ELEMENT_LOCKED_STATE);
  GST_OBJECT_UNLOCK (element);

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
gst_bin_change_state_func (GstElement * element, GstStateChange transition)
{
  GstBin *bin;
  GstStateChangeReturn ret;
  GstState current, next;
  gboolean have_async;
  gboolean have_no_preroll;
  GstClockTime base_time;
  GstIterator *it;
  gboolean done;

  /* we don't need to take the STATE_LOCK, it is already taken */
  current = GST_STATE_TRANSITION_CURRENT (transition);
  next = GST_STATE_TRANSITION_NEXT (transition);

  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
      "changing state of children from %s to %s",
      gst_element_state_get_name (current), gst_element_state_get_name (next));

  bin = GST_BIN_CAST (element);

  switch (next) {
    case GST_STATE_PAUSED:
      /* Clear EOS list on next PAUSED */
      GST_OBJECT_LOCK (bin);
      GST_DEBUG_OBJECT (element, "clearing EOS elements");
      bin_remove_messages (bin, NULL, GST_MESSAGE_EOS);
      GST_OBJECT_UNLOCK (bin);
      break;
    case GST_STATE_READY:
      /* Clear message list on next READY */
      GST_OBJECT_LOCK (bin);
      GST_DEBUG_OBJECT (element, "clearing all cached messages");
      bin_remove_messages (bin, NULL, GST_MESSAGE_ANY);
      GST_OBJECT_UNLOCK (bin);
      break;
    default:
      break;
  }

  /* iterate in state change order */
  it = gst_bin_iterate_sorted (bin);

restart:
  /* take base time */
  base_time = gst_element_get_base_time (element);

  have_async = FALSE;
  have_no_preroll = FALSE;

  done = FALSE;
  while (!done) {
    gpointer data;

    switch (gst_iterator_next (it, &data)) {
      case GST_ITERATOR_OK:
      {
        GstElement *child;

        child = GST_ELEMENT_CAST (data);

        /* set base time on child */
        gst_element_set_base_time (child, base_time);

        /* set state now */
        ret = gst_bin_element_set_state (bin, child, next);

        switch (ret) {
          case GST_STATE_CHANGE_SUCCESS:
            GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
                "child '%s' changed state to %d(%s) successfully",
                GST_ELEMENT_NAME (child), next,
                gst_element_state_get_name (next));
            break;
          case GST_STATE_CHANGE_ASYNC:
            GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
                "child '%s' is changing state asynchronously",
                GST_ELEMENT_NAME (child));
            have_async = TRUE;
            break;
          case GST_STATE_CHANGE_FAILURE:
            GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
                "child '%s' failed to go to state %d(%s)",
                GST_ELEMENT_NAME (child),
                next, gst_element_state_get_name (next));
            gst_object_unref (child);
            goto done;
          case GST_STATE_CHANGE_NO_PREROLL:
            GST_CAT_INFO_OBJECT (GST_CAT_STATES, element,
                "child '%s' changed state to %d(%s) successfully without preroll",
                GST_ELEMENT_NAME (child), next,
                gst_element_state_get_name (next));
            have_no_preroll = TRUE;
            break;
          default:
            g_assert_not_reached ();
            break;
        }
        gst_object_unref (child);
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

  ret = parent_class->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  if (have_no_preroll) {
    ret = GST_STATE_CHANGE_NO_PREROLL;
  } else if (have_async) {
    ret = GST_STATE_CHANGE_ASYNC;
  }

done:
  gst_iterator_free (it);

  GST_CAT_DEBUG_OBJECT (GST_CAT_STATES, element,
      "done changing bin's state from %s to %s, now in %s, ret %d",
      gst_element_state_get_name (current),
      gst_element_state_get_name (next),
      gst_element_state_get_name (GST_STATE (element)), ret);

  return ret;
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

static void
gst_bin_recalc_func (GstBin * bin, gpointer data)
{
  GST_DEBUG_OBJECT (bin, "doing state recalc");
  GST_STATE_LOCK (bin);
  gst_bin_recalc_state (bin, FALSE);
  GST_STATE_UNLOCK (bin);
  GST_DEBUG_OBJECT (bin, "state recalc done");
  gst_object_unref (bin);
}

/* handle child messages:
 *
 * GST_MESSAGE_EOS: This message is only posted by sinks
 *     in the PLAYING state. If all sinks posted the EOS message, post
 *     one upwards.
 *
 * GST_MESSAGE_STATE_DIRTY: if we are the toplevel bin we do a state
 *     recalc. If we are not toplevel (we have a parent) we just post
 *     the message upwards.
 *
 * GST_MESSAGE_SEGMENT_START: just collect, never forward upwards. If an
 *     element posts segment_start twice, only the last message is kept.
 *
 * GST_MESSAGE_SEGMENT_DONE: replace SEGMENT_START message from same poster
 *     with the segment_done message. If there are no more segment_start
 *     messages, post segment_done message upwards.
 *
 * GST_MESSAGE_DURATION: remove all previously cached duration messages.
 *     Whenever someone performs a duration query on the bin, we store the
 *     result so we can answer it quicker the next time. Any element that
 *     changes its duration marks our cached values invalid.
 *     This message is also posted upwards.
 *
 * OTHER: post upwards.
 */
static GstBusSyncReply
bin_bus_handler (GstBus * bus, GstMessage * message, GstBin * bin)
{

  GstBinClass *bclass;

  bclass = GST_BIN_GET_CLASS (bin);
  if (bclass->handle_message)
    bclass->handle_message (bin, message);
  else
    gst_message_unref (message);

  return GST_BUS_DROP;
}

static void
gst_bin_handle_message_func (GstBin * bin, GstMessage * message)
{
  GST_DEBUG_OBJECT (bin, "[msg %p] handling child message of type %s",
      message, gst_message_type_get_name (GST_MESSAGE_TYPE (message)));

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
    {
      gboolean eos;

      /* collect all eos messages from the children */
      GST_OBJECT_LOCK (bin);
      bin_replace_message (bin, message, GST_MESSAGE_EOS);
      eos = is_eos (bin);
      GST_OBJECT_UNLOCK (bin);

      /* if we are completely EOS, we forward an EOS message */
      if (eos) {
        GST_DEBUG_OBJECT (bin, "all sinks posted EOS");
        gst_element_post_message (GST_ELEMENT_CAST (bin),
            gst_message_new_eos (GST_OBJECT_CAST (bin)));
      }
      break;
    }
    case GST_MESSAGE_STATE_DIRTY:
    {
      GstObject *src;
      GstBinClass *klass;

      src = GST_MESSAGE_SRC (message);

      GST_DEBUG_OBJECT (bin, "%s gave state dirty", GST_ELEMENT_NAME (src));

      /* mark the bin dirty */
      GST_OBJECT_LOCK (bin);
      GST_DEBUG_OBJECT (bin, "marking dirty");
      bin->state_dirty = TRUE;

      if (GST_OBJECT_PARENT (bin))
        goto not_toplevel;

      /* free message */
      gst_message_unref (message);

      klass = GST_BIN_GET_CLASS (bin);
      gst_object_ref (bin);
      GST_DEBUG_OBJECT (bin, "pushing recalc on thread pool");
      g_thread_pool_push (klass->pool, bin, NULL);
      GST_OBJECT_UNLOCK (bin);
      break;

      /* non toplevel bins just forward the message and don't start
       * a recalc themselves */
    not_toplevel:
      {
        GST_OBJECT_UNLOCK (bin);
        GST_DEBUG_OBJECT (bin, "not toplevel");

        /* post message up, mark parent bins dirty */
        gst_element_post_message (GST_ELEMENT_CAST (bin), message);

        break;
      }
    }
    case GST_MESSAGE_SEGMENT_START:
      GST_OBJECT_LOCK (bin);
      /* replace any previous segment_start message from this source 
       * with the new segment start message */
      bin_replace_message (bin, message, GST_MESSAGE_SEGMENT_START);
      GST_OBJECT_UNLOCK (bin);
      break;
    case GST_MESSAGE_SEGMENT_DONE:
    {
      MessageFind find;
      gboolean post = FALSE;
      GstFormat format;
      gint64 position;

      gst_message_parse_segment_done (message, &format, &position);

      GST_OBJECT_LOCK (bin);
      bin_replace_message (bin, message, GST_MESSAGE_SEGMENT_START);
      /* if there are no more segment_start messages, everybody posted
       * a segment_done and we can post one on the bus. */

      /* we don't care who still has a pending segment start */
      find.src = NULL;
      find.types = GST_MESSAGE_SEGMENT_START;

      if (!g_list_find_custom (bin->messages, &find,
              (GCompareFunc) message_check)) {
        /* nothing found */
        post = TRUE;
        /* remove all old segment_done messages */
        bin_remove_messages (bin, NULL, GST_MESSAGE_SEGMENT_DONE);
      }
      GST_OBJECT_UNLOCK (bin);
      if (post) {
        /* post segment done with latest format and position. */
        gst_element_post_message (GST_ELEMENT_CAST (bin),
            gst_message_new_segment_done (GST_OBJECT_CAST (bin),
                format, position));
      }
      break;
    }
    case GST_MESSAGE_DURATION:
    {
      /* remove all cached duration messages, next time somebody asks
       * for duration, we will recalculate. */
      GST_OBJECT_LOCK (bin);
      bin_remove_messages (bin, NULL, GST_MESSAGE_DURATION);
      GST_OBJECT_UNLOCK (bin);
      goto forward;
    }
    case GST_MESSAGE_CLOCK_LOST:
    {
      gboolean playing, provided, forward;
      GstClock *clock;

      gst_message_parse_clock_lost (message, &clock);

      GST_OBJECT_LOCK (bin);
      bin->clock_dirty = TRUE;
      /* if we lost the clock that we provided, post to parent but 
       * only if we are PLAYING. */
      provided = (clock == bin->provided_clock);
      playing = (GST_STATE (bin) == GST_STATE_PLAYING);
      forward = playing & provided;
      GST_DEBUG_OBJECT (bin, "provided %d, playing %d, forward %d",
          provided, playing, forward);
      GST_OBJECT_UNLOCK (bin);

      if (forward) {
        goto forward;
      }
      /* free message */
      gst_message_unref (message);

      break;
    }
    case GST_MESSAGE_CLOCK_PROVIDE:
    {
      gboolean forward;

      GST_OBJECT_LOCK (bin);
      bin->clock_dirty = TRUE;
      /* a new clock is available, post to parent but not
       * to the application */
      forward = GST_OBJECT_PARENT (bin) != NULL;
      GST_OBJECT_UNLOCK (bin);

      if (forward)
        goto forward;
      /* free message */
      gst_message_unref (message);

      break;
    }
    default:
      goto forward;
  }

  return;

forward:
  {
    /* Send all other messages upward */
    GST_DEBUG_OBJECT (bin, "posting message upward");
    gst_element_post_message (GST_ELEMENT_CAST (bin), message);
    return;
  }
}

/* generic struct passed to all query fold methods */
typedef struct
{
  GstQuery *query;
  gint64 max;
} QueryFold;

typedef void (*QueryInitFunction) (GstBin * bin, QueryFold * fold);
typedef void (*QueryDoneFunction) (GstBin * bin, QueryFold * fold);

/* for duration we collect all durations and take the MAX of
 * all valid results */
static void
bin_query_duration_init (GstBin * bin, QueryFold * fold)
{
  fold->max = -1;
}

static gboolean
bin_query_duration_fold (GstElement * item, GValue * ret, QueryFold * fold)
{
  if (gst_element_query (item, fold->query)) {
    gint64 duration;

    g_value_set_boolean (ret, TRUE);

    gst_query_parse_duration (fold->query, NULL, &duration);

    GST_DEBUG_OBJECT (item, "got duration %" G_GINT64_FORMAT, duration);

    if (duration > fold->max)
      fold->max = duration;
  }
  return TRUE;
}
static void
bin_query_duration_done (GstBin * bin, QueryFold * fold)
{
  GstFormat format;

  gst_query_parse_duration (fold->query, &format, NULL);
  /* store max in query result */
  gst_query_set_duration (fold->query, format, fold->max);

  GST_DEBUG_OBJECT (bin, "max duration %" G_GINT64_FORMAT, fold->max);

  /* and cache now */
  GST_OBJECT_LOCK (bin);
  bin->messages = g_list_prepend (bin->messages,
      gst_message_new_duration (GST_OBJECT_CAST (bin), format, fold->max));
  GST_OBJECT_UNLOCK (bin);
}

/* generic fold, return first valid result */
static gboolean
bin_query_generic_fold (GstElement * item, GValue * ret, QueryFold * fold)
{
  gboolean res;

  if ((res = gst_element_query (item, fold->query))) {
    g_value_set_boolean (ret, TRUE);
    GST_DEBUG_OBJECT (item, "answered query %p", fold->query);
  }

  /* and stop as soon as we have a valid result */
  return !res;
}

static gboolean
gst_bin_query (GstElement * element, GstQuery * query)
{
  GstBin *bin = GST_BIN (element);
  GstIterator *iter;
  gboolean res = FALSE;
  GstIteratorFoldFunction fold_func;
  QueryInitFunction fold_init = NULL;
  QueryDoneFunction fold_done = NULL;
  QueryFold fold_data;
  GValue ret = { 0 };

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GList *cached;
      GstFormat qformat;

      gst_query_parse_duration (query, &qformat, NULL);

      /* find cached duration query */
      GST_OBJECT_LOCK (bin);
      for (cached = bin->messages; cached; cached = g_list_next (cached)) {
        GstMessage *message = (GstMessage *) cached->data;

        if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_DURATION &&
            GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (bin)) {
          GstFormat format;
          gint64 duration;

          gst_message_parse_duration (message, &format, &duration);

          /* if cached same format, copy duration in query result */
          if (format == qformat) {
            GST_DEBUG_OBJECT (bin, "return cached duration %" G_GINT64_FORMAT,
                duration);
            GST_OBJECT_UNLOCK (bin);

            gst_query_set_duration (query, qformat, duration);
            res = TRUE;
            goto exit;
          }
        }
      }
      GST_OBJECT_UNLOCK (bin);

      fold_func = (GstIteratorFoldFunction) bin_query_duration_fold;
      fold_init = bin_query_duration_init;
      fold_done = bin_query_duration_done;
      break;
    }
    default:
      fold_func = (GstIteratorFoldFunction) bin_query_generic_fold;
      break;
  }

  fold_data.query = query;

  g_value_init (&ret, G_TYPE_BOOLEAN);
  g_value_set_boolean (&ret, FALSE);

  iter = gst_bin_iterate_sinks (bin);
  GST_DEBUG_OBJECT (bin, "Sending query %p (type %d) to sink children",
      query, GST_QUERY_TYPE (query));

  if (fold_init)
    fold_init (bin, &fold_data);

  while (TRUE) {
    GstIteratorResult ires;

    ires = gst_iterator_fold (iter, fold_func, &ret, &fold_data);

    switch (ires) {
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        if (fold_init)
          fold_init (bin, &fold_data);
        g_value_set_boolean (&ret, FALSE);
        break;
      case GST_ITERATOR_OK:
      case GST_ITERATOR_DONE:
        res = g_value_get_boolean (&ret);
        if (fold_done != NULL && res)
          fold_done (bin, &fold_data);
        goto done;
      default:
        res = FALSE;
        goto done;
    }
  }
done:
  gst_iterator_free (iter);

exit:
  GST_DEBUG_OBJECT (bin, "query %p result %d", query, res);

  return res;
}

static gint
compare_name (GstElement * element, const gchar * name)
{
  gint eq;

  GST_OBJECT_LOCK (element);
  eq = strcmp (GST_ELEMENT_NAME (element), name);
  GST_OBJECT_UNLOCK (element);

  if (eq != 0) {
    gst_object_unref (element);
  }
  return eq;
}

/**
 * gst_bin_get_by_name:
 * @bin: a #GstBin
 * @name: the element name to search for
 *
 * Gets the element with the given name from a bin. This
 * function recurses into child bins.
 *
 * Returns NULL if no element with the given name is found in the bin.
 *
 * MT safe.  Caller owns returned reference.
 *
 * Returns: the #GstElement with the given name, or NULL
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
 * @bin: a #GstBin
 * @name: the element name to search for
 *
 * Gets the element with the given name from this bin. If the
 * element is not found, a recursion is performed on the parent bin.
 *
 * Returns NULL if:
 * - no element with the given name is found in the bin
 *
 * MT safe.  Caller owns returned reference.
 *
 * Returns: the #GstElement with the given name, or NULL
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
 * @bin: a #GstBin
 * @interface: the #GType of an interface
 *
 * Looks for an element inside the bin that implements the given
 * interface. If such an element is found, it returns the element.
 * You can cast this element to the given interface afterwards.  If you want
 * all elements that implement the interface, use
 * gst_bin_iterate_all_by_interface(). This function recurses into child bins.
 *
 * MT safe.  Caller owns returned reference.
 *
 * Returns: A #GstElement inside the bin implementing the interface
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
 * @bin: a #GstBin
 * @interface: the #GType of an interface
 *
 * Looks for all elements inside the bin that implements the given
 * interface. You can safely cast all returned elements to the given interface.
 * The function recurses inside child bins. The iterator will yield a series
 * of #GstElement that should be unreffed after use.
 *
 * Each element yielded by the iterator will have its refcount increased, so
 * unref after use.
 *
 * MT safe.  Caller owns returned value.
 *
 * Returns: a #GstIterator of #GstElement for all elements in the bin
 *          implementing the given interface, or NULL
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
