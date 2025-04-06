/*
 * GStreamer
 * Copyright (C) 2025 Collabora Ltd.
 *
 * gsttfliteedgetpuinference.c
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
 * SECTION:element-tfliteedgetpuinference
 * @short_description: Run TFLITE inference model on video buffers using a EdgeTpu device
 *
 * This element can apply an TFLITE model to video buffers. It attaches
 * the tensor output to the buffer as a @ref GstTensorMeta.
 *
 * Uses the Google Coral EdgeTpu devices.
 *
 * To install TFLITE on your system, follow the instructions in the
 * README.md in with this plugin.
 *
 * ## Example launch command:
 *
 * GST_DEBUG=ssdobjectdetector:5 \
 * gst-launch-1.0 filesrc location=tflite-models/images/bus.jpg ! \
 * jpegdec ! videoconvert ! tfliteedgetpuinference model-file=tflite-models/models/ssd_mobilenet_v1_coco.tflite !  \
 * ssdobjectdetector label-file=tflite-models/labels/COCO_classes.txt  ! videoconvert ! imagefreeze ! autovideosink
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsttfliteedgetpuinference.h"
#include <libedgetpu/edgetpu_c.h>


typedef struct _GstTFliteEdgeTpuInference
{
  GstTFliteInference parent;

  TfLiteDelegate *tflite_delegate;
} GstTFliteEdgeTpuInference;

GST_DEBUG_CATEGORY (tflite_edgetpu_inference_debug);
#define GST_CAT_DEFAULT tflite_edgetpu_inference_debug

GST_ELEMENT_REGISTER_DEFINE (tflite_edgetpu_inference,
    "tfliteedgetpuinference", GST_RANK_NONE, GST_TYPE_TFLITE_EDGETPU_INFERENCE);


static gboolean gst_tflite_edgetpu_update_options (GstTFliteInference * inf,
    TfLiteInterpreterOptions * interpreter_options);
static gboolean gst_tflite_edgetpu_inference_stop (GstBaseTransform * trans);

G_DEFINE_TYPE (GstTFliteEdgeTpuInference, gst_tflite_edgetpu_inference,
    GST_TYPE_TFLITE_INFERENCE);

static void
gst_tflite_edgetpu_inference_class_init (GstTFliteEdgeTpuInferenceClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;
  GstTFliteInferenceClass *tflite_class = (GstTFliteInferenceClass *) klass;

  GST_DEBUG_CATEGORY_INIT (tflite_edgetpu_inference_debug,
      "tfliteedgetpuinference", 0, "TFLlite edgetpu inference");

  gst_element_class_set_static_metadata (element_class,
      "tfliteedgetpuinference",
      "Filter/Effect",
      "Apply neural network to video frames and create tensor output"
      " using the Google Edge TPU",
      "Olivier Crête <olivier.crete@collabora.com>");

  basetransform_class->stop = gst_tflite_edgetpu_inference_stop;

  tflite_class->update_options = gst_tflite_edgetpu_update_options;
}

static void
gst_tflite_edgetpu_inference_init (GstTFliteEdgeTpuInference * self)
{
}

static gboolean
gst_tflite_edgetpu_update_options (GstTFliteInference * inf,
    TfLiteInterpreterOptions * interpreter_options)
{
  GstTFliteEdgeTpuInference *self = GST_TFLITE_EDGETPU_INFERENCE (inf);
  size_t num_devices = 0;
  struct edgetpu_device *devices;

  devices = edgetpu_list_devices (&num_devices);

  if (num_devices == 0) {
    GST_ERROR_OBJECT (self,
        "Could not create EdgeTPU session because no EdgeTPU"
        " device is connected");
    return FALSE;
  }

  /* Not passing options or a path for now */
  self->tflite_delegate = edgetpu_create_delegate (devices[0].type,
      devices[0].path, NULL, 0);

  if (self->tflite_delegate == NULL) {
    GST_ERROR_OBJECT (self, "Could not create EdgeTPU session");
    edgetpu_free_devices (devices);
    return FALSE;
  }

  const gchar *dev_type_str = "";
  switch (devices[0].type) {
    case EDGETPU_APEX_PCI:
      dev_type_str = "PCIe";
      break;
    case EDGETPU_APEX_USB:
      dev_type_str = "USB";
      break;
    default:
      dev_type_str = "unknown";
      break;
  }

  GST_DEBUG ("Using EdgeTPU version %s device of type %s at %s",
      edgetpu_version (), dev_type_str, devices[0].path);

  edgetpu_free_devices (devices);

  if (self->tflite_delegate)
    TfLiteInterpreterOptionsAddDelegate (interpreter_options,
        self->tflite_delegate);

  return TRUE;
}

static gboolean
gst_tflite_edgetpu_inference_stop (GstBaseTransform * trans)
{
  GstTFliteEdgeTpuInference *self = GST_TFLITE_EDGETPU_INFERENCE (trans);
  gboolean ret;

  ret = GST_BASE_TRANSFORM_CLASS (gst_tflite_edgetpu_inference_parent_class)
      ->stop (trans);

  if (self->tflite_delegate)
    edgetpu_free_delegate (self->tflite_delegate);
  self->tflite_delegate = NULL;

  return ret;
}
