/* GStreamer
 * Copyright (C) <2021> Collabora Ltd.
 *   Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
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

#include "gstalphadecodebin.h"

GST_DEBUG_CATEGORY_STATIC (alphadecodebin_debug);
#define GST_CAT_DEFAULT (alphadecodebin_debug)

typedef struct
{
  const gchar *factory_names[GST_ALPHA_DECODE_BIN_ROLE_COUNT];
} GstAlphaDecodeBinClassData;

typedef struct
{
  gboolean constructed;
  gboolean children_added;
  gchar *missing_element;
  gchar *construction_error;

  GstElement *alphademux;
  GstElement *mq;
  GstElement *decoder;
  GstElement *alpha_decoder;
  GstElement *alphacombine;
} GstAlphaDecodeBinPrivate;

static GQuark class_data_quark;

#define gst_alpha_decode_bin_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstAlphaDecodeBin, gst_alpha_decode_bin,
    GST_TYPE_BIN,
    G_ADD_PRIVATE (GstAlphaDecodeBin);
    GST_DEBUG_CATEGORY_INIT (alphadecodebin_debug, "alphadecodebin", 0,
        "alphadecodebin"));

static GstStaticPadTemplate gst_alpha_decode_bin_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static gboolean
gst_alpha_decode_bin_role_is_valid (GstAlphaDecodeBinRole role)
{
  return role >= GST_ALPHA_DECODE_BIN_ROLE_DEMUX
      && role < GST_ALPHA_DECODE_BIN_ROLE_COUNT;
}

static const gchar *
gst_alpha_decode_bin_role_name (GstAlphaDecodeBinRole role)
{
  switch (role) {
    case GST_ALPHA_DECODE_BIN_ROLE_DEMUX:
      return "demux";
    case GST_ALPHA_DECODE_BIN_ROLE_COLOR_DECODER:
      return "color decoder";
    case GST_ALPHA_DECODE_BIN_ROLE_ALPHA_DECODER:
      return "alpha decoder";
    case GST_ALPHA_DECODE_BIN_ROLE_COMBINE:
      return "combine";
    default:
      return "unknown";
  }
}

static GstAlphaDecodeBinClassData *
gst_alpha_decode_bin_class_get_data (GstAlphaDecodeBinClass * klass,
    gboolean create)
{
  GstAlphaDecodeBinClassData *data =
      g_type_get_qdata (G_TYPE_FROM_CLASS (klass), class_data_quark);
  if (!data && create) {
    data = g_new0 (GstAlphaDecodeBinClassData, 1);
    g_type_set_qdata (G_TYPE_FROM_CLASS (klass), class_data_quark, data);
  }

  return data;
}

void
gst_alpha_decode_bin_class_set_role_factory_name (GstAlphaDecodeBinClass *
    klass, GstAlphaDecodeBinRole role, const gchar * factory_name)
{
  GstAlphaDecodeBinClassData *data;

  g_return_if_fail (GST_IS_ALPHA_DECODE_BIN_CLASS (klass));
  g_return_if_fail (gst_alpha_decode_bin_role_is_valid (role));

  data = gst_alpha_decode_bin_class_get_data (klass, TRUE);
  data->factory_names[role] =
      factory_name ? g_intern_string (factory_name) : NULL;
}

static const gchar *
gst_alpha_decode_bin_class_get_role_factory_name (GstAlphaDecodeBinClass *
    klass, GstAlphaDecodeBinRole role)
{
  GstAlphaDecodeBinClassData *data =
      gst_alpha_decode_bin_class_get_data (klass, FALSE);

  g_return_val_if_fail (gst_alpha_decode_bin_role_is_valid (role), NULL);

  if (!data)
    return NULL;

  return data->factory_names[role];
}

static GstMessage *
gst_alpha_decode_bin_missing_element_message_new (GstElement * element,
    const gchar * factory_name)
{
  GstStructure *s;
  gchar *description;

  /* Keep the missing-plugin message shape without adding a gstvideo dependency
   * on gstpbutils just for gst_missing_element_message_new(). */
  description = g_strdup_printf ("GStreamer element %s", factory_name);
  s = gst_structure_new ("missing-plugin",
      "type", G_TYPE_STRING, "element",
      "detail", G_TYPE_STRING, factory_name,
      "name", G_TYPE_STRING, description, NULL);
  g_free (description);

  return gst_message_new_element (GST_OBJECT_CAST (element), s);
}

static void
gst_alpha_decode_bin_disable_qos_if_supported (GstAlphaDecodeBin * self,
    GstElement * element)
{
  GParamSpec *pspec =
      g_object_class_find_property (G_OBJECT_GET_CLASS (element), "qos");

  if (pspec && pspec->value_type == G_TYPE_BOOLEAN) {
    g_object_set (element, "qos", FALSE, NULL);
  } else {
    GST_DEBUG_OBJECT (self, "Element %" GST_PTR_FORMAT
        " has no boolean qos property", element);
  }
}

/* *INDENT-OFF* */
G_GNUC_PRINTF (2, 3)
/* *INDENT-ON* */

static void
gst_alpha_decode_bin_set_construction_error (GstAlphaDecodeBin * self,
    const gchar * format, ...)
{
  GstAlphaDecodeBinPrivate *priv =
      gst_alpha_decode_bin_get_instance_private (self);
  va_list args;

  g_free (priv->construction_error);

  va_start (args, format);
  priv->construction_error = g_strdup_vprintf (format, args);
  va_end (args);
}

static GstElement *
gst_alpha_decode_bin_create_role_element (GstAlphaDecodeBin * self,
    GstAlphaDecodeBinRole role, const gchar * name, const GstCaps * input_caps)
{
  GstAlphaDecodeBinClass *klass = GST_ALPHA_DECODE_BIN_GET_CLASS (self);
  GstAlphaDecodeBinPrivate *priv =
      gst_alpha_decode_bin_get_instance_private (self);
  GstElement *element = NULL;
  GError *error = NULL;
  const gchar *factory_name;

  g_return_val_if_fail (gst_alpha_decode_bin_role_is_valid (role), NULL);

  if (klass->create_role_element) {
    element = klass->create_role_element (self, role, input_caps, &error);
    if (element && error) {
      GST_WARNING_OBJECT (self,
          "create_role_element returned both an element and an error for "
          "%s role; using the element and ignoring error: %s",
          gst_alpha_decode_bin_role_name (role), error->message);
      g_clear_error (&error);
    }

    if (element)
      return element;

    if (error) {
      gst_alpha_decode_bin_set_construction_error (self,
          "Failed to create %s role element: %s",
          gst_alpha_decode_bin_role_name (role), error->message);
      g_clear_error (&error);
      return NULL;
    }
  }

  factory_name = gst_alpha_decode_bin_class_get_role_factory_name (klass, role);
  if (!factory_name) {
    gst_alpha_decode_bin_set_construction_error (self,
        "No element factory configured for %s role",
        gst_alpha_decode_bin_role_name (role));
    return NULL;
  }

  element = gst_element_factory_make (factory_name, name);
  if (!element) {
    g_free (priv->missing_element);
    priv->missing_element = g_strdup (factory_name);
  }

  return element;
}

static void
gst_alpha_decode_bin_clear_children (GstAlphaDecodeBin * self,
    gboolean clear_error_state)
{
  GstAlphaDecodeBinPrivate *priv =
      gst_alpha_decode_bin_get_instance_private (self);
  GstPad *pad;

  pad = gst_element_get_static_pad (GST_ELEMENT (self), "sink");
  if (pad) {
    gst_ghost_pad_set_target (GST_GHOST_PAD (pad), NULL);
    gst_object_unref (pad);
  }

  pad = gst_element_get_static_pad (GST_ELEMENT (self), "src");
  if (pad) {
    gst_ghost_pad_set_target (GST_GHOST_PAD (pad), NULL);
    gst_object_unref (pad);
  }

  if (priv->children_added) {
    gst_bin_remove_many (GST_BIN (self), priv->alphademux, priv->mq,
        priv->decoder, priv->alpha_decoder, priv->alphacombine, NULL);
  } else {
    gst_clear_object (&priv->alphademux);
    gst_clear_object (&priv->mq);
    gst_clear_object (&priv->decoder);
    gst_clear_object (&priv->alpha_decoder);
    gst_clear_object (&priv->alphacombine);
  }

  priv->alphademux = NULL;
  priv->mq = NULL;
  priv->decoder = NULL;
  priv->alpha_decoder = NULL;
  priv->alphacombine = NULL;
  priv->constructed = FALSE;
  priv->children_added = FALSE;

  if (clear_error_state) {
    g_clear_pointer (&priv->missing_element, g_free);
    g_clear_pointer (&priv->construction_error, g_free);
  }
}

static gboolean
gst_alpha_decode_bin_link_pads (GstAlphaDecodeBin * self,
    GstElement * src, const gchar * srcpad, GstElement * sink,
    const gchar * sinkpad)
{
  if (!gst_element_link_pads (src, srcpad, sink, sinkpad)) {
    gst_alpha_decode_bin_set_construction_error (self,
        "Failed to link %s:%s to %s:%s", GST_ELEMENT_NAME (src), srcpad,
        GST_ELEMENT_NAME (sink), sinkpad);
    return FALSE;
  }

  return TRUE;
}

static GstCaps *
gst_alpha_decode_bin_get_input_caps (GstAlphaDecodeBin * self)
{
  GstPad *sink_pad = gst_element_get_static_pad (GST_ELEMENT (self), "sink");

  if (sink_pad) {
    GstCaps *input_caps = gst_pad_peer_query_caps (sink_pad, NULL);

    gst_object_unref (sink_pad);
    return input_caps;
  }

  return NULL;
}

static gboolean
gst_alpha_decode_bin_construct_pipeline (GstAlphaDecodeBin * self)
{
  GstAlphaDecodeBinPrivate *priv =
      gst_alpha_decode_bin_get_instance_private (self);
  GstCaps *input_caps;
  GstPad *src_gpad, *sink_gpad;
  GstPad *src_pad = NULL, *sink_pad = NULL;

  if (priv->constructed)
    return TRUE;

  g_clear_pointer (&priv->missing_element, g_free);
  g_clear_pointer (&priv->construction_error, g_free);

  input_caps = gst_alpha_decode_bin_get_input_caps (self);

  priv->alphademux =
      gst_alpha_decode_bin_create_role_element (self,
      GST_ALPHA_DECODE_BIN_ROLE_DEMUX, "alphademux", input_caps);
  if (!priv->alphademux)
    goto cleanup;

  priv->mq = gst_element_factory_make ("multiqueue", NULL);
  if (!priv->mq) {
    priv->missing_element = g_strdup ("multiqueue");
    goto cleanup;
  }

  priv->decoder =
      gst_alpha_decode_bin_create_role_element (self,
      GST_ALPHA_DECODE_BIN_ROLE_COLOR_DECODER, "maindec", input_caps);
  if (!priv->decoder)
    goto cleanup;

  priv->alpha_decoder =
      gst_alpha_decode_bin_create_role_element (self,
      GST_ALPHA_DECODE_BIN_ROLE_ALPHA_DECODER, "alphadec", input_caps);
  if (!priv->alpha_decoder)
    goto cleanup;

  /* QoS can drop frames independently in each decoder, which may temporally
   * misalign the color and alpha streams before alphacombine. */
  gst_alpha_decode_bin_disable_qos_if_supported (self, priv->decoder);
  gst_alpha_decode_bin_disable_qos_if_supported (self, priv->alpha_decoder);

  priv->alphacombine =
      gst_alpha_decode_bin_create_role_element (self,
      GST_ALPHA_DECODE_BIN_ROLE_COMBINE, "alphacombine", NULL);
  if (!priv->alphacombine)
    goto cleanup;

  gst_bin_add_many (GST_BIN (self), priv->alphademux, priv->mq,
      priv->decoder, priv->alpha_decoder, priv->alphacombine, NULL);
  priv->children_added = TRUE;

  sink_gpad = gst_element_get_static_pad (GST_ELEMENT (self), "sink");
  sink_pad = gst_element_get_static_pad (priv->alphademux, "sink");
  if (!sink_gpad || !sink_pad
      || !gst_ghost_pad_set_target (GST_GHOST_PAD (sink_gpad), sink_pad)) {
    gst_alpha_decode_bin_set_construction_error (self,
        "Failed to configure sink ghost pad");
    gst_clear_object (&sink_gpad);
    gst_clear_object (&sink_pad);
    goto cleanup;
  }
  gst_clear_object (&sink_gpad);
  gst_clear_object (&sink_pad);

  if (!gst_alpha_decode_bin_link_pads (self, priv->alphademux, "src",
          priv->mq, "sink_0"))
    goto cleanup;
  if (!gst_alpha_decode_bin_link_pads (self, priv->mq, "src_0",
          priv->decoder, "sink"))
    goto cleanup;
  if (!gst_alpha_decode_bin_link_pads (self, priv->decoder, "src",
          priv->alphacombine, "sink"))
    goto cleanup;
  if (!gst_alpha_decode_bin_link_pads (self, priv->alphademux, "alpha",
          priv->mq, "sink_1"))
    goto cleanup;
  if (!gst_alpha_decode_bin_link_pads (self, priv->mq, "src_1",
          priv->alpha_decoder, "sink"))
    goto cleanup;
  if (!gst_alpha_decode_bin_link_pads (self, priv->alpha_decoder, "src",
          priv->alphacombine, "alpha"))
    goto cleanup;

  src_gpad = gst_element_get_static_pad (GST_ELEMENT (self), "src");
  src_pad = gst_element_get_static_pad (priv->alphacombine, "src");
  if (!src_gpad || !src_pad
      || !gst_ghost_pad_set_target (GST_GHOST_PAD (src_gpad), src_pad)) {
    gst_alpha_decode_bin_set_construction_error (self,
        "Failed to configure src ghost pad");
    gst_clear_object (&src_gpad);
    gst_clear_object (&src_pad);
    goto cleanup;
  }
  gst_clear_object (&src_gpad);
  gst_clear_object (&src_pad);

  g_object_set (priv->mq, "max-size-bytes", 0, "max-size-time",
      G_GUINT64_CONSTANT (0), "max-size-buffers", 1, NULL);

  priv->constructed = TRUE;
  gst_clear_caps (&input_caps);
  return TRUE;

cleanup:
  gst_clear_caps (&input_caps);
  if (!priv->missing_element && !priv->construction_error) {
    gst_alpha_decode_bin_set_construction_error (self,
        "Failed to construct alpha decoder pipeline for an unknown reason");
  }
  gst_alpha_decode_bin_clear_children (self, FALSE);
  return FALSE;
}

static gboolean
gst_alpha_decode_bin_open (GstAlphaDecodeBin * self)
{
  GstAlphaDecodeBinPrivate *priv =
      gst_alpha_decode_bin_get_instance_private (self);

  if (!gst_alpha_decode_bin_construct_pipeline (self)) {
    if (priv->missing_element) {
      gst_element_post_message (GST_ELEMENT (self),
          gst_alpha_decode_bin_missing_element_message_new (GST_ELEMENT (self),
              priv->missing_element));
      GST_ELEMENT_ERROR (self, CORE, MISSING_PLUGIN,
          ("Missing element '%s'", priv->missing_element), (NULL));
    } else if (priv->construction_error) {
      GST_ELEMENT_ERROR (self, CORE, FAILED,
          ("Failed to construct alpha decoder pipeline."),
          ("%s", priv->construction_error));
    } else {
      GST_ELEMENT_ERROR (self, CORE, FAILED,
          ("Failed to construct alpha decoder pipeline."), (NULL));
    }
  }

  return priv->constructed;
}

static GstStateChangeReturn
gst_alpha_decode_bin_change_state (GstElement * element,
    GstStateChange transition)
{
  GstAlphaDecodeBin *self = GST_ALPHA_DECODE_BIN (element);
  GstStateChangeReturn ret;

  if (transition == GST_STATE_CHANGE_NULL_TO_READY
      && !gst_alpha_decode_bin_open (self))
    return GST_STATE_CHANGE_FAILURE;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (transition == GST_STATE_CHANGE_READY_TO_NULL
      && ret != GST_STATE_CHANGE_FAILURE)
    gst_alpha_decode_bin_clear_children (self, TRUE);

  return ret;
}

static void
gst_alpha_decode_bin_constructed (GObject * object)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (object);
  GstPad *src_gpad, *sink_gpad;

  G_OBJECT_CLASS (parent_class)->constructed (object);

  sink_gpad = gst_ghost_pad_new_no_target_from_template ("sink",
      gst_element_class_get_pad_template (klass, "sink"));
  gst_element_add_pad (GST_ELEMENT (object), sink_gpad);

  src_gpad = gst_ghost_pad_new_no_target_from_template ("src",
      gst_element_class_get_pad_template (klass, "src"));
  gst_element_add_pad (GST_ELEMENT (object), src_gpad);
}

static void
gst_alpha_decode_bin_finalize (GObject * object)
{
  GstAlphaDecodeBin *self = GST_ALPHA_DECODE_BIN (object);

  gst_alpha_decode_bin_clear_children (self, TRUE);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_alpha_decode_bin_class_init (GstAlphaDecodeBinClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;
  GObjectClass *obj_class = (GObjectClass *) klass;

  class_data_quark = g_quark_from_static_string ("GstAlphaDecodeBinClassData");

  obj_class->constructed = gst_alpha_decode_bin_constructed;
  obj_class->finalize = gst_alpha_decode_bin_finalize;

  gst_element_class_add_static_pad_template (element_class,
      &gst_alpha_decode_bin_src_template);
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_alpha_decode_bin_change_state);
}

static void
gst_alpha_decode_bin_init (GstAlphaDecodeBin * self)
{
}
