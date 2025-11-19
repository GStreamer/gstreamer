/*
 * GStreamer gstreamer-onnxinference
 * Copyright (C) 2023-2025 Collabora Ltd.
 *
 * gstonnxinference.c
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
 * SECTION:element-onnxinference
 * @short_description: Run ONNX inference model on video buffers
 *
 * This element can apply an ONNX model to video buffers. It attaches
 * the tensor output to the buffer as a @ref GstTensorMeta.
 *
 * To install ONNX on your system, follow the instructions in the
 * README.md in with this plugin.
 *
 * ## Example launch command:
 *
 * Test image file, model file (SSD) and label file can be found here :
 * https://gitlab.collabora.com/gstreamer/onnx-models
 *
 * GST_DEBUG=ssdobjectdetector:5 \
 * gst-launch-1.0 filesrc location=onnx-models/images/bus.jpg ! \
 * jpegdec ! videoconvert ! onnxinference execution-provider=cpu model-file=onnx-models/models/ssd_mobilenet_v1_coco.onnx !  \
 * ssdobjectdetector label-file=onnx-models/labels/COCO_classes.txt  ! videoconvert ! imagefreeze ! autovideosink
 *
 *
 * Note: in order for downstream tensor decoders to correctly parse the tensor
 * data in the GstTensorMeta, meta data must be attached to the ONNX model
 * assigning a unique string id to each output layer. These unique string ids
 * and corresponding GQuark ids are currently stored in the tensor decoder's
 * header file, in this case gstssdobjectdetector.h. If the meta data is absent,
 * the pipeline will fail.
 *
 * As a convenience, there is a python script
 * currently stored at
 * https://gitlab.collabora.com/gstreamer/onnx-models/-/blob/master/scripts/modify_onnx_metadata.py
 * to enable users to easily add and remove meta data from json files. It can also dump
 * the names of all output layers, which can then be used to craft the json meta data file.
 *
 * Since: 1.20
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstonnxinference.h"

#include <gst/gst.h>
#include <gst/analytics/analytics.h>

#include <onnxruntime_c_api.h>

#ifdef HAVE_VSI_NPU
#include <core/providers/vsinpu/vsinpu_provider_factory.h>
#endif

typedef enum
{
  GST_ONNX_OPTIMIZATION_LEVEL_DISABLE_ALL,
  GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_BASIC,
  GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED,
  GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_ALL,
} GstOnnxOptimizationLevel;

typedef enum
{
  GST_ONNX_EXECUTION_PROVIDER_CPU,
  GST_ONNX_EXECUTION_PROVIDER_CUDA,
  GST_ONNX_EXECUTION_PROVIDER_VSI,
} GstOnnxExecutionProvider;

struct _GstOnnxInference
{
  GstBaseTransform basetransform;
  gchar *model_file;
  GstOnnxOptimizationLevel optimization_level;
  GstOnnxExecutionProvider execution_provider;
  GstVideoInfo video_info;
  GstCaps *model_caps;

  OrtEnv *env;
  OrtSession *session;
  OrtMemoryInfo *memory_info;
  OrtAllocator *allocator;
  int32_t width;
  int32_t height;
  int32_t channels;
  gboolean planar;
  gint height_dim;
  gint width_dim;
  gint channels_dim;
  gint batch_dim;
  uint8_t *dest;
  size_t output_count;
  gchar **output_names;
  GQuark *output_ids;
  GstTensorDataType input_data_type;
  bool fixedInputImageSize;
  double *means;
  double *stddevs;
};

static const OrtApi *api = NULL;


GST_DEBUG_CATEGORY (onnx_inference_debug);
GST_DEBUG_CATEGORY (onnx_runtime_debug);

#define GST_CAT_DEFAULT onnx_inference_debug
GST_ELEMENT_REGISTER_DEFINE (onnx_inference, "onnxinference",
    GST_RANK_PRIMARY, GST_TYPE_ONNX_INFERENCE);

/* GstOnnxInference properties */
enum
{
  PROP_0,
  PROP_MODEL_FILE,
  PROP_OPTIMIZATION_LEVEL,
  PROP_EXECUTION_PROVIDER,
  PROP_INPUT_OFFSET,
  PROP_INPUT_SCALE
};

#define GST_ONNX_INFERENCE_DEFAULT_EXECUTION_PROVIDER    GST_ONNX_EXECUTION_PROVIDER_CPU
#define GST_ONNX_INFERENCE_DEFAULT_OPTIMIZATION_LEVEL    GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED

static GstStaticPadTemplate gst_onnx_inference_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB,RGBA,BGR,BGRA }"))
    );

static GstStaticPadTemplate gst_onnx_inference_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB,RGBA,BGR,BGRA }"))
    );


static void gst_onnx_inference_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_onnx_inference_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_onnx_inference_finalize (GObject * object);
static GstFlowReturn gst_onnx_inference_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);
static GstCaps *gst_onnx_inference_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps);
static gboolean
gst_onnx_inference_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps);
static gboolean gst_onnx_inference_start (GstBaseTransform * trans);
static gboolean gst_onnx_inference_stop (GstBaseTransform * trans);

G_DEFINE_TYPE (GstOnnxInference, gst_onnx_inference, GST_TYPE_BASE_TRANSFORM);

GType gst_onnx_optimization_level_get_type (void);
#define GST_TYPE_ONNX_OPTIMIZATION_LEVEL (gst_onnx_optimization_level_get_type ())

GType gst_onnx_execution_provider_get_type (void);
#define GST_TYPE_ONNX_EXECUTION_PROVIDER (gst_onnx_execution_provider_get_type ())

GType
gst_onnx_optimization_level_get_type (void)
{
  static GType onnx_optimization_type = 0;

  if (g_once_init_enter (&onnx_optimization_type)) {
    static GEnumValue optimization_level_types[] = {
      {GST_ONNX_OPTIMIZATION_LEVEL_DISABLE_ALL, "Disable all optimization",
          "disable-all"},
      {GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_BASIC,
            "Enable basic optimizations (redundant node removals))",
          "enable-basic"},
      {GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED,
            "Enable extended optimizations (redundant node removals + node fusions)",
          "enable-extended"},
      {GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_ALL,
          "Enable all possible optimizations", "enable-all"},
      {0, NULL, NULL},
    };

    GType temp = g_enum_register_static ("GstOnnxOptimizationLevel",
        optimization_level_types);

    g_once_init_leave (&onnx_optimization_type, temp);
  }

  return onnx_optimization_type;
}

GType
gst_onnx_execution_provider_get_type (void)
{
  static GType onnx_execution_type = 0;

  if (g_once_init_enter (&onnx_execution_type)) {
    static GEnumValue execution_provider_types[] = {
      {GST_ONNX_EXECUTION_PROVIDER_CPU, "CPU execution provider",
          "cpu"},
#if HAVE_CUDA
      {GST_ONNX_EXECUTION_PROVIDER_CUDA,
            "CUDA execution provider",
          "cuda"},
#else
      {GST_ONNX_EXECUTION_PROVIDER_CUDA,
            "CUDA execution provider (compiled out, will use CPU)",
          "cuda"},
#endif
#ifdef HAVE_VSI_NPU
      {GST_ONNX_EXECUTION_PROVIDER_VSI,
            "VeriSilicon NPU execution provider",
          "vsi"},
#else
      {GST_ONNX_EXECUTION_PROVIDER_VSI,
            "VeriSilicon NPU execution provider (compiled out, will use CPU)",
          "vsi"},
#endif
      {0, NULL, NULL},
    };

    GType temp = g_enum_register_static ("GstOnnxExecutionProvider",
        execution_provider_types);

    g_once_init_leave (&onnx_execution_type, temp);
  }

  return onnx_execution_type;
}

static void
gst_onnx_inference_class_init (GstOnnxInferenceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  GST_DEBUG_CATEGORY_INIT (onnx_inference_debug, "onnxinference",
      0, "ONNX Runtime Inference");
  GST_DEBUG_CATEGORY_INIT (onnx_runtime_debug, "onnxruntime",
      0, "ONNX Runtime");
  gobject_class->set_property = gst_onnx_inference_set_property;
  gobject_class->get_property = gst_onnx_inference_get_property;
  gobject_class->finalize = gst_onnx_inference_finalize;

  /**
   * GstOnnxInference:model-file
   *
   * ONNX model file
   *
   * Since: 1.24
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MODEL_FILE,
      g_param_spec_string ("model-file",
          "ONNX model file", "ONNX model file", NULL, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

   /**
    * GstOnnxInference:optimization-level
    *
    * ONNX optimization level
    *
    * Since: 1.24
    */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_OPTIMIZATION_LEVEL,
      g_param_spec_enum ("optimization-level",
          "Optimization level",
          "ONNX optimization level",
          GST_TYPE_ONNX_OPTIMIZATION_LEVEL,
          GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstOnnxInference:execution-provider
   *
   * ONNX execution provider
   *
   * Since: 1.24
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_EXECUTION_PROVIDER,
      g_param_spec_enum ("execution-provider",
          "Execution provider",
          "ONNX execution provider",
          GST_TYPE_ONNX_EXECUTION_PROVIDER,
          GST_ONNX_EXECUTION_PROVIDER_CPU, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_INPUT_OFFSET,
      g_param_spec_float ("input-tensor-offset",
          "Input tensor offset",
          "offset each tensor value by this value",
          -G_MAXFLOAT, G_MAXFLOAT, 0.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_INPUT_SCALE,
      g_param_spec_float ("input-tensor-scale",
          "Input tensor scale",
          "Divide each tensor value by this value",
          G_MINFLOAT, G_MAXFLOAT, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));


  gst_element_class_set_static_metadata (element_class, "onnxinference",
      "Filter/Video",
      "Apply neural network to video frames and create tensor output",
      "Aaron Boxer <aaron.boxer@collabora.com>");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_onnx_inference_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_onnx_inference_src_template));
  basetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_onnx_inference_transform_ip);
  basetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_onnx_inference_transform_caps);
  basetransform_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_onnx_inference_set_caps);
  basetransform_class->start = GST_DEBUG_FUNCPTR (gst_onnx_inference_start);
  basetransform_class->stop = GST_DEBUG_FUNCPTR (gst_onnx_inference_stop);

  gst_type_mark_as_plugin_api (GST_TYPE_ONNX_OPTIMIZATION_LEVEL,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_ONNX_EXECUTION_PROVIDER,
      (GstPluginAPIFlags) 0);

  api = OrtGetApiBase ()->GetApi (ORT_API_VERSION);
}

static void
gst_onnx_inference_init (GstOnnxInference * self)
{
  /* TODO: at the moment onnx inference only support video output. We
   * should revisit this aspect once we generalize it */
  self->model_caps = gst_caps_new_empty_simple ("video/x-raw");

  self->execution_provider = GST_ONNX_EXECUTION_PROVIDER_CPU;

  self->means = g_new0 (double, 1);
  self->stddevs = g_new (double, 1);
  self->stddevs[0] = 1.0;

  self->height_dim = -1;
  self->width_dim = -1;
  self->channels_dim = -1;
  self->batch_dim = -1;

}

static void
gst_onnx_inference_finalize (GObject * object)
{
  GstOnnxInference *self = GST_ONNX_INFERENCE (object);

  g_free (self->means);
  g_free (self->stddevs);

  g_free (self->model_file);
  gst_caps_unref (self->model_caps);
  G_OBJECT_CLASS (gst_onnx_inference_parent_class)->finalize (object);
}

static void
gst_onnx_inference_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOnnxInference *self = GST_ONNX_INFERENCE (object);
  const gchar *filename;

  switch (prop_id) {
    case PROP_MODEL_FILE:
      filename = g_value_get_string (value);
      if (filename
          && g_file_test (filename,
              (GFileTest) (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR))) {
        if (self->model_file)
          g_free (self->model_file);
        self->model_file = g_strdup (filename);
      } else {
        GST_WARNING_OBJECT (self, "Model file '%s' not found!", filename);
      }
      break;
    case PROP_OPTIMIZATION_LEVEL:
      self->optimization_level =
          (GstOnnxOptimizationLevel) g_value_get_enum (value);
      break;
    case PROP_EXECUTION_PROVIDER:
      self->execution_provider =
          (GstOnnxExecutionProvider) g_value_get_enum (value);
      break;
    case PROP_INPUT_OFFSET:{
      int c = self->channels ? self->channels : 1;
      int i;
      for (i = 0; i < c; i++)
        self->means[i] = g_value_get_float (value);
      break;
    }
    case PROP_INPUT_SCALE:{
      int c = self->channels ? self->channels : 1;
      int i;
      for (i = 0; i < c; i++)
        self->stddevs[i] = g_value_get_float (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_onnx_inference_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOnnxInference *self = GST_ONNX_INFERENCE (object);

  switch (prop_id) {
    case PROP_MODEL_FILE:
      g_value_set_string (value, self->model_file);
      break;
    case PROP_OPTIMIZATION_LEVEL:
      g_value_set_enum (value, self->optimization_level);
      break;
    case PROP_EXECUTION_PROVIDER:
      g_value_set_enum (value, self->execution_provider);
      break;
    case PROP_INPUT_OFFSET:
      g_value_set_float (value, self->means[0]);
      break;
    case PROP_INPUT_SCALE:
      g_value_set_float (value, self->stddevs[0]);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gsize
get_tensor_type_size (GstTensorDataType data_type)
{
  switch (data_type) {
    case GST_TENSOR_DATA_TYPE_UINT8:
      return sizeof (uint8_t);
    case GST_TENSOR_DATA_TYPE_UINT16:
      return sizeof (uint16_t);
    case GST_TENSOR_DATA_TYPE_UINT32:
      return sizeof (uint32_t);
    case GST_TENSOR_DATA_TYPE_INT32:
      return sizeof (int32_t);
    case GST_TENSOR_DATA_TYPE_FLOAT16:
      return 2;
    case GST_TENSOR_DATA_TYPE_FLOAT32:
      return sizeof (float);
    default:
      g_error ("Data type %d not handled", data_type);
      return 0;
  };
}

static GstCaps *
gst_onnx_inference_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps)
{
  GstOnnxInference *self = GST_ONNX_INFERENCE (trans);
  GstCaps *other_caps;
  bool has_session;

  GST_OBJECT_LOCK (self);
  has_session = self->session != NULL;
  GST_OBJECT_UNLOCK (self);

  if (!has_session) {
    other_caps = gst_caps_ref (caps);
    goto done;
  }

  GST_LOG_OBJECT (self, "transforming caps %" GST_PTR_FORMAT, caps);

  GST_DEBUG_OBJECT (self, "Applying model caps restrictions: %" GST_PTR_FORMAT,
      self->model_caps);

  other_caps = gst_caps_intersect_full (caps, self->model_caps,
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

static GstTensorDataType
onnx_data_type_to_gst (ONNXTensorElementDataType dt)
{
  const gint ONNX_TO_GST_TENSOR_DATATYPE[] = {
    -1,                         /* ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED */
    GST_TENSOR_DATA_TYPE_FLOAT32,       /* ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT */
    GST_TENSOR_DATA_TYPE_UINT8, /* ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8 */
    GST_TENSOR_DATA_TYPE_INT8,  /* ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8 */
    GST_TENSOR_DATA_TYPE_UINT16,        /* ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16 */
    GST_TENSOR_DATA_TYPE_INT16, /* ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16 */
    GST_TENSOR_DATA_TYPE_INT32, /* ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32 */
    GST_TENSOR_DATA_TYPE_INT64, /* ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 */
    GST_TENSOR_DATA_TYPE_STRING,        /* ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING */
    GST_TENSOR_DATA_TYPE_BOOL,  /* ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL */
    GST_TENSOR_DATA_TYPE_FLOAT16,       /* ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16 */
    GST_TENSOR_DATA_TYPE_FLOAT64,       /* ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE */
    GST_TENSOR_DATA_TYPE_UINT32,        /* ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32 */
    GST_TENSOR_DATA_TYPE_UINT64,        /* ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64 */
    GST_TENSOR_DATA_TYPE_COMPLEX64,     /* ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX64 */
    GST_TENSOR_DATA_TYPE_COMPLEX128,    /* ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX128 */
    GST_TENSOR_DATA_TYPE_BFLOAT16,      /* ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16 */
    GST_TENSOR_DATA_TYPE_FLOAT8E4M3FN,  /* ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E4M3FN */
    GST_TENSOR_DATA_TYPE_FLOAT8E4M3FNUZ,        /* ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E4M3FNUZ */
    GST_TENSOR_DATA_TYPE_FLOAT8E5M2,    /* ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E5M2 */
    GST_TENSOR_DATA_TYPE_FLOAT8E5M2FNUZ,        /* ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E5M2FNUZ */
    GST_TENSOR_DATA_TYPE_UINT4, /* ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT4 */
    GST_TENSOR_DATA_TYPE_INT4,  /* ONNX_TENSOR_ELEMENT_DATA_TYPE_INT4 */
  };

  if (dt > ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED &&
      dt <= ONNX_TENSOR_ELEMENT_DATA_TYPE_INT4) {
    return ONNX_TO_GST_TENSOR_DATATYPE[dt];
  }

  g_error ("Unexpected datatype: %d", dt);
}

static void
gst_onnx_log_function (void *param, OrtLoggingLevel severity,
    const char *category, const char *logid, const char *code_location,
    const char *message)
{
  GObject *obj = param;
  GstDebugLevel level = GST_LEVEL_ERROR;

  switch (severity) {
    case ORT_LOGGING_LEVEL_VERBOSE:
      level = GST_LEVEL_LOG;
      break;
    case ORT_LOGGING_LEVEL_INFO:
      level = GST_LEVEL_INFO;
      break;
    case ORT_LOGGING_LEVEL_WARNING:
      level = GST_LEVEL_WARNING;
      break;
    case ORT_LOGGING_LEVEL_ERROR:
    case ORT_LOGGING_LEVEL_FATAL:
      level = GST_LEVEL_ERROR;
      break;
  }

  gst_debug_log (onnx_runtime_debug, level, code_location,
      "gst_onnx_log_function", 0, obj, "%s", message);
}

/* FIXME: This is copied from Gsttfliteinference and we should create something
 * more generic
 */

static gboolean
_guess_tensor_data_type (GstOnnxInference * self, gsize dims_count,
    gsize * dims, const gchar ** gst_format)
{
  self->height_dim = -1;
  self->width_dim = -1;
  self->channels_dim = -1;
  self->batch_dim = -1;

  if (dims_count < 2 || dims_count > 4) {
    GST_ERROR_OBJECT (self,
        "Don't know how to interpret tensors with %zu dimensions", dims_count);
    return FALSE;
  }

  switch (dims_count) {
    case 2:
      *gst_format = "GRAY8";
      self->height_dim = 0;
      self->width_dim = 1;
      break;
    case 3:
      if (dims[0] == 1 || dims[0] == 3) {
        self->channels_dim = 0;
        if (dims[0] == 1) {
          *gst_format = "GRAY8";
        } else {
          *gst_format = "RGBP";
        }
        self->height_dim = 1;
        self->width_dim = 2;
      } else if (dims[2] == 1 || dims[2] == 3) {
        self->channels_dim = 2;
        if (dims[2] == 1)
          *gst_format = "GRAY";
        else
          *gst_format = "RGB";
        self->height_dim = 0;
        self->width_dim = 1;
      } else {
        GST_ERROR_OBJECT (self, "Don't know how to interpret dims");
        return FALSE;
      }
      break;
    case 4:
      /* Assuming dims[0] is a batch */
      self->batch_dim = 0;
      if (dims[1] == 1 || dims[1] == 3) {
        self->channels_dim = 1;
        self->height_dim = 2;
        self->width_dim = 3;
      } else if (dims[3] == 1 || dims[3] == 3) {
        self->height_dim = 1;
        self->width_dim = 2;
        self->channels_dim = 3;
      } else {
        GST_ERROR_OBJECT (self, "Don't know how to interpret dims");
        return FALSE;
      }

      if (dims[self->channels_dim] == 1) {
        *gst_format = "GRAY8";
      } else if (dims[self->channels_dim] == 3) {
        if (self->planar)
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
gst_onnx_inference_start (GstBaseTransform * trans)
{
  GstOnnxInference *self = GST_ONNX_INFERENCE (trans);
  gboolean ret = FALSE;
  OrtStatus *status = NULL;
  OrtSessionOptions *session_options = NULL;
  OrtTypeInfo *input_type_info = NULL;
  const OrtTensorTypeAndShapeInfo *input_tensor_info = NULL;
  OrtModelMetadata *metadata = NULL;
  GraphOptimizationLevel onnx_optim;
  size_t num_input_dims;
  int64_t *input_dims;
  gsize *gst_input_dims;
  ONNXTensorElementDataType element_type;
  char *input_name = NULL;
  size_t i;
  const gchar *gst_format;

  GST_OBJECT_LOCK (self);
  if (self->session) {
    ret = TRUE;
    goto done;
  }

  if (self->model_file == NULL) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("model-file property not set"));
    goto done;
  }

  if (self->session) {
    ret = TRUE;
    goto done;
  }
  // Create environment
  OrtLoggingLevel ort_logging;

  switch (gst_debug_category_get_threshold (GST_CAT_DEFAULT)) {
    case GST_LEVEL_NONE:
    case GST_LEVEL_ERROR:
      ort_logging = ORT_LOGGING_LEVEL_ERROR;
      break;
    case GST_LEVEL_WARNING:
    case GST_LEVEL_FIXME:
      ort_logging = ORT_LOGGING_LEVEL_WARNING;
      break;
    case GST_LEVEL_INFO:
      ort_logging = ORT_LOGGING_LEVEL_INFO;
      break;
    case GST_LEVEL_DEBUG:
    case GST_LEVEL_LOG:
    case GST_LEVEL_TRACE:
    case GST_LEVEL_MEMDUMP:
    default:
      ort_logging = ORT_LOGGING_LEVEL_VERBOSE;
      break;
  }

  status = api->CreateEnvWithCustomLogger (gst_onnx_log_function, self,
      ort_logging, "GstOnnx", &self->env);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to create environment: %s",
        api->GetErrorMessage (status));
    goto error;
  }
  // Create session options
  status = api->CreateSessionOptions (&session_options);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to create session options: %s",
        api->GetErrorMessage (status));
    goto error;
  }
  // Set graph optimization level
  switch (self->optimization_level) {
    case GST_ONNX_OPTIMIZATION_LEVEL_DISABLE_ALL:
      onnx_optim = ORT_DISABLE_ALL;
      break;
    case GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_BASIC:
      onnx_optim = ORT_ENABLE_BASIC;
      break;
    case GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED:
      onnx_optim = ORT_ENABLE_EXTENDED;
      break;
    case GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_ALL:
      onnx_optim = ORT_ENABLE_ALL;
      break;
    default:
      onnx_optim = ORT_ENABLE_EXTENDED;
      break;
  }

  status = api->SetSessionGraphOptimizationLevel (session_options, onnx_optim);
  if (status) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("Failed to set optimization level: %s",
            api->GetErrorMessage (status)));
    goto error;
  }
  // Set execution provider
  switch (self->execution_provider) {
    case GST_ONNX_EXECUTION_PROVIDER_CUDA:
    {
      OrtCUDAProviderOptionsV2 *cuda_options = NULL;
      status = api->CreateCUDAProviderOptions (&cuda_options);
      if (status) {
        GST_ERROR_OBJECT (self,
            "Failed to create CUDA provider %s", api->GetErrorMessage (status));
        goto error;
      }

      status =
          api->SessionOptionsAppendExecutionProvider_CUDA_V2 (session_options,
          cuda_options);
      api->ReleaseCUDAProviderOptions (cuda_options);
      if (status) {
        GST_ERROR_OBJECT (self, "Failed to append CUDA provider: %s",
            api->GetErrorMessage (status));
        goto error;
      }
      break;
    }
    case GST_ONNX_EXECUTION_PROVIDER_VSI:
#ifdef HAVE_VSI_NPU
      status =
          OrtSessionOptionsAppendExecutionProvider_VSINPU (session_options);
      if (status) {
        GST_ERROR_OBJECT (self, "Failed to set VSINPU AI execution provider:"
            " %s", api->GetErrorMessage (status));
        goto error;
      }
      api->DisableCpuMemArena (session_options);
#else
      GST_ERROR_OBJECT (self, "Compiled without VSI support");
      goto error;
#endif
      break;
    default:
      break;
  }

  // Create session
  status = api->CreateSession (self->env, self->model_file, session_options,
      &self->session);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to create session: %s",
        api->GetErrorMessage (status));
    self->session = NULL;
    goto error;
  }

  api->ReleaseSessionOptions (session_options);
  session_options = NULL;

  // Get allocator
  status = api->GetAllocatorWithDefaultOptions (&self->allocator);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to get allocator: %s",
        api->GetErrorMessage (status));
    goto error;
  }
  // Get input info
  status = api->SessionGetInputTypeInfo (self->session, 0, &input_type_info);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to get input type info: %s",
        api->GetErrorMessage (status));
    goto error;
  }

  status = api->CastTypeInfoToTensorInfo (input_type_info, &input_tensor_info);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to cast type info: %s",
        api->GetErrorMessage (status));
    goto error;
  }

  status = api->GetDimensionsCount (input_tensor_info, &num_input_dims);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to get dimensions count: %s",
        api->GetErrorMessage (status));
    goto error;
  }

  input_dims = (int64_t *) g_alloca (num_input_dims * sizeof (int64_t));
  gst_input_dims = (gsize *) g_alloca (num_input_dims * sizeof (gsize));
  status = api->GetDimensions (input_tensor_info, input_dims, num_input_dims);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to get dimensions: %s",
        api->GetErrorMessage (status));
    goto error;
  }

  for (i = 0; i < num_input_dims; i++) {
    if (input_dims[i] < 0)
      gst_input_dims[i] = G_MAXSIZE;
    else
      gst_input_dims[i] = input_dims[i];
  }

  gchar *dims = build_dims_str (num_input_dims, gst_input_dims);
  GST_DEBUG_OBJECT (self, "Input dimensions: %s", dims);
  g_free (dims);

  if (!_guess_tensor_data_type (self, num_input_dims, gst_input_dims,
          &gst_format))
    goto error;

  self->height = gst_input_dims[self->height_dim];
  self->width = gst_input_dims[self->width_dim];
  if (self->channels_dim >= 0) {
    self->channels = gst_input_dims[self->channels_dim];
    self->planar = (self->channels_dim != num_input_dims - 1);
  } else {
    self->channels = 1;
  }


  GST_DEBUG_OBJECT (self, "height dim[%d]=%d, width dim[%d]=%d,"
      " channels dim[%d]=%d, batch_dim[%d]=%zu planar=%d",
      self->height_dim, self->height, self->width_dim, self->width,
      self->channels_dim, self->channels, self->batch_dim,
      self->batch_dim >= 0 ? gst_input_dims[self->batch_dim] : 0, self->planar);

  self->means = g_renew (double, self->means, self->channels);
  self->stddevs = g_renew (double, self->stddevs, self->channels);

  for (int i = 1; i < self->channels; i++) {
    self->means[i] = self->means[0];
    self->stddevs[i] = self->stddevs[0];
  }

  self->fixedInputImageSize = self->width > 0 && self->height > 0;

  status = api->SessionGetOutputCount (self->session, &self->output_count);
  if (status) {
    GST_ERROR_OBJECT (self, "Could to retrieve output count: %s",
        api->GetErrorMessage (status));
    goto error;
  }
  GST_DEBUG_OBJECT (self, "Number of Output Nodes: %zu", self->output_count);

  status = api->GetTensorElementType (input_tensor_info, &element_type);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to get element type: %s",
        api->GetErrorMessage (status));
    goto error;
  }

  api->ReleaseTypeInfo (input_type_info);
  input_type_info = NULL;

  switch (element_type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
      break;
    default:
      GST_ERROR_OBJECT (self,
          "Only input tensors of type int8 and float are supported");
      goto error;
  }
  self->input_data_type = onnx_data_type_to_gst (element_type);

  /* Setting datatype */
  self->model_caps = gst_caps_make_writable (self->model_caps);
  if (self->input_data_type == GST_TENSOR_DATA_TYPE_UINT8 && gst_format &&
      self->means[0] == 0.0 && self->stddevs[0] == 1.0)
    gst_caps_set_simple (self->model_caps, "format", G_TYPE_STRING, gst_format,
        NULL);
  if (self->fixedInputImageSize)
    gst_caps_set_simple (self->model_caps, "width", G_TYPE_INT,
        self->width, "height", G_TYPE_INT, self->height, NULL);

  // Get input name
  status =
      api->SessionGetInputName (self->session, 0, self->allocator, &input_name);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to get input name: %s",
        api->GetErrorMessage (status));
    goto error;
  }
  GST_DEBUG_OBJECT (self, "Input name: %s", input_name);
  self->allocator->Free (self->allocator, input_name);

  // Get output names
  self->output_names = g_new0 (char *, self->output_count);
  for (i = 0; i < self->output_count; ++i) {
    status =
        api->SessionGetOutputName (self->session, i, self->allocator,
        &self->output_names[i]);
    if (status) {
      GST_ERROR_OBJECT (self, "Failed to get output name %zu: %s", i,
          api->GetErrorMessage (status));
      goto error;
    }
    GST_DEBUG_OBJECT (self, "Output name %lu:%s", i, self->output_names[i]);
  }

  // Look up tensor ids
  status = api->SessionGetModelMetadata (self->session, &metadata);
  if (status) {
    GST_INFO_OBJECT (self, "Could not get model metadata: %s",
        api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    status = NULL;
  }

  self->output_ids = g_new0 (GQuark, self->output_count);

  for (i = 0; i < self->output_count; i++) {
    OrtTypeInfo *output_type_info = NULL;
    const OrtTensorTypeAndShapeInfo *output_tensor_info = NULL;
    size_t card;
    ONNXTensorElementDataType type;
    char *res = NULL;
    size_t j;


    status =
        api->SessionGetOutputTypeInfo (self->session, i, &output_type_info);
    if (status) {
      GST_ERROR_OBJECT (self, "Failed to get info for output tensor %zu: %s",
          i, api->GetErrorMessage (status));
      goto error;
    }

    status =
        api->CastTypeInfoToTensorInfo (output_type_info, &output_tensor_info);
    if (status) {
      GST_ERROR_OBJECT (self, "Failed to get cast type for output tensor"
          " %zu: %s", i, api->GetErrorMessage (status));
      api->ReleaseTypeInfo (output_type_info);
      goto error;
    }

    status = api->GetDimensionsCount (output_tensor_info, &card);
    if (status) {
      GST_ERROR_OBJECT (self, "Failed to get cardinality for output tensor"
          " %zu: %s", i, api->GetErrorMessage (status));
      api->ReleaseTypeInfo (output_type_info);
      goto error;
    }

    status = api->GetTensorElementType (output_tensor_info, &type);
    if (status) {
      GST_ERROR_OBJECT (self, "Failed to get element type for output tensor"
          " %zu: %s", i, api->GetErrorMessage (status));
      api->ReleaseTypeInfo (output_type_info);
      goto error;
    }

    if (metadata)
      status =
          api->ModelMetadataLookupCustomMetadataMap (metadata, self->allocator,
          self->output_names[i], &res);

    if (!status && res) {
      GST_INFO_OBJECT (self, "Tensor %zu name is %s from metadata", i, res);
      self->output_ids[i] = g_quark_from_string (res);
      self->allocator->Free (self->allocator, res);
    } else {
      if (status) {
        GST_WARNING_OBJECT (self, "Could not find key %s in model metadata: %s",
            self->output_names[i], api->GetErrorMessage (status));
        api->ReleaseStatus (status);
        status = NULL;
      }

      /* FIXME: to be replaced by ModelInfo files */
#define GST_MODEL_OBJECT_DETECTOR_BOXES "ssd-mobilenet-v1-variant-1-out-boxes"
#define GST_MODEL_OBJECT_DETECTOR_SCORES "ssd-mobilenet-v1-variant-1-out-scores"
#define GST_MODEL_OBJECT_DETECTOR_NUM_DETECTIONS "generic-variant-1-out-count"
#define GST_MODEL_OBJECT_DETECTOR_CLASSES "ssd-mobilenet-v1-variant-1-out-classes"

      if (g_str_has_prefix (self->output_names[i], "scores")) {
        self->output_ids[i] =
            g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_SCORES);
        GST_INFO_OBJECT (self, "No custom metadata for key '%s', assuming %s",
            self->output_names[i], GST_MODEL_OBJECT_DETECTOR_SCORES);
      } else if (g_str_has_prefix (self->output_names[i], "boxes")) {
        self->output_ids[i] =
            g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_BOXES);
        GST_INFO_OBJECT (self, "No custom metadata for key '%s', assuming %s",
            self->output_names[i], GST_MODEL_OBJECT_DETECTOR_BOXES);
      } else if (g_str_has_prefix (self->output_names[i], "detection_classes")) {
        self->output_ids[i] =
            g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_CLASSES);
        GST_INFO_OBJECT (self, "No custom metadata for key '%s', assuming %s",
            self->output_names[i], GST_MODEL_OBJECT_DETECTOR_CLASSES);
      } else if (g_str_has_prefix (self->output_names[i], "num_detections")) {
        self->output_ids[i] =
            g_quark_from_static_string
            (GST_MODEL_OBJECT_DETECTOR_NUM_DETECTIONS);
        GST_INFO_OBJECT (self, "No custom metadata for key '%s', assuming %s",
            self->output_names[i], GST_MODEL_OBJECT_DETECTOR_NUM_DETECTIONS);
      } else {
        GST_ERROR_OBJECT (self, "Failed to look up id for key %s",
            self->output_names[i]);
        api->ReleaseTypeInfo (output_type_info);
        goto error;
      }

      /* tensor description */
      GstStructure *tensor_desc = gst_structure_new_empty ("tensor/strided");

      /* Setting dims */
      GValue val_dims = G_VALUE_INIT, val = G_VALUE_INIT;
      gst_value_array_init (&val_dims, card);
      g_value_init (&val, G_TYPE_INT);

      int64_t *shape = (int64_t *) g_alloca (card * sizeof (int64_t));
      status = api->GetDimensions (output_tensor_info, shape, card);
      if (!status) {
        for (j = 0; j < card; j++) {
          g_value_set_int (&val, shape[j] > 0 ? shape[j] : 0);
          gst_value_array_append_value (&val_dims, &val);
        }
      } else {
        api->ReleaseStatus (status);
        status = NULL;
      }

      gst_structure_take_value (tensor_desc, "dims", &val_dims);
      g_value_unset (&val_dims);
      g_value_unset (&val);

      /* Setting tensors caps */
      char *meta_key = NULL;
      OrtStatus *lookup_status =
          api->ModelMetadataLookupCustomMetadataMap (metadata, self->allocator,
          self->output_names[i], &meta_key);
      if (!lookup_status && meta_key) {
        gst_caps_set_simple (self->model_caps, meta_key, GST_TYPE_CAPS,
            gst_caps_new_full (tensor_desc, NULL), NULL);
        self->allocator->Free (self->allocator, meta_key);
      } else {
        if (lookup_status)
          api->ReleaseStatus (lookup_status);
        gst_structure_free (tensor_desc);
      }

      api->ReleaseTypeInfo (output_type_info);
    }
  }

  if (metadata)
    api->ReleaseModelMetadata (metadata);
  metadata = NULL;

  // Create memory info for CPU
  status =
      api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault,
      &self->memory_info);
  if (status) {
    GST_WARNING_OBJECT (self, "Failed to create memory info: %s",
        api->GetErrorMessage (status));
    goto error;
  }

  ret = TRUE;
done:
  GST_OBJECT_UNLOCK (self);

  return ret;

error:
  if (status)
    api->ReleaseStatus (status);
  if (input_type_info)
    api->ReleaseTypeInfo (input_type_info);
  if (metadata)
    api->ReleaseModelMetadata (metadata);
  if (session_options)
    api->ReleaseSessionOptions (session_options);

  GST_OBJECT_UNLOCK (self);

  gst_onnx_inference_stop (trans);
  return ret;

}

static gboolean
gst_onnx_inference_stop (GstBaseTransform * trans)
{
  GstOnnxInference *self = GST_ONNX_INFERENCE (trans);
  size_t i;

  GST_OBJECT_LOCK (self);
  if (!self->session)
    goto done;
  // Clean up output names

  for (i = 0; i < self->output_count; i++) {
    if (self->output_names[i])
      self->allocator->Free (self->allocator, self->output_names[i]);
  }
  g_free (self->output_names);
  self->output_names = NULL;

  g_free (self->output_ids);
  self->output_ids = NULL;
  self->output_count = 0;

  if (self->memory_info)
    api->ReleaseMemoryInfo (self->memory_info);

  api->ReleaseSession (self->session);
  self->session = NULL;

  if (self->env)
    api->ReleaseEnv (self->env);
  self->env = NULL;

done:
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

static gboolean
gst_onnx_inference_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstOnnxInference *self = GST_ONNX_INFERENCE (trans);

  if (!gst_video_info_from_caps (&self->video_info, incaps)) {
    GST_ERROR_OBJECT (self, "Failed to parse caps");
    return FALSE;
  }

  if (self->fixedInputImageSize &&
      (self->video_info.width != self->width ||
          self->video_info.height != self->height)) {
    GST_ERROR_OBJECT (self, "Dimensions from caps %ux%u doesn't match model"
        " dimensions %dx%d", self->video_info.width, self->video_info.height,
        self->width, self->height);
    return FALSE;
  }

  if (self->dest == NULL || self->width * self->height !=
      self->video_info.width * self->video_info.height) {
    g_free (self->dest);
    self->dest = g_malloc (self->video_info.width * self->video_info.height *
        self->channels * get_tensor_type_size (self->input_data_type));
  }
  self->width = self->video_info.width;
  self->height = self->video_info.height;

  return TRUE;
}

#define _convert_image_remove_alpha(Type)                               \
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
          srcPtr[k] += pixel_stride;                                    \
        }                                                                     \
      }                                                                       \
      /* correct for stride */                                                \
      for (uint32_t k = 0; k < 3; ++k)                                        \
        srcPtr[k] += stride - pixel_stride * dstWidth;                  \
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
          srcPtr[k] += pixel_stride;                                    \
        }                                                                     \
        destIndex++;                                                          \
      }                                                                       \
      /* correct for stride */                                                \
      for (uint32_t k = 0; k < 3; ++k)                                        \
        srcPtr[k] += stride - pixel_stride * dstWidth;                  \
    }                                                                         \
  }                                                                           \
}                                                                             \
G_STMT_END;

static void
convert_image_remove_alpha_u8 (guint8 * dst, gint dstWidth, gint dstHeight,
    gint dstChannels, gboolean planar, guint8 ** srcPtr,
    guint8 pixel_stride, guint32 stride, const gdouble * means,
    const gdouble * stddevs)
{
  static const gdouble zeros[] = { 0, 0, 0, 0 };
  static const gdouble ones[] = { 1.0, 1.0, 1.0, 1.0 };
  if (means == NULL)
    means = zeros;
  if (stddevs == NULL)
    stddevs = ones;

  _convert_image_remove_alpha (guint8);
}

static void
convert_image_remove_alpha_f32 (gfloat * dst, gint dstWidth, gint dstHeight,
    gint dstChannels, gboolean planar, guint8 ** srcPtr,
    guint8 pixel_stride, guint32 stride, const gdouble * means,
    const gdouble * stddevs)
{
  static const gdouble zeros[] = { 0, 0, 0, 0 };
  static const gdouble two_five_fives[] = { 255.0, 255.0, 255.0, 255.0 };
  if (means == NULL)
    means = zeros;
  if (stddevs == NULL)
    stddevs = two_five_fives;

  _convert_image_remove_alpha (gfloat);
}

static GstFlowReturn
gst_onnx_inference_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstOnnxInference *self = GST_ONNX_INFERENCE (trans);
  GstMapInfo info;
  OrtStatus *status = NULL;
  OrtTypeInfo *input_type_info = NULL;
  OrtValue *input_tensor = NULL;
  OrtValue **output_tensors = NULL;
  const OrtTensorTypeAndShapeInfo *input_tensor_info;
  size_t num_dims;
  int64_t *input_dims;
  uint8_t *srcPtr[3];
  size_t inputTensorSize;
  char *input_names[1];
  GstTensorMeta *tmeta = NULL;
  OrtTensorTypeAndShapeInfo *output_tensor_info = NULL;

  if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
    GST_ELEMENT_ERROR (trans, STREAM, FAILED, (NULL),
        ("Could not map input buffer"));
    return GST_FLOW_ERROR;
  }

  status =
      api->SessionGetInputName (self->session, 0, self->allocator, input_names);
  if (status) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("Failed to get input name"));
    goto error;
  }

  status = api->SessionGetInputTypeInfo (self->session, 0, &input_type_info);
  if (status) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("Failed to get input type info: %s", api->GetErrorMessage (status)));
    goto error;
  }

  status = api->CastTypeInfoToTensorInfo (input_type_info, &input_tensor_info);
  if (status) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("Failed to cast type info: %s", api->GetErrorMessage (status)));
    goto error;
  }

  status = api->GetDimensionsCount (input_tensor_info, &num_dims);
  if (status) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("Failed to get dimensions count: %s", api->GetErrorMessage (status)));
    goto error;
  }

  input_dims = (int64_t *) g_alloca (num_dims * sizeof (int64_t));
  status = api->GetDimensions (input_tensor_info, input_dims, num_dims);
  if (status) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("Failed to get dimensions: %s", api->GetErrorMessage (status)));
    goto error;
  }

  api->ReleaseTypeInfo (input_type_info);
  input_type_info = NULL;

  if (self->batch_dim >= 0)
    input_dims[self->batch_dim] = 1;

  if (input_dims[self->height_dim] >= 0) {
    if (input_dims[self->height_dim] != self->height) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Buffer has height %d, but model expects %zu",
              self->height, input_dims[self->height_dim]));
      goto error;
    }
  } else {
    input_dims[self->height_dim] = self->height;
  }
  if (input_dims[self->width_dim] >= 0) {
    if (input_dims[self->width_dim] != self->width) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Buffer has width %d, but model expects %zu",
              self->width, input_dims[self->width_dim]));
      goto error;
    }
  } else {
    input_dims[self->width_dim] = self->width;
  }

  GST_LOG_OBJECT (self, "Input dimensions: %" G_GINT64_FORMAT
      ":%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT,
      input_dims[0], input_dims[1], input_dims[2], input_dims[3]);

  // copy video frame
  switch (self->video_info.finfo->format) {
    case GST_VIDEO_FORMAT_RGBA:
      srcPtr[0] = info.data;
      srcPtr[1] = info.data + 1;
      srcPtr[2] = info.data + 2;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      srcPtr[0] = info.data + 2;
      srcPtr[1] = info.data + 1;
      srcPtr[2] = info.data + 0;
      break;
    case GST_VIDEO_FORMAT_ARGB:
      srcPtr[0] = info.data + 1;
      srcPtr[1] = info.data + 2;
      srcPtr[2] = info.data + 3;
      break;
    case GST_VIDEO_FORMAT_ABGR:
      srcPtr[0] = info.data + 3;
      srcPtr[1] = info.data + 2;
      srcPtr[2] = info.data + 1;
      break;
    case GST_VIDEO_FORMAT_RGB:
      srcPtr[0] = info.data;
      srcPtr[1] = info.data + 1;
      srcPtr[2] = info.data + 2;
      break;
    case GST_VIDEO_FORMAT_BGR:
      srcPtr[0] = info.data + 2;
      srcPtr[1] = info.data + 1;
      srcPtr[2] = info.data + 0;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  inputTensorSize = self->width * self->height * self->channels *
      get_tensor_type_size (self->input_data_type);

  switch (self->input_data_type) {
    case GST_TENSOR_DATA_TYPE_UINT8:{
      uint8_t *src_data;

      if (self->means[0] == 0.0 && self->stddevs[0] == 1.0) {
        src_data = info.data;
      } else {
        convert_image_remove_alpha_u8 (self->dest, self->width, self->height,
            self->channels, self->planar, srcPtr,
            GST_VIDEO_INFO_COMP_PSTRIDE (&self->video_info, 0),
            GST_VIDEO_INFO_PLANE_STRIDE (&self->video_info, 0),
            self->means, self->stddevs);
        src_data = self->dest;
      }

      status = api->CreateTensorWithDataAsOrtValue (self->memory_info, src_data,
          inputTensorSize, input_dims, num_dims,
          ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8, &input_tensor);
      break;
    }
    case GST_TENSOR_DATA_TYPE_FLOAT32:{
      convert_image_remove_alpha_f32 ((float *) self->dest, self->width,
          self->height,
          self->channels, self->planar, srcPtr,
          GST_VIDEO_INFO_COMP_PSTRIDE (&self->video_info, 0),
          GST_VIDEO_INFO_PLANE_STRIDE (&self->video_info, 0),
          self->means, self->stddevs);

      status = api->CreateTensorWithDataAsOrtValue (self->memory_info,
          (float *) self->dest,
          inputTensorSize, input_dims, num_dims,
          ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input_tensor);
      break;
    }
    default:
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Unsupported input datatype"));
      goto error;
  }

  if (status) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("Failed to create input tensor: %s", api->GetErrorMessage (status)));
    goto error;
  }

  output_tensors = g_new0 (OrtValue *, self->output_count);

  status = api->Run (self->session, NULL, (const char *const *) input_names,
      (const OrtValue * const *) &input_tensor, 1,
      (const char *const *) self->output_names, self->output_count,
      output_tensors);

  if (status) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("Failed to run inference: %s", api->GetErrorMessage (status)));
    goto error;
  }

  self->allocator->Free (self->allocator, input_names[0]);
  api->ReleaseValue (input_tensor);

  if (!output_tensors || self->output_count == 0) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("ONNX inference failed to produce outputs"));
    goto error;
  }


  tmeta = gst_buffer_add_tensor_meta (buf);
  tmeta->num_tensors = self->output_count;
  tmeta->tensors = g_new0 (GstTensor *, self->output_count);

  for (size_t i = 0; i < self->output_count; i++) {
    size_t j;
    ONNXTensorElementDataType tensor_type;
    size_t num_dims;
    size_t num_elements;
    void *tensor_data;

    status =
        api->GetTensorTypeAndShape (output_tensors[i], &output_tensor_info);
    if (status) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Failed to get tensor info: %s", api->GetErrorMessage (status)));
      goto error;
    }

    status = api->GetTensorElementType (output_tensor_info, &tensor_type);
    if (status) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Failed to get tensor type: %s", api->GetErrorMessage (status)));
      goto error;
    }

    status = api->GetDimensionsCount (output_tensor_info, &num_dims);
    if (status) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Failed to get dimensions count: %s",
              api->GetErrorMessage (status)));

      api->ReleaseTensorTypeAndShapeInfo (output_tensor_info);
      goto error;
    }

    int64_t *shape = (int64_t *) g_alloca (num_dims * sizeof (int64_t));
    status = api->GetDimensions (output_tensor_info, shape, num_dims);
    if (status) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Failed to get dimensions: %s", api->GetErrorMessage (status)));
      goto error;
    }

    GstTensor *tensor = gst_tensor_alloc (num_dims);
    tmeta->tensors[i] = tensor;
    tensor->id = self->output_ids[i];

    for (j = 0; j < num_dims; ++j)
      tensor->dims[j] = shape[j];

    status =
        api->GetTensorShapeElementCount (output_tensor_info, &num_elements);
    if (status) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Could not get the number of elements in the tensor: %s",
              api->GetErrorMessage (status)));
      goto error;
    }

    api->ReleaseTensorTypeAndShapeInfo (output_tensor_info);
    output_tensor_info = NULL;

    status = api->GetTensorMutableData (output_tensors[i], &tensor_data);
    if (status) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Failed to get tensor data: %s", api->GetErrorMessage (status)));
      goto error;
    }

    if (tensor_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
      size_t buffer_size = num_elements * sizeof (float);
      tensor->data = gst_buffer_new_allocate (NULL, buffer_size, NULL);
      gst_buffer_fill (tensor->data, 0, tensor_data, buffer_size);
      tensor->data_type = GST_TENSOR_DATA_TYPE_FLOAT32;
    } else if (tensor_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
      size_t buffer_size = num_elements * sizeof (int);
      tensor->data = gst_buffer_new_allocate (NULL, buffer_size, NULL);
      gst_buffer_fill (tensor->data, 0, tensor_data, buffer_size);
      tensor->data_type = GST_TENSOR_DATA_TYPE_INT32;
    } else {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Output tensor is not FLOAT32 or INT32, not supported"));
      goto error;
    }
  }

  // Clean up output tensors
  for (size_t i = 0; i < self->output_count; i++) {
    if (output_tensors[i])
      api->ReleaseValue (output_tensors[i]);
  }
  g_free (output_tensors);

  GST_TRACE_OBJECT (trans, "Num tensors:%zu", tmeta->num_tensors);
  gst_buffer_unmap (buf, &info);

  return GST_FLOW_OK;

error:
  if (status)
    api->ReleaseStatus (status);
  if (input_names[0])
    self->allocator->Free (self->allocator, input_names[0]);
  if (input_type_info)
    api->ReleaseTypeInfo (input_type_info);
  if (input_tensor)
    api->ReleaseValue (input_tensor);
  if (output_tensors) {
    for (size_t i = 0; i < self->output_count; i++) {
      if (output_tensors[i])
        api->ReleaseValue (output_tensors[i]);
    }
    g_free (output_tensors);
  }

  if (output_tensor_info)
    api->ReleaseTensorTypeAndShapeInfo (output_tensor_info);

  if (tmeta)
    gst_buffer_remove_meta (buf, (GstMeta *) tmeta);


  gst_buffer_unmap (buf, &info);

  return GST_FLOW_ERROR;
}
