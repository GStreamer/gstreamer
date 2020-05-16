/*
 *  gstvaapidisplay_x11.c - VA/X11 display abstraction
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
 * SECTION:gstvaapidisplay_x11
 * @short_description: VA/X11 display abstraction
 */

#include "sysdeps.h"
#include "gstvaapiutils.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapidisplay_x11.h"
#include "gstvaapidisplay_x11_priv.h"
#include "gstvaapiwindow_x11.h"

#if HAVE_XRANDR
# include <X11/extensions/Xrandr.h>
#endif

#define DEBUG_VAAPI_DISPLAY 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE_WITH_PRIVATE (GstVaapiDisplayX11, gst_vaapi_display_x11,
    GST_TYPE_VAAPI_DISPLAY);

static inline const gchar *
get_default_display_name (void)
{
  static const gchar *g_display_name;

  if (!g_display_name)
    g_display_name = getenv ("DISPLAY");
  return g_display_name;
}

/* Reconstruct a display name without our prefix */
static const gchar *
get_display_name (GstVaapiDisplayX11 * display)
{
  GstVaapiDisplayX11Private *const priv = display->priv;
  const gchar *display_name = priv->display_name;

  if (!display_name || *display_name == '\0')
    return NULL;
  return display_name;
}

/* Mangle display name with our prefix */
static gboolean
set_display_name (GstVaapiDisplayX11 * display, const gchar * display_name)
{
  GstVaapiDisplayX11Private *const priv = display->priv;

  g_free (priv->display_name);

  if (!display_name) {
    display_name = get_default_display_name ();
    if (!display_name)
      display_name = "";
  }
  priv->display_name = g_strdup (display_name);
  return priv->display_name != NULL;
}

/* Set synchronous behavious on the underlying X11 display */
static void
set_synchronous (GstVaapiDisplayX11 * display, gboolean synchronous)
{
  GstVaapiDisplayX11Private *const priv = display->priv;

  if (priv->synchronous != synchronous) {
    priv->synchronous = synchronous;
    if (priv->x11_display) {
      GST_VAAPI_DISPLAY_LOCK (display);
      XSynchronize (priv->x11_display, synchronous);
      GST_VAAPI_DISPLAY_UNLOCK (display);
    }
  }
}

/* Check for display server extensions */
static void
check_extensions (GstVaapiDisplayX11 * display)
{
  GstVaapiDisplayX11Private *const priv = display->priv;
  int evt_base, err_base;

#if HAVE_XRANDR
  priv->use_xrandr = XRRQueryExtension (priv->x11_display,
      &evt_base, &err_base);
#endif
}

static gboolean
gst_vaapi_display_x11_bind_display (GstVaapiDisplay * base_display,
    gpointer native_display)
{
  GstVaapiDisplayX11 *const display = GST_VAAPI_DISPLAY_X11_CAST (base_display);
  GstVaapiDisplayX11Private *const priv = display->priv;

  priv->x11_display = native_display;
  priv->x11_screen = DefaultScreen (native_display);
  priv->use_foreign_display = TRUE;

  check_extensions (display);
  if (!set_display_name (display, XDisplayString (priv->x11_display)))
    return FALSE;
  return TRUE;
}

static gboolean
gst_vaapi_display_x11_open_display (GstVaapiDisplay * base_display,
    const gchar * name)
{
  GstVaapiDisplayX11 *const display = GST_VAAPI_DISPLAY_X11_CAST (base_display);
  GstVaapiDisplayX11Private *const priv = display->priv;

  if (!set_display_name (display, name))
    return FALSE;

  priv->x11_display = XOpenDisplay (get_display_name (display));
  if (!priv->x11_display)
    return FALSE;
  priv->use_foreign_display = FALSE;

  priv->x11_screen = DefaultScreen (priv->x11_display);

  check_extensions (display);
  return TRUE;
}

static void
gst_vaapi_display_x11_close_display (GstVaapiDisplay * display)
{
  GstVaapiDisplayX11Private *const priv =
      GST_VAAPI_DISPLAY_X11_PRIVATE (display);

  g_clear_pointer (&priv->pixmap_formats, g_array_unref);

  if (priv->x11_display) {
    if (!priv->use_foreign_display)
      XCloseDisplay (priv->x11_display);
    priv->x11_display = NULL;
  }

  g_clear_pointer (&priv->display_name, g_free);
}

static void
gst_vaapi_display_x11_sync (GstVaapiDisplay * display)
{
  GstVaapiDisplayX11Private *const priv =
      GST_VAAPI_DISPLAY_X11_PRIVATE (display);

  if (priv->x11_display) {
    GST_VAAPI_DISPLAY_LOCK (display);
    XSync (priv->x11_display, False);
    GST_VAAPI_DISPLAY_UNLOCK (display);
  }
}

static void
gst_vaapi_display_x11_flush (GstVaapiDisplay * display)
{
  GstVaapiDisplayX11Private *const priv =
      GST_VAAPI_DISPLAY_X11_PRIVATE (display);

  if (priv->x11_display) {
    GST_VAAPI_DISPLAY_LOCK (display);
    XFlush (priv->x11_display);
    GST_VAAPI_DISPLAY_UNLOCK (display);
  }
}

static gboolean
gst_vaapi_display_x11_get_display_info (GstVaapiDisplay * display,
    GstVaapiDisplayInfo * info)
{
  GstVaapiDisplayX11Private *const priv =
      GST_VAAPI_DISPLAY_X11_PRIVATE (display);

  info->native_display = priv->x11_display;
  info->display_name = priv->display_name;
  if (!info->va_display) {
    info->va_display = vaGetDisplay (priv->x11_display);
    if (!info->va_display)
      return FALSE;
  }
  return TRUE;
}

static void
gst_vaapi_display_x11_get_size (GstVaapiDisplay * display,
    guint * pwidth, guint * pheight)
{
  GstVaapiDisplayX11Private *const priv =
      GST_VAAPI_DISPLAY_X11_PRIVATE (display);

  if (!priv->x11_display)
    return;

  if (pwidth)
    *pwidth = DisplayWidth (priv->x11_display, priv->x11_screen);

  if (pheight)
    *pheight = DisplayHeight (priv->x11_display, priv->x11_screen);
}

static void
gst_vaapi_display_x11_get_size_mm (GstVaapiDisplay * display,
    guint * pwidth, guint * pheight)
{
  GstVaapiDisplayX11Private *const priv =
      GST_VAAPI_DISPLAY_X11_PRIVATE (display);
  guint width_mm, height_mm;

  if (!priv->x11_display)
    return;

  width_mm = DisplayWidthMM (priv->x11_display, priv->x11_screen);
  height_mm = DisplayHeightMM (priv->x11_display, priv->x11_screen);

#if HAVE_XRANDR
  /* XXX: fix up physical size if the display is rotated */
  if (priv->use_xrandr) {
    XRRScreenConfiguration *xrr_config = NULL;
    XRRScreenSize *xrr_sizes;
    Window win;
    int num_xrr_sizes, size_id, screen;
    Rotation rotation;

    do {
      win = DefaultRootWindow (priv->x11_display);
      screen = XRRRootToScreen (priv->x11_display, win);

      xrr_config = XRRGetScreenInfo (priv->x11_display, win);
      if (!xrr_config)
        break;

      size_id = XRRConfigCurrentConfiguration (xrr_config, &rotation);
      if (rotation == RR_Rotate_0 || rotation == RR_Rotate_180)
        break;

      xrr_sizes = XRRSizes (priv->x11_display, screen, &num_xrr_sizes);
      if (!xrr_sizes || size_id >= num_xrr_sizes)
        break;

      width_mm = xrr_sizes[size_id].mheight;
      height_mm = xrr_sizes[size_id].mwidth;
    } while (0);
    if (xrr_config)
      XRRFreeScreenConfigInfo (xrr_config);
  }
#endif

  if (pwidth)
    *pwidth = width_mm;

  if (pheight)
    *pheight = height_mm;
}

static GstVaapiWindow *
gst_vaapi_display_x11_create_window (GstVaapiDisplay * display, GstVaapiID id,
    guint width, guint height)
{
  return id != GST_VAAPI_ID_INVALID ?
      gst_vaapi_window_x11_new_with_xid (display, id) :
      gst_vaapi_window_x11_new (display, width, height);
}

void
gst_vaapi_display_x11_class_init (GstVaapiDisplayX11Class * klass)
{
  GstVaapiDisplayClass *const dpy_class = GST_VAAPI_DISPLAY_CLASS (klass);

  dpy_class->display_type = GST_VAAPI_DISPLAY_TYPE_X11;
  dpy_class->bind_display = gst_vaapi_display_x11_bind_display;
  dpy_class->open_display = gst_vaapi_display_x11_open_display;
  dpy_class->close_display = gst_vaapi_display_x11_close_display;
  dpy_class->sync = gst_vaapi_display_x11_sync;
  dpy_class->flush = gst_vaapi_display_x11_flush;
  dpy_class->get_display = gst_vaapi_display_x11_get_display_info;
  dpy_class->get_size = gst_vaapi_display_x11_get_size;
  dpy_class->get_size_mm = gst_vaapi_display_x11_get_size_mm;
  dpy_class->create_window = gst_vaapi_display_x11_create_window;
}

static void
gst_vaapi_display_x11_init (GstVaapiDisplayX11 * display)
{
  GstVaapiDisplayX11Private *const priv =
      gst_vaapi_display_x11_get_instance_private (display);

  display->priv = priv;
}

/**
 * gst_vaapi_display_x11_new:
 * @display_name: the X11 display name
 *
 * Opens an X11 #Display using @display_name and returns a newly
 * allocated #GstVaapiDisplay object. The X11 display will be cloed
 * when the reference count of the object reaches zero.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_x11_new (const gchar * display_name)
{
  GstVaapiDisplay *display;

  display = g_object_new (GST_TYPE_VAAPI_DISPLAY_X11, NULL);
  return gst_vaapi_display_config (display,
      GST_VAAPI_DISPLAY_INIT_FROM_DISPLAY_NAME, (gpointer) display_name);
}

/**
 * gst_vaapi_display_x11_new_with_display:
 * @x11_display: an X11 #Display
 *
 * Creates a #GstVaapiDisplay based on the X11 @x11_display
 * display. The caller still owns the display and must call
 * XCloseDisplay() when all #GstVaapiDisplay references are
 * released. Doing so too early can yield undefined behaviour.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_x11_new_with_display (Display * x11_display)
{
  GstVaapiDisplay *display;

  g_return_val_if_fail (x11_display, NULL);

  display = g_object_new (GST_TYPE_VAAPI_DISPLAY_X11, NULL);
  return gst_vaapi_display_config (display,
      GST_VAAPI_DISPLAY_INIT_FROM_NATIVE_DISPLAY, x11_display);
}

GstVaapiDisplay *
gst_vaapi_display_x11_new_with_va_display (VADisplay va_display,
    Display * x11_display)
{
  GstVaapiDisplay *display;
  GstVaapiDisplayInfo info = {
    .va_display = va_display,
    .native_display = x11_display,
  };

  g_return_val_if_fail (x11_display, NULL);

  display = g_object_new (GST_TYPE_VAAPI_DISPLAY_X11, NULL);
  return gst_vaapi_display_config (display,
      GST_VAAPI_DISPLAY_INIT_FROM_VA_DISPLAY, &info);
}

/**
 * gst_vaapi_display_x11_get_display:
 * @display: a #GstVaapiDisplayX11
 *
 * Returns the underlying X11 #Display that was created by
 * gst_vaapi_display_x11_new() or that was bound from
 * gst_vaapi_display_x11_new_with_display().
 *
 * Return value: the X11 #Display attached to @display
 */
Display *
gst_vaapi_display_x11_get_display (GstVaapiDisplayX11 * display)
{
  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_X11 (display), NULL);

  return GST_VAAPI_DISPLAY_XDISPLAY (display);
}

/**
 * gst_vaapi_display_x11_get_screen:
 * @display: a #GstVaapiDisplayX11
 *
 * Returns the default X11 screen that was created by
 * gst_vaapi_display_x11_new() or that was bound from
 * gst_vaapi_display_x11_new_with_display().
 *
 * Return value: the X11 #Display attached to @display
 */
int
gst_vaapi_display_x11_get_screen (GstVaapiDisplayX11 * display)
{
  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_X11 (display), -1);

  return GST_VAAPI_DISPLAY_XSCREEN (display);
}

/**
 * gst_vaapi_display_x11_set_synchronous:
 * @display: a #GstVaapiDisplayX11
 * @synchronous: boolean value that indicates whether to enable or
 *   disable synchronization
 *
 * If @synchronous is %TRUE, gst_vaapi_display_x11_set_synchronous()
 * turns on synchronous behaviour on the underlying X11
 * display. Otherwise, synchronous behaviour is disabled if
 * @synchronous is %FALSE.
 */
void
gst_vaapi_display_x11_set_synchronous (GstVaapiDisplayX11 * display,
    gboolean synchronous)
{
  g_return_if_fail (GST_VAAPI_IS_DISPLAY_X11 (display));

  set_synchronous (display, synchronous);
}

typedef struct _GstVaapiPixmapFormatX11 GstVaapiPixmapFormatX11;
struct _GstVaapiPixmapFormatX11
{
  GstVideoFormat format;
  gint depth;
  gint bpp;
};

static GstVideoFormat
pix_fmt_to_video_format (gint depth, gint bpp)
{
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;

  switch (bpp) {
    case 16:
      if (depth == 15)
        format = GST_VIDEO_FORMAT_RGB15;
      else if (depth == 16)
        format = GST_VIDEO_FORMAT_RGB16;
      break;
    case 24:
      if (depth == 24)
        format = GST_VIDEO_FORMAT_RGB;
      break;
    case 32:
      if (depth == 24 || depth == 32)
        format = GST_VIDEO_FORMAT_xRGB;
      break;
  }
  return format;
}

static gboolean
ensure_pix_fmts (GstVaapiDisplayX11 * display)
{
  GstVaapiDisplayX11Private *const priv =
      GST_VAAPI_DISPLAY_X11_PRIVATE (display);
  XPixmapFormatValues *pix_fmts;
  int i, n, num_pix_fmts;

  if (priv->pixmap_formats)
    return TRUE;

  GST_VAAPI_DISPLAY_LOCK (display);
  pix_fmts = XListPixmapFormats (GST_VAAPI_DISPLAY_XDISPLAY (display),
      &num_pix_fmts);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!pix_fmts)
    return FALSE;

  priv->pixmap_formats = g_array_sized_new (FALSE, FALSE,
      sizeof (GstVaapiPixmapFormatX11), num_pix_fmts);
  if (!priv->pixmap_formats) {
    XFree (pix_fmts);
    return FALSE;
  }

  for (i = 0, n = 0; i < num_pix_fmts; i++) {
    GstVaapiPixmapFormatX11 *const pix_fmt =
        &g_array_index (priv->pixmap_formats, GstVaapiPixmapFormatX11, n);

    pix_fmt->depth = pix_fmts[i].depth;
    pix_fmt->bpp = pix_fmts[i].bits_per_pixel;
    pix_fmt->format = pix_fmt_to_video_format (pix_fmt->depth, pix_fmt->bpp);
    if (pix_fmt->format != GST_VIDEO_FORMAT_UNKNOWN)
      n++;
  }
  priv->pixmap_formats->len = n;
  XFree (pix_fmts);
  return TRUE;
}

/* Determine the GstVideoFormat based on a supported Pixmap depth */
GstVideoFormat
gst_vaapi_display_x11_get_pixmap_format (GstVaapiDisplayX11 * display,
    guint depth)
{
  if (ensure_pix_fmts (display)) {
    GstVaapiDisplayX11Private *const priv =
        GST_VAAPI_DISPLAY_X11_PRIVATE (display);
    guint i;

    for (i = 0; i < priv->pixmap_formats->len; i++) {
      GstVaapiPixmapFormatX11 *const pix_fmt =
          &g_array_index (priv->pixmap_formats, GstVaapiPixmapFormatX11, i);
      if (pix_fmt->depth == depth)
        return pix_fmt->format;
    }
  }
  return GST_VIDEO_FORMAT_UNKNOWN;
}

/* Determine the Pixmap depth based on a GstVideoFormat */
guint
gst_vaapi_display_x11_get_pixmap_depth (GstVaapiDisplayX11 * display,
    GstVideoFormat format)
{
  if (ensure_pix_fmts (display)) {
    GstVaapiDisplayX11Private *const priv =
        GST_VAAPI_DISPLAY_X11_PRIVATE (display);
    guint i;

    for (i = 0; i < priv->pixmap_formats->len; i++) {
      GstVaapiPixmapFormatX11 *const pix_fmt =
          &g_array_index (priv->pixmap_formats, GstVaapiPixmapFormatX11, i);
      if (pix_fmt->format == format)
        return pix_fmt->depth;
    }
  }
  return 0;
}
