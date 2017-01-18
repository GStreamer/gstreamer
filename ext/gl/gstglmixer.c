/* Generic video mixer plugin
 *
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
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

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstglmixer.h"

#define gst_gl_mixer_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstGLMixer, gst_gl_mixer, GST_TYPE_GL_BASE_MIXER);

#define GST_CAT_DEFAULT gst_gl_mixer_debug
GST_DEBUG_CATEGORY (gst_gl_mixer_debug);

static void gst_gl_mixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_gl_mixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

enum
{
  PROP_PAD_0
};

#define GST_GL_MIXER_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_GL_MIXER, GstGLMixerPrivate))

struct _GstGLMixerPrivate
{
  gboolean negotiated;

  gboolean gl_resource_ready;
  GMutex gl_resource_lock;
  GCond gl_resource_cond;
};

G_DEFINE_TYPE (GstGLMixerPad, gst_gl_mixer_pad, GST_TYPE_GL_BASE_MIXER_PAD);

static void
gst_gl_mixer_pad_class_init (GstGLMixerPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstVideoAggregatorPadClass *vaggpad_class =
      (GstVideoAggregatorPadClass *) klass;

  gobject_class->set_property = gst_gl_mixer_pad_set_property;
  gobject_class->get_property = gst_gl_mixer_pad_get_property;

  vaggpad_class->set_info = NULL;
  vaggpad_class->prepare_frame = NULL;
  vaggpad_class->clean_frame = NULL;
}

static void
gst_gl_mixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_mixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
_negotiated_caps (GstVideoAggregator * vagg, GstCaps * caps)
{
  GstGLMixer *mix = GST_GL_MIXER (vagg);
  gboolean ret;

  mix->priv->negotiated = TRUE;

  gst_caps_replace (&mix->out_caps, caps);

  ret = GST_VIDEO_AGGREGATOR_CLASS (parent_class)->negotiated_caps (vagg, caps);

  return ret;
}

static void
_find_best_format (GstVideoAggregator * vagg, GstCaps * downstream_caps,
    GstVideoInfo * best_info, gboolean * at_least_one_alpha)
{
  GstVideoInfo tmp_info;

  GST_VIDEO_AGGREGATOR_CLASS (parent_class)->find_best_format (vagg,
      downstream_caps, best_info, at_least_one_alpha);

  gst_video_info_set_format (&tmp_info, GST_VIDEO_FORMAT_RGBA,
      best_info->width, best_info->height);
  tmp_info.par_n = best_info->par_n;
  tmp_info.par_d = best_info->par_d;
  tmp_info.fps_n = best_info->fps_n;
  tmp_info.fps_d = best_info->fps_d;
  tmp_info.flags = best_info->flags;
  tmp_info.interlace_mode = best_info->interlace_mode;
  *best_info = tmp_info;
}

static gboolean
gst_gl_mixer_propose_allocation (GstGLBaseMixer * base_mix,
    GstGLBaseMixerPad * base_pad, GstQuery * decide_query, GstQuery * query)
{
  GstGLMixer *mix = GST_GL_MIXER (base_mix);
  GstGLContext *context = base_mix->context;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint size = 0;
  gboolean need_pool;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if (need_pool) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps))
      goto invalid_caps;

    GST_DEBUG_OBJECT (mix, "create new pool");
    pool = gst_gl_buffer_pool_new (context);

    /* the normal size of a frame */
    size = info.size;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      g_object_unref (pool);
      goto config_failed;
    }

    gst_query_add_allocation_pool (query, pool, size, 1, 0);
    g_object_unref (pool);
  }

  /* we also support various metadata */
  if (context->gl_vtable->FenceSync)
    gst_query_add_allocation_meta (query, GST_GL_SYNC_META_API_TYPE, 0);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (mix, "no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_DEBUG_OBJECT (mix, "invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_DEBUG_OBJECT (mix, "failed setting config");
    return FALSE;
  }
}

static gboolean
gst_gl_mixer_pad_sink_acceptcaps (GstPad * pad, GstGLMixer * mix,
    GstCaps * caps)
{
  gboolean ret;
  GstCaps *template_caps;

  GST_DEBUG_OBJECT (pad, "try accept caps of %" GST_PTR_FORMAT, caps);

  template_caps = gst_pad_get_pad_template_caps (pad);
  template_caps = gst_caps_make_writable (template_caps);

  ret = gst_caps_can_intersect (caps, template_caps);
  GST_DEBUG_OBJECT (pad, "%saccepted caps %" GST_PTR_FORMAT,
      (ret ? "" : "not "), caps);
  gst_caps_unref (template_caps);

  return ret;
}

/* copies the given caps */
static GstCaps *
_update_caps (GstVideoAggregator * vagg, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp;
  guint i, n;

  if (filter) {
    tmp = gst_caps_intersect (caps, filter);
    tmp = gst_caps_make_writable (tmp);
  } else {
    tmp = gst_caps_copy (caps);
  }

  n = gst_caps_get_size (tmp);
  for (i = 0; i < n; i++) {
    gst_caps_set_features (tmp, i,
        gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_GL_MEMORY));
  }

  return tmp;
}

static GstCaps *
gst_gl_mixer_pad_sink_getcaps (GstPad * pad, GstGLMixer * mix, GstCaps * filter)
{
  GstCaps *sinkcaps;
  GstCaps *template_caps;
  GstCaps *filtered_caps;
  GstCaps *returned_caps;

  template_caps = gst_pad_get_pad_template_caps (pad);

  sinkcaps = gst_pad_get_current_caps (pad);
  if (sinkcaps == NULL) {
    sinkcaps = gst_caps_ref (template_caps);
  } else {
    sinkcaps = gst_caps_merge (sinkcaps, gst_caps_ref (template_caps));
  }

  if (filter) {
    filtered_caps = gst_caps_intersect (sinkcaps, filter);
    gst_caps_unref (sinkcaps);
  } else {
    filtered_caps = sinkcaps;   /* pass ownership */
  }

  returned_caps = gst_caps_intersect (filtered_caps, template_caps);

  gst_caps_unref (template_caps);
  gst_caps_unref (filtered_caps);

  GST_DEBUG_OBJECT (pad, "returning %" GST_PTR_FORMAT, returned_caps);

  return returned_caps;
}

static gboolean
gst_gl_mixer_sink_query (GstAggregator * agg, GstAggregatorPad * bpad,
    GstQuery * query)
{
  gboolean ret = FALSE;
  GstGLMixer *mix = GST_GL_MIXER (agg);

  GST_TRACE ("QUERY %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_gl_mixer_pad_sink_getcaps (GST_PAD (bpad), mix, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;

      gst_query_parse_accept_caps (query, &caps);
      ret = gst_gl_mixer_pad_sink_acceptcaps (GST_PAD (bpad), mix, caps);
      gst_query_set_accept_caps_result (query, ret);
      ret = TRUE;
      break;
    }
    default:
      ret = GST_AGGREGATOR_CLASS (parent_class)->sink_query (agg, bpad, query);
      break;
  }

  return ret;
}

static void
gst_gl_mixer_pad_init (GstGLMixerPad * mixerpad)
{
}

/* GLMixer signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_GL_MEMORY,
            "RGBA"))
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_GL_MEMORY,
            "RGBA"))
    );

static gboolean gst_gl_mixer_src_query (GstAggregator * agg, GstQuery * query);
static GstFlowReturn gst_gl_mixer_get_output_buffer (GstVideoAggregator *
    videoaggregator, GstBuffer ** outbuf);
static gboolean gst_gl_mixer_stop (GstAggregator * agg);
static gboolean gst_gl_mixer_start (GstAggregator * agg);

static GstFlowReturn
gst_gl_mixer_aggregate_frames (GstVideoAggregator * vagg,
    GstBuffer * outbuffer);

static void gst_gl_mixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_mixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_mixer_decide_allocation (GstGLBaseMixer * mix,
    GstQuery * query);

static void gst_gl_mixer_finalize (GObject * object);

static void
gst_gl_mixer_class_init (GstGLMixerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoAggregatorClass *videoaggregator_class =
      (GstVideoAggregatorClass *) klass;
  GstAggregatorClass *agg_class = (GstAggregatorClass *) klass;
  GstGLBaseMixerClass *mix_class = GST_GL_BASE_MIXER_CLASS (klass);;

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "glmixer", 0, "OpenGL mixer");

  g_type_class_add_private (klass, sizeof (GstGLMixerPrivate));

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_gl_mixer_finalize);

  gobject_class->get_property = gst_gl_mixer_get_property;
  gobject_class->set_property = gst_gl_mixer_set_property;

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  agg_class->sinkpads_type = GST_TYPE_GL_MIXER_PAD;
  agg_class->sink_query = gst_gl_mixer_sink_query;
  agg_class->src_query = gst_gl_mixer_src_query;
  agg_class->stop = gst_gl_mixer_stop;
  agg_class->start = gst_gl_mixer_start;

  videoaggregator_class->aggregate_frames = gst_gl_mixer_aggregate_frames;
  videoaggregator_class->get_output_buffer = gst_gl_mixer_get_output_buffer;
  videoaggregator_class->negotiated_caps = _negotiated_caps;
  videoaggregator_class->update_caps = _update_caps;
  videoaggregator_class->find_best_format = _find_best_format;

  mix_class->propose_allocation = gst_gl_mixer_propose_allocation;
  mix_class->decide_allocation = gst_gl_mixer_decide_allocation;

  /* Register the pad class */
  g_type_class_ref (GST_TYPE_GL_MIXER_PAD);

  klass->set_caps = NULL;
}

static void
gst_gl_mixer_reset (GstGLMixer * mix)
{
  mix->priv->negotiated = FALSE;
}

static void
gst_gl_mixer_init (GstGLMixer * mix)
{
  mix->priv = GST_GL_MIXER_GET_PRIVATE (mix);

  mix->priv->gl_resource_ready = FALSE;
  g_mutex_init (&mix->priv->gl_resource_lock);
  g_cond_init (&mix->priv->gl_resource_cond);
  /* initialize variables */
  gst_gl_mixer_reset (mix);
}

static void
gst_gl_mixer_finalize (GObject * object)
{
  GstGLMixer *mix = GST_GL_MIXER (object);
  GstGLMixerPrivate *priv = mix->priv;

  if (mix->out_caps)
    gst_caps_unref (mix->out_caps);

  g_mutex_clear (&priv->gl_resource_lock);
  g_cond_clear (&priv->gl_resource_cond);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_gl_mixer_query_caps (GstPad * pad, GstAggregator * agg, GstQuery * query)
{
  GstCaps *filter, *current_caps, *retcaps, *template_caps;

  gst_query_parse_caps (query, &filter);

  template_caps = gst_pad_get_pad_template_caps (agg->srcpad);

  current_caps = gst_pad_get_current_caps (pad);
  if (current_caps == NULL)
    retcaps = gst_caps_ref (template_caps);
  else {
    retcaps = gst_caps_merge (current_caps, template_caps);
    template_caps = NULL;
  }

  if (filter) {
    current_caps =
        gst_caps_intersect_full (filter, retcaps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (retcaps);
    retcaps = current_caps;
  }

  gst_query_set_caps_result (query, retcaps);
  gst_caps_unref (retcaps);

  if (template_caps)
    gst_caps_unref (template_caps);

  return TRUE;
}

static gboolean
gst_gl_mixer_src_query (GstAggregator * agg, GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
      res = gst_gl_mixer_query_caps (agg->srcpad, agg, query);
      break;
    default:
      res = GST_AGGREGATOR_CLASS (parent_class)->src_query (agg, query);
      break;
  }

  return res;
}

static GstFlowReturn
gst_gl_mixer_get_output_buffer (GstVideoAggregator * videoaggregator,
    GstBuffer ** outbuf)
{
  GstGLMixer *mix = GST_GL_MIXER (videoaggregator);
  GstBufferPool *pool;
  GstFlowReturn ret;

  pool =
      gst_gl_base_mixer_get_buffer_pool (GST_GL_BASE_MIXER (videoaggregator));

  if (!pool)
    return GST_FLOW_NOT_NEGOTIATED;

  if (!gst_buffer_pool_is_active (pool)) {
    if (!gst_buffer_pool_set_active (pool, TRUE)) {
      GST_ELEMENT_ERROR (mix, RESOURCE, SETTINGS,
          ("failed to activate bufferpool"), ("failed to activate bufferpool"));
      return GST_FLOW_ERROR;
    }
  }

  ret = gst_buffer_pool_acquire_buffer (pool, outbuf, NULL);
  gst_object_unref (pool);

  return ret;
}

static void
_mixer_create_fbo (GstGLContext * context, GstGLMixer * mix)
{
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (mix);
  guint out_width = GST_VIDEO_INFO_WIDTH (&vagg->info);
  guint out_height = GST_VIDEO_INFO_HEIGHT (&vagg->info);

  mix->fbo =
      gst_gl_framebuffer_new_with_default_depth (context, out_width,
      out_height);
}

static gboolean
gst_gl_mixer_decide_allocation (GstGLBaseMixer * base_mix, GstQuery * query)
{
  GstGLMixer *mix = GST_GL_MIXER (base_mix);
  GstGLMixerClass *mixer_class = GST_GL_MIXER_GET_CLASS (mix);
  GstGLContext *context = base_mix->context;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;

  g_mutex_lock (&mix->priv->gl_resource_lock);
  mix->priv->gl_resource_ready = FALSE;
  if (mix->fbo)
    gst_object_unref (mix->fbo);

  gst_gl_context_thread_add (context,
      (GstGLContextThreadFunc) _mixer_create_fbo, mix);
  if (!mix->fbo) {
    g_cond_signal (&mix->priv->gl_resource_cond);
    g_mutex_unlock (&mix->priv->gl_resource_lock);
    goto context_error;
  }

  gst_query_parse_allocation (query, &caps, NULL);

  if (mixer_class->set_caps)
    mixer_class->set_caps (mix, caps);

  mix->priv->gl_resource_ready = TRUE;
  g_cond_signal (&mix->priv->gl_resource_cond);
  g_mutex_unlock (&mix->priv->gl_resource_lock);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;

    gst_video_info_init (&vinfo);
    gst_video_info_from_caps (&vinfo, caps);
    size = vinfo.size;
    min = max = 0;
    update_pool = FALSE;
  }

  if (!pool)
    pool = gst_gl_buffer_pool_new (context);
  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  gst_buffer_pool_set_config (pool, config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return TRUE;

context_error:
  {
    GST_ELEMENT_ERROR (mix, RESOURCE, NOT_FOUND, ("Context error"), (NULL));
    return FALSE;
  }
}

static gboolean
_upload_frames (GstAggregator * agg, GstAggregatorPad * agg_pad,
    gpointer user_data)
{
  GstVideoAggregatorPad *vaggpad = GST_VIDEO_AGGREGATOR_PAD (agg_pad);
  GstGLMixerPad *pad = GST_GL_MIXER_PAD (agg_pad);
  GstGLMixer *mix = GST_GL_MIXER (agg);

  pad->current_texture = 0;
  if (vaggpad->buffer != NULL) {
    GstVideoInfo gl_info;
    GstVideoFrame gl_frame;
    GstGLSyncMeta *sync_meta;

    gst_video_info_set_format (&gl_info,
        GST_VIDEO_FORMAT_RGBA,
        GST_VIDEO_INFO_WIDTH (&vaggpad->info),
        GST_VIDEO_INFO_HEIGHT (&vaggpad->info));

    sync_meta = gst_buffer_get_gl_sync_meta (vaggpad->buffer);
    if (sync_meta)
      gst_gl_sync_meta_wait (sync_meta, GST_GL_BASE_MIXER (mix)->context);

    if (!gst_video_frame_map (&gl_frame, &gl_info, vaggpad->buffer,
            GST_MAP_READ | GST_MAP_GL)) {
      GST_ERROR_OBJECT (agg_pad, "Failed to map input frame");
      return FALSE;
    }

    pad->current_texture = *(guint *) gl_frame.data[0];
    gst_video_frame_unmap (&gl_frame);
  }

  return TRUE;
}

gboolean
gst_gl_mixer_process_textures (GstGLMixer * mix, GstBuffer * outbuf)
{
  GstGLMemory *out_tex;
  gboolean res = TRUE;
  GstVideoFrame out_frame;
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (mix);
  GstGLMixerClass *mix_class = GST_GL_MIXER_GET_CLASS (mix);
  GstGLMixerPrivate *priv = mix->priv;

  GST_TRACE ("Processing buffers");

  if (!gst_video_frame_map (&out_frame, &vagg->info, outbuf,
          GST_MAP_WRITE | GST_MAP_GL)) {
    return FALSE;
  }

  out_tex = (GstGLMemory *) out_frame.map[0].memory;

  if (!gst_aggregator_iterate_sinkpads (GST_AGGREGATOR (mix),
          (GstAggregatorPadForeachFunc) _upload_frames, NULL))
    return FALSE;

  g_mutex_lock (&priv->gl_resource_lock);
  if (!priv->gl_resource_ready)
    g_cond_wait (&priv->gl_resource_cond, &priv->gl_resource_lock);

  if (!priv->gl_resource_ready) {
    g_mutex_unlock (&priv->gl_resource_lock);
    GST_ERROR_OBJECT (mix,
        "fbo used to render can't be created, do not run process_textures");
    res = FALSE;
    goto out;
  }

  mix_class->process_textures (mix, out_tex);

  g_mutex_unlock (&priv->gl_resource_lock);

out:
  gst_video_frame_unmap (&out_frame);

  return res;
}

static gboolean
gst_gl_mixer_process_buffers (GstGLMixer * mix, GstBuffer * outbuf)
{
  GstGLMixerClass *mix_class = GST_GL_MIXER_GET_CLASS (mix);

  return mix_class->process_buffers (mix, outbuf);
}

static GstFlowReturn
gst_gl_mixer_aggregate_frames (GstVideoAggregator * vagg, GstBuffer * outbuf)
{
  gboolean res = FALSE;
  GstGLMixer *mix = GST_GL_MIXER (vagg);
  GstGLMixerClass *mix_class = GST_GL_MIXER_GET_CLASS (vagg);
  GstGLContext *context = GST_GL_BASE_MIXER (mix)->context;
  GstGLSyncMeta *sync_meta;

  if (mix_class->process_buffers)
    res = gst_gl_mixer_process_buffers (mix, outbuf);
  else if (mix_class->process_textures)
    res = gst_gl_mixer_process_textures (mix, outbuf);

  sync_meta = gst_buffer_get_gl_sync_meta (outbuf);
  if (sync_meta)
    gst_gl_sync_meta_set_sync_point (sync_meta, context);

  return res ? GST_FLOW_OK : GST_FLOW_ERROR;
}

static void
gst_gl_mixer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_mixer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_mixer_start (GstAggregator * agg)
{
  return GST_AGGREGATOR_CLASS (parent_class)->start (agg);
}

static gboolean
gst_gl_mixer_stop (GstAggregator * agg)
{
  GstGLMixer *mix = GST_GL_MIXER (agg);
  GstGLMixerClass *mixer_class = GST_GL_MIXER_GET_CLASS (mix);

  if (mixer_class->reset)
    mixer_class->reset (mix);

  if (mix->fbo) {
    gst_object_unref (mix->fbo);
    mix->fbo = NULL;
  }

  gst_gl_mixer_reset (mix);

  return GST_AGGREGATOR_CLASS (parent_class)->stop (agg);
}
