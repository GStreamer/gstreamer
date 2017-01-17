/*
 * Copyright (C) 2014 Collabora Ltd.
 *     Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
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

#include "config.h"

#ifndef _GNU_SOURCE
# define _GNU_SOURCE            /* O_CLOEXEC */
#endif

#include "ext/videodev2.h"
#include "gstv4l2allocator.h"
#include "v4l2_calls.h"

#include <gst/allocators/gstdmabuf.h>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

#define GST_V4L2_MEMORY_TYPE "V4l2Memory"

#define gst_v4l2_allocator_parent_class parent_class
G_DEFINE_TYPE (GstV4l2Allocator, gst_v4l2_allocator, GST_TYPE_ALLOCATOR);

GST_DEBUG_CATEGORY_STATIC (v4l2allocator_debug);
#define GST_CAT_DEFAULT v4l2allocator_debug

#define UNSET_QUEUED(buffer) \
    ((buffer).flags &= ~(V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE))

#define SET_QUEUED(buffer) ((buffer).flags |= V4L2_BUF_FLAG_QUEUED)

#define IS_QUEUED(buffer) \
    ((buffer).flags & (V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE))

enum
{
  GROUP_RELEASED,
  LAST_SIGNAL
};

static guint gst_v4l2_allocator_signals[LAST_SIGNAL] = { 0 };

static void gst_v4l2_allocator_release (GstV4l2Allocator * allocator,
    GstV4l2Memory * mem);

static const gchar *
memory_type_to_str (guint32 memory)
{
  switch (memory) {
    case V4L2_MEMORY_MMAP:
      return "mmap";
    case V4L2_MEMORY_USERPTR:
      return "userptr";
    case V4L2_MEMORY_DMABUF:
      return "dmabuf";
    default:
      return "unknown";
  }
}

/*************************************/
/* GstV4lMemory implementation */
/*************************************/

static gpointer
_v4l2mem_map (GstV4l2Memory * mem, gsize maxsize, GstMapFlags flags)
{
  gpointer data = NULL;

  switch (mem->group->buffer.memory) {
    case V4L2_MEMORY_MMAP:
    case V4L2_MEMORY_USERPTR:
      data = mem->data;
      break;
    case V4L2_MEMORY_DMABUF:
      /* v4l2 dmabuf memory are not shared with downstream */
      g_assert_not_reached ();
      break;
    default:
      GST_WARNING ("Unknown memory type %i", mem->group->buffer.memory);
      break;
  }
  return data;
}

static gboolean
_v4l2mem_unmap (GstV4l2Memory * mem)
{
  gboolean ret = FALSE;

  switch (mem->group->buffer.memory) {
    case V4L2_MEMORY_MMAP:
    case V4L2_MEMORY_USERPTR:
      ret = TRUE;
      break;
    case V4L2_MEMORY_DMABUF:
      /* v4l2 dmabuf memory are not share with downstream */
      g_assert_not_reached ();
      break;
    default:
      GST_WARNING ("Unknown memory type %i", mem->group->buffer.memory);
      break;
  }
  return ret;
}

static gboolean
_v4l2mem_dispose (GstV4l2Memory * mem)
{
  GstV4l2Allocator *allocator = (GstV4l2Allocator *) mem->mem.allocator;
  GstV4l2MemoryGroup *group = mem->group;
  gboolean ret;

  if (group->mem[mem->plane]) {
    /* We may have a dmabuf, replace it with returned original memory */
    group->mem[mem->plane] = gst_memory_ref ((GstMemory *) mem);
    gst_v4l2_allocator_release (allocator, mem);
    ret = FALSE;
  } else {
    gst_object_ref (allocator);
    ret = TRUE;
  }

  return ret;
}

static inline GstV4l2Memory *
_v4l2mem_new (GstMemoryFlags flags, GstAllocator * allocator,
    GstMemory * parent, gsize maxsize, gsize align, gsize offset, gsize size,
    gint plane, gpointer data, int dmafd, GstV4l2MemoryGroup * group)
{
  GstV4l2Memory *mem;

  mem = g_slice_new0 (GstV4l2Memory);
  gst_memory_init (GST_MEMORY_CAST (mem),
      flags, allocator, parent, maxsize, align, offset, size);

  if (parent == NULL)
    mem->mem.mini_object.dispose =
        (GstMiniObjectDisposeFunction) _v4l2mem_dispose;

  mem->plane = plane;
  mem->data = data;
  mem->dmafd = dmafd;
  mem->group = group;

  return mem;
}

static GstV4l2Memory *
_v4l2mem_share (GstV4l2Memory * mem, gssize offset, gsize size)
{
  GstV4l2Memory *sub;
  GstMemory *parent;

  /* find the real parent */
  if ((parent = mem->mem.parent) == NULL)
    parent = (GstMemory *) mem;

  if (size == -1)
    size = mem->mem.size - offset;

  /* the shared memory is always readonly */
  sub = _v4l2mem_new (GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, mem->mem.allocator, parent,
      mem->mem.maxsize, mem->mem.align, offset, size, mem->plane, mem->data,
      -1, mem->group);

  return sub;
}

static gboolean
_v4l2mem_is_span (GstV4l2Memory * mem1, GstV4l2Memory * mem2, gsize * offset)
{
  if (offset)
    *offset = mem1->mem.offset - mem1->mem.parent->offset;

  /* and memory is contiguous */
  return mem1->mem.offset + mem1->mem.size == mem2->mem.offset;
}

gboolean
gst_is_v4l2_memory (GstMemory * mem)
{
  return gst_memory_is_type (mem, GST_V4L2_MEMORY_TYPE);
}

GQuark
gst_v4l2_memory_quark (void)
{
  static GQuark quark = 0;

  if (quark == 0)
    quark = g_quark_from_string ("GstV4l2Memory");

  return quark;
}


/*************************************/
/* GstV4l2MemoryGroup implementation */
/*************************************/

static void
gst_v4l2_memory_group_free (GstV4l2MemoryGroup * group)
{
  gint i;

  for (i = 0; i < group->n_mem; i++) {
    GstMemory *mem = group->mem[i];
    group->mem[i] = NULL;
    if (mem)
      gst_memory_unref (mem);
  }

  g_slice_free (GstV4l2MemoryGroup, group);
}

static GstV4l2MemoryGroup *
gst_v4l2_memory_group_new (GstV4l2Allocator * allocator, guint32 index)
{
  gint video_fd = allocator->video_fd;
  guint32 memory = allocator->memory;
  struct v4l2_format *format = &allocator->format;
  GstV4l2MemoryGroup *group;
  gsize img_size, buf_size;

  group = g_slice_new0 (GstV4l2MemoryGroup);

  group->buffer.type = format->type;
  group->buffer.index = index;
  group->buffer.memory = memory;

  if (V4L2_TYPE_IS_MULTIPLANAR (format->type)) {
    group->n_mem = group->buffer.length = format->fmt.pix_mp.num_planes;
    group->buffer.m.planes = group->planes;
  } else {
    group->n_mem = 1;
  }

  if (v4l2_ioctl (video_fd, VIDIOC_QUERYBUF, &group->buffer) < 0)
    goto querybuf_failed;

  if (group->buffer.index != index) {
    GST_ERROR_OBJECT (allocator, "Buffer index returned by VIDIOC_QUERYBUF "
        "didn't match, this indicate the presence of a bug in your driver or "
        "libv4l2");
    g_slice_free (GstV4l2MemoryGroup, group);
    return NULL;
  }

  /* Check that provided size matches the format we have negotiation. Failing
   * there usually means a driver of libv4l bug. */
  if (V4L2_TYPE_IS_MULTIPLANAR (allocator->type)) {
    gint i;

    for (i = 0; i < group->n_mem; i++) {
      img_size = allocator->format.fmt.pix_mp.plane_fmt[i].sizeimage;
      buf_size = group->planes[i].length;
      if (buf_size < img_size)
        goto buffer_too_short;
    }
  } else {
    img_size = allocator->format.fmt.pix.sizeimage;
    buf_size = group->buffer.length;
    if (buf_size < img_size)
      goto buffer_too_short;
  }

  /* We save non planar buffer information into the multi-planar plane array
   * to avoid duplicating the code later */
  if (!V4L2_TYPE_IS_MULTIPLANAR (format->type)) {
    group->planes[0].bytesused = group->buffer.bytesused;
    group->planes[0].length = group->buffer.length;
    g_assert (sizeof (group->planes[0].m) == sizeof (group->buffer.m));
    memcpy (&group->planes[0].m, &group->buffer.m, sizeof (group->buffer.m));
  }

  GST_LOG_OBJECT (allocator, "Got %s buffer", memory_type_to_str (memory));
  GST_LOG_OBJECT (allocator, "  index:     %u", group->buffer.index);
  GST_LOG_OBJECT (allocator, "  type:      %d", group->buffer.type);
  GST_LOG_OBJECT (allocator, "  flags:     %08x", group->buffer.flags);
  GST_LOG_OBJECT (allocator, "  field:     %d", group->buffer.field);
  GST_LOG_OBJECT (allocator, "  memory:    %d", group->buffer.memory);
  GST_LOG_OBJECT (allocator, "  planes:    %d", group->n_mem);

#ifndef GST_DISABLE_GST_DEBUG
  if (memory == V4L2_MEMORY_MMAP) {
    gint i;
    for (i = 0; i < group->n_mem; i++) {
      GST_LOG_OBJECT (allocator, "  [%u] bytesused: %u, length: %u", i,
          group->planes[i].bytesused, group->planes[i].length);
      GST_LOG_OBJECT (allocator, "  [%u] MMAP offset:  %u", i,
          group->planes[i].m.mem_offset);
    }
  }
#endif

  return group;

querybuf_failed:
  {
    GST_ERROR ("error querying buffer %d: %s", index, g_strerror (errno));
    goto failed;
  }
buffer_too_short:
  {
    GST_ERROR ("buffer size %" G_GSIZE_FORMAT
        " is smaller then negotiated size %" G_GSIZE_FORMAT
        ", this is usually the result of a bug in the v4l2 driver or libv4l.",
        buf_size, img_size);
    goto failed;
  }
failed:
  gst_v4l2_memory_group_free (group);
  return NULL;
}


/*************************************/
/* GstV4lAllocator implementation    */
/*************************************/

static void
gst_v4l2_allocator_release (GstV4l2Allocator * allocator, GstV4l2Memory * mem)
{
  GstV4l2MemoryGroup *group = mem->group;

  GST_LOG_OBJECT (allocator, "plane %i of buffer %u released",
      mem->plane, group->buffer.index);

  switch (allocator->memory) {
    case V4L2_MEMORY_DMABUF:
      close (mem->dmafd);
      mem->dmafd = -1;
      break;
    case V4L2_MEMORY_USERPTR:
      mem->data = NULL;
      break;
    default:
      break;
  }

  /* When all memory are back, put the group back in the free queue */
  if (g_atomic_int_dec_and_test (&group->mems_allocated)) {
    GST_LOG_OBJECT (allocator, "buffer %u released", group->buffer.index);
    gst_atomic_queue_push (allocator->free_queue, group);
    g_signal_emit (allocator, gst_v4l2_allocator_signals[GROUP_RELEASED], 0);
  }

  /* Keep last, allocator may be freed after this call */
  g_object_unref (allocator);
}

static void
gst_v4l2_allocator_free (GstAllocator * gallocator, GstMemory * gmem)
{
  GstV4l2Allocator *allocator = (GstV4l2Allocator *) gallocator;
  GstV4l2Memory *mem = (GstV4l2Memory *) gmem;
  GstV4l2MemoryGroup *group = mem->group;

  /* Only free unparented memory */
  if (mem->mem.parent == NULL) {
    GST_LOG_OBJECT (allocator, "freeing plane %i of buffer %u",
        mem->plane, group->buffer.index);

    if (allocator->memory == V4L2_MEMORY_MMAP) {
      if (mem->data)
        v4l2_munmap (mem->data, group->planes[mem->plane].length);
    }

    /* This apply for both mmap with expbuf, and dmabuf imported memory */
    if (mem->dmafd >= 0)
      close (mem->dmafd);
  }

  g_slice_free (GstV4l2Memory, mem);
}

static void
gst_v4l2_allocator_dispose (GObject * obj)
{
  GstV4l2Allocator *allocator = (GstV4l2Allocator *) obj;
  gint i;

  GST_LOG_OBJECT (obj, "called");

  for (i = 0; i < allocator->count; i++) {
    GstV4l2MemoryGroup *group = allocator->groups[i];
    allocator->groups[i] = NULL;
    if (group)
      gst_v4l2_memory_group_free (group);
  }

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_v4l2_allocator_finalize (GObject * obj)
{
  GstV4l2Allocator *allocator = (GstV4l2Allocator *) obj;

  GST_LOG_OBJECT (obj, "called");

  v4l2_close (allocator->video_fd);
  gst_atomic_queue_unref (allocator->free_queue);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_v4l2_allocator_class_init (GstV4l2AllocatorClass * klass)
{
  GObjectClass *object_class;
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;
  object_class = (GObjectClass *) klass;

  allocator_class->alloc = NULL;
  allocator_class->free = gst_v4l2_allocator_free;

  object_class->dispose = gst_v4l2_allocator_dispose;
  object_class->finalize = gst_v4l2_allocator_finalize;

  gst_v4l2_allocator_signals[GROUP_RELEASED] = g_signal_new ("group-released",
      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 0);

  GST_DEBUG_CATEGORY_INIT (v4l2allocator_debug, "v4l2allocator", 0,
      "V4L2 Allocator");
}

static void
gst_v4l2_allocator_init (GstV4l2Allocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_V4L2_MEMORY_TYPE;
  alloc->mem_map = (GstMemoryMapFunction) _v4l2mem_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) _v4l2mem_unmap;
  alloc->mem_share = (GstMemoryShareFunction) _v4l2mem_share;
  alloc->mem_is_span = (GstMemoryIsSpanFunction) _v4l2mem_is_span;
  /* Use the default, fallback copy function */

  allocator->free_queue = gst_atomic_queue_new (VIDEO_MAX_FRAME);

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

#define GST_V4L2_ALLOCATOR_PROBE(obj,type) \
    gst_v4l2_allocator_probe ((obj), V4L2_MEMORY_ ## type, \
        GST_V4L2_ALLOCATOR_FLAG_ ## type ## _REQBUFS, \
        GST_V4L2_ALLOCATOR_FLAG_ ## type ## _CREATE_BUFS)
static guint32
gst_v4l2_allocator_probe (GstV4l2Allocator * allocator, guint32 memory,
    guint32 breq_flag, guint32 bcreate_flag)
{
  struct v4l2_requestbuffers breq = { 0 };
  guint32 flags = 0;

  breq.type = allocator->type;
  breq.count = 0;
  breq.memory = memory;

  if (v4l2_ioctl (allocator->video_fd, VIDIOC_REQBUFS, &breq) == 0) {
    struct v4l2_create_buffers bcreate = { 0 };

    flags |= breq_flag;

    bcreate.memory = memory;
    bcreate.format = allocator->format;

    if ((v4l2_ioctl (allocator->video_fd, VIDIOC_CREATE_BUFS, &bcreate) == 0))
      flags |= bcreate_flag;
  }

  return flags;
}

static GstV4l2MemoryGroup *
gst_v4l2_allocator_create_buf (GstV4l2Allocator * allocator)
{
  struct v4l2_create_buffers bcreate = { 0 };
  GstV4l2MemoryGroup *group = NULL;

  GST_OBJECT_LOCK (allocator);

  if (!g_atomic_int_get (&allocator->active))
    goto done;

  bcreate.memory = allocator->memory;
  bcreate.format = allocator->format;
  bcreate.count = 1;

  if (!allocator->can_allocate)
    goto done;

  if (v4l2_ioctl (allocator->video_fd, VIDIOC_CREATE_BUFS, &bcreate) < 0)
    goto create_bufs_failed;

  if (allocator->groups[bcreate.index] != NULL)
    goto create_bufs_bug;

  group = gst_v4l2_memory_group_new (allocator, bcreate.index);

  if (group) {
    allocator->groups[bcreate.index] = group;
    allocator->count++;
  }

done:
  GST_OBJECT_UNLOCK (allocator);
  return group;

create_bufs_failed:
  {
    GST_WARNING_OBJECT (allocator, "error creating a new buffer: %s",
        g_strerror (errno));
    goto done;
  }
create_bufs_bug:
  {
    GST_ERROR_OBJECT (allocator, "created buffer has already used buffer "
        "index %i, this means there is an bug in your driver or libv4l2",
        bcreate.index);
    goto done;
  }
}

static GstV4l2MemoryGroup *
gst_v4l2_allocator_alloc (GstV4l2Allocator * allocator)
{
  GstV4l2MemoryGroup *group;

  if (!g_atomic_int_get (&allocator->active))
    return NULL;

  group = gst_atomic_queue_pop (allocator->free_queue);

  if (group == NULL) {
    if (allocator->can_allocate) {
      group = gst_v4l2_allocator_create_buf (allocator);

      /* Don't hammer on CREATE_BUFS */
      if (group == NULL)
        allocator->can_allocate = FALSE;
    }
  }

  return group;
}

static void
gst_v4l2_allocator_reset_size (GstV4l2Allocator * allocator,
    GstV4l2MemoryGroup * group)
{
  gsize size;
  gboolean imported = FALSE;

  switch (allocator->memory) {
    case V4L2_MEMORY_USERPTR:
    case V4L2_MEMORY_DMABUF:
      imported = TRUE;
      break;
  }

  if (V4L2_TYPE_IS_MULTIPLANAR (allocator->type)) {
    gint i;

    for (i = 0; i < group->n_mem; i++) {
      size = allocator->format.fmt.pix_mp.plane_fmt[i].sizeimage;

      if (imported)
        group->mem[i]->maxsize = size;

      gst_memory_resize (group->mem[i], 0, size);
    }

  } else {
    size = allocator->format.fmt.pix.sizeimage;

    if (imported)
      group->mem[0]->maxsize = size;

    gst_memory_resize (group->mem[0], 0, size);
  }
}

static void
_cleanup_failed_alloc (GstV4l2Allocator * allocator, GstV4l2MemoryGroup * group)
{
  if (group->mems_allocated > 0) {
    gint i;
    /* If one or more mmap worked, we need to unref the memory, otherwise
     * they will keep a ref on the allocator and leak it. This will put back
     * the group into the free_queue */
    for (i = 0; i < group->n_mem; i++)
      gst_memory_unref (group->mem[i]);
  } else {
    /* Otherwise, group has to be on free queue for _stop() to work */
    gst_atomic_queue_push (allocator->free_queue, group);
  }
}



GstV4l2Allocator *
gst_v4l2_allocator_new (GstObject * parent, gint video_fd,
    struct v4l2_format *format)
{
  GstV4l2Allocator *allocator;
  guint32 flags = 0;
  gchar *name, *parent_name;

  parent_name = gst_object_get_name (parent);
  name = g_strconcat (parent_name, ":allocator", NULL);
  g_free (parent_name);

  allocator = g_object_new (GST_TYPE_V4L2_ALLOCATOR, "name", name, NULL);
  g_free (name);

  /* Save everything */
  allocator->video_fd = v4l2_dup (video_fd);
  allocator->type = format->type;
  allocator->format = *format;

  flags |= GST_V4L2_ALLOCATOR_PROBE (allocator, MMAP);
  flags |= GST_V4L2_ALLOCATOR_PROBE (allocator, USERPTR);
  flags |= GST_V4L2_ALLOCATOR_PROBE (allocator, DMABUF);


  if (flags == 0) {
    /* Drivers not ported from videobuf to videbuf2 don't allow freeing buffers
     * using REQBUFS(0). This is a workaround to still support these drivers,
     * which are known to have MMAP support. */
    GST_WARNING_OBJECT (allocator, "Could not probe supported memory type, "
        "assuming MMAP is supported, this is expected for older drivers not "
        " yet ported to videobuf2 framework");
    flags = GST_V4L2_ALLOCATOR_FLAG_MMAP_REQBUFS;
  }

  GST_OBJECT_FLAG_SET (allocator, flags);

  return allocator;
}

guint
gst_v4l2_allocator_start (GstV4l2Allocator * allocator, guint32 count,
    guint32 memory)
{
  struct v4l2_requestbuffers breq = { count, allocator->type, memory };
  gboolean can_allocate;
  gint i;

  g_return_val_if_fail (count != 0, 0);

  GST_OBJECT_LOCK (allocator);

  if (g_atomic_int_get (&allocator->active))
    goto already_active;

  if (v4l2_ioctl (allocator->video_fd, VIDIOC_REQBUFS, &breq) < 0)
    goto reqbufs_failed;

  if (breq.count < 1)
    goto out_of_memory;

  switch (memory) {
    case V4L2_MEMORY_MMAP:
      can_allocate = GST_V4L2_ALLOCATOR_CAN_ALLOCATE (allocator, MMAP);
      break;
    case V4L2_MEMORY_USERPTR:
      can_allocate = GST_V4L2_ALLOCATOR_CAN_ALLOCATE (allocator, USERPTR);
      break;
    case V4L2_MEMORY_DMABUF:
      can_allocate = GST_V4L2_ALLOCATOR_CAN_ALLOCATE (allocator, DMABUF);
      break;
    default:
      can_allocate = FALSE;
      break;
  }

  GST_DEBUG_OBJECT (allocator, "allocated %u %s buffers out of %u requested",
      breq.count, memory_type_to_str (memory), count);

  allocator->can_allocate = can_allocate;
  allocator->count = breq.count;
  allocator->memory = memory;

  /* Create memory groups */
  for (i = 0; i < allocator->count; i++) {
    allocator->groups[i] = gst_v4l2_memory_group_new (allocator, i);
    if (allocator->groups[i] == NULL)
      goto error;

    gst_atomic_queue_push (allocator->free_queue, allocator->groups[i]);
  }

  g_atomic_int_set (&allocator->active, TRUE);

done:
  GST_OBJECT_UNLOCK (allocator);
  return breq.count;

already_active:
  {
    GST_ERROR_OBJECT (allocator, "allocator already active");
    goto error;
  }
reqbufs_failed:
  {
    GST_ERROR_OBJECT (allocator,
        "error requesting %d buffers: %s", count, g_strerror (errno));
    goto error;
  }
out_of_memory:
  {
    GST_ERROR_OBJECT (allocator, "Not enough memory to allocate buffers");
    goto error;
  }
error:
  {
    breq.count = 0;
    goto done;
  }
}

GstV4l2Return
gst_v4l2_allocator_stop (GstV4l2Allocator * allocator)
{
  struct v4l2_requestbuffers breq = { 0, allocator->type, allocator->memory };
  gint i = 0;
  GstV4l2Return ret = GST_V4L2_OK;

  GST_DEBUG_OBJECT (allocator, "stop allocator");

  GST_OBJECT_LOCK (allocator);

  if (!g_atomic_int_get (&allocator->active))
    goto done;

  if (gst_atomic_queue_length (allocator->free_queue) != allocator->count) {
    GST_DEBUG_OBJECT (allocator, "allocator is still in use");
    ret = GST_V4L2_BUSY;
    goto done;
  }

  while (gst_atomic_queue_pop (allocator->free_queue)) {
    /* nothing */
  };

  for (i = 0; i < allocator->count; i++) {
    GstV4l2MemoryGroup *group = allocator->groups[i];
    allocator->groups[i] = NULL;
    if (group)
      gst_v4l2_memory_group_free (group);
  }

  /* Not all drivers support rebufs(0), so warn only */
  if (v4l2_ioctl (allocator->video_fd, VIDIOC_REQBUFS, &breq) < 0)
    GST_WARNING_OBJECT (allocator,
        "error releasing buffers buffers: %s", g_strerror (errno));

  allocator->count = 0;

  g_atomic_int_set (&allocator->active, FALSE);

done:
  GST_OBJECT_UNLOCK (allocator);
  return ret;
}

GstV4l2MemoryGroup *
gst_v4l2_allocator_alloc_mmap (GstV4l2Allocator * allocator)
{
  GstV4l2MemoryGroup *group;
  gint i;

  g_return_val_if_fail (allocator->memory == V4L2_MEMORY_MMAP, NULL);

  group = gst_v4l2_allocator_alloc (allocator);

  if (group == NULL)
    return NULL;

  for (i = 0; i < group->n_mem; i++) {
    if (group->mem[i] == NULL) {
      gpointer data;
      data = v4l2_mmap (NULL, group->planes[i].length, PROT_READ | PROT_WRITE,
          MAP_SHARED, allocator->video_fd, group->planes[i].m.mem_offset);

      if (data == MAP_FAILED)
        goto mmap_failed;

      GST_LOG_OBJECT (allocator,
          "mmap buffer length %d, data offset %d, plane %d",
          group->planes[i].length, group->planes[i].data_offset, i);

      group->mem[i] = (GstMemory *) _v4l2mem_new (0, GST_ALLOCATOR (allocator),
          NULL, group->planes[i].length, 0, 0, group->planes[i].length, i,
          data, -1, group);
    } else {
      /* Take back the allocator reference */
      gst_object_ref (allocator);
    }

    group->mems_allocated++;
  }

  /* Ensure group size. Unlike GST, v4l2 have size (bytesused) initially set
   * to 0. As length might be bigger then the expected size exposed in the
   * format, we simply set bytesused initially and reset it here for
   * simplicity */
  gst_v4l2_allocator_reset_size (allocator, group);

  return group;

mmap_failed:
  {
    GST_ERROR_OBJECT (allocator, "Failed to mmap buffer: %s",
        g_strerror (errno));
    _cleanup_failed_alloc (allocator, group);
    return NULL;
  }
}

GstV4l2MemoryGroup *
gst_v4l2_allocator_alloc_dmabuf (GstV4l2Allocator * allocator,
    GstAllocator * dmabuf_allocator)
{
  GstV4l2MemoryGroup *group;
  gint i;

  g_return_val_if_fail (allocator->memory == V4L2_MEMORY_MMAP, NULL);

  group = gst_v4l2_allocator_alloc (allocator);

  if (group == NULL)
    return NULL;

  for (i = 0; i < group->n_mem; i++) {
    GstV4l2Memory *mem;
    GstMemory *dma_mem;
    gint dmafd;

    if (group->mem[i] == NULL) {
      struct v4l2_exportbuffer expbuf = { 0 };

      expbuf.type = allocator->type;
      expbuf.index = group->buffer.index;
      expbuf.plane = i;
      expbuf.flags = O_CLOEXEC | O_RDWR;

      if (v4l2_ioctl (allocator->video_fd, VIDIOC_EXPBUF, &expbuf) < 0)
        goto expbuf_failed;

      GST_LOG_OBJECT (allocator, "exported DMABUF as fd %i plane %d",
          expbuf.fd, i);

      group->mem[i] = (GstMemory *) _v4l2mem_new (0, GST_ALLOCATOR (allocator),
          NULL, group->planes[i].length, 0, 0, group->planes[i].length, i,
          NULL, expbuf.fd, group);
    } else {
      /* Take back the allocator reference */
      gst_object_ref (allocator);
    }

    g_assert (gst_is_v4l2_memory (group->mem[i]));
    mem = (GstV4l2Memory *) group->mem[i];

    if ((dmafd = dup (mem->dmafd)) < 0)
      goto dup_failed;

    dma_mem = gst_dmabuf_allocator_alloc (dmabuf_allocator, dmafd,
        mem->mem.maxsize);

    gst_mini_object_set_qdata (GST_MINI_OBJECT (dma_mem),
        GST_V4L2_MEMORY_QUARK, mem, (GDestroyNotify) gst_memory_unref);

    group->mem[i] = dma_mem;
    group->mems_allocated++;
  }

  gst_v4l2_allocator_reset_size (allocator, group);

  return group;

expbuf_failed:
  {
    GST_ERROR_OBJECT (allocator, "Failed to export DMABUF: %s",
        g_strerror (errno));
    goto cleanup;
  }
dup_failed:
  {
    GST_ERROR_OBJECT (allocator, "Failed to dup DMABUF descriptor: %s",
        g_strerror (errno));
    goto cleanup;
  }
cleanup:
  {
    _cleanup_failed_alloc (allocator, group);
    return NULL;
  }
}

static void
gst_v4l2_allocator_clear_dmabufin (GstV4l2Allocator * allocator,
    GstV4l2MemoryGroup * group)
{
  GstV4l2Memory *mem;
  gint i;

  g_return_if_fail (allocator->memory == V4L2_MEMORY_DMABUF);

  for (i = 0; i < group->n_mem; i++) {

    mem = (GstV4l2Memory *) group->mem[i];

    GST_LOG_OBJECT (allocator, "clearing DMABUF import, fd %i plane %d",
        mem->dmafd, i);

    if (mem->dmafd >= 0)
      close (mem->dmafd);

    /* Update memory */
    mem->mem.maxsize = 0;
    mem->mem.offset = 0;
    mem->mem.size = 0;
    mem->dmafd = -1;

    /* Update v4l2 structure */
    group->planes[i].length = 0;
    group->planes[i].bytesused = 0;
    group->planes[i].m.fd = -1;
    group->planes[i].data_offset = 0;
  }

  if (!V4L2_TYPE_IS_MULTIPLANAR (allocator->type)) {
    group->buffer.bytesused = 0;
    group->buffer.length = 0;
    group->buffer.m.fd = -1;
  }
}

GstV4l2MemoryGroup *
gst_v4l2_allocator_alloc_dmabufin (GstV4l2Allocator * allocator)
{
  GstV4l2MemoryGroup *group;
  gint i;

  g_return_val_if_fail (allocator->memory == V4L2_MEMORY_DMABUF, NULL);

  group = gst_v4l2_allocator_alloc (allocator);

  if (group == NULL)
    return NULL;

  GST_LOG_OBJECT (allocator, "allocating empty DMABUF import group");

  for (i = 0; i < group->n_mem; i++) {
    if (group->mem[i] == NULL) {
      group->mem[i] = (GstMemory *) _v4l2mem_new (0, GST_ALLOCATOR (allocator),
          NULL, 0, 0, 0, 0, i, NULL, -1, group);
    } else {
      /* Take back the allocator reference */
      gst_object_ref (allocator);
    }

    group->mems_allocated++;
  }

  gst_v4l2_allocator_clear_dmabufin (allocator, group);

  return group;
}

static void
gst_v4l2_allocator_clear_userptr (GstV4l2Allocator * allocator,
    GstV4l2MemoryGroup * group)
{
  GstV4l2Memory *mem;
  gint i;

  g_return_if_fail (allocator->memory == V4L2_MEMORY_USERPTR);

  for (i = 0; i < group->n_mem; i++) {
    mem = (GstV4l2Memory *) group->mem[i];

    GST_LOG_OBJECT (allocator, "clearing USERPTR %p plane %d size %"
        G_GSIZE_FORMAT, mem->data, i, mem->mem.size);

    mem->mem.maxsize = 0;
    mem->mem.size = 0;
    mem->data = NULL;

    group->planes[i].length = 0;
    group->planes[i].bytesused = 0;
    group->planes[i].m.userptr = 0;
  }

  if (!V4L2_TYPE_IS_MULTIPLANAR (allocator->type)) {
    group->buffer.bytesused = 0;
    group->buffer.length = 0;
    group->buffer.m.userptr = 0;
  }
}

GstV4l2MemoryGroup *
gst_v4l2_allocator_alloc_userptr (GstV4l2Allocator * allocator)
{
  GstV4l2MemoryGroup *group;
  gint i;

  g_return_val_if_fail (allocator->memory == V4L2_MEMORY_USERPTR, NULL);

  group = gst_v4l2_allocator_alloc (allocator);

  if (group == NULL)
    return NULL;

  GST_LOG_OBJECT (allocator, "allocating empty USERPTR group");

  for (i = 0; i < group->n_mem; i++) {

    if (group->mem[i] == NULL) {
      group->mem[i] = (GstMemory *) _v4l2mem_new (0, GST_ALLOCATOR (allocator),
          NULL, 0, 0, 0, 0, i, NULL, -1, group);
    } else {
      /* Take back the allocator reference */
      gst_object_ref (allocator);
    }

    group->mems_allocated++;
  }

  gst_v4l2_allocator_clear_userptr (allocator, group);

  return group;
}

gboolean
gst_v4l2_allocator_import_dmabuf (GstV4l2Allocator * allocator,
    GstV4l2MemoryGroup * group, gint n_mem, GstMemory ** dma_mem)
{
  GstV4l2Memory *mem;
  gint i;

  g_return_val_if_fail (allocator->memory == V4L2_MEMORY_DMABUF, FALSE);

  if (group->n_mem != n_mem)
    goto n_mem_missmatch;

  for (i = 0; i < group->n_mem; i++) {
    gint dmafd;
    gsize size, offset, maxsize;

    if (!gst_is_dmabuf_memory (dma_mem[i]))
      goto not_dmabuf;

    size = gst_memory_get_sizes (dma_mem[i], &offset, &maxsize);

    if ((dmafd = dup (gst_dmabuf_memory_get_fd (dma_mem[i]))) < 0)
      goto dup_failed;

    GST_LOG_OBJECT (allocator, "imported DMABUF as fd %i plane %d", dmafd, i);

    mem = (GstV4l2Memory *) group->mem[i];

    /* Update memory */
    mem->mem.maxsize = maxsize;
    mem->mem.offset = offset;
    mem->mem.size = size;
    mem->dmafd = dmafd;

    /* Update v4l2 structure */
    group->planes[i].length = maxsize;
    group->planes[i].bytesused = size;
    group->planes[i].m.fd = dmafd;
    group->planes[i].data_offset = offset;
  }

  /* Copy into buffer structure if not using planes */
  if (!V4L2_TYPE_IS_MULTIPLANAR (allocator->type)) {
    group->buffer.bytesused = group->planes[0].bytesused;
    group->buffer.length = group->planes[0].length;
    group->buffer.m.fd = group->planes[0].m.userptr;
  } else {
    group->buffer.length = group->n_mem;
  }

  return TRUE;

n_mem_missmatch:
  {
    GST_ERROR_OBJECT (allocator, "Got %i dmabuf but needed %i", n_mem,
        group->n_mem);
    return FALSE;
  }
not_dmabuf:
  {
    GST_ERROR_OBJECT (allocator, "Memory %i is not of DMABUF", i);
    return FALSE;
  }
dup_failed:
  {
    GST_ERROR_OBJECT (allocator, "Failed to dup DMABUF descriptor: %s",
        g_strerror (errno));
    return FALSE;
  }
}

gboolean
gst_v4l2_allocator_import_userptr (GstV4l2Allocator * allocator,
    GstV4l2MemoryGroup * group, gsize img_size, int n_planes,
    gpointer * data, gsize * size)
{
  GstV4l2Memory *mem;
  gint i;

  g_return_val_if_fail (allocator->memory == V4L2_MEMORY_USERPTR, FALSE);

  /* TODO Support passing N plane from 1 memory to MPLANE v4l2 format */
  if (V4L2_TYPE_IS_MULTIPLANAR (allocator->type) && n_planes != group->n_mem)
    goto n_mem_missmatch;

  for (i = 0; i < group->n_mem; i++) {
    gsize maxsize, psize;

    if (V4L2_TYPE_IS_MULTIPLANAR (allocator->type)) {
      struct v4l2_pix_format_mplane *pix = &allocator->format.fmt.pix_mp;
      maxsize = pix->plane_fmt[i].sizeimage;
      psize = size[i];
    } else {
      maxsize = allocator->format.fmt.pix.sizeimage;
      psize = img_size;
    }

    g_assert (psize <= img_size);

    GST_LOG_OBJECT (allocator, "imported USERPTR %p plane %d size %"
        G_GSIZE_FORMAT, data[i], i, psize);

    mem = (GstV4l2Memory *) group->mem[i];

    mem->mem.maxsize = maxsize;
    mem->mem.size = psize;
    mem->data = data[i];

    group->planes[i].length = maxsize;
    group->planes[i].bytesused = psize;
    group->planes[i].m.userptr = (unsigned long) data[i];
    group->planes[i].data_offset = 0;
  }

  /* Copy into buffer structure if not using planes */
  if (!V4L2_TYPE_IS_MULTIPLANAR (allocator->type)) {
    group->buffer.bytesused = group->planes[0].bytesused;
    group->buffer.length = group->planes[0].length;
    group->buffer.m.userptr = group->planes[0].m.userptr;
  } else {
    group->buffer.length = group->n_mem;
  }

  return TRUE;

n_mem_missmatch:
  {
    GST_ERROR_OBJECT (allocator, "Got %i userptr plane while driver need %i",
        n_planes, group->n_mem);
    return FALSE;
  }
}

void
gst_v4l2_allocator_flush (GstV4l2Allocator * allocator)
{
  gint i;

  GST_OBJECT_LOCK (allocator);

  if (!g_atomic_int_get (&allocator->active))
    goto done;

  for (i = 0; i < allocator->count; i++) {
    GstV4l2MemoryGroup *group = allocator->groups[i];
    gint n;

    if (IS_QUEUED (group->buffer)) {
      UNSET_QUEUED (group->buffer);

      gst_v4l2_allocator_reset_group (allocator, group);

      for (n = 0; n < group->n_mem; n++)
        gst_memory_unref (group->mem[n]);
    }
  }

done:
  GST_OBJECT_UNLOCK (allocator);
}

gboolean
gst_v4l2_allocator_qbuf (GstV4l2Allocator * allocator,
    GstV4l2MemoryGroup * group)
{
  gboolean ret = TRUE;
  gint i;

  g_return_val_if_fail (g_atomic_int_get (&allocator->active), FALSE);

  /* update sizes */
  if (V4L2_TYPE_IS_MULTIPLANAR (allocator->type)) {
    for (i = 0; i < group->n_mem; i++)
      group->planes[i].bytesused =
          gst_memory_get_sizes (group->mem[i], NULL, NULL);
  } else {
    group->buffer.bytesused = gst_memory_get_sizes (group->mem[0], NULL, NULL);
  }

  /* Ensure the memory will stay around and is RO */
  for (i = 0; i < group->n_mem; i++)
    gst_memory_ref (group->mem[i]);

  if (v4l2_ioctl (allocator->video_fd, VIDIOC_QBUF, &group->buffer) < 0) {
    GST_ERROR_OBJECT (allocator, "failed queueing buffer %i: %s",
        group->buffer.index, g_strerror (errno));

    /* Release the memory, possibly making it RW again */
    for (i = 0; i < group->n_mem; i++)
      gst_memory_unref (group->mem[i]);

    ret = FALSE;
    if (IS_QUEUED (group->buffer)) {
      GST_DEBUG_OBJECT (allocator,
          "driver pretends buffer is queued even if queue failed");
      UNSET_QUEUED (group->buffer);
    }
    goto done;
  }

  GST_LOG_OBJECT (allocator, "queued buffer %i (flags 0x%X)",
      group->buffer.index, group->buffer.flags);

  if (!IS_QUEUED (group->buffer)) {
    GST_DEBUG_OBJECT (allocator,
        "driver pretends buffer is not queued even if queue succeeded");
    SET_QUEUED (group->buffer);
  }

done:
  return ret;
}

GstFlowReturn
gst_v4l2_allocator_dqbuf (GstV4l2Allocator * allocator,
    GstV4l2MemoryGroup ** group_out)
{
  struct v4l2_buffer buffer = { 0 };
  struct v4l2_plane planes[VIDEO_MAX_PLANES] = { {0} };
  gint i;

  GstV4l2MemoryGroup *group = NULL;

  g_return_val_if_fail (g_atomic_int_get (&allocator->active), GST_FLOW_ERROR);

  buffer.type = allocator->type;
  buffer.memory = allocator->memory;

  if (V4L2_TYPE_IS_MULTIPLANAR (allocator->type)) {
    buffer.length = allocator->format.fmt.pix_mp.num_planes;
    buffer.m.planes = planes;
  }

  if (v4l2_ioctl (allocator->video_fd, VIDIOC_DQBUF, &buffer) < 0)
    goto error;

  group = allocator->groups[buffer.index];

  if (!IS_QUEUED (group->buffer)) {
    GST_ERROR_OBJECT (allocator,
        "buffer %i was not queued, this indicate a driver bug.", buffer.index);
    return GST_FLOW_ERROR;
  }

  group->buffer = buffer;

  GST_LOG_OBJECT (allocator, "dequeued buffer %i (flags 0x%X)", buffer.index,
      buffer.flags);

  if (IS_QUEUED (group->buffer)) {
    GST_DEBUG_OBJECT (allocator,
        "driver pretends buffer is queued even if dequeue succeeded");
    UNSET_QUEUED (group->buffer);
  }

  if (V4L2_TYPE_IS_MULTIPLANAR (allocator->type)) {
    group->buffer.m.planes = group->planes;
    memcpy (group->planes, buffer.m.planes, sizeof (planes));
  } else {
    group->planes[0].bytesused = group->buffer.bytesused;
    group->planes[0].length = group->buffer.length;
    g_assert (sizeof (group->planes[0].m) == sizeof (group->buffer.m));
    memcpy (&group->planes[0].m, &group->buffer.m, sizeof (group->buffer.m));
  }

  /* And update memory size */
  if (V4L2_TYPE_IS_OUTPUT (allocator->type)) {
    gst_v4l2_allocator_reset_size (allocator, group);
  } else {
    /* for capture, simply read the size */
    for (i = 0; i < group->n_mem; i++) {
      if (G_LIKELY (group->planes[i].bytesused <= group->mem[i]->maxsize))
        gst_memory_resize (group->mem[i], 0, group->planes[i].bytesused);
      else {
        GST_WARNING_OBJECT (allocator,
            "v4l2 provided buffer that is too big for the memory it was "
            "writing into.  v4l2 claims %" G_GUINT32_FORMAT " bytes used but "
            "memory is only %" G_GSIZE_FORMAT "B.  This is probably a driver "
            "bug.", group->planes[i].bytesused, group->mem[i]->maxsize);
        gst_memory_resize (group->mem[i], 0, group->mem[i]->maxsize);
      }
    }
  }

  /* Release the memory, possibly making it RW again */
  for (i = 0; i < group->n_mem; i++)
    gst_memory_unref (group->mem[i]);

  *group_out = group;
  return GST_FLOW_OK;

error:
  if (errno == EPIPE) {
    GST_DEBUG_OBJECT (allocator, "broken pipe signals last buffer");
    return GST_FLOW_EOS;
  }

  GST_ERROR_OBJECT (allocator, "failed dequeuing a %s buffer: %s",
      memory_type_to_str (allocator->memory), g_strerror (errno));

  switch (errno) {
    case EAGAIN:
      GST_WARNING_OBJECT (allocator,
          "Non-blocking I/O has been selected using O_NONBLOCK and"
          " no buffer was in the outgoing queue.");
      break;
    case EINVAL:
      GST_ERROR_OBJECT (allocator,
          "The buffer type is not supported, or the index is out of bounds, "
          "or no buffers have been allocated yet, or the userptr "
          "or length are invalid.");
      break;
    case ENOMEM:
      GST_ERROR_OBJECT (allocator,
          "insufficient memory to enqueue a user pointer buffer");
      break;
    case EIO:
      GST_INFO_OBJECT (allocator,
          "VIDIOC_DQBUF failed due to an internal error."
          " Can also indicate temporary problems like signal loss."
          " Note the driver might dequeue an (empty) buffer despite"
          " returning an error, or even stop capturing.");
      /* have we de-queued a buffer ? */
      if (!IS_QUEUED (buffer)) {
        GST_DEBUG_OBJECT (allocator, "reenqueueing buffer");
        /* FIXME ... should we do something here? */
      }
      break;
    case EINTR:
      GST_WARNING_OBJECT (allocator, "could not sync on a buffer on device");
      break;
    default:
      GST_WARNING_OBJECT (allocator,
          "Grabbing frame got interrupted unexpectedly. %d: %s.", errno,
          g_strerror (errno));
      break;
  }

  return GST_FLOW_ERROR;
}

void
gst_v4l2_allocator_reset_group (GstV4l2Allocator * allocator,
    GstV4l2MemoryGroup * group)
{
  switch (allocator->memory) {
    case V4L2_MEMORY_USERPTR:
      gst_v4l2_allocator_clear_userptr (allocator, group);
      break;
    case V4L2_MEMORY_DMABUF:
      gst_v4l2_allocator_clear_dmabufin (allocator, group);
      break;
    case V4L2_MEMORY_MMAP:
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  gst_v4l2_allocator_reset_size (allocator, group);
}
