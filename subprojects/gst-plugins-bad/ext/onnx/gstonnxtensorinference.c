/* GStreamer
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

/**
 * SECTION:element-onnxtensorinference
 * @short_description: Run ONNX inference on input tensors
 *
 * This element runs an ONNX model using tensors attached to the input buffer.
 * Unlike #onnxinference, it takes tensors from #GstTensorMeta or
 * #GstTensorListMeta as model inputs instead of processing the input media
 * buffer. Inference results are attached to the buffer as tensor metadata.
 *
 * Input tensors can be produced by an upstream preprocessor or another
 * inference element, allowing multiple inference models to be chained.
 * #GstTensorListMeta can also be used to process multiple tensor sets from
 * the same buffer.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstonnx-config.h"
#include "gstonnxtensorinference.h"

#include <gst/gst.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (onnx_tensor_inference_debug);
#define GST_CAT_DEFAULT onnx_tensor_inference_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_REMOVE_TENSOR_META,
};

#define DEFAULT_REMOVE_TENSOR_META TRUE

struct _GstOnnxTensorInference
{
  GstOnnxBaseInference parent;

  GstStructure *input_tensorgroups;
  GstStructure *output_tensorgroups;
  GPtrArray *input_targets;
  GstOnnxImporter *importer;
  GQuark group_id;
  GstTensorMeta *tensor_meta;
  GstTensorListMeta *tensor_list_meta;

  GMutex lock;

  gboolean remove_tensor_meta;
};

static void gst_onnx_tensor_inference_finalize (GObject * object);
static void gst_onnx_tensor_inference_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_onnx_tensor_inference_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstCaps *gst_onnx_tensor_inference_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps);
static gboolean
gst_onnx_tensor_inference_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps);

static void gst_onnx_tensor_inference_reset (GstOnnxBaseInference * base);
static gboolean
gst_onnx_tensor_inference_process_model_info (GstOnnxBaseInference * base,
    GstAnalyticsModelInfo * modelinfo, const GstOnnxModelInfo * parsed_info,
    GstOnnxImporter * importer);
static GstFlowReturn
gst_onnx_tensor_inference_start_inference (GstOnnxBaseInference * base,
    GstBuffer * buffer, const GArray * model_inputs, gsize * num_iterations);
static GstFlowReturn
gst_onnx_tensor_inference_prepare_input_tensors (GstOnnxBaseInference * base,
    GstBuffer * buffer, const GArray * model_inputs, gsize iteration,
    GPtrArray * input_tensors);
static void
gst_onnx_tensor_inference_unprepare_input_tensors (GstOnnxBaseInference * base,
    GstBuffer * buffer, GPtrArray * input_tensors);
static GstFlowReturn
gst_onnx_tensor_inference_end_inference (GstOnnxBaseInference * base,
    GstBuffer * buffer, GPtrArray * results, gsize num_tensors,
    gboolean attach_meta);

#define gst_onnx_tensor_inference_parent_class parent_class
G_DEFINE_TYPE (GstOnnxTensorInference,
    gst_onnx_tensor_inference, GST_TYPE_ONNX_BASE_INFERENCE);
GST_ELEMENT_REGISTER_DEFINE (onnx_tensor_inference, "onnxtensorinference",
    GST_RANK_NONE, GST_TYPE_ONNX_TENSOR_INFERENCE);

static void
gst_onnx_tensor_inference_class_init (GstOnnxTensorInferenceClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstOnnxBaseInferenceClass *onnx_class = GST_ONNX_BASE_INFERENCE_CLASS (klass);

  gobject_class->finalize = gst_onnx_tensor_inference_finalize;
  gobject_class->set_property = gst_onnx_tensor_inference_set_property;
  gobject_class->get_property = gst_onnx_tensor_inference_get_property;

  g_object_class_install_property (gobject_class, PROP_REMOVE_TENSOR_META,
      g_param_spec_boolean ("remove-tensor-meta",
          "Remove tensor meta",
          "Remove used input tensor meta after inference",
          DEFAULT_REMOVE_TENSOR_META,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class, "onnxtensorinference",
      "Filter",
      "Apply neural network to input tensors and create tensor output",
      "Seungha Yang <seungha@centricular.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_onnx_tensor_inference_transform_caps);
  trans_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_onnx_tensor_inference_set_caps);

  onnx_class->reset = GST_DEBUG_FUNCPTR (gst_onnx_tensor_inference_reset);
  onnx_class->process_model_info =
      GST_DEBUG_FUNCPTR (gst_onnx_tensor_inference_process_model_info);
  onnx_class->start_inference =
      GST_DEBUG_FUNCPTR (gst_onnx_tensor_inference_start_inference);
  onnx_class->prepare_input_tensors =
      GST_DEBUG_FUNCPTR (gst_onnx_tensor_inference_prepare_input_tensors);
  onnx_class->unprepare_input_tensors =
      GST_DEBUG_FUNCPTR (gst_onnx_tensor_inference_unprepare_input_tensors);
  onnx_class->end_inference =
      GST_DEBUG_FUNCPTR (gst_onnx_tensor_inference_end_inference);

  onnx_class->proc_type = GST_ONNX_IMPORTER_PROC_WRAP;

  GST_DEBUG_CATEGORY_INIT (onnx_tensor_inference_debug,
      "onnxtensorinference", 0, "onnxtensorinference");
}

static void
gst_onnx_tensor_inference_init (GstOnnxTensorInference * self)
{
  self->input_targets = g_ptr_array_new ();
  self->remove_tensor_meta = DEFAULT_REMOVE_TENSOR_META;

  g_mutex_init (&self->lock);
}

static void
gst_onnx_tensor_inference_finalize (GObject * object)
{
  GstOnnxTensorInference *self = GST_ONNX_TENSOR_INFERENCE (object);

  gst_onnx_tensor_inference_reset (GST_ONNX_BASE_INFERENCE (self));
  g_ptr_array_unref (self->input_targets);
  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_onnx_tensor_inference_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOnnxTensorInference *self = GST_ONNX_TENSOR_INFERENCE (object);

  switch (prop_id) {
    case PROP_REMOVE_TENSOR_META:
      self->remove_tensor_meta = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_onnx_tensor_inference_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOnnxTensorInference *self = GST_ONNX_TENSOR_INFERENCE (object);

  switch (prop_id) {
    case PROP_REMOVE_TENSOR_META:
      g_value_set_boolean (value, self->remove_tensor_meta);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
caps_has_tensor_group (const GstCaps * caps, const gchar * group_id)
{
  if (!group_id)
    return FALSE;

  for (guint i = 0; i < gst_caps_get_size (caps); i++) {
    const GstStructure *s = gst_caps_get_structure (caps, i);
    const GValue *tensors_value = gst_structure_get_value (s, "tensors");
    const GstStructure *tensorgroups = tensors_value ?
        gst_value_get_structure (tensors_value) : NULL;

    if (tensorgroups && gst_structure_has_field (tensorgroups, group_id))
      return TRUE;
  }

  return FALSE;
}

static GstCaps *
gst_onnx_tensor_inference_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps)
{
  GstOnnxTensorInference *self = GST_ONNX_TENSOR_INFERENCE (trans);
  const gchar *group_id;
  GstCaps *ret = gst_caps_copy (caps);

  g_mutex_lock (&self->lock);
  group_id = g_quark_to_string (self->group_id);
  if (direction == GST_PAD_SRC) {
    /* Advertise only the tensors this model needs from upstream. */
    for (guint i = 0; i < gst_caps_get_size (ret); i++) {
      GstStructure *s = gst_caps_get_structure (ret, i);

      gst_structure_remove_field (s, "tensors");
      if (self->input_tensorgroups)
        gst_structure_set (s, "tensors", GST_TYPE_STRUCTURE,
            self->input_tensorgroups, NULL);
    }
  } else if (self->output_tensorgroups && group_id) {
    const GValue *group_value =
        gst_structure_get_value (self->output_tensorgroups, group_id);

    for (guint i = 0; i < gst_caps_get_size (ret); i++) {
      GstStructure *s = gst_caps_get_structure (ret, i);
      const GValue *tensors_value = gst_structure_get_value (s, "tensors");
      GstStructure *tensorgroups = tensors_value ?
          gst_structure_copy (gst_value_get_structure (tensors_value)) :
          gst_structure_new_empty ("tensorgroups");

      gst_structure_remove_field (tensorgroups, group_id);
      gst_structure_set_value (tensorgroups, group_id, group_value);
      gst_structure_set (s, "tensors", GST_TYPE_STRUCTURE, tensorgroups, NULL);
      gst_structure_free (tensorgroups);
    }
  }
  g_mutex_unlock (&self->lock);

  if (filter_caps) {
    GstCaps *tmp = gst_caps_intersect_full (ret, filter_caps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = tmp;
  }

  return ret;
}

static gboolean
gst_onnx_tensor_inference_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstOnnxTensorInference *self = GST_ONNX_TENSOR_INFERENCE (trans);
  const gchar *group_id;
  gboolean has_group;

  GST_DEBUG_OBJECT (trans, "Input caps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (trans, "Output caps: %" GST_PTR_FORMAT, outcaps);

  g_mutex_lock (&self->lock);
  group_id = g_quark_to_string (self->group_id);
  has_group = caps_has_tensor_group (incaps, group_id);
  g_mutex_unlock (&self->lock);

  if (!has_group) {
    GST_ERROR_OBJECT (self, "Input caps missing tensor group '%s'", group_id);
    return FALSE;
  }

  return TRUE;
}

static void
gst_onnx_tensor_inference_reset (GstOnnxBaseInference * base)
{
  GstOnnxTensorInference *self = GST_ONNX_TENSOR_INFERENCE (base);

  gst_clear_object (&self->importer);
  self->tensor_meta = NULL;
  self->tensor_list_meta = NULL;
  g_ptr_array_set_size (self->input_targets, 0);

  g_mutex_lock (&self->lock);
  self->group_id = 0;
  gst_clear_structure (&self->input_tensorgroups);
  gst_clear_structure (&self->output_tensorgroups);
  g_mutex_unlock (&self->lock);
}

static GstStructure *
create_input_tensorgroups (const GArray * model_inputs, GQuark group_id)
{
  GValue tensors = G_VALUE_INIT;
  GstStructure *ret;

  g_value_init (&tensors, GST_TYPE_UNIQUE_LIST);
  for (guint i = 0; i < model_inputs->len; i++) {
    const GstOnnxModelInput *input =
        &g_array_index (model_inputs, GstOnnxModelInput, i);
    GstStructure *desc;
    GstCaps *caps;
    GValue dims = G_VALUE_INIT;
    GValue dim = G_VALUE_INIT;
    GValue value = G_VALUE_INIT;

    desc = gst_structure_new ("tensor/strided",
        "tensor-id", G_TYPE_STRING, g_quark_to_string (input->id),
        "dims-order", G_TYPE_STRING,
        input->dims_order == GST_TENSOR_DIM_ORDER_COL_MAJOR ?
        "col-major" : "row-major",
        "type", G_TYPE_STRING,
        gst_tensor_data_type_get_name (input->data_type), NULL);
    gst_value_array_init (&dims, input->num_dims);
    g_value_init (&dim, G_TYPE_INT);

    for (gsize j = 0; j < input->num_dims; j++) {
      g_value_set_int (&dim, input->dims[j] < 0 ? 0 : input->dims[j]);
      gst_value_array_append_value (&dims, &dim);
    }

    g_value_unset (&dim);
    gst_structure_take_value (desc, "dims", &dims);

    caps = gst_caps_new_full (desc, NULL);
    g_value_init (&value, GST_TYPE_CAPS);
    gst_value_set_caps (&value, caps);
    gst_caps_unref (caps);
    gst_value_unique_list_append_and_take_value (&tensors, &value);
  }

  ret = gst_structure_new_empty ("tensorgroups");
  gst_structure_take_value (ret, g_quark_to_string (group_id), &tensors);

  return ret;
}

static gboolean
gst_onnx_tensor_inference_process_model_info (GstOnnxBaseInference * base,
    GstAnalyticsModelInfo * modelinfo, const GstOnnxModelInfo * parsed_info,
    GstOnnxImporter * importer)
{
  GstOnnxTensorInference *self = GST_ONNX_TENSOR_INFERENCE (base);

  g_mutex_lock (&self->lock);
  gst_clear_object (&self->importer);
  self->importer = gst_object_ref (importer);
  self->group_id = parsed_info->group_id;

  gst_clear_structure (&self->input_tensorgroups);
  gst_clear_structure (&self->output_tensorgroups);
  self->input_tensorgroups = create_input_tensorgroups (parsed_info->inputs,
      parsed_info->group_id);
  self->output_tensorgroups =
      gst_structure_copy (parsed_info->output_tensorgroups);
  g_mutex_unlock (&self->lock);

  return TRUE;
}

static GstTensorListMeta *
gst_onnx_tensor_inference_find_list_meta (GstOnnxTensorInference * self,
    GstBuffer * buffer, const GArray * model_inputs)
{
  gpointer state = NULL;
  GstMeta *meta;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    GstTensorListMeta *tmeta;

    if (meta->info->api != GST_TENSOR_LIST_META_API_TYPE)
      continue;

    tmeta = (GstTensorListMeta *) meta;
    if (tmeta->group_id != self->group_id)
      continue;

    /* Assume this is valid, actual validation would need to be in
     * prepare_input_tensors stage */
    return tmeta;
  }

  return NULL;
}

static gboolean
gst_onnx_tensor_inference_match_tensor (GstOnnxTensorInference * self,
    const GstTensor * tensor, const GstOnnxModelInput * input, guint i, gsize j)
{
  GST_DEBUG_OBJECT (self,
      "Checking tensor %" G_GSIZE_FORMAT " for input %u: "
      "id=%s (expected %s), type=%s (expected %s), "
      "dims-order=%d (expected %d), "
      "num-dims=%" G_GSIZE_FORMAT " (expected %" G_GSIZE_FORMAT ")", j, i,
      g_quark_to_string (tensor->id),
      g_quark_to_string (input->id),
      gst_tensor_data_type_get_name (tensor->data_type),
      gst_tensor_data_type_get_name (input->data_type),
      tensor->dims_order, input->dims_order, tensor->num_dims, input->num_dims);

  if (tensor->id != input->id ||
      tensor->data_type != input->data_type ||
      tensor->dims_order != input->dims_order ||
      tensor->num_dims != input->num_dims)
    return FALSE;

  for (gsize k = 0; k < input->num_dims; k++) {
    GST_DEBUG_OBJECT (self,
        "Input %u dim[%" G_GSIZE_FORMAT "]: "
        "tensor=%" G_GSIZE_FORMAT ", expected=%" G_GINT64_FORMAT,
        i, k, tensor->dims[k], input->dims[k]);

    if (input->dims[k] > 0 && input->dims[k] != (gint64) tensor->dims[k]) {
      return FALSE;
    }
  }

  return TRUE;
}

static GstTensorMeta *
gst_onnx_tensor_inference_find_meta (GstOnnxTensorInference * self,
    GstBuffer * buffer, const GArray * model_inputs)
{
  gpointer state = NULL;
  GstMeta *meta;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    GstTensorMeta *tmeta;
    gboolean matched = TRUE;

    if (meta->info->api != GST_TENSOR_META_API_TYPE)
      continue;

    tmeta = (GstTensorMeta *) meta;
    /* Accept only meta that has the same number of our input requirement */
    if (tmeta->num_tensors != model_inputs->len)
      continue;

    for (guint i = 0; i < model_inputs->len; i++) {
      const GstOnnxModelInput *input = &g_array_index (model_inputs,
          GstOnnxModelInput, i);
      GstTensor *target = NULL;

      for (gsize j = 0; j < tmeta->num_tensors; j++) {
        GstTensor *tensor = tmeta->tensors[j];
        gboolean tensor_match = gst_onnx_tensor_inference_match_tensor (self,
            tensor, input, i, j);

        if (tensor_match) {
          target = tensor;
          break;
        }
      }

      if (!target) {
        matched = FALSE;
        break;
      }

      g_ptr_array_index (self->input_targets, i) = target;
    }

    if (matched)
      return tmeta;
  }

  return NULL;
}

static GstFlowReturn
gst_onnx_tensor_inference_start_inference (GstOnnxBaseInference * base,
    GstBuffer * buffer, const GArray * model_inputs, gsize * num_iterations)
{
  GstOnnxTensorInference *self = GST_ONNX_TENSOR_INFERENCE (base);

  self->tensor_meta = NULL;
  self->tensor_list_meta = NULL;
  *num_iterations = 0;

  g_ptr_array_set_size (self->input_targets, model_inputs->len);

  self->tensor_list_meta =
      gst_onnx_tensor_inference_find_list_meta (self, buffer, model_inputs);
  if (self->tensor_list_meta) {
    *num_iterations = self->tensor_list_meta->num_items;

    GST_DEBUG_OBJECT (self, "Found TensorListMeta, num-items: %" G_GSIZE_FORMAT,
        self->tensor_list_meta->num_items);

    return GST_FLOW_OK;
  }

  self->tensor_meta =
      gst_onnx_tensor_inference_find_meta (self, buffer, model_inputs);
  if (self->tensor_meta) {
    *num_iterations = 1;
  } else {
    GST_DEBUG_OBJECT (self,
        "No matching tensor meta found, skipping inference");
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_onnx_tensor_inference_prepare_input_tensors (GstOnnxBaseInference * base,
    GstBuffer * buffer, const GArray * model_inputs, gsize iteration,
    GPtrArray * input_tensors)
{
  GstOnnxTensorInference *self = GST_ONNX_TENSOR_INFERENCE (base);

  if (!self->importer) {
    GST_ERROR_OBJECT (self, "Importer not prepared");
    return GST_FLOW_ERROR;
  }

  if (self->tensor_list_meta) {
    for (guint i = 0; i < model_inputs->len; i++) {
      const GstOnnxModelInput *input = &g_array_index (model_inputs,
          GstOnnxModelInput, i);
      gsize dims[GST_ONNX_MAX_DIMS];
      const GstTensor *tensor;

      for (gsize j = 0; j < input->num_dims; j++)
        dims[j] = input->dims[j] > 0 ? (gsize) input->dims[j] : G_MAXSIZE;

      tensor = gst_tensor_list_meta_get_typed_tensor (self->tensor_list_meta,
          iteration, input->id, input->data_type, input->dims_order,
          input->num_dims, dims);

      if (!tensor) {
        GST_ERROR_OBJECT (self, "No matching tensor for input %u in item %"
            G_GSIZE_FORMAT, i, iteration);
        return GST_FLOW_ERROR;
      }

      g_ptr_array_index (self->input_targets, i) = (gpointer) tensor;
    }
  }

  for (guint i = 0; i < model_inputs->len; i++) {
    const GstOnnxModelInput *input = &g_array_index (model_inputs,
        GstOnnxModelInput, i);
    GstTensor *target = g_ptr_array_index (self->input_targets, i);
    OrtValue *value = NULL;
    gint64 dims[GST_ONNX_MAX_DIMS];

    for (gsize j = 0; j < target->num_dims; j++)
      dims[j] = (gint64) target->dims[j];

    value = gst_onnx_importer_prepare_wrapped (self->importer, target->data,
        input->data_type_onnx, dims, target->num_dims);
    if (!value) {
      GST_ERROR_OBJECT (self, "Couldn't create tensor");
      return GST_FLOW_ERROR;
    }

    g_ptr_array_add (input_tensors, value);
  }

  return GST_FLOW_OK;
}

static void
gst_onnx_tensor_inference_unprepare_input_tensors (GstOnnxBaseInference * base,
    GstBuffer * buffer, GPtrArray * input_tensors)
{
  GstOnnxTensorInference *self = GST_ONNX_TENSOR_INFERENCE (base);

  if (self->importer)
    gst_onnx_importer_unprepare (self->importer);
}

static GstFlowReturn
gst_onnx_tensor_inference_end_inference (GstOnnxBaseInference * base,
    GstBuffer * buffer, GPtrArray * results, gsize num_tensors,
    gboolean attach_meta)
{
  GstOnnxTensorInference *self = GST_ONNX_TENSOR_INFERENCE (base);
  GstFlowReturn ret = GST_FLOW_OK;

  if (attach_meta && self->tensor_list_meta) {
    GstTensorListMeta *output = NULL;

    if (results->len != self->tensor_list_meta->num_items) {
      GST_ERROR_OBJECT (self,
          "Inference result count does not match input list");
      ret = GST_FLOW_ERROR;
      goto done;
    }

    output = gst_buffer_add_tensor_list_meta (buffer, self->group_id);
    if (!output) {
      ret = GST_FLOW_ERROR;
      goto done;
    }

    for (guint i = 0; i < results->len; i++) {
      gboolean appended = gst_tensor_list_meta_append_item_from (output,
          num_tensors, g_ptr_array_index (results, i), self->tensor_list_meta,
          i);

      if (!appended) {
        gst_buffer_remove_meta (buffer, (GstMeta *) output);
        GST_ERROR_OBJECT (self, "Could not append inference output item");
        ret = GST_FLOW_ERROR;
        goto done;
      }

      g_ptr_array_index (results, i) = NULL;
    }
  } else if (attach_meta && self->tensor_meta && results->len > 0) {
    ret =
        GST_ONNX_BASE_INFERENCE_CLASS (parent_class)->end_inference (base,
        buffer, results, num_tensors, TRUE);
  }

done:

  if (self->remove_tensor_meta) {
    if (self->tensor_list_meta)
      gst_buffer_remove_meta (buffer, (GstMeta *) self->tensor_list_meta);
    if (self->tensor_meta)
      gst_buffer_remove_meta (buffer, (GstMeta *) self->tensor_meta);
  }

  self->tensor_meta = NULL;
  self->tensor_list_meta = NULL;

  g_ptr_array_set_size (self->input_targets, 0);

  return ret;
}
