/*
 * GStreamer gstreamer-onnxobjectdetector
 * Copyright (C) 2021 Collabora Ltd.
 *
 * gstonnxobjectdetector.c
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
 * SECTION:element-onnxobjectdetector
 * @short_description: Detect objects in video frame
 *
 * This element can apply a generic ONNX object detection model such as YOLO or SSD
 * to each video frame.
 *
 * To install ONNX on your system, recursively clone this repository
 * https://github.com/microsoft/onnxruntime.git
 *
 * and build and install with cmake:
 *
 * CPU:
 *
 *  cmake -Donnxruntime_BUILD_SHARED_LIB:ON -DBUILD_TESTING:OFF \
 *  $SRC_DIR/onnxruntime/cmake && make -j8 && sudo make install
 *
 *
 * GPU :
 *
 * cmake -Donnxruntime_BUILD_SHARED_LIB:ON -DBUILD_TESTING:OFF -Donnxruntime_USE_CUDA:ON \
 * -Donnxruntime_CUDA_HOME=$CUDA_PATH -Donnxruntime_CUDNN_HOME=$CUDA_PATH \
 *  $SRC_DIR/onnxruntime/cmake && make -j8 && sudo make install
 *
 *
 * where :
 *
 * 1. $SRC_DIR and $BUILD_DIR are local source and build directories
 * 2. To run with CUDA, both CUDA and cuDNN libraries must be installed.
 *    $CUDA_PATH is an environment variable set to the CUDA root path.
 *    On Linux, it would be /usr/local/cuda-XX.X where XX.X is the installed version of CUDA.
 *
 *
 * ## Example launch command:
 *
 * (note: an object detection model has 3 or 4 output nodes, but there is no
 * naming convention to indicate which node outputs the bounding box, which
 * node outputs the label, etc. So, the `onnxobjectdetector` element has
 * properties to map each node's functionality to its respective node index in
 * the specified model. Image resolution also need to be adapted to the model.
 * The videoscale in the pipeline below will scale the image, using padding if
 * required, to 640x383 resolution required by the model.)
 *
 * model.onnx can be found here:
 * https://github.com/zoq/onnx-runtime-examples/raw/main/data/models/model.onnx
 *
 * ```
 * GST_DEBUG=objectdetector:5 gst-launch-1.0 multifilesrc \
 * location=000000088462.jpg caps=image/jpeg,framerate=\(fraction\)30/1 ! jpegdec ! \
 * videoconvert ! \
 * videoscale ! \
 * 'video/x-raw,width=640,height=383' ! \
 * onnxobjectdetector \
 * box-node-index=0 \
 * class-node-index=1 \
 * score-node-index=2 \
 * detection-node-index=3 \
 * execution-provider=cpu \
 * model-file=model.onnx \
 * label-file=COCO_classes.txt  !  \
 * videoconvert ! \
 * autovideosink
 * ```
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstonnxobjectdetector.h"
#include "gstonnxclient.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

GST_DEBUG_CATEGORY_STATIC (onnx_object_detector_debug);
#define GST_CAT_DEFAULT onnx_object_detector_debug
#define GST_ONNX_MEMBER( self ) ((GstOnnxNamespace::GstOnnxClient *) (self->onnx_ptr))
GST_ELEMENT_REGISTER_DEFINE (onnx_object_detector, "onnxobjectdetector",
    GST_RANK_PRIMARY, GST_TYPE_ONNX_OBJECT_DETECTOR);

/* GstOnnxObjectDetector properties */
enum
{
  PROP_0,
  PROP_MODEL_FILE,
  PROP_LABEL_FILE,
  PROP_SCORE_THRESHOLD,
  PROP_DETECTION_NODE_INDEX,
  PROP_BOUNDING_BOX_NODE_INDEX,
  PROP_SCORE_NODE_INDEX,
  PROP_CLASS_NODE_INDEX,
  PROP_INPUT_IMAGE_FORMAT,
  PROP_OPTIMIZATION_LEVEL,
  PROP_EXECUTION_PROVIDER
};


#define GST_ONNX_OBJECT_DETECTOR_DEFAULT_EXECUTION_PROVIDER    GST_ONNX_EXECUTION_PROVIDER_CPU
#define GST_ONNX_OBJECT_DETECTOR_DEFAULT_OPTIMIZATION_LEVEL    GST_ONNX_OPTIMIZATION_LEVEL_ENABLE_EXTENDED
#define GST_ONNX_OBJECT_DETECTOR_DEFAULT_SCORE_THRESHOLD       0.3f     /* 0 to 1 */

static GstStaticPadTemplate gst_onnx_object_detector_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB,RGBA,BGR,BGRA }"))
    );

static GstStaticPadTemplate gst_onnx_object_detector_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ RGB,RGBA,BGR,BGRA }"))
    );

static void gst_onnx_object_detector_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_onnx_object_detector_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_onnx_object_detector_finalize (GObject * object);
static GstFlowReturn gst_onnx_object_detector_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);
static gboolean gst_onnx_object_detector_process (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean gst_onnx_object_detector_create_session (GstBaseTransform * trans);
static GstCaps *gst_onnx_object_detector_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps);

G_DEFINE_TYPE (GstOnnxObjectDetector, gst_onnx_object_detector,
    GST_TYPE_BASE_TRANSFORM);

static void
gst_onnx_object_detector_class_init (GstOnnxObjectDetectorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  GST_DEBUG_CATEGORY_INIT (onnx_object_detector_debug, "onnxobjectdetector",
      0, "onnx_objectdetector");
  gobject_class->set_property = gst_onnx_object_detector_set_property;
  gobject_class->get_property = gst_onnx_object_detector_get_property;
  gobject_class->finalize = gst_onnx_object_detector_finalize;

  /**
   * GstOnnxObjectDetector:model-file
   *
   * ONNX model file
   *
   * Since: 1.20
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MODEL_FILE,
      g_param_spec_string ("model-file",
          "ONNX model file", "ONNX model file", NULL, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstOnnxObjectDetector:label-file
   *
   * Label file for ONNX model
   *
   * Since: 1.20
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LABEL_FILE,
      g_param_spec_string ("label-file",
          "Label file", "Label file associated with model", NULL, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));


  /**
   * GstOnnxObjectDetector:detection-node-index
   *
   * Index of model detection node
   *
   * Since: 1.20
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_DETECTION_NODE_INDEX,
      g_param_spec_int ("detection-node-index",
          "Detection node index",
          "Index of neural network output node corresponding to number of detected objects",
          GstOnnxNamespace::GST_ML_NODE_INDEX_DISABLED,
          GstOnnxNamespace::GST_ML_OUTPUT_NODE_NUMBER_OF-1,
		  GstOnnxNamespace::GST_ML_NODE_INDEX_DISABLED, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));


  /**
   * GstOnnxObjectDetector:bounding-box-node-index
   *
   * Index of model bounding box node
   *
   * Since: 1.20
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_BOUNDING_BOX_NODE_INDEX,
      g_param_spec_int ("box-node-index",
          "Bounding box node index",
          "Index of neural network output node corresponding to bounding box",
          GstOnnxNamespace::GST_ML_NODE_INDEX_DISABLED,
          GstOnnxNamespace::GST_ML_OUTPUT_NODE_NUMBER_OF-1,
		  GstOnnxNamespace::GST_ML_NODE_INDEX_DISABLED, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstOnnxObjectDetector:score-node-index
   *
   * Index of model score node
   *
   * Since: 1.20
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_SCORE_NODE_INDEX,
      g_param_spec_int ("score-node-index",
          "Score node index",
          "Index of neural network output node corresponding to score",
          GstOnnxNamespace::GST_ML_NODE_INDEX_DISABLED,
          GstOnnxNamespace::GST_ML_OUTPUT_NODE_NUMBER_OF-1,
		  GstOnnxNamespace::GST_ML_NODE_INDEX_DISABLED, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstOnnxObjectDetector:class-node-index
   *
   * Index of model class (label) node
   *
   * Since: 1.20
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_CLASS_NODE_INDEX,
      g_param_spec_int ("class-node-index",
          "Class node index",
          "Index of neural network output node corresponding to class (label)",
          GstOnnxNamespace::GST_ML_NODE_INDEX_DISABLED,
          GstOnnxNamespace::GST_ML_OUTPUT_NODE_NUMBER_OF-1,
		  GstOnnxNamespace::GST_ML_NODE_INDEX_DISABLED, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));


  /**
   * GstOnnxObjectDetector:score-threshold
   *
   * Threshold for deciding when to remove boxes based on score
   *
   * Since: 1.20
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SCORE_THRESHOLD,
      g_param_spec_float ("score-threshold",
          "Score threshold",
          "Threshold for deciding when to remove boxes based on score",
          0.0, 1.0,
          GST_ONNX_OBJECT_DETECTOR_DEFAULT_SCORE_THRESHOLD, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstOnnxObjectDetector:input-image-format
   *
   * Model input image format
   *
   * Since: 1.20
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_INPUT_IMAGE_FORMAT,
      g_param_spec_enum ("input-image-format",
          "Input image format",
          "Input image format",
          GST_TYPE_ML_MODEL_INPUT_IMAGE_FORMAT,
          GST_ML_MODEL_INPUT_IMAGE_FORMAT_HWC, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

   /**
    * GstOnnxObjectDetector:optimization-level
    *
    * ONNX optimization level
    *
    * Since: 1.20
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
   * GstOnnxObjectDetector:execution-provider
   *
   * ONNX execution provider
   *
   * Since: 1.20
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_EXECUTION_PROVIDER,
      g_param_spec_enum ("execution-provider",
          "Execution provider",
          "ONNX execution provider",
          GST_TYPE_ONNX_EXECUTION_PROVIDER,
          GST_ONNX_EXECUTION_PROVIDER_CPU, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class, "onnxobjectdetector",
      "Filter/Effect/Video",
      "Apply neural network to detect objects in video frames",
      "Aaron Boxer <aaron.boxer@collabora.com>, Marcus Edel <marcus.edel@collabora.com>");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_onnx_object_detector_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_onnx_object_detector_src_template));
  basetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_onnx_object_detector_transform_ip);
  basetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_onnx_object_detector_transform_caps);
}

static void
gst_onnx_object_detector_init (GstOnnxObjectDetector * self)
{
  self->onnx_ptr = new GstOnnxNamespace::GstOnnxClient ();
  self->onnx_disabled = false;
}

static void
gst_onnx_object_detector_finalize (GObject * object)
{
  GstOnnxObjectDetector *self = GST_ONNX_OBJECT_DETECTOR (object);

  g_free (self->model_file);
  delete GST_ONNX_MEMBER (self);
  G_OBJECT_CLASS (gst_onnx_object_detector_parent_class)->finalize (object);
}

static void
gst_onnx_object_detector_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOnnxObjectDetector *self = GST_ONNX_OBJECT_DETECTOR (object);
  const gchar *filename;
  auto onnxClient = GST_ONNX_MEMBER (self);

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
        gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), TRUE);
      }
      break;
    case PROP_LABEL_FILE:
      filename = g_value_get_string (value);
      if (filename
          && g_file_test (filename,
              (GFileTest) (G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR))) {
        if (self->label_file)
          g_free (self->label_file);
        self->label_file = g_strdup (filename);
      } else {
        GST_WARNING_OBJECT (self, "Label file '%s' not found!", filename);
      }
      break;
    case PROP_SCORE_THRESHOLD:
      GST_OBJECT_LOCK (self);
      self->score_threshold = g_value_get_float (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_OPTIMIZATION_LEVEL:
      self->optimization_level =
          (GstOnnxOptimizationLevel) g_value_get_enum (value);
      break;
    case PROP_EXECUTION_PROVIDER:
      self->execution_provider =
          (GstOnnxExecutionProvider) g_value_get_enum (value);
      break;
    case PROP_DETECTION_NODE_INDEX:
      onnxClient->setOutputNodeIndex
          (GstOnnxNamespace::GST_ML_OUTPUT_NODE_FUNCTION_DETECTION,
          g_value_get_int (value));
      break;
    case PROP_BOUNDING_BOX_NODE_INDEX:
      onnxClient->setOutputNodeIndex
          (GstOnnxNamespace::GST_ML_OUTPUT_NODE_FUNCTION_BOUNDING_BOX,
          g_value_get_int (value));
      break;
      break;
    case PROP_SCORE_NODE_INDEX:
      onnxClient->setOutputNodeIndex
          (GstOnnxNamespace::GST_ML_OUTPUT_NODE_FUNCTION_SCORE,
          g_value_get_int (value));
      break;
      break;
    case PROP_CLASS_NODE_INDEX:
      onnxClient->setOutputNodeIndex
          (GstOnnxNamespace::GST_ML_OUTPUT_NODE_FUNCTION_CLASS,
          g_value_get_int (value));
      break;
    case PROP_INPUT_IMAGE_FORMAT:
      onnxClient->setInputImageFormat ((GstMlModelInputImageFormat)
          g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_onnx_object_detector_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOnnxObjectDetector *self = GST_ONNX_OBJECT_DETECTOR (object);
  auto onnxClient = GST_ONNX_MEMBER (self);

  switch (prop_id) {
    case PROP_MODEL_FILE:
      g_value_set_string (value, self->model_file);
      break;
    case PROP_LABEL_FILE:
      g_value_set_string (value, self->label_file);
      break;
    case PROP_SCORE_THRESHOLD:
      GST_OBJECT_LOCK (self);
      g_value_set_float (value, self->score_threshold);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_OPTIMIZATION_LEVEL:
      g_value_set_enum (value, self->optimization_level);
      break;
    case PROP_EXECUTION_PROVIDER:
      g_value_set_enum (value, self->execution_provider);
      break;
    case PROP_DETECTION_NODE_INDEX:
      g_value_set_int (value,
          onnxClient->getOutputNodeIndex
          (GstOnnxNamespace::GST_ML_OUTPUT_NODE_FUNCTION_DETECTION));
      break;
    case PROP_BOUNDING_BOX_NODE_INDEX:
      g_value_set_int (value,
          onnxClient->getOutputNodeIndex
          (GstOnnxNamespace::GST_ML_OUTPUT_NODE_FUNCTION_BOUNDING_BOX));
      break;
      break;
    case PROP_SCORE_NODE_INDEX:
      g_value_set_int (value,
          onnxClient->getOutputNodeIndex
          (GstOnnxNamespace::GST_ML_OUTPUT_NODE_FUNCTION_SCORE));
      break;
      break;
    case PROP_CLASS_NODE_INDEX:
      g_value_set_int (value,
          onnxClient->getOutputNodeIndex
          (GstOnnxNamespace::GST_ML_OUTPUT_NODE_FUNCTION_CLASS));
      break;
    case PROP_INPUT_IMAGE_FORMAT:
      g_value_set_enum (value, onnxClient->getInputImageFormat ());
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_onnx_object_detector_create_session (GstBaseTransform * trans)
{
  GstOnnxObjectDetector *self = GST_ONNX_OBJECT_DETECTOR (trans);
  auto onnxClient = GST_ONNX_MEMBER (self);

  GST_OBJECT_LOCK (self);
  if (self->onnx_disabled || onnxClient->hasSession ()) {
    GST_OBJECT_UNLOCK (self);

    return TRUE;
  }
  if (self->model_file) {
    gboolean ret = GST_ONNX_MEMBER (self)->createSession (self->model_file,
        self->optimization_level,
        self->execution_provider);
    if (!ret) {
      GST_ERROR_OBJECT (self,
          "Unable to create ONNX session. Detection disabled.");
    } else {
      auto outputNames = onnxClient->getOutputNodeNames ();

      for (size_t i = 0; i < outputNames.size (); ++i)
        GST_INFO_OBJECT (self, "Output node index: %d for node: %s", (gint) i,
            outputNames[i]);
      if (outputNames.size () < 3) {
        GST_ERROR_OBJECT (self,
            "Number of output tensor nodes %d does not match the 3 or 4 nodes "
            "required for an object detection model. Detection is disabled.",
            (gint) outputNames.size ());
        self->onnx_disabled = TRUE;
      }
      // sanity check on output node indices
      if (onnxClient->getOutputNodeIndex
          (GstOnnxNamespace::GST_ML_OUTPUT_NODE_FUNCTION_DETECTION) ==
          GstOnnxNamespace::GST_ML_NODE_INDEX_DISABLED) {
        GST_ERROR_OBJECT (self,
            "Output detection node index not set. Detection disabled.");
        self->onnx_disabled = TRUE;
      }
      if (onnxClient->getOutputNodeIndex
          (GstOnnxNamespace::GST_ML_OUTPUT_NODE_FUNCTION_BOUNDING_BOX) ==
          GstOnnxNamespace::GST_ML_NODE_INDEX_DISABLED) {
        GST_ERROR_OBJECT (self,
            "Output bounding box node index not set. Detection disabled.");
        self->onnx_disabled = TRUE;
      }
      if (onnxClient->getOutputNodeIndex
          (GstOnnxNamespace::GST_ML_OUTPUT_NODE_FUNCTION_SCORE) ==
          GstOnnxNamespace::GST_ML_NODE_INDEX_DISABLED) {
        GST_ERROR_OBJECT (self,
            "Output score node index not set. Detection disabled.");
        self->onnx_disabled = TRUE;
      }
      if (outputNames.size () == 4 && onnxClient->getOutputNodeIndex
          (GstOnnxNamespace::GST_ML_OUTPUT_NODE_FUNCTION_CLASS) ==
          GstOnnxNamespace::GST_ML_NODE_INDEX_DISABLED) {
        GST_ERROR_OBJECT (self,
            "Output class node index not set. Detection disabled.");
        self->onnx_disabled = TRUE;
      }
	  // model is not usable, so fail
      if (self->onnx_disabled) {
		  GST_ELEMENT_WARNING (self, RESOURCE, FAILED,
			  ("ONNX model cannot be used for object detection"), (NULL));

		  return FALSE;
      }
    }
  } else {
    self->onnx_disabled = TRUE;
  }
  GST_OBJECT_UNLOCK (self);
  if (self->onnx_disabled){
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), TRUE);
  }

  return TRUE;
}


static GstCaps *
gst_onnx_object_detector_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps)
{
  GstOnnxObjectDetector *self = GST_ONNX_OBJECT_DETECTOR (trans);
  auto onnxClient = GST_ONNX_MEMBER (self);
  GstCaps *other_caps;
  guint i;

  if ( !gst_onnx_object_detector_create_session (trans) )
	  return NULL;
  GST_LOG_OBJECT (self, "transforming caps %" GST_PTR_FORMAT, caps);

  if (gst_base_transform_is_passthrough (trans)
      || (!onnxClient->isFixedInputImageSize ()))
    return gst_caps_ref (caps);

  other_caps = gst_caps_new_empty ();
  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    GstStructure *structure, *new_structure;

    structure = gst_caps_get_structure (caps, i);
    new_structure = gst_structure_copy (structure);
    gst_structure_set (new_structure, "width", G_TYPE_INT,
        onnxClient->getWidth (), "height", G_TYPE_INT,
        onnxClient->getHeight (), NULL);
    GST_LOG_OBJECT (self,
        "transformed structure %2d: %" GST_PTR_FORMAT " => %"
        GST_PTR_FORMAT, i, structure, new_structure);
    gst_caps_append_structure (other_caps, new_structure);
  }

  if (!gst_caps_is_empty (other_caps) && filter_caps) {
    GstCaps *tmp = gst_caps_intersect_full (other_caps,filter_caps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_replace (&other_caps, tmp);
    gst_caps_unref (tmp);
  }

  return other_caps;
}


static GstFlowReturn
gst_onnx_object_detector_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf)
{
  if (!gst_base_transform_is_passthrough (trans)
      && !gst_onnx_object_detector_process (trans, buf)){
	    GST_ELEMENT_WARNING (trans, STREAM, FAILED,
          ("ONNX object detection failed"), (NULL));
	    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static gboolean
gst_onnx_object_detector_process (GstBaseTransform * trans, GstBuffer * buf)
{
  GstMapInfo info;
  GstVideoMeta *vmeta = gst_buffer_get_video_meta (buf);

  if (!vmeta) {
    GST_WARNING_OBJECT (trans, "missing video meta");
    return FALSE;
  }
  if (gst_buffer_map (buf, &info, GST_MAP_READ)) {
    GstOnnxObjectDetector *self = GST_ONNX_OBJECT_DETECTOR (trans);
    std::vector < GstOnnxNamespace::GstMlBoundingBox > boxes;
    try {
      boxes = GST_ONNX_MEMBER (self)->run (info.data, vmeta,
          self->label_file ? self->label_file : "", self->score_threshold);
    }
    catch (Ort::Exception & ortex) {
      GST_ERROR_OBJECT (self, "%s", ortex.what ());
      return FALSE;
    }
  for (auto & b:boxes) {
      auto vroi_meta = gst_buffer_add_video_region_of_interest_meta (buf,
          GST_ONNX_OBJECT_DETECTOR_META_NAME,
          b.x0, b.y0,
          b.width,
          b.height);
      if (!vroi_meta) {
        GST_WARNING_OBJECT (trans,
            "Unable to attach GstVideoRegionOfInterestMeta to buffer");
        return FALSE;
      }
      auto s = gst_structure_new (GST_ONNX_OBJECT_DETECTOR_META_PARAM_NAME,
          GST_ONNX_OBJECT_DETECTOR_META_FIELD_LABEL,
          G_TYPE_STRING,
          b.label.c_str (),
          GST_ONNX_OBJECT_DETECTOR_META_FIELD_SCORE,
          G_TYPE_DOUBLE,
          b.score,
          NULL);
      gst_video_region_of_interest_meta_add_param (vroi_meta, s);
      GST_DEBUG_OBJECT (self,
          "Object detected with label : %s, score: %f, bound box: (%f,%f,%f,%f) \n",
          b.label.c_str (), b.score, b.x0, b.y0,
          b.x0 + b.width, b.y0 + b.height);
    }
    gst_buffer_unmap (buf, &info);
  }

  return TRUE;
}
