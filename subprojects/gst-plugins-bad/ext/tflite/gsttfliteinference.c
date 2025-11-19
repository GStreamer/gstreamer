/*
 * GStreamer
 * Copyright (C) 2024 Collabora Ltd.
 *
 * gsttfliteinference.c
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
 * SECTION:element-tfliteinference
 * @short_description: Run TFLITE inference model on video buffers
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
 * jpegdec ! videoconvert ! tfliteinference model-file=tflite-models/models/ssd_mobilenet_v1_coco.tflite !  \
 * ssdobjectdetector label-file=tflite-models/labels/COCO_classes.txt  ! videoconvert ! imagefreeze ! autovideosink
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gsttfliteinference.h"
#include "modelinfo.h"

#include <tensorflow/lite/c/common.h>

#define DEFAULT_MODEL_FILE              ""
#define DEFAULT_THREADS                 0

/*
 * GstTFliteInference:
 *
 * @model_file model file
 * @tflite_client opaque pointer to TFLITE client
 * @tflite_disabled true if inference is disabled
 * @video_info @ref GstVideoInfo of sink caps
 */
typedef struct _GstTFliteInferencePrivate
{
  GstBaseTransform basetransform;
  gchar *model_file;
  gsize numberOfThreads;
  gchar *vxdelegate;
  gboolean planar;
  GPtrArray *tensor_templates;

  TfLiteInterpreter *interpreter;
  TfLiteInterpreterOptions *interpreter_options;
  TfLiteModel *model;
  gboolean tflite_disabled;
  GstVideoInfo video_info;
  guint8 *dest;

  GstCaps *model_caps;

  gint channels;
  gdouble *means;
  gdouble *stddevs;

} GstTFliteInferencePrivate;

GST_DEBUG_CATEGORY (tflite_inference_debug);

#define GST_CAT_DEFAULT tflite_inference_debug
GST_ELEMENT_REGISTER_DEFINE (tflite_inference, "tfliteinference",
    GST_RANK_NONE, GST_TYPE_TFLITE_INFERENCE);

/* GstTFliteInference properties */
enum
{
  PROP_0,
  PROP_MODEL_FILE,
  PROP_THREADS,
};

#define VIDEO_CAPS GST_VIDEO_CAPS_MAKE ("{ RGB, RGBA, BGR, BGRA }")

static GstStaticPadTemplate gst_tflite_inference_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS)
    );

static GstStaticPadTemplate gst_tflite_inference_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS)
    );

static gboolean gst_tflite_inference_start (GstBaseTransform * trans);
static gboolean gst_tflite_inference_stop (GstBaseTransform * trans);

static void gst_tflite_inference_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_tflite_inference_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_tflite_inference_finalize (GObject * object);
static GstFlowReturn gst_tflite_inference_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);
static gboolean gst_tflite_inference_process (GstBaseTransform * trans,
    GstBuffer * buf);
static GstCaps *gst_tflite_inference_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps);
static gboolean
gst_tflite_inference_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps);

G_DEFINE_TYPE_WITH_PRIVATE (GstTFliteInference, gst_tflite_inference,
    GST_TYPE_BASE_TRANSFORM);

static void
gst_tflite_inference_class_init (GstTFliteInferenceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  GST_DEBUG_CATEGORY_INIT (tflite_inference_debug, "tfliteinference",
      0, "tflite_inference");
  gobject_class->set_property = gst_tflite_inference_set_property;
  gobject_class->get_property = gst_tflite_inference_get_property;
  gobject_class->finalize = gst_tflite_inference_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_MODEL_FILE,
      g_param_spec_string ("model-file",
          "TFLITE model file", "TFLITE model file", DEFAULT_MODEL_FILE,
          (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_THREADS,
      g_param_spec_int ("threads",
          "Number of Threads",
          "Set the number of threads to be used by the TFLITE inference (-1 for auto)",
          -1, G_MAXINT, DEFAULT_THREADS,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));


  gst_element_class_set_static_metadata (element_class, "tfliteinference",
      "Filter/Video",
      "Apply neural network to video frames and create tensor output",
      "Denis Shimizu <denis.shimizu@collabora.com>, "
      "Aaron Boxer <aaron.boxer@collabora.com>,"
      "Daniel Morin <daniel.morin@collabora.com>");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_tflite_inference_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_tflite_inference_src_template));
  basetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_tflite_inference_transform_ip);
  basetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_tflite_inference_transform_caps);
  basetransform_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_tflite_inference_set_caps);
  basetransform_class->start = GST_DEBUG_FUNCPTR (gst_tflite_inference_start);
  basetransform_class->stop = GST_DEBUG_FUNCPTR (gst_tflite_inference_stop);
}

static gboolean
gst_tflite_inference_has_session (GstTFliteInference * self)
{
  GstTFliteInferencePrivate *priv =
      gst_tflite_inference_get_instance_private (self);

  return priv->interpreter != NULL;
}

static void
gst_tflite_inference_init (GstTFliteInference * self)
{
  GstTFliteInferencePrivate *priv =
      gst_tflite_inference_get_instance_private (self);

  priv->numberOfThreads = DEFAULT_THREADS;
  priv->tensor_templates = g_ptr_array_new_with_free_func ((GDestroyNotify)
      gst_tensor_free);
  priv->tflite_disabled = TRUE;
}

static void
gst_tflite_inference_finalize (GObject * object)
{
  GstTFliteInference *self = GST_TFLITE_INFERENCE (object);
  GstTFliteInferencePrivate *priv =
      gst_tflite_inference_get_instance_private (self);

  g_free (priv->model_file);
  g_ptr_array_unref (priv->tensor_templates);
  G_OBJECT_CLASS (gst_tflite_inference_parent_class)->finalize (object);
}

static void
gst_tflite_inference_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTFliteInference *self = GST_TFLITE_INFERENCE (object);
  GstTFliteInferencePrivate *priv =
      gst_tflite_inference_get_instance_private (self);
  const gchar *filename;

  switch (prop_id) {
    case PROP_MODEL_FILE:
      filename = g_value_get_string (value);
      if (filename
          && g_file_test (filename,
              (GFileTest) (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR))) {
        if (priv->model_file)
          g_free (priv->model_file);
        priv->model_file = g_strdup (filename);
        priv->tflite_disabled = FALSE;
      } else {
        GST_WARNING_OBJECT (self, "Model file '%s' not found!", filename);
      }
      break;
    case PROP_THREADS:
      priv->numberOfThreads = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tflite_inference_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTFliteInference *self = GST_TFLITE_INFERENCE (object);
  GstTFliteInferencePrivate *priv =
      gst_tflite_inference_get_instance_private (self);

  switch (prop_id) {
    case PROP_MODEL_FILE:
      g_value_set_string (value, priv->model_file);
      break;
    case PROP_THREADS:
      g_value_set_int (value, priv->numberOfThreads);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstTensorDataType
gst_tflite_convert_data_type (TfLiteType type)
{
  switch (type) {
    case kTfLiteFloat32:
      return GST_TENSOR_DATA_TYPE_FLOAT32;
    case kTfLiteInt32:
      return GST_TENSOR_DATA_TYPE_INT32;
    case kTfLiteUInt8:
      return GST_TENSOR_DATA_TYPE_UINT8;
    case kTfLiteInt64:
      return GST_TENSOR_DATA_TYPE_INT64;
    case kTfLiteInt16:
      return GST_TENSOR_DATA_TYPE_INT16;
    case kTfLiteInt8:
      return GST_TENSOR_DATA_TYPE_INT8;
    case kTfLiteFloat16:
      return GST_TENSOR_DATA_TYPE_FLOAT16;
    case kTfLiteFloat64:
      return GST_TENSOR_DATA_TYPE_FLOAT64;
    case kTfLiteUInt64:
      return GST_TENSOR_DATA_TYPE_UINT64;
    case kTfLiteUInt32:
      return GST_TENSOR_DATA_TYPE_UINT32;
    case kTfLiteUInt16:
      return GST_TENSOR_DATA_TYPE_UINT16;
    case kTfLiteInt4:
      return GST_TENSOR_DATA_TYPE_INT4;
#ifdef TFLITE_HAS_BFLOAT16
    case kTfLiteBFloat16:
      return GST_TENSOR_DATA_TYPE_BFLOAT16;
#endif

    default:
      GST_FIXME ("GstTensorDataType currently does not have a mapping \
          for this type.");
      g_assert_not_reached ();
  }
}

static gboolean
convert_tensor_info (const TfLiteTensor * tflite_tensor,
    const gchar ** tname, GstTensorDataType * data_type,
    gsize * dims_count, gsize ** out_dims)
{
  gsize j;
  gsize *dims;

  if (tname)
    *tname = TfLiteTensorName (tflite_tensor);
  *dims_count = TfLiteTensorNumDims (tflite_tensor);

  if (*dims_count == 0)
    return FALSE;

  dims = *out_dims = (gsize *) g_malloc0_n (*dims_count, sizeof (gsize));

  if (tflite_tensor->dims_signature && tflite_tensor->dims_signature->size) {
    for (j = 0; j < *dims_count; j++) {
      if (tflite_tensor->dims_signature->data[j] < 0)
        dims[j] = G_MAXSIZE;
      else
        dims[j] = tflite_tensor->dims_signature->data[j];
    }
  } else {
    for (j = 0; j < *dims_count; j++)
      dims[j] = TfLiteTensorDim (tflite_tensor, j);
  }

  *data_type = gst_tflite_convert_data_type (TfLiteTensorType (tflite_tensor));

  return TRUE;
}

static gchar *
build_dims_str (gsize dims_count, gsize * dims)
{
  GString *dims_gstr = g_string_new ("");
  gsize j;

  if (dims_count == 0)
    goto done;


  if (dims[0] == G_MAXSIZE)
    g_string_append (dims_gstr, "-1");
  else
    g_string_append_printf (dims_gstr, "%zu", dims[0]);

  for (j = 1; j < dims_count; j++)
    if (dims[j] == G_MAXSIZE)
      g_string_append (dims_gstr, ",-1");
    else
      g_string_append_printf (dims_gstr, ",%zu", dims[j]);

done:
  return g_string_free (dims_gstr, FALSE);
}

static gboolean
_guess_tensor_data_type (GstTFliteInference * self, gsize dims_count,
    gsize * dims, const gchar ** gst_format, gint * width, gint * height,
    gint * channels, gboolean * planar)
{
  if (dims_count < 2 || dims_count > 4) {
    GST_ERROR_OBJECT (self,
        "Don't know how to interpret tensors with %zu dimensions", dims_count);
    return FALSE;
  }

  *planar = FALSE;

  switch (dims_count) {
    case 2:
      *gst_format = "GRAY8";
      *height = dims[0];
      *width = dims[1];
      break;
    case 3:
      if (dims[0] == 1 || dims[0] == 3) {
        *channels = dims[0];
        if (dims[0] == 1) {
          *gst_format = "GRAY8";
        } else {
          *gst_format = "RGBP";
          *planar = TRUE;
        }
        *height = dims[1];
        *width = dims[2];
      } else if (dims[2] == 1 || dims[2] == 3) {
        *channels = dims[2];
        if (dims[2] == 1)
          *gst_format = "GRAY";
        else
          *gst_format = "RGB";
        *height = dims[0];
        *width = dims[1];
      } else {
        GST_ERROR_OBJECT (self, "Don't know how to interpret dims");
        return FALSE;
      }
      break;
    case 4:
      /* Assuming dims[0] is a batch */
      if (dims[1] == 1 || dims[1] == 3) {
        *channels = dims[1];
        *planar = TRUE;
        *height = dims[2];
        *width = dims[3];
      } else if (dims[3] == 1 || dims[3] == 3) {
        *channels = dims[3];
        *height = dims[1];
        *width = dims[2];
      } else {
        GST_ERROR_OBJECT (self, "Don't know how to interpret dims");
        return FALSE;
      }

      if (*channels == 1) {
        *gst_format = "GRAY8";
        *planar = FALSE;
      } else if (*channels == 3) {
        if (*planar)
          *gst_format = "RGBP";
        else
          *gst_format = "RGB";
      } else {
        g_assert_not_reached ();
      }
      break;
  }

  return TRUE;
}

static gboolean
_get_input_params (GstTFliteInference * self, GstTensorDataType * data_type,
    gint * width, gint * height, const gchar ** gst_format,
    gint * channels, gboolean * planar)
{
  GstTFliteInferencePrivate *priv =
      gst_tflite_inference_get_instance_private (self);
  const TfLiteTensor *input_tensor;
  gint i_size = TfLiteInterpreterGetInputTensorCount (priv->interpreter);
  gsize dims_count;
  gsize *dims = NULL;
  gboolean ret;

  if (i_size != 1) {
    GST_ERROR_OBJECT (self, "Currently only support model with a single"
        " input tensor, but model has %d", i_size);
    return FALSE;
  }

  input_tensor = TfLiteInterpreterGetInputTensor (priv->interpreter, 0);
  if (convert_tensor_info (input_tensor, NULL, data_type, &dims_count, &dims)) {
    ret = _guess_tensor_data_type (self, dims_count, dims, gst_format, width,
        height, channels, planar);
  } else {
    GST_ERROR_OBJECT (self, "Input tensor has no dimensions, rejecting");
    ret = FALSE;
  }
  g_free (dims);

  return ret;
}



static gboolean
gst_tflite_inference_start (GstBaseTransform * trans)
{
  GstTFliteInference *self = GST_TFLITE_INFERENCE (trans);
  GstTFliteInferencePrivate *priv =
      gst_tflite_inference_get_instance_private (self);
  gboolean ret = FALSE;
  ModelInfo *modelinfo = NULL;
  gint i_size, o_size;
  GstTFliteInferenceClass *klass = GST_TFLITE_INFERENCE_GET_CLASS (self);

  GST_OBJECT_LOCK (self);
  if (gst_tflite_inference_has_session (self)) {
    ret = TRUE;
    goto done;
  }

  if (priv->model_file == NULL) {
    GST_ERROR_OBJECT (self, "model-file property not set");
    goto done;
  }

  priv->model = TfLiteModelCreateFromFile (priv->model_file);
  if (!priv->model) {
    GST_ERROR_OBJECT (self, "Failed to mmap model %s", priv->model_file);
    goto error;
  }

  GST_DEBUG_OBJECT (self, "Loaded model %s", priv->model_file);

  priv->interpreter_options = TfLiteInterpreterOptionsCreate ();
  if (priv->numberOfThreads != 0) {
    TfLiteInterpreterOptionsSetNumThreads (priv->interpreter_options,
        priv->numberOfThreads);
  }

  if (klass->update_options)
    if (!klass->update_options (self, priv->interpreter_options))
      goto error;

  priv->interpreter = TfLiteInterpreterCreate (priv->model,
      priv->interpreter_options);
  if (!priv->interpreter) {
    GST_ERROR_OBJECT (self, "Failed to construct interpreter");
    goto error;
  }

  modelinfo = modelinfo_load (priv->model_file);
  if (!modelinfo) {
    GST_ERROR_OBJECT (self, "Can't find modelinfo for %s", priv->model_file);
    goto error;
  }

  i_size = TfLiteInterpreterGetInputTensorCount (priv->interpreter);
  if (i_size != 1) {
    GST_ERROR_OBJECT (self, "Currently only support model with a single"
        " input tensor, but model has %d", i_size);
    goto error;
  }

  {
    const guint i = 0;
    const TfLiteTensor *tflite_tensor =
        TfLiteInterpreterGetInputTensor (priv->interpreter, i);
    const gchar *tname;
    GstTensorDataType data_type;
    gsize dims_count;
    gsize *dims;
    gchar *tensor_name = NULL;
    gint width = 0, height = 0;
    const gchar *gst_format = NULL;
    guint num_means, num_stddevs;

    if (!_get_input_params (self, &data_type, &width, &height, &gst_format,
            &priv->channels, &priv->planar)) {
      GST_ERROR_OBJECT (self, "Failed to get parameters");
      goto error;
    }

    if (!convert_tensor_info (tflite_tensor, &tname, &data_type,
            &dims_count, &dims)) {
      GST_ERROR_OBJECT (self, "Rejecting input_tensor[%d]:%s with no dims",
          i, tname);
      goto error;
    }

    tensor_name = modelinfo_find_tensor_name (modelinfo,
        MODELINFO_DIRECTION_INPUT, i, tname, data_type, dims_count, dims);

    if (tensor_name == NULL) {
      gchar *dims_str = build_dims_str (dims_count, dims);
      GST_DEBUG_OBJECT (self,
          "Model info file doesn't contain info for input_tensor[%u]:%s matching the"
          " type %s and dims %s", i, tname,
          gst_tensor_data_type_get_name (data_type), dims_str);
      g_free (dims);
      g_free (dims_str);
    } else {

      num_means = modelinfo_get_normalization_means (modelinfo,
          tensor_name, priv->channels, &priv->means);
      if (num_means != priv->channels) {
        priv->means = g_renew (gdouble, priv->means, priv->channels);

        /* initialize means array to zeroes */
        if (num_means == 0) {
          priv->means[0] = 0;
        }
        for (guint j = 1; j < priv->channels; j++)
          priv->means[j] = priv->means[0];
      }

      num_stddevs = modelinfo_get_normalization_stddevs (modelinfo,
          tensor_name, priv->channels, &priv->stddevs);
      if (num_stddevs != priv->channels) {
        priv->stddevs = g_renew (gdouble, priv->stddevs, priv->channels);

        /* initialize stddevs array to ones */
        if (num_stddevs == 0) {
          priv->stddevs[0] = 1.0;
        }
        for (guint j = 1; j < priv->channels; j++)
          priv->stddevs[j] = priv->stddevs[0];
      }

    }

    gst_clear_caps (&priv->model_caps);
    priv->model_caps = gst_caps_new_empty_simple ("video/x-raw");
    if (width && height)
      gst_caps_set_simple (priv->model_caps, "width", G_TYPE_INT, width,
          "height", G_TYPE_INT, height, NULL);

    if (data_type == GST_TENSOR_DATA_TYPE_UINT8 && gst_format &&
        priv->means == NULL && priv->stddevs == NULL)
      gst_caps_set_simple (priv->model_caps, "format", G_TYPE_STRING,
          gst_format, NULL);

    g_free (tensor_name);
  }

  if (TfLiteInterpreterAllocateTensors (priv->interpreter) != kTfLiteOk) {
    GST_ERROR_OBJECT (self, "Failed to allocate tensors");
    goto error;
  }

  o_size = TfLiteInterpreterGetOutputTensorCount (priv->interpreter);
  for (guint i = 0; i < o_size; i++) {
    const TfLiteTensor *tflite_tensor =
        TfLiteInterpreterGetOutputTensor (priv->interpreter, i);
    const gchar *tname;
    GstTensorDataType data_type;
    gsize dims_count;
    gsize *dims;
    gchar *tensor_name = NULL;

    if (!convert_tensor_info (tflite_tensor, &tname, &data_type,
            &dims_count, &dims)) {
      GST_WARNING_OBJECT (self, "Skipping output_tensor[%d]:%s with no dims",
          i, tname);
      continue;
    }

    tensor_name = modelinfo_find_tensor_name (modelinfo,
        MODELINFO_DIRECTION_OUTPUT, i, tname, data_type, dims_count, dims);


    gchar *dims_str = build_dims_str (dims_count, dims);
    if (tensor_name == NULL) {
      GST_ERROR_OBJECT (self,
          "Model info file doesn't contain info for output_tensor[%u]:%s matching the"
          " type %s and dims %s", i, tname,
          gst_tensor_data_type_get_name (data_type), dims_str);
      g_free (dims);
      g_free (dims_str);
      g_ptr_array_set_size (priv->tensor_templates, 0);
      goto error;
    }

    GstTensor *t = gst_tensor_alloc (dims_count);

    gchar *id = modelinfo_get_id (modelinfo, tensor_name);
    GST_DEBUG_OBJECT (self, "Mapping output_tensor[%d]:%s of type %s and"
        " dims %s to id %s", i, tname,
        gst_tensor_data_type_get_name (data_type), dims_str, id);
    g_free (id);
    g_free (dims_str);

    t->id = modelinfo_get_quark_id (modelinfo, tensor_name);
    t->layout = GST_TENSOR_LAYOUT_CONTIGUOUS;
    t->data_type = data_type;
    t->dims_order = GST_TENSOR_DIM_ORDER_ROW_MAJOR;
    memcpy (t->dims, dims, sizeof (gsize) * t->num_dims);

    g_free (dims);

    g_ptr_array_add (priv->tensor_templates, t);

    g_free (tensor_name);
  }


  TfLiteTensor *itensor = TfLiteInterpreterGetInputTensor (priv->interpreter,
      0);
  if (TfLiteTensorType (itensor) == kTfLiteFloat32) {
    GST_DEBUG_OBJECT (self, "Floating point Tensorflow Lite Model");
  }

  ret = TRUE;

done:
  if (modelinfo)
    modelinfo_free (modelinfo);

  GST_OBJECT_UNLOCK (self);

  return ret;

error:

  GST_ERROR_OBJECT (self,
      "Unable to create TFLITE session. Inference is disabled.");

  GST_BASE_TRANSFORM_GET_CLASS (self)->stop (trans);

  goto done;
}

static gboolean
gst_tflite_inference_stop (GstBaseTransform * trans)
{
  GstTFliteInference *self = GST_TFLITE_INFERENCE (trans);
  GstTFliteInferencePrivate *priv =
      gst_tflite_inference_get_instance_private (self);

  if (priv->interpreter)
    TfLiteInterpreterDelete (priv->interpreter);
  priv->interpreter = NULL;

  if (priv->interpreter_options)
    TfLiteInterpreterOptionsDelete (priv->interpreter_options);
  priv->interpreter_options = NULL;

  if (priv->model)
    TfLiteModelDelete (priv->model);
  priv->model = NULL;

  gst_clear_caps (&priv->model_caps);

  g_ptr_array_set_size (priv->tensor_templates, 0);

  return TRUE;
}

static GstCaps *
gst_tflite_inference_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps)
{
  GstTFliteInference *self = GST_TFLITE_INFERENCE (trans);
  GstTFliteInferencePrivate *priv =
      gst_tflite_inference_get_instance_private (self);
  GstCaps *other_caps;

  if (priv->model_caps == NULL) {
    other_caps = gst_caps_ref (caps);
    goto done;
  }

  GST_DEBUG_OBJECT (self, "Applying caps restrictions: %" GST_PTR_FORMAT,
      priv->model_caps);

  other_caps = gst_caps_intersect_full (caps, priv->model_caps,
      GST_CAPS_INTERSECT_FIRST);

done:
  if (filter_caps) {
    GstCaps *tmp = gst_caps_intersect_full (other_caps, filter_caps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_replace (&other_caps, tmp);
    gst_caps_unref (tmp);
  }

  return other_caps;
}

static gboolean
gst_tflite_inference_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstTFliteInference *self = GST_TFLITE_INFERENCE (trans);
  GstTFliteInferencePrivate *priv =
      gst_tflite_inference_get_instance_private (self);

  if (!gst_video_info_from_caps (&priv->video_info, incaps)) {
    GST_ERROR_OBJECT (self, "Failed to parse caps");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_tflite_inference_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  if (!gst_base_transform_is_passthrough (trans)
      && !gst_tflite_inference_process (trans, buf)) {
    GST_ELEMENT_ERROR (trans, STREAM, FAILED,
        (NULL), ("TFLITE inference failed"));
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

#define _convert_image_remove_alpha(Type, dst, srcPtr,                        \
    srcSamplesPerPixel, stride, means, stddevs)                                  \
G_STMT_START {                                                                \
  size_t destIndex = 0;                                                       \
  Type tmp;                                                                   \
                                                                              \
  if (!planar) {                                                              \
    for (int32_t j = 0; j < dstHeight; ++j) {                                 \
      for (int32_t i = 0; i < dstWidth; ++i) {                                \
        for (int32_t k = 0; k < dstChannels; ++k) {                           \
          tmp = *srcPtr[k];                                                   \
          tmp -= means[k];                                                    \
          dst[destIndex++] = (Type)(tmp / stddevs[k]);                        \
          srcPtr[k] += srcSamplesPerPixel;                                    \
        }                                                                     \
      }                                                                       \
      /* correct for stride */                                                \
      for (uint32_t k = 0; k < 3; ++k)                                        \
        srcPtr[k] += stride - srcSamplesPerPixel * dstWidth;                  \
    }                                                                         \
  } else {                                                                    \
    size_t frameSize = dstWidth * dstHeight;                                  \
    Type *destPtr[3] = { dst, dst + frameSize, dst + 2 * frameSize };         \
    for (int32_t j = 0; j < dstHeight; ++j) {                                 \
      for (int32_t i = 0; i < dstWidth; ++i) {                                \
        for (int32_t k = 0; k < dstChannels; ++k) {                           \
          tmp = *srcPtr[k];                                                   \
          tmp -= means[k];                                                    \
          destPtr[k][destIndex] = (Type)(tmp / stddevs[k]);                   \
          srcPtr[k] += srcSamplesPerPixel;                                    \
        }                                                                     \
        destIndex++;                                                          \
      }                                                                       \
      /* correct for stride */                                                \
      for (uint32_t k = 0; k < 3; ++k)                                        \
        srcPtr[k] += stride - srcSamplesPerPixel * dstWidth;                  \
    }                                                                         \
  }                                                                           \
}                                                                             \
G_STMT_END;

static void
convert_image_remove_alpha_u8 (guint8 * dst, gint dstWidth, gint dstHeight,
    gint dstChannels, gboolean planar, guint8 ** srcPtr,
    guint8 srcSamplesPerPixel, guint32 stride, const gdouble * means,
    const gdouble * stddevs)
{
  static const gdouble zeros[] = { 0, 0, 0, 0 };
  static const gdouble ones[] = { 1.0, 1.0, 1.0, 1.0 };
  if (means == NULL)
    means = zeros;
  if (stddevs == NULL)
    stddevs = ones;

  _convert_image_remove_alpha (guint8, dst, srcPtr, srcSamplesPerPixel,
      stride, means, stddevs);
}

static void
convert_image_remove_alpha_f32 (gfloat * dst, gint dstWidth, gint dstHeight,
    gint dstChannels, gboolean planar, guint8 ** srcPtr,
    guint8 srcSamplesPerPixel, guint32 stride, const gdouble * means,
    const gdouble * stddevs)
{
  static const gdouble zeros[] = { 0, 0, 0, 0 };
  static const gdouble two_five_fives[] = { 255.0, 255.0, 255.0, 255.0 };
  if (means == NULL)
    means = zeros;
  if (stddevs == NULL)
    stddevs = two_five_fives;

  _convert_image_remove_alpha (gfloat, dst, srcPtr, srcSamplesPerPixel,
      stride, means, stddevs);
}

static gboolean
gst_tflite_inference_process (GstBaseTransform * trans, GstBuffer * buf)
{
  GstTFliteInference *self = GST_TFLITE_INFERENCE (trans);
  GstTFliteInferencePrivate *priv =
      gst_tflite_inference_get_instance_private (self);
  GstMapInfo info;
  guint8 *srcPtr[3];
  gsize srcSamplesPerPixel = 3;
  GstTensorDataType datatype;

  if (gst_buffer_map (buf, &info, GST_MAP_READ)) {

    // <==
    srcPtr[0] = info.data;
    srcPtr[1] = info.data + 1;
    srcPtr[2] = info.data + 2;

    switch (priv->video_info.finfo->format) {
      case GST_VIDEO_FORMAT_RGBA:
        srcSamplesPerPixel = 4;
        break;
      case GST_VIDEO_FORMAT_BGRA:
        srcSamplesPerPixel = 4;
        srcPtr[0] = info.data + 2;
        srcPtr[1] = info.data + 1;
        srcPtr[2] = info.data + 0;
        break;
      case GST_VIDEO_FORMAT_ARGB:
        srcSamplesPerPixel = 4;
        srcPtr[0] = info.data + 1;
        srcPtr[1] = info.data + 2;
        srcPtr[2] = info.data + 3;
        break;
      case GST_VIDEO_FORMAT_ABGR:
        srcSamplesPerPixel = 4;
        srcPtr[0] = info.data + 3;
        srcPtr[1] = info.data + 2;
        srcPtr[2] = info.data + 1;
        break;
      case GST_VIDEO_FORMAT_BGR:
        srcPtr[0] = info.data + 2;
        srcPtr[1] = info.data + 1;
        srcPtr[2] = info.data + 0;
        break;
      default:
        break;
    }

    TfLiteTensor *tensor = TfLiteInterpreterGetInputTensor (priv->interpreter,
        0);

    guint width = GST_VIDEO_INFO_WIDTH (&priv->video_info);
    guint height = GST_VIDEO_INFO_HEIGHT (&priv->video_info);
    guint32 stride = priv->video_info.stride[0];
    guint channels;
    if (GST_VIDEO_INFO_IS_GRAY (&priv->video_info)) {
      channels = 1;
    } else if (GST_VIDEO_INFO_IS_RGB (&priv->video_info)) {
      channels = 3;
    } else {
      g_assert_not_reached ();
    }


    datatype = gst_tflite_convert_data_type (TfLiteTensorType (tensor));
    switch (datatype) {
      case GST_TENSOR_DATA_TYPE_UINT8:{
        uint8_t *dest = (uint8_t *) TfLiteTensorData (tensor);

        if (dest == NULL)
          return false;
        convert_image_remove_alpha_u8 (dest, width, height, channels,
            priv->planar, srcPtr, srcSamplesPerPixel, stride, priv->means,
            priv->stddevs);
        break;
      }
      case GST_TENSOR_DATA_TYPE_FLOAT32:{
        float *dest = (float *) TfLiteTensorData (tensor);

        if (dest == NULL)
          return false;
        convert_image_remove_alpha_f32 (dest, width, height, channels,
            priv->planar, srcPtr, srcSamplesPerPixel, stride, priv->means,
            priv->stddevs);
        break;
      }
      default:{
        GST_ERROR_OBJECT (self, "Data type not handled");
        return false;
      }
        break;
    }

    /* Run inference */
    if (TfLiteInterpreterInvoke (priv->interpreter) != kTfLiteOk) {
      GST_ERROR_OBJECT (self, "Failed to invoke tflite!");
      return false;
    }

    gsize num_tensors =
        TfLiteInterpreterGetOutputTensorCount (priv->interpreter);

    g_assert (num_tensors == priv->tensor_templates->len);
    GstTensor **tensors =
        (GstTensor **) g_malloc0_n (num_tensors, sizeof (gpointer));

    for (size_t i = 0; i < num_tensors; i++) {

      const TfLiteTensor *output_tensor =
          TfLiteInterpreterGetOutputTensor (priv->interpreter, i);

      tensors[i] = gst_tensor_alloc (TfLiteTensorNumDims (output_tensor));
      memcpy (tensors[i], g_ptr_array_index (priv->tensor_templates, i),
          sizeof (GstTensor));
      tensors[i]->num_dims = TfLiteTensorNumDims (output_tensor);

      for (gsize j = 0; j < tensors[i]->num_dims; j++)
        tensors[i]->dims[j] = TfLiteTensorDim (output_tensor, j);;

      tensors[i]->data =
          gst_buffer_new_allocate (NULL, TfLiteTensorByteSize (output_tensor),
          NULL);

      gst_buffer_fill (tensors[i]->data, 0, TfLiteTensorData (output_tensor),
          TfLiteTensorByteSize (output_tensor));
    }

    GstTensorMeta *tmeta = gst_buffer_add_tensor_meta (buf);
    gst_tensor_meta_set (tmeta, num_tensors, tensors);

    if (!tmeta)
      return FALSE;

    GST_TRACE_OBJECT (trans, "Num tensors: %zu", tmeta->num_tensors);
    gst_buffer_unmap (buf, &info);
  }

  return TRUE;
}
