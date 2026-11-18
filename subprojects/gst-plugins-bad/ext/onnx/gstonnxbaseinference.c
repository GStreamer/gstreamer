/* GStreamer
 * Copyright (C) 2023-2025 Collabora Ltd.
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#include "gstonnx-config.h"
#include "gstonnx-utils.h"
#include "gstonnxbaseinference.h"

#include <gst/gst.h>
#include <string.h>

#if HAVE_WINML
#include "gstonnx-winml.h"
#endif

#if HAVE_DIRECTML
#include "gstonnximporter-dml.h"
#include "gstonnxexporter-dml.h"
#endif

#if HAVE_GST_HIP
#include <gst/hip/gsthip.h>
#include "gstonnximporter-hip.h"
#include "gstonnxexporter-hip.h"
#endif

#include "gstonnxexporter-cpu.h"

#define HIP_DYNAMIC_EP_NAME "hipgpu"
#define MIGRAPHX_DYNAMIC_EP_NAME "MIGraphXExecutionProvider"
#define GST_ONNX_MAX_OUTPUT_COUNT 128
/* minimum version we require because of APIs we have compiled-in support for */
#define MIN_ORT_API_VERSION 16

static int gst_min_ort_api_version (void);

typedef enum
{
  GST_ONNX_OPTIMIZATION_LEVEL_DISABLE_ALL,
  GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_BASIC,
  GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED,
  GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_ALL,
} GstOnnxOptimizationLevel;

static const OrtApi *api = NULL;
static const OrtApiBase *api_base = NULL;
static guint loaded_api_version = 0;

#ifdef G_OS_WIN32
static HMODULE onnxruntime_module = NULL;
#else
static void *onnxruntime_module = NULL;
#endif

typedef const OrtApiBase *(ORT_API_CALL * GstOrtGetApiBaseFunc) (void);

GST_DEBUG_CATEGORY (onnx_base_inference_debug);
GST_DEBUG_CATEGORY (onnx_runtime_debug);

#define GST_CAT_DEFAULT onnx_base_inference_debug

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

#define GST_ONNX_INFERENCE_DEFAULT_EXECUTION_PROVIDER GST_ONNX_EXECUTION_PROVIDER_CPU
#define GST_ONNX_INFERENCE_DEFAULT_OPTIMIZATION_LEVEL GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED

#define GST_TYPE_ONNX_OPTIMIZATION_LEVEL (gst_onnx_optimization_level_get_type ())
static GType
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

#define GST_TYPE_ONNX_EXECUTION_PROVIDER (gst_onnx_execution_provider_get_type ())
static GType
gst_onnx_execution_provider_get_type (void)
{
  static GType onnx_execution_type = 0;

  if (g_once_init_enter (&onnx_execution_type)) {
    static GEnumValue execution_provider_types[] = {
      {GST_ONNX_EXECUTION_PROVIDER_CPU, "CPU execution provider", "cpu"},
      {GST_ONNX_EXECUTION_PROVIDER_CUDA, "CUDA execution provider", "cuda"},
      /**
       * GstOnnxExecutionProvider::vsi
       *
       * VeriSilicon NPU execution provider
       *
       * Since: 1.28
       */
      {GST_ONNX_EXECUTION_PROVIDER_VSI,
          "VeriSilicon NPU execution provider", "vsi"},

      /**
       * GstOnnxExecutionProvider::migraphx
       *
       * AMD MIGraphX execution provider
       *
       * Since: 1.30
       */
      {GST_ONNX_EXECUTION_PROVIDER_MIGRAPHX,
          "AMD MIGraphX execution provider", "migraphx"},

      /**
       * GstOnnxExecutionProvider::hip
       *
       * AMD HIP execution provider
       *
       * Since: 1.30
       */
      {GST_ONNX_EXECUTION_PROVIDER_HIP,
#if ORT_API_VERSION >= 22
#if MIN_ORT_API_VERSION < 22
#undef MIN_ORT_API_VERSION
#define MIN_ORT_API_VERSION 22
#endif
            "AMD HIP execution provider",
#else
            "AMD HIP execution provider (compiled out, requires ONNX Runtime >= 1.22)",
#endif
          "hip"},

#if HAVE_DIRECTML
      /**
       * GstOnnxExecutionProvider::dml
       *
       * Microsoft DirectML execution provider
       *
       * Since: 1.30
       */
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
#if ORT_API_VERSION >= 18
#if MIN_ORT_API_VERSION < 18
#undef MIN_ORT_API_VERSION
#define MIN_ORT_API_VERSION 18
#endif
            "AMD Vitis AI execution provider",
#else
            "AMD Vitis AI execution provider (compiled out, requires ONNX Runtime >= 1.18)",
#endif
          "vitisai"},
      /**
       * GstOnnxExecutionProvider::coreml
       *
       * Apple Core ML execution provider
       *
       * Since: 1.30
       */
      {GST_ONNX_EXECUTION_PROVIDER_COREML,
#if defined(__APPLE__) && ORT_API_VERSION >= 21
            "Apple Core ML execution provider",
#else
            "Apple Core ML execution provider (compiled out, requires macOS and ONNX Runtime >= 1.21)",
#endif
          "coreml"},
      {0, NULL, NULL},
    };

    GType temp = g_enum_register_static ("GstOnnxExecutionProvider",
        execution_provider_types);

    g_once_init_leave (&onnx_execution_type, temp);
  }

  return onnx_execution_type;
}

struct _GstOnnxBaseInferencePrivate
{
  gchar *model_file;
  GstOnnxOptimizationLevel optimization_level;
  GstOnnxExecutionProvider execution_provider;
  gchar *device;
  gchar *model_cache_dir;
  gchar *vitisai_config_file;

  GRecMutex context_lock;

  OrtEnv *env;
  OrtSession *session;
  OrtAllocator *allocator;

  GPtrArray *input_tensors;
  GPtrArray *output_tensors;
  GPtrArray *result_tensors;

  GArray *model_inputs;
  gchar **input_names;

  size_t output_count;
  gchar **output_names;
  GQuark *output_ids;
  GstTensorDimOrder *output_dims_orders;
  GstOnnxExporter *exporter;
  GstStructure *output_tensorgroups;

#if HAVE_DIRECTML
  GstD3D12Device *device12;
#endif

#if HAVE_GST_HIP
  GstHipDevice *device_hip;
#endif

  /* DXGI adapter luid of selected device on Windows */
  gint64 adapter_luid;
  /* Device ID selected for the execution provider */
  guint selected_device_id;
  /* PCI bus ID of plugin loaded execution provider on Linux */
  gchar *pci_bus_id;
};

static void gst_onnx_base_inference_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_onnx_base_inference_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_onnx_base_inference_finalize (GObject * object);

static void gst_onnx_base_inference_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_onnx_base_inference_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static gboolean gst_onnx_base_inference_start (GstBaseTransform * trans);
static gboolean gst_onnx_base_inference_stop (GstBaseTransform * trans);
static GstFlowReturn gst_onnx_base_inference_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);
static void gst_onnx_base_inference_clear_result_tensors (GstOnnxBaseInference *
    self);
static GstFlowReturn
gst_onnx_base_inference_end_inference_default (GstOnnxBaseInference * self,
    GstBuffer * buf, GPtrArray * results, gsize num_tensors,
    gboolean attach_meta);

/**
 * GstOnnxBaseInference:
 *
 * Since: 1.30
 */
#define gst_onnx_base_inference_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GstOnnxBaseInference,
    gst_onnx_base_inference, GST_TYPE_BASE_TRANSFORM);

static void
gst_onnx_base_inference_class_init (GstOnnxBaseInferenceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GParamSpec *pspec;

  gobject_class->set_property = gst_onnx_base_inference_set_property;
  gobject_class->get_property = gst_onnx_base_inference_get_property;
  gobject_class->finalize = gst_onnx_base_inference_finalize;

  /**
   * GstOnnxBaseInference:model-file
   *
   * Path to the ONNX model file.
   *
   * A modelinfo file describing the model's input and output tensors is
   * required alongside it, and is looked up as `<model-file>.modelinfo`
   * first, then as `<model-file without extension>.modelinfo`. For
   * `/path/model.onnx` that is `/path/model.onnx.modelinfo`, then
   * `/path/model.modelinfo`.
   *
   * Use the modelinfo-generator.py tool to generate one.
   *
   * Since: 1.24
   */
  pspec = g_param_spec_string ("model-file",
      "ONNX model file",
      "Path to the ONNX model file. A companion modelinfo file is "
      "required, looked up as <model-file>.modelinfo then "
      "<model-file without extension>.modelinfo", NULL, (GParamFlags)
      (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  gst_param_spec_set_kind (pspec, gst_structure_new (GST_PROP_KIND_LOCATION,
          GST_PROP_KIND_FIELD_SUBTYPE, G_TYPE_STRING,
          GST_PROP_KIND_SUBTYPE_PATH, GST_PROP_KIND_FIELD_EXTENSIONS,
          G_TYPE_STRING, "onnx", NULL));
  g_object_class_install_property (gobject_class, PROP_MODEL_FILE, pspec);

  /**
    * GstOnnxBaseInference:optimization-level
    *
    * ONNX optimization level
    *
    * Since: 1.24
    */
  g_object_class_install_property (gobject_class, PROP_OPTIMIZATION_LEVEL,
      g_param_spec_enum ("optimization-level",
          "Optimization level",
          "ONNX optimization level",
          GST_TYPE_ONNX_OPTIMIZATION_LEVEL,
          GST_ONNX_INFERENCE_DEFAULT_OPTIMIZATION_LEVEL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstOnnxBaseInference:execution-provider
   *
   * ONNX execution provider
   *
   * Since: 1.24
   */
  g_object_class_install_property (gobject_class, PROP_EXECUTION_PROVIDER,
      g_param_spec_enum ("execution-provider",
          "Execution provider",
          "ONNX execution provider",
          GST_TYPE_ONNX_EXECUTION_PROVIDER,
          GST_ONNX_INFERENCE_DEFAULT_EXECUTION_PROVIDER,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstOnnxBaseInference:device
   *
   * Device identifier to use for inference, interpreted per execution provider.
   * For CUDA, MIGraphX and HIP this is the integer device index (e.g. "0").
   * For Core ML this is the set of compute units the model can run on:
   * "CPUOnly", "CPUAndGPU", "CPUAndNeuralEngine" or "ALL".
   * When NULL (the default) the execution provider default is used.
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device",
          "Device",
          "Device identifier for inference, interpreted per execution provider "
          "(e.g. \"0\" for device index). NULL means use the provider default.",
          NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstOnnxBaseInference:model-cache-dir
   *
   * Directory used by the execution provider to cache compiled models.
   * Currently supported by the MIGraphX, Vitis AI, HIP and Core ML execution
   * providers.
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_MODEL_CACHE_DIR,
      g_param_spec_string ("model-cache-dir",
          "Model cache directory",
          "Directory used by the execution provider to cache compiled models",
          NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstOnnxBaseInference:vitisai-config-file
   *
   * Vitis AI execution provider configuration file. This is passed to the
   * provider as its `config_file` option when execution-provider is "vitisai".
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_VITISAI_CONFIG_FILE,
      g_param_spec_string ("vitisai-config-file",
          "Vitis AI configuration file",
          "Configuration file for the Vitis AI execution provider. NULL "
          "means use the provider default.", NULL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_onnx_base_inference_set_context);

  trans_class->query = GST_DEBUG_FUNCPTR (gst_onnx_base_inference_query);
  trans_class->start = GST_DEBUG_FUNCPTR (gst_onnx_base_inference_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_onnx_base_inference_stop);
  trans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_onnx_base_inference_transform_ip);

  klass->end_inference =
      GST_DEBUG_FUNCPTR (gst_onnx_base_inference_end_inference_default);

  gst_type_mark_as_plugin_api (GST_TYPE_ONNX_OPTIMIZATION_LEVEL, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_ONNX_EXECUTION_PROVIDER, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_ONNX_BASE_INFERENCE, 0);

  GST_DEBUG_CATEGORY_INIT (onnx_base_inference_debug, "onnxbaseinference", 0,
      "onnxbaseinference");
  GST_DEBUG_CATEGORY_INIT (onnx_runtime_debug, "onnxruntime", 0, "onnxruntime");
}

static void
clear_model_input (GstOnnxModelInput * input)
{
  g_free (input->onnx_name);
  g_free (input->modelinfo_name);
}

static void
clear_exporter_config (GstOnnxExporterConfig * config)
{
  g_clear_pointer (&config->dims, g_free);
}

static void
release_ort_value (OrtValue * data)
{
  if (data && api)
    api->ReleaseValue (data);
}

static void
gst_onnx_base_inference_init (GstOnnxBaseInference * self)
{
  GstOnnxBaseInferencePrivate *priv;

  self->priv = priv = gst_onnx_base_inference_get_instance_private (self);

  self->execution_provider = GST_ONNX_INFERENCE_DEFAULT_EXECUTION_PROVIDER;
  priv->optimization_level = GST_ONNX_INFERENCE_DEFAULT_OPTIMIZATION_LEVEL;
  priv->model_inputs = g_array_new (FALSE, TRUE, sizeof (GstOnnxModelInput));
  g_array_set_clear_func (priv->model_inputs,
      (GDestroyNotify) clear_model_input);
  priv->input_tensors = g_ptr_array_new ();
  priv->output_tensors =
      g_ptr_array_new_full (0, (GDestroyNotify) release_ort_value);
  priv->result_tensors = g_ptr_array_new ();

  g_rec_mutex_init (&priv->context_lock);

  /* Passthrough would propagate tensors caps upstream */
  gst_base_transform_set_prefer_passthrough (GST_BASE_TRANSFORM (self), FALSE);
}

static void
gst_onnx_base_inference_finalize (GObject * object)
{
  GstOnnxBaseInference *self = GST_ONNX_BASE_INFERENCE (object);
  GstOnnxBaseInferencePrivate *priv = self->priv;

  g_free (priv->model_file);
  g_free (priv->device);
  g_free (priv->model_cache_dir);
  g_free (priv->vitisai_config_file);
  g_free (priv->pci_bus_id);
  g_array_unref (priv->model_inputs);
  gst_clear_object (&priv->exporter);
  g_ptr_array_unref (priv->input_tensors);
  g_ptr_array_unref (priv->output_tensors);
  gst_onnx_base_inference_clear_result_tensors (self);
  g_ptr_array_unref (priv->result_tensors);
  gst_clear_structure (&priv->output_tensorgroups);

  g_rec_mutex_clear (&priv->context_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_onnx_base_inference_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOnnxBaseInference *self = GST_ONNX_BASE_INFERENCE (object);
  GstOnnxBaseInferencePrivate *priv = self->priv;
  const gchar *filename;

  switch (prop_id) {
    case PROP_MODEL_FILE:
      filename = g_value_get_string (value);
      if (filename &&
          g_file_test (filename,
              (GFileTest) (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR))) {
        g_free (priv->model_file);
        priv->model_file = g_strdup (filename);
      } else {
        GST_WARNING_OBJECT (self, "Model file '%s' not found!", filename);
      }
      break;
    case PROP_OPTIMIZATION_LEVEL:
      priv->optimization_level =
          (GstOnnxOptimizationLevel) g_value_get_enum (value);
      break;
    case PROP_EXECUTION_PROVIDER:
      self->execution_provider =
          (GstOnnxExecutionProvider) g_value_get_enum (value);
      break;
    case PROP_DEVICE:
      g_free (priv->device);
      priv->device = g_value_dup_string (value);
      break;
    case PROP_MODEL_CACHE_DIR:
      g_free (priv->model_cache_dir);
      priv->model_cache_dir = g_value_dup_string (value);
      break;
    case PROP_VITISAI_CONFIG_FILE:
      g_free (priv->vitisai_config_file);
      priv->vitisai_config_file = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_onnx_base_inference_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOnnxBaseInference *self = GST_ONNX_BASE_INFERENCE (object);
  GstOnnxBaseInferencePrivate *priv = self->priv;

  switch (prop_id) {
    case PROP_MODEL_FILE:
      g_value_set_string (value, priv->model_file);
      break;
    case PROP_OPTIMIZATION_LEVEL:
      g_value_set_enum (value, priv->optimization_level);
      break;
    case PROP_EXECUTION_PROVIDER:
      g_value_set_enum (value, self->execution_provider);
      break;
    case PROP_DEVICE:
      g_value_set_string (value, priv->device);
      break;
    case PROP_MODEL_CACHE_DIR:
      g_value_set_string (value, priv->model_cache_dir);
      break;
    case PROP_VITISAI_CONFIG_FILE:
      g_value_set_string (value, priv->vitisai_config_file);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_onnx_base_inference_set_context (GstElement * element, GstContext * context)
{
#if HAVE_GST_HIP || HAVE_DIRECTML
  GstOnnxBaseInference *self = GST_ONNX_BASE_INFERENCE (element);
  GstOnnxBaseInferencePrivate *priv = self->priv;

  g_rec_mutex_lock (&priv->context_lock);
#if HAVE_DIRECTML
  gst_d3d12_handle_set_context (element, context, priv->selected_device_id,
      &priv->device12);
#endif
#if HAVE_GST_HIP
#ifdef G_OS_WIN32
  gst_hip_handle_set_context_for_adapter_luid (element, context,
      GST_HIP_VENDOR_AMD, priv->adapter_luid, &priv->device_hip);
#else
  if (priv->pci_bus_id) {
    gst_hip_handle_set_context_for_pci_bus_id (element, context,
        GST_HIP_VENDOR_AMD, priv->pci_bus_id, &priv->device_hip);
  } else {
    gst_hip_handle_set_context (element, context,
        GST_HIP_VENDOR_AMD, priv->selected_device_id, &priv->device_hip);
  }
#endif
#endif

  g_rec_mutex_unlock (&priv->context_lock);
#endif

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_onnx_base_inference_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query)
{
#if HAVE_GST_HIP || HAVE_DIRECTML
  GstOnnxBaseInference *self = GST_ONNX_BASE_INFERENCE (trans);
  GstOnnxBaseInferencePrivate *priv = self->priv;

  if (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT) {
    gboolean ret;

    g_rec_mutex_lock (&priv->context_lock);
#if HAVE_DIRECTML
    ret = gst_d3d12_handle_context_query (GST_ELEMENT (self),
        query, priv->device12);
    if (ret) {
      g_rec_mutex_unlock (&priv->context_lock);
      return TRUE;
    }
#endif
#if HAVE_GST_HIP
    ret = gst_hip_handle_context_query (GST_ELEMENT (self),
        query, priv->device_hip);
#else
    ret = FALSE;
#endif
    g_rec_mutex_unlock (&priv->context_lock);

    if (ret)
      return TRUE;
  }
#endif

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans,
      direction, query);
}

static GError *
gst_onnx_load_library_once (void)
{
  GstOrtGetApiBaseFunc get_api_base;

#ifdef G_OS_WIN32
  /* Pick the user-specified onnxruntime */
  onnxruntime_module = gst_onnx_find_onnxrt ();

  /* Fallback: search in the usual paths */
  if (!onnxruntime_module)
    onnxruntime_module = LoadLibraryA ("onnxruntime.dll");

  if (!onnxruntime_module) {
    return g_error_new (GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Could not load onnxruntime.dll (error 0x%lx)", GetLastError ());
  }

  get_api_base = (GstOrtGetApiBaseFunc) GetProcAddress (onnxruntime_module,
      "OrtGetApiBase");
#else
  /* Pick the user-specified onnxruntime */
  onnxruntime_module = gst_onnx_find_onnxrt ();

  /* Fallback: search in the usual paths */
  if (!onnxruntime_module)
#ifdef __APPLE__
    onnxruntime_module = dlopen ("libonnxruntime.1.dylib", GST_DLOPEN_OPTS);
#else
    onnxruntime_module = dlopen ("libonnxruntime.so.1", GST_DLOPEN_OPTS);
#endif

  if (!onnxruntime_module) {
    return g_error_new (GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Could not load libonnxruntime: %s", dlerror ());
  }

  get_api_base = (GstOrtGetApiBaseFunc) dlsym (onnxruntime_module,
      "OrtGetApiBase");
#endif

  if (!get_api_base) {
    return g_error_new_literal (GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT,
        "Could not resolve OrtGetApiBase");
  }

  api_base = get_api_base ();
  if (!api_base) {
    return g_error_new_literal (GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT,
        "OrtGetApiBase returned NULL");
  }

  {
    uint32_t api_version;
    char *end = NULL;
    const char *minor_version = strstr (api_base->GetVersionString (), ".") + 1;

    errno = 0;
    api_version = (uint32_t) g_ascii_strtoull (minor_version, &end, 10);
    if (end == minor_version || errno || api_version == 0 ||
        api_version == G_MAXUINT32)
      api_version = ORT_API_VERSION;

    if (api_version < gst_min_ort_api_version ()) {
      return g_error_new (GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT,
          "Build does not support libonnxruntime API version < %d (loaded %d)",
          gst_min_ort_api_version (), api_version);
    }
    // GetApi says we must only pass ORT_API_VERSION to it, but actually any
    // version older than ORT_API_VERSION also works. We rely on that behavior
    // to be able to build using the headers of one libonnxruntime version, and
    // allow users to load a different (newer) one at runtime.
    api = api_base->GetApi (api_version);
    if (!api) {
      return g_error_new (GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_INIT,
          "Loaded libonnxruntime does not support API version %u", api_version);
    }

    loaded_api_version = api_version;
  }

  return NULL;
}

static gboolean
gst_onnx_base_inference_load_library (GstOnnxBaseInference * self)
{
  static gsize once = 0;
  static GError *error = NULL;

  if (g_once_init_enter (&once)) {
    error = gst_onnx_load_library_once ();
    g_once_init_leave (&once, 1);
  }

  if (error) {
    GstMessage *msg;

    GST_ERROR_OBJECT (self, "Couldn't load library");
    msg = gst_message_new_error (GST_OBJECT (self), error, NULL);
    gst_element_post_message (GST_ELEMENT (self), msg);

    return FALSE;
  }

  self->api = api;

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

static ORTCHAR_T *
string_to_ort_string (const gchar * str)
{
#ifdef G_OS_WIN32
  return (ORTCHAR_T *) g_utf8_to_utf16 (str, -1, NULL, NULL, NULL);
#else
  return (ORTCHAR_T *) g_strdup (str);
#endif
}

static const gchar *
gst_onnx_base_inference_get_model_cache_dir (GstOnnxBaseInference * self)
{
  GstOnnxBaseInferencePrivate *priv = self->priv;

  /* env var fallback is only for use with the devenv */
  const char *cache_dir = priv->model_cache_dir ? priv->model_cache_dir
      : g_getenv ("GST_ORT_MODEL_CACHE_DIR");

  if (cache_dir && *cache_dir && g_mkdir_with_parents (cache_dir, 0755) < 0)
    GST_WARNING_OBJECT (self, "Failed to create model cache dir '%s', model "
        "compilation may fail or be uncached", cache_dir);

  return cache_dir;
}

#if ORT_API_VERSION >= 22
#if MIN_ORT_API_VERSION < 22
#undef MIN_ORT_API_VERSION
#define MIN_ORT_API_VERSION 22
#endif
static gboolean
gst_onnx_base_inference_append_ep (GstOnnxBaseInference * self,
    OrtSessionOptions * opts, const gchar * ep_name)
{
  GstOnnxBaseInferencePrivate *priv = self->priv;
  const OrtEpDevice *const *ep_devices = NULL;
  const OrtEpDevice **target_devices = NULL;
  size_t num_ep_devices = 0;
  size_t num_target_devices = 0;
  const char *provider_options_keys[] = { "cache_dir" };
  const char *provider_options_values[1];
  size_t num_provider_options = 0;
  size_t i;
  OrtStatus *status;

  if (loaded_api_version < 22) {
    GST_ERROR_OBJECT (self,
        "SessionOptionsAppendExecutionProvider_V2 requires ONNX Runtime >= 1.22");
    return FALSE;
  }

  status = api->GetEpDevices (priv->env, &ep_devices, &num_ep_devices);
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
      if (priv->device) {
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
          } else if (g_strcmp0 (ep_name, MIGRAPHX_DYNAMIC_EP_NAME) == 0) {
            /* MIGraphX EP on Windows reports DXGI LUID and adapter number.
             * Use adapter number here */
            card_idx_str = api->GetKeyValue (metadata, "DxgiAdapterNumber");
          }
        }

        if (!card_idx_str)
          continue;

        requested = g_ascii_strtoll (priv->device, NULL, 10);
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
    if (priv->device) {
      GST_ERROR_OBJECT (self,
          "No %s devices found matching card_idx %s", ep_name, priv->device);
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
  if (g_strcmp0 (ep_name, HIP_DYNAMIC_EP_NAME) == 0 ||
      g_strcmp0 (ep_name, MIGRAPHX_DYNAMIC_EP_NAME) == 0) {
    const gchar *cache_dir = gst_onnx_base_inference_get_model_cache_dir (self);

    if (cache_dir && *cache_dir) {
      provider_options_values[0] = cache_dir;
      num_provider_options = 1;
    }
  }

  status =
      api->SessionOptionsAppendExecutionProvider_V2 (opts,
      priv->env, target_devices, num_target_devices,
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

static gboolean
gst_onnx_base_inference_register_ep (GstOnnxBaseInference * self,
    const gchar * ep_name, const gchar * lib_path)
{
  GstOnnxBaseInferencePrivate *priv = self->priv;
  OrtStatus *status = NULL;
  ORTCHAR_T *lib_path_ort;

  if (loaded_api_version < 22) {
    GST_ERROR_OBJECT (self,
        "RegisterExecutionProviderLibrary requires ONNX Runtime >= 1.22");
    return FALSE;
  }

  lib_path_ort = string_to_ort_string (lib_path);
  status =
      api->RegisterExecutionProviderLibrary (priv->env, ep_name, lib_path_ort);
  g_free (lib_path_ort);

  if (status) {
    /* Registration can fail if the EP is already registered.
     * Defer validation to gst_onnx_base_inference_append_ep() instead. */
    GST_DEBUG_OBJECT (self, "Failed to register EP %s (%s): %s",
        ep_name, lib_path, api->GetErrorMessage (status));
    api->ReleaseStatus (status);
  }

  return TRUE;
}
#endif

typedef OrtStatus *(ORT_API_CALL * GstOrtAppendVsiNpuFunc) (OrtSessionOptions *
    options);

static GstOrtAppendVsiNpuFunc
gst_onnx_base_inference_get_vsi_npu_append_func (void)
{
#ifdef G_OS_WIN32
  return (GstOrtAppendVsiNpuFunc) GetProcAddress (onnxruntime_module,
      "OrtSessionOptionsAppendExecutionProvider_VSINPU");
#else
  return (GstOrtAppendVsiNpuFunc) dlsym (onnxruntime_module,
      "OrtSessionOptionsAppendExecutionProvider_VSINPU");
#endif
}

#if HAVE_GST_HIP && ORT_API_VERSION >= 23
#if MIN_ORT_API_VERSION < 23
#undef MIN_ORT_API_VERSION
#define MIN_ORT_API_VERSION 23
#endif
static gchar *
get_selected_device_from_session (GstOnnxBaseInference * self,
    OrtSession * session, size_t input_count)
{
  OrtStatus *status = NULL;
  const OrtEpDevice **ep_devices;
  gchar *luid_or_pci_bus_id = NULL;
#ifdef G_OS_WIN32
  const gchar *device_id_key = "LUID";
#else
  const gchar *device_id_key = "pci_bus_id";
#endif

  if (loaded_api_version < 23) {
    GST_WARNING_OBJECT (self,
        "SessionGetEpDeviceForInputs requires ONNX Runtime >= 1.23");
    return NULL;
  }

  ep_devices = g_new0 (const OrtEpDevice *, input_count);
  status = api->SessionGetEpDeviceForInputs (session, ep_devices, input_count);
  if (status) {
    GST_WARNING_OBJECT (self, "Couldn't get EP devices for inputs: %s",
        api->GetErrorMessage (status));
    api->ReleaseStatus (status);
    goto done;
  }

  for (size_t i = 0; i < input_count; i++) {
    const OrtHardwareDevice *hwdev;
    const OrtKeyValuePairs *metadata;
    const gchar *device_id_value = NULL;

    if (!ep_devices[i])
      continue;

    hwdev = api->EpDevice_Device (ep_devices[i]);
    if (api->HardwareDevice_Type (hwdev) != OrtHardwareDeviceType_GPU)
      continue;

    metadata = api->HardwareDevice_Metadata (hwdev);
    {
      const char *const *keys = NULL;
      const char *const *values = NULL;
      size_t num_entries = 0;
      size_t j;

      api->GetKeyValuePairs (metadata, &keys, &values, &num_entries);

      GST_LOG_OBJECT (self, "EP device metadata entries: %" G_GSIZE_FORMAT,
          num_entries);

      for (j = 0; j < num_entries; j++)
        GST_LOG_OBJECT (self, "  %s = %s", keys[j], values[j]);
    }

    device_id_value = api->GetKeyValue (metadata, device_id_key);
    if (device_id_value) {
      luid_or_pci_bus_id = g_strdup (device_id_value);
      break;
    }
  }

  if (!luid_or_pci_bus_id)
    GST_WARNING_OBJECT (self, "No GPU EP device ID found for session inputs");

done:
  g_free (ep_devices);
  return luid_or_pci_bus_id;
}
#endif

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

static void
gst_onnx_base_inference_clear_free_tensors (GstOnnxBaseInference * self,
    GstTensor ** tensors)
{
  GstOnnxBaseInferencePrivate *priv = self->priv;

  if (!tensors)
    return;

  for (gsize i = 0; i < priv->output_count; i++) {
    if (tensors[i])
      gst_tensor_free (tensors[i]);
  }

  g_free (tensors);
}

static void
gst_onnx_base_inference_clear_result_tensors (GstOnnxBaseInference * self)
{
  GstOnnxBaseInferencePrivate *priv = self->priv;

  for (guint i = 0; i < priv->result_tensors->len; i++) {
    GstTensor **ret = g_ptr_array_index (priv->result_tensors, i);
    gst_onnx_base_inference_clear_free_tensors (self, ret);
  }

  g_ptr_array_set_size (priv->result_tensors, 0);
}

static void
gst_onnx_base_inference_reset (GstOnnxBaseInference * self)
{
  GstOnnxBaseInferencePrivate *priv = self->priv;
  GstOnnxBaseInferenceClass *klass = GST_ONNX_BASE_INFERENCE_GET_CLASS (self);

  /* Free cached input tensor metadata */
  g_clear_pointer (&priv->input_names, g_free);
  g_array_set_size (priv->model_inputs, 0);

  // Clean up output names
  if (priv->output_names) {
    for (size_t i = 0; i < priv->output_count; i++) {
      if (priv->output_names[i])
        priv->allocator->Free (priv->allocator, priv->output_names[i]);
    }
  }

  /* This must be done before resetting priv->output_count */
  gst_onnx_base_inference_clear_result_tensors (self);

  g_clear_pointer (&priv->output_names, g_free);
  g_clear_pointer (&priv->output_ids, g_free);
  g_clear_pointer (&priv->output_dims_orders, g_free);
  priv->output_count = 0;

  g_ptr_array_set_size (priv->input_tensors, 0);
  g_ptr_array_set_size (priv->output_tensors, 0);
  gst_clear_structure (&priv->output_tensorgroups);
  gst_clear_object (&priv->exporter);

  if (priv->session) {
    self->api->ReleaseSession (priv->session);
    priv->session = NULL;
  }

  if (klass->reset)
    klass->reset (self);

  if (priv->env) {
    /* Do not unregister plugin EPs here. OrtEnv is a refcounted singleton
     * and they may still be in use by other sessions. Explicit unregister
     * is also unnecessary since EPs are automatically unregistered when
     * the OrtEnv is destroyed */
    self->api->ReleaseEnv (priv->env);
    priv->env = NULL;
  }

  priv->allocator = NULL;

  g_rec_mutex_lock (&priv->context_lock);
#if HAVE_GST_HIP
  gst_clear_object (&priv->device_hip);
#endif
#if HAVE_DIRECTML
  gst_clear_object (&priv->device12);
#endif

  g_clear_pointer (&priv->pci_bus_id, g_free);
  priv->adapter_luid = 0;
  priv->selected_device_id = 0;
  g_rec_mutex_unlock (&priv->context_lock);
}

static gboolean
gst_onnx_base_inference_start (GstBaseTransform * trans)
{
  GstOnnxBaseInference *self = GST_ONNX_BASE_INFERENCE (trans);
  GstOnnxBaseInferencePrivate *priv = self->priv;
  GstOnnxBaseInferenceClass *klass = GST_ONNX_BASE_INFERENCE_GET_CLASS (self);
  OrtStatus *status = NULL;
  OrtSessionOptions *session_options = NULL;
  GraphOptimizationLevel onnx_optim;
  GstAnalyticsModelInfo *modelinfo = NULL;
  GstOnnxImporter *importer = NULL;
  size_t input_count = 0;
  size_t i;
  GstOnnxModelInfo parsed_info = { 0, };
  GArray *exporter_configs = NULL;

  if (!gst_onnx_base_inference_load_library (self))
    return FALSE;

  if (priv->session)
    return TRUE;

  if (!priv->model_file) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("model-file property not set"));
    return FALSE;
  }

  if (priv->model_cache_dir && *priv->model_cache_dir &&
      self->execution_provider != GST_ONNX_EXECUTION_PROVIDER_MIGRAPHX &&
      self->execution_provider != GST_ONNX_EXECUTION_PROVIDER_HIP &&
      self->execution_provider != GST_ONNX_EXECUTION_PROVIDER_VITIS_AI &&
      self->execution_provider != GST_ONNX_EXECUTION_PROVIDER_COREML) {
    GST_WARNING_OBJECT (self,
        "model-cache-dir is not supported by the selected execution provider");
  }

  modelinfo = gst_analytics_modelinfo_load (priv->model_file);
  if (!modelinfo) {
    GST_ERROR_OBJECT (self, "Failed to load modelinfo for model '%s'. "
        "A '.modelinfo' file is required alongside the model; generate one "
        "with the modelinfo-generator.py tool. Run with GST_DEBUG=modelinfo:5 "
        "for the exact reason.", priv->model_file);
    goto error;
  }

  /* Create environment */
  {
    OrtLoggingLevel ort_logging;

    switch (gst_debug_category_get_threshold (onnx_runtime_debug)) {
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

    status = self->api->CreateEnvWithCustomLogger (gst_onnx_log_function,
        self, ort_logging, "GstOnnx", &priv->env);
    if (status) {
      GST_ERROR_OBJECT (self, "Failed to create environment: %s",
          self->api->GetErrorMessage (status));
      goto error;
    }
  }

  /* Disable telemetry events */
  status = self->api->DisableTelemetryEvents (priv->env);
  if (status) {
    GST_WARNING_OBJECT (self, "Failed to disable telemetry events: %s",
        self->api->GetErrorMessage (status));
    goto error;
  }

  /* Create session options */
  status = self->api->CreateSessionOptions (&session_options);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to create session options: %s",
        self->api->GetErrorMessage (status));
    goto error;
  }

  /* Set graph optimization level */
  switch (priv->optimization_level) {
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

  status =
      self->api->SetSessionGraphOptimizationLevel (session_options, onnx_optim);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to set optimization level: %s",
        self->api->GetErrorMessage (status));
    goto error;
  }

  /* Set execution provider */
  switch (self->execution_provider) {
    case GST_ONNX_EXECUTION_PROVIDER_CUDA:
    {
      OrtCUDAProviderOptionsV2 *cuda_options = NULL;

      status = self->api->CreateCUDAProviderOptions (&cuda_options);
      if (status) {
        GST_ERROR_OBJECT (self, "Failed to create CUDA provider %s",
            self->api->GetErrorMessage (status));
        goto error;
      }

      if (priv->device) {
        const char *keys[] = { "device_id" };
        const char *values[] = { priv->device };

        status = self->api->UpdateCUDAProviderOptions (cuda_options,
            keys, values, 1);
        if (status) {
          GST_ERROR_OBJECT (self, "Failed to set CUDA device_id: %s",
              self->api->GetErrorMessage (status));
          self->api->ReleaseCUDAProviderOptions (cuda_options);
          goto error;
        }
      }

      status =
          self->
          api->SessionOptionsAppendExecutionProvider_CUDA_V2 (session_options,
          cuda_options);
      self->api->ReleaseCUDAProviderOptions (cuda_options);

      if (status) {
        GST_ERROR_OBJECT (self, "Failed to append CUDA provider: %s",
            self->api->GetErrorMessage (status));
        goto error;
      }
      break;
    }

    case GST_ONNX_EXECUTION_PROVIDER_VSI:
    {
      GstOrtAppendVsiNpuFunc append_vsi_npu =
          gst_onnx_base_inference_get_vsi_npu_append_func ();

      if (!append_vsi_npu) {
        GST_ERROR_OBJECT (self, "VSI NPU execution provider is unavailable");
        goto error;
      }

      status = append_vsi_npu (session_options);
      if (status) {
        GST_ERROR_OBJECT (self, "Failed to append VSI NPU provider: %s",
            self->api->GetErrorMessage (status));
        goto error;
      }

      status = self->api->DisableCpuMemArena (session_options);
      if (status) {
        GST_WARNING_OBJECT (self, "Failed to disable the CPU memory arena: %s",
            self->api->GetErrorMessage (status));
        self->api->ReleaseStatus (status);
        status = NULL;
      }
      break;
    }

    case GST_ONNX_EXECUTION_PROVIDER_VITIS_AI:
    {
#if ORT_API_VERSION >= 18
#if MIN_ORT_API_VERSION < 18
#undef MIN_ORT_API_VERSION
#define MIN_ORT_API_VERSION 18
#endif
      const gchar *cache_dir;
      const char *keys[2];
      const char *values[2];
      size_t num_options = 0;

      if (loaded_api_version < 18) {
        GST_ERROR_OBJECT (self,
            "Vitis AI execution provider requires ONNX Runtime >= 1.18");
        goto error;
      }

      cache_dir = gst_onnx_base_inference_get_model_cache_dir (self);

      if (priv->vitisai_config_file && *priv->vitisai_config_file) {
        keys[num_options] = "config_file";
        values[num_options++] = priv->vitisai_config_file;
      }

      if (cache_dir && *cache_dir) {
        keys[num_options] = "cache_dir";
        values[num_options++] = cache_dir;
      }

      status =
          self->
          api->SessionOptionsAppendExecutionProvider_VitisAI (session_options,
          num_options ? keys : NULL, num_options ? values : NULL, num_options);
      if (status) {
        GST_ERROR_OBJECT (self, "Failed to append Vitis AI provider: %s",
            self->api->GetErrorMessage (status));
        goto error;
      }
#else
      GST_ERROR_OBJECT (self, "Vitis AI execution provider is unavailable, "
          "need ONNX Runtime >=1.18");
      goto error;
#endif
      break;
    }

    case GST_ONNX_EXECUTION_PROVIDER_MIGRAPHX:
#if !HAVE_WINML
    {
      const gchar *cache_dir =
          gst_onnx_base_inference_get_model_cache_dir (self);

      priv->selected_device_id = 0;
      if (priv->device)
        priv->selected_device_id =
            (guint) g_ascii_strtoll (priv->device, NULL, 10);

      if (cache_dir && *cache_dir && loaded_api_version >= 23) {
        /*
         * onnxruntime online docs are usually out of date, so you have to
         * check the headers. For example, migraphx_model_cache_dir is
         * documented here and was backported to onnxruntime 1.23:
         * core/providers/migraphx/migraphx_execution_provider_info.h
         */
        const char *keys[2] = {
          "migraphx_model_cache_dir",
          "device_id"
        };
        const char *values[2] = {
          cache_dir,
          priv->device
        };
        size_t num_options = priv->device ? 2 : 1;

        status =
            self->api->SessionOptionsAppendExecutionProvider (session_options,
            "MIGraphX", keys, values, num_options);
      } else {
        OrtMIGraphXProviderOptions migraphx_options;

        if (cache_dir && *cache_dir) {
          GST_WARNING_OBJECT (self,
              "model-cache-dir requires ONNX Runtime >= 1.23 for the "
              "MIGraphX execution provider; runtime version is %s",
              api_base->GetVersionString ());
        }

        memset (&migraphx_options, 0, sizeof (migraphx_options));
        migraphx_options.device_id = priv->selected_device_id;

        status =
            self->api->SessionOptionsAppendExecutionProvider_MIGraphX
            (session_options, &migraphx_options);
      }

      if (status) {
        GST_ERROR_OBJECT (self, "Failed to create MIGraphX provider: %s",
            self->api->GetErrorMessage (status));
        goto error;
      }
    }
#else
    {
      gchar *lib_path =
          gst_onnx_winml_ep_catalog_find (MIGRAPHX_DYNAMIC_EP_NAME);

      if (!lib_path) {
        GST_ERROR_OBJECT (self, "Couldn't prepare MIGraphX EP via WindowsML");
        goto error;
      }

      if (!gst_onnx_base_inference_register_ep (self, MIGRAPHX_DYNAMIC_EP_NAME,
              lib_path)) {
        g_free (lib_path);
        goto error;
      }

      g_free (lib_path);

      if (!gst_onnx_base_inference_append_ep (self, session_options,
              MIGRAPHX_DYNAMIC_EP_NAME))
        goto error;
    }
#endif
      break;

    case GST_ONNX_EXECUTION_PROVIDER_HIP:
#if ORT_API_VERSION >= 22
#if MIN_ORT_API_VERSION < 22
#undef MIN_ORT_API_VERSION
#define MIN_ORT_API_VERSION 22
#endif
    {
      const gchar *ep_lib_path = g_getenv ("MORPHIZEN_EP_LIB");

      if (!ep_lib_path || !*ep_lib_path) {
        GST_ERROR_OBJECT (self,
            "MORPHIZEN_EP_LIB environment variable not set");
        goto error;
      }

      if (!gst_onnx_base_inference_register_ep (self, HIP_DYNAMIC_EP_NAME,
              ep_lib_path))
        goto error;

      if (!gst_onnx_base_inference_append_ep (self, session_options,
              HIP_DYNAMIC_EP_NAME))
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

      if (priv->device)
        device_id = (guint) g_ascii_strtoll (priv->device, NULL, 10);

      g_rec_mutex_lock (&priv->context_lock);
      priv->selected_device_id = device_id;
      if (priv->device12) {
        guint current_adapter;
        g_object_get (priv->device12, "adapter-index", &current_adapter, NULL);
        if (current_adapter != device_id)
          gst_clear_object (&priv->device12);
      }
      gst_d3d12_ensure_element_data (GST_ELEMENT (self), device_id,
          &priv->device12);
      if (priv->device12)
        importer = gst_onnx_importer_dml_new (self->api,
            priv->device12, session_options, klass->proc_type);
      g_rec_mutex_unlock (&priv->context_lock);

      if (!importer) {
        GST_ERROR_OBJECT (self,
            "Couldn't create DML importer with adapter index %u", device_id);
        goto error;
      }
    }
#else
      GST_ERROR_OBJECT (self, "Compiled without DirectML support");
      goto error;
#endif
      break;

    case GST_ONNX_EXECUTION_PROVIDER_COREML:
#if defined(__APPLE__) && ORT_API_VERSION >= 21
    {
      if (loaded_api_version < 21) {
        GST_ERROR_OBJECT (self,
            "Core ML execution provider requires ONNX Runtime >= 1.21");
        goto error;
      }

      const gchar *cache_dir =
          gst_onnx_base_inference_get_model_cache_dir (self);
      const char *keys[3];
      const char *values[3];
      size_t num_options = 0;

      /* NeuralNetwork is the default in ONNX,
       * but MLProgram is what Apple recommends these days. See:
       * https://apple.github.io/coremltools/docs-guides/source/target-conversion-formats.html#default-format
       * TODO: Expose some config property for this */
      keys[num_options] = "ModelFormat";
      values[num_options++] = "MLProgram";

      if (priv->device) {
        keys[num_options] = "MLComputeUnits";
        values[num_options++] = priv->device;
      }

      if (cache_dir && *cache_dir) {
        keys[num_options] = "ModelCacheDirectory";
        values[num_options++] = cache_dir;
      }

      status =
          self->api->SessionOptionsAppendExecutionProvider (session_options,
          "CoreML", keys, values, num_options);
      if (status) {
        GST_ERROR_OBJECT (self, "Failed to append Core ML provider: %s",
            self->api->GetErrorMessage (status));
        goto error;
      }
    }
#else
      GST_ERROR_OBJECT (self, "Core ML execution provider is unavailable, "
          "need macOS and ONNX Runtime >= 1.21");
      goto error;
#endif
      break;

    case GST_ONNX_EXECUTION_PROVIDER_CPU:
    default:
      break;
  }

  {
    /* Create session */
    ORTCHAR_T *model_file = string_to_ort_string (priv->model_file);
    status = self->api->CreateSession (priv->env, model_file,
        session_options, &priv->session);
    g_free (model_file);
  }

  if (status) {
    GST_ERROR_OBJECT (self, "Failed to create session: %s",
        self->api->GetErrorMessage (status));
    priv->session = NULL;
    goto error;
  }

  self->api->ReleaseSessionOptions (session_options);
  session_options = NULL;

  status = self->api->SessionGetInputCount (priv->session, &input_count);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to get input count: %s",
        self->api->GetErrorMessage (status));
    goto error;
  }

  if (input_count == 0) {
    GST_ERROR_OBJECT (self, "Model has no input tensors");
    goto error;
  }
#if HAVE_GST_HIP
  if (self->execution_provider == GST_ONNX_EXECUTION_PROVIDER_MIGRAPHX ||
      self->execution_provider == GST_ONNX_EXECUTION_PROVIDER_HIP) {
    g_rec_mutex_lock (&priv->context_lock);
    priv->adapter_luid = 0;
    g_clear_pointer (&priv->pci_bus_id, g_free);
    gst_clear_object (&priv->device_hip);

#if HAVE_WINML
    {
      gchar *luid_str =
          get_selected_device_from_session (self, priv->session, input_count);

      if (luid_str) {
        GST_DEBUG_OBJECT (self, "Input EP device LUID: %s", luid_str);

        priv->adapter_luid = g_ascii_strtoll (luid_str, NULL, 10);
        g_free (luid_str);

        gst_hip_ensure_element_data_for_adapter_luid (GST_ELEMENT (self),
            GST_HIP_VENDOR_AMD, priv->adapter_luid, &priv->device_hip);
      }
    }
#else
#if ORT_API_VERSION >= 23
#if MIN_ORT_API_VERSION < 23
#undef MIN_ORT_API_VERSION
#define MIN_ORT_API_VERSION 23
#endif
    if (self->execution_provider == GST_ONNX_EXECUTION_PROVIDER_HIP) {
      gchar *pci_bus_id =
          get_selected_device_from_session (self, priv->session, input_count);

      if (pci_bus_id) {
        GST_DEBUG_OBJECT (self, "Input EP device PCI bus ID: %s", pci_bus_id);
        priv->pci_bus_id = pci_bus_id;
        gst_hip_ensure_element_data_for_pci_bus_id (GST_ELEMENT (self),
            GST_HIP_VENDOR_AMD, priv->pci_bus_id, &priv->device_hip);
        g_clear_pointer (&priv->pci_bus_id, g_free);
      }
    } else
#endif /* ORT_API_VERSION >= 23 */
    if (self->execution_provider == GST_ONNX_EXECUTION_PROVIDER_MIGRAPHX) {
      gst_hip_ensure_element_data (GST_ELEMENT (self),
          GST_HIP_VENDOR_AMD, priv->selected_device_id, &priv->device_hip);
    }
#endif /* HAVE_WINML */

    if (priv->device_hip) {
      GST_DEBUG_OBJECT (self, "Creating HIP importer");
      importer = gst_onnx_importer_hip_new (self->api,
          priv->device_hip, klass->proc_type);
      if (!importer)
        GST_WARNING_OBJECT (self, "Couldn't create HIP importer");
    }
    g_rec_mutex_unlock (&priv->context_lock);
  }
#endif /* HAVE_GST_HIP */

  if (!importer)
    importer = gst_onnx_importer_new (self->api, klass->proc_type);

  if (!importer) {
    GST_ERROR_OBJECT (self, "Couldn't create importer");
    goto error;
  }

  /* Get allocator */
  status = self->api->GetAllocatorWithDefaultOptions (&priv->allocator);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to get allocator: %s",
        self->api->GetErrorMessage (status));
    goto error;
  }

  g_array_set_size (priv->model_inputs, 0);

  /* Get input info */
  for (i = 0; i < input_count; i++) {
    GstOnnxModelInput input = { 0, };
    OrtTypeInfo *type_info = NULL;
    const OrtTensorTypeAndShapeInfo *tensor_info = NULL;
    ONNXTensorElementDataType element_type;
    gchar *onnx_name = NULL;
    gsize gst_dims[GST_ONNX_MAX_DIMS];
    size_t j;

    status = self->api->SessionGetInputTypeInfo (priv->session, i, &type_info);
    if (status) {
      GST_ERROR_OBJECT (self, "Failed to get input[%zu] type info: %s",
          i, self->api->GetErrorMessage (status));
      goto input_error;
    }

    status = self->api->CastTypeInfoToTensorInfo (type_info, &tensor_info);
    if (status) {
      GST_ERROR_OBJECT (self,
          "Failed to get tensor info for input[%zu]: %s",
          i, self->api->GetErrorMessage (status));
      goto input_error;
    }

    status = self->api->GetDimensionsCount (tensor_info, &input.num_dims);
    if (status) {
      GST_ERROR_OBJECT (self,
          "Failed to get dimension count for input[%zu]: %s",
          i, self->api->GetErrorMessage (status));
      goto input_error;
    }

    if (input.num_dims > GST_ONNX_MAX_DIMS) {
      GST_ERROR_OBJECT (self,
          "Input[%zu] has %" G_GSIZE_FORMAT " dimensions, maximum supported "
          "is %d", i, input.num_dims, GST_ONNX_MAX_DIMS);
      goto input_error;
    }

    status = self->api->GetDimensions (tensor_info, input.dims, input.num_dims);
    if (status) {
      GST_ERROR_OBJECT (self,
          "Failed to get dimensions for input[%zu]: %s",
          i, self->api->GetErrorMessage (status));
      goto input_error;
    }

    for (j = 0; j < input.num_dims; j++) {
      if (input.dims[j] < 0)
        gst_dims[j] = G_MAXSIZE;
      else
        gst_dims[j] = (gsize) input.dims[j];
    }

    status = self->api->GetTensorElementType (tensor_info, &element_type);
    if (status) {
      GST_ERROR_OBJECT (self,
          "Failed to get element type for input[%zu]: %s",
          i, self->api->GetErrorMessage (status));
      goto input_error;
    }

    input.data_type = onnx_data_type_to_gst (element_type);
    if (input.data_type == -1) {
      GST_ERROR_OBJECT (self,
          "Unsupported input[%zu] tensor data type %d", i, element_type);
      goto input_error;
    }

    input.data_type_onnx = element_type;

    /* Get input tensor name from ONNX file */
    status = self->api->SessionGetInputName (priv->session, i,
        priv->allocator, &onnx_name);
    if (status) {
      GST_ERROR_OBJECT (self, "Failed to get input[%zu] name: %s",
          i, self->api->GetErrorMessage (status));
      goto input_error;
    }

    input.modelinfo_name = gst_analytics_modelinfo_find_tensor_name (modelinfo,
        MODELINFO_DIRECTION_INPUT, i, onnx_name, input.data_type,
        input.num_dims, gst_dims);
    if (!input.modelinfo_name) {
      gchar *dims_str = build_dims_str (input.num_dims, gst_dims);
      GST_ERROR_OBJECT (self,
          "Model info file doesn't contain info for input_tensor[%zu]:%s matching the"
          " type %s and dims %s", i, onnx_name,
          gst_tensor_data_type_get_name (input.data_type), dims_str);
      g_free (dims_str);
      goto input_error;
    }

    {
      gchar *tensor_id =
          gst_analytics_modelinfo_get_id (modelinfo, input.modelinfo_name);

      if (!tensor_id) {
        GST_ERROR_OBJECT (self,
            "Model info doesn't have 'id' for input tensor %s",
            input.modelinfo_name);
        goto input_error;
      }

      input.id = g_quark_from_string (tensor_id);
      g_free (tensor_id);
    }

    input.dims_order =
        gst_analytics_modelinfo_get_dims_order (modelinfo,
        input.modelinfo_name);

    input.onnx_name = g_strdup (onnx_name);
    priv->allocator->Free (priv->allocator, onnx_name);
    onnx_name = NULL;

    GST_DEBUG_OBJECT (self,
        "Input[%zu]: ONNX name '%s', modelinfo name '%s', tensor-id '%s', "
        "type %s, dims-order %s, %" G_GSIZE_FORMAT " dimensions",
        i, input.onnx_name, input.modelinfo_name,
        g_quark_to_string (input.id),
        gst_tensor_data_type_get_name (input.data_type),
        input.dims_order == GST_TENSOR_DIM_ORDER_COL_MAJOR ?
        "col-major" : "row-major", input.num_dims);

    self->api->ReleaseTypeInfo (type_info);
    type_info = NULL;

    g_array_append_val (priv->model_inputs, input);
    continue;

  input_error:
    if (onnx_name)
      priv->allocator->Free (priv->allocator, onnx_name);

    g_free (input.onnx_name);
    g_free (input.modelinfo_name);

    if (type_info)
      self->api->ReleaseTypeInfo (type_info);

    goto error;
  }

  status = self->api->SessionGetOutputCount (priv->session,
      &priv->output_count);
  if (status) {
    GST_ERROR_OBJECT (self, "Failed to retrieve output count: %s",
        self->api->GetErrorMessage (status));
    goto error;
  }

  GST_DEBUG_OBJECT (self, "Number of Output Nodes: %zu", priv->output_count);

  if (priv->output_count == 0) {
    GST_ERROR_OBJECT (self, "Model with 0 output nodes is not supported.");
    goto error;
  }

  if (priv->output_count > GST_ONNX_MAX_OUTPUT_COUNT) {
    GST_ERROR_OBJECT (self, "Too large output count %" G_GSIZE_FORMAT,
        priv->output_count);
    goto error;
  }

  /* Get output names */
  priv->output_names = g_new0 (gchar *, priv->output_count);
  for (i = 0; i < priv->output_count; i++) {
    status = self->api->SessionGetOutputName (priv->session, i,
        priv->allocator, &priv->output_names[i]);
    if (status) {
      GST_ERROR_OBJECT (self, "Failed to get output name %zu: %s", i,
          self->api->GetErrorMessage (status));
      goto error;
    }

    GST_DEBUG_OBJECT (self, "Output name %zu:%s", i, priv->output_names[i]);
  }

  {
    GValue v_tensors_set = G_VALUE_INIT;
    gchar *group_id = NULL;

    g_value_init (&v_tensors_set, GST_TYPE_UNIQUE_LIST);

    priv->output_ids = g_new0 (GQuark, priv->output_count);
    priv->output_dims_orders = g_new0 (GstTensorDimOrder, priv->output_count);
    exporter_configs =
        g_array_new (FALSE, TRUE, sizeof (GstOnnxExporterConfig));
    g_array_set_clear_func (exporter_configs,
        (GDestroyNotify) clear_exporter_config);
    g_array_set_size (exporter_configs, priv->output_count);

    for (i = 0; i < priv->output_count; i++) {
      OrtTypeInfo *output_type_info = NULL;
      const OrtTensorTypeAndShapeInfo *output_tensor_info = NULL;
      size_t card;
      ONNXTensorElementDataType type;
      GstTensorDataType gst_data_type;
      size_t j;
      gchar *tensor_name = NULL;
      gchar *tensor_id = NULL;
      gsize *output_dims = NULL;
      GstOnnxExporterConfig *exporter_config = &g_array_index (exporter_configs,
          GstOnnxExporterConfig, i);
      gsize element_size;
      gsize num_elements = 1;
      gboolean dynamic = FALSE;

      status = self->api->SessionGetOutputTypeInfo (priv->session, i,
          &output_type_info);
      if (status) {
        GST_ERROR_OBJECT (self,
            "Failed to get info for output tensor %zu: %s", i,
            self->api->GetErrorMessage (status));
        goto output_error;
      }

      status = self->api->CastTypeInfoToTensorInfo (output_type_info,
          &output_tensor_info);
      if (status) {
        GST_ERROR_OBJECT (self,
            "Failed to get cast type for output tensor %zu: %s", i,
            self->api->GetErrorMessage (status));
        goto output_error;
      }

      status = self->api->GetDimensionsCount (output_tensor_info, &card);
      if (status) {
        GST_ERROR_OBJECT (self,
            "Failed to get cardinality for output tensor %zu: %s", i,
            self->api->GetErrorMessage (status));
        goto output_error;
      }

      status = self->api->GetTensorElementType (output_tensor_info, &type);
      if (status) {
        GST_ERROR_OBJECT (self,
            "Failed to get element type for output tensor %zu: %s", i,
            self->api->GetErrorMessage (status));
        goto output_error;
      }

      gst_data_type = onnx_data_type_to_gst (type);
      if (gst_data_type == -1) {
        GST_ERROR_OBJECT (self, "Unsupported output tensor data type %d", type);
        goto output_error;
      }

      switch (type) {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
          element_size = get_tensor_type_size (gst_data_type);
          break;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
          element_size = get_tensor_type_size (gst_data_type);
          break;
        default:
          GST_ERROR_OBJECT (self, "Unsupported output tensor data type %d",
              type);
          goto output_error;
      }

      {
        gint64 *shape = g_alloca (card * sizeof (gint64));

        /* Get dimensions from ONNX */
        status = self->api->GetDimensions (output_tensor_info, shape, card);
        if (status) {
          GST_ERROR_OBJECT (self, "Failed to get output tensor (%s) dimensions",
              priv->output_names[i]);
          goto output_error;
        }

        for (j = 0; j < card; j++) {
          if (shape[j] < 0)
            dynamic = TRUE;
        }

        if (!dynamic) {
          for (j = 0; j < card; j++) {
            if ((guint64) shape[j] > G_MAXSIZE ||
                !g_size_checked_mul (&num_elements, num_elements,
                    (gsize) shape[j])) {
              GST_ERROR_OBJECT (self, "Element count overflow for output %zu",
                  i);
              goto output_error;
            }
          }

          if (!g_size_checked_mul (&exporter_config->size, num_elements,
                  element_size) || exporter_config->size > G_MAXUINT) {
            GST_ERROR_OBJECT (self, "Output %zu size exceeds G_MAXUINT", i);
            goto output_error;
          }
        }

        output_dims = g_new0 (gsize, card);
        for (j = 0; j < card; j++)
          output_dims[j] = shape[j] > 0 ? (gsize) shape[j] : G_MAXSIZE;

        exporter_config->data_type = type;
        exporter_config->dims_count = card;
        exporter_config->dims = g_memdup2 (shape, card * sizeof (gint64));
        /* ORT can skip the preallocated buffer when an input is also an output
         * Copy the result before unpreparing the importer */
        for (guint k = 0; k < priv->model_inputs->len; k++) {
          const GstOnnxModelInput *input = &g_array_index (priv->model_inputs,
              GstOnnxModelInput, k);
          if (g_strcmp0 (priv->output_names[i], input->onnx_name) == 0) {
            exporter_config->use_ort_allocation = TRUE;
            break;
          }
        }
      }

      /* Look up tensor name in modelinfo */
      tensor_name = gst_analytics_modelinfo_find_tensor_name (modelinfo,
          MODELINFO_DIRECTION_OUTPUT, i, priv->output_names[i],
          gst_data_type, card, output_dims);

      if (!tensor_name) {
        gchar *dims_str = build_dims_str (card, output_dims);

        GST_ERROR_OBJECT (self,
            "Model info file doesn't contain info for output_tensor[%zu]:%s "
            "matching the type %s and dims %s", i, priv->output_names[i],
            gst_tensor_data_type_get_name (gst_data_type), dims_str);
        g_free (dims_str);
        goto output_error;
      }

      /* Validation: modelinfo successfully matched dims and datatype from ONNX */
      GST_INFO_OBJECT (self,
          "Output tensor[%zu]:%s validated - modelinfo matches ONNX model "
          "(type: %s)", i, priv->output_names[i],
          gst_tensor_data_type_get_name (gst_data_type));

      /* Get tensor ID from modelinfo */
      tensor_id = gst_analytics_modelinfo_get_id (modelinfo, tensor_name);
      if (!tensor_id) {
        GST_ERROR_OBJECT (self,
            "Model info doesn't have 'id' for tensor %s", tensor_name);
        goto output_error;
      }

      GST_DEBUG_OBJECT (self,
          "Mapping output_tensor[%zu]:%s of type %s to id %s",
          i, priv->output_names[i],
          gst_tensor_data_type_get_name (gst_data_type), tensor_id);

      priv->output_ids[i] = g_quark_from_string (tensor_id);

      /* tensor description */
      {
        GstStructure *tensor_desc = gst_structure_new_empty ("tensor/strided");
        GValue val_dims = G_VALUE_INIT;
        GValue val = G_VALUE_INIT;
        GValue val_caps = G_VALUE_INIT;
        GstTensorDimOrder dims_order;
        const gchar *dims_order_str;
        GstCaps *tensor_caps;

        gst_value_array_init (&val_dims, card);
        g_value_init (&val, G_TYPE_INT);
        g_value_init (&val_caps, GST_TYPE_CAPS);

        /* Setting dims */
        for (j = 0; j < card; j++) {
          g_value_set_int (&val,
              output_dims[j] != G_MAXSIZE ? output_dims[j] : 0);
          gst_value_array_append_value (&val_dims, &val);
        }

        /* Get dims-order from modelinfo (defaults to row-major if not specified) */
        dims_order =
            gst_analytics_modelinfo_get_dims_order (modelinfo, tensor_name);
        priv->output_dims_orders[i] = dims_order;
        dims_order_str =
            dims_order == GST_TENSOR_DIM_ORDER_COL_MAJOR ?
            "col-major" : "row-major";

        gst_structure_set (tensor_desc,
            "dims-order", G_TYPE_STRING, dims_order_str,
            "tensor-id", G_TYPE_STRING,
            g_quark_to_string (priv->output_ids[i]), NULL);

        GST_INFO_OBJECT (self, "%s[dims-order]=%s",
            g_quark_to_string (priv->output_ids[i]), dims_order_str);

        gst_structure_take_value (tensor_desc, "dims", &val_dims);
        g_value_unset (&val);

        /* Setting datatype */
        {
          GValue value = G_VALUE_INIT;

          g_value_init (&value, G_TYPE_STRING);
          g_value_set_string (&value,
              gst_tensor_data_type_get_name (gst_data_type));
          gst_structure_take_value (tensor_desc, "type", &value);
          g_value_unset (&value);
        }

        /* tensor caps */
        tensor_caps = gst_caps_new_full (tensor_desc, NULL);

        /* Append tensor caps to set */
        gst_value_set_caps (&val_caps, tensor_caps);
        gst_caps_unref (tensor_caps);
        gst_value_unique_list_append_and_take_value (&v_tensors_set, &val_caps);
      }

      /* Get group-id from modelinfo on last tensor */
      if (i == priv->output_count - 1) {
        group_id = gst_analytics_modelinfo_get_group_id (modelinfo);
        if (!group_id) {
          GST_ERROR_OBJECT (self, "Model info doesn't have 'group-id'");
          goto output_error;
        }
      }

      /* Cleanup */
      g_free (tensor_name);
      g_free (tensor_id);
      g_free (output_dims);
      self->api->ReleaseTypeInfo (output_type_info);
      continue;

    output_error:
      g_free (tensor_name);
      g_free (tensor_id);
      g_free (output_dims);

      if (output_type_info)
        self->api->ReleaseTypeInfo (output_type_info);

      g_value_unset (&v_tensors_set);
      g_free (group_id);
      goto error;
    }

    /* Setting output tensors for caps */
    gst_clear_structure (&priv->output_tensorgroups);
    priv->output_tensorgroups = gst_structure_new_empty ("tensorgroups");

    gst_structure_take_value (priv->output_tensorgroups,
        group_id, &v_tensors_set);
    parsed_info.group_id = g_quark_from_string (group_id);
    g_free (group_id);
  }

#if HAVE_DIRECTML
  if (self->execution_provider == GST_ONNX_EXECUTION_PROVIDER_DIRECTML) {
    priv->exporter = gst_onnx_exporter_dml_new (self->api, priv->device12,
        priv->session);
  }
#endif

#if HAVE_GST_HIP
  if (priv->device_hip &&
      (self->execution_provider == GST_ONNX_EXECUTION_PROVIDER_MIGRAPHX ||
          self->execution_provider == GST_ONNX_EXECUTION_PROVIDER_HIP)) {
    priv->exporter = gst_onnx_exporter_hip_new (self->api, priv->device_hip);
  }
#endif

  if (!priv->exporter)
    priv->exporter = gst_onnx_exporter_cpu_new (self->api);
  if (!priv->exporter) {
    GST_ERROR_OBJECT (self, "Couldn't create output exporter");
    goto error;
  }

  if (!gst_onnx_exporter_setup (priv->exporter,
          (const GstOnnxExporterConfig *) exporter_configs->data,
          exporter_configs->len)) {
    GST_ERROR_OBJECT (self, "Couldn't configure output exporter");
    goto error;
  }

  parsed_info.inputs = priv->model_inputs;
  parsed_info.output_tensorgroups = priv->output_tensorgroups;

  if (!klass->process_model_info (self, modelinfo, &parsed_info, importer)) {
    GST_ERROR_OBJECT (self, "Failed to process model info");
    goto error;
  }

  gst_clear_object (&importer);

  priv->input_names = g_new (gchar *, priv->model_inputs->len);
  for (i = 0; i < priv->model_inputs->len; i++) {
    GstOnnxModelInput *input =
        &g_array_index (priv->model_inputs, GstOnnxModelInput, i);

    priv->input_names[i] = input->onnx_name;
  }

  g_clear_pointer (&exporter_configs, g_array_unref);
  gst_analytics_modelinfo_free (modelinfo);
  modelinfo = NULL;

  return TRUE;

error:
  g_clear_pointer (&exporter_configs, g_array_unref);
  if (status)
    self->api->ReleaseStatus (status);

  if (session_options)
    self->api->ReleaseSessionOptions (session_options);

  if (modelinfo)
    gst_analytics_modelinfo_free (modelinfo);

  gst_clear_object (&importer);
  gst_onnx_base_inference_reset (self);

  return FALSE;
}

static gboolean
gst_onnx_base_inference_stop (GstBaseTransform * trans)
{
  GstOnnxBaseInference *self = GST_ONNX_BASE_INFERENCE (trans);

  gst_onnx_base_inference_reset (self);

  return TRUE;
}

static GstFlowReturn
gst_onnx_base_inference_run (GstOnnxBaseInference * self, GstBuffer * buf,
    gsize run_index, GstTensor *** results)
{
  GstOnnxBaseInferencePrivate *priv = self->priv;
  GstOnnxBaseInferenceClass *klass = GST_ONNX_BASE_INFERENCE_GET_CLASS (self);
  OrtStatus *status = NULL;
  OrtValue **output_tensors = NULL;
  OrtIoBinding *binding = NULL;
  const OrtMemoryInfo **output_memory_infos = NULL;
  gboolean needs_binding = FALSE;
  OrtTensorTypeAndShapeInfo *output_tensor_info = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  GstTensor **output_tensors_gst = NULL;

  *results = NULL;

  g_ptr_array_set_size (priv->input_tensors, 0);
  g_ptr_array_set_size (priv->output_tensors, 0);

  ret = klass->prepare_input_tensors (self, buf,
      priv->model_inputs, run_index, priv->input_tensors);
  if (ret != GST_FLOW_OK)
    goto done;

  /* subclass does not want to run for the current buffer */
  if (priv->input_tensors->len == 0)
    goto done;

  if (priv->input_tensors->len != priv->model_inputs->len) {
    GST_ERROR_OBJECT (self, "Expected %u input tensors, got %u",
        priv->model_inputs->len, priv->input_tensors->len);
    ret = GST_FLOW_ERROR;
    goto done;
  }

  g_ptr_array_set_size (priv->output_tensors, priv->output_count);
  output_tensors = (OrtValue **) priv->output_tensors->pdata;

  output_memory_infos = g_newa (const OrtMemoryInfo *, priv->output_count);

  for (guint i = 0; i < priv->output_count; i++) {
    ret = gst_onnx_exporter_prepare (priv->exporter, i, &output_tensors[i],
        &output_memory_infos[i]);
    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "Couldn't prepare output %u", i);
      ret = GST_FLOW_ERROR;
      goto done;
    }

    if (!output_tensors[i] && !GST_IS_ONNX_EXPORTER_CPU (priv->exporter)) {
      needs_binding = TRUE;
    }
  }

  if (needs_binding) {
    status = self->api->CreateIoBinding (priv->session, &binding);
    if (status) {
      GST_ERROR_OBJECT (self, "Couldn't create I/O binding: %s",
          self->api->GetErrorMessage (status));
      ret = GST_FLOW_ERROR;
      goto done;
    }

    for (guint i = 0; i < priv->input_tensors->len; i++) {
      status = self->api->BindInput (binding, priv->input_names[i],
          g_ptr_array_index (priv->input_tensors, i));
      if (status) {
        GST_ERROR_OBJECT (self, "Couldn't bind input %u: %s", i,
            self->api->GetErrorMessage (status));
        ret = GST_FLOW_ERROR;
        goto done;
      }
    }

    for (guint i = 0; i < priv->output_count; i++) {
      if (output_tensors[i]) {
        status = self->api->BindOutput (binding, priv->output_names[i],
            output_tensors[i]);
      } else {
        status = self->api->BindOutputToDevice (binding, priv->output_names[i],
            output_memory_infos[i]);
      }

      if (status) {
        GST_ERROR_OBJECT (self, "Couldn't bind output %u: %s", i,
            self->api->GetErrorMessage (status));
        ret = GST_FLOW_ERROR;
        goto done;
      }
    }
  }

  if (binding) {
    status = self->api->RunWithBinding (priv->session, NULL, binding);
  } else {
    status = self->api->Run (priv->session, NULL,
        (const char *const *) priv->input_names,
        (const OrtValue * const *) priv->input_tensors->pdata,
        priv->input_tensors->len,
        (const char *const *) priv->output_names,
        priv->output_count, output_tensors);

  }

  if (status) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("Failed to run inference: %s", self->api->GetErrorMessage (status)));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  if (binding) {
    OrtValue **bound_outputs = NULL;
    size_t num_outputs = 0;

    status = self->api->GetBoundOutputValues (binding, priv->allocator,
        &bound_outputs, &num_outputs);
    if (status) {
      GST_ERROR_OBJECT (self, "Couldn't get bound outputs: %s",
          self->api->GetErrorMessage (status));
      ret = GST_FLOW_ERROR;
      goto done;
    }

    if (num_outputs != priv->output_count) {
      GST_ERROR_OBJECT (self, "Unexpected output count %zu", num_outputs);

      for (size_t i = 0; i < num_outputs; i++)
        self->api->ReleaseValue (bound_outputs[i]);

      priv->allocator->Free (priv->allocator, bound_outputs);
      ret = GST_FLOW_ERROR;
      goto done;
    }

    g_ptr_array_set_size (priv->output_tensors, 0);
    for (size_t i = 0; i < num_outputs; i++)
      g_ptr_array_add (priv->output_tensors, bound_outputs[i]);
    priv->allocator->Free (priv->allocator, bound_outputs);
    output_tensors = (OrtValue **) priv->output_tensors->pdata;
    self->api->ReleaseIoBinding (binding);
    binding = NULL;
  }

  output_tensors_gst = g_new0 (GstTensor *, priv->output_count);

  for (size_t i = 0; i < priv->output_count; i++) {
    size_t j;
    ONNXTensorElementDataType tensor_type;
    size_t num_dims;
    size_t num_elements;
    gsize buffer_size;
    gsize element_size;

    status = self->api->GetTensorTypeAndShape (output_tensors[i],
        &output_tensor_info);
    if (status) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Failed to get tensor info: %s",
              self->api->GetErrorMessage (status)));
      ret = GST_FLOW_ERROR;
      goto done;
    }

    status = self->api->GetTensorElementType (output_tensor_info, &tensor_type);
    if (status) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Failed to get tensor type: %s",
              self->api->GetErrorMessage (status)));
      ret = GST_FLOW_ERROR;
      goto done;
    }

    status = self->api->GetDimensionsCount (output_tensor_info, &num_dims);
    if (status) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Failed to get dimensions count: %s",
              self->api->GetErrorMessage (status)));
      ret = GST_FLOW_ERROR;
      goto done;
    }

    {
      int64_t *shape = g_alloca (num_dims * sizeof (int64_t));
      GstTensor *tensor;

      status = self->api->GetDimensions (output_tensor_info, shape, num_dims);
      if (status) {
        GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
            ("Failed to get dimensions: %s",
                self->api->GetErrorMessage (status)));
        ret = GST_FLOW_ERROR;
        goto done;
      }

      tensor = gst_tensor_alloc (num_dims);
      output_tensors_gst[i] = tensor;
      tensor->id = priv->output_ids[i];
      tensor->dims_order = priv->output_dims_orders[i];

      for (j = 0; j < num_dims; j++)
        tensor->dims[j] = shape[j];
    }

    status = self->api->GetTensorShapeElementCount (output_tensor_info,
        &num_elements);
    if (status) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Could not get the number of elements in the tensor: %s",
              self->api->GetErrorMessage (status)));
      ret = GST_FLOW_ERROR;
      goto done;
    }

    self->api->ReleaseTensorTypeAndShapeInfo (output_tensor_info);
    output_tensor_info = NULL;

    if (tensor_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
      element_size = sizeof (float);
      output_tensors_gst[i]->data_type = GST_TENSOR_DATA_TYPE_FLOAT32;
    } else if (tensor_type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
      element_size = sizeof (int32_t);
      output_tensors_gst[i]->data_type = GST_TENSOR_DATA_TYPE_INT32;
    } else {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
          ("Output tensor is not FLOAT32 or INT32, not supported"));
      ret = GST_FLOW_ERROR;
      goto done;
    }

    if (!g_size_checked_mul (&buffer_size, num_elements, element_size) ||
        buffer_size > G_MAXUINT) {
      GST_ERROR_OBJECT (self, "Output tensor %zu size exceeds G_MAXUINT", i);
      ret = GST_FLOW_ERROR;
      goto done;
    }

    ret = gst_onnx_exporter_export (priv->exporter, (guint) i,
        output_tensors[i], buffer_size, &output_tensors_gst[i]->data);
    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "Couldn't export output %zu", i);
      goto done;
    }
  }

  *results = output_tensors_gst;
  output_tensors_gst = NULL;

  GST_TRACE_OBJECT (self, "Num tensors:%zu", priv->output_count);

done:
  if (binding)
    self->api->ReleaseIoBinding (binding);
  if (status)
    self->api->ReleaseStatus (status);

  if (output_tensor_info)
    self->api->ReleaseTensorTypeAndShapeInfo (output_tensor_info);

  /* Clean up output tensors */
  g_ptr_array_set_size (priv->output_tensors, 0);
  if (priv->exporter)
    gst_onnx_exporter_unprepare (priv->exporter);

  klass->unprepare_input_tensors (self, buf, priv->input_tensors);
  g_ptr_array_set_size (priv->input_tensors, 0);

  gst_onnx_base_inference_clear_free_tensors (self, output_tensors_gst);

  return ret;
}

static GstFlowReturn
gst_onnx_base_inference_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstOnnxBaseInference *self = GST_ONNX_BASE_INFERENCE (trans);
  GstOnnxBaseInferencePrivate *priv = self->priv;
  GstOnnxBaseInferenceClass *klass = GST_ONNX_BASE_INFERENCE_GET_CLASS (self);
  GstTensor **result = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  gsize num_iterations = 1;

  gst_onnx_base_inference_clear_result_tensors (self);

  if (klass->start_inference) {
    ret = klass->start_inference (self,
        buf, priv->model_inputs, &num_iterations);
    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (self, "Failed to start inference");
      return ret;
    }
  }

  for (gsize i = 0; i < num_iterations; i++) {
    result = NULL;
    ret = gst_onnx_base_inference_run (self, buf, i, &result);
    if (ret != GST_FLOW_OK)
      break;

    if (result) {
      g_ptr_array_add (priv->result_tensors, result);
    } else {
      GST_WARNING_OBJECT (self, "No inference result for iteration %"
          G_GSIZE_FORMAT, i);
    }
  }

  if (klass->end_inference) {
    GstFlowReturn end_ret = klass->end_inference (self,
        buf, priv->result_tensors, priv->output_count, ret == GST_FLOW_OK);

    if (ret == GST_FLOW_OK)
      ret = end_ret;
  }

  gst_onnx_base_inference_clear_result_tensors (self);

  return ret;
}

static GstFlowReturn
gst_onnx_base_inference_end_inference_default (GstOnnxBaseInference * self,
    GstBuffer * buf, GPtrArray * results, gsize num_tensors,
    gboolean attach_meta)
{
  GstTensorMeta *tmeta;
  GstTensor **tensors;

  if (results->len == 0 || !attach_meta)
    return GST_FLOW_OK;

  /* default path is always single-run */
  g_assert (results->len == 1);

  tensors = g_ptr_array_index (results, 0);
  tmeta = gst_buffer_add_tensor_meta (buf);
  if (!tmeta)
    return GST_FLOW_ERROR;

  g_ptr_array_index (results, 0) = NULL;
  tmeta->num_tensors = num_tensors;
  tmeta->tensors = tensors;

  return GST_FLOW_OK;
}

// This has to be last in this file to ensure that MIN_ORT_API_VERSION is
// correctly defined by the pre-processor
static inline int
gst_min_ort_api_version (void)
{
  return MIN_ORT_API_VERSION;
}
