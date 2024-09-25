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
#include "config.h"
#endif

#include "gstd3d12decodercpbpool.h"
#include <gst/d3d12/gstd3d12-private.h>
#include <directx/d3dx12.h>
#include <wrl.h>
#include <mutex>
#include <memory>
#include <vector>
#include <queue>
#include <iterator>
#include <algorithm>
#include <string>

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_decoder_cpb_pool_debug);
#define GST_CAT_DEFAULT gst_d3d12_decoder_cpb_pool_debug

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

struct DecoderBuffer
{
  DecoderBuffer() = delete;
  explicit DecoderBuffer (ID3D12Resource * resource, UINT64 size,
      guint8 * mapped_data, UINT64 id, const gchar * debug_name)
  {
    resource_ = resource;
    mapped_data_ = mapped_data;
    alloc_size_ = size;
    id_ = id;
    if (debug_name)
      debug_name_ = debug_name;

    GST_DEBUG_ID (debug_name_.c_str (), "Create new buffer[%"
        G_GUINT64_FORMAT "], size: %" G_GUINT64_FORMAT, id_, alloc_size_);
  }

  ~DecoderBuffer ()
  {
    GST_DEBUG_ID (debug_name_.c_str (), "Releasing buffer[%"
        G_GUINT64_FORMAT "], size: %" G_GUINT64_FORMAT, id_, alloc_size_);
  }

  bool HasSpace (UINT64 size)
  {
    if (!IsUnused ())
      return false;

    if (alloc_size_ >= size)
      return true;

    return false;
  }

  bool IsUnused ()
  {
    return is_unused_;
  }

  void MarkUnused ()
  {
    is_unused_ = true;
  }

  bool PopBs (UINT64 size, D3D12_VIDEO_DECODE_COMPRESSED_BITSTREAM & bs)
  {
    if (!HasSpace (size))
      return false;

    bs.pBuffer = resource_.Get ();
    bs.Offset = 0;
    bs.Size = size;

    is_unused_ = false;

    return true;
  }

  ComPtr<ID3D12Resource> resource_;
  UINT64 alloc_size_;
  guint8 *mapped_data_;
  UINT64 id_;
  std::string debug_name_;
  bool is_unused_ = true;
};

struct _GstD3D12DecoderCpb : public GstMiniObject
{
  GstD3D12DecoderCpbPool *pool = nullptr;
  std::shared_ptr<DecoderBuffer> buffer;
  ComPtr<ID3D12CommandAllocator> ca;
  D3D12_VIDEO_DECODE_COMPRESSED_BITSTREAM bs = { };
};

bool operator<(const std::shared_ptr<DecoderBuffer> & a,
    const std::shared_ptr<DecoderBuffer> & b)
{
  return a->alloc_size_ < b->alloc_size_;
}

struct GstD3D12DecoderCpbPoolPrivate
{
  ~GstD3D12DecoderCpbPoolPrivate ()
  {
    while (!cpb_pool.empty ()) {
      auto cpb = cpb_pool.front ();
      cpb_pool.pop ();
      gst_mini_object_unref (cpb);
    }

    buffer_pool.clear ();
  }

  ComPtr<ID3D12Device> device;
  std::vector<std::shared_ptr<DecoderBuffer>> buffer_pool;
  std::queue<GstD3D12DecoderCpb *> cpb_pool;
  UINT64 buffer_id = 0;
  UINT64 max_alloc_size = 0;
  guint allocated_ca_size = 0;
  bool supports_non_zeroed = false;

  std::mutex lock;
};

struct _GstD3D12DecoderCpbPool
{
  GstObject parent;
  GstD3D12DecoderCpbPoolPrivate *priv;
};
/* *INDENT-ON* */

GST_DEFINE_MINI_OBJECT_TYPE (GstD3D12DecoderCpb, gst_d3d12_decoder_cpb);

static void gst_d3d12_decoder_cpb_pool_finalize (GObject * object);

#define gst_d3d12_decoder_cpb_pool_parent_class parent_class
G_DEFINE_TYPE (GstD3D12DecoderCpbPool,
    gst_d3d12_decoder_cpb_pool, GST_TYPE_OBJECT);

static void
gst_d3d12_decoder_cpb_pool_class_init (GstD3D12DecoderCpbPoolClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_d3d12_decoder_cpb_pool_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_decoder_cpb_pool_debug,
      "d3d12decodercpbpool", 0, "d3d12decodercpbpool");
}

static void
gst_d3d12_decoder_cpb_pool_init (GstD3D12DecoderCpbPool * self)
{
  self->priv = new GstD3D12DecoderCpbPoolPrivate ();
}

static void
gst_d3d12_decoder_cpb_pool_finalize (GObject * object)
{
  auto self = GST_D3D12_DECODER_CPB_POOL (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

GstD3D12DecoderCpbPool *
gst_d3d12_decoder_cpb_pool_new (ID3D12Device * device)
{
  g_return_val_if_fail (device, nullptr);

  auto self = (GstD3D12DecoderCpbPool *)
      g_object_new (GST_TYPE_D3D12_DECODER_CPB_POOL, nullptr);
  gst_object_ref_sink (self);

  auto priv = self->priv;
  priv->device = device;

  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = { };
  auto hr =
      device->CheckFeatureSupport (D3D12_FEATURE_D3D12_OPTIONS7, &options7,
      sizeof (options7));
  if (SUCCEEDED (hr))
    priv->supports_non_zeroed = true;

  return self;
}

/* *INDENT-OFF* */
static void
gst_d3d12_decoder_cpb_pool_release (GstD3D12DecoderCpbPool * pool,
    GstD3D12DecoderCpb * cpb)
{
  auto priv = pool->priv;
  {
    std::lock_guard < std::mutex > lk (priv->lock);
    GST_LOG_OBJECT (pool, "Releasing cpb, bitstream %p, Offset: %"
        G_GUINT64_FORMAT ", Size %" G_GUINT64_FORMAT,
        cpb->bs.pBuffer, cpb->bs.Offset, cpb->bs.Size);
    cpb->dispose = nullptr;
    cpb->pool = nullptr;
    if (cpb->buffer)
      cpb->buffer->MarkUnused ();
    cpb->buffer = nullptr;
    priv->cpb_pool.push (cpb);
  }

  gst_object_unref (pool);
}
/* *INDENT-ON* */

static gboolean
gst_d3d12_decoder_cpb_dispose (GstD3D12DecoderCpb * cpb)
{
  if (!cpb->pool)
    return TRUE;

  gst_mini_object_ref (cpb);
  gst_d3d12_decoder_cpb_pool_release (cpb->pool, cpb);

  return FALSE;
}

static void
gst_d3d12_decoder_cpb_free (GstD3D12DecoderCpb * cpb)
{
  delete cpb;
}

#define ROUND_UP_N(num,align) \
    (((((UINT64) num) + (((UINT64) align) - 1)) & ~(((UINT64) align) - 1)))

/* *INDENT-OFF* */
HRESULT
gst_d3d12_decoder_cpb_pool_acquire (GstD3D12DecoderCpbPool * pool,
    gpointer data, gsize size, GstD3D12DecoderCpb ** cpb)
{
  g_return_val_if_fail (GST_IS_D3D12_DECODER_CPB_POOL (pool), E_INVALIDARG);
  g_return_val_if_fail (data, E_INVALIDARG);
  g_return_val_if_fail (size > 0, E_INVALIDARG);

  auto priv = pool->priv;
  std::shared_ptr<DecoderBuffer> buffer;

  UINT64 aligned_size = ROUND_UP_N (size,
      D3D12_VIDEO_DECODE_MIN_BITSTREAM_OFFSET_ALIGNMENT);
  GST_DEBUG_OBJECT (pool, "Uploading bitstream, size: %" G_GSIZE_FORMAT
      ", aligned-size %" G_GUINT64_FORMAT, size, aligned_size);

  std::unique_lock < std::mutex > lk (priv->lock);
  /* buffers are sorted in increasing size order. Try to find available
   * blocks from largest buffer, in order to release unused small
   * buffers efficiently */
  for (auto it = priv->buffer_pool.rbegin ();
      it != priv->buffer_pool.rend(); it++) {
    auto & tmp = *it;
    if (tmp->HasSpace (aligned_size)) {
      GST_DEBUG_OBJECT (pool, "Buffer[%" G_GUINT64_FORMAT "] has space",
          tmp->id_);
      buffer = tmp;
      break;
    }
  }

  if (!buffer) {
    UINT64 alloc_size = ROUND_UP_N (aligned_size,
        D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);

    alloc_size = MAX (alloc_size, priv->max_alloc_size);

    D3D12_HEAP_PROPERTIES heap_prop =
        CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer (alloc_size);
    D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;
    if (priv->supports_non_zeroed)
      heap_flags = D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

    ComPtr < ID3D12Resource > resource;

    GST_DEBUG_OBJECT (pool, "Allocating new buffer, size %" G_GUINT64_FORMAT,
        alloc_size);

    auto hr = priv->device->CreateCommittedResource (&heap_prop,
        heap_flags, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS (&resource));
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (pool, "Couldn't allocate upload resource");
      return hr;
    }

    D3D12_RANGE range;
    range.Begin = 0;
    range.End = 0;

    guint8 *mapped_data;
    hr = resource->Map (0, &range, (void **) &mapped_data);
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (pool, "Couldn't map resource");
      return hr;
    }

    buffer = std::make_shared<DecoderBuffer> (resource.Get (), desc.Width,
        mapped_data, priv->buffer_id, GST_OBJECT_NAME (pool));
    priv->buffer_id++;

    priv->buffer_pool.insert (std::lower_bound (priv->buffer_pool.begin (),
        priv->buffer_pool.end (), buffer), buffer);
    priv->max_alloc_size = alloc_size;
  }

  GstD3D12DecoderCpb *ret = nullptr;
  if (!priv->cpb_pool.empty ()) {
    ret = priv->cpb_pool.front ();
    priv->cpb_pool.pop ();
  } else {
    ComPtr<ID3D12CommandAllocator> ca;
    auto hr = priv->device->CreateCommandAllocator (
          D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE, IID_PPV_ARGS (&ca));
    if (FAILED (hr)) {
      GST_ERROR_OBJECT (pool, "Couldn't create command allocator");
      return hr;
    }

    ret = new GstD3D12DecoderCpb ();
    gst_mini_object_init (ret, 0, gst_d3d12_decoder_cpb_get_type (),
      nullptr, nullptr, (GstMiniObjectFreeFunction) gst_d3d12_decoder_cpb_free);

    ret->ca = ca;
    priv->allocated_ca_size++;
  }

  auto pop_ret = buffer->PopBs (aligned_size, ret->bs);
  g_assert (pop_ret);

  /* Release unused buffers */
  if (priv->buffer_pool.size () > priv->allocated_ca_size) {
    auto it = priv->buffer_pool.begin ();

    while (it != priv->buffer_pool.end () &&
        priv->buffer_pool.size () > priv->allocated_ca_size) {
      auto tmp = *it;
      if (tmp == buffer || !tmp->IsUnused ()) {
        it++;
        continue;
      }

      GST_DEBUG_OBJECT (pool, "Releasing unused buffer[%" G_GUINT64_FORMAT
            "], Size: %" G_GUINT64_FORMAT, tmp->id_, tmp->alloc_size_);
      it = priv->buffer_pool.erase (it);
    }
  }

  lk.unlock ();

  memcpy (buffer->mapped_data_ + ret->bs.Offset, data, size);

  ret->pool = (GstD3D12DecoderCpbPool *) gst_object_ref (pool);
  ret->dispose = (GstMiniObjectDisposeFunction) gst_d3d12_decoder_cpb_dispose;
  ret->buffer = buffer;

  *cpb = ret;

  return S_OK;
}
/* *INDENT-ON* */

GstD3D12DecoderCpb *
gst_d3d12_decoder_cpb_ref (GstD3D12DecoderCpb * cpb)
{
  return (GstD3D12DecoderCpb *) gst_mini_object_ref (cpb);
}

void
gst_d3d12_decoder_cpb_unref (GstD3D12DecoderCpb * cpb)
{
  gst_mini_object_unref (cpb);
}

gboolean
gst_d3d12_decoder_cpb_get_bitstream (GstD3D12DecoderCpb * cpb,
    D3D12_VIDEO_DECODE_COMPRESSED_BITSTREAM * bs)
{
  g_return_val_if_fail (cpb, FALSE);
  g_return_val_if_fail (bs, FALSE);

  *bs = cpb->bs;

  return TRUE;
}

ID3D12CommandAllocator *
gst_d3d12_decoder_cpb_get_command_allocator (GstD3D12DecoderCpb * cpb)
{
  g_return_val_if_fail (cpb, nullptr);

  return cpb->ca.Get ();
}
