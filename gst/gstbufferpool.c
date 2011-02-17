/* GStreamer
 * Copyright (C) 2010 Wim Taymans <wim.taymans@gmail.com>
 *
 * gstbufferpool.c: GstBufferPool baseclass
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:gstbufferpool
 * @short_description: Pool for buffers
 * @see_also: #GstBuffer
 *
 */

#include "gst_private.h"

#include <errno.h>
#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif
#include <sys/types.h>

#include "gstinfo.h"

#include "gstbufferpool.h"


#define GST_BUFFER_POOL_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_BUFFER_POOL, GstBufferPoolPrivate))

struct _GstBufferPoolPrivate
{
  guint min_buffers;
  guint max_buffers;
  guint size;
  guint prefix;
  guint postfix;
  guint align;
};

enum
{
  /* add more above */
  LAST_SIGNAL
};

static void gst_buffer_pool_finalize (GObject * object);

G_DEFINE_TYPE (GstBufferPool, gst_buffer_pool, GST_TYPE_OBJECT);

static void default_set_flushing (GstBufferPool * pool, gboolean flushing);
static gboolean default_set_config (GstBufferPool * pool,
    GstStructure * config);
static GstFlowReturn default_alloc_buffer (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolParams * params);
static GstFlowReturn default_acquire_buffer (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolParams * params);
static void default_free_buffer (GstBufferPool * pool, GstBuffer * buffer);
static void default_release_buffer (GstBufferPool * pool, GstBuffer * buffer);

static void
gst_buffer_pool_class_init (GstBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_buffer_pool_finalize;

  klass->set_flushing = default_set_flushing;
  klass->set_config = default_set_config;
  klass->acquire_buffer = default_acquire_buffer;
  klass->alloc_buffer = default_alloc_buffer;
  klass->release_buffer = default_release_buffer;
  klass->free_buffer = default_free_buffer;
}

static void
gst_buffer_pool_init (GstBufferPool * pool)
{
  pool->priv = GST_BUFFER_POOL_GET_PRIVATE (pool);

  pool->config = gst_structure_new ("GstBufferPoolConfig",
      "size", G_TYPE_UINT, 0,
      "min-buffers", G_TYPE_UINT, 0,
      "max-buffers", G_TYPE_UINT, 0,
      "prefix", G_TYPE_UINT, 0,
      "postfix", G_TYPE_UINT, 0, "align", G_TYPE_UINT, 1, NULL);
  pool->poll = gst_poll_new_timer ();
  pool->queue = gst_atomic_queue_new (10);
  default_set_flushing (pool, TRUE);

  GST_DEBUG_OBJECT (pool, "created");
}

static void
gst_buffer_pool_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_buffer_pool_parent_class)->finalize (object);
}

/**
 * gst_buffer_pool_new:
 *
 * Creates a new #GstBufferPool instance.
 *
 * Returns: a new #GstBufferPool instance
 */
GstBufferPool *
gst_buffer_pool_new (void)
{
  GstBufferPool *result;

  result = g_object_newv (GST_TYPE_BUFFER_POOL, 0, NULL);
  GST_DEBUG_OBJECT (result, "created new buffer pool");

  return result;
}

static void
flush_buffers (GstBufferPool * pool)
{
  GstBuffer *buffer;
  GstBufferPoolClass *pclass;

  pclass = GST_BUFFER_POOL_GET_CLASS (pool);

  while ((buffer = gst_atomic_queue_pop (pool->queue))) {
    gst_poll_read_control (pool->poll);
    if (G_LIKELY (pclass->free_buffer))
      pclass->free_buffer (pool, buffer);
  }
}

static void
default_set_flushing (GstBufferPool * pool, gboolean flushing)
{
  g_atomic_int_set (&pool->flushing, flushing);

  if (flushing) {
    /* write the control socket so that waiters get woken up and can check the
     * flushing flag we set above */
    gst_poll_write_control (pool->poll);
    flush_buffers (pool);
  } else {
    gst_poll_read_control (pool->poll);
  }
}

/**
 * gst_buffer_pool_set_flushing:
 * @pool: a #GstBufferPool
 * @flushing: the new flushing state
 *
 * Control the flushing state of @pool. When the pool is flushing, new calls to
 * gst_buffer_pool_acquire_buffer() will return with GST_FLOW_WRONG_STATE.
 */
void
gst_buffer_pool_set_flushing (GstBufferPool * pool, gboolean flushing)
{
  GstBufferPoolClass *pclass;

  g_return_if_fail (GST_IS_BUFFER_POOL (pool));

  pclass = GST_BUFFER_POOL_GET_CLASS (pool);

  if (G_LIKELY (pclass->set_flushing))
    pclass->set_flushing (pool, flushing);
}

static gboolean
default_set_config (GstBufferPool * pool, GstStructure * config)
{
  guint i;
  GstBufferPoolClass *pclass;
  GstBufferPoolPrivate *priv = pool->priv;

  pclass = GST_BUFFER_POOL_GET_CLASS (pool);

  /* parse the config and keep around */
  gst_buffer_pool_config_get (config, &priv->size, &priv->min_buffers,
      &priv->max_buffers, &priv->prefix, &priv->postfix, &priv->align);

  /* we need to prealloc buffers */
  for (i = priv->min_buffers; i > 0; i--) {
    GstBuffer *buffer;

    if (G_LIKELY (pclass->alloc_buffer)) {
      if (!pclass->alloc_buffer (pool, &buffer, NULL))
        return FALSE;
    } else
      return FALSE;

    /* store in the queue */
    gst_atomic_queue_push (pool->queue, buffer);
    gst_poll_write_control (pool->poll);
  }

  return TRUE;
}

/**
 * gst_buffer_pool_set_config:
 * @pool: a #GstBufferPool
 * @config: a #GstStructure
 *
 * Set the configuration of the pool. The pool must be flushing or else this
 * function will do nothing and return FALSE.
 *
 * @condfig is a #GstStructure that contains the configuration parameters for
 * the pool. A default and mandatory set of parameters can be configured with
 * gst_buffer_pool_config_set().
 *
 * Returns: TRUE when the configuration could be set.
 */
gboolean
gst_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  gboolean result;
  GstBufferPoolClass *pclass;

  g_return_val_if_fail (GST_IS_BUFFER_POOL (pool), FALSE);
  g_return_val_if_fail (config != NULL, FALSE);

  if (!g_atomic_int_get (&pool->flushing))
    return FALSE;

  pclass = GST_BUFFER_POOL_GET_CLASS (pool);

  /* free the buffer when we are flushing */
  if (G_LIKELY (pclass->set_config))
    result = pclass->set_config (pool, config);
  else
    result = FALSE;

  if (result) {
    if (pool->config)
      gst_structure_free (pool->config);
    pool->config = config;
  }

  return result;
}

/**
 * gst_buffer_pool_get_config:
 * @pool: a #GstBufferPool
 *
 * Get the current configuration of the pool. This configuration is read-only,
 * use gst_structure_copy() to make a writable copy.
 */
const GstStructure *
gst_buffer_pool_get_config (GstBufferPool * pool)
{
  g_return_val_if_fail (GST_IS_BUFFER_POOL (pool), NULL);

  return pool->config;
}


/**
 * gst_buffer_pool_config_set:
 * @pool: a #GstBufferPool
 * @size: the size of each buffer, not including pre and post fix
 * @min_buffers: the minimum amount of buffers to allocate.
 * @max_buffers: the maximum amount of buffers to allocate or 0 for unlimited.
 * @prefix: prefix each buffer with this many bytes
 * @postfix: postfix each buffer with this many bytes
 * @align: alignment of the buffer data.
 *
 * Configure @config with the given parameters.
 */
void
gst_buffer_pool_config_set (GstStructure * config, guint size,
    guint min_buffers, guint max_buffers, guint prefix, guint postfix,
    guint align)
{
  g_return_if_fail (config != NULL);

  gst_structure_set (config,
      "size", G_TYPE_UINT, size,
      "min-buffers", G_TYPE_UINT, min_buffers,
      "max-buffers", G_TYPE_UINT, max_buffers,
      "prefix", G_TYPE_UINT, prefix,
      "postfix", G_TYPE_UINT, postfix, "align", G_TYPE_UINT, align, NULL);
}

/**
 * gst_buffer_pool_config_get:
 * @pool: a #GstBufferPool
 * @size: the size of each buffer, not including pre and post fix
 * @min_buffers: the minimum amount of buffers to allocate.
 * @max_buffers: the maximum amount of buffers to allocate or 0 for unlimited.
 * @prefix: prefix each buffer with this many bytes
 * @postfix: postfix each buffer with this many bytes
 * @align: alignment of the buffer data.
 *
 * Get the configuration values from @config.
 */
gboolean
gst_buffer_pool_config_get (GstStructure * config, guint * size,
    guint * min_buffers, guint * max_buffers, guint * prefix, guint * postfix,
    guint * align)
{
  g_return_val_if_fail (config != NULL, FALSE);

  return gst_structure_get (config,
      "size", G_TYPE_UINT, size,
      "min-buffers", G_TYPE_UINT, min_buffers,
      "max-buffers", G_TYPE_UINT, max_buffers,
      "prefix", G_TYPE_UINT, prefix,
      "postfix", G_TYPE_UINT, postfix, "align", G_TYPE_UINT, align, NULL);
}

static GstFlowReturn
default_alloc_buffer (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolParams * params)
{
  guint size, align;
  GstBufferPoolPrivate *priv = pool->priv;

  *buffer = gst_buffer_new ();

  align = priv->align - 1;
  size = priv->prefix + priv->postfix + priv->size + align;
  if (size > 0) {
    guint8 *memptr;

    memptr = g_malloc (size);
    GST_BUFFER_MALLOCDATA (*buffer) = memptr;
    memptr = (guint8 *) ((guintptr) (memptr + align) & ~align);
    GST_BUFFER_DATA (*buffer) = memptr + priv->prefix;
    GST_BUFFER_SIZE (*buffer) = priv->size;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
default_acquire_buffer (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolParams * params)
{
  GstFlowReturn result;
  GstBufferPoolClass *pclass;
  GstBufferPoolPrivate *priv = pool->priv;

  pclass = GST_BUFFER_POOL_GET_CLASS (pool);

  while (TRUE) {
    if (g_atomic_int_get (&pool->flushing))
      return GST_FLOW_WRONG_STATE;

    /* try to get a buffer from the queue */
    *buffer = gst_atomic_queue_pop (pool->queue);
    if (*buffer) {
      /* FIXME check the size of the buffer */
      gst_poll_read_control (pool->poll);
      result = GST_FLOW_OK;
      break;
    }

    /* no buffer */
    if (priv->max_buffers == 0) {
      /* no max_buffers, we allocate some more */
      if (G_LIKELY (pclass->alloc_buffer))
        result = pclass->alloc_buffer (pool, buffer, params);
      else
        result = GST_FLOW_NOT_SUPPORTED;
      break;
    }

    /* check if we need to wait */
    if (!(params->flags & GST_BUFFER_POOL_FLAG_WAIT)) {
      result = GST_FLOW_UNEXPECTED;
      break;
    }

    /* now wait */
    gst_poll_wait (pool->poll, GST_CLOCK_TIME_NONE);
  }
  return result;
}

/**
 * gst_buffer_pool_acquire_buffer:
 * @pool: a #GstBufferPool
 * @buffer: a location for a #GstBuffer
 * @params: parameters.
 *
 * Acquire a buffer from @pool. @buffer should point to a memory location that
 * can hold a pointer to the new buffer.
 *
 * @params can be NULL or contain optional parameters to influence the allocation.
 *
 * Returns: a #GstFlowReturn such as GST_FLOW_WRONG_STATE when the pool is
 * flushing.
 */
GstFlowReturn
gst_buffer_pool_acquire_buffer (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolParams * params)
{
  GstBufferPoolClass *pclass;
  GstFlowReturn result;

  g_return_val_if_fail (GST_IS_BUFFER_POOL (pool), GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);

  pclass = GST_BUFFER_POOL_GET_CLASS (pool);

  if (G_LIKELY (pclass->acquire_buffer))
    result = pclass->acquire_buffer (pool, buffer, params);
  else
    result = GST_FLOW_NOT_SUPPORTED;

  return result;
}

static void
default_free_buffer (GstBufferPool * pool, GstBuffer * buffer)
{
  gst_buffer_unref (buffer);
}

static void
default_release_buffer (GstBufferPool * pool, GstBuffer * buffer)
{
  /* keep it around in our queue, we might be flushing but that's ok because we
   * handle that unlikely case below. */
  gst_atomic_queue_push (pool->queue, buffer);
  gst_poll_write_control (pool->poll);

  if (G_UNLIKELY (g_atomic_int_get (&pool->flushing))) {
    /* we are flushing, remove the buffers again */
    flush_buffers (pool);
  }
}

/**
 * gst_buffer_pool_release_buffer:
 * @pool: a #GstBufferPool
 * @buffer: a #GstBuffer
 *
 * Release @buffer to @pool. @buffer should have previously been allocated from
 * @pool with gst_buffer_pool_acquire_buffer().
 *
 * This function is usually called automatically when the last ref on @buffer
 * disappears.
 */
void
gst_buffer_pool_release_buffer (GstBufferPool * pool, GstBuffer * buffer)
{
  GstBufferPoolClass *pclass;

  g_return_if_fail (GST_IS_BUFFER_POOL (pool));
  g_return_if_fail (buffer != NULL);

  pclass = GST_BUFFER_POOL_GET_CLASS (pool);

  if (G_LIKELY (pclass->release_buffer))
    pclass->release_buffer (pool, buffer);
}
