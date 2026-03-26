/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
 * Copyright (C) 2021 Collabora Ltd.
 *   @author George Kiagiadakis <george.kiagiadakis@collabora.com>
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

#include "gstgtkwaylandsink.h"
#include "gstgtkutils.h"
#include "gtkgstwaylandwidget.h"

#include <drm_fourcc.h>
#include <gdk/gdk.h>
#include <gst/video/gstvideodmabufpool.h>
#include <gst/wayland/wayland.h>

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#else
#error "Wayland is not supported in GTK+"
#endif

#define GST_CAT_DEFAULT gst_debug_gtk_wayland_sink
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_DMA_DRM_CAPS_MAKE ";"
        GST_VIDEO_CAPS_MAKE (GST_WL_VIDEO_FORMATS))
    );

static void gst_gtk_wayland_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_gtk_wayland_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_gtk_wayland_sink_finalize (GObject * object);

static GstStateChangeReturn gst_gtk_wayland_sink_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_gtk_wayland_sink_event (GstBaseSink * sink,
    GstEvent * event);
static GstCaps *gst_gtk_wayland_sink_get_caps (GstBaseSink * bsink,
    GstCaps * filter);
static gboolean gst_gtk_wayland_sink_set_caps (GstBaseSink * bsink,
    GstCaps * caps);
static gboolean gst_gtk_wayland_sink_propose_allocation (GstBaseSink * bsink,
    GstQuery * query);
static GstFlowReturn gst_gtk_wayland_sink_show_frame (GstVideoSink * bsink,
    GstBuffer * buffer);
static void gst_gtk_wayland_sink_set_rotate_method (GstGtkWaylandSink * self,
    GstVideoOrientationMethod method, gboolean from_tag);

static void
gst_gtk_wayland_sink_navigation_interface_init (GstNavigationInterface * iface);

static void
calculate_adjustment (GtkWidget * start_widget, GtkAllocation * allocation);

enum
{
  PROP_0,
  PROP_WIDGET,
  PROP_DISPLAY,
  PROP_ROTATE_METHOD,
  PROP_DRM_DEVICE,
};

typedef struct _GstGtkWaylandSinkPrivate
{
  GtkWidget *gtk_widget;
  GtkWidget *gtk_window;
  gulong gtk_window_destroy_id;

  /* from GstWaylandSink */
  GMutex display_lock;
  GstWlDisplay *display;

  GstWlWindow *wl_window;
  gboolean is_wl_window_sync;

  GstBuffer *last_buffer;

  GstVideoInfo render_info;
  GstVideoInfo video_info;
  GstVideoInfoDmaDrm drm_info;
  GstVideoMasteringDisplayInfo minfo;
  GstVideoContentLightLevel linfo;
  gboolean have_mastering_info;
  gboolean have_light_info;
  GstCaps *caps;

  GMutex render_lock;

  GstVideoOrientationMethod sink_rotate_method;
  GstVideoOrientationMethod tag_rotate_method;
  GstVideoOrientationMethod current_rotate_method;

  gchar *drm_device;
} GstGtkWaylandSinkPrivate;

#define gst_gtk_wayland_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGtkWaylandSink, gst_gtk_wayland_sink,
    GST_TYPE_VIDEO_SINK, G_ADD_PRIVATE (GstGtkWaylandSink)
    G_IMPLEMENT_INTERFACE (GST_TYPE_NAVIGATION,
        gst_gtk_wayland_sink_navigation_interface_init)
    GST_DEBUG_CATEGORY_INIT (gst_debug_gtk_wayland_sink,
        "gtkwaylandsink", 0, "Gtk Wayland Video sink");
    );
GST_ELEMENT_REGISTER_DEFINE (gtkwaylandsink, "gtkwaylandsink",
    GST_RANK_MARGINAL, GST_TYPE_GTK_WAYLAND_SINK);

static void
gst_gtk_wayland_sink_class_init (GstGtkWaylandSinkClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseSinkClass *gstbasesink_class = (GstBaseSinkClass *) klass;
  GstVideoSinkClass *gstvideosink_class = (GstVideoSinkClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_gtk_wayland_sink_finalize);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_gtk_wayland_sink_get_property);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_gtk_wayland_sink_set_property);

  g_object_class_install_property (gobject_class, PROP_WIDGET,
      g_param_spec_object ("widget", "Gtk Widget",
          "The GtkWidget to place in the widget hierarchy "
          "(must only be get from the GTK main thread)",
          GTK_TYPE_WIDGET,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_DOC_SHOW_DEFAULT));

  g_object_class_install_property (gobject_class, PROP_ROTATE_METHOD,
      g_param_spec_enum ("rotate-method",
          "rotate method",
          "rotate method",
          GST_TYPE_VIDEO_ORIENTATION_METHOD, GST_VIDEO_ORIENTATION_IDENTITY,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));

  /**
   * GstGtkWaylandSink:drm-device:
   *
   * Since: 1.24
   */
  g_object_class_install_property (gobject_class, PROP_DRM_DEVICE,
      g_param_spec_string ("drm-device", "DRM Device", "Path of the "
          "DRM device to use for dumb buffer allocation",
          NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_gtk_wayland_sink_change_state);

  gst_element_class_set_static_metadata (gstelement_class,
      "Gtk Wayland Video Sink", "Sink/Video",
      "A video sink that renders to a GtkWidget using Wayland API",
      "George Kiagiadakis <george.kiagiadakis@collabora.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_gtk_wayland_sink_event);
  gstbasesink_class->get_caps =
      GST_DEBUG_FUNCPTR (gst_gtk_wayland_sink_get_caps);
  gstbasesink_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_gtk_wayland_sink_set_caps);
  gstbasesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_gtk_wayland_sink_propose_allocation);

  gstvideosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_gtk_wayland_sink_show_frame);
}

static void
gst_gtk_wayland_sink_init (GstGtkWaylandSink * self)
{
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);

  g_mutex_init (&priv->display_lock);
  g_mutex_init (&priv->render_lock);
}

static void
gst_gtk_wayland_sink_finalize (GObject * object)
{
  GstGtkWaylandSink *self = GST_GTK_WAYLAND_SINK (object);
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);

  gst_clear_object (&priv->display);
  gst_clear_object (&priv->wl_window);

  g_clear_object (&priv->gtk_widget);
  gst_clear_caps (&priv->caps);

  g_free (priv->drm_device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
widget_destroy_cb (GtkWidget * widget, GstGtkWaylandSink * self)
{
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);

  GST_OBJECT_LOCK (self);
  g_clear_object (&priv->gtk_widget);
  GST_OBJECT_UNLOCK (self);
}

static void
window_destroy_cb (GtkWidget * widget, GstGtkWaylandSink * self)
{
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);

  GST_OBJECT_LOCK (self);
  gst_wl_window_flush (priv->wl_window);
  gst_clear_object (&priv->wl_window);
  priv->gtk_window = NULL;
  GST_OBJECT_UNLOCK (self);

  GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, ("Window was closed"), (NULL));
}

static gboolean
widget_size_allocate_cb (GtkWidget * widget, GtkAllocation * allocation,
    gpointer user_data)
{
  GstGtkWaylandSink *self = user_data;
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);
  struct wl_subsurface *window_subsurface;

  g_mutex_lock (&priv->render_lock);

  priv->is_wl_window_sync = TRUE;

  window_subsurface = gst_wl_window_get_subsurface (priv->wl_window);
  if (window_subsurface)
    wl_subsurface_set_sync (window_subsurface);

  calculate_adjustment (priv->gtk_widget, allocation);

  GST_DEBUG_OBJECT (self, "window geometry changed to (%d, %d) %d x %d",
      allocation->x, allocation->y, allocation->width, allocation->height);
  gst_wl_window_set_render_rectangle (priv->wl_window, allocation->x,
      allocation->y, allocation->width, allocation->height);

  g_mutex_unlock (&priv->render_lock);

  return FALSE;
}

static gboolean
window_after_after_paint_cb (GtkWidget * widget, gpointer user_data)
{
  GstGtkWaylandSink *self = user_data;
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);

  g_mutex_lock (&priv->render_lock);

  if (priv->is_wl_window_sync) {
    struct wl_subsurface *window_subsurface;

    priv->is_wl_window_sync = FALSE;

    window_subsurface = gst_wl_window_get_subsurface (priv->wl_window);
    if (window_subsurface)
      wl_subsurface_set_desync (window_subsurface);
  }

  g_mutex_unlock (&priv->render_lock);

  return FALSE;
}

static GtkWidget *
gst_gtk_wayland_sink_get_widget (GstGtkWaylandSink * self)
{
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);

  if (priv->gtk_widget != NULL)
    return g_object_ref (priv->gtk_widget);

  /* Ensure GTK is initialized, this has no side effect if it was already
   * initialized. Also, we do that lazily, so the application can be first */
  if (!gtk_init_check (NULL, NULL)) {
    GST_INFO_OBJECT (self, "Could not ensure GTK initialization.");
    return NULL;
  }

  priv->gtk_widget = gtk_gst_wayland_widget_new ();
  gtk_gst_base_widget_set_element (GTK_GST_BASE_WIDGET (priv->gtk_widget),
      GST_ELEMENT (self));

  /* Take the floating ref, other wise the destruction of the container will
   * make this widget disappear possibly before we are done. */
  g_object_ref_sink (priv->gtk_widget);
  g_signal_connect_object (priv->gtk_widget, "destroy",
      G_CALLBACK (widget_destroy_cb), self, 0);

  return g_object_ref (priv->gtk_widget);
}

static GtkWidget *
gst_gtk_wayland_sink_acquire_widget (GstGtkWaylandSink * self)
{
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);
  gpointer widget = NULL;

  GST_OBJECT_LOCK (self);
  if (priv->gtk_widget != NULL)
    widget = g_object_ref (priv->gtk_widget);
  GST_OBJECT_UNLOCK (self);

  if (!widget)
    widget =
        gst_gtk_invoke_on_main ((GThreadFunc) gst_gtk_wayland_sink_get_widget,
        self);

  return widget;
}

static gboolean
gst_gtk_wayland_sink_event (GstBaseSink * sink, GstEvent * event)
{
  GstGtkWaylandSink *self = GST_GTK_WAYLAND_SINK (sink);
  GstTagList *taglist;
  GstVideoOrientationMethod method;
  gboolean ret;

  GST_DEBUG_OBJECT (self, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
      gst_gtk_wayland_sink_set_rotate_method (self,
          GST_VIDEO_ORIENTATION_IDENTITY, TRUE);
      break;
    case GST_EVENT_TAG:
      gst_event_parse_tag (event, &taglist);

      if (gst_video_orientation_from_tag (taglist, &method)) {
        gst_gtk_wayland_sink_set_rotate_method (self, method, TRUE);
      } else {
        gst_gtk_wayland_sink_set_rotate_method (self,
            GST_VIDEO_ORIENTATION_IDENTITY, TRUE);
      }

      break;
    default:
      break;
  }

  ret = GST_BASE_SINK_CLASS (parent_class)->event (sink, event);

  return ret;
}

static void
gst_gtk_wayland_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGtkWaylandSink *self = GST_GTK_WAYLAND_SINK (object);
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);

  switch (prop_id) {
    case PROP_WIDGET:
      g_value_take_object (value, gst_gtk_wayland_sink_acquire_widget (self));
      break;
    case PROP_ROTATE_METHOD:
      g_value_set_enum (value, priv->current_rotate_method);
      break;
    case PROP_DRM_DEVICE:
      GST_OBJECT_LOCK (self);
      g_value_set_string (value, priv->drm_device);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gtk_wayland_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGtkWaylandSink *self = GST_GTK_WAYLAND_SINK (object);
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);

  switch (prop_id) {
    case PROP_ROTATE_METHOD:
      gst_gtk_wayland_sink_set_rotate_method (self, g_value_get_enum (value),
          FALSE);
      break;
    case PROP_DRM_DEVICE:
      /* G_PARAM_CONSTRUCT_ONLY */
      GST_OBJECT_LOCK (self);
      priv->drm_device = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
calculate_adjustment (GtkWidget * widget, GtkAllocation * allocation)
{
  GdkWindow *window;
  gint wx, wy;

  window = gtk_widget_get_window (widget);
  gdk_window_get_origin (window, &wx, &wy);

  allocation->x = wx;
  allocation->y = wy;
}

static gboolean
scrollable_window_adjustment_changed_cb (GtkAdjustment * adjustment,
    gpointer user_data)
{
  GstGtkWaylandSink *self = user_data;
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);
  GtkAllocation allocation;

  gtk_widget_get_allocation (priv->gtk_widget, &allocation);
  calculate_adjustment (priv->gtk_widget, &allocation);
  gst_wl_window_set_render_rectangle (priv->wl_window, allocation.x,
      allocation.y, allocation.width, allocation.height);

  return FALSE;
}

static void
wl_window_map_cb (GstWlWindow * wl_window, GstGtkWaylandSink * self)
{
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);

  GST_DEBUG_OBJECT (self, "waylandsink surface is ready");

  gtk_gst_base_widget_queue_draw (GTK_GST_BASE_WIDGET (priv->gtk_widget));
}

static void
setup_wl_window (GstGtkWaylandSink * self)
{
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);
  GdkWindow *gdk_window;
  GdkFrameClock *gdk_frame_clock;
  GtkAllocation allocation;
  GtkWidget *widget;

  g_mutex_lock (&priv->render_lock);

  gdk_window = gtk_widget_get_window (priv->gtk_widget);
  g_assert (gdk_window);

  if (!priv->wl_window) {
    struct wl_surface *wl_surface;

    wl_surface = gdk_wayland_window_get_wl_surface (gdk_window);

    GST_INFO_OBJECT (self, "setting window handle");

    priv->wl_window = gst_wl_window_new_in_surface (priv->display,
        wl_surface, &priv->render_lock);
    gst_wl_window_set_rotate_method (priv->wl_window,
        priv->current_rotate_method);
    if (priv->caps)
      gst_wl_window_set_source_info (priv->wl_window, &priv->drm_info,
          &priv->video_info, priv->caps, priv->drm_device);
    g_signal_connect_object (priv->wl_window, "map",
        G_CALLBACK (wl_window_map_cb), self, 0);
  }

  /* In order to position the subsurface correctly within a scrollable widget,
   * we can not rely on the allocation alone but need to take the window
   * origin into account
   */
  widget = priv->gtk_widget;
  do {
    if (GTK_IS_SCROLLABLE (widget)) {
      GtkAdjustment *hadjustment;
      GtkAdjustment *vadjustment;

      hadjustment = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (widget));
      vadjustment = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (widget));

      g_signal_connect (hadjustment, "value-changed",
          G_CALLBACK (scrollable_window_adjustment_changed_cb), self);
      g_signal_connect (vadjustment, "value-changed",
          G_CALLBACK (scrollable_window_adjustment_changed_cb), self);
    }
  } while ((widget = gtk_widget_get_parent (widget)));

  gtk_widget_get_allocation (priv->gtk_widget, &allocation);
  calculate_adjustment (priv->gtk_widget, &allocation);
  gst_wl_window_set_render_rectangle (priv->wl_window, allocation.x,
      allocation.y, allocation.width, allocation.height);

  /* Make subsurfaces syncronous during resizes.
   * Unfortunately GTK/GDK does not provide easier to use signals.
   */
  g_signal_connect (priv->gtk_widget, "size-allocate",
      G_CALLBACK (widget_size_allocate_cb), self);
  gdk_frame_clock = gdk_window_get_frame_clock (gdk_window);
  g_signal_connect_after (gdk_frame_clock, "after-paint",
      G_CALLBACK (window_after_after_paint_cb), self);

  /* Ensure the base widget is initialized */
  gtk_gst_base_widget_set_buffer (GTK_GST_BASE_WIDGET (priv->gtk_widget), NULL);

  g_mutex_unlock (&priv->render_lock);
}

static void
window_initial_map_cb (GtkWidget * widget, gpointer user_data)
{
  GstGtkWaylandSink *self = user_data;
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);

  setup_wl_window (self);
  g_signal_handlers_disconnect_by_func (priv->gtk_widget,
      window_initial_map_cb, self);
}

static void
gst_gtk_wayland_sink_navigation_send_event (GstNavigation * navigation,
    GstEvent * event)
{
  GstGtkWaylandSink *self = GST_GTK_WAYLAND_SINK (navigation);
  GstPad *pad;
  gdouble x, y;

  event = gst_event_make_writable (event);

  if (gst_navigation_event_get_coordinates (event, &x, &y)) {
    GtkGstBaseWidget *widget =
        GTK_GST_BASE_WIDGET (gst_gtk_wayland_sink_get_widget (self));
    gdouble stream_x, stream_y;

    if (widget == NULL) {
      GST_ERROR_OBJECT (self, "Could not ensure GTK initialization.");
      return;
    }

    gtk_gst_base_widget_display_size_to_stream_size (widget,
        x, y, &stream_x, &stream_y);
    gst_navigation_event_set_coordinates (event, stream_x, stream_y);
  }

  pad = gst_pad_get_peer (GST_VIDEO_SINK_PAD (self));

  GST_TRACE_OBJECT (self, "navigation event %" GST_PTR_FORMAT,
      gst_event_get_structure (event));

  if (GST_IS_PAD (pad) && GST_IS_EVENT (event)) {
    if (!gst_pad_send_event (pad, gst_event_ref (event))) {
      /* If upstream didn't handle the event we'll post a message with it
       * for the application in case it wants to do something with it */
      gst_element_post_message (GST_ELEMENT_CAST (self),
          gst_navigation_message_new_event (GST_OBJECT_CAST (self), event));
    }
    gst_event_unref (event);
    gst_object_unref (pad);
  }
}

static void
gst_gtk_wayland_sink_navigation_interface_init (GstNavigationInterface * iface)
{
  iface->send_event_simple = gst_gtk_wayland_sink_navigation_send_event;
}


static gboolean
gst_gtk_wayland_sink_start_on_main (GstGtkWaylandSink * self)
{
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);
  GtkWidget *toplevel;
  GdkDisplay *gdk_display;
  struct wl_display *wl_display;

  if ((toplevel = gst_gtk_wayland_sink_get_widget (self)) == NULL) {
    GST_ERROR_OBJECT (self, "Could not ensure GTK initialization.");
    return FALSE;
  }
  g_object_unref (toplevel);

  /* After this point, priv->gtk_widget will always be set */

  gdk_display = gtk_widget_get_display (priv->gtk_widget);
  if (!GDK_IS_WAYLAND_DISPLAY (gdk_display)) {
    GST_ERROR_OBJECT (self, "GDK is not using its wayland backend.");
    return FALSE;
  }
  wl_display = gdk_wayland_display_get_wl_display (gdk_display);
  priv->display = gst_wl_display_new_existing (wl_display, FALSE, NULL);

  toplevel = gtk_widget_get_toplevel (GTK_WIDGET (priv->gtk_widget));
  if (!gtk_widget_is_toplevel (toplevel)) {
    /* User did not add widget its own UI, let's popup a new GtkWindow to
     * make gst-launch-1.0 work. */
    priv->gtk_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size (GTK_WINDOW (priv->gtk_window), 640, 480);
    gtk_window_set_title (GTK_WINDOW (priv->gtk_window),
        "Gst GTK Wayland Sink");
    gtk_container_add (GTK_CONTAINER (priv->gtk_window), toplevel);
    priv->gtk_window_destroy_id = g_signal_connect (priv->gtk_window, "destroy",
        G_CALLBACK (window_destroy_cb), self);

    g_signal_connect (priv->gtk_widget, "map",
        G_CALLBACK (window_initial_map_cb), self);
  } else {
    if (gtk_widget_get_mapped (priv->gtk_widget)) {
      setup_wl_window (self);
    } else {
      g_signal_connect (priv->gtk_widget, "map",
          G_CALLBACK (window_initial_map_cb), self);
    }
  }

  return TRUE;
}

static gboolean
gst_gtk_wayland_sink_stop_on_main (GstGtkWaylandSink * self)
{
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);

  if (priv->gtk_window) {
    if (priv->gtk_window_destroy_id)
      g_signal_handler_disconnect (priv->gtk_window,
          priv->gtk_window_destroy_id);
    priv->gtk_window_destroy_id = 0;
    gst_wl_window_flush (priv->wl_window);
    gst_clear_object (&priv->wl_window);
    gtk_widget_destroy (priv->gtk_window);
    priv->gtk_window = NULL;
  }

  if (priv->gtk_widget) {
    GtkWidget *widget;
    GdkWindow *gdk_window;

    g_signal_handlers_disconnect_by_func (priv->gtk_widget,
        widget_size_allocate_cb, self);

    widget = priv->gtk_widget;
    do {
      if (GTK_IS_SCROLLABLE (widget)) {
        GtkAdjustment *hadjustment;
        GtkAdjustment *vadjustment;

        hadjustment = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (widget));
        vadjustment = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (widget));

        g_signal_handlers_disconnect_by_func (hadjustment,
            scrollable_window_adjustment_changed_cb, self);
        g_signal_handlers_disconnect_by_func (vadjustment,
            scrollable_window_adjustment_changed_cb, self);
      }
    } while ((widget = gtk_widget_get_parent (widget)));

    gdk_window = gtk_widget_get_window (priv->gtk_widget);
    if (gdk_window) {
      GdkFrameClock *gdk_frame_clock;

      gdk_frame_clock = gdk_window_get_frame_clock (gdk_window);
      g_signal_handlers_disconnect_by_func (gdk_frame_clock,
          window_after_after_paint_cb, self);
    }
  }

  return TRUE;
}

static void
gst_gtk_widget_show_all_and_unref (GtkWidget * widget)
{
  gtk_widget_show_all (widget);
  g_object_unref (widget);
}

static GstStateChangeReturn
gst_gtk_wayland_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstGtkWaylandSink *self = GST_GTK_WAYLAND_SINK (element);
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_gtk_invoke_on_main ((GThreadFunc)
              gst_gtk_wayland_sink_start_on_main, element))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      GtkWindow *window = NULL;

      GST_OBJECT_LOCK (self);
      if (priv->gtk_window)
        window = g_object_ref (GTK_WINDOW (priv->gtk_window));
      GST_OBJECT_UNLOCK (self);

      if (window)
        gst_gtk_invoke_on_main ((GThreadFunc) gst_gtk_widget_show_all_and_unref,
            window);

      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* fallthrough */
    case GST_STATE_CHANGE_NULL_TO_NULL:
      gst_gtk_invoke_on_main ((GThreadFunc)
          gst_gtk_wayland_sink_stop_on_main, element);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (priv->wl_window) {
        gst_wl_window_flush (priv->wl_window);
        /* remove buffer from surface, show nothing */
        gst_wl_window_render (priv->wl_window, NULL);
      }
      gst_buffer_replace (&priv->last_buffer, NULL);
      break;
    default:
      break;
  }

  return ret;
}

static GstCaps *
gst_gtk_wayland_sink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstGtkWaylandSink *self = GST_GTK_WAYLAND_SINK (bsink);
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);
  GstCaps *caps;

  caps = gst_pad_get_pad_template_caps (GST_VIDEO_SINK_PAD (self));
  caps = gst_caps_make_writable (caps);

  g_mutex_lock (&priv->display_lock);

  if (priv->display) {
    GValue format_list = G_VALUE_INIT;

    g_value_init (&format_list, GST_TYPE_LIST);

    /* Add corresponding dmabuf formats */
    gst_wl_display_fill_dmabuf_format_list (priv->display, &format_list);
    gst_structure_take_value (gst_caps_get_structure (caps, 0), "drm-format",
        &format_list);

    g_value_init (&format_list, GST_TYPE_LIST);

    /* Add corresponding shm formats */
    gst_wl_display_fill_shm_format_list (priv->display, &format_list);
    gst_structure_take_value (gst_caps_get_structure (caps, 1), "format",
        &format_list);

    GST_DEBUG_OBJECT (self, "display caps: %" GST_PTR_FORMAT, caps);
  }

  g_mutex_unlock (&priv->display_lock);

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
gst_gtk_wayland_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstGtkWaylandSink *self = GST_GTK_WAYLAND_SINK (bsink);
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);
  gboolean use_dmabuf;

  GST_DEBUG_OBJECT (self, "set caps %" GST_PTR_FORMAT, caps);

  if (gst_video_is_dma_drm_caps (caps)) {
    if (!gst_video_info_dma_drm_from_caps (&priv->drm_info, caps))
      goto invalid_format;

    if (!gst_video_info_dma_drm_to_video_info (&priv->drm_info,
            &priv->render_info))
      goto invalid_format;
  } else {
    /* extract info from caps */
    if (!gst_video_info_from_caps (&priv->render_info, caps))
      goto invalid_format;

    if (!gst_video_info_dma_drm_from_video_info (&priv->drm_info,
            &priv->render_info, DRM_FORMAT_MOD_LINEAR))
      gst_video_info_dma_drm_init (&priv->drm_info);
  }

  priv->have_mastering_info =
      gst_video_mastering_display_info_from_caps (&priv->minfo, caps);
  priv->have_light_info =
      gst_video_content_light_level_from_caps (&priv->linfo, caps);

  use_dmabuf = gst_caps_features_contains (gst_caps_get_features (caps, 0),
      GST_CAPS_FEATURE_MEMORY_DMABUF);

  /* validate the format base on the memory type. */
  if (use_dmabuf) {
    if (!gst_wl_display_check_format_for_dmabuf (priv->display,
            &priv->drm_info))
      goto unsupported_drm_format;
  } else if (!gst_wl_display_check_format_for_shm (priv->display,
          &priv->render_info)) {
    /* Note: we still support dmabuf in this case, but formats must also be
     * supported on SHM interface to ensure a fallback is possible as we are
     * not guarantied we'll get dmabuf in the buffers. */
    goto unsupported_format;
  }

  GST_OBJECT_LOCK (self);

  if (priv->gtk_widget == NULL) {
    GST_OBJECT_UNLOCK (self);
    GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
        ("Output widget was destroyed"), (NULL));
    return FALSE;
  }

  if (!gtk_gst_base_widget_set_format (GTK_GST_BASE_WIDGET (priv->gtk_widget),
          &priv->render_info)) {
    GST_OBJECT_UNLOCK (self);
    return FALSE;
  }

  /* Ensure queue_draw get executed and internal display size get initialized.
   * This does not happen otherwise as we don't draw in the widget
   */
  gtk_gst_base_widget_queue_draw (GTK_GST_BASE_WIDGET (priv->gtk_widget));

  GST_OBJECT_UNLOCK (self);

  /* Will be used when the window creation is delayed */
  gst_caps_replace (&priv->caps, caps);
  priv->video_info = priv->render_info;

  if (priv->wl_window)
    gst_wl_window_set_source_info (priv->wl_window, &priv->drm_info,
        &priv->video_info, priv->caps, priv->drm_device);

  return TRUE;

invalid_format:
  {
    GST_ERROR_OBJECT (self,
        "Could not locate image format from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
unsupported_drm_format:
  {
    GST_ERROR_OBJECT (self, "DRM format %" GST_FOURCC_FORMAT
        " is not available on the display",
        GST_FOURCC_ARGS (priv->drm_info.drm_fourcc));
    return FALSE;
  }
unsupported_format:
  {
    GST_ERROR_OBJECT (self, "Format %s is not available on the display",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT
            (&priv->render_info)));
    return FALSE;
  }
}

static gboolean
gst_gtk_wayland_sink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstGtkWaylandSink *self = GST_GTK_WAYLAND_SINK (bsink);
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);
  GstAllocator *allocator = NULL;
  GstCaps *caps;
  GstBufferPool *pool = NULL;
  gboolean need_pool;
  guint size;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    return FALSE;

  if (gst_video_is_dma_drm_caps (caps)) {
    GstVideoInfoDmaDrm drm_info;

    if (!gst_video_info_dma_drm_from_caps (&drm_info, caps))
      return FALSE;

    size = drm_info.vinfo.size;
  } else {
    GstVideoInfo vinfo;

    /* extract info from caps */
    if (!gst_video_info_from_caps (&vinfo, caps))
      return FALSE;

    size = vinfo.size;

    allocator = gst_udmabuf_allocator_get ();
    if (!allocator)
      allocator = gst_shm_allocator_get ();
  }

  if (need_pool && !gst_video_is_dma_drm_caps (caps)) {
    GstStructure *config;

    if (GST_IS_UDMABUF_ALLOCATOR (allocator)) {
      pool = gst_video_dmabuf_pool_new ();
    } else {
      pool = gst_wl_video_buffer_pool_new ();
      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_set_params (config, caps, size, 2, 0);
      gst_buffer_pool_config_set_allocator (config,
          gst_object_ref (allocator), NULL);
      gst_buffer_pool_set_config (pool, config);
    }
  }

  gst_query_add_allocation_pool (query, pool, size, 2, 0);
  if (pool)
    gst_object_unref (pool);

  if (!gst_video_is_dma_drm_caps (caps))
    gst_query_add_allocation_param (query, allocator, NULL);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  if (gst_wl_display_get_viewporter (priv->display))
    gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);

  gst_clear_object (&allocator);
  return TRUE;
}

static GstFlowReturn
gst_gtk_wayland_sink_show_frame (GstVideoSink * vsink, GstBuffer * buffer)
{
  GstGtkWaylandSink *self = GST_GTK_WAYLAND_SINK (vsink);
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);
  GstFlowReturn ret = GST_FLOW_OK;

  g_mutex_lock (&priv->render_lock);

  GST_LOG_OBJECT (self, "render buffer %" GST_PTR_FORMAT "", buffer);

  if (!priv->wl_window) {
    GST_LOG_OBJECT (self,
        "buffer %" GST_PTR_FORMAT " dropped (waiting for window)", buffer);
    ret = GST_BASE_SINK_FLOW_DROPPED;
    goto done;
  }

  /* make sure that the application has called set_render_rectangle() */
  if (G_UNLIKELY (gst_wl_window_get_render_rectangle (priv->wl_window)->w == 0))
    goto no_window_size;

  /* drop double rendering */
  if (priv->last_buffer) {
    const GstMemory *last_mem = gst_buffer_peek_memory (priv->last_buffer, 0);
    const GstMemory *next_mem = gst_buffer_peek_memory (buffer, 0);

    if (G_UNLIKELY (last_mem == next_mem)) {
      GST_LOG_OBJECT (self, "Buffer already being rendered");
      goto done;
    }
  }

  ret = gst_wl_window_render (priv->wl_window, buffer);

  if (ret >= GST_FLOW_OK)
    gst_buffer_replace (&priv->last_buffer, buffer);

done:
  g_mutex_unlock (&priv->render_lock);
  return ret;

no_window_size:
  GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
      ("Window has no size set"),
      ("Make sure you set the size after calling set_window_handle"));
  ret = GST_FLOW_ERROR;
  goto done;
}

static void
gst_gtk_wayland_sink_set_rotate_method (GstGtkWaylandSink * self,
    GstVideoOrientationMethod method, gboolean from_tag)
{
  GstGtkWaylandSinkPrivate *priv =
      gst_gtk_wayland_sink_get_instance_private (self);
  GstVideoOrientationMethod new_method;

  if (method == GST_VIDEO_ORIENTATION_CUSTOM) {
    GST_WARNING_OBJECT (self, "unsupported custom orientation");
    return;
  }

  GST_OBJECT_LOCK (self);
  if (from_tag)
    priv->tag_rotate_method = method;
  else
    priv->sink_rotate_method = method;

  if (priv->sink_rotate_method == GST_VIDEO_ORIENTATION_AUTO)
    new_method = priv->tag_rotate_method;
  else
    new_method = priv->sink_rotate_method;

  if (new_method != priv->current_rotate_method) {
    GST_DEBUG_OBJECT (priv, "Changing method from %d to %d",
        priv->current_rotate_method, new_method);

    if (priv->wl_window) {
      g_mutex_lock (&priv->render_lock);
      gst_wl_window_set_rotate_method (priv->wl_window, new_method);
      g_mutex_unlock (&priv->render_lock);
    }

    priv->current_rotate_method = new_method;
  }
  GST_OBJECT_UNLOCK (self);
}
