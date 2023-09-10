/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d12decoder.h"
#include "gstd3d12device.h"
#include "gstd3d12utils.h"
#include "gstd3d12format.h"
#include "gstd3d12memory.h"
#include "gstd3d12fence.h"
#include <wrl.h>
#include <string.h>
#include <mutex>
#include <set>
#include <vector>
#include <queue>
#include <string>
#include <vector>
#include <memory>
#include <atomic>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d12_decoder_debug);
#define GST_CAT_DEFAULT gst_d3d12_decoder_debug

struct DecoderFormat
{
  GstDxvaCodec codec;
  const GUID decode_profile;
  DXGI_FORMAT format;
};

static const DecoderFormat format_list[] = {
  {GST_DXVA_CODEC_H264, D3D12_VIDEO_DECODE_PROFILE_H264, DXGI_FORMAT_NV12}
};

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

class GstD3D12Dpb
{
public:
  GstD3D12Dpb (guint8 size) : size_(size)
  {
    textures_.resize (size);
    subresources_.resize (size);
    heaps_.resize (size);

    for (guint i = 0; i < size; i++) {
      dxva_id_.push (i);
      textures_[i] = nullptr;
      subresources_[i] = 0;
      heaps_[i] = nullptr;
    }
  }

  guint8 Acquire (GstD3D12Memory * mem, ID3D12VideoDecoderHeap * heap)
  {
    std::lock_guard <std::mutex> lk (lock_);
    if (dxva_id_.empty ())
      return 0xff;

    guint8 ret = dxva_id_.front ();
    dxva_id_.pop ();

    GstD3D12Memory *dmem;
    ID3D12Resource *resource;
    UINT subresource = 0;

    dmem = GST_D3D12_MEMORY_CAST (mem);
    resource = gst_d3d12_memory_get_resource_handle (dmem);
    gst_d3d12_memory_get_subresource_index (dmem, 0, &subresource);

    textures_[ret] = resource;
    subresources_[ret] = subresource;
    heaps_[ret] = heap;

    return ret;
  }

  void Release (guint8 id)
  {
    std::lock_guard <std::mutex> lk (lock_);
    if (id == 0xff || id >= size_)
      return;

    dxva_id_.push (id);

    textures_[id] = nullptr;
    subresources_[id] = 0;
    heaps_[id] = nullptr;
  }

  guint8 GetSize ()
  {
    return size_;
  }

  ID3D12Resource ** GetTextures ()
  {
    return &textures_[0];
  }

  UINT * GetSubresources ()
  {
    return &subresources_[0];
  }

  ID3D12VideoDecoderHeap ** GetHeaps ()
  {
    return &heaps_[0];
  }

private:
  std::queue<guint8> dxva_id_;
  std::mutex lock_;
  guint size_;
  std::vector<ID3D12Resource *> textures_;
  std::vector<UINT> subresources_;
  std::vector<ID3D12VideoDecoderHeap *> heaps_;
};

struct GstD3D12DecoderPicture : public GstMiniObject
{
  GstD3D12DecoderPicture (GstMemory * dpb_mem, GstMemory * out_mem,
      std::shared_ptr<GstD3D12Dpb> d3d12_dpb,
      ID3D12VideoDecoderHeap * decoder_heap, guint8 dxva_id)
      : mem(dpb_mem), output_mem(out_mem), dpb(d3d12_dpb), heap(decoder_heap)
      , view_id(dxva_id)
  {
  }

  ~GstD3D12DecoderPicture ()
  {
    auto d3d12_dpb = dpb.lock ();
    if (d3d12_dpb)
      d3d12_dpb->Release (view_id);

    if (mem)
      gst_memory_unref (mem);
    if (output_mem)
      gst_memory_unref (output_mem);
  }

  GstMemory *mem;
  GstMemory *output_mem;
  ComPtr<ID3D12VideoDecoderHeap> heap;
  std::weak_ptr<GstD3D12Dpb> dpb;

  guint8 view_id;
};

#define GST_TYPE_D3D12_DECODER_PICTURE (gst_d3d12_decoder_picture_get_type ())
GST_DEFINE_MINI_OBJECT_TYPE (GstD3D12DecoderPicture, gst_d3d12_decoder_picture);

struct GstD3D12DecoderPrivate
{
  GstD3D12DecoderPrivate ()
  {
    fence_value = 1;
  }

  ~GstD3D12DecoderPrivate()
  {
    if (input_state)
      gst_video_codec_state_unref (input_state);

    if (output_state)
      gst_video_codec_state_unref (output_state);

    if (fence) {
      gst_d3d12_fence_wait (fence);
      gst_d3d12_fence_unref (fence);
    }

    if (allocator) {
      gst_d3d12_allocator_set_active (allocator, FALSE);
      gst_object_unref (allocator);
    }

    if (output_allocator) {
      gst_d3d12_allocator_set_active (output_allocator, FALSE);
      gst_object_unref (output_allocator);
    }

    if (device)
      gst_object_unref (device);
  }

  GstD3D12Device *device = nullptr;

  GstDxvaCodec codec = GST_DXVA_CODEC_NONE;

  /* Allocator for dpb textures */
  GstD3D12Allocator *allocator = nullptr;

  /* Used for output */
  GstD3D12Allocator *output_allocator = nullptr;

  gboolean configured = FALSE;
  gboolean opened = FALSE;

  GstVideoInfo info;
  GstVideoInfo output_info;
  gint crop_x = 0;
  gint crop_y = 0;
  gint coded_width = 0;
  gint coded_height = 0;
  DXGI_FORMAT decoder_format = DXGI_FORMAT_UNKNOWN;
  gboolean reference_only = FALSE;

  D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT support = { 0, };

  /* For staging */
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout[GST_VIDEO_MAX_PLANES] = { 0, };

  GstVideoCodecState *input_state = nullptr;
  GstVideoCodecState *output_state = nullptr;

  /* Internal pool params */
  gint aligned_width = 0;
  gint aligned_height = 0;
  guint dpb_size = 0;

  ComPtr<ID3D12VideoDevice> video_device;

  ComPtr<ID3D12VideoDecoder> decoder;
  ComPtr<ID3D12VideoDecoderHeap> heap;
  ComPtr<ID3D12CommandAllocator> ca;
  ComPtr<ID3D12VideoDecodeCommandList> cl;
  ComPtr<ID3D12CommandQueue> cq;
  ComPtr<ID3D12Resource> bitstream;
  gsize bitstream_size = 0;

  std::shared_ptr<GstD3D12Dpb> dpb;

  /* Used for download decoded picture to staging */
  ComPtr<ID3D12CommandAllocator> copy_ca;
  ComPtr<ID3D12GraphicsCommandList> copy_cl;
  ComPtr<ID3D12CommandQueue> copy_cq;

  ComPtr<ID3D12Resource> staging;

  GstD3D12Fence *fence = nullptr;
  std::atomic<UINT64> fence_value;

  gint64 luid;

  std::mutex lock;
};
/* *INDENT-ON* */

struct _GstD3D12Decoder
{
  GstObject parent;

  GstD3D12DecoderPrivate *priv;
};

static void gst_d3d12_decoder_finalize (GObject * object);

#define parent_class gst_d3d12_decoder_parent_class
G_DEFINE_TYPE (GstD3D12Decoder, gst_d3d12_decoder, GST_TYPE_OBJECT);

static void
gst_d3d12_decoder_class_init (GstD3D12DecoderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_d3d12_decoder_finalize;
}

static void
gst_d3d12_decoder_init (GstD3D12Decoder * self)
{
  GstD3D12DecoderPrivate *priv;

  self->priv = priv = new GstD3D12DecoderPrivate ();
}

static void
gst_d3d12_decoder_finalize (GObject * object)
{
  GstD3D12Decoder *self = GST_D3D12_DECODER (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_decoder_clear_resource (GstD3D12Decoder * self)
{
  GstD3D12DecoderPrivate *priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  gst_d3d12_fence_wait (priv->fence);

  if (priv->allocator) {
    gst_d3d12_allocator_set_active (priv->allocator, FALSE);
    gst_clear_object (&priv->allocator);
  }

  if (priv->output_allocator) {
    gst_d3d12_allocator_set_active (priv->output_allocator, FALSE);
    gst_clear_object (&priv->output_allocator);
  }

  priv->heap = nullptr;
  priv->staging = nullptr;
}

static void
gst_d3d12_decoder_reset (GstD3D12Decoder * self)
{
  GstD3D12DecoderPrivate *priv = self->priv;

  gst_d3d12_decoder_clear_resource (self);
  priv->dpb_size = 0;

  priv->configured = FALSE;
  priv->opened = FALSE;

  g_clear_pointer (&priv->output_state, gst_video_codec_state_unref);
  g_clear_pointer (&priv->input_state, gst_video_codec_state_unref);
}

GstD3D12Decoder *
gst_d3d12_decoder_new (GstD3D12Device * device, GstDxvaCodec codec)
{
  GstD3D12Decoder *self;
  GstD3D12DecoderPrivate *priv;
  ComPtr < ID3D12VideoDevice > video_device;
  ComPtr < ID3D12CommandAllocator > copy_ca;
  ComPtr < ID3D12GraphicsCommandList > copy_cl;
  ComPtr < ID3D12CommandQueue > copy_cq;
  ComPtr < ID3D12CommandAllocator > ca;
  ComPtr < ID3D12VideoDecodeCommandList > cl;
  ComPtr < ID3D12CommandQueue > cq;
  ID3D12Device *device_handle;
  D3D12_COMMAND_QUEUE_DESC desc = { };
  HRESULT hr;

  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);
  g_return_val_if_fail (codec > GST_DXVA_CODEC_NONE, nullptr);
  g_return_val_if_fail (codec < GST_DXVA_CODEC_LAST, nullptr);

  device_handle = gst_d3d12_device_get_device_handle (device);
  hr = device_handle->QueryInterface (IID_PPV_ARGS (&video_device));
  if (!gst_d3d12_result (hr, device))
    return nullptr;

  hr = device_handle->CreateCommandAllocator (D3D12_COMMAND_LIST_TYPE_COPY,
      IID_PPV_ARGS (&copy_ca));
  if (!gst_d3d12_result (hr, device))
    return nullptr;

  hr = device_handle->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_COPY,
      copy_ca.Get (), nullptr, IID_PPV_ARGS (&copy_cl));
  if (!gst_d3d12_result (hr, device))
    return nullptr;

  hr = copy_cl->Close ();
  if (!gst_d3d12_result (hr, device))
    return nullptr;

  desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
  desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  hr = device_handle->CreateCommandQueue (&desc, IID_PPV_ARGS (&copy_cq));
  if (!gst_d3d12_result (hr, device))
    return nullptr;

  hr = device_handle->CreateCommandAllocator
      (D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE, IID_PPV_ARGS (&ca));
  if (!gst_d3d12_result (hr, device))
    return nullptr;

  hr = device_handle->CreateCommandList (0,
      D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE, ca.Get (), nullptr,
      IID_PPV_ARGS (&cl));
  if (!gst_d3d12_result (hr, device))
    return nullptr;

  hr = cl->Close ();
  if (!gst_d3d12_result (hr, device))
    return nullptr;

  desc.Type = D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE;
  desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  hr = device_handle->CreateCommandQueue (&desc, IID_PPV_ARGS (&cq));
  if (!gst_d3d12_result (hr, device))
    return nullptr;

  self = (GstD3D12Decoder *) g_object_new (GST_TYPE_D3D12_DECODER, nullptr);
  priv = self->priv;
  priv->codec = codec;
  priv->device = (GstD3D12Device *) gst_object_ref (device);
  priv->fence = gst_d3d12_fence_new (device);
  priv->video_device = video_device;
  priv->copy_ca = copy_ca;
  priv->copy_cl = copy_cl;
  priv->copy_cq = copy_cq;
  priv->ca = ca;
  priv->cl = cl;
  priv->cq = cq;
  g_object_get (priv->device, "adapter-luid", &priv->luid, nullptr);

  gst_object_ref_sink (self);

  return self;
}

GstFlowReturn
gst_d3d12_decoder_configure (GstD3D12Decoder * decoder,
    GstVideoCodecState * input_state, const GstVideoInfo * info,
    gint crop_x, gint crop_y, gint coded_width,
    gint coded_height, guint dpb_size)
{
  g_return_val_if_fail (GST_IS_D3D12_DECODER (decoder), GST_FLOW_ERROR);
  g_return_val_if_fail (info, GST_FLOW_ERROR);
  g_return_val_if_fail (input_state, GST_FLOW_ERROR);
  g_return_val_if_fail (coded_width >= GST_VIDEO_INFO_WIDTH (info),
      GST_FLOW_ERROR);
  g_return_val_if_fail (coded_height >= GST_VIDEO_INFO_HEIGHT (info),
      GST_FLOW_ERROR);
  g_return_val_if_fail (dpb_size > 0, GST_FLOW_ERROR);

  GstD3D12DecoderPrivate *priv = decoder->priv;
  GstD3D12Format device_format;

  gst_d3d12_decoder_reset (decoder);

  if (!gst_d3d12_device_get_device_format (priv->device,
          GST_VIDEO_INFO_FORMAT (info), &device_format) ||
      device_format.dxgi_format == DXGI_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (decoder, "Could not determine dxgi format from %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)));
    return GST_FLOW_ERROR;
  }

  if (priv->codec == GST_DXVA_CODEC_H264)
    dpb_size += 1;

  priv->input_state = gst_video_codec_state_ref (input_state);
  priv->info = priv->output_info = *info;
  priv->crop_x = crop_x;
  priv->crop_y = crop_y;
  priv->coded_width = coded_width;
  priv->coded_height = coded_height;
  priv->dpb_size = dpb_size;
  priv->decoder_format = device_format.dxgi_format;

  priv->configured = TRUE;

  return GST_FLOW_OK;
}

static gboolean
gst_d3d12_decoder_prepare_allocator (GstD3D12Decoder * self)
{
  GstD3D12DecoderPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (self, "Preparing allocator");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->allocator) {
    gst_d3d12_allocator_set_active (priv->allocator, FALSE);
    gst_clear_object (&priv->allocator);
  }

  if (priv->output_allocator) {
    gst_d3d12_allocator_set_active (priv->output_allocator, FALSE);
    gst_clear_object (&priv->output_allocator);
  }

  D3D12_RESOURCE_FLAGS resource_flags = D3D12_RESOURCE_FLAG_NONE;
  if ((priv->support.ConfigurationFlags &
          D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_REFERENCE_ONLY_ALLOCATIONS_REQUIRED)
      != 0) {
    resource_flags =
        D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY |
        D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    priv->reference_only = TRUE;
  } else {
    priv->reference_only = FALSE;
  }

  UINT16 array_size;
  /* Tier 1 decoder requires array */
  if (priv->support.DecodeTier == D3D12_VIDEO_DECODE_TIER_1) {
    array_size = priv->dpb_size;
  } else {
    array_size = 1;
  }

  D3D12_HEAP_PROPERTIES heap_prop =
      CD3D12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
  D3D12_RESOURCE_DESC desc = CD3D12_RESOURCE_DESC::Tex2D (priv->decoder_format,
      priv->aligned_width,
      priv->aligned_height,
      array_size,
      1,
      1,
      0,
      resource_flags);

  priv->allocator = (GstD3D12Allocator *)
      gst_d3d12_pool_allocator_new (priv->device,
      &heap_prop, D3D12_HEAP_FLAG_NONE,
      &desc, D3D12_RESOURCE_STATE_COMMON, nullptr);
  gst_d3d12_allocator_set_active (priv->allocator, TRUE);

  /* In case that device requires reference only dpb texture, we need another
   * texture pool for outputting without VIDEO_DECODE_REFERENCE_ONLY flag */
  if (priv->reference_only) {
    GST_DEBUG_OBJECT (self, "Creating output only allocator");
    D3D12_RESOURCE_DESC ref_desc =
        CD3D12_RESOURCE_DESC::Tex2D (priv->decoder_format,
        priv->aligned_width,
        priv->aligned_height,
        1,
        1);

    priv->output_allocator = (GstD3D12Allocator *)
        gst_d3d12_pool_allocator_new (priv->device, &heap_prop,
        D3D12_HEAP_FLAG_NONE, &ref_desc, D3D12_RESOURCE_STATE_COMMON, nullptr);
    gst_d3d12_allocator_set_active (priv->output_allocator, TRUE);
  }

  priv->dpb = std::make_shared < GstD3D12Dpb > ((guint8) priv->dpb_size);

  return TRUE;
}

static void
gst_d3d12_decoder_picture_free (GstD3D12DecoderPicture * self)
{
  delete self;
}

static GstD3D12DecoderPicture *
gst_d3d12_decoder_picture_new (GstD3D12Decoder * self, GstMemory * mem,
    GstMemory * output_mem, ID3D12VideoDecoderHeap * heap)
{
  GstD3D12DecoderPrivate *priv = self->priv;
  GstD3D12DecoderPicture *picture;

  auto view_id = priv->dpb->Acquire (GST_D3D12_MEMORY_CAST (mem), heap);
  if (view_id == 0xff) {
    GST_WARNING_OBJECT (self, "No empty picture");
    if (mem)
      gst_memory_unref (mem);
    if (output_mem)
      gst_memory_unref (output_mem);
    return nullptr;
  }

  picture = new GstD3D12DecoderPicture (mem, output_mem, priv->dpb, heap,
      view_id);

  gst_mini_object_init (picture, 0, GST_TYPE_D3D12_DECODER_PICTURE,
      nullptr, nullptr,
      (GstMiniObjectFreeFunction) gst_d3d12_decoder_picture_free);

  return picture;
}

GstFlowReturn
gst_d3d12_decoder_new_picture (GstD3D12Decoder * decoder,
    GstVideoDecoder * videodec, GstCodecPicture * picture)
{
  GstD3D12DecoderPrivate *priv;
  GstD3D12DecoderPicture *decoder_pic = nullptr;
  GstMemory *mem;
  GstMemory *output_mem = nullptr;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_D3D12_DECODER (decoder), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), GST_FLOW_ERROR);
  g_return_val_if_fail (picture != nullptr, GST_FLOW_ERROR);

  priv = decoder->priv;

  if (!priv->allocator) {
    /* Try negotiate again whatever the previous negotiation result was.
     * There could be updated field(s) in sinkpad caps after we negotiated with
     * downstream on new_sequence() call. For example, h264/h265 parse
     * will be able to update HDR10 related caps field after parsing
     * corresponding SEI messages which are usually placed after the essential
     * headers */
    gst_video_decoder_negotiate (videodec);

    if (!gst_d3d12_decoder_prepare_allocator (decoder)) {
      GST_ERROR_OBJECT (videodec, "Failed to setup dpb pool");
      return GST_FLOW_ERROR;
    }
  }

  ret =
      gst_d3d12_pool_allocator_acquire_memory (GST_D3D12_POOL_ALLOCATOR
      (priv->allocator), &mem);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (videodec, "Couldn't acquire memory");
    return ret;
  }

  if (priv->reference_only) {
    ret =
        gst_d3d12_pool_allocator_acquire_memory (GST_D3D12_POOL_ALLOCATOR
        (priv->output_allocator), &output_mem);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (videodec, "Couldn't acquire output memory");
      gst_memory_unref (mem);
      return ret;
    }
  }

  decoder_pic = gst_d3d12_decoder_picture_new (decoder, mem, output_mem,
      priv->heap.Get ());
  if (!decoder_pic) {
    GST_ERROR_OBJECT (videodec, "Couldn't create new picture");
    return GST_FLOW_ERROR;
  }

  gst_codec_picture_set_user_data (picture, decoder_pic,
      (GDestroyNotify) gst_mini_object_unref);

  return GST_FLOW_OK;
}

GstFlowReturn
gst_d3d12_decoder_duplicate_picture (GstD3D12Decoder * decoder,
    GstCodecPicture * src, GstCodecPicture * dst)
{
  GstD3D12DecoderPicture *decoder_pic =
      (GstD3D12DecoderPicture *) gst_codec_picture_get_user_data (src);

  if (!decoder_pic)
    return GST_FLOW_ERROR;

  gst_codec_picture_set_user_data (dst, gst_mini_object_ref (decoder_pic),
      (GDestroyNotify) gst_mini_object_unref);

  return GST_FLOW_OK;
}

guint8
gst_d3d12_decoder_get_picture_id (GstD3D12Decoder * decoder,
    GstCodecPicture * picture)
{
  GstD3D12DecoderPicture *decoder_pic =
      (GstD3D12DecoderPicture *) gst_codec_picture_get_user_data (picture);

  if (!picture)
    return 0xff;

  return decoder_pic->view_id;
}

GstFlowReturn
gst_d3d12_decoder_start_picture (GstD3D12Decoder * decoder,
    GstCodecPicture * picture, guint8 * picture_id)
{
  GstD3D12DecoderPicture *decoder_pic =
      (GstD3D12DecoderPicture *) gst_codec_picture_get_user_data (picture);

  if (picture_id)
    *picture_id = 0xff;

  if (!decoder_pic)
    return GST_FLOW_ERROR;

  if (picture_id)
    *picture_id = decoder_pic->view_id;

  return GST_FLOW_OK;
}

static gboolean
gst_d3d12_decoder_upload_bitstream (GstD3D12Decoder * self, gpointer data,
    gsize size)
{
  GstD3D12DecoderPrivate *priv = self->priv;
  gpointer map_data;
  HRESULT hr;

  if (priv->bitstream && priv->bitstream_size < size)
    priv->bitstream = nullptr;

  if (!priv->bitstream) {
    ID3D12Device *device_handle =
        gst_d3d12_device_get_device_handle (priv->device);
    ComPtr < ID3D12Resource > bitstream;
    size_t alloc_size = GST_ROUND_UP_128 (size) + 1024;

    D3D12_HEAP_PROPERTIES heap_prop =
        CD3D12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC desc = CD3D12_RESOURCE_DESC::Buffer (alloc_size);
    hr = device_handle->CreateCommittedResource (&heap_prop,
        D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS (&bitstream));
    if (!gst_d3d12_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Failed to create bitstream buffer");
      return FALSE;
    }

    GST_LOG_OBJECT (self, "Allocated new bitstream buffer with size %"
        G_GSIZE_FORMAT, size);

    priv->bitstream = bitstream;
    priv->bitstream_size = alloc_size;
  }

  hr = priv->bitstream->Map (0, nullptr, &map_data);
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Couldn't map bitstream buffer");
    return FALSE;
  }

  memcpy (map_data, data, size);
  priv->bitstream->Unmap (0, nullptr);

  return TRUE;
}

GstFlowReturn
gst_d3d12_decoder_end_picture (GstD3D12Decoder * decoder,
    GstCodecPicture * picture, GPtrArray * ref_pics,
    const GstDxvaDecodingArgs * args)
{
  GstD3D12DecoderPrivate *priv = decoder->priv;
  HRESULT hr;
  D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS out_args;
  D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS in_args;
  ID3D12Resource *resource;
  ID3D12Resource *out_resource = nullptr;
  GstD3D12Memory *dmem;
  UINT subresource[2];
  GstD3D12DecoderPicture *decoder_pic = (GstD3D12DecoderPicture *)
      gst_codec_picture_get_user_data (picture);
  UINT64 fence_value;
  ID3D12Fence *fence_handle;
  std::vector < D3D12_RESOURCE_BARRIER > pre_barriers;
  std::vector < D3D12_RESOURCE_BARRIER > post_barriers;

  if (!decoder_pic) {
    GST_ERROR_OBJECT (decoder, "No attached decoder picture");
    return GST_FLOW_ERROR;
  }

  if (!args->bitstream || args->bitstream_size == 0) {
    GST_ERROR_OBJECT (decoder, "No bitstream buffer passed");
    return GST_FLOW_ERROR;
  }

  /* Wait for previous fence if needed */
  gst_d3d12_fence_wait (priv->fence);

  if (!gst_d3d12_decoder_upload_bitstream (decoder, args->bitstream,
          args->bitstream_size)) {
    return GST_FLOW_ERROR;
  }

  memset (&in_args, 0, sizeof (D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS));
  memset (&out_args, 0, sizeof (D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS));

  hr = priv->ca->Reset ();
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (decoder, "Couldn't reset command allocator");
    return GST_FLOW_ERROR;
  }

  hr = priv->cl->Reset (priv->ca.Get ());
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (decoder, "Couldn't reset command list");
    return GST_FLOW_ERROR;
  }

  for (guint i = 0; i < ref_pics->len; i++) {
    GstCodecPicture *ref_pic =
        (GstCodecPicture *) g_ptr_array_index (ref_pics, i);
    GstD3D12DecoderPicture *ref_dec_pic =
        (GstD3D12DecoderPicture *) gst_codec_picture_get_user_data (ref_pic);

    if (!ref_dec_pic || ref_dec_pic == decoder_pic)
      continue;

    dmem = (GstD3D12Memory *) ref_dec_pic->mem;

    resource = gst_d3d12_memory_get_resource_handle (dmem);
    gst_d3d12_memory_get_subresource_index (dmem, 0, &subresource[0]);
    gst_d3d12_memory_get_subresource_index (dmem, 1, &subresource[1]);

    pre_barriers.push_back (CD3D12_RESOURCE_BARRIER::Transition (resource,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_DECODE_READ,
            subresource[0]));
    pre_barriers.push_back (CD3D12_RESOURCE_BARRIER::Transition (resource,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_DECODE_READ,
            subresource[1]));

    post_barriers.push_back (CD3D12_RESOURCE_BARRIER::Transition (resource,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_READ, D3D12_RESOURCE_STATE_COMMON,
            subresource[0]));
    post_barriers.push_back (CD3D12_RESOURCE_BARRIER::Transition (resource,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_READ, D3D12_RESOURCE_STATE_COMMON,
            subresource[1]));
  }

  dmem = (GstD3D12Memory *) decoder_pic->output_mem;
  if (dmem) {
    out_resource = gst_d3d12_memory_get_resource_handle (dmem);

    pre_barriers.push_back (CD3D12_RESOURCE_BARRIER::Transition (out_resource,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE));
    post_barriers.push_back (CD3D12_RESOURCE_BARRIER::Transition (out_resource,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
            D3D12_RESOURCE_STATE_COMMON));
  }

  dmem = (GstD3D12Memory *) decoder_pic->mem;
  resource = gst_d3d12_memory_get_resource_handle (dmem);
  gst_d3d12_memory_get_subresource_index (GST_D3D12_MEMORY_CAST (dmem), 0,
      &subresource[0]);
  gst_d3d12_memory_get_subresource_index (GST_D3D12_MEMORY_CAST (dmem), 1,
      &subresource[1]);

  pre_barriers.push_back (CD3D12_RESOURCE_BARRIER::Transition (resource,
          D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
          subresource[0]));
  pre_barriers.push_back (CD3D12_RESOURCE_BARRIER::Transition (resource,
          D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
          subresource[1]));

  post_barriers.push_back (CD3D12_RESOURCE_BARRIER::Transition (resource,
          D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE, D3D12_RESOURCE_STATE_COMMON,
          subresource[0]));
  post_barriers.push_back (CD3D12_RESOURCE_BARRIER::Transition (resource,
          D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE, D3D12_RESOURCE_STATE_COMMON,
          subresource[1]));

  priv->cl->ResourceBarrier (pre_barriers.size (), &pre_barriers[0]);

  if (out_resource) {
    out_args.pOutputTexture2D = out_resource;
    out_args.OutputSubresource = 0;
    out_args.ConversionArguments.Enable = TRUE;
    out_args.ConversionArguments.pReferenceTexture2D = resource;
    out_args.ConversionArguments.ReferenceSubresource = subresource[0];
  } else {
    out_args.pOutputTexture2D = resource;
    out_args.OutputSubresource = subresource[0];
    out_args.ConversionArguments.Enable = FALSE;
  }

  if (args->picture_params) {
    in_args.FrameArguments[in_args.NumFrameArguments].Type =
        D3D12_VIDEO_DECODE_ARGUMENT_TYPE_PICTURE_PARAMETERS;
    in_args.FrameArguments[in_args.NumFrameArguments].Size =
        args->picture_params_size;
    in_args.FrameArguments[in_args.NumFrameArguments].pData =
        args->picture_params;
    in_args.NumFrameArguments++;
  }

  if (args->slice_control) {
    in_args.FrameArguments[in_args.NumFrameArguments].Type =
        D3D12_VIDEO_DECODE_ARGUMENT_TYPE_SLICE_CONTROL;
    in_args.FrameArguments[in_args.NumFrameArguments].Size =
        args->slice_control_size;
    in_args.FrameArguments[in_args.NumFrameArguments].pData =
        args->slice_control;
    in_args.NumFrameArguments++;
  }

  if (args->inverse_quantization_matrix) {
    in_args.FrameArguments[in_args.NumFrameArguments].Type =
        D3D12_VIDEO_DECODE_ARGUMENT_TYPE_INVERSE_QUANTIZATION_MATRIX;
    in_args.FrameArguments[in_args.NumFrameArguments].Size =
        args->inverse_quantization_matrix_size;
    in_args.FrameArguments[in_args.NumFrameArguments].pData =
        args->inverse_quantization_matrix;
    in_args.NumFrameArguments++;
  }

  in_args.CompressedBitstream.pBuffer = priv->bitstream.Get ();
  in_args.CompressedBitstream.Offset = 0;
  in_args.CompressedBitstream.Size = args->bitstream_size;
  in_args.pHeap = decoder_pic->heap.Get ();

  in_args.ReferenceFrames.NumTexture2Ds = priv->dpb->GetSize ();
  in_args.ReferenceFrames.ppHeaps = priv->dpb->GetHeaps ();
  in_args.ReferenceFrames.ppTexture2Ds = priv->dpb->GetTextures ();
  in_args.ReferenceFrames.pSubresources = priv->dpb->GetSubresources ();

  priv->cl->DecodeFrame (priv->decoder.Get (), &out_args, &in_args);
  priv->cl->ResourceBarrier (post_barriers.size (), &post_barriers[0]);

  hr = priv->cl->Close ();
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (decoder, "Couldn't record decoding command");
    return GST_FLOW_ERROR;
  }

  ID3D12CommandList *cl[] = { priv->cl.Get () };
  priv->cq->ExecuteCommandLists (1, cl);

  fence_handle = gst_d3d12_fence_get_handle (priv->fence);
  fence_value = priv->fence_value.fetch_add (1);
  hr = priv->cq->Signal (fence_handle, fence_value);
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_DEBUG_OBJECT (decoder, "Couldn't signal fence value");
    return GST_FLOW_ERROR;
  }

  gst_d3d12_fence_set_event_on_completion_value (priv->fence, fence_value);
  /* Will wait on output picture or start of next decoding loop */

  return GST_FLOW_OK;
}

static gboolean
gst_d3d12_decoder_ensure_staging_texture (GstD3D12Decoder * self)
{
  GstD3D12DecoderPrivate *priv = self->priv;

  if (priv->staging)
    return TRUE;

  ComPtr < ID3D12Resource > staging;
  HRESULT hr;
  UINT64 size;
  ID3D12Device *device = gst_d3d12_device_get_device_handle (priv->device);
  D3D12_RESOURCE_DESC tex_desc =
      CD3D12_RESOURCE_DESC::Tex2D (priv->decoder_format,
      priv->aligned_width,
      priv->aligned_height,
      1,
      1);

  device->GetCopyableFootprints (&tex_desc, 0, 2, 0, priv->layout, nullptr,
      nullptr, &size);

  D3D12_HEAP_PROPERTIES heap_prop = CD3D12_HEAP_PROPERTIES
      (D3D12_HEAP_TYPE_READBACK);
  D3D12_RESOURCE_DESC desc = CD3D12_RESOURCE_DESC::Buffer (size);

  hr = device->CreateCommittedResource (&heap_prop, D3D12_HEAP_FLAG_NONE,
      &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS (&staging));
  if (!gst_d3d12_result (hr, priv->device))
    return FALSE;

  priv->staging = staging;

  return TRUE;
}

GstFlowReturn
gst_d3d12_decoder_output_picture (GstD3D12Decoder * decoder,
    GstVideoDecoder * videodec, GstVideoCodecFrame * frame,
    GstCodecPicture * picture, GstVideoBufferFlags buffer_flags,
    gint display_width, gint display_height)
{
  GstD3D12DecoderPrivate *priv = decoder->priv;
  GstD3D12DecoderPicture *decoder_pic;
  std::vector < D3D12_RESOURCE_BARRIER > barriers;
  GstFlowReturn ret = GST_FLOW_ERROR;
  ID3D12CommandList *list[1];
  UINT64 fence_value;
  GstMemory *mem;
  GstD3D12Memory *dmem;
  ID3D12Resource *resource;
  UINT subresource[2];
  void *map_data;
  HRESULT hr;
  GstVideoFrame vframe;

  g_return_val_if_fail (GST_IS_D3D12_DECODER (decoder), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), GST_FLOW_ERROR);
  g_return_val_if_fail (picture != nullptr, GST_FLOW_ERROR);

  priv = decoder->priv;

  decoder_pic = (GstD3D12DecoderPicture *)
      gst_codec_picture_get_user_data (picture);
  if (!decoder_pic) {
    ret = GST_FLOW_ERROR;
    goto error;
  }

  if (picture->discont_state) {
    g_clear_pointer (&priv->input_state, gst_video_codec_state_unref);
    priv->input_state = gst_video_codec_state_ref (picture->discont_state);

    if (!gst_video_decoder_negotiate (videodec)) {
      GST_ERROR_OBJECT (videodec, "Failed to re-negotiate with new frame size");
      ret = GST_FLOW_NOT_NEGOTIATED;
      goto error;
    }
  }

  ret = gst_video_decoder_allocate_output_frame (videodec, frame);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (videodec, "Couldn't allocate output buffer");
    goto error;
  }

  if (!gst_d3d12_decoder_ensure_staging_texture (decoder)) {
    GST_ERROR_OBJECT (videodec, "Couldn't allocate staging texture");
    ret = GST_FLOW_ERROR;
    goto error;
  }

  /* Wait for pending decoding operation before copying */
  gst_d3d12_fence_wait (priv->fence);

  mem = decoder_pic->output_mem ? decoder_pic->output_mem : decoder_pic->mem;
  dmem = GST_D3D12_MEMORY_CAST (mem);
  resource = gst_d3d12_memory_get_resource_handle (dmem);

  gst_d3d12_memory_get_subresource_index (dmem, 0, &subresource[0]);
  gst_d3d12_memory_get_subresource_index (dmem, 1, &subresource[1]);

  /* Copy texture to staging */
  hr = priv->copy_ca->Reset ();
  if (!gst_d3d12_result (hr, priv->device)) {
    ret = GST_FLOW_ERROR;
    goto error;
  }

  hr = priv->copy_cl->Reset (priv->copy_ca.Get (), nullptr);
  if (!gst_d3d12_result (hr, priv->device)) {
    ret = GST_FLOW_ERROR;
    goto error;
  }

  barriers.push_back (CD3D12_RESOURCE_BARRIER::Transition (resource,
          D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE,
          subresource[0]));
  barriers.push_back (CD3D12_RESOURCE_BARRIER::Transition (resource,
          D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE,
          subresource[1]));

  priv->copy_cl->ResourceBarrier (barriers.size (), &barriers[0]);

  for (guint i = 0; i < 2; i++) {
    D3D12_TEXTURE_COPY_LOCATION src =
        CD3D12_TEXTURE_COPY_LOCATION (resource, subresource[i]);
    D3D12_TEXTURE_COPY_LOCATION dst =
        CD3D12_TEXTURE_COPY_LOCATION (priv->staging.Get (), priv->layout[i]);
    D3D12_BOX src_box = { 0, };

    /* FIXME: only 4:2:0 */
    if (i == 0) {
      src_box.left = GST_ROUND_UP_2 (priv->crop_x);
      src_box.top = GST_ROUND_UP_2 (priv->crop_y);
      src_box.right = GST_ROUND_UP_2 (priv->crop_x + priv->output_info.width);
      src_box.bottom = GST_ROUND_UP_2 (priv->crop_y + priv->output_info.height);
    } else {
      src_box.left = GST_ROUND_UP_2 (priv->crop_x) / 2;
      src_box.top = GST_ROUND_UP_2 (priv->crop_y) / 2;
      src_box.right =
          GST_ROUND_UP_2 (priv->crop_x + priv->output_info.width) / 2;
      src_box.bottom =
          GST_ROUND_UP_2 (priv->crop_y + priv->output_info.height) / 2;
    }

    src_box.front = 0;
    src_box.back = 1;

    priv->copy_cl->CopyTextureRegion (&dst, 0, 0, 0, &src, &src_box);
  }

  hr = priv->copy_cl->Close ();
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (videodec, "Couldn't record copy command");
    ret = GST_FLOW_ERROR;
    goto error;
  }

  list[0] = priv->copy_cl.Get ();
  priv->copy_cq->ExecuteCommandLists (1, list);

  fence_value = priv->fence_value.fetch_add (1);
  hr = priv->copy_cq->Signal (gst_d3d12_fence_get_handle (priv->fence),
      fence_value);
  if (!gst_d3d12_result (hr, priv->device)) {
    ret = GST_FLOW_ERROR;
    goto error;
  }

  gst_d3d12_fence_set_event_on_completion_value (priv->fence, fence_value);
  gst_d3d12_fence_wait (priv->fence);

  hr = priv->staging->Map (0, nullptr, &map_data);
  if (!gst_d3d12_result (hr, priv->device)) {
    ret = GST_FLOW_ERROR;
    goto error;
  }

  gst_video_frame_map (&vframe,
      &priv->output_info, frame->output_buffer, GST_MAP_WRITE);

  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&vframe); i++) {
    guint8 *src = (guint8 *) map_data + priv->layout[i].Offset;
    guint8 *dst = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe, i);
    gint width = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, i) *
        GST_VIDEO_FRAME_COMP_PSTRIDE (&vframe, i);

    for (guint j = 0; j < GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, i); j++) {
      memcpy (dst, src, width);
      dst += GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, i);
      src += priv->layout[i].Footprint.RowPitch;
    }
  }

  priv->staging->Unmap (0, nullptr);
  gst_video_frame_unmap (&vframe);

  gst_codec_picture_unref (picture);
  return gst_video_decoder_finish_frame (videodec, frame);

error:
  gst_codec_picture_unref (picture);
  gst_video_decoder_release_frame (videodec, frame);

  return ret;
}

static gboolean
gst_d3d12_decoder_open (GstD3D12Decoder * self)
{
  GstD3D12DecoderPrivate *priv = self->priv;
  HRESULT hr;
  gint aligned_width, aligned_height;
  ID3D12VideoDevice *video_device;
  GstVideoInfo *info = &priv->info;
  D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT support;
  ComPtr < ID3D12VideoDecoderHeap > heap;

  if (priv->opened)
    return TRUE;

  if (!priv->configured) {
    GST_ERROR_OBJECT (self, "Should configure first");
    return FALSE;
  }

  video_device = priv->video_device.Get ();

  const DecoderFormat *decoder_foramt = nullptr;
  for (guint i = 0; i < G_N_ELEMENTS (format_list); i++) {
    if (format_list[i].codec != priv->codec ||
        format_list[i].format != priv->decoder_format) {
      continue;
    }

    D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT s;
    s.NodeIndex = 0;

    s.Configuration.DecodeProfile = format_list[i].decode_profile;
    s.Configuration.BitstreamEncryption = D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE;
    if (GST_VIDEO_INFO_IS_INTERLACED (info) &&
        GST_VIDEO_INFO_INTERLACE_MODE (info) !=
        GST_VIDEO_INTERLACE_MODE_ALTERNATE) {
      s.Configuration.InterlaceType =
          D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_FIELD_BASED;
    } else {
      s.Configuration.InterlaceType =
          D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE;
    }
    s.DecodeFormat = priv->decoder_format;
    s.FrameRate = { 0, 1 };
    s.BitRate = 0;
    s.Width = priv->coded_width;
    s.Height = priv->coded_height;

    hr = video_device->CheckFeatureSupport (D3D12_FEATURE_VIDEO_DECODE_SUPPORT,
        &s, sizeof (D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT));
    if (FAILED (hr))
      continue;

    if ((s.SupportFlags & D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED) == 0) {
      GST_INFO_OBJECT (self, "Device doesn't support current configuration");
      continue;
    }

    decoder_foramt = &format_list[i];
    support = s;

    GST_INFO_OBJECT (self,
        "Created decoder support tier %d with configuration flags 0x%x",
        support.DecodeTier, support.ConfigurationFlags);
    break;
  }

  if (!decoder_foramt) {
    GST_WARNING_OBJECT (self, "Failed to find supported configuration");
    return FALSE;
  }

  guint alignment = 16;
  if ((support.ConfigurationFlags &
          D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_HEIGHT_ALIGNMENT_MULTIPLE_32_REQUIRED)
      != 0) {
    alignment = 32;
  }

  aligned_width = GST_ROUND_UP_N (priv->coded_width, alignment);
  aligned_height = GST_ROUND_UP_N (priv->coded_height, alignment);

  priv->aligned_width = aligned_width;
  priv->aligned_height = aligned_height;

  /* We can reuse decoder object, which is resolution independent */
  if (!priv->decoder ||
      priv->support.Configuration.DecodeProfile !=
      support.Configuration.DecodeProfile
      || priv->support.Configuration.InterlaceType !=
      support.Configuration.InterlaceType) {
    ComPtr < ID3D12VideoDecoder > decoder;

    priv->decoder = nullptr;

    D3D12_VIDEO_DECODER_DESC desc;
    desc.NodeMask = 0;
    desc.Configuration = support.Configuration;
    hr = video_device->CreateVideoDecoder (&desc, IID_PPV_ARGS (&decoder));
    if (!gst_d3d12_result (hr, priv->device)) {
      GST_ERROR_OBJECT (self, "Failed to create decoder");
      return FALSE;
    }

    priv->decoder = decoder;
  }

  D3D12_VIDEO_DECODER_HEAP_DESC heap_desc;
  heap_desc.NodeMask = 0;
  heap_desc.Configuration = support.Configuration;
  heap_desc.DecodeWidth = priv->aligned_width;
  heap_desc.DecodeHeight = priv->aligned_height;
  heap_desc.Format = priv->decoder_format;
  heap_desc.FrameRate = { 0, 1 };
  heap_desc.BitRate = 0;
  heap_desc.MaxDecodePictureBufferCount = priv->dpb_size;
  hr = video_device->CreateVideoDecoderHeap (&heap_desc, IID_PPV_ARGS (&heap));
  if (!gst_d3d12_result (hr, priv->device)) {
    GST_ERROR_OBJECT (self, "Failed to create decoder heap");
    return FALSE;
  }

  priv->heap = heap;
  priv->support = support;

  priv->opened = TRUE;

  return TRUE;
}

gboolean
gst_d3d12_decoder_negotiate (GstD3D12Decoder * decoder,
    GstVideoDecoder * videodec)
{
  g_return_val_if_fail (GST_IS_D3D12_DECODER (decoder), FALSE);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), FALSE);

  GstD3D12DecoderPrivate *priv = decoder->priv;
  GstVideoInfo *info = &priv->output_info;
  GstVideoCodecState *state = nullptr;
  GstVideoCodecState *input_state = priv->input_state;
  GstStructure *s;
  const gchar *str;

  /* TODO: add support alternate interlace */
  state = gst_video_decoder_set_interlaced_output_state (videodec,
      GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_INTERLACE_MODE (info),
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info), input_state);

  if (!state) {
    GST_ERROR_OBJECT (decoder, "Couldn't set output state");
    return FALSE;
  }

  state->caps = gst_video_info_to_caps (&state->info);

  s = gst_caps_get_structure (input_state->caps, 0);
  str = gst_structure_get_string (s, "mastering-display-info");
  if (str) {
    gst_caps_set_simple (state->caps,
        "mastering-display-info", G_TYPE_STRING, str, nullptr);
  }

  str = gst_structure_get_string (s, "content-light-level");
  if (str) {
    gst_caps_set_simple (state->caps,
        "content-light-level", G_TYPE_STRING, str, nullptr);
  }

  g_clear_pointer (&priv->output_state, gst_video_codec_state_unref);
  priv->output_state = state;

  return gst_d3d12_decoder_open (decoder);
}

gboolean
gst_d3d12_decoder_decide_allocation (GstD3D12Decoder * decoder,
    GstVideoDecoder * videodec, GstQuery * query)
{
  GstD3D12DecoderPrivate *priv = decoder->priv;
  GstCaps *outcaps;
  GstBufferPool *pool = nullptr;
  guint n, size, min = 0, max = 0;
  GstVideoInfo vinfo = { 0, };
  GstStructure *config;

  g_return_val_if_fail (GST_IS_D3D12_DECODER (decoder), FALSE);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), FALSE);
  g_return_val_if_fail (query != nullptr, FALSE);

  if (!priv->opened) {
    GST_ERROR_OBJECT (videodec, "Should open decoder first");
    return FALSE;
  }

  gst_query_parse_allocation (query, &outcaps, nullptr);

  if (!outcaps) {
    GST_DEBUG_OBJECT (decoder, "No output caps");
    return FALSE;
  }

  gst_video_info_from_caps (&vinfo, outcaps);
  n = gst_query_get_n_allocation_pools (query);
  if (n > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  if (!pool) {
    pool = gst_video_buffer_pool_new ();
    size = (guint) vinfo.size;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  gst_buffer_pool_set_config (pool, config);

  if (n > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);
  gst_object_unref (pool);

  return TRUE;
}

enum
{
  PROP_DECODER_ADAPTER_LUID = 1,
  PROP_DECODER_DEVICE_ID,
  PROP_DECODER_VENDOR_ID,
};

struct _GstD3D12DecoderClassData
{
  GstD3D12DecoderSubClassData subclass_data;
  GstCaps *sink_caps;
  GstCaps *src_caps;
  gchar *description;
};

GstD3D12DecoderClassData *
gst_d3d12_decoder_check_feature_support (GstD3D12Device * device,
    ID3D12VideoDevice * video_device, GstDxvaCodec codec)
{
  HRESULT hr;
  GstDxvaResolution max_resolution = { 0, 0 };
  D3D12_VIDEO_DECODE_CONFIGURATION_FLAGS config_flags =
      D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_NONE;
  D3D12_VIDEO_DECODE_TIER tier = D3D12_VIDEO_DECODE_TIER_NOT_SUPPORTED;
  std::set < DXGI_FORMAT > supported_formats;

  g_return_val_if_fail (GST_IS_D3D12_DEVICE (device), nullptr);
  g_return_val_if_fail (video_device != nullptr, nullptr);

  for (guint i = 0; i < G_N_ELEMENTS (format_list); i++) {
    if (format_list[i].codec != codec)
      continue;

    D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT s;
    s.NodeIndex = 0;
    s.Configuration.DecodeProfile = format_list[i].decode_profile;
    s.Configuration.BitstreamEncryption = D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE;
    s.Configuration.InterlaceType = D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE;
    s.DecodeFormat = format_list[i].format;
    s.FrameRate = { 0, 1 };
    s.BitRate = 0;

    for (guint j = 0; j < G_N_ELEMENTS (gst_dxva_resolutions); j++) {
      s.Width = gst_dxva_resolutions[j].width;
      s.Height = gst_dxva_resolutions[j].height;

      hr = video_device->CheckFeatureSupport
          (D3D12_FEATURE_VIDEO_DECODE_SUPPORT, &s,
          sizeof (D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT));
      if (FAILED (hr))
        break;

      if ((s.SupportFlags & D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED) == 0)
        break;

      if (max_resolution.width < gst_dxva_resolutions[j].width)
        max_resolution.width = gst_dxva_resolutions[j].width;
      if (max_resolution.height < gst_dxva_resolutions[j].height)
        max_resolution.height = gst_dxva_resolutions[j].height;

      supported_formats.insert (format_list[i].format);
      config_flags = s.ConfigurationFlags;
      tier = s.DecodeTier;
    }
  }

  if (supported_formats.empty ()) {
    GST_DEBUG_OBJECT (device, "Device doesn't support %s",
        gst_dxva_codec_to_string (codec));
    return nullptr;
  }

  GST_DEBUG_OBJECT (device,
      "Device supports codec %s (%dx%d), configuration flags 0x%x, tier: %d",
      gst_dxva_codec_to_string (codec),
      max_resolution.width, max_resolution.height, config_flags, tier);

  std::string format_string;

  /* *INDENT-OFF* */
  for (const auto &iter : supported_formats) {
    GstVideoFormat format = gst_d3d12_dxgi_format_to_gst (iter);

    if (format == GST_VIDEO_FORMAT_UNKNOWN) {
      GST_ERROR_OBJECT (device,
          "Failed to get video format from dxgi format %d", iter);
    }

    if (!format_string.empty ())
      format_string += ", ";

     format_string += gst_video_format_to_string (format);
  }
  /* *INDENT-ON* */

  std::string src_caps_string;
  /* TODO: support d3d12 memory */
  src_caps_string = "video/x-raw, format = (string) ";
  if (supported_formats.size () > 1) {
    src_caps_string += "{ " + format_string + " }";
  } else {
    src_caps_string += format_string;
  }

  GstCaps *src_caps = gst_caps_from_string (src_caps_string.c_str ());
  GstCaps *sink_caps;

  switch (codec) {
    case GST_DXVA_CODEC_H264:
      sink_caps = gst_caps_from_string ("video/x-h264, "
          "stream-format= (string) { avc, avc3, byte-stream }, "
          "alignment= (string) au, "
          "profile = (string) { high, progressive-high, constrained-high, main, "
          "constrained-baseline, baseline }");
      break;
    default:
      g_assert_not_reached ();
      return nullptr;
  }

  gint max_res = MAX (max_resolution.width, max_resolution.height);
  gst_caps_set_simple (sink_caps,
      "width", GST_TYPE_INT_RANGE, 1, max_res,
      "height", GST_TYPE_INT_RANGE, 1, max_res, nullptr);
  gst_caps_set_simple (src_caps,
      "width", GST_TYPE_INT_RANGE, 1, max_res,
      "height", GST_TYPE_INT_RANGE, 1, max_res, nullptr);

  GstD3D12DecoderClassData *ret = g_new0 (GstD3D12DecoderClassData, 1);
  GstD3D12DecoderSubClassData *subclass_data = &ret->subclass_data;

  ret->sink_caps = sink_caps;
  ret->src_caps = src_caps;

  subclass_data->codec = codec;

  /* class data will be leaked if the element never gets instantiated */
  GST_MINI_OBJECT_FLAG_SET (sink_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (src_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  g_object_get (device, "adapter-luid", &subclass_data->adapter_luid,
      "device-id", &subclass_data->device_id, "vendor-id",
      &subclass_data->vendor_id, "description", &ret->description, nullptr);

  GST_DEBUG_OBJECT (device,
      "Configured sink caps: %" GST_PTR_FORMAT ", src caps: %" GST_PTR_FORMAT,
      sink_caps, src_caps);

  return ret;
}

static void
gst_d3d12_decoder_class_data_free (GstD3D12DecoderClassData * data)
{
  if (!data)
    return;

  gst_clear_caps (&data->sink_caps);
  gst_clear_caps (&data->src_caps);
  g_free (data->description);
  g_free (data);
}

void
gst_d3d12_decoder_class_data_fill_subclass_data (GstD3D12DecoderClassData *
    data, GstD3D12DecoderSubClassData * subclass_data)
{
  g_return_if_fail (data != nullptr);
  g_return_if_fail (subclass_data != nullptr);

  *subclass_data = data->subclass_data;
}

void
gst_d3d12_decoder_proxy_class_init (GstElementClass * klass,
    GstD3D12DecoderClassData * data, const gchar * author)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstD3D12DecoderSubClassData *cdata = &data->subclass_data;
  std::string long_name;
  std::string description;
  const gchar *codec_name;
  GParamFlags param_flags = (GParamFlags) (GST_PARAM_DOC_SHOW_DEFAULT |
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  GstPadTemplate *pad_templ;

  g_object_class_install_property (gobject_class, PROP_DECODER_ADAPTER_LUID,
      g_param_spec_int64 ("adapter-luid", "Adapter LUID",
          "DXGI Adapter LUID (Locally Unique Identifier) of created device",
          G_MININT64, G_MAXINT64, 0, param_flags));

  g_object_class_install_property (gobject_class, PROP_DECODER_DEVICE_ID,
      g_param_spec_uint ("device-id", "Device Id",
          "DXGI Device ID", 0, G_MAXUINT32, 0, param_flags));

  g_object_class_install_property (gobject_class, PROP_DECODER_VENDOR_ID,
      g_param_spec_uint ("vendor-id", "Vendor Id",
          "DXGI Vendor ID", 0, G_MAXUINT32, 0, param_flags));

  codec_name = gst_dxva_codec_to_string (cdata->codec);
  long_name = "Direct3D12/DXVA " + std::string (codec_name) + " " +
      std::string (data->description) + " Decoder";
  description = "Direct3D12/DXVA based " + std::string (codec_name) +
      " video decoder";

  gst_element_class_set_metadata (klass, long_name.c_str (),
      "Codec/Decoder/Video/Hardware", description.c_str (), author);

  pad_templ = gst_pad_template_new ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS, data->sink_caps);
  gst_element_class_add_pad_template (klass, pad_templ);

  pad_templ = gst_pad_template_new ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS, data->src_caps);
  gst_element_class_add_pad_template (klass, pad_templ);

  gst_d3d12_decoder_class_data_free (data);
}

void
gst_d3d12_decoder_proxy_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec,
    GstD3D12DecoderSubClassData * subclass_data)
{
  switch (prop_id) {
    case PROP_DECODER_ADAPTER_LUID:
      g_value_set_int64 (value, subclass_data->adapter_luid);
      break;
    case PROP_DECODER_DEVICE_ID:
      g_value_set_uint (value, subclass_data->device_id);
      break;
    case PROP_DECODER_VENDOR_ID:
      g_value_set_uint (value, subclass_data->vendor_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_d3d12_decoder_proxy_open (GstVideoDecoder * videodec,
    GstD3D12DecoderSubClassData * subclass_data, GstD3D12Device ** device,
    GstD3D12Decoder ** decoder)
{
  GstElement *elem = GST_ELEMENT (videodec);

  if (!gst_d3d12_ensure_element_data_for_adapter_luid (elem,
          subclass_data->adapter_luid, device)) {
    GST_ERROR_OBJECT (elem, "Cannot create d3d12device");
    return FALSE;
  }

  *decoder = gst_d3d12_decoder_new (*device, subclass_data->codec);

  if (*decoder == nullptr) {
    GST_ERROR_OBJECT (elem, "Cannot create d3d12 decoder");
    gst_clear_object (device);
    return FALSE;
  }

  return TRUE;
}
