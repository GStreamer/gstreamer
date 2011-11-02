/*
 * GStreamer Wayland video sink
 *
 * Copyright: Intel Corporation
 * Copyright: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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


/* The waylandsink is currently just a prototype . It creates its own window and render the decoded video frames to that.*/

/* FixMe: Needs to add more synchronization stuffs */
/* FixMe: Remove the extra memcopy by giving buffer to decoder with buffer_alloc*/
/* FixMe: Add signals so that the application/compositor is responsible for rendering */
/* FixMe: Add h/w decoding support: buffers/libva surface */
/* FixMe: Add the interfaces */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>

#include "gstwaylandsink.h"

#include <wayland-client.h>
#include <wayland-egl.h>

/* signals */
enum
{
  SIGNAL_0,
  SIGNAL_FRAME_READY,
  LAST_SIGNAL
};

/* Properties */
/*Fixme: Not yet implemented */
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

GType gst_wayland_sink_get_type (void);

/*Fixme: Add more interfaces */
GST_BOILERPLATE (GstWayLandSink, gst_wayland_sink, GstVideoSink,
    GST_TYPE_VIDEO_SINK);

static void gst_wayland_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_wayland_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_wayland_sink_dispose (GObject * object);
static void gst_wayland_sink_finalize (GObject * object);

static GstCaps *gst_wayland_sink_get_caps (GstBaseSink * bsink);
static gboolean gst_wayland_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static gboolean gst_wayland_sink_start (GstBaseSink * bsink);
static gboolean gst_wayland_sink_stop (GstBaseSink * bsink);
static gboolean gst_wayland_sink_unlock (GstBaseSink * bsink);
static gboolean gst_wayland_sink_unlock_stop (GstBaseSink * bsink);
static gboolean gst_wayland_sink_preroll (GstBaseSink * bsink,
    GstBuffer * buffer);
static gboolean gst_wayland_sink_render (GstBaseSink * bsink,
    GstBuffer * buffer);

static gboolean create_shm_buffer (GstWayLandSink * sink);
static int event_mask_update (uint32_t mask, void *data);
static void sync_callback (void *data);
static struct display *create_display (void);
static void display_handle_global (struct wl_display *display, uint32_t id,
    const char *interface, uint32_t version, void *data);
static void compositor_handle_visual (void *data,
    struct wl_compositor *compositor, uint32_t id, uint32_t token);
static void redraw (struct wl_surface *surface, void *data, uint32_t time);
static struct window *create_window (GstWayLandSink * sink,
    struct display *display, int width, int height);

static guint gst_wayland_sink_signals[LAST_SIGNAL] = { 0 };

static void
gst_wayland_sink_base_init (gpointer gclass)
{

  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details_simple (element_class,
      "wayland video sink", "Sink/Video",
      "Output to wayland surface",
      "Sreerenj Balachandran <sreerenj.balachandran@intel.com>,");
}

static void
gst_wayland_sink_class_init (GstWayLandSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property = gst_wayland_sink_set_property;
  gobject_class->get_property = gst_wayland_sink_get_property;
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_wayland_sink_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_wayland_sink_finalize);

  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_wayland_sink_get_caps);
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_wayland_sink_set_caps);
  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_wayland_sink_start);
  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_wayland_sink_unlock);
  gstbasesink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_wayland_sink_unlock_stop);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_wayland_sink_stop);
  gstbasesink_class->preroll = GST_DEBUG_FUNCPTR (gst_wayland_sink_preroll);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_wayland_sink_render);

  g_object_class_install_property (gobject_class, PROP_WAYLAND_DISPLAY,
      g_param_spec_pointer ("wayland-display", "WayLand Display",
          "WayLand  Display id created by the application ",
          G_PARAM_READWRITE));

  /*Fixme: not using now */
  gst_wayland_sink_signals[SIGNAL_FRAME_READY] =
      g_signal_new ("frame-ready",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0, NULL, NULL,
      g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);

  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_wayland_sink_init (GstWayLandSink * sink,
    GstWayLandSinkClass * wayland_sink_class)
{

  sink->caps = NULL;

  sink->buffer_cond = g_cond_new ();
  sink->buffer_lock = g_mutex_new ();

  sink->wayland_cond = g_cond_new ();
  sink->wayland_lock = g_mutex_new ();

  sink->render_finish = TRUE;
}

static void
gst_wayland_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstWayLandSink *sink = GST_WAYLAND_SINK (object);

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
  GstWayLandSink *sink = GST_WAYLAND_SINK (object);

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
gst_wayland_sink_dispose (GObject * object)
{
  GstWayLandSink *sink = GST_WAYLAND_SINK (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_wayland_sink_finalize (GObject * object)
{
  GstWayLandSink *sink = GST_WAYLAND_SINK (object);

  gst_caps_replace (&sink->caps, NULL);
  
  free (sink->display);
  free (sink->window);
  
  g_cond_free (sink->buffer_cond);
  g_cond_free (sink->wayland_cond);
  g_mutex_free (sink->buffer_lock);
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
sync_callback (void *data)
{
  int *done = data;

  *done = 1;
}

static const struct wl_compositor_listener compositor_listener = {
  compositor_handle_visual,
};

static void
display_handle_global (struct wl_display *display, uint32_t id,
    const char *interface, uint32_t version, void *data)
{
  struct display *d = data;

  if (strcmp (interface, "wl_compositor") == 0) {
    d->compositor = wl_compositor_create (display, id, 1);
    wl_compositor_add_listener (d->compositor, &compositor_listener, d);
  } else if (strcmp (interface, "wl_shell") == 0) {
    d->shell = wl_shell_create (display, id, 1);
  } else if (strcmp (interface, "wl_shm") == 0) {
    d->shm = wl_shm_create (display, id, 1);
  }

}

static struct display *
create_display (void)
{
  struct display *display;
  int done;

  display = malloc (sizeof *display);
  display->display = wl_display_connect (NULL);

  wl_display_add_global_listener (display->display,
      display_handle_global, display);
  wl_display_iterate (display->display, WL_DISPLAY_READABLE);

  wl_display_get_fd (display->display, event_mask_update, display);

  wl_display_sync_callback (display->display, sync_callback, &done);

  while (!display->xrgb_visual) {
    wl_display_iterate (display->display, display->mask);
  }

  return display;
}

static gboolean
create_shm_buffer (GstWayLandSink * sink)
{
  char filename[] = "/tmp/wayland-shm-XXXXXX";
  struct wl_buffer *wbuffer;
  int i, fd, size, stride;
  static void *data;

  GST_DEBUG_OBJECT (sink, "Creating wayland-shm buffers");

  wl_display_iterate (sink->display->display, sink->display->mask);

  fd = mkstemp (filename);
  if (fd < 0) {
    fprintf (stderr, "open %s failed: %m\n", filename);
    exit (0);
  }

  stride = sink->width * 4;
  size = stride * sink->height;

  if (ftruncate (fd, size) < 0) {
    fprintf (stderr, "ftruncate failed: %m\n");
    close (fd);
    exit (0);
  }

  data = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  unlink (filename);
  if (data == MAP_FAILED) {
    fprintf (stderr, "mmap failed: %m\n");
    close (fd);
    exit (0);
  }

  wbuffer = wl_shm_create_buffer (sink->display->shm, fd,
      sink->width, sink->height, stride, sink->display->xrgb_visual);

  close (fd);

  sink->window->buffer = wbuffer;
  wl_surface_attach (sink->window->surface, sink->window->buffer, 0, 0);
  sink->MapAddr = data;

  return TRUE;
}

static gboolean
gst_wayland_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstWayLandSink *sink = GST_WAYLAND_SINK (bsink);
  const GstStructure *structure;
  GstFlowReturn result = GST_FLOW_OK;
  GstCaps *allowed_caps;
  gboolean ret = TRUE;
  GstCaps *intersection;
  const GValue *fps;

  GST_LOG_OBJECT (sink, "set caps %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);

  allowed_caps = gst_pad_get_caps (GST_BASE_SINK_PAD (bsink));

  /* We intersect those caps with our template to make sure they are correct */
  intersection = gst_caps_intersect (allowed_caps, caps);
  gst_caps_unref (allowed_caps);

  if (gst_caps_is_empty (intersection)) {
    gst_caps_unref (intersection);
    return FALSE;
  }

  structure = gst_caps_get_structure (caps, 0);

  ret &= gst_structure_get_int (structure, "width", &sink->width);
  ret &= gst_structure_get_int (structure, "height", &sink->height);

  if (!ret)
    return FALSE;

  gst_caps_replace (&sink->caps, caps);

  gst_caps_unref (intersection);

  if (!create_shm_buffer (sink)) {
    GST_ERROR_OBJECT (sink, "Failed to create the wayland buffers..");
    return FALSE;
  }

  return TRUE;
}

static void
compositor_handle_visual (void *data,
    struct wl_compositor *compositor, uint32_t id, uint32_t token)
{
  struct display *d = data;

  switch (token) {
    case WL_COMPOSITOR_VISUAL_XRGB32:
      d->xrgb_visual = wl_visual_create (d->display, id, 1);
      break;
  }
}

static void
redraw (struct wl_surface *surface, void *data, uint32_t time)
{

  GstWayLandSink *sink = (GstWayLandSink *) data;
  g_mutex_lock (sink->wayland_lock);

  sink->render_finish = TRUE;

  g_mutex_unlock (sink->wayland_lock);
}

static struct window *
create_window (GstWayLandSink * sink, struct display *display, int width,
    int height)
{
  struct window *window;
  struct wl_visual *visual;
  void *data;

  g_mutex_lock (sink->wayland_lock);

  window = malloc (sizeof *window);
  window->display = display;
  window->width = width;
  window->height = height;
  window->surface = wl_compositor_create_surface (display->compositor);

  wl_shell_set_toplevel (display->shell, window->surface);

  g_mutex_unlock (sink->wayland_lock);
  return window;
}


static gboolean
gst_wayland_sink_start (GstBaseSink * bsink)
{
  GstWayLandSink *sink = (GstWayLandSink *) bsink;
  gboolean result = TRUE;

  GST_DEBUG_OBJECT (sink, "start");

  if (!sink->display)
    sink->display = create_display ();
  if (!sink->window)
    sink->window = create_window (sink, sink->display, 1280, 720);

  return result;
}

static gboolean
gst_wayland_sink_unlock (GstBaseSink * bsink)
{
  GstWayLandSink *sink = (GstWayLandSink *) bsink;

  GST_DEBUG_OBJECT (sink, "unlock");

  g_mutex_lock (sink->buffer_lock);

  sink->unlock = TRUE;

  g_mutex_unlock (sink->buffer_lock);

  return TRUE;
}

static gboolean
gst_wayland_sink_unlock_stop (GstBaseSink * bsink)
{
  GstWayLandSink *sink = (GstWayLandSink *) bsink;

  GST_DEBUG_OBJECT (sink, "unlock_stop");

  g_mutex_lock (sink->buffer_lock);

  sink->unlock = FALSE;

  g_mutex_unlock (sink->buffer_lock);

  return TRUE;
}


static gboolean
gst_wayland_sink_stop (GstBaseSink * bsink)
{
  GstWayLandSink *sink = (GstWayLandSink *) bsink;

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
  GstWayLandSink *sink = GST_WAYLAND_SINK (bsink);

  if (sink->render_finish) {
    GST_LOG_OBJECT (sink,
        "render buffer %p, data = %p, timestamp = %" GST_TIME_FORMAT, buffer,
        GST_BUFFER_DATA (buffer),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

    /*Fixme: remove the memcpy and add memory allocation stuffs to buff_alloc */
    guint8 *src = GST_BUFFER_DATA (buffer);
    guint len = GST_BUFFER_SIZE (buffer) / sink->height;

    /*for (i = 0; i < sink->height; i++) {
       memcpy (data, src, len);
       src += len;
       data += len;
       } */

    memcpy (sink->MapAddr, src, GST_BUFFER_SIZE (buffer));

    sink->render_finish = FALSE;

    wl_buffer_damage (sink->window->buffer, 0, 0, sink->width, sink->height);

    wl_surface_damage (sink->window->surface, 0, 0, sink->width, sink->height);

    wl_display_frame_callback (sink->display->display,
        sink->window->surface, redraw, sink);

    wl_display_iterate (sink->display->display, sink->display->mask);

  } else {
    GST_LOG_OBJECT (sink,
        "Waiting to get the signal from compositor to render the next frame..");
    g_usleep (50000);
    sink->render_finish = TRUE;
  }
  return GST_FLOW_OK;
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
    "waylandsink",
    "WayLand Video Sink", plugin_init, VERSION, "LGPL", "gst-wayland", "")
