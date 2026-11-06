/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

/**
 * SECTION:gstvadisplay
 * @title: GstVaDisplay
 * @short_description: Generic VADisplay wrapper.
 * @sources:
 * - gstva.h
 * - gstvadisplay.h
 *
 * It is a generic wrapper for VADisplay. To create new instances
 * subclasses are required, depending on the display type to use
 * (v.gr. DRM, X11, Wayland, etc.).
 *
 * The purpose of this class is to be shared among pipelines via
 * #GstContext so all the VA processing elements will use the same
 * display entry. Application developers can create their own
 * subclass, based on their display, and shared it via the synced bus
 * message for the application.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvadisplay.h"

#include <stdio.h>              /* sscanf */

#ifndef G_OS_WIN32
#include <sys/types.h>
#endif

#include "gstvaallocator_priv.h"
#include "gstvadisplay_priv.h"
#include "gstvavideoformat.h"
#include "vasurfaceimage.h"

#ifndef DRM_FORMAT_INVALID
#define DRM_FORMAT_INVALID    0
#endif

GST_DEBUG_CATEGORY (gst_va_display_debug);
#define GST_CAT_DEFAULT gst_va_display_debug

typedef struct _GstVaDisplayPrivate GstVaDisplayPrivate;
struct _GstVaDisplayPrivate
{
  VADisplay display;

  gboolean foreign;
  gboolean init;
  GstVaImplementation impl;
  gchar *vendor_desc;

  guint driver_major;
  guint driver_minor;

  /* GstMemory (weak ref) -> GstVaBufferSurface (ref) */
  GMutex surfaces_lock;
  GHashTable *surfaces;
  /* GstMemory (weak ref) -> GstVaBufferSurface (ref) */
  GMutex aux_surfaces_lock;
  GHashTable *aux_surfaces;
};

#define gst_va_display_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVaDisplay, gst_va_display, GST_TYPE_OBJECT,
    G_ADD_PRIVATE (GstVaDisplay);
    GST_DEBUG_CATEGORY_INIT (gst_va_display_debug, "vadisplay", 0,
        "VA Display"));
enum
{
  PROP_VA_DISPLAY = 1,
  PROP_DESC,
  N_PROPERTIES
};

static GParamSpec *g_properties[N_PROPERTIES];

#define GET_PRIV(obj) gst_va_display_get_instance_private (GST_VA_DISPLAY (obj))

static GstVaImplementation
_get_implementation (const char *vendor)
{
  if (g_str_has_prefix (vendor, "Mesa Gallium driver"))
    return GST_VA_IMPLEMENTATION_MESA_GALLIUM;
  else if (g_str_has_prefix (vendor, "Intel i965 driver"))
    return GST_VA_IMPLEMENTATION_INTEL_I965;
  else if (g_str_has_prefix (vendor, "Intel iHD driver"))
    return GST_VA_IMPLEMENTATION_INTEL_IHD;

  return GST_VA_IMPLEMENTATION_OTHER;
}

static char *
_get_desc (const char *vendor, GstVaImplementation impl)
{
  const char *end, *start;
  char desc[1024];
  size_t size;

  if (impl == GST_VA_IMPLEMENTATION_OTHER)
    return g_strdup (vendor);

  start = strstr (vendor, "for ");
  if (!start)
    return g_strdup (vendor);
  start += 4;

  switch (impl) {
    case GST_VA_IMPLEMENTATION_MESA_GALLIUM:
      end = strchr (start, '(');
      break;
    default:
      end = strstr (start, "- ");
      break;
  }

  if (!end)
    return g_strdup (vendor);
  end -= 1;

  size = MIN (1024, end - start);
  memcpy (desc, start, size);
  desc[size] = '\0';
  return g_strdup (desc);
}

static gboolean
_get_driver_version (const char *vendor, GstVaImplementation impl,
    guint * major, guint * minor)
{
  guint maj, min;

  if (!vendor)
    return FALSE;

  switch (impl) {
    case GST_VA_IMPLEMENTATION_MESA_GALLIUM:
      if (sscanf (vendor, "Mesa Gallium driver %d.%d.", &maj, &min) == 2) {
        *major = maj;
        *minor = min;
        return TRUE;
      }
      break;
    case GST_VA_IMPLEMENTATION_INTEL_IHD:
    case GST_VA_IMPLEMENTATION_INTEL_I965:{
      const char *end = strstr (vendor, " - ");
      if (end && sscanf (end, " - %d.%d.", &maj, &min) == 2) {
        *major = maj;
        *minor = min;
        return TRUE;
      }
      break;
    }
    default:
      break;
  }

  return FALSE;
}

static gboolean
_gst_va_display_filter_driver (GstVaDisplay * self, gpointer foreign_display)
{
  GstVaDisplayPrivate *priv = GET_PRIV (self);
  VADisplay dpy;
  const char *vendor;
  GstVaImplementation impl;
  guint major, minor;

  g_assert ((foreign_display != NULL) ^ (priv->display != NULL));
  dpy = foreign_display ? foreign_display : priv->display;

  vendor = vaQueryVendorString (dpy);
  GST_INFO ("VA-API driver vendor: %s", vendor);

  impl = _get_implementation (vendor);

  if (foreign_display) {
    priv->display = foreign_display;
    priv->foreign = TRUE;
  } else {
    if (g_getenv ("GST_VA_ALL_DRIVERS") == NULL
        && impl == GST_VA_IMPLEMENTATION_OTHER) {
      GST_WARNING_OBJECT (self, "Unsupported driver: %s", vendor);
      return FALSE;
    }
  }
  priv->impl = impl;
  priv->vendor_desc = _get_desc (vendor, priv->impl);
  if (_get_driver_version (vendor, priv->impl, &major, &minor)) {
    priv->driver_major = major;
    priv->driver_minor = minor;
  }

  return TRUE;
}

static void
gst_va_display_set_display (GstVaDisplay * self, gpointer display)
{
  GstVaDisplayPrivate *priv = GET_PRIV (self);

  if (!display)
    return;

  if (vaDisplayIsValid (display) == 0) {
    GST_WARNING_OBJECT (self,
        "User's VA display is invalid. An internal one will be tried.");
    return;
  }

  /* assume driver is already initialized */
  priv->init = TRUE;

  /* assumed that user knows what's doing so all drivers are allowed */
  _gst_va_display_filter_driver (self, display);
}

static void
gst_va_display_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaDisplay *self = GST_VA_DISPLAY (object);

  switch (prop_id) {
    case PROP_VA_DISPLAY:{
      gpointer display = g_value_get_pointer (value);
      gst_va_display_set_display (self, display);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_va_display_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVaDisplayPrivate *priv = GET_PRIV (object);

  switch (prop_id) {
    case PROP_VA_DISPLAY:
      g_value_set_pointer (value, priv->display);
      break;
    case PROP_DESC:
      g_value_set_string (value, priv->vendor_desc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_va_display_constructed (GObject * object)
{
  GstVaDisplay *self = GST_VA_DISPLAY (object);
  GstVaDisplayPrivate *priv = GET_PRIV (object);
  GstVaDisplayClass *klass = GST_VA_DISPLAY_GET_CLASS (object);

  if (!priv->display && klass->create_va_display)
    priv->display = klass->create_va_display (self);

  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
gst_va_display_dispose (GObject * object)
{
  GstVaDisplayPrivate *priv = GET_PRIV (object);

  if (priv->display && !priv->foreign)
    vaTerminate (priv->display);
  priv->display = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_va_display_finalize (GObject * object)
{
  GstVaDisplayPrivate *priv = GET_PRIV (object);

  g_free (priv->vendor_desc);

  /* every surface in the table holds a reference on the display */
  g_assert (g_hash_table_size (priv->surfaces) == 0);
  g_hash_table_unref (priv->surfaces);
  g_assert (g_hash_table_size (priv->aux_surfaces) == 0);
  g_hash_table_unref (priv->aux_surfaces);
  g_mutex_clear (&priv->surfaces_lock);
  g_mutex_clear (&priv->aux_surfaces_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_va_display_class_init (GstVaDisplayClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_va_display_set_property;
  gobject_class->get_property = gst_va_display_get_property;
  gobject_class->constructed = gst_va_display_constructed;
  gobject_class->dispose = gst_va_display_dispose;
  gobject_class->finalize = gst_va_display_finalize;

  g_properties[PROP_VA_DISPLAY] =
      g_param_spec_pointer ("va-display", "VADisplay", "VA Display handler",
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_properties[PROP_DESC] =
      g_param_spec_string ("description", "Description",
      "Vendor specific VA implementation description", NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, N_PROPERTIES, g_properties);
}

static void
gst_va_display_init (GstVaDisplay * self)
{
  GstVaDisplayPrivate *priv = GET_PRIV (self);

  priv->impl = GST_VA_IMPLEMENTATION_INVALID;

  g_mutex_init (&priv->surfaces_lock);
  g_mutex_init (&priv->aux_surfaces_lock);
  priv->surfaces =
      g_hash_table_new_full (NULL, NULL, NULL, gst_va_buffer_surface_unref);
  priv->aux_surfaces =
      g_hash_table_new_full (NULL, NULL, NULL, gst_va_buffer_surface_unref);
}

#ifndef GST_DISABLE_GST_DEBUG
static gchar *
_strip_msg (const char *message)
{
  gchar *msg = g_strdup (message);
  if (!msg)
    return NULL;
  return g_strstrip (msg);
}

static void
_va_warning (gpointer object, const char *message)
{
  GstVaDisplay *self = GST_VA_DISPLAY (object);
  gchar *msg;

  if ((msg = _strip_msg (message))) {
    GST_WARNING_OBJECT (self, "VA error: %s", msg);
    g_free (msg);
  }
}

static void
_va_info (gpointer object, const char *message)
{
  GstVaDisplay *self = GST_VA_DISPLAY (object);
  gchar *msg;

  if ((msg = _strip_msg (message))) {
    GST_INFO_OBJECT (self, "VA info: %s", msg);
    g_free (msg);
  }
}
#endif

/**
 * gst_va_display_initialize:
 * @self: a #GstVaDisplay
 *
 * If the display is set by the user (foreign) it is assumed that the
 * driver is already initialized, thus this function is noop.
 *
 * If the display is opened internally, this function will initialize
 * the driver and it will set driver's message callbacks.
 *
 * NOTE: this function is supposed to be private, only used by
 * GstVaDisplay descendants.
 *
 * Returns: %TRUE if the VA driver can be initialized; %FALSE
 *     otherwise
 *
 * Since: 1.20
 **/
gboolean
gst_va_display_initialize (GstVaDisplay * self)
{
  GstVaDisplayPrivate *priv;
  VAStatus status;
  int major_version = -1, minor_version = -1;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), FALSE);

  priv = GET_PRIV (self);

  if (priv->init)
    return TRUE;

  if (!priv->display)
    return FALSE;

#ifndef GST_DISABLE_GST_DEBUG
  vaSetErrorCallback (priv->display, _va_warning, self);
  vaSetInfoCallback (priv->display, _va_info, self);
#endif

  status = vaInitialize (priv->display, &major_version, &minor_version);
  if (status != VA_STATUS_SUCCESS) {
    GST_WARNING_OBJECT (self, "vaInitialize: %s", vaErrorStr (status));
    return FALSE;
  }

  GST_INFO_OBJECT (self, "VA-API version %d.%d", major_version, minor_version);

  priv->init = TRUE;

  if (!_gst_va_display_filter_driver (self, NULL))
    return FALSE;

  return TRUE;
}

/**
 * gst_va_display_get_va_dpy:
 * @self: a #GstVaDisplay type display.
 *
 * Get the VA display handle of the @self.
 *
 * Returns: the VA display handle.
 *
 * Since: 1.20
 */
gpointer
gst_va_display_get_va_dpy (GstVaDisplay * self)
{
  GstVaDisplayPrivate *priv;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), NULL);
  priv = GET_PRIV (self);

  return priv->display;
}

/**
 * gst_va_display_get_implementation:
 * @self: a #GstVaDisplay type display.
 *
 * Get the the #GstVaImplementation type of @self.
 *
 * Returns: #GstVaImplementation.
 *
 * Since: 1.20
 */
GstVaImplementation
gst_va_display_get_implementation (GstVaDisplay * self)
{
  GstVaDisplayPrivate *priv;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self),
      GST_VA_IMPLEMENTATION_INVALID);

  priv = GET_PRIV (self);
  return priv->impl;
}

/**
 * gst_va_display_check_version:
 * @self: a #GstVaDisplay
 * @major: major version to check
 * @minor: minor version to check
 *
 * Returns: whether driver version is equal or greater than @major.@minor
 *
 * Since: 1.24
 */
gboolean
gst_va_display_check_version (GstVaDisplay * self, guint major, guint minor)
{
  GstVaDisplayPrivate *priv;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), FALSE);

  priv = GET_PRIV (self);

  /* if cannot parse the version, all it's valid */
  if (priv->driver_major == 0 && priv->driver_minor == 0)
    return TRUE;

  if (priv->driver_major < major)
    return FALSE;
  if (priv->driver_major == major && priv->driver_minor < minor)
    return FALSE;

  return TRUE;
}

/*========================= GstVaBufferSurface ===============================*/

static void
_memory_weak_notify (gpointer data, GstMiniObject * obj)
{
  GstVaDisplay *self = data;
  GstVaDisplayPrivate *priv = GET_PRIV (self);
  GstVaBufferSurface *buf = NULL;

  g_mutex_lock (&priv->surfaces_lock);
  g_hash_table_steal_extended (priv->surfaces, obj, NULL, (gpointer *) & buf);
  g_mutex_unlock (&priv->surfaces_lock);

  gst_va_buffer_surface_unref (buf);
}

static void
_memory_aux_weak_notify (gpointer data, GstMiniObject * obj)
{
  GstVaDisplay *self = data;
  GstVaDisplayPrivate *priv = GET_PRIV (self);
  GstVaBufferSurface *buf = NULL;

  g_mutex_lock (&priv->aux_surfaces_lock);
  g_hash_table_steal_extended (priv->aux_surfaces, obj, NULL,
      (gpointer *) & buf);
  g_mutex_unlock (&priv->aux_surfaces_lock);

  gst_va_buffer_surface_unref (buf);
}

static void
_set_buffer_surface_unlocked (GstVaDisplay * self, GstVaBufferSurface * buf)
{
  GstVaDisplayPrivate *priv = GET_PRIV (self);

  for (guint i = 0; i < buf->n_mems; i++) {
    gst_va_buffer_surface_ref (buf);

    /* We could have multiple times the same memory, when planes are on a single
     * dmabuf. Only take one weak ref in that case, the hash table's destroy
     * function will unref buf. */
    if (g_hash_table_insert (priv->surfaces, buf->mems[i], buf)) {
      gst_mini_object_weak_ref (GST_MINI_OBJECT (buf->mems[i]),
          _memory_weak_notify, self);
    }
  }
}

/* Associates @buf to each of its memories in @self, taking a reference
 * on @buf until each memory is finalized. If a memory was already
 * associated with a surface in @self, the previous one is replaced. */
void
gst_va_display_set_buffer_surface (GstVaDisplay * self,
    GstVaBufferSurface * buf)
{
  GstVaDisplayPrivate *priv;

  g_return_if_fail (GST_IS_VA_DISPLAY (self));
  g_return_if_fail (buf != NULL);
  g_return_if_fail (buf->display == self);
  g_return_if_fail (buf->n_mems <= GST_VIDEO_MAX_PLANES);

  priv = GET_PRIV (self);

  g_mutex_lock (&priv->surfaces_lock);
  _set_buffer_surface_unlocked (self, buf);
  g_mutex_unlock (&priv->surfaces_lock);
}

GstVaBufferSurface *
gst_va_display_peek_buffer_surface (GstVaDisplay * self, GstMemory * mem)
{
  GstVaDisplayPrivate *priv;
  GstVaBufferSurface *buf;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), NULL);
  g_return_val_if_fail (mem != NULL, NULL);

  priv = GET_PRIV (self);

  g_mutex_lock (&priv->surfaces_lock);
  buf = g_hash_table_lookup (priv->surfaces, mem);
  g_mutex_unlock (&priv->surfaces_lock);

  return buf;
}

static gboolean
_is_fd_repeated (uintptr_t fds[GST_VIDEO_MAX_PLANES], guint cur, guint * prev)
{
  guint i;

  g_assert (cur <= GST_VIDEO_MAX_PLANES);

  if (cur == 0)
    return FALSE;

  for (i = 0; i < cur; i++) {
    if (fds[i] == fds[cur]) {
      if (prev)
        *prev = i;
      return TRUE;
    }
  }
  return FALSE;
}

/* Associates the auxiliary surface @buf to @mem in @self, taking a
 * reference on @buf until @mem is finalized. If @mem already had an
 * auxiliary surface in @self, the previous one is replaced. */
void
gst_va_display_set_aux_surface (GstVaDisplay * self, GstMemory * mem,
    GstVaBufferSurface * buf)
{
  GstVaDisplayPrivate *priv;

  g_return_if_fail (GST_IS_VA_DISPLAY (self));
  g_return_if_fail (mem != NULL);
  g_return_if_fail (buf != NULL);
  g_return_if_fail (buf->display == self);

  priv = GET_PRIV (self);

  gst_va_buffer_surface_ref (buf);

  g_mutex_lock (&priv->aux_surfaces_lock);
  if (g_hash_table_insert (priv->aux_surfaces, mem, buf)) {
    gst_mini_object_weak_ref (GST_MINI_OBJECT (mem), _memory_aux_weak_notify,
        self);
  }
  g_mutex_unlock (&priv->aux_surfaces_lock);
}

GstVaBufferSurface *
gst_va_display_peek_aux_surface (GstVaDisplay * self, GstMemory * mem)
{
  GstVaDisplayPrivate *priv;
  GstVaBufferSurface *buf;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), NULL);
  g_return_val_if_fail (mem != NULL, NULL);

  priv = GET_PRIV (self);

  g_mutex_lock (&priv->aux_surfaces_lock);
  buf = g_hash_table_lookup (priv->aux_surfaces, mem);
  g_mutex_unlock (&priv->aux_surfaces_lock);

  return buf;
}

/* Must be called with surfaces_lock held, so concurrent imports of
 * the same memories create only one surface in this display. */
/* XXX: use a surface pool to control the created surfaces */
static VASurfaceID
_import_dmabuf_memories_unlocked (GstVaDisplay * self,
    const GstVideoInfoDmaDrm * drm_info, GstMemory * mem[GST_VIDEO_MAX_PLANES],
    uintptr_t fds[GST_VIDEO_MAX_PLANES], gsize offset[GST_VIDEO_MAX_PLANES],
    guint usage_hint)
{
  GstVideoFormat format;
  GstVaBufferSurface *buf;
  const GstVideoInfo *info = &drm_info->vinfo;
  VADRMPRIMESurfaceDescriptor desc = { 0, };
  VASurfaceID surface;
  guint32 fourcc, rt_format, drm_fourcc;
  guint64 drm_modifier;
  guint i, j, prev, n_planes;

  desc.width = GST_VIDEO_INFO_WIDTH (info);
  desc.height = GST_VIDEO_INFO_HEIGHT (info);
  /* GStreamer can only describe one PRIME layer */
  desc.num_layers = 1;

  n_planes = GST_VIDEO_INFO_N_PLANES (info);

  format = (drm_info->drm_fourcc == DRM_FORMAT_INVALID) ?
      GST_VIDEO_INFO_FORMAT (info) :
      gst_va_video_format_from_drm_fourcc (drm_info->drm_fourcc);
  if (format == GST_VIDEO_FORMAT_UNKNOWN)
    return VA_INVALID_ID;

  drm_fourcc = (drm_info->drm_fourcc == DRM_FORMAT_INVALID) ?
      gst_va_drm_fourcc_from_video_format (format) : drm_info->drm_fourcc;
  if (drm_fourcc == 0)
    return VA_INVALID_ID;

  rt_format = gst_va_chroma_from_video_format (format);
  if (rt_format == 0)
    return VA_INVALID_ID;

  fourcc = gst_va_fourcc_from_video_format (format);
  if (fourcc == 0)
    return VA_INVALID_ID;

  drm_modifier = (drm_info->drm_modifier == DRM_FORMAT_MOD_INVALID) ?
      DRM_FORMAT_MOD_LINEAR : drm_info->drm_modifier;

  desc.fourcc = fourcc;
  desc.layers[0].num_planes = n_planes;
  desc.layers[0].drm_format = drm_fourcc;

  for (i = j = 0; i < n_planes; i++) {
    desc.layers[0].pitch[i] = GST_VIDEO_INFO_PLANE_STRIDE (info, i);
    desc.layers[0].offset[i] = offset[i];

    if (_is_fd_repeated (fds, i, &prev)) {
      desc.layers[0].object_index[i] = prev;
      continue;
    }

    desc.objects[j].fd = fds[i];
    desc.objects[j].size = gst_va_get_fd_size (fds[i]);
    desc.objects[j].drm_format_modifier = drm_modifier;

    desc.layers[0].object_index[i] = j;
    j++;
  }

  desc.num_objects = j;

  if (!va_create_surfaces (self, rt_format, fourcc, desc.width, desc.height,
          usage_hint, NULL, 0, &desc, &surface, 1))
    return VA_INVALID_ID;

  GST_LOG_OBJECT (self, "Created surface %#x [%dx%d]", surface, desc.width,
      desc.height);

  buf = gst_va_buffer_surface_new (self, surface);
  buf->n_mems = n_planes;
  memcpy (buf->mems, mem, sizeof (buf->mems));
  _set_buffer_surface_unlocked (self, buf);
  gst_va_buffer_surface_unref (buf);

  return surface;
}

/* Imports @mem, representing a single frame, into a VASurfaceID
 * associated with every @mem in @self, unless already imported. */
gboolean
gst_va_display_setup_dmabuf_memories (GstVaDisplay * self,
    GstVideoInfoDmaDrm * drm_info, GstMemory * mem[GST_VIDEO_MAX_PLANES],
    uintptr_t fds[GST_VIDEO_MAX_PLANES], gsize offset[GST_VIDEO_MAX_PLANES],
    guint usage_hint)
{
  GstVaDisplayPrivate *priv;
  GstVaBufferSurface *buf;
  VASurfaceID surface;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), FALSE);
  g_return_val_if_fail (drm_info != NULL, FALSE);
  g_return_val_if_fail (mem != NULL, FALSE);
  g_return_val_if_fail (mem[0] != NULL, FALSE);
  g_return_val_if_fail (fds != NULL, FALSE);
  g_return_val_if_fail (offset != NULL, FALSE);

  priv = GET_PRIV (self);

  /* memories of a frame are always imported together */
  g_mutex_lock (&priv->surfaces_lock);
  buf = g_hash_table_lookup (priv->surfaces, mem[0]);
  if (buf) {
    surface = buf->surface;
  } else {
    surface = _import_dmabuf_memories_unlocked (self, drm_info, mem, fds,
        offset, usage_hint);
  }
  g_mutex_unlock (&priv->surfaces_lock);

  return surface != VA_INVALID_ID;
}

/* Finds the DMABuf memory, fd and offset of each plane of @buffer, and
 * updates @drm_info with @buffer's video meta. */
static gboolean
_find_dmabuf_memories (GstBuffer * buffer, GstVideoInfoDmaDrm * drm_info,
    GstMemory * mems[GST_VIDEO_MAX_PLANES], uintptr_t fds[GST_VIDEO_MAX_PLANES],
    gsize offset[GST_VIDEO_MAX_PLANES])
{
  GstVideoMeta *meta;
  guint i, n_planes;

  n_planes = GST_VIDEO_INFO_N_PLANES (&drm_info->vinfo);

  meta = gst_buffer_get_video_meta (buffer);

  /* Update video info based on video meta */
  if (meta) {
    if (meta->n_planes != n_planes)
      return FALSE;

    GST_VIDEO_INFO_WIDTH (&drm_info->vinfo) = meta->width;
    GST_VIDEO_INFO_HEIGHT (&drm_info->vinfo) = meta->height;

    for (i = 0; i < n_planes; i++) {
      GST_VIDEO_INFO_PLANE_OFFSET (&drm_info->vinfo, i) = meta->offset[i];
      GST_VIDEO_INFO_PLANE_STRIDE (&drm_info->vinfo, i) = meta->stride[i];
    }
  }

  /* Find and validate all memories */
  for (i = 0; i < n_planes; i++) {
    guint length;
    guint mem_idx;
    gsize mem_skip;

    if (!gst_buffer_find_memory (buffer,
            GST_VIDEO_INFO_PLANE_OFFSET (&drm_info->vinfo, i), 1, &mem_idx,
            &length, &mem_skip))
      return FALSE;

    /* We can't have more then one dmabuf per plane */
    if (length != 1)
      return FALSE;

    mems[i] = gst_buffer_peek_memory (buffer, mem_idx);

    /* And all memory found must be dmabuf */
    if (!gst_is_dmabuf_memory (mems[i]))
      return FALSE;

    offset[i] = mems[i]->offset + mem_skip;
    fds[i] = gst_dmabuf_memory_get_fd (mems[i]);
  }

  return TRUE;
}

/**
 * gst_va_display_import_buffer: (skip)
 * @self: a #GstVaDisplay
 * @buffer: a #GstBuffer
 * @drm_info: a #GstVideoInfoDmaDrm describing @buffer
 * @usage_hint: VA usage hint.
 *
 * Returns the VASurfaceID of @buffer in @self. If @buffer is made of
 * DMABuf memories not yet imported in @self, they are imported into a
 * new VASurfaceID.
 *
 * It is safe to call this function concurrently with the same @buffer:
 * only one VASurfaceID is created in @self.
 *
 * Returns: (type guint): the VASurfaceID of @buffer, or VA_INVALID_ID
 *     if @buffer cannot be imported.
 *
 * Since: 1.30
 */
VASurfaceID
gst_va_display_import_buffer (GstVaDisplay * self, GstBuffer * buffer,
    const GstVideoInfoDmaDrm * drm_info, guint usage_hint)
{
  GstVaDisplayPrivate *priv;
  GstVaBufferSurface *buf;
  GstMemory *mem;
  VASurfaceID surface = VA_INVALID_ID;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), VA_INVALID_ID);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), VA_INVALID_ID);
  g_return_val_if_fail (drm_info != NULL, VA_INVALID_ID);

  if (gst_buffer_n_memory (buffer) == 0)
    return VA_INVALID_ID;

  mem = gst_buffer_peek_memory (buffer, 0);

  /* This will eliminate most non-dmabuf out there */
  if (!gst_is_dmabuf_memory (mem))
    return gst_va_display_get_memory_surface (self, mem);

  priv = GET_PRIV (self);

  g_mutex_lock (&priv->surfaces_lock);
  buf = g_hash_table_lookup (priv->surfaces, mem);
  if (buf) {
    surface = buf->surface;
  } else {
    GstVideoInfoDmaDrm info = *drm_info;
    GstMemory *mems[GST_VIDEO_MAX_PLANES];
    uintptr_t fds[GST_VIDEO_MAX_PLANES];
    gsize offset[GST_VIDEO_MAX_PLANES];

    if (_find_dmabuf_memories (buffer, &info, mems, fds, offset)) {
      surface = _import_dmabuf_memories_unlocked (self, &info, mems, fds,
          offset, usage_hint);
    }
  }
  g_mutex_unlock (&priv->surfaces_lock);

  return surface;
}

/**
 * gst_va_display_get_memory_surface: (skip)
 * @self: a #GstVaDisplay
 * @mem: a #GstMemory
 *
 * Returns: (type guint): the VASurfaceID in @mem, or VA_INVALID_ID if
 *     @mem has no surface associated with @self.
 *
 * Since: 1.30
 */
VASurfaceID
gst_va_display_get_memory_surface (GstVaDisplay * self, GstMemory * mem)
{
  GstVaBufferSurface *buf;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), VA_INVALID_ID);
  g_return_val_if_fail (mem != NULL, VA_INVALID_ID);

  if (!mem->allocator)
    return VA_INVALID_ID;

  if (GST_IS_VA_ALLOCATOR (mem->allocator)) {
    if (gst_va_allocator_peek_display (mem->allocator) != self)
      return VA_INVALID_ID;
    return gst_va_allocator_get_memory_surface (mem);
  }

  if (!GST_IS_DMABUF_ALLOCATOR (mem->allocator))
    return VA_INVALID_ID;

  buf = gst_va_display_peek_buffer_surface (self, mem);
  return buf ? buf->surface : VA_INVALID_ID;
}

/**
 * gst_va_display_get_buffer_surface: (skip)
 * @self: a #GstVaDisplay
 * @buffer: a #GstBuffer
 *
 * Returns: (type guint): the VASurfaceID in @buffer, or VA_INVALID_ID
 *     if @buffer has no surface associated with @self.
 *
 * Since: 1.30
 */
VASurfaceID
gst_va_display_get_buffer_surface (GstVaDisplay * self, GstBuffer * buffer)
{
  GstMemory *mem;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (self), VA_INVALID_ID);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), VA_INVALID_ID);

  if (gst_buffer_n_memory (buffer) == 0)
    return VA_INVALID_ID;

  mem = gst_buffer_peek_memory (buffer, 0);
  return gst_va_display_get_memory_surface (self, mem);
}
