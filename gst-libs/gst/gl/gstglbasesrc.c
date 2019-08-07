/*
 * GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2002,2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
 * Copyright (C) 2019 Philippe Normand <philn@igalia.com>
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

#include <gst/gl/gl.h>
#include <gst/gst-i18n-plugin.h>

/**
 * SECTION:gstglbasesrc
 * @short_description: #GstPushSrc subclass for injecting OpenGL resources in a pipeline
 * @title: GstGLBaseSrc
 * @see_also: #GstPushSrc
 *
 * #GstGLBaseSrc handles the nitty gritty details of retrieving an OpenGL
 * context. It also provided some wrappers around #GstBaseSrc's `start()` and
 * `stop()` virtual methods that ensure an OpenGL context is available and
 * current in the calling thread.
 */

#define GST_CAT_DEFAULT gst_gl_base_src_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

struct _GstGLBaseSrcPrivate
{
  GstGLContext *other_context;

  GstGLMemory *out_tex;
  gint64 timestamp_offset;      /* base offset */
  gint64 n_frames;              /* total frames sent */
  gboolean negotiated;
  gboolean gl_result;
  gboolean gl_started;
};

/* Properties */
enum
{
  PROP_0,
  PROP_TIMESTAMP_OFFSET
};

#define gst_gl_base_src_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstGLBaseSrc, gst_gl_base_src,
    GST_TYPE_PUSH_SRC, G_ADD_PRIVATE (GstGLBaseSrc)
    GST_DEBUG_CATEGORY_INIT (gst_gl_base_src_debug,
        "glbasesrc", 0, "glbasesrc element");
    );

static void gst_gl_base_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_base_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_base_src_setcaps (GstBaseSrc * bsrc, GstCaps * caps);
static gboolean gst_gl_base_src_query (GstBaseSrc * bsrc, GstQuery * query);
static void gst_gl_base_src_set_context (GstElement * element,
    GstContext * context);
static GstStateChangeReturn gst_gl_base_src_change_state (GstElement * element,
    GstStateChange transition);

static void gst_gl_base_src_get_times (GstBaseSrc * basesrc,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static GstFlowReturn gst_gl_base_src_fill (GstPushSrc * psrc,
    GstBuffer * buffer);
static gboolean gst_gl_base_src_start (GstBaseSrc * basesrc);
static gboolean gst_gl_base_src_stop (GstBaseSrc * basesrc);
static gboolean gst_gl_base_src_decide_allocation (GstBaseSrc * basesrc,
    GstQuery * query);
static gboolean gst_gl_base_src_do_seek (GstBaseSrc * basesrc,
    GstSegment * segment);

static gboolean gst_gl_base_src_default_gl_start (GstGLBaseSrc * src);
static void gst_gl_base_src_default_gl_stop (GstGLBaseSrc * src);
static gboolean gst_gl_base_src_default_fill_gl_memory (GstGLBaseSrc * src,
    GstGLMemory * mem);

static void
gst_gl_base_src_class_init (GstGLBaseSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_base_src_set_property;
  gobject_class->get_property = gst_gl_base_src_get_property;

  g_object_class_install_property (gobject_class,
      PROP_TIMESTAMP_OFFSET, g_param_spec_int64 ("timestamp-offset",
          "Timestamp offset",
          "An offset added to timestamps set on buffers (in ns)", G_MININT64,
          G_MAXINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class->set_context = GST_DEBUG_FUNCPTR (gst_gl_base_src_set_context);
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_gl_base_src_change_state);

  gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_gl_base_src_setcaps);
  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (gst_gl_base_src_query);
  gstbasesrc_class->get_times = GST_DEBUG_FUNCPTR (gst_gl_base_src_get_times);
  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_gl_base_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_gl_base_src_stop);
  gstbasesrc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_gl_base_src_decide_allocation);
  gstbasesrc_class->do_seek = GST_DEBUG_FUNCPTR (gst_gl_base_src_do_seek);

  gstpushsrc_class->fill = GST_DEBUG_FUNCPTR (gst_gl_base_src_fill);

  klass->supported_gl_api = GST_GL_API_ANY;
  klass->gl_start = GST_DEBUG_FUNCPTR (gst_gl_base_src_default_gl_start);
  klass->gl_stop = GST_DEBUG_FUNCPTR (gst_gl_base_src_default_gl_stop);
  klass->fill_gl_memory =
      GST_DEBUG_FUNCPTR (gst_gl_base_src_default_fill_gl_memory);
}

static void
gst_gl_base_src_init (GstGLBaseSrc * src)
{
  src->priv = gst_gl_base_src_get_instance_private (src);
  src->priv->timestamp_offset = 0;

  /* we operate in time */
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (src), FALSE);
}

static void
gst_gl_base_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLBaseSrc *src = GST_GL_BASE_SRC (object);

  switch (prop_id) {
    case PROP_TIMESTAMP_OFFSET:
      src->priv->timestamp_offset = g_value_get_int64 (value);
      break;
    default:
      break;
  }
}

static void
gst_gl_base_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLBaseSrc *src = GST_GL_BASE_SRC (object);

  switch (prop_id) {
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int64 (value, src->priv->timestamp_offset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_base_src_setcaps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstGLBaseSrc *glbasesrc = GST_GL_BASE_SRC (bsrc);

  GST_DEBUG ("setcaps");

  if (!gst_video_info_from_caps (&glbasesrc->out_info, caps))
    goto wrong_caps;

  glbasesrc->priv->negotiated = TRUE;

  gst_caps_replace (&glbasesrc->out_caps, caps);

  return TRUE;

/* ERRORS */
wrong_caps:
  {
    GST_WARNING ("wrong caps");
    return FALSE;
  }
}

static void
gst_gl_base_src_set_context (GstElement * element, GstContext * context)
{
  GstGLBaseSrc *src = GST_GL_BASE_SRC (element);
  GstGLBaseSrcClass *klass = GST_GL_BASE_SRC_GET_CLASS (src);

  gst_gl_handle_set_context (element, context, &src->display,
      &src->priv->other_context);

  if (src->display)
    gst_gl_display_filter_gl_api (src->display, klass->supported_gl_api);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_gl_base_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  gboolean res = FALSE;
  GstGLBaseSrc *src;

  src = GST_GL_BASE_SRC (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      if (gst_gl_handle_context_query ((GstElement *) src, query,
              src->display, src->context, src->priv->other_context))
        return TRUE;
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res =
          gst_video_info_convert (&src->out_info, src_fmt, src_val, dest_fmt,
          &dest_val);
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);

      return res;
    }
    default:
      break;
  }

  return GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
}

static void
gst_gl_base_src_get_times (GstBaseSrc * basesrc, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* for live sources, sync on the timestamp of the buffer */
  if (gst_base_src_is_live (basesrc)) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);

    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      /* get duration to calculate end time */
      GstClockTime duration = GST_BUFFER_DURATION (buffer);

      if (GST_CLOCK_TIME_IS_VALID (duration))
        *end = timestamp + duration;
      *start = timestamp;
    }
  } else {
    *start = -1;
    *end = -1;
  }
}

static gboolean
gst_gl_base_src_default_gl_start (GstGLBaseSrc * src)
{
  return TRUE;
}

static void
gst_gl_base_src_gl_start (GstGLContext * context, gpointer data)
{
  GstGLBaseSrc *src = GST_GL_BASE_SRC (data);
  GstGLBaseSrcClass *src_class = GST_GL_BASE_SRC_GET_CLASS (src);

  gst_gl_insert_debug_marker (src->context,
      "starting element %s", GST_OBJECT_NAME (src));

  src->priv->gl_started = src_class->gl_start (src);
}

static void
gst_gl_base_src_default_gl_stop (GstGLBaseSrc * src)
{
}

static void
gst_gl_base_src_gl_stop (GstGLContext * context, gpointer data)
{
  GstGLBaseSrc *src = GST_GL_BASE_SRC (data);
  GstGLBaseSrcClass *src_class = GST_GL_BASE_SRC_GET_CLASS (src);

  gst_gl_insert_debug_marker (src->context,
      "stopping element %s", GST_OBJECT_NAME (src));

  if (src->priv->gl_started)
    src_class->gl_stop (src);

  src->priv->gl_started = FALSE;
}

static gboolean
gst_gl_base_src_default_fill_gl_memory (GstGLBaseSrc * src, GstGLMemory * mem)
{
  return TRUE;
}

static void
_fill_gl (GstGLContext * context, GstGLBaseSrc * src)
{
  GstGLBaseSrcClass *klass = GST_GL_BASE_SRC_GET_CLASS (src);

  src->priv->gl_result = klass->fill_gl_memory (src, src->priv->out_tex);
}

static GstFlowReturn
gst_gl_base_src_fill (GstPushSrc * psrc, GstBuffer * buffer)
{
  GstGLBaseSrc *src = GST_GL_BASE_SRC (psrc);
  GstClockTime next_time;
  GstVideoFrame out_frame;
  GstGLSyncMeta *sync_meta;

  if (G_UNLIKELY (!src->priv->negotiated || !src->context))
    goto not_negotiated;

  /* 0 framerate and we are at the second frame, eos */
  if (G_UNLIKELY (GST_VIDEO_INFO_FPS_N (&src->out_info) == 0
          && src->priv->n_frames == 1))
    goto eos;

  if (!gst_video_frame_map (&out_frame, &src->out_info, buffer,
          GST_MAP_WRITE | GST_MAP_GL)) {
    return GST_FLOW_NOT_NEGOTIATED;
  }

  src->priv->out_tex = (GstGLMemory *) out_frame.map[0].memory;

  gst_gl_context_thread_add (src->context, (GstGLContextThreadFunc) _fill_gl,
      src);
  if (!src->priv->gl_result) {
    gst_video_frame_unmap (&out_frame);
    goto gl_error;
  }
  gst_video_frame_unmap (&out_frame);
  if (!src->priv->gl_result)
    goto gl_error;

  sync_meta = gst_buffer_get_gl_sync_meta (buffer);
  if (sync_meta)
    gst_gl_sync_meta_set_sync_point (sync_meta, src->context);

  GST_BUFFER_TIMESTAMP (buffer) =
      src->priv->timestamp_offset + src->running_time;
  GST_BUFFER_OFFSET (buffer) = src->priv->n_frames;
  src->priv->n_frames++;
  GST_BUFFER_OFFSET_END (buffer) = src->priv->n_frames;
  if (src->out_info.fps_n) {
    next_time = gst_util_uint64_scale_int (src->priv->n_frames * GST_SECOND,
        src->out_info.fps_d, src->out_info.fps_n);
    GST_BUFFER_DURATION (buffer) = next_time - src->running_time;
  } else {
    next_time = src->priv->timestamp_offset;
    /* NONE means forever */
    GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  }

  src->running_time = next_time;

  return GST_FLOW_OK;

gl_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (_("failed to draw pattern")),
        (_("A GL error occurred")));
    return GST_FLOW_NOT_NEGOTIATED;
  }
not_negotiated:
  {
    GST_ELEMENT_ERROR (src, CORE, NEGOTIATION, (NULL),
        (_("format wasn't negotiated before get function")));
    return GST_FLOW_NOT_NEGOTIATED;
  }
eos:
  {
    GST_DEBUG_OBJECT (src, "eos: 0 framerate, frame %d",
        (gint) src->priv->n_frames);
    return GST_FLOW_EOS;
  }
}

static gboolean
gst_gl_base_src_start (GstBaseSrc * basesrc)
{
  GstGLBaseSrc *src = GST_GL_BASE_SRC (basesrc);

  src->running_time = 0;
  src->priv->n_frames = 0;
  src->priv->negotiated = FALSE;

  return TRUE;
}

static gboolean
gst_gl_base_src_stop (GstBaseSrc * basesrc)
{
  GstGLBaseSrc *src = GST_GL_BASE_SRC (basesrc);

  gst_caps_replace (&src->out_caps, NULL);

  if (src->context)
    gst_object_unref (src->context);
  src->context = NULL;

  return TRUE;
}

static gboolean
_find_local_gl_context (GstGLBaseSrc * src)
{
  if (gst_gl_query_local_gl_context (GST_ELEMENT (src), GST_PAD_SRC,
          &src->context))
    return TRUE;
  return FALSE;
}

static gboolean
gst_gl_base_src_decide_allocation (GstBaseSrc * basesrc, GstQuery * query)
{
  GstGLBaseSrc *src = GST_GL_BASE_SRC (basesrc);
  GstGLBaseSrcClass *klass = GST_GL_BASE_SRC_GET_CLASS (src);
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;
  GError *error = NULL;
  gboolean new_context = FALSE;

  if (!src->context)
    new_context = TRUE;

  if (!gst_gl_ensure_element_data (src, &src->display,
          &src->priv->other_context))
    return FALSE;

  gst_gl_display_filter_gl_api (src->display, klass->supported_gl_api);

  _find_local_gl_context (src);

  if (!src->context) {
    GST_OBJECT_LOCK (src->display);
    do {
      if (src->context) {
        gst_object_unref (src->context);
        src->context = NULL;
      }
      /* just get a GL context.  we don't care */
      src->context =
          gst_gl_display_get_gl_context_for_thread (src->display, NULL);
      if (!src->context) {
        if (!gst_gl_display_create_context (src->display,
                src->priv->other_context, &src->context, &error)) {
          GST_OBJECT_UNLOCK (src->display);
          goto context_error;
        }
      }
    } while (!gst_gl_display_add_context (src->display, src->context));
    GST_OBJECT_UNLOCK (src->display);
  }

  if (new_context || !src->priv->gl_started) {
    if (src->priv->gl_started)
      gst_gl_context_thread_add (src->context, gst_gl_base_src_gl_stop, src);

    {
      if ((gst_gl_context_get_gl_api (src->
                  context) & klass->supported_gl_api) == 0)
        goto unsupported_gl_api;
    }

    gst_gl_context_thread_add (src->context, gst_gl_base_src_gl_start, src);

    if (!src->priv->gl_started)
      goto error;
  }

  gst_query_parse_allocation (query, &caps, NULL);

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

  if (!pool || !GST_IS_GL_BUFFER_POOL (pool)) {
    /* can't use this pool */
    if (pool)
      gst_object_unref (pool);
    pool = gst_gl_buffer_pool_new (src->context);
  }
  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  if (gst_query_find_allocation_meta (query, GST_GL_SYNC_META_API_TYPE, NULL))
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_GL_SYNC_META);
  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_GL_TEXTURE_UPLOAD_META);

  gst_buffer_pool_set_config (pool, config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return TRUE;

unsupported_gl_api:
  {
    GstGLAPI gl_api = gst_gl_context_get_gl_api (src->context);
    gchar *gl_api_str = gst_gl_api_to_string (gl_api);
    gchar *supported_gl_api_str =
        gst_gl_api_to_string (klass->supported_gl_api);
    GST_ELEMENT_ERROR (src, RESOURCE, BUSY,
        ("GL API's not compatible context: %s supported: %s", gl_api_str,
            supported_gl_api_str), (NULL));

    g_free (supported_gl_api_str);
    g_free (gl_api_str);
    return FALSE;
  }
context_error:
  {
    if (error) {
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, ("%s", error->message),
          (NULL));
      g_clear_error (&error);
    } else {
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL), (NULL));
    }
    if (src->context)
      gst_object_unref (src->context);
    src->context = NULL;
    return FALSE;
  }
error:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Subclass failed to initialize."), (NULL));
    return FALSE;
  }
}

static GstStateChangeReturn
gst_gl_base_src_change_state (GstElement * element, GstStateChange transition)
{
  GstGLBaseSrc *src = GST_GL_BASE_SRC (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG_OBJECT (src, "changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (src->priv->other_context) {
        gst_object_unref (src->priv->other_context);
        src->priv->other_context = NULL;
      }

      if (src->display) {
        gst_object_unref (src->display);
        src->display = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_gl_base_src_do_seek (GstBaseSrc * basesrc, GstSegment * segment)
{
  GstGLBaseSrc *src = GST_GL_BASE_SRC (basesrc);
  GstClockTime time;
  segment->time = segment->start;
  time = segment->position;

  /* now move to the time indicated */
  if (src->out_info.fps_n) {
    src->priv->n_frames = gst_util_uint64_scale (time,
        src->out_info.fps_n, src->out_info.fps_d * GST_SECOND);
  } else
    src->priv->n_frames = 0;

  if (src->out_info.fps_n) {
    src->running_time = gst_util_uint64_scale (src->priv->n_frames,
        src->out_info.fps_d * GST_SECOND, src->out_info.fps_n);
  } else {
    /* FIXME : Not sure what to set here */
    src->running_time = 0;
  }

  g_return_val_if_fail (src->running_time <= time, FALSE);

  return GST_BASE_SRC_CLASS (parent_class)->do_seek (basesrc, segment);
}
