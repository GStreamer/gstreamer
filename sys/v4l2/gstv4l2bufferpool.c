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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef _GNU_SOURCE
# define _GNU_SOURCE            /* O_CLOEXEC */
#endif
#include <fcntl.h>

#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

#include "gst/video/video.h"
#include "gst/video/gstvideometa.h"
#include "gst/video/gstvideopool.h"
#include "gst/allocators/gstdmabuf.h"

#include <gstv4l2bufferpool.h>

#include "v4l2_calls.h"
#include "gst/gst-i18n-plugin.h"
#include <gst/glib-compat-private.h>

GST_DEBUG_CATEGORY_EXTERN (v4l2_debug);
#define GST_CAT_DEFAULT v4l2_debug

/*
 * GstV4l2BufferPool:
 */
#define gst_v4l2_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstV4l2BufferPool, gst_v4l2_buffer_pool, GST_TYPE_BUFFER_POOL);

enum _GstV4l2BufferPoolAcquireFlags
{
  GST_V4L2_POOL_ACQUIRE_FLAG_RESURECT = GST_BUFFER_POOL_ACQUIRE_FLAG_LAST,
  GST_V4L2_BUFFER_POOL_ACQUIRE_FAG_LAST
};

static void gst_v4l2_buffer_pool_release_buffer (GstBufferPool * bpool,
    GstBuffer * buffer);

static gboolean
gst_v4l2_is_buffer_valid (GstBuffer * buffer, GstV4l2MemoryGroup ** group)
{
  GstMemory *mem = gst_buffer_peek_memory (buffer, 0);
  gboolean valid = FALSE;

  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_TAG_MEMORY))
    goto done;

  if (gst_is_v4l2_memory (mem)) {
    GstV4l2Memory *vmem = (GstV4l2Memory *) mem;
    valid = TRUE;
    if (group)
      *group = vmem->group;
  }

done:
  return valid;
}

static GstFlowReturn
gst_v4l2_buffer_pool_alloc_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstV4l2MemoryGroup *group = NULL;
  GstBuffer *newbuf = NULL;
  GstV4l2Object *obj;
  GstVideoInfo *info;
  GstVideoAlignment *align;

  obj = pool->obj;
  info = &obj->info;
  align = &obj->align;

  switch (obj->mode) {
    case GST_V4L2_IO_RW:
      newbuf =
          gst_buffer_new_allocate (pool->allocator, pool->size, &pool->params);
      break;
    case GST_V4L2_IO_MMAP:
      group = gst_v4l2_allocator_alloc_mmap (pool->vallocator);
      break;
    case GST_V4L2_IO_DMABUF:
      /* TODO group = gst_v4l2_allocator_alloc_dmabuf (pool->vallocator); */
      break;
    case GST_V4L2_IO_USERPTR:
    default:
      newbuf = NULL;
      g_assert_not_reached ();
      break;
  }

  if (group != NULL) {
    gint i;
    newbuf = gst_buffer_new ();

    for (i = 0; i < group->n_mem; i++)
      gst_buffer_append_memory (newbuf, group->mem[i]);
  } else if (newbuf == NULL) {
    goto allocation_failed;
  }

  /* add metadata to raw video buffers */
  if (pool->add_videometa && info->finfo) {
    const GstVideoFormatInfo *finfo = info->finfo;
    gsize offset[GST_VIDEO_MAX_PLANES];
    gint width, height, n_gst_planes, offs, i, stride[GST_VIDEO_MAX_PLANES];

    width = GST_VIDEO_INFO_WIDTH (info);
    height = GST_VIDEO_INFO_HEIGHT (info);

    /* n_gst_planes is the number of planes
     * (RGB: 1, YUY2: 1, NV12: 2, I420: 3)
     * It's greater or equal than the number of v4l2 planes. */
    n_gst_planes = GST_VIDEO_INFO_N_PLANES (info);

    /* the basic are common between MPLANE mode and non MPLANE mode
     * except a special case inside the loop at the end
     */
    offs = 0;
    for (i = 0; i < n_gst_planes; i++) {
      GST_DEBUG_OBJECT (pool, "adding video meta, bytesperline %d",
          obj->bytesperline[i]);

      offset[i] = offs;

      if (GST_VIDEO_FORMAT_INFO_IS_TILED (finfo)) {
        guint x_tiles, y_tiles, ws, hs, tile_height;

        ws = GST_VIDEO_FORMAT_INFO_TILE_WS (finfo);
        hs = GST_VIDEO_FORMAT_INFO_TILE_HS (finfo);
        tile_height = 1 << hs;

        x_tiles = obj->bytesperline[i] >> ws;
        y_tiles = GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (finfo, i,
            GST_ROUND_UP_N (height, tile_height) >> hs);
        stride[i] = GST_VIDEO_TILE_MAKE_STRIDE (x_tiles, y_tiles);
      } else {
        stride[i] = obj->bytesperline[i];
      }

      /* when using multiplanar mode and if there is more then one v4l
       * plane for each gst plane
       */
      if (V4L2_TYPE_IS_MULTIPLANAR (obj->type) && group->n_mem > 1)
        /* non_contiguous case here so we have to make sure that gst goes to the
         * next plane (using default gstvideometa.c::default_map).
         * And the next plane is after length bytes of the previous one from
         * the gst buffer point of view. */
        offs += gst_memory_get_sizes (group->mem[i], NULL, NULL);
      else
        offs += obj->bytesperline[i] *
            GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (finfo, i, height);
    }

    gst_buffer_add_video_meta_full (newbuf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), width, height, n_gst_planes,
        offset, stride);
  }

  if (pool->add_cropmeta) {
    GstVideoCropMeta *crop;
    crop = gst_buffer_add_video_crop_meta (newbuf);
    crop->x = align->padding_left;
    crop->y = align->padding_top;
    crop->width = info->width;
    crop->width = info->height;
  }

  pool->num_allocated++;
  *buffer = newbuf;

  return GST_FLOW_OK;

  /* ERRORS */
allocation_failed:
  {
    GST_WARNING ("Failed to allocated buffer");
    return GST_FLOW_EOS;
  }
}

static gboolean
gst_v4l2_buffer_pool_set_config (GstBufferPool * bpool, GstStructure * config)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstV4l2Object *obj = pool->obj;
  GstCaps *caps;
  guint size, min_buffers, max_buffers;
  GstAllocator *allocator;
  GstAllocationParams params;
  gboolean can_allocate = FALSE;
  gboolean updated = FALSE;
  gboolean ret;

  pool->add_videometa =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!pool->add_videometa && obj->need_video_meta)
    goto missing_video_api;

  /* parse the config and keep around */
  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
          &max_buffers))
    goto wrong_config;

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    goto wrong_config;

  GST_DEBUG_OBJECT (pool, "config %" GST_PTR_FORMAT, config);

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  switch (obj->mode) {
    case GST_V4L2_IO_DMABUF:
      pool->allocator = gst_dmabuf_allocator_new ();
      can_allocate = GST_V4L2_ALLOCATOR_CAN_ALLOCATE (pool->vallocator, MMAP);
      break;
    case GST_V4L2_IO_MMAP:
      can_allocate = GST_V4L2_ALLOCATOR_CAN_ALLOCATE (pool->vallocator, MMAP);
      break;
    case GST_V4L2_IO_USERPTR:
      can_allocate =
          GST_V4L2_ALLOCATOR_CAN_ALLOCATE (pool->vallocator, USERPTR);
      break;
    case GST_V4L2_IO_RW:
    default:
      pool->allocator = g_object_ref (allocator);
      pool->params = params;
      /* No need to change the configuration */
      goto done;
      break;
  }

  if (min_buffers < GST_V4L2_MIN_BUFFERS) {
    updated = TRUE;
    min_buffers = GST_V4L2_MIN_BUFFERS;
    GST_INFO_OBJECT (pool, "increasing minimum buffers to %u", min_buffers);
  }

  if (max_buffers > VIDEO_MAX_FRAME || max_buffers == 0) {
    updated = TRUE;
    max_buffers = VIDEO_MAX_FRAME;
    GST_INFO_OBJECT (pool, "reducing maximum buffers to %u", max_buffers);
  }

  if (min_buffers > max_buffers) {
    updated = TRUE;
    min_buffers = max_buffers;
    GST_INFO_OBJECT (pool, "reducing minimum buffers to %u", min_buffers);
  } else if (min_buffers != max_buffers) {
    if (!can_allocate) {
      updated = TRUE;
      max_buffers = min_buffers;
      GST_INFO_OBJECT (pool, "can't allocate, setting maximum to minimum");
    }
  }

  if (updated)
    gst_buffer_pool_config_set_params (config, caps, size, min_buffers,
        max_buffers);

done:
  ret = GST_BUFFER_POOL_CLASS (parent_class)->set_config (bpool, config);

  /* If anything was changed documentation recommand to return FALSE */
  return !updated && ret;

  /* ERRORS */
missing_video_api:
  {
    GST_ERROR_OBJECT (pool, "missing GstMetaVideo API in config");
    return FALSE;
  }
wrong_config:
  {
    GST_ERROR_OBJECT (pool, "invalid config %" GST_PTR_FORMAT, config);
    return FALSE;
  }
}

static gboolean
start_streaming (GstV4l2BufferPool * pool)
{
  GstV4l2Object *obj = pool->obj;

  switch (obj->mode) {
    case GST_V4L2_IO_RW:
      break;
    case GST_V4L2_IO_MMAP:
    case GST_V4L2_IO_USERPTR:
    case GST_V4L2_IO_DMABUF:
      GST_DEBUG_OBJECT (pool, "STREAMON");
      if (v4l2_ioctl (pool->video_fd, VIDIOC_STREAMON, &obj->type) < 0)
        goto start_failed;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  pool->streaming = TRUE;

  return TRUE;

  /* ERRORS */
start_failed:
  {
    GST_ERROR_OBJECT (pool, "error with STREAMON %d (%s)", errno,
        g_strerror (errno));
    return FALSE;
  }
}

static void
gst_v4l2_buffer_pool_group_released (GstV4l2BufferPool * pool)
{
  GstBufferPoolAcquireParams params = { 0 };
  GstBuffer *buffer = NULL;
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (pool, "A buffer was lost, reallocating it");

  params.flags = GST_V4L2_POOL_ACQUIRE_FLAG_RESURECT;
  ret = gst_buffer_pool_acquire_buffer (GST_BUFFER_POOL (pool), &buffer,
      &params);

  if (ret == GST_FLOW_OK)
    gst_buffer_unref (buffer);
}

static gboolean
gst_v4l2_buffer_pool_start (GstBufferPool * bpool)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstV4l2Object *obj = pool->obj;
  GstStructure *config;
  GstCaps *caps;
  guint size, min_buffers, max_buffers;
  guint num_buffers = 0, copy_threshold = 0;

  config = gst_buffer_pool_get_config (bpool);
  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
          &max_buffers))
    goto wrong_config;

  switch (obj->mode) {
    case GST_V4L2_IO_RW:
      /* we preallocate 1 buffer, this value also instructs the latency
       * calculation to have 1 frame latency max */
      num_buffers = 1;
      break;
    case GST_V4L2_IO_DMABUF:
    case GST_V4L2_IO_MMAP:
    {
      guint count;

      if (GST_V4L2_ALLOCATOR_CAN_ALLOCATE (pool->vallocator, MMAP)) {
        num_buffers = min_buffers;
      } else {
        num_buffers = max_buffers;
      }

      /* first, lets request buffers, and see how many we can get: */
      GST_DEBUG_OBJECT (pool, "requesting %d MMAP buffers", num_buffers);

      count = gst_v4l2_allocator_start (pool->vallocator, num_buffers,
          V4L2_MEMORY_MMAP);

      if (count < GST_V4L2_MIN_BUFFERS) {
        num_buffers = count;
        goto no_buffers;
      }

      /* V4L2 buffer pool are often very limited in the amount of buffers it
       * can offer. The copy_threshold will workaround this limitation by
       * falling back to copy if the pipeline needed more buffers. This also
       * prevent having to do REQBUFS(N)/REQBUFS(0) everytime configure is
       * called. */
      if (count != num_buffers) {
        GST_WARNING_OBJECT (pool, "using %u buffers instead of %u",
            count, num_buffers);
        num_buffers = count;
        copy_threshold =
            MAX (GST_V4L2_MIN_BUFFERS, obj->min_buffers_for_capture);

        /* Ensure GstBufferPool don't expect initial minimum */
        if (min_buffers > count)
          min_buffers = count;
      }

      break;
    }
    case GST_V4L2_IO_USERPTR:
    default:
      num_buffers = 0;
      copy_threshold = 0;
      g_assert_not_reached ();
      break;
  }

  pool->size = size;
  pool->copy_threshold = copy_threshold;
  pool->num_buffers = num_buffers;
  pool->num_allocated = 0;
  pool->num_queued = 0;

  gst_buffer_pool_config_set_params (config, caps, size, min_buffers,
      max_buffers);
  GST_BUFFER_POOL_CLASS (parent_class)->set_config (bpool, config);
  gst_structure_free (config);

  /* now, allocate the buffers: */
  if (!GST_BUFFER_POOL_CLASS (parent_class)->start (bpool))
    goto start_failed;

  /* we can start capturing now, we wait for the playback case until we queued
   * the first buffer */
  if (!V4L2_TYPE_IS_OUTPUT (obj->type))
    if (!start_streaming (pool))
      goto start_failed;

  if (!V4L2_TYPE_IS_OUTPUT (obj->type))
    pool->group_released_handler =
        g_signal_connect_swapped (pool->vallocator, "group-released",
        G_CALLBACK (gst_v4l2_buffer_pool_group_released), pool);

  gst_poll_set_flushing (obj->poll, FALSE);

  return TRUE;

  /* ERRORS */
wrong_config:
  {
    GST_ERROR_OBJECT (pool, "invalid config %" GST_PTR_FORMAT, config);
    return FALSE;
  }
no_buffers:
  {
    GST_ERROR_OBJECT (pool,
        "we received %d buffer from device '%s', we want at least %d",
        num_buffers, obj->videodev, GST_V4L2_MIN_BUFFERS);
    return FALSE;
  }
start_failed:
  {
    GST_ERROR_OBJECT (pool, "failed to start streaming");
    return FALSE;
  }
}


static gboolean
stop_streaming (GstV4l2BufferPool * pool)
{
  GstV4l2Object *obj = pool->obj;
  gint i;

  GST_DEBUG_OBJECT (pool, "stopping stream");

  gst_poll_set_flushing (obj->poll, TRUE);

  if (!pool->streaming) {
    /* it avoid error: STREAMOFF 22 (Invalid argument) when
     * attempting to stop a stream not previously started */
    GST_DEBUG_OBJECT (pool, "no need to stop, was not previously started");
    return TRUE;
  }

  switch (obj->mode) {
    case GST_V4L2_IO_RW:
      break;
    case GST_V4L2_IO_MMAP:
    case GST_V4L2_IO_USERPTR:
    case GST_V4L2_IO_DMABUF:
      GST_DEBUG_OBJECT (pool, "STREAMOFF");
      if (v4l2_ioctl (pool->video_fd, VIDIOC_STREAMOFF, &obj->type) < 0)
        goto stop_failed;

      gst_v4l2_allocator_flush (pool->vallocator);

      for (i = 0; i < pool->num_allocated; i++) {
        if (pool->buffers[i]) {
          GstBufferPool *bpool = (GstBufferPool *) pool;
          GstBuffer *buffer = pool->buffers[i];

          pool->buffers[i] = NULL;
          pool->num_queued--;

          if (V4L2_TYPE_IS_OUTPUT (obj->type))
            gst_buffer_unref (buffer);
          else
            /* Give back the outstanding buffer to the pool */
            GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (bpool,
                buffer);
        }
      }
      g_return_val_if_fail (pool->num_queued == 0, FALSE);

      break;
    default:
      g_return_val_if_reached (FALSE);
      break;
  }

  pool->streaming = FALSE;

  return TRUE;

  /* ERRORS */
stop_failed:
  {
    GST_ERROR_OBJECT (pool, "error with STREAMOFF %d (%s)", errno,
        g_strerror (errno));
    return FALSE;
  }
}

static gboolean
gst_v4l2_buffer_pool_stop (GstBufferPool * bpool)
{
  gboolean ret;
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstV4l2Object *obj = pool->obj;

  GST_DEBUG_OBJECT (pool, "stopping pool");

  if (pool->group_released_handler > 0) {
    g_signal_handler_disconnect (pool->vallocator,
        pool->group_released_handler);
    pool->group_released_handler = 0;
  }

  gst_poll_set_flushing (obj->poll, TRUE);
  if (!stop_streaming (pool))
    goto stop_failed;

  ret = GST_BUFFER_POOL_CLASS (parent_class)->stop (bpool);

  if (ret) {
    GstV4l2Return vret;

    vret = gst_v4l2_allocator_stop (pool->vallocator);

    if (vret == GST_V4L2_BUSY) {
      GST_WARNING_OBJECT (pool, "allocated buffer need to be reclaimed");
      /* FIXME deal with reclaiming */
    } else if (vret == GST_V4L2_ERROR) {
      ret = FALSE;
    }
  }

  return ret;

  /* ERRORS */
stop_failed:
  {
    GST_ERROR_OBJECT (pool, "error with STREAMOFF %d (%s)", errno,
        g_strerror (errno));
    return FALSE;
  }
}

static GstFlowReturn
gst_v4l2_object_poll (GstV4l2Object * v4l2object)
{
  gint ret;

  if (v4l2object->can_poll_device) {
    GST_LOG_OBJECT (v4l2object->element, "polling device");
    ret = gst_poll_wait (v4l2object->poll, GST_CLOCK_TIME_NONE);
    if (G_UNLIKELY (ret < 0)) {
      if (errno == EBUSY)
        goto stopped;
      if (errno == ENXIO) {
        GST_WARNING_OBJECT (v4l2object->element,
            "v4l2 device doesn't support polling. Disabling");
        v4l2object->can_poll_device = FALSE;
      } else {
        if (errno != EAGAIN && errno != EINTR)
          goto select_error;
      }
    }
  }
  return GST_FLOW_OK;

  /* ERRORS */
stopped:
  {
    GST_DEBUG ("stop called");
    return GST_FLOW_FLUSHING;
  }
select_error:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, READ, (NULL),
        ("poll error %d: %s (%d)", ret, g_strerror (errno), errno));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2_buffer_pool_qbuf (GstV4l2BufferPool * pool, GstBuffer * buf)
{
  GstV4l2MemoryGroup *group = NULL;
  gint index;

  if (!gst_v4l2_is_buffer_valid (buf, &group)) {
    GST_LOG_OBJECT (pool, "unref copied/invalid buffer %p", buf);
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }

  index = group->buffer.index;

  if (pool->buffers[index] != NULL)
    goto already_queued;

  GST_LOG_OBJECT (pool, "queuing buffer %i", index);

  if (!gst_v4l2_allocator_qbuf (pool->vallocator, group))
    goto queue_failed;

  pool->buffers[index] = buf;
  pool->num_queued++;

  return GST_FLOW_OK;

already_queued:
  {
    GST_ERROR_OBJECT (pool, "the buffer %i was already queued", index);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
queue_failed:
  {
    GST_ERROR_OBJECT (pool, "could not queue a buffer %i", index);
    /* Return broken buffer to the allocator */
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_TAG_MEMORY);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2_buffer_pool_dqbuf (GstV4l2BufferPool * pool, GstBuffer ** buffer)
{
  GstFlowReturn res;
  GstBuffer *outbuf;
  GstV4l2Object *obj = pool->obj;
  GstClockTime timestamp;
  GstV4l2MemoryGroup *group;
  gint i;

  if ((res = gst_v4l2_object_poll (obj)) != GST_FLOW_OK)
    goto poll_failed;

  GST_LOG_OBJECT (pool, "dequeueing a buffer");

  group = gst_v4l2_allocator_dqbuf (pool->vallocator);
  if (group == NULL)
    goto dqbuf_failed;

  /* get our GstBuffer with that index from the pool, if the buffer was
   * outstanding we have a serious problem.
   */
  outbuf = pool->buffers[group->buffer.index];
  if (outbuf == NULL)
    goto no_buffer;

  /* mark the buffer outstanding */
  pool->buffers[group->buffer.index] = NULL;
  pool->num_queued--;

  timestamp = GST_TIMEVAL_TO_TIME (group->buffer.timestamp);

#ifndef GST_DISABLE_GST_DEBUG
  for (i = 0; i < group->n_mem; i++) {
    GST_LOG_OBJECT (pool,
        "dequeued buffer %p seq:%d (ix=%d), mem %p used %d, plane=%d, flags %08x, ts %"
        GST_TIME_FORMAT ", pool-queued=%d, buffer=%p", outbuf,
        group->buffer.sequence, group->buffer.index, group->mem[i],
        group->planes[i].bytesused, i, group->buffer.flags,
        GST_TIME_ARGS (timestamp), pool->num_queued, outbuf);
  }
#endif

  /* set top/bottom field first if v4l2_buffer has the information */
  if (group->buffer.field == V4L2_FIELD_INTERLACED_TB) {
    GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_INTERLACED);
    GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_TFF);
  } else if (group->buffer.field == V4L2_FIELD_INTERLACED_BT) {
    GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_INTERLACED);
    GST_BUFFER_FLAG_UNSET (outbuf, GST_VIDEO_BUFFER_FLAG_TFF);
  } else {
    GST_BUFFER_FLAG_UNSET (outbuf, GST_VIDEO_BUFFER_FLAG_INTERLACED);
    GST_BUFFER_FLAG_UNSET (outbuf, GST_VIDEO_BUFFER_FLAG_TFF);
  }

  if (GST_VIDEO_INFO_FORMAT (&obj->info) == GST_VIDEO_FORMAT_ENCODED) {
    if (group->buffer.flags & V4L2_BUF_FLAG_KEYFRAME)
      GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
    else
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

  *buffer = outbuf;

  return GST_FLOW_OK;

  /* ERRORS */
poll_failed:
  {
    GST_DEBUG_OBJECT (pool, "poll error %s", gst_flow_get_name (res));
    return res;
  }
dqbuf_failed:
  {
    return GST_FLOW_ERROR;
  }
no_buffer:
  {
    GST_ERROR_OBJECT (pool, "No free buffer found in the pool at index %d.",
        group->buffer.index);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2_buffer_pool_acquire_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret;
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstV4l2Object *obj = pool->obj;

  GST_DEBUG_OBJECT (pool, "acquire");

  if (GST_BUFFER_POOL_IS_FLUSHING (bpool))
    goto flushing;

  switch (obj->type) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
      /* capture, This function should return a buffer with new captured data */
      switch (obj->mode) {
        case GST_V4L2_IO_RW:
          /* take empty buffer from the pool */
          ret = GST_BUFFER_POOL_CLASS (parent_class)->acquire_buffer (bpool,
              buffer, params);
          break;
        case GST_V4L2_IO_DMABUF:
        case GST_V4L2_IO_MMAP:
          /* If this is being called to resurect a lost buffer */
          if (params && params->flags & GST_V4L2_POOL_ACQUIRE_FLAG_RESURECT) {
            ret = GST_BUFFER_POOL_CLASS (parent_class)->acquire_buffer (bpool,
                buffer, params);
            break;
          }

          /* just dequeue a buffer, we basically use the queue of v4l2 as the
           * storage for our buffers. This function does poll first so we can
           * interrupt it fine. */
          ret = gst_v4l2_buffer_pool_dqbuf (pool, buffer);
          if (G_UNLIKELY (ret != GST_FLOW_OK))
            goto done;

          /* start copying buffers when we are running low on buffers */
          if (pool->num_queued < pool->copy_threshold) {
            GstBuffer *copy;

            if (GST_V4L2_ALLOCATOR_CAN_ALLOCATE (pool->vallocator, MMAP)) {
              if (GST_BUFFER_POOL_CLASS (parent_class)->acquire_buffer (bpool,
                      &copy, params) == GST_FLOW_OK) {
                gst_v4l2_buffer_pool_release_buffer (bpool, copy);
                break;
              }
            }

            /* copy the buffer */
            copy = gst_buffer_copy_region (*buffer,
                GST_BUFFER_COPY_ALL | GST_BUFFER_COPY_DEEP, 0, -1);
            GST_LOG_OBJECT (pool, "copy buffer %p->%p", *buffer, copy);

            /* and requeue so that we can continue capturing */
            ret = gst_v4l2_buffer_pool_qbuf (pool, *buffer);
            *buffer = copy;
          }
          break;

        case GST_V4L2_IO_USERPTR:
        default:
          ret = GST_FLOW_ERROR;
          g_assert_not_reached ();
          break;
      }
      break;

    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
      /* playback, This function should return an empty buffer */
      switch (obj->mode) {
        case GST_V4L2_IO_RW:
          /* get an empty buffer */
          ret = GST_BUFFER_POOL_CLASS (parent_class)->acquire_buffer (bpool,
              buffer, params);
          break;

        case GST_V4L2_IO_MMAP:
          /* get a free unqueued buffer */
          ret = GST_BUFFER_POOL_CLASS (parent_class)->acquire_buffer (bpool,
              buffer, params);
          break;

        case GST_V4L2_IO_USERPTR:
        default:
          ret = GST_FLOW_ERROR;
          g_assert_not_reached ();
          break;
      }
      break;

    default:
      ret = GST_FLOW_ERROR;
      g_assert_not_reached ();
      break;
  }
done:
  return ret;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (pool, "We are flushing");
    return GST_FLOW_FLUSHING;
  }
}

static void
gst_v4l2_buffer_pool_release_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstV4l2Object *obj = pool->obj;

  GST_DEBUG_OBJECT (pool, "release buffer %p", buffer);

  switch (obj->type) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
      /* capture, put the buffer back in the queue so that we can refill it
       * later. */
      switch (obj->mode) {
        case GST_V4L2_IO_RW:
          /* release back in the pool */
          GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (bpool, buffer);
          break;

        case GST_V4L2_IO_DMABUF:
        case GST_V4L2_IO_MMAP:
        {
          if (gst_v4l2_is_buffer_valid (buffer, NULL)) {
            /* queue back in the device */
            gst_v4l2_buffer_pool_qbuf (pool, buffer);
          } else {
            /* Simply release invalide/modified buffer, the allocator will
             * give it back later */
            GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_TAG_MEMORY);
            GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (bpool,
                buffer);
          }
          break;
        }
        case GST_V4L2_IO_USERPTR:
        default:
          g_assert_not_reached ();
          break;
      }
      break;

    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
      switch (obj->mode) {
        case GST_V4L2_IO_RW:
          /* release back in the pool */
          GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (bpool, buffer);
          break;

        case GST_V4L2_IO_MMAP:
        {
          GstV4l2MemoryGroup *group;
          guint index;

          if (!gst_v4l2_is_buffer_valid (buffer, &group)) {
            /* Simply release invalide/modified buffer, the allocator will
             * give it back later */
            GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_TAG_MEMORY);
            GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (bpool,
                buffer);
            break;
          }

          index = group->buffer.index;

          if (pool->buffers[index] == NULL) {
            GST_LOG_OBJECT (pool, "buffer %u not queued, putting on free list",
                index);

            /* reset to default size */
            gst_v4l2_allocator_reset_size (pool->vallocator, group);

            /* playback, put the buffer back in the queue to refill later. */
            GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (bpool,
                buffer);
          } else {
            /* the buffer is queued in the device but maybe not played yet. We just
             * leave it there and not make it available for future calls to acquire
             * for now. The buffer will be dequeued and reused later. */
            GST_LOG_OBJECT (pool, "buffer %u is queued", index);
          }
          break;
        }

        case GST_V4L2_IO_USERPTR:
        default:
          g_assert_not_reached ();
          break;
      }
      break;

    default:
      g_assert_not_reached ();
      break;
  }
}

static void
gst_v4l2_buffer_pool_finalize (GObject * object)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (object);
  gint i;

  for (i = 0; i < VIDEO_MAX_FRAME; i++) {
    if (pool->buffers[i])
      gst_buffer_replace (&(pool->buffers[i]), NULL);
  }

  if (pool->video_fd >= 0)
    v4l2_close (pool->video_fd);
  if (pool->vallocator)
    gst_object_unref (pool->vallocator);
  if (pool->allocator)
    gst_object_unref (pool->allocator);

  /* FIXME Is this required to keep around ? */
  gst_object_unref (pool->obj->element);

  /* FIXME have we done enough here ? */

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
}

/**
 * gst_v4l2_buffer_pool_new:
 * @obj:  the v4l2 object owning the pool
 *
 * Construct a new buffer pool.
 *
 * Returns: the new pool, use gst_object_unref() to free resources
 */
GstBufferPool *
gst_v4l2_buffer_pool_new (GstV4l2Object * obj, GstCaps * caps)
{
  GstV4l2BufferPool *pool;
  GstStructure *config;
  gchar *name, *parent_name;
  gboolean res = FALSE;
  gint fd;
  guint min, max;

  fd = v4l2_dup (obj->video_fd);
  if (fd < 0)
    goto dup_failed;

  /* setting a significant unique name */
  parent_name = gst_object_get_name (GST_OBJECT (obj->element));
  name = g_strconcat (parent_name, ":", "pool:",
      V4L2_TYPE_IS_OUTPUT (obj->type) ? "sink" : "src", NULL);
  g_free (parent_name);

  pool = (GstV4l2BufferPool *) g_object_new (GST_TYPE_V4L2_BUFFER_POOL,
      "name", name, NULL);
  g_free (name);

  pool->video_fd = fd;
  pool->obj = obj;
  min = max = 2;

  pool->vallocator =
      gst_v4l2_allocator_new (GST_OBJECT (pool), obj->video_fd, &obj->format);

  switch (obj->mode) {
    case GST_V4L2_IO_RW:
      max = 0;
      break;
    case GST_V4L2_IO_MMAP:
    case GST_V4L2_IO_DMABUF:
      if (GST_V4L2_ALLOCATOR_CAN_ALLOCATE (pool->vallocator, MMAP))
        max = VIDEO_MAX_FRAME;
      break;
    case GST_V4L2_IO_USERPTR:
      if (GST_V4L2_ALLOCATOR_CAN_ALLOCATE (pool->vallocator, USERPTR))
        max = VIDEO_MAX_FRAME;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));
  gst_buffer_pool_config_set_params (config, caps, obj->sizeimage, min, max);

  /* Ensure our internal pool has required features */
  if (obj->need_video_meta)
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (obj->need_crop_meta)
    pool->add_cropmeta = obj->need_crop_meta;

  res = gst_buffer_pool_set_config (GST_BUFFER_POOL_CAST (pool), config);
  if (!res)
    goto config_failed;

  gst_object_ref (obj->element);

  return GST_BUFFER_POOL (pool);

  /* ERRORS */
dup_failed:
  {
    GST_ERROR ("failed to dup fd %d (%s)", errno, g_strerror (errno));
    return NULL;
  }
config_failed:
  {
    GST_WARNING ("failed to set pool config");
    return NULL;
  }
}

static GstFlowReturn
gst_v4l2_do_read (GstV4l2BufferPool * pool, GstBuffer * buf)
{
  GstFlowReturn res;
  GstV4l2Object *obj = pool->obj;
  gint amount;
  GstMapInfo map;
  gint toread;

  toread = obj->sizeimage;

  GST_LOG_OBJECT (pool, "reading %d bytes into buffer %p", toread, buf);

  gst_buffer_map (buf, &map, GST_MAP_WRITE);

  do {
    if ((res = gst_v4l2_object_poll (obj)) != GST_FLOW_OK)
      goto poll_error;

    amount = v4l2_read (obj->video_fd, map.data, toread);

    if (amount == toread) {
      break;
    } else if (amount == -1) {
      if (errno == EAGAIN || errno == EINTR) {
        continue;
      } else
        goto read_error;
    } else {
      /* short reads can happen if a signal interrupts the read */
      continue;
    }
  } while (TRUE);

  GST_LOG_OBJECT (pool, "read %d bytes", amount);
  gst_buffer_unmap (buf, &map);
  gst_buffer_resize (buf, 0, amount);

  return GST_FLOW_OK;

  /* ERRORS */
poll_error:
  {
    GST_DEBUG ("poll error %s", gst_flow_get_name (res));
    goto cleanup;
  }
read_error:
  {
    GST_ELEMENT_ERROR (obj->element, RESOURCE, READ,
        (_("Error reading %d bytes from device '%s'."),
            toread, obj->videodev), GST_ERROR_SYSTEM);
    res = GST_FLOW_ERROR;
    goto cleanup;
  }
cleanup:
  {
    gst_buffer_unmap (buf, &map);
    gst_buffer_resize (buf, 0, 0);
    return res;
  }
}

/**
 * gst_v4l2_buffer_pool_process:
 * @bpool: a #GstBufferPool
 * @buf: a #GstBuffer
 *
 * Process @buf in @bpool. For capture devices, this functions fills @buf with
 * data from the device. For output devices, this functions send the contents of
 * @buf to the device for playback.
 *
 * Returns: %GST_FLOW_OK on success.
 */
GstFlowReturn
gst_v4l2_buffer_pool_process (GstV4l2BufferPool * pool, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBufferPool *bpool = GST_BUFFER_POOL_CAST (pool);
  GstV4l2Object *obj = pool->obj;

  GST_DEBUG_OBJECT (pool, "process buffer %p", buf);

  g_return_val_if_fail (gst_buffer_pool_is_active (bpool), GST_FLOW_ERROR);

  switch (obj->type) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
      /* capture */
      switch (obj->mode) {
        case GST_V4L2_IO_RW:
          /* capture into the buffer */
          ret = gst_v4l2_do_read (pool, buf);
          break;

        case GST_V4L2_IO_MMAP:
        {
          GstBuffer *tmp;

          if (buf->pool == bpool) {
            if (gst_buffer_get_size (buf) == 0)
              goto eos;
            else
              /* nothing, data was inside the buffer when we did _acquire() */
              goto done;
          }

          /* buffer not from our pool, grab a frame and copy it into the target */
          if ((ret = gst_v4l2_buffer_pool_dqbuf (pool, &tmp)) != GST_FLOW_OK)
            goto done;

          /* An empty buffer on capture indicates the end of stream */
          if (gst_buffer_get_size (tmp) == 0) {
            gst_buffer_unref (tmp);
            goto eos;
          }

          if (!gst_v4l2_object_copy (obj, buf, tmp))
            goto copy_failed;

          /* an queue the buffer again after the copy */
          if ((ret = gst_v4l2_buffer_pool_qbuf (pool, tmp)) != GST_FLOW_OK)
            goto done;
          break;
        }

        case GST_V4L2_IO_USERPTR:
        default:
          g_assert_not_reached ();
          break;
      }
      break;

    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
      /* playback */
      switch (obj->mode) {
        case GST_V4L2_IO_RW:
          /* FIXME, do write() */
          GST_WARNING_OBJECT (pool, "implement write()");
          break;
        case GST_V4L2_IO_DMABUF:
        case GST_V4L2_IO_MMAP:
        {
          GstBuffer *to_queue;

          if (buf->pool == bpool) {
            /* nothing, we can queue directly */
            to_queue = gst_buffer_ref (buf);
            GST_LOG_OBJECT (pool, "processing buffer from our pool");
          } else {
            GstBufferPoolAcquireParams params = { 0 };

            GST_LOG_OBJECT (pool, "alloc buffer from our pool");

            /* this can return EOS if all buffers are outstanding which would
             * be strange because we would expect the upstream element to have
             * allocated them and returned to us.. */
            params.flags = GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
            ret = gst_buffer_pool_acquire_buffer (bpool, &to_queue, &params);
            if (ret != GST_FLOW_OK)
              goto acquire_failed;

            /* copy into it and queue */
            if (!gst_v4l2_object_copy (obj, to_queue, buf))
              goto copy_failed;
          }

          if ((ret = gst_v4l2_buffer_pool_qbuf (pool, to_queue)) != GST_FLOW_OK)
            goto done;

          /* if we are not streaming yet (this is the first buffer, start
           * streaming now */
          if (!pool->streaming)
            if (!start_streaming (pool))
              goto start_failed;

          if (pool->num_queued == pool->num_allocated) {
            GstBuffer *out;
            /* all buffers are queued, try to dequeue one and release it back
             * into the pool so that _acquire can get to it again. */
            ret = gst_v4l2_buffer_pool_dqbuf (pool, &out);
            if (ret != GST_FLOW_OK) {
              gst_buffer_unref (to_queue);
              goto done;
            }

            /* release the rendered buffer back into the pool. This wakes up any
             * thread waiting for a buffer in _acquire(). If the buffer still has
             * a pool then this will happen when the refcount reaches 0 */
            if (!out->pool)
              gst_v4l2_buffer_pool_release_buffer (bpool, out);
          }
          gst_buffer_unref (to_queue);
          break;
        }

        case GST_V4L2_IO_USERPTR:
        default:
          g_assert_not_reached ();
          break;
      }
      break;
    default:
      g_assert_not_reached ();
      break;
  }
done:
  return ret;

  /* ERRORS */
acquire_failed:
  {
    GST_WARNING_OBJECT (obj->element, "failed to acquire a buffer: %s",
        gst_flow_get_name (ret));
    return ret;
  }
copy_failed:
  {
    GST_ERROR_OBJECT (obj->element, "failed to copy data");
    return GST_FLOW_ERROR;
  }
start_failed:
  {
    GST_ERROR_OBJECT (obj->element, "failed to start streaming");
    return GST_FLOW_ERROR;
  }
eos:
  {
    GST_DEBUG_OBJECT (obj->element, "end of stream reached");
    return GST_FLOW_EOS;
  }
}


/**
 * gst_v4l2_buffer_pool_flush:
 * @bpool: a #GstBufferPool
 *
 * First, set obj->poll to be flushing
 * Call STREAMOFF to clear QUEUED flag on every driver buffers.
 * Then release all buffers that are in pool->buffers array.
 * Finally call STREAMON if CAPTURE type
 * The caller is responsible to unset flushing on obj->pool
 *
 * Returns: TRUE on success.
 */
gboolean
gst_v4l2_buffer_pool_flush (GstV4l2BufferPool * pool)
{
  GstV4l2Object *obj = pool->obj;

  GST_DEBUG_OBJECT (pool, "flush");

  stop_streaming (pool);

  /* we can start capturing now, we wait for the playback
   * case until we queued the first buffer */
  if (!V4L2_TYPE_IS_OUTPUT (obj->type))
    if (!start_streaming (pool))
      goto start_failed;

  return TRUE;

  /* ERRORS */
start_failed:
  {
    GST_ERROR_OBJECT (pool, "failed to start streaming");
    return FALSE;
  }
}
