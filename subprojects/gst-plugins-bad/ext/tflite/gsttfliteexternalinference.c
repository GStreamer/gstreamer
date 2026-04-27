/*
 * GStreamer
 * Copyright (C) 2026 Collabora Ltd.
 *
 * gsttfliteexternalinference.c
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
 * SECTION:element-tfliteexternalinference
 * @short_description: Run TFLITE inference model on video buffers
 * using a Verisilicon accelerator
 *
 * This element can apply an TFLITE model to video buffers. It attaches
 * the tensor output to the buffer as a @ref GstTensorMeta.
 *
 * To install TFLITE on your system, follow the instructions in the
 * README.md in with this plugin.
 *
 * ## Example launch command:
 *
 * GST_DEBUG=ssdtensordec:5 \
 * gst-launch-1.0 filesrc location=tflite-models/images/bus.jpg ! \
 * jpegdec ! videoconvert ! tfliteexternalinference delegate-library=libteflon.so model-file=tflite-models/models/ssd_mobilenet_v1_coco.tflite !  \
 * ssdtensordec label-file=tflite-models/labels/COCO_classes.txt  ! videoconvert ! imagefreeze ! autovideosink
 *
 * Since: 1.30
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsttfliteexternalinference.h"

#include <gmodule.h>

enum
{
  PROP_0,
  PROP_DELEGATE_LIBRARY,
  PROP_DELEGATE_OPTIONS,
};

struct _GstTFliteExternalInference
{
  GstTFliteInference parent;

  GModule *module;
  TfLiteDelegate *(*plugin_create) (const char *const *options_keys,
      const char *const *options_values, size_t num_options,
      void (*report_error) (const char *));
  void (*plugin_destroy) (TfLiteDelegate * delegate);

  TfLiteDelegate *external_delegate;
  gchar *delegate_library;
  GstStructure *delegate_options;
};

GST_DEBUG_CATEGORY (tflite_external_inference_debug);
#define GST_CAT_DEFAULT tflite_external_inference_debug

GST_ELEMENT_REGISTER_DEFINE (tflite_external_inference,
    "tfliteexternalinference", GST_RANK_NONE,
    GST_TYPE_TFLITE_EXTERNAL_INFERENCE);

G_DEFINE_TYPE (GstTFliteExternalInference, gst_tflite_external_inference,
    GST_TYPE_TFLITE_INFERENCE);
#define parent_class gst_tflite_external_inference_parent_class

static void
gst_tflite_external_inference_init (GstTFliteExternalInference * self)
{
}

static void
gst_tflite_external_inference_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTFliteExternalInference *self = GST_TFLITE_EXTERNAL_INFERENCE (object);

  switch (prop_id) {
    case PROP_DELEGATE_LIBRARY:
      g_free (self->delegate_library);
      self->delegate_library = g_value_dup_string (value);
      break;
    case PROP_DELEGATE_OPTIONS:
    {
      const GstStructure *s = gst_value_get_structure (value);
      if (self->delegate_options)
        gst_structure_free (self->delegate_options);
      self->delegate_options = s ? gst_structure_copy (s) : NULL;
      break;
    }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tflite_external_inference_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTFliteExternalInference *self = GST_TFLITE_EXTERNAL_INFERENCE (object);

  switch (prop_id) {
    case PROP_DELEGATE_LIBRARY:
      g_value_set_string (value, self->delegate_library);
      break;
    case PROP_DELEGATE_OPTIONS:
      gst_value_set_structure (value, self->delegate_options);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tflite_external_inference_finalize (GObject * object)
{
  GstTFliteExternalInference *self = GST_TFLITE_EXTERNAL_INFERENCE (object);

  g_free (self->delegate_library);
  if (self->delegate_options)
    gst_structure_free (self->delegate_options);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_tflite_external_inference_update_options (GstTFliteInference * inf,
    TfLiteInterpreterOptions * interpreter_options)
{
  GstTFliteExternalInference *self = GST_TFLITE_EXTERNAL_INFERENCE (inf);

  if (!self->delegate_library) {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS,
        (NULL), ("A TFLite external delegate must be specifified."));
    return FALSE;
  }

  self->module = g_module_open (self->delegate_library, G_MODULE_BIND_LAZY);
  if (!self->module) {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        (NULL), ("Failed to load TFLite external delegate '%s': %s",
            self->delegate_library, g_module_error ()));
    return FALSE;
  }

  if (!g_module_symbol (self->module, "tflite_plugin_create_delegate",
          (gpointer *) & self->plugin_create)
      || !g_module_symbol (self->module, "tflite_plugin_destroy_delegate",
          (gpointer *) & self->plugin_destroy)) {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        (NULL), ("Delegate '%s' is missing required symbols: %s",
            self->delegate_library, g_module_error ()));
    g_module_close (self->module);
    self->module = NULL;
    return FALSE;
  }

  GPtrArray *keys = g_ptr_array_new ();
  GPtrArray *values = g_ptr_array_new ();

  if (self->delegate_options) {
    gint n = gst_structure_n_fields (self->delegate_options);
    for (gint i = 0; i < n; i++) {
      const gchar *key =
          gst_structure_nth_field_name (self->delegate_options, i);
      const gchar *val = gst_structure_get_string (self->delegate_options, key);
      if (val) {
        g_ptr_array_add (keys, (gpointer) key);
        g_ptr_array_add (values, (gpointer) val);
        GST_DEBUG_OBJECT (self, "External delegate option: %s=%s", key, val);
      } else {
        GST_WARNING_OBJECT (self,
            "Ignoring non-string delegate option '%s'", key);
      }
    }
  }

  self->external_delegate = self->plugin_create (
      (const char *const *) keys->pdata,
      (const char *const *) values->pdata, keys->len, NULL);

  g_ptr_array_unref (keys);
  g_ptr_array_unref (values);

  if (!self->external_delegate) {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        (NULL), ("Failed to create TFLite external delegate '%s'",
            self->delegate_library));
    g_module_close (self->module);
    self->module = NULL;
    return FALSE;
  }

  TfLiteInterpreterOptionsAddDelegate (interpreter_options,
      self->external_delegate);
  GST_INFO_OBJECT (self,
      "Created TensorFlow Lite external delegate from '%s'.",
      self->delegate_library);

  return TRUE;
}

static gboolean
gst_tflite_external_inference_stop (GstBaseTransform * trans)
{
  GstTFliteExternalInference *self = GST_TFLITE_EXTERNAL_INFERENCE (trans);
  gboolean ret;

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);

  if (self->external_delegate && self->plugin_destroy)
    self->plugin_destroy (self->external_delegate);
  self->external_delegate = NULL;

  if (self->module)
    g_module_close (self->module);
  self->module = NULL;
  self->plugin_create = NULL;
  self->plugin_destroy = NULL;

  return ret;
}

static void
gst_tflite_external_inference_class_init (GstTFliteExternalInferenceClass *
    klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;
  GstTFliteInferenceClass *tflite_class = (GstTFliteInferenceClass *) klass;

  GST_DEBUG_CATEGORY_INIT (tflite_external_inference_debug,
      "tfliteexternalinference", 0, "TFLlite external inference");

  gst_element_class_set_static_metadata (element_class,
      "tfliteexternalinference",
      "Filter/Effect",
      "Apply neural network to video frames and create tensor output"
      " using an external delegate plugin",
      "Nicolas Dufresne <nicolas.dufresne@collabora.com>");

  gobject_class->set_property = gst_tflite_external_inference_set_property;
  gobject_class->get_property = gst_tflite_external_inference_get_property;
  gobject_class->finalize = gst_tflite_external_inference_finalize;
  basetransform_class->stop =
      GST_DEBUG_FUNCPTR (gst_tflite_external_inference_stop);
  tflite_class->update_options =
      GST_DEBUG_FUNCPTR (gst_tflite_external_inference_update_options);

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_DELEGATE_LIBRARY,
      g_param_spec_string ("delegate-library",
          "External Delegate Library",
          "Shared library implementing the TFLite external delegate to load",
          NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_DELEGATE_OPTIONS,
      g_param_spec_boxed ("delegate-options",
          "External Delegate Options",
          "GstStructure of string key/value pairs passed as options to"
          " the external delegate, e.g."
          " delegate-options=\"tflite-delegate, key1=value1, key2=value2;\"",
          GST_TYPE_STRUCTURE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}
