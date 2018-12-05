/* Copyright (C) <2018> Philippe Normand <philn@igalia.com>
 * Copyright (C) <2018> Žan Doberšek <zdobersek@igalia.com>
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
 * SECTION:element-wpesrc
 * @title: wpesrc
 *
 * The wpesrc element is used to produce a video texture representing a web page
 * rendered off-screen by WPE.
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 -v wpesrc location="https://gstreamer.freedesktop.org" ! queue ! glimagesink
 * ]|
 * Shows the GStreamer website homepage
 *
 * |[
 * gst-play-1.0 --videosink gtkglsink wpe://https://gstreamer.freedesktop.org
 * ]|
 * Shows the GStreamer website homepage as played with GstPlayer in a GTK+ window.
 *
 * |[
 * gst-launch-1.0  glvideomixer name=m sink_1::zorder=0 ! glimagesink wpesrc location="file:///home/phil/Downloads/plunk/index.html" draw-background=0 ! m. videotestsrc ! queue ! glupload ! glcolorconvert ! m.
 * ]|
 * Composite WPE with a video stream in a single OpenGL scene.
 *
 * |[
 * gst-launch-1.0 glvideomixer name=m sink_1::zorder=0 sink_0::height=818 sink_0::width=1920 ! gtkglsink wpesrc location="file:///home/phil/Downloads/plunk/index.html" draw-background=0 ! m. uridecodebin uri="http://192.168.1.44/Sintel.2010.1080p.mkv" name=d d. ! queue ! glupload ! glcolorconvert ! m.
 * ]|
 * Composite WPE with a video stream, sink_0 pad properties have to match the video dimensions.
 */

/*
 * TODO:
 * - Audio support (requires an AudioSession implementation in WebKit and a WPEBackend-fdo API for it)
 * - DMABuf support (requires changes in WPEBackend-fdo to expose DMABuf planes and fds)
 * - Custom EGLMemory allocator
 * - Better navigation events handling (would require a new GstNavigation API)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwpesrc.h"
#include <gst/video/video.h>
#include <gst/gl/gl.h>
#include <gst/gl/egl/gstglmemoryegl.h>
#include <gst/gl/wayland/gstgldisplay_wayland.h>
#include <xkbcommon/xkbcommon.h>

#include "WPEThreadedView.h"

GST_DEBUG_CATEGORY (wpe_src_debug);
#define GST_CAT_DEFAULT wpe_src_debug

#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080
#define DEFAULT_FPS_N 30
#define DEFAULT_FPS_D 1

#define SUPPORTED_GL_APIS static_cast<GstGLAPI>(GST_GL_API_OPENGL | GST_GL_API_OPENGL3 | GST_GL_API_GLES2)

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_DRAW_BACKGROUND
};

enum
{
  SIGNAL_CONFIGURE_WEB_VIEW,
  LAST_SIGNAL
};
static guint gst_wpe_src_signals[LAST_SIGNAL] = { 0 };

struct _GstWpeSrc
{
  GstPushSrc parent;
  GstGLDisplay *display;
  GstGLContext *context, *other_context;
  GstVideoInfo out_info;
  GstCaps *out_caps;
  GstGLMemoryAllocator *allocator;
  GstGLVideoAllocationParams *gl_alloc_params;
  guint64 n_frames;
  gchar *location;
  gboolean draw_background;
  gboolean negotiated;
  WPEThreadedView *view;
};

static void gst_wpe_src_uri_handler_init (gpointer iface, gpointer data);

#define gst_wpe_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWpeSrc, gst_wpe_src, GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_wpe_src_uri_handler_init));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(memory:GLMemory), "
        "format = (string) RGBA, "
        "width = " GST_VIDEO_SIZE_RANGE ", "
        "height = " GST_VIDEO_SIZE_RANGE ", "
        "framerate = " GST_VIDEO_FPS_RANGE ", "
        "pixel-aspect-ratio = (fraction)1/1")
    );

static GstFlowReturn
gst_wpe_src_create (GstPushSrc * psrc, GstBuffer ** buffer)
{
  GstWpeSrc *src = GST_WPE_SRC (psrc);
  GstGLSyncMeta *sync_meta;
  GstEGLImage *img = src->view->image ();
  GstGLVideoAllocationParams *alloc_params = src->gl_alloc_params;
  GstGLFormat formats[1] { GST_GL_RGBA };
  gpointer imgs[1] { NULL };

  if (G_UNLIKELY (!src->negotiated || !src->context))
    goto not_negotiated;

  g_return_val_if_fail(img != NULL, GST_FLOW_ERROR);

  *buffer = gst_buffer_new ();
  imgs[0] = (gpointer) img;
  alloc_params->parent.gl_handle = img;
  alloc_params->plane = 0;
  gst_gl_memory_setup_buffer (src->allocator, *buffer, alloc_params, formats, imgs, 1);

  sync_meta = gst_buffer_get_gl_sync_meta (*buffer);
  if (sync_meta)
    gst_gl_sync_meta_set_sync_point (sync_meta, src->context);

  GST_BUFFER_OFFSET (*buffer) = src->n_frames;
  src->n_frames++;
  GST_BUFFER_OFFSET_END (*buffer) = src->n_frames;

  return GST_FLOW_OK;

not_negotiated:
  {
    GST_ELEMENT_ERROR (src, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before get function"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static gboolean
gst_wpe_src_start (GstBaseSrc * base_src)
{
  GstWpeSrc *src = GST_WPE_SRC (base_src);

  GST_INFO_OBJECT (src, "Starting up");
  GST_OBJECT_LOCK (src);

  src->n_frames = 0;
  src->negotiated = FALSE;
  src->allocator = GST_GL_MEMORY_ALLOCATOR (gst_allocator_find (GST_GL_MEMORY_EGL_ALLOCATOR_NAME));
  src->gl_alloc_params = NULL;
  src->view = new WPEThreadedView;

  GST_OBJECT_UNLOCK (src);
  return TRUE;
}

static gboolean
gst_wpe_src_stop (GstBaseSrc * base_src)
{
  GstWpeSrc *src = GST_WPE_SRC (base_src);

  GST_INFO_OBJECT (src, "Stopping");
  GST_OBJECT_LOCK (src);

  if (src->gl_alloc_params) {
    gst_gl_allocation_params_free ((GstGLAllocationParams *)
        src->gl_alloc_params);
    src->gl_alloc_params = NULL;
  }

  gst_caps_replace (&src->out_caps, NULL);

  if (src->context)
    g_clear_object (&src->context);

  delete src->view;
  src->view = NULL;

  if (src->allocator)
    g_clear_object (&src->allocator);

  GST_OBJECT_UNLOCK (src);
  return TRUE;
}

static void
gst_wpe_src_get_times (GstBaseSrc * base_src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstClockTime timestamp = GST_BUFFER_PTS (buffer);
  GstClockTime duration = GST_BUFFER_DURATION (buffer);

  *end = timestamp + duration;
  *start = timestamp;

  GST_LOG_OBJECT (base_src,
      "Got times start: %" GST_TIME_FORMAT " end: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (*start), GST_TIME_ARGS (*end));
}

static gboolean
gst_wpe_src_query (GstBaseSrc * base_src, GstQuery * query)
{
  gboolean res = FALSE;
  GstWpeSrc *src = GST_WPE_SRC (base_src);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      if (gst_gl_handle_context_query ((GstElement *) src, query,
              src->display, src->context, src->other_context)) {

        return TRUE;
      }
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
      res = GST_BASE_SRC_CLASS (parent_class)->query (base_src, query);
      break;
  }

  return res;
}

static GstCaps *
gst_wpe_src_fixate (GstBaseSrc * base_src, GstCaps * caps)
{
  GstStructure *structure;

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", DEFAULT_WIDTH);
  gst_structure_fixate_field_nearest_int (structure, "height", DEFAULT_HEIGHT);

  if (gst_structure_has_field (structure, "framerate"))
    gst_structure_fixate_field_nearest_fraction (structure, "framerate",
        DEFAULT_FPS_N, DEFAULT_FPS_D);
  else
    gst_structure_set (structure, "framerate", GST_TYPE_FRACTION, DEFAULT_FPS_N,
        DEFAULT_FPS_D, NULL);

  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (base_src, caps);
  GST_INFO_OBJECT (base_src, "Fixated caps to %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_wpe_src_set_caps (GstBaseSrc * base_src, GstCaps * caps)
{
  GstWpeSrc *src = GST_WPE_SRC (base_src);

  GST_INFO_OBJECT (base_src, "Caps set to %" GST_PTR_FORMAT, caps);
  if (!gst_video_info_from_caps (&src->out_info, caps))
    goto wrong_caps;

  if (src->view) {
    src->view->resize (GST_VIDEO_INFO_WIDTH (&src->out_info),
        GST_VIDEO_INFO_HEIGHT (&src->out_info));
  }

  src->negotiated = TRUE;
  gst_caps_replace (&src->out_caps, caps);
  return TRUE;

wrong_caps:
  {
    GST_WARNING ("wrong caps");
    return FALSE;
  }
}

static void
gst_wpe_src_set_context (GstElement * element, GstContext * context)
{
  GstWpeSrc *src = GST_WPE_SRC (element);

  gst_gl_handle_set_context (element, context, &src->display,
      &src->other_context);

  if (src->display)
    gst_gl_display_filter_gl_api (src->display, SUPPORTED_GL_APIS);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
_find_local_gl_context (GstWpeSrc * src)
{
  if (gst_gl_query_local_gl_context (GST_ELEMENT (src), GST_PAD_SRC,
          &src->context))
    return TRUE;
  return FALSE;
}

static void
_src_initialize_wpe_view (GstGLContext * context, GstWpeSrc * src)
{
  src->view->initialize (src, context, src->display,
      GST_VIDEO_INFO_WIDTH (&src->out_info),
      GST_VIDEO_INFO_HEIGHT (&src->out_info));
}

void
gst_wpe_src_configure_web_view (GstWpeSrc * src, WebKitWebView * webview)
{
  GValue args[2] = { {0}, {0} };

  g_value_init (&args[0], GST_TYPE_ELEMENT);
  g_value_set_object (&args[0], src);
  g_value_init (&args[1], G_TYPE_OBJECT);
  g_value_set_object (&args[1], webview);

  g_signal_emitv (args, gst_wpe_src_signals[SIGNAL_CONFIGURE_WEB_VIEW], 0,
      NULL);

  g_value_unset (&args[0]);
  g_value_unset (&args[1]);
}

static gboolean
gst_wpe_src_decide_allocation (GstBaseSrc * basesrc, GstQuery * query)
{
  GstWpeSrc *src = GST_WPE_SRC (basesrc);
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;
  GError *error = NULL;
  GstAllocationParams *alloc_params = NULL;
  guint plane = 1;
  GstVideoAlignment *valign = NULL;
  GstGLTextureTarget target = GST_GL_TEXTURE_TARGET_2D;
  GstGLFormat tex_format = GST_GL_RGBA;

  if (!gst_gl_ensure_element_data (src, &src->display, &src->other_context))
    return FALSE;

  gst_gl_display_filter_gl_api (src->display, SUPPORTED_GL_APIS);

  _find_local_gl_context (src);

  if (!src->context) {
    GST_OBJECT_LOCK (src->display);
    do {
      if (src->context) {
        gst_object_unref (src->context);
        src->context = NULL;
      }

      src->context =
          gst_gl_display_get_gl_context_for_thread (src->display, NULL);
      if (!src->context) {
        if (!gst_gl_display_create_context (src->display, src->other_context,
                &src->context, &error)) {
          GST_OBJECT_UNLOCK (src->display);
          goto context_error;
        }
      }
    } while (!gst_gl_display_add_context (src->display, src->context));
    GST_OBJECT_UNLOCK (src->display);
  }

  if ((gst_gl_context_get_gl_api (src->context) & SUPPORTED_GL_APIS) == 0)
    goto unsupported_gl_api;

  gst_gl_context_thread_add (src->context,
      (GstGLContextThreadFunc) _src_initialize_wpe_view, src);

  gst_query_parse_allocation (query, &caps, NULL);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;

    gst_video_info_init (&vinfo);
    gst_video_info_from_caps (&vinfo, caps);
    size = vinfo.size;
    min = max = 1;
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

  gst_buffer_pool_set_config (pool, config);

  src->gl_alloc_params = gst_gl_video_allocation_params_new_wrapped_gl_handle
      (src->context,
      alloc_params,
      &src->out_info, plane, valign, target, tex_format, NULL, NULL, NULL);

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
    gchar *supported_gl_api_str = gst_gl_api_to_string (SUPPORTED_GL_APIS);
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
}

static GstStateChangeReturn
gst_wpe_src_change_state (GstElement * element, GstStateChange transition)
{
  GstWpeSrc *src = GST_WPE_SRC (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG_OBJECT (src, "changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (src->display != NULL && !GST_IS_GL_DISPLAY_WAYLAND (src->display)) {
        GST_ERROR_OBJECT (src,
            "wpesrc currently only supports Wayland GstGLDisplays %"
            GST_PTR_FORMAT, src->display);
        ret = GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (src->other_context)
        g_clear_object (&src->other_context);

      if (src->display)
        g_clear_object (&src->display);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_wpe_src_set_location (GstWpeSrc * src, const gchar * location,
    GError ** error)
{
  g_free (src->location);
  src->location = g_strdup (location);
  if (src->view)
    src->view->loadUri (src->location);

  return TRUE;
}

static void
gst_wpe_src_set_draw_background (GstWpeSrc * src, gboolean draw_background)
{
  if (src->view)
    src->view->setDrawBackground (draw_background);
  src->draw_background = draw_background;
}

static void
gst_wpe_src_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstWpeSrc *src = GST_WPE_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
    {
      const gchar *location;

      location = g_value_get_string (value);
      if (location == NULL) {
        GST_WARNING_OBJECT (src, "location property cannot be NULL");
        return;
      }

      if (!gst_wpe_src_set_location (src, location, NULL)) {
        GST_WARNING_OBJECT (src, "badly formatted location");
        return;
      }
      break;
    }
    case PROP_DRAW_BACKGROUND:
      gst_wpe_src_set_draw_background (src, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wpe_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstWpeSrc *src = GST_WPE_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, src->location);
      break;
    case PROP_DRAW_BACKGROUND:
      g_value_set_boolean (value, src->draw_background);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_wpe_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret = FALSE;
  GstWpeSrc *src = GST_WPE_SRC (parent);

  if (GST_EVENT_TYPE (event) == GST_EVENT_NAVIGATION) {
    const gchar *key;
    gint button;
    gdouble x, y;

    GST_DEBUG_OBJECT (src, "Processing event %" GST_PTR_FORMAT, event);
    switch (gst_navigation_event_get_type (event)) {
      case GST_NAVIGATION_EVENT_KEY_PRESS:
      case GST_NAVIGATION_EVENT_KEY_RELEASE:
        if (gst_navigation_event_parse_key_event (event, &key)) {
          /* FIXME: This is wrong... The GstNavigation API should pass
             hardware-level informations, not high-level keysym strings */
          uint32_t keysym =
              (uint32_t) xkb_keysym_from_name (key, XKB_KEYSYM_NO_FLAGS);
          struct wpe_input_keyboard_event wpe_event;
          wpe_event.key_code = keysym;
          wpe_event.pressed =
              gst_navigation_event_get_type (event) ==
              GST_NAVIGATION_EVENT_KEY_PRESS;
          wpe_view_backend_dispatch_keyboard_event (src->view->backend (),
              &wpe_event);
          ret = TRUE;
        }
        break;
      case GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS:
      case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE:
        if (gst_navigation_event_parse_mouse_button_event (event, &button, &x,
                &y)) {
          struct wpe_input_pointer_event wpe_event;
          wpe_event.type = wpe_input_pointer_event_type_button;
          wpe_event.x = (int) x;
          wpe_event.y = (int) y;
          if (button == 1) {
            wpe_event.modifiers = wpe_input_pointer_modifier_button1;
          } else if (button == 2) {
            wpe_event.modifiers = wpe_input_pointer_modifier_button2;
          } else if (button == 3) {
            wpe_event.modifiers = wpe_input_pointer_modifier_button3;
          } else if (button == 4) {
            wpe_event.modifiers = wpe_input_pointer_modifier_button4;
          } else if (button == 5) {
            wpe_event.modifiers = wpe_input_pointer_modifier_button5;
          }
          wpe_event.button = button;
          wpe_event.state = 1;
          wpe_event.state =
              gst_navigation_event_get_type (event) ==
              GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS;
          wpe_view_backend_dispatch_pointer_event (src->view->backend (),
              &wpe_event);
          ret = TRUE;
        }
        break;
      case GST_NAVIGATION_EVENT_MOUSE_MOVE:
        if (gst_navigation_event_parse_mouse_move_event (event, &x, &y)) {
          struct wpe_input_pointer_event wpe_event;
          wpe_event.type = wpe_input_pointer_event_type_motion;
          wpe_event.x = (int) x;
          wpe_event.y = (int) y;
          wpe_view_backend_dispatch_pointer_event (src->view->backend (),
              &wpe_event);
          ret = TRUE;
        }
        break;
      default:
        break;
    }
    /* FIXME: No touch events handling support in GstNavigation */
  }

  if (!ret) {
    ret = gst_pad_event_default (pad, parent, event);
  }
  return ret;
}

static void
gst_wpe_src_init (GstWpeSrc * src)
{
  GstBaseSrc *base_src = GST_BASE_SRC (src);
  GstPad *pad = gst_element_get_static_pad (GST_ELEMENT_CAST (src), "src");

  gst_pad_set_event_function (pad, gst_wpe_src_event);
  gst_object_unref (pad);

  src->n_frames = 0;
  src->draw_background = TRUE;

  gst_base_src_set_format (base_src, GST_FORMAT_TIME);
  gst_base_src_set_live (base_src, TRUE);
  gst_base_src_set_do_timestamp (base_src, TRUE);
}

static GstURIType
gst_wpe_src_uri_get_type (GType)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_wpe_src_get_protocols (GType)
{
  static const char *protocols[] = { "wpe", NULL };
  return protocols;
}

static gchar *
gst_wpe_src_get_uri (GstURIHandler * handler)
{
  GstWpeSrc *src = GST_WPE_SRC (handler);
  return g_strdup_printf ("wpe://%s", src->location);
}

static gboolean
gst_wpe_src_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstWpeSrc *src = GST_WPE_SRC (handler);

  return gst_wpe_src_set_location (src, uri + 6, error);
}

static void
gst_wpe_src_uri_handler_init (gpointer iface_ptr, gpointer data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) iface_ptr;

  iface->get_type = gst_wpe_src_uri_get_type;
  iface->get_protocols = gst_wpe_src_get_protocols;
  iface->get_uri = gst_wpe_src_get_uri;
  iface->set_uri = gst_wpe_src_set_uri;
}

static void
gst_wpe_src_class_init (GstWpeSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstPushSrcClass *push_src_class = GST_PUSH_SRC_CLASS (klass);
  GstBaseSrcClass *base_src_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_wpe_src_set_property;
  gobject_class->get_property = gst_wpe_src_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "location",
          "The URL to display",
          "", (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_DRAW_BACKGROUND,
      g_param_spec_boolean ("draw-background", "Draws the background",
          "Whether to draw the WebView background", TRUE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (gstelement_class,
      "WPE source", "Source/Video",
      "Creates a video stream from a WPE browser",
      "Philippe Normand <philn@igalia.com>, Žan Doberšek <zdobersek@igalia.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);

  gstelement_class->set_context = GST_DEBUG_FUNCPTR (gst_wpe_src_set_context);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_wpe_src_change_state);

  base_src_class->fixate = GST_DEBUG_FUNCPTR (gst_wpe_src_fixate);
  base_src_class->set_caps = GST_DEBUG_FUNCPTR (gst_wpe_src_set_caps);
  base_src_class->start = GST_DEBUG_FUNCPTR (gst_wpe_src_start);
  base_src_class->stop = GST_DEBUG_FUNCPTR (gst_wpe_src_stop);
  base_src_class->get_times = GST_DEBUG_FUNCPTR (gst_wpe_src_get_times);
  base_src_class->query = GST_DEBUG_FUNCPTR (gst_wpe_src_query);
  base_src_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_wpe_src_decide_allocation);

  push_src_class->create = GST_DEBUG_FUNCPTR (gst_wpe_src_create);

  /**
   * GstWpeSrc::configure-web-view:
   * @src: the object which received the signal
   * @webview: the webView
   *
   * Allow application to configure the webView settings.
   */
  gst_wpe_src_signals[SIGNAL_CONFIGURE_WEB_VIEW] =
      g_signal_new ("configure-web-view", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, G_TYPE_OBJECT);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (wpe_src_debug, "wpesrc", 0, "WPE Source");

  return gst_element_register (plugin, "wpesrc", GST_RANK_NONE,
      GST_TYPE_WPE_SRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    wpe, "WPE src plugin", plugin_init, VERSION, GST_LICENSE, PACKAGE,
    GST_PACKAGE_ORIGIN)
