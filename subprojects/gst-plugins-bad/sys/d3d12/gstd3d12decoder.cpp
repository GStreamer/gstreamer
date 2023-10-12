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
#include "gstd3d12bufferpool.h"
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
#include <algorithm>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d12_decoder_debug);
#define GST_CAT_DEFAULT gst_d3d12_decoder_debug

struct DecoderFormat
{
  GstDxvaCodec codec;
  const GUID decode_profile;
  DXGI_FORMAT format[3];
};

static const DecoderFormat format_list[] = {
  {GST_DXVA_CODEC_H264, D3D12_VIDEO_DECODE_PROFILE_H264,
      {DXGI_FORMAT_NV12, DXGI_FORMAT_UNKNOWN,}},
  {GST_DXVA_CODEC_H265, D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN,
      {DXGI_FORMAT_NV12, DXGI_FORMAT_UNKNOWN,}},
  {GST_DXVA_CODEC_H265, D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN10,
      DXGI_FORMAT_P010},
  {GST_DXVA_CODEC_VP9, D3D12_VIDEO_DECODE_PROFILE_VP9,
      {DXGI_FORMAT_NV12, DXGI_FORMAT_UNKNOWN,}},
  {GST_DXVA_CODEC_VP9, D3D12_VIDEO_DECODE_PROFILE_VP9_10BIT_PROFILE2,
      {DXGI_FORMAT_P010, DXGI_FORMAT_UNKNOWN,}},
  {GST_DXVA_CODEC_AV1, D3D12_VIDEO_DECODE_PROFILE_AV1_PROFILE0,
      {DXGI_FORMAT_NV12, DXGI_FORMAT_P010}},
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
  GstD3D12DecoderPicture (GstBuffer * dpb_buf, GstBuffer * out_buf,
      std::shared_ptr<GstD3D12Dpb> d3d12_dpb,
      ID3D12VideoDecoderHeap * decoder_heap, guint8 dxva_id)
      : buffer(dpb_buf), output_buffer(out_buf)
      , heap(decoder_heap), dpb(d3d12_dpb), view_id(dxva_id)
  {
  }

  ~GstD3D12DecoderPicture ()
  {
    auto d3d12_dpb = dpb.lock ();
    if (d3d12_dpb)
      d3d12_dpb->Release (view_id);

    if (buffer)
      gst_buffer_unref (buffer);
    if (output_buffer)
      gst_buffer_unref (output_buffer);
  }

  GstBuffer *buffer;
  GstBuffer *output_buffer;
  ComPtr<ID3D12VideoDecoderHeap> heap;
  std::weak_ptr<GstD3D12Dpb> dpb;

  guint8 view_id;
};

static GType gst_d3d12_decoder_picture_get_type (void);
#define GST_TYPE_D3D12_DECODER_PICTURE (gst_d3d12_decoder_picture_get_type ())
GST_DEFINE_MINI_OBJECT_TYPE (GstD3D12DecoderPicture, gst_d3d12_decoder_picture);

typedef enum
{
  GST_D3D12_DECODER_OUTPUT_D3D12,
  GST_D3D12_DECODER_OUTPUT_D3D11,
  GST_D3D12_DECODER_OUTPUT_SYSTEM,
} GstD3D12DecoderOutputType;

struct GstD3D12DecoderPrivate
{
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

    if (dpb_pool) {
      gst_buffer_pool_set_active (dpb_pool, FALSE);
      gst_object_unref (dpb_pool);
    }

    if (output_pool) {
      gst_buffer_pool_set_active (output_pool, FALSE);
      gst_object_unref (output_pool);
    }
  }

  /* reference textures */
  GstBufferPool *dpb_pool = nullptr;

  /* Used for output if reference-only texture is required */
  GstBufferPool *output_pool = nullptr;

  gboolean configured = FALSE;
  gboolean opened = FALSE;
  gboolean flushing = FALSE;

  GstVideoInfo info;
  GstVideoInfo output_info;
  gint crop_x = 0;
  gint crop_y = 0;
  gint coded_width = 0;
  gint coded_height = 0;
  DXGI_FORMAT decoder_format = DXGI_FORMAT_UNKNOWN;
  gboolean reference_only = FALSE;
  gboolean use_array_of_texture = FALSE;
  GstD3D12DecoderOutputType output_type = GST_D3D12_DECODER_OUTPUT_SYSTEM;
  guint downstream_min_buffers = 0;
  gboolean need_crop = FALSE;
  gboolean use_crop_meta = FALSE;
  gboolean wait_on_pool_full = FALSE;

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

  ComPtr<ID3D12Resource> staging;

  GstD3D12Fence *fence = nullptr;

  gint64 luid;

  std::mutex lock;
};
/* *INDENT-ON* */

struct _GstD3D12Decoder
{
  GstObject parent;

  GstDxvaCodec codec;
  GstD3D12Device *device;
  GstD3D11Device *d3d11_device;
  gint64 adapter_luid;

  CRITICAL_SECTION context_lock;

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
  InitializeCriticalSection (&self->context_lock);
}

static void
gst_d3d12_decoder_finalize (GObject * object)
{
  GstD3D12Decoder *self = GST_D3D12_DECODER (object);

  gst_d3d12_decoder_close (self);
  DeleteCriticalSection (&self->context_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_decoder_clear_resource (GstD3D12Decoder * self)
{
  GstD3D12DecoderPrivate *priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  gst_d3d12_fence_wait (priv->fence);

  if (priv->dpb_pool) {
    gst_buffer_pool_set_active (priv->dpb_pool, FALSE);
    gst_clear_object (&priv->dpb_pool);
  }

  if (priv->output_pool) {
    gst_buffer_pool_set_active (priv->output_pool, FALSE);
    gst_clear_object (&priv->output_pool);
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
gst_d3d12_decoder_new (GstDxvaCodec codec, gint64 adapter_luid)
{
  GstD3D12Decoder *self;

  g_return_val_if_fail (codec > GST_DXVA_CODEC_NONE, nullptr);
  g_return_val_if_fail (codec < GST_DXVA_CODEC_LAST, nullptr);

  self = (GstD3D12Decoder *) g_object_new (GST_TYPE_D3D12_DECODER, nullptr);
  self->codec = codec;
  self->adapter_luid = adapter_luid;

  return self;
}

gboolean
gst_d3d12_decoder_open (GstD3D12Decoder * decoder, GstElement * element)
{
  if (!gst_d3d12_ensure_element_data_for_adapter_luid (element,
          decoder->adapter_luid, &decoder->device)) {
    GST_ERROR_OBJECT (element, "Cannot create d3d12device");
    return FALSE;
  }

  if (!decoder->priv) {
    GstD3D12DecoderPrivate *priv;
    ComPtr < ID3D12VideoDevice > video_device;
    ComPtr < ID3D12CommandAllocator > copy_ca;
    ComPtr < ID3D12GraphicsCommandList > copy_cl;
    ComPtr < ID3D12CommandAllocator > ca;
    ComPtr < ID3D12VideoDecodeCommandList > cl;
    ComPtr < ID3D12CommandQueue > cq;
    ID3D12Device *device_handle;
    D3D12_COMMAND_QUEUE_DESC desc = { };
    HRESULT hr;

    device_handle = gst_d3d12_device_get_device_handle (decoder->device);
    hr = device_handle->QueryInterface (IID_PPV_ARGS (&video_device));
    if (!gst_d3d12_result (hr, decoder->device))
      return FALSE;

    hr = device_handle->CreateCommandAllocator (D3D12_COMMAND_LIST_TYPE_COPY,
        IID_PPV_ARGS (&copy_ca));
    if (!gst_d3d12_result (hr, decoder->device))
      return FALSE;

    hr = device_handle->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_COPY,
        copy_ca.Get (), nullptr, IID_PPV_ARGS (&copy_cl));
    if (!gst_d3d12_result (hr, decoder->device))
      return FALSE;

    hr = copy_cl->Close ();
    if (!gst_d3d12_result (hr, decoder->device))
      return FALSE;

    hr = device_handle->CreateCommandAllocator
        (D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE, IID_PPV_ARGS (&ca));
    if (!gst_d3d12_result (hr, decoder->device))
      return FALSE;

    hr = device_handle->CreateCommandList (0,
        D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE, ca.Get (), nullptr,
        IID_PPV_ARGS (&cl));
    if (!gst_d3d12_result (hr, decoder->device))
      return FALSE;

    hr = cl->Close ();
    if (!gst_d3d12_result (hr, decoder->device))
      return FALSE;

    desc.Type = D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr = device_handle->CreateCommandQueue (&desc, IID_PPV_ARGS (&cq));
    if (!gst_d3d12_result (hr, decoder->device))
      return FALSE;

    priv = new GstD3D12DecoderPrivate ();
    priv->fence = gst_d3d12_fence_new (decoder->device);
    priv->video_device = video_device;
    priv->copy_ca = copy_ca;
    priv->copy_cl = copy_cl;
    priv->ca = ca;
    priv->cl = cl;
    priv->cq = cq;

    decoder->priv = priv;
  }

  return TRUE;
}

gboolean
gst_d3d12_decoder_close (GstD3D12Decoder * decoder)
{
  if (decoder->priv) {
    delete decoder->priv;
    decoder->priv = nullptr;
  }

  gst_clear_object (&decoder->device);
  gst_clear_object (&decoder->d3d11_device);

  return TRUE;
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

  if (!priv) {
    GST_ERROR_OBJECT (decoder, "Device is not configured");
    return GST_FLOW_ERROR;
  }

  gst_d3d12_decoder_reset (decoder);

  if (!gst_d3d12_device_get_format (decoder->device,
          GST_VIDEO_INFO_FORMAT (info), &device_format) ||
      device_format.dxgi_format == DXGI_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (decoder, "Could not determine dxgi format from %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)));
    return GST_FLOW_ERROR;
  }

  if (decoder->codec == GST_DXVA_CODEC_H264)
    dpb_size += 1;

  priv->input_state = gst_video_codec_state_ref (input_state);
  priv->info = priv->output_info = *info;
  priv->crop_x = crop_x;
  priv->crop_y = crop_y;
  priv->coded_width = coded_width;
  priv->coded_height = coded_height;
  priv->dpb_size = dpb_size;
  priv->decoder_format = device_format.dxgi_format;

  if (crop_x != 0 || crop_y != 0)
    priv->need_crop = TRUE;
  else
    priv->need_crop = FALSE;

  priv->configured = TRUE;

  return GST_FLOW_OK;
}

static gboolean
gst_d3d12_decoder_prepare_pool (GstD3D12Decoder * self)
{
  GstD3D12DecoderPrivate *priv = self->priv;
  GstVideoInfo *info = &priv->info;
  GstVideoAlignment align;
  GstD3D12AllocationParams *params;
  GstStructure *config;
  GstCaps *caps;

  GST_DEBUG_OBJECT (self, "Preparing allocator");

  std::lock_guard < std::mutex > lk (priv->lock);
  if (priv->dpb_pool) {
    gst_buffer_pool_set_active (priv->dpb_pool, FALSE);
    gst_clear_object (&priv->dpb_pool);
  }

  if (priv->output_pool) {
    gst_buffer_pool_set_active (priv->output_pool, FALSE);
    gst_clear_object (&priv->output_pool);
  }

  D3D12_RESOURCE_FLAGS resource_flags;
  if ((priv->support.ConfigurationFlags &
          D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_REFERENCE_ONLY_ALLOCATIONS_REQUIRED)
      != 0) {
    resource_flags =
        D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY |
        D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    priv->reference_only = TRUE;
  } else {
    resource_flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    priv->reference_only = FALSE;
  }

  GstD3D12AllocationFlags alloc_flags = GST_D3D12_ALLOCATION_FLAG_DEFAULT;
  guint max_buffers = priv->dpb_size;
  /* Tier 1 decoder requires array */
  if (priv->support.DecodeTier == D3D12_VIDEO_DECODE_TIER_1) {
    priv->use_array_of_texture = FALSE;
    alloc_flags = GST_D3D12_ALLOCATION_FLAG_TEXTURE_ARRAY;
  } else {
    priv->use_array_of_texture = TRUE;
    max_buffers = 0;
  }

  params = gst_d3d12_allocation_params_new (self->device, info, alloc_flags,
      resource_flags);

  gst_video_alignment_reset (&align);
  align.padding_right = priv->aligned_width - GST_VIDEO_INFO_WIDTH (info);
  align.padding_bottom = priv->aligned_height - GST_VIDEO_INFO_HEIGHT (info);
  gst_d3d12_allocation_params_alignment (params, &align);

  if (!priv->use_array_of_texture)
    params->desc[0].DepthOrArraySize = (UINT16) max_buffers;

  priv->dpb_pool = gst_d3d12_buffer_pool_new (self->device);
  config = gst_buffer_pool_get_config (priv->dpb_pool);
  caps = gst_video_info_to_caps (info);

  gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
  gst_d3d12_allocation_params_free (params);
  gst_buffer_pool_config_set_params (config, caps, GST_VIDEO_INFO_SIZE (info),
      0, max_buffers);
  if (!gst_buffer_pool_set_config (priv->dpb_pool, config)) {
    GST_ERROR_OBJECT (self, "Pool config failed");
    gst_caps_unref (caps);
    return FALSE;
  }

  gst_buffer_pool_set_active (priv->dpb_pool, TRUE);

  /* In case that device requires reference only dpb texture, we need another
   * texture pool for outputting without VIDEO_DECODE_REFERENCE_ONLY flag */
  if (priv->reference_only) {
    GST_DEBUG_OBJECT (self, "Creating output only allocator");
    priv->output_pool = gst_d3d12_buffer_pool_new (self->device);
    config = gst_buffer_pool_get_config (priv->output_pool);

    params = gst_d3d12_allocation_params_new (self->device, info, alloc_flags,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
    gst_d3d12_allocation_params_alignment (params, &align);
    gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
    gst_d3d12_allocation_params_free (params);
    gst_buffer_pool_config_set_params (config, caps, GST_VIDEO_INFO_SIZE (info),
        0, 0);
    if (!gst_buffer_pool_set_config (priv->output_pool, config)) {
      GST_ERROR_OBJECT (self, "Output pool config failed");
      gst_caps_unref (caps);
      return FALSE;
    }
    gst_buffer_pool_set_active (priv->output_pool, TRUE);
  }

  gst_caps_unref (caps);

  priv->dpb = std::make_shared < GstD3D12Dpb > ((guint8) priv->dpb_size);

  return TRUE;
}

static void
gst_d3d12_decoder_picture_free (GstD3D12DecoderPicture * self)
{
  delete self;
}

static GstD3D12DecoderPicture *
gst_d3d12_decoder_picture_new (GstD3D12Decoder * self, GstBuffer * buffer,
    GstBuffer * output_buffer, ID3D12VideoDecoderHeap * heap)
{
  GstD3D12DecoderPrivate *priv = self->priv;
  GstD3D12DecoderPicture *picture;
  GstMemory *mem = gst_buffer_peek_memory (buffer, 0);

  auto view_id = priv->dpb->Acquire (GST_D3D12_MEMORY_CAST (mem), heap);
  if (view_id == 0xff) {
    GST_WARNING_OBJECT (self, "No empty picture");
    gst_buffer_unref (buffer);
    if (output_buffer)
      gst_buffer_unref (output_buffer);
    return nullptr;
  }

  picture = new GstD3D12DecoderPicture (buffer, output_buffer, priv->dpb, heap,
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
  GstBuffer *buffer;
  GstBuffer *output_buffer = nullptr;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_D3D12_DECODER (decoder), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), GST_FLOW_ERROR);
  g_return_val_if_fail (picture != nullptr, GST_FLOW_ERROR);

  priv = decoder->priv;

  if (!priv->dpb_pool) {
    /* Try negotiate again whatever the previous negotiation result was.
     * There could be updated field(s) in sinkpad caps after we negotiated with
     * downstream on new_sequence() call. For example, h264/h265 parse
     * will be able to update HDR10 related caps field after parsing
     * corresponding SEI messages which are usually placed after the essential
     * headers */
    gst_video_decoder_negotiate (videodec);

    if (!gst_d3d12_decoder_prepare_pool (decoder)) {
      GST_ERROR_OBJECT (videodec, "Failed to setup dpb pool");
      return GST_FLOW_ERROR;
    }
  }

  ret = gst_buffer_pool_acquire_buffer (priv->dpb_pool, &buffer, nullptr);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (videodec, "Couldn't acquire memory");
    return ret;
  }

  if (priv->reference_only) {
    ret = gst_buffer_pool_acquire_buffer (priv->output_pool,
        &output_buffer, nullptr);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (videodec, "Couldn't acquire output memory");
      gst_buffer_unref (buffer);
      return ret;
    }
  }

  decoder_pic = gst_d3d12_decoder_picture_new (decoder, buffer, output_buffer,
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
        gst_d3d12_device_get_device_handle (self->device);
    ComPtr < ID3D12Resource > bitstream;
    size_t alloc_size = GST_ROUND_UP_128 (size) + 1024;

    D3D12_HEAP_PROPERTIES heap_prop =
        CD3D12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC desc = CD3D12_RESOURCE_DESC::Buffer (alloc_size);
    hr = device_handle->CreateCommittedResource (&heap_prop,
        D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS (&bitstream));
    if (!gst_d3d12_result (hr, self->device)) {
      GST_ERROR_OBJECT (self, "Failed to create bitstream buffer");
      return FALSE;
    }

    GST_LOG_OBJECT (self, "Allocated new bitstream buffer with size %"
        G_GSIZE_FORMAT, size);

    priv->bitstream = bitstream;
    priv->bitstream_size = alloc_size;
  }

  hr = priv->bitstream->Map (0, nullptr, &map_data);
  if (!gst_d3d12_result (hr, self->device)) {
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
  std::vector < GstD3D12DecoderPicture * >configured_ref_pics;

  if (!decoder_pic) {
    GST_ERROR_OBJECT (decoder, "No attached decoder picture");
    return GST_FLOW_ERROR;
  }

  if (!args->bitstream || args->bitstream_size == 0) {
    GST_ERROR_OBJECT (decoder, "No bitstream buffer passed");
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (decoder, "End picture with dxva-id %d, num-ref-pics %u",
      decoder_pic->view_id, ref_pics->len);

  /* Wait for previous fence if needed */
  gst_d3d12_fence_wait (priv->fence);

  if (!gst_d3d12_decoder_upload_bitstream (decoder, args->bitstream,
          args->bitstream_size)) {
    return GST_FLOW_ERROR;
  }

  memset (&in_args, 0, sizeof (D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS));
  memset (&out_args, 0, sizeof (D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS));

  hr = priv->ca->Reset ();
  if (!gst_d3d12_result (hr, decoder->device)) {
    GST_ERROR_OBJECT (decoder, "Couldn't reset command allocator");
    return GST_FLOW_ERROR;
  }

  hr = priv->cl->Reset (priv->ca.Get ());
  if (!gst_d3d12_result (hr, decoder->device)) {
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

    if (std::find (configured_ref_pics.begin (), configured_ref_pics.end (),
            ref_dec_pic) != configured_ref_pics.end ()) {
      continue;
    }

    configured_ref_pics.push_back (ref_dec_pic);

    dmem = (GstD3D12Memory *) gst_buffer_peek_memory (ref_dec_pic->buffer, 0);

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

  if (decoder_pic->output_buffer) {
    dmem = (GstD3D12Memory *)
        gst_buffer_peek_memory (decoder_pic->output_buffer, 0);
    out_resource = gst_d3d12_memory_get_resource_handle (dmem);

    pre_barriers.push_back (CD3D12_RESOURCE_BARRIER::Transition (out_resource,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE));
    post_barriers.push_back (CD3D12_RESOURCE_BARRIER::Transition (out_resource,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
            D3D12_RESOURCE_STATE_COMMON));
  }

  dmem = (GstD3D12Memory *) gst_buffer_peek_memory (decoder_pic->buffer, 0);
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
  if (!gst_d3d12_result (hr, decoder->device)) {
    GST_ERROR_OBJECT (decoder, "Couldn't record decoding command");
    return GST_FLOW_ERROR;
  }

  ID3D12CommandList *cl[] = { priv->cl.Get () };
  priv->cq->ExecuteCommandLists (1, cl);

  fence_handle = gst_d3d12_fence_get_handle (priv->fence);
  fence_value = gst_d3d12_device_get_fence_value (decoder->device);
  hr = priv->cq->Signal (fence_handle, fence_value);
  if (!gst_d3d12_result (hr, decoder->device)) {
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
  ID3D12Device *device = gst_d3d12_device_get_device_handle (self->device);
  D3D12_RESOURCE_DESC tex_desc =
      CD3D12_RESOURCE_DESC::Tex2D (priv->decoder_format,
      priv->aligned_width, priv->aligned_height);

  device->GetCopyableFootprints (&tex_desc, 0, 2, 0, priv->layout, nullptr,
      nullptr, &size);

  D3D12_HEAP_PROPERTIES heap_prop = CD3D12_HEAP_PROPERTIES
      (D3D12_HEAP_TYPE_READBACK);
  D3D12_RESOURCE_DESC desc = CD3D12_RESOURCE_DESC::Buffer (size);

  hr = device->CreateCommittedResource (&heap_prop, D3D12_HEAP_FLAG_NONE,
      &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS (&staging));
  if (!gst_d3d12_result (hr, self->device))
    return FALSE;

  priv->staging = staging;

  return TRUE;
}

static gboolean
gst_d3d12_decoder_can_direct_render (GstD3D12Decoder * self,
    GstVideoDecoder * videodec, GstBuffer * buffer,
    gint display_width, gint display_height)
{
  GstD3D12DecoderPrivate *priv = self->priv;

  /* We don't support direct render for reverse playback */
  if (videodec->input_segment.rate < 0)
    return FALSE;

  if (priv->output_type != GST_D3D12_DECODER_OUTPUT_D3D12)
    return FALSE;

  if (display_width != GST_VIDEO_INFO_WIDTH (&priv->info) ||
      display_height != GST_VIDEO_INFO_HEIGHT (&priv->info)) {
    return FALSE;
  }

  /* We need to crop but downstream does not support crop, need to copy */
  if (priv->need_crop && !priv->use_crop_meta)
    return FALSE;

  /* we can do direct render in this case, since there is no DPB pool size
   * limit, or output picture does not use texture array */
  if (priv->use_array_of_texture || priv->reference_only)
    return TRUE;

  /* Let's believe downstream info */
  if (priv->wait_on_pool_full)
    return TRUE;

  GstMemory *mem = gst_buffer_peek_memory (buffer, 0);
  GstD3D12PoolAllocator *alloc = (GstD3D12PoolAllocator *) mem->allocator;
  guint max_size = 0;
  guint outstanding_size = 0;

  gst_d3d12_pool_allocator_get_pool_size (alloc, &max_size, &outstanding_size);

  if (max_size <= outstanding_size + 1) {
    GST_DEBUG_OBJECT (self, "memory pool is about to full (%u/%u)",
        outstanding_size, max_size);
    return FALSE;
  }

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
  GstFlowReturn ret = GST_FLOW_ERROR;
  ID3D12CommandList *list[1];
  UINT64 fence_value;
  GstBuffer *buffer;
  GstD3D12Memory *dmem;
  ID3D12Resource *resource;
  UINT subresource[2];
  void *map_data;
  HRESULT hr;
  GstVideoFrame vframe;
  ID3D12CommandQueue *copy_queue;

  g_return_val_if_fail (GST_IS_D3D12_DECODER (decoder), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), GST_FLOW_ERROR);
  g_return_val_if_fail (picture != nullptr, GST_FLOW_ERROR);

  priv = decoder->priv;

  copy_queue = gst_d3d12_device_get_copy_queue (decoder->device);
  if (!copy_queue) {
    ret = GST_FLOW_ERROR;
    goto error;
  }

  decoder_pic = (GstD3D12DecoderPicture *)
      gst_codec_picture_get_user_data (picture);
  if (!decoder_pic) {
    ret = GST_FLOW_ERROR;
    goto error;
  }

  if (display_width != GST_VIDEO_INFO_WIDTH (&priv->output_info) ||
      display_height != GST_VIDEO_INFO_HEIGHT (&priv->output_info)) {
    GST_INFO_OBJECT (videodec, "Frame size changed, do renegotiate");

    gst_video_info_set_format (&priv->output_info,
        GST_VIDEO_INFO_FORMAT (&priv->info), display_width, display_height);
    GST_VIDEO_INFO_INTERLACE_MODE (&priv->output_info) =
        GST_VIDEO_INFO_INTERLACE_MODE (&priv->info);

    if (!gst_video_decoder_negotiate (videodec)) {
      GST_ERROR_OBJECT (videodec, "Failed to re-negotiate with new frame size");
      ret = GST_FLOW_NOT_NEGOTIATED;
      goto error;
    }
  } else if (picture->discont_state) {
    if (!gst_video_decoder_negotiate (videodec)) {
      GST_ERROR_OBJECT (videodec, "Could not re-negotiate with updated state");
      ret = GST_FLOW_NOT_NEGOTIATED;
      goto error;
    }
  }

  /* Wait for pending decoding operation before copying */
  gst_d3d12_fence_wait (priv->fence);
  buffer = decoder_pic->output_buffer ?
      decoder_pic->output_buffer : decoder_pic->buffer;
  dmem = (GstD3D12Memory *) gst_buffer_peek_memory (buffer, 0);

  if (gst_d3d12_decoder_can_direct_render (decoder, videodec,
          decoder_pic->buffer, display_width, display_height)) {
    GST_LOG_OBJECT (decoder, "Outputting without copy");

    GST_MINI_OBJECT_FLAG_SET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);
    GST_MINI_OBJECT_FLAG_UNSET (dmem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);

    if (priv->need_crop) {
      GstVideoCropMeta *crop_meta;

      buffer = gst_buffer_make_writable (buffer);
      crop_meta = gst_buffer_get_video_crop_meta (buffer);
      if (!crop_meta)
        crop_meta = gst_buffer_add_video_crop_meta (buffer);

      crop_meta->x = priv->crop_x;
      crop_meta->y = priv->crop_y;
      crop_meta->width = priv->info.width;
      crop_meta->height = priv->info.height;

      GST_TRACE_OBJECT (decoder, "Attatching crop meta");
    }

    frame->output_buffer = gst_buffer_ref (buffer);
  } else {
    GstMemory *mem;
    ID3D12Resource *out_resource = nullptr;
    UINT out_subresource[2];
    GstD3D12Fence *out_fence = priv->fence;
    ComPtr < ID3D12Resource > shared_resource;

    ret = gst_video_decoder_allocate_output_frame (videodec, frame);
    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (videodec, "Couldn't allocate output buffer");
      goto error;
    }

    mem = gst_buffer_peek_memory (frame->output_buffer, 0);
    if (gst_is_d3d12_memory (mem)) {
      dmem = GST_D3D12_MEMORY_CAST (mem);
      if (dmem->device == decoder->device) {
        out_resource = gst_d3d12_memory_get_resource_handle (dmem);
        gst_d3d12_memory_get_subresource_index (dmem, 0, &out_subresource[0]);
        gst_d3d12_memory_get_subresource_index (dmem, 1, &out_subresource[1]);
        out_fence = dmem->fence;

        GST_MINI_OBJECT_FLAG_SET (dmem,
            GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);
        GST_MINI_OBJECT_FLAG_UNSET (dmem,
            GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);
      }
    } else if (gst_is_d3d11_memory (mem) &&
        GST_D3D11_MEMORY_CAST (mem)->device == decoder->d3d11_device) {
      HANDLE resource_handle;
      if (gst_d3d11_memory_get_nt_handle (GST_D3D11_MEMORY_CAST (mem),
              &resource_handle)) {
        ID3D12Device *device_handle =
            gst_d3d12_device_get_device_handle (decoder->device);
        hr = device_handle->OpenSharedHandle (resource_handle,
            IID_PPV_ARGS (&shared_resource));
        if (gst_d3d12_result (hr, decoder->device)) {
          out_resource = shared_resource.Get ();
          out_subresource[0] = 0;
          out_subresource[1] = 1;
          GST_MINI_OBJECT_FLAG_SET (mem,
              GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);
          GST_MINI_OBJECT_FLAG_UNSET (mem,
              GST_D3D11_MEMORY_TRANSFER_NEED_UPLOAD);
        }
      }
    }

    if (!out_resource && !gst_d3d12_decoder_ensure_staging_texture (decoder)) {
      GST_ERROR_OBJECT (videodec, "Couldn't allocate staging texture");
      ret = GST_FLOW_ERROR;
      goto error;
    }

    resource = gst_d3d12_memory_get_resource_handle (dmem);

    gst_d3d12_memory_get_subresource_index (dmem, 0, &subresource[0]);
    gst_d3d12_memory_get_subresource_index (dmem, 1, &subresource[1]);

    /* Copy texture to staging */
    hr = priv->copy_ca->Reset ();
    if (!gst_d3d12_result (hr, decoder->device)) {
      ret = GST_FLOW_ERROR;
      goto error;
    }

    hr = priv->copy_cl->Reset (priv->copy_ca.Get (), nullptr);
    if (!gst_d3d12_result (hr, decoder->device)) {
      ret = GST_FLOW_ERROR;
      goto error;
    }

    if (shared_resource)
      gst_d3d11_device_lock (decoder->d3d11_device);

    /* simultaneous access must be enabled already, so,barrier is not needed */
    for (guint i = 0; i < 2; i++) {
      D3D12_TEXTURE_COPY_LOCATION src =
          CD3D12_TEXTURE_COPY_LOCATION (resource, subresource[i]);
      D3D12_TEXTURE_COPY_LOCATION dst;
      D3D12_BOX src_box = { 0, };

      if (out_resource) {
        dst = CD3D12_TEXTURE_COPY_LOCATION (out_resource, out_subresource[i]);
      } else {
        dst =
            CD3D12_TEXTURE_COPY_LOCATION (priv->staging.Get (),
            priv->layout[i]);
      }

      /* FIXME: only 4:2:0 */
      if (i == 0) {
        src_box.left = GST_ROUND_UP_2 (priv->crop_x);
        src_box.top = GST_ROUND_UP_2 (priv->crop_y);
        src_box.right = GST_ROUND_UP_2 (priv->crop_x + priv->output_info.width);
        src_box.bottom =
            GST_ROUND_UP_2 (priv->crop_y + priv->output_info.height);
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
    if (!gst_d3d12_result (hr, decoder->device)) {
      GST_ERROR_OBJECT (videodec, "Couldn't record copy command");
      if (shared_resource)
        gst_d3d11_device_unlock (decoder->d3d11_device);
      ret = GST_FLOW_ERROR;
      goto error;
    }

    list[0] = priv->copy_cl.Get ();
    copy_queue->ExecuteCommandLists (1, list);

    fence_value = gst_d3d12_device_get_fence_value (decoder->device);
    hr = copy_queue->Signal (gst_d3d12_fence_get_handle (out_fence),
        fence_value);
    if (!gst_d3d12_result (hr, decoder->device)) {
      if (shared_resource)
        gst_d3d11_device_unlock (decoder->d3d11_device);
      ret = GST_FLOW_ERROR;
      goto error;
    }

    gst_d3d12_fence_set_event_on_completion_value (out_fence, fence_value);

    if (!out_resource) {
      gst_d3d12_fence_wait (out_fence);

      hr = priv->staging->Map (0, nullptr, &map_data);
      if (!gst_d3d12_result (hr, decoder->device)) {
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

        for (gint j = 0; j < GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, i); j++) {
          memcpy (dst, src, width);
          dst += GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, i);
          src += priv->layout[i].Footprint.RowPitch;
        }
      }

      priv->staging->Unmap (0, nullptr);
      gst_video_frame_unmap (&vframe);
    } else if (shared_resource) {
      gst_d3d12_fence_wait (out_fence);
      gst_d3d11_device_unlock (decoder->d3d11_device);
    }
  }

  gst_codec_picture_unref (picture);
  return gst_video_decoder_finish_frame (videodec, frame);

error:
  gst_codec_picture_unref (picture);
  gst_video_decoder_release_frame (videodec, frame);

  return ret;
}

static gboolean
gst_d3d12_decoder_create (GstD3D12Decoder * self)
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
    decoder_foramt = nullptr;

    if (format_list[i].codec != self->codec)
      continue;

    for (guint j = 0; j < G_N_ELEMENTS (format_list[i].format); j++) {
      DXGI_FORMAT format = format_list[i].format[j];

      if (format == DXGI_FORMAT_UNKNOWN)
        break;

      if (format == priv->decoder_format) {
        decoder_foramt = &format_list[i];
        break;
      }
    }

    if (!decoder_foramt)
      continue;

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
    if (!gst_d3d12_result (hr, self->device)) {
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
  if (!gst_d3d12_result (hr, self->device)) {
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
  GstCaps *peer_caps;
  GstVideoCodecState *state = nullptr;
  GstVideoCodecState *input_state = priv->input_state;
  GstStructure *s;
  const gchar *str;
  gboolean d3d12_supported = FALSE;
  gboolean d3d11_supported = FALSE;

  peer_caps = gst_pad_get_allowed_caps (GST_VIDEO_DECODER_SRC_PAD (videodec));
  GST_DEBUG_OBJECT (videodec, "Allowed caps %" GST_PTR_FORMAT, peer_caps);

  if (!peer_caps || gst_caps_is_any (peer_caps)) {
    GST_DEBUG_OBJECT (videodec,
        "cannot determine output format, use system memory");
  } else {
    GstCapsFeatures *features;
    guint size = gst_caps_get_size (peer_caps);
    guint i;

    for (i = 0; i < size; i++) {
      features = gst_caps_get_features (peer_caps, i);

      if (!features)
        continue;

      if (gst_caps_features_contains (features,
              GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY)) {
        d3d12_supported = TRUE;
      } else if (gst_caps_features_contains (features,
              GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
        d3d11_supported = TRUE;
      }
    }
  }
  gst_clear_caps (&peer_caps);

  GST_DEBUG_OBJECT (videodec, "Downstream feature support, D3D12 memory: %d",
      d3d12_supported);

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

  priv->output_type = GST_D3D12_DECODER_OUTPUT_SYSTEM;
  if (d3d12_supported) {
    gst_caps_set_features (state->caps, 0,
        gst_caps_features_new_single (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY));
    priv->output_type = GST_D3D12_DECODER_OUTPUT_D3D12;
  } else if (d3d11_supported
      && gst_d3d11_ensure_element_data_for_adapter_luid (GST_ELEMENT (videodec),
          decoder->adapter_luid, &decoder->d3d11_device)) {
    gst_caps_set_features (state->caps, 0,
        gst_caps_features_new_single (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY));
    priv->output_type = GST_D3D12_DECODER_OUTPUT_D3D11;
  }

  return gst_d3d12_decoder_create (decoder);
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

  if (priv->output_type == GST_D3D12_DECODER_OUTPUT_D3D12) {
    priv->use_crop_meta =
        gst_query_find_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE,
        nullptr);
  } else {
    priv->use_crop_meta = FALSE;
  }

  gst_video_info_from_caps (&vinfo, outcaps);
  n = gst_query_get_n_allocation_pools (query);
  if (n > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  if (pool) {
    if (priv->output_type == GST_D3D12_DECODER_OUTPUT_D3D12) {
      if (!GST_IS_D3D12_BUFFER_POOL (pool)) {
        GST_DEBUG_OBJECT (videodec,
            "Downstream pool is not d3d12, will create new one");
        gst_clear_object (&pool);
      } else {
        GstD3D12BufferPool *dpool = GST_D3D12_BUFFER_POOL (pool);
        if (dpool->device != decoder->device) {
          GST_DEBUG_OBJECT (videodec, "Different device, will create new one");
          gst_clear_object (&pool);
        }
      }
    } else if (priv->output_type == GST_D3D12_DECODER_OUTPUT_D3D11) {
      if (!GST_IS_D3D11_BUFFER_POOL (pool)) {
        GST_DEBUG_OBJECT (videodec,
            "Downstream pool is not d3d11, will create new one");
        gst_clear_object (&pool);
      } else {
        GstD3D11BufferPool *dpool = GST_D3D11_BUFFER_POOL (pool);
        if (dpool->device != decoder->d3d11_device) {
          GST_DEBUG_OBJECT (videodec, "Different device, will create new one");
          gst_clear_object (&pool);
        }
      }
    }
  }

  if (!pool) {
    switch (priv->output_type) {
      case GST_D3D12_DECODER_OUTPUT_D3D12:
        pool = gst_d3d12_buffer_pool_new (decoder->device);
        break;
      case GST_D3D12_DECODER_OUTPUT_D3D11:
        pool = gst_d3d11_buffer_pool_new (decoder->d3d11_device);
        break;
      default:
        pool = gst_video_buffer_pool_new ();
        break;
    }

    size = (guint) vinfo.size;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (priv->output_type == GST_D3D12_DECODER_OUTPUT_D3D12) {
    GstD3D12AllocationParams *params;
    GstVideoAlignment align;
    gint width, height;

    gst_video_alignment_reset (&align);

    params = gst_buffer_pool_config_get_d3d12_allocation_params (config);
    if (!params) {
      params = gst_d3d12_allocation_params_new (decoder->device, &vinfo,
          GST_D3D12_ALLOCATION_FLAG_DEFAULT,
          D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
    } else {
      params->desc[0].Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    }

    width = GST_VIDEO_INFO_WIDTH (&vinfo);
    height = GST_VIDEO_INFO_HEIGHT (&vinfo);

    /* need alignment to copy decoder output texture to downstream texture */
    align.padding_right = GST_ROUND_UP_16 (width) - width;
    align.padding_bottom = GST_ROUND_UP_16 (height) - height;
    gst_d3d12_allocation_params_alignment (params, &align);
    gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
    gst_d3d12_allocation_params_free (params);

    /* Store min buffer size. We need to take account of the amount of buffers
     * which might be held by downstream in case of zero-copy playback */
    if (!priv->dpb_pool) {
      if (n > 0) {
        GST_DEBUG_OBJECT (videodec, "Downstream proposed pool");
        priv->wait_on_pool_full = TRUE;
        /* XXX: hardcoded bound 16, to avoid too large pool size */
        priv->downstream_min_buffers = MIN (min, 16);
      } else {
        GST_DEBUG_OBJECT (videodec, "Downstream didn't propose pool");
        priv->wait_on_pool_full = FALSE;
        /* don't know how many buffers would be queued by downstream */
        priv->downstream_min_buffers = 4;
      }
    } else {
      /* We configured our DPB pool already, let's check if our margin can
       * cover min size */
      priv->wait_on_pool_full = FALSE;

      if (n > 0) {
        if (priv->downstream_min_buffers >= min)
          priv->wait_on_pool_full = TRUE;

        GST_DEBUG_OBJECT (videodec,
            "Pre-allocated margin %d can%s cover downstream min size %d",
            priv->downstream_min_buffers,
            priv->wait_on_pool_full ? "" : "not", min);
      } else {
        GST_DEBUG_OBJECT (videodec, "Downstream min size is unknown");
      }
    }

    GST_DEBUG_OBJECT (videodec, "Downstream min buffres: %d", min);

    /* We will not use downstream pool for decoding, and therefore preallocation
     * is unnecessary. So, Non-zero min buffer will be a waste of GPU memory */
    min = 0;
  } else if (priv->output_type == GST_D3D12_DECODER_OUTPUT_D3D11) {
    GstD3D11AllocationParams *params;
    const guint bind_flags = D3D11_BIND_SHADER_RESOURCE |
        D3D11_BIND_RENDER_TARGET;
    const guint misc_flags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
        D3D11_RESOURCE_MISC_SHARED;

    params = gst_d3d11_allocation_params_new (decoder->d3d11_device, &vinfo,
        GST_D3D11_ALLOCATION_FLAG_DEFAULT, bind_flags, misc_flags);
    gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
    gst_d3d11_allocation_params_free (params);
  }

  gst_buffer_pool_set_config (pool, config);
  /* d3d12 buffer pool will update buffer size based on allocated texture,
   * get size from config again */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  if (n > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);
  gst_object_unref (pool);

  return TRUE;
}

static void
gst_d3d12_decoder_set_flushing (GstD3D12Decoder * self, gboolean flushing)
{
  GstD3D12DecoderPrivate *priv = self->priv;
  std::lock_guard < std::mutex > lk (priv->lock);

  if (priv->dpb_pool)
    gst_buffer_pool_set_flushing (priv->dpb_pool, flushing);
  if (priv->output_pool)
    gst_buffer_pool_set_flushing (priv->output_pool, flushing);
  priv->flushing = flushing;
}

void
gst_d3d12_decoder_sink_event (GstD3D12Decoder * decoder, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      gst_d3d12_decoder_set_flushing (decoder, TRUE);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_d3d12_decoder_set_flushing (decoder, FALSE);
      break;
    default:
      break;
  }
}

void
gst_d3d12_decoder_set_context (GstD3D12Decoder * decoder, GstElement * element,
    GstContext * context)
{
  GstD3D12CSLockGuard lk (&decoder->context_lock);

  gst_d3d12_handle_set_context_for_adapter_luid (element,
      context, decoder->adapter_luid, &decoder->device);
  gst_d3d11_handle_set_context_for_adapter_luid (element,
      context, decoder->adapter_luid, &decoder->d3d11_device);
}

gboolean
gst_d3d12_decoder_handle_query (GstD3D12Decoder * decoder, GstElement * element,
    GstQuery * query)
{
  if (GST_QUERY_TYPE (query) != GST_QUERY_CONTEXT)
    return FALSE;

  GstD3D12CSLockGuard lk (&decoder->context_lock);
  if (gst_d3d12_handle_context_query (element, query, decoder->device) ||
      gst_d3d11_handle_context_query (element, query, decoder->d3d11_device)) {
    return TRUE;
  }

  return FALSE;
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

static void
gst_d3d12_decoder_get_profiles (const GUID & profile,
    std::vector < std::string > &list)
{
  if (profile == D3D12_VIDEO_DECODE_PROFILE_H264) {
    list.push_back ("high");
    list.push_back ("progressive-high");
    list.push_back ("constrained-high");
    list.push_back ("main");
    list.push_back ("constrained-baseline");
    list.push_back ("baseline");
  } else if (profile == D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN) {
    list.push_back ("main");
  } else if (profile == D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN10) {
    list.push_back ("main-10");
  } else if (profile == D3D12_VIDEO_DECODE_PROFILE_VP9) {
    list.push_back ("0");
  } else if (profile == D3D12_VIDEO_DECODE_PROFILE_VP9_10BIT_PROFILE2) {
    list.push_back ("2");
  } else if (profile == D3D12_VIDEO_DECODE_PROFILE_AV1_PROFILE0) {
    list.push_back ("main");
  } else {
    g_assert_not_reached ();
  }
}

GstD3D12DecoderClassData *
gst_d3d12_decoder_check_feature_support (GstD3D12Device * device,
    ID3D12VideoDevice * video_device, GstDxvaCodec codec,
    gboolean d3d11_interop)
{
  HRESULT hr;
  GstDxvaResolution max_resolution = { 0, 0 };
  D3D12_VIDEO_DECODE_CONFIGURATION_FLAGS config_flags =
      D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_NONE;
  D3D12_VIDEO_DECODE_TIER tier = D3D12_VIDEO_DECODE_TIER_NOT_SUPPORTED;
  std::set < DXGI_FORMAT > supported_formats;
  std::vector < std::string > profiles;

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
    s.FrameRate = { 0, 1 };
    s.BitRate = 0;

    bool supported = false;
    for (guint j = 0; j < G_N_ELEMENTS (format_list[i].format); j++) {
      s.DecodeFormat = format_list[i].format[j];
      if (s.DecodeFormat == DXGI_FORMAT_UNKNOWN)
        break;

      for (guint k = 0; k < G_N_ELEMENTS (gst_dxva_resolutions); k++) {
        s.Width = gst_dxva_resolutions[k].width;
        s.Height = gst_dxva_resolutions[k].height;

        hr = video_device->CheckFeatureSupport
            (D3D12_FEATURE_VIDEO_DECODE_SUPPORT, &s,
            sizeof (D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT));
        if (FAILED (hr))
          break;

        if ((s.SupportFlags & D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED) == 0)
          break;

        if (max_resolution.width < gst_dxva_resolutions[k].width)
          max_resolution.width = gst_dxva_resolutions[k].width;
        if (max_resolution.height < gst_dxva_resolutions[k].height)
          max_resolution.height = gst_dxva_resolutions[k].height;

        supported_formats.insert (format_list[i].format[j]);
        config_flags = s.ConfigurationFlags;
        tier = s.DecodeTier;
        supported = true;
      }
    }

    if (supported)
      gst_d3d12_decoder_get_profiles (format_list[i].decode_profile, profiles);
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

  std::string sink_caps_string;
  std::string profile_string;

  switch (codec) {
    case GST_DXVA_CODEC_H264:
      sink_caps_string = "video/x-h264, "
          "stream-format=(string) { avc, avc3, byte-stream }, "
          "alignment=(string) au";
      break;
    case GST_DXVA_CODEC_H265:
      sink_caps_string = "video/x-h265, "
          "stream-format=(string) { hev1, hvc1, byte-stream }, "
          "alignment=(string) au";
      break;
    case GST_DXVA_CODEC_VP9:
      if (profiles.size () > 1) {
        sink_caps_string =
            "video/x-vp9, alignment = (string) frame, profile = (string) 0; "
            "video/x-vp9, alignment = (string) frame, profile = (string) 2, "
            "bit-depth-luma = (uint) 10, bit-depth-chroma = (uint) 10";
      } else if (profiles[0] == "0") {
        sink_caps_string =
            "video/x-vp9, alignment = (string) frame, profile = (string) 0";
      } else {
        sink_caps_string =
            "video/x-vp9, alignment = (string) frame, profile = (string) 2, "
            "bit-depth-luma = (uint) 10, bit-depth-chroma = (uint) 10";
      }
      break;
    case GST_DXVA_CODEC_AV1:
      sink_caps_string = "video/x-av1, alignment = (string) frame";
      break;
    default:
      g_assert_not_reached ();
      return nullptr;
  }

  /* *INDENT-OFF* */
  if (codec != GST_DXVA_CODEC_VP9) {
    if (profiles.size () > 1) {
      profile_string = "{ ";
      bool first = true;
      for (auto it : profiles) {
        if (!first)
          profile_string += ", ";

        profile_string += it;
        first = false;
      }

      profile_string += " }";
    } else {
      profile_string = profiles[0];
    }

    sink_caps_string += ", profile=(string) ";
    sink_caps_string += profile_string;
  }
  /* *INDENT-ON* */

  GstCaps *sink_caps = gst_caps_from_string (sink_caps_string.c_str ());
  GstCaps *raw_caps = gst_caps_from_string (src_caps_string.c_str ());
  GstCaps *src_caps = gst_caps_copy (raw_caps);
  gst_caps_set_features_simple (src_caps,
      gst_caps_features_new_single (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY));
  if (d3d11_interop) {
    GstCaps *d3d11_caps = gst_caps_copy (raw_caps);
    gst_caps_set_features_simple (d3d11_caps,
        gst_caps_features_new_single (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY));
    gst_caps_append (src_caps, d3d11_caps);
  }
  gst_caps_append (src_caps, raw_caps);

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
