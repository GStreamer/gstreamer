/* GStreamer object detection overlay
 * Copyright (C) <2024, 2025> Collabora Ltd.
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gsttensordecodebin.c
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
 * SECTION:element-tensordecodebin
 * @short_description: Find and instantiate compatible tensor decoder
 *
 * This element instantiate a tensor decoder compatible  with upstream caps.
 *
 * ## Example launch command:
 * |[
 * gst-launch-1.0 filesrc location=/onnx-models/images/bus.jpg !
 *  ! jpegdec ! videoconvert ! onnxinference execution-provider=cpu
 *    model-file=/onnx-models/models/ssd_mobilenet_v1_coco.onnx
 *  ! tensordecodebin ! objectdetectionoverlay ! videoconvert ! imagefreeze
 *  ! autovideosink
 * ]| Assuming the model is a object detection model this pipeline will instantiate
 * a tensor decoder compatible upstream tensor caps.
 *
 * Since: 1.28
 */

#include <gst/gst.h>
#ifdef HAVE_CONFI_H
#include "config.h"
#endif

#include "gsttensordecodebin.h"

#include <gst/gst.h>


GST_DEBUG_CATEGORY_STATIC (tensordecodebin_debug);
#define GST_CAT_DEFAULT tensordecodebin_debug

GST_ELEMENT_REGISTER_DEFINE (tensordecodebin, "tensordecodebin",
    GST_RANK_NONE, GST_TYPE_TENSORDECODEBIN);

static GstStaticPadTemplate gst_tensordecodebin_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_tensordecodebin_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static gboolean
gst_tensordecodebin_sink_query (GstPad * pad, GstObject * parend, GstQuery *
    query);

static gboolean
gst_tensordecodebin_sink_event (GstPad * pad, GstObject * parent, GstEvent *
    event);

static void gst_tensordecodebin_finalize (GObject * object);

G_DEFINE_TYPE (GstTensorDecodeBin, gst_tensordecodebin, GST_TYPE_BIN);

static void
gst_tensordecodebin_class_init (GstTensorDecodeBinClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (tensordecodebin_debug, "tensordecodebin", 0,
      "Tensor decode bin");

  /* Element description. */
  gst_element_class_set_static_metadata (element_class, "tensordecodebin",
      "Tensor Decoder Bin",
      "Tensor Decode Bin", "Daniel Morin <daniel.morin@collabora.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_tensordecodebin_src_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_tensordecodebin_sink_template));

  gobject_class->finalize = gst_tensordecodebin_finalize;
}

static void
gst_tensordecodebin_init (GstTensorDecodeBin * self)
{
  GstPadTemplate *pad_tmpl;
  pad_tmpl = gst_static_pad_template_get (&gst_tensordecodebin_sink_template);
  self->sinkpad = gst_ghost_pad_new_no_target_from_template ("sink", pad_tmpl);

  gst_clear_object (&pad_tmpl);
  pad_tmpl = gst_static_pad_template_get (&gst_tensordecodebin_src_template);
  self->srcpad = gst_ghost_pad_new_no_target_from_template ("src", pad_tmpl);
  gst_clear_object (&pad_tmpl);

  self->last_event_caps = NULL;
  self->factories_cookie = 0;
  self->aggregated_caps = NULL;

  gst_pad_set_query_function (self->sinkpad, gst_tensordecodebin_sink_query);
  gst_pad_set_event_function_full (self->sinkpad,
      gst_tensordecodebin_sink_event, self, NULL);

  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}

static void
gst_tensordecodebin_finalize (GObject * object)
{
  GstTensorDecodeBin *self = GST_TENSORDECODEBIN (object);
  g_list_free_full (self->tensordec_factories, gst_object_unref);
  gst_clear_caps (&self->last_event_caps);
  gst_clear_caps (&self->aggregated_caps);
  G_OBJECT_CLASS (gst_tensordecodebin_parent_class)->finalize (object);
}

static gboolean
decoder_filter (GstPluginFeature * feature, GstTensorDecodeBin * self)
{
  const gchar *klass;
  GstElementFactory *fact;
  GstElementClass *self_class = GST_ELEMENT_GET_CLASS (self);

  /* we only care about element factories */
  if (G_UNLIKELY (!GST_IS_ELEMENT_FACTORY (feature)))
    return FALSE;

  fact = GST_ELEMENT_FACTORY_CAST (feature);

  klass = gst_element_factory_get_metadata (fact, GST_ELEMENT_METADATA_KLASS);

  /* Filter on Tensordecoder Klass */
  if (strstr (klass, "Tensordecoder")) {

    /* Skip ourself */
    if (fact == self_class->elementfactory)
      return FALSE;

    /* Only keep element with rank equal or above marginal */
    if (gst_plugin_feature_get_rank (feature) < GST_RANK_MARGINAL)
      return FALSE;

    GST_DEBUG_OBJECT (self, "adding %s factory", GST_OBJECT_NAME (fact));
    return TRUE;
  }

  return FALSE;
}

static GList *
gst_tensordecodebin_get_or_load_tensordec_factories_unlocked (GstTensorDecodeBin
    * self)
{
  guint32 cookie;
  GList *all_tensordec_factories;

  cookie = gst_registry_get_feature_list_cookie (gst_registry_get ());
  if (!self->tensordec_factories || self->factories_cookie != cookie) {

    if (self->tensordec_factories)
      g_list_free_full (self->tensordec_factories, gst_object_unref);

    all_tensordec_factories =
        g_list_sort (gst_registry_feature_filter (gst_registry_get (),
            (GstPluginFeatureFilter) decoder_filter, FALSE, self),
        (GCompareFunc) gst_plugin_feature_rank_compare_func);

    self->tensordec_factories = all_tensordec_factories;
    self->factories_cookie = cookie;
    gst_clear_caps (&self->aggregated_caps);
  }

  return g_list_copy_deep (self->tensordec_factories,
      (GCopyFunc) gst_object_ref, NULL);
}

static GList *
gst_tensordecodebin_get_or_load_tensordec_factories (GstTensorDecodeBin * self)
{
  GList *factories;
  GST_OBJECT_LOCK (self);
  factories =
      gst_tensordecodebin_get_or_load_tensordec_factories_unlocked (self);
  GST_OBJECT_UNLOCK (self);
  return factories;
}

static void
_remove_all_elements (GstBin * bin)
{
  GstElement *e;
  GList *childs, *l;

  GST_DEBUG_OBJECT (bin, "Removing all childs");

  GST_OBJECT_LOCK (bin);
  childs = g_list_copy_deep (bin->children, (GCopyFunc) gst_object_ref, NULL);
  GST_OBJECT_UNLOCK (bin);

  l = childs;
  while (l) {
    GST_TRACE_OBJECT (bin, "Removing child %p", l->data);
    e = l->data;
    gst_bin_remove (bin, e);
    gst_element_set_state (GST_ELEMENT (e), GST_STATE_NULL);
    l = l->next;
  };
  g_list_free_full (childs, gst_object_unref);
}

static GstPadTemplate *
_get_compatible_sinkpad_template (GstTensorDecodeBin * self,
    GstElementFactory * factory)
{
  const GList *tpls;
  GstPadTemplate *tpl;
  const GList *fact_tpls;
  GstPadTemplate *compa_sinkpad_tpl = NULL;
  guint16 num_compa_sinkpad_template = 0, num_compa_srcpad_template = 0;

  fact_tpls = gst_element_factory_get_static_pad_templates (factory);
  for (tpls = fact_tpls; tpls; tpls = tpls->next) {
    tpl = gst_static_pad_template_get (tpls->data);

    /* FIXME: Add support for Request pads and Sometime pads */
    if (tpl->presence != GST_PAD_ALWAYS) {
      GST_WARNING_OBJECT (self, "Tensor decoder %s has %s pad which is "
          "not currently supported by the tensordecodebin and is ignored.",
          gst_element_factory_get_metadata (factory,
              GST_ELEMENT_METADATA_LONGNAME),
          tpl->presence == GST_PAD_REQUEST ? "request" : "sometimes");

      /* Skip this template */
      gst_clear_object (&tpl);
      continue;
    }

    if (tpl->direction == GST_PAD_SINK) {
      num_compa_sinkpad_template++;

      if (num_compa_sinkpad_template == 1)
        compa_sinkpad_tpl = gst_object_ref (tpl);

    } else if (tpl->direction == GST_PAD_SRC) {
      num_compa_srcpad_template++;
    } else {
      GST_WARNING_OBJECT (self,
          "Tensor decoder %s has a pad template with UNKNOWN direction,"
          " skipping this template.", gst_element_factory_get_metadata (factory,
              GST_ELEMENT_METADATA_LONGNAME));

      /* Skip this template */
      gst_clear_object (&tpl);
      continue;
    }
    gst_clear_object (&tpl);
  }

  /* FIXME: Add support for tensor decoder with multiple sinkpads and/or
   * srcpads */
  if (num_compa_sinkpad_template != 1 || num_compa_srcpad_template != 1) {
    GST_WARNING_OBJECT (self,
        "tensordecodebin only support tensor decoder with 1 always"
        " sinkpad and 1 always srcpad, but %s has %u sinkpad and %u srcpad and will not be condiered",
        gst_element_factory_get_metadata (factory,
            GST_ELEMENT_METADATA_LONGNAME), num_compa_sinkpad_template,
        num_compa_srcpad_template);
    gst_clear_object (&compa_sinkpad_tpl);
  }

  return compa_sinkpad_tpl;
}

static gboolean
gst_tensordecodebin_sink_caps_event (GstTensorDecodeBin * self, GstCaps * ecaps)
{
  gboolean ret = TRUE;
  GstElement *e = NULL;
  GstPad *sinkpad = NULL, *srcpad = NULL;
  GstElementFactory *factory;
  GList *factories = NULL;
  GstCaps *tplcaps;

  const GstStructure *s;
  const GValue *v;

  gst_ghost_pad_set_target (GST_GHOST_PAD (self->sinkpad), NULL);
  gst_ghost_pad_set_target (GST_GHOST_PAD (self->srcpad), NULL);
  _remove_all_elements (GST_BIN (self));
  gst_caps_replace (&self->last_event_caps, ecaps);

  /* We check all tensor group can be handled by a tensor decoder */
  s = gst_caps_get_structure (ecaps, 0);
  v = gst_structure_get_value (s, "tensors");

  if (v == NULL) {
    /* No tensor caps, we don't need any tensor decoder */
    GST_INFO_OBJECT (self, "No tensor caps in, tensordecodebin will be "
        "passthrough");
    e = gst_element_factory_make ("identity", NULL);
    if (!gst_bin_add (GST_BIN (self), e)) {
      GST_ERROR_OBJECT (self, "Failed to add identity");
      goto fail;
    } else {
      sinkpad = gst_element_get_static_pad (e, "sink");
      if (!gst_ghost_pad_set_target (GST_GHOST_PAD (self->sinkpad), sinkpad)) {
        GST_ERROR_OBJECT (self, "Failed to set sinkpad target to "
            "identity.sinkpad");
        goto fail;
      }

      gst_clear_object (&sinkpad);
      srcpad = gst_element_get_static_pad (e, "src");
      gst_element_sync_state_with_parent (e);

      goto done;
    }
  }

  /* NOTE: tensordecodebin assumes that tensordecoder does not modify the media
   * or the capabilities. This is not a fundamental limitation of tensor
   * capabilities but rather a limitation of the current tensordecodebin
   * implementation. To implement support for tensordecoder-induced capability
   * changes, we would need to maintain a full history of transformations. Currently,
   * tensordecoder assumes the tensor was produced by inference on the attached
   * media. However, this assumption will not hold if tensordecoder can modify
   * media. Consequently, a tensordecoder following one that changes media would
   * need to retrieve media details from the time the inference produced the
   * tensor being decoded.
   */
  factories = gst_tensordecodebin_get_or_load_tensordec_factories (self);
  for (GList * f = factories; f; f = g_list_next (f)) {
    GstPadTemplate *compa_sinkpad_tpl = NULL;
    factory = GST_ELEMENT_FACTORY (f->data);
    compa_sinkpad_tpl = _get_compatible_sinkpad_template (self, factory);

    tplcaps = gst_pad_template_get_caps (compa_sinkpad_tpl);

    /* Check if sinkpad has at least a tensors field */
    s = gst_caps_get_structure (tplcaps, 0);
    if (!gst_structure_has_field (s, "tensors")) {
      GST_WARNING_OBJECT (self,
          "Element from %s factory have no tensors capabilities",
          gst_element_factory_get_longname (factory));
      gst_clear_caps (&tplcaps);
      gst_clear_object (&compa_sinkpad_tpl);
      continue;
    }

    if (gst_caps_is_subset (ecaps, tplcaps)) {
      gst_clear_caps (&tplcaps);

      e = gst_element_factory_create (factory, NULL);
      sinkpad =
          gst_element_get_static_pad (e, compa_sinkpad_tpl->name_template);
      if (!sinkpad) {
        GST_WARNING_OBJECT (self, "Element %p from %s factory has no sinkpad",
            e, gst_element_factory_get_longname (factory));
        gst_clear_object (&e);
        gst_clear_object (&compa_sinkpad_tpl);

        continue;
      }
    } else {
      gst_clear_caps (&tplcaps);
      gst_clear_object (&compa_sinkpad_tpl);
      continue;
    }

    gst_clear_object (&compa_sinkpad_tpl);

    if (gst_pad_query_accept_caps (sinkpad, ecaps)) {
      gst_bin_add (GST_BIN (self), e);

      GST_DEBUG_OBJECT (self, "selected tensor decoder: %" GST_PTR_FORMAT, e);

      if (!gst_element_sync_state_with_parent (e)) {
        GST_WARNING_OBJECT (self, "Element %" GST_PTR_FORMAT " failed to "
            "synchronise its state with parent and will not be added to "
            "this bin.", e);
        gst_bin_remove (GST_BIN (self), e);
        gst_clear_object (&e);
        continue;
      }

      if (srcpad) {
        if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK) {
          GST_ERROR_OBJECT (self,
              "Could not link %" GST_PTR_FORMAT " and %" GST_PTR_FORMAT, srcpad,
              sinkpad);
          goto fail;
        }

        gst_clear_object (&srcpad);
      } else {
        if (!gst_ghost_pad_set_target (GST_GHOST_PAD (self->sinkpad), sinkpad)) {
          GST_ERROR_OBJECT (self, "Failed to set sinkpad target");
          goto fail;
        }
      }

      gst_clear_object (&sinkpad);
      srcpad = gst_element_get_static_pad (e, "src");

      e = NULL;
      continue;

    } else {
      GST_WARNING_OBJECT (self, "Factory (%p)'s sinkpad (%p) didn't accept "
          "caps:%" GST_PTR_FORMAT, factory, sinkpad, ecaps);
      gst_clear_object (&sinkpad);
      gst_clear_object (&e);
    }
  }

  g_list_free_full (g_steal_pointer (&factories), gst_object_unref);

  if (srcpad == NULL) {
    GST_WARNING_OBJECT (self, "Could not find tensor decoder for %"
        GST_PTR_FORMAT, ecaps);
    goto fail;
  }

done:
  if (!srcpad || !gst_ghost_pad_set_target (GST_GHOST_PAD (self->srcpad),
          srcpad)) {
    GST_ERROR_OBJECT (self, "Failed to set srcpad target");
    goto fail;
  }

  gst_clear_object (&srcpad);
  return ret;

fail:
  g_list_free_full (factories, gst_object_unref);

  _remove_all_elements (GST_BIN (self));

  gst_clear_object (&srcpad);
  gst_clear_object (&sinkpad);
  gst_clear_object (&e);
  return FALSE;
}

static gboolean
gst_tensordecodebin_sink_event (GstPad * pad, GstObject * parent, GstEvent *
    event)
{
  gboolean ret = TRUE;
  GstTensorDecodeBin *self = GST_TENSORDECODEBIN (parent);
  GstCaps *ecaps = NULL;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      gst_event_parse_caps (event, &ecaps);

      if (!gst_tensordecodebin_sink_caps_event (self, ecaps)) {
        gst_caps_unref (ecaps);
        goto done;
      }

      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

done:
  return ret;
}

static GstCaps *
_get_tensordecoders_caps (GstTensorDecodeBin * self)
{
  GstElementFactory *factory;
  GstCaps *tplcaps, *acc_caps = NULL;
  GstPadTemplate *tpl;

  GList *factories =
      gst_tensordecodebin_get_or_load_tensordec_factories_unlocked (self);

  if (self->aggregated_caps != NULL)
    goto done;

  acc_caps = gst_caps_new_empty ();

  for (GList * f = factories; f; f = g_list_next (f)) {
    factory = GST_ELEMENT_FACTORY (f->data);
    tpl = _get_compatible_sinkpad_template (self, factory);

    if (!tpl) {
      GST_WARNING_OBJECT (self,
          "No compatible sinkpad template found %s factory",
          gst_element_factory_get_metadata (factory,
              GST_ELEMENT_METADATA_LONGNAME));
      continue;
    }

    tplcaps = gst_pad_template_get_caps (tpl);
    gst_clear_object (&tpl);
    acc_caps = gst_caps_merge (acc_caps, tplcaps);
  }

  self->aggregated_caps = acc_caps;

done:
  g_list_free_full (factories, gst_object_unref);

  return gst_caps_ref (self->aggregated_caps);
}

static gboolean
gst_tensordecodebin_sink_query (GstPad * pad, GstObject * parent, GstQuery *
    query)
{
  gboolean ret;
  GstTensorDecodeBin *self = GST_TENSORDECODEBIN (parent);

  switch (query->type) {
    case GST_QUERY_CAPS:
    {
      GstCaps *acc_caps, *filter_caps = NULL, *intersection;
      GstQuery *dn_query;
      gst_query_parse_caps (query, &filter_caps);

      GST_OBJECT_LOCK (self);
      acc_caps = _get_tensordecoders_caps (self);
      GST_OBJECT_UNLOCK (self);

      if (filter_caps) {
        intersection = gst_caps_intersect (acc_caps, filter_caps);
        gst_caps_replace (&acc_caps, intersection);
      }

      dn_query = gst_query_new_caps (acc_caps);
      if ((ret = gst_pad_peer_query (self->srcpad, dn_query))) {
        gst_query_parse_caps (dn_query, &filter_caps);
        if (filter_caps) {
          intersection = gst_caps_intersect (acc_caps, filter_caps);
          gst_caps_replace (&acc_caps, intersection);
        }
      }

      gst_clear_query (&dn_query);
      gst_query_set_caps_result (query, acc_caps);
      gst_caps_unref (acc_caps);
      break;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps, *acc_caps = NULL;

      GST_OBJECT_LOCK (self);
      acc_caps = _get_tensordecoders_caps (self);
      gst_query_parse_accept_caps (query, &caps);
      gst_query_set_accept_caps_result (query, gst_caps_can_intersect (acc_caps,
              caps));
      GST_OBJECT_UNLOCK (self);

      gst_caps_unref (acc_caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}
