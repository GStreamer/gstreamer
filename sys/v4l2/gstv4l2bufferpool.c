/* GStreamer
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@indt.org.br>
 *               2009 Texas Instruments, Inc - http://www.ti.com/
 *
 * gstv4l2bufferpool.c V4L2 buffer pool class
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

#include "gst/video/video.h"

#include <gstv4l2bufferpool.h>
#include "gstv4l2src.h"
#include "gstv4l2sink.h"
#include "v4l2_calls.h"
#include "gst/gst-i18n-plugin.h"

/* videodev2.h is not versioned and we can't easily check for the presence
 * of enum values at compile time, but the V4L2_CAP_VIDEO_OUTPUT_OVERLAY define
 * was added in the same commit as V4L2_FIELD_INTERLACED_{TB,BT} (b2787845) */
#ifndef V4L2_CAP_VIDEO_OUTPUT_OVERLAY
#define V4L2_FIELD_INTERLACED_TB 8
#define V4L2_FIELD_INTERLACED_BT 9
#endif


GST_DEBUG_CATEGORY_EXTERN (v4l2_debug);
#define GST_CAT_DEFAULT v4l2_debug

/*
 * GstV4l2Buffer:
 */
const GstMetaInfo *
gst_meta_v4l2_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (meta_info == NULL) {
    meta_info =
        gst_meta_register ("GstMetaV4l2", "GstMetaV4l2",
        sizeof (GstMetaV4l2), (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) NULL, (GstMetaCopyFunction) NULL,
        (GstMetaTransformFunction) NULL);
  }
  return meta_info;
}

static void
gst_v4l2_buffer_dispose (GstBuffer * buffer)
{
  GstV4l2BufferPool *pool;
  gboolean resuscitated = FALSE;
  gint index;
  GstMetaV4l2 *meta;

  meta = GST_META_V4L2_GET (buffer);
  g_assert (meta != NULL);

  pool = meta->pool;
  index = meta->vbuffer.index;

  GST_LOG_OBJECT (pool->v4l2elem, "finalizing buffer %p %d", buffer, index);

  GST_V4L2_BUFFER_POOL_LOCK (pool);
  if (pool->running) {
    if (pool->requeuebuf) {
      if (!gst_v4l2_buffer_pool_qbuf (pool, buffer)) {
        GST_WARNING ("could not requeue buffer %p %d", buffer, index);
      } else {
        resuscitated = TRUE;
      }
    } else {
      resuscitated = TRUE;
      /* XXX double check this... I think it is ok to not synchronize this
       * w.r.t. destruction of the pool, since the buffer is still live and
       * the buffer holds a ref to the pool..
       */
      g_async_queue_push (pool->avail_buffers, buffer);
    }
  } else {
    GST_LOG_OBJECT (pool->v4l2elem, "the pool is shutting down");
  }

  if (resuscitated) {
    /* FIXME: check that the caps didn't change */
    GST_LOG_OBJECT (pool->v4l2elem, "reviving buffer %p, %d", buffer, index);
    gst_buffer_ref (buffer);
    pool->buffers[index] = buffer;
  }

  GST_V4L2_BUFFER_POOL_UNLOCK (pool);

  if (!resuscitated) {
    GST_LOG_OBJECT (pool->v4l2elem,
        "buffer %p (data %p, len %u) not recovered, unmapping",
        buffer, meta->mem, meta->vbuffer.length);
    v4l2_munmap (meta->mem, meta->vbuffer.length);

    g_object_unref (pool);
  }
}

static GstBuffer *
gst_v4l2_buffer_new (GstV4l2BufferPool * pool, guint index, GstCaps * caps)
{
  GstBuffer *ret;
  GstMetaV4l2 *meta;

  ret = gst_buffer_new ();
  GST_MINI_OBJECT_CAST (ret)->dispose =
      (GstMiniObjectDisposeFunction) gst_v4l2_buffer_dispose;

  meta = GST_META_V4L2_ADD (ret);

  GST_LOG_OBJECT (pool->v4l2elem, "creating buffer %u, %p in pool %p", index,
      ret, pool);

  meta->pool = (GstV4l2BufferPool *) g_object_ref (pool);

  meta->vbuffer.index = index;
  meta->vbuffer.type = pool->type;
  meta->vbuffer.memory = V4L2_MEMORY_MMAP;

  if (v4l2_ioctl (pool->video_fd, VIDIOC_QUERYBUF, &meta->vbuffer) < 0)
    goto querybuf_failed;

  GST_LOG_OBJECT (pool->v4l2elem, "  index:     %u", meta->vbuffer.index);
  GST_LOG_OBJECT (pool->v4l2elem, "  type:      %d", meta->vbuffer.type);
  GST_LOG_OBJECT (pool->v4l2elem, "  bytesused: %u", meta->vbuffer.bytesused);
  GST_LOG_OBJECT (pool->v4l2elem, "  flags:     %08x", meta->vbuffer.flags);
  GST_LOG_OBJECT (pool->v4l2elem, "  field:     %d", meta->vbuffer.field);
  GST_LOG_OBJECT (pool->v4l2elem, "  memory:    %d", meta->vbuffer.memory);
  if (meta->vbuffer.memory == V4L2_MEMORY_MMAP)
    GST_LOG_OBJECT (pool->v4l2elem, "  MMAP offset:  %u",
        meta->vbuffer.m.offset);
  GST_LOG_OBJECT (pool->v4l2elem, "  length:    %u", meta->vbuffer.length);
  GST_LOG_OBJECT (pool->v4l2elem, "  input:     %u", meta->vbuffer.input);

  meta->mem = v4l2_mmap (0, meta->vbuffer.length,
      PROT_READ | PROT_WRITE, MAP_SHARED, pool->video_fd,
      meta->vbuffer.m.offset);
  if (meta->mem == MAP_FAILED)
    goto mmap_failed;

  gst_buffer_take_memory (ret, -1,
      gst_memory_new_wrapped (0,
          meta->mem, NULL, meta->vbuffer.length, 0, meta->vbuffer.length));

  return ret;

  /* ERRORS */
querybuf_failed:
  {
    gint errnosave = errno;

    GST_WARNING ("Failed QUERYBUF: %s", g_strerror (errnosave));
    gst_buffer_unref (ret);
    errno = errnosave;
    return NULL;
  }
mmap_failed:
  {
    gint errnosave = errno;

    GST_WARNING ("Failed to mmap: %s", g_strerror (errnosave));
    gst_buffer_unref (ret);
    errno = errnosave;
    return NULL;
  }
}

/*
 * GstV4l2BufferPool:
 */
#define gst_v4l2_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstV4l2BufferPool, gst_v4l2_buffer_pool, G_TYPE_OBJECT);

static void
gst_v4l2_buffer_pool_finalize (GObject * object)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (object);

  g_mutex_free (pool->lock);
  pool->lock = NULL;

  g_async_queue_unref (pool->avail_buffers);
  pool->avail_buffers = NULL;

  if (pool->video_fd >= 0)
    v4l2_close (pool->video_fd);

  if (pool->buffers) {
    g_free (pool->buffers);
    pool->buffers = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_v4l2_buffer_pool_init (GstV4l2BufferPool * pool)
{
  pool->lock = g_mutex_new ();
  pool->running = FALSE;
  pool->num_live_buffers = 0;
}

static void
gst_v4l2_buffer_pool_class_init (GstV4l2BufferPoolClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_v4l2_buffer_pool_finalize;
}

/* this is somewhat of a hack.. but better to keep the hack in
 * one place than copy/pasting it around..
 */
static GstV4l2Object *
get_v4l2_object (GstElement * v4l2elem)
{
  GstV4l2Object *v4l2object = NULL;
  if (GST_IS_V4L2SRC (v4l2elem)) {
    v4l2object = (GST_V4L2SRC (v4l2elem))->v4l2object;
  } else if (GST_IS_V4L2SINK (v4l2elem)) {
    v4l2object = (GST_V4L2SINK (v4l2elem))->v4l2object;
  } else {
    GST_ERROR_OBJECT (v4l2elem, "unknown v4l2 element");
  }
  return v4l2object;
}

/**
 * gst_v4l2_buffer_pool_new:
 * @v4l2elem:  the v4l2 element (src or sink) that owns this pool
 * @fd:   the video device file descriptor
 * @num_buffers:  the requested number of buffers in the pool
 * @caps:  the caps to set on the buffer
 * @requeuebuf: if %TRUE, and if the pool is still in the running state, a
 *  buffer with no remaining references is immediately passed back to v4l2
 *  (VIDIOC_QBUF), otherwise it is returned to the pool of available buffers
 *  (which can be accessed via gst_v4l2_buffer_pool_get().
 *
 * Construct a new buffer pool.
 *
 * Returns: the new pool, use gst_v4l2_buffer_pool_destroy() to free resources
 */
GstV4l2BufferPool *
gst_v4l2_buffer_pool_new (GstElement * v4l2elem, gint fd, gint num_buffers,
    GstCaps * caps, gboolean requeuebuf, enum v4l2_buf_type type)
{
  GstV4l2BufferPool *pool;
  gint n;
  struct v4l2_requestbuffers breq;

  pool = (GstV4l2BufferPool *) g_object_new (GST_TYPE_V4L2_BUFFER_POOL, NULL);

  pool->video_fd = v4l2_dup (fd);
  if (pool->video_fd < 0)
    goto dup_failed;

  /* first, lets request buffers, and see how many we can get: */
  GST_DEBUG_OBJECT (v4l2elem, "STREAMING, requesting %d MMAP buffers",
      num_buffers);

  memset (&breq, 0, sizeof (struct v4l2_requestbuffers));
  breq.type = type;
  breq.count = num_buffers;
  breq.memory = V4L2_MEMORY_MMAP;

  if (v4l2_ioctl (fd, VIDIOC_REQBUFS, &breq) < 0)
    goto reqbufs_failed;

  GST_LOG_OBJECT (v4l2elem, " count:  %u", breq.count);
  GST_LOG_OBJECT (v4l2elem, " type:   %d", breq.type);
  GST_LOG_OBJECT (v4l2elem, " memory: %d", breq.memory);

  if (breq.count < GST_V4L2_MIN_BUFFERS)
    goto no_buffers;

  if (num_buffers != breq.count) {
    GST_WARNING_OBJECT (v4l2elem, "using %u buffers instead", breq.count);
    num_buffers = breq.count;
  }

  pool->v4l2elem = v4l2elem;
  pool->requeuebuf = requeuebuf;
  pool->type = type;
  pool->buffer_count = num_buffers;
  pool->buffers = g_new0 (GstBuffer *, num_buffers);
  pool->avail_buffers = g_async_queue_new ();

  /* now, map the buffers: */
  for (n = 0; n < num_buffers; n++) {
    pool->buffers[n] = gst_v4l2_buffer_new (pool, n, caps);
    if (!pool->buffers[n])
      goto buffer_new_failed;
    pool->num_live_buffers++;
    g_async_queue_push (pool->avail_buffers, pool->buffers[n]);
  }

  return pool;

  /* ERRORS */
dup_failed:
  {
    gint errnosave = errno;

    g_object_unref (pool);

    errno = errnosave;

    return NULL;
  }
reqbufs_failed:
  {
    GstV4l2Object *v4l2object = get_v4l2_object (v4l2elem);
    GST_ELEMENT_ERROR (v4l2elem, RESOURCE, READ,
        (_("Could not get buffers from device '%s'."),
            v4l2object->videodev),
        ("error requesting %d buffers: %s", num_buffers, g_strerror (errno)));
    return NULL;
  }
no_buffers:
  {
    GstV4l2Object *v4l2object = get_v4l2_object (v4l2elem);
    GST_ELEMENT_ERROR (v4l2elem, RESOURCE, READ,
        (_("Could not get enough buffers from device '%s'."),
            v4l2object->videodev),
        ("we received %d from device '%s', we want at least %d",
            breq.count, v4l2object->videodev, GST_V4L2_MIN_BUFFERS));
    return NULL;
  }
buffer_new_failed:
  {
    gint errnosave = errno;

    gst_v4l2_buffer_pool_destroy (pool);

    errno = errnosave;

    return NULL;
  }
}

/**
 * gst_v4l2_buffer_pool_destroy:
 * @pool: the pool
 *
 * Free all resources in the pool and the pool itself.
 */
void
gst_v4l2_buffer_pool_destroy (GstV4l2BufferPool * pool)
{
  gint n;

  GST_V4L2_BUFFER_POOL_LOCK (pool);
  pool->running = FALSE;
  GST_V4L2_BUFFER_POOL_UNLOCK (pool);

  GST_DEBUG_OBJECT (pool->v4l2elem, "destroy pool");

  /* after this point, no more buffers will be queued or dequeued; no buffer
   * from pool->buffers that is NULL will be set to a buffer, and no buffer that
   * is not NULL will be pushed out. */

  /* miniobjects have no dispose, so they can't break ref-cycles, as buffers ref
   * the pool, we need to unref the buffer to properly finalize te pool */
  for (n = 0; n < pool->buffer_count; n++) {
    GstBuffer *buf;

    GST_V4L2_BUFFER_POOL_LOCK (pool);
    buf = pool->buffers[n];
    GST_V4L2_BUFFER_POOL_UNLOCK (pool);

    if (buf)
      /* we own the ref if the buffer is in pool->buffers; drop it. */
      gst_buffer_unref (buf);
  }

  g_object_unref (pool);
}

/**
 * gst_v4l2_buffer_pool_get:
 * @pool:   the "this" object
 * @blocking:  should this call suspend until there is a buffer available
 *    in the buffer pool?
 *
 * Get an available buffer in the pool
 */
GstBuffer *
gst_v4l2_buffer_pool_get (GstV4l2BufferPool * pool, gboolean blocking)
{
  GstBuffer *buf;

  if (blocking) {
    buf = g_async_queue_pop (pool->avail_buffers);
  } else {
    buf = g_async_queue_try_pop (pool->avail_buffers);
  }

  if (buf) {
    GstMetaV4l2 *meta = GST_META_V4L2_GET (buf);

    GST_V4L2_BUFFER_POOL_LOCK (pool);
    gst_buffer_resize (buf, 0, meta->vbuffer.length);
    GST_BUFFER_FLAG_UNSET (buf, 0xffffffff);
    GST_V4L2_BUFFER_POOL_UNLOCK (pool);
  }

  pool->running = TRUE;

  return buf;
}


/**
 * gst_v4l2_buffer_pool_qbuf:
 * @pool: the pool
 * @buf: the buffer to queue
 *
 * Queue a buffer to the driver
 *
 * Returns: %TRUE for success
 */
gboolean
gst_v4l2_buffer_pool_qbuf (GstV4l2BufferPool * pool, GstBuffer * buf)
{
  GstMetaV4l2 *meta;

  meta = GST_META_V4L2_GET (buf);

  GST_LOG_OBJECT (pool->v4l2elem, "enqueue pool buffer %d",
      meta->vbuffer.index);

  if (v4l2_ioctl (pool->video_fd, VIDIOC_QBUF, &meta->vbuffer) < 0)
    goto queue_failed;

  pool->num_live_buffers--;
  GST_DEBUG_OBJECT (pool->v4l2elem, "num_live_buffers--: %d",
      pool->num_live_buffers);

  return TRUE;

  /* ERRORS */
queue_failed:
  {
    GST_WARNING_OBJECT (pool->v4l2elem, "could not queue a buffer");
    return FALSE;
  }
}

/**
 * gst_v4l2_buffer_pool_dqbuf:
 * @pool: the pool
 *
 * Dequeue a buffer from the driver.  Some generic error handling is done in
 * this function, but any error handling specific to v4l2src (capture) or
 * v4l2sink (output) can be done outside this function by checking 'errno'
 *
 * Returns: a buffer
 */
GstBuffer *
gst_v4l2_buffer_pool_dqbuf (GstV4l2BufferPool * pool)
{
  GstV4l2Object *v4l2object = get_v4l2_object (pool->v4l2elem);
  GstBuffer *pool_buffer;
  struct v4l2_buffer buffer;

  memset (&buffer, 0x00, sizeof (buffer));
  buffer.type = pool->type;
  buffer.memory = V4L2_MEMORY_MMAP;

  if (v4l2_ioctl (pool->video_fd, VIDIOC_DQBUF, &buffer) < 0)
    goto error;

  GST_V4L2_BUFFER_POOL_LOCK (pool);

  /* get our GstBuffer with that index from the pool, if the buffer was
   * outstanding we have a serious problem.
   */
  pool_buffer = pool->buffers[buffer.index];

  if (pool_buffer == NULL)
    goto no_buffers;

  GST_LOG_OBJECT (pool->v4l2elem,
      "grabbed frame %d (ix=%d), flags %08x, pool-ct=%d, buffer=%p",
      buffer.sequence, buffer.index, buffer.flags, pool->num_live_buffers,
      pool_buffer);

  pool->num_live_buffers++;
  GST_DEBUG_OBJECT (pool->v4l2elem, "num_live_buffers++: %d",
      pool->num_live_buffers);

  /* set top/bottom field first if v4l2_buffer has the information */
  if (buffer.field == V4L2_FIELD_INTERLACED_TB)
    GST_BUFFER_FLAG_SET (pool_buffer, GST_VIDEO_BUFFER_TFF);
  if (buffer.field == V4L2_FIELD_INTERLACED_BT)
    GST_BUFFER_FLAG_UNSET (pool_buffer, GST_VIDEO_BUFFER_TFF);

  /* this can change at every frame, esp. with jpeg */
  gst_buffer_resize (pool_buffer, 0, buffer.bytesused);

  GST_V4L2_BUFFER_POOL_UNLOCK (pool);

  return pool_buffer;

  /* ERRORS */
error:
  {
    GST_WARNING_OBJECT (pool->v4l2elem,
        "problem grabbing frame %d (ix=%d), pool-ct=%d, buf.flags=%d",
        buffer.sequence, buffer.index,
        GST_MINI_OBJECT_REFCOUNT (pool), buffer.flags);

    switch (errno) {
      case EAGAIN:
        GST_WARNING_OBJECT (pool->v4l2elem,
            "Non-blocking I/O has been selected using O_NONBLOCK and"
            " no buffer was in the outgoing queue. device %s",
            v4l2object->videodev);
        break;
      case EINVAL:
        GST_ELEMENT_ERROR (pool->v4l2elem, RESOURCE, FAILED,
            (_("Failed trying to get video frames from device '%s'."),
                v4l2object->videodev),
            (_("The buffer type is not supported, or the index is out of bounds," " or no buffers have been allocated yet, or the userptr" " or length are invalid. device %s"), v4l2object->videodev));
        break;
      case ENOMEM:
        GST_ELEMENT_ERROR (pool->v4l2elem, RESOURCE, FAILED,
            (_("Failed trying to get video frames from device '%s'. Not enough memory."), v4l2object->videodev), (_("insufficient memory to enqueue a user pointer buffer. device %s."), v4l2object->videodev));
        break;
      case EIO:
        GST_INFO_OBJECT (pool->v4l2elem,
            "VIDIOC_DQBUF failed due to an internal error."
            " Can also indicate temporary problems like signal loss."
            " Note the driver might dequeue an (empty) buffer despite"
            " returning an error, or even stop capturing."
            " device %s", v4l2object->videodev);
        /* have we de-queued a buffer ? */
        if (!(buffer.flags & (V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE))) {
          GST_DEBUG_OBJECT (pool->v4l2elem, "reenqueing buffer");
          /* FIXME ... should we do something here? */
        }
        break;
      case EINTR:
        GST_WARNING_OBJECT (pool->v4l2elem,
            "could not sync on a buffer on device %s", v4l2object->videodev);
        break;
      default:
        GST_WARNING_OBJECT (pool->v4l2elem,
            "Grabbing frame got interrupted on %s unexpectedly. %d: %s.",
            v4l2object->videodev, errno, g_strerror (errno));
        break;
    }
    return NULL;
  }
no_buffers:
  {
    GST_ELEMENT_ERROR (pool->v4l2elem, RESOURCE, FAILED,
        (_("Failed trying to get video frames from device '%s'."),
            v4l2object->videodev),
        (_("No free buffers found in the pool at index %d."), buffer.index));
    GST_V4L2_BUFFER_POOL_UNLOCK (pool);
    return NULL;
  }
}

/**
 * gst_v4l2_buffer_pool_available_buffers:
 * @pool: the pool
 *
 * Check the number of buffers available to the driver, ie. buffers that
 * have been QBUF'd but not yet DQBUF'd.
 *
 * Returns: the number of buffers available.
 */
gint
gst_v4l2_buffer_pool_available_buffers (GstV4l2BufferPool * pool)
{
  return pool->buffer_count - pool->num_live_buffers;
}
