/* Gnonlin
 * Copyright (C) <2001> Wim Taymans <wim.taymans@gmail.com>
 *               <2004-2008> Edward Hervey <bilboed@bilboed.com>
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "nle.h"

/**
 * SECTION:element-nlesource
 *
 * The NleSource encapsulates a pipeline which produces data for processing
 * in a #NleComposition.
 */

static GstStaticPadTemplate nle_source_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (nlesource);
#define GST_CAT_DEFAULT nlesource

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (nlesource, "nlesource", GST_DEBUG_FG_BLUE | GST_DEBUG_BOLD, "GNonLin Source Element");
#define nle_source_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (NleSource, nle_source, NLE_TYPE_OBJECT, _do_init);

struct _NleSourcePrivate
{
  gboolean dispose_has_run;

  gboolean dynamicpads;         /* TRUE if the controlled element has dynamic pads */

  gulong padremovedid;          /* signal handler for element pad-removed signal */
  gulong padaddedid;            /* signal handler for element pad-added signal */

  gboolean pendingblock;        /* We have a pending pad_block */
  GstPad *ghostedpad;           /* Pad (to be) ghosted */
  GstPad *staticpad;            /* The only pad. We keep an extra ref */
};

static gboolean nle_source_prepare (NleObject * object);
static gboolean nle_source_add_element (GstBin * bin, GstElement * element);
static gboolean nle_source_remove_element (GstBin * bin, GstElement * element);
static void nle_source_dispose (GObject * object);

static gboolean
nle_source_control_element_func (NleSource * source, GstElement * element);

static void
nle_source_class_init (NleSourceClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;
  NleObjectClass *nleobject_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbin_class = (GstBinClass *) klass;
  nleobject_class = (NleObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (NleSourcePrivate));

  gst_element_class_set_static_metadata (gstelement_class, "GNonLin Source",
      "Filter/Editor",
      "Manages source elements",
      "Wim Taymans <wim.taymans@gmail.com>, Edward Hervey <bilboed@bilboed.com>");

  parent_class = g_type_class_ref (NLE_TYPE_OBJECT);

  klass->control_element = GST_DEBUG_FUNCPTR (nle_source_control_element_func);

  nleobject_class->prepare = GST_DEBUG_FUNCPTR (nle_source_prepare);

  gstbin_class->add_element = GST_DEBUG_FUNCPTR (nle_source_add_element);
  gstbin_class->remove_element = GST_DEBUG_FUNCPTR (nle_source_remove_element);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (nle_source_dispose);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&nle_source_src_template));

}


static void
nle_source_init (NleSource * source)
{
  GST_OBJECT_FLAG_SET (source, NLE_OBJECT_SOURCE);
  source->element = NULL;
  source->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (source, NLE_TYPE_SOURCE, NleSourcePrivate);

  GST_DEBUG_OBJECT (source, "Setting GstBin async-handling to TRUE");
  g_object_set (G_OBJECT (source), "async-handling", TRUE, NULL);
}

static void
nle_source_dispose (GObject * object)
{
  NleObject *nleobject = (NleObject *) object;
  NleSource *source = (NleSource *) object;
  NleSourcePrivate *priv = source->priv;

  GST_DEBUG_OBJECT (object, "dispose");

  if (priv->dispose_has_run)
    return;

  if (source->element) {
    gst_object_unref (source->element);
    source->element = NULL;
  }

  priv->dispose_has_run = TRUE;
  if (priv->ghostedpad)
    nle_object_ghost_pad_set_target (nleobject, nleobject->srcpad, NULL);

  if (priv->staticpad) {
    gst_object_unref (priv->staticpad);
    priv->staticpad = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
element_pad_added_cb (GstElement * element G_GNUC_UNUSED, GstPad * pad,
    NleSource * source)
{
  GstCaps *srccaps;
  NleSourcePrivate *priv = source->priv;
  NleObject *nleobject = (NleObject *) source;

  GST_DEBUG_OBJECT (source, "pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  if (priv->ghostedpad) {
    GST_DEBUG_OBJECT (source,
        "We already have a target, not doing anything with %s:%s",
        GST_DEBUG_PAD_NAME (pad));

    return;
  }

  /* FIXME: pass filter caps to query_caps directly */
  srccaps = gst_pad_query_caps (pad, NULL);
  if (nleobject->caps && !gst_caps_can_intersect (srccaps, nleobject->caps)) {
    gst_caps_unref (srccaps);
    GST_DEBUG_OBJECT (source, "Pad doesn't have valid caps, ignoring");
    return;
  }
  gst_caps_unref (srccaps);

  priv->ghostedpad = pad;
  GST_DEBUG_OBJECT (nleobject, "SET target %" GST_PTR_FORMAT, pad);
  nle_object_ghost_pad_set_target (nleobject, nleobject->srcpad, pad);

  GST_DEBUG_OBJECT (source, "Using pad pad %s:%s as a target now!",
      GST_DEBUG_PAD_NAME (pad));
}

static void
element_pad_removed_cb (GstElement * element G_GNUC_UNUSED, GstPad * pad,
    NleSource * source)
{
  NleSourcePrivate *priv = source->priv;
  NleObject *nleobject = (NleObject *) source;

  GST_DEBUG_OBJECT (source, "pad %s:%s (controlled pad %s:%s)",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (priv->ghostedpad));

  if (pad == priv->ghostedpad) {
    GST_DEBUG_OBJECT (source,
        "The removed pad is the controlled pad, clearing up");

    GST_DEBUG_OBJECT (source, "Clearing up ghostpad");

    nle_object_ghost_pad_set_target (NLE_OBJECT (source), nleobject->srcpad,
        NULL);
    priv->ghostedpad = NULL;
  } else {
    GST_DEBUG_OBJECT (source, "The removed pad is NOT our controlled pad");
  }
}

static gint
compare_src_pad (GValue * item, GstCaps * caps)
{
  gint ret = 1;
  GstPad *pad = g_value_get_object (item);
  GstCaps *padcaps;

  GST_DEBUG_OBJECT (pad, "Trying pad for caps %" GST_PTR_FORMAT, caps);

  /* FIXME: can pass the filter caps right away.. */
  padcaps = gst_pad_query_caps (pad, NULL);

  if (gst_caps_can_intersect (padcaps, caps))
    ret = 0;

  gst_caps_unref (padcaps);

  return ret;
}

/*
  get_valid_src_pad

  Returns True if there's a src pad compatible with the NleObject caps in the
  given element. Fills in pad if so. The returned pad has an incremented refcount
*/

static gboolean
get_valid_src_pad (NleSource * source, GstElement * element, GstPad ** pad)
{
  gboolean res = FALSE;
  GstIterator *srcpads;
  GValue item = { 0, };

  g_return_val_if_fail (pad, FALSE);

  srcpads = gst_element_iterate_src_pads (element);
  if (gst_iterator_find_custom (srcpads, (GCompareFunc) compare_src_pad, &item,
          NLE_OBJECT (source)->caps)) {
    *pad = g_value_get_object (&item);
    gst_object_ref (*pad);
    g_value_reset (&item);
    res = TRUE;
  }
  gst_iterator_free (srcpads);

  return res;
}

/*
 * has_dynamic_pads
 * Returns TRUE if the element has only dynamic pads.
 */

static gboolean
has_dynamic_srcpads (GstElement * element)
{
  gboolean ret = TRUE;
  GList *templates;
  GstPadTemplate *template;

  templates =
      gst_element_class_get_pad_template_list (GST_ELEMENT_GET_CLASS (element));

  while (templates) {
    template = (GstPadTemplate *) templates->data;

    if ((GST_PAD_TEMPLATE_DIRECTION (template) == GST_PAD_SRC)
        && (GST_PAD_TEMPLATE_PRESENCE (template) == GST_PAD_ALWAYS)) {
      ret = FALSE;
      break;
    }

    templates = templates->next;
  }

  return ret;
}

static gboolean
nle_source_control_element_func (NleSource * source, GstElement * element)
{
  NleSourcePrivate *priv = source->priv;
  GstPad *pad = NULL;

  g_return_val_if_fail (source->element == NULL, FALSE);

  GST_DEBUG_OBJECT (source, "element:%s, source->element:%p",
      GST_ELEMENT_NAME (element), source->element);

  source->element = element;
  gst_object_ref (element);

  if (get_valid_src_pad (source, source->element, &pad)) {
    priv->staticpad = pad;
    nle_object_ghost_pad_set_target (NLE_OBJECT (source),
        NLE_OBJECT_SRC (source), pad);
    priv->dynamicpads = FALSE;
  } else {
    priv->dynamicpads = has_dynamic_srcpads (element);
    GST_DEBUG_OBJECT (source, "No valid source pad yet, dynamicpads:%d",
        priv->dynamicpads);
    if (priv->dynamicpads) {
      /* connect to pad-added/removed signals */
      priv->padremovedid = g_signal_connect
          (G_OBJECT (element), "pad-removed",
          G_CALLBACK (element_pad_removed_cb), source);
      priv->padaddedid =
          g_signal_connect (G_OBJECT (element), "pad-added",
          G_CALLBACK (element_pad_added_cb), source);
    }
  }

  return TRUE;
}

static gboolean
nle_source_add_element (GstBin * bin, GstElement * element)
{
  NleSource *source = (NleSource *) bin;
  gboolean pret;

  GST_DEBUG_OBJECT (source, "Adding element %s", GST_ELEMENT_NAME (element));

  if (source->element) {
    GST_WARNING_OBJECT (bin, "NleSource can only handle one element at a time");
    return FALSE;
  }

  /* call parent add_element */
  pret = GST_BIN_CLASS (parent_class)->add_element (bin, element);

  if (pret) {
    nle_source_control_element_func (source, element);
  }
  return pret;
}

static gboolean
nle_source_remove_element (GstBin * bin, GstElement * element)
{
  NleSource *source = (NleSource *) bin;
  NleObject *nleobject = (NleObject *) element;
  NleSourcePrivate *priv = source->priv;
  gboolean pret;

  GST_DEBUG_OBJECT (source, "Removing element %s", GST_ELEMENT_NAME (element));

  /* try to remove it */
  pret = GST_BIN_CLASS (parent_class)->remove_element (bin, element);

  if ((!source->element) || (source->element != element)) {
    return TRUE;
  }

  if (pret) {
    nle_object_ghost_pad_set_target (NLE_OBJECT (source), nleobject->srcpad,
        NULL);

    /* remove signal handlers */
    if (priv->padremovedid) {
      g_signal_handler_disconnect (source->element, priv->padremovedid);
      priv->padremovedid = 0;
    }
    if (priv->padaddedid) {
      g_signal_handler_disconnect (source->element, priv->padaddedid);
      priv->padaddedid = 0;
    }

    priv->dynamicpads = FALSE;
    gst_object_unref (element);
    source->element = NULL;
  }
  return pret;
}

static gboolean
nle_source_prepare (NleObject * object)
{
  GstPad *pad;
  NleSource *source = NLE_SOURCE (object);
  NleSourcePrivate *priv = source->priv;
  GstElement *parent =
      (GstElement *) gst_element_get_parent ((GstElement *) object);

  if (!source->element) {
    GST_WARNING_OBJECT (source,
        "NleSource doesn't have an element to control !");
    if (parent)
      gst_object_unref (parent);
    return FALSE;
  }

  if (object->in_composition == FALSE) {
    gst_element_send_event (GST_ELEMENT_CAST (parent),
        gst_event_new_seek (1.0, GST_FORMAT_TIME,
            GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH,
            GST_SEEK_TYPE_SET, object->start, GST_SEEK_TYPE_SET, object->stop));
  }

  GST_LOG_OBJECT (source, "srcpad:%p, dynamicpads:%d",
      object->srcpad, priv->dynamicpads);

  if (!priv->staticpad && !(get_valid_src_pad (source, source->element, &pad))) {
    GST_DEBUG_OBJECT (source, "Couldn't find a valid source pad");
  } else {
    if (priv->staticpad)
      pad = gst_object_ref (priv->staticpad);
    GST_LOG_OBJECT (source, "Trying to async block source pad %s:%s",
        GST_DEBUG_PAD_NAME (pad));
    priv->ghostedpad = pad;
    gst_object_unref (pad);
  }

  gst_object_unref (parent);

  return TRUE;
}
