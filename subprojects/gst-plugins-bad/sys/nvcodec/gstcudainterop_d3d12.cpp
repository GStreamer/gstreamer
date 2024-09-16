/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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
#include <config.h>
#endif

#include "gstcudainterop_d3d12.h"
#include <gst/d3d12/gstd3d12-private.h>
#include <wrl.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

struct GstCudaD3D12InteropResource : public GstMiniObject
{
  GstCudaD3D12InteropResource() = default;

  ~GstCudaD3D12InteropResource()
  {
    if (context) {
      if (gst_cuda_context_push (context)) {
        if (devptr)
          CuMemFree (devptr);

        if (ext_mem)
          CuDestroyExternalMemory (ext_mem);
      }

      gst_object_unref (context);
    }
  }

  GstCudaD3D12Interop *interop = nullptr;

  ComPtr<ID3D12Resource> resource;
  GstCudaContext *context = nullptr;
  CUdeviceptr devptr = 0;
  CUexternalMemory ext_mem = nullptr;
};
/* *INDENT-ON* */

GST_DEFINE_MINI_OBJECT_TYPE (GstCudaD3D12InteropResource,
    gst_cuda_d3d12_interop_resource);

#define ASYNC_FENCE_WAIT_DEPTH 16

struct FenceWaitData
{
  UINT64 fence_value = 0;
  GstCudaD3D12InteropResource *resource = nullptr;
};

static gpointer gst_cuda_d3d12_interop_fence_wait_thread (gpointer data);

struct FenceAsyncWaiter
{
  FenceAsyncWaiter (ID3D12Fence * fence)
  {
    fence_ = fence;
    queue_ = gst_vec_deque_new_for_struct (sizeof (FenceWaitData),
        ASYNC_FENCE_WAIT_DEPTH);
    thread_ = g_thread_new ("GstCudaD3D12Interop",
        gst_cuda_d3d12_interop_fence_wait_thread, this);
  }

   ~FenceAsyncWaiter ()
  {
    {
      std::lock_guard < std::mutex > lk (lock_);
      shutdown_ = true;
      cond_.notify_one ();
    }
    g_thread_join (thread_);

    while (!gst_vec_deque_is_empty (queue_)) {
      auto fence_data = *((FenceWaitData *)
          gst_vec_deque_pop_head_struct (queue_));
      auto completed = fence_->GetCompletedValue ();
      if (completed < fence_data.fence_value)
        fence_->SetEventOnCompletion (fence_data.fence_value, nullptr);
      gst_mini_object_unref (fence_data.resource);
    }

    gst_vec_deque_free (queue_);
  }

  void wait_async (UINT64 fence_value, GstCudaD3D12InteropResource * resource)
  {
    auto completed = fence_->GetCompletedValue ();
    if (completed + ASYNC_FENCE_WAIT_DEPTH < fence_value) {
      fence_->SetEventOnCompletion (fence_value - ASYNC_FENCE_WAIT_DEPTH,
          nullptr);
    }

    FenceWaitData data;
    data.fence_value = fence_value;
    data.resource = resource;

    std::lock_guard < std::mutex > lk (lock_);
    gst_vec_deque_push_tail_struct (queue_, &data);
    cond_.notify_one ();
  }

  ComPtr < ID3D12Fence > fence_;
  GThread *thread_;
  std::mutex lock_;
  std::condition_variable cond_;
  GstVecDeque *queue_;
  bool shutdown_ = false;
};

static gpointer
gst_cuda_d3d12_interop_fence_wait_thread (gpointer data)
{
  auto self = (FenceAsyncWaiter *) data;

  while (true) {
    FenceWaitData fence_data;

    {
      std::unique_lock < std::mutex > lk (self->lock_);
      while (gst_vec_deque_is_empty (self->queue_) && !self->shutdown_)
        self->cond_.wait (lk);

      if (self->shutdown_)
        return nullptr;

      fence_data = *((FenceWaitData *)
          gst_vec_deque_pop_head_struct (self->queue_));
    }

    auto completed = self->fence_->GetCompletedValue ();
    if (completed < fence_data.fence_value) {
      GST_TRACE ("Waiting for fence value %" G_GUINT64_FORMAT,
          fence_data.fence_value);
      self->fence_->SetEventOnCompletion (fence_data.fence_value, nullptr);
      GST_TRACE ("Fence completed with value %" G_GUINT64_FORMAT,
          fence_data.fence_value);
    } else {
      GST_TRACE ("Fence was completed already, fence value: %" G_GUINT64_FORMAT
          ", completed: %" G_GUINT64_FORMAT, fence_data.fence_value, completed);
    }

    gst_mini_object_unref (fence_data.resource);
  }

  return nullptr;
}

struct GstCudaD3D12InteropPrivate
{
  GstCudaD3D12InteropPrivate ()
  {
    fence_data_pool = gst_d3d12_fence_data_pool_new ();
  }

   ~GstCudaD3D12InteropPrivate ()
  {
    fence_waiter = nullptr;

    while (!resource_pool.empty ()) {
      auto resource = resource_pool.front ();
      resource_pool.pop ();
      gst_mini_object_unref (resource);
    }

    if (gst_cuda_context_push (context)) {
      if (in_sem)
        CuDestroyExternalSemaphore (in_sem);

      if (out_sem)
        CuDestroyExternalSemaphore (out_sem);

      gst_cuda_context_pop (nullptr);
    }

    in_fence = nullptr;
    out_fence = nullptr;

    gst_clear_object (&fence_data_pool);
    gst_clear_object (&context);
    gst_clear_object (&device);
  }

  GstVideoInfo info;

  D3D12_RESOURCE_DESC desc;
  D3D12_HEAP_PROPERTIES heap_prop;
  D3D12_RESOURCE_ALLOCATION_INFO alloc_info;

  ComPtr < ID3D12Fence > in_fence;
  ComPtr < ID3D12Fence > out_fence;
  guint64 fence_val = 0;
  CUexternalSemaphore in_sem = nullptr;
  CUexternalSemaphore out_sem = nullptr;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout[GST_VIDEO_MAX_PLANES];

  GstD3D12FenceDataPool *fence_data_pool;

  std::shared_ptr < FenceAsyncWaiter > fence_waiter;

  std::mutex lock;
  std::queue < GstCudaD3D12InteropResource * >resource_pool;

  GstCudaContext *context = nullptr;
  GstD3D12Device *device = nullptr;
};

struct _GstCudaD3D12Interop
{
  GstObject parent;

  GstCudaD3D12InteropPrivate *priv;
};

#define gst_cuda_d3d12_interop_parent_class parent_class
G_DEFINE_TYPE (GstCudaD3D12Interop, gst_cuda_d3d12_interop, GST_TYPE_OBJECT);

static void gst_cuda_d3d12_interop_finalize (GObject * object);

static void
gst_cuda_d3d12_interop_class_init (GstCudaD3D12InteropClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_cuda_d3d12_interop_finalize;
}

static void
gst_cuda_d3d12_interop_finalize (GObject * object)
{
  auto self = GST_CUDA_D3D12_INTEROP (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_cuda_d3d12_interop_init (GstCudaD3D12Interop * self)
{
  self->priv = new GstCudaD3D12InteropPrivate ();
}

GstCudaD3D12Interop *
gst_cuda_d3d12_interop_new (GstCudaContext * context, GstD3D12Device * device,
    const GstVideoInfo * info, gboolean is_uploader)
{
  gint64 cuda_luid = 0;
  gint64 d3d_luid = 0;

  g_object_get (context, "dxgi-adapter-luid", &cuda_luid, nullptr);
  g_object_get (device, "adapter-luid", &d3d_luid, nullptr);

  if (cuda_luid != d3d_luid)
    return nullptr;

  auto self = (GstCudaD3D12Interop *)
      g_object_new (GST_TYPE_CUDA_D3D12_INTEROP, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;

  priv->context = (GstCudaContext *) gst_object_ref (context);
  priv->device = (GstD3D12Device *) gst_object_ref (device);

  guint64 size;
  if (!gst_d3d12_get_copyable_footprints (device, info, priv->layout, &size)) {
    gst_object_unref (self);
    return nullptr;
  }

  priv->info = *info;

  D3D12_RESOURCE_DESC desc = { };
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Alignment = 0;
  desc.Width = size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = D3D12_RESOURCE_FLAG_NONE;

  priv->desc = desc;

  D3D12_HEAP_PROPERTIES heap_prop = { };
  heap_prop.Type = D3D12_HEAP_TYPE_DEFAULT;
  heap_prop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  heap_prop.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  heap_prop.CreationNodeMask = 1;
  heap_prop.VisibleNodeMask = 1;

  priv->heap_prop = heap_prop;

  auto device_handle = gst_d3d12_device_get_device_handle (device);
  priv->alloc_info = device_handle->GetResourceAllocationInfo (0, 1, &desc);

  HRESULT hr;
  if (is_uploader) {
    priv->in_fence = gst_d3d12_device_get_fence_handle (device,
        D3D12_COMMAND_LIST_TYPE_COMPUTE);
    hr = device_handle->CreateFence (0, D3D12_FENCE_FLAG_SHARED,
        IID_PPV_ARGS (&priv->out_fence));
  } else {
    priv->out_fence = gst_d3d12_device_get_fence_handle (device,
        D3D12_COMMAND_LIST_TYPE_COMPUTE);
    hr = device_handle->CreateFence (0, D3D12_FENCE_FLAG_SHARED,
        IID_PPV_ARGS (&priv->in_fence));
  }

  if (!gst_d3d12_result (hr, device)) {
    gst_object_unref (self);
    return nullptr;
  }

  HANDLE nt_handle;
  hr = device_handle->CreateSharedHandle (priv->in_fence.Get (),
      nullptr, GENERIC_ALL, nullptr, &nt_handle);
  if (!gst_d3d12_result (hr, device)) {
    gst_object_unref (self);
    return nullptr;
  }

  gst_cuda_context_push (context);

  CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC sem_desc = { };
  sem_desc.type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE;
  sem_desc.handle.win32.handle = nt_handle;

  auto cuda_ret = CuImportExternalSemaphore (&priv->in_sem, &sem_desc);
  CloseHandle (nt_handle);

  if (!gst_cuda_result (cuda_ret)) {
    gst_cuda_context_pop (nullptr);
    gst_object_unref (self);
    return nullptr;
  }

  if (is_uploader) {
    hr = device_handle->CreateSharedHandle (priv->out_fence.Get (), nullptr,
        GENERIC_ALL, nullptr, &nt_handle);
    if (!gst_d3d12_result (hr, device)) {
      gst_cuda_context_pop (nullptr);
      gst_object_unref (self);
      return nullptr;
    }

    sem_desc.handle.win32.handle = nt_handle;
    cuda_ret = CuImportExternalSemaphore (&priv->out_sem, &sem_desc);
    CloseHandle (nt_handle);
    gst_cuda_context_pop (nullptr);

    if (!gst_cuda_result (cuda_ret)) {
      gst_object_unref (self);
      return nullptr;
    }
  }

  priv->fence_waiter =
      std::make_shared < FenceAsyncWaiter > (priv->out_fence.Get ());

  return self;
}

static void
gst_cuda_d3d12_interop_resource_free (GstCudaD3D12InteropResource * resource)
{
  delete resource;
}

static void
gst_cuda_d3d12_interop_resource_release (GstCudaD3D12Interop * interop,
    GstCudaD3D12InteropResource * resource)
{
  auto priv = interop->priv;
  {
    std::lock_guard < std::mutex > lk (priv->lock);
    resource->dispose = nullptr;
    resource->interop = nullptr;
    priv->resource_pool.push (resource);
  }

  gst_object_unref (interop);
}

static gboolean
gst_cuda_d3d12_interop_resource_dispose (GstCudaD3D12InteropResource * resource)
{
  if (!resource->interop)
    return TRUE;

  gst_mini_object_ref (resource);
  gst_cuda_d3d12_interop_resource_release (resource->interop, resource);

  return FALSE;
}

static gboolean
gst_cuda_d3d12_interop_acquire_resource (GstCudaD3D12Interop * self,
    GstCudaD3D12InteropResource ** resource)
{
  auto priv = self->priv;

  *resource = nullptr;

  GstCudaD3D12InteropResource *ret = nullptr;

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    if (!priv->resource_pool.empty ()) {
      ret = priv->resource_pool.front ();
      priv->resource_pool.pop ();
    }
  }

  if (!ret) {
    auto device = gst_d3d12_device_get_device_handle (priv->device);
    ComPtr < ID3D12Resource > resource_12;
    auto hr = device->CreateCommittedResource (&priv->heap_prop,
        D3D12_HEAP_FLAG_SHARED, &priv->desc, D3D12_RESOURCE_STATE_COMMON,
        nullptr, IID_PPV_ARGS (&resource_12));
    if (!gst_d3d12_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't allocate resource");
      return FALSE;
    }

    HANDLE nt_handle;
    hr = device->CreateSharedHandle (resource_12.Get (), nullptr,
        GENERIC_ALL, nullptr, &nt_handle);
    if (!gst_d3d12_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Couldn't create shared handle");
      return FALSE;
    }

    if (!gst_cuda_context_push (priv->context)) {
      GST_ERROR_OBJECT (self, "Couldn't push context");
      CloseHandle (nt_handle);
      return FALSE;
    }

    CUDA_EXTERNAL_MEMORY_HANDLE_DESC mem_desc = { };
    mem_desc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
    mem_desc.handle.win32.handle = nt_handle;
    mem_desc.size = priv->alloc_info.SizeInBytes;
    /* CUDA_EXTERNAL_MEMORY_DEDICATED = 0x1 */
    mem_desc.flags = 0x1;

    CUexternalMemory ext_mem;
    auto cuda_ret = CuImportExternalMemory (&ext_mem, &mem_desc);
    CloseHandle (nt_handle);
    if (!gst_cuda_result (cuda_ret)) {
      GST_ERROR_OBJECT (self, "Couldn't import NT handle");
      gst_cuda_context_pop (nullptr);
      return FALSE;
    }

    CUDA_EXTERNAL_MEMORY_BUFFER_DESC buf_desc = { };
    buf_desc.size = priv->desc.Width;

    CUdeviceptr devptr;
    cuda_ret = CuExternalMemoryGetMappedBuffer (&devptr, ext_mem, &buf_desc);
    if (!gst_cuda_result (cuda_ret)) {
      GST_ERROR_OBJECT (self, "Couldn't get mapped buffer");
      CuDestroyExternalMemory (ext_mem);
      gst_cuda_context_pop (nullptr);
      return FALSE;
    }

    gst_cuda_context_pop (nullptr);

    ret = new GstCudaD3D12InteropResource ();
    gst_mini_object_init (ret, 0, gst_cuda_d3d12_interop_resource_get_type (),
        nullptr, nullptr,
        (GstMiniObjectFreeFunction) gst_cuda_d3d12_interop_resource_free);

    ret->context = (GstCudaContext *) gst_object_ref (priv->context);
    ret->resource = resource_12;
    ret->ext_mem = ext_mem;
    ret->devptr = devptr;
  }

  ret->interop = (GstCudaD3D12Interop *) gst_object_ref (self);
  ret->dispose =
      (GstMiniObjectDisposeFunction) gst_cuda_d3d12_interop_resource_dispose;

  *resource = ret;

  return TRUE;
}

gboolean
gst_cuda_d3d12_interop_upload_async (GstCudaD3D12Interop * interop,
    GstBuffer * dst_cuda, GstBuffer * src_d3d12, GstCudaStream * stream)
{
  GstD3D12Frame frame_12;
  GstVideoFrame frame_cuda;

  auto priv = interop->priv;

  if (!gst_d3d12_frame_map (&frame_12, &priv->info,
          src_d3d12, GST_MAP_READ_D3D12, GST_D3D12_FRAME_MAP_FLAG_NONE)) {
    GST_ERROR_OBJECT (interop, "Couldn't map d3d12 buffer");
    return FALSE;
  }

  if (!gst_d3d12_device_is_equal (priv->device, frame_12.device)) {
    GST_WARNING_OBJECT (interop, "Different d3d12 device");
    gst_d3d12_frame_unmap (&frame_12);
    return FALSE;
  }

  if (!gst_video_frame_map (&frame_cuda, &priv->info, dst_cuda,
          (GstMapFlags) (GST_MAP_WRITE | GST_MAP_CUDA))) {
    GST_ERROR_OBJECT (interop, "Couldn't map cuda buffer");
    gst_d3d12_frame_unmap (&frame_12);
    return FALSE;
  }

  GstCudaD3D12InteropResource *resource;
  if (!gst_cuda_d3d12_interop_acquire_resource (interop, &resource)) {
    GST_ERROR_OBJECT (interop, "Couldn't acquire resource");
    gst_d3d12_frame_unmap (&frame_12);
    gst_video_frame_unmap (&frame_cuda);
    return FALSE;
  }

  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);

  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_MINI_OBJECT (gst_buffer_ref (src_d3d12)));
  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_MINI_OBJECT (gst_mini_object_ref (resource)));

  GstD3D12CopyTextureRegionArgs args[GST_VIDEO_MAX_PLANES] = { };
  D3D12_BOX src_box[GST_VIDEO_MAX_PLANES] = { };
  std::vector < ID3D12Fence * >fences_to_wait;
  std::vector < guint64 > fence_values_to_wait;

  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (&priv->info); i++) {
    auto fence = frame_12.fence[i].fence;
    auto fence_val = frame_12.fence[i].fence_value;

    if (fence) {
      auto completed = fence->GetCompletedValue ();
      if (completed < fence_val) {
        fences_to_wait.push_back (fence);
        fence_values_to_wait.push_back (fence_val);
      }
    }

    src_box[i].left = 0;
    src_box[i].top = 0;
    src_box[i].right = MIN (frame_12.plane_rect[i].right,
        priv->layout[i].Footprint.Width);
    src_box[i].bottom = MIN (frame_12.plane_rect[i].bottom,
        priv->layout[i].Footprint.Height);
    src_box[i].front = 0;
    src_box[i].back = 1;

    args[i].src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    args[i].src.pResource = frame_12.data[i];
    args[i].src.SubresourceIndex = frame_12.subresource_index[i];

    args[i].dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    args[i].dst.pResource = resource->resource.Get ();
    args[i].dst.PlacedFootprint = priv->layout[i];
  }

  guint64 fence_val;
  auto ret = gst_d3d12_device_copy_texture_region (priv->device,
      GST_VIDEO_INFO_N_PLANES (&priv->info), args, fence_data,
      fences_to_wait.size (), fences_to_wait.data (),
      fence_values_to_wait.data (),
      D3D12_COMMAND_LIST_TYPE_COMPUTE, &fence_val);
  gst_d3d12_frame_unmap (&frame_12);

  if (!ret) {
    GST_ERROR_OBJECT (interop, "Couldn't execute d3d12 copy");
    gst_video_frame_unmap (&frame_cuda);
    gst_mini_object_unref (resource);
    return FALSE;
  }

  if (!gst_cuda_context_push (priv->context)) {
    GST_ERROR_OBJECT (interop, "Couldn't push context");
    gst_video_frame_unmap (&frame_cuda);
    gst_mini_object_unref (resource);
    return FALSE;
  }

  CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS wait_params = { };
  wait_params.params.fence.value = fence_val;

  auto stream_handle = gst_cuda_stream_get_handle (stream);
  auto cuda_ret = CuWaitExternalSemaphoresAsync (&priv->in_sem, &wait_params,
      1, stream_handle);
  if (!gst_cuda_result (cuda_ret)) {
    GST_ERROR_OBJECT (interop, "CuWaitExternalSemaphoresAsync failed");
    gst_video_frame_unmap (&frame_cuda);
    gst_mini_object_unref (resource);

    gst_cuda_context_pop (nullptr);
    priv->in_fence->SetEventOnCompletion (fence_val, nullptr);

    return FALSE;
  }

  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&frame_cuda); i++) {
    CUDA_MEMCPY2D copy_params = { };
    guint8 *src_data = (guint8 *) resource->devptr;

    src_data += priv->layout[i].Offset;

    copy_params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    copy_params.srcDevice = (CUdeviceptr) src_data;
    copy_params.srcPitch = priv->layout[i].Footprint.RowPitch;

    copy_params.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    copy_params.dstDevice = (CUdeviceptr)
        GST_VIDEO_FRAME_PLANE_DATA (&frame_cuda, i);
    copy_params.dstPitch = GST_VIDEO_FRAME_PLANE_STRIDE (&frame_cuda, i);

    copy_params.WidthInBytes = GST_VIDEO_FRAME_COMP_WIDTH (&frame_cuda, i) *
        GST_VIDEO_FRAME_COMP_PSTRIDE (&frame_cuda, i);
    copy_params.Height = GST_VIDEO_FRAME_COMP_HEIGHT (&frame_cuda, i);

    cuda_ret = CuMemcpy2DAsync (&copy_params, stream_handle);
    if (!gst_cuda_result (cuda_ret)) {
      GST_ERROR_OBJECT (interop, "CuMemcpy2DAsync failed");
      gst_video_frame_unmap (&frame_cuda);
      gst_mini_object_unref (resource);

      gst_cuda_context_pop (nullptr);
      priv->in_fence->SetEventOnCompletion (fence_val, nullptr);

      return FALSE;
    }
  }

  priv->fence_val++;

  CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS signal_params = { };
  signal_params.params.fence.value = priv->fence_val;

  cuda_ret = CuSignalExternalSemaphoresAsync (&priv->out_sem, &signal_params,
      1, stream_handle);
  gst_cuda_context_pop (nullptr);
  gst_video_frame_unmap (&frame_cuda);

  if (!gst_cuda_result (cuda_ret)) {
    GST_ERROR_OBJECT (interop, "CuSignalExternalSemaphoresAsync failed");
    gst_mini_object_unref (resource);
    priv->fence_val--;

    return FALSE;
  }

  priv->fence_waiter->wait_async (priv->fence_val, resource);

  return TRUE;
}

gboolean
gst_cuda_d3d12_interop_download_async (GstCudaD3D12Interop * interop,
    GstBuffer * dst_d3d12, GstBuffer * src_cuda, GstCudaStream * stream)
{
  GstD3D12Frame frame_12;
  GstVideoFrame frame_cuda;

  auto priv = interop->priv;

  if (!gst_d3d12_frame_map (&frame_12, &priv->info,
          dst_d3d12, GST_MAP_WRITE_D3D12, GST_D3D12_FRAME_MAP_FLAG_NONE)) {
    GST_ERROR_OBJECT (interop, "Couldn't map d3d12 buffer");
    return FALSE;
  }

  if (!gst_d3d12_device_is_equal (priv->device, frame_12.device)) {
    GST_WARNING_OBJECT (interop, "Different d3d12 device");
    gst_d3d12_frame_unmap (&frame_12);
    return FALSE;
  }

  if (!gst_video_frame_map (&frame_cuda, &priv->info, src_cuda,
          (GstMapFlags) (GST_MAP_READ | GST_MAP_CUDA))) {
    GST_ERROR_OBJECT (interop, "Couldn't map cuda buffer");
    gst_d3d12_frame_unmap (&frame_12);
    return FALSE;
  }

  GstCudaD3D12InteropResource *resource;
  if (!gst_cuda_d3d12_interop_acquire_resource (interop, &resource)) {
    GST_ERROR_OBJECT (interop, "Couldn't acquire resource");
    gst_d3d12_frame_unmap (&frame_12);
    gst_video_frame_unmap (&frame_cuda);
    return FALSE;
  }

  if (!gst_cuda_context_push (priv->context)) {
    GST_ERROR_OBJECT (interop, "Couldn't push context");
    gst_d3d12_frame_unmap (&frame_12);
    gst_video_frame_unmap (&frame_cuda);
    return FALSE;
  }

  auto stream_handle = gst_cuda_stream_get_handle (stream);
  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&frame_cuda); i++) {
    CUDA_MEMCPY2D copy_params = { };
    guint8 *dst_data = (guint8 *) resource->devptr;

    dst_data += priv->layout[i].Offset;

    copy_params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    copy_params.srcDevice = (CUdeviceptr)
        GST_VIDEO_FRAME_PLANE_DATA (&frame_cuda, i);
    copy_params.srcPitch = GST_VIDEO_FRAME_PLANE_STRIDE (&frame_cuda, i);

    copy_params.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    copy_params.dstDevice = (CUdeviceptr) dst_data;
    copy_params.dstPitch = priv->layout[i].Footprint.RowPitch;

    copy_params.WidthInBytes = GST_VIDEO_FRAME_COMP_WIDTH (&frame_cuda, i) *
        GST_VIDEO_FRAME_COMP_PSTRIDE (&frame_cuda, i);
    copy_params.Height = GST_VIDEO_FRAME_COMP_HEIGHT (&frame_cuda, i);

    auto cuda_ret = CuMemcpy2DAsync (&copy_params, stream_handle);
    if (!gst_cuda_result (cuda_ret)) {
      GST_ERROR_OBJECT (interop, "CuMemcpy2DAsync failed");
      gst_video_frame_unmap (&frame_cuda);
      gst_d3d12_frame_unmap (&frame_12);
      gst_mini_object_unref (resource);
      gst_cuda_context_pop (nullptr);

      return FALSE;
    }
  }

  priv->fence_val++;
  CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS signal_params = { };
  signal_params.params.fence.value = priv->fence_val;

  auto cuda_ret = CuSignalExternalSemaphoresAsync (&priv->in_sem,
      &signal_params, 1, stream_handle);
  gst_cuda_context_pop (nullptr);
  gst_video_frame_unmap (&frame_cuda);

  if (!gst_cuda_result (cuda_ret)) {
    GST_ERROR_OBJECT (interop, "CuSignalExternalSemaphoresAsync failed");
    gst_mini_object_unref (resource);
    priv->fence_val--;

    return FALSE;
  }

  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);

  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_MINI_OBJECT (gst_mini_object_ref (resource)));

  GstD3D12CopyTextureRegionArgs args[GST_VIDEO_MAX_PLANES] = { };
  D3D12_BOX src_box[GST_VIDEO_MAX_PLANES] = { };

  for (guint i = 0; i < GST_VIDEO_INFO_N_PLANES (&priv->info); i++) {
    src_box[i].left = 0;
    src_box[i].top = 0;
    src_box[i].right = MIN (frame_12.plane_rect[i].right,
        priv->layout[i].Footprint.Width);
    src_box[i].bottom = MIN (frame_12.plane_rect[i].bottom,
        priv->layout[i].Footprint.Height);
    src_box[i].front = 0;
    src_box[i].back = 1;

    args[i].src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    args[i].src.pResource = resource->resource.Get ();
    args[i].src.PlacedFootprint = priv->layout[i];

    args[i].dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    args[i].dst.pResource = frame_12.data[i];
    args[i].dst.SubresourceIndex = frame_12.subresource_index[i];
  }

  auto in_fence = priv->in_fence.Get ();
  guint64 fence_val;
  auto ret = gst_d3d12_device_copy_texture_region (priv->device,
      GST_VIDEO_INFO_N_PLANES (&priv->info), args, fence_data,
      1, &in_fence, &priv->fence_val, D3D12_COMMAND_LIST_TYPE_COMPUTE,
      &fence_val);
  gst_d3d12_frame_unmap (&frame_12);

  if (!ret) {
    GST_ERROR_OBJECT (interop, "Couldn't execute d3d12 copy");
    gst_mini_object_unref (resource);
    return FALSE;
  }

  priv->fence_waiter->wait_async (priv->fence_val, resource);

  gst_d3d12_buffer_set_fence (dst_d3d12, priv->out_fence.Get (), fence_val,
      FALSE);

  return TRUE;
}
