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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvaallocator.h"

#include <sys/types.h>
#include <unistd.h>
#include <va/va_drmcommon.h>

#include "gstvacaps.h"
#include "gstvavideoformat.h"

#define GST_CAT_DEFAULT gst_va_memory_debug
GST_DEBUG_CATEGORY_STATIC (gst_va_memory_debug);

struct _GstVaDmabufAllocator
{
  GstDmaBufAllocator parent;

  /* queue for disposable surfaces */
  GstAtomicQueue *queue;
  GstVaDisplay *display;

  GstMemoryMapFunction parent_map;
};

static void _init_debug_category (void);

#define gst_va_dmabuf_allocator_parent_class dmabuf_parent_class
G_DEFINE_TYPE_WITH_CODE (GstVaDmabufAllocator, gst_va_dmabuf_allocator,
    GST_TYPE_DMABUF_ALLOCATOR, _init_debug_category ());

typedef struct _GstVaBufferSurface GstVaBufferSurface;
struct _GstVaBufferSurface
{
  GstVideoInfo info;
  VASurfaceID surface;
  volatile gint ref_count;
};

static void
_init_debug_category (void)
{
#ifndef GST_DISABLE_GST_DEBUG
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_va_memory_debug, "vamemory", 0, "VA memory");
    g_once_init_leave (&_init, 1);
  }
#endif
}

static gboolean
_destroy_surfaces (GstVaDisplay * display, VASurfaceID * surfaces,
    gint num_surfaces)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  g_return_val_if_fail (num_surfaces > 0, FALSE);

  gst_va_display_lock (display);
  status = vaDestroySurfaces (dpy, surfaces, num_surfaces);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaDestroySurfaces: %s", vaErrorStr (status));
    return FALSE;
  }

  return TRUE;

}

static gboolean
_create_surfaces (GstVaDisplay * display, guint rt_format, guint fourcc,
    guint width, guint height, gint usage_hint, VASurfaceID * surfaces,
    guint num_surfaces)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  /* *INDENT-OFF* */
  VASurfaceAttrib attrs[] = {
    {
      .type = VASurfaceAttribUsageHint,
      .flags = VA_SURFACE_ATTRIB_SETTABLE,
      .value.type = VAGenericValueTypeInteger,
      .value.value.i = usage_hint,
    },
    {
      .type = VASurfaceAttribPixelFormat,
      .flags = VA_SURFACE_ATTRIB_SETTABLE,
      .value.type = VAGenericValueTypeInteger,
      .value.value.i = fourcc,
    },
    {
      .type = VASurfaceAttribMemoryType,
      .flags = VA_SURFACE_ATTRIB_SETTABLE,
      .value.type = VAGenericValueTypeInteger,
      .value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_VA,
    },
  };
  /* *INDENT-ON* */
  VAStatus status;

  g_return_val_if_fail (num_surfaces > 0, FALSE);

  gst_va_display_lock (display);
  status = vaCreateSurfaces (dpy, rt_format, width, height, surfaces,
      num_surfaces, attrs, G_N_ELEMENTS (attrs));
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaCreateSurfaces: %s", vaErrorStr (status));
    return FALSE;
  }

  return TRUE;
}

static gboolean
_export_surface_to_dmabuf (GstVaDisplay * display, VASurfaceID surface,
    guint32 flags, VADRMPRIMESurfaceDescriptor * desc)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  gst_va_display_lock (display);
  status = vaExportSurfaceHandle (dpy, surface,
      VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2, flags, desc);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaExportSurfaceHandle: %s", vaErrorStr (status));
    return FALSE;
  }

  return TRUE;
}

static gboolean
_destroy_image (GstVaDisplay * display, VAImageID image_id)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  gst_va_display_lock (display);
  status = vaDestroyImage (dpy, image_id);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaDestroyImage: %s", vaErrorStr (status));
    return FALSE;
  }
  return TRUE;
}

static gboolean
_get_derive_image (GstVaDisplay * display, VASurfaceID surface, VAImage * image)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  gst_va_display_lock (display);
  status = vaDeriveImage (dpy, surface, image);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_WARNING ("vaDeriveImage: %s", vaErrorStr (status));
    return FALSE;
  }

  return TRUE;
}

static gboolean
_create_image (GstVaDisplay * display, GstVideoFormat format, gint width,
    gint height, VAImage * image)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  const VAImageFormat *va_format;
  VAStatus status;

  va_format = gst_va_image_format_from_video_format (format);
  if (!va_format)
    return FALSE;

  gst_va_display_lock (display);
  status =
      vaCreateImage (dpy, (VAImageFormat *) va_format, width, height, image);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaCreateImage: %s", vaErrorStr (status));
    return FALSE;
  }
  return TRUE;
}

static gboolean
_get_image (GstVaDisplay * display, VASurfaceID surface, VAImage * image)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  gst_va_display_lock (display);
  status = vaGetImage (dpy, surface, 0, 0, image->width, image->height,
      image->image_id);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaGetImage: %s", vaErrorStr (status));
    return FALSE;
  }

  return TRUE;
}

static gboolean
_sync_surface (GstVaDisplay * display, VASurfaceID surface)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  gst_va_display_lock (display);
  status = vaSyncSurface (dpy, surface);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_WARNING ("vaSyncSurface: %s", vaErrorStr (status));
    return FALSE;
  }
  return TRUE;
}

static gboolean
_map_buffer (GstVaDisplay * display, VABufferID buffer, gpointer * data)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  gst_va_display_lock (display);
  status = vaMapBuffer (dpy, buffer, data);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_WARNING ("vaMapBuffer: %s", vaErrorStr (status));
    return FALSE;
  }
  return TRUE;
}

static gboolean
_unmap_buffer (GstVaDisplay * display, VABufferID buffer)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  gst_va_display_lock (display);
  status = vaUnmapBuffer (dpy, buffer);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_WARNING ("vaUnmapBuffer: %s", vaErrorStr (status));
    return FALSE;
  }
  return TRUE;
}

static gboolean
_put_image (GstVaDisplay * display, VASurfaceID surface, VAImage * image)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  if (!_sync_surface (display, surface))
    return FALSE;

  gst_va_display_lock (display);
  status = vaPutImage (dpy, surface, image->image_id, 0, 0, image->width,
      image->height, 0, 0, image->width, image->height);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaPutImage: %s", vaErrorStr (status));
    return FALSE;
  }
  return TRUE;
}

static GQuark
gst_va_buffer_surface_quark (void)
{
  static gsize surface_quark = 0;

  if (g_once_init_enter (&surface_quark)) {
    GQuark quark = g_quark_from_string ("GstVaBufferSurface");
    g_once_init_leave (&surface_quark, quark);
  }

  return surface_quark;
}

static GQuark
gst_va_drm_mod_quark (void)
{
  static gsize drm_mod_quark = 0;

  if (g_once_init_enter (&drm_mod_quark)) {
    GQuark quark = g_quark_from_string ("DRMModifier");
    g_once_init_leave (&drm_mod_quark, quark);
  }

  return drm_mod_quark;
}

static gpointer
gst_va_dmabuf_mem_map (GstMemory * gmem, gsize maxsize, GstMapFlags flags)
{
  GstVaDmabufAllocator *self = GST_VA_DMABUF_ALLOCATOR (gmem->allocator);
  VASurfaceID surface = gst_va_memory_get_surface (gmem, NULL);

  _sync_surface (self->display, surface);

  /* @TODO: if mapping with flag GST_MAP_VASURFACE return the
   * VA_SURFACE_ID.
   * if mapping and drm_modifers are not lineal, use vaDeriveImage */
#ifndef GST_DISABLE_GST_DEBUG
  {
    guint64 *drm_mod;

    drm_mod = gst_mini_object_get_qdata (GST_MINI_OBJECT (gmem),
        gst_va_drm_mod_quark ());
    GST_TRACE_OBJECT (self, "DRM modifiers: %#lx", *drm_mod);
  }
#endif

  return self->parent_map (gmem, maxsize, flags);
}

static void
gst_va_dmabuf_allocator_dispose (GObject * object)
{
  GstVaDmabufAllocator *self = GST_VA_DMABUF_ALLOCATOR (object);

  gst_clear_object (&self->display);
  gst_atomic_queue_unref (self->queue);

  G_OBJECT_CLASS (dmabuf_parent_class)->dispose (object);
}

static void
gst_va_dmabuf_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstVaDmabufAllocator *self = GST_VA_DMABUF_ALLOCATOR (allocator);
  GstVaBufferSurface *buf;

  /* first close the dmabuf fd */
  GST_ALLOCATOR_CLASS (dmabuf_parent_class)->free (allocator, mem);

  while ((buf = gst_atomic_queue_pop (self->queue))) {
    GST_LOG_OBJECT (self, "Destroying surface %#x", buf->surface);
    _destroy_surfaces (self->display, &buf->surface, 1);
    g_slice_free (GstVaBufferSurface, buf);
  }
}

static void
gst_va_dmabuf_allocator_class_init (GstVaDmabufAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_va_dmabuf_allocator_dispose;
  allocator_class->free = gst_va_dmabuf_allocator_free;
}

static void
gst_va_dmabuf_allocator_init (GstVaDmabufAllocator * self)
{
  self->queue = gst_atomic_queue_new (2);

  self->parent_map = GST_ALLOCATOR (self)->mem_map;
  GST_ALLOCATOR (self)->mem_map = gst_va_dmabuf_mem_map;
}

GstAllocator *
gst_va_dmabuf_allocator_new (GstVaDisplay * display)
{
  GstVaDmabufAllocator *self;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (display), NULL);

  self = g_object_new (GST_TYPE_VA_DMABUF_ALLOCATOR, NULL);
  self->display = gst_object_ref (display);
  gst_object_ref_sink (self);

  return GST_ALLOCATOR (self);
}

static GstVaBufferSurface *
_create_buffer_surface (VASurfaceID surface, GstVideoFormat format,
    gint width, gint height)
{
  GstVaBufferSurface *buf = g_slice_new (GstVaBufferSurface);

  g_atomic_int_set (&buf->ref_count, 0);
  buf->surface = surface;
  gst_video_info_set_format (&buf->info, format, width, height);

  return buf;
}

static inline goffset
_get_fd_size (gint fd)
{
  return lseek (fd, 0, SEEK_END);
}

static gboolean
gst_va_memory_dispose (GstMiniObject * mini_object)
{
  GstMemory *mem = GST_MEMORY_CAST (mini_object);
  GstVaDmabufAllocator *self = GST_VA_DMABUF_ALLOCATOR (mem->allocator);
  GstVaBufferSurface *buf;

  buf = gst_mini_object_get_qdata (mini_object, gst_va_buffer_surface_quark ());
  if (buf && g_atomic_int_dec_and_test (&buf->ref_count))
    gst_atomic_queue_push (self->queue, buf);

  return TRUE;
}

gboolean
gst_va_dmabuf_setup_buffer (GstAllocator * allocator, GstBuffer * buffer,
    GstVaAllocationParams * params)
{
  GstVaBufferSurface *buf;
  GstVaDmabufAllocator *self = GST_VA_DMABUF_ALLOCATOR (allocator);
  GstVideoFormat format;
  VADRMPRIMESurfaceDescriptor desc = { 0, };
  VASurfaceID surface;
  guint32 i, fourcc, rt_format, export_flags;

  g_return_val_if_fail (GST_IS_VA_DMABUF_ALLOCATOR (allocator), FALSE);
  g_return_val_if_fail (params, FALSE);

  format = GST_VIDEO_INFO_FORMAT (&params->info);
  fourcc = gst_va_fourcc_from_video_format (format);
  rt_format = gst_va_chroma_from_video_format (format);
  if (fourcc == 0 || rt_format == 0) {
    GST_ERROR_OBJECT (allocator, "Unsupported format: %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&params->info)));
    return FALSE;
  }

  if (!_create_surfaces (self->display, rt_format, fourcc,
          GST_VIDEO_INFO_WIDTH (&params->info),
          GST_VIDEO_INFO_HEIGHT (&params->info), params->usage_hint, &surface,
          1))
    return FALSE;

  /* Each layer will contain exactly one plane.  For example, an NV12
   * surface will be exported as two layers */
  export_flags = VA_EXPORT_SURFACE_SEPARATE_LAYERS
      | VA_EXPORT_SURFACE_READ_WRITE;
  if (!_export_surface_to_dmabuf (self->display, surface, export_flags, &desc))
    goto failed;

  g_assert (GST_VIDEO_INFO_N_PLANES (&params->info) == desc.num_layers);

  if (fourcc != desc.fourcc) {
    GST_ERROR ("Unsupported fourcc: %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (desc.fourcc));
    goto failed;
  }

  buf = _create_buffer_surface (surface, format, desc.width, desc.height);

  for (i = 0; i < desc.num_objects; i++) {
    gint fd = desc.objects[i].fd;
    gsize size = desc.objects[i].size > 0 ?
        desc.objects[i].size : _get_fd_size (fd);
    GstMemory *mem = gst_dmabuf_allocator_alloc (allocator, fd, size);
    guint64 *drm_mod = g_new (guint64, 1);

    gst_buffer_append_memory (buffer, mem);

    GST_MINI_OBJECT (mem)->dispose = gst_va_memory_dispose;

    g_atomic_int_add (&buf->ref_count, 1);
    gst_mini_object_set_qdata (GST_MINI_OBJECT (mem),
        gst_va_buffer_surface_quark (), buf, NULL);

    *drm_mod = desc.objects[i].drm_format_modifier;
    gst_mini_object_set_qdata (GST_MINI_OBJECT (mem), gst_va_drm_mod_quark (),
        drm_mod, g_free);
  }

  for (i = 0; i < desc.num_layers; i++) {
    g_assert (desc.layers[i].num_planes == 1);
    GST_VIDEO_INFO_PLANE_OFFSET (&buf->info, i) = desc.layers[i].offset[0];
    GST_VIDEO_INFO_PLANE_STRIDE (&buf->info, i) = desc.layers[i].pitch[0];
  }

  GST_VIDEO_INFO_SIZE (&buf->info) = gst_buffer_get_size (buffer);
  GST_LOG_OBJECT (self, "Created surface %#x [%dx%d] size %" G_GSIZE_FORMAT,
      buf->surface, GST_VIDEO_INFO_WIDTH (&buf->info),
      GST_VIDEO_INFO_HEIGHT (&buf->info), GST_VIDEO_INFO_SIZE (&buf->info));

  params->info = buf->info;

  return TRUE;

failed:
  {
    _destroy_surfaces (self->display, &surface, 1);
    return FALSE;
  }
}

gboolean
gst_va_dmabuf_try (GstAllocator * allocator, GstVaAllocationParams * params)
{
  GstBuffer *buffer = gst_buffer_new ();
  GstMapInfo map_info;
  gboolean ret;

  ret = gst_va_dmabuf_setup_buffer (allocator, buffer, params);
  if (ret) {
    /* XXX: radeonsi for kadaveri cannot map dmabufs to user space */
    if (!gst_buffer_map (buffer, &map_info, GST_MAP_READWRITE)) {
      GST_WARNING_OBJECT (allocator,
          "DMABuf backend cannot map frames to user space.");
    }
    gst_buffer_unmap (buffer, &map_info);
  }
  gst_buffer_unref (buffer);

  return ret;
}

/*===================== GstVaAllocator / GstVaMemory =========================*/

struct _GstVaAllocator
{
  GstAllocator parent;

  GstVaDisplay *display;
  gboolean use_derived;
  GArray *surface_formats;
};

typedef struct _GstVaMemory GstVaMemory;
struct _GstVaMemory
{
  GstMemory parent;

  GstVideoInfo info;
  VASurfaceID surface;
  GstVideoFormat surface_format;
  VAImage image;
  gpointer mapped_data;

  GstMapFlags prev_mapflags;
  volatile gint map_count;

  gboolean is_derived;
  gboolean is_dirty;
  GMutex lock;
};

G_DEFINE_TYPE_WITH_CODE (GstVaAllocator, gst_va_allocator, GST_TYPE_ALLOCATOR,
    _init_debug_category ());

static void
gst_va_allocator_dispose (GObject * object)
{
  GstVaAllocator *self = GST_VA_ALLOCATOR (object);

  gst_clear_object (&self->display);
  g_clear_pointer (&self->surface_formats, g_array_unref);

  G_OBJECT_CLASS (gst_va_allocator_parent_class)->dispose (object);
}

static void
_va_free (GstAllocator * allocator, GstMemory * mem)
{
  GstVaAllocator *self = GST_VA_ALLOCATOR (allocator);
  GstVaMemory *va_mem = (GstVaMemory *) mem;

  GST_LOG_OBJECT (self, "Destroying surface %#x", va_mem->surface);

  _destroy_surfaces (self->display, &va_mem->surface, 1);
  g_mutex_clear (&va_mem->lock);

  g_slice_free (GstVaMemory, va_mem);
}

static void
gst_va_allocator_class_init (GstVaAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_va_allocator_dispose;
  allocator_class->free = _va_free;
}

static inline void
_clean_mem (GstVaMemory * mem)
{
  memset (&mem->image, 0, sizeof (mem->image));
  mem->image.image_id = VA_INVALID_ID;
  mem->image.buf = VA_INVALID_ID;

  mem->is_derived = TRUE;
  mem->is_dirty = FALSE;
  mem->prev_mapflags = 0;
  mem->mapped_data = NULL;
}

static void
_reset_mem (GstVaMemory * mem, GstAllocator * allocator, gsize size)
{
  _clean_mem (mem);
  g_atomic_int_set (&mem->map_count, 0);
  g_mutex_init (&mem->lock);

  gst_memory_init (GST_MEMORY_CAST (mem), GST_MEMORY_FLAG_NO_SHARE, allocator,
      NULL, size, 0 /* align */ , 0 /* offset */ , size);
}

static inline gboolean
_ensure_image (GstVaDisplay * display, VASurfaceID surface, GstVideoInfo * info,
    VAImage * image, gboolean * derived)
{
  gint i;
  gboolean try_derived;

  if (image->image_id != VA_INVALID_ID)
    return TRUE;

  if (!_sync_surface (display, surface))
    return FALSE;

  try_derived = (derived) ? *derived : FALSE;

  if (try_derived && _get_derive_image (display, surface, image))
    goto bail;
  if (!_create_image (display, GST_VIDEO_INFO_FORMAT (info),
          GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info), image))
    return FALSE;

  if (derived)
    *derived = FALSE;

bail:
  for (i = 0; i < image->num_planes; i++) {
    GST_VIDEO_INFO_PLANE_OFFSET (info, i) = image->offsets[i];
    GST_VIDEO_INFO_PLANE_STRIDE (info, i) = image->pitches[i];
  }

  GST_VIDEO_INFO_SIZE (info) = image->data_size;

  return TRUE;
}

static gpointer
_va_map_unlocked (GstVaMemory * mem, GstMapFlags flags)
{
  GstAllocator *allocator = GST_MEMORY_CAST (mem)->allocator;
  GstVaAllocator *va_allocator;
  GstVaDisplay *display;

  g_return_val_if_fail (mem->surface != VA_INVALID_ID, NULL);
  g_return_val_if_fail (GST_IS_VA_ALLOCATOR (allocator), NULL);

  if (g_atomic_int_get (&mem->map_count) > 0) {
    if (mem->prev_mapflags != flags || !mem->mapped_data)
      return NULL;
    else
      goto success;
  }

  va_allocator = GST_VA_ALLOCATOR (allocator);
  display = va_allocator->display;

  if (flags & GST_MAP_WRITE) {
    mem->is_dirty = TRUE;
    mem->is_derived = FALSE;
  } else {                      /* GST_MAP_READ only */
    mem->is_dirty = FALSE;
    mem->is_derived = va_allocator->use_derived &&
        (GST_VIDEO_INFO_FORMAT (&mem->info) == mem->surface_format);
  }

  if (flags & GST_MAP_VA) {
    mem->mapped_data = &mem->surface;
    goto success;
  }

  if (!_ensure_image (display, mem->surface, &mem->info, &mem->image,
          &mem->is_derived))
    return NULL;

  va_allocator->use_derived = mem->is_derived;

  if (!mem->is_derived) {
    if (!_get_image (display, mem->surface, &mem->image))
      goto fail;
  }

  if (!_map_buffer (display, mem->image.buf, &mem->mapped_data))
    goto fail;

success:
  {
    mem->prev_mapflags = flags;
    g_atomic_int_add (&mem->map_count, 1);
    return mem->mapped_data;
  }

fail:
  {
    _destroy_image (display, mem->image.image_id);
    _clean_mem (mem);
    return NULL;
  }
}

static gpointer
_va_map (GstVaMemory * mem, gsize maxsize, GstMapFlags flags)
{
  gpointer data;

  g_mutex_lock (&mem->lock);
  data = _va_map_unlocked (mem, flags);
  g_mutex_unlock (&mem->lock);

  return data;
}

static gboolean
_va_unmap_unlocked (GstVaMemory * mem)
{
  GstAllocator *allocator = GST_MEMORY_CAST (mem)->allocator;
  GstVaDisplay *display;
  gboolean ret = TRUE;

  if (!g_atomic_int_dec_and_test (&mem->map_count))
    return TRUE;

  if (mem->prev_mapflags & GST_MAP_VA)
    goto bail;

  display = GST_VA_ALLOCATOR (allocator)->display;

  if (mem->image.image_id != VA_INVALID_ID) {
    if (mem->is_dirty && !mem->is_derived) {
      ret = _put_image (display, mem->surface, &mem->image);
      mem->is_dirty = FALSE;
    }
    /* XXX(victor): if is derived and is dirty, create another surface
     * an replace it in mem */
  }

  ret &= _unmap_buffer (display, mem->image.buf);
  ret &= _destroy_image (display, mem->image.image_id);

bail:
  _clean_mem (mem);

  return ret;
}

static gboolean
_va_unmap (GstVaMemory * mem)
{
  gboolean ret;

  g_mutex_lock (&mem->lock);
  ret = _va_unmap_unlocked (mem);
  g_mutex_unlock (&mem->lock);

  return ret;
}

/* XXX(victor): shallow copy -- only the surface */
static GstMemory *
_va_copy_unlocked (GstVaMemory * mem)
{
  GstVaMemory *ret;
  gsize size;

  ret = g_slice_new (GstVaMemory);

  size = GST_VIDEO_INFO_SIZE (&mem->info);

  ret->info = mem->info;
  ret->surface = mem->surface;

  _reset_mem (ret, GST_MEMORY_CAST (mem)->allocator, size);

  return GST_MEMORY_CAST (ret);
}

static GstMemory *
_va_copy (GstVaMemory * mem, gssize offset, gssize size)
{
  GstMemory *ret;

  g_mutex_lock (&mem->lock);
  ret = _va_copy_unlocked (mem);
  g_mutex_unlock (&mem->lock);

  return ret;
}

static GstMemory *
_va_share (GstMemory * mem, gssize offset, gssize size)
{
  /* VA surfaces are opaque structures, which cannot be shared */
  return NULL;
}

static gboolean
_va_is_span (GstMemory * mem1, GstMemory * mem2, gsize * offset)
{
  /* VA surfaces are opaque structures, which might live in other
   * memory. It is impossible to know, so far, if they can mergable. */
  return FALSE;
}

static void
gst_va_allocator_init (GstVaAllocator * self)
{
  GstAllocator *allocator = GST_ALLOCATOR (self);

  allocator->mem_type = GST_ALLOCATOR_VASURFACE;
  allocator->mem_map = (GstMemoryMapFunction) _va_map;
  allocator->mem_unmap = (GstMemoryUnmapFunction) _va_unmap;
  allocator->mem_copy = (GstMemoryCopyFunction) _va_copy;
  allocator->mem_share = _va_share;
  allocator->mem_is_span = _va_is_span;

  self->use_derived = TRUE;

  GST_OBJECT_FLAG_SET (self, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

GstMemory *
gst_va_allocator_alloc (GstAllocator * allocator,
    GstVaAllocationParams * params)
{
  GstVaAllocator *self;
  GstVaMemory *mem;
  GstVideoFormat format;
  VAImage image = { 0, };
  VASurfaceID surface;
  guint32 fourcc, rt_format;

  g_return_val_if_fail (GST_IS_VA_ALLOCATOR (allocator), NULL);

  self = GST_VA_ALLOCATOR (allocator);

  format =
      gst_va_video_surface_format_from_image_format (GST_VIDEO_INFO_FORMAT
      (&params->info), self->surface_formats);
  if (format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (allocator, "Unsupported format: %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&params->info)));
    return NULL;
  }

  fourcc = gst_va_fourcc_from_video_format (format);
  rt_format = gst_va_chroma_from_video_format (format);
  if (fourcc == 0 || rt_format == 0) {
    GST_ERROR_OBJECT (allocator, "Unsupported format: %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&params->info)));
    return NULL;
  }

  if (!_create_surfaces (self->display, rt_format, fourcc,
          GST_VIDEO_INFO_WIDTH (&params->info),
          GST_VIDEO_INFO_HEIGHT (&params->info), params->usage_hint, &surface,
          1))
    return NULL;

  image.image_id = VA_INVALID_ID;
  if (!_ensure_image (self->display, surface, &params->info, &image, NULL))
    return NULL;
  _destroy_image (self->display, image.image_id);

  mem = g_slice_new (GstVaMemory);

  mem->surface = surface;
  mem->surface_format = format;
  mem->info = params->info;

  _reset_mem (mem, allocator, GST_VIDEO_INFO_SIZE (&params->info));

  GST_LOG_OBJECT (self, "Created surface %#x [%dx%d]", mem->surface,
      GST_VIDEO_INFO_WIDTH (&mem->info), GST_VIDEO_INFO_HEIGHT (&mem->info));

  return GST_MEMORY_CAST (mem);
}

GstAllocator *
gst_va_allocator_new (GstVaDisplay * display, GArray * surface_formats)
{
  GstVaAllocator *self;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (display), NULL);

  self = g_object_new (GST_TYPE_VA_ALLOCATOR, NULL);
  self->display = gst_object_ref (display);
  self->surface_formats = surface_formats;
  gst_object_ref_sink (self);

  return GST_ALLOCATOR (self);
}

gboolean
gst_va_allocator_try (GstAllocator * allocator, GstVaAllocationParams * params)
{
  GstMemory *mem;

  mem = gst_va_allocator_alloc (allocator, params);
  if (!mem)
    return FALSE;
  gst_memory_unref (mem);
  return TRUE;
}

/*============ Utilities =====================================================*/

VASurfaceID
gst_va_memory_get_surface (GstMemory * mem, GstVideoInfo * info)
{
  VASurfaceID surface = VA_INVALID_ID;

  if (!mem->allocator)
    return VA_INVALID_ID;

  if (GST_IS_VA_DMABUF_ALLOCATOR (mem->allocator)) {
    GstVaBufferSurface *buf;

    buf = gst_mini_object_get_qdata (GST_MINI_OBJECT (mem),
        gst_va_buffer_surface_quark ());
    if (buf) {
      if (info)
        *info = buf->info;
      surface = buf->surface;
    }
  } else if (GST_IS_VA_ALLOCATOR (mem->allocator)) {
    GstVaMemory *va_mem = (GstVaMemory *) mem;
    surface = va_mem->surface;
    if (info)
      *info = va_mem->info;
  }

  return surface;
}

VASurfaceID
gst_va_buffer_get_surface (GstBuffer * buffer, GstVideoInfo * info)
{
  GstMemory *mem;

  mem = gst_buffer_peek_memory (buffer, 0);
  if (!mem)
    return VA_INVALID_ID;

  return gst_va_memory_get_surface (mem, info);
}
