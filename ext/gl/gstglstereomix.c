/*
 * Combine video streams to 3D stereo
 *
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2014 Jan Schmidt <jan@noraisin.net>
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

#include "gstglstereomix.h"

#define GST_CAT_DEFAULT gst_gl_stereo_mix_debug
GST_DEBUG_CATEGORY (gst_gl_stereo_mix_debug);

#define gst_gl_stereo_mix_parent_class parent_class
G_DEFINE_TYPE (GstGLStereoMix, gst_gl_stereo_mix, GST_TYPE_GL_MIXER);

static GstCaps *_update_caps (GstVideoAggregator * vagg, GstCaps * caps);
static gboolean _negotiated_caps (GstVideoAggregator * videoaggregator,
    GstCaps * caps);
gboolean gst_gl_stereo_mix_make_output (GstGLStereoMix * mix);
static gboolean gst_gl_stereo_mix_process_frames (GstGLStereoMix * mixer,
    GPtrArray * in_frames);

#define DEFAULT_DOWNMIX GST_GL_STEREO_DOWNMIX_ANAGLYPH_GREEN_MAGENTA_DUBOIS

/* GLStereoMix signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_DOWNMIX_MODE
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_GL_MEMORY,
            "RGBA") "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META,
            "RGBA")
        "; " GST_VIDEO_CAPS_MAKE (GST_GL_COLOR_CONVERT_FORMATS))
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_GL_MEMORY,
            "RGBA") "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META,
            "RGBA")
        "; " GST_VIDEO_CAPS_MAKE (GST_GL_COLOR_CONVERT_FORMATS))
    );

static GstFlowReturn gst_gl_stereo_mix_get_output_buffer (GstVideoAggregator *
    videoaggregator, GstBuffer ** outbuf);
static gboolean gst_gl_stereo_mix_stop (GstAggregator * agg);
static gboolean gst_gl_stereo_mix_start (GstAggregator * agg);
static gboolean gst_gl_stereo_mix_src_query (GstAggregator * agg,
    GstQuery * query);

static void
gst_gl_stereo_mix_find_best_format (GstVideoAggregator * vagg,
    GstCaps * downstream_caps, GstVideoInfo * best_info,
    gboolean * at_least_one_alpha);

static void gst_gl_stereo_mix_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_stereo_mix_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_stereo_mix_finalize (GObject * object);

static GstFlowReturn
gst_gl_stereo_mix_aggregate_frames (GstVideoAggregator * vagg,
    GstBuffer * outbuffer);

static void
gst_gl_stereo_mix_class_init (GstGLStereoMixClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoAggregatorClass *videoaggregator_class =
      (GstVideoAggregatorClass *) klass;
  GstAggregatorClass *agg_class = (GstAggregatorClass *) klass;
  GstGLBaseMixerClass *base_mix_class = (GstGLBaseMixerClass *) klass;

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "glstereomixer", 0,
      "opengl stereoscopic mixer");

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_gl_stereo_mix_finalize);

  gobject_class->get_property = gst_gl_stereo_mix_get_property;
  gobject_class->set_property = gst_gl_stereo_mix_set_property;

  gst_element_class_set_metadata (element_class, "OpenGL stereo video combiner",
      "Filter/Effect/Video", "OpenGL stereo video combiner",
      "Jan Schmidt <jan@centricular.com>");

  g_object_class_install_property (gobject_class, PROP_DOWNMIX_MODE,
      g_param_spec_enum ("downmix-mode", "Mode for mono downmixed output",
          "Output anaglyph type to generate when downmixing to mono",
          GST_TYPE_GL_STEREO_DOWNMIX_MODE_TYPE, DEFAULT_DOWNMIX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  agg_class->stop = gst_gl_stereo_mix_stop;
  agg_class->start = gst_gl_stereo_mix_start;
  agg_class->src_query = gst_gl_stereo_mix_src_query;

  videoaggregator_class->aggregate_frames = gst_gl_stereo_mix_aggregate_frames;
  videoaggregator_class->update_caps = _update_caps;
  videoaggregator_class->negotiated_caps = _negotiated_caps;
  videoaggregator_class->get_output_buffer =
      gst_gl_stereo_mix_get_output_buffer;
  videoaggregator_class->find_best_format = gst_gl_stereo_mix_find_best_format;
  videoaggregator_class->preserve_update_caps_result = TRUE;

  base_mix_class->supported_gl_api = GST_GL_API_OPENGL | GST_GL_API_OPENGL3;
}

static void
gst_gl_stereo_mix_init (GstGLStereoMix * mix)
{
}

static void
gst_gl_stereo_mix_finalize (GObject * object)
{
  //GstGLStereoMix *mix = GST_GL_STEREO_MIX (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_gl_stereo_mix_query_caps (GstPad * pad, GstAggregator * agg,
    GstQuery * query)
{
  GstCaps *filter, *caps;

  gst_query_parse_caps (query, &filter);

  caps = gst_pad_get_current_caps (agg->srcpad);
  if (caps == NULL) {
    caps = gst_pad_get_pad_template_caps (agg->srcpad);
  }

  if (filter)
    caps = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);

  gst_query_set_caps_result (query, caps);
  gst_caps_unref (caps);

  return TRUE;
}

static gboolean
gst_gl_stereo_mix_src_query (GstAggregator * agg, GstQuery * query)
{
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
      return gst_gl_stereo_mix_query_caps (agg->srcpad, agg, query);
      break;
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_query (agg, query);
}


static GstFlowReturn
gst_gl_stereo_mix_get_output_buffer (GstVideoAggregator * videoaggregator,
    GstBuffer ** outbuf)
{
  GstGLStereoMix *mix = GST_GL_STEREO_MIX (videoaggregator);
  GstFlowReturn ret = GST_FLOW_OK;

#if 0

  if (!mix->priv->pool_active) {
    if (!gst_buffer_pool_set_active (mix->priv->pool, TRUE)) {
      GST_ELEMENT_ERROR (mix, RESOURCE, SETTINGS,
          ("failed to activate bufferpool"), ("failed to activate bufferpool"));
      return GST_FLOW_ERROR;
    }
    mix->priv->pool_active = TRUE;
  }

  return gst_buffer_pool_acquire_buffer (mix->priv->pool, outbuf, NULL);
#endif

  if (!gst_gl_stereo_mix_make_output (mix)) {
    gst_buffer_replace (&mix->primary_out, NULL);
    gst_buffer_replace (&mix->auxilliary_out, NULL);
    GST_ELEMENT_ERROR (mix, RESOURCE, SETTINGS,
        ("Failed to generate output"), ("failed to generate output"));
    ret = GST_FLOW_ERROR;
  }

  if (mix->auxilliary_out) {
    *outbuf = mix->auxilliary_out;
    mix->auxilliary_out = NULL;
  } else {
    *outbuf = mix->primary_out;
    mix->primary_out = NULL;
  }
  return ret;
}

gboolean
gst_gl_stereo_mix_make_output (GstGLStereoMix * mix)
{
  guint i;
  GList *walk;
  gboolean res = FALSE;
  guint array_index = 0;
  GstElement *element = GST_ELEMENT (mix);
  gboolean missing_buffer = FALSE;

  GST_LOG_OBJECT (mix, "Processing buffers");

  GST_OBJECT_LOCK (mix);
  walk = element->sinkpads;

  i = mix->frames->len;
  g_ptr_array_set_size (mix->frames, element->numsinkpads);
  for (; i < element->numsinkpads; i++)
    mix->frames->pdata[i] = g_slice_new0 (GstGLStereoMixFrameData);
  while (walk) {
    GstGLMixerPad *pad = GST_GL_MIXER_PAD (walk->data);
    GstVideoAggregatorPad *vaggpad = walk->data;
    GstGLStereoMixFrameData *frame;

    GST_LOG_OBJECT (mix, "Checking pad %" GST_PTR_FORMAT, vaggpad);

    frame = g_ptr_array_index (mix->frames, array_index);
    frame->base.pad = pad;
    frame->buf = NULL;

    walk = g_list_next (walk);

    if (vaggpad->buffer != NULL) {
      frame->buf = vaggpad->buffer;

      GST_DEBUG_OBJECT (pad, "Got buffer %" GST_PTR_FORMAT, frame->buf);
    } else {
      GST_LOG_OBJECT (mix, "No buffer on pad %" GST_PTR_FORMAT, vaggpad);
      missing_buffer = TRUE;
    }
    ++array_index;
  }
  if (missing_buffer) {
    /* We're still waiting for a buffer to turn up on at least one input */
    GST_WARNING_OBJECT (mix, "Not generating output - need more input buffers");
    res = TRUE;
    goto out;
  }

  /* Copy GL memory from each input frame to the output */
  if (!gst_gl_stereo_mix_process_frames (mix, mix->frames)) {
    GST_LOG_OBJECT (mix, "Failed to process frames to output");
    goto out;
  }

  if (mix->primary_out == NULL)
    goto out;

  res = TRUE;

out:
  GST_OBJECT_UNLOCK (mix);

  return res;
}

static GstFlowReturn
gst_gl_stereo_mix_aggregate_frames (GstVideoAggregator * vagg,
    GstBuffer * outbuf)
{
  GstGLStereoMix *mix = GST_GL_STEREO_MIX (vagg);
  /* If we're operating in frame-by-frame mode, push
   * the primary view now, and let the parent class
   * push the remaining auxilliary view */
  if (GST_VIDEO_INFO_MULTIVIEW_MODE (&vagg->info) ==
      GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME) {
    /* Transfer the timestamps video-agg put on the aux buffer */
    gst_buffer_copy_into (mix->primary_out, outbuf,
        GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
    gst_aggregator_finish_buffer (GST_AGGREGATOR (vagg), mix->primary_out);
    mix->primary_out = NULL;

    /* And actually, we don't want timestamps on the aux buffer */
    GST_BUFFER_TIMESTAMP (outbuf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (outbuf) = GST_CLOCK_TIME_NONE;
  }
  return GST_FLOW_OK;
}

static void
gst_gl_stereo_mix_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstGLStereoMix *mix = GST_GL_STEREO_MIX (object);

  switch (prop_id) {
    case PROP_DOWNMIX_MODE:
      g_value_set_enum (value, mix->downmix_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_stereo_mix_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstGLStereoMix *mix = GST_GL_STEREO_MIX (object);

  switch (prop_id) {
    case PROP_DOWNMIX_MODE:
      mix->downmix_mode = g_value_get_enum (value);
      if (mix->viewconvert)
        g_object_set_property (G_OBJECT (mix->viewconvert), "downmix-mode",
            value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
_free_glmixer_frame_data (GstGLStereoMixFrameData * frame)
{
  if (frame == NULL)
    return;
  if (frame->buf)
    gst_buffer_unref (frame->buf);
  g_slice_free1 (sizeof (GstGLStereoMixFrameData), frame);
}

static gboolean
gst_gl_stereo_mix_start (GstAggregator * agg)
{
  guint i;
  GstGLStereoMix *mix = GST_GL_STEREO_MIX (agg);
  GstElement *element = GST_ELEMENT (agg);

  if (!GST_AGGREGATOR_CLASS (parent_class)->start (agg))
    return FALSE;

  GST_OBJECT_LOCK (mix);
  mix->array_buffers = g_ptr_array_new_full (element->numsinkpads,
      (GDestroyNotify) _free_glmixer_frame_data);
  mix->frames = g_ptr_array_new_full (element->numsinkpads, NULL);

  g_ptr_array_set_size (mix->array_buffers, element->numsinkpads);
  g_ptr_array_set_size (mix->frames, element->numsinkpads);

  for (i = 0; i < element->numsinkpads; i++)
    mix->frames->pdata[i] = g_slice_new0 (GstGLStereoMixFrameData);

  mix->viewconvert = gst_gl_view_convert_new ();
  g_object_set (G_OBJECT (mix->viewconvert), "downmix-mode",
      mix->downmix_mode, NULL);

  GST_OBJECT_UNLOCK (mix);

  return TRUE;
}

static gboolean
gst_gl_stereo_mix_stop (GstAggregator * agg)
{
  GstGLStereoMix *mix = GST_GL_STEREO_MIX (agg);

  if (!GST_AGGREGATOR_CLASS (parent_class)->stop (agg))
    return FALSE;

  GST_OBJECT_LOCK (agg);
  g_ptr_array_free (mix->frames, TRUE);
  mix->frames = NULL;
  g_ptr_array_free (mix->array_buffers, TRUE);
  mix->array_buffers = NULL;
  GST_OBJECT_UNLOCK (agg);

  if (mix->viewconvert) {
    gst_object_unref (mix->viewconvert);
    mix->viewconvert = NULL;
  }

  return TRUE;
}

/* Convert to caps that can be accepted by this element... */
static GstCaps *
get_converted_caps (GstGLStereoMix * mix, GstCaps * caps)
{
#if 0
  GstGLContext *context = GST_GL_BASE_MIXER (mix)->context;
  GstCaps *result, *tmp;

  GST_LOG_OBJECT (mix, "Converting caps %" GST_PTR_FORMAT, caps);
  result = gst_gl_upload_transform_caps (context, GST_PAD_SINK, caps, NULL);
  tmp = result;
  GST_TRACE_OBJECT (mix, "transfer returned caps %" GST_PTR_FORMAT, tmp);

  result =
      gst_gl_color_convert_transform_caps (context, GST_PAD_SINK, tmp, NULL);
  gst_caps_unref (tmp);
  GST_TRACE_OBJECT (mix, "convert returned caps %" GST_PTR_FORMAT, tmp);

  tmp = result;
  result = gst_gl_view_convert_transform_caps (mix->viewconvert,
      GST_PAD_SINK, tmp, NULL);
  gst_caps_unref (tmp);
#else
  GstCaps *result;

  GST_LOG_OBJECT (mix, "Converting caps %" GST_PTR_FORMAT, caps);
  result = gst_gl_view_convert_transform_caps (mix->viewconvert,
      GST_PAD_SINK, caps, NULL);
#endif

  GST_LOG_OBJECT (mix, "returning caps %" GST_PTR_FORMAT, result);

  return result;
}

/* Return the possible output caps we decided in find_best_format() */
static GstCaps *
_update_caps (GstVideoAggregator * vagg, GstCaps * caps)
{
  GstGLStereoMix *mix = GST_GL_STEREO_MIX (vagg);

  return gst_caps_ref (mix->out_caps);
}

/* Called after videoaggregator fixates our caps */
static gboolean
_negotiated_caps (GstVideoAggregator * vagg, GstCaps * caps)
{
  GstGLStereoMix *mix = GST_GL_STEREO_MIX (vagg);

  GST_LOG_OBJECT (mix, "Configured output caps %" GST_PTR_FORMAT, caps);

  if (GST_VIDEO_AGGREGATOR_CLASS (parent_class)->negotiated_caps)
    if (!GST_VIDEO_AGGREGATOR_CLASS (parent_class)->negotiated_caps (vagg,
            caps))
      return FALSE;

  /* Update the glview_convert output */
  if (!gst_video_info_from_caps (&mix->out_info, caps))
    return FALSE;

  /* We can configure the view_converter now */
  gst_gl_view_convert_set_context (mix->viewconvert,
      GST_GL_BASE_MIXER (mix)->context);
  gst_gl_view_convert_set_format (mix->viewconvert, &mix->mix_info,
      &mix->out_info);

  return TRUE;

}

static gboolean
gst_gl_stereo_mix_process_frames (GstGLStereoMix * mixer, GPtrArray * frames)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (mixer);
  GstBuffer *converted_buffer, *inbuf;
  GstVideoInfo *out_info = &vagg->info;
  gint count = 0, n;
  gint v, views;
  gint valid_views = 0;

  inbuf = gst_buffer_new ();
  while (count < frames->len) {
    GstGLStereoMixFrameData *frame;
    GstMemory *in_mem;

    frame = g_ptr_array_index (frames, count);
    GST_LOG_OBJECT (mixer, "Handling frame %d", count);

    if (!frame) {
      GST_DEBUG ("skipping texture, null frame");
      count++;
      continue;
    }

    in_mem = gst_buffer_get_memory (frame->buf, 0);

    GST_LOG_OBJECT (mixer,
        "Appending memory %" GST_PTR_FORMAT " to intermediate buffer", in_mem);
    /* Appending the memory to a 2nd buffer locks it
     * exclusive a 2nd time, which will mark it for
     * copy-on-write. The ref will keep the memory
     * alive but we add a parent_buffer_meta to also
     * prevent the input buffer from returning to any buffer
     * pool it might belong to
     */
    gst_buffer_append_memory (inbuf, in_mem);
    /* Use parent buffer meta to keep input buffer alive */
    gst_buffer_add_parent_buffer_meta (inbuf, frame->buf);

    count++;
    valid_views++;
  }

  if (mixer->mix_info.views != valid_views) {
    GST_WARNING_OBJECT (mixer, "Not enough input views to process");
    return FALSE;
  }

  if (GST_VIDEO_INFO_MULTIVIEW_MODE (out_info) ==
      GST_VIDEO_MULTIVIEW_MODE_SEPARATED)
    views = out_info->views;
  else
    views = 1;

  if (gst_gl_view_convert_submit_input_buffer (mixer->viewconvert,
          FALSE, inbuf) != GST_FLOW_OK)
    return FALSE;

  /* Clear any existing buffers, just in case */
  gst_buffer_replace (&mixer->primary_out, NULL);
  gst_buffer_replace (&mixer->auxilliary_out, NULL);

  if (gst_gl_view_convert_get_output (mixer->viewconvert,
          &mixer->primary_out) != GST_FLOW_OK)
    return FALSE;

  if (GST_VIDEO_INFO_MULTIVIEW_MODE (out_info) ==
      GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME) {
    if (gst_gl_view_convert_get_output (mixer->viewconvert,
            &mixer->auxilliary_out) != GST_FLOW_OK)
      return FALSE;
  }

  if (mixer->primary_out == NULL)
    return FALSE;

  converted_buffer = mixer->primary_out;
  v = 0;
  n = gst_buffer_n_memory (converted_buffer);
  g_assert (n == GST_VIDEO_INFO_N_PLANES (out_info) * views);
  for (v = 0; v < views; v++) {
    gst_buffer_add_video_meta_full (converted_buffer, v,
        GST_VIDEO_INFO_FORMAT (out_info),
        GST_VIDEO_INFO_WIDTH (out_info),
        GST_VIDEO_INFO_HEIGHT (out_info),
        GST_VIDEO_INFO_N_PLANES (out_info), out_info->offset, out_info->stride);
    if (mixer->auxilliary_out) {
      gst_buffer_add_video_meta_full (mixer->auxilliary_out, v,
          GST_VIDEO_INFO_FORMAT (out_info),
          GST_VIDEO_INFO_WIDTH (out_info),
          GST_VIDEO_INFO_HEIGHT (out_info),
          GST_VIDEO_INFO_N_PLANES (out_info), out_info->offset,
          out_info->stride);
    }
  }

  return TRUE;
}

/* Iterate the input sink pads, and choose the blend format
 * we will generate before output conversion, which is RGBA
 * at some suitable size */
static void
gst_gl_stereo_mix_find_best_format (GstVideoAggregator * vagg,
    GstCaps * downstream_caps, GstVideoInfo * best_info,
    gboolean * at_least_one_alpha)
{
  GstGLStereoMix *mix = GST_GL_STEREO_MIX (vagg);
  GList *l;
  gint best_width = -1, best_height = -1;
  gdouble best_fps = -1, cur_fps;
  gint best_fps_n = 0, best_fps_d = 1;
  GstVideoInfo *mix_info;
  GstCaps *blend_caps, *tmp_caps;

  /* We'll deal with alpha internally, so just tell aggregator to
   * be quiet */
  *at_least_one_alpha = FALSE;

  GST_OBJECT_LOCK (vagg);

  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *pad = l->data;
    GstVideoInfo tmp = pad->info;
    gint this_width, this_height;
    gint fps_n, fps_d;

    if (!pad->info.finfo)
      continue;

    /* This can happen if we release a pad and another pad hasn't been negotiated_caps yet */
    if (GST_VIDEO_INFO_FORMAT (&pad->info) == GST_VIDEO_FORMAT_UNKNOWN)
      continue;

    /* Convert to per-view width/height for unpacked forms */
    gst_video_multiview_video_info_change_mode (&tmp,
        GST_VIDEO_MULTIVIEW_MODE_SEPARATED, GST_VIDEO_MULTIVIEW_FLAGS_NONE);

    this_width = GST_VIDEO_INFO_WIDTH (&tmp);
    this_height = GST_VIDEO_INFO_HEIGHT (&tmp);
    fps_n = GST_VIDEO_INFO_FPS_N (&tmp);
    fps_d = GST_VIDEO_INFO_FPS_D (&tmp);

    GST_INFO_OBJECT (vagg, "Input pad %" GST_PTR_FORMAT
        " w %u h %u", pad, this_width, this_height);

    if (this_width == 0 || this_height == 0)
      continue;

    if (best_width < this_width)
      best_width = this_width;
    if (best_height < this_height)
      best_height = this_height;

    if (fps_d == 0)
      cur_fps = 0.0;
    else
      gst_util_fraction_to_double (fps_n, fps_d, &cur_fps);

    if (best_fps < cur_fps) {
      best_fps = cur_fps;
      best_fps_n = fps_n;
      best_fps_d = fps_d;
    }

    /* FIXME: Preserve PAR for at least one input when different sized inputs */
  }
  GST_OBJECT_UNLOCK (vagg);

  mix_info = &mix->mix_info;
  gst_video_info_set_format (mix_info, GST_VIDEO_FORMAT_RGBA, best_width,
      best_height);

  GST_VIDEO_INFO_FPS_N (mix_info) = best_fps_n;
  GST_VIDEO_INFO_FPS_D (mix_info) = best_fps_d;

  GST_VIDEO_INFO_MULTIVIEW_MODE (mix_info) = GST_VIDEO_MULTIVIEW_MODE_SEPARATED;
  GST_VIDEO_INFO_VIEWS (mix_info) = 2;

  /* FIXME: If input is marked as flipped or flopped, preserve those flags */
  GST_VIDEO_INFO_MULTIVIEW_FLAGS (mix_info) = GST_VIDEO_MULTIVIEW_FLAGS_NONE;

  /* Choose our output format based on downstream preferences */
  blend_caps = gst_video_info_to_caps (mix_info);

  gst_caps_set_features (blend_caps, 0,
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_GL_MEMORY));

  tmp_caps = get_converted_caps (GST_GL_STEREO_MIX (vagg), blend_caps);
  gst_caps_unref (blend_caps);

  if (mix->out_caps)
    gst_caps_unref (mix->out_caps);

  mix->out_caps = gst_caps_intersect (downstream_caps, tmp_caps);
  gst_caps_unref (tmp_caps);

  GST_DEBUG_OBJECT (vagg, "Possible output caps %" GST_PTR_FORMAT,
      mix->out_caps);
  /* Tell videoaggregator our preferred size. Actual info gets
   * overridden during caps nego */
  *best_info = *mix_info;
}
