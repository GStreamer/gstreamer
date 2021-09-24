/*
 *  gstvaapidisplay_drm.c - VA/DRM display abstraction
 *
 *  Copyright (C) 2012-2013 Intel Corporation
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
 * SECTION:gstvaapidisplay_drm
 * @short_description: VA/DRM display abstraction
 */

#define _GNU_SOURCE
#include "sysdeps.h"
#include <unistd.h>
#include <fcntl.h>
#include <libudev.h>
#include <xf86drm.h>
#include <va/va_drm.h>
#include "gstvaapiutils.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapidisplay_drm.h"
#include "gstvaapidisplay_drm_priv.h"
#include "gstvaapiwindow_drm.h"

#define DEBUG_VAAPI_DISPLAY 1
#include "gstvaapidebug.h"

#ifndef MAXPATHLEN
#if defined(PATH_MAX)
#define MAXPATHLEN PATH_MAX
#elif defined(_PC_PATH_MAX)
#define MAXPATHLEN sysconf(_PC_PATH_MAX)
#else
#define MAXPATHLEN 2048
#endif
#endif

G_DEFINE_TYPE_WITH_PRIVATE (GstVaapiDisplayDRM, gst_vaapi_display_drm,
    GST_TYPE_VAAPI_DISPLAY);

typedef enum
{
  DRM_DEVICE_LEGACY = 1,
  DRM_DEVICE_RENDERNODES,
} DRMDeviceType;

static DRMDeviceType g_drm_device_type;
static GMutex g_drm_device_type_lock;
static const gchar *allowed_subsystems[] = { "pci", "platform", NULL };

static gboolean
supports_vaapi (int fd)
{
  gboolean ret;
  VADisplay va_dpy;

  va_dpy = vaGetDisplayDRM (fd);
  if (!va_dpy)
    return FALSE;

  ret = vaapi_initialize (va_dpy);
  vaTerminate (va_dpy);
  return ret;
}

/* Get default device path. Actually, the first match in the DRM subsystem */
static const gchar *
get_default_device_path (GstVaapiDisplay * display)
{
  GstVaapiDisplayDRMPrivate *const priv =
      GST_VAAPI_DISPLAY_DRM_PRIVATE (display);
  const gchar *syspath, *devpath;
  struct udev *udev = NULL;
  struct udev_device *device, *parent;
  struct udev_enumerate *e = NULL;
  struct udev_list_entry *l;
  gint i;
  int fd;

  if (!priv->device_path_default) {
    udev = udev_new ();
    if (!udev)
      goto end;

    e = udev_enumerate_new (udev);
    if (!e)
      goto end;

    udev_enumerate_add_match_subsystem (e, "drm");
    switch (g_drm_device_type) {
      case DRM_DEVICE_LEGACY:
        udev_enumerate_add_match_sysname (e, "card[0-9]*");
        break;
      case DRM_DEVICE_RENDERNODES:
        udev_enumerate_add_match_sysname (e, "renderD[0-9]*");
        break;
      default:
        GST_ERROR ("unknown drm device type (%d)", g_drm_device_type);
        goto end;
    }
    udev_enumerate_scan_devices (e);
    udev_list_entry_foreach (l, udev_enumerate_get_list_entry (e)) {
      syspath = udev_list_entry_get_name (l);
      device = udev_device_new_from_syspath (udev, syspath);
      parent = udev_device_get_parent (device);

      for (i = 0; allowed_subsystems[i] != NULL; i++)
        if (g_strcmp0 (udev_device_get_subsystem (parent),
                allowed_subsystems[i]) == 0)
          break;

      if (allowed_subsystems[i] == NULL) {
        udev_device_unref (device);
        continue;
      }

      devpath = udev_device_get_devnode (device);
      fd = open (devpath, O_RDWR | O_CLOEXEC);
      if (fd < 0) {
        udev_device_unref (device);
        continue;
      }

      if (supports_vaapi (fd))
        priv->device_path_default = g_strdup (devpath);
      close (fd);
      udev_device_unref (device);
      if (priv->device_path_default)
        break;
    }

  end:
    if (e)
      udev_enumerate_unref (e);
    if (udev)
      udev_unref (udev);
  }
  return priv->device_path_default;
}

/* Reconstruct a device path without our prefix */
static const gchar *
get_device_path (GstVaapiDisplay * display)
{
  GstVaapiDisplayDRMPrivate *const priv =
      GST_VAAPI_DISPLAY_DRM_PRIVATE (display);
  const gchar *device_path = priv->device_path;

  if (!device_path || *device_path == '\0')
    return NULL;
  return device_path;
}

/* Mangle device path with our prefix */
static gboolean
set_device_path (GstVaapiDisplay * display, const gchar * device_path)
{
  GstVaapiDisplayDRMPrivate *const priv =
      GST_VAAPI_DISPLAY_DRM_PRIVATE (display);

  g_free (priv->device_path);
  priv->device_path = NULL;

  if (!device_path) {
    device_path = get_default_device_path (display);
    if (!device_path)
      return FALSE;
  }
  priv->device_path = g_strdup (device_path);
  return priv->device_path != NULL;
}

/* Set device path from file descriptor */
static gboolean
set_device_path_from_fd (GstVaapiDisplay * display, gint drm_device)
{
  GstVaapiDisplayDRMPrivate *const priv =
      GST_VAAPI_DISPLAY_DRM_PRIVATE (display);
  gboolean success = FALSE;
  gchar fd_name[MAXPATHLEN];
  GError *error = NULL;

  g_free (priv->device_path);
  priv->device_path = NULL;

  if (drm_device < 0)
    goto end;

  sprintf (fd_name, "/proc/%d/fd/%d", getpid (), drm_device);
  priv->device_path = g_file_read_link (fd_name, &error);

  if (error) {
    g_error_free (error);
    goto end;
  }

  if (g_str_has_prefix (priv->device_path, "/dev/dri/card") ||
      g_str_has_prefix (priv->device_path, "/dev/dri/renderD"))
    success = TRUE;
  else {
    g_free (priv->device_path);
    priv->device_path = NULL;
  }

end:
  return success;
}

static gboolean
gst_vaapi_display_drm_bind_display (GstVaapiDisplay * display,
    gpointer native_display)
{
  GstVaapiDisplayDRMPrivate *const priv =
      GST_VAAPI_DISPLAY_DRM_PRIVATE (display);

  priv->drm_device = GPOINTER_TO_INT (native_display);
  priv->use_foreign_display = TRUE;

  if (!set_device_path_from_fd (display, priv->drm_device))
    return FALSE;
  return TRUE;
}

static gboolean
gst_vaapi_display_drm_open_display (GstVaapiDisplay * display,
    const gchar * name)
{
  GstVaapiDisplayDRMPrivate *const priv =
      GST_VAAPI_DISPLAY_DRM_PRIVATE (display);

  if (!set_device_path (display, name))
    return FALSE;

  priv->drm_device = open (get_device_path (display), O_RDWR | O_CLOEXEC);
  if (priv->drm_device < 0)
    return FALSE;
  priv->use_foreign_display = FALSE;

  return TRUE;
}

static void
gst_vaapi_display_drm_close_display (GstVaapiDisplay * display)
{
  GstVaapiDisplayDRMPrivate *const priv =
      GST_VAAPI_DISPLAY_DRM_PRIVATE (display);

  if (priv->drm_device >= 0) {
    if (!priv->use_foreign_display)
      close (priv->drm_device);
    priv->drm_device = -1;
  }

  g_clear_pointer (&priv->device_path, g_free);
  g_clear_pointer (&priv->device_path_default, g_free);
}

static gboolean
gst_vaapi_display_drm_get_display_info (GstVaapiDisplay * display,
    GstVaapiDisplayInfo * info)
{
  GstVaapiDisplayDRMPrivate *const priv =
      GST_VAAPI_DISPLAY_DRM_PRIVATE (display);

  info->native_display = GINT_TO_POINTER (priv->drm_device);
  info->display_name = priv->device_path;
  if (!info->va_display) {
    info->va_display = vaGetDisplayDRM (priv->drm_device);
    if (!info->va_display)
      return FALSE;
  }
  return TRUE;
}

static GstVaapiWindow *
gst_vaapi_display_drm_create_window (GstVaapiDisplay * display, GstVaapiID id,
    guint width, guint height)
{
  return id != GST_VAAPI_ID_INVALID ?
      NULL : gst_vaapi_window_drm_new (display, width, height);
}

static void
gst_vaapi_display_drm_init (GstVaapiDisplayDRM * display)
{
  GstVaapiDisplayDRMPrivate *const priv =
      gst_vaapi_display_drm_get_instance_private (display);

  display->priv = priv;
  priv->drm_device = -1;
}

static void
gst_vaapi_display_drm_class_init (GstVaapiDisplayDRMClass * klass)
{
  GstVaapiDisplayClass *const dpy_class = GST_VAAPI_DISPLAY_CLASS (klass);

  dpy_class->display_type = GST_VAAPI_DISPLAY_TYPE_DRM;
  dpy_class->bind_display = gst_vaapi_display_drm_bind_display;
  dpy_class->open_display = gst_vaapi_display_drm_open_display;
  dpy_class->close_display = gst_vaapi_display_drm_close_display;
  dpy_class->get_display = gst_vaapi_display_drm_get_display_info;
  dpy_class->create_window = gst_vaapi_display_drm_create_window;
}

/**
 * gst_vaapi_display_drm_new:
 * @device_path: the DRM device path
 *
 * Opens an DRM file descriptor using @device_path and returns a newly
 * allocated #GstVaapiDisplay object. The DRM display will be cloed
 * when the reference count of the object reaches zero.
 *
 * If @device_path is NULL, the DRM device path will be automatically
 * determined as the first positive match in the list of available DRM
 * devices.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_drm_new (const gchar * device_path)
{
  GstVaapiDisplay *display;
  guint types[3], i, num_types = 0;
  gpointer device_paths[3];

  g_mutex_lock (&g_drm_device_type_lock);
  if (device_path) {
    device_paths[num_types] = (gpointer) device_path;
    types[num_types++] = 0;
  } else if (g_drm_device_type) {
    device_paths[num_types] = (gpointer) device_path;
    types[num_types++] = g_drm_device_type;
  } else {
    const gchar *user_choice = g_getenv ("GST_VAAPI_DRM_DEVICE");

    if (user_choice) {
      device_paths[num_types] = (gpointer) user_choice;
      types[num_types++] = 0;
    } else {
      device_paths[num_types] = (gpointer) device_path;
      types[num_types++] = DRM_DEVICE_RENDERNODES;
      device_paths[num_types] = (gpointer) device_path;
      types[num_types++] = DRM_DEVICE_LEGACY;
    }
  }

  for (i = 0; i < num_types; i++) {
    g_drm_device_type = types[i];
    display = g_object_new (GST_TYPE_VAAPI_DISPLAY_DRM, NULL);
    display = gst_vaapi_display_config (display,
        GST_VAAPI_DISPLAY_INIT_FROM_DISPLAY_NAME, device_paths[i]);
    if (display || device_path)
      break;
  }
  g_mutex_unlock (&g_drm_device_type_lock);
  return display;
}

/**
 * gst_vaapi_display_drm_new_with_device:
 * @device: an open DRM device (file descriptor)
 *
 * Creates a #GstVaapiDisplay based on the open DRM @device. The
 * caller still owns the device file descriptor and must call close()
 * when all #GstVaapiDisplay references are released. Doing so too
 * early can yield undefined behaviour.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_drm_new_with_device (gint device)
{
  GstVaapiDisplay *display;

  g_return_val_if_fail (device >= 0, NULL);

  display = g_object_new (GST_TYPE_VAAPI_DISPLAY_DRM, NULL);
  return gst_vaapi_display_config (display,
      GST_VAAPI_DISPLAY_INIT_FROM_NATIVE_DISPLAY, GINT_TO_POINTER (device));
}

/**
 * gst_vaapi_display_drm_new_with_va_display:
 * @va_display: a VADisplay #va_display
 * @fd: an open DRM device (file descriptor) #fd
 *
 * Creates a #GstVaapiDisplay based on the VADisplay @va_display and
 * the open DRM device @fd.
 * The caller still owns the device file descriptor and must call close()
 * when all #GstVaapiDisplay references are released.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */

GstVaapiDisplay *
gst_vaapi_display_drm_new_with_va_display (VADisplay va_display, gint fd)
{
  GstVaapiDisplay *display;
  GstVaapiDisplayInfo info = {
    .va_display = va_display,
    .native_display = GINT_TO_POINTER (fd),
  };

  g_return_val_if_fail (fd >= 0, NULL);

  display = g_object_new (GST_TYPE_VAAPI_DISPLAY_DRM, NULL);
  if (!gst_vaapi_display_config (display,
          GST_VAAPI_DISPLAY_INIT_FROM_VA_DISPLAY, &info)) {
    gst_object_unref (display);
    return NULL;
  }

  return display;
}

/**
 * gst_vaapi_display_drm_get_device:
 * @display: a #GstVaapiDisplayDRM
 *
 * Returns the underlying DRM device file descriptor that was created
 * by gst_vaapi_display_drm_new() or that was bound from
 * gst_vaapi_display_drm_new_with_device().
 *
 * Return value: the DRM file descriptor attached to @display
 */
gint
gst_vaapi_display_drm_get_device (GstVaapiDisplayDRM * display)
{
  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_DRM (display), -1);

  return GST_VAAPI_DISPLAY_DRM_DEVICE (display);
}

/**
 * gst_vaapi_display_drm_get_device_path:
 * @display: a #GstVaapiDisplayDRM
 *
 * Returns the underlying DRM device path name was created by
 * gst_vaapi_display_drm_new() or that was bound from
 * gst_vaapi_display_drm_new_with_device().
 *
 * Note: the #GstVaapiDisplayDRM object owns the resulting string, so
 * it shall not be deallocated.
 *
 * Return value: the DRM device path name attached to @display
 */
const gchar *
gst_vaapi_display_drm_get_device_path (GstVaapiDisplayDRM * display)
{
  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_DRM (display), NULL);

  return get_device_path (GST_VAAPI_DISPLAY_CAST (display));
}
