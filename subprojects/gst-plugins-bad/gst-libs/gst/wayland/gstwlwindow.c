/* GStreamer Wayland Library
 *
 * Copyright (C) 2011 Intel Corporation
 * Copyright (C) 2011 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwlwindow.h"

#include "color-management-v1-client-protocol.h"
#include "color-representation-v1-client-protocol.h"
#include "fullscreen-shell-unstable-v1-client-protocol.h"
#include "single-pixel-buffer-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define GST_CAT_DEFAULT gst_wl_window_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

typedef struct _GstWlWindowPrivate
{
  GObject parent_instance;

  GMutex *render_lock;

  GstWlDisplay *display;
  struct wl_surface *area_surface;
  struct wl_surface *area_surface_wrapper;
  struct wl_subsurface *area_subsurface;
  struct wp_viewport *area_viewport;
  struct wl_surface *video_surface;
  struct wl_surface *video_surface_wrapper;
  struct wl_subsurface *video_subsurface;
  struct wp_viewport *video_viewport;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  struct wp_color_management_surface_v1 *color_management_surface;
  struct wp_color_representation_surface_v1 *color_representation_surface;
  gboolean configured;
  GCond configure_cond;
  GMutex configure_mutex;

  /* the size and position of the area_(sub)surface */
  GstVideoRectangle render_rectangle;

  /* the size and position of the video_subsurface */
  GstVideoRectangle video_rectangle;

  /* the size of the video in the buffers */
  gint video_width, video_height;

  /* video width scaled according to par */
  gint scaled_width;

  enum wl_output_transform buffer_transform;

  gboolean force_aspect_ratio;

  /* when this is not set both the area_surface and the video_surface are not
   * visible and certain steps should be skipped */
  gboolean is_area_surface_mapped;

  GMutex window_lock;
  GstWlBuffer *next_buffer;
  GstVideoInfo *next_video_info;
  GstVideoMasteringDisplayInfo *next_minfo;
  GstVideoContentLightLevel *next_linfo;
  GstWlBuffer *staged_buffer;
  gboolean clear_window;
  struct wl_callback *frame_callback;
  struct wl_callback *commit_callback;
} GstWlWindowPrivate;

G_DEFINE_TYPE_WITH_CODE (GstWlWindow, gst_wl_window, G_TYPE_OBJECT,
    G_ADD_PRIVATE (GstWlWindow)
    GST_DEBUG_CATEGORY_INIT (gst_wl_window_debug,
        "wlwindow", 0, "wlwindow library");
    );

enum
{
  CLOSED,
  MAP,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void gst_wl_window_finalize (GObject * gobject);

static void gst_wl_window_update_borders (GstWlWindow * self);

static void gst_wl_window_commit_buffer (GstWlWindow * self,
    GstWlBuffer * buffer);

static void gst_wl_window_set_colorimetry (GstWlWindow * self,
    const GstVideoColorimetry * colorimetry,
    const GstVideoMasteringDisplayInfo * minfo,
    const GstVideoContentLightLevel * linfo);

static void
handle_xdg_toplevel_close (void *data, struct xdg_toplevel *xdg_toplevel)
{
  GstWlWindow *self = data;

  GST_DEBUG_OBJECT (self, "XDG toplevel got a \"close\" event.");
  g_signal_emit (self, signals[CLOSED], 0);
}

static void
handle_xdg_toplevel_configure (void *data, struct xdg_toplevel *xdg_toplevel,
    int32_t width, int32_t height, struct wl_array *states)
{
  GstWlWindow *self = data;
  const uint32_t *state;

  GST_DEBUG_OBJECT (self, "XDG toplevel got a \"configure\" event, [ %d, %d ].",
      width, height);

  wl_array_for_each (state, states) {
    switch (*state) {
      case XDG_TOPLEVEL_STATE_FULLSCREEN:
        GST_DEBUG_OBJECT (self, "XDG top-level now FULLSCREEN");
        break;
      case XDG_TOPLEVEL_STATE_MAXIMIZED:
        GST_DEBUG_OBJECT (self, "XDG top-level now MAXIMIXED");
        break;
      case XDG_TOPLEVEL_STATE_RESIZING:
        GST_DEBUG_OBJECT (self, "XDG top-level being RESIZED");
        break;
      case XDG_TOPLEVEL_STATE_ACTIVATED:
        GST_DEBUG_OBJECT (self, "XDG top-level being ACTIVATED");
        break;
    }
  }

  if (width <= 0 || height <= 0)
    return;

  gst_wl_window_set_render_rectangle (self, 0, 0, width, height);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
  handle_xdg_toplevel_configure,
  handle_xdg_toplevel_close,
};

static void
handle_xdg_surface_configure (void *data, struct xdg_surface *xdg_surface,
    uint32_t serial)
{
  GstWlWindow *self = data;
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  xdg_surface_ack_configure (xdg_surface, serial);

  g_mutex_lock (&priv->configure_mutex);
  priv->configured = TRUE;
  g_cond_signal (&priv->configure_cond);
  g_mutex_unlock (&priv->configure_mutex);
}

static const struct xdg_surface_listener xdg_surface_listener = {
  handle_xdg_surface_configure,
};

static void
gst_wl_window_class_init (GstWlWindowClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = gst_wl_window_finalize;

  signals[CLOSED] = g_signal_new ("closed", G_TYPE_FROM_CLASS (gobject_class),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[MAP] = g_signal_new ("map", G_TYPE_FROM_CLASS (gobject_class),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

}

static void
gst_wl_window_init (GstWlWindow * self)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  priv->configured = TRUE;
  g_cond_init (&priv->configure_cond);
  g_mutex_init (&priv->configure_mutex);
  g_mutex_init (&priv->window_lock);
}

static void
gst_wl_window_finalize (GObject * gobject)
{
  GstWlWindow *self = GST_WL_WINDOW (gobject);
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  gst_wl_display_callback_destroy (priv->display, &priv->frame_callback);
  gst_wl_display_callback_destroy (priv->display, &priv->commit_callback);
  gst_wl_display_object_destroy (priv->display,
      (gpointer *) & priv->xdg_toplevel, (GDestroyNotify) xdg_toplevel_destroy);
  gst_wl_display_object_destroy (priv->display,
      (gpointer *) & priv->xdg_surface, (GDestroyNotify) xdg_surface_destroy);

  if (priv->staged_buffer)
    gst_wl_buffer_unref_buffer (priv->staged_buffer);

  g_cond_clear (&priv->configure_cond);
  g_mutex_clear (&priv->configure_mutex);
  g_mutex_clear (&priv->window_lock);

  if (priv->video_viewport)
    wp_viewport_destroy (priv->video_viewport);

  if (priv->color_management_surface)
    wp_color_management_surface_v1_destroy (priv->color_management_surface);

  if (priv->color_representation_surface)
    wp_color_representation_surface_v1_destroy
        (priv->color_representation_surface);

  wl_proxy_wrapper_destroy (priv->video_surface_wrapper);
  wl_subsurface_destroy (priv->video_subsurface);
  wl_surface_destroy (priv->video_surface);

  if (priv->area_subsurface)
    wl_subsurface_destroy (priv->area_subsurface);

  if (priv->area_viewport)
    wp_viewport_destroy (priv->area_viewport);

  wl_proxy_wrapper_destroy (priv->area_surface_wrapper);
  wl_surface_destroy (priv->area_surface);

  g_clear_object (&priv->display);

  G_OBJECT_CLASS (gst_wl_window_parent_class)->finalize (gobject);
}

static GstWlWindow *
gst_wl_window_new_internal (GstWlDisplay * display, GMutex * render_lock)
{
  GstWlWindow *self;
  GstWlWindowPrivate *priv;
  struct wl_compositor *compositor;
  struct wl_event_queue *event_queue;
  struct wl_region *region;
  struct wp_viewporter *viewporter;

  self = g_object_new (GST_TYPE_WL_WINDOW, NULL);
  priv = gst_wl_window_get_instance_private (self);
  priv->display = g_object_ref (display);
  priv->render_lock = render_lock;
  g_cond_init (&priv->configure_cond);
  priv->force_aspect_ratio = TRUE;

  compositor = gst_wl_display_get_compositor (display);
  priv->area_surface = wl_compositor_create_surface (compositor);
  priv->video_surface = wl_compositor_create_surface (compositor);

  priv->area_surface_wrapper = wl_proxy_create_wrapper (priv->area_surface);
  priv->video_surface_wrapper = wl_proxy_create_wrapper (priv->video_surface);

  event_queue = gst_wl_display_get_event_queue (display);
  wl_proxy_set_queue ((struct wl_proxy *) priv->area_surface_wrapper,
      event_queue);
  wl_proxy_set_queue ((struct wl_proxy *) priv->video_surface_wrapper,
      event_queue);

  /* embed video_surface in area_surface */
  priv->video_subsurface =
      wl_subcompositor_get_subsurface (gst_wl_display_get_subcompositor
      (display), priv->video_surface, priv->area_surface);
  wl_subsurface_set_desync (priv->video_subsurface);

  viewporter = gst_wl_display_get_viewporter (display);
  if (viewporter) {
    priv->area_viewport = wp_viewporter_get_viewport (viewporter,
        priv->area_surface);
    priv->video_viewport = wp_viewporter_get_viewport (viewporter,
        priv->video_surface);
  }

  /* never accept input events on the video surface */
  region = wl_compositor_create_region (compositor);
  wl_surface_set_input_region (priv->video_surface, region);
  wl_region_destroy (region);

  return self;
}

/**
 * gst_wl_window_ensure_fullscreen_for_output:
 * @self: A #GstWlWindow
 * @fullscreen: %TRUE to set fullscreen, %FALSE to unset it
 * @output_nane: (nullable): The name of the wl_output to fullscreen to
 *
 * Ensure the window fullscreen state matches the desired state. If a
 * output_name is provided, and this output exists, the window will be set to
 * fullscreen on that screen. Otherwise the compisitor will decide.
 *
 * Since: 1.28
 */
void
gst_wl_window_ensure_fullscreen_for_output (GstWlWindow * self,
    gboolean fullscreen, const gchar * output_name)
{
  GstWlWindowPrivate *priv;
  GstWlOutput *output = NULL;
  struct wl_output *wl_output = NULL;

  g_return_if_fail (self);
  priv = gst_wl_window_get_instance_private (self);

  if (!fullscreen) {
    xdg_toplevel_unset_fullscreen (priv->xdg_toplevel);
    return;
  }

  if (output_name) {
    output = gst_wl_display_get_output_by_name (priv->display, output_name);
    if (output)
      wl_output = gst_wl_output_get_wl_output (output);
    else
      GST_WARNING ("Could not find any output named '%s'", output_name);
  }

  xdg_toplevel_set_fullscreen (priv->xdg_toplevel, wl_output);

  // Unref last for thread safety
  if (output)
    g_object_unref (output);
}

/**
 * gst_wl_window_ensure_fullscreen:
 * @self: A #GstWlWindow
 * @fullscreen: %TRUE to set fullscreen, %FALSE to unset it
 *
 * Same as gst_wl_window_ensure_fullscreen_for_output() without specifying an
 * output.
 */
void
gst_wl_window_ensure_fullscreen (GstWlWindow * self, gboolean fullscreen)
{
  gst_wl_window_ensure_fullscreen_for_output (self, fullscreen, NULL);
}

GstWlWindow *
gst_wl_window_new_toplevel (GstWlDisplay * display, const GstVideoInfo * info,
    gboolean fullscreen, GMutex * render_lock)
{
  GstWlWindow *self;
  GstWlWindowPrivate *priv;
  struct xdg_wm_base *xdg_wm_base;
  struct zwp_fullscreen_shell_v1 *fullscreen_shell;

  self = gst_wl_window_new_internal (display, render_lock);
  priv = gst_wl_window_get_instance_private (self);

  xdg_wm_base = gst_wl_display_get_xdg_wm_base (display);
  fullscreen_shell = gst_wl_display_get_fullscreen_shell_v1 (display);

  /* Check which protocol we will use (in order of preference) */
  if (xdg_wm_base) {
    gint64 timeout;

    /* First create the XDG surface */
    priv->xdg_surface = xdg_wm_base_get_xdg_surface (xdg_wm_base,
        priv->area_surface);
    if (!priv->xdg_surface) {
      GST_ERROR_OBJECT (self, "Unable to get xdg_surface");
      goto error;
    }
    xdg_surface_add_listener (priv->xdg_surface, &xdg_surface_listener, self);

    /* Then the toplevel */
    priv->xdg_toplevel = xdg_surface_get_toplevel (priv->xdg_surface);
    if (!priv->xdg_toplevel) {
      GST_ERROR_OBJECT (self, "Unable to get xdg_toplevel");
      goto error;
    }
    xdg_toplevel_add_listener (priv->xdg_toplevel,
        &xdg_toplevel_listener, self);
    if (g_get_prgname ()) {
      xdg_toplevel_set_app_id (priv->xdg_toplevel, g_get_prgname ());
    } else {
      xdg_toplevel_set_app_id (priv->xdg_toplevel, "org.gstreamer.wayland");
    }

    gst_wl_window_ensure_fullscreen (self, fullscreen);

    /* Finally, commit the xdg_surface state as toplevel */
    priv->configured = FALSE;
    wl_surface_commit (priv->area_surface);
    wl_display_flush (gst_wl_display_get_display (display));

    g_mutex_lock (&priv->configure_mutex);
    timeout = g_get_monotonic_time () + 100 * G_TIME_SPAN_MILLISECOND;
    while (!priv->configured) {
      if (!g_cond_wait_until (&priv->configure_cond, &priv->configure_mutex,
              timeout)) {
        GST_WARNING_OBJECT (self,
            "The compositor did not send configure event.");
        break;
      }
    }
    g_mutex_unlock (&priv->configure_mutex);
  } else if (fullscreen_shell) {
    zwp_fullscreen_shell_v1_present_surface (fullscreen_shell,
        priv->area_surface, ZWP_FULLSCREEN_SHELL_V1_PRESENT_METHOD_ZOOM, NULL);
  } else {
    GST_ERROR_OBJECT (self,
        "Unable to use either xdg_wm_base or zwp_fullscreen_shell.");
    goto error;
  }

  /* render_rectangle is already set via toplevel_configure in
   * xdg_shell fullscreen mode */
  if (!(xdg_wm_base && fullscreen)) {
    /* set the initial size to be the same as the reported video size */
    gint width =
        gst_util_uint64_scale_int_round (info->width, info->par_n, info->par_d);
    gst_wl_window_set_render_rectangle (self, 0, 0, width, info->height);
  }

  return self;

error:
  g_object_unref (self);
  return NULL;
}

GstWlWindow *
gst_wl_window_new_in_surface (GstWlDisplay * display,
    struct wl_surface *parent, GMutex * render_lock)
{
  GstWlWindow *self;
  GstWlWindowPrivate *priv;
  struct wl_region *region;

  self = gst_wl_window_new_internal (display, render_lock);
  priv = gst_wl_window_get_instance_private (self);

  /* do not accept input events on the area surface when embedded */
  region =
      wl_compositor_create_region (gst_wl_display_get_compositor (display));
  wl_surface_set_input_region (priv->area_surface, region);
  wl_region_destroy (region);

  /* embed in parent */
  priv->area_subsurface =
      wl_subcompositor_get_subsurface (gst_wl_display_get_subcompositor
      (display), priv->area_surface, parent);
  wl_subsurface_set_desync (priv->area_subsurface);

  wl_surface_commit (parent);

  return self;
}

GstWlDisplay *
gst_wl_window_get_display (GstWlWindow * self)
{
  GstWlWindowPrivate *priv;

  g_return_val_if_fail (self != NULL, NULL);

  priv = gst_wl_window_get_instance_private (self);
  return g_object_ref (priv->display);
}

struct wl_surface *
gst_wl_window_get_wl_surface (GstWlWindow * self)
{
  GstWlWindowPrivate *priv;

  g_return_val_if_fail (self != NULL, NULL);

  priv = gst_wl_window_get_instance_private (self);
  return priv->video_surface_wrapper;
}

struct wl_subsurface *
gst_wl_window_get_subsurface (GstWlWindow * self)
{
  GstWlWindowPrivate *priv;

  g_return_val_if_fail (self != NULL, NULL);

  priv = gst_wl_window_get_instance_private (self);
  return priv->area_subsurface;
}

gboolean
gst_wl_window_is_toplevel (GstWlWindow * self)
{
  GstWlWindowPrivate *priv;

  g_return_val_if_fail (self != NULL, FALSE);

  priv = gst_wl_window_get_instance_private (self);
  return (priv->xdg_toplevel != NULL);
}

static void
gst_wl_window_resize_video_surface (GstWlWindow * self, gboolean commit)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  GstVideoRectangle src = { 0, };
  GstVideoRectangle dst = { 0, };
  GstVideoRectangle res;
  int wp_src_width;
  int wp_src_height;

  switch (priv->buffer_transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL:
    case WL_OUTPUT_TRANSFORM_180:
    case WL_OUTPUT_TRANSFORM_FLIPPED:
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
      src.w = priv->scaled_width;
      src.h = priv->video_height;
      wp_src_width = priv->video_width;
      wp_src_height = priv->video_height;
      break;
    case WL_OUTPUT_TRANSFORM_90:
    case WL_OUTPUT_TRANSFORM_270:
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
      src.w = priv->video_height;
      src.h = priv->scaled_width;
      wp_src_width = priv->video_height;
      wp_src_height = priv->video_width;
      break;
    default:
      g_assert_not_reached ();
  }

  dst.w = priv->render_rectangle.w;
  dst.h = priv->render_rectangle.h;

  /* center the video_subsurface inside area_subsurface */
  if (priv->video_viewport) {
    if (!priv->force_aspect_ratio)
      res = dst;
    else
      gst_video_center_rect (&src, &dst, &res, TRUE);
    wp_viewport_set_source (priv->video_viewport, wl_fixed_from_int (0),
        wl_fixed_from_int (0), wl_fixed_from_int (wp_src_width),
        wl_fixed_from_int (wp_src_height));
    wp_viewport_set_destination (priv->video_viewport, res.w, res.h);
  } else {
    gst_video_center_rect (&src, &dst, &res, FALSE);
  }

  wl_subsurface_set_position (priv->video_subsurface, res.x, res.y);
  wl_surface_set_buffer_transform (priv->video_surface_wrapper,
      priv->buffer_transform);

  if (commit)
    wl_surface_commit (priv->video_surface_wrapper);

  priv->video_rectangle = res;
}

static void
gst_wl_window_set_opaque (GstWlWindow * self, const GstVideoInfo * info)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  struct wl_compositor *compositor;
  struct wl_region *region;

  /* Set area opaque */
  compositor = gst_wl_display_get_compositor (priv->display);
  region = wl_compositor_create_region (compositor);
  wl_region_add (region, 0, 0, G_MAXINT32, G_MAXINT32);
  wl_surface_set_opaque_region (priv->area_surface, region);
  wl_region_destroy (region);

  if (!GST_VIDEO_INFO_HAS_ALPHA (info)) {
    /* Set video opaque */
    region = wl_compositor_create_region (compositor);
    wl_region_add (region, 0, 0, G_MAXINT32, G_MAXINT32);
    wl_surface_set_opaque_region (priv->video_surface, region);
    wl_region_destroy (region);
  }
}

static void
frame_redraw_callback (void *data, struct wl_callback *callback, uint32_t time)
{
  GstWlWindow *self = data;
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  GstWlBuffer *next_buffer;

  GST_DEBUG_OBJECT (self, "frame_redraw_cb");

  wl_callback_destroy (callback);
  priv->frame_callback = NULL;

  g_mutex_lock (&priv->window_lock);
  next_buffer = priv->next_buffer = priv->staged_buffer;
  priv->staged_buffer = NULL;
  g_mutex_unlock (&priv->window_lock);

  if (next_buffer || priv->clear_window)
    gst_wl_window_commit_buffer (self, next_buffer);

  if (next_buffer)
    gst_wl_buffer_unref_buffer (next_buffer);
}

static const struct wl_callback_listener frame_callback_listener = {
  frame_redraw_callback
};

static void
gst_wl_window_commit_buffer (GstWlWindow * self, GstWlBuffer * buffer)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  GstVideoInfo *info = priv->next_video_info;
  GstVideoMasteringDisplayInfo *minfo = priv->next_minfo;
  GstVideoContentLightLevel *linfo = priv->next_linfo;
  struct wl_callback *callback;

  if (G_UNLIKELY (info)) {
    priv->scaled_width =
        gst_util_uint64_scale_int_round (info->width, info->par_n, info->par_d);
    priv->video_width = info->width;
    priv->video_height = info->height;

    wl_subsurface_set_sync (priv->video_subsurface);
    gst_wl_window_resize_video_surface (self, FALSE);
    gst_wl_window_set_opaque (self, info);

    gst_wl_window_set_colorimetry (self, &info->colorimetry, minfo, linfo);
  }

  if (G_LIKELY (buffer)) {
    callback = wl_surface_frame (priv->video_surface_wrapper);
    priv->frame_callback = callback;
    wl_callback_add_listener (callback, &frame_callback_listener, self);
    gst_wl_buffer_attach (buffer, priv->video_surface_wrapper);
    wl_surface_damage_buffer (priv->video_surface_wrapper, 0, 0, G_MAXINT32,
        G_MAXINT32);
    wl_surface_commit (priv->video_surface_wrapper);

    if (!priv->is_area_surface_mapped) {
      gst_wl_window_update_borders (self);
      wl_surface_commit (priv->area_surface_wrapper);
      priv->is_area_surface_mapped = TRUE;
      g_signal_emit (self, signals[MAP], 0);
    }
  } else {
    /* clear both video and parent surfaces */
    wl_surface_attach (priv->video_surface_wrapper, NULL, 0, 0);
    wl_surface_commit (priv->video_surface_wrapper);
    wl_surface_attach (priv->area_surface_wrapper, NULL, 0, 0);
    wl_surface_commit (priv->area_surface_wrapper);
    priv->is_area_surface_mapped = FALSE;
    priv->clear_window = FALSE;
  }

  if (G_UNLIKELY (info)) {
    /* commit also the parent (area_surface) in order to change
     * the position of the video_subsurface */
    wl_surface_commit (priv->area_surface_wrapper);
    wl_subsurface_set_desync (priv->video_subsurface);
    gst_video_info_free (priv->next_video_info);
    priv->next_video_info = NULL;
    g_clear_pointer (&priv->next_minfo, g_free);
    g_clear_pointer (&priv->next_linfo, g_free);
  }

}

static void
commit_callback (void *data, struct wl_callback *callback, uint32_t serial)
{
  GstWlWindow *self = data;
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  GstWlBuffer *next_buffer;

  wl_callback_destroy (callback);
  priv->commit_callback = NULL;

  g_mutex_lock (&priv->window_lock);
  next_buffer = priv->next_buffer;
  g_mutex_unlock (&priv->window_lock);

  gst_wl_window_commit_buffer (self, next_buffer);

  if (next_buffer)
    gst_wl_buffer_unref_buffer (next_buffer);
}

static const struct wl_callback_listener commit_listener = {
  commit_callback
};

gboolean
gst_wl_window_render (GstWlWindow * self, GstWlBuffer * buffer,
    const GstVideoInfo * info)
{
  return gst_wl_window_render_hdr (self, buffer, info, NULL, NULL);
}

gboolean
gst_wl_window_render_hdr (GstWlWindow * self, GstWlBuffer * buffer,
    const GstVideoInfo * info, const GstVideoMasteringDisplayInfo * minfo,
    const GstVideoContentLightLevel * linfo)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  gboolean ret = TRUE;

  if (G_LIKELY (buffer))
    gst_wl_buffer_ref_gst_buffer (buffer);

  g_mutex_lock (&priv->window_lock);
  if (G_UNLIKELY (info)) {
    gst_video_info_free (priv->next_video_info);
    priv->next_video_info = gst_video_info_copy (info);
  }

  if (G_UNLIKELY (minfo)) {
    g_clear_pointer (&priv->next_minfo, g_free);
    priv->next_minfo = g_memdup2 (minfo, sizeof (*minfo));
  }

  if (G_UNLIKELY (linfo)) {
    g_clear_pointer (&priv->next_linfo, g_free);
    priv->next_linfo = g_memdup2 (linfo, sizeof (*linfo));
  }

  if (priv->next_buffer && priv->staged_buffer) {
    GST_LOG_OBJECT (self, "buffer %p dropped (replaced)", priv->staged_buffer);
    gst_wl_buffer_unref_buffer (priv->staged_buffer);
    ret = FALSE;
  }

  if (!priv->next_buffer) {
    priv->next_buffer = buffer;
    priv->commit_callback =
        gst_wl_display_sync (priv->display, &commit_listener, self);
    wl_display_flush (gst_wl_display_get_display (priv->display));
  } else {
    priv->staged_buffer = buffer;
  }
  if (!buffer)
    priv->clear_window = TRUE;

  g_mutex_unlock (&priv->window_lock);

  return ret;
}

/* Update the buffer used to draw black borders. When we have viewporter
 * support, this is a scaled up 1x1 image, and without we need an black image
 * the size of the rendering areay. */
static void
gst_wl_window_update_borders (GstWlWindow * self)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  gint width, height;
  GstBuffer *buf;
  struct wl_buffer *wlbuf;
  struct wp_single_pixel_buffer_manager_v1 *single_pixel;
  GstWlBuffer *gwlbuf;

  if (gst_wl_display_get_viewporter (priv->display)) {
    wp_viewport_set_destination (priv->area_viewport,
        priv->render_rectangle.w, priv->render_rectangle.h);

    if (priv->is_area_surface_mapped) {
      /* The area_surface is already visible and only needed to get resized.
       * We don't need to attach a new buffer and are done here. */
      return;
    }
  }

  if (gst_wl_display_get_viewporter (priv->display)) {
    width = height = 1;
  } else {
    width = priv->render_rectangle.w;
    height = priv->render_rectangle.h;
  }

  /* draw the area_subsurface */
  single_pixel =
      gst_wl_display_get_single_pixel_buffer_manager_v1 (priv->display);
  if (width == 1 && height == 1 && single_pixel) {
    buf = gst_buffer_new_allocate (NULL, 1, NULL);
    wlbuf =
        wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer (single_pixel,
        0, 0, 0, 0xffffffffU);
  } else {
    GstVideoFormat format;
    GstVideoInfo info;
    GstAllocator *alloc;

    /* we want WL_SHM_FORMAT_XRGB8888 */
    format = GST_VIDEO_FORMAT_BGRx;
    gst_video_info_set_format (&info, format, width, height);
    alloc = gst_shm_allocator_get ();

    buf = gst_buffer_new_allocate (alloc, info.size, NULL);
    gst_buffer_memset (buf, 0, 0, info.size);

    wlbuf =
        gst_wl_shm_memory_construct_wl_buffer (gst_buffer_peek_memory (buf, 0),
        priv->display, &info);

    g_object_unref (alloc);
  }

  gwlbuf = gst_buffer_add_wl_buffer (buf, wlbuf, priv->display);
  gst_wl_buffer_attach (gwlbuf, priv->area_surface_wrapper);
  wl_surface_damage_buffer (priv->area_surface_wrapper, 0, 0, G_MAXINT32,
      G_MAXINT32);

  /* at this point, the GstWlBuffer keeps the buffer
   * alive and will free it on wl_buffer::release */
  gst_buffer_unref (buf);
}

static void
gst_wl_window_update_geometry (GstWlWindow * self)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  /* position the area inside the parent - needs a parent commit to apply */
  if (priv->area_subsurface) {
    wl_subsurface_set_position (priv->area_subsurface, priv->render_rectangle.x,
        priv->render_rectangle.y);
  }

  if (priv->is_area_surface_mapped)
    gst_wl_window_update_borders (self);

  if (!priv->configured)
    return;

  if (priv->scaled_width != 0) {
    wl_subsurface_set_sync (priv->video_subsurface);
    gst_wl_window_resize_video_surface (self, TRUE);
  }

  wl_surface_commit (priv->area_surface_wrapper);

  if (priv->scaled_width != 0)
    wl_subsurface_set_desync (priv->video_subsurface);
}

void
gst_wl_window_set_render_rectangle (GstWlWindow * self, gint x, gint y,
    gint w, gint h)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  if (priv->render_rectangle.x == x && priv->render_rectangle.y == y &&
      priv->render_rectangle.w == w && priv->render_rectangle.h == h)
    return;

  priv->render_rectangle.x = x;
  priv->render_rectangle.y = y;
  priv->render_rectangle.w = w;
  priv->render_rectangle.h = h;

  gst_wl_window_update_geometry (self);
}

const GstVideoRectangle *
gst_wl_window_get_render_rectangle (GstWlWindow * self)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  return &priv->render_rectangle;
}

static enum wl_output_transform
output_transform_from_orientation_method (GstVideoOrientationMethod method)
{
  switch (method) {
    case GST_VIDEO_ORIENTATION_IDENTITY:
      return WL_OUTPUT_TRANSFORM_NORMAL;
    case GST_VIDEO_ORIENTATION_90R:
      return WL_OUTPUT_TRANSFORM_90;
    case GST_VIDEO_ORIENTATION_180:
      return WL_OUTPUT_TRANSFORM_180;
    case GST_VIDEO_ORIENTATION_90L:
      return WL_OUTPUT_TRANSFORM_270;
    case GST_VIDEO_ORIENTATION_HORIZ:
      return WL_OUTPUT_TRANSFORM_FLIPPED;
    case GST_VIDEO_ORIENTATION_VERT:
      return WL_OUTPUT_TRANSFORM_FLIPPED_180;
    case GST_VIDEO_ORIENTATION_UL_LR:
      return WL_OUTPUT_TRANSFORM_FLIPPED_90;
    case GST_VIDEO_ORIENTATION_UR_LL:
      return WL_OUTPUT_TRANSFORM_FLIPPED_270;
    default:
      g_assert_not_reached ();
  }
}

void
gst_wl_window_set_rotate_method (GstWlWindow * self,
    GstVideoOrientationMethod method)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  priv->buffer_transform = output_transform_from_orientation_method (method);

  gst_wl_window_update_geometry (self);
}

enum ImageDescriptionFeedback
{
  IMAGE_DESCRIPTION_FEEDBACK_UNKNOWN = 0,
  IMAGE_DESCRIPTION_FEEDBACK_READY,
  IMAGE_DESCRIPTION_FEEDBACK_FAILED,
};

static void
image_description_failed (void *data,
    struct wp_image_description_v1 *wp_image_description_v1, uint32_t cause,
    const char *msg)
{
  enum ImageDescriptionFeedback *image_description_feedback = data;

  *image_description_feedback = IMAGE_DESCRIPTION_FEEDBACK_FAILED;
}

static void
image_description_ready (void *data,
    struct wp_image_description_v1 *wp_image_description_v1, uint32_t identity)
{
  enum ImageDescriptionFeedback *image_description_feedback = data;

  *image_description_feedback = IMAGE_DESCRIPTION_FEEDBACK_READY;
}

static const struct wp_image_description_v1_listener description_listerer = {
  .failed = image_description_failed,
  .ready = image_description_ready,
};

static enum wp_color_manager_v1_transfer_function
gst_colorimetry_tf_to_wl (GstVideoTransferFunction tf)
{
  switch (tf) {
    case GST_VIDEO_TRANSFER_SRGB:
      return WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_SRGB;
    case GST_VIDEO_TRANSFER_BT601:
    case GST_VIDEO_TRANSFER_BT709:
    case GST_VIDEO_TRANSFER_BT2020_10:
      return WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_BT1886;
    case GST_VIDEO_TRANSFER_SMPTE2084:
      return WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_ST2084_PQ;
    case GST_VIDEO_TRANSFER_ARIB_STD_B67:
      return WP_COLOR_MANAGER_V1_TRANSFER_FUNCTION_HLG;
    default:
      GST_WARNING ("Transfer function not handled");
      return 0;
  }
}

static enum wp_color_manager_v1_primaries
gst_colorimetry_primaries_to_wl (GstVideoColorPrimaries primaries)
{
  switch (primaries) {
    case GST_VIDEO_COLOR_PRIMARIES_BT709:
      return WP_COLOR_MANAGER_V1_PRIMARIES_SRGB;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTE170M:
      return WP_COLOR_MANAGER_V1_PRIMARIES_NTSC;
    case GST_VIDEO_COLOR_PRIMARIES_BT2020:
      return WP_COLOR_MANAGER_V1_PRIMARIES_BT2020;
    default:
      GST_WARNING ("Primaries not handled");
      return 0;
  }
}

static enum wp_color_representation_surface_v1_coefficients
gst_colorimetry_matrix_to_wl (GstVideoColorMatrix matrix)
{
  switch (matrix) {
    case GST_VIDEO_COLOR_MATRIX_RGB:
      return WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_IDENTITY;
    case GST_VIDEO_COLOR_MATRIX_BT709:
      return WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709;
    case GST_VIDEO_COLOR_MATRIX_BT601:
      return WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT601;
    case GST_VIDEO_COLOR_MATRIX_BT2020:
      return WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT2020;
    default:
      GST_WARNING ("Matrix not handled");
      return 0;
  }
}

static enum wp_color_representation_surface_v1_range
gst_colorimetry_range_to_wl (GstVideoColorRange range)
{
  switch (range) {
    case GST_VIDEO_COLOR_RANGE_0_255:
      return WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_FULL;
    case GST_VIDEO_COLOR_RANGE_16_235:
      return WP_COLOR_REPRESENTATION_SURFACE_V1_RANGE_LIMITED;
    default:
      GST_WARNING ("Range not handled");
      return 0;
  }
}

static void
gst_wl_window_set_image_description (GstWlWindow * self,
    const GstVideoColorimetry * colorimetry,
    const GstVideoMasteringDisplayInfo * minfo,
    const GstVideoContentLightLevel * linfo)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  struct wl_display *wl_display;
  struct wp_color_manager_v1 *color_manager;
  struct wp_color_manager_v1 *color_manager_wrapper = NULL;
  struct wl_event_queue *color_manager_queue = NULL;
  struct wp_image_description_v1 *image_description = NULL;
  struct wp_image_description_creator_params_v1 *params;
  enum ImageDescriptionFeedback image_description_feedback =
      IMAGE_DESCRIPTION_FEEDBACK_UNKNOWN;
  uint32_t wl_transfer_function;
  uint32_t wl_primaries;

  if (!gst_wl_display_is_color_parametric_creator_supported (priv->display)) {
    GST_INFO_OBJECT (self,
        "Color management or parametric creator not supported");
    return;
  }

  color_manager = gst_wl_display_get_color_manager_v1 (priv->display);
  if (!priv->color_management_surface) {
    priv->color_management_surface =
        wp_color_manager_v1_get_surface (color_manager,
        priv->video_surface_wrapper);
  }

  wl_transfer_function = gst_colorimetry_tf_to_wl (colorimetry->transfer);
  wl_primaries = gst_colorimetry_primaries_to_wl (colorimetry->primaries);

  if (!gst_wl_display_is_color_transfer_function_supported (priv->display,
          wl_transfer_function) ||
      !gst_wl_display_are_color_primaries_supported (priv->display,
          wl_primaries)) {
    wp_color_management_surface_v1_unset_image_description
        (priv->color_management_surface);

    GST_INFO_OBJECT (self,
        "Can not create image description: primaries or transfer function not supported");
    return;
  }

  color_manager_wrapper = wl_proxy_create_wrapper (color_manager);
  wl_display = gst_wl_display_get_display (priv->display);
#ifdef HAVE_WL_EVENT_QUEUE_NAME
  color_manager_queue = wl_display_create_queue_with_name (wl_display,
      "GStreamer color manager queue");
#else
  color_manager_queue = wl_display_create_queue (wl_display);
#endif
  wl_proxy_set_queue ((struct wl_proxy *) color_manager_wrapper,
      color_manager_queue);

  params =
      wp_color_manager_v1_create_parametric_creator (color_manager_wrapper);

  wp_image_description_creator_params_v1_set_tf_named (params,
      wl_transfer_function);
  wp_image_description_creator_params_v1_set_primaries_named (params,
      wl_primaries);

  if (gst_wl_display_is_color_mastering_display_supported (priv->display)
      && minfo) {
    /* first validate our luminance range */
    guint min_luminance = minfo->min_display_mastering_luminance / 10000;
    guint max_luminance =
        MAX (min_luminance + 1, minfo->max_display_mastering_luminance / 10000);

    /* We need to convert from 0.00002 unit to 0.000001 */
    const guint f = 20;
    wp_image_description_creator_params_v1_set_mastering_display_primaries
        (params,
        minfo->display_primaries[0].x * f, minfo->display_primaries[0].y * f,
        minfo->display_primaries[1].x * f, minfo->display_primaries[1].y * f,
        minfo->display_primaries[2].x * f, minfo->display_primaries[2].y * f,
        minfo->white_point.x * f, minfo->white_point.y * f);
    wp_image_description_creator_params_v1_set_mastering_luminance (params,
        minfo->min_display_mastering_luminance, max_luminance);

    /*
     * FIXME its unclear what makes a color volume exceeds the primary volume,
     * and how to verify it, ignoring this aspect for now, but may need to be
     * revisited.
     */

    /* We can't set the light level if we don't know the luminance range */
    if (linfo) {
      guint maxFALL = CLAMP (min_luminance + 1,
          linfo->max_frame_average_light_level, max_luminance);
      guint maxCLL =
          CLAMP (maxFALL, linfo->max_content_light_level, max_luminance);
      wp_image_description_creator_params_v1_set_max_cll (params, maxCLL);
      wp_image_description_creator_params_v1_set_max_fall (params, maxFALL);
    }
  }

  image_description = wp_image_description_creator_params_v1_create (params);
  wp_image_description_v1_add_listener (image_description,
      &description_listerer, &image_description_feedback);

  while (image_description_feedback == IMAGE_DESCRIPTION_FEEDBACK_UNKNOWN) {
    if (wl_display_dispatch_queue (wl_display, color_manager_queue) == -1)
      break;
  }

  if (image_description_feedback == IMAGE_DESCRIPTION_FEEDBACK_READY) {
    wp_color_management_surface_v1_set_image_description
        (priv->color_management_surface, image_description,
        WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL);

    GST_INFO_OBJECT (self, "Successfully set parametric image description");
  } else {
    wp_color_management_surface_v1_unset_image_description
        (priv->color_management_surface);

    GST_INFO_OBJECT (self, "Creating image description failed");
  }

  /* Setting the image description has copy semantics */
  wp_image_description_v1_destroy (image_description);
  wl_proxy_wrapper_destroy (color_manager_wrapper);
  wl_event_queue_destroy (color_manager_queue);
}

static void
gst_wl_window_set_color_representation (GstWlWindow * self,
    const GstVideoColorimetry * colorimetry)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);
  struct wp_color_representation_manager_v1 *cr_manager;
  uint32_t wl_alpha_mode;
  uint32_t wl_coefficients;
  uint32_t wl_range;
  gboolean alpha_mode_supported;
  gboolean coefficients_supported;

  cr_manager =
      gst_wl_display_get_color_representation_manager_v1 (priv->display);
  if (!cr_manager) {
    GST_INFO_OBJECT (self, "Color representation not supported");
    return;
  }

  wl_alpha_mode = WP_COLOR_REPRESENTATION_SURFACE_V1_ALPHA_MODE_STRAIGHT;
  alpha_mode_supported =
      gst_wl_display_is_color_alpha_mode_supported (priv->display,
      wl_alpha_mode);

  wl_coefficients = gst_colorimetry_matrix_to_wl (colorimetry->matrix);
  wl_range = gst_colorimetry_range_to_wl (colorimetry->range);
  coefficients_supported =
      gst_wl_display_are_color_coefficients_supported (priv->display,
      wl_coefficients, wl_range);

  if (alpha_mode_supported || coefficients_supported) {
    if (!priv->color_representation_surface) {
      priv->color_representation_surface =
          wp_color_representation_manager_v1_get_surface (cr_manager,
          priv->video_surface_wrapper);
    }

    if (alpha_mode_supported)
      wp_color_representation_surface_v1_set_alpha_mode
          (priv->color_representation_surface, wl_alpha_mode);

    if (coefficients_supported)
      wp_color_representation_surface_v1_set_coefficients_and_range
          (priv->color_representation_surface, wl_coefficients, wl_range);

    GST_INFO_OBJECT (self, "Successfully set color representation");
  } else {
    if (priv->color_representation_surface) {
      wp_color_representation_surface_v1_destroy
          (priv->color_representation_surface);
      priv->color_representation_surface = NULL;
    }

    GST_INFO_OBJECT (self, "Coefficients and range not supported");
  }
}

static void
gst_wl_window_set_colorimetry (GstWlWindow * self,
    const GstVideoColorimetry * colorimetry,
    const GstVideoMasteringDisplayInfo * minfo,
    const GstVideoContentLightLevel * linfo)
{
  GST_OBJECT_LOCK (self);

  GST_INFO_OBJECT (self, "Trying to set colorimetry: %s",
      gst_video_colorimetry_to_string (colorimetry));

  gst_wl_window_set_image_description (self, colorimetry, minfo, linfo);
  gst_wl_window_set_color_representation (self, colorimetry);

  GST_OBJECT_UNLOCK (self);
}

void
gst_wl_window_set_force_aspect_ratio (GstWlWindow * self,
    gboolean force_aspect_ratio)
{
  GstWlWindowPrivate *priv = gst_wl_window_get_instance_private (self);

  priv->force_aspect_ratio = force_aspect_ratio;

  gst_wl_window_update_geometry (self);
}
