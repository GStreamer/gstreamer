/* GStreamer
 * Copyright (C) 2021 Seungha Yang <seungha@centricular.com>
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

#include "gstqsvallocator.h"

GST_DEBUG_CATEGORY_EXTERN (gst_qsv_allocator_debug);
#define GST_CAT_DEFAULT gst_qsv_allocator_debug

/* Both d3d11 and va use (GST_MAP_FLAG_LAST << 1) value
 * for GPU access */
#define GST_MAP_QSV (GST_MAP_FLAG_LAST << 1)

struct _GstQsvFrame
{
  GstMiniObject parent;

  GstQsvAllocator *allocator;

  GMutex lock;

  guint map_count;
  GstBuffer *buffer;
  GstVideoInfo info;
  GstVideoFrame frame;
  GstQsvMemoryType mem_type;

  /* For direct GPU access */
  GstMapInfo map_info;
};

GST_DEFINE_MINI_OBJECT_TYPE (GstQsvFrame, gst_qsv_frame);

static void
_gst_qsv_frame_free (GstQsvFrame * frame)
{
  g_mutex_clear (&frame->lock);
  gst_clear_buffer (&frame->buffer);
  gst_clear_object (&frame->allocator);
  g_free (frame);
}

static GstQsvFrame *
gst_qsv_frame_new (void)
{
  GstQsvFrame *self;

  self = g_new0 (GstQsvFrame, 1);
  g_mutex_init (&self->lock);

  gst_mini_object_init (GST_MINI_OBJECT_CAST (self), 0,
      GST_TYPE_QSV_FRAME, nullptr, nullptr,
      (GstMiniObjectFreeFunction) _gst_qsv_frame_free);

  return self;
}

GstBuffer *
gst_qsv_frame_peek_buffer (GstQsvFrame * frame)
{
  g_return_val_if_fail (GST_IS_QSV_FRAME (frame), nullptr);

  return frame->buffer;
}

struct _GstQsvAllocatorPrivate
{
  GstAtomicQueue *queue;

  mfxFrameAllocator allocator;
};

#define gst_qsv_allocator_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GstQsvAllocator,
    gst_qsv_allocator, GST_TYPE_OBJECT);

static void gst_qsv_allocator_finalize (GObject * object);
static mfxStatus gst_qsv_allocator_alloc (mfxHDL pthis,
    mfxFrameAllocRequest * request, mfxFrameAllocResponse * response);
static mfxStatus gst_qsv_allocator_lock (mfxHDL pthis, mfxMemId mid,
    mfxFrameData * ptr);
static mfxStatus gst_qsv_allocator_unlock (mfxHDL pthis, mfxMemId mid,
    mfxFrameData * ptr);
static mfxStatus gst_qsv_allocator_get_hdl (mfxHDL pthis, mfxMemId mid,
    mfxHDL * handle);
static mfxStatus gst_qsv_allocator_free (mfxHDL pthis,
    mfxFrameAllocResponse * response);

static void
gst_qsv_allocator_class_init (GstQsvAllocatorClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_qsv_allocator_finalize;
}

static void
gst_qsv_allocator_init (GstQsvAllocator * self)
{
  GstQsvAllocatorPrivate *priv;

  priv = self->priv = (GstQsvAllocatorPrivate *)
      gst_qsv_allocator_get_instance_private (self);

  priv->queue = gst_atomic_queue_new (16);

  priv->allocator.pthis = self;
  priv->allocator.Alloc = gst_qsv_allocator_alloc;
  priv->allocator.Lock = gst_qsv_allocator_lock;
  priv->allocator.Unlock = gst_qsv_allocator_unlock;
  priv->allocator.GetHDL = gst_qsv_allocator_get_hdl;
  priv->allocator.Free = gst_qsv_allocator_free;
}

static void
gst_qsv_allocator_finalize (GObject * object)
{
  GstQsvAllocator *self = GST_QSV_ALLOCATOR (object);
  GstQsvAllocatorPrivate *priv = self->priv;
  GstQsvFrame *frame;

  GST_DEBUG_OBJECT (object, "finalize");

  while ((frame = (GstQsvFrame *) gst_atomic_queue_pop (priv->queue)))
    gst_qsv_frame_unref (frame);

  gst_atomic_queue_unref (priv->queue);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static mfxStatus
gst_qsv_allocator_alloc_default (GstQsvAllocator * self,
    mfxFrameAllocRequest * request, mfxFrameAllocResponse * response)
{
  GstQsvFrame **mids = nullptr;
  GstVideoInfo info;
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;

  GST_TRACE_OBJECT (self, "Alloc");

  /* Something unexpected and went wrong */
  if ((request->Type & MFX_MEMTYPE_SYSTEM_MEMORY) == 0) {
    GST_ERROR_OBJECT (self,
        "MFX is requesting system memory, type 0x%x", request->Type);
    return MFX_ERR_UNSUPPORTED;
  }

  switch (request->Info.FourCC) {
    case MFX_FOURCC_NV12:
      format = GST_VIDEO_FORMAT_NV12;
      break;
    default:
      /* TODO: add more formats */
      break;
  }

  if (format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (self, "Unknown MFX format fourcc %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (request->Info.FourCC));

    return MFX_ERR_UNSUPPORTED;
  }

  mids = g_new0 (GstQsvFrame *, request->NumFrameSuggested);
  response->NumFrameActual = request->NumFrameSuggested;

  gst_video_info_set_format (&info,
      format, request->Info.Width, request->Info.Height);
  for (guint i = 0; i < request->NumFrameSuggested; i++) {
    GstBuffer *buffer;

    buffer = gst_buffer_new_and_alloc (info.size);
    mids[i] = gst_qsv_allocator_acquire_frame (self,
        GST_QSV_SYSTEM_MEMORY, &info, buffer, nullptr);
    gst_buffer_unref (buffer);
  }

  response->mids = (mfxMemId *) mids;

  return MFX_ERR_NONE;
}

static mfxStatus
gst_qsv_allocator_alloc (mfxHDL pthis,
    mfxFrameAllocRequest * request, mfxFrameAllocResponse * response)
{
  GstQsvAllocator *self = GST_QSV_ALLOCATOR (pthis);
  GstQsvAllocatorClass *klass;

  if ((request->Type & MFX_MEMTYPE_SYSTEM_MEMORY) != 0)
    return gst_qsv_allocator_alloc_default (self, request, response);

  klass = GST_QSV_ALLOCATOR_GET_CLASS (self);

  g_assert (klass->alloc);

  return klass->alloc (self, request, response);
}

static mfxStatus
gst_qsv_allocator_lock (mfxHDL pthis, mfxMemId mid, mfxFrameData * ptr)
{
  GstQsvAllocator *self = GST_QSV_ALLOCATOR (pthis);
  GstQsvFrame *frame = (GstQsvFrame *) mid;

  GST_TRACE_OBJECT (self, "Lock mfxMemId %p", mid);

  g_mutex_lock (&frame->lock);
  if (frame->map_count == 0) {
    gst_video_frame_map (&frame->frame, &frame->info, frame->buffer,
        GST_MAP_READ);
  }

  frame->map_count++;

  ptr->Pitch = (mfxU16) GST_VIDEO_FRAME_PLANE_STRIDE (&frame->frame, 0);
  ptr->Y = (mfxU8 *) GST_VIDEO_FRAME_PLANE_DATA (&frame->frame, 0);

  /* FIXME: check and handle other formats */
  if (GST_VIDEO_INFO_FORMAT (&frame->info) == GST_VIDEO_FORMAT_NV12)
    ptr->UV = (mfxU8 *) GST_VIDEO_FRAME_PLANE_DATA (&frame->frame, 1);

  g_mutex_unlock (&frame->lock);

  return MFX_ERR_NONE;
}

static mfxStatus
gst_qsv_allocator_unlock (mfxHDL pthis, mfxMemId mid, mfxFrameData * ptr)
{
  GstQsvAllocator *self = GST_QSV_ALLOCATOR (pthis);
  GstQsvFrame *frame = (GstQsvFrame *) mid;

  GST_TRACE_OBJECT (self, "Unlock mfxMemId %p", mid);

  g_mutex_lock (&frame->lock);

  if (frame->map_count > 0) {
    frame->map_count--;

    if (frame->map_count == 0)
      gst_video_frame_unmap (&frame->frame);
  } else {
    GST_WARNING_OBJECT (self, "Unlock request for non-locked memory");
  }

  g_mutex_unlock (&frame->lock);

  return MFX_ERR_NONE;
}

static mfxStatus
gst_qsv_allocator_get_hdl (mfxHDL pthis, mfxMemId mid, mfxHDL * handle)
{
  GstQsvAllocator *self = GST_QSV_ALLOCATOR (pthis);
  GstQsvFrame *frame = GST_QSV_FRAME_CAST (mid);

  if (frame->mem_type != GST_QSV_VIDEO_MEMORY) {
    GST_ERROR_OBJECT (self, "Unexpected call");

    return MFX_ERR_UNSUPPORTED;
  }

  if (!frame->map_info.data) {
    GST_ERROR_OBJECT (self, "No mapped data");
    return MFX_ERR_UNSUPPORTED;
  }

  GST_TRACE_OBJECT (self, "Get handle for mfxMemId %p", mid);

#ifdef G_OS_WIN32
  mfxHDLPair *pair = (mfxHDLPair *) handle;
  pair->first = (mfxHDL) frame->map_info.data;

  /* GstD3D11 will fill user_data[0] with subresource index */
  pair->second = (mfxHDL) frame->map_info.user_data[0];
#else
  *handle = (mfxHDL) frame->map_info.data;
#endif

  return MFX_ERR_NONE;
}

static mfxStatus
gst_qsv_allocator_free (mfxHDL pthis, mfxFrameAllocResponse * response)
{
  GstQsvFrame **frames = (GstQsvFrame **) response->mids;

  for (guint i = 0; i < response->NumFrameActual; i++)
    gst_clear_qsv_frame (&frames[i]);

  g_clear_pointer (&response->mids, g_free);

  return MFX_ERR_NONE;
}

static void
gst_qsv_frame_release (GstQsvFrame * frame)
{
  GstQsvAllocator *allocator = frame->allocator;

  g_mutex_lock (&frame->lock);
  if (frame->map_count > 0) {
    GST_WARNING_OBJECT (allocator, "Releasing mapped frame %p", frame);
    gst_video_frame_unmap (&frame->frame);
  }
  frame->map_count = 0;
  g_mutex_unlock (&frame->lock);

  if (frame->mem_type == GST_QSV_VIDEO_MEMORY && frame->map_info.data)
    gst_buffer_unmap (frame->buffer, &frame->map_info);

  memset (&frame->map_info, 0, sizeof (GstMapInfo));

  gst_clear_buffer (&frame->buffer);
  GST_MINI_OBJECT_CAST (frame)->dispose = nullptr;
  frame->allocator = nullptr;

  GST_TRACE_OBJECT (allocator, "Moving frame %p back to pool", frame);

  gst_atomic_queue_push (allocator->priv->queue, frame);
  gst_object_unref (allocator);
}

static gboolean
gst_qsv_frame_dispose (GstQsvFrame * frame)
{
  g_assert (frame->allocator);

  gst_qsv_frame_ref (frame);
  gst_qsv_frame_release (frame);

  return FALSE;
}

static GstBuffer *
gst_qsv_allocator_upload_default (GstQsvAllocator * allocator,
    const GstVideoInfo * info, GstBuffer * buffer, GstBufferPool * pool)
{
  GstBuffer *dst_buf;
  GstFlowReturn flow_ret;
  GstVideoFrame src_frame, dst_frame;

  flow_ret = gst_buffer_pool_acquire_buffer (pool, &dst_buf, nullptr);
  if (flow_ret != GST_FLOW_OK) {
    GST_WARNING ("Failed to acquire buffer from pool, return %s",
        gst_flow_get_name (flow_ret));
    return nullptr;
  }

  gst_video_frame_map (&src_frame, info, buffer, GST_MAP_READ);
  gst_video_frame_map (&dst_frame, info, dst_buf, GST_MAP_WRITE);

  if (GST_VIDEO_FRAME_WIDTH (&src_frame) == GST_VIDEO_FRAME_WIDTH (&dst_frame)
      && GST_VIDEO_FRAME_HEIGHT (&src_frame) ==
      GST_VIDEO_FRAME_HEIGHT (&dst_frame)) {
    gst_video_frame_unmap (&src_frame);
    gst_video_frame_unmap (&dst_frame);

    gst_buffer_unref (dst_buf);
    return gst_buffer_ref (buffer);
  }

  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&src_frame); i++) {
    guint src_width_in_bytes, src_height;
    guint dst_width_in_bytes, dst_height;
    guint width_in_bytes, height;
    guint src_stride, dst_stride;
    guint8 *src_data, *dst_data;

    src_width_in_bytes = GST_VIDEO_FRAME_COMP_WIDTH (&src_frame, i) *
        GST_VIDEO_FRAME_COMP_PSTRIDE (&src_frame, i);
    src_height = GST_VIDEO_FRAME_COMP_HEIGHT (&src_frame, i);
    src_stride = GST_VIDEO_FRAME_COMP_STRIDE (&src_frame, i);

    dst_width_in_bytes = GST_VIDEO_FRAME_COMP_WIDTH (&dst_frame, i) *
        GST_VIDEO_FRAME_COMP_PSTRIDE (&dst_frame, i);
    dst_height = GST_VIDEO_FRAME_COMP_HEIGHT (&dst_frame, i);
    dst_stride = GST_VIDEO_FRAME_COMP_STRIDE (&dst_frame, i);

    width_in_bytes = MIN (src_width_in_bytes, dst_width_in_bytes);
    height = MIN (src_height, dst_height);

    src_data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&src_frame, i);
    dst_data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&dst_frame, i);

    for (guint j = 0; j < height; j++) {
      memcpy (dst_data, src_data, width_in_bytes);
      dst_data += dst_stride;
      src_data += src_stride;
    }
  }

  gst_video_frame_unmap (&dst_frame);
  gst_video_frame_unmap (&src_frame);

  return dst_buf;
}

/**
 * gst_qsv_allocator_acquire_frame:
 * @allocator: a #GstQsvAllocator
 * @mem_type: a memory type
 * @info: a #GstVideoInfo
 * @buffer: (transfer none): a #GstBuffer
 * @pool: (nullable): a #GstBufferPool
 *
 * Uploads @buffer to video memory if required, and wraps GstBuffer using
 * #GstQsvFrame object so that QSV API can access native memory handle
 * via mfxFrameAllocator interface.
 *
 * Returns: a #GstQsvFrame object
 */
GstQsvFrame *
gst_qsv_allocator_acquire_frame (GstQsvAllocator * allocator,
    GstQsvMemoryType mem_type, const GstVideoInfo * info, GstBuffer * buffer,
    GstBufferPool * pool)
{
  GstQsvAllocatorPrivate *priv;
  GstQsvFrame *frame;

  g_return_val_if_fail (GST_IS_QSV_ALLOCATOR (allocator), nullptr);

  priv = allocator->priv;
  frame = (GstQsvFrame *) gst_atomic_queue_pop (priv->queue);

  if (!frame)
    frame = gst_qsv_frame_new ();

  frame->mem_type = mem_type;
  frame->allocator = (GstQsvAllocator *) gst_object_ref (allocator);
  GST_MINI_OBJECT_CAST (frame)->dispose =
      (GstMiniObjectDisposeFunction) gst_qsv_frame_dispose;

  if (!pool) {
    frame->buffer = gst_buffer_ref (buffer);
    frame->info = *info;
  } else {
    GstBuffer *upload_buf;

    if (mem_type == GST_QSV_SYSTEM_MEMORY) {
      upload_buf = gst_qsv_allocator_upload_default (allocator, info, buffer,
          pool);
    } else {
      GstQsvAllocatorClass *klass;

      klass = GST_QSV_ALLOCATOR_GET_CLASS (allocator);
      g_assert (klass->upload);

      upload_buf = klass->upload (allocator, info, buffer, pool);
    }

    if (!upload_buf) {
      GST_WARNING_OBJECT (allocator, "Failed to upload buffer");
      gst_qsv_frame_unref (frame);

      return nullptr;
    }

    frame->buffer = upload_buf;
    frame->info = *info;
  }

  if (mem_type == GST_QSV_VIDEO_MEMORY) {
    /* TODO: we need to know context whether this memory is for
     * output (e.g., decoder or vpp), but we have only encoder
     * implementation at the moment, so GST_MAP_READ should be fine */
    if (!gst_buffer_map (frame->buffer, &frame->map_info,
            (GstMapFlags) (GST_MAP_READ | GST_MAP_QSV))) {
      GST_ERROR_OBJECT (allocator, "Failed to map video buffer");
      gst_qsv_frame_unref (frame);

      return nullptr;
    }
  }

  return frame;
}

mfxFrameAllocator *
gst_qsv_allocator_get_allocator_handle (GstQsvAllocator * allocator)
{
  g_return_val_if_fail (GST_IS_QSV_ALLOCATOR (allocator), nullptr);

  return &allocator->priv->allocator;
}
