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

struct AllocBlock
{
  AllocBlock() = delete;

  explicit AllocBlock (UINT64 offset, UINT64 size)
    : offset_(offset), size_(size) {}

  AllocBlock (const AllocBlock & other)
    : offset_(other.offset_), size_(other.size_) {}

  AllocBlock (AllocBlock && other)
    : offset_(other.offset_), size_(other.size_) {}

  AllocBlock& operator=(const AllocBlock & other)
  {
    offset_ = other.offset_;
    size_ = other.size_;
    return *this;
  }

  UINT64 offset_ = 0;
  UINT64 size_ = 0;
};

struct DecoderBuffer
{
  DecoderBuffer() = delete;
  explicit DecoderBuffer (ID3D12Resource * resource, UINT64 size,
      guint8 * mapped_data, UINT64 id, const gchar * debug_name)
  {
    resource_ = resource;
    mapped_data_ = mapped_data;
    alloc_vec_.emplace_back (0, size);
    largest_block_ = size;
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
    if (largest_block_ >= size)
      return true;

    return false;
  }

  bool IsUnused ()
  {
    if (alloc_vec_.size () == 1 && alloc_vec_[0].size_ == alloc_size_)
      return true;

    return false;
  }

  void InsertBs (const D3D12_VIDEO_DECODE_COMPRESSED_BITSTREAM & bs)
  {
    if (bs.Size == 0)
      return;

    g_assert (bs.Offset + bs.Size <= alloc_size_);

#ifndef GST_DISABLE_GST_DEBUG
    if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_TRACE) {
      auto it = alloc_vec_.cbegin ();
      guint i = 0;
      while (it != alloc_vec_.cend ()) {
        GST_TRACE_ID (debug_name_.c_str (), "BeforeInsert[%u], Offset: %"
            G_GUINT64_FORMAT ", Size: %" G_GSIZE_FORMAT, i,
            it->offset_, it->size_);
        it++;
        i++;
      }
    }
#endif

    if (largest_block_ < bs.Size)
      largest_block_ = bs.Size;

    if (alloc_vec_.empty ()) {
      alloc_vec_.emplace_back (bs.Offset, bs.Size);
      GST_TRACE_ID (debug_name_.c_str (),
          "[%" G_GUINT64_FORMAT "] pushed to empty array", id_);
      return;
    }

    auto new_block = AllocBlock (bs.Offset, bs.Size);
    auto end_offset = bs.Offset + bs.Size;

    auto it = alloc_vec_.insert (std::lower_bound (alloc_vec_.begin (),
        alloc_vec_.end (), new_block), new_block);
    auto next = std::next (it);
    if (it == alloc_vec_.begin ()) {
      /* Check if we can merge this block with the next block */
      bool merged = false;
      if (next != alloc_vec_.end () && next->offset_ == end_offset) {
        /* contiguous, do merge */
        merged = true;
        it->size_ += next->size_;
        if (largest_block_ < it->size_)
          largest_block_ = it->size_;

        alloc_vec_.erase (next);
      }

      GST_TRACE_ID (debug_name_.c_str (),
          "InsertedPos: begin, MergeNext: %d", merged);
    } else if (next == alloc_vec_.end ()) {
      /* This is the last element, and not the first element.
       * Check if this block can be merged with previous one */
      auto prev = std::prev (it);
      bool merged = false;
      if (prev->offset_ + prev->size_ == it->offset_) {
        /* contiguous, do merge */
        merged = true;
        prev->size_ += it->size_;
        if (largest_block_ < prev->size_)
          largest_block_ = prev->size_;
        alloc_vec_.erase (it);
      }

      GST_TRACE_ID (debug_name_.c_str (),
          "InsertedPos: end, MergePrev: %d", merged);
    } else {
      /* Checks if we can merge new block with prev and/or next */
      auto prev = std::prev (it);
      bool merge_prev = false;
      bool merge_next = false;
      if (prev->offset_ + prev->size_ == it->offset_) {
        /* contiguous, do merge */
        merge_prev = true;
        prev->size_ += it->size_;
        if (largest_block_ < prev->size_)
          largest_block_ = prev->size_;
        next = alloc_vec_.erase (it);
        it = prev;
      }

      if (next->offset_ == end_offset) {
        /* contiguous, do merge */
        merge_next = true;
        it->size_ += next->size_;
        if (largest_block_ < it->size_)
          largest_block_ = it->size_;

        alloc_vec_.erase (next);
      }

      GST_TRACE_ID (debug_name_.c_str (),
          "InsertedPos: end, MergePrev: %d, MergeNext", merge_prev,
          merge_next);
    }

#ifndef GST_DISABLE_GST_DEBUG
    if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_TRACE) {
      auto it = alloc_vec_.cbegin ();
      guint i = 0;
      while (it != alloc_vec_.cend ()) {
        GST_TRACE_ID (debug_name_.c_str (), "AfterInsert[%u], Offset: %"
            G_GUINT64_FORMAT ", Size: %" G_GUINT64_FORMAT, i,
            it->offset_, it->size_);
        it++;
        i++;
      }
    }
#endif
  }

  bool PopBs (UINT64 size, D3D12_VIDEO_DECODE_COMPRESSED_BITSTREAM & bs)
  {
    if (!HasSpace (size))
      return false;

    auto it = alloc_vec_.begin ();
    bool found = false;
    largest_block_ = 0;

    /* Extracts allocation block and updates largest block size after
     * extracted */
    while (it != alloc_vec_.end ()) {
      if (!found) {
        if (it->size_ >= size) {
          bs.pBuffer = resource_.Get ();
          bs.Offset = it->offset_;
          bs.Size = size;
          if (it->size_ == size) {
            it = alloc_vec_.erase (it);
          } else {
            it->offset_ += size;
            it->size_ -= size;
            if (largest_block_ < it->size_)
              largest_block_ = it->size_;

            it++;
          }

          found = true;
          continue;
        }
      }

      if (largest_block_ < it->size_)
        largest_block_ = it->size_;

      it++;
    }

    g_assert (found);

    return true;
  }

  ComPtr<ID3D12Resource> resource_;
  std::vector<AllocBlock> alloc_vec_;
  UINT64 largest_block_;
  UINT64 alloc_size_;
  guint8 *mapped_data_;
  UINT64 id_;
  std::string debug_name_;
};

struct _GstD3D12DecoderCpb : public GstMiniObject
{
  GstD3D12DecoderCpbPool *pool = nullptr;
  std::shared_ptr<DecoderBuffer> buffer;
  ComPtr<ID3D12CommandAllocator> ca;
  D3D12_VIDEO_DECODE_COMPRESSED_BITSTREAM bs = { };
};

bool operator<(const AllocBlock & a, const AllocBlock & b)
{
  return a.offset_ < b.offset_;
}

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

  std::mutex lock;
};

struct _GstD3D12DecoderCpbPool
{
  GstObject parent;
  GstD3D12Device *device;
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
  gst_clear_object (&self->device);

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
    if (cpb->buffer) {
      cpb->buffer->InsertBs (cpb->bs);
#ifndef GST_DISABLE_GST_DEBUG
      const auto & buffer = cpb->buffer;
      GST_TRACE_OBJECT (pool, "Buffer[%" G_GUINT64_FORMAT "] status, "
          "alloc-size %" G_GUINT64_FORMAT ", num-free-blocks %"
          G_GSIZE_FORMAT ", largest-block-size %" G_GUINT64_FORMAT,
          buffer->id_, buffer->alloc_size_, buffer->alloc_vec_.size (),
          buffer->largest_block_);
#endif
    }
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
    ComPtr < ID3D12Resource > resource;

    GST_DEBUG_OBJECT (pool, "Allocating new buffer, size %" G_GUINT64_FORMAT,
        alloc_size);

    auto hr = priv->device->CreateCommittedResource (&heap_prop,
        D3D12_HEAP_FLAG_CREATE_NOT_ZEROED, &desc,
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

#ifndef GST_DISABLE_GST_DEBUG
  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_TRACE) {
    GST_TRACE_OBJECT (pool, "Total num-buffers: %" G_GSIZE_FORMAT
        ", Buffer[%" G_GUINT64_FORMAT "] status, "
        "alloc-size %" G_GUINT64_FORMAT ", num-free-blocks %"
        G_GSIZE_FORMAT ", largest-block-size %" G_GUINT64_FORMAT
        ", popped-offset: %" G_GUINT64_FORMAT ", popped-size %"
        G_GUINT64_FORMAT, priv->buffer_pool.size (),
        buffer->id_, buffer->alloc_size_, buffer->alloc_vec_.size (),
        buffer->largest_block_, ret->bs.Offset, ret->bs.Size);

    auto it = buffer->alloc_vec_.cbegin ();
    guint i = 0;
    while (it != buffer->alloc_vec_.cend ()) {
      GST_TRACE_OBJECT (pool, "Remaining[%u] Offset: %" G_GUINT64_FORMAT
          ", Size: %" G_GSIZE_FORMAT, i, it->offset_, it->size_);
      it++;
      i++;
    }
  }
#endif
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
