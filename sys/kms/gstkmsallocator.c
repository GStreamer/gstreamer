/* GStreamer
 *
 * Copyright (C) 2016 Igalia
 *
 * Authors:
 *  Víctor Manuel Jáquez Leal <vjaquez@igalia.com>
 *  Javier Martin <javiermartin@by.com.es>
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <string.h>
#include <unistd.h>

#include "gstkmsallocator.h"
#include "gstkmsutils.h"

#define GST_CAT_DEFAULT kmsallocator_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define GST_KMS_MEMORY_TYPE "KMSMemory"

struct _GstKMSAllocatorPrivate
{
  int fd;
  struct kms_driver *driver;
};

#define parent_class gst_kms_allocator_parent_class
G_DEFINE_TYPE_WITH_CODE (GstKMSAllocator, gst_kms_allocator, GST_TYPE_ALLOCATOR,
    G_ADD_PRIVATE (GstKMSAllocator);
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "kmsallocator", 0,
        "KMS allocator"));

enum
{
  PROP_DRM_FD = 1,
  PROP_N,
};

static GParamSpec *g_props[PROP_N] = { NULL, };

gboolean
gst_is_kms_memory (GstMemory * mem)
{
  return gst_memory_is_type (mem, GST_KMS_MEMORY_TYPE);
}

guint32
gst_kms_memory_get_fb_id (GstMemory * mem)
{
  if (!gst_is_kms_memory (mem))
    return 0;
  return ((GstKMSMemory *) mem)->fb_id;
}

static gboolean
ensure_kms_driver (GstKMSAllocator * alloc)
{
  GstKMSAllocatorPrivate *priv;
  int err;

  priv = alloc->priv;

  if (priv->driver)
    return TRUE;

  if (priv->fd < 0)
    return FALSE;

  err = kms_create (priv->fd, &priv->driver);
  if (err) {
    GST_ERROR_OBJECT (alloc, "Could not create KMS driver: %s",
        strerror (-err));
    return FALSE;
  }

  return TRUE;
}

static void
gst_kms_allocator_memory_reset (GstKMSAllocator * allocator, GstKMSMemory * mem)
{
  if (mem->fb_id) {
    GST_DEBUG_OBJECT (allocator, "removing fb id %d", mem->fb_id);
    drmModeRmFB (allocator->priv->fd, mem->fb_id);
    mem->fb_id = 0;
  }

  if (!ensure_kms_driver (allocator))
    return;

  if (mem->bo) {
    kms_bo_destroy (&mem->bo);
    mem->bo = NULL;
  }
}

static gboolean
gst_kms_allocator_memory_create (GstKMSAllocator * allocator,
    GstKMSMemory * kmsmem, GstVideoInfo * vinfo)
{
  gint ret;
  guint attrs[] = {
    KMS_WIDTH, GST_VIDEO_INFO_WIDTH (vinfo),
    KMS_HEIGHT, GST_VIDEO_INFO_HEIGHT (vinfo),
    KMS_TERMINATE_PROP_LIST,
  };

  if (kmsmem->bo)
    return TRUE;

  if (!ensure_kms_driver (allocator))
    return FALSE;

  ret = kms_bo_create (allocator->priv->driver, attrs, &kmsmem->bo);
  if (ret) {
    GST_ERROR_OBJECT (allocator, "Failed to create buffer object: %s (%d)",
        strerror (-ret), ret);
    return FALSE;
  }

  return TRUE;
}

static void
gst_kms_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstKMSAllocator *alloc;
  GstKMSMemory *kmsmem;

  alloc = GST_KMS_ALLOCATOR (allocator);
  kmsmem = (GstKMSMemory *) mem;

  gst_kms_allocator_memory_reset (alloc, kmsmem);
  g_slice_free (GstKMSMemory, kmsmem);
}

static void
gst_kms_allocator_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstKMSAllocator *alloc;

  alloc = GST_KMS_ALLOCATOR (object);

  switch (prop_id) {
    case PROP_DRM_FD:{
      int fd = g_value_get_int (value);
      if (fd > -1)
        alloc->priv->fd = dup (fd);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_kms_allocator_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstKMSAllocator *alloc;

  alloc = GST_KMS_ALLOCATOR (object);

  switch (prop_id) {
    case PROP_DRM_FD:
      g_value_set_int (value, alloc->priv->fd);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_kms_allocator_finalize (GObject * obj)
{
  GstKMSAllocator *alloc;

  alloc = GST_KMS_ALLOCATOR (obj);

  if (alloc->priv->driver)
    kms_destroy (&alloc->priv->driver);

  if (alloc->priv->fd > -1)
    close (alloc->priv->fd);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_kms_allocator_class_init (GstKMSAllocatorClass * klass)
{
  GObjectClass *gobject_class;
  GstAllocatorClass *allocator_class;

  allocator_class = GST_ALLOCATOR_CLASS (klass);
  gobject_class = G_OBJECT_CLASS (klass);

  allocator_class->free = gst_kms_allocator_free;

  gobject_class->set_property = gst_kms_allocator_set_property;
  gobject_class->get_property = gst_kms_allocator_get_property;
  gobject_class->finalize = gst_kms_allocator_finalize;

  g_props[PROP_DRM_FD] = g_param_spec_int ("drm-fd", "DRM fd",
      "DRM file descriptor", -1, G_MAXINT, -1,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_properties (gobject_class, PROP_N, g_props);
}

static gpointer
gst_kms_memory_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  GstKMSMemory *kmsmem;
  int err;
  gpointer out;

  if (!ensure_kms_driver ((GstKMSAllocator *) mem->allocator))
    return NULL;

  kmsmem = (GstKMSMemory *) mem;
  if (!kmsmem->bo)
    return NULL;

  out = NULL;
  err = kms_bo_map (kmsmem->bo, &out);
  if (err) {
    GST_ERROR ("could not map memory: %s %d", strerror (-err), err);
    return NULL;
  }

  return out;
}

static void
gst_kms_memory_unmap (GstMemory * mem)
{
  GstKMSMemory *kmsmem;

  if (!ensure_kms_driver ((GstKMSAllocator *) mem->allocator))
    return;

  kmsmem = (GstKMSMemory *) mem;
  if (kmsmem->bo)
    kms_bo_unmap (kmsmem->bo);
}

static void
gst_kms_allocator_init (GstKMSAllocator * allocator)
{
  GstAllocator *alloc;

  alloc = GST_ALLOCATOR_CAST (allocator);

  allocator->priv = gst_kms_allocator_get_instance_private (allocator);
  allocator->priv->fd = -1;

  alloc->mem_type = GST_KMS_MEMORY_TYPE;
  alloc->mem_map = gst_kms_memory_map;
  alloc->mem_unmap = gst_kms_memory_unmap;
  /* Use the default, fallback copy function */

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

GstAllocator *
gst_kms_allocator_new (int fd)
{
  return g_object_new (GST_TYPE_KMS_ALLOCATOR, "name",
      "KMSMemory::allocator", "drm-fd", fd, NULL);
}

/* The mem_offsets are relative to the GstMemory start, unlike the vinfo->offset
 * which are relative to the GstBuffer start. */
static gboolean
gst_kms_allocator_add_fb (GstKMSAllocator * alloc, GstKMSMemory * kmsmem,
    gsize mem_offsets[GST_VIDEO_MAX_PLANES], GstVideoInfo * vinfo)
{
  int i, ret;
  gint num_planes = GST_VIDEO_INFO_N_PLANES (vinfo);
  guint32 w, h, fmt, pitch = 0, bo_handles[4] = { 0, };
  guint32 offsets[4] = { 0, };
  guint32 pitches[4] = { 0, };

  if (kmsmem->fb_id)
    return TRUE;

  w = GST_VIDEO_INFO_WIDTH (vinfo);
  h = GST_VIDEO_INFO_HEIGHT (vinfo);
  fmt = gst_drm_format_from_video (GST_VIDEO_INFO_FORMAT (vinfo));

  if (kmsmem->bo) {
    kms_bo_get_prop (kmsmem->bo, KMS_HANDLE, &bo_handles[0]);
    for (i = 1; i < num_planes; i++)
      bo_handles[i] = bo_handles[0];

    /* Get the bo pitch calculated by the kms driver.
     * If it's defined, it will overwrite the video info's stride */
    kms_bo_get_prop (kmsmem->bo, KMS_PITCH, &pitch);
  } else {
    for (i = 0; i < num_planes; i++)
      bo_handles[i] = kmsmem->gem_handle[i];
  }

  GST_DEBUG_OBJECT (alloc, "bo handles: %d, %d, %d, %d", bo_handles[0],
      bo_handles[1], bo_handles[2], bo_handles[3]);

  for (i = 0; i < num_planes; i++) {
    offsets[i] = mem_offsets[i];
    if (pitch)
      GST_VIDEO_INFO_PLANE_STRIDE (vinfo, i) = pitch;
    pitches[i] = GST_VIDEO_INFO_PLANE_STRIDE (vinfo, i);
    GST_DEBUG_OBJECT (alloc, "Create FB plane %i with stride %u and offset %u",
        i, pitches[i], offsets[i]);
  }

  ret = drmModeAddFB2 (alloc->priv->fd, w, h, fmt, bo_handles, pitches,
      offsets, &kmsmem->fb_id, 0);
  if (ret) {
    GST_ERROR_OBJECT (alloc, "Failed to bind to framebuffer: %s (%d)",
        strerror (-ret), ret);
    return FALSE;
  }
  return TRUE;
}

static GstMemory *
gst_kms_allocator_alloc_empty (GstAllocator * allocator, GstVideoInfo * vinfo)
{
  GstKMSMemory *kmsmem;
  GstMemory *mem;

  kmsmem = g_slice_new0 (GstKMSMemory);
  if (!kmsmem)
    return NULL;
  mem = GST_MEMORY_CAST (kmsmem);

  gst_memory_init (mem, GST_MEMORY_FLAG_NO_SHARE, allocator, NULL,
      GST_VIDEO_INFO_SIZE (vinfo), 0, 0, GST_VIDEO_INFO_SIZE (vinfo));

  return mem;
}

GstMemory *
gst_kms_allocator_bo_alloc (GstAllocator * allocator, GstVideoInfo * vinfo)
{
  GstKMSAllocator *alloc;
  GstKMSMemory *kmsmem;
  GstMemory *mem;

  mem = gst_kms_allocator_alloc_empty (allocator, vinfo);
  if (!mem)
    return NULL;

  alloc = GST_KMS_ALLOCATOR (allocator);
  kmsmem = (GstKMSMemory *) mem;
  if (!gst_kms_allocator_memory_create (alloc, kmsmem, vinfo))
    goto fail;
  if (!gst_kms_allocator_add_fb (alloc, kmsmem, vinfo->offset, vinfo))
    goto fail;

  return mem;

  /* ERRORS */
fail:
  gst_memory_unref (mem);
  return NULL;
}

GstKMSMemory *
gst_kms_allocator_dmabuf_import (GstAllocator * allocator, gint * prime_fds,
    gint n_planes, gsize offsets[GST_VIDEO_MAX_PLANES], GstVideoInfo * vinfo)
{
  GstKMSAllocator *alloc;
  GstMemory *mem;
  GstKMSMemory *tmp;
  gint i, ret;

  g_return_val_if_fail (n_planes <= GST_VIDEO_MAX_PLANES, FALSE);

  mem = gst_kms_allocator_alloc_empty (allocator, vinfo);
  if (!mem)
    return FALSE;

  tmp = (GstKMSMemory *) mem;
  alloc = GST_KMS_ALLOCATOR (allocator);
  for (i = 0; i < n_planes; i++) {
    ret = drmPrimeFDToHandle (alloc->priv->fd, prime_fds[i],
        &tmp->gem_handle[i]);
    if (ret)
      goto import_fd_failed;
  }

  if (!gst_kms_allocator_add_fb (alloc, tmp, offsets, vinfo))
    goto failed;

  return tmp;

  /* ERRORS */
import_fd_failed:
  {
    GST_ERROR_OBJECT (alloc, "Failed to import prime fd %d: %s (%d)",
        prime_fds[i], strerror (-ret), ret);
    /* fallback */
  }

failed:
  {
    gst_memory_unref (mem);
    return NULL;
  }
}
