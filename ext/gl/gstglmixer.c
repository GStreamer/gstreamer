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
#include <gst/base/gstcollectpads.h>
#include <gst/video/video.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "gstglmixer.h"

#define gst_gl_mixer_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstGLMixer, gst_gl_mixer, GST_TYPE_VIDEO_AGGREGATOR);
static gboolean gst_gl_mixer_do_bufferpool (GstGLMixer * mix,
    GstCaps * outcaps);


#define GST_CAT_DEFAULT gst_gl_mixer_debug
GST_DEBUG_CATEGORY (gst_gl_mixer_debug);

static void gst_gl_mixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_gl_mixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_mixer_pad_finalize (GObject * object);

static void gst_gl_mixer_set_context (GstElement * element,
    GstContext * context);

enum
{
  PROP_PAD_0
};

#define GST_GL_MIXER_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_GL_MIXER, GstGLMixerPrivate))

struct _GstGLMixerPrivate
{
  gboolean negotiated;

  GstBufferPool *pool;
  gboolean pool_active;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstQuery *query;

  gboolean gl_resource_ready;
  GMutex gl_resource_lock;
  GCond gl_resource_cond;
};

G_DEFINE_TYPE (GstGLMixerPad, gst_gl_mixer_pad, GST_TYPE_VIDEO_AGGREGATOR_PAD);

static void
gst_gl_mixer_pad_class_init (GstGLMixerPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_gl_mixer_pad_set_property;
  gobject_class->get_property = gst_gl_mixer_pad_get_property;

  gobject_class->finalize = gst_gl_mixer_pad_finalize;
}

static void
gst_gl_mixer_pad_finalize (GObject * object)
{
  GstGLMixerPad *pad = GST_GL_MIXER_PAD (object);

  if (pad->upload) {
    gst_object_unref (pad->upload);
    pad->upload = NULL;
  }
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
  gboolean ret = gst_gl_mixer_do_bufferpool (mix, caps);

  mix->priv->negotiated = ret;

  return ret;
}

static gboolean
gst_gl_mixer_propose_allocation (GstGLMixer * mix,
    GstQuery * decide_query, GstQuery * query)
{
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *caps;
  guint size = 0;
  gboolean need_pool;
  GError *error = NULL;
  GstStructure *gl_context;
  gchar *platform, *gl_apis;
  gpointer handle;
  GstAllocator *allocator = NULL;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if ((pool = mix->priv->pool))
    gst_object_ref (pool);

  if (pool != NULL) {
    GstCaps *pcaps;

    /* we had a pool, check caps */
    GST_DEBUG_OBJECT (mix, "check existing pool caps");
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

    if (!gst_caps_is_equal (caps, pcaps)) {
      GST_DEBUG_OBJECT (mix, "pool has different caps");
      /* different caps, we can't use this pool */
      gst_object_unref (pool);
      pool = NULL;
    }
    gst_structure_free (config);
  }

  if (!gst_gl_ensure_display (mix, &mix->display))
    return FALSE;

  if (!mix->context) {
    mix->context = gst_gl_context_new (mix->display);
    if (!gst_gl_context_create (mix->context, NULL, &error))
      goto context_error;
  }

  if (pool == NULL && need_pool) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps))
      goto invalid_caps;

    GST_DEBUG_OBJECT (mix, "create new pool");
    pool = gst_gl_buffer_pool_new (mix->context);

    /* the normal size of a frame */
    size = info.size;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
    if (!gst_buffer_pool_set_config (pool, config))
      goto config_failed;
  }

  if (pool) {
    gst_query_add_allocation_pool (query, pool, size, 1, 0);
    gst_object_unref (pool);
  }

  /* we also support various metadata */
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, 0);

  gl_apis = gst_gl_api_to_string (gst_gl_context_get_gl_api (mix->context));
  platform =
      gst_gl_platform_to_string (gst_gl_context_get_gl_platform (mix->context));
  handle = (gpointer) gst_gl_context_get_gl_context (mix->context);

  gl_context =
      gst_structure_new ("GstVideoGLTextureUploadMeta", "gst.gl.GstGLContext",
      GST_GL_TYPE_CONTEXT, mix->context, "gst.gl.context.handle",
      G_TYPE_POINTER, handle, "gst.gl.context.type", G_TYPE_STRING, platform,
      "gst.gl.context.apis", G_TYPE_STRING, gl_apis, NULL);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, gl_context);

  g_free (gl_apis);
  g_free (platform);
  gst_structure_free (gl_context);

  gst_allocation_params_init (&params);

  allocator = gst_allocator_find (GST_GL_MEMORY_ALLOCATOR);
  gst_query_add_allocation_param (query, allocator, &params);
  gst_object_unref (allocator);

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
context_error:
  {
    GST_ELEMENT_ERROR (mix, RESOURCE, NOT_FOUND, ("%s", error->message),
        (NULL));
    return FALSE;
  }
}

static gboolean
gst_gl_mixer_sink_query (GstAggregator * agg, GstAggregatorPad * bpad,
    GstQuery * query)
{
  gboolean ret = FALSE;
  GstGLMixer *mix = GST_GL_MIXER (agg);

  GST_TRACE ("QUERY %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
    {
      GstQuery *decide_query = NULL;
      gboolean negotiated;

      GST_OBJECT_LOCK (mix);
      if (G_UNLIKELY (!(negotiated = mix->priv->negotiated))) {
        GST_DEBUG_OBJECT (mix,
            "not negotiated yet, can't answer ALLOCATION query");
        GST_OBJECT_UNLOCK (mix);
        return FALSE;
      }
      if ((decide_query = mix->priv->query))
        gst_query_ref (decide_query);
      GST_OBJECT_UNLOCK (mix);

      GST_DEBUG_OBJECT (mix,
          "calling propose allocation with query %" GST_PTR_FORMAT,
          decide_query);

      /* pass the query to the propose_allocation vmethod if any */
      ret = gst_gl_mixer_propose_allocation (mix, decide_query, query);

      if (decide_query)
        gst_query_unref (decide_query);

      GST_DEBUG_OBJECT (mix, "ALLOCATION ret %d, %" GST_PTR_FORMAT, ret, query);
      break;
    }
    case GST_QUERY_CONTEXT:
    {
      ret = gst_gl_handle_context_query ((GstElement *) mix, query,
          &mix->display);
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
  PROP_0
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

static gboolean gst_gl_mixer_src_query (GstAggregator * agg, GstQuery * query);
static GstFlowReturn
gst_gl_mixer_get_output_buffer (GstVideoAggregator * videoaggregator,
    GstBuffer ** outbuf);
static gboolean
gst_gl_mixer_src_activate_mode (GstAggregator * aggregator, GstPadMode mode,
    gboolean active);
static gboolean gst_gl_mixer_stop (GstAggregator * agg);
static gboolean gst_gl_mixer_start (GstAggregator * agg);

static GstFlowReturn
gst_gl_mixer_aggregate_frames (GstVideoAggregator * vagg,
    GstBuffer * outbuffer);

static void gst_gl_mixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_mixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_mixer_decide_allocation (GstGLMixer * mix,
    GstQuery * query);
static gboolean gst_gl_mixer_set_allocation (GstGLMixer * mix,
    GstBufferPool * pool, GstAllocator * allocator,
    GstAllocationParams * params, GstQuery * query);

static void gst_gl_mixer_finalize (GObject * object);

static void
gst_gl_mixer_class_init (GstGLMixerClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  GstVideoAggregatorClass *videoaggregator_class =
      (GstVideoAggregatorClass *) klass;
  GstAggregatorClass *agg_class = (GstAggregatorClass *) klass;

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "glmixer", 0, "opengl mixer");

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstGLMixerPrivate));

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_gl_mixer_finalize);

  gobject_class->get_property = gst_gl_mixer_get_property;
  gobject_class->set_property = gst_gl_mixer_set_property;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  element_class->set_context = GST_DEBUG_FUNCPTR (gst_gl_mixer_set_context);

  agg_class->sinkpads_type = GST_TYPE_GL_MIXER_PAD;
  agg_class->sink_query = gst_gl_mixer_sink_query;
  agg_class->src_query = gst_gl_mixer_src_query;
  agg_class->src_activate = gst_gl_mixer_src_activate_mode;
  agg_class->stop = gst_gl_mixer_stop;
  agg_class->start = gst_gl_mixer_start;

  videoaggregator_class->disable_frame_conversion = TRUE;
  videoaggregator_class->aggregate_frames = gst_gl_mixer_aggregate_frames;
  videoaggregator_class->get_output_buffer = gst_gl_mixer_get_output_buffer;
  videoaggregator_class->negotiated_caps = _negotiated_caps;


  /* Register the pad class */
  g_type_class_ref (GST_TYPE_GL_MIXER_PAD);

  klass->set_caps = NULL;

}

static void
gst_gl_mixer_reset (GstGLMixer * mix)
{
  /* clean up collect data */
  mix->priv->negotiated = FALSE;
}

static void
gst_gl_mixer_init (GstGLMixer * mix)
{
  mix->priv = GST_GL_MIXER_GET_PRIVATE (mix);
  mix->array_buffers = 0;
  mix->display = NULL;
  mix->fbo = 0;
  mix->depthbuffer = 0;

  mix->priv->gl_resource_ready = FALSE;
  g_mutex_init (&mix->priv->gl_resource_lock);
  g_cond_init (&mix->priv->gl_resource_cond);
  /* initialize variables */
  gst_gl_mixer_reset (mix);
}

static void
gst_gl_mixer_finalize (GObject * object)
{
  GstGLMixerPrivate *priv = GST_GL_MIXER (object)->priv;

  g_mutex_clear (&priv->gl_resource_lock);
  g_cond_clear (&priv->gl_resource_cond);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_gl_mixer_set_context (GstElement * element, GstContext * context)
{
  GstGLMixer *mix = GST_GL_MIXER (element);

  gst_gl_handle_set_context (element, context, &mix->display);
}

static gboolean
gst_gl_mixer_activate (GstGLMixer * mix, gboolean active)
{
  gboolean result = TRUE;

  if (active) {
    if (!gst_gl_ensure_display (mix, &mix->display))
      result = FALSE;
  }

  return result;
}

static gboolean
gst_gl_mixer_src_activate_mode (GstAggregator * aggregator, GstPadMode mode,
    gboolean active)
{
  GstGLMixer *mix;
  gboolean result = FALSE;

  mix = GST_GL_MIXER (aggregator);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
    case GST_PAD_MODE_PULL:
      result = gst_gl_mixer_activate (mix, active);
      break;
    default:
      result = TRUE;
      break;
  }
  return result;
}

static gboolean
gst_gl_mixer_query_caps (GstPad * pad, GstAggregator * agg, GstQuery * query)
{
  GstCaps *filter, *caps;
  GstStructure *s;
  gint n;

  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);

  gst_query_parse_caps (query, &filter);

  if (GST_VIDEO_INFO_FORMAT (&vagg->info) != GST_VIDEO_FORMAT_UNKNOWN) {
    caps = gst_video_info_to_caps (&vagg->info);
  } else {
    caps = gst_pad_get_pad_template_caps (agg->srcpad);
  }

  caps = gst_caps_make_writable (caps);

  n = gst_caps_get_size (caps) - 1;
  for (; n >= 0; n--) {
    s = gst_caps_get_structure (caps, n);
    gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
    if (GST_VIDEO_INFO_FPS_D (&vagg->info) != 0) {
      gst_structure_set (s,
          "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
    }
  }

  if (filter)
    caps = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);

  gst_query_set_caps_result (query, caps);
  gst_caps_unref (caps);

  return TRUE;
}

static gboolean
gst_gl_mixer_src_query (GstAggregator * agg, GstQuery * query)
{
  gboolean res = FALSE;
  GstGLMixer *mix = GST_GL_MIXER (agg);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      res = gst_gl_handle_context_query ((GstElement *) mix, query,
          &mix->display);
      break;
    }
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

  if (!mix->priv->pool_active) {
    if (!gst_buffer_pool_set_active (mix->priv->pool, TRUE)) {
      GST_ELEMENT_ERROR (mix, RESOURCE, SETTINGS,
          ("failed to activate bufferpool"), ("failed to activate bufferpool"));
      return GST_FLOW_ERROR;
    }
    mix->priv->pool_active = TRUE;
  }

  return gst_buffer_pool_acquire_buffer (mix->priv->pool, outbuf, NULL);
}

static gboolean
gst_gl_mixer_decide_allocation (GstGLMixer * mix, GstQuery * query)
{
  GstGLMixerClass *mixer_class = GST_GL_MIXER_GET_CLASS (mix);
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;
  GError *error = NULL;
  guint idx;
  guint out_width, out_height;
  GstGLContext *other_context = NULL;
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (mix);

  if (!gst_gl_ensure_display (mix, &mix->display))
    return FALSE;

  if (gst_query_find_allocation_meta (query,
          GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, &idx)) {
    GstGLContext *context;
    const GstStructure *upload_meta_params;
    gpointer handle;
    gchar *type;
    gchar *apis;

    gst_query_parse_nth_allocation_meta (query, idx, &upload_meta_params);
    if (upload_meta_params) {
      if (gst_structure_get (upload_meta_params, "gst.gl.GstGLContext",
              GST_GL_TYPE_CONTEXT, &context, NULL) && context) {
        GstGLContext *old = mix->context;

        mix->context = context;
        if (old)
          gst_object_unref (old);
      } else if (gst_structure_get (upload_meta_params, "gst.gl.context.handle",
              G_TYPE_POINTER, &handle, "gst.gl.context.type", G_TYPE_STRING,
              &type, "gst.gl.context.apis", G_TYPE_STRING, &apis, NULL)
          && handle) {
        GstGLPlatform platform = GST_GL_PLATFORM_NONE;
        GstGLAPI gl_apis;

        GST_DEBUG ("got GL context handle 0x%p with type %s and apis %s",
            handle, type, apis);

        platform = gst_gl_platform_from_string (type);
        gl_apis = gst_gl_api_from_string (apis);

        if (gl_apis && platform)
          other_context =
              gst_gl_context_new_wrapped (mix->display, (guintptr) handle,
              platform, gl_apis);
      }
    }
  }

  if (!mix->context) {
    mix->context = gst_gl_context_new (mix->display);
    if (!gst_gl_context_create (mix->context, other_context, &error))
      goto context_error;
  }

  out_width = GST_VIDEO_INFO_WIDTH (&vagg->info);
  out_height = GST_VIDEO_INFO_HEIGHT (&vagg->info);

  g_mutex_lock (&mix->priv->gl_resource_lock);
  mix->priv->gl_resource_ready = FALSE;
  if (mix->fbo) {
    gst_gl_context_del_fbo (mix->context, mix->fbo, mix->depthbuffer);
    mix->fbo = 0;
    mix->depthbuffer = 0;
  }

  if (!gst_gl_context_gen_fbo (mix->context, out_width, out_height,
          &mix->fbo, &mix->depthbuffer)) {
    g_cond_signal (&mix->priv->gl_resource_cond);
    g_mutex_unlock (&mix->priv->gl_resource_lock);
    goto context_error;
  }

  if (mix->out_tex_id)
    gst_gl_context_del_texture (mix->context, &mix->out_tex_id);
  gst_gl_context_gen_texture (mix->context, &mix->out_tex_id,
      GST_VIDEO_FORMAT_RGBA, out_width, out_height);

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
    pool = gst_gl_buffer_pool_new (mix->context);

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
    GST_ELEMENT_ERROR (mix, RESOURCE, NOT_FOUND, ("%s", error->message),
        (NULL));
    return FALSE;
  }
}

/* takes ownership of the pool, allocator and query */
static gboolean
gst_gl_mixer_set_allocation (GstGLMixer * mix,
    GstBufferPool * pool, GstAllocator * allocator,
    GstAllocationParams * params, GstQuery * query)
{
  GstAllocator *oldalloc;
  GstBufferPool *oldpool;
  GstQuery *oldquery;
  GstGLMixerPrivate *priv = mix->priv;

  GST_DEBUG ("storing allocation query");

  GST_OBJECT_LOCK (mix);
  oldpool = priv->pool;
  priv->pool = pool;
  priv->pool_active = FALSE;

  oldalloc = priv->allocator;
  priv->allocator = allocator;

  oldquery = priv->query;
  priv->query = query;

  if (params)
    priv->params = *params;
  else
    gst_allocation_params_init (&priv->params);
  GST_OBJECT_UNLOCK (mix);

  if (oldpool) {
    GST_DEBUG_OBJECT (mix, "deactivating old pool %p", oldpool);
    gst_buffer_pool_set_active (oldpool, FALSE);
    gst_object_unref (oldpool);
  }
  if (oldalloc) {
    gst_object_unref (oldalloc);
  }
  if (oldquery) {
    gst_query_unref (oldquery);
  }
  return TRUE;
}

static gboolean
gst_gl_mixer_do_bufferpool (GstGLMixer * mix, GstCaps * outcaps)
{
  GstQuery *query;
  gboolean result = TRUE;
  GstBufferPool *pool = NULL;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstAggregator *agg = GST_AGGREGATOR (mix);

  /* find a pool for the negotiated caps now */
  GST_DEBUG_OBJECT (mix, "doing allocation query");
  query = gst_query_new_allocation (outcaps, TRUE);
  if (!gst_pad_peer_query (agg->srcpad, query)) {
    /* not a problem, just debug a little */
    GST_DEBUG_OBJECT (mix, "peer ALLOCATION query failed");
  }

  GST_DEBUG_OBJECT (mix, "calling decide_allocation");
  result = gst_gl_mixer_decide_allocation (mix, query);

  GST_DEBUG_OBJECT (mix, "ALLOCATION (%d) params: %" GST_PTR_FORMAT, result,
      query);

  if (!result)
    goto no_decide_allocation;

  /* we got configuration from our peer or the decide_allocation method,
   * parse them */
  if (gst_query_get_n_allocation_params (query) > 0) {
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
  } else {
    allocator = NULL;
    gst_allocation_params_init (&params);
  }

  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, NULL, NULL, NULL);

  /* now store */
  result = gst_gl_mixer_set_allocation (mix, pool, allocator, &params, query);

  return result;

  /* Errors */
no_decide_allocation:
  {
    GST_WARNING_OBJECT (mix, "Failed to decide allocation");
    gst_query_unref (query);

    return result;
  }
}

gboolean
gst_gl_mixer_process_textures (GstGLMixer * mix, GstBuffer * outbuf)
{
  guint i;
  GList *walk;
  guint out_tex;
  gboolean res = TRUE;
  guint array_index = 0;
  GstVideoFrame out_frame;
  gboolean out_gl_wrapped = FALSE;
  GstElement *element = GST_ELEMENT (mix);
  GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (mix);
  GstGLMixerClass *mix_class = GST_GL_MIXER_GET_CLASS (mix);
  GstGLMixerPrivate *priv = mix->priv;

  GST_TRACE ("Processing buffers");

  if (!gst_video_frame_map (&out_frame, &vagg->info, outbuf,
          GST_MAP_WRITE | GST_MAP_GL)) {
    return FALSE;
  }

  if (gst_is_gl_memory (out_frame.map[0].memory)) {
    out_tex = *(guint *) out_frame.data[0];
  } else {
    GST_INFO ("Output Buffer does not contain correct memory, "
        "attempting to wrap for download");

    out_tex = mix->out_tex_id;;

    if (!mix->download)
      mix->download = gst_gl_download_new (mix->context);

    gst_gl_download_set_format (mix->download, &out_frame.info);
    out_gl_wrapped = TRUE;
  }

  GST_OBJECT_LOCK (mix);
  walk = element->sinkpads;

  i = mix->frames->len;
  g_ptr_array_set_size (mix->frames, element->numsinkpads);
  for (; i < element->numsinkpads; i++)
    mix->frames->pdata[i] = g_slice_new0 (GstGLMixerFrameData);
  while (walk) {
    GstGLMixerPad *pad = GST_GL_MIXER_PAD (walk->data);
    GstVideoAggregatorPad *vaggpad = walk->data;
    GstGLMixerFrameData *frame;

    frame = g_ptr_array_index (mix->frames, array_index);
    frame->pad = pad;
    frame->texture = 0;

    walk = g_list_next (walk);

    if (vaggpad->buffer != NULL) {
      guint in_tex;

      if (!pad->upload) {
        pad->upload = gst_gl_upload_new (mix->context);

        gst_gl_upload_set_format (pad->upload, &vaggpad->info);
      }

      if (!gst_gl_upload_perform_with_buffer (pad->upload,
              vaggpad->buffer, &in_tex)) {
        ++array_index;
        pad->mapped = FALSE;
        continue;
      }
      pad->mapped = TRUE;

      frame->texture = in_tex;
    }
    ++array_index;
  }

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

  mix_class->process_textures (mix, mix->frames, out_tex);

  g_mutex_unlock (&priv->gl_resource_lock);

  if (out_gl_wrapped) {
    if (!gst_gl_download_perform_with_data (mix->download, out_tex,
            out_frame.data)) {
      GST_ELEMENT_ERROR (mix, RESOURCE, NOT_FOUND, ("%s",
              "Failed to download video frame"), (NULL));
      res = FALSE;
      goto out;
    }
  }

out:
  i = 0;
  walk = GST_ELEMENT (mix)->sinkpads;
  while (walk) {
    GstGLMixerPad *pad = GST_GL_MIXER_PAD (walk->data);

    if (pad->mapped)
      gst_gl_upload_release_buffer (pad->upload);

    pad->mapped = FALSE;
    walk = g_list_next (walk);
    i++;
  }
  GST_OBJECT_UNLOCK (mix);

  gst_video_frame_unmap (&out_frame);

  return res;
}

static gboolean
gst_gl_mixer_process_buffers (GstGLMixer * mix, GstBuffer * outbuf)
{
  GList *walk;
  guint i, array_index = 0;
  GstElement *element = GST_ELEMENT (mix);
  GstGLMixerClass *mix_class = GST_GL_MIXER_GET_CLASS (mix);

  GST_OBJECT_LOCK (mix);
  walk = GST_ELEMENT (mix)->sinkpads;
  i = mix->frames->len;
  g_ptr_array_set_size (mix->frames, element->numsinkpads);
  for (; i < element->numsinkpads; i++)
    mix->frames->pdata[i] = g_slice_new0 (GstGLMixerFrameData);
  while (walk) {                /* We walk with this list because it's ordered */
    GstVideoAggregatorPad *vaggpad = walk->data;

    walk = g_list_next (walk);

    if (vaggpad->buffer != NULL) {
      /* put buffer into array */
      mix->array_buffers->pdata[array_index] = vaggpad->buffer;
    }
    ++array_index;
  }
  GST_OBJECT_UNLOCK (mix);

  return mix_class->process_buffers (mix, mix->array_buffers, outbuf);
}



static GstFlowReturn
gst_gl_mixer_aggregate_frames (GstVideoAggregator * vagg, GstBuffer * outbuf)
{
  gboolean res = FALSE;
  GstGLMixer *mix = GST_GL_MIXER (vagg);
  GstGLMixerClass *mix_class = GST_GL_MIXER_GET_CLASS (vagg);

  if (mix_class->process_buffers)
    res = gst_gl_mixer_process_buffers (mix, outbuf);
  else if (mix_class->process_textures)
    res = gst_gl_mixer_process_textures (mix, outbuf);

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
_clean_upload (GstAggregator * agg, GstPad * aggpad, gpointer udata)
{
  GstGLMixerPad *pad = GST_GL_MIXER_PAD (aggpad);

  if (pad->upload) {
    gst_object_unref (pad->upload);
    pad->upload = NULL;
  }

  return TRUE;
}

static void
_free_glmixer_frame_data (GstGLMixerFrameData * frame)
{
  g_slice_free1 (sizeof (GstGLMixerFrameData), frame);
}

static gboolean
gst_gl_mixer_start (GstAggregator * agg)
{
  guint i;
  GstGLMixer *mix = GST_GL_MIXER (agg);
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
    mix->frames->pdata[i] = g_slice_new0 (GstGLMixerFrameData);

  GST_OBJECT_UNLOCK (mix);

  return TRUE;
}

static gboolean
gst_gl_mixer_stop (GstAggregator * agg)
{
  GstGLMixer *mix = GST_GL_MIXER (agg);
  GstGLMixerClass *mixer_class = GST_GL_MIXER_GET_CLASS (mix);

  if (!GST_AGGREGATOR_CLASS (parent_class)->stop (agg))
    return FALSE;

  GST_OBJECT_LOCK (agg);
  g_ptr_array_free (mix->frames, TRUE);
  mix->frames = NULL;
  g_ptr_array_free (mix->array_buffers, TRUE);
  mix->array_buffers = NULL;
  GST_OBJECT_UNLOCK (agg);

  if (mixer_class->reset)
    mixer_class->reset (mix);
  if (mix->fbo) {
    gst_gl_context_del_fbo (mix->context, mix->fbo, mix->depthbuffer);
    mix->fbo = 0;
    mix->depthbuffer = 0;
  }
  if (mix->download) {
    gst_object_unref (mix->download);
    mix->download = NULL;
  }

  gst_aggregator_iterate_sinkpads (GST_AGGREGATOR (mix), _clean_upload, NULL);

  if (mix->priv->query) {
    gst_query_unref (mix->priv->query);
    mix->priv->query = NULL;
  }

  if (mix->priv->pool) {
    gst_object_unref (mix->priv->pool);
    mix->priv->pool = NULL;
  }

  if (mix->display) {
    gst_object_unref (mix->display);
    mix->display = NULL;
  }

  if (mix->context) {
    gst_object_unref (mix->context);
    mix->context = NULL;
  }
  gst_gl_mixer_reset (mix);

  return TRUE;
}
