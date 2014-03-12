/* GStreamer Wayland video sink
 *
 * Copyright (C) 2011 Intel Corporation
 * Copyright (C) 2011 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 * Copyright (C) 2012 Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) 2014 Collabora Ltd.
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:element-waylandsink
 *
 *  The waylandsink is creating its own window and render the decoded video frames to that.
 *  Setup the Wayland environment as described in
 *  <ulink url="http://wayland.freedesktop.org/building.html">Wayland</ulink> home page.
 *  The current implementaion is based on weston compositor.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v videotestsrc ! waylandsink
 * ]| test the video rendering in wayland
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwaylandsink.h"
#include "wlvideoformat.h"
#include "waylandpool.h"

#include <gst/wayland/wayland.h>
#include <gst/video/videooverlay.h>

/* signals */
enum
{
  SIGNAL_0,
  LAST_SIGNAL
};

/* Properties */
enum
{
  PROP_0,
  PROP_DISPLAY
};

GST_DEBUG_CATEGORY (gstwayland_debug);
#define GST_CAT_DEFAULT gstwayland_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{ BGRx, BGRA, RGBx, xBGR, xRGB, RGBA, ABGR, ARGB, RGB, BGR, "
            "RGB16, BGR16, YUY2, YVYU, UYVY, AYUV, NV12, NV21, NV16, "
            "YUV9, YVU9, Y41B, I420, YV12, Y42B, v308 }"))
    );

static void gst_wayland_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_wayland_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_wayland_sink_finalize (GObject * object);

static GstStateChangeReturn gst_wayland_sink_change_state (GstElement * element,
    GstStateChange transition);

static GstCaps *gst_wayland_sink_get_caps (GstBaseSink * bsink,
    GstCaps * filter);
static gboolean gst_wayland_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static gboolean gst_wayland_sink_preroll (GstBaseSink * bsink,
    GstBuffer * buffer);
static gboolean
gst_wayland_sink_propose_allocation (GstBaseSink * bsink, GstQuery * query);
static gboolean gst_wayland_sink_render (GstBaseSink * bsink,
    GstBuffer * buffer);

/* VideoOverlay interface */
static void gst_wayland_sink_videooverlay_init (GstVideoOverlayInterface *
    iface);
static void gst_wayland_sink_set_window_handle (GstVideoOverlay * overlay,
    guintptr handle);
static void gst_wayland_sink_expose (GstVideoOverlay * overlay);

/* WaylandVideo interface */
static void gst_wayland_sink_waylandvideo_init (GstWaylandVideoInterface *
    iface);
static void gst_wayland_sink_set_surface_size (GstWaylandVideo * video,
    gint w, gint h);
static void gst_wayland_sink_pause_rendering (GstWaylandVideo * video);
static void gst_wayland_sink_resume_rendering (GstWaylandVideo * video);

#define gst_wayland_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstWaylandSink, gst_wayland_sink, GST_TYPE_VIDEO_SINK,
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_wayland_sink_videooverlay_init)
    G_IMPLEMENT_INTERFACE (GST_TYPE_WAYLAND_VIDEO,
        gst_wayland_sink_waylandvideo_init));

static void
gst_wayland_sink_class_init (GstWaylandSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property = gst_wayland_sink_set_property;
  gobject_class->get_property = gst_wayland_sink_get_property;
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_wayland_sink_finalize);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "wayland video sink", "Sink/Video",
      "Output to wayland surface",
      "Sreerenj Balachandran <sreerenj.balachandran@intel.com>, "
      "George Kiagiadakis <george.kiagiadakis@collabora.com>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_wayland_sink_change_state);

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_wayland_sink_get_caps);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_wayland_sink_set_caps);
  gstbasesink_class->preroll = GST_DEBUG_FUNCPTR (gst_wayland_sink_preroll);
  gstbasesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_wayland_sink_propose_allocation);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_wayland_sink_render);

  g_object_class_install_property (gobject_class, PROP_DISPLAY,
      g_param_spec_string ("display", "Wayland Display name", "Wayland "
          "display name to connect to, if not supplied with GstVideoOverlay",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_wayland_sink_init (GstWaylandSink * sink)
{
  g_cond_init (&sink->render_cond);
}

static void
gst_wayland_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      g_value_set_string (value, sink->display_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wayland_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (object);

  switch (prop_id) {
    case PROP_DISPLAY:
      sink->display_name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wayland_sink_finalize (GObject * object)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (object);

  GST_DEBUG_OBJECT (sink, "Finalizing the sink..");

  if (sink->last_buffer)
    gst_buffer_unref (sink->last_buffer);
  if (sink->display) {
    /* see comment about this call in gst_wayland_sink_change_state() */
    if (sink->pool) {
      gst_wayland_compositor_release_all_buffers (GST_WAYLAND_BUFFER_POOL
          (sink->pool));
    }
    g_object_unref (sink->display);
  }
  if (sink->window)
    g_object_unref (sink->window);
  if (sink->pool)
    gst_object_unref (sink->pool);

  if (sink->display_name)
    g_free (sink->display_name);

  g_cond_clear (&sink->render_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_wayland_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!sink->window)
        gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (sink));

      /* if nobody set a window handle, create at least a display */
      if (!sink->display) {
        GError *error = NULL;

        sink->display = gst_wl_display_new (sink->display_name, &error);

        if (sink->display == NULL) {
          GST_ELEMENT_WARNING (sink, RESOURCE, OPEN_READ_WRITE,
              ("Could not initialise Wayland output"),
              ("Failed to create GstWlDisplay: '%s'", error->message));
          g_error_free (error);
          return GST_STATE_CHANGE_FAILURE;
        }
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (gst_wl_window_is_toplevel (sink->window)) {
        gst_buffer_replace (&sink->last_buffer, NULL);
        g_clear_object (&sink->window);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (sink->display && !sink->window) {     /* -> the window was toplevel */
        /* Force all buffers to return to the pool, regardless of
         * whether the compositor has released them or not. We are
         * going to kill the display, so we need to return all buffers
         * to be destroyed before this happens.
         * Note that this is done here instead of the pool destructor
         * because the buffers hold a reference to the pool. Also,
         * the buffers can only be unref'ed from the display's event loop
         * and the pool holds a reference to the display. If we drop
         * our references here, when the compositor releases the buffers,
         * they will be unref'ed from the event loop thread, which will
         * unref the pool and therefore the display, which will try to
         * stop the thread from within itself and cause a deadlock.
         */
        if (sink->pool) {
          gst_wayland_compositor_release_all_buffers (GST_WAYLAND_BUFFER_POOL
              (sink->pool));
        }
        g_clear_object (&sink->display);
        g_clear_object (&sink->pool);
      }
      break;
    default:
      break;
  }

  return ret;
}

static GstCaps *
gst_wayland_sink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstWaylandSink *sink;
  GstCaps *caps;

  sink = GST_WAYLAND_SINK (bsink);

  caps = gst_pad_get_pad_template_caps (GST_VIDEO_SINK_PAD (sink));

  if (sink->display) {
    GValue list = G_VALUE_INIT;
    GValue value = G_VALUE_INIT;
    GArray *formats;
    gint i;
    enum wl_shm_format fmt;

    g_value_init (&list, GST_TYPE_LIST);
    g_value_init (&value, G_TYPE_STRING);

    formats = sink->display->formats;
    for (i = 0; i < formats->len; i++) {
      fmt = g_array_index (formats, uint32_t, i);
      g_value_set_string (&value, gst_wayland_format_to_string (fmt));
      gst_value_list_append_value (&list, &value);
    }

    caps = gst_caps_make_writable (caps);
    gst_structure_set_value (gst_caps_get_structure (caps, 0), "format", &list);

    GST_DEBUG_OBJECT (sink, "display caps: %" GST_PTR_FORMAT, caps);
  }

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = intersection;
  }

  return caps;
}

static gboolean
gst_wayland_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstWaylandSink *sink;
  GstBufferPool *newpool;
  GstVideoInfo info;
  enum wl_shm_format format;
  GArray *formats;
  gint i;
  GstStructure *structure;
  static GstAllocationParams params = { 0, 0, 0, 15, };

  sink = GST_WAYLAND_SINK (bsink);
  GST_OBJECT_LOCK (sink);

  GST_DEBUG_OBJECT (sink, "set caps %" GST_PTR_FORMAT, caps);

  /* extract info from caps */
  if (!gst_video_info_from_caps (&info, caps))
    goto invalid_format;

  format = gst_video_format_to_wayland_format (GST_VIDEO_INFO_FORMAT (&info));
  if (format == -1)
    goto invalid_format;

  /* store the video size */
  sink->video_width = info.width;
  sink->video_height = info.height;

  /* verify we support the requested format */
  formats = sink->display->formats;
  for (i = 0; i < formats->len; i++) {
    if (g_array_index (formats, uint32_t, i) == format)
      break;
  }

  if (i >= formats->len)
    goto unsupported_format;

  /* create a new pool for the new configuration */
  newpool = gst_wayland_buffer_pool_new (sink->display);
  if (!newpool)
    goto pool_failed;

  structure = gst_buffer_pool_get_config (newpool);
  gst_buffer_pool_config_set_params (structure, caps, info.size, 2, 0);
  gst_buffer_pool_config_set_allocator (structure, NULL, &params);
  if (!gst_buffer_pool_set_config (newpool, structure))
    goto config_failed;

  gst_object_replace ((GstObject **) & sink->pool, (GstObject *) newpool);
  gst_object_unref (newpool);

  GST_OBJECT_UNLOCK (sink);
  return TRUE;

invalid_format:
  {
    GST_DEBUG_OBJECT (sink,
        "Could not locate image format from caps %" GST_PTR_FORMAT, caps);
    goto failure;
  }
unsupported_format:
  {
    GST_DEBUG_OBJECT (sink, "Format %s is not available on the display",
        gst_wayland_format_to_string (format));
    goto failure;
  }
pool_failed:
  {
    GST_DEBUG_OBJECT (sink, "Failed to create new pool");
    goto failure;
  }
config_failed:
  {
    GST_DEBUG_OBJECT (bsink, "failed setting config");
    goto failure;
  }
failure:
  {
    GST_OBJECT_UNLOCK (sink);
    return FALSE;
  }
}

static gboolean
gst_wayland_sink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (bsink);
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint size;
  gboolean need_pool;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if (sink->pool)
    pool = gst_object_ref (sink->pool);

  if (pool != NULL) {
    GstCaps *pcaps;

    /* we had a pool, check caps */
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

    if (!gst_caps_is_equal (caps, pcaps)) {
      /* different caps, we can't use this pool */
      gst_object_unref (pool);
      pool = NULL;
    }
    gst_structure_free (config);
  }

  if (pool == NULL && need_pool) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps))
      goto invalid_caps;

    GST_DEBUG_OBJECT (sink, "create new pool");
    pool = gst_wayland_buffer_pool_new (sink->display);

    /* the normal size of a frame */
    size = info.size;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 2, 0);
    if (!gst_buffer_pool_set_config (pool, config))
      goto config_failed;
  }
  if (pool) {
    gst_query_add_allocation_pool (query, pool, size, 2, 0);
    gst_object_unref (pool);
  }

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (bsink, "no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_DEBUG_OBJECT (bsink, "invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_DEBUG_OBJECT (bsink, "failed setting config");
    gst_object_unref (pool);
    return FALSE;
  }
}

static GstFlowReturn
gst_wayland_sink_preroll (GstBaseSink * bsink, GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (bsink, "preroll buffer %p", buffer);
  return gst_wayland_sink_render (bsink, buffer);
}

static void
frame_redraw_callback (void *data, struct wl_callback *callback, uint32_t time)
{
  GstWaylandSink *sink = data;

  GST_LOG ("frame_redraw_cb");

  g_atomic_int_set (&sink->redraw_pending, FALSE);
  wl_callback_destroy (callback);
}

static const struct wl_callback_listener frame_callback_listener = {
  frame_redraw_callback
};

/* must be called with the object lock */
static void
render_last_buffer (GstWaylandSink * sink)
{
  GstWlMeta *meta;
  struct wl_surface *surface;
  struct wl_callback *callback;
  GstVideoRectangle src, dst, res;

  meta = gst_buffer_get_wl_meta (sink->last_buffer);
  surface = gst_wl_window_get_wl_surface (sink->window);

  g_atomic_int_set (&sink->redraw_pending, TRUE);
  callback = wl_surface_frame (surface);
  wl_callback_add_listener (callback, &frame_callback_listener, sink);

  /* Here we essentially add a reference to the buffer. This represents
   * the fact that the compositor is using the buffer and it should
   * not return back to the pool and be reused until the compositor
   * releases it. The release is handled internally in the pool */
  gst_wayland_compositor_acquire_buffer (meta->pool, sink->last_buffer);

  src.w = sink->video_width;
  src.h = sink->video_height;
  dst.w = sink->window->width;
  dst.h = sink->window->height;
  gst_video_sink_center_rect (src, dst, &res, FALSE);

  wl_viewport_set (sink->window->viewport, wl_fixed_from_int (0),
      wl_fixed_from_int (0), wl_fixed_from_int (src.w),
      wl_fixed_from_int (src.h), res.w, res.h);

  wl_surface_attach (surface, meta->wbuffer, 0, 0);
  wl_surface_damage (surface, 0, 0, res.w, res.h);

  wl_surface_commit (surface);
  wl_display_flush (sink->display->display);
}

static GstFlowReturn
gst_wayland_sink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (bsink);
  GstBuffer *to_render;
  GstWlMeta *meta;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_OBJECT_LOCK (sink);

  GST_LOG_OBJECT (sink, "render buffer %p", buffer);

  if (!sink->window)
    sink->window = gst_wl_window_new_toplevel (sink->display, sink->video_width,
        sink->video_height);

  /* surface is resizing - drop buffers until finished */
  if (sink->drawing_frozen)
    goto done;

  /* drop buffers until we get a frame callback */
  if (g_atomic_int_get (&sink->redraw_pending) == TRUE)
    goto done;

  meta = gst_buffer_get_wl_meta (buffer);

  if (meta && meta->pool->display == sink->display) {
    GST_LOG_OBJECT (sink, "buffer %p from our pool, writing directly", buffer);
    to_render = buffer;
  } else {
    GstMapInfo src;
    GST_LOG_OBJECT (sink, "buffer %p not from our pool, copying", buffer);

    if (!sink->pool)
      goto no_pool;

    if (!gst_buffer_pool_set_active (sink->pool, TRUE))
      goto activate_failed;

    ret = gst_buffer_pool_acquire_buffer (sink->pool, &to_render, NULL);
    if (ret != GST_FLOW_OK)
      goto no_buffer;

    gst_buffer_map (buffer, &src, GST_MAP_READ);
    gst_buffer_fill (to_render, 0, src.data, src.size);
    gst_buffer_unmap (buffer, &src);
  }

  gst_buffer_replace (&sink->last_buffer, to_render);
  render_last_buffer (sink);

  /* notify _resume_rendering() in case it's waiting */
  sink->rendered = TRUE;
  g_cond_broadcast (&sink->render_cond);

  if (buffer != to_render)
    gst_buffer_unref (to_render);
  goto done;

no_buffer:
  {
    GST_WARNING_OBJECT (sink, "could not create image");
    goto done;
  }
no_pool:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE,
        ("Internal error: can't allocate images"),
        ("We don't have a bufferpool negotiated"));
    ret = GST_FLOW_ERROR;
    goto done;
  }
activate_failed:
  {
    GST_ERROR_OBJECT (sink, "failed to activate bufferpool.");
    ret = GST_FLOW_ERROR;
    goto done;
  }
done:
  {
    GST_OBJECT_UNLOCK (sink);
    return ret;
  }
}

static void
gst_wayland_sink_videooverlay_init (GstVideoOverlayInterface * iface)
{
  iface->set_window_handle = gst_wayland_sink_set_window_handle;
  iface->expose = gst_wayland_sink_expose;
}

static void
gst_wayland_sink_set_window_handle (GstVideoOverlay * overlay, guintptr handle)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (overlay);
  GstWaylandWindowHandle *whandle = (GstWaylandWindowHandle *) handle;
  GError *error = NULL;

  g_return_if_fail (sink != NULL);
  g_return_if_fail (GST_STATE (sink) < GST_STATE_PAUSED);

  GST_OBJECT_LOCK (sink);

  GST_DEBUG_OBJECT (sink, "Setting window handle %" GST_PTR_FORMAT,
      (void *) handle);

  g_clear_object (&sink->window);
  g_clear_object (&sink->display);

  if (handle) {
    sink->display =
        gst_wl_display_new_existing (whandle->display, FALSE, &error);
    if (error) {
      GST_ELEMENT_WARNING (sink, RESOURCE, OPEN_READ_WRITE,
          ("Could not set window handle"),
          ("Failed to use the external wayland display: '%s'", error->message));
      g_error_free (error);
    } else {
      wl_proxy_set_queue ((struct wl_proxy *) whandle->surface,
          sink->display->queue);
      sink->window = gst_wl_window_new_from_surface (sink->display,
          whandle->surface, whandle->width, whandle->height);
    }
  }

  if (!sink->display && GST_STATE (sink) == GST_STATE_READY) {
    /* we need a display to be in READY */
    sink->display = gst_wl_display_new (sink->display_name, &error);

    if (sink->display == NULL) {
      GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_READ_WRITE,
          ("Could not initialise Wayland output"),
          ("Failed to create GstWlDisplay: '%s'", error->message));
      g_error_free (error);
    }
  }

  GST_OBJECT_UNLOCK (sink);
}

static void
gst_wayland_sink_expose (GstVideoOverlay * overlay)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (overlay);

  g_return_if_fail (sink != NULL);

  GST_DEBUG_OBJECT (sink, "expose");

  GST_OBJECT_LOCK (sink);
  if (sink->last_buffer && g_atomic_int_get (&sink->redraw_pending) == FALSE) {
    GST_DEBUG_OBJECT (sink, "redrawing last buffer");
    render_last_buffer (sink);
  }
  GST_OBJECT_UNLOCK (sink);
}

static void
gst_wayland_sink_waylandvideo_init (GstWaylandVideoInterface * iface)
{
  iface->set_surface_size = gst_wayland_sink_set_surface_size;
  iface->pause_rendering = gst_wayland_sink_pause_rendering;
  iface->resume_rendering = gst_wayland_sink_resume_rendering;
}

static void
gst_wayland_sink_set_surface_size (GstWaylandVideo * video, gint w, gint h)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (video);

  g_return_if_fail (sink != NULL);
  g_return_if_fail (sink->window != NULL);

  GST_OBJECT_LOCK (sink);
  if (!sink->window) {
    GST_OBJECT_UNLOCK (sink);
    GST_WARNING_OBJECT (sink,
        "set_surface_size called without window, ignoring");
    return;
  }

  GST_DEBUG_OBJECT (sink, "changing window size to %d x %d", w, h);
  gst_wl_window_set_size (sink->window, w, h);
  GST_OBJECT_UNLOCK (sink);
}

static void
gst_wayland_sink_pause_rendering (GstWaylandVideo * video)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (video);
  g_return_if_fail (sink != NULL);

  GST_OBJECT_LOCK (sink);
  sink->drawing_frozen = TRUE;
  GST_OBJECT_UNLOCK (sink);
}

static void
gst_wayland_sink_resume_rendering (GstWaylandVideo * video)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (video);
  g_return_if_fail (sink != NULL);

  GST_DEBUG_OBJECT (sink, "resuming rendering");

  GST_OBJECT_LOCK (sink);
  sink->drawing_frozen = FALSE;

  if (GST_STATE (sink) == GST_STATE_PLAYING) {
    sink->rendered = FALSE;
    while (sink->rendered == FALSE)
      g_cond_wait (&sink->render_cond, GST_OBJECT_GET_LOCK (sink));
    GST_DEBUG_OBJECT (sink, "synchronized with render()");
  } else if (sink->window && sink->last_buffer &&
      g_atomic_int_get (&sink->redraw_pending) == FALSE) {
    render_last_buffer (sink);
    GST_DEBUG_OBJECT (sink, "last buffer redrawn");
  }

  GST_OBJECT_UNLOCK (sink);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gstwayland_debug, "waylandsink", 0,
      " wayland video sink");

  return gst_element_register (plugin, "waylandsink", GST_RANK_MARGINAL,
      GST_TYPE_WAYLAND_SINK);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    waylandsink,
    "Wayland Video Sink", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
