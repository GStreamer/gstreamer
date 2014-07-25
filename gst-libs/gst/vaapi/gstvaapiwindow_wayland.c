/*
 *  gstvaapiwindow_wayland.c - VA/Wayland window abstraction
 *
 *  Copyright (C) 2012-2013 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapiwindow_wayland
 * @short_description: VA/Wayland window abstraction
 */

#include "sysdeps.h"
#include <string.h>
#include "gstvaapicompat.h"
#include "gstvaapiwindow_wayland.h"
#include "gstvaapiwindow_priv.h"
#include "gstvaapidisplay_wayland.h"
#include "gstvaapidisplay_wayland_priv.h"
#include "gstvaapiutils.h"
#include "gstvaapifilter.h"
#include "gstvaapisurfacepool.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_WINDOW_WAYLAND_CAST(obj) \
    ((GstVaapiWindowWayland *)(obj))

#define GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE(obj) \
    (&GST_VAAPI_WINDOW_WAYLAND_CAST(obj)->priv)

typedef struct _GstVaapiWindowWaylandPrivate GstVaapiWindowWaylandPrivate;
typedef struct _GstVaapiWindowWaylandClass GstVaapiWindowWaylandClass;
typedef struct _FrameState FrameState;

struct _FrameState
{
  GstVaapiWindow *window;
  GstVaapiSurface *surface;
  GstVaapiVideoPool *surface_pool;
  struct wl_buffer *buffer;
  struct wl_callback *callback;
};

static FrameState *
frame_state_new (GstVaapiWindow * window)
{
  FrameState *frame;

  frame = g_slice_new (FrameState);
  if (!frame)
    return NULL;

  frame->window = window;
  frame->surface = NULL;
  frame->surface_pool = NULL;
  frame->buffer = NULL;
  frame->callback = NULL;
  return frame;
}

static void
frame_state_free (FrameState * frame)
{
  if (!frame)
    return;

  if (frame->surface) {
    if (frame->surface_pool)
      gst_vaapi_video_pool_put_object (frame->surface_pool, frame->surface);
    frame->surface = NULL;
  }
  gst_vaapi_video_pool_replace (&frame->surface_pool, NULL);

  if (frame->buffer) {
    wl_buffer_destroy (frame->buffer);
    frame->buffer = NULL;
  }

  if (frame->callback) {
    wl_callback_destroy (frame->callback);
    frame->callback = NULL;
  }
  g_slice_free (FrameState, frame);
}

struct _GstVaapiWindowWaylandPrivate
{
  struct wl_shell_surface *shell_surface;
  struct wl_surface *surface;
  struct wl_region *opaque_region;
  struct wl_event_queue *event_queue;
  FrameState *frame;
  GstVideoFormat surface_format;
  GstVaapiVideoPool *surface_pool;
  GstVaapiFilter *filter;
  guint is_shown:1;
  guint fullscreen_on_show:1;
  guint use_vpp:1;
};

/**
 * GstVaapiWindowWayland:
 *
 * A Wayland window abstraction.
 */
struct _GstVaapiWindowWayland
{
  /*< private >*/
  GstVaapiWindow parent_instance;

  GstVaapiWindowWaylandPrivate priv;
};

/**
 * GstVaapiWindowWaylandClass:
 *
 * An Wayland #Window wrapper class.
 */
struct _GstVaapiWindowWaylandClass
{
  /*< private >*/
  GstVaapiWindowClass parent_class;
};

static gboolean
gst_vaapi_window_wayland_show (GstVaapiWindow * window)
{
  GST_WARNING ("unimplemented GstVaapiWindowWayland::show()");

  return TRUE;
}

static gboolean
gst_vaapi_window_wayland_hide (GstVaapiWindow * window)
{
  GST_WARNING ("unimplemented GstVaapiWindowWayland::hide()");

  return TRUE;
}

static gboolean
gst_vaapi_window_wayland_sync (GstVaapiWindow * window)
{
  GstVaapiWindowWaylandPrivate *const priv =
      GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE (window);

  if (priv->frame) {
    struct wl_display *const wl_display = GST_VAAPI_OBJECT_WL_DISPLAY (window);

    do {
      if (wl_display_dispatch_queue (wl_display, priv->event_queue) < 0)
        return FALSE;
    } while (priv->frame);
  }
  return TRUE;
}

static void
handle_ping (void *data, struct wl_shell_surface *shell_surface,
    uint32_t serial)
{
  wl_shell_surface_pong (shell_surface, serial);
}

static void
handle_configure (void *data, struct wl_shell_surface *shell_surface,
    uint32_t edges, int32_t width, int32_t height)
{
}

static void
handle_popup_done (void *data, struct wl_shell_surface *shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener = {
  handle_ping,
  handle_configure,
  handle_popup_done
};

static gboolean
gst_vaapi_window_wayland_set_fullscreen (GstVaapiWindow * window,
    gboolean fullscreen)
{
  GstVaapiWindowWaylandPrivate *const priv =
      GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE (window);

  if (!priv->is_shown) {
    priv->fullscreen_on_show = fullscreen;
    return TRUE;
  }

  if (!fullscreen)
    wl_shell_surface_set_toplevel (priv->shell_surface);
  else {
    wl_shell_surface_set_fullscreen (priv->shell_surface,
        WL_SHELL_SURFACE_FULLSCREEN_METHOD_SCALE, 0, NULL);
  }

  return TRUE;
}

static gboolean
gst_vaapi_window_wayland_create (GstVaapiWindow * window,
    guint * width, guint * height)
{
  GstVaapiWindowWaylandPrivate *const priv =
      GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE (window);
  GstVaapiDisplayWaylandPrivate *const priv_display =
      GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE (GST_VAAPI_OBJECT_DISPLAY (window));

  GST_DEBUG ("create window, size %ux%u", *width, *height);

  g_return_val_if_fail (priv_display->compositor != NULL, FALSE);
  g_return_val_if_fail (priv_display->shell != NULL, FALSE);

  GST_VAAPI_OBJECT_LOCK_DISPLAY (window);
  priv->event_queue = wl_display_create_queue (priv_display->wl_display);
  GST_VAAPI_OBJECT_UNLOCK_DISPLAY (window);
  if (!priv->event_queue)
    return FALSE;

  GST_VAAPI_OBJECT_LOCK_DISPLAY (window);
  priv->surface = wl_compositor_create_surface (priv_display->compositor);
  GST_VAAPI_OBJECT_UNLOCK_DISPLAY (window);
  if (!priv->surface)
    return FALSE;
  wl_proxy_set_queue ((struct wl_proxy *) priv->surface, priv->event_queue);

  GST_VAAPI_OBJECT_LOCK_DISPLAY (window);
  priv->shell_surface =
      wl_shell_get_shell_surface (priv_display->shell, priv->surface);
  GST_VAAPI_OBJECT_UNLOCK_DISPLAY (window);
  if (!priv->shell_surface)
    return FALSE;
  wl_proxy_set_queue ((struct wl_proxy *) priv->shell_surface,
      priv->event_queue);

  wl_shell_surface_add_listener (priv->shell_surface,
      &shell_surface_listener, priv);
  wl_shell_surface_set_toplevel (priv->shell_surface);

  if (priv->fullscreen_on_show)
    gst_vaapi_window_wayland_set_fullscreen (window, TRUE);

  priv->surface_format = GST_VIDEO_FORMAT_ENCODED;
  priv->use_vpp = GST_VAAPI_DISPLAY_HAS_VPP (GST_VAAPI_OBJECT_DISPLAY (window));
  priv->is_shown = TRUE;

  return TRUE;
}

static void
gst_vaapi_window_wayland_destroy (GstVaapiWindow * window)
{
  GstVaapiWindowWaylandPrivate *const priv =
      GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE (window);

  if (priv->frame) {
    frame_state_free (priv->frame);
    priv->frame = NULL;
  }

  if (priv->shell_surface) {
    wl_shell_surface_destroy (priv->shell_surface);
    priv->shell_surface = NULL;
  }

  if (priv->surface) {
    wl_surface_destroy (priv->surface);
    priv->surface = NULL;
  }

  if (priv->event_queue) {
    wl_event_queue_destroy (priv->event_queue);
    priv->event_queue = NULL;
  }

  gst_vaapi_filter_replace (&priv->filter, NULL);
  gst_vaapi_video_pool_replace (&priv->surface_pool, NULL);
}

static gboolean
gst_vaapi_window_wayland_resize (GstVaapiWindow * window,
    guint width, guint height)
{
  GstVaapiWindowWaylandPrivate *const priv =
      GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE (window);
  GstVaapiDisplayWaylandPrivate *const priv_display =
      GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE (GST_VAAPI_OBJECT_DISPLAY (window));

  GST_DEBUG ("resize window, new size %ux%u", width, height);

  if (priv->opaque_region)
    wl_region_destroy (priv->opaque_region);
  GST_VAAPI_OBJECT_LOCK_DISPLAY (window);
  priv->opaque_region = wl_compositor_create_region (priv_display->compositor);
  GST_VAAPI_OBJECT_UNLOCK_DISPLAY (window);
  wl_region_add (priv->opaque_region, 0, 0, width, height);

  return TRUE;
}

static void
frame_redraw_callback (void *data, struct wl_callback *callback, uint32_t time)
{
  FrameState *const frame = data;
  GstVaapiWindowWaylandPrivate *const priv =
      GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE (frame->window);

  frame_state_free (frame);
  if (priv->frame == frame)
    priv->frame = NULL;
}

static const struct wl_callback_listener frame_callback_listener = {
  frame_redraw_callback
};

static GstVaapiSurface *
vpp_convert (GstVaapiWindow * window,
    GstVaapiSurface * surface,
    const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect, guint flags)
{
  GstVaapiWindowWaylandPrivate *const priv =
      GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE (window);
  GstVaapiDisplay *const display = GST_VAAPI_OBJECT_DISPLAY (window);
  GstVaapiSurface *vpp_surface = NULL;
  GstVaapiFilterStatus status;
  GstVideoInfo vi;

  /* Ensure VA surface pool is created */
  /* XXX: optimize the surface format to use. e.g. YUY2 */
  if (!priv->surface_pool) {
    gst_video_info_set_format (&vi, priv->surface_format,
        window->width, window->height);
    priv->surface_pool = gst_vaapi_surface_pool_new (display, &vi);
    if (!priv->surface_pool)
      return NULL;
    gst_vaapi_filter_replace (&priv->filter, NULL);
  }

  /* Ensure VPP pipeline is built */
  if (!priv->filter) {
    priv->filter = gst_vaapi_filter_new (display);
    if (!priv->filter)
      goto error_create_filter;
    if (!gst_vaapi_filter_set_format (priv->filter, priv->surface_format))
      goto error_unsupported_format;
  }
  if (!gst_vaapi_filter_set_cropping_rectangle (priv->filter, src_rect))
    return NULL;
  if (!gst_vaapi_filter_set_target_rectangle (priv->filter, dst_rect))
    return NULL;

  /* Post-process the decoded source surface */
  vpp_surface = gst_vaapi_video_pool_get_object (priv->surface_pool);
  if (!vpp_surface)
    return NULL;

  status = gst_vaapi_filter_process (priv->filter, surface, vpp_surface, flags);
  if (status != GST_VAAPI_FILTER_STATUS_SUCCESS)
    goto error_process_filter;
  return vpp_surface;

  /* ERRORS */
error_create_filter:
  GST_WARNING ("failed to create VPP filter. Disabling");
  priv->use_vpp = FALSE;
  return NULL;
error_unsupported_format:
  GST_ERROR ("unsupported render target format %s",
      gst_vaapi_video_format_to_string (priv->surface_format));
  priv->use_vpp = FALSE;
  return NULL;
error_process_filter:
  GST_ERROR ("failed to process surface %" GST_VAAPI_ID_FORMAT " (error %d)",
      GST_VAAPI_ID_ARGS (GST_VAAPI_OBJECT_ID (surface)), status);
  gst_vaapi_video_pool_put_object (priv->surface_pool, vpp_surface);
  return NULL;
}

static gboolean
gst_vaapi_window_wayland_render (GstVaapiWindow * window,
    GstVaapiSurface * surface,
    const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect, guint flags)
{
  GstVaapiWindowWaylandPrivate *const priv =
      GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE (window);
  GstVaapiDisplay *const display = GST_VAAPI_OBJECT_DISPLAY (window);
  struct wl_display *const wl_display = GST_VAAPI_OBJECT_WL_DISPLAY (window);
  struct wl_buffer *buffer;
  FrameState *frame;
  guint width, height, va_flags;
  VAStatus status;
  gboolean need_vpp = FALSE;

  /* Check that we don't need to crop source VA surface */
  gst_vaapi_surface_get_size (surface, &width, &height);
  if (src_rect->x != 0 || src_rect->y != 0)
    need_vpp = TRUE;
  if (src_rect->width != width || src_rect->height != height)
    need_vpp = TRUE;

  /* Check that we don't render to a subregion of this window */
  if (dst_rect->x != 0 || dst_rect->y != 0)
    need_vpp = TRUE;
  if (dst_rect->width != window->width || dst_rect->height != window->height)
    need_vpp = TRUE;

  /* Try to construct a Wayland buffer from VA surface as is (without VPP) */
  if (!need_vpp) {
    GST_VAAPI_OBJECT_LOCK_DISPLAY (window);
    va_flags = from_GstVaapiSurfaceRenderFlags (flags);
    status = vaGetSurfaceBufferWl (GST_VAAPI_DISPLAY_VADISPLAY (display),
        GST_VAAPI_OBJECT_ID (surface),
        va_flags & (VA_TOP_FIELD | VA_BOTTOM_FIELD), &buffer);
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY (window);
    if (status == VA_STATUS_ERROR_FLAG_NOT_SUPPORTED)
      need_vpp = TRUE;
    else if (!vaapi_check_status (status, "vaGetSurfaceBufferWl()"))
      return FALSE;
  }

  /* Try to construct a Wayland buffer with VPP */
  if (need_vpp) {
    if (priv->use_vpp) {
      GstVaapiSurface *const vpp_surface =
          vpp_convert (window, surface, src_rect, dst_rect, flags);
      if (G_UNLIKELY (!vpp_surface))
        need_vpp = FALSE;
      else {
        surface = vpp_surface;
        width = window->width;
        height = window->height;
      }
    }

    GST_VAAPI_OBJECT_LOCK_DISPLAY (window);
    status = vaGetSurfaceBufferWl (GST_VAAPI_DISPLAY_VADISPLAY (display),
        GST_VAAPI_OBJECT_ID (surface), VA_FRAME_PICTURE, &buffer);
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY (window);
    if (!vaapi_check_status (status, "vaGetSurfaceBufferWl()"))
      return FALSE;
  }

  /* Wait for the previous frame to complete redraw */
  if (!gst_vaapi_window_wayland_sync (window))
    return FALSE;

  frame = frame_state_new (window);
  if (!frame)
    return FALSE;
  priv->frame = frame;

  if (need_vpp && priv->use_vpp) {
    frame->surface = surface;
    frame->surface_pool = gst_vaapi_video_pool_ref (priv->surface_pool);
  }

  /* XXX: attach to the specified target rectangle */
  GST_VAAPI_OBJECT_LOCK_DISPLAY (window);
  wl_surface_attach (priv->surface, buffer, 0, 0);
  wl_surface_damage (priv->surface, 0, 0, width, height);

  if (priv->opaque_region) {
    wl_surface_set_opaque_region (priv->surface, priv->opaque_region);
    wl_region_destroy (priv->opaque_region);
    priv->opaque_region = NULL;
  }

  frame->buffer = buffer;
  frame->callback = wl_surface_frame (priv->surface);
  wl_callback_add_listener (frame->callback, &frame_callback_listener, frame);

  wl_surface_commit (priv->surface);
  wl_display_flush (wl_display);
  GST_VAAPI_OBJECT_UNLOCK_DISPLAY (window);
  return TRUE;
}

static void
gst_vaapi_window_wayland_class_init (GstVaapiWindowWaylandClass * klass)
{
  GstVaapiObjectClass *const object_class = GST_VAAPI_OBJECT_CLASS (klass);
  GstVaapiWindowClass *const window_class = GST_VAAPI_WINDOW_CLASS (klass);

  object_class->finalize = (GstVaapiObjectFinalizeFunc)
      gst_vaapi_window_wayland_destroy;

  window_class->create = gst_vaapi_window_wayland_create;
  window_class->show = gst_vaapi_window_wayland_show;
  window_class->hide = gst_vaapi_window_wayland_hide;
  window_class->render = gst_vaapi_window_wayland_render;
  window_class->resize = gst_vaapi_window_wayland_resize;
  window_class->set_fullscreen = gst_vaapi_window_wayland_set_fullscreen;
}

#define gst_vaapi_window_wayland_finalize \
    gst_vaapi_window_wayland_destroy

GST_VAAPI_OBJECT_DEFINE_CLASS_WITH_CODE (GstVaapiWindowWayland,
    gst_vaapi_window_wayland, gst_vaapi_window_wayland_class_init (&g_class));

/**
 * gst_vaapi_window_wayland_new:
 * @display: a #GstVaapiDisplay
 * @width: the requested window width, in pixels
 * @height: the requested windo height, in pixels
 *
 * Creates a window with the specified @width and @height. The window
 * will be attached to the @display and remains invisible to the user
 * until gst_vaapi_window_show() is called.
 *
 * Return value: the newly allocated #GstVaapiWindow object
 */
GstVaapiWindow *
gst_vaapi_window_wayland_new (GstVaapiDisplay * display,
    guint width, guint height)
{
  GST_DEBUG ("new window, size %ux%u", width, height);

  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_WAYLAND (display), NULL);

  return
      gst_vaapi_window_new (GST_VAAPI_WINDOW_CLASS
      (gst_vaapi_window_wayland_class ()), display, width, height);
}
