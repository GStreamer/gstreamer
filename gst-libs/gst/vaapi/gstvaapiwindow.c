/*
 *  gstvaapiwindow.c - VA window abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
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
 * SECTION:gstvaapiwindow
 * @short_description: VA window abstraction
 */

#include "sysdeps.h"
#include "gstvaapiwindow.h"
#include "gstvaapiwindow_priv.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapisurface_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* Ensure those symbols are actually defined in the resulting libraries */
#undef gst_vaapi_window_ref
#undef gst_vaapi_window_unref
#undef gst_vaapi_window_replace

static void
gst_vaapi_window_ensure_size (GstVaapiWindow * window)
{
  const GstVaapiWindowClass *const klass = GST_VAAPI_WINDOW_GET_CLASS (window);

  if (!window->check_geometry)
    return;

  if (klass->get_geometry)
    klass->get_geometry (window, NULL, NULL, &window->width, &window->height);

  window->check_geometry = FALSE;
  window->is_fullscreen = (window->width == window->display_width &&
      window->height == window->display_height);
}

static gboolean
ensure_filter (GstVaapiWindow * window)
{
  GstVaapiDisplay *const display = GST_VAAPI_OBJECT_DISPLAY (window);

  /* Ensure VPP pipeline is built */
  if (window->filter)
    return TRUE;

  window->filter = gst_vaapi_filter_new (display);
  if (!window->filter)
    goto error_create_filter;
  if (!gst_vaapi_filter_set_format (window->filter, GST_VIDEO_FORMAT_NV12))
    goto error_unsupported_format;

  return TRUE;

error_create_filter:
  {
    GST_WARNING ("failed to create VPP filter. Disabling");
    window->has_vpp = FALSE;
    return FALSE;
  }
error_unsupported_format:
  {
    GST_ERROR ("unsupported render target format %s",
        gst_vaapi_video_format_to_string (GST_VIDEO_FORMAT_NV12));
    window->has_vpp = FALSE;
    return FALSE;
  }
}

static gboolean
ensure_filter_surface_pool (GstVaapiWindow * window)
{
  GstVaapiDisplay *const display = GST_VAAPI_OBJECT_DISPLAY (window);

  if (window->surface_pool)
    goto ensure_filter;

  /* Ensure VA surface pool is created */
  /* XXX: optimize the surface format to use. e.g. YUY2 */
  window->surface_pool = gst_vaapi_surface_pool_new (display,
      GST_VIDEO_FORMAT_NV12, window->width, window->height);
  if (!window->surface_pool) {
    GST_WARNING ("failed to create surface pool for conversion");
    return FALSE;
  }
  gst_vaapi_filter_replace (&window->filter, NULL);

ensure_filter:
  return ensure_filter (window);
}

static gboolean
gst_vaapi_window_create (GstVaapiWindow * window, guint width, guint height)
{
  gst_vaapi_display_get_size (GST_VAAPI_OBJECT_DISPLAY (window),
      &window->display_width, &window->display_height);

  if (!GST_VAAPI_WINDOW_GET_CLASS (window)->create (window, &width, &height))
    return FALSE;

  if (width != window->width || height != window->height) {
    GST_DEBUG ("backend resized window to %ux%u", width, height);
    window->width = width;
    window->height = height;
  }
  return TRUE;
}

static void
gst_vaapi_window_finalize (GstVaapiWindow * window)
{
  gst_vaapi_video_pool_replace (&window->surface_pool, NULL);
  gst_vaapi_filter_replace (&window->filter, NULL);
}

void
gst_vaapi_window_class_init (GstVaapiWindowClass * klass)
{
  GstVaapiObjectClass *const object_class = GST_VAAPI_OBJECT_CLASS (klass);

  object_class->finalize = (GstVaapiObjectFinalizeFunc)
      gst_vaapi_window_finalize;
}

GstVaapiWindow *
gst_vaapi_window_new_internal (const GstVaapiWindowClass * window_class,
    GstVaapiDisplay * display, GstVaapiID id, guint width, guint height)
{
  GstVaapiWindow *window;

  if (id != GST_VAAPI_ID_INVALID) {
    g_return_val_if_fail (width == 0, NULL);
    g_return_val_if_fail (height == 0, NULL);
  } else {
    g_return_val_if_fail (width > 0, NULL);
    g_return_val_if_fail (height > 0, NULL);
  }

  window = gst_vaapi_object_new (GST_VAAPI_OBJECT_CLASS (window_class),
      display);
  if (!window)
    return NULL;

  window->use_foreign_window = id != GST_VAAPI_ID_INVALID;
  GST_VAAPI_OBJECT_ID (window) = window->use_foreign_window ? id : 0;
  window->has_vpp =
      GST_VAAPI_DISPLAY_HAS_VPP (GST_VAAPI_OBJECT_DISPLAY (window));

  if (!gst_vaapi_window_create (window, width, height))
    goto error;
  return window;

  /* ERRORS */
error:
  {
    gst_vaapi_window_unref_internal (window);
    return NULL;
  }
}

GstVaapiSurface *
gst_vaapi_window_vpp_convert_internal (GstVaapiWindow * window,
    GstVaapiSurface * surface, const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect, guint flags)
{
  GstVaapiSurface *vpp_surface = NULL;
  GstVaapiFilterStatus status;

  if (!window->has_vpp)
    return NULL;

  if (!ensure_filter_surface_pool (window))
    return NULL;

  if (src_rect)
    if (!gst_vaapi_filter_set_cropping_rectangle (window->filter, src_rect))
      return NULL;
  if (dst_rect)
    if (!gst_vaapi_filter_set_target_rectangle (window->filter, dst_rect))
      return NULL;

  /* Post-process the decoded source surface */
  vpp_surface = gst_vaapi_video_pool_get_object (window->surface_pool);
  if (!vpp_surface)
    return NULL;

  status =
      gst_vaapi_filter_process (window->filter, surface, vpp_surface, flags);
  if (status != GST_VAAPI_FILTER_STATUS_SUCCESS)
    goto error_process_filter;
  return vpp_surface;

  /* ERRORS */
error_process_filter:
  {
    GST_ERROR ("failed to process surface %" GST_VAAPI_ID_FORMAT " (error %d)",
        GST_VAAPI_ID_ARGS (GST_VAAPI_OBJECT_ID (surface)), status);
    gst_vaapi_video_pool_put_object (window->surface_pool, vpp_surface);
    return NULL;
  }
}

/**
 * gst_vaapi_window_new:
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
gst_vaapi_window_new (GstVaapiDisplay * display, guint width, guint height)
{
  GstVaapiDisplayClass *dpy_class;

  g_return_val_if_fail (display != NULL, NULL);

  dpy_class = GST_VAAPI_DISPLAY_GET_CLASS (display);
  if (G_UNLIKELY (!dpy_class->create_window))
    return NULL;
  return dpy_class->create_window (display, GST_VAAPI_ID_INVALID, width,
      height);
}

/**
 * gst_vaapi_window_ref:
 * @window: a #GstVaapiWindow
 *
 * Atomically increases the reference count of the given @window by one.
 *
 * Returns: The same @window argument
 */
GstVaapiWindow *
gst_vaapi_window_ref (GstVaapiWindow * window)
{
  return gst_vaapi_window_ref_internal (window);
}

/**
 * gst_vaapi_window_unref:
 * @window: a #GstVaapiWindow
 *
 * Atomically decreases the reference count of the @window by one. If
 * the reference count reaches zero, the window will be free'd.
 */
void
gst_vaapi_window_unref (GstVaapiWindow * window)
{
  gst_vaapi_window_unref_internal (window);
}

/**
 * gst_vaapi_window_replace:
 * @old_window_ptr: a pointer to a #GstVaapiWindow
 * @new_window: a #GstVaapiWindow
 *
 * Atomically replaces the window window held in @old_window_ptr with
 * @new_window. This means that @old_window_ptr shall reference a
 * valid window. However, @new_window can be NULL.
 */
void
gst_vaapi_window_replace (GstVaapiWindow ** old_window_ptr,
    GstVaapiWindow * new_window)
{
  gst_vaapi_window_replace_internal (old_window_ptr, new_window);
}

/**
 * gst_vaapi_window_get_display:
 * @window: a #GstVaapiWindow
 *
 * Returns the #GstVaapiDisplay this @window is bound to.
 *
 * Return value: the parent #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_window_get_display (GstVaapiWindow * window)
{
  g_return_val_if_fail (window != NULL, NULL);

  return GST_VAAPI_OBJECT_DISPLAY (window);
}

/**
 * gst_vaapi_window_show:
 * @window: a #GstVaapiWindow
 *
 * Flags a window to be displayed. Any window that is not shown will
 * not appear on the screen.
 */
void
gst_vaapi_window_show (GstVaapiWindow * window)
{
  g_return_if_fail (window != NULL);

  GST_VAAPI_WINDOW_GET_CLASS (window)->show (window);
  window->check_geometry = TRUE;
}

/**
 * gst_vaapi_window_hide:
 * @window: a #GstVaapiWindow
 *
 * Reverses the effects of gst_vaapi_window_show(), causing the window
 * to be hidden (invisible to the user).
 */
void
gst_vaapi_window_hide (GstVaapiWindow * window)
{
  g_return_if_fail (window != NULL);

  GST_VAAPI_WINDOW_GET_CLASS (window)->hide (window);
}

/**
 * gst_vaapi_window_get_fullscreen:
 * @window: a #GstVaapiWindow
 *
 * Retrieves whether the @window is fullscreen or not
 *
 * Return value: %TRUE if the window is fullscreen
 */
gboolean
gst_vaapi_window_get_fullscreen (GstVaapiWindow * window)
{
  g_return_val_if_fail (window != NULL, FALSE);

  gst_vaapi_window_ensure_size (window);

  return window->is_fullscreen;
}

/**
 * gst_vaapi_window_set_fullscreen:
 * @window: a #GstVaapiWindow
 * @fullscreen: %TRUE to request window to get fullscreen
 *
 * Requests to place the @window in fullscreen or unfullscreen states.
 */
void
gst_vaapi_window_set_fullscreen (GstVaapiWindow * window, gboolean fullscreen)
{
  const GstVaapiWindowClass *klass;

  g_return_if_fail (window != NULL);

  klass = GST_VAAPI_WINDOW_GET_CLASS (window);

  if (window->is_fullscreen != fullscreen &&
      klass->set_fullscreen && klass->set_fullscreen (window, fullscreen)) {
    window->is_fullscreen = fullscreen;
    window->check_geometry = TRUE;
  }
}

/**
 * gst_vaapi_window_get_width:
 * @window: a #GstVaapiWindow
 *
 * Retrieves the width of a #GstVaapiWindow.
 *
 * Return value: the width of the @window, in pixels
 */
guint
gst_vaapi_window_get_width (GstVaapiWindow * window)
{
  g_return_val_if_fail (window != NULL, 0);

  gst_vaapi_window_ensure_size (window);

  return window->width;
}

/**
 * gst_vaapi_window_get_height:
 * @window: a #GstVaapiWindow
 *
 * Retrieves the height of a #GstVaapiWindow
 *
 * Return value: the height of the @window, in pixels
 */
guint
gst_vaapi_window_get_height (GstVaapiWindow * window)
{
  g_return_val_if_fail (window != NULL, 0);

  gst_vaapi_window_ensure_size (window);

  return window->height;
}

/**
 * gst_vaapi_window_get_size:
 * @window: a #GstVaapiWindow
 * @width_ptr: return location for the width, or %NULL
 * @height_ptr: return location for the height, or %NULL
 *
 * Retrieves the dimensions of a #GstVaapiWindow.
 */
void
gst_vaapi_window_get_size (GstVaapiWindow * window, guint * width_ptr,
    guint * height_ptr)
{
  g_return_if_fail (window != NULL);

  gst_vaapi_window_ensure_size (window);

  if (width_ptr)
    *width_ptr = window->width;

  if (height_ptr)
    *height_ptr = window->height;
}

/**
 * gst_vaapi_window_set_width:
 * @window: a #GstVaapiWindow
 * @width: requested new width for the window, in pixels
 *
 * Resizes the @window to match the specified @width.
 */
void
gst_vaapi_window_set_width (GstVaapiWindow * window, guint width)
{
  g_return_if_fail (window != NULL);

  gst_vaapi_window_set_size (window, width, window->height);
}

/**
 * gst_vaapi_window_set_height:
 * @window: a #GstVaapiWindow
 * @height: requested new height for the window, in pixels
 *
 * Resizes the @window to match the specified @height.
 */
void
gst_vaapi_window_set_height (GstVaapiWindow * window, guint height)
{
  g_return_if_fail (window != NULL);

  gst_vaapi_window_set_size (window, window->width, height);
}

/**
 * gst_vaapi_window_set_size:
 * @window: a #GstVaapiWindow
 * @width: requested new width for the window, in pixels
 * @height: requested new height for the window, in pixels
 *
 * Resizes the @window to match the specified @width and @height.
 */
void
gst_vaapi_window_set_size (GstVaapiWindow * window, guint width, guint height)
{
  g_return_if_fail (window != NULL);

  if (width == window->width && height == window->height)
    return;

  if (!GST_VAAPI_WINDOW_GET_CLASS (window)->resize (window, width, height))
    return;

  gst_vaapi_video_pool_replace (&window->surface_pool, NULL);

  window->width = width;
  window->height = height;
}

static inline void
get_surface_rect (GstVaapiSurface * surface, GstVaapiRectangle * rect)
{
  rect->x = 0;
  rect->y = 0;
  rect->width = GST_VAAPI_SURFACE_WIDTH (surface);
  rect->height = GST_VAAPI_SURFACE_HEIGHT (surface);
}

static inline void
get_window_rect (GstVaapiWindow * window, GstVaapiRectangle * rect)
{
  guint width, height;

  gst_vaapi_window_get_size (window, &width, &height);
  rect->x = 0;
  rect->y = 0;
  rect->width = width;
  rect->height = height;
}

/**
 * gst_vaapi_window_put_surface:
 * @window: a #GstVaapiWindow
 * @surface: a #GstVaapiSurface
 * @src_rect: the sub-rectangle of the source surface to
 *   extract and process. If %NULL, the entire surface will be used.
 * @dst_rect: the sub-rectangle of the destination
 *   window into which the surface is rendered. If %NULL, the entire
 *   window will be used.
 * @flags: postprocessing flags. See #GstVaapiSurfaceRenderFlags
 *
 * Renders the @surface region specified by @src_rect into the @window
 * region specified by @dst_rect. The @flags specify how de-interlacing
 * (if needed), color space conversion, scaling and other postprocessing
 * transformations are performed.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_window_put_surface (GstVaapiWindow * window,
    GstVaapiSurface * surface,
    const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect, guint flags)
{
  const GstVaapiWindowClass *klass;
  GstVaapiRectangle src_rect_default, dst_rect_default;

  g_return_val_if_fail (window != NULL, FALSE);
  g_return_val_if_fail (surface != NULL, FALSE);

  klass = GST_VAAPI_WINDOW_GET_CLASS (window);
  if (!klass->render)
    return FALSE;

  if (!src_rect) {
    src_rect = &src_rect_default;
    get_surface_rect (surface, &src_rect_default);
  }

  if (!dst_rect) {
    dst_rect = &dst_rect_default;
    get_window_rect (window, &dst_rect_default);
  }

  return klass->render (window, surface, src_rect, dst_rect, flags);
}

static inline void
get_pixmap_rect (GstVaapiPixmap * pixmap, GstVaapiRectangle * rect)
{
  guint width, height;

  gst_vaapi_pixmap_get_size (pixmap, &width, &height);
  rect->x = 0;
  rect->y = 0;
  rect->width = width;
  rect->height = height;
}

/**
 * gst_vaapi_window_put_pixmap:
 * @window: a #GstVaapiWindow
 * @pixmap: a #GstVaapiPixmap
 * @src_rect: the sub-rectangle of the source pixmap to
 *   extract and process. If %NULL, the entire pixmap will be used.
 * @dst_rect: the sub-rectangle of the destination
 *   window into which the pixmap is rendered. If %NULL, the entire
 *   window will be used.
 *
 * Renders the @pixmap region specified by @src_rect into the @window
 * region specified by @dst_rect.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_window_put_pixmap (GstVaapiWindow * window,
    GstVaapiPixmap * pixmap,
    const GstVaapiRectangle * src_rect, const GstVaapiRectangle * dst_rect)
{
  const GstVaapiWindowClass *klass;
  GstVaapiRectangle src_rect_default, dst_rect_default;

  g_return_val_if_fail (window != NULL, FALSE);
  g_return_val_if_fail (pixmap != NULL, FALSE);

  klass = GST_VAAPI_WINDOW_GET_CLASS (window);
  if (!klass->render_pixmap)
    return FALSE;

  if (!src_rect) {
    src_rect = &src_rect_default;
    get_pixmap_rect (pixmap, &src_rect_default);
  }

  if (!dst_rect) {
    dst_rect = &dst_rect_default;
    get_window_rect (window, &dst_rect_default);
  }
  return klass->render_pixmap (window, pixmap, src_rect, dst_rect);
}

/**
 * gst_vaapi_window_reconfigure:
 * @window: a #GstVaapiWindow
 *
 * Updates internal window size from geometry of the underlying window
 * implementation if necessary.
 */
void
gst_vaapi_window_reconfigure (GstVaapiWindow * window)
{
  g_return_if_fail (window != NULL);

  window->check_geometry = TRUE;
  gst_vaapi_window_ensure_size (window);
}

/**
 * gst_vaapi_window_unblock:
 * @window: a #GstVaapiWindow
 *
 * Unblocks a rendering surface operation.
 */
gboolean
gst_vaapi_window_unblock (GstVaapiWindow * window)
{
  const GstVaapiWindowClass *klass;

  g_return_val_if_fail (window != NULL, FALSE);

  klass = GST_VAAPI_WINDOW_GET_CLASS (window);

  if (klass->unblock)
    return klass->unblock (window);

  return TRUE;
}

/**
 * gst_vaapi_window_unblock_cancel:
 * @window: a #GstVaapiWindow
 *
 * Cancels the previous unblock request.
 */
gboolean
gst_vaapi_window_unblock_cancel (GstVaapiWindow * window)
{
  const GstVaapiWindowClass *klass;

  g_return_val_if_fail (window != NULL, FALSE);

  klass = GST_VAAPI_WINDOW_GET_CLASS (window);

  if (klass->unblock_cancel)
    return klass->unblock_cancel (window);

  return TRUE;
}
