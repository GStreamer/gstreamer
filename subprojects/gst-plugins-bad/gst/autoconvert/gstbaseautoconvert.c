/* GStreamer
 *
 *  Copyright 2007-2012 Collabora Ltd
 *   @author: Olivier Crete <olivier.crete@collabora.com>
 *  Copyright 2007-2008 Nokia
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * GstBaseAutoConvert:
 *
 * Since: 1.24
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstbaseautoconvert.h"

#include <string.h>

GST_DEBUG_CATEGORY (baseautoconvert_debug);
#define GST_CAT_DEFAULT (baseautoconvert_debug)

#define GST_BASEAUTOCONVERT_LOCK(ac) GST_OBJECT_LOCK (ac)
#define GST_BASEAUTOCONVERT_UNLOCK(ac) GST_OBJECT_UNLOCK (ac)

#define SUPRESS_UNUSED_WARNING(a) (void)a

/* elementfactory information */
static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* GstBaseAutoConvert signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

static void gst_base_auto_convert_dispose (GObject * object);
static void gst_base_auto_convert_finalize (GObject * object);

static GstElement *gst_base_auto_convert_get_subelement (GstBaseAutoConvert *
    self);
static GstPad *gst_base_auto_convert_get_internal_sinkpad (GstBaseAutoConvert *
    self);
static GstPad *gst_base_auto_convert_get_internal_srcpad (GstBaseAutoConvert *
    self);

static GstIterator *gst_base_auto_convert_iterate_internal_links (GstPad * pad,
    GstObject * parent);

static GstCaps *gst_base_auto_convert_getcaps (GstBaseAutoConvert * self,
    GstCaps * filter, GstPadDirection dir);
static GstFlowReturn gst_base_auto_convert_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstFlowReturn gst_base_auto_convert_sink_chain_list (GstPad * pad,
    GstObject * parent, GstBufferList * list);
static gboolean gst_base_auto_convert_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_base_auto_convert_sink_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

static gboolean gst_base_auto_convert_src_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_base_auto_convert_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

static GstFlowReturn gst_base_auto_convert_internal_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstFlowReturn gst_base_auto_convert_internal_sink_chain_list (GstPad *
    pad, GstObject * parent, GstBufferList * list);
static gboolean gst_base_auto_convert_internal_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_base_auto_convert_internal_sink_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

static gboolean gst_base_auto_convert_internal_src_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_base_auto_convert_internal_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static GList *gst_base_auto_convert_get_or_load_factories (GstBaseAutoConvert *
    self);

G_DECLARE_FINAL_TYPE (GstBaseAutoConvertPad, gst_base_auto_convert_pad, GST,
    BASE_AUTO_CONVERT_PAD, GstPad);
struct _GstBaseAutoConvertPad
{
  GstPad parent;

  GstBaseAutoConvert *obj;
};
G_DEFINE_TYPE (GstBaseAutoConvertPad, gst_base_auto_convert_pad, GST_TYPE_PAD);

static void
gst_base_auto_convert_pad_class_init (GstBaseAutoConvertPadClass * klass)
{
}

static void
gst_base_auto_convert_pad_init (GstBaseAutoConvertPad * self)
{
  SUPRESS_UNUSED_WARNING (GST_IS_BASE_AUTO_CONVERT_PAD);
}

G_DEFINE_TYPE (GstBaseAutoConvert, gst_base_auto_convert, GST_TYPE_BIN);

typedef struct
{
  gint refcount;

  GstPad *sink;
  GstPad *src;
} InternalPads;

static InternalPads *
internal_pads_new (GstBaseAutoConvert * self, const gchar * subelement_name)
{
  InternalPads *pads = g_new0 (InternalPads, 1);
  gchar *name = g_strdup_printf ("internal_sink_%s", subelement_name);

  pads->refcount = 1;
  pads->sink = g_object_new (gst_base_auto_convert_pad_get_type (),
      "name", name, "direction", GST_PAD_SINK, NULL);
  g_free (name);
  GST_BASE_AUTO_CONVERT_PAD (pads->sink)->obj = self;

  name = g_strdup_printf ("internal_src_%s", subelement_name);
  pads->src = g_object_new (gst_base_auto_convert_pad_get_type (),
      "name", name, "direction", GST_PAD_SRC, NULL);
  g_free (name);
  GST_BASE_AUTO_CONVERT_PAD (pads->src)->obj = self;

  return pads;
}

static void
internal_pads_unref (InternalPads * pads)
{
  if (!g_atomic_int_dec_and_test (&pads->refcount))
    return;

  gst_clear_object (&pads->sink);
  gst_clear_object (&pads->src);
  g_free (pads);
}

static InternalPads *
internal_pads_ref (InternalPads * pads)
{
  g_atomic_int_inc (&pads->refcount);

  return pads;
}

static void
gst_base_auto_convert_element_removed (GstBin * bin, GstElement * child)
{
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT (bin);

  g_hash_table_remove (self->elements, child);
}

static void
gst_base_auto_convert_class_init (GstBaseAutoConvertClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBinClass *gstbin_class = (GstBinClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;

  GST_DEBUG_CATEGORY_INIT (baseautoconvert_debug, "baseautoconvert", 0,
      "Auto convert based on caps");

  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_base_auto_convert_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_base_auto_convert_finalize);

  gstbin_class->element_removed = gst_base_auto_convert_element_removed;
}

static void
gst_base_auto_convert_init (GstBaseAutoConvert * self)
{
  self->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  self->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");
  self->elements = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) internal_pads_unref);

  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_auto_convert_sink_chain));
  gst_pad_set_chain_list_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_auto_convert_sink_chain_list));
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_auto_convert_sink_event));
  gst_pad_set_query_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_auto_convert_sink_query));
  gst_pad_set_iterate_internal_links_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_auto_convert_iterate_internal_links));

  gst_pad_set_event_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_auto_convert_src_event));
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_auto_convert_src_query));
  gst_pad_set_iterate_internal_links_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_auto_convert_iterate_internal_links));

  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}

static void
gst_base_auto_convert_dispose (GObject * object)
{
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT (object);

  GST_BASEAUTOCONVERT_LOCK (self);
  g_clear_object (&self->current_subelement);
  g_clear_object (&self->current_internal_sinkpad);
  g_clear_object (&self->current_internal_srcpad);
  GST_BASEAUTOCONVERT_UNLOCK (self);

  G_OBJECT_CLASS (gst_base_auto_convert_parent_class)->dispose (object);
}

static void
gst_base_auto_convert_finalize (GObject * object)
{
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT (object);

  if (self->factories)
    gst_plugin_feature_list_free (self->factories);
  g_hash_table_unref (self->elements);

  G_OBJECT_CLASS (gst_base_auto_convert_parent_class)->finalize (object);
}

static GstElement *
gst_base_auto_convert_get_element_by_type (GstBaseAutoConvert * self,
    GType type)
{
  GList *item, *elements;
  GstElement *element = NULL;

  g_return_val_if_fail (type != 0, NULL);

  GST_BASEAUTOCONVERT_LOCK (self);
  elements = g_hash_table_get_keys (self->elements);
  for (item = elements; item; item = item->next) {
    if (G_OBJECT_TYPE (item->data) == type) {
      element = gst_object_ref (item->data);
      break;
    }
  }
  GST_BASEAUTOCONVERT_UNLOCK (self);

  g_list_free (elements);

  return element;
}

/**
 * get_pad_by_direction:
 * @element: The Element
 * @direction: The direction
 *
 * Gets a #GstPad that goes in the requested direction. I will return NULL
 * if there is no pad or if there is more than one pad in this direction
 */

static GstPad *
get_pad_by_direction (GstElement * element, GstPadDirection direction)
{
  GstIterator *iter = gst_element_iterate_pads (element);
  GstPad *selected_pad = NULL;
  gboolean done;
  GValue item = { 0, };

  if (!iter)
    return NULL;

  done = FALSE;
  while (!done) {
    switch (gst_iterator_next (iter, &item)) {
      case GST_ITERATOR_OK:
      {
        GstPad *pad = g_value_get_object (&item);

        if (gst_pad_get_direction (pad) == direction) {
          /* We check if there is more than one pad in this direction,
           * if there is, we return NULL so that the element is refused
           */
          if (selected_pad) {
            done = TRUE;
            gst_object_unref (selected_pad);
            selected_pad = NULL;
          } else {
            selected_pad = g_object_ref (pad);
          }
        }
        g_value_unset (&item);
      }
        break;
      case GST_ITERATOR_RESYNC:
        if (selected_pad) {
          gst_object_unref (selected_pad);
          selected_pad = NULL;
        }
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_ERROR ("Error iterating pads of element %s",
            GST_OBJECT_NAME (element));
        gst_object_unref (selected_pad);
        selected_pad = NULL;
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (iter);

  if (!selected_pad)
    GST_ERROR ("Did not find pad of direction %d in %s",
        direction, GST_OBJECT_NAME (element));

  return selected_pad;
}

static GstElement *
gst_base_auto_convert_get_subelement (GstBaseAutoConvert * self)
{
  GstElement *element = NULL;

  GST_BASEAUTOCONVERT_LOCK (self);
  if (self->current_subelement)
    element = gst_object_ref (self->current_subelement);
  GST_BASEAUTOCONVERT_UNLOCK (self);

  return element;
}

static GstPad *
gst_base_auto_convert_get_internal_sinkpad (GstBaseAutoConvert * self)
{
  GstPad *pad = NULL;

  GST_BASEAUTOCONVERT_LOCK (self);
  if (self->current_internal_sinkpad)
    pad = gst_object_ref (self->current_internal_sinkpad);
  GST_BASEAUTOCONVERT_UNLOCK (self);

  return pad;
}

static GstPad *
gst_base_auto_convert_get_internal_srcpad (GstBaseAutoConvert * self)
{
  GstPad *pad = NULL;

  GST_BASEAUTOCONVERT_LOCK (self);
  if (self->current_internal_srcpad)
    pad = gst_object_ref (self->current_internal_srcpad);
  GST_BASEAUTOCONVERT_UNLOCK (self);

  return pad;
}

/*
 * This function creates and adds an element to the GstBaseAutoConvert
 * it then creates the internal pads and links them
 *
 */

static GstElement *
gst_base_auto_convert_add_element (GstBaseAutoConvert * self,
    GstElementFactory * factory)
{
  GstElement *element = NULL;
  InternalPads *pads;

  GST_DEBUG_OBJECT (self, "Adding element %s to the baseautoconvert bin",
      gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));

  element = gst_element_factory_create (factory, NULL);
  if (!element)
    return NULL;

  pads = internal_pads_new (self, GST_OBJECT_NAME (element));
  g_hash_table_insert (self->elements, element, pads);

  gst_pad_set_chain_function (pads->sink,
      GST_DEBUG_FUNCPTR (gst_base_auto_convert_internal_sink_chain));
  gst_pad_set_chain_list_function (pads->sink,
      GST_DEBUG_FUNCPTR (gst_base_auto_convert_internal_sink_chain_list));
  gst_pad_set_event_function (pads->sink,
      GST_DEBUG_FUNCPTR (gst_base_auto_convert_internal_sink_event));
  gst_pad_set_query_function (pads->sink,
      GST_DEBUG_FUNCPTR (gst_base_auto_convert_internal_sink_query));

  gst_pad_set_event_function (pads->src,
      GST_DEBUG_FUNCPTR (gst_base_auto_convert_internal_src_event));
  gst_pad_set_query_function (pads->src,
      GST_DEBUG_FUNCPTR (gst_base_auto_convert_internal_src_query));

  return gst_object_ref (element);
}

static GstElement *
gst_base_auto_convert_get_or_make_element_from_factory (GstBaseAutoConvert *
    self, GstElementFactory * factory)
{
  GstElement *element = NULL;
  GstElementFactory *loaded_factory =
      GST_ELEMENT_FACTORY (gst_plugin_feature_load (GST_PLUGIN_FEATURE
          (factory)));

  if (!loaded_factory)
    return NULL;

  element = gst_base_auto_convert_get_element_by_type (self,
      gst_element_factory_get_element_type (loaded_factory));

  if (!element) {
    element = gst_base_auto_convert_add_element (self, loaded_factory);
  }

  gst_object_unref (loaded_factory);

  return element;
}

/*
 * This function checks if there is one and only one pad template on the
 * factory that can accept the given caps. If there is one and only one,
 * it returns TRUE, FALSE otherwise
 */

static gboolean
factory_can_intersect (GstBaseAutoConvert * self,
    GstElementFactory * factory, GstPadDirection direction, GstCaps * caps)
{
  const GList *templates;
  gint has_direction = FALSE;
  gboolean ret = FALSE;

  g_return_val_if_fail (factory != NULL, FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);

  templates = gst_element_factory_get_static_pad_templates (factory);

  while (templates) {
    GstStaticPadTemplate *template = (GstStaticPadTemplate *) templates->data;

    if (template->direction == direction) {
      GstCaps *tmpl_caps = NULL;
      gboolean intersect;

      /* If there is more than one pad in this direction, we return FALSE
       * Only transform elements (with one sink and one source pad)
       * are accepted
       */
      if (has_direction) {
        GST_ERROR_OBJECT (self, "Factory %p"
            " has more than one static template with dir %d",
            template, direction);
        return FALSE;
      }
      has_direction = TRUE;

      tmpl_caps = gst_static_caps_get (&template->static_caps);
      intersect = gst_caps_can_intersect (tmpl_caps, caps);
      GST_DEBUG_OBJECT (self, "Factories %" GST_PTR_FORMAT
          " static caps %" GST_PTR_FORMAT " and caps %" GST_PTR_FORMAT
          " can%s intersect", factory, tmpl_caps, caps,
          intersect ? "" : " not");
      gst_caps_unref (tmpl_caps);

      ret |= intersect;
    }
    templates = g_list_next (templates);
  }

  return ret;
}

static gboolean
sticky_event_push (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT (user_data);

  gst_event_ref (*event);
  gst_pad_push_event (self->current_internal_srcpad, *event);

  return TRUE;
}

static InternalPads *
gst_base_auto_convert_get_element_internal_pads (GstBaseAutoConvert * self,
    GstElement * element)
{
  InternalPads *pads;

  GST_OBJECT_LOCK (self);
  pads = g_hash_table_lookup (self->elements, element);
  if (pads)
    pads = internal_pads_ref (pads);
  GST_OBJECT_UNLOCK (self);


  return pads;
}

static gboolean
gst_base_auto_convert_activate_element (GstBaseAutoConvert * self,
    GstElement * element, GstCaps * caps)
{
  gboolean res = TRUE;
  GstPad *sinkpad = NULL, *srcpad = NULL;
  GstPadLinkReturn padlinkret;
  GstElement *current_subelement = NULL;
  InternalPads *pads =
      gst_base_auto_convert_get_element_internal_pads (self, element);

  g_assert (pads);

  if (caps) {
    /* check if the element can really accept said caps */
    if (!gst_pad_peer_query_accept_caps (pads->src, caps)) {
      GST_DEBUG_OBJECT (self, "Could not set %s:%s to %"
          GST_PTR_FORMAT, GST_DEBUG_PAD_NAME (pads->src), caps);
      goto error;
    }
  }

  current_subelement = gst_base_auto_convert_get_subelement (self);
  gst_element_set_locked_state (element, FALSE);
  if (!gst_bin_add (GST_BIN (self), gst_object_ref (element))) {
    GST_ERROR_OBJECT (self, "Could not add element %s to the bin",
        GST_OBJECT_NAME (element));
    goto error;
  }

  if (!gst_element_sync_state_with_parent (element)) {
    GST_WARNING_OBJECT (self, "Could sync %" GST_PTR_FORMAT " state", element);
    goto error;
  }

  srcpad = get_pad_by_direction (element, GST_PAD_SRC);
  if (!srcpad) {
    GST_ERROR_OBJECT (self, "Could not find source in %s",
        GST_OBJECT_NAME (element));
    goto error;
  }

  sinkpad = get_pad_by_direction (element, GST_PAD_SINK);
  if (!sinkpad) {
    GST_ERROR_OBJECT (self, "Could not find sink in %s",
        GST_OBJECT_NAME (element));
    goto error;
  }

  gst_pad_set_active (pads->sink, TRUE);
  gst_pad_set_active (pads->src, TRUE);

  padlinkret = gst_pad_link_full (pads->src, sinkpad,
      GST_PAD_LINK_CHECK_NOTHING);
  if (GST_PAD_LINK_FAILED (padlinkret)) {
    GST_WARNING_OBJECT (self, "Could not links pad %s:%s to %s:%s"
        " for reason %d",
        GST_DEBUG_PAD_NAME (pads->src),
        GST_DEBUG_PAD_NAME (sinkpad), padlinkret);
    goto error;
  }

  padlinkret = gst_pad_link_full (srcpad, pads->sink,
      GST_PAD_LINK_CHECK_NOTHING);
  if (GST_PAD_LINK_FAILED (padlinkret)) {
    GST_WARNING_OBJECT (self, "Could not links pad %s:%s to %s:%s"
        " for reason %d",
        GST_DEBUG_PAD_NAME (pads->src),
        GST_DEBUG_PAD_NAME (sinkpad), padlinkret);
    goto error;
  }

  GST_BASEAUTOCONVERT_LOCK (self);
  gst_object_replace ((GstObject **) & self->current_subelement,
      GST_OBJECT (element));
  gst_object_replace ((GstObject **) & self->current_internal_srcpad,
      GST_OBJECT (pads->src));
  gst_object_replace ((GstObject **) & self->current_internal_sinkpad,
      GST_OBJECT (pads->sink));
  GST_BASEAUTOCONVERT_UNLOCK (self);

  if (current_subelement) {
    gst_element_set_locked_state (current_subelement, TRUE);
    gst_element_set_state (current_subelement, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (self), current_subelement);
  }

  gst_pad_sticky_events_foreach (self->sinkpad, sticky_event_push, self);

  gst_pad_push_event (self->sinkpad, gst_event_new_reconfigure ());

  GST_INFO_OBJECT (self, "Selected element %s",
      GST_OBJECT_NAME (GST_OBJECT (element)));

done:
  gst_object_unref (element);
  internal_pads_unref (pads);
  gst_clear_object (&srcpad);
  gst_clear_object (&sinkpad);
  gst_clear_object (&current_subelement);

  return res;

error:
  gst_element_set_locked_state (element, TRUE);
  gst_element_set_state (element, GST_STATE_NULL);

  if (GST_OBJECT_PARENT (element))
    gst_bin_remove (GST_BIN (self), element);

  res = FALSE;
  goto done;
}

static GstIterator *
gst_base_auto_convert_iterate_internal_links (GstPad * pad, GstObject * parent)
{
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT (parent);
  GstIterator *it = NULL;
  GstPad *internal;

  if (pad == self->sinkpad)
    internal = gst_base_auto_convert_get_internal_srcpad (self);
  else
    internal = gst_base_auto_convert_get_internal_sinkpad (self);

  if (internal) {
    GValue val = { 0, };

    g_value_init (&val, GST_TYPE_PAD);
    g_value_take_object (&val, internal);

    it = gst_iterator_new_single (GST_TYPE_PAD, &val);
    g_value_unset (&val);
  }

  return it;
}

/*
 * If there is already an internal element, it will try to call set_caps on it
 *
 * If there isn't an internal element or if the set_caps() on the internal
 * element failed, it will try to find another element where it would succeed
 * and will change the internal element.
 */

static gboolean
gst_base_auto_convert_sink_setcaps (GstBaseAutoConvert * self, GstCaps * caps,
    gboolean check_downstream)
{
  GList *elem;
  GstCaps *other_caps = NULL;
  GList *factories;
  GstCaps *current_caps = NULL;
  gboolean res = FALSE;
  GstElement *current_subelement = NULL;

  g_return_val_if_fail (self != NULL, FALSE);

  if (!check_downstream) {
    current_caps = gst_pad_get_current_caps (self->sinkpad);

    if (current_caps && gst_caps_is_equal_fixed (caps, current_caps))
      goto get_out;
  }

  if (check_downstream)
    other_caps = gst_pad_peer_query_caps (self->srcpad, NULL);

  current_subelement = gst_base_auto_convert_get_subelement (self);
  if (current_subelement) {
    if (gst_pad_peer_query_accept_caps (self->current_internal_srcpad, caps)) {

      res = TRUE;
      if (other_caps) {
        GstElementFactory *factory =
            gst_element_get_factory (current_subelement);

        if (!factory_can_intersect (self, factory, GST_PAD_SRC, other_caps)) {
          GST_LOG_OBJECT (self,
              "Factory %s does not accept src caps %" GST_PTR_FORMAT,
              gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)),
              other_caps);
          res = FALSE;
        }
      }

      if (res) {
        /* If we can set the new caps on the current element,
         * then we just get out
         */
        GST_ERROR_OBJECT (self, "Could set %s:%s to %" GST_PTR_FORMAT,
            GST_DEBUG_PAD_NAME (self->current_internal_srcpad), caps);
        goto get_out;
      }
    }
  }

  if (!check_downstream)
    other_caps = gst_pad_peer_query_caps (self->srcpad, NULL);
  /* We already queries downstream caps otherwise */

  factories = gst_base_auto_convert_get_or_load_factories (self);

  for (elem = factories; elem; elem = g_list_next (elem)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (elem->data);
    GstElement *element;

    /* Lets first check if according to the static pad templates on the factory
     * these caps have any chance of success
     */
    if (!factory_can_intersect (self, factory, GST_PAD_SINK, caps)) {
      GST_LOG_OBJECT (self, "Factory %s does not accept sink caps %"
          GST_PTR_FORMAT,
          gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)), caps);
      continue;
    }
    if (other_caps != NULL) {
      if (!factory_can_intersect (self, factory, GST_PAD_SRC, other_caps)) {
        GST_LOG_OBJECT (self,
            "Factory %s does not accept src caps %" GST_PTR_FORMAT,
            gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)),
            other_caps);
        continue;
      }
    }

    /* The element had a chance of success, lets make it */
    element =
        gst_base_auto_convert_get_or_make_element_from_factory (self, factory);
    if (!element)
      continue;

    /* And make it the current child */
    if (gst_base_auto_convert_activate_element (self, element, caps)) {
      res = TRUE;
      break;
    } else {
      gst_object_unref (element);
    }
  }

get_out:
  gst_clear_object (&current_subelement);
  gst_clear_caps (&other_caps);
  gst_clear_caps (&current_caps);

  if (!res)
    GST_WARNING_OBJECT (self,
        "Could not find a matching element for caps: %" GST_PTR_FORMAT, caps);

  return res;
}

/*
 * This function filters the pad pad templates, taking only transform element
 * (with one sink and one src pad)
 */
static gboolean
gst_base_auto_convert_default_filter_func (GstPluginFeature * feature,
    gpointer user_data)
{
  GstElementFactory *factory = NULL;
  const GList *static_pad_templates, *tmp;
  GstStaticPadTemplate *src = NULL, *sink = NULL;

  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;

  factory = GST_ELEMENT_FACTORY (feature);

  static_pad_templates = gst_element_factory_get_static_pad_templates (factory);

  for (tmp = static_pad_templates; tmp; tmp = g_list_next (tmp)) {
    GstStaticPadTemplate *template = tmp->data;
    GstCaps *caps;

    if (template->presence == GST_PAD_SOMETIMES)
      return FALSE;

    if (template->presence != GST_PAD_ALWAYS)
      continue;

    switch (template->direction) {
      case GST_PAD_SRC:
        if (src)
          return FALSE;
        src = template;
        break;
      case GST_PAD_SINK:
        if (sink)
          return FALSE;
        sink = template;
        break;
      default:
        return FALSE;
    }

    caps = gst_static_pad_template_get_caps (template);

    if (gst_caps_is_any (caps) || gst_caps_is_empty (caps))
      return FALSE;
  }

  if (!src || !sink)
    return FALSE;

  return TRUE;
}

/* function used to sort element features
 * Copy-pasted from decodebin */
static gint
compare_ranks (GstPluginFeature * f1, GstPluginFeature * f2)
{
  gint diff;
  const gchar *rname1, *rname2;

  diff = gst_plugin_feature_get_rank (f2) - gst_plugin_feature_get_rank (f1);
  if (diff != 0)
    return diff;

  rname1 = gst_plugin_feature_get_name (f1);
  rname2 = gst_plugin_feature_get_name (f2);

  diff = strcmp (rname2, rname1);

  return diff;
}

static GList *
gst_base_auto_convert_get_or_load_factories (GstBaseAutoConvert * self)
{
  GList *all_factories;
  GstBaseAutoConvertClass *klass = GST_BASE_AUTO_CONVERT_GET_CLASS (self);

  GST_OBJECT_LOCK (self);
  if (self->factories)
    goto done;
  GST_OBJECT_UNLOCK (self);

  if (klass->load_factories) {
    all_factories = klass->load_factories (self);
  } else {
    all_factories =
        g_list_sort (gst_registry_feature_filter (gst_registry_get (),
            gst_base_auto_convert_default_filter_func, FALSE, NULL),
        (GCompareFunc) compare_ranks);
  }

  GST_OBJECT_LOCK (self);
  self->factories = all_factories;

done:
  GST_OBJECT_UNLOCK (self);

  return self->factories;
}

/* In this case, we should almost always have an internal element, because
 * set_caps() should have been called first
 */

static GstFlowReturn
gst_base_auto_convert_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_NOT_NEGOTIATED;
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT (parent);

  if (gst_pad_check_reconfigure (self->srcpad)) {
    GstCaps *sinkcaps = gst_pad_get_current_caps (pad);

    GST_INFO_OBJECT (parent, "Needs reconfigure.");
    /* if we need to reconfigure we pretend new caps arrived. This
     * will reconfigure the transform with the new output format. */
    if (sinkcaps && !gst_base_auto_convert_sink_setcaps (self, sinkcaps, TRUE)) {
      gst_clear_caps (&sinkcaps);
      GST_ERROR_OBJECT (self, "Could not reconfigure.");

      return GST_FLOW_NOT_NEGOTIATED;
    }
    gst_clear_caps (&sinkcaps);
  }

  if (self->current_internal_srcpad) {
    ret = gst_pad_push (self->current_internal_srcpad, buffer);
    if (ret != GST_FLOW_OK)
      GST_DEBUG_OBJECT (self,
          "Child element %" GST_PTR_FORMAT "returned flow %s",
          self->current_subelement, gst_flow_get_name (ret));
  } else {
    GST_ERROR_OBJECT (self, "Got buffer without an negotiated element,"
        " returning not-negotiated");
    gst_buffer_unref (buffer);
  }

  return ret;
}

static GstFlowReturn
gst_base_auto_convert_sink_chain_list (GstPad * pad, GstObject * parent,
    GstBufferList * list)
{
  GstFlowReturn ret = GST_FLOW_NOT_NEGOTIATED;
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT (parent);

  if (self->current_internal_srcpad) {
    ret = gst_pad_push_list (self->current_internal_srcpad, list);
    if (ret != GST_FLOW_OK)
      GST_DEBUG_OBJECT (self,
          "Child element %" GST_PTR_FORMAT "returned flow %s",
          self->current_subelement, gst_flow_get_name (ret));
  } else {
    GST_ERROR_OBJECT (self, "Got buffer without an negotiated element,"
        " returning not-negotiated");
    gst_buffer_list_unref (list);
  }

  return ret;
}

static gboolean
gst_base_auto_convert_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean ret = TRUE;
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT (parent);
  GstPad *internal_srcpad;

  if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS) {
    GstCaps *caps;

    gst_event_parse_caps (event, &caps);
    ret = gst_base_auto_convert_sink_setcaps (self, caps, FALSE);
    if (!ret) {
      gst_event_unref (event);
      return ret;
    }
  }

  internal_srcpad = gst_base_auto_convert_get_internal_srcpad (self);
  if (internal_srcpad) {
    ret = gst_pad_push_event (internal_srcpad, event);
    gst_object_unref (internal_srcpad);
  } else {
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_FLUSH_STOP:
      case GST_EVENT_FLUSH_START:
        ret = gst_pad_push_event (self->srcpad, event);
        break;
      default:
        gst_event_unref (event);
        ret = TRUE;
        break;
    }
  }

  return ret;
}

/* TODO Properly test that this code works well for queries */
static gboolean
gst_base_auto_convert_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  gboolean ret = TRUE;
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT (parent);
  GstElement *subelement;

  if (GST_QUERY_TYPE (query) == GST_QUERY_CAPS) {
    GstCaps *filter, *caps;

    gst_query_parse_caps (query, &filter);
    caps = gst_base_auto_convert_getcaps (self, filter, GST_PAD_SINK);
    gst_query_set_caps_result (query, caps);
    gst_caps_unref (caps);

    return TRUE;
  }

  subelement = gst_base_auto_convert_get_subelement (self);
  if (subelement) {
    GstPad *sub_sinkpad = get_pad_by_direction (subelement, GST_PAD_SINK);

    ret = gst_pad_query (sub_sinkpad, query);

    gst_object_unref (sub_sinkpad);
    gst_object_unref (subelement);

    if (ret && GST_QUERY_TYPE (query) == GST_QUERY_ACCEPT_CAPS) {
      gboolean res;
      gst_query_parse_accept_caps_result (query, &res);

      if (!res)
        goto ignore_acceptcaps_failure;
    }
    return ret;
  }

ignore_acceptcaps_failure:

  if (GST_QUERY_TYPE (query) == GST_QUERY_ACCEPT_CAPS) {
    GstCaps *caps;
    GstCaps *accept_caps;

    gst_query_parse_accept_caps (query, &accept_caps);

    caps = gst_base_auto_convert_getcaps (self, accept_caps, GST_PAD_SINK);
    gst_query_set_accept_caps_result (query,
        gst_caps_can_intersect (caps, accept_caps));
    gst_caps_unref (caps);

    return TRUE;
  }

  GST_WARNING_OBJECT (self, "Got query %s while no element was"
      " selected, letting through",
      gst_query_type_get_name (GST_QUERY_TYPE (query)));
  return gst_pad_peer_query (self->srcpad, query);
}

/**
 * gst_base_auto_convert_getcaps:
 * @pad: the sink #GstPad
 *
 * This function returns the union of the caps of all the possible element
 * factories, based on the static pad templates.
 * It also checks does a getcaps on the downstream element and ignores all
 * factories whose static caps can not satisfy it.
 *
 * It does not try to use each elements getcaps() function
 */

static GstCaps *
gst_base_auto_convert_getcaps (GstBaseAutoConvert * self, GstCaps * filter,
    GstPadDirection dir)
{
  GstCaps *caps = NULL, *other_caps = NULL;
  GList *elem, *factories;

  caps = gst_caps_new_empty ();

  if (dir == GST_PAD_SINK)
    other_caps = gst_pad_peer_query_caps (self->srcpad, NULL);
  else
    other_caps = gst_pad_peer_query_caps (self->sinkpad, NULL);

  GST_DEBUG_OBJECT (self,
      "Finding elements that can fit with src caps %" GST_PTR_FORMAT,
      other_caps);

  if (other_caps && gst_caps_is_empty (other_caps)) {
    goto out;
  }

  factories = gst_base_auto_convert_get_or_load_factories (self);
  for (elem = factories; elem; elem = g_list_next (elem)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (elem->data);
    GstElement *element = NULL;
    GstCaps *element_caps;
    InternalPads *pads;

    if (filter) {
      if (!factory_can_intersect (self, factory, dir, filter)) {
        GST_LOG_OBJECT (self,
            "Factory %s does not accept src caps %" GST_PTR_FORMAT,
            gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)),
            other_caps);
        continue;
      }
    }

    if (other_caps != NULL) {
      if (!factory_can_intersect (self, factory,
              dir == GST_PAD_SINK ? GST_PAD_SRC : GST_PAD_SINK, other_caps)) {
        GST_LOG_OBJECT (self,
            "Factory %s does not accept src caps %" GST_PTR_FORMAT,
            gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)),
            other_caps);
        continue;
      }

      element = gst_base_auto_convert_get_or_make_element_from_factory (self,
          factory);
      if (element == NULL)
        continue;

      pads = gst_base_auto_convert_get_element_internal_pads (self, element);
      element_caps =
          gst_pad_peer_query_caps ((dir ==
              GST_PAD_SINK) ? pads->src : pads->sink, filter);
      internal_pads_unref (pads);
      if (element_caps)
        caps = gst_caps_merge (caps, element_caps);

      gst_object_unref (element);

      /* Early out, any is absorbing */
      if (gst_caps_is_any (caps))
        goto out;
    } else {
      const GList *tmp;

      for (tmp = gst_element_factory_get_static_pad_templates (factory);
          tmp; tmp = g_list_next (tmp)) {
        GstStaticPadTemplate *template = tmp->data;

        if (GST_PAD_TEMPLATE_DIRECTION (template) == dir) {
          GstCaps *static_caps = gst_static_pad_template_get_caps (template);

          if (static_caps) {
            caps = gst_caps_merge (caps, static_caps);
          }

          /* Early out, any is absorbing */
          if (gst_caps_is_any (caps))
            goto out;
        }
      }
    }
  }

  GST_DEBUG_OBJECT (self, "Returning unioned caps %" GST_PTR_FORMAT, caps);

out:

  if (other_caps)
    gst_caps_unref (other_caps);

  return caps;
}



static gboolean
gst_base_auto_convert_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean ret = TRUE;
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT (parent);
  GstPad *internal_sinkpad;

  if (GST_EVENT_TYPE (event) == GST_EVENT_RECONFIGURE)
    gst_pad_push_event (self->sinkpad, gst_event_ref (event));

  internal_sinkpad = gst_base_auto_convert_get_internal_sinkpad (self);
  if (internal_sinkpad) {
    ret = gst_pad_push_event (internal_sinkpad, event);
    gst_object_unref (internal_sinkpad);
  } else if (GST_EVENT_TYPE (event) != GST_EVENT_RECONFIGURE) {
    GST_WARNING_OBJECT (self,
        "Got upstream event while no element was selected," "forwarding.");
    ret = gst_pad_push_event (self->sinkpad, event);
  } else
    gst_event_unref (event);

  return ret;
}

/* TODO Properly test that this code works well for queries */
static gboolean
gst_base_auto_convert_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  gboolean ret = TRUE;
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT (parent);
  GstElement *subelement;

  if (GST_QUERY_TYPE (query) == GST_QUERY_CAPS) {
    GstCaps *filter, *caps;

    gst_query_parse_caps (query, &filter);
    caps = gst_base_auto_convert_getcaps (self, filter, GST_PAD_SRC);
    gst_query_set_caps_result (query, caps);
    gst_caps_unref (caps);

    return TRUE;
  }

  subelement = gst_base_auto_convert_get_subelement (self);
  if (subelement) {
    GstPad *sub_srcpad = get_pad_by_direction (subelement, GST_PAD_SRC);

    ret = gst_pad_query (sub_srcpad, query);

    gst_object_unref (sub_srcpad);
    gst_object_unref (subelement);
  } else {
    GST_WARNING_OBJECT (self,
        "Got upstream query of type %s while no element was selected,"
        " forwarding.", gst_query_type_get_name (GST_QUERY_TYPE (query)));
    ret = gst_pad_peer_query (self->sinkpad, query);
  }

  return ret;
}

static GstFlowReturn
gst_base_auto_convert_internal_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT_PAD (pad)->obj;

  return gst_pad_push (self->srcpad, buffer);
}

static GstFlowReturn
gst_base_auto_convert_internal_sink_chain_list (GstPad * pad,
    GstObject * parent, GstBufferList * list)
{
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT_PAD (pad)->obj;

  return gst_pad_push_list (self->srcpad, list);
}

static gboolean
gst_base_auto_convert_internal_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT_PAD (pad)->obj;
  gboolean drop = FALSE;

  GST_BASEAUTOCONVERT_LOCK (self);
  if (self->current_internal_sinkpad != pad) {
    drop = TRUE;
  }
  GST_BASEAUTOCONVERT_UNLOCK (self);

  if (drop) {
    gst_event_unref (event);
    return TRUE;
  }

  return gst_pad_push_event (self->srcpad, event);
}

static gboolean
gst_base_auto_convert_internal_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT_PAD (pad)->obj;

  if (!gst_pad_peer_query (self->srcpad, query)) {
    switch (GST_QUERY_TYPE (query)) {
      case GST_QUERY_CAPS:
      {
        GstCaps *filter;

        gst_query_parse_caps (query, &filter);
        if (filter) {
          gst_query_set_caps_result (query, filter);
        } else {
          filter = gst_caps_new_any ();
          gst_query_set_caps_result (query, filter);
          gst_caps_unref (filter);
        }
        return TRUE;
      }
      case GST_QUERY_ACCEPT_CAPS:
        gst_query_set_accept_caps_result (query, TRUE);
        return TRUE;
      default:
        return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_base_auto_convert_internal_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT_PAD (pad)->obj;
  gboolean drop = FALSE;

  GST_BASEAUTOCONVERT_LOCK (self);
  if (self->current_internal_srcpad != pad) {
    drop = TRUE;
  }
  GST_BASEAUTOCONVERT_UNLOCK (self);

  if (drop) {
    GST_DEBUG_OBJECT (self, "Dropping event %" GST_PTR_FORMAT, event);
    gst_event_unref (event);
    return TRUE;
  }

  return gst_pad_push_event (self->sinkpad, event);
}

static gboolean
gst_base_auto_convert_internal_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstBaseAutoConvert *self = GST_BASE_AUTO_CONVERT_PAD (pad)->obj;

  return gst_pad_peer_query (self->sinkpad, query);
}
