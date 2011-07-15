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
#include "gst/video/gstmetavideo.h"

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

/*
 * GstV4l2BufferPool:
 */
#define gst_v4l2_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstV4l2BufferPool, gst_v4l2_buffer_pool, GST_TYPE_BUFFER_POOL);

static void
gst_v4l2_buffer_pool_free_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  gint index;
  GstMetaV4l2 *meta;
  GstV4l2Object *obj;

  meta = GST_META_V4L2_GET (buffer);
  g_assert (meta != NULL);

  obj = pool->obj;

  index = meta->vbuffer.index;
  GST_LOG_OBJECT (pool, "finalizing buffer %p %d", buffer, index);
  pool->buffers[index] = NULL;

  GST_LOG_OBJECT (pool,
      "buffer %p (data %p, len %u) freed, unmapping",
      buffer, meta->mem, meta->vbuffer.length);
  v4l2_munmap (meta->mem, meta->vbuffer.length);

  gst_buffer_unref (buffer);
}

static GstFlowReturn
gst_v4l2_buffer_pool_alloc_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolParams * params)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstBuffer *newbuf;
  GstMetaV4l2 *meta;
  GstV4l2Object *obj;
  GstVideoInfo *info;
  guint index;

  obj = pool->obj;
  info = &obj->info;

  newbuf = gst_buffer_new ();
  meta = GST_META_V4L2_ADD (newbuf);

  index = pool->index;

  GST_LOG_OBJECT (pool, "creating buffer %u, %p", index, newbuf, pool);

  meta->vbuffer.index = index;
  meta->vbuffer.type = obj->type;
  meta->vbuffer.memory = V4L2_MEMORY_MMAP;

  if (v4l2_ioctl (pool->video_fd, VIDIOC_QUERYBUF, &meta->vbuffer) < 0)
    goto querybuf_failed;

  GST_LOG_OBJECT (pool, "  index:     %u", meta->vbuffer.index);
  GST_LOG_OBJECT (pool, "  type:      %d", meta->vbuffer.type);
  GST_LOG_OBJECT (pool, "  bytesused: %u", meta->vbuffer.bytesused);
  GST_LOG_OBJECT (pool, "  flags:     %08x", meta->vbuffer.flags);
  GST_LOG_OBJECT (pool, "  field:     %d", meta->vbuffer.field);
  GST_LOG_OBJECT (pool, "  memory:    %d", meta->vbuffer.memory);
  if (meta->vbuffer.memory == V4L2_MEMORY_MMAP)
    GST_LOG_OBJECT (pool, "  MMAP offset:  %u", meta->vbuffer.m.offset);
  GST_LOG_OBJECT (pool, "  length:    %u", meta->vbuffer.length);
  GST_LOG_OBJECT (pool, "  input:     %u", meta->vbuffer.input);

  meta->mem = v4l2_mmap (0, meta->vbuffer.length,
      PROT_READ | PROT_WRITE, MAP_SHARED, pool->video_fd,
      meta->vbuffer.m.offset);
  if (meta->mem == MAP_FAILED)
    goto mmap_failed;

  gst_buffer_take_memory (newbuf, -1,
      gst_memory_new_wrapped (0,
          meta->mem, NULL, meta->vbuffer.length, 0, meta->vbuffer.length));

  /* add metadata to raw video buffers */
  if (info->finfo) {
    gsize offset[GST_VIDEO_MAX_PLANES];
    gint stride[GST_VIDEO_MAX_PLANES];

    offset[0] = 0;
    stride[0] = obj->bytesperline;

    GST_DEBUG_OBJECT (pool, "adding video meta");
    gst_buffer_add_meta_video_full (newbuf, info->flags,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
        offset, stride);
  }

  pool->index++;

  *buffer = newbuf;

  return GST_FLOW_OK;

  /* ERRORS */
querybuf_failed:
  {
    gint errnosave = errno;

    GST_WARNING ("Failed QUERYBUF: %s", g_strerror (errnosave));
    gst_buffer_unref (newbuf);
    errno = errnosave;
    return GST_FLOW_ERROR;
  }
mmap_failed:
  {
    gint errnosave = errno;

    GST_WARNING ("Failed to mmap: %s", g_strerror (errnosave));
    gst_buffer_unref (newbuf);
    errno = errnosave;
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_v4l2_buffer_pool_set_config (GstBufferPool * bpool, GstStructure * config)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  const GstCaps *caps;
  guint size, min_buffers, max_buffers;
  guint prefix, align;

  GST_DEBUG_OBJECT (pool, "set config");

  /* parse the config and keep around */
  if (!gst_buffer_pool_config_get (config, &caps, &size, &min_buffers,
          &max_buffers, &prefix, &align))
    goto wrong_config;

  GST_DEBUG_OBJECT (pool, "config %" GST_PTR_FORMAT, config);

  pool->min_buffers = min_buffers;
  pool->max_buffers = max_buffers;

  return TRUE;

wrong_config:
  {
    GST_WARNING_OBJECT (pool, "invalid config %" GST_PTR_FORMAT, config);
    return FALSE;
  }
}

static gboolean
gst_v4l2_buffer_pool_start (GstBufferPool * bpool)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstV4l2Object *obj = pool->obj;
  gint n;
  struct v4l2_requestbuffers breq;
  gint num_buffers;

  num_buffers = pool->max_buffers;

  /* first, lets request buffers, and see how many we can get: */
  GST_DEBUG_OBJECT (pool, "starting, requesting %d MMAP buffers", num_buffers);

  memset (&breq, 0, sizeof (struct v4l2_requestbuffers));
  breq.type = obj->type;
  breq.count = num_buffers;
  breq.memory = V4L2_MEMORY_MMAP;

  if (v4l2_ioctl (pool->video_fd, VIDIOC_REQBUFS, &breq) < 0)
    goto reqbufs_failed;

  GST_LOG_OBJECT (pool, " count:  %u", breq.count);
  GST_LOG_OBJECT (pool, " type:   %d", breq.type);
  GST_LOG_OBJECT (pool, " memory: %d", breq.memory);

  if (breq.count < GST_V4L2_MIN_BUFFERS)
    goto no_buffers;

  if (num_buffers != breq.count) {
    GST_WARNING_OBJECT (pool, "using %u buffers instead", breq.count);
    num_buffers = breq.count;
  }

  pool->obj = obj;
  pool->requeuebuf = (obj->type == V4L2_BUF_TYPE_VIDEO_CAPTURE ? TRUE : FALSE);
  pool->buffer_count = num_buffers;
  pool->buffers = g_new0 (GstBuffer *, num_buffers);
  pool->index = 0;

  /* now, map the buffers: */
  for (n = 0; n < num_buffers; n++) {
    GstBuffer *buffer;

    if (gst_v4l2_buffer_pool_alloc_buffer (bpool, &buffer, NULL) != GST_FLOW_OK)
      goto buffer_new_failed;

    if (pool->requeuebuf)
      gst_v4l2_buffer_pool_qbuf (bpool, buffer);
  }
  return TRUE;

  /* ERRORS */
reqbufs_failed:
  {
    GST_ERROR_OBJECT (pool,
        "error requesting %d buffers: %s", num_buffers, g_strerror (errno));
    return FALSE;
  }
no_buffers:
  {
    GST_ERROR_OBJECT (pool,
        "we received %d from device '%s', we want at least %d",
        breq.count, obj->videodev, GST_V4L2_MIN_BUFFERS);
    return FALSE;
  }
buffer_new_failed:
  {
    GST_ERROR_OBJECT (pool, "failed to create a buffer");
    return FALSE;
  }
}

static gboolean
gst_v4l2_buffer_pool_stop (GstBufferPool * bpool)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  guint n;

  GST_DEBUG_OBJECT (pool, "stopping pool");

  /* free the buffers: */
  for (n = 0; n < pool->buffer_count; n++)
    gst_v4l2_buffer_pool_free_buffer (bpool, pool->buffers[n]);

  return TRUE;
}

static GstFlowReturn
gst_v4l2_buffer_pool_acquire_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolParams * params)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstBuffer *outbuf;
  struct v4l2_buffer vbuffer;
  GstV4l2Object *obj = pool->obj;

  if (GST_BUFFER_POOL_IS_FLUSHING (bpool))
    goto flushing;

  memset (&vbuffer, 0x00, sizeof (vbuffer));
  vbuffer.type = obj->type;
  vbuffer.memory = V4L2_MEMORY_MMAP;

  if (v4l2_ioctl (pool->video_fd, VIDIOC_DQBUF, &vbuffer) < 0)
    goto error;

  /* get our GstBuffer with that index from the pool, if the buffer was
   * outstanding we have a serious problem.
   */
  outbuf = pool->buffers[vbuffer.index];
  if (outbuf == NULL)
    goto no_buffers;

  /* mark the buffer outstanding */
  pool->buffers[vbuffer.index] = NULL;

  GST_LOG_OBJECT (pool,
      "dequeued frame %d (ix=%d), flags %08x, pool-ct=%d, buffer=%p",
      vbuffer.sequence, vbuffer.index, vbuffer.flags, pool->num_live_buffers,
      outbuf);

  pool->num_live_buffers++;
  GST_DEBUG_OBJECT (pool, "num_live_buffers++: %d", pool->num_live_buffers);

  /* set top/bottom field first if v4l2_buffer has the information */
  if (vbuffer.field == V4L2_FIELD_INTERLACED_TB)
    GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_TFF);
  if (vbuffer.field == V4L2_FIELD_INTERLACED_BT)
    GST_BUFFER_FLAG_UNSET (outbuf, GST_VIDEO_BUFFER_TFF);

  /* this can change at every frame, esp. with jpeg */
  gst_buffer_resize (outbuf, 0, vbuffer.bytesused);

  *buffer = outbuf;

  return GST_FLOW_OK;

  /* ERRORS */
flushing:
  {
    return GST_FLOW_WRONG_STATE;
  }
error:
  {
    GST_WARNING_OBJECT (pool,
        "problem grabbing frame %d (ix=%d), pool-ct=%d, buf.flags=%d",
        vbuffer.sequence, vbuffer.index,
        GST_MINI_OBJECT_REFCOUNT (pool), vbuffer.flags);

    switch (errno) {
      case EAGAIN:
        GST_WARNING_OBJECT (pool,
            "Non-blocking I/O has been selected using O_NONBLOCK and"
            " no buffer was in the outgoing queue. device %s", obj->videodev);
        break;
      case EINVAL:
        GST_ERROR_OBJECT (pool,
            "The buffer type is not supported, or the index is out of bounds, "
            "or no buffers have been allocated yet, or the userptr "
            "or length are invalid. device %s", obj->videodev);
        break;
      case ENOMEM:
        GST_ERROR_OBJECT (pool,
            "insufficient memory to enqueue a user pointer buffer");
        break;
      case EIO:
        GST_INFO_OBJECT (pool,
            "VIDIOC_DQBUF failed due to an internal error."
            " Can also indicate temporary problems like signal loss."
            " Note the driver might dequeue an (empty) buffer despite"
            " returning an error, or even stop capturing."
            " device %s", obj->videodev);
        /* have we de-queued a buffer ? */
        if (!(vbuffer.flags & (V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE))) {
          GST_DEBUG_OBJECT (pool, "reenqueing buffer");
          /* FIXME ... should we do something here? */
        }
        break;
      case EINTR:
        GST_WARNING_OBJECT (pool,
            "could not sync on a buffer on device %s", obj->videodev);
        break;
      default:
        GST_WARNING_OBJECT (pool,
            "Grabbing frame got interrupted on %s unexpectedly. %d: %s.",
            obj->videodev, errno, g_strerror (errno));
        break;
    }
    return GST_FLOW_ERROR;
  }
no_buffers:
  {
    GST_ERROR_OBJECT (pool, "No free buffers found in the pool at index %d.",
        vbuffer.index);
    return GST_FLOW_ERROR;
  }
}

static void
gst_v4l2_buffer_pool_release_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);

  GST_DEBUG_OBJECT (pool, "release");

  if (pool->requeuebuf)
    gst_v4l2_buffer_pool_qbuf (bpool, buffer);
}

static void
gst_v4l2_buffer_pool_finalize (GObject * object)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (object);

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
}

static void
gst_v4l2_buffer_pool_class_init (GstV4l2BufferPoolClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *bufferpool_class = GST_BUFFER_POOL_CLASS (klass);

  object_class->finalize = gst_v4l2_buffer_pool_finalize;

  bufferpool_class->start = gst_v4l2_buffer_pool_start;
  bufferpool_class->stop = gst_v4l2_buffer_pool_stop;
  bufferpool_class->set_config = gst_v4l2_buffer_pool_set_config;
  bufferpool_class->alloc_buffer = gst_v4l2_buffer_pool_alloc_buffer;
  bufferpool_class->acquire_buffer = gst_v4l2_buffer_pool_acquire_buffer;
  bufferpool_class->release_buffer = gst_v4l2_buffer_pool_release_buffer;
  bufferpool_class->free_buffer = gst_v4l2_buffer_pool_free_buffer;
}

/**
 * gst_v4l2_buffer_pool_new:
 * @obj:  the v4l2 object owning the pool
 * @num_buffers:  the requested number of buffers in the pool
 * @requeuebuf: if %TRUE, and if the pool is still in the running state, a
 *  buffer with no remaining references is immediately passed back to v4l2
 *  (VIDIOC_QBUF), otherwise it is returned to the pool of available buffers
 *  (which can be accessed via gst_v4l2_buffer_pool_get().
 *
 * Construct a new buffer pool.
 *
 * Returns: the new pool, use gst_v4l2_buffer_pool_destroy() to free resources
 */
GstBufferPool *
gst_v4l2_buffer_pool_new (GstV4l2Object * obj)
{
  GstV4l2BufferPool *pool;

  pool = (GstV4l2BufferPool *) g_object_new (GST_TYPE_V4L2_BUFFER_POOL, NULL);

  pool->video_fd = v4l2_dup (obj->video_fd);
  if (pool->video_fd < 0)
    goto dup_failed;

  pool->obj = obj;

  return GST_BUFFER_POOL_CAST (pool);

  /* ERRORS */
dup_failed:
  {
    gint errnosave = errno;
    gst_object_unref (pool);
    errno = errnosave;
    return NULL;
  }
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
gst_v4l2_buffer_pool_qbuf (GstBufferPool * bpool, GstBuffer * buf)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstMetaV4l2 *meta;
  gint index;

  meta = GST_META_V4L2_GET (buf);
  g_assert (meta != NULL);

  index = meta->vbuffer.index;

  GST_LOG_OBJECT (pool, "enqueue pool buffer %d", index);

  if (pool->buffers[index] != NULL)
    goto already_queued;

  if (v4l2_ioctl (pool->video_fd, VIDIOC_QBUF, &meta->vbuffer) < 0)
    goto queue_failed;

  pool->buffers[index] = buf;

  pool->num_live_buffers--;
  GST_DEBUG_OBJECT (pool, "num_live_buffers--: %d", pool->num_live_buffers);

  return TRUE;

  /* ERRORS */
already_queued:
  {
    GST_WARNING_OBJECT (pool, "the buffer was already queued");
    return FALSE;
  }
queue_failed:
  {
    GST_WARNING_OBJECT (pool, "could not queue a buffer");
    return FALSE;
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
gst_v4l2_buffer_pool_available_buffers (GstBufferPool * bpool)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);

  return pool->buffer_count - pool->num_live_buffers;
}
