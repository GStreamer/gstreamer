/*
 * GStreamer gstreamer-yolotensordecoder
 * Copyright (C) 2024 Collabora Ltd.
 *  Authors: Daniel Morin <daniel.morin@collabora.com>
 *           Vineet Suryan <vineet.suryan@collabora.com>
 *           Santosh Mahto <santosh.mahto@collabora.com>
 *
 * gstyolotensordecoder.c
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
 * SECTION:element-yolotensordec
 * @short_description: Decode tensors from YOLO detection models
 *
 * This element can parse per-buffer inference tensors meta data generated
 * by an upstream inference element
 *
 *
 * ## Example launch command:
 *
 * Test image file, model file and labels file can be found here :
 * https://gitlab.collabora.com/gstreamer/onnx-models
 *
 *  gst-launch-1.0 -v v4l2src \
 *    ! videoconvertscale qos=false ! video/x-raw, pixel-aspect-ratio=1/1 \
 *    ! onnxinference model-file=yolov8s.onnx \
 *    ! yolov8tensordec class-confidence-threshold=0.8 iou-threshold=0.3 \
 *      max-detections=100 label-file=labels/COCO_classes.txt \
 *    ! objectdetectionoverlay ! glimagesink sink=gtkglsink
 *
 *  The original repository of the Yolo is located at
 *  https://github.com/ultralytics/ultralytics.
 *  For easy experimentation, the models based on Yolo architecture in Onnx
 *  format can be found at https://col.la/gstonnxmodels . This model already
 *  has tensors name embedded matching default values of tensors-detections-name
 *  and tensors-logits-name properties. It's also possible to embed tensor-ids
 *  into any model based on Yolo architecture to allow this tensor-decoder
 *  to decode tensors.This process is described in the Readme of
 *  repository: https://col.la/gstonnxmodels"
 *
 * Since: 1.28
 */

#ifdef HAVE_CONFI_H
#include "config.h"
#endif

#include "gstyolotensordecoder.h"

#include <gst/analytics/analytics.h>
#include <gst/analytics/gstanalytics_image_util.h>
#include <gio/gio.h>

#include <math.h>

#define YOLO_DETECTION_MASK "yolo-v8-out"
GQuark YOLO_DETECTION_MASK_ID;

/**
 * GstYoloTensorDecoder:
 *
 * A tensor decoder for YOLO v8-v11 models.
 *
 * Since: 1.28
 */

GST_DEBUG_CATEGORY_STATIC (yolo_tensor_decoder_debug);
#define GST_CAT_DEFAULT yolo_tensor_decoder_debug

GST_ELEMENT_REGISTER_DEFINE (yolo_tensor_decoder, "yolov8tensordec",
    GST_RANK_PRIMARY, GST_TYPE_YOLO_TENSOR_DECODER);

/* GstYoloTensorDecoder properties, see properties description in
 * gst_yolo_tensor_decoder_class_init for more details. */
enum
{
  PROP_0,
  PROP_BOX_CONFI_THRESH,
  PROP_CLS_CONFI_THRESH,
  PROP_IOU_THRESH,
  PROP_MAX_DETECTION,
  PROP_LABEL_FILE
};

/* Specify the range of confidence level in tensor output*/
typedef struct _ConfidenceRange
{
  gsize start;                  /* Start index of confidence level */
  gsize end;                    /* End index of confidence level */
  gsize step;                   /* Step size of next confidence level index */
} ConfidenceRange;

/* Default properties value */
static const gfloat DEFAULT_BOX_CONFI_THRESH = 0.4f;
static const gfloat DEFAULT_CLS_CONFI_THRESH = 0.4f;
static const gfloat DEFAULT_IOU_THRESH = 0.7f;
static const gsize DEFAULT_MAX_DETECTION = 100;

/* Global variable storing class for OD. Generally OD has class
 * and we need to provide one but this class is just a placeholder.*/
GQuark OOI_CLASS_ID;

/* GStreamer element srcpad template. Template of a srcpad that can receive
 * any raw video. */
static GstStaticPadTemplate gst_yolo_tensor_decoder_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw"));

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_yolo_tensor_decoder_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      "video/x-raw,"
        "tensors=(structure)["
          "tensorgroups,"
            "yolo-v8-out=(/set){"
            "(GstCaps)["
              "tensor/strided,"
                "tensor-id=(string)yolo-v8-out,"
                "dims=<(int)1,(int)[1,max],(int)[1,max]>,"
                "dims-order=(string)col-major,"
                "type=(string)float32"
             "]"
           "}"
        "]"
      ));
/* *INDENT-ON* */

/* GstYoloTensorDecoder Prototypes */
static gboolean gst_yolo_tensor_decoder_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static void gst_yolo_tensor_decoder_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_yolo_tensor_decoder_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_yolo_tensor_decoder_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);
static void gst_yolo_tensor_decoder_finalize (GObject * object);

static void gst_yolo_tensor_decoder_object_found (GstYoloTensorDecoder * self,
    GstAnalyticsRelationMeta * rmeta, BBox * bb, gfloat confidence,
    GQuark class_quark, const gfloat * candidate_masks, gsize offset,
    guint count);

G_DEFINE_TYPE (GstYoloTensorDecoder, gst_yolo_tensor_decoder,
    GST_TYPE_BASE_TRANSFORM);

static GArray *
read_labels (const char *labels_file)
{
  GArray *array;
  GFile *file = g_file_new_for_path (labels_file);
  GFileInputStream *file_stream;
  GDataInputStream *data_stream;
  GError *error = NULL;
  gchar *line;

  file_stream = g_file_read (file, NULL, &error);
  g_object_unref (file);
  if (!file_stream) {
    GST_WARNING ("Could not open file %s: %s\n", labels_file, error->message);
    g_clear_error (&error);
    return NULL;
  }

  data_stream = g_data_input_stream_new (G_INPUT_STREAM (file_stream));
  g_object_unref (file_stream);

  array = g_array_new (FALSE, FALSE, sizeof (GQuark));

  while ((line = g_data_input_stream_read_line (data_stream, NULL, NULL,
              &error))) {
    GQuark label = g_quark_from_string (line);
    g_array_append_val (array, label);
    g_free (line);
  }

  g_object_unref (data_stream);

  if (error) {
    GST_WARNING ("Could not open file %s: %s", labels_file, error->message);
    g_array_free (array, TRUE);
    g_clear_error (&error);
    return NULL;
  }

  if (array->len == 0) {
    g_array_free (array, TRUE);
    return NULL;
  }

  return array;
}

static gboolean
gst_yolo_tensor_decoder_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstYoloTensorDecoder *self = GST_YOLO_TENSOR_DECODER (trans);

  if (!gst_video_info_from_caps (&self->video_info, incaps)) {
    GST_ERROR_OBJECT (self, "Failed to parse caps");
    return FALSE;
  }

  if (gst_base_transform_is_passthrough (trans)) {
    GST_ERROR_OBJECT (self, "Failed. Can't handle passthrough");
    return FALSE;
  }

  return TRUE;
}

static void
gst_yolo_tensor_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstYoloTensorDecoder *self = GST_YOLO_TENSOR_DECODER (object);
  const gchar *filename;

  switch (prop_id) {
    case PROP_BOX_CONFI_THRESH:
      self->box_confi_thresh = g_value_get_float (value);
      break;
    case PROP_CLS_CONFI_THRESH:
      self->cls_confi_thresh = g_value_get_float (value);
      break;
    case PROP_IOU_THRESH:
      self->iou_thresh = g_value_get_float (value);
      break;
    case PROP_MAX_DETECTION:
      self->max_detection = g_value_get_uint (value);
      break;
    case PROP_LABEL_FILE:
    {
      GArray *labels;

      filename = g_value_get_string (value);
      labels = read_labels (filename);

      if (labels) {
        g_free (self->label_file);
        self->label_file = g_strdup (filename);
        g_clear_pointer (&self->labels, g_array_unref);
        self->labels = labels;
      } else {
        GST_WARNING_OBJECT (self, "Label file '%s' not found!", filename);
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_yolo_tensor_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstYoloTensorDecoder *self = GST_YOLO_TENSOR_DECODER (object);

  switch (prop_id) {
    case PROP_BOX_CONFI_THRESH:
      g_value_set_float (value, self->box_confi_thresh);
      break;
    case PROP_CLS_CONFI_THRESH:
      g_value_set_float (value, self->cls_confi_thresh);
      break;
    case PROP_IOU_THRESH:
      g_value_set_float (value, self->iou_thresh);
      break;
    case PROP_MAX_DETECTION:
      g_value_set_uint (value, self->max_detection);
      break;
    case PROP_LABEL_FILE:
      g_value_set_string (value, self->label_file);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_yolo_tensor_decoder_class_init (GstYoloTensorDecoderClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  /* Define GstYoloTensorDecoder debug category. */
  GST_DEBUG_CATEGORY_INIT (yolo_tensor_decoder_debug,
      "yolov8tensordec", 0, "Tensor decoder for Yolo detection models");

  YOLO_DETECTION_MASK_ID = g_quark_from_static_string (YOLO_DETECTION_MASK);

  /* Set GObject vmethod to get and set property */
  gobject_class->set_property = gst_yolo_tensor_decoder_set_property;
  gobject_class->get_property = gst_yolo_tensor_decoder_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_BOX_CONFI_THRESH,
      g_param_spec_float ("box-confidence-threshold",
          "Box location confidence threshold",
          "Boxes with a location confidence level inferior to this threshold "
          "will be excluded",
          0.0, 1.0, DEFAULT_BOX_CONFI_THRESH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_CLS_CONFI_THRESH,
      g_param_spec_float ("class-confidence-threshold",
          "Class confidence threshold",
          "Classes with a confidence level inferior to this threshold "
          "will be excluded",
          0.0, 1.0, DEFAULT_CLS_CONFI_THRESH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_IOU_THRESH,
      g_param_spec_float ("iou-threshold",
          "Maximum IOU threshold",
          "Maximum intersection-over-union between bounding boxes to "
          "consider them distinct.",
          0.0, 1.0, DEFAULT_IOU_THRESH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_MAX_DETECTION,
      g_param_spec_uint ("max-detections",
          "Maximum object/masks detections.",
          "Maximum object/masks detections.",
          1, G_MAXUINT, DEFAULT_MAX_DETECTION,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LABEL_FILE,
      g_param_spec_string ("label-file",
          "Label file", "Label file", NULL, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));


  /* Element description. */
  gst_element_class_set_static_metadata (element_class,
      "YOLO v8-11 object detection tensor decoder", "Tensordecoder/Video",
      "Decode tensors output from the inference of YOLO Object Detection or FastSAM model (Detection)"
      "on video frames. This works on YOLO version 8 and later(v11), and FastSAM models.",
      "Daniel Morin <daniel.morin@collabora.com>, Santosh Mahto <santosh.mahto@collabora.com>");

  /* Add pads to element base on pad template defined earlier */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_yolo_tensor_decoder_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_yolo_tensor_decoder_sink_template));

  /* Set GstBaseTransform vmethod transform_ip. This methode is called
   * by the srcpad when it receive buffer. ip stand for in-place meaning the
   * buffer remain unchanged by the element. Tensor-decoder only monitor
   * buffer it receive for a meta attach to the buffer that is a GstTensorMeta
   * and has a tensor-id can be handled by GstYoloTensorDecoder. */
  basetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_yolo_tensor_decoder_transform_ip);

  /* Set GstBaseTransform set_caps vmethod. This will be called once the
   * capability negotiation has been completed. We will be able to extract
   * resolution from this callback. */
  basetransform_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_yolo_tensor_decoder_set_caps);

  gobject_class->finalize = gst_yolo_tensor_decoder_finalize;

  klass->object_found = gst_yolo_tensor_decoder_object_found;

  /* Calculate the class id placeholder (also a quark) that will be set
   * as label if object if labels are not provided via label-file. */
  OOI_CLASS_ID = g_quark_from_static_string ("Yolo-None");
}

struct Candidate
{
  const float *candidate;
  const float max_confidence;
  const guint max_class_offset;
};

static void
gst_yolo_tensor_decoder_init (GstYoloTensorDecoder * self)
{
  /* GstYoloTensorDecoder instance initialization */
  self->box_confi_thresh = DEFAULT_BOX_CONFI_THRESH;
  self->cls_confi_thresh = DEFAULT_CLS_CONFI_THRESH;
  self->iou_thresh = DEFAULT_IOU_THRESH;
  self->max_detection = DEFAULT_MAX_DETECTION;

  self->sel_candidates = g_array_new (FALSE, FALSE, sizeof (struct Candidate));
  self->selected = g_ptr_array_new ();

  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), FALSE);
}

static const GstTensor *
gst_yolo_tensor_decoder_get_tensor (GstYoloTensorDecoder *
    self, GstBuffer * buf)
{
  GstMeta *meta = NULL;
  gpointer iter_state = NULL;

  if (!gst_buffer_get_meta (buf, GST_TENSOR_META_API_TYPE)) {
    GST_DEBUG_OBJECT (self,
        "missing tensor meta from buffer %" GST_PTR_FORMAT, buf);
    return NULL;
  }

  while ((meta = gst_buffer_iterate_meta_filtered (buf, &iter_state,
              GST_TENSOR_META_API_TYPE))) {
    GstTensorMeta *tensor_meta = (GstTensorMeta *) meta;
    const GstTensor *tensor;
    const gsize YOLO_DETECTIONS_TENSOR_N_DIMS = 3;
    static const gsize dims[3] = { 1, G_MAXSIZE, G_MAXSIZE };

    tensor = gst_tensor_meta_get_typed_tensor (tensor_meta,
        YOLO_DETECTION_MASK_ID, GST_TENSOR_DATA_TYPE_FLOAT32,
        GST_TENSOR_DIM_ORDER_ROW_MAJOR, YOLO_DETECTIONS_TENSOR_N_DIMS, dims);

    if (tensor) {
      if (tensor->dims[1] < 5) {
        GST_WARNING_OBJECT (self, "Ignore tensor because dims[1] is %zu < 5",
            tensor->dims[1]);
        continue;
      }

      return tensor;
    }
  }

  return NULL;
}


static GstFlowReturn
gst_yolo_tensor_decoder_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstYoloTensorDecoder *self = GST_YOLO_TENSOR_DECODER (trans);
  GstAnalyticsRelationMeta *rmeta;
  const GstTensor *detections_tensor;

  detections_tensor = gst_yolo_tensor_decoder_get_tensor (self, buf);
  if (detections_tensor == NULL) {
    GST_WARNING_OBJECT (self, "Couldn't find mask tensor, skipping");
    return GST_FLOW_OK;
  }

  /* Retrieve or attach an analytics-relation-meta to the buffer.
   * Analytics-relation-meta are container that can reveive multiple
   * analytics-meta, like OD and Segmentation. The following call will only
   * retrieve an analytics-relation-meta if it exist or create one if it
   * does not exist. */
  rmeta = gst_buffer_add_analytics_relation_meta (buf);
  if (rmeta == NULL) {
    GST_ELEMENT_ERROR (trans, STREAM, FAILED, (NULL),
        ("Analytics Relation meta allocation failed"));
    return GST_FLOW_ERROR;
  }

  if (!gst_yolo_tensor_decoder_decode_f32 (self, rmeta, detections_tensor, 0))
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static void
gst_yolo_tensor_decoder_finalize (GObject * object)
{
  GstYoloTensorDecoder *self = GST_YOLO_TENSOR_DECODER (object);

  g_clear_pointer (&self->sel_candidates, g_array_unref);
  g_clear_pointer (&self->selected, g_ptr_array_unref);

  g_free (self->label_file);
  g_clear_pointer (&self->labels, g_array_unref);

  G_OBJECT_CLASS (gst_yolo_tensor_decoder_parent_class)->finalize (object);
}

/* Extract bounding box from tensor data */
static void
gst_yolo_tensor_decoder_convert_bbox (const gfloat * candidate,
    const gsize * offset, BBox * bbox)
{
  gfloat w = *(candidate + offset[2]);
  gfloat h = *(candidate + offset[3]);
  bbox->x = *(candidate + offset[0]) - (w / 2);
  bbox->y = *(candidate + offset[1]) - (h / 2);
  bbox->w = w + 0.5;
  bbox->h = h + 0.5;
}

/* Calculate iou between boundingbox of candidate c1 and c2
 */
static gfloat
gst_yolo_tensor_decoder_iou (const gfloat * c1, const gfloat * c2,
    const gsize * offset, BBox * bb1, BBox * bb2)
{
  gst_yolo_tensor_decoder_convert_bbox (c1, offset, bb1);
  gst_yolo_tensor_decoder_convert_bbox (c2, offset, bb2);
  return gst_analytics_image_util_iou_int (bb1->x, bb1->y, bb1->w, bb1->h,
      bb2->x, bb2->y, bb2->w, bb2->h);
}

/* Utility function to find maxmum confidence value across classes
 * specified by range.
 */
static gfloat
gst_yolo_tensor_decoder_find_max_class_confidence (const gfloat * c,
    const ConfidenceRange * c_range, gsize * max_class_ofs)
{
  gfloat max_val = 0.0;
  for (gsize i = c_range->start; i <= c_range->end; i += c_range->step) {
    if (*(c + i) > max_val) {
      max_val = *(c + i);
      *max_class_ofs = i;
    }
  }
  return max_val;
}

/* Compare c1 and c2
 * Utility function for sorting candiates based on the a field identified
 * by offset.
 */
static gint
gst_yolo_tensor_decoder_sort_candidates (gconstpointer p1, gconstpointer p2)
{
  const struct Candidate *c1 = p1;
  const struct Candidate *c2 = p2;

  if (c1->max_confidence < c2->max_confidence)
    return 1;
  else if (c1->max_confidence > c2->max_confidence)
    return -1;
  else
    return 0;
}

static gboolean
gst_yolo_tensor_decoder_decode_valid_bb (GstYoloTensorDecoder * self,
    gfloat x, gfloat y, gfloat w, gfloat h)
{
  GstYoloTensorDecoder *parent = GST_YOLO_TENSOR_DECODER (self);

  if (x > (GST_VIDEO_INFO_WIDTH (&parent->video_info)))
    return FALSE;
  if (y > (GST_VIDEO_INFO_HEIGHT (&parent->video_info)))
    return FALSE;
  if (x < -(gfloat) (GST_VIDEO_INFO_WIDTH (&parent->video_info) / 2.0))
    return FALSE;
  if (y < -(gfloat) (GST_VIDEO_INFO_HEIGHT (&parent->video_info) / 2.0))
    return FALSE;
  if (w <= 0)
    return FALSE;
  if (h <= 0)
    return FALSE;
  if (w > (GST_VIDEO_INFO_WIDTH (&parent->video_info)))
    return FALSE;
  if (h > (GST_VIDEO_INFO_HEIGHT (&parent->video_info)))
    return FALSE;

  return TRUE;
}

gboolean
gst_yolo_tensor_decoder_decode_f32 (GstYoloTensorDecoder * self,
    GstAnalyticsRelationMeta * rmeta, const GstTensor * detections_tensor,
    guint num_masks)
{
  GstMapInfo map_info_detections;
  gfloat iou;
  gboolean rv, keep;
  gsize offset, x_offset, y_offset, w_offset, h_offset, offsets[4];
  BBox bb1, bb2;
  ConfidenceRange c_range;
  gsize max_class_offset = 0, class_index;
  GQuark class_quark = OOI_CLASS_ID;
  gboolean ret = TRUE;
  gsize i;

  /* Retrieve memory at index 0 and map it in READWRITE mode */
  rv = gst_buffer_map (detections_tensor->data, &map_info_detections,
      GST_MAP_READ);
  if (rv == FALSE) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
        ("Could not map tensor buffer %" GST_PTR_FORMAT,
            detections_tensor->data));
    return FALSE;
  }

  GST_LOG_OBJECT (self, "Mask Tensor shape dims %zu",
      detections_tensor->num_dims);

  /* Trace detections tensor dimensions */
  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_TRACE) {
    for (gsize i = 0; i < detections_tensor->num_dims; i++) {
      GST_TRACE_OBJECT (self, "Detections Tensor dim %zu: %zu", i,
          detections_tensor->dims[i]);
    }
  }

  /* Number of candidates can be large, reset the arrays */
  g_array_set_size (self->sel_candidates, 0);
  g_ptr_array_set_size (self->selected, 0);

  /* detections_tensor->dims[2] contain the number of candidates. Let's call the
   * number of candidates C. We store this value in offset as we use it
   * calculate the offset of candidate fields. The variable #data_detections above point
   * at the detections tensor data, but candidates data is organize like a plane.
   * Candidates bbox X coord fields from 0 to C start at the begining of the
   * tensor data and are continguous in memory, followed by all candidates
   * field Y, followed by field W, ... followed by field class confidence level,
   * ..., followed by all candidates mask0, ..., followed by all candidates
   * mask31. Bellow we pre-calculate each field offset relative to the
   * candidate pointer (pointer to field X), which will allow us to easily
   * access each candiates field.
   * */
  offset = detections_tensor->dims[2];
  x_offset = 0;
  y_offset = offset;
  w_offset = 2 * offset;
  h_offset = 3 * offset;
  /* first index that contain confidence level */
  c_range.start = 4 * offset;
  /* Last index that contains confidence level */
  c_range.end = (detections_tensor->dims[1] - num_masks - 1) * offset;
  /* Step between class confidence level */
  c_range.step = offset;
  offsets[0] = x_offset;
  offsets[1] = y_offset;
  offsets[2] = w_offset;
  offsets[3] = h_offset;

#define BB_X(candidate) candidate[x_offset]
#define BB_Y(candidate) candidate[y_offset]
#define BB_W(candidate) candidate[w_offset]
#define BB_H(candidate) candidate[h_offset]

  for (gsize c_idx = 0; c_idx < detections_tensor->dims[2]; c_idx++) {
    float *candidate = (float *) map_info_detections.data;

    candidate += c_idx;

    /* Yolo have multiple class, so maximum confidence level across all class is used
     * to evaluate the relevance of the candidate. Here we filter candidates
     * based on their class confidence level.*/
    gfloat max_confidence =
        gst_yolo_tensor_decoder_find_max_class_confidence (candidate, &c_range,
        &max_class_offset);
    if (max_confidence > self->cls_confi_thresh
        && gst_yolo_tensor_decoder_decode_valid_bb (self,
            BB_X (candidate), BB_Y (candidate), BB_W (candidate),
            BB_H (candidate))) {

      struct Candidate c = {
        candidate,
        max_confidence,
        max_class_offset,
      };
      g_array_append_val (self->sel_candidates, c);

      GST_TRACE_OBJECT (self, "%zu: x,y=(%f;%f) w,h=(%f;%f), s=%f c=%f",
          c_idx, candidate[x_offset], candidate[y_offset],
          candidate[w_offset], candidate[h_offset],
          candidate[w_offset] * candidate[h_offset], max_confidence);
    }

    /* Pointer arithmetic, going to the next candidate. This is the candidate
     * pointer that is now incremented to the next candidate which is also
     * the field X of the next candidate.*/
    candidate += 1;
  }

  GST_LOG_OBJECT (self, "Before NMS selected candidates count: %u",
      self->sel_candidates->len);

  /* We sort the remaining candidates because, in the next selection phase we
   * have a maximum and we want to make sure that considered only the candidates
   * with the highest class confidence level before potentially reaching the
   * maximum.*/
  g_array_sort (self->sel_candidates, gst_yolo_tensor_decoder_sort_candidates);

  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_TRACE) {
    for (i = 0; i < self->sel_candidates->len; i++) {
      struct Candidate *c = &g_array_index (self->sel_candidates,
          struct Candidate, i);
      GST_TRACE_OBJECT (self,
          "Sorted: %zu: x,y=(%f;%f) w,h=(%f;%f), s=%f c=%f",
          i,
          c->candidate[x_offset],
          c->candidate[y_offset],
          c->candidate[w_offset],
          c->candidate[h_offset],
          c->candidate[w_offset] * c->candidate[h_offset], c->max_confidence);
    }
  }

  /* Algorithm in part inspired by OpenCV NMSBoxes */
  for (i = 0; i < self->sel_candidates->len; i++) {
    const struct Candidate *c = &g_array_index (self->sel_candidates,
        struct Candidate, i);
    keep = TRUE;

    /* We only want to a NMS using IoU between candidates we've decided to
     * keep and the new one we considering to keep. selected array contain
     * the candidates we decided to keep and candidates[c] is the candidate
     * we're considering to keep or reject */
    for (gsize s = 0; s < self->selected->len && keep; s++) {
      const float *candidate2 = g_ptr_array_index (self->selected, s);
      iou = gst_yolo_tensor_decoder_iou (c->candidate, candidate2,
          offsets, &bb1, &bb2);
      keep = (iou <= self->iou_thresh);
    }

    if (keep) {
      if (self->selected->len == 0) {
        /* The first bounding-box always get in as there's no others bbox
         * to filter on based on IoU */
        gst_yolo_tensor_decoder_convert_bbox (c->candidate, offsets, &bb1);
      }

      g_ptr_array_add (self->selected, (gpointer) c->candidate);

      if (self->labels) {
        class_index = (c->max_class_offset - c_range.start) / c_range.step;

        if (class_index < self->labels->len)
          class_quark = g_array_index (self->labels, GQuark, class_index);
      }

      const gfloat *candidate_masks = NULL;
      if (num_masks) {
        /* detections weight will be stored in last `num_masks`
         * row of detections_tensor, so mask offset
         * will start at the end of the detections_tensor minus
         * `num_masks`
         */
        candidate_masks = c->candidate +
            ((detections_tensor->dims[1] - num_masks) * offset);

        if (candidate_masks + num_masks + offset >
            (gfloat *) (map_info_detections.data + map_info_detections.size)) {
          GST_ELEMENT_ERROR (self, STREAM, FAILED, (NULL),
              ("Mask tensor data size %zu is smaller than expected (%zu)",
                  (candidate_masks - (gfloat *) map_info_detections.data) +
                  offset + num_masks, map_info_detections.size));
          ret = FALSE;
          break;
        }
      }

      GST_YOLO_TENSOR_DECODER_GET_CLASS (self)->object_found (self, rmeta, &bb1,
          c->max_confidence, class_quark, candidate_masks, offset,
          self->selected->len);

      /* If the maximum number of candidate selected is reached exit the
       * selection process. */
      if (self->selected->len >= self->max_detection) {
        break;
      }
    }
  }

  GST_LOG_OBJECT (self, "After NMS selected count: %u", self->selected->len);

  /* We unmap the memory */
  gst_buffer_unmap (detections_tensor->data, &map_info_detections);

  return ret;
}

static void
gst_yolo_tensor_decoder_object_found (GstYoloTensorDecoder * self,
    GstAnalyticsRelationMeta * rmeta, BBox * bb, gfloat confidence,
    GQuark class_quark, const gfloat * candidate_masks, gsize offset,
    guint count)
{
  gst_analytics_relation_meta_add_od_mtd (rmeta, class_quark, bb->x, bb->y,
      bb->w, bb->h, confidence, NULL);
}
