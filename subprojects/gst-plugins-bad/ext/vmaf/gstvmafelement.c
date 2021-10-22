/* VMAF plugin
 * Copyright (C) 2021 Hudl
 *   @author: Casey Bateman <Casey.Bateman@hudl.com>
 * Copyright (C) 2025 Fluendo S.A. <contact@fluendo.com>
 *   Authors: Diego Nieto <dnieto@fluendo.com>
 *   Authors: Andoni Morales Alastruey <amorales@fluendo.com>
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
 * SECTION:element-vmaf
 * @title: vmaf
 * @short_description: Provides Video Multi-Method Assessment Fusion quality metrics
 *
 * VMAF (Video Multi-Method Assessment Fusion) is a perceptual video quality
 * assessment algorithm developed by Netflix. It combines multiple elementary
 * quality metrics (VIF, DLM, Motion, ADM) and fuses them using a machine
 * learning model to predict the perceived video quality as experienced by
 * human viewers. VMAF scores range from 0 to 100, where higher scores indicate
 * better perceptual quality.
 *
 * This element is useful for:
 * - Evaluating video encoding quality and compression efficiency
 * - Comparing different encoding settings or codecs
 * - Quality assurance in video processing pipelines
 * - A/B testing of video content
 *
 * For more information about VMAF, see: https://github.com/Netflix/vmaf
 *
 * VMAF will perform perceptive video quality analysis on a set of input
 * pads, the first pad is the reference video, the second is the distorted pad.
 *
 * The image output will be the be the reference video pad, ref_pad.
 *
 * VMAF will post a message containing a structure named "VMAF" at EOS 
 * or every reference frame if the property for frame-message=true.
 *
 * The VMAF message structure contains the following fields:
 *
 * - "timestamp"     #G_TYPE_UINT64   Buffer timestamp in nanoseconds
 * - "stream-time"   #G_TYPE_UINT64   Stream time in nanoseconds
 * - "running-time"  #G_TYPE_UINT64   Running time in nanoseconds
 * - "duration"      #G_TYPE_UINT64   Duration in nanoseconds
 * - "score"         #G_TYPE_DOUBLE   The VMAF quality score (0-100, higher is better)
 * - "type"          #G_TYPE_STRING   Message type: "frame" = per-frame score, "pooled" = aggregate score
 * - "index"         #G_TYPE_INT      Frame index (only present for type="frame", per-frame messages)
 * - "psnr-y"        #G_TYPE_DOUBLE   Peak Signal-to-Noise Ratio for Y (luma) channel in dB
 *                                     (only present if psnr property is enabled)
 * - "ssim"          #G_TYPE_DOUBLE   Structural Similarity Index (0-1, higher is better)
 *                                     (only present if ssim property is enabled)
 * - "ms-ssim"       #G_TYPE_DOUBLE   Multi-Scale Structural Similarity Index (0-1, higher is better)
 *                                     (only present if ms-ssim property is enabled)
 *
 * The "type" field indicates whether the message contains a score for an individual
 * frame (type="frame") or a pooled score for the entire stream up to that point (type="pooled").
 * Pooled scores are calculated at EOS using the pool-method property (mean, min, max,
 * or harmonic mean).
 *
 * The timing fields (timestamp, stream-time, running-time, duration) allow correlation
 * of VMAF scores with specific video frames in the pipeline.
 *
 * Per-frame messages (type="frame") include an "index" field indicating the frame number.
 * With sub-sampling enabled, scores are only computed for frames at the sub-sampling
 * rate, except motion scores which are computed for every frame.
 *
 * It is possible to configure and run PSNR, SSIM, MS-SSIM together with VMAF
 * by setting the appropriate properties to true.
 *
 * For example, if ms-ssim, ssim, psnr are set to true, the emitted structure will look like this:
 *
 * VMAF, timestamp=(guint64)1234567890, stream-time=(guint64)1234567890, running-time=(guint64)1234567890, duration=(guint64)40000000, score=(double)78.910751757633022, index=(int)26, type=(string)frame, ms-ssim=(double)0.96676034472760064, ssim=(double)0.8706783652305603, psnr-y=(double)30.758853484390933;
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -m \
 *   filesrc location=test1.yuv ! rawvideoparse width=1920 height=1080 ! v.ref_sink  \
 *   filesrc location=test2.yuv ! rawvideoparse width=1920 height=1080 ! v.dist_sink \
 *   vmaf name=v frame-message=true results-filename=scores.json psnr=true ssim=true ms-ssim=true ! autovideosink \
 * ]| This pipeline will output messages to the console for each set of compared frames.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

#include "gstvmafelement.h"

#include <stdio.h>
#include <libvmaf.h>

GST_DEBUG_CATEGORY_STATIC (gst_vmaf_debug);
#define GST_CAT_DEFAULT gst_vmaf_debug
#define SINK_FORMATS " { I420, NV12, YV12, Y42B, Y444, I420_10LE, I422_10LE, Y444_10LE } "
#define SRC_FORMAT " { I420, NV12, YV12, Y42B, Y444, I420_10LE, I422_10LE, Y444_10LE } "
#define DEFAULT_MODEL_FILENAME     "vmaf_v0.6.1"
#define DEFAULT_DISABLE_CLIP       FALSE
#define DEFAULT_ENABLE_TRANSFORM   FALSE
#define DEFAULT_PHONE_MODEL        FALSE
#define DEFAULT_PSNR               FALSE
#define DEFAULT_SSIM               FALSE
#define DEFAULT_MS_SSIM            FALSE
#define DEFAULT_FRAME_MESSAGING    FALSE
#define DEFAULT_POOL_METHOD        VMAF_POOL_METHOD_MEAN
#define DEFAULT_NUM_THREADS        g_get_num_processors()
#define DEFAULT_SUBSAMPLE          1
#define DEFAULT_CONF_INT           FALSE
#define DEFAULT_VMAF_LOG_LEVEL     VMAF_LOG_LEVEL_NONE
#define DEFAULT_VMAF_RESULTS_FORMAT    VMAF_OUTPUT_FORMAT_NONE
#define DEFAULT_VMAF_RESULTS_FILENAME  NULL
#define GST_TYPE_VMAF_POOL_METHOD  (gst_vmaf_pool_method_get_type ())
#define GST_TYPE_VMAF_OUTPUT_FORMATS  (gst_vmaf_results_format_get_type ())
#define GST_TYPE_VMAF_LOG_LEVEL  (gst_vmaf_log_level_get_type ())

typedef enum _GstReadFrameReturnCodes
{
  READING_SUCCESSFUL = 0,
  READING_ERROR = 1,
  READING_EOS = 2,
} GstReadFrameReturnCodes;

typedef enum _GstVmafPropertyTypes
{
  PROP_0,
  PROP_MODEL_FILENAME,
  PROP_DISABLE_CLIP,
  PROP_ENABLE_TRANSFORM,
  PROP_PHONE_MODEL,
  PROP_PSNR,
  PROP_SSIM,
  PROP_MS_SSIM,
  PROP_NUM_THREADS,
  PROP_SUBSAMPLE,
  PROP_CONF_INT,
  PROP_LAST,
  PROP_POOL_METHOD,
  PROP_FRAME_MESSAGING,
  PROP_VMAF_RESULTS_FORMAT,
  PROP_VMAF_RESULTS_FILENAME,
  PROP_LOG_LEVEL,
} GstVmafPropertyTypes;

typedef enum _GstVmafMessageBusScoreTypes
{
  MESSAGE_TYPE_FRAME = 0,
  MESSAGE_TYPE_POOLED = 1,
} GstVmafMessageBusScoreTypes;

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (SRC_FORMAT)));

static GstStaticPadTemplate ref_factory = GST_STATIC_PAD_TEMPLATE ("ref_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (SINK_FORMATS)));

static GstStaticPadTemplate dist_factory = GST_STATIC_PAD_TEMPLATE ("dist_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (SINK_FORMATS)));

#define gst_vmaf_parent_class parent_class
G_DEFINE_TYPE (GstVmaf, gst_vmaf, GST_TYPE_VIDEO_AGGREGATOR);

static GType
gst_vmaf_pool_method_get_type (void)
{
  static const GEnumValue types[] = {
    {VMAF_POOL_METHOD_MIN, "Minimum value", "min"},
    {VMAF_POOL_METHOD_MAX, "Maximum value", "max"},
    {VMAF_POOL_METHOD_MEAN, "Arithmetic mean", "mean"},
    {VMAF_POOL_METHOD_HARMONIC_MEAN, "Harmonic mean", "harmonic_mean"},
    {0, NULL, NULL},
  };
  static gsize id = 0;

  if (g_once_init_enter (&id)) {
    GType _id = g_enum_register_static ("GstVmafPoolMethod", types);
    g_once_init_leave (&id, _id);
  }

  return (GType) id;
}

#define GST_VMAF_POOL_METHOD_TYPE (gst_vmaf_pool_method_get_type())

static GType
gst_vmaf_results_format_get_type (void)
{
  static const GEnumValue types[] = {
    {VMAF_OUTPUT_FORMAT_NONE, "None", "none"},
    {VMAF_OUTPUT_FORMAT_XML, "XML", "xml"},
    {VMAF_OUTPUT_FORMAT_CSV, "Comma Separated File (csv)", "csv"},
    {VMAF_OUTPUT_FORMAT_JSON, "JSON", "json"},
    {0, NULL, NULL},
  };
  static gsize id = 0;

  if (g_once_init_enter (&id)) {
    GType _id = g_enum_register_static ("GstVmafResultsFormat", types);
    g_once_init_leave (&id, _id);
  }

  return (GType) id;
}

#define GST_VMAF_RESULTS_FORMAT_TYPE (gst_vmaf_results_format_get_type())

static GType
gst_vmaf_log_level_get_type (void)
{
  static const GEnumValue types[] = {
    {VMAF_LOG_LEVEL_NONE, "No logging", "none"},
    {VMAF_LOG_LEVEL_ERROR, "Error", "error"},
    {VMAF_LOG_LEVEL_WARNING, "Warning", "warning"},
    {VMAF_LOG_LEVEL_INFO, "Info", "info"},
    {VMAF_LOG_LEVEL_DEBUG, "Debug", "debug"},
    {0, NULL, NULL},
  };
  static gsize id = 0;

  if (g_once_init_enter (&id)) {
    GType _id = g_enum_register_static ("GstVmafLogLevel", types);
    g_once_init_leave (&id, _id);
  }

  return (GType) id;
}

#define GST_VMAF_LOG_LEVEL_TYPE (gst_vmaf_log_level_get_type())

static void
gst_vmaf_context_free (GstVmaf * self)
{
  g_clear_pointer (&self->vmaf_ctx, vmaf_close);
  g_clear_pointer (&self->vmaf_model, vmaf_model_destroy);
  g_clear_pointer (&self->vmaf_model_collection, vmaf_model_collection_destroy);
}

static gboolean
gst_vmaf_model_init (GstVmaf * self, VmafModelConfig * model_cfg)
{
  gint err = 0;
  gint err_builtin = 0;
  gint err_path = 0;

  //attempt to load from the built in models first
  err_builtin =
      vmaf_model_load (&self->vmaf_model, model_cfg,
      self->vmaf_config_model_filename);
  if (err_builtin) {
    //if built in model will not load, attempt to load from file path
    err_path =
        vmaf_model_load_from_path (&self->vmaf_model, model_cfg,
        self->vmaf_config_model_filename);
    if (err_path) {
      GST_ERROR_OBJECT (self,
          "Failed to load VMAF model '%s': not found as built-in model "
          "(err=%d) or file path (err=%d)",
          self->vmaf_config_model_filename, err_builtin, err_path);
      return FALSE;
    }
  }

  err = vmaf_use_features_from_model (self->vmaf_ctx, self->vmaf_model);
  if (err) {
    GST_ERROR_OBJECT (self,
        "Error %d. Failed to load self feature extractors from model file: %s",
        err, self->vmaf_config_model_filename);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vmaf_model_collection_init (GstVmaf * self, VmafModelConfig * model_cfg)
{
  gint err = 0;
  gint err_builtin = 0;
  gint err_path = 0;
  //attempt to load from the built in models first
  err_builtin =
      vmaf_model_collection_load (&self->vmaf_model,
      &self->vmaf_model_collection, model_cfg,
      self->vmaf_config_model_filename);
  if (err_builtin) {
    //if built in model will not load, attempt to load from file path
    err_path =
        vmaf_model_collection_load_from_path (&self->vmaf_model,
        &self->vmaf_model_collection, model_cfg,
        self->vmaf_config_model_filename);
    if (err_path) {
      GST_ERROR_OBJECT (self,
          "Failed to load VMAF model collection '%s': not found as built-in model collection "
          "(err=%d) or file path (err=%d)",
          self->vmaf_config_model_filename, err_builtin, err_path);
      return FALSE;
    }
  }

  err =
      vmaf_use_features_from_model_collection (self->vmaf_ctx,
      self->vmaf_model_collection);
  if (err) {
    GST_ERROR_OBJECT (self,
        "Error %d. Failed to load self feature extractors from model file: %s",
        err, self->vmaf_config_model_filename);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_vmaf_context_init (GstVmaf * self)
{
  gint err = 0;
  gboolean result = TRUE;
  VmafFeatureDictionary *d = NULL;
  enum VmafModelFlags flags = VMAF_MODEL_FLAGS_DEFAULT;
  VmafModelConfig model_cfg = { 0 };
  VmafConfiguration cfg = {
    .log_level = self->vmaf_config_log_level,
    .n_threads =
        self->vmaf_config_frame_messaging ? 0 : self->vmaf_config_num_threads,
    .n_subsample = self->vmaf_config_subsample,
  };

  GST_INFO_OBJECT (self, "Initializing VMAF");

  err = vmaf_init (&self->vmaf_ctx, cfg);
  if (err) {
    GST_ERROR_OBJECT (self, "Failed to initialize self context.");
    result = FALSE;
    goto free_data;
  }

  if (self->vmaf_config_disable_clip)
    flags |= VMAF_MODEL_FLAG_DISABLE_CLIP;
  if (self->vmaf_config_enable_transform || self->vmaf_config_phone_model)
    flags |= VMAF_MODEL_FLAG_ENABLE_TRANSFORM;

  model_cfg.name = "self";
  model_cfg.flags = flags;

  if (self->vmaf_config_conf_int) {
    if (!gst_vmaf_model_collection_init (self, &model_cfg)) {
      GST_ERROR_OBJECT (self, "Failed to initialize model collection");
      result = FALSE;
      goto free_data;
    }
  } else {
    if (!gst_vmaf_model_init (self, &model_cfg)) {
      GST_ERROR_OBJECT (self, "Failed to initialize model");
      result = FALSE;
      goto free_data;
    }
  }

  if (self->vmaf_config_psnr) {
    vmaf_feature_dictionary_set (&d, "enable_chroma", "false");

    err = vmaf_use_feature (self->vmaf_ctx, "psnr", d);
    if (err) {
      GST_ERROR_OBJECT (self, "Problem loading feature extractor: psnr");
      result = FALSE;
      goto free_data;
    }
  }
  if (self->vmaf_config_ssim) {
    err = vmaf_use_feature (self->vmaf_ctx, "float_ssim", NULL);
    if (err) {
      GST_ERROR_OBJECT (self, "Problem loading feature extractor: ssim");
      result = FALSE;
      goto free_data;
    }
  }
  if (self->vmaf_config_ms_ssim) {
    err = vmaf_use_feature (self->vmaf_ctx, "float_ms_ssim", NULL);
    if (err) {
      GST_ERROR_OBJECT (self,
          "Problem loading feature extractor: float_ms_ssim");
      result = FALSE;
      goto free_data;
    }
  }

  self->processed_frames = 0;
  self->pix_fmt = VMAF_PIX_FMT_YUV400P;
  self->initialized = TRUE;
  self->flushed = FALSE;

  GST_INFO_OBJECT (self, "Initialized VMAF");

end:
  return result;
free_data:
  gst_vmaf_context_free (self);
  goto end;
}

static gboolean
gst_vmaf_context_flush (GstVmaf * self)
{
  gint err = 0;

  GST_DEBUG_OBJECT (self, "Flushing buffers and calculating pooled score.");

  GST_OBJECT_LOCK (self);

  if (self->vmaf_ctx && !self->flushed) {
    err = vmaf_read_pictures (self->vmaf_ctx, NULL, NULL, 0);
    if (err) {
      GST_ERROR_OBJECT (self, "failed to flush VMAF context");
      GST_OBJECT_UNLOCK (self);
      return FALSE;
    }
    self->flushed = TRUE;
  }

  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

static void
gst_vmaf_add_feature_score (GstVmaf * self,
    GstStructure * structure,
    const gchar * feature_name, const gchar * field_name, gint frame_index)
{
  gint err;
  gdouble score = 0;

  err = vmaf_feature_score_at_index (self->vmaf_ctx, feature_name,
      &score, frame_index);
  if (err) {
    GST_WARNING_OBJECT (self,
        "could not calculate %s score on frame:%d err:%d",
        feature_name, frame_index, err);
  } else {
    gst_structure_set (structure, field_name, G_TYPE_DOUBLE, score, NULL);
  }
}

static void
gst_vmaf_add_pooled_feature_score (GstVmaf * self, GstStructure * structure,
    const gchar * feature_name, const gchar * field_name,
    enum VmafPoolingMethod pooling_method, gint start_frame, gint end_frame)
{
  gint err;
  gdouble score = 0;

  err = vmaf_feature_score_pooled (self->vmaf_ctx, feature_name,
      pooling_method, &score, start_frame, end_frame);
  if (err) {
    GST_WARNING_OBJECT (self,
        "could not calculate %s score on range:%d-%d err:%d",
        feature_name, start_frame, end_frame, err);
  } else {
    gst_structure_set (structure, field_name, G_TYPE_DOUBLE, score, NULL);
  }
}

static gint
gst_vmaf_post_pooled_score (GstVmaf * self)
{
  gint err = 0;
  gdouble vmaf_score = 0;
  gboolean successful_post = TRUE;
  VmafModelCollectionScore model_collection_score;
  GstStructure *vmaf_message_structure;
  GstMessage *vmaf_message;
  enum VmafOutputFormat vmaf_output_format = self->vmaf_config_results_format;
  gint last_frame_index = self->processed_frames - 1;
  GstClockTime timestamp, stream_time, running_time, duration;
  GstAggregator *agg = GST_AGGREGATOR (self);
  GstSegment *segment;

  if (self->vmaf_config_conf_int) {
    err = vmaf_score_pooled_model_collection (self->vmaf_ctx,
        self->vmaf_model_collection,
        self->vmaf_config_pool_method, &model_collection_score, 0,
        last_frame_index);
    if (err) {
      GST_DEBUG_OBJECT (self,
          "could not calculate pooled vmaf score on range 0 to %d, for model collection",
          last_frame_index);
      return FALSE;
    }
  }

  err = vmaf_score_pooled (self->vmaf_ctx,
      self->vmaf_model,
      self->vmaf_config_pool_method, &vmaf_score, 0, last_frame_index);
  if (err) {
    GST_WARNING_OBJECT (self,
        "could not calculate pooled vmaf score on range 0 to %d",
        last_frame_index);
    return FALSE;
  }
  GST_DEBUG_OBJECT (self,
      "posting pooled vmaf score on range:0-%d score:%f",
      last_frame_index, vmaf_score);

  GST_OBJECT_LOCK (agg->srcpad);
  segment = &GST_AGGREGATOR_PAD (agg->srcpad)->segment;
  timestamp = segment->position;

  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    if (GST_VIDEO_INFO_FPS_N (&GST_VIDEO_AGGREGATOR_PAD (self->ref_pad)->info) >
        0) {
      duration =
          gst_util_uint64_scale (self->processed_frames,
          GST_SECOND *
          GST_VIDEO_INFO_FPS_D (&GST_VIDEO_AGGREGATOR_PAD (self->
                  ref_pad)->info),
          GST_VIDEO_INFO_FPS_N (&GST_VIDEO_AGGREGATOR_PAD (self->
                  ref_pad)->info));
    } else {
      duration = GST_CLOCK_TIME_NONE;
    }

    running_time = gst_segment_to_running_time (segment, GST_FORMAT_TIME,
        timestamp);
    stream_time = gst_segment_to_stream_time (segment, GST_FORMAT_TIME,
        timestamp);
  } else {
    duration = GST_CLOCK_TIME_NONE;
    running_time = GST_CLOCK_TIME_NONE;
    stream_time = GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (agg->srcpad);

  vmaf_message_structure = gst_structure_new_empty ("VMAF");
  gst_structure_set (vmaf_message_structure,
      "timestamp", G_TYPE_UINT64, timestamp,
      "stream-time", G_TYPE_UINT64, stream_time,
      "running-time", G_TYPE_UINT64, running_time,
      "duration", G_TYPE_UINT64, duration,
      "score", G_TYPE_DOUBLE, vmaf_score,
      "type", G_TYPE_STRING, "pooled", NULL);

  if (self->vmaf_config_ms_ssim) {
    gst_vmaf_add_pooled_feature_score (self,
        vmaf_message_structure, "float_ms_ssim", "ms-ssim",
        self->vmaf_config_pool_method, 0, last_frame_index);
  }
  if (self->vmaf_config_ssim) {
    gst_vmaf_add_pooled_feature_score (self,
        vmaf_message_structure, "float_ssim", "ssim",
        self->vmaf_config_pool_method, 0, last_frame_index);
  }
  if (self->vmaf_config_psnr) {
    gst_vmaf_add_pooled_feature_score (self,
        vmaf_message_structure, "psnr_y", "psnr-y",
        self->vmaf_config_pool_method, 0, last_frame_index);
  }

  vmaf_message =
      gst_message_new_element (GST_OBJECT (self), vmaf_message_structure);
  successful_post = gst_element_post_message (GST_ELEMENT (self), vmaf_message);
  if (!successful_post) {
    GST_WARNING_OBJECT (self,
        "could not post pooled VMAF on message bus. score:%f", vmaf_score);
  }

  if (vmaf_output_format == VMAF_OUTPUT_FORMAT_NONE
      && self->vmaf_config_results_filename) {
    vmaf_output_format = VMAF_OUTPUT_FORMAT_JSON;
    GST_DEBUG_OBJECT (self, "using default JSON style logging.");
  }

  if (vmaf_output_format) {
    GST_DEBUG_OBJECT (self,
        "writing VMAF score data to location:%s.",
        self->vmaf_config_results_filename);

    err =
        vmaf_write_output (self->vmaf_ctx, self->vmaf_config_results_filename,
        vmaf_output_format);

    if (err) {
      GST_WARNING_OBJECT (self,
          "Failed to write VMAF output to '%s' (format=%d, err=%d)",
          self->vmaf_config_results_filename, vmaf_output_format, err);
      return FALSE;
    }
  }

  return TRUE;
}

static gint
gst_vmaf_post_frame_score (GstVmaf * self, gint frame_index)
{
  gint err = 0, scored_frame;
  gdouble vmaf_score = 0;
  gboolean mod_frame;
  GstStructure *vmaf_message_structure;
  GstMessage *vmaf_message;
  GstClockTime timestamp, stream_time, running_time, duration;
  GstAggregator *agg = GST_AGGREGATOR (self);
  GstSegment *segment;

  /* With sub-sampling, scores are only computed for frames at the sub-sampling rate
   * except VMAF_integer_feature_motion_score and VMAF_integer_feature_motion_score2
   * that are computed for every frame.
   * VMAF_integer_feature_motion2_score is computed for the past frame, so there is a
   * 1 frame delay in scores.
   * mod_frame is true when the current frame is one where a score was computed.
   * scored_frame is the frame index where the score was actually computed.
   */
  if (self->vmaf_config_subsample <= 1) {
    mod_frame = TRUE;
  } else {
    mod_frame = (frame_index % self->vmaf_config_subsample) == 1;
  }
  scored_frame = frame_index - 2;

  if ((!self->vmaf_config_frame_messaging)
      || frame_index <= 0 || !mod_frame) {
    GST_LOG_OBJECT (self,
        "Skipping frame vmaf score posting. frame:%d", frame_index);
    return TRUE;
  }

  err =
      vmaf_score_at_index (self->vmaf_ctx, self->vmaf_model,
      &vmaf_score, scored_frame);
  if (err) {
    GST_WARNING_OBJECT (self,
        "could not calculate vmaf score on frame:%d err:%d", scored_frame, err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self,
      "posting frame vmaf score. score:%f frame:%d", vmaf_score, scored_frame);

  GST_OBJECT_LOCK (agg->srcpad);
  segment = &GST_AGGREGATOR_PAD (agg->srcpad)->segment;
  timestamp = segment->position;

  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    if (GST_VIDEO_INFO_FPS_N (&GST_VIDEO_AGGREGATOR_PAD (self->ref_pad)->info) >
        0) {
      duration =
          gst_util_uint64_scale (1,
          GST_SECOND *
          GST_VIDEO_INFO_FPS_D (&GST_VIDEO_AGGREGATOR_PAD (self->
                  ref_pad)->info),
          GST_VIDEO_INFO_FPS_N (&GST_VIDEO_AGGREGATOR_PAD (self->
                  ref_pad)->info));
    } else {
      duration = GST_CLOCK_TIME_NONE;
    }

    running_time = gst_segment_to_running_time (segment, GST_FORMAT_TIME,
        timestamp);
    stream_time = gst_segment_to_stream_time (segment, GST_FORMAT_TIME,
        timestamp);
  } else {
    duration = GST_CLOCK_TIME_NONE;
    running_time = GST_CLOCK_TIME_NONE;
    stream_time = GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (agg->srcpad);

  vmaf_message_structure = gst_structure_new_empty ("VMAF");
  vmaf_message = gst_message_new_element (GST_OBJECT (self),
      vmaf_message_structure);

  gst_structure_set (vmaf_message_structure,
      "timestamp", G_TYPE_UINT64, timestamp,
      "stream-time", G_TYPE_UINT64, stream_time,
      "running-time", G_TYPE_UINT64, running_time,
      "duration", G_TYPE_UINT64, duration,
      "score", G_TYPE_DOUBLE, vmaf_score,
      "index", G_TYPE_INT, scored_frame, "type", G_TYPE_STRING, "frame", NULL);

  if (self->vmaf_config_ms_ssim) {
    gst_vmaf_add_feature_score (self,
        vmaf_message_structure, "float_ms_ssim", "ms-ssim", scored_frame);
  }
  if (self->vmaf_config_ssim) {
    gst_vmaf_add_feature_score (self,
        vmaf_message_structure, "float_ssim", "ssim", scored_frame);
  }
  if (self->vmaf_config_psnr) {
    gst_vmaf_add_feature_score (self, vmaf_message_structure,
        "psnr_y", "psnr-y", scored_frame);
  }
  if (!gst_element_post_message (GST_ELEMENT (self), vmaf_message)) {
    GST_WARNING_OBJECT (self,
        "could not post frame VMAF on message bus. score:%f frame:%d",
        vmaf_score, scored_frame);
    return FALSE;
  }

  return TRUE;
}

static void
gst_vmaf_fill_picture (VmafPicture * dst, guint8 * src, unsigned width,
    unsigned height, int src_stride)
{
  guint8 *a = src;
  uint8_t *b = dst->data[0];
  for (unsigned i = 0; i < height; i++) {
    memcpy (b, a, width);
    a += src_stride;
    b += dst->stride[0];
  }
}

static void
gst_vmaf_process_frame (GstVmaf * self, GstVideoFrame * ref_frame,
    GstVideoFrame * dist_frame)
{
  gint err = 0;
  VmafPicture pic_ref, pic_dist;
  gint frame_index = self->processed_frames;
  guint8 *ref_data, *dist_data;

  // allocate vmaf pictures
  err =
      vmaf_picture_alloc (&pic_ref, self->pix_fmt, ref_frame->info.finfo->bits,
      ref_frame->info.width, ref_frame->info.height);
  if (err) {
    GST_ERROR_OBJECT (self,
        "failed to allocate reference picture VMAF picture memory");
    goto end;
  }
  err =
      vmaf_picture_alloc (&pic_dist, self->pix_fmt,
      dist_frame->info.finfo->bits, dist_frame->info.width,
      dist_frame->info.height);
  if (err) {
    vmaf_picture_unref (&pic_ref);
    GST_ERROR_OBJECT (self,
        "failed to allocate distorted picture VMAF picture memory");
    goto end;
  }

  ref_data = ref_frame->map->data;
  dist_data = dist_frame->map->data;

  // vmaf only uses luma data, so we only fill that plane
  gst_vmaf_fill_picture (&pic_ref, ref_data, ref_frame->info.width,
      ref_frame->info.height, ref_frame->info.stride[0]);
  gst_vmaf_fill_picture (&pic_dist, dist_data, dist_frame->info.width,
      dist_frame->info.height, dist_frame->info.stride[0]);

  //read pictures, run calculation
  GST_DEBUG_OBJECT (self,
      "reading images into vmaf context. ref:%p dist:%p frame:%d",
      &pic_ref, &pic_dist, frame_index);

  err = vmaf_read_pictures (self->vmaf_ctx, &pic_ref, &pic_dist, frame_index);
  self->processed_frames++;
  if (err != 0) {
    vmaf_picture_unref (&pic_ref);
    vmaf_picture_unref (&pic_dist);
    GST_ERROR_OBJECT (self, "failed to read VMAF pictures into context");
  }
end:
  return;
}

static GstFlowReturn
gst_vmaf_create_output_buffer (GstVideoAggregator * videoaggregator,
    GstBuffer ** outbuffer)
{
  GstVmaf *self = GST_VMAF (videoaggregator);
  GstBuffer *current_buf;

  current_buf =
      gst_video_aggregator_pad_get_current_buffer (GST_VIDEO_AGGREGATOR_PAD
      (self->ref_pad));

  if (current_buf == NULL) {
    if (gst_aggregator_pad_is_eos (GST_AGGREGATOR_PAD (self->ref_pad))) {
      GST_INFO_OBJECT (self, "Reference pad is EOS, forwarding EOS");
      return GST_FLOW_EOS;
    }
    GST_ERROR_OBJECT (self, "No frame available on reference pad.");
    return GST_FLOW_ERROR;
  }

  *outbuffer = gst_buffer_ref (current_buf);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vmaf_aggregate_frames (GstVideoAggregator * vagg, GstBuffer * outbuf)
{
  GstVmaf *self = GST_VMAF (vagg);
  GstVideoFrame *ref_frame = NULL;
  GstVideoFrame *dist_frame = NULL;

  GST_DEBUG_OBJECT (self, "frames are prepared and ready for processing");

  ref_frame = gst_video_aggregator_pad_get_prepared_frame (self->ref_pad);
  dist_frame = gst_video_aggregator_pad_get_prepared_frame (self->dist_pad);

  if (ref_frame == NULL) {
    GST_ERROR_OBJECT (self,
        "No frame available on reference pad but not EOS yet");
  }

  if (dist_frame == NULL) {
    if (gst_aggregator_pad_is_eos (GST_AGGREGATOR_PAD (self->dist_pad))) {
      GST_INFO_OBJECT (self,
          "Distorted pad is EOS, skipping VMAF processing for remaining frames");
      return GST_FLOW_OK;
    } else {
      GST_ERROR_OBJECT (self,
          "No frame available on distorted pad but not EOS yet");
      return GST_FLOW_ERROR;
    }
  }

  if (G_UNLIKELY (!self->initialized)) {
    gst_vmaf_context_init (self);
  }

  gst_vmaf_process_frame (self, ref_frame, dist_frame);
  gst_vmaf_post_frame_score (self, self->processed_frames);

  return GST_FLOW_OK;
}

static void
gst_vmaf_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstVmaf *self = GST_VMAF (object);

  GST_OBJECT_LOCK (self);
  switch (prop_id) {
    case PROP_MODEL_FILENAME:
      g_free (self->vmaf_config_model_filename);
      self->vmaf_config_model_filename = g_value_dup_string (value);
      break;
    case PROP_DISABLE_CLIP:
      self->vmaf_config_disable_clip = g_value_get_boolean (value);
      break;
    case PROP_ENABLE_TRANSFORM:
      self->vmaf_config_enable_transform = g_value_get_boolean (value);
      break;
    case PROP_PHONE_MODEL:
      self->vmaf_config_phone_model = g_value_get_boolean (value);
      break;
    case PROP_PSNR:
      self->vmaf_config_psnr = g_value_get_boolean (value);
      break;
    case PROP_SSIM:
      self->vmaf_config_ssim = g_value_get_boolean (value);
      break;
    case PROP_MS_SSIM:
      self->vmaf_config_ms_ssim = g_value_get_boolean (value);
      break;
    case PROP_POOL_METHOD:
      self->vmaf_config_pool_method = g_value_get_enum (value);
      break;
    case PROP_NUM_THREADS:
      self->vmaf_config_num_threads = g_value_get_uint (value);
      break;
    case PROP_SUBSAMPLE:
      self->vmaf_config_subsample = g_value_get_uint (value);
      break;
    case PROP_CONF_INT:
      self->vmaf_config_conf_int = g_value_get_boolean (value);
      break;
    case PROP_FRAME_MESSAGING:
      self->vmaf_config_frame_messaging = g_value_get_boolean (value);
      break;
    case PROP_VMAF_RESULTS_FORMAT:
      self->vmaf_config_results_format = g_value_get_enum (value);
      break;
    case PROP_VMAF_RESULTS_FILENAME:
      g_free (self->vmaf_config_results_filename);
      self->vmaf_config_results_filename = g_value_dup_string (value);
      break;
    case PROP_LOG_LEVEL:
      self->vmaf_config_log_level = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_vmaf_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVmaf *self = GST_VMAF (object);

  GST_OBJECT_LOCK (self);
  switch (prop_id) {
    case PROP_MODEL_FILENAME:
      g_value_set_string (value, self->vmaf_config_model_filename);
      break;
    case PROP_DISABLE_CLIP:
      g_value_set_boolean (value, self->vmaf_config_disable_clip);
      break;
    case PROP_ENABLE_TRANSFORM:
      g_value_set_boolean (value, self->vmaf_config_enable_transform);
      break;
    case PROP_PHONE_MODEL:
      g_value_set_boolean (value, self->vmaf_config_phone_model);
      break;
    case PROP_PSNR:
      g_value_set_boolean (value, self->vmaf_config_psnr);
      break;
    case PROP_SSIM:
      g_value_set_boolean (value, self->vmaf_config_ssim);
      break;
    case PROP_MS_SSIM:
      g_value_set_boolean (value, self->vmaf_config_ms_ssim);
      break;
    case PROP_POOL_METHOD:
      g_value_set_enum (value, self->vmaf_config_pool_method);
      break;
    case PROP_NUM_THREADS:
      g_value_set_uint (value, self->vmaf_config_num_threads);
      break;
    case PROP_SUBSAMPLE:
      g_value_set_uint (value, self->vmaf_config_subsample);
      break;
    case PROP_CONF_INT:
      g_value_set_boolean (value, self->vmaf_config_conf_int);
      break;
    case PROP_FRAME_MESSAGING:
      g_value_set_boolean (value, self->vmaf_config_frame_messaging);
      break;
    case PROP_VMAF_RESULTS_FORMAT:
      g_value_set_enum (value, self->vmaf_config_results_format);;
      break;
    case PROP_VMAF_RESULTS_FILENAME:
      g_value_set_string (value, self->vmaf_config_results_filename);
      break;
    case PROP_LOG_LEVEL:
      g_value_set_enum (value, self->vmaf_config_log_level);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_vmaf_init (GstVmaf * self)
{
  GstPadTemplate *ref_template = gst_static_pad_template_get (&ref_factory);
  GstPadTemplate *dist_template = gst_static_pad_template_get (&dist_factory);
  self->vmaf_config_model_filename = g_strdup (DEFAULT_MODEL_FILENAME);
  self->vmaf_config_disable_clip = DEFAULT_DISABLE_CLIP;
  self->vmaf_config_enable_transform = DEFAULT_ENABLE_TRANSFORM;
  self->vmaf_config_phone_model = DEFAULT_PHONE_MODEL;
  self->vmaf_config_psnr = DEFAULT_PSNR;
  self->vmaf_config_ssim = DEFAULT_SSIM;
  self->vmaf_config_ms_ssim = DEFAULT_MS_SSIM;
  self->vmaf_config_num_threads = DEFAULT_NUM_THREADS;
  self->vmaf_config_subsample = DEFAULT_SUBSAMPLE;
  self->vmaf_config_conf_int = DEFAULT_CONF_INT;
  self->vmaf_config_pool_method = DEFAULT_POOL_METHOD;
  self->vmaf_config_frame_messaging = DEFAULT_FRAME_MESSAGING;
  self->vmaf_config_results_filename = DEFAULT_VMAF_RESULTS_FILENAME;
  self->vmaf_config_results_format = DEFAULT_VMAF_RESULTS_FORMAT;
  self->vmaf_config_log_level = DEFAULT_VMAF_LOG_LEVEL;
  self->initialized = FALSE;

  self->ref_pad =
      GST_VIDEO_AGGREGATOR_PAD (g_object_new (gst_video_aggregator_pad_get_type
          (), "name", "ref_sink", "direction", GST_PAD_SINK, "template",
          ref_template, NULL));
  gst_element_add_pad (GST_ELEMENT (self), GST_PAD (self->ref_pad));
  gst_object_unref (ref_template);

  self->dist_pad =
      GST_VIDEO_AGGREGATOR_PAD (g_object_new (gst_video_aggregator_pad_get_type
          (), "name", "dist_sink", "direction", GST_PAD_SINK, "template",
          dist_template, NULL));
  gst_element_add_pad (GST_ELEMENT (self), GST_PAD (self->dist_pad));
  gst_object_unref (dist_template);
}

static void
gst_vmaf_finalize (GObject * object)
{
  GstVmaf *self = GST_VMAF (object);
  GST_DEBUG_OBJECT (self, "finalize plugin called, freeing memory");
  g_free (self->vmaf_config_model_filename);
  self->vmaf_config_model_filename = NULL;
  g_free (self->vmaf_config_results_filename);
  self->vmaf_config_results_filename = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_vmaf_sink_event (GstAggregator * aggregator,
    GstAggregatorPad * aggregator_pad, GstEvent * event)
{
  GstVmaf *self = GST_VMAF (aggregator);

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    GST_DEBUG_OBJECT (self, "Received EOS on pad %s",
        GST_PAD_NAME (aggregator_pad));
    if (GST_VIDEO_AGGREGATOR_PAD (aggregator_pad) == self->ref_pad) {
      gst_vmaf_context_flush (self);
      if (self->vmaf_ctx != NULL) {
        gst_vmaf_post_pooled_score (self);
      }
    }
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_event (aggregator,
      aggregator_pad, event);
}

static gboolean
gst_vmaf_start (GstAggregator * agg)
{
  GstVmaf *self = GST_VMAF (agg);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (self, "Starting vmaf");

  return ret;
}

static gboolean
gst_vmaf_stop (GstAggregator * agg)
{
  GstVmaf *self = GST_VMAF (agg);

  gst_vmaf_context_free (self);

  GST_DEBUG_OBJECT (self, "Stopping vmaf element.");

  return TRUE;
}

static gboolean
gst_vmaf_flush (GstAggregator * agg)
{
  GstVmaf *self = GST_VMAF (agg);
  gboolean ret = TRUE;
  GST_DEBUG_OBJECT (self, "Flushing vmaf element.");

  ret &= gst_vmaf_context_flush (self);
  ret &= gst_vmaf_post_pooled_score (self);

  return ret;
}

static void
gst_vmaf_class_init (GstVmafClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstVideoAggregatorClass *videoaggregator_class =
      (GstVideoAggregatorClass *) klass;
  GstAggregatorClass *aggregator_class = (GstAggregatorClass *) klass;

  videoaggregator_class->aggregate_frames = gst_vmaf_aggregate_frames;
  videoaggregator_class->create_output_buffer = gst_vmaf_create_output_buffer;

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &src_factory, GST_TYPE_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &ref_factory, GST_TYPE_VIDEO_AGGREGATOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &dist_factory, GST_TYPE_VIDEO_AGGREGATOR_PAD);

  aggregator_class->sink_event = gst_vmaf_sink_event;
  aggregator_class->start = gst_vmaf_start;
  aggregator_class->stop = gst_vmaf_stop;
  aggregator_class->flush = gst_vmaf_flush;

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_vmaf_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_vmaf_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_vmaf_finalize);

  g_object_class_install_property (gobject_class, PROP_MODEL_FILENAME,
      g_param_spec_string ("model-filename",
          "model-filename",
          "Model *.pkl abs filename, or file version for built in models",
          DEFAULT_MODEL_FILENAME, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_DISABLE_CLIP,
      g_param_spec_boolean ("disable-clip",
          "disable-clip",
          "Disable clipping VMAF values",
          DEFAULT_DISABLE_CLIP, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_ENABLE_TRANSFORM,
      g_param_spec_boolean ("enable-transform",
          "enable-transform",
          "Enable transform VMAF scores",
          DEFAULT_ENABLE_TRANSFORM, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PHONE_MODEL,
      g_param_spec_boolean ("phone-model",
          "phone-model",
          "Use VMAF phone model", DEFAULT_PHONE_MODEL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PSNR,
      g_param_spec_boolean ("psnr", "psnr",
          "Estimate PSNR", DEFAULT_PSNR, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SSIM,
      g_param_spec_boolean ("ssim", "ssim",
          "Estimate SSIM", DEFAULT_SSIM, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_MS_SSIM,
      g_param_spec_boolean ("ms-ssim", "ms-ssim",
          "Estimate MS-SSIM", DEFAULT_MS_SSIM, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_POOL_METHOD,
      g_param_spec_enum ("pool-method", "pool-method",
          "Pool method for mean", GST_TYPE_VMAF_POOL_METHOD,
          DEFAULT_POOL_METHOD,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_NUM_THREADS,
      g_param_spec_uint ("threads", "threads",
          "The number of threads",
          0, G_MAXINT, DEFAULT_NUM_THREADS, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_SUBSAMPLE,
      g_param_spec_uint ("subsample",
          "subsample",
          "Computing on one of every N frames",
          1, 128, DEFAULT_SUBSAMPLE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_CONF_INT,
      g_param_spec_boolean ("conf-interval",
          "conf-interval",
          "Enable confidence intervals", DEFAULT_CONF_INT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_FRAME_MESSAGING,
      g_param_spec_boolean ("frame-message",
          "frame-message",
          "Enable frame level score messaging", DEFAULT_FRAME_MESSAGING,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_VMAF_RESULTS_FILENAME,
      g_param_spec_string ("results-filename",
          "results-filename",
          "VMAF results filename for scores",
          DEFAULT_VMAF_RESULTS_FILENAME, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_VMAF_RESULTS_FORMAT,
      g_param_spec_enum ("results-format", "results-format",
          "VMAF results file format used for scores (csv, xml, json)",
          GST_TYPE_VMAF_OUTPUT_FORMATS, DEFAULT_VMAF_RESULTS_FORMAT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_LOG_LEVEL,
      g_param_spec_enum ("log-level", "(internal) VMAF log level",
          "VMAF log level", GST_TYPE_VMAF_LOG_LEVEL,
          DEFAULT_VMAF_LOG_LEVEL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class, "vmaf",
      "Filter/Analyzer/Video",
      "Provides Video Multi-Method Assessment Fusion metric",
      "Casey Bateman <casey.bateman@hudl.com>, Andoni Morales <amorales@fluendo.com>, Diego Nieto <dnieto@fluendo.com>");
  GST_DEBUG_CATEGORY_INIT (gst_vmaf_debug, "vmaf", 0, "vmaf");

  gst_type_mark_as_plugin_api (GST_VMAF_RESULTS_FORMAT_TYPE, 0);
  gst_type_mark_as_plugin_api (GST_VMAF_POOL_METHOD_TYPE, 0);
  gst_type_mark_as_plugin_api (GST_VMAF_LOG_LEVEL_TYPE, 0);
}
