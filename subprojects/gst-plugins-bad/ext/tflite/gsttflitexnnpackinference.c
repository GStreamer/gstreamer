/*
 * GStreamer
 * Copyright (C) 2026 Collabora Ltd.
 *
 * gsttflitexnnpackinference.c
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
 * SECTION:element-tflitexnnpackinference
 * @short_description: Run TFLITE inference model on video buffers
 * using XNNPACK CPU optimized delegate.
 *
 * This element can apply an TFLITE model to video buffers. It attaches
 * the tensor output to the buffer as a @ref GstTensorMeta.
 *
 * ## Example launch command:
 * |[
 * GST_DEBUG=ssdtensordec:5 \
 * gst-launch-1.0 filesrc location=tflite-models/images/bus.jpg ! \
 *   jpegdec ! videoconvert ! tflitexnnpackinference model-file=tflite-models/models/ssd_mobilenet_v1_coco.tflite  !  \
 *   ssdtensordec label-file=tflite-models/labels/COCO_classes.txt  ! videoconvert ! imagefreeze ! autovideosink
 * ]|
 *
 * Since: 1.30
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsttflitexnnpackinference.h"
#include "tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h"

enum
{
  PROP_0,
  PROP_THREADS,
};

struct _GstTFliteXnnpackInference
{
  GstTFliteInference parent;
  TfLiteDelegate *xnnpack_delegate;
  gint n_threads;
};

GST_DEBUG_CATEGORY (tflite_xnnpack_inference_debug);
#define GST_CAT_DEFAULT tflite_xnnpack_inference_debug

GST_ELEMENT_REGISTER_DEFINE (tflite_xnnpack_inference,
    "tflitexnnpackinference", GST_RANK_NONE, GST_TYPE_TFLITE_XNNPACK_INFERENCE);

G_DEFINE_TYPE (GstTFliteXnnpackInference, gst_tflite_xnnpack_inference,
    GST_TYPE_TFLITE_INFERENCE);
#define parent_class gst_tflite_xnnpack_inference_parent_class

static void
gst_tflite_xnnpack_inference_init (GstTFliteXnnpackInference * self)
{
}

static void
gst_tflite_xnnpack_inference_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTFliteXnnpackInference *self = GST_TFLITE_XNNPACK_INFERENCE (object);

  switch (prop_id) {
    case PROP_THREADS:
      self->n_threads = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tflite_xnnpack_inference_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTFliteXnnpackInference *self = GST_TFLITE_XNNPACK_INFERENCE (object);

  switch (prop_id) {
    case PROP_THREADS:
      g_value_set_int (value, self->n_threads);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_tflite_xnnpack_inference_update_options (GstTFliteInference * inf,
    TfLiteInterpreterOptions * interpreter_options)
{
  GstTFliteXnnpackInference *self = GST_TFLITE_XNNPACK_INFERENCE (inf);

  TfLiteXNNPackDelegateOptions xnnpack_opts =
      TfLiteXNNPackDelegateOptionsDefault ();

  xnnpack_opts.num_threads = self->n_threads;

  self->xnnpack_delegate = TfLiteXNNPackDelegateCreate (&xnnpack_opts);
  if (self->xnnpack_delegate) {
    TfLiteInterpreterOptionsAddDelegate (interpreter_options,
        self->xnnpack_delegate);
    GST_INFO_OBJECT (self, "Created TensorFlow Lite XNNPACK delegate for CPU.");
  }

  return TRUE;
}

static gboolean
gst_tflite_xnnpack_inference_stop (GstBaseTransform * trans)
{
  GstTFliteXnnpackInference *self = GST_TFLITE_XNNPACK_INFERENCE (trans);

  if (self->xnnpack_delegate)
    TfLiteXNNPackDelegateDelete (self->xnnpack_delegate);
  self->xnnpack_delegate = NULL;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (trans);
}

static void
gst_tflite_xnnpack_inference_class_init (GstTFliteXnnpackInferenceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;
  GstTFliteInferenceClass *tflite_class = (GstTFliteInferenceClass *) klass;

  GST_DEBUG_CATEGORY_INIT (tflite_xnnpack_inference_debug,
      "tflitexnnpackinference", 0, "TFLlite XNNPACK inference");

  gst_element_class_set_static_metadata (element_class,
      "tflitexnnpackinference",
      "Filter/Effect",
      "Apply neural network to video frames and create tensor output"
      " using a XNNPACK CPU delegate",
      "Nicolas Dufresne <nicolas.dufresne@collabora.com>");

  gobject_class->set_property = gst_tflite_xnnpack_inference_set_property;
  gobject_class->get_property = gst_tflite_xnnpack_inference_get_property;

  tflite_class->update_options =
      GST_DEBUG_FUNCPTR (gst_tflite_xnnpack_inference_update_options);
  basetransform_class->stop =
      GST_DEBUG_FUNCPTR (gst_tflite_xnnpack_inference_stop);

  g_object_class_override_property (gobject_class, PROP_THREADS, "threads");
}
