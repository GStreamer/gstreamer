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
 * ## Example launch line
 *
 * Test image file, model file (SSD) and label file can be found here:
 * https://gitlab.collabora.com/gstreamer/onnx-models
 *
 * |[
 * GST_DEBUG=ssdobjectdetector:5 \
 * gst-launch-1.0 filesrc location=onnx-models/images/bus.jpg ! \
 * jpegdec ! videoconvert ! onnxinference execution-provider=cpu model-file=onnx-models/models/ssd_mobilenet_v1_coco.onnx ! \
 * ssdobjectdetector label-file=onnx-models/labels/COCO_classes.txt ! videoconvert ! imagefreeze ! autovideosink
 * ]|
 *
 * Note: in order for downstream tensor decoders to correctly parse the tensor
 * data in the GstTensorMeta, meta data must be attached to tensors. The
 * inference element gets this model metadata from the modelinfo file annexed
 * to the model. The modelinfo-helper tool can be used to create a modelinfo
 * file: https://gitlab.freedesktop.org/gstreamer/gstreamer/-/tree/main/subprojects/gst-devtools/modelinfo-helper
 *
 * ## Modelinfo example for ssd_mobilenet_v1_coco.onnx
 *
 * |[
 * [modelinfo]
 * version=1.0
 * group-id=ssd-mobilenet-v1-variant-1-out
 *
 * [image_tensor:0]
 * id=image_tensor_0
 * type=uint8
 * dims=-1,-1,-1,3
 * dir=input
 * ranges=0.0,255.0;0.0,255.0;0.0,255.0
 *
 * [detection_boxes:0]
 * id=ssd-mobilenet-v1-variant-1-out-boxes
 * type=float32
 * dims=-1,-1,4
 * dir=output
 *
 * [detection_classes:0]
 * id=ssd-mobilenet-v1-variant-1-out-classes
 * type=float32
 * dims=-1,-1
 * dir=output
 *
 * [detection_scores:0]
 * id=ssd-mobilenet-v1-variant-1-out-scores
 * type=float32
 * dims=-1,-1
 * dir=output
 *
 * [num_detections:0]
 * id=generic-variant-1-out-count
 * type=float32
 * dims=-1
 * dir=output
 * ]|
 *
 * The modelinfo file should be placed alongside the model file with a
 * `.modelinfo` suffix appended to the model filename. For example:
 *
 * |[
 * /path/to/model.onnx
 * /path/to/model.onnx.modelinfo
 * ]|
 *
 * As a convenience, sample models with their modelinfo files are available
 * here: https://gitlab.collabora.com/gstreamer/onnx-models/-/tree/master/models
 *
 * Since: 1.20
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstonnx-config.h"
#include "gstonnxinference.h"

#include <gst/gst.h>
#include <string.h>
#include <math.h>
#include <gst/analytics/analytics.h>

#include <onnxruntime_c_api.h>

#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#if HAVE_WINML
#include "gstonnx-winml.h"
#endif

#if HAVE_DIRECTML
#include "gstonnx-dml.h"
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
  GST_ONNX_EXECUTION_PROVIDER_MIGRAPHX,
  GST_ONNX_EXECUTION_PROVIDER_HIP,
  GST_ONNX_EXECUTION_PROVIDER_DIRECTML,
  GST_ONNX_EXECUTION_PROVIDER_VITIS_AI,
} GstOnnxExecutionProvider;

struct _GstOnnxInference
{
  GstBaseTransform basetransform;
  gchar *model_file;
  GstOnnxOptimizationLevel optimization_level;
  GstOnnxExecutionProvider execution_provider;
  gchar *device;
  gchar *model_cache_dir;
  gchar *vitisai_config_file;
  GstVideoInfo video_info;
  GstCaps *input_tensors_caps;
  GstCaps *output_tensors_caps;

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
  gsize input_tensor_size;
  size_t output_count;
  gchar **output_names;
  GQuark *output_ids;
  GstTensorDimOrder *output_dims_orders;
  GstTensorDataType input_data_type;
  bool fixedInputImageSize;
  double *scales;
  double *offsets;
  gchar *input_name;
  size_t input_dims_count;
  int64_t *input_dims_model;
  int64_t *input_dims_runtime;
  const gchar *registered_ep_name;
#if HAVE_DIRECTML
  GstOnnxDmlCtx *dml_ctx;
#endif
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
  PROP_DEVICE,
  PROP_MODEL_CACHE_DIR,
  PROP_VITISAI_CONFIG_FILE
};

#define GST_ONNX_INFERENCE_DEFAULT_EXECUTION_PROVIDER    GST_ONNX_EXECUTION_PROVIDER_CPU
#define GST_ONNX_INFERENCE_DEFAULT_OPTIMIZATION_LEVEL    GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED

static GstStaticPadTemplate gst_onnx_inference_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB, RGBP, GRAY8 }"))
    );

static GstStaticPadTemplate gst_onnx_inference_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB, RGBP, GRAY8 }"))
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
static gboolean
gst_onnx_inference_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
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
            "Enable basic optimizations (redundant node removals)",
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
      {GST_ONNX_EXECUTION_PROVIDER_CUDA,
            "CUDA execution provider",
          "cuda"},
      /**
       * GstOnnxExecutionProvider::vsi
       *
       * VeriSilicon NPU execution provider
       *
       * Since: 1.28
       */

      {GST_ONNX_EXECUTION_PROVIDER_VSI,
            "VeriSilicon NPU execution provider",
          "vsi"},

      /**
       * GstOnnxExecutionProvider::migraphx
       *
       * AMD MIGraphX execution provider
       *
       * Since: 1.30
       */

      {GST_ONNX_EXECUTION_PROVIDER_MIGRAPHX,
            "AMD MIGraphX execution provider",
          "migraphx"},

      /**
       * GstOnnxExecutionProvider::hip
       *
       * AMD HIP execution provider
       *
       * Since: 1.30
       */

      {GST_ONNX_EXECUTION_PROVIDER_HIP,
#if HAVE_ORT_REGISTER_EXECUTION_PROVIDER_LIBRARY
            "AMD HIP execution provider",
#else
            "AMD HIP execution provider (compiled out, requires ONNX Runtime >= 1.22)",
#endif
          "hip"},
      /**
       * GstOnnxExecutionProvider::dml
       *
       * Microsoft DirectML execution provider
       *
       * Since: 1.30
       */
#if HAVE_DIRECTML
      {GST_ONNX_EXECUTION_PROVIDER_DIRECTML,
          "Microsoft DirectML execution provider", "dml"},
#else
      {GST_ONNX_EXECUTION_PROVIDER_DIRECTML,
            "Microsoft DirectML execution provider (compiled out, will error)",
          "dml"},
#endif
      /**
       * GstOnnxExecutionProvider::vitisai
       *
       * AMD Vitis AI execution provider
       *
       * Since: 1.30
       */
      {GST_ONNX_EXECUTION_PROVIDER_VITIS_AI,
#if HAVE_VITISAI
            "AMD Vitis AI execution provider",
#else
            "AMD Vitis AI execution provider (compiled out, requires ONNX Runtime >= 1.18)",
#endif
          "vitisai"},
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

  /**
   * GstOnnxInference:device
   *
   * Device identifier to use for inference, interpreted per execution provider.
   * For CUDA, MIGraphX and HIP this is the integer device index (e.g. "0").
   * When NULL (the default) the execution provider default is used.
   *
   * Since: 1.30
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DEVICE,
      g_param_spec_string ("device",
          "Device",
          "Device identifier for inference, interpreted per execution provider "
          "(e.g. \"0\" for device index). NULL means use the provider default.",
          NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstOnnxInference:model-cache-dir
   *
   * Directory used by the execution provider to cache compiled models.
   * Currently supported by the MIGraphX, Vitis AI and HIP execution providers.
   *
   * Since: 1.30
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_MODEL_CACHE_DIR,
      g_param_spec_string ("model-cache-dir",
          "Model cache directory",
          "Directory used by the execution provider to cache compiled models",
          NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstOnnxInference:vitisai-config-file
   *
   * Vitis AI execution provider configuration file. This is passed to the
   * provider as its `config_file` option when execution-provider is "vitisai".
   *
   * Since: 1.30
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_VITISAI_CONFIG_FILE,
      g_param_spec_string ("vitisai-config-file",
          "Vitis AI configuration file",
          "Configuration file for the Vitis AI execution provider. NULL "
          "means use the provider default.", NULL,
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
  basetransform_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_onnx_inference_propose_allocation);
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
  self->input_tensors_caps = gst_caps_new_empty_simple ("video/x-raw");
  self->output_tensors_caps = gst_caps_new_empty_simple ("video/x-raw");

  self->execution_provider = GST_ONNX_EXECUTION_PROVIDER_CPU;
  self->device = NULL;
  self->model_cache_dir = NULL;
  self->vitisai_config_file = NULL;

  self->scales = NULL;
  self->offsets = NULL;

  self->height_dim = -1;
  self->width_dim = -1;
  self->channels_dim = -1;
  self->batch_dim = -1;

  /* Passthrough would propagate tensors caps upstream */
  gst_base_transform_set_prefer_passthrough (GST_BASE_TRANSFORM (self), FALSE);
}

static void
gst_onnx_inference_finalize (GObject * object)
{
  GstOnnxInference *self = GST_ONNX_INFERENCE (object);

  g_free (self->dest);
  g_free (self->model_file);
  g_free (self->device);
  g_free (self->model_cache_dir);
  g_free (self->vitisai_config_file);
  g_free (self->scales);
  g_free (self->offsets);
  g_free (self->input_dims_model);
  g_free (self->input_dims_runtime);
  gst_caps_unref (self->input_tensors_caps);
  gst_caps_unref (self->output_tensors_caps);
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
    case PROP_DEVICE:
      g_free (self->device);
      self->device = g_value_dup_string (value);
      break;
    case PROP_MODEL_CACHE_DIR:
      g_free (self->model_cache_dir);
      self->model_cache_dir = g_value_dup_string (value);
      break;
    case PROP_VITISAI_CONFIG_FILE:
      g_free (self->vitisai_config_file);
      self->vitisai_config_file = g_value_dup_string (value);
      break;
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
    case PROP_DEVICE:
      g_value_set_string (value, self->device);
      break;
    case PROP_MODEL_CACHE_DIR:
      g_value_set_string (value, self->model_cache_dir);
      break;
    case PROP_VITISAI_CONFIG_FILE:
      g_value_set_string (value, self->vitisai_config_file);
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
      return 0;
  };
}

static GstCaps *
gst_onnx_inference_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps)
{
  GstOnnxInference *self = GST_ONNX_INFERENCE (trans);
  GstCaps *other_caps;
  GstCaps *restrictions;
  bool has_session;

  GST_OBJECT_LOCK (self);
  has_session = self->session != NULL;
  restrictions = gst_caps_ref (self->input_tensors_caps);
  GST_OBJECT_UNLOCK (self);

  if (!has_session) {
    other_caps = gst_caps_ref (caps);
    gst_caps_unref (restrictions);
    goto done;
  }

  GST_LOG_OBJECT (self, "transforming caps %" GST_PTR_FORMAT, caps);

  GST_DEBUG_OBJECT (self, "Applying model input tensors caps restrictions: %"
      GST_PTR_FORMAT, self->input_tensors_caps);

  if (direction == GST_PAD_SINK) {
    /* Create tensors_caps from output_tensor_caps and intersect with
     * restrictions */
    GstCaps *tensors_caps = gst_caps_copy (self->output_tensors_caps);
    GstCaps *intersect = gst_caps_intersect_full (restrictions, tensors_caps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_replace (&restrictions, intersect);
    gst_caps_unref (tensors_caps);
    gst_caps_unref (intersect);
    other_caps = gst_caps_intersect_full (caps, restrictions,
        GST_CAPS_INTERSECT_FIRST);

  } else if (direction == GST_PAD_SRC) {
    /* Remove tensors from caps to prevent upstream propagation. */
    GstCaps *tmp_caps = gst_caps_copy (caps);

    if (!gst_caps_is_empty (tmp_caps)) {
      GstStructure *tstruct = gst_caps_get_structure (tmp_caps, 0);
      gst_structure_remove_field (tstruct, "tensors");
    }

    other_caps = gst_caps_intersect_full (tmp_caps, restrictions,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp_caps);
  } else {
    other_caps = gst_caps_intersect_full (caps, restrictions,
        GST_CAPS_INTERSECT_FIRST);
  }

  gst_caps_unref (restrictions);

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

  return -1;
}

static gboolean
gst_onnx_inference_set_tensordec_datatype (GstOnnxInference * self,
    ONNXTensorElementDataType dt, GstStructure * tensor_desc)
{
  GValue val = G_VALUE_INIT;
  GstTensorDataType gst_dt;

  g_value_init (&val, G_TYPE_STRING);

  if (dt > ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED &&
      dt <= ONNX_TENSOR_ELEMENT_DATA_TYPE_INT4) {
    gst_dt = onnx_data_type_to_gst (dt);
    g_value_set_string (&val, gst_tensor_data_type_get_name (gst_dt));
  } else {
    GST_ERROR_OBJECT (self, "Unexpected datatype: %d", dt);
    g_value_unset (&val);
    return FALSE;
  }

  gst_structure_take_value (tensor_desc, "type", &val);
  g_value_unset (&val);
  return TRUE;
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

  GST_CAT_LEVEL_LOG (onnx_runtime_debug, level, obj,
      "%s: %s", code_location, message);
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
  self->planar = FALSE;

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
      self->planar = FALSE;
      break;
    case 3:
      if (dims[0] == 1 || dims[0] == 3) {
        self->channels_dim = 0;
        if (dims[0] == 1) {
          *gst_format = "GRAY8";
          self->planar = FALSE;
        } else {
          *gst_format = "RGBP";
          self->planar = TRUE;
        }
        self->height_dim = 1;
        self->width_dim = 2;
      } else if (dims[2] == 1 || dims[2] == 3) {
        self->channels_dim = 2;
        if (dims[2] == 1) {
          *gst_format = "GRAY8";
          self->planar = FALSE;
        } else {
          *gst_format = "RGB";
          self->planar = FALSE;
        }
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
        self->planar = TRUE;
      } else if (dims[3] == 1 || dims[3] == 3) {
        self->height_dim = 1;
        self->width_dim = 2;
        self->channels_dim = 3;
        self->planar = FALSE;
      } else {
        GST_ERROR_OBJECT (self, "Don't know how to interpret dims");
        return FALSE;
      }

      if (dims[self->channels_dim] == 1) {
        *gst_format = "GRAY8";
        self->planar = FALSE;
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

static const gchar *
gst_onnx_inference_get_model_cache_dir (GstOnnxInference * self)
{
  /* env var fallback is only for use with the devenv */
  const char *cache_dir = self->model_cache_dir ? self->model_cache_dir
      : g_getenv ("GST_ORT_MODEL_CACHE_DIR");

  if (cache_dir && *cache_dir && g_mkdir_with_parents (cache_dir, 0755) < 0)
    GST_WARNING_OBJECT (self, "Failed to create model cache dir '%s', model "
        "compilation may fail or be uncached", cache_dir);

  return cache_dir;
}

static gboolean
gst_onnx_runtime_version_at_least (guint required_major, guint required_minor)
{
  const gchar *version = OrtGetApiBase ()->GetVersionString ();
  gchar *end = NULL;
  guint64 major, minor;

  major = g_ascii_strtoull (version, &end, 10);
  if (end == version || *end != '.')
    return FALSE;

  version = end + 1;
  minor = g_ascii_strtoull (version, &end, 10);
  if (end == version)
    return FALSE;

  return major > required_major ||
      (major == required_major && minor >= required_minor);
}

#if HAVE_ORT_REGISTER_EXECUTION_PROVIDER_LIBRARY
static gboolean
gst_onnx_inference_append_ep (GstOnnxInference * self, OrtSessionOptions * opts,
    const gchar * ep_name)
{
  const OrtEpDevice *const *ep_devices = NULL;
  const OrtEpDevice **target_devices = NULL;
  size_t num_ep_devices = 0;
  size_t num_target_devices = 0;
  const char *provider_options_keys[] = { "cache_dir" };
  const char *provider_options_values[1];
  size_t num_provider_options = 0;
  size_t i;
  OrtStatus *status;

  status = api->GetEpDevices (self->env, &ep_devices, &num_ep_devices);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to get EP devices: %s",
        api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    return FALSE;
  }

  target_devices = g_new (const OrtEpDevice *, num_ep_devices);
  for (i = 0; i < num_ep_devices; i++) {
    const char *dev_name = api->EpDevice_EpName (ep_devices[i]);
    if (g_strcmp0 (dev_name, ep_name) == 0) {
      // Apply card_idx filter if specified, otherwise pass all devices to
      // the EP and let it select whatever makes sense.
      if (self->device) {
        const OrtHardwareDevice *hwdev = api->EpDevice_Device (ep_devices[i]);
        const OrtKeyValuePairs *metadata = api->HardwareDevice_Metadata (hwdev);
        const char *card_idx_str = NULL;
        gint64 requested;
        gint64 actual;

        if (metadata) {
          const char *const *keys = NULL;
          const char *const *values = NULL;
          size_t num_entries = 0;
          size_t j;

          api->GetKeyValuePairs (metadata, &keys, &values, &num_entries);
          GST_LOG_OBJECT (self, "Metadata entries for EP %s: %" G_GSIZE_FORMAT,
              ep_name, num_entries);

          for (j = 0; j < num_entries; j++)
            GST_LOG_OBJECT (self, "  %s = %s", keys[j], values[j]);

          if (g_strcmp0 (ep_name, "hipgpu") == 0) {
            card_idx_str = api->GetKeyValue (metadata, "card_idx");
          } else if (g_strcmp0 (ep_name, "MIGraphXExecutionProvider") == 0) {
            /* MIGraphX EP on Windows reports DXGI LUID and adapter number.
             * Use adapter number here */
            card_idx_str = api->GetKeyValue (metadata, "DxgiAdapterNumber");
          }
        }

        if (!card_idx_str)
          continue;

        requested = g_ascii_strtoll (self->device, NULL, 10);
        actual = g_ascii_strtoll (card_idx_str, NULL, 10);
        GST_DEBUG_OBJECT (self,
            "card_idx=%" G_GINT64_FORMAT " for device %" G_GSIZE_FORMAT,
            actual, i);
        if (requested != actual)
          continue;
      }
      target_devices[num_target_devices++] = ep_devices[i];
    }
  }

  if (num_target_devices == 0) {
    if (self->device) {
      GST_ERROR_OBJECT (self,
          "No %s devices found matching card_idx %s", ep_name, self->device);
    } else {
      GST_ERROR_OBJECT (self, "No %s devices found", ep_name);
    }
    g_free (target_devices);
    return FALSE;
  }
  /*
   * Finally, append EP to session options.
   * Plugin EPs use their own option schema. Both the HIP and MIGraphX plugin
   * EPs call this option cache_dir at present. This may change in the future.
   * MIGraphX:
   *  - repo: https://github.com/onnxruntime/onnxruntime-ep-amdgpu
   *  - header: src/migraphx/mgx_options.h
   * HIP:
   *  - repo: https://github.com/ROCm/hip-ep/
   *  - header: morphizen/morphizen-core/include/morphizen/provider_option_keys.hpp
   */
  if (g_strcmp0 (ep_name, "hipgpu") == 0 ||
      g_strcmp0 (ep_name, "MIGraphXExecutionProvider") == 0) {
    const gchar *cache_dir = gst_onnx_inference_get_model_cache_dir (self);

    if (cache_dir && *cache_dir) {
      provider_options_values[0] = cache_dir;
      num_provider_options = 1;
    }
  }

  status =
      api->SessionOptionsAppendExecutionProvider_V2 (opts,
      self->env, target_devices, num_target_devices,
      num_provider_options ? provider_options_keys : NULL,
      num_provider_options ? provider_options_values : NULL,
      num_provider_options);
  g_free (target_devices);

  if (status) {
    GST_ERROR_OBJECT (self, "Failed to append %s EP: %s",
        ep_name, api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    return FALSE;
  }

  return TRUE;
}
#endif

typedef OrtStatus *(ORT_API_CALL * GstOrtAppendVsiNpuFunc) (OrtSessionOptions *
    options);

static GstOrtAppendVsiNpuFunc
gst_onnx_inference_get_vsi_npu_append_func (void)
{
#ifdef G_OS_WIN32
  HMODULE module = GetModuleHandleW (L"onnxruntime.dll");

  if (!module)
    return NULL;

  return (GstOrtAppendVsiNpuFunc) GetProcAddress (module,
      "OrtSessionOptionsAppendExecutionProvider_VSINPU");
#else
  return (GstOrtAppendVsiNpuFunc) dlsym (RTLD_DEFAULT,
      "OrtSessionOptionsAppendExecutionProvider_VSINPU");
#endif
}

static gboolean
gst_onnx_inference_start (GstBaseTransform * trans)
{
  GstOnnxInference *self = GST_ONNX_INFERENCE (trans);
  gboolean ret = FALSE;
  OrtStatus *status = NULL;
  OrtSessionOptions *session_options = NULL;
  OrtTypeInfo *input_type_info = NULL;
  size_t input_count = 0;
  const OrtTensorTypeAndShapeInfo *input_tensor_info = NULL;
  GraphOptimizationLevel onnx_optim;
  size_t num_input_dims;
  int64_t *input_dims;
  gsize *gst_input_dims;
  ONNXTensorElementDataType element_type;
  size_t i;
  const gchar *gst_format;
  GstAnalyticsModelInfo *modelinfo = NULL;
  const gchar *onnx_input_tensor_name = NULL;
  gchar *tensor_name = NULL;
  gdouble *input_mins;
  gdouble *input_maxs;

  if (!api) {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("ORT_API_VERSION %d not supported by ONNX runtime", ORT_API_VERSION));
    return FALSE;
  }

  GST_OBJECT_LOCK (self);
  if (self->session) {
    ret = TRUE;
    goto done;
  }

  if (self->model_file == NULL) {
    GST_OBJECT_UNLOCK (self);
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("model-file property not set"));
    goto error_no_lock;
  }

  if (self->model_cache_dir && *self->model_cache_dir &&
      self->execution_provider != GST_ONNX_EXECUTION_PROVIDER_MIGRAPHX &&
      self->execution_provider != GST_ONNX_EXECUTION_PROVIDER_HIP &&
      self->execution_provider != GST_ONNX_EXECUTION_PROVIDER_VITIS_AI) {
    GST_WARNING_OBJECT (self,
        "model-cache-dir is not supported by the selected execution provider");
  }

  modelinfo = gst_analytics_modelinfo_load (self->model_file);
  if (!modelinfo) {
    GST_ERROR_OBJECT (self, "Failed to load modelinfo for %s. "
        "This could be due to: file not found, unsupported version, "
        "or invalid file format.", self->model_file);
    goto error;
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
    GST_OBJECT_UNLOCK (self);
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("Failed to set optimization level: %s",
            api->GetErrorMessage (status)));
    goto error_no_lock;
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

      if (self->device) {
        const char *keys[] = { "device_id" };
        const char *values[] = { self->device };
        status = api->UpdateCUDAProviderOptions (cuda_options, keys, values, 1);
        if (status) {
          GST_ERROR_OBJECT (self, "Failed to set CUDA device_id: %s",
              api->GetErrorMessage (status));
          api->ReleaseCUDAProviderOptions (cuda_options);
          goto error;
        }
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
    {
      GstOrtAppendVsiNpuFunc append_vsi_npu =
          gst_onnx_inference_get_vsi_npu_append_func ();

      if (!append_vsi_npu) {
        GST_ERROR_OBJECT (self, "VSI NPU execution provider is unavailable");
        goto error;
      }
      status = append_vsi_npu (session_options);
      if (status) {
        GST_ERROR_OBJECT (self, "Failed to append VSI NPU provider: %s",
            api->GetErrorMessage (status));
        goto error;
      }
      status = api->DisableCpuMemArena (session_options);
      if (status) {
        GST_WARNING_OBJECT (self, "Failed to disable the CPU memory arena: %s",
            api->GetErrorMessage (status));
        api->ReleaseStatus (status);
      }
      break;
    }
    case GST_ONNX_EXECUTION_PROVIDER_VITIS_AI:
    {
#if HAVE_VITISAI
      const gchar *cache_dir = gst_onnx_inference_get_model_cache_dir (self);
      const char *keys[2];
      const char *values[2];
      size_t num_options = 0;

      if (self->vitisai_config_file && *self->vitisai_config_file) {
        keys[num_options] = "config_file";
        values[num_options++] = self->vitisai_config_file;
      }

      if (cache_dir && *cache_dir) {
        keys[num_options] = "cache_dir";
        values[num_options++] = cache_dir;
      }

      status =
          api->SessionOptionsAppendExecutionProvider_VitisAI (session_options,
          num_options ? keys : NULL, num_options ? values : NULL, num_options);
      if (status) {
        GST_ERROR_OBJECT (self, "Failed to append Vitis AI provider: %s",
            api->GetErrorMessage (status));
        goto error;
      }
      break;
#else
      GST_ERROR_OBJECT (self, "Vitis AI execution provider is unavailable, "
          "need ONNX Runtime >=1.18");
      goto error;
#endif
    }
    case GST_ONNX_EXECUTION_PROVIDER_MIGRAPHX:
#if !HAVE_WINML
    {
      const gchar *cache_dir = gst_onnx_inference_get_model_cache_dir (self);

      if (cache_dir && *cache_dir && gst_onnx_runtime_version_at_least (1, 23)) {
        /*
         * onnxruntime online docs are usually out of date, so you have to
         * check the headers. For example, migraphx_model_cache_dir is
         * documented here and was backported to onnxruntime 1.23:
         * core/providers/migraphx/migraphx_execution_provider_info.h
         */
        const char *keys[2] = { "migraphx_model_cache_dir", "device_id" };
        const char *values[2] = { cache_dir, self->device };
        size_t num_options = self->device ? 2 : 1;

        status = api->SessionOptionsAppendExecutionProvider (session_options,
            "MIGraphX", keys, values, num_options);
      } else {
        OrtMIGraphXProviderOptions migraphx_options;

        if (cache_dir && *cache_dir) {
          GST_WARNING_OBJECT (self,
              "model-cache-dir requires ONNX Runtime >= 1.23 for the "
              "MIGraphX execution provider; runtime version is %s",
              OrtGetApiBase ()->GetVersionString ());
        }

        memset (&migraphx_options, 0, sizeof (migraphx_options));

        if (self->device)
          migraphx_options.device_id =
              (int) g_ascii_strtoll (self->device, NULL, 10);

        status =
            api->SessionOptionsAppendExecutionProvider_MIGraphX
            (session_options, &migraphx_options);
      }
      if (status) {
        GST_ERROR_OBJECT (self, "Failed to create MIGraphX provider: %s",
            api->GetErrorMessage (status));
        goto error;
      }
    }
#else
    {
      ORTCHAR_T *lib_path_w;
      gchar *lib_path =
          gst_onnx_winml_ep_catalog_find ("MIGraphXExecutionProvider");
      if (!lib_path) {
        GST_ERROR_OBJECT (self, "Couldn't prepare MIGraphX EP via WindowsML");
        goto error;
      }

      lib_path_w =
          (ORTCHAR_T *) g_utf8_to_utf16 (lib_path, -1, NULL, NULL, NULL);
      status = api->RegisterExecutionProviderLibrary (self->env,
          "MIGraphXExecutionProvider", lib_path_w);
      g_free (lib_path_w);

      if (status) {
        GST_ERROR_OBJECT (self, "Failed to register MIGraphX library (%s): %s",
            lib_path, api->GetErrorMessage (status));
        g_free (lib_path);
        goto error;
      }
      g_free (lib_path);

      self->registered_ep_name = "MIGraphXExecutionProvider";

      if (!gst_onnx_inference_append_ep (self,
              session_options, "MIGraphXExecutionProvider")) {
        goto error;
      }
    }
#endif
      break;
    case GST_ONNX_EXECUTION_PROVIDER_HIP:
#if HAVE_ORT_REGISTER_EXECUTION_PROVIDER_LIBRARY
    {
      const gchar *ep_lib_path = g_getenv ("MORPHIZEN_EP_LIB");

      if (!api->RegisterExecutionProviderLibrary ||
          !api->UnregisterExecutionProviderLibrary || !api->GetEpDevices ||
          !api->SessionOptionsAppendExecutionProvider_V2) {
        GST_ERROR_OBJECT (self,
            "ONNX Runtime does not support dynamically loaded execution providers");
        goto error;
      }

      if (!ep_lib_path || !*ep_lib_path) {
        GST_ERROR_OBJECT (self,
            "MORPHIZEN_EP_LIB environment variable not set");
        goto error;
      }
      // First register the EP library.
#ifdef G_OS_WIN32
      {
        ORTCHAR_T *lib_path_w =
            (ORTCHAR_T *) g_utf8_to_utf16 (ep_lib_path, -1, NULL, NULL, NULL);
        status = api->RegisterExecutionProviderLibrary (self->env, "hipgpu",
            lib_path_w);
        g_free (lib_path_w);
      }
#else
      status = api->RegisterExecutionProviderLibrary (self->env, "hipgpu",
          ep_lib_path);
#endif
      if (status) {
        GST_ERROR_OBJECT (self, "Failed to register HIP EP library (%s): %s",
            ep_lib_path, api->GetErrorMessage (status));
        goto error;
      }

      self->registered_ep_name = "hipgpu";

      if (!gst_onnx_inference_append_ep (self, session_options, "hipgpu"))
        goto error;
    }
#else
      GST_ERROR_OBJECT (self,
          "HIP execution provider requires ONNX Runtime >= 1.22");
      goto error;
#endif
      break;
    case GST_ONNX_EXECUTION_PROVIDER_DIRECTML:
#if HAVE_DIRECTML
    {
      guint device_id = 0;
      GstD3D12Device *device12;
      GstOnnxDmlCtx *dml_ctx;

      if (self->device)
        device_id = (guint) g_ascii_strtoll (self->device, NULL, 10);

      device12 = gst_d3d12_device_new (device_id);
      if (!device12) {
        GST_ERROR_OBJECT (self,
            "Couldn't create D3D12 device with adapter index %d", device_id);
        goto error;
      }

      dml_ctx = gst_onnx_dml_create_context (device12, session_options);
      gst_object_unref (device12);
      if (!dml_ctx) {
        GST_ERROR_OBJECT (self,
            "Couldn't create DML context with adapter index %d", device_id);
        goto error;
      }

      self->dml_ctx = dml_ctx;
    }
#else
      GST_ERROR_OBJECT (self, "Compiled without DirectML support");
      goto error;
#endif
      break;
    default:
      break;
  }

  // Create session
  {
    ORTCHAR_T *model_file;
#ifdef G_OS_WIN32
    model_file = (ORTCHAR_T *) g_utf8_to_utf16 (self->model_file, -1, NULL,
        NULL, NULL);
#else
    model_file = self->model_file;
#endif
    status = api->CreateSession (self->env, model_file, session_options,
        &self->session);
#ifdef G_OS_WIN32
    g_free (model_file);
#endif
  }
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
  // Get input count
  status = api->SessionGetInputCount (self->session, &input_count);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to get input count: %s",
        api->GetErrorMessage (status));
    goto error;
  }

  if (input_count != 1) {
    GST_ERROR_OBJECT (self, "Only models with 1 input tensor are supported,"
        " but model has %zu inputs", input_count);
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
  } else {
    self->channels = 1;
  }


  GST_DEBUG_OBJECT (self, "height dim[%d]=%d, width dim[%d]=%d,"
      " channels dim[%d]=%d, batch_dim[%d]=%zu planar=%d",
      self->height_dim, self->height, self->width_dim, self->width,
      self->channels_dim, self->channels, self->batch_dim,
      self->batch_dim >= 0 ? gst_input_dims[self->batch_dim] : 0, self->planar);

  self->fixedInputImageSize = self->width > 0 && self->height > 0;

  status = api->SessionGetOutputCount (self->session, &self->output_count);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to retrieve output count: %s",
        api->GetErrorMessage (status));
    goto error;
  }
  GST_DEBUG_OBJECT (self, "Number of Output Nodes: %zu", self->output_count);

  if (self->output_count == 0) {
    GST_ERROR_OBJECT (self, "Model with 0 output nodes is not " "supported.");
    goto error;
  }

  status = api->GetTensorElementType (input_tensor_info, &element_type);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to get element type: %s",
        api->GetErrorMessage (status));
    goto error;
  }

  api->ReleaseTypeInfo (input_type_info);
  input_type_info = NULL;

  self->input_data_type = onnx_data_type_to_gst (element_type);
  if (self->input_data_type == -1) {
    GST_ERROR_OBJECT (self, "Unsupported input tensor data type %d",
        element_type);
    goto error;
  }

  /* Only u8 / f32 inputs are supported right now */
  switch (self->input_data_type) {
    case GST_TENSOR_DATA_TYPE_UINT8:
    case GST_TENSOR_DATA_TYPE_FLOAT32:
      break;
    default:
      GST_ERROR_OBJECT (self, "Unsupported input tensor data type %d",
          element_type);
      goto error;
  }

  /* Get input tensor name from ONNX file */
  status = api->SessionGetInputName (self->session, 0, self->allocator,
      (char **) &onnx_input_tensor_name);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to get input name: %s",
        api->GetErrorMessage (status));
    goto error;
  }

  tensor_name = gst_analytics_modelinfo_find_tensor_name (modelinfo,
      MODELINFO_DIRECTION_INPUT, 0, onnx_input_tensor_name,
      self->input_data_type, num_input_dims, gst_input_dims);

  if (!tensor_name) {
    gchar *dims_str = build_dims_str (num_input_dims, gst_input_dims);
    GST_ERROR_OBJECT (self,
        "Model info file doesn't contain info for input_tensor[0]:%s matching the"
        " type %s and dims %s", onnx_input_tensor_name,
        gst_tensor_data_type_get_name (self->input_data_type), dims_str);
    g_free (dims_str);
    if (onnx_input_tensor_name)
      self->allocator->Free (self->allocator, (char *) onnx_input_tensor_name);
    goto error;
  }

  /* Validation: modelinfo successfully matched dims and datatype from ONNX */
  GST_INFO_OBJECT (self,
      "Input tensor[0]:%s validated - modelinfo matches ONNX model (type: %s)",
      onnx_input_tensor_name,
      gst_tensor_data_type_get_name (self->input_data_type));

  /* Get per-channel scales and offsets from modelinfo */
  /* For video input, we assume uint8 pixel values in range [0, 255] */
  input_mins = g_alloca (sizeof (gdouble) * self->channels);
  input_maxs = g_alloca (sizeof (gdouble) * self->channels);
  for (i = 0; i < self->channels; i++) {
    input_mins[i] = 0.0;
    input_maxs[i] = 255.0;
  }

  if (!gst_analytics_modelinfo_get_input_scales_offsets (modelinfo,
          tensor_name, self->channels, input_mins, input_maxs,
          NULL, &self->scales, &self->offsets)) {
    GST_ERROR_OBJECT (self, "Failed to get scales/offsets for tensor %s",
        tensor_name);
    g_free (tensor_name);
    if (onnx_input_tensor_name)
      self->allocator->Free (self->allocator, (char *) onnx_input_tensor_name);
    goto error;
  }

  GST_INFO_OBJECT (self, "Input tensor normalization: %u channel(s)",
      self->channels);
  for (i = 0; i < self->channels; i++) {
    GST_DEBUG_OBJECT (self, "  Channel[%zu]: scale=%f, offset=%f", i,
        self->scales[i], self->offsets[i]);
  }

  g_free (tensor_name);

  self->input_name = (gchar *) onnx_input_tensor_name;
  self->input_dims_count = num_input_dims;
  self->input_dims_model =
      g_memdup2 (input_dims, num_input_dims * sizeof (int64_t));
  self->input_dims_runtime = g_new0 (int64_t, num_input_dims);

  /* Setting input tensor caps */
  self->input_tensors_caps = gst_caps_make_writable (self->input_tensors_caps);

  gst_caps_set_simple (self->input_tensors_caps, "pixel-aspect-ratio",
      GST_TYPE_FRACTION, 1, 1, NULL);

  gst_caps_set_simple (self->input_tensors_caps, "format", G_TYPE_STRING,
      gst_format, NULL);

  if (self->fixedInputImageSize)
    gst_caps_set_simple (self->input_tensors_caps, "width", G_TYPE_INT,
        self->width, "height", G_TYPE_INT, self->height, NULL);

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

  GValue v_tensors_set = G_VALUE_INIT;
  GstStructure *tensors_s = NULL;
  gchar *group_id = NULL;

  g_value_init (&v_tensors_set, GST_TYPE_UNIQUE_LIST);

  self->output_ids = g_new0 (GQuark, self->output_count);
  self->output_dims_orders = g_new0 (GstTensorDimOrder, self->output_count);

  for (i = 0; i < self->output_count; i++) {
    OrtTypeInfo *output_type_info = NULL;
    const OrtTensorTypeAndShapeInfo *output_tensor_info = NULL;
    size_t card;
    ONNXTensorElementDataType type;
    GstTensorDataType gst_data_type;
    size_t j;
    gchar *tensor_name = NULL;
    gchar *tensor_id = NULL;
    gsize *output_dims = NULL;


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

    gst_data_type = onnx_data_type_to_gst (type);
    if (gst_data_type == -1) {
      GST_ERROR_OBJECT (self, "Unsupported output tensor data type %d", type);
      api->ReleaseTypeInfo (output_type_info);
      goto error;
    }

    /* Get dimensions from ONNX */
    int64_t *shape = (int64_t *) g_alloca (card * sizeof (int64_t));
    status = api->GetDimensions (output_tensor_info, shape, card);
    if (status) {
      GST_ERROR_OBJECT (self, "Failed to get output tensor (%s) dimensions",
          self->output_names[i]);
      api->ReleaseTypeInfo (output_type_info);
      goto error;
    }

    output_dims = (gsize *) g_malloc0 (card * sizeof (gsize));
    for (j = 0; j < card; j++) {
      output_dims[j] = shape[j] > 0 ? shape[j] : G_MAXSIZE;
    }

    /* Look up tensor name in modelinfo */
    tensor_name = gst_analytics_modelinfo_find_tensor_name (modelinfo,
        MODELINFO_DIRECTION_OUTPUT, i, self->output_names[i],
        gst_data_type, card, output_dims);

    if (!tensor_name) {
      gchar *dims_str = build_dims_str (card, output_dims);
      GST_ERROR_OBJECT (self,
          "Model info file doesn't contain info for output_tensor[%zu]:%s matching the"
          " type %s and dims %s", i, self->output_names[i],
          gst_tensor_data_type_get_name (gst_data_type), dims_str);
      g_free (dims_str);
      g_free (output_dims);
      api->ReleaseTypeInfo (output_type_info);
      goto error;
    }

    /* Validation: modelinfo successfully matched dims and datatype from ONNX */
    GST_INFO_OBJECT (self,
        "Output tensor[%zu]:%s validated - modelinfo matches ONNX model "
        "(type: %s)", i, self->output_names[i],
        gst_tensor_data_type_get_name (gst_data_type));

    /* Get tensor ID from modelinfo */
    tensor_id = gst_analytics_modelinfo_get_id (modelinfo, tensor_name);
    if (!tensor_id) {
      GST_ERROR_OBJECT (self, "Model info doesn't have 'id' for tensor %s",
          tensor_name);
      g_free (tensor_name);
      g_free (output_dims);
      api->ReleaseTypeInfo (output_type_info);
      goto error;
    }

    GST_DEBUG_OBJECT (self, "Mapping output_tensor[%zu]:%s of type %s to id %s",
        i, self->output_names[i], gst_tensor_data_type_get_name (gst_data_type),
        tensor_id);

    self->output_ids[i] = g_quark_from_string (tensor_id);

    /* tensor description */
    GstStructure *tensor_desc = gst_structure_new_empty ("tensor/strided");

    /* Setting dims */
    GValue val_dims = G_VALUE_INIT, val = G_VALUE_INIT;
    GValue val_caps = G_VALUE_INIT;
    gst_value_array_init (&val_dims, card);
    g_value_init (&val, G_TYPE_INT);
    g_value_init (&val_caps, GST_TYPE_CAPS);

    for (j = 0; j < card; j++) {
      g_value_set_int (&val, output_dims[j] != G_MAXSIZE ? output_dims[j] : 0);
      gst_value_array_append_value (&val_dims, &val);
    }

    /* Get dims-order from modelinfo (defaults to row-major if not specified) */
    GstTensorDimOrder dims_order =
        gst_analytics_modelinfo_get_dims_order (modelinfo, tensor_name);
    self->output_dims_orders[i] = dims_order;
    const gchar *dims_order_str =
        dims_order ==
        GST_TENSOR_DIM_ORDER_COL_MAJOR ? "col-major" : "row-major";
    gst_structure_set (tensor_desc, "dims-order", G_TYPE_STRING, dims_order_str,
        "tensor-id", G_TYPE_STRING, g_quark_to_string (self->output_ids[i]),
        NULL);
    GST_INFO_OBJECT (self, "%s[dims-order]=%s",
        g_quark_to_string (self->output_ids[i]), dims_order_str);

    gst_structure_take_value (tensor_desc, "dims", &val_dims);
    g_value_unset (&val);

    /* Setting datatype */
    if (!gst_onnx_inference_set_tensordec_datatype (self, type, tensor_desc)) {
      GST_ERROR_OBJECT (self,
          "Failed to datatype for output tensor (%s) dimensions",
          self->output_names[i]);

      gst_structure_free (tensor_desc);
      g_value_unset (&v_tensors_set);
      api->ReleaseTypeInfo (output_type_info);
      goto error;
    }

    /* tensor caps */
    GstCaps *tensor_caps = gst_caps_new_full (tensor_desc, NULL);

    /* Append tensor caps to set */
    gst_value_set_caps (&val_caps, tensor_caps);
    gst_caps_unref (tensor_caps);
    gst_value_unique_list_append_and_take_value (&v_tensors_set, &val_caps);

    /* Get group-id from modelinfo on last tensor */
    if (i == (self->output_count - 1)) {
      group_id = gst_analytics_modelinfo_get_group_id (modelinfo);
      if (!group_id) {
        GST_ERROR_OBJECT (self, "Model info doesn't have 'group-id'");
        g_free (tensor_name);
        g_free (tensor_id);
        g_free (output_dims);
        api->ReleaseTypeInfo (output_type_info);
        goto error;
      }
    }

    /* Cleanup */
    g_free (tensor_name);
    g_free (tensor_id);
    g_free (output_dims);
    api->ReleaseTypeInfo (output_type_info);
  }

  /* Setting output tensors caps */
  self->output_tensors_caps =
      gst_caps_make_writable (self->output_tensors_caps);

  if (!tensors_s)
    tensors_s = gst_structure_new_empty ("tensorgroups");
  GstStructure *output_caps_struct;

  gst_structure_set_value (tensors_s, group_id, &v_tensors_set);
  output_caps_struct = gst_caps_get_structure (self->output_tensors_caps, 0);
  gst_structure_set (output_caps_struct, "tensors", GST_TYPE_STRUCTURE,
      tensors_s, NULL);
  gst_structure_free (tensors_s);
  g_value_unset (&v_tensors_set);

  if (group_id)
    g_free (group_id);

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
  if (modelinfo)
    gst_analytics_modelinfo_free (modelinfo);
  GST_OBJECT_UNLOCK (self);

  return ret;

error:
  GST_OBJECT_UNLOCK (self);
error_no_lock:
  if (status)
    api->ReleaseStatus (status);
  if (input_type_info)
    api->ReleaseTypeInfo (input_type_info);
  if (session_options)
    api->ReleaseSessionOptions (session_options);

  if (modelinfo)
    gst_analytics_modelinfo_free (modelinfo);

  gst_onnx_inference_stop (trans);
  return ret;

}

static gboolean
gst_onnx_inference_stop (GstBaseTransform * trans)
{
  GstOnnxInference *self = GST_ONNX_INFERENCE (trans);
  size_t i;

  GST_OBJECT_LOCK (self);

  // Clean up output names
  if (self->output_names) {
    for (i = 0; i < self->output_count; i++) {
      if (self->output_names[i])
        self->allocator->Free (self->allocator, self->output_names[i]);
    }
  }
  g_free (self->output_names);
  self->output_names = NULL;

  g_free (self->output_ids);
  self->output_ids = NULL;
  g_free (self->output_dims_orders);
  self->output_dims_orders = NULL;
  self->output_count = 0;

  if (self->memory_info)
    api->ReleaseMemoryInfo (self->memory_info);
  self->memory_info = NULL;

  if (self->session)
    api->ReleaseSession (self->session);
  self->session = NULL;

  /* Free cached input tensor metadata */
  if (self->input_name) {
    self->allocator->Free (self->allocator, self->input_name);
    self->input_name = NULL;
  }
  g_free (self->input_dims_model);
  self->input_dims_model = NULL;
  g_free (self->input_dims_runtime);
  self->input_dims_runtime = NULL;
  self->input_dims_count = 0;

  self->allocator = NULL;

  if (self->env) {
#if HAVE_ORT_REGISTER_EXECUTION_PROVIDER_LIBRARY
    if (self->registered_ep_name) {
      if (api->UnregisterExecutionProviderLibrary)
        (void) api->UnregisterExecutionProviderLibrary (self->env,
            self->registered_ep_name);
    }
    self->registered_ep_name = NULL;
#endif
    api->ReleaseEnv (self->env);
  }
  self->env = NULL;

  g_free (self->dest);
  self->dest = NULL;
  self->input_tensor_size = 0;
  g_free (self->scales);
  self->scales = NULL;
  g_free (self->offsets);
  self->offsets = NULL;

  gst_caps_unref (self->input_tensors_caps);
  self->input_tensors_caps = gst_caps_new_empty_simple ("video/x-raw");
  gst_caps_unref (self->output_tensors_caps);
  self->output_tensors_caps = gst_caps_new_empty_simple ("video/x-raw");

#if HAVE_DIRECTML
  g_clear_pointer (&self->dml_ctx, gst_onnx_dml_free_context);
#endif

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

  gsize element_size = get_tensor_type_size (self->input_data_type);
  gsize input_tensor_size;

  g_assert (element_size != 0);

  /* Use GLib's checked multiplication to prevent overflow */
  if (!g_size_checked_mul (&input_tensor_size, self->video_info.width,
          self->video_info.height) ||
      !g_size_checked_mul (&input_tensor_size, input_tensor_size,
          self->channels)
      || !g_size_checked_mul (&input_tensor_size, input_tensor_size,
          element_size)) {
    GST_ERROR_OBJECT (self,
        "Integer overflow in input tensor size: %dx%d pixels, %u channels, %zu bytes per element",
        self->video_info.width, self->video_info.height, self->channels,
        element_size);
    return FALSE;
  }

  if (self->dest == NULL || self->input_tensor_size != input_tensor_size) {
    g_free (self->dest);
    self->dest = g_malloc (input_tensor_size);
  }
  self->width = self->video_info.width;
  self->height = self->video_info.height;
  self->input_tensor_size = input_tensor_size;

  /* Resolve dynamic input dimensions and validate fixed ones */
  memcpy (self->input_dims_runtime, self->input_dims_model,
      self->input_dims_count * sizeof (int64_t));

  if (self->batch_dim >= 0)
    self->input_dims_runtime[self->batch_dim] = 1;

  if (self->input_dims_runtime[self->height_dim] >= 0) {
    if (self->input_dims_runtime[self->height_dim] != self->height) {
      GST_ERROR_OBJECT (self, "Caps have height %d, but model expects %"
          G_GINT64_FORMAT, self->height,
          self->input_dims_runtime[self->height_dim]);
      return FALSE;
    }
  } else {
    self->input_dims_runtime[self->height_dim] = self->height;
  }

  if (self->input_dims_runtime[self->width_dim] >= 0) {
    if (self->input_dims_runtime[self->width_dim] != self->width) {
      GST_ERROR_OBJECT (self, "Caps have width %d, but model expects %"
          G_GINT64_FORMAT, self->width,
          self->input_dims_runtime[self->width_dim]);
      return FALSE;
    }
  } else {
    self->input_dims_runtime[self->width_dim] = self->width;
  }

  return TRUE;
}

static gboolean
gst_onnx_inference_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  if (!GST_BASE_TRANSFORM_CLASS
      (gst_onnx_inference_parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

#define CONVERT_INTERLEAVED_FUNC(name, type, ncomps, clamp)                 \
static void                                                                 \
convert_image_ ##name##_##type (type * dst, const GstVideoFrame * vframe,   \
    const gdouble * scales, const gdouble * offsets)                        \
{                                                                           \
  const guint8 *src = GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);               \
  gsize height = GST_VIDEO_FRAME_HEIGHT (vframe);                           \
  gsize width = GST_VIDEO_FRAME_WIDTH (vframe);                             \
  gsize stride = GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0);                  \
  gsize row_size = width * ncomps;                                          \
                                                                            \
  for (size_t y = 0; y < height; y++) {                                     \
    for (size_t x = 0; x < width; x++) {                                    \
      for (size_t c = 0; c < ncomps; c++) {                                 \
        dst[c] = clamp (src[c] * scales[c] + offsets[c]);                   \
      }                                                                     \
      src += ncomps;                                                        \
      dst += ncomps;                                                        \
    }                                                                       \
    src += stride - row_size;                                               \
  }                                                                         \
}

#define CONVERT_PLANAR_FUNC(name, type, ncomps, clamp)                      \
static void                                                                 \
convert_image_ ##name##_##type (type * dst, const GstVideoFrame * vframe,   \
    const gdouble * scales, const gdouble * offsets)                        \
{                                                                           \
  gsize height = GST_VIDEO_FRAME_HEIGHT (vframe);                           \
  gsize width = GST_VIDEO_FRAME_WIDTH (vframe);                             \
                                                                            \
  for (size_t c = 0; c < ncomps; c++) {                                     \
    gsize stride = GST_VIDEO_FRAME_PLANE_STRIDE (vframe, c);                \
    const guint8 *src = GST_VIDEO_FRAME_PLANE_DATA (vframe, c);             \
    gsize row_size = width;                                                 \
    for (size_t y = 0; y < height; y++) {                                   \
      for (size_t x = 0; x < width; x++) {                                  \
        *dst = clamp (*src * scales[c] + offsets[c]);                       \
        dst++;                                                              \
        src++;                                                              \
      }                                                                     \
      src += stride - row_size;                                             \
    }                                                                       \
  }                                                                         \
}

#define CLAMP_U8(x) ((uint8_t) CLAMP((x), 0.0, 255.0))
#define CLAMP_F32(x) (x)

CONVERT_INTERLEAVED_FUNC (gray, uint8_t, 1, CLAMP_U8);
CONVERT_INTERLEAVED_FUNC (gray, float, 1, CLAMP_F32);
CONVERT_INTERLEAVED_FUNC (rgb, uint8_t, 3, CLAMP_U8);
CONVERT_INTERLEAVED_FUNC (rgb, float, 3, CLAMP_F32);

CONVERT_PLANAR_FUNC (rgbp, uint8_t, 3, CLAMP_U8);
CONVERT_PLANAR_FUNC (rgbp, float, 3, CLAMP_F32);

static GstFlowReturn
gst_onnx_inference_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstOnnxInference *self = GST_ONNX_INFERENCE (trans);
  GstVideoFrame vframe = GST_VIDEO_FRAME_INIT;
  OrtStatus *status = NULL;
  OrtValue *input_tensor = NULL;
  OrtValue **output_tensors = NULL;
  GstTensorMeta *tmeta = NULL;
  OrtTensorTypeAndShapeInfo *output_tensor_info = NULL;

  if (!gst_video_frame_map (&vframe, &self->video_info, buf,
          GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ELEMENT_ERROR (trans, STREAM, FAILED, (NULL),
        ("Could not map input buffer"));
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (self, "Input dimensions: %" G_GINT64_FORMAT
      ":%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT ":%" G_GINT64_FORMAT,
      self->input_dims_count > 0 ? self->input_dims_runtime[0] : -1,
      self->input_dims_count > 1 ? self->input_dims_runtime[1] : -1,
      self->input_dims_count > 2 ? self->input_dims_runtime[2] : -1,
      self->input_dims_count > 3 ? self->input_dims_runtime[3] : -1);

  switch (self->input_data_type) {
    case GST_TENSOR_DATA_TYPE_UINT8:{
      uint8_t *src_data;

      gboolean needs_conversion = FALSE;

      /* Check if conversion is needed based on scales/offsets */
      for (int32_t i = 0; i < self->channels; i++) {
        if (fabs (self->scales[i] - 1.0) > 0.001
            || fabs (self->offsets[i]) > 0.001) {
          needs_conversion = TRUE;
          break;
        }
      }
      /* Check if conversion is needed based on strides / plane offsets.
       * ONNX needs tightly packed data */
      void (*convert) (guint8 * dst, const GstVideoFrame * vframe,
          const gdouble * scales, const gdouble * offsets) = NULL;
      switch (GST_VIDEO_FRAME_FORMAT (&vframe)) {
        case GST_VIDEO_FORMAT_RGB:
          needs_conversion = needs_conversion
              || GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0) != self->width * 3;
          convert = convert_image_rgb_uint8_t;
          break;
        case GST_VIDEO_FORMAT_RGBP:
          needs_conversion = needs_conversion ||
              GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0) != self->width ||
              GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 1) != self->width ||
              GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 2) != self->width ||
              GST_VIDEO_FRAME_PLANE_DATA (&vframe,
              1) != (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe,
              0) + self->width * self->height
              || GST_VIDEO_FRAME_PLANE_DATA (&vframe,
              2) != (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe,
              1) + self->width * self->height;
          convert = convert_image_rgbp_uint8_t;
          break;
        case GST_VIDEO_FORMAT_GRAY8:
          needs_conversion = needs_conversion
              || GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0) != self->width;
          convert = convert_image_gray_uint8_t;
          break;
        default:
          g_assert_not_reached ();
          break;
      }

      if (!needs_conversion) {
        src_data = GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0);
      } else {
        convert (self->dest, &vframe, self->scales, self->offsets);
        src_data = self->dest;
      }

      status = api->CreateTensorWithDataAsOrtValue (self->memory_info, src_data,
          self->input_tensor_size, self->input_dims_runtime,
          self->input_dims_count,
          ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8, &input_tensor);
      break;
    }
    case GST_TENSOR_DATA_TYPE_FLOAT32:{
      /* F32 always needs conversion for now until we get suitable
       * GstVideoFormats */
      switch (GST_VIDEO_FRAME_FORMAT (&vframe)) {
        case GST_VIDEO_FORMAT_RGB:
          convert_image_rgb_float ((float *) self->dest, &vframe, self->scales,
              self->offsets);
          break;
        case GST_VIDEO_FORMAT_RGBP:
          convert_image_rgbp_float ((float *) self->dest, &vframe, self->scales,
              self->offsets);
          break;
        case GST_VIDEO_FORMAT_GRAY8:
          convert_image_gray_float ((float *) self->dest, &vframe, self->scales,
              self->offsets);
          break;
        default:
          g_assert_not_reached ();
          break;
      }

      status = api->CreateTensorWithDataAsOrtValue (self->memory_info,
          (float *) self->dest,
          self->input_tensor_size, self->input_dims_runtime,
          self->input_dims_count,
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

  status =
      api->Run (self->session, NULL, (const char *const *) &self->input_name,
      (const OrtValue * const *) &input_tensor, 1,
      (const char *const *) self->output_names, self->output_count,
      output_tensors);

  if (status) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("Failed to run inference: %s", api->GetErrorMessage (status)));
    goto error;
  }

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
    tensor->dims_order = self->output_dims_orders[i];

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

  GST_TRACE_OBJECT (trans, "Num tensors:%zu", self->output_count);
  gst_video_frame_unmap (&vframe);

  return GST_FLOW_OK;

error:
  if (status)
    api->ReleaseStatus (status);
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


  gst_video_frame_unmap (&vframe);

  return GST_FLOW_ERROR;
}
