/*
 * GStreamer gstreamer-executorchinference
 * Copyright (C) 2025 Collabora Ltd.
 *
 * gstexecutorchinference.cpp
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
 * SECTION:element-executorchinference
 * @short_description: Run inference using ExecuTorch models in a GStreamer pipeline.
 *
 * This element loads an ExecuTorch model and performs per-buffer inference on incoming
 * video frames. It converts video frames into a tensor that is fed to the model, and the
 * output tensor is attached as meta data to the buffer.
 *
 * The element expects input frames to be in RGB format. The required input size is
 * taken from the model itself, and the element advertises it on its sink pad, so the
 * pipeline only needs to scale the frames to match. If the negotiated frame dimensions
 * do not match the model's expected size, the element will produce an error.
 *
 * ## Example launch command:
 *
 * GST_DEBUG="executorchinference:5,classifiertensordecoder:7" \
 * gst-launch-1.0 filesrc location=path/to/dog.jpg ! \
 * jpegdec ! videoconvertscale ! \
 * executorchinference model-file=mobilenetv2.pte ! \
 * classifiertensordecoder ! fakesink
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <vector>
#include <memory>

#include <executorch/runtime/core/evalue.h>
#include <executorch/runtime/core/portable_type/tensor.h>
#include <executorch/runtime/platform/runtime.h>
#include <executorch/extension/data_loader/file_data_loader.h>
#include <executorch/extension/runner_util/inputs.h>
#include <executorch/runtime/executor/method.h>
#include <executorch/runtime/executor/program.h>
#include <executorch/runtime/core/event_tracer.h>
#include <executorch/extension/evalue_util/print_evalue.h>
#include <executorch/runtime/core/exec_aten/util/scalar_type_util.h>

#include "gstexecutorchinference.h"


GST_DEBUG_CATEGORY_STATIC (executorch_inference_debug);
#define GST_CAT_DEFAULT executorch_inference_debug

GST_ELEMENT_REGISTER_DEFINE (executorch_inference, "executorchinference",
    GST_RANK_PRIMARY, GST_TYPE_EXECUTORCH_INFERENCE);

/* GstExecuTorchInference properties */
enum
{
  PROP_0,
  PROP_MODEL_FILE,
};

/* Only the formats video_frame_to_tensor_data() can convert */
#define VIDEO_CAPS GST_VIDEO_CAPS_MAKE ("{ RGB, RGBA, BGR, BGRA }")

/* Pools backing the ExecuTorch method and temp allocators. They hold the
 * runtime's own bookkeeping during Method load and kernel scratch space
 * during execute(), not the input tensor data (which lives in the
 * std::vector filled by video_frame_to_tensor_data()). */
#define METHOD_ALLOCATOR_POOL_SIZE (4 * 1024U * 1024U)
#define TEMP_ALLOCATOR_POOL_SIZE (1024U * 1024U)

static GstStaticPadTemplate gst_executorch_inference_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS)
    );

static GstStaticPadTemplate gst_executorch_inference_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS)
    );

static gboolean gst_executorch_inference_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstCaps *gst_executorch_inference_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps);
static gboolean gst_executorch_load_model_and_initialize (GstExecuTorchInference
    * self);
static void gst_executorch_inference_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_executorch_inference_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean gst_executorch_inference_process (GstBaseTransform * trans,
    GstBuffer * buf);
static GstFlowReturn gst_executorch_inference_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);
static gboolean gst_executorch_inference_start (GstBaseTransform * trans);
static gboolean gst_executorch_inference_stop (GstBaseTransform * trans);
static void gst_executorch_inference_finalize (GObject * object);
static void video_frame_to_tensor_data (GstExecuTorchInference * self,
    const GstVideoFrame * frame, std::vector < float >&tensor_data);

/**
 * GstExecuTorchInference:
 *
 * ExecuTorch Inference element.
 */
struct _GstExecuTorchInference
{
  GstBaseTransform parent;

  /* "model-file" property: path to the .pte file */
  gchar *model_path;

  /* ExecuTorch runtime objects for the loaded model */
  std::unique_ptr < executorch::runtime::Method > method;
  std::unique_ptr < executorch::extension::FileDataLoader > data_loader;
  std::unique_ptr < executorch::runtime::Program > program;
  std::unique_ptr < executorch::runtime::MethodMeta > method_meta;

  /* Memory-planned buffers requested by the model and the allocators
   * handed to the Method */
  std::vector < std::unique_ptr < uint8_t[] >> planned_buffers;
  std::vector < executorch::runtime::Span < uint8_t >> planned_spans;
  std::unique_ptr < executorch::runtime::HierarchicalAllocator > planned_memory;
  std::unique_ptr < executorch::runtime::MemoryManager > memory_manager;
  std::unique_ptr < executorch::runtime::MemoryAllocator > method_allocator;
  std::unique_ptr < executorch::runtime::MemoryAllocator > temp_allocator;
  uint8_t *method_allocator_pool;
  uint8_t *temp_allocator_pool;

  /* Negotiated input video format */
  GstVideoInfo in_info;

  /* Input size read from the model ([N, C, H, W]), 0 until it is loaded */
  guint input_height;
  guint input_width;

  /* Per-channel (R, G, B) normalization from the modelinfo ranges:
   * tensor = pixel * scale + offset */
  gdouble *scales;
  gdouble *offsets;

  /* Outputs described by modelinfo: a GstTensor template per mapped output
   * and the index of that output in the model */
  GPtrArray *m_tensor_templates;
  std::vector < size_t > m_output_indices;

  /* Caps restrictions derived from the model, applied in transform_caps() */
  GstCaps *model_incaps;
  GstCaps *model_outcaps;
};

G_DEFINE_TYPE (GstExecuTorchInference, gst_executorch_inference,
    GST_TYPE_BASE_TRANSFORM);

static void
gst_executorch_inference_reset (GstExecuTorchInference * self)
{
  self->method.reset ();
  self->method_meta.reset ();
  self->memory_manager.reset ();
  self->method_allocator.reset ();
  self->temp_allocator.reset ();
  self->planned_memory.reset ();
  self->program.reset ();
  self->data_loader.reset ();

  self->planned_buffers.clear ();
  self->planned_spans.clear ();

  g_clear_pointer (&self->method_allocator_pool, g_free);
  g_clear_pointer (&self->temp_allocator_pool, g_free);
  g_clear_pointer (&self->scales, g_free);
  g_clear_pointer (&self->offsets, g_free);

  self->m_output_indices.clear ();
  if (self->m_tensor_templates)
    g_ptr_array_set_size (self->m_tensor_templates, 0);

  gst_clear_caps (&self->model_incaps);
  gst_clear_caps (&self->model_outcaps);
}

static void
gst_executorch_inference_finalize (GObject * object)
{
  GstExecuTorchInference *self = GST_EXECUTORCH_INFERENCE (object);

  gst_executorch_inference_reset (self);

  g_free (self->model_path);
  g_ptr_array_unref (self->m_tensor_templates);

  G_OBJECT_CLASS (gst_executorch_inference_parent_class)->finalize (object);
}

static void
gst_executorch_inference_class_init (GstExecuTorchInferenceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_executorch_inference_set_property;
  gobject_class->get_property = gst_executorch_inference_get_property;
  gobject_class->finalize = gst_executorch_inference_finalize;

  GST_DEBUG_CATEGORY_INIT (executorch_inference_debug, "executorchinference", 0,
      "ExecuTorch Inference element");

  /**
   * GstExecuTorchInference:model-file
   *
   * Path to the ExecuTorch model file
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_MODEL_FILE,
      g_param_spec_string ("model-file",
          "ExecuTorch model file",
          "Path to the ExecuTorch model file (.pte). A companion modelinfo "
          "file is required, looked up as <model-file>.modelinfo then "
          "<model-file without extension>.modelinfo",
          "",
          static_cast < GParamFlags >
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class,
      "executorchinference",
      "Filter/Video",
      "Run inference using executorch model and create output tensor",
      "Vineet Suryan <vineet.suryan@collabora.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_executorch_inference_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_executorch_inference_sink_template));
  basetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_executorch_inference_transform_ip);
  basetransform_class->start =
      GST_DEBUG_FUNCPTR (gst_executorch_inference_start);
  basetransform_class->stop =
      GST_DEBUG_FUNCPTR (gst_executorch_inference_stop);
  basetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_executorch_inference_transform_caps);
  basetransform_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_executorch_inference_set_caps);
}

static void
gst_executorch_inference_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstExecuTorchInference *self = GST_EXECUTORCH_INFERENCE (object);

  switch (prop_id) {
    case PROP_MODEL_FILE:
      g_free (self->model_path);
      self->model_path = g_value_dup_string (value);
      GST_DEBUG_OBJECT (self, "Model file set to %s", self->model_path);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_executorch_inference_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstExecuTorchInference *self = GST_EXECUTORCH_INFERENCE (object);
  switch (prop_id) {
    case PROP_MODEL_FILE:
      g_value_set_string (value, self->model_path);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstTensorDataType
gst_executorch_convert_data_type (::executorch::aten::ScalarType t)
{
  switch (t) {
    case::executorch::aten::ScalarType::Byte:
      return GST_TENSOR_DATA_TYPE_UINT8;
    case::executorch::aten::ScalarType::Char:
      return GST_TENSOR_DATA_TYPE_INT8;
    case::executorch::aten::ScalarType::Short:
      return GST_TENSOR_DATA_TYPE_INT16;
    case::executorch::aten::ScalarType::Int:
      return GST_TENSOR_DATA_TYPE_INT32;
    case::executorch::aten::ScalarType::Long:
      return GST_TENSOR_DATA_TYPE_INT64;
    case::executorch::aten::ScalarType::Half:
      return GST_TENSOR_DATA_TYPE_FLOAT16;
    case::executorch::aten::ScalarType::Float:
      return GST_TENSOR_DATA_TYPE_FLOAT32;
    case::executorch::aten::ScalarType::Double:
      return GST_TENSOR_DATA_TYPE_FLOAT64;
    default:
      GST_FIXME ("Unsupported ExecuTorch scalar type %s",::executorch::
          runtime::toString (t));
      g_assert_not_reached ();
      return GST_TENSOR_DATA_TYPE_FLOAT32;
  }
}


static void
gst_executorch_inference_init (GstExecuTorchInference * self)
{
  self->method = nullptr;
  self->data_loader = nullptr;
  self->program = nullptr;
  self->planned_memory = nullptr;
  self->memory_manager = nullptr;
  self->method_allocator = nullptr;
  self->temp_allocator = nullptr;
  self->method_allocator_pool = nullptr;
  self->temp_allocator_pool = nullptr;
  self->planned_buffers.clear ();
  self->planned_spans.clear ();

  gst_video_info_init (&self->in_info);
  self->scales = nullptr;
  self->offsets = nullptr;

  self->m_tensor_templates = g_ptr_array_new_with_free_func ((GDestroyNotify)
      gst_tensor_free);
  self->model_incaps = NULL;
  self->model_outcaps = NULL;
}

static gboolean
gst_executorch_inference_start (GstBaseTransform * trans)
{
  GstExecuTorchInference *self = GST_EXECUTORCH_INFERENCE (trans);
  GST_DEBUG_OBJECT (self, "Starting element: loading model from %s",
      self->model_path);

  /* Allocate memory pools */
  self->method_allocator_pool =
      static_cast < uint8_t * >(g_malloc (METHOD_ALLOCATOR_POOL_SIZE));
  self->temp_allocator_pool =
      static_cast < uint8_t * >(g_malloc (TEMP_ALLOCATOR_POOL_SIZE));

  /* Initialize the ExecuTorch runtime */
  executorch::runtime::runtime_init ();

  if (!gst_executorch_load_model_and_initialize (self)) {
    GST_ERROR_OBJECT (self, "Failed to initialize ExecuTorch model");
    gst_executorch_inference_reset (self);
    return FALSE;
  }

  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), FALSE);
  return TRUE;
}

static gboolean
gst_executorch_inference_stop (GstBaseTransform * trans)
{
  GstExecuTorchInference *self = GST_EXECUTORCH_INFERENCE (trans);

  GST_DEBUG_OBJECT (self, "Stopping element");
  gst_executorch_inference_reset (self);

  return TRUE;
}

static void
gst_executorch_free_tensors (GstTensor ** tensors, gsize num)
{
  for (gsize i = 0; i < num; i++)
    gst_tensor_free (tensors[i]);
  g_free (tensors);
}

static gboolean
gst_executorch_inference_process (GstBaseTransform * trans, GstBuffer * buf)
{
  GstExecuTorchInference *self = GST_EXECUTORCH_INFERENCE (trans);
  GstVideoFrame frame;
  bool frame_mapped = false;

  try {
    if (!self->method) {
      GST_ERROR_OBJECT (self, "Model not loaded; cannot execute inference");
      return FALSE;
    }

    if (!gst_video_frame_map (&frame, &self->in_info, buf, GST_MAP_READ)) {
      GST_ERROR_OBJECT (self, "Failed to map input buffer as video frame");
      return FALSE;
    }
    frame_mapped = true;

    std::vector < float >input_data;
    video_frame_to_tensor_data (self, &frame, input_data);
    gst_video_frame_unmap (&frame);
    frame_mapped = false;

    auto tensor_meta = self->method_meta->input_tensor_meta (0);
    if (!tensor_meta.ok ()) {
      GST_ERROR_OBJECT (self, "Failed to get input tensor meta");
      return FALSE;
    }
    executorch::runtime::etensor::Tensor::SizesType sizes[] =
        { 1, 3, (executorch::runtime::etensor::Tensor::SizesType)
        self->input_height,
        (executorch::runtime::etensor::Tensor::SizesType) self->input_width };
    executorch::runtime::etensor::Tensor::DimOrderType dim_order[] =
        { 0, 1, 2, 3 };

    executorch::runtime::etensor::TensorImpl impl (tensor_meta->scalar_type (),
        4, sizes,
        reinterpret_cast < uint8_t * >(input_data.data ()), dim_order);
    executorch::runtime::etensor::Tensor t (&impl);

    auto set_input_error = self->method->set_input (t, 0);
    if (set_input_error != executorch::runtime::Error::Ok) {
      GST_ERROR_OBJECT (self, "Failed to set input tensor");
      return FALSE;
    }

    auto status = self->method->execute ();
    if (status != executorch::runtime::Error::Ok) {
      GST_ERROR_OBJECT (self, "Model execution failed: 0x%" PRIx32,
          (uint32_t) status);
      return FALSE;
    }

    std::vector < executorch::runtime::EValue >
        outputs (self->method->outputs_size ());
    auto out_status =
        self->method->get_outputs (outputs.data (), outputs.size ());
    if (out_status != executorch::runtime::Error::Ok) {
      GST_ERROR_OBJECT (self, "Failed to retrieve ExecuTorch outputs");
      return FALSE;
    }

    if (outputs.empty ()) {
      GST_WARNING_OBJECT (self, "No outputs returned");
      return TRUE;
    }

    gsize num_mapped = self->m_tensor_templates->len;
    GstTensor **tensors =
        (GstTensor **) g_malloc0_n (num_mapped, sizeof (gpointer));

    for (gsize t = 0; t < num_mapped; ++t) {
      size_t i = self->m_output_indices[t];
      if (i >= outputs.size ()
          || outputs[i].tag != executorch::runtime::Tag::Tensor) {
        GST_ERROR_OBJECT (self,
            "Output %zu is missing or not a tensor (model has %zu outputs)",
            i, outputs.size ());
        gst_executorch_free_tensors (tensors, t);
        return FALSE;
      }
      auto output_tensor = outputs[i].toTensor ();
      size_t rank = static_cast < size_t >(output_tensor.dim ());
      tensors[t] = gst_tensor_alloc (rank);
      memcpy (tensors[t], g_ptr_array_index (self->m_tensor_templates, t),
          sizeof (GstTensor));
      tensors[t]->num_dims = rank;
      for (gsize j = 0; j < rank; j++) {
        tensors[t]->dims[j] = static_cast < gsize > (output_tensor.size (j));
      }

      size_t numel = 1;
      for (size_t d = 0; d < rank; d++) {
        numel *= static_cast < size_t >(output_tensor.size (d));
      }
      size_t element_size =
          executorch::runtime::elementSize (output_tensor.scalar_type ());
      size_t buffer_size = numel * element_size;
      tensors[t]->data = gst_buffer_new_allocate (NULL, buffer_size, NULL);
      const void *data_ptr = output_tensor.data_ptr ();
      gst_buffer_fill (tensors[t]->data, 0, data_ptr, buffer_size);
    }

    /* Attach the output tensors as GstTensorMeta to the output GstBuffer */
    GstTensorMeta *tmeta = gst_buffer_add_tensor_meta (buf);
    if (!tmeta) {
      GST_ERROR_OBJECT (self, "Failed to add tensor meta to buffer");
      gst_executorch_free_tensors (tensors, num_mapped);
      return FALSE;
    }
    gst_tensor_meta_set (tmeta, num_mapped, tensors);

    return TRUE;
  }
  catch (const std::exception & e)
  {
    GST_ERROR_OBJECT (self,
        "Exception caught in gstexecutorch inference process: %s", e.what ());
    if (frame_mapped) {
      gst_video_frame_unmap (&frame);
    }
    return FALSE;
  }
  catch ( ...) {
    GST_ERROR_OBJECT (self,
        "Unknown exception caught in gstexecutorch inference process");
    if (frame_mapped) {
      gst_video_frame_unmap (&frame);
    }
    return FALSE;
  }
}

static GstFlowReturn
gst_executorch_inference_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf)
{
  if (!gst_base_transform_is_passthrough (trans) &&
      !gst_executorch_inference_process (trans, buf)) {
    GST_ELEMENT_ERROR (trans, STREAM, FAILED, (NULL),
        ("ExecuTorch inference processing failed"));
    return GST_FLOW_ERROR;
  }
  return GST_FLOW_OK;
}

/* Read the per-channel normalization of the model's input tensor from the
 * modelinfo "ranges" field, for uint8 [0, 255] video input */
static gboolean
gst_executorch_get_input_scales_offsets (GstExecuTorchInference * self,
    GstAnalyticsModelInfo * modelinfo,
    const executorch::runtime::TensorInfo & input_info)
{
  auto sizes = input_info.sizes ();
  gsize num_dims = sizes.size ();
  gsize *dims = (gsize *) g_malloc0_n (num_dims, sizeof (gsize));
  gchar *tensor_name;
  gdouble *mins = NULL, *maxs = NULL;
  gsize num_ranges;
  gboolean ret = FALSE;

  for (gsize i = 0; i < num_dims; i++)
    dims[i] = (gsize) sizes[i];

  tensor_name = gst_analytics_modelinfo_find_tensor_name (modelinfo,
      MODELINFO_DIRECTION_INPUT, 0, NULL,
      gst_executorch_convert_data_type (input_info.scalar_type ()),
      num_dims, dims);
  g_free (dims);

  if (tensor_name == NULL) {
    GST_ERROR_OBJECT (self, "No modelinfo for the input tensor");
    return FALSE;
  }

  if (!gst_analytics_modelinfo_get_target_ranges (modelinfo, tensor_name,
          &num_ranges, &mins, &maxs)) {
    GST_ERROR_OBJECT (self, "No ranges in modelinfo for input tensor %s",
        tensor_name);
    goto done;
  }

  if (num_ranges != 3) {
    GST_ERROR_OBJECT (self,
        "Expected 3 (R, G, B) ranges for input tensor %s, got %zu",
        tensor_name, num_ranges);
    goto done;
  }

  /* Reuse the arrays for the uint8 input range of every channel */
  for (gsize i = 0; i < num_ranges; i++) {
    mins[i] = 0.0;
    maxs[i] = 255.0;
  }

  ret = gst_analytics_modelinfo_get_input_scales_offsets (modelinfo,
      tensor_name, num_ranges, mins, maxs, &num_ranges, &self->scales,
      &self->offsets);
  if (!ret)
    GST_ERROR_OBJECT (self, "Failed to get scales/offsets for input tensor %s",
        tensor_name);

  for (gsize i = 0; ret && i < num_ranges; i++)
    GST_DEBUG_OBJECT (self, "Input channel %zu: scale=%f offset=%f", i,
        self->scales[i], self->offsets[i]);

done:
  g_free (mins);
  g_free (maxs);
  g_free (tensor_name);
  return ret;
}

static gboolean
gst_executorch_load_model_and_initialize (GstExecuTorchInference * self)
{
  const char *model_path = self->model_path;
  GstAnalyticsModelInfo *modelinfo = NULL;
  gchar *tensor_name = NULL;
  GstStructure *tensors_s = NULL;
  GValue v_tensors_set = G_VALUE_INIT;
  size_t num_outputs;

  auto loader_result = executorch::extension::FileDataLoader::from (model_path);
  if (!loader_result.ok ()) {
    GST_ERROR_OBJECT (self, "Failed to load model file: %s", model_path);
    return FALSE;
  }
  self->data_loader =
      std::make_unique < executorch::extension::FileDataLoader >
      (std::move (loader_result.get ()));

  auto program_result =
      executorch::runtime::Program::load (self->data_loader.get ());
  if (!program_result.ok ()) {
    GST_ERROR_OBJECT (self, "Failed to parse model file: %s", model_path);
    return FALSE;
  }
  self->program =
      std::make_unique < executorch::runtime::Program >
      (std::move (program_result.get ()));

  auto method_name_result = self->program->get_method_name (0);
  if (!method_name_result.ok ()) {
    GST_ERROR_OBJECT (self, "No methods found in model file: %s", model_path);
    return FALSE;
  }
  const char *method_name = *method_name_result;

  auto method_meta_result = self->program->method_meta (method_name);
  if (!method_meta_result.ok ()) {
    GST_ERROR_OBJECT (self, "Failed to get method meta for %s", method_name);
    return FALSE;
  }
  self->method_meta =
      std::make_unique < executorch::runtime::MethodMeta >
      (std::move (method_meta_result.get ()));

  /* Derive the expected input dimensions from the model's input tensor meta
   * ([N, C, H, W]). The model is the single source of truth for the input
   * size, so there is no need to configure it externally. */
  auto input_tensor_meta = self->method_meta->input_tensor_meta (0);
  if (!input_tensor_meta.ok ()) {
    GST_ERROR_OBJECT (self, "Failed to get input tensor meta");
    return FALSE;
  }
  auto input_sizes = input_tensor_meta->sizes ();
  if (input_sizes.size () != 4) {
    GST_ERROR_OBJECT (self, "Expected input tensor to have rank 4, got %zu",
        input_sizes.size ());
    return FALSE;
  }
  if (input_tensor_meta->scalar_type () !=
      ::executorch::aten::ScalarType::Float) {
    GST_ERROR_OBJECT (self, "Only float32 input tensors are supported, got %s",
        ::executorch::runtime::toString (input_tensor_meta->scalar_type ()));
    return FALSE;
  }
  self->input_height = input_sizes[2];
  self->input_width = input_sizes[3];
  GST_DEBUG_OBJECT (self, "Model input size: %ux%u",
      self->input_width, self->input_height);

  size_t num_planned = self->method_meta->num_memory_planned_buffers ();
  for (size_t id = 0; id < num_planned; ++id) {
    size_t buffer_size =
        static_cast <
        size_t >(self->method_meta->memory_planned_buffer_size (id).get ());
    self->planned_buffers.push_back (std::make_unique < uint8_t[] >
        (buffer_size));
    self->planned_spans.push_back ( {
        self->planned_buffers.back ().get (), buffer_size});
  }
  self->planned_memory =
      std::make_unique < executorch::runtime::HierarchicalAllocator >
      (executorch::runtime::HierarchicalAllocator ( {
          self->planned_spans.data (), self->planned_spans.size ()})
      );
  self->method_allocator =
      std::make_unique < executorch::runtime::MemoryAllocator >
      (METHOD_ALLOCATOR_POOL_SIZE, self->method_allocator_pool);
  self->temp_allocator =
      std::make_unique < executorch::runtime::MemoryAllocator >
      (TEMP_ALLOCATOR_POOL_SIZE, self->temp_allocator_pool);
  self->memory_manager =
      std::make_unique < executorch::runtime::MemoryManager >
      (self->method_allocator.get (), self->planned_memory.get (),
      self->temp_allocator.get ()
      );

  auto method_result =
      self->program->load_method (method_name, self->memory_manager.get (),
      /* event_tracer= */ nullptr);
  if (!method_result.ok ()) {
    GST_ERROR_OBJECT (self, "Failed to load method %s", method_name);
    return FALSE;
  }
  self->method =
      std::make_unique < executorch::runtime::Method >
      (std::move (method_result.get ()));

  modelinfo = gst_analytics_modelinfo_load (self->model_path);
  if (!modelinfo) {
    GST_ERROR_OBJECT (self, "Can't find modelinfo for %s", self->model_path);
    return FALSE;
  }

  if (!gst_executorch_get_input_scales_offsets (self, modelinfo,
          *input_tensor_meta)) {
    gst_analytics_modelinfo_free (modelinfo);
    return FALSE;
  }

  gst_clear_caps (&self->model_incaps);
  self->model_incaps = gst_caps_new_empty_simple ("video/x-raw");
  gst_caps_set_simple (self->model_incaps,
      "width", G_TYPE_INT, self->input_width,
      "height", G_TYPE_INT, self->input_height, NULL);

  gst_caps_set_simple (self->model_incaps, "pixel-aspect-ratio",
      GST_TYPE_FRACTION, 1, 1, NULL);

  gst_clear_caps (&self->model_outcaps);
  num_outputs = self->method_meta->num_outputs ();

  if (num_outputs != 0) {
    tensors_s = gst_structure_new_empty ("tensorgroups");
    g_value_init (&v_tensors_set, GST_TYPE_UNIQUE_LIST);
  }

  for (size_t i = 0; i < num_outputs; ++i) {
    auto tensor_info = *self->method_meta->output_tensor_meta (i);
    auto sizes = tensor_info.sizes ();
    auto dtype = tensor_info.scalar_type ();
    gsize dims_count = sizes.size ();
    gsize *dims = (gsize *) g_malloc0_n (dims_count, sizeof (gsize));
    for (gsize j = 0; j < dims_count; j++)
      dims[j] = (gsize) sizes[j];
    GstTensorDataType data_type = gst_executorch_convert_data_type (dtype);

    tensor_name = gst_analytics_modelinfo_find_tensor_name (modelinfo,
        MODELINFO_DIRECTION_OUTPUT, i, NULL, data_type, dims_count, dims);

    if (tensor_name == NULL) {
      GST_WARNING_OBJECT (self,
          "No model info for output_tensor[%zu] (type %s), skipping",
          i, gst_tensor_data_type_get_name (data_type));
      g_free (dims);
      continue;
    }

    GstTensor *t = gst_tensor_alloc (dims_count);

    gchar *id = gst_analytics_modelinfo_get_id (modelinfo, tensor_name);
    GstTensorDimOrder dims_order =
        gst_analytics_modelinfo_get_dims_order (modelinfo, tensor_name);
    const gchar *dims_order_str =
        dims_order ==
        GST_TENSOR_DIM_ORDER_COL_MAJOR ? "col-major" : "row-major";

    GST_DEBUG_OBJECT (self,
        "Mapping output_tensor[%zu] of type %s to id %s (%s)", i,
        gst_tensor_data_type_get_name (data_type), id, dims_order_str);

    t->id = gst_analytics_modelinfo_get_quark_id (modelinfo, tensor_name);
    t->layout = GST_TENSOR_LAYOUT_CONTIGUOUS;
    t->data_type = data_type;
    t->dims_order = dims_order;
    memcpy (t->dims, dims, sizeof (gsize) * t->num_dims);

    GstStructure *tensor_desc = gst_structure_new_empty ("tensor/strided");
    GValue val_dims = G_VALUE_INIT, val = G_VALUE_INIT;
    GValue val_caps = G_VALUE_INIT;
    GValue val_dt = G_VALUE_INIT;

    gst_value_array_init (&val_dims, t->num_dims);
    g_value_init (&val, G_TYPE_INT);
    g_value_init (&val_caps, GST_TYPE_CAPS);
    g_value_init (&val_dt, G_TYPE_STRING);

    for (gsize k = 0; k < t->num_dims; k++) {
      g_value_set_int (&val, t->dims[k] ? t->dims[k] : 0);
      gst_value_array_append_value (&val_dims, &val);
    }

    gst_structure_set (tensor_desc, "dims-order", G_TYPE_STRING, dims_order_str,
        "tensor-id", G_TYPE_STRING, id, NULL);
    gst_structure_take_value (tensor_desc, "dims", &val_dims);
    g_value_unset (&val);

    g_value_set_string (&val_dt, gst_tensor_data_type_get_name (t->data_type));
    gst_structure_take_value (tensor_desc, "type", &val_dt);

    GstCaps *tensor_caps = gst_caps_new_full (tensor_desc, NULL);
    gst_value_set_caps (&val_caps, tensor_caps);
    gst_caps_unref (tensor_caps);
    gst_value_unique_list_append_and_take_value (&v_tensors_set, &val_caps);

    g_free (dims);
    g_free (id);
    g_ptr_array_add (self->m_tensor_templates, t);
    self->m_output_indices.push_back (i);
    g_free (tensor_name);
  }

  /* Finalize output caps if we have any mapped tensors */
  if (self->m_tensor_templates->len > 0 && tensors_s) {
    gchar *gid = gst_analytics_modelinfo_get_group_id (modelinfo);
    gst_structure_set_value (tensors_s, gid, &v_tensors_set);
    g_free (gid);

    self->model_outcaps = gst_caps_new_simple ("video/x-raw", "tensors",
        GST_TYPE_STRUCTURE, tensors_s, NULL);
  }

  gst_analytics_modelinfo_free (modelinfo);
  return TRUE;
}

static GstCaps *
gst_executorch_inference_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps)
{
  GstExecuTorchInference *self = GST_EXECUTORCH_INFERENCE (trans);
  GstCaps *other_caps, *restrictions;

  if (self->model_incaps == NULL) {
    other_caps = gst_caps_ref (caps);
    goto done;
  }

  GST_DEBUG_OBJECT (self, "Applying caps restrictions: %" GST_PTR_FORMAT,
      self->model_incaps);

  if (direction == GST_PAD_SINK) {
    restrictions = gst_caps_intersect_full (caps, self->model_incaps,
        GST_CAPS_INTERSECT_FIRST);
    other_caps = gst_caps_intersect (restrictions, self->model_outcaps);
    gst_caps_unref (restrictions);
  } else if (direction == GST_PAD_SRC) {
    GstCaps *tmp_caps = gst_caps_copy (caps);

    if (!gst_caps_is_empty (tmp_caps)) {
      GstStructure *tstruct = gst_caps_get_structure (tmp_caps, 0);
      gst_structure_remove_field (tstruct, "tensors");
    }

    other_caps = gst_caps_intersect_full (tmp_caps, self->model_incaps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp_caps);
  }

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
gst_executorch_inference_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstExecuTorchInference *self = GST_EXECUTORCH_INFERENCE (trans);
  if (!gst_video_info_from_caps (&self->in_info, incaps)) {
    GST_ERROR_OBJECT (self, "Failed to get video info from input caps");
    return FALSE;
  }

  guint caps_width = GST_VIDEO_INFO_WIDTH (&self->in_info);
  guint caps_height = GST_VIDEO_INFO_HEIGHT (&self->in_info);

  if (caps_width != self->input_width || caps_height != self->input_height) {
    GST_ERROR_OBJECT (self,
        "Input caps size %ux%u does not match the model input size %ux%u",
        caps_width, caps_height, self->input_width, self->input_height);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Input caps set: width=%d, height=%d, format=%s",
      caps_width,
      caps_height,
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&self->in_info)));
  return TRUE;
}

static void
video_frame_to_tensor_data (GstExecuTorchInference * self,
    const GstVideoFrame * frame, std::vector < float >&tensor_data)
{
  guint width = GST_VIDEO_FRAME_WIDTH (frame);
  guint height = GST_VIDEO_FRAME_HEIGHT (frame);
  gsize plane_size = (gsize) width * height;
  const guint8 *src = (const guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  gint stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);
  gint pstride = GST_VIDEO_FRAME_COMP_PSTRIDE (frame, 0);

  tensor_data.resize (3 * plane_size);

  /* Packed 8-bit RGB/BGR(A) to planar float [C, H, W]. The component
   * offsets of the video format take care of the channel order. */
  for (guint c = 0; c < 3; c++) {
    const guint8 *comp = src + GST_VIDEO_FRAME_COMP_OFFSET (frame, c);
    float *dst = tensor_data.data () + c * plane_size;
    float scale = self->scales[c];
    float offset = self->offsets[c];

    for (guint y = 0; y < height; y++) {
      const guint8 *row = comp + y * stride;
      for (guint x = 0; x < width; x++)
        dst[y * width + x] = row[x * pstride] * scale + offset;
    }
  }
}
