/*
 * Copyright (C) 2019, Collabora Ltd.
 *   Author: George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstomxallocator.h"
#include <gst/allocators/gstdmabuf.h>

GST_DEBUG_CATEGORY_STATIC (gst_omx_allocator_debug_category);
#define GST_CAT_DEFAULT gst_omx_allocator_debug_category

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_allocator_debug_category, "omxallocator", 0, \
      "debug category for gst-omx allocator class");

G_DEFINE_TYPE_WITH_CODE (GstOMXAllocator, gst_omx_allocator, GST_TYPE_ALLOCATOR,
    DEBUG_INIT);

enum
{
  SIG_OMXBUF_RELEASED,
  SIG_FOREIGN_MEM_RELEASED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* Custom allocator for memory associated with OpenMAX buffers
 *
 * The main purpose of this allocator is to track memory that is associated
 * with OpenMAX buffers, so that we know when the buffers can be released
 * back to OpenMAX.
 *
 * This allocator looks and behaves more like a buffer pool. It allocates
 * the memory objects before starting and sets a miniobject dispose function
 * on them, which allows them to return when their last ref count is dropped.
 *
 * The type of memory that this allocator manages is GstOMXMemory. However, it
 * is possible to manage a different type of memory, in which case the
 * GstOMXMemory object is used only internally. There are two supported cases:
 * - Allocate memory from the dmabuf allocator
 * - Take memory that was allocated externally and manage it here
 *
 * In both cases, this allocator will replace the miniobject dispose function
 * of these memory objects, so if they were acquired from here, they will also
 * return here on their last unref.
 *
 * The caller initially needs to configure how many memory objects will be
 * managed here by calling configure(). After that it needs to call
 * set_active(TRUE) and finally allocate() for each memory. Allocation is done
 * like this to facilitate calling allocate() from the alloc() function of
 * the buffer pool for each OMX buffer on the port.
 *
 * After the allocator has been activated and all buffers have been allocated,
 * the acquire() method can be called to retrieve a memory object. acquire() can
 * be given an OMX buffer index or pointer to locate and return the memory
 * object that corresponds to this OMX buffer. If the buffer is already
 * acquired, this will result in a GST_FLOW_ERROR.
 *
 * When the last reference count is dropped on a memory that was acquired from
 * here, its dispose function will ref it again and allow it to be acquired
 * again. In addition, the omxbuf-released signal is fired to let the caller
 * know that it can return this OMX buffer to the port, as it is no longer
 * used outside this allocator.
 */

/******************/
/** GstOMXMemory **/
/******************/

#define GST_OMX_MEMORY_TYPE "openmax"

GQuark
gst_omx_memory_quark (void)
{
  static GQuark quark = 0;

  if (quark == 0)
    quark = g_quark_from_static_string ("GstOMXMemory");

  return quark;
}

static GstOMXMemory *
gst_omx_memory_new (GstOMXAllocator * allocator, GstOMXBuffer * omx_buf,
    GstMemoryFlags flags, GstMemory * parent, gssize offset, gssize size)
{
  GstOMXMemory *mem;
  gint align;
  gsize maxsize;

  /* GStreamer uses a bitmask for the alignment while
   * OMX uses the alignment itself. So we have to convert
   * here */
  align = allocator->port->port_def.nBufferAlignment;
  if (align > 0)
    align -= 1;
  if (((align + 1) & align) != 0) {
    GST_WARNING ("Invalid alignment that is not a power of two: %u",
        (guint) allocator->port->port_def.nBufferAlignment);
    align = 0;
  }

  maxsize = omx_buf->omx_buf->nAllocLen;

  if (size == -1) {
    size = maxsize - offset;
  }

  mem = g_slice_new0 (GstOMXMemory);
  gst_memory_init (GST_MEMORY_CAST (mem), flags, (GstAllocator *) allocator,
      parent, maxsize, align, offset, size);

  mem->buf = omx_buf;

  return mem;
}

static gpointer
gst_omx_memory_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  GstOMXMemory *omem = (GstOMXMemory *) mem;

  /* if we are using foreign_mem, the GstOMXMemory should never appear
   * anywhere outside this allocator, therefore it should never be mapped */
  g_return_val_if_fail (!omem->foreign_mem, NULL);

  return omem->buf->omx_buf->pBuffer;
}

static void
gst_omx_memory_unmap (GstMemory * mem)
{
}

static GstMemory *
gst_omx_memory_share (GstMemory * mem, gssize offset, gssize size)
{
  GstOMXMemory *omem = (GstOMXMemory *) mem;
  GstOMXMemory *sub;
  GstMemory *parent;

  /* find the real parent */
  if ((parent = mem->parent) == NULL)
    parent = mem;

  if (size == -1)
    size = mem->size - offset;

  /* the shared memory is always readonly */
  sub = gst_omx_memory_new ((GstOMXAllocator *) mem->allocator, omem->buf,
      GST_MINI_OBJECT_FLAGS (parent) | GST_MINI_OBJECT_FLAG_LOCK_READONLY,
      parent, offset, size);

  return (GstMemory *) sub;
}

GstOMXBuffer *
gst_omx_memory_get_omx_buf (GstMemory * mem)
{
  GstOMXMemory *omx_mem;

  if (GST_IS_OMX_ALLOCATOR (mem->allocator))
    omx_mem = (GstOMXMemory *) mem;
  else
    omx_mem = gst_mini_object_get_qdata (GST_MINI_OBJECT (mem),
        GST_OMX_MEMORY_QUARK);

  if (!omx_mem)
    return NULL;

  return omx_mem->buf;
}

/*********************/
/** GstOMXAllocator **/
/*********************/

static void
gst_omx_allocator_init (GstOMXAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_OMX_MEMORY_TYPE;

  alloc->mem_map = gst_omx_memory_map;
  alloc->mem_unmap = gst_omx_memory_unmap;
  alloc->mem_share = gst_omx_memory_share;
  /* default copy & is_span */

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);

  g_mutex_init (&allocator->lock);
  g_cond_init (&allocator->cond);
}

GstOMXAllocator *
gst_omx_allocator_new (GstOMXComponent * component, GstOMXPort * port)
{
  GstOMXAllocator *allocator;

  allocator = g_object_new (gst_omx_allocator_get_type (), NULL);
  allocator->component = gst_omx_component_ref (component);
  allocator->port = port;

  return allocator;
}

static void
gst_omx_allocator_finalize (GObject * object)
{
  GstOMXAllocator *allocator = GST_OMX_ALLOCATOR (object);

  gst_omx_component_unref (allocator->component);
  g_mutex_clear (&allocator->lock);
  g_cond_clear (&allocator->cond);

  G_OBJECT_CLASS (gst_omx_allocator_parent_class)->finalize (object);
}

gboolean
gst_omx_allocator_configure (GstOMXAllocator * allocator, guint count,
    GstOMXAllocatorForeignMemMode mode)
{
  /* check if already configured */
  if (allocator->n_memories > 0)
    return FALSE;

  allocator->n_memories = count;
  allocator->foreign_mode = mode;
  if (mode == GST_OMX_ALLOCATOR_FOREIGN_MEM_DMABUF)
    allocator->foreign_allocator = gst_dmabuf_allocator_new ();

  return TRUE;
}

/* must be protected with allocator->lock */
static void
gst_omx_allocator_dealloc (GstOMXAllocator * allocator)
{
  /* might be called more than once */
  if (!allocator->memories)
    return;

  /* return foreign memory back to whoever lended it to us.
   * the signal handler is expected to increase the ref count of foreign_mem */
  if (allocator->foreign_mode == GST_OMX_ALLOCATOR_FOREIGN_MEM_OTHER_POOL) {
    gint i;
    GstOMXMemory *m;

    for (i = 0; i < allocator->memories->len; i++) {
      m = g_ptr_array_index (allocator->memories, i);

      /* this should not happen, but let's not crash for this */
      if (!m->foreign_mem) {
        GST_WARNING_OBJECT (allocator, "no foreign_mem to release");
        continue;
      }

      /* restore the original dispose function */
      GST_MINI_OBJECT_CAST (m->foreign_mem)->dispose =
          (GstMiniObjectDisposeFunction) m->foreign_dispose;

      g_signal_emit (allocator, signals[SIG_FOREIGN_MEM_RELEASED], 0, i,
          m->foreign_mem);
    }
  }

  g_ptr_array_foreach (allocator->memories, (GFunc) gst_memory_unref, NULL);
  g_ptr_array_free (allocator->memories, TRUE);
  allocator->memories = NULL;
  allocator->n_memories = 0;
  allocator->foreign_mode = GST_OMX_ALLOCATOR_FOREIGN_MEM_NONE;
  if (allocator->foreign_allocator) {
    g_object_unref (allocator->foreign_allocator);
    allocator->foreign_allocator = NULL;
  }

  g_cond_broadcast (&allocator->cond);
}

gboolean
gst_omx_allocator_set_active (GstOMXAllocator * allocator, gboolean active)
{
  gboolean changed = FALSE;

  /* on activation, _configure() must be called first */
  g_return_val_if_fail (!active || allocator->n_memories > 0, FALSE);

  g_mutex_lock (&allocator->lock);

  if (allocator->active != active)
    changed = TRUE;

  if (changed) {
    if (active) {
      allocator->memories = g_ptr_array_sized_new (allocator->n_memories);
      g_ptr_array_set_size (allocator->memories, allocator->n_memories);
    } else {
      if (g_atomic_int_get (&allocator->n_outstanding) == 0)
        gst_omx_allocator_dealloc (allocator);
    }
  }

  allocator->active = active;
  g_mutex_unlock (&allocator->lock);

  return changed;
}

void
gst_omx_allocator_wait_inactive (GstOMXAllocator * allocator)
{
  g_mutex_lock (&allocator->lock);
  while (allocator->memories)
    g_cond_wait (&allocator->cond, &allocator->lock);
  g_mutex_unlock (&allocator->lock);
}

static inline void
dec_outstanding (GstOMXAllocator * allocator)
{
  if (g_atomic_int_dec_and_test (&allocator->n_outstanding)) {
    /* keep a ref to the allocator because _dealloc() will free
     * all the memories and the memories might be the only thing holding
     * a reference to the allocator; we need to keep it alive until the
     * end of this function call */
    g_object_ref (allocator);

    /* take the lock so that _set_active() is not run concurrently */
    g_mutex_lock (&allocator->lock);

    /* now that we have the lock, check if we have been de-activated with
     * outstanding buffers */
    if (!allocator->active)
      gst_omx_allocator_dealloc (allocator);

    g_mutex_unlock (&allocator->lock);
    g_object_unref (allocator);
  }
}

GstFlowReturn
gst_omx_allocator_acquire (GstOMXAllocator * allocator, GstMemory ** memory,
    gint index, GstOMXBuffer * omx_buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstOMXMemory *omx_mem = NULL;

  /* ensure memories are not going to disappear concurrently */
  g_atomic_int_inc (&allocator->n_outstanding);

  if (!allocator->active) {
    ret = GST_FLOW_FLUSHING;
    goto beach;
  }

  if (index >= 0 && index < allocator->n_memories)
    omx_mem = g_ptr_array_index (allocator->memories, index);
  else if (omx_buf) {
    for (index = 0; index < allocator->n_memories; index++) {
      omx_mem = g_ptr_array_index (allocator->memories, index);
      if (omx_mem->buf == omx_buf)
        break;
    }
  }

  if (G_UNLIKELY (!omx_mem || index >= allocator->n_memories)) {
    GST_ERROR_OBJECT (allocator, "Failed to find OMX memory");
    ret = GST_FLOW_ERROR;
    goto beach;
  }

  if (G_UNLIKELY (omx_mem->buf->used)) {
    GST_ERROR_OBJECT (allocator,
        "Trying to acquire a buffer that is being used by the OMX port");
    ret = GST_FLOW_ERROR;
    goto beach;
  }

  omx_mem->acquired = TRUE;

  if (omx_mem->foreign_mem)
    *memory = omx_mem->foreign_mem;
  else
    *memory = GST_MEMORY_CAST (omx_mem);

beach:
  if (ret != GST_FLOW_OK)
    dec_outstanding (allocator);
  return ret;
}

/* installed as the GstMiniObject::dispose function of the acquired GstMemory */
static gboolean
gst_omx_allocator_memory_dispose (GstMemory * mem)
{
  GstOMXMemory *omx_mem;
  GstOMXAllocator *allocator;

  /* memory may be from our allocator, but
   * may as well be from the dmabuf allocator */
  if (GST_IS_OMX_ALLOCATOR (mem->allocator))
    omx_mem = (GstOMXMemory *) mem;
  else
    omx_mem = gst_mini_object_get_qdata (GST_MINI_OBJECT (mem),
        GST_OMX_MEMORY_QUARK);

  if (omx_mem->acquired) {
    /* keep the memory alive */
    gst_memory_ref (mem);

    omx_mem->acquired = FALSE;

    allocator = GST_OMX_ALLOCATOR (GST_MEMORY_CAST (omx_mem)->allocator);

    /* inform the upper layer that we are no longer using this GstOMXBuffer */
    g_signal_emit (allocator, signals[SIG_OMXBUF_RELEASED], 0, omx_mem->buf);

    dec_outstanding (allocator);

    /* be careful here, both the memory and the allocator
     * may have been free'd as part of the call to dec_outstanding() */

    return FALSE;
  }

  /* if the foreign memory had a dispose function, let that one decide
   * the fate of this memory. We are no longer going to be using it here */
  if (omx_mem->foreign_dispose)
    return omx_mem->foreign_dispose (GST_MINI_OBJECT_CAST (mem));

  return TRUE;
}

static inline void
install_mem_dispose (GstOMXMemory * mem)
{
  GstMemory *managed_mem = (GstMemory *) mem;

  if (mem->foreign_mem) {
    managed_mem = mem->foreign_mem;
    mem->foreign_dispose = GST_MINI_OBJECT_CAST (managed_mem)->dispose;
  }

  GST_MINI_OBJECT_CAST (managed_mem)->dispose =
      (GstMiniObjectDisposeFunction) gst_omx_allocator_memory_dispose;
}

/* the returned memory is transfer:none, ref still belongs to the allocator */
GstMemory *
gst_omx_allocator_allocate (GstOMXAllocator * allocator, gint index,
    GstMemory * foreign_mem)
{
  GstOMXMemory *mem;
  GstOMXBuffer *omx_buf;

  g_return_val_if_fail (allocator->port->buffers, NULL);
  g_return_val_if_fail (allocator->memories, NULL);
  g_return_val_if_fail (index >= 0 && index < allocator->n_memories, NULL);
  g_return_val_if_fail ((foreign_mem == NULL &&
          allocator->foreign_mode != GST_OMX_ALLOCATOR_FOREIGN_MEM_OTHER_POOL)
      || (foreign_mem != NULL
          && allocator->foreign_mode ==
          GST_OMX_ALLOCATOR_FOREIGN_MEM_OTHER_POOL), NULL);

  omx_buf = g_ptr_array_index (allocator->port->buffers, index);
  g_return_val_if_fail (omx_buf != NULL, NULL);

  mem = gst_omx_memory_new (allocator, omx_buf, 0, NULL, 0, -1);

  switch (allocator->foreign_mode) {
    case GST_OMX_ALLOCATOR_FOREIGN_MEM_NONE:
      install_mem_dispose (mem);
      break;
    case GST_OMX_ALLOCATOR_FOREIGN_MEM_DMABUF:
    {
      gint fd = GPOINTER_TO_INT (omx_buf->omx_buf->pBuffer);
      mem->foreign_mem =
          gst_dmabuf_allocator_alloc (allocator->foreign_allocator, fd,
          omx_buf->omx_buf->nAllocLen);
      gst_mini_object_set_qdata (GST_MINI_OBJECT (mem->foreign_mem),
          GST_OMX_MEMORY_QUARK, mem, NULL);
      install_mem_dispose (mem);
      break;
    }
    case GST_OMX_ALLOCATOR_FOREIGN_MEM_OTHER_POOL:
      mem->foreign_mem = foreign_mem;
      gst_mini_object_set_qdata (GST_MINI_OBJECT (mem->foreign_mem),
          GST_OMX_MEMORY_QUARK, mem, NULL);
      install_mem_dispose (mem);
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  g_ptr_array_index (allocator->memories, index) = mem;
  return mem->foreign_mem ? mem->foreign_mem : (GstMemory *) mem;
}

static void
gst_omx_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstOMXMemory *omem = (GstOMXMemory *) mem;

  g_warn_if_fail (!omem->acquired);

  if (omem->foreign_mem)
    gst_memory_unref (omem->foreign_mem);

  g_slice_free (GstOMXMemory, omem);
}

static void
gst_omx_allocator_class_init (GstOMXAllocatorClass * klass)
{
  GObjectClass *object_class;
  GstAllocatorClass *allocator_class;

  object_class = (GObjectClass *) klass;
  allocator_class = (GstAllocatorClass *) klass;

  object_class->finalize = gst_omx_allocator_finalize;
  allocator_class->alloc = NULL;
  allocator_class->free = gst_omx_allocator_free;

  signals[SIG_OMXBUF_RELEASED] = g_signal_new ("omxbuf-released",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);

  signals[SIG_FOREIGN_MEM_RELEASED] = g_signal_new ("foreign-mem-released",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_POINTER);
}
