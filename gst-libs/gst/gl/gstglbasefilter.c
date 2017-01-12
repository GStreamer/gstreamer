/*
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

#include <gst/video/gstvideometa.h>

#include <gst/gl/gl.h>
#include <gst/gl/gstglutils_private.h>

/**
 * SECTION:gstglbasefilter
 * @short_description: #GstBaseTransform subclass for transforming OpenGL resources
 * @title: GstGLBaseFilter
 * @see_also: #GstBaseTransform
 *
 * #GstGLBaseFilter handles the nitty gritty details of retrieving an OpenGL
 * context.  It also provided some wrappers around #GstBaseTransform's
 * start(), stop() and set_caps() virtual methods that ensure an OpenGL context
 * is available and current in the calling thread.
 */

#define GST_CAT_DEFAULT gst_gl_base_filter_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define GST_GL_BASE_FILTER_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_GL_BASE_FILTER, GstGLBaseFilterPrivate))

struct _GstGLBaseFilterPrivate
{
  GstGLContext *other_context;

  gboolean gl_result;
  gboolean gl_started;
};

/* Properties */
enum
{
  PROP_0,
  PROP_CONTEXT
};

#define gst_gl_base_filter_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLBaseFilter, gst_gl_base_filter,
    GST_TYPE_BASE_TRANSFORM, GST_DEBUG_CATEGORY_INIT (gst_gl_base_filter_debug,
        "glbasefilter", 0, "glbasefilter element"););

static void gst_gl_base_filter_finalize (GObject * object);
static void gst_gl_base_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_base_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_base_filter_set_context (GstElement * element,
    GstContext * context);
static GstStateChangeReturn gst_gl_base_filter_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_gl_base_filter_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static void gst_gl_base_filter_reset (GstGLBaseFilter * filter);
static gboolean gst_gl_base_filter_start (GstBaseTransform * bt);
static gboolean gst_gl_base_filter_stop (GstBaseTransform * bt);
static gboolean gst_gl_base_filter_set_caps (GstBaseTransform * bt,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_gl_base_filter_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);

/* GstGLContextThreadFunc */
static void gst_gl_base_filter_gl_start (GstGLContext * context, gpointer data);
static void gst_gl_base_filter_gl_stop (GstGLContext * context, gpointer data);

static gboolean gst_gl_base_filter_default_gl_start (GstGLBaseFilter * filter);
static void gst_gl_base_filter_default_gl_stop (GstGLBaseFilter * filter);

static void
gst_gl_base_filter_class_init (GstGLBaseFilterClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  g_type_class_add_private (klass, sizeof (GstGLBaseFilterPrivate));

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_gl_base_filter_finalize;
  gobject_class->set_property = gst_gl_base_filter_set_property;
  gobject_class->get_property = gst_gl_base_filter_get_property;

  GST_BASE_TRANSFORM_CLASS (klass)->query = gst_gl_base_filter_query;
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = gst_gl_base_filter_set_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->start = gst_gl_base_filter_start;
  GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_base_filter_stop;
  GST_BASE_TRANSFORM_CLASS (klass)->decide_allocation =
      gst_gl_base_filter_decide_allocation;

  element_class->set_context = gst_gl_base_filter_set_context;
  element_class->change_state = gst_gl_base_filter_change_state;

  g_object_class_install_property (gobject_class, PROP_CONTEXT,
      g_param_spec_object ("context",
          "OpenGL context",
          "Get OpenGL context",
          GST_TYPE_GL_CONTEXT, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  klass->supported_gl_api = GST_GL_API_ANY;
  klass->gl_start = gst_gl_base_filter_default_gl_start;
  klass->gl_stop = gst_gl_base_filter_default_gl_stop;
}

static void
gst_gl_base_filter_init (GstGLBaseFilter * filter)
{
  gst_base_transform_set_qos_enabled (GST_BASE_TRANSFORM (filter), TRUE);

  filter->priv = GST_GL_BASE_FILTER_GET_PRIVATE (filter);
}

static void
gst_gl_base_filter_finalize (GObject * object)
{
  GstGLBaseFilter *filter = GST_GL_BASE_FILTER (object);

  gst_caps_replace (&filter->in_caps, NULL);
  gst_caps_replace (&filter->out_caps, NULL);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_gl_base_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_base_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLBaseFilter *filter = GST_GL_BASE_FILTER (object);

  switch (prop_id) {
    case PROP_CONTEXT:
      g_value_set_object (value, filter->context);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_base_filter_set_context (GstElement * element, GstContext * context)
{
  GstGLBaseFilter *filter = GST_GL_BASE_FILTER (element);
  GstGLBaseFilterClass *filter_class = GST_GL_BASE_FILTER_GET_CLASS (filter);

  gst_gl_handle_set_context (element, context, &filter->display,
      &filter->priv->other_context);
  if (filter->display)
    gst_gl_display_filter_gl_api (filter->display,
        filter_class->supported_gl_api);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
_find_local_gl_context (GstGLBaseFilter * filter)
{
  if (gst_gl_query_local_gl_context (GST_ELEMENT (filter), GST_PAD_SRC,
          &filter->context))
    return TRUE;
  if (gst_gl_query_local_gl_context (GST_ELEMENT (filter), GST_PAD_SINK,
          &filter->context))
    return TRUE;
  return FALSE;
}

static gboolean
gst_gl_base_filter_query (GstBaseTransform * trans, GstPadDirection direction,
    GstQuery * query)
{
  GstGLBaseFilter *filter = GST_GL_BASE_FILTER (trans);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
    {
      if (direction == GST_PAD_SINK
          && gst_base_transform_is_passthrough (trans)) {
        _find_local_gl_context (filter);

        return gst_pad_peer_query (GST_BASE_TRANSFORM_SRC_PAD (trans), query);
      }
      break;
    }
    case GST_QUERY_CONTEXT:
    {
      if (gst_gl_handle_context_query ((GstElement *) filter, query,
              filter->display, filter->context, filter->priv->other_context))
        return TRUE;
      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
      query);
}

static void
gst_gl_base_filter_reset (GstGLBaseFilter * filter)
{
  GstGLBaseFilterClass *filter_class = GST_GL_BASE_FILTER_GET_CLASS (filter);

  if (filter->context) {
    if (filter_class->gl_stop != NULL) {
      gst_gl_context_thread_add (filter->context, gst_gl_base_filter_gl_stop,
          filter);
    }

    gst_object_unref (filter->context);
    filter->context = NULL;
  }
}

static gboolean
gst_gl_base_filter_start (GstBaseTransform * bt)
{
  return TRUE;
}

static gboolean
gst_gl_base_filter_stop (GstBaseTransform * bt)
{
  GstGLBaseFilter *filter = GST_GL_BASE_FILTER (bt);

  gst_gl_base_filter_reset (filter);

  return TRUE;
}

static gboolean
gst_gl_base_filter_default_gl_start (GstGLBaseFilter * filter)
{
  return TRUE;
}

static void
gst_gl_base_filter_gl_start (GstGLContext * context, gpointer data)
{
  GstGLBaseFilter *filter = GST_GL_BASE_FILTER (data);
  GstGLBaseFilterClass *filter_class = GST_GL_BASE_FILTER_GET_CLASS (filter);

  gst_gl_insert_debug_marker (filter->context,
      "starting element %s", GST_OBJECT_NAME (filter));

  filter->priv->gl_result = filter_class->gl_start (filter);
}

static void
gst_gl_base_filter_default_gl_stop (GstGLBaseFilter * filter)
{
}

static void
gst_gl_base_filter_gl_stop (GstGLContext * context, gpointer data)
{
  GstGLBaseFilter *filter = GST_GL_BASE_FILTER (data);
  GstGLBaseFilterClass *filter_class = GST_GL_BASE_FILTER_GET_CLASS (filter);

  gst_gl_insert_debug_marker (filter->context,
      "stopping element %s", GST_OBJECT_NAME (filter));

  if (filter->priv->gl_started)
    filter_class->gl_stop (filter);

  filter->priv->gl_started = FALSE;
}

static void
_gl_set_caps (GstGLContext * context, GstGLBaseFilter * filter)
{
  GstGLBaseFilterClass *filter_class = GST_GL_BASE_FILTER_GET_CLASS (filter);

  if (filter_class->gl_set_caps)
    filter->priv->gl_result =
        filter_class->gl_set_caps (filter, filter->in_caps, filter->out_caps);
}

static gboolean
gst_gl_base_filter_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstGLBaseFilter *filter = GST_GL_BASE_FILTER (trans);
  GstGLBaseFilterClass *filter_class = GST_GL_BASE_FILTER_GET_CLASS (filter);
  GError *error = NULL;
  gboolean new_context = FALSE;

  if (!filter->context)
    new_context = TRUE;

  _find_local_gl_context (filter);

  if (!filter->context) {
    GST_OBJECT_LOCK (filter->display);
    do {
      if (filter->context)
        gst_object_unref (filter->context);
      /* just get a GL context.  we don't care */
      filter->context =
          gst_gl_display_get_gl_context_for_thread (filter->display, NULL);
      if (!filter->context) {
        if (!gst_gl_display_create_context (filter->display,
                filter->priv->other_context, &filter->context, &error)) {
          GST_OBJECT_UNLOCK (filter->display);
          goto context_error;
        }
      }
    } while (!gst_gl_display_add_context (filter->display, filter->context));
    GST_OBJECT_UNLOCK (filter->display);
  }

  if (new_context || !filter->priv->gl_started) {
    if (filter->priv->gl_started)
      gst_gl_context_thread_add (filter->context, gst_gl_base_filter_gl_stop,
          filter);

    {
      GstGLAPI current_gl_api = gst_gl_context_get_gl_api (filter->context);
      if ((current_gl_api & filter_class->supported_gl_api) == 0)
        goto unsupported_gl_api;
    }

    gst_gl_context_thread_add (filter->context, gst_gl_base_filter_gl_start,
        filter);

    if (!filter->priv->gl_result)
      goto error;
  }

  gst_gl_context_thread_add (filter->context,
      (GstGLContextThreadFunc) _gl_set_caps, filter);
  if (!filter->priv->gl_result)
    goto error;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);


unsupported_gl_api:
  {
    GstGLAPI gl_api = gst_gl_context_get_gl_api (filter->context);
    gchar *gl_api_str = gst_gl_api_to_string (gl_api);
    gchar *supported_gl_api_str =
        gst_gl_api_to_string (filter_class->supported_gl_api);
    GST_ELEMENT_ERROR (filter, RESOURCE, BUSY,
        ("GL API's not compatible context: %s supported: %s", gl_api_str,
            supported_gl_api_str), (NULL));

    g_free (supported_gl_api_str);
    g_free (gl_api_str);
    return FALSE;
  }
context_error:
  {
    GST_ELEMENT_ERROR (trans, RESOURCE, NOT_FOUND, ("%s", error->message),
        (NULL));
    g_clear_error (&error);
    return FALSE;
  }
error:
  {
    GST_ELEMENT_ERROR (trans, LIBRARY, INIT,
        ("Subclass failed to initialize."), (NULL));
    return FALSE;
  }
}

static gboolean
gst_gl_base_filter_set_caps (GstBaseTransform * bt, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGLBaseFilter *filter = GST_GL_BASE_FILTER (bt);

  gst_caps_replace (&filter->in_caps, incaps);
  gst_caps_replace (&filter->out_caps, outcaps);

  return TRUE;
}

static GstStateChangeReturn
gst_gl_base_filter_change_state (GstElement * element,
    GstStateChange transition)
{
  GstGLBaseFilter *filter = GST_GL_BASE_FILTER (element);
  GstGLBaseFilterClass *filter_class = GST_GL_BASE_FILTER_GET_CLASS (filter);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG_OBJECT (filter, "changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_gl_ensure_element_data (element, &filter->display,
              &filter->priv->other_context))
        return GST_STATE_CHANGE_FAILURE;

      gst_gl_display_filter_gl_api (filter->display,
          filter_class->supported_gl_api);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (filter->priv->other_context) {
        gst_object_unref (filter->priv->other_context);
        filter->priv->other_context = NULL;
      }

      if (filter->display) {
        gst_object_unref (filter->display);
        filter->display = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}
