/* Generic video mixer plugin
 *
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#include "gstglbasemixer.h"

#define gst_gl_base_mixer_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstGLBaseMixer, gst_gl_base_mixer,
    GST_TYPE_VIDEO_AGGREGATOR);
static gboolean gst_gl_base_mixer_do_bufferpool (GstGLBaseMixer * mix,
    GstCaps * outcaps);

#define GST_CAT_DEFAULT gst_gl_base_mixer_debug
GST_DEBUG_CATEGORY (gst_gl_base_mixer_debug);

static void gst_gl_base_mixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_gl_base_mixer_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void gst_gl_base_mixer_set_context (GstElement * element,
    GstContext * context);
static GstStateChangeReturn gst_gl_base_mixer_change_state (GstElement *
    element, GstStateChange transition);

enum
{
  PROP_PAD_0
};

#define GST_GL_BASE_MIXER_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_GL_BASE_MIXER, GstGLBaseMixerPrivate))

struct _GstGLBaseMixerPrivate
{
  gboolean negotiated;

  GstGLContext *other_context;

  GstBufferPool *pool;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstQuery *query;
};

G_DEFINE_TYPE (GstGLBaseMixerPad, gst_gl_base_mixer_pad,
    GST_TYPE_VIDEO_AGGREGATOR_PAD);

static void
gst_gl_base_mixer_pad_class_init (GstGLBaseMixerPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstVideoAggregatorPadClass *vaggpad_class =
      (GstVideoAggregatorPadClass *) klass;

  gobject_class->set_property = gst_gl_base_mixer_pad_set_property;
  gobject_class->get_property = gst_gl_base_mixer_pad_get_property;

  vaggpad_class->set_info = NULL;
  vaggpad_class->prepare_frame = NULL;
  vaggpad_class->clean_frame = NULL;
}

static void
gst_gl_base_mixer_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_base_mixer_pad_set_property (GObject * object, guint prop_id,
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
  GstGLBaseMixer *mix = GST_GL_BASE_MIXER (vagg);

  return gst_gl_base_mixer_do_bufferpool (mix, caps);
}

static gboolean
_default_propose_allocation (GstGLBaseMixer * mix, GstGLBaseMixerPad * pad,
    GstQuery * decide_query, GstQuery * query)
{
  return TRUE;
}

static gboolean
gst_gl_base_mixer_sink_event (GstAggregator * agg, GstAggregatorPad * bpad,
    GstEvent * event)
{
  GstGLBaseMixerPad *pad = GST_GL_BASE_MIXER_PAD (bpad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
      if (!GST_AGGREGATOR_CLASS (parent_class)->sink_event (agg, bpad, event))
        return FALSE;

      pad->negotiated = TRUE;
      return TRUE;
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_event (agg, bpad, event);
}

static gboolean
_find_local_gl_context (GstGLBaseMixer * mix)
{
  if (gst_gl_query_local_gl_context (GST_ELEMENT (mix), GST_PAD_SRC,
          &mix->context))
    return TRUE;
  if (gst_gl_query_local_gl_context (GST_ELEMENT (mix), GST_PAD_SINK,
          &mix->context))
    return TRUE;
  return FALSE;
}

static gboolean
_get_gl_context (GstGLBaseMixer * mix)
{
  GstGLBaseMixerClass *mix_class = GST_GL_BASE_MIXER_GET_CLASS (mix);
  GError *error = NULL;

  if (!gst_gl_ensure_element_data (mix, &mix->display,
          &mix->priv->other_context))
    return FALSE;

  gst_gl_display_filter_gl_api (mix->display, mix_class->supported_gl_api);

  _find_local_gl_context (mix);

  GST_OBJECT_LOCK (mix->display);
  if (!mix->context) {
    do {
      if (mix->context) {
        gst_object_unref (mix->context);
        mix->context = NULL;
      }
      /* just get a GL context.  we don't care */
      mix->context =
          gst_gl_display_get_gl_context_for_thread (mix->display, NULL);
      if (!mix->context) {
        if (!gst_gl_display_create_context (mix->display,
                mix->priv->other_context, &mix->context, &error)) {
          GST_OBJECT_UNLOCK (mix->display);
          goto context_error;
        }
      }
    } while (!gst_gl_display_add_context (mix->display, mix->context));
  }
  GST_OBJECT_UNLOCK (mix->display);

  {
    GstGLAPI current_gl_api = gst_gl_context_get_gl_api (mix->context);
    if ((current_gl_api & mix_class->supported_gl_api) == 0)
      goto unsupported_gl_api;
  }

  return TRUE;

unsupported_gl_api:
  {
    GstGLAPI gl_api = gst_gl_context_get_gl_api (mix->context);
    gchar *gl_api_str = gst_gl_api_to_string (gl_api);
    gchar *supported_gl_api_str =
        gst_gl_api_to_string (mix_class->supported_gl_api);
    GST_ELEMENT_ERROR (mix, RESOURCE, BUSY,
        ("GL API's not compatible context: %s supported: %s", gl_api_str,
            supported_gl_api_str), (NULL));

    g_free (supported_gl_api_str);
    g_free (gl_api_str);
    return FALSE;
  }
context_error:
  {
    GST_ELEMENT_ERROR (mix, RESOURCE, NOT_FOUND, ("%s", error->message),
        (NULL));
    g_clear_error (&error);
    return FALSE;
  }
}

static gboolean
gst_gl_base_mixer_sink_query (GstAggregator * agg, GstAggregatorPad * bpad,
    GstQuery * query)
{
  gboolean ret = FALSE;
  GstGLBaseMixer *mix = GST_GL_BASE_MIXER (agg);
  GstGLBaseMixerClass *mix_class = GST_GL_BASE_MIXER_GET_CLASS (mix);
  GstGLBaseMixerPad *pad = GST_GL_BASE_MIXER_PAD (bpad);

  GST_TRACE ("QUERY %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
    {
      GstQuery *decide_query = NULL;

      GST_OBJECT_LOCK (mix);
      if (G_UNLIKELY (!pad->negotiated)) {
        GST_DEBUG_OBJECT (mix,
            "not negotiated yet, can't answer ALLOCATION query");
        GST_OBJECT_UNLOCK (mix);
        return FALSE;
      }

      if ((decide_query = mix->priv->query))
        gst_query_ref (decide_query);
      GST_OBJECT_UNLOCK (mix);

      if (!_get_gl_context (mix))
        return FALSE;

      GST_DEBUG_OBJECT (mix,
          "calling propose allocation with query %" GST_PTR_FORMAT,
          decide_query);

      /* pass the query to the propose_allocation vmethod if any */
      if (mix_class->propose_allocation)
        ret = mix_class->propose_allocation (mix, pad, decide_query, query);
      else
        ret = FALSE;

      if (decide_query)
        gst_query_unref (decide_query);

      GST_DEBUG_OBJECT (mix, "ALLOCATION ret %d, %" GST_PTR_FORMAT, ret, query);
      return ret;
    }
    case GST_QUERY_CONTEXT:
    {
      if (gst_gl_handle_context_query ((GstElement *) mix, query,
              mix->display, mix->context, mix->priv->other_context))
        return TRUE;
      break;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_query (agg, bpad, query);;
}

static void
gst_gl_base_mixer_pad_init (GstGLBaseMixerPad * mixerpad)
{
}

/* GLBaseMixer signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_CONTEXT
};

static gboolean gst_gl_base_mixer_src_query (GstAggregator * agg,
    GstQuery * query);

static gboolean
gst_gl_base_mixer_src_activate_mode (GstAggregator * aggregator,
    GstPadMode mode, gboolean active);
static gboolean gst_gl_base_mixer_stop (GstAggregator * agg);
static gboolean gst_gl_base_mixer_start (GstAggregator * agg);

static void gst_gl_base_mixer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_base_mixer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_base_mixer_decide_allocation (GstGLBaseMixer * mix,
    GstQuery * query);
static gboolean gst_gl_base_mixer_set_allocation (GstGLBaseMixer * mix,
    GstBufferPool * pool, GstAllocator * allocator,
    GstAllocationParams * params, GstQuery * query);

static void
gst_gl_base_mixer_class_init (GstGLBaseMixerClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  GstVideoAggregatorClass *videoaggregator_class =
      (GstVideoAggregatorClass *) klass;
  GstAggregatorClass *agg_class = (GstAggregatorClass *) klass;

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "glmixer", 0, "opengl mixer");

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstGLBaseMixerPrivate));

  gobject_class->get_property = gst_gl_base_mixer_get_property;
  gobject_class->set_property = gst_gl_base_mixer_set_property;

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_gl_base_mixer_set_context);
  element_class->change_state = gst_gl_base_mixer_change_state;

  agg_class->sinkpads_type = GST_TYPE_GL_BASE_MIXER_PAD;
  agg_class->sink_query = gst_gl_base_mixer_sink_query;
  agg_class->sink_event = gst_gl_base_mixer_sink_event;
  agg_class->src_query = gst_gl_base_mixer_src_query;
  agg_class->src_activate = gst_gl_base_mixer_src_activate_mode;
  agg_class->stop = gst_gl_base_mixer_stop;
  agg_class->start = gst_gl_base_mixer_start;

  videoaggregator_class->negotiated_caps = _negotiated_caps;

  klass->propose_allocation = _default_propose_allocation;

  g_object_class_install_property (gobject_class, PROP_CONTEXT,
      g_param_spec_object ("context",
          "OpenGL context",
          "Get OpenGL context",
          GST_TYPE_GL_CONTEXT, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /* Register the pad class */
  g_type_class_ref (GST_TYPE_GL_BASE_MIXER_PAD);

  klass->supported_gl_api = GST_GL_API_ANY;
}

static gboolean
_reset_pad (GstAggregator * self, GstAggregatorPad * base_pad,
    gpointer user_data)
{
  GstGLBaseMixerPad *mix_pad = GST_GL_BASE_MIXER_PAD (base_pad);

  mix_pad->negotiated = FALSE;

  return TRUE;
}

static void
gst_gl_base_mixer_reset (GstGLBaseMixer * mix)
{
  /* clean up collect data */

  gst_aggregator_iterate_sinkpads (GST_AGGREGATOR (mix),
      (GstAggregatorPadForeachFunc) _reset_pad, NULL);
}

static void
gst_gl_base_mixer_init (GstGLBaseMixer * mix)
{
  mix->priv = GST_GL_BASE_MIXER_GET_PRIVATE (mix);

  gst_gl_base_mixer_reset (mix);
}

static void
gst_gl_base_mixer_set_context (GstElement * element, GstContext * context)
{
  GstGLBaseMixer *mix = GST_GL_BASE_MIXER (element);
  GstGLBaseMixerClass *mix_class = GST_GL_BASE_MIXER_GET_CLASS (mix);

  gst_gl_handle_set_context (element, context, &mix->display,
      &mix->priv->other_context);

  if (mix->display)
    gst_gl_display_filter_gl_api (mix->display, mix_class->supported_gl_api);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_gl_base_mixer_activate (GstGLBaseMixer * mix, gboolean active)
{
  GstGLBaseMixerClass *mix_class = GST_GL_BASE_MIXER_GET_CLASS (mix);
  gboolean result = TRUE;

  if (active) {
    if (!gst_gl_ensure_element_data (mix, &mix->display,
            &mix->priv->other_context))
      return FALSE;

    gst_gl_display_filter_gl_api (mix->display, mix_class->supported_gl_api);
  }

  return result;
}

static gboolean
gst_gl_base_mixer_src_activate_mode (GstAggregator * aggregator,
    GstPadMode mode, gboolean active)
{
  GstGLBaseMixer *mix;
  gboolean result = FALSE;

  mix = GST_GL_BASE_MIXER (aggregator);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
    case GST_PAD_MODE_PULL:
      result = gst_gl_base_mixer_activate (mix, active);
      break;
    default:
      result = TRUE;
      break;
  }
  return result;
}

static gboolean
gst_gl_base_mixer_src_query (GstAggregator * agg, GstQuery * query)
{
  GstGLBaseMixer *mix = GST_GL_BASE_MIXER (agg);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      if (gst_gl_handle_context_query ((GstElement *) mix, query,
              mix->display, mix->context, mix->priv->other_context))
        return TRUE;
      break;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_query (agg, query);
}

static gboolean
gst_gl_base_mixer_decide_allocation (GstGLBaseMixer * mix, GstQuery * query)
{
  GstGLBaseMixerClass *mix_class = GST_GL_BASE_MIXER_GET_CLASS (mix);

  if (!_get_gl_context (mix))
    return FALSE;

  if (mix_class->decide_allocation)
    if (!mix_class->decide_allocation (mix, query))
      return FALSE;

  return TRUE;
}

/* takes ownership of the pool, allocator and query */
static gboolean
gst_gl_base_mixer_set_allocation (GstGLBaseMixer * mix,
    GstBufferPool * pool, GstAllocator * allocator,
    GstAllocationParams * params, GstQuery * query)
{
  GstAllocator *oldalloc;
  GstBufferPool *oldpool;
  GstQuery *oldquery;
  GstGLBaseMixerPrivate *priv = mix->priv;

  GST_DEBUG ("storing allocation query");

  GST_OBJECT_LOCK (mix);
  oldpool = priv->pool;
  priv->pool = pool;

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
gst_gl_base_mixer_do_bufferpool (GstGLBaseMixer * mix, GstCaps * outcaps)
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
  result = gst_gl_base_mixer_decide_allocation (mix, query);

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
  result =
      gst_gl_base_mixer_set_allocation (mix, pool, allocator, &params, query);

  return result;

  /* Errors */
no_decide_allocation:
  {
    GST_WARNING_OBJECT (mix, "Failed to decide allocation");
    gst_query_unref (query);

    return result;
  }
}

GstBufferPool *
gst_gl_base_mixer_get_buffer_pool (GstGLBaseMixer * mix)
{
  GstBufferPool *pool;

  GST_OBJECT_LOCK (mix);
  pool = mix->priv->pool;
  if (pool)
    gst_object_ref (pool);
  GST_OBJECT_UNLOCK (mix);

  return pool;
}

static void
gst_gl_base_mixer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstGLBaseMixer *mixer = GST_GL_BASE_MIXER (object);

  switch (prop_id) {
    case PROP_CONTEXT:
      g_value_set_object (value, mixer->context);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_base_mixer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_base_mixer_start (GstAggregator * agg)
{
  return GST_AGGREGATOR_CLASS (parent_class)->start (agg);;
}

static gboolean
gst_gl_base_mixer_stop (GstAggregator * agg)
{
  GstGLBaseMixer *mix = GST_GL_BASE_MIXER (agg);

  if (mix->priv->query) {
    gst_query_unref (mix->priv->query);
    mix->priv->query = NULL;
  }

  if (mix->priv->pool) {
    gst_object_unref (mix->priv->pool);
    mix->priv->pool = NULL;
  }

  if (mix->context) {
    gst_object_unref (mix->context);
    mix->context = NULL;
  }

  gst_gl_base_mixer_reset (mix);

  return TRUE;
}

static GstStateChangeReturn
gst_gl_base_mixer_change_state (GstElement * element, GstStateChange transition)
{
  GstGLBaseMixer *mix = GST_GL_BASE_MIXER (element);
  GstGLBaseMixerClass *mix_class = GST_GL_BASE_MIXER_GET_CLASS (mix);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG_OBJECT (mix, "changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_gl_ensure_element_data (element, &mix->display,
              &mix->priv->other_context))
        return GST_STATE_CHANGE_FAILURE;

      gst_gl_display_filter_gl_api (mix->display, mix_class->supported_gl_api);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (mix->priv->other_context) {
        gst_object_unref (mix->priv->other_context);
        mix->priv->other_context = NULL;
      }

      if (mix->display) {
        gst_object_unref (mix->display);
        mix->display = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}
