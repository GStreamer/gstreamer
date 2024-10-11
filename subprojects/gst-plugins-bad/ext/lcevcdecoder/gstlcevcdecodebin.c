/* GStreamer
 *  Copyright (C) <2024> V-Nova International Limited
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

#include <gst/pbutils/pbutils.h>

#include "gstlcevcdecutils.h"
#include "gstlcevcdecodebin.h"

enum
{
  PROP_0,
  PROP_BASE_DECODER,
};

GST_DEBUG_CATEGORY_STATIC (lcevcdecodebin_debug);
#define GST_CAT_DEFAULT (lcevcdecodebin_debug)

typedef struct
{
  /* Props */
  gchar *base_decoder;

  gboolean constructed;
  const gchar *missing_element;
} GstLcevcDecodeBinPrivate;

#define gst_lcevc_decode_bin_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstLcevcDecodeBin, gst_lcevc_decode_bin,
    GST_TYPE_BIN,
    G_ADD_PRIVATE (GstLcevcDecodeBin);
    GST_DEBUG_CATEGORY_INIT (lcevcdecodebin_debug, "lcevcdecodebin", 0,
        "lcevcdecodebin"));

static GstStaticPadTemplate gst_lcevc_decode_bin_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        (GST_LCEVC_DEC_UTILS_SUPPORTED_FORMATS))
    );

static gboolean
gst_lcevc_decode_bin_open (GstLcevcDecodeBin * self)
{
  GstLcevcDecodeBinPrivate *priv =
      gst_lcevc_decode_bin_get_instance_private (self);

  if (priv->missing_element) {
    gst_element_post_message (GST_ELEMENT (self),
        gst_missing_element_message_new (GST_ELEMENT (self),
            priv->missing_element));
  } else if (!priv->constructed) {
    GST_ELEMENT_ERROR (self, CORE, FAILED, (NULL),
        ("Failed to construct or link LCEVC decoder elements."));
  }

  return priv->constructed;
}

static GstStateChangeReturn
gst_lcevc_decode_bin_change_state (GstElement * element,
    GstStateChange transition)
{
  GstLcevcDecodeBin *self = GST_LCEVC_DECODE_BIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_lcevc_decode_bin_open (self))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static char *
gst_lcevc_decode_bin_find_base_decoder (GstLcevcDecodeBin * self)
{
  GstLcevcDecodeBinClass *klass = GST_LCEVC_DECODE_BIN_GET_CLASS (self);
  GList *factories;
  gchar *res = NULL;
  GstCaps *accepted_caps;

  /* Get the accepted sink caps for the base decoder */
  if (!klass->get_base_decoder_sink_caps)
    return NULL;
  accepted_caps = klass->get_base_decoder_sink_caps (self);
  if (!accepted_caps)
    return NULL;

  /* Get all decoders and sort them by rank */
  factories =
      gst_element_factory_list_get_elements (GST_ELEMENT_FACTORY_TYPE_DECODER,
      GST_RANK_MARGINAL);
  factories = g_list_sort (factories, gst_plugin_feature_rank_compare_func);

  /* Select the first compatible factory (list is sorted by rank) */
  while (factories) {
    GstElementFactory *f = factories->data;
    const GList *pad_templates;
    gboolean is_compatible = FALSE;

    factories = g_list_next (factories);

    pad_templates = gst_element_factory_get_static_pad_templates (f);
    while (pad_templates) {
      GstStaticPadTemplate *st = (GstStaticPadTemplate *) pad_templates->data;
      GstCaps *template_caps;

      pad_templates = g_list_next (pad_templates);

      /* Get the sink pad template */
      if (st->direction != GST_PAD_SINK)
        continue;

      /* Skip any pad that is not h264 with lcevc=false */
      template_caps = gst_static_pad_template_get_caps (st);
      if (!gst_caps_can_intersect (template_caps, accepted_caps)) {
        gst_caps_unref (template_caps);
        continue;
      }

      gst_caps_unref (template_caps);
      is_compatible = TRUE;
      break;
    }

    if (is_compatible) {
      res = gst_object_get_name (GST_OBJECT (f));
      break;
    }
  }

  g_list_free (factories);
  gst_caps_unref (accepted_caps);
  return res;
}

static void
gst_lcevc_decode_bin_constructed (GObject * obj)
{
  GstLcevcDecodeBin *self = GST_LCEVC_DECODE_BIN (obj);
  GstLcevcDecodeBinClass *klass = GST_LCEVC_DECODE_BIN_GET_CLASS (self);
  GstLcevcDecodeBinPrivate *priv =
      gst_lcevc_decode_bin_get_instance_private (self);
  GstPad *src_gpad, *sink_gpad;
  GstPad *src_pad = NULL, *sink_pad = NULL;
  GstElement *base_decoder = NULL;
  GstElement *lcevcdec = NULL;

  /* setup ghost pads */
  sink_gpad = gst_ghost_pad_new_no_target_from_template ("sink",
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink"));
  gst_element_add_pad (GST_ELEMENT (self), sink_gpad);

  src_gpad = gst_ghost_pad_new_no_target_from_template ("src",
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src"));
  gst_element_add_pad (GST_ELEMENT (self), src_gpad);

  /* Create base decoder if name is given, otherwise fine one */
  if (priv->base_decoder) {
    base_decoder = gst_element_factory_make (priv->base_decoder, NULL);
    if (!base_decoder) {
      priv->missing_element = priv->base_decoder;
      goto error;
    }
  } else {
    gchar *name = gst_lcevc_decode_bin_find_base_decoder (self);
    if (!name)
      goto error;
    base_decoder = gst_element_factory_make (name, NULL);
    g_free (name);
    if (!base_decoder)
      goto error;
  }

  /* Create LCEVC decoder */
  lcevcdec = gst_element_factory_make ("lcevcdec", NULL);
  if (!lcevcdec) {
    priv->missing_element = "lcevcdec";
    goto error;
  }

  if (!gst_bin_add (GST_BIN (self), base_decoder))
    goto error;
  if (!gst_bin_add (GST_BIN (self), lcevcdec))
    goto error;

  if (!gst_element_link (base_decoder, lcevcdec))
    goto error;

  /* link elements */
  sink_pad = gst_element_get_static_pad (base_decoder, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (sink_gpad), sink_pad);
  gst_clear_object (&sink_pad);

  src_pad = gst_element_get_static_pad (lcevcdec, "src");
  gst_ghost_pad_set_target (GST_GHOST_PAD (src_gpad), src_pad);
  gst_object_unref (src_pad);

  /* signal success, we will handle this in NULL->READY transition */
  priv->constructed = TRUE;
  G_OBJECT_CLASS (parent_class)->constructed (obj);
  return;

error:
  gst_clear_object (&base_decoder);
  gst_clear_object (&lcevcdec);

  priv->constructed = FALSE;
  G_OBJECT_CLASS (parent_class)->constructed (obj);
}

static void
gst_lcevc_decode_bin_finalize (GObject * obj)
{
  GstLcevcDecodeBin *self = GST_LCEVC_DECODE_BIN (obj);
  GstLcevcDecodeBinPrivate *priv =
      gst_lcevc_decode_bin_get_instance_private (self);

  g_free (priv->base_decoder);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}


static void
gst_lcevc_decode_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstLcevcDecodeBin *self = GST_LCEVC_DECODE_BIN (object);
  GstLcevcDecodeBinPrivate *priv =
      gst_lcevc_decode_bin_get_instance_private (self);

  switch (prop_id) {
    case PROP_BASE_DECODER:
      g_clear_pointer (&priv->base_decoder, g_free);
      priv->base_decoder = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_lcevc_decode_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstLcevcDecodeBin *self = GST_LCEVC_DECODE_BIN (object);
  GstLcevcDecodeBinPrivate *priv =
      gst_lcevc_decode_bin_get_instance_private (self);

  switch (prop_id) {
    case PROP_BASE_DECODER:
      g_value_set_string (value, priv->base_decoder);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_lcevc_decode_bin_handle_message (GstBin * bin, GstMessage * message)
{
  /* We use bin as source for latency messages (fixes decodebin3 autopluggin) */
  switch (message->type) {
    case GST_MESSAGE_LATENCY:
      gst_message_unref (message);
      message = gst_message_new_latency (GST_OBJECT (bin));
      return;
    default:
      break;
  }

  GST_BIN_CLASS (parent_class)->handle_message (bin, message);
}

static void
gst_lcevc_decode_bin_class_init (GstLcevcDecodeBinClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBinClass *bin_class = GST_BIN_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &gst_lcevc_decode_bin_src_template);
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_lcevc_decode_bin_change_state);

  gst_type_mark_as_plugin_api (GST_TYPE_LCEVC_DECODE_BIN, 0);

  gobject_class->constructed = gst_lcevc_decode_bin_constructed;
  gobject_class->finalize = gst_lcevc_decode_bin_finalize;
  gobject_class->set_property = gst_lcevc_decode_bin_set_property;
  gobject_class->get_property = gst_lcevc_decode_bin_get_property;

  bin_class->handle_message = gst_lcevc_decode_bin_handle_message;

  g_object_class_install_property (gobject_class, PROP_BASE_DECODER,
      g_param_spec_string ("base-decoder", "Base Decoder",
          "The base decoder element name (NULL for automatic)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_lcevc_decode_bin_init (GstLcevcDecodeBin * self)
{
}
