/*
 * GStreamer
 * Copyright (C) 2025 Collabora Ltd.
 *
 * gsttflitevsiinference.c
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
 * SECTION:element-tflitevsiinference
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
 * GST_DEBUG=ssdobjectdetector:5 \
 * gst-launch-1.0 filesrc location=tflite-models/images/bus.jpg ! \
 * jpegdec ! videoconvert ! tflitevsiinference model-file=tflite-models/models/ssd_mobilenet_v1_coco.tflite !  \
 * ssdobjectdetector label-file=tflite-models/labels/COCO_classes.txt  ! videoconvert ! imagefreeze ! autovideosink
 *
 * Since: 1.28
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsttflitevsiinference.h"

#include "VX/vsi_npu_custom_op.h"

#include <tensorflow/lite/delegates/external/external_delegate.h>

typedef struct _GstTFliteVsiInference
{
  GstTFliteInference parent;

  gchar *delegate_path;

  TfLiteDelegate *tflite_delegate;
} GstTFliteVsiInference;

GST_DEBUG_CATEGORY (tflite_vsi_inference_debug);
#define GST_CAT_DEFAULT tflite_vsi_inference_debug

GST_ELEMENT_REGISTER_DEFINE (tflite_vsi_inference,
    "tflitevsiinference", GST_RANK_NONE, GST_TYPE_TFLITE_VSI_INFERENCE);

enum
{
  PROP_0,
  PROP_DELEGATE,
};

#define DEFAULT_DELEGATE_PATH "libvx_delegate.so.2"

static void gst_tflite_vsi_inference_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_tflite_vsi_inference_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_tflite_vsi_inference_finalize (GObject * object);

static gboolean gst_tflite_vsi_update_options (GstTFliteInference * inf,
    TfLiteInterpreterOptions * interpreter_options);
static gboolean gst_tflite_vsi_inference_stop (GstBaseTransform * trans);

G_DEFINE_TYPE (GstTFliteVsiInference, gst_tflite_vsi_inference,
    GST_TYPE_TFLITE_INFERENCE);

static void
gst_tflite_vsi_inference_class_init (GstTFliteVsiInferenceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;
  GstTFliteInferenceClass *tflite_class = (GstTFliteInferenceClass *) klass;

  GST_DEBUG_CATEGORY_INIT (tflite_vsi_inference_debug,
      "tflitevsiinference", 0, "TFLlite vsi inference");

  gst_element_class_set_static_metadata (element_class,
      "tflitevsiinference",
      "Filter/Effect",
      "Apply neural network to video frames and create tensor output"
      " using a Verisilicon accelerator",
      "Olivier Crête <olivier.crete@collabora.com>");

  gobject_class->set_property = gst_tflite_vsi_inference_set_property;
  gobject_class->get_property = gst_tflite_vsi_inference_get_property;
  gobject_class->finalize = gst_tflite_vsi_inference_finalize;
  basetransform_class->stop = gst_tflite_vsi_inference_stop;
  tflite_class->update_options = gst_tflite_vsi_update_options;

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_DELEGATE,
      g_param_spec_string ("delegate",
          "TfLite Delegate", "Path to the VSI TfLite delegate library",
          DEFAULT_DELEGATE_PATH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_tflite_vsi_inference_init (GstTFliteVsiInference * self)
{
  self->delegate_path = g_strdup (DEFAULT_DELEGATE_PATH);
}

static void
gst_tflite_vsi_inference_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTFliteVsiInference *self = GST_TFLITE_VSI_INFERENCE (object);

  switch (prop_id) {
    case PROP_DELEGATE:
      g_free (self->delegate_path);
      self->delegate_path = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tflite_vsi_inference_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTFliteVsiInference *self = GST_TFLITE_VSI_INFERENCE (object);

  switch (prop_id) {
    case PROP_DELEGATE:
      g_value_set_string (value, self->delegate_path);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tflite_vsi_inference_finalize (GObject * object)
{
  GstTFliteVsiInference *self = GST_TFLITE_VSI_INFERENCE (object);

  g_free (self->delegate_path);

  G_OBJECT_CLASS (gst_tflite_vsi_inference_parent_class)->finalize (object);
}

static gboolean
gst_tflite_vsi_update_options (GstTFliteInference * inf,
    TfLiteInterpreterOptions * interpreter_options)
{
  GstTFliteVsiInference *self = GST_TFLITE_VSI_INFERENCE (inf);
  TfLiteExternalDelegateOptions external_delegate_options;

  external_delegate_options =
      TfLiteExternalDelegateOptionsDefault (self->delegate_path);

  self->tflite_delegate =
      TfLiteExternalDelegateCreate (&external_delegate_options);

  TfLiteInterpreterOptionsAddDelegate (interpreter_options,
      self->tflite_delegate);

  TfLiteInterpreterOptionsAddRegistrationExternal (interpreter_options,
      (TfLiteRegistrationExternal *) Register_VSI_NPU_PRECOMPILED ());

  return TRUE;
}

static gboolean
gst_tflite_vsi_inference_stop (GstBaseTransform * trans)
{
  GstTFliteVsiInference *self = GST_TFLITE_VSI_INFERENCE (trans);
  gboolean ret;

  ret = GST_BASE_TRANSFORM_CLASS (gst_tflite_vsi_inference_parent_class)
      ->stop (trans);

  if (self->tflite_delegate)
    TfLiteExternalDelegateDelete (self->tflite_delegate);
  self->tflite_delegate = NULL;

  return ret;
}
