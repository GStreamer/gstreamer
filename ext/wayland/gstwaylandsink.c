/*
 * GStreamer Wayland video sink
 *
 * Copyright (C) 2011 Intel Corporation
 * Copyright (C) 2011 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

/* signals */
enum
{
  SIGNAL_0,
  SIGNAL_FRAME_READY,
  LAST_SIGNAL
};

/* Properties */
enum
{
  PROP_0,
  PROP_WAYLAND_DISPLAY
};

GST_DEBUG_CATEGORY (gstwayland_debug);
#define GST_CAT_DEFAULT gstwayland_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "framerate = (fraction) [ 0, MAX ], "
        "endianness = (int) 4321,"
        "red_mask = (int) 65280, "
        "green_mask = (int) 16711680, "
        "blue_mask = (int) -16777216,"
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ] "));

G_DEFINE_TYPE (GstWlBuffer, gst_wlbuffer, GST_TYPE_BUFFER);

/*Fixme: Add more interfaces */
GST_BOILERPLATE (GstWaylandSink, gst_wayland_sink, GstVideoSink,
    GST_TYPE_VIDEO_SINK);

static void gst_wlbuffer_finalize (GstWlBuffer * wbuffer);

static void gst_wayland_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_wayland_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_wayland_sink_finalize (GObject * object);
static GstCaps *gst_wayland_sink_get_caps (GstBaseSink * bsink);
static gboolean gst_wayland_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static gboolean gst_wayland_sink_start (GstBaseSink * bsink);
static gboolean gst_wayland_sink_stop (GstBaseSink * bsink);
static GstFlowReturn
gst_wayland_sink_buffer_alloc (GstBaseSink * bsink, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf);
static gboolean gst_wayland_sink_preroll (GstBaseSink * bsink,
    GstBuffer * buffer);
static gboolean gst_wayland_sink_render (GstBaseSink * bsink,
    GstBuffer * buffer);
static void gst_wayland_bufferpool_clear (GstWaylandSink * sink);
static void
gst_wayland_buffer_destroy (GstWaylandSink * sink, GstWlBuffer * buffer);

static int event_mask_update (uint32_t mask, void *data);
static struct display *create_display (void);
static void display_handle_global (struct wl_display *display, uint32_t id,
    const char *interface, uint32_t version, void *data);
static void redraw (void *data, struct wl_callback *callback, uint32_t time);
static void create_window (GstWaylandSink * sink, struct display *display,
    int width, int height);

static void
gst_wlbuffer_init (GstWlBuffer * buffer)
{
  buffer->wbuffer = NULL;
  buffer->wlsink = NULL;
}

static void
gst_wlbuffer_class_init (GstWlBufferClass * klass)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (klass);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_wlbuffer_finalize;
}

static void
gst_wlbuffer_finalize (GstWlBuffer * wbuffer)
{
  GstWaylandSink *sink = NULL;

  g_return_if_fail (wbuffer != NULL);

  GST_DEBUG_OBJECT (sink, "Finalizing the WlBuffer");
  sink = wbuffer->wlsink;
  if (!sink) {
    GST_WARNING_OBJECT (wbuffer, "No sink..");
    goto beach;
  }

  GST_DEBUG_OBJECT (sink, "recycling buffer %p in pool", wbuffer);
  /* need to increment the refcount again to recycle */
  gst_buffer_ref (GST_BUFFER (wbuffer));
  g_mutex_lock (sink->pool_lock);
  sink->buffer_pool = g_slist_prepend (sink->buffer_pool, wbuffer);
  g_mutex_unlock (sink->pool_lock);

beach:
  return;
}

static void
gst_wayland_sink_base_init (gpointer gclass)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details_simple (element_class,
      "wayland video sink", "Sink/Video",
      "Output to wayland surface",
      "Sreerenj Balachandran <sreerenj.balachandran@intel.com>");
}

static void
gst_wayland_sink_class_init (GstWaylandSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property = gst_wayland_sink_set_property;
  gobject_class->get_property = gst_wayland_sink_get_property;
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_wayland_sink_finalize);

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_wayland_sink_get_caps);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_wayland_sink_set_caps);
  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_wayland_sink_start);
  gstbasesink_class->buffer_alloc =
      GST_DEBUG_FUNCPTR (gst_wayland_sink_buffer_alloc);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_wayland_sink_stop);
  gstbasesink_class->preroll = GST_DEBUG_FUNCPTR (gst_wayland_sink_preroll);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_wayland_sink_render);

  g_object_class_install_property (gobject_class, PROP_WAYLAND_DISPLAY,
      g_param_spec_pointer ("wayland-display", "Wayland Display",
          "Wayland  Display handle created by the application ",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_wayland_sink_init (GstWaylandSink * sink,
    GstWaylandSinkClass * wayland_sink_class)
{

  sink->caps = NULL;
  sink->render_finish = TRUE;
  sink->display = NULL;
  sink->window = NULL;

  sink->pool_lock = g_mutex_new ();
  sink->buffer_pool = NULL;

  sink->wayland_lock = g_mutex_new ();
}

static void
gst_wayland_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (object);

  switch (prop_id) {
    case PROP_WAYLAND_DISPLAY:
      g_value_set_pointer (value, sink->display);
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
    case PROP_WAYLAND_DISPLAY:
      sink->display = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
destroy_display (struct display *display)
{
  if (display->shm)
    wl_shm_destroy (display->shm);

  if (display->shell)
    wl_shell_destroy (display->shell);

  if (display->compositor)
    wl_compositor_destroy (display->compositor);

  wl_display_flush (display->display);
  wl_display_destroy (display->display);
  free (display);
}

static void
destroy_window (struct window *window)
{
  if (window->shell_surface)
    wl_shell_surface_destroy (window->shell_surface);
  if (window->surface)
    wl_surface_destroy (window->surface);
  free (window);
}

static void
gst_wayland_sink_finalize (GObject * object)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (object);

  GST_DEBUG_OBJECT (sink, "Finalizing the sink..");

  if (sink->window)
    destroy_window (sink->window);
  if (sink->display)
    destroy_display (sink->display);

  if (sink->pool_lock) {
    g_mutex_free (sink->pool_lock);
    sink->pool_lock = NULL;
  }

  if (sink->buffer_pool) {
    gst_wayland_bufferpool_clear (sink);
  }

  g_mutex_free (sink->wayland_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_wayland_sink_get_caps (GstBaseSink * bsink)
{
  return gst_caps_copy (gst_static_pad_template_get_caps (&sink_template));
}

static int
event_mask_update (uint32_t mask, void *data)
{
  struct display *d = data;

  d->mask = mask;

  return 0;
}

static void
shm_format (void *data, struct wl_shm *wl_shm, uint32_t format)
{
  struct display *d = data;

  d->formats |= (1 << format);
}

struct wl_shm_listener shm_listenter = {
  shm_format
};

static void
display_handle_global (struct wl_display *display, uint32_t id,
    const char *interface, uint32_t version, void *data)
{
  struct display *d = data;

  if (strcmp (interface, "wl_compositor") == 0) {
    d->compositor = wl_display_bind (display, id, &wl_compositor_interface);
  } else if (strcmp (interface, "wl_shell") == 0) {
    d->shell = wl_display_bind (display, id, &wl_shell_interface);
  } else if (strcmp (interface, "wl_shm") == 0) {
    d->shm = wl_display_bind (display, id, &wl_shm_interface);
    wl_shm_add_listener (d->shm, &shm_listenter, d);
  }

}

static struct display *
create_display (void)
{
  struct display *display;

  display = malloc (sizeof *display);
  display->display = wl_display_connect (NULL);
  assert (display->display);

  wl_display_add_global_listener (display->display,
      display_handle_global, display);

  wl_display_iterate (display->display, WL_DISPLAY_READABLE);
  wl_display_roundtrip (display->display);

  if (!(display->formats & (1 << WL_SHM_FORMAT_XRGB8888))) {
    GST_ERROR ("WL_SHM_FORMAT_XRGB32 not available");
    return NULL;
  }

  wl_display_get_fd (display->display, event_mask_update, display);

  return display;
}

static GstWlBuffer *
wayland_buffer_create (GstWaylandSink * sink)
{
  char filename[1024];
  int fd, size, stride;
  static void *data;
  static int init = 0;
  GstWlBuffer *wbuffer;

  GST_DEBUG_OBJECT (sink, "Creating wayland-shm buffers");

  wbuffer = (GstWlBuffer *) gst_mini_object_new (GST_TYPE_WLBUFFER);
  wbuffer->wlsink = gst_object_ref (sink);

  snprintf (filename, 256, "%s-%d-%s", "/tmp/wayland-shm", init++, "XXXXXX");

  fd = mkstemp (filename);
  if (fd < 0) {
    GST_ERROR_OBJECT (sink, "open %s failed:", filename);
    exit (0);
  }

  stride = sink->video_width * 4;
  size = stride * sink->video_height;

  if (ftruncate (fd, size) < 0) {
    GST_ERROR_OBJECT (sink, "ftruncate failed:");
    close (fd);
    exit (0);
  }

  data = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  unlink (filename);
  if (data == MAP_FAILED) {
    GST_ELEMENT_ERROR (sink, LIBRARY, SHUTDOWN, (NULL),
        ("mmap() failed: %s", strerror (errno)));
    close (fd);
    exit (0);
  }

  wbuffer->wbuffer = wl_shm_create_buffer (sink->display->shm, fd,
      sink->video_width, sink->video_height, stride, WL_SHM_FORMAT_XRGB8888);

  close (fd);

  GST_BUFFER_DATA (wbuffer) = data;
  GST_BUFFER_SIZE (wbuffer) = size;

  return wbuffer;
}

static void
gst_wayland_buffer_destroy (GstWaylandSink * sink, GstWlBuffer * buffer)
{
  if (buffer->wlsink) {
    buffer->wlsink = NULL;
    gst_object_unref (sink);
  }

  GST_MINI_OBJECT_CLASS (gst_wlbuffer_parent_class)->finalize (GST_MINI_OBJECT
      (buffer));
}

static void
gst_wayland_bufferpool_clear (GstWaylandSink * sink)
{
  g_mutex_lock (sink->pool_lock);
  while (sink->buffer_pool) {
    GstWlBuffer *buffer = sink->buffer_pool->data;

    sink->buffer_pool = g_slist_delete_link (sink->buffer_pool,
        sink->buffer_pool);
    gst_wayland_buffer_destroy (sink, buffer);
  }
  g_mutex_unlock (sink->pool_lock);
}

static GstFlowReturn
gst_wayland_sink_buffer_alloc (GstBaseSink * bsink, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (bsink);
  GstWlBuffer *buffer = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  GstStructure *structure = NULL;
  GstCaps *desired_caps = NULL;

  GST_LOG_OBJECT (sink, "a buffer of %u bytes was requested with caps "
      "%" GST_PTR_FORMAT " and offset %" G_GUINT64_FORMAT, size, caps, offset);

  desired_caps = gst_caps_copy (caps);
  structure = gst_caps_get_structure (desired_caps, 0);

  if (gst_structure_get_int (structure, "width", &sink->video_width) &&
      gst_structure_get_int (structure, "height", &sink->video_height)) {
    sink->bpp = size / sink->video_width / sink->video_height;
  }

  g_mutex_lock (sink->pool_lock);
  while (sink->buffer_pool) {
    buffer = (GstWlBuffer *) sink->buffer_pool->data;

    if (buffer) {
      sink->buffer_pool =
          g_slist_delete_link (sink->buffer_pool, sink->buffer_pool);
    } else {
      break;
    }
  }

  g_mutex_unlock (sink->pool_lock);

  if (!buffer)
    buffer = wayland_buffer_create (sink);

  if (buffer)
    gst_buffer_set_caps (GST_BUFFER (buffer), caps);

  *buf = GST_BUFFER (buffer);

  gst_caps_unref (desired_caps);

  return ret;
}

static gboolean
gst_wayland_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (bsink);
  const GstStructure *structure;
  GstCaps *allowed_caps;
  gboolean ret = TRUE;

  GST_LOG_OBJECT (sink, "set caps %" GST_PTR_FORMAT, caps);

  allowed_caps = gst_pad_get_caps (GST_BASE_SINK_PAD (bsink));

  if (!gst_caps_can_intersect (allowed_caps, caps))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  ret &= gst_structure_get_int (structure, "width", &sink->video_width);
  ret &= gst_structure_get_int (structure, "height", &sink->video_height);

  if (!ret)
    return FALSE;

  gst_caps_replace (&sink->caps, caps);

  return TRUE;
}

static const struct wl_callback_listener frame_listener;

static void
redraw (void *data, struct wl_callback *callback, uint32_t time)
{

  GstWaylandSink *sink = (GstWaylandSink *) data;

  sink->render_finish = TRUE;
}

static void
create_window (GstWaylandSink * sink, struct display *display, int width,
    int height)
{
  struct window *window;

  if (sink->window)
    return;

  g_mutex_lock (sink->wayland_lock);

  window = malloc (sizeof *window);
  window->display = display;
  window->width = width;
  window->height = height;
  window->surface = wl_compositor_create_surface (display->compositor);

  window->shell_surface = wl_shell_get_shell_surface (display->shell,
      window->surface);
  /* wl_shell_surface_set_toplevel (window->shell_surface); */
  wl_shell_surface_set_fullscreen (window->shell_surface,
      WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT, 0, NULL);

  sink->window = window;

  g_mutex_unlock (sink->wayland_lock);
}

static gboolean
gst_wayland_sink_start (GstBaseSink * bsink)
{
  GstWaylandSink *sink = (GstWaylandSink *) bsink;
  gboolean result = TRUE;

  GST_DEBUG_OBJECT (sink, "start");

  if (!sink->display)
    sink->display = create_display ();

  return result;
}

static gboolean
gst_wayland_sink_stop (GstBaseSink * bsink)
{
  GstWaylandSink *sink = (GstWaylandSink *) bsink;

  GST_DEBUG_OBJECT (sink, "stop");

  return TRUE;
}

static GstFlowReturn
gst_wayland_sink_preroll (GstBaseSink * bsink, GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (bsink, "preroll buffer %p, data = %p", buffer,
      GST_BUFFER_DATA (buffer));
  return gst_wayland_sink_render (bsink, buffer);
}

static GstFlowReturn
gst_wayland_sink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstWaylandSink *sink = GST_WAYLAND_SINK (bsink);
  gboolean mem_cpy = TRUE;
  GstVideoRectangle src, dst, res;

  GST_LOG_OBJECT (sink,
      "render buffer %p, data = %p, timestamp = %" GST_TIME_FORMAT, buffer,
      GST_BUFFER_DATA (buffer), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

  if (!sink->window)
    create_window (sink, sink->display, sink->video_width, sink->video_height);

  if (sink->render_finish) {
    if (GST_IS_WLBUFFER (buffer)) {
      GstWlBuffer *tmp_buffer = (GstWlBuffer *) buffer;

      /* Does it have a waylandbuffer ? */
      if (tmp_buffer->wbuffer) {
        mem_cpy = FALSE;
        GST_DEBUG_OBJECT (sink, "we have a buffer (%p) we allocated "
            "ourselves and it has a wayland buffer, no memcpy then", buffer);
        sink->window->buffer = tmp_buffer->wbuffer;
      } else {
        /* No wayland buffer, that's a malloc */
        GST_DEBUG_OBJECT (sink, "we have a buffer (%p) we allocated "
            "ourselves but it does not hold a wayland buffer", buffer);
      }
    } else {
      /* Not our baby! */
      GST_DEBUG_OBJECT (sink, "we have a buffer (%p) we did not allocate",
          buffer);
    }

    if (mem_cpy) {

      GstWlBuffer *wlbuf = wayland_buffer_create (sink);

      memcpy (GST_BUFFER_DATA (wlbuf), GST_BUFFER_DATA (buffer),
          GST_BUFFER_SIZE (buffer));
      sink->window->buffer = wlbuf->wbuffer;
    }

    src.w = sink->video_width;
    src.h = sink->video_height;
    dst.w = sink->window->width;
    dst.h = sink->window->height;

    gst_video_sink_center_rect (src, dst, &res, FALSE);

    sink->render_finish = FALSE;

    wl_buffer_damage (sink->window->buffer, 0, 0, res.w, res.h);

    wl_surface_attach (sink->window->surface, sink->window->buffer, 0, 0);

    wl_surface_damage (sink->window->surface, 0, 0, res.w, res.h);

    if (sink->callback)
      wl_callback_destroy (sink->callback);

    sink->callback = wl_surface_frame (sink->window->surface);
    wl_callback_add_listener (sink->callback, &frame_listener, sink);
    wl_display_iterate (sink->display->display, sink->display->mask);

  } else
    GST_LOG_OBJECT (sink,
        "Waiting to get the signal from compositor to render the next frame..");

  return GST_FLOW_OK;
}

static const struct wl_callback_listener frame_listener = {
  redraw
};

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
