/*
 *  gstvaapidisplay_drm.c - VA/DRM display abstraction
 *
 *  Copyright (C) 2012 Intel Corporation
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

#include "sysdeps.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libudev.h>
#include <xf86drm.h>
#include <va/va_drm.h>
#include "gstvaapiutils.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapidisplay_drm.h"
#include "gstvaapidisplay_drm_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiDisplayDRM,
              gst_vaapi_display_drm,
              GST_VAAPI_TYPE_DISPLAY);

enum {
    PROP_0,

    PROP_DEVICE_PATH,
    PROP_DRM_DEVICE
};

#define NAME_PREFIX "DRM:"
#define NAME_PREFIX_LENGTH 4

static inline gboolean
is_device_path(const gchar *device_path)
{
    return strncmp(device_path, NAME_PREFIX, NAME_PREFIX_LENGTH) == 0;
}

static gboolean
compare_device_path(gconstpointer a, gconstpointer b, gpointer user_data)
{
    const gchar *cached_name = a;
    const gchar *tested_name = b;

    if (!cached_name || !is_device_path(cached_name))
        return FALSE;
    g_return_val_if_fail(tested_name && is_device_path(tested_name), FALSE);

    cached_name += NAME_PREFIX_LENGTH;
    tested_name += NAME_PREFIX_LENGTH;
    return strcmp(cached_name, tested_name) == 0;
}

static void
gst_vaapi_display_drm_finalize(GObject *object)
{
    G_OBJECT_CLASS(gst_vaapi_display_drm_parent_class)->finalize(object);
}

/* Get default device path. Actually, the first match in the DRM subsystem */
static const gchar *
get_default_device_path(gpointer ptr)
{
    GstVaapiDisplayDRM * const display = GST_VAAPI_DISPLAY_DRM(ptr);
    GstVaapiDisplayDRMPrivate * const priv = display->priv;
    const gchar *syspath, *devpath;
    struct udev *udev = NULL;
    struct udev_device *device, *parent;
    struct udev_enumerate *e = NULL;
    struct udev_list_entry *l;
    int fd;

    if (!priv->device_path_default) {
        udev = udev_new();
        if (!udev)
            goto end;

        e = udev_enumerate_new(udev);
        if (!e)
            goto end;

        udev_enumerate_add_match_subsystem(e, "drm");
        udev_enumerate_scan_devices(e);
        udev_list_entry_foreach(l, udev_enumerate_get_list_entry(e)) {
            syspath = udev_list_entry_get_name(l);
            device  = udev_device_new_from_syspath(udev, syspath);
            parent  = udev_device_get_parent(device);
            if (strcmp(udev_device_get_subsystem(parent), "pci") != 0) {
                udev_device_unref(device);
                continue;
            }

            devpath = udev_device_get_devnode(device);
            fd = open(devpath, O_RDWR|O_CLOEXEC);
            if (fd < 0) {
                udev_device_unref(device);
                continue;
            }

            priv->device_path_default = g_strdup(devpath);
            close(fd);
            udev_device_unref(device);
            break;
        }

    end:
        if (e)
            udev_enumerate_unref(e);
        if (udev)
            udev_unref(udev);
    }
    return priv->device_path_default;
}

/* Reconstruct a device path without our prefix */
static const gchar *
get_device_path(gpointer ptr)
{
    GstVaapiDisplayDRM * const display = GST_VAAPI_DISPLAY_DRM(ptr);
    const gchar *device_path = display->priv->device_path;

    if (!device_path)
        return NULL;

    g_return_val_if_fail(is_device_path(device_path), NULL);

    device_path += NAME_PREFIX_LENGTH;
    if (*device_path == '\0')
        return NULL;
    return device_path;
}

/* Mangle device path with our prefix */
static void
set_device_path(GstVaapiDisplayDRM *display, const gchar *device_path)
{
    GstVaapiDisplayDRMPrivate * const priv = display->priv;

    g_free(priv->device_path);
    priv->device_path = NULL;

    if (!device_path) {
        device_path = get_default_device_path(display);
        if (!device_path)
            return;
    }
    priv->device_path = g_strdup_printf("%s%s", NAME_PREFIX, device_path);
}

/* Set device path from file descriptor */
static void
set_device_path_from_fd(GstVaapiDisplayDRM *display, gint drm_device)
{
    GstVaapiDisplayDRMPrivate * const priv = display->priv;
    const gchar *busid, *path, *str;
    gsize busid_length, path_length;
    struct udev *udev = NULL;
    struct udev_device *device;
    struct udev_enumerate *e = NULL;
    struct udev_list_entry *l;

    g_free(priv->device_path);
    priv->device_path = NULL;

    if (drm_device < 0)
        return;

    busid = drmGetBusid(drm_device);
    if (!busid)
        return;
    if (strncmp(busid, "pci:", 4) != 0)
        return;
    busid += 4;
    busid_length = strlen(busid);

    udev = udev_new();
    if (!udev)
        goto end;

    e = udev_enumerate_new(udev);
    if (!e)
        goto end;

    udev_enumerate_add_match_subsystem(e, "drm");
    udev_enumerate_scan_devices(e);
    udev_list_entry_foreach(l, udev_enumerate_get_list_entry(e)) {
        path = udev_list_entry_get_name(l);
        str  = strstr(path, busid);
        if (!str || str <= path || str[-1] != '/')
            continue;

        path_length = strlen(path);
        if (str + busid_length >= path + path_length)
            continue;
        if (strncmp(&str[busid_length], "/drm/card", 9) != 0)
            continue;

        device = udev_device_new_from_syspath(udev, path);
        if (!device)
            continue;

        path = udev_device_get_devnode(device);
        priv->device_path = g_strdup_printf("%s%s", NAME_PREFIX, path);
        udev_device_unref(device);
        break;
    }

end:
    if (e)
        udev_enumerate_unref(e);
    if (udev)
        udev_unref(udev);
}

static void
gst_vaapi_display_drm_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiDisplayDRM * const display = GST_VAAPI_DISPLAY_DRM(object);

    switch (prop_id) {
    case PROP_DEVICE_PATH:
        set_device_path(display, g_value_get_string(value));
        break;
    case PROP_DRM_DEVICE:
        display->priv->drm_device = g_value_get_int(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_drm_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiDisplayDRM * const display = GST_VAAPI_DISPLAY_DRM(object);

    switch (prop_id) {
    case PROP_DEVICE_PATH:
        g_value_set_string(value, get_device_path(display));
        break;
    case PROP_DRM_DEVICE:
        g_value_set_int(value, display->priv->drm_device);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_drm_constructed(GObject *object)
{
    GstVaapiDisplayDRM * const display = GST_VAAPI_DISPLAY_DRM(object);
    GstVaapiDisplayDRMPrivate * const priv = display->priv;
    GstVaapiDisplayCache * const cache = gst_vaapi_display_get_cache();
    const GstVaapiDisplayInfo *info;
    GObjectClass *parent_class;

    priv->create_display = priv->drm_device < 0;

    /* Don't create DRM display if there is one in the cache already */
    if (priv->create_display) {
        info = gst_vaapi_display_cache_lookup_by_name(
            cache,
            priv->device_path,
            compare_device_path, NULL
        );
        if (info) {
            priv->drm_device     = GPOINTER_TO_INT(info->native_display);
            priv->create_display = FALSE;
        }
    }

    /* Reset device-path if the user provided his own DRM display */
    if (!priv->create_display)
        set_device_path_from_fd(display, priv->drm_device);

    parent_class = G_OBJECT_CLASS(gst_vaapi_display_drm_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static gboolean
gst_vaapi_display_drm_open_display(GstVaapiDisplay *display)
{
    GstVaapiDisplayDRMPrivate * const priv =
        GST_VAAPI_DISPLAY_DRM(display)->priv;

    if (priv->create_display) {
        const gchar *device_path = get_device_path(display);
        if (!device_path)
            return FALSE;
        priv->drm_device = open(device_path, O_RDWR|O_CLOEXEC);
        if (priv->drm_device < 0)
            return FALSE;
    }
    if (priv->drm_device < 0)
        return FALSE;
    return TRUE;
}

static void
gst_vaapi_display_drm_close_display(GstVaapiDisplay *display)
{
    GstVaapiDisplayDRMPrivate * const priv =
        GST_VAAPI_DISPLAY_DRM(display)->priv;

    if (priv->drm_device >= 0) {
        if (priv->create_display)
            close(priv->drm_device);
        priv->drm_device = -1;
    }

    if (priv->device_path) {
        g_free(priv->device_path);
        priv->device_path = NULL;
    }

    if (priv->device_path_default) {
        g_free(priv->device_path_default);
        priv->device_path_default = NULL;
    }
}

static gboolean
gst_vaapi_display_drm_get_display_info(
    GstVaapiDisplay     *display,
    GstVaapiDisplayInfo *info
)
{
    GstVaapiDisplayDRMPrivate * const priv =
        GST_VAAPI_DISPLAY_DRM(display)->priv;
    GstVaapiDisplayCache *cache;
    const GstVaapiDisplayInfo *cached_info;

    /* Return any cached info even if child has its own VA display */
    cache = gst_vaapi_display_get_cache();
    if (!cache)
        return FALSE;
    cached_info = gst_vaapi_display_cache_lookup_by_native_display(
        cache, GINT_TO_POINTER(priv->drm_device));
    if (cached_info) {
        *info = *cached_info;
        return TRUE;
    }

    /* Otherwise, create VA display if there is none already */
    info->native_display = GINT_TO_POINTER(priv->drm_device);
    info->display_name   = priv->device_path;
    if (!info->va_display) {
        info->va_display = vaGetDisplayDRM(priv->drm_device);
        if (!info->va_display)
            return FALSE;
        info->display_type = GST_VAAPI_DISPLAY_TYPE_DRM;
    }
    return TRUE;
}

static void
gst_vaapi_display_drm_class_init(GstVaapiDisplayDRMClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDisplayClass * const dpy_class = GST_VAAPI_DISPLAY_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDisplayDRMPrivate));

    object_class->finalize      = gst_vaapi_display_drm_finalize;
    object_class->set_property  = gst_vaapi_display_drm_set_property;
    object_class->get_property  = gst_vaapi_display_drm_get_property;
    object_class->constructed   = gst_vaapi_display_drm_constructed;

    dpy_class->open_display     = gst_vaapi_display_drm_open_display;
    dpy_class->close_display    = gst_vaapi_display_drm_close_display;
    dpy_class->get_display      = gst_vaapi_display_drm_get_display_info;

    /**
     * GstVaapiDisplayDRM:drm-device:
     *
     * The DRM device (file descriptor).
     */
    g_object_class_install_property
        (object_class,
         PROP_DRM_DEVICE,
         g_param_spec_int("drm-device",
                          "DRM device",
                          "DRM device",
                          -1, G_MAXINT32, -1,
                          G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    /**
     * GstVaapiDisplayDRM:device-path:
     *
     * The DRM device path.
     */
    g_object_class_install_property
        (object_class,
         PROP_DEVICE_PATH,
         g_param_spec_string("device-path",
                             "DRM device path",
                             "DRM device path",
                             NULL,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_display_drm_init(GstVaapiDisplayDRM *display)
{
    GstVaapiDisplayDRMPrivate * const priv =
        GST_VAAPI_DISPLAY_DRM_GET_PRIVATE(display);

    display->priv               = priv;
    priv->device_path_default   = NULL;
    priv->device_path           = NULL;
    priv->drm_device            = -1;
    priv->create_display        = TRUE;
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
gst_vaapi_display_drm_new(const gchar *device_path)
{
    return g_object_new(GST_VAAPI_TYPE_DISPLAY_DRM,
                        "device-path", device_path,
                        NULL);
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
gst_vaapi_display_drm_new_with_device(gint device)
{
    g_return_val_if_fail(device >= 0, NULL);

    return g_object_new(GST_VAAPI_TYPE_DISPLAY_DRM,
                        "drm-device", device,
                        NULL);
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
gst_vaapi_display_drm_get_device(GstVaapiDisplayDRM *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY_DRM(display), -1);

    return display->priv->drm_device;
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
gst_vaapi_display_drm_get_device_path(GstVaapiDisplayDRM *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY_DRM(display), NULL);

    return display->priv->device_path;
}
