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

#include "gstd3d12plugin-config.h"

#include "gstd3d12decoder.h"
#include "gstd3d12decodercpbpool.h"
#include <directx/d3dx12.h>
#include <gst/base/gstqueuearray.h>
#include <wrl.h>
#include <string.h>
#include <mutex>
#include <condition_variable>
#include <set>
#include <vector>
#include <queue>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <atomic>

#ifdef HAVE_GST_D3D11
#include <d3d11_4.h>
#include <gst/d3d11/gstd3d11.h>
#include <gst/d3d11/gstd3d11-private.h>
#endif

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    cat = _gst_debug_category_new ("d3d12decoder", 0, "d3d12decoder");
  } GST_D3D12_CALL_ONCE_END;

  return cat;
}
#endif /* GST_DISABLE_GST_DEBUG */

struct DecoderFormat
{
  GstDxvaCodec codec;
  const GUID decode_profile;
  DXGI_FORMAT format[3];
};

static const DecoderFormat format_list[] = {
  {GST_DXVA_CODEC_MPEG2, D3D12_VIDEO_DECODE_PROFILE_MPEG2,
      {DXGI_FORMAT_NV12, DXGI_FORMAT_UNKNOWN,}},
  {GST_DXVA_CODEC_MPEG2, D3D12_VIDEO_DECODE_PROFILE_MPEG1_AND_MPEG2,
      {DXGI_FORMAT_NV12, DXGI_FORMAT_UNKNOWN,}},
  {GST_DXVA_CODEC_H264, D3D12_VIDEO_DECODE_PROFILE_H264,
      {DXGI_FORMAT_NV12, DXGI_FORMAT_UNKNOWN,}},
  {GST_DXVA_CODEC_H265, D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN,
      {DXGI_FORMAT_NV12, DXGI_FORMAT_UNKNOWN,}},
  {GST_DXVA_CODEC_H265, D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN10,
      DXGI_FORMAT_P010},
  {GST_DXVA_CODEC_VP8, D3D12_VIDEO_DECODE_PROFILE_VP8,
      {DXGI_FORMAT_NV12, DXGI_FORMAT_UNKNOWN,}},
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
    std::unique_lock <std::mutex> lk (lock_);
    while (dxva_id_.empty ())
      cond_.wait (lk);

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

    if (id == 0xff || id >= size_) {
      GST_WARNING ("Unexpected id %d", id);
      return;
    }

    dxva_id_.push (id);
    textures_[id] = nullptr;
    subresources_[id] = 0;
    heaps_[id] = nullptr;

    cond_.notify_one ();
  }

  void GetReferenceFrames (D3D12_VIDEO_DECODE_REFERENCE_FRAMES & frames)
  {
    frames.NumTexture2Ds = size_;
    frames.ppTexture2Ds = textures_.data ();
    frames.pSubresources = subresources_.data ();
    frames.ppHeaps = heaps_.data ();
  }

  void Lock ()
  {
    lock_.lock ();
  }

  void Unlock ()
  {
    lock_.unlock ();
  }

private:
  std::queue<guint8> dxva_id_;
  std::mutex lock_;
  std::condition_variable cond_;
  guint size_;
  std::vector<ID3D12Resource *> textures_;
  std::vector<UINT> subresources_;
  std::vector<ID3D12VideoDecoderHeap *> heaps_;
  bool flushing = false;
};

struct GstD3D12DecoderPicture : public GstMiniObject
{
  GstD3D12DecoderPicture (GstBuffer * dpb_buf, GstBuffer * out_buf,
      std::shared_ptr<GstD3D12Dpb> d3d12_dpb, ID3D12VideoDecoder * dec,
      ID3D12VideoDecoderHeap * decoder_heap, guint8 dxva_id)
      : buffer(dpb_buf), output_buffer(out_buf)
      , decoder(dec), heap(decoder_heap), dpb(d3d12_dpb), view_id(dxva_id) {}

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
  ComPtr<ID3D12VideoDecoder> decoder;
  ComPtr<ID3D12VideoDecoderHeap> heap;
  std::weak_ptr<GstD3D12Dpb> dpb;
  guint64 fence_val = 0;

  guint8 view_id;
};

static GType gst_d3d12_decoder_picture_get_type (void);
#define GST_TYPE_D3D12_DECODER_PICTURE (gst_d3d12_decoder_picture_get_type ())
GST_DEFINE_MINI_OBJECT_TYPE (GstD3D12DecoderPicture, gst_d3d12_decoder_picture);

enum GstD3D12DecoderOutputType
{
  GST_D3D12_DECODER_OUTPUT_UNKNOWN = 0,
  GST_D3D12_DECODER_OUTPUT_SYSTEM = (1 << 0),
  GST_D3D12_DECODER_OUTPUT_D3D12 = (1 << 1),
  GST_D3D12_DECODER_OUTPUT_D3D11 = (1 << 2),
};

DEFINE_ENUM_FLAG_OPERATORS (GstD3D12DecoderOutputType);

constexpr UINT64 ASYNC_DEPTH = 4;

struct DecoderCmdData
{
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12VideoDevice> video_device;
  ComPtr<ID3D12VideoDecodeCommandList> cl;
  GstD3D12CmdQueue *queue = nullptr;
  bool need_full_drain = false;

  /* Fence to wait at command record thread */
  UINT64 fence_val = 0;

#ifdef HAVE_GST_D3D11
  ComPtr<ID3D11Device5> device11_5;
  ComPtr<ID3D11DeviceContext4> context11_4;
  ComPtr<ID3D11Fence> fence11;
  ComPtr<ID3D12Fence> fence12;

  UINT64 fence_val_11 = 0;
#endif
};

struct DecoderOutputData
{
  GstVideoDecoder *decoder = nullptr;
  GstVideoCodecFrame *frame = nullptr;
  GstCodecPicture *picture = nullptr;
  gint width = 0;
  gint height = 0;
  GstVideoBufferFlags buffer_flags = (GstVideoBufferFlags) 0;
};

struct DecoderSessionData
{
  DecoderSessionData ()
  {
    output_queue = gst_vec_deque_new_for_struct (sizeof (DecoderOutputData),
        16);
  }

  ~DecoderSessionData ()
  {
    if (dpb_pool) {
      gst_buffer_pool_set_active (dpb_pool, FALSE);
      gst_object_unref (dpb_pool);
    }

    if (output_pool) {
      gst_buffer_pool_set_active (output_pool, FALSE);
      gst_object_unref (output_pool);
    }

    if (input_state)
      gst_video_codec_state_unref (input_state);

    if (output_state)
      gst_video_codec_state_unref (output_state);

    gst_vec_deque_free (output_queue);
    gst_clear_object (&cpb_pool);
  }

  D3D12_VIDEO_DECODER_DESC decoder_desc = {};
  ComPtr<ID3D12VideoDecoder> decoder;

  D3D12_VIDEO_DECODER_HEAP_DESC heap_desc = {};
  ComPtr<ID3D12VideoDecoderHeap> heap;

  std::shared_ptr<GstD3D12Dpb> dpb;

  ComPtr<ID3D12Resource> staging;

  GstBufferPool *dpb_pool = nullptr;

  /* Used for output if reference-only texture is required */
  GstBufferPool *output_pool = nullptr;

  GstD3D12DecoderCpbPool *cpb_pool = nullptr;

  GstVideoCodecState *input_state = nullptr;
  GstVideoCodecState *output_state = nullptr;

  gint aligned_width = 0;
  gint aligned_height = 0;
  guint dpb_size = 0;

  GstVideoInfo info;
  GstVideoInfo output_info;
  gint crop_x = 0;
  gint crop_y = 0;
  gint coded_width = 0;
  gint coded_height = 0;
  DXGI_FORMAT decoder_format = DXGI_FORMAT_UNKNOWN;
  bool need_crop = false;
  bool use_crop_meta = false;
  bool array_of_textures = false;
  bool reference_only = false;

  GstD3D12DecoderOutputType output_type = GST_D3D12_DECODER_OUTPUT_SYSTEM;

  D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT support = { 0, };

  /* For staging */
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout[GST_VIDEO_MAX_PLANES] = { 0, };

  std::mutex queue_lock;
  std::condition_variable queue_cond;
  GstVecDeque *output_queue;

  std::recursive_mutex lock;
};

struct GstD3D12DecoderPrivate
{
  GstD3D12DecoderPrivate ()
  {
    fence_data_pool = gst_d3d12_fence_data_pool_new ();
  }

  ~GstD3D12DecoderPrivate ()
  {
    gst_clear_object (&fence_data_pool);
  }

  std::mutex lock;
  std::recursive_mutex context_lock;

  std::unique_ptr<DecoderCmdData> cmd;
  std::unique_ptr<DecoderSessionData> session;
  GThread *output_thread = nullptr;
  std::atomic<bool> flushing;
  std::atomic<GstFlowReturn> last_flow;

  GstD3D12FenceDataPool *fence_data_pool;

  std::vector<D3D12_RESOURCE_BARRIER> pre_barriers;
  std::vector<D3D12_RESOURCE_BARRIER> post_barriers;
  std::vector < GstD3D12DecoderPicture * >configured_ref_pics;
};

/* *INDENT-ON* */

struct _GstD3D12Decoder
{
  GstObject parent;

  GstDxvaCodec codec;
  GstD3D12Device *device;
  gint64 adapter_luid;
  gboolean d3d11_interop;

#ifdef HAVE_GST_D3D11
  GstD3D11Device *device11;
#endif

  GstD3D12DecoderPrivate *priv;
};

static void gst_d3d12_decoder_finalize (GObject * object);
static gpointer gst_d3d12_decoder_output_loop (GstD3D12Decoder * self);

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
  self->priv = new GstD3D12DecoderPrivate ();
}

static void
gst_d3d12_decoder_finalize (GObject * object)
{
  auto self = GST_D3D12_DECODER (object);

  gst_clear_object (&self->device);
#ifdef HAVE_GST_D3D11
  gst_clear_object (&self->device11);
#endif

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

GstD3D12Decoder *
gst_d3d12_decoder_new (const GstD3D12DecoderSubClassData * cdata)
{
  GstD3D12Decoder *self;

  g_return_val_if_fail (cdata, nullptr);

  self = (GstD3D12Decoder *) g_object_new (GST_TYPE_D3D12_DECODER, nullptr);
  self->codec = cdata->codec;
  self->adapter_luid = cdata->adapter_luid;
  self->d3d11_interop = cdata->d3d11_interop;

  return self;
}

gboolean
gst_d3d12_decoder_open (GstD3D12Decoder * decoder, GstElement * element)
{
  auto priv = decoder->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->context_lock);
    if (!gst_d3d12_ensure_element_data_for_adapter_luid (element,
            decoder->adapter_luid, &decoder->device)) {
      GST_ERROR_OBJECT (element, "Cannot create d3d12device");
      return FALSE;
    }
  }

  auto cmd = std::make_unique < DecoderCmdData > ();
  HRESULT hr;

  cmd->device = gst_d3d12_device_get_device_handle (decoder->device);

  hr = cmd->device.As (&cmd->video_device);
  if (!gst_d3d12_result (hr, decoder->device)) {
    GST_ERROR_OBJECT (element, "ID3D12VideoDevice interface is unavailable");
    return FALSE;
  }

  cmd->queue = gst_d3d12_device_get_decode_queue (decoder->device);
  if (!cmd->queue) {
    GST_ERROR_OBJECT (element, "Couldn't create command queue");
    return FALSE;
  }

  auto flags = gst_d3d12_device_get_workaround_flags (decoder->device);
  if ((flags & GST_D3D12_WA_DECODER_RACE) == GST_D3D12_WA_DECODER_RACE)
    cmd->need_full_drain = true;

  priv->cmd = std::move (cmd);
  priv->flushing = false;

  return TRUE;
}

GstFlowReturn
gst_d3d12_decoder_drain (GstD3D12Decoder * decoder, GstVideoDecoder * videodec)
{
  auto priv = decoder->priv;

  GST_DEBUG_OBJECT (decoder, "Draining");
  if (priv->cmd)
    gst_d3d12_cmd_queue_fence_wait (priv->cmd->queue, priv->cmd->fence_val);

  GST_VIDEO_DECODER_STREAM_UNLOCK (videodec);
  if (priv->output_thread && priv->session) {
    auto empty_data = DecoderOutputData ();
    std::lock_guard < std::mutex > lk (priv->session->queue_lock);
    gst_vec_deque_push_tail_struct (priv->session->output_queue, &empty_data);
    priv->session->queue_cond.notify_one ();
  }

  g_clear_pointer (&priv->output_thread, g_thread_join);
  GST_VIDEO_DECODER_STREAM_LOCK (videodec);

  GST_DEBUG_OBJECT (decoder, "Drain done");

  return GST_FLOW_OK;
}

gboolean
gst_d3d12_decoder_flush (GstD3D12Decoder * decoder, GstVideoDecoder * videodec)
{
  auto priv = decoder->priv;

  GST_DEBUG_OBJECT (decoder, "Flushing");

  priv->flushing = true;
  gst_d3d12_decoder_drain (decoder, videodec);
  priv->flushing = false;
  priv->last_flow = GST_FLOW_OK;

  GST_DEBUG_OBJECT (decoder, "Flush done");

  return TRUE;
}

gboolean
gst_d3d12_decoder_close (GstD3D12Decoder * decoder)
{
  auto priv = decoder->priv;

  GST_DEBUG_OBJECT (decoder, "Close");

  {
    GstD3D12DeviceDecoderLockGuard lk (decoder->device);
    priv->session = nullptr;
    priv->cmd = nullptr;
  }

  gst_clear_object (&decoder->device);

  return TRUE;
}

GstFlowReturn
gst_d3d12_decoder_configure (GstD3D12Decoder * decoder,
    GstVideoDecoder * videodec, GstVideoCodecState * input_state,
    const GstVideoInfo * info,
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

  if (!decoder->device) {
    GST_ERROR_OBJECT (decoder, "Device was not configured");
    return GST_FLOW_ERROR;
  }

  GstD3D12DeviceDecoderLockGuard dlk (decoder->device);

  GstD3D12Format device_format;
  auto priv = decoder->priv;
  HRESULT hr;

  gst_d3d12_decoder_drain (decoder, videodec);
  priv->session = nullptr;

  /* XXX: NVIDIA driver crash if cmdlist is reused... */
  priv->cmd->cl = nullptr;

  if (!gst_d3d12_device_get_format (decoder->device,
          GST_VIDEO_INFO_FORMAT (info), &device_format) ||
      device_format.dxgi_format == DXGI_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (decoder, "Could not determine dxgi format from %s",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (info)));
    return GST_FLOW_ERROR;
  }

  if (decoder->codec == GST_DXVA_CODEC_H264)
    dpb_size += 1;

  /* +2 for threading */
  dpb_size += 2;

  dpb_size = MAX (dpb_size, ASYNC_DEPTH);

  auto session = std::make_unique < DecoderSessionData > ();
  session->input_state = gst_video_codec_state_ref (input_state);
  session->info = *info;
  session->output_info = *info;
  session->crop_x = crop_x;
  session->crop_y = crop_y;
  session->coded_width = coded_width;
  session->coded_height = coded_height;
  session->dpb_size = dpb_size;
  session->decoder_format = device_format.dxgi_format;
  session->cpb_pool = gst_d3d12_decoder_cpb_pool_new (priv->cmd->device.Get ());

  if (crop_x != 0 || crop_y != 0)
    session->need_crop = true;

  bool supported = false;
  for (guint i = 0; i < G_N_ELEMENTS (format_list); i++) {
    const DecoderFormat *decoder_format = nullptr;
    if (format_list[i].codec != decoder->codec)
      continue;

    for (guint j = 0; j < G_N_ELEMENTS (format_list[i].format); j++) {
      DXGI_FORMAT format = format_list[i].format[j];

      if (format == DXGI_FORMAT_UNKNOWN)
        break;

      if (format == session->decoder_format) {
        decoder_format = &format_list[i];
        break;
      }
    }

    if (!decoder_format)
      continue;

    D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT support = { };
    support.Configuration.DecodeProfile = decoder_format->decode_profile;
    support.Configuration.BitstreamEncryption =
        D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE;

    if (GST_VIDEO_INFO_IS_INTERLACED (info) &&
        GST_VIDEO_INFO_INTERLACE_MODE (info) !=
        GST_VIDEO_INTERLACE_MODE_ALTERNATE) {
      support.Configuration.InterlaceType =
          D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_FIELD_BASED;
    } else {
      support.Configuration.InterlaceType =
          D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE;
    }

    support.DecodeFormat = session->decoder_format;
    support.FrameRate = { 0, 1 };
    support.Width = coded_width;
    support.Height = coded_height;
    hr = priv->cmd->
        video_device->CheckFeatureSupport (D3D12_FEATURE_VIDEO_DECODE_SUPPORT,
        &support, sizeof (support));
    if (FAILED (hr))
      continue;

    if ((support.SupportFlags & D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED) == 0)
      continue;

    session->support = support;
    supported = true;
    break;
  }

  if (!supported) {
    GST_ERROR_OBJECT (decoder,
        "Decoder does not support current configuration");
    return GST_FLOW_ERROR;
  }

  const auto & support = session->support;
  guint alignment = 16;
  if ((support.ConfigurationFlags &
          D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_HEIGHT_ALIGNMENT_MULTIPLE_32_REQUIRED)
      != 0) {
    alignment = 32;
  }

  session->aligned_width = GST_ROUND_UP_N (session->coded_width, alignment);
  session->aligned_height = GST_ROUND_UP_N (session->coded_height, alignment);

  D3D12_VIDEO_DECODER_DESC desc = { };
  desc.NodeMask = 0;
  desc.Configuration = support.Configuration;
  hr = priv->cmd->video_device->CreateVideoDecoder (&desc,
      IID_PPV_ARGS (&session->decoder));
  if (!gst_d3d12_result (hr, decoder->device)) {
    GST_ERROR_OBJECT (decoder, "Couldn't create decoder object");
    return GST_FLOW_ERROR;
  }

  session->decoder_desc = desc;

  D3D12_VIDEO_DECODER_HEAP_DESC heap_desc;
  heap_desc.NodeMask = 0;
  heap_desc.Configuration = session->support.Configuration;
  heap_desc.DecodeWidth = session->aligned_width;
  heap_desc.DecodeHeight = session->aligned_height;
  heap_desc.Format = session->decoder_format;
  heap_desc.FrameRate = { 0, 1 };
  heap_desc.BitRate = 0;
  heap_desc.MaxDecodePictureBufferCount = session->dpb_size;
  hr = priv->cmd->video_device->CreateVideoDecoderHeap (&heap_desc,
      IID_PPV_ARGS (&session->heap));
  if (!gst_d3d12_result (hr, decoder->device)) {
    GST_ERROR_OBJECT (decoder, "Couldn't create decoder heap");
    return GST_FLOW_ERROR;
  }

  guint max_buffers = session->dpb_size;
  if (support.DecodeTier == D3D12_VIDEO_DECODE_TIER_1) {
    session->array_of_textures = false;
  } else {
    session->array_of_textures = true;
    max_buffers = 0;
  }

  D3D12_RESOURCE_FLAGS resource_flags;
  if ((support.ConfigurationFlags &
          D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_REFERENCE_ONLY_ALLOCATIONS_REQUIRED)
      != 0 || !session->array_of_textures) {
    resource_flags =
        D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY |
        D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    session->reference_only = true;
  } else {
    resource_flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    session->reference_only = false;
  }

  GST_DEBUG_OBJECT (decoder, "reference only: %d, array-of-textures: %d",
      session->reference_only, session->array_of_textures);

  GstVideoAlignment align;
  gst_video_alignment_reset (&align);
  align.padding_right = session->aligned_width - info->width;
  align.padding_bottom = session->aligned_height - info->height;

  D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;
  if (gst_d3d12_device_non_zeroed_supported (decoder->device))
    heap_flags = D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

  if (!session->reference_only)
    heap_flags |= D3D12_HEAP_FLAG_SHARED;

  auto params = gst_d3d12_allocation_params_new (decoder->device, info,
      GST_D3D12_ALLOCATION_FLAG_DEFAULT, resource_flags, heap_flags);
  gst_d3d12_allocation_params_alignment (params, &align);
  if (!session->array_of_textures)
    gst_d3d12_allocation_params_set_array_size (params, session->dpb_size);

  session->dpb_pool = gst_d3d12_buffer_pool_new (decoder->device);
  auto config = gst_buffer_pool_get_config (session->dpb_pool);
  auto caps = gst_video_info_to_caps (info);

  gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
  gst_d3d12_allocation_params_free (params);
  gst_buffer_pool_config_set_params (config, caps, info->size, 0, max_buffers);
  if (!gst_buffer_pool_set_config (session->dpb_pool, config)) {
    GST_ERROR_OBJECT (decoder, "Couldn't set pool config");
    gst_caps_unref (caps);
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_pool_set_active (session->dpb_pool, TRUE)) {
    GST_ERROR_OBJECT (decoder, "Set active failed");
    gst_caps_unref (caps);
    return GST_FLOW_ERROR;
  }

  if (session->reference_only) {
    GST_DEBUG_OBJECT (decoder, "Creating output only pool");
    session->output_pool = gst_d3d12_buffer_pool_new (decoder->device);
    config = gst_buffer_pool_get_config (session->output_pool);

    heap_flags = D3D12_HEAP_FLAG_NONE;
    if (gst_d3d12_device_non_zeroed_supported (decoder->device))
      heap_flags = D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

    params = gst_d3d12_allocation_params_new (decoder->device, info,
        GST_D3D12_ALLOCATION_FLAG_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        heap_flags | D3D12_HEAP_FLAG_SHARED);
    gst_d3d12_allocation_params_alignment (params, &align);
    gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
    gst_d3d12_allocation_params_free (params);
    gst_buffer_pool_config_set_params (config, caps, info->size, 0, 0);

    if (!gst_buffer_pool_set_config (session->output_pool, config)) {
      GST_ERROR_OBJECT (decoder, "Couldn't set pool config");
      gst_caps_unref (caps);
      return GST_FLOW_ERROR;
    }

    if (!gst_buffer_pool_set_active (session->output_pool, TRUE)) {
      GST_ERROR_OBJECT (decoder, "Set active failed");
      gst_caps_unref (caps);
      return GST_FLOW_ERROR;
    }
  }

  gst_caps_unref (caps);

  session->dpb = std::make_shared < GstD3D12Dpb > ((guint8) session->dpb_size);

  priv->session = std::move (session);
  priv->last_flow = GST_FLOW_OK;

  return GST_FLOW_OK;
}

gboolean
gst_d3d12_decoder_stop (GstD3D12Decoder * decoder)
{
  auto priv = decoder->priv;

  GST_DEBUG_OBJECT (decoder, "Stop");

  priv->flushing = true;
  if (priv->cmd) {
    if (priv->cmd->need_full_drain) {
      gst_d3d12_cmd_queue_drain (priv->cmd->queue);
    } else {
      gst_d3d12_cmd_queue_fence_wait (priv->cmd->queue, priv->cmd->fence_val);
    }
  }

  if (priv->output_thread && priv->session) {
    auto empty_data = DecoderOutputData ();
    std::lock_guard < std::mutex > lk (priv->session->queue_lock);
    gst_vec_deque_push_tail_struct (priv->session->output_queue, &empty_data);
    priv->session->queue_cond.notify_one ();
  }

  g_clear_pointer (&priv->output_thread, g_thread_join);
  priv->flushing = false;

  GstD3D12DeviceDecoderLockGuard lk (decoder->device);
  priv->session = nullptr;

  return TRUE;
}

static void
gst_d3d12_decoder_picture_free (GstD3D12DecoderPicture * self)
{
  delete self;
}

static GstD3D12DecoderPicture *
gst_d3d12_decoder_picture_new (GstD3D12Decoder * self, GstBuffer * buffer,
    GstBuffer * output_buffer, ID3D12VideoDecoder * dec,
    ID3D12VideoDecoderHeap * heap)
{
  auto priv = self->priv;
  auto mem = (GstD3D12Memory *) gst_buffer_peek_memory (buffer, 0);

  auto view_id = priv->session->dpb->Acquire (mem, heap);
  if (view_id == 0xff) {
    GST_WARNING_OBJECT (self, "No empty picture");
    gst_buffer_unref (buffer);
    if (output_buffer)
      gst_buffer_unref (output_buffer);
    return nullptr;
  }

  auto picture = new GstD3D12DecoderPicture (buffer, output_buffer,
      priv->session->dpb, dec, heap, view_id);

  gst_mini_object_init (picture, 0, GST_TYPE_D3D12_DECODER_PICTURE,
      nullptr, nullptr,
      (GstMiniObjectFreeFunction) gst_d3d12_decoder_picture_free);

  return picture;
}

GstFlowReturn
gst_d3d12_decoder_new_picture (GstD3D12Decoder * decoder,
    GstVideoDecoder * videodec, GstCodecPicture * picture)
{
  g_return_val_if_fail (GST_IS_D3D12_DECODER (decoder), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), GST_FLOW_ERROR);
  g_return_val_if_fail (picture != nullptr, GST_FLOW_ERROR);

  auto priv = decoder->priv;
  if (!priv->session) {
    GST_ERROR_OBJECT (decoder, "No session configured");
    return GST_FLOW_ERROR;
  }

  GST_VIDEO_DECODER_STREAM_UNLOCK (videodec);
  GstBuffer *buffer;
  auto ret = gst_buffer_pool_acquire_buffer (priv->session->dpb_pool,
      &buffer, nullptr);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (videodec, "Couldn't acquire memory");
    GST_VIDEO_DECODER_STREAM_LOCK (videodec);
    return ret;
  }

  GstBuffer *output_buffer = nullptr;
  if (priv->session->reference_only) {
    ret = gst_buffer_pool_acquire_buffer (priv->session->output_pool,
        &output_buffer, nullptr);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (videodec, "Couldn't acquire output memory");
      GST_VIDEO_DECODER_STREAM_LOCK (videodec);
      gst_buffer_unref (buffer);
      return ret;
    }
  }

  /* unlock so that output thread can output picture and return
   * back to dpb */
  auto decoder_pic = gst_d3d12_decoder_picture_new (decoder, buffer,
      output_buffer, priv->session->decoder.Get (), priv->session->heap.Get ());
  GST_VIDEO_DECODER_STREAM_LOCK (videodec);
  if (!decoder_pic) {
    GST_ERROR_OBJECT (videodec, "Couldn't create new picture");
    return GST_FLOW_ERROR;
  }

  gst_codec_picture_set_user_data (picture, decoder_pic,
      (GDestroyNotify) gst_mini_object_unref);

  return GST_FLOW_OK;
}

GstFlowReturn
gst_d3d12_decoder_new_picture_with_size (GstD3D12Decoder * decoder,
    GstVideoDecoder * videodec, GstCodecPicture * picture, guint width,
    guint height)
{
  g_return_val_if_fail (GST_IS_D3D12_DECODER (decoder), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), GST_FLOW_ERROR);
  g_return_val_if_fail (picture != nullptr, GST_FLOW_ERROR);

  auto priv = decoder->priv;
  if (!priv->session) {
    GST_ERROR_OBJECT (decoder, "No session configured");
    return GST_FLOW_ERROR;
  }

  if (priv->session->coded_width >= (gint) width &&
      priv->session->coded_height >= (gint) height) {
    return gst_d3d12_decoder_new_picture (decoder, videodec, picture);
  }

  /* FIXME: D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_ALLOW_RESOLUTION_CHANGE_ON_NON_KEY_FRAME
   * supported GPU can decode stream with mixed decoder heap */
  GST_ERROR_OBJECT (decoder,
      "Non-keyframe resolution change with larger size is not supported");

  return GST_FLOW_ERROR;
}

static inline GstD3D12DecoderPicture *
get_decoder_picture (GstCodecPicture * picture)
{
  return (GstD3D12DecoderPicture *) gst_codec_picture_get_user_data (picture);
}

GstFlowReturn
gst_d3d12_decoder_duplicate_picture (GstD3D12Decoder * decoder,
    GstCodecPicture * src, GstCodecPicture * dst)
{
  auto decoder_pic = get_decoder_picture (src);

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
  if (!picture)
    return 0xff;

  auto decoder_pic = get_decoder_picture (picture);
  if (!decoder_pic)
    return 0xff;

  return decoder_pic->view_id;
}

GstFlowReturn
gst_d3d12_decoder_start_picture (GstD3D12Decoder * decoder,
    GstCodecPicture * picture, guint8 * picture_id)
{
  auto decoder_pic = get_decoder_picture (picture);

  if (picture_id)
    *picture_id = 0xff;

  if (!decoder_pic)
    return GST_FLOW_ERROR;

  if (picture_id)
    *picture_id = decoder_pic->view_id;

  return GST_FLOW_OK;
}

GstFlowReturn
gst_d3d12_decoder_end_picture (GstD3D12Decoder * decoder,
    GstCodecPicture * picture, GPtrArray * ref_pics,
    const GstDxvaDecodingArgs * args)
{
  auto priv = decoder->priv;
  HRESULT hr;
  D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS out_args;
  D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS in_args;
  ID3D12Resource *resource;
  ID3D12Resource *out_resource = nullptr;
  GstD3D12Memory *dmem;
  UINT subresource[2];
  auto & pre_barriers = priv->pre_barriers;
  auto & post_barriers = priv->post_barriers;
  auto & configured_ref_pics = priv->configured_ref_pics;

  pre_barriers.clear ();
  post_barriers.clear ();
  configured_ref_pics.clear ();

  auto decoder_pic = get_decoder_picture (picture);
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

  if (!priv->output_thread) {
    GST_DEBUG_OBJECT (decoder, "Spawning output thread");
    priv->output_thread = g_thread_new ("GstD3D12DecoderLoop",
        (GThreadFunc) gst_d3d12_decoder_output_loop, decoder);
  }

  GstD3D12DecoderCpb *cpb;
  hr = gst_d3d12_decoder_cpb_pool_acquire (priv->session->cpb_pool,
      args->bitstream, args->bitstream_size, &cpb);
  if (!gst_d3d12_result (hr, decoder->device)) {
    GST_ERROR_OBJECT (decoder, "Couldn't upload bitstream");
    return GST_FLOW_ERROR;
  }

  memset (&in_args, 0, sizeof (D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS));
  memset (&out_args, 0, sizeof (D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS));

  GstD3D12DeviceDecoderLockGuard dlk (decoder->device);
  auto ca = gst_d3d12_decoder_cpb_get_command_allocator (cpb);
  hr = ca->Reset ();
  if (!gst_d3d12_result (hr, decoder->device)) {
    GST_ERROR_OBJECT (decoder, "Couldn't reset command allocator");
    gst_d3d12_decoder_cpb_unref (cpb);
    return GST_FLOW_ERROR;
  }

  if (!priv->cmd->cl) {
    hr = priv->cmd->device->CreateCommandList (0,
        D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE,
        ca, nullptr, IID_PPV_ARGS (&priv->cmd->cl));
  } else {
    hr = priv->cmd->cl->Reset (ca);
  }

  if (!gst_d3d12_result (hr, decoder->device)) {
    GST_ERROR_OBJECT (decoder, "Couldn't configure command list");
    gst_d3d12_decoder_cpb_unref (cpb);
    return GST_FLOW_ERROR;
  }

  for (guint i = 0; i < ref_pics->len; i++) {
    auto ref_pic = (GstCodecPicture *) g_ptr_array_index (ref_pics, i);
    auto ref_dec_pic = get_decoder_picture (ref_pic);

    if (!ref_dec_pic || ref_dec_pic == decoder_pic)
      continue;

    if (std::find (configured_ref_pics.begin (), configured_ref_pics.end (),
            ref_dec_pic) != configured_ref_pics.end ()) {
      continue;
    }

    configured_ref_pics.push_back (ref_dec_pic);

    dmem = (GstD3D12Memory *) gst_buffer_peek_memory (ref_dec_pic->buffer, 0);

    resource = gst_d3d12_memory_get_resource_handle (dmem);
    if (priv->session->array_of_textures) {
      pre_barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (resource,
              D3D12_RESOURCE_STATE_COMMON,
              D3D12_RESOURCE_STATE_VIDEO_DECODE_READ));
      post_barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (resource,
              D3D12_RESOURCE_STATE_VIDEO_DECODE_READ,
              D3D12_RESOURCE_STATE_COMMON));
    } else {
      gst_d3d12_memory_get_subresource_index (dmem, 0, &subresource[0]);
      gst_d3d12_memory_get_subresource_index (dmem, 1, &subresource[1]);

      pre_barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (resource,
              D3D12_RESOURCE_STATE_COMMON,
              D3D12_RESOURCE_STATE_VIDEO_DECODE_READ, subresource[0]));
      pre_barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (resource,
              D3D12_RESOURCE_STATE_COMMON,
              D3D12_RESOURCE_STATE_VIDEO_DECODE_READ, subresource[1]));
      post_barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (resource,
              D3D12_RESOURCE_STATE_VIDEO_DECODE_READ,
              D3D12_RESOURCE_STATE_COMMON, subresource[0]));
      post_barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (resource,
              D3D12_RESOURCE_STATE_VIDEO_DECODE_READ,
              D3D12_RESOURCE_STATE_COMMON, subresource[1]));
    }
  }

  if (decoder_pic->output_buffer) {
    dmem = (GstD3D12Memory *)
        gst_buffer_peek_memory (decoder_pic->output_buffer, 0);
    out_resource = gst_d3d12_memory_get_resource_handle (dmem);

    pre_barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (out_resource,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE));
    post_barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (out_resource,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
            D3D12_RESOURCE_STATE_COMMON));
  }

  dmem = (GstD3D12Memory *) gst_buffer_peek_memory (decoder_pic->buffer, 0);
  resource = gst_d3d12_memory_get_resource_handle (dmem);
  gst_d3d12_memory_get_subresource_index (GST_D3D12_MEMORY_CAST (dmem), 0,
      &subresource[0]);

  if (priv->session->array_of_textures) {
    pre_barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (resource,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE));
    post_barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (resource,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
            D3D12_RESOURCE_STATE_COMMON));
  } else {
    gst_d3d12_memory_get_subresource_index (GST_D3D12_MEMORY_CAST (dmem), 1,
        &subresource[1]);

    pre_barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (resource,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE, subresource[0]));
    pre_barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (resource,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE, subresource[1]));
    post_barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (resource,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
            D3D12_RESOURCE_STATE_COMMON, subresource[0]));
    post_barriers.push_back (CD3DX12_RESOURCE_BARRIER::Transition (resource,
            D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
            D3D12_RESOURCE_STATE_COMMON, subresource[1]));
  }

  priv->cmd->cl->ResourceBarrier (pre_barriers.size (), &pre_barriers[0]);

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

  gst_d3d12_decoder_cpb_get_bitstream (cpb, &in_args.CompressedBitstream);
  in_args.CompressedBitstream.Size = args->bitstream_size;
  in_args.pHeap = decoder_pic->heap.Get ();

  priv->session->dpb->Lock ();
  priv->session->dpb->GetReferenceFrames (in_args.ReferenceFrames);

  priv->cmd->cl->DecodeFrame (priv->session->decoder.Get (),
      &out_args, &in_args);

  if (!post_barriers.empty ())
    priv->cmd->cl->ResourceBarrier (post_barriers.size (), &post_barriers[0]);

  hr = priv->cmd->cl->Close ();
  priv->session->dpb->Unlock ();

  if (!gst_d3d12_result (hr, decoder->device)) {
    GST_ERROR_OBJECT (decoder, "Couldn't record decoding command");
    gst_d3d12_decoder_cpb_unref (cpb);
    return GST_FLOW_ERROR;
  }

  ID3D12CommandList *cl[] = { priv->cmd->cl.Get () };

  hr = gst_d3d12_cmd_queue_execute_command_lists (priv->cmd->queue,
      1, cl, &priv->cmd->fence_val);
  if (!gst_d3d12_result (hr, decoder->device)) {
    GST_ERROR_OBJECT (decoder, "Couldn't execute command list");
    gst_d3d12_decoder_cpb_unref (cpb);
    return GST_FLOW_ERROR;
  }

  decoder_pic->fence_val = priv->cmd->fence_val;

  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);
  gst_d3d12_fence_data_push (fence_data,
      FENCE_NOTIFY_MINI_OBJECT (gst_mini_object_ref (decoder_pic)));
  for (guint i = 0; i < ref_pics->len; i++) {
    auto ref_pic = (GstCodecPicture *) g_ptr_array_index (ref_pics, i);
    gst_d3d12_fence_data_push (fence_data,
        FENCE_NOTIFY_MINI_OBJECT (gst_codec_picture_ref (ref_pic)));
  }
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (cpb));

  gst_d3d12_cmd_queue_set_notify (priv->cmd->queue, priv->cmd->fence_val,
      fence_data, (GDestroyNotify) gst_d3d12_fence_data_unref);

  return GST_FLOW_OK;
}

static gboolean
gst_d3d12_decoder_ensure_staging_texture (GstD3D12Decoder * self)
{
  GstD3D12DecoderPrivate *priv = self->priv;

  if (priv->session->staging)
    return TRUE;

  ComPtr < ID3D12Resource > staging;
  HRESULT hr;
  UINT64 size;
  auto device = gst_d3d12_device_get_device_handle (self->device);
  D3D12_RESOURCE_DESC tex_desc =
      CD3DX12_RESOURCE_DESC::Tex2D (priv->session->decoder_format,
      priv->session->aligned_width, priv->session->aligned_height, 1, 1);

  device->GetCopyableFootprints (&tex_desc, 0, 2, 0,
      priv->session->layout, nullptr, nullptr, &size);

  D3D12_HEAP_PROPERTIES heap_prop = CD3DX12_HEAP_PROPERTIES
      (D3D12_HEAP_TYPE_READBACK);
  D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer (size);
  D3D12_HEAP_FLAGS heap_flags = D3D12_HEAP_FLAG_NONE;
  if (gst_d3d12_device_non_zeroed_supported (self->device))
    heap_flags = D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

  hr = device->CreateCommittedResource (&heap_prop, heap_flags,
      &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS (&staging));
  if (!gst_d3d12_result (hr, self->device))
    return FALSE;

  priv->session->staging = staging;

  return TRUE;
}

static gboolean
gst_d3d12_decoder_can_direct_render (GstD3D12Decoder * self,
    GstVideoDecoder * videodec, GstBuffer * buffer,
    gint display_width, gint display_height)
{
  auto priv = self->priv;

  if (priv->session->output_type != GST_D3D12_DECODER_OUTPUT_D3D12)
    return FALSE;

  if (display_width != GST_VIDEO_INFO_WIDTH (&priv->session->info) ||
      display_height != GST_VIDEO_INFO_HEIGHT (&priv->session->info)) {
    return FALSE;
  }

  /* We need to crop but downstream does not support crop, need to copy */
  if (priv->session->need_crop && !priv->session->use_crop_meta)
    return FALSE;

  return TRUE;
}

static GstFlowReturn
gst_d3d12_decoder_copy_output_12 (GstD3D12Decoder * self, GstBuffer * srcbuf,
    ID3D12Resource * src, D3D12_TEXTURE_COPY_LOCATION dst[2],
    D3D12_COMMAND_LIST_TYPE cmd_type, guint64 * fence_val)
{
  auto priv = self->priv;
  D3D12_BOX src_box[2];
  std::vector < GstD3D12CopyTextureRegionArgs > copy_args;

  for (guint i = 0; i < 2; i++) {
    GstD3D12CopyTextureRegionArgs args;
    memset (&args, 0, sizeof (args));

    args.src = CD3DX12_TEXTURE_COPY_LOCATION (src, i);
    args.dst = dst[i];

    /* FIXME: only 4:2:0 */
    if (i == 0) {
      src_box[i].left = GST_ROUND_UP_2 (priv->session->crop_x);
      src_box[i].top = GST_ROUND_UP_2 (priv->session->crop_y);
      src_box[i].right = GST_ROUND_UP_2 (priv->session->crop_x +
          priv->session->output_info.width);
      src_box[i].bottom =
          GST_ROUND_UP_2 (priv->session->crop_y +
          priv->session->output_info.height);
    } else {
      src_box[i].left = GST_ROUND_UP_2 (priv->session->crop_x) / 2;
      src_box[i].top = GST_ROUND_UP_2 (priv->session->crop_y) / 2;
      src_box[i].right =
          GST_ROUND_UP_2 (priv->session->crop_x +
          priv->session->output_info.width) / 2;
      src_box[i].bottom =
          GST_ROUND_UP_2 (priv->session->crop_y +
          priv->session->output_info.height) / 2;
    }

    src_box[i].front = 0;
    src_box[i].back = 1;

    args.src_box = &src_box[i];
    copy_args.push_back (args);
  }

  GstD3D12FenceData *fence_data = nullptr;
  if (cmd_type != D3D12_COMMAND_LIST_TYPE_COPY) {
    gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);
    gst_d3d12_fence_data_push (fence_data,
        FENCE_NOTIFY_MINI_OBJECT (gst_buffer_ref (srcbuf)));
  }

  guint64 copy_fence_val = 0;
  auto copy_ret = gst_d3d12_device_copy_texture_region (self->device,
      copy_args.size (), copy_args.data (), fence_data, 0, nullptr, nullptr,
      cmd_type, &copy_fence_val);
  if (!copy_ret) {
    GST_ERROR_OBJECT (self, "Couldn't copy decoded texture to output");
    gst_clear_d3d12_fence_data (&fence_data);
    return GST_FLOW_ERROR;
  }

  if (fence_val)
    *fence_val = copy_fence_val;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d12_decoder_copy_output (GstD3D12Decoder * self, GstBuffer * src_buf,
    GstBuffer * dst_buf)
{
  auto priv = self->priv;
  auto session = priv->session.get ();

  auto in_dmem = (GstD3D12Memory *) gst_buffer_peek_memory (src_buf, 0);
  auto out_mem = gst_buffer_peek_memory (dst_buf, 0);
  if (gst_is_d3d12_memory (out_mem)) {
    auto out_dmem = GST_D3D12_MEMORY_CAST (out_mem);
    if (gst_d3d12_device_is_equal (out_dmem->device, self->device)) {
      D3D12_TEXTURE_COPY_LOCATION dst_location[2];
      UINT out_subresource[2];
      guint64 copy_fence_val;

      auto in_resource = gst_d3d12_memory_get_resource_handle (in_dmem);
      auto out_resource = gst_d3d12_memory_get_resource_handle (out_dmem);
      gst_d3d12_memory_get_subresource_index (out_dmem, 0, &out_subresource[0]);
      gst_d3d12_memory_get_subresource_index (out_dmem, 1, &out_subresource[1]);

      dst_location[0] = CD3DX12_TEXTURE_COPY_LOCATION (out_resource,
          out_subresource[0]);
      dst_location[1] = CD3DX12_TEXTURE_COPY_LOCATION (out_resource,
          out_subresource[1]);

      auto ret = gst_d3d12_decoder_copy_output_12 (self, src_buf, in_resource,
          dst_location, D3D12_COMMAND_LIST_TYPE_DIRECT, &copy_fence_val);
      if (ret != GST_FLOW_OK)
        return ret;

      auto fence = gst_d3d12_device_get_fence_handle (self->device,
          D3D12_COMMAND_LIST_TYPE_DIRECT);
      gst_d3d12_memory_set_fence (out_dmem, fence, copy_fence_val, FALSE);

      GST_MINI_OBJECT_FLAG_SET (out_dmem,
          GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);
      GST_MINI_OBJECT_FLAG_UNSET (out_dmem,
          GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);

      GST_TRACE_OBJECT (self, "d3d12 copy done");

      return GST_FLOW_OK;
    }
  }
#ifdef HAVE_GST_D3D11
  else if (session->output_type == GST_D3D12_DECODER_OUTPUT_D3D11 &&
      self->d3d11_interop && gst_is_d3d11_memory (out_mem)) {
    auto out_dmem = GST_D3D11_MEMORY_CAST (out_mem);
    if (out_dmem->device == self->device11) {
      GstMapInfo out_map;
      auto device11 = gst_d3d11_device_get_device_handle (self->device11);
      auto in_texture11 =
          gst_d3d12_memory_get_d3d11_texture (in_dmem, device11);

      if (!in_texture11) {
        GST_ERROR_OBJECT (self,
            "Couldn't get d3d11 texture from decoded texture");
        return GST_FLOW_ERROR;
      }

      if (!gst_memory_map (out_mem, &out_map,
              (GstMapFlags) (GST_MAP_WRITE | GST_MAP_D3D11))) {
        GST_ERROR_OBJECT (self, "Couldn't map output d3d11 memory");
        return GST_FLOW_ERROR;
      }

      HRESULT hr;
      {
        GstD3D11DeviceLockGuard device11_lk (self->device11);
        D3D11_BOX src_box = { };
        src_box.left = GST_ROUND_UP_2 (session->crop_x);
        src_box.top = GST_ROUND_UP_2 (session->crop_y);
        src_box.right = GST_ROUND_UP_2 (session->crop_x +
            session->output_info.width);
        src_box.bottom = GST_ROUND_UP_2 (session->crop_y +
            session->output_info.height);
        src_box.front = 0;
        src_box.back = 1;

        auto out_texture11 = gst_d3d11_memory_get_resource_handle (out_dmem);
        auto out_subresource =
            gst_d3d11_memory_get_subresource_index (out_dmem);

        priv->cmd->context11_4->CopySubresourceRegion (out_texture11,
            out_subresource, 0, 0, 0, in_texture11, 0, &src_box);
        priv->cmd->fence_val_11++;
        hr = priv->cmd->context11_4->Signal (priv->cmd->fence11.Get (),
            priv->cmd->fence_val_11);
      }

      gst_memory_unmap (out_mem, &out_map);

      if (!gst_d3d11_result (hr, self->device11)) {
        GST_ERROR_OBJECT (self, "Signal failed");
        return GST_FLOW_ERROR;
      }

      /* Set d3d11 device signalled fence to d3d12 memory.
       * d3d12 buffer pool or allocator will wait for this fence
       * to keep resource alive if it's still being used by d3d11 */
      gst_d3d12_memory_set_fence (in_dmem, priv->cmd->fence12.Get (),
          priv->cmd->fence_val_11, FALSE);

      GST_TRACE_OBJECT (self, "Copy done via d3d11 interop");

      return GST_FLOW_OK;
    }
  }
#endif

  if (!gst_d3d12_decoder_ensure_staging_texture (self)) {
    GST_ERROR_OBJECT (self, "Couldn't allocate staging texture");
    return GST_FLOW_ERROR;
  }

  auto in_resource = gst_d3d12_memory_get_resource_handle (in_dmem);
  D3D12_TEXTURE_COPY_LOCATION dst_location[2];

  dst_location[0] = CD3DX12_TEXTURE_COPY_LOCATION (session->staging.Get (),
      session->layout[0]);
  dst_location[1] = CD3DX12_TEXTURE_COPY_LOCATION (session->staging.Get (),
      session->layout[1]);

  guint64 copy_fence_val;
  auto ret = gst_d3d12_decoder_copy_output_12 (self, src_buf, in_resource,
      dst_location, D3D12_COMMAND_LIST_TYPE_COPY, &copy_fence_val);
  if (ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Couldn't download to staging buffer");
    return ret;
  }

  auto hr = gst_d3d12_device_fence_wait (self->device,
      D3D12_COMMAND_LIST_TYPE_COPY, copy_fence_val);
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't wait for fence");
    return GST_FLOW_ERROR;
  }

  guint8 *map_data;
  GstVideoFrame vframe;
  hr = session->staging->Map (0, nullptr, (void **) &map_data);
  if (!gst_d3d12_result (hr, self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't map staging texture");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&vframe,
          &session->output_info, dst_buf, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Couldn't map output buffer");
    session->staging->Unmap (0, nullptr);
    return GST_FLOW_ERROR;
  }

  for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (&vframe); i++) {
    guint8 *src_data = map_data + session->layout[i].Offset;
    guint8 *dst_data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe, i);
    gint width = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, i) *
        GST_VIDEO_FRAME_COMP_PSTRIDE (&vframe, i);

    for (gint j = 0; j < GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, i); j++) {
      memcpy (dst_data, src_data, width);
      dst_data += GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, i);
      src_data += session->layout[i].Footprint.RowPitch;
    }
  }

  session->staging->Unmap (0, nullptr);
  gst_video_frame_unmap (&vframe);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d12_decoder_process_output (GstD3D12Decoder * self,
    GstVideoDecoder * videodec, GstVideoCodecFrame * frame,
    GstCodecPicture * picture, GstVideoBufferFlags buffer_flags,
    gint display_width, gint display_height)
{
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstBuffer *buffer;
  bool attach_crop_meta = false;

  auto priv = self->priv;

  auto decoder_pic = get_decoder_picture (picture);
  g_assert (decoder_pic);

  gboolean need_negotiate = FALSE;
  if (display_width != GST_VIDEO_INFO_WIDTH (&priv->session->output_info) ||
      display_height != GST_VIDEO_INFO_HEIGHT (&priv->session->output_info)) {
    GST_INFO_OBJECT (videodec, "Frame size changed, do renegotiate");

    gst_video_info_set_interlaced_format (&priv->session->output_info,
        GST_VIDEO_INFO_FORMAT (&priv->session->info),
        GST_VIDEO_INFO_INTERLACE_MODE (&priv->session->info),
        display_width, display_height);

    need_negotiate = TRUE;
  } else if (picture->discont_state) {
    g_clear_pointer (&priv->session->input_state, gst_video_codec_state_unref);
    priv->session->input_state =
        gst_video_codec_state_ref (picture->discont_state);
    need_negotiate = TRUE;
  } else if (gst_pad_check_reconfigure (GST_VIDEO_DECODER_SRC_PAD (videodec))) {
    need_negotiate = TRUE;
  }

  if (need_negotiate && !gst_video_decoder_negotiate (videodec)) {
    GST_ERROR_OBJECT (videodec, "Couldn't negotiate with downstream");
    gst_codec_picture_unref (picture);
    gst_video_decoder_release_frame (videodec, frame);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  buffer = decoder_pic->output_buffer ?
      decoder_pic->output_buffer : decoder_pic->buffer;

  priv->session->lock.lock ();
  if (gst_d3d12_decoder_can_direct_render (self, videodec,
          decoder_pic->buffer, display_width, display_height)) {
    GST_LOG_OBJECT (self, "Outputting without copy");

    auto mem = gst_buffer_peek_memory (buffer, 0);
    GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D12_MEMORY_TRANSFER_NEED_DOWNLOAD);
    GST_MINI_OBJECT_FLAG_UNSET (mem, GST_D3D12_MEMORY_TRANSFER_NEED_UPLOAD);

    if (priv->session->need_crop)
      attach_crop_meta = true;

    frame->output_buffer = gst_buffer_ref (buffer);
  } else {
    ret = gst_video_decoder_allocate_output_frame (videodec, frame);
    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (videodec, "Couldn't allocate output buffer");
      goto error;
    }

    ret = gst_d3d12_decoder_copy_output (self, buffer, frame->output_buffer);
    if (ret != GST_FLOW_OK)
      goto error;
  }

  priv->session->lock.unlock ();

  GST_BUFFER_FLAG_SET (frame->output_buffer, buffer_flags);
  gst_codec_picture_unref (picture);

  if (attach_crop_meta) {
    frame->output_buffer = gst_buffer_make_writable (frame->output_buffer);

    auto crop_meta = gst_buffer_add_video_crop_meta (frame->output_buffer);
    crop_meta->x = priv->session->crop_x;
    crop_meta->y = priv->session->crop_y;
    crop_meta->width = priv->session->info.width;
    crop_meta->height = priv->session->info.height;

    GST_TRACE_OBJECT (self, "Attatching crop meta");
  }

  return gst_video_decoder_finish_frame (videodec, frame);

error:
  priv->session->lock.unlock ();
  gst_codec_picture_unref (picture);
  gst_video_decoder_release_frame (videodec, frame);

  return ret;
}

static gpointer
gst_d3d12_decoder_output_loop (GstD3D12Decoder * self)
{
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Entering output thread");

  while (true) {
    DecoderOutputData output_data;
    {
      GST_LOG_OBJECT (self, "Waiting for output data");
      std::unique_lock < std::mutex > lk (priv->session->queue_lock);
      while (gst_vec_deque_is_empty (priv->session->output_queue))
        priv->session->queue_cond.wait (lk);

      output_data = *((DecoderOutputData *)
          gst_vec_deque_pop_head_struct (priv->session->output_queue));
    }

    if (!output_data.frame) {
      GST_DEBUG_OBJECT (self, "Got terminate data");
      break;
    }

    auto decoder_pic = get_decoder_picture (output_data.picture);
    g_assert (decoder_pic);

    gst_d3d12_cmd_queue_fence_wait (priv->cmd->queue, decoder_pic->fence_val);

    if (priv->flushing) {
      GST_DEBUG_OBJECT (self, "Drop framem, we are flushing");
      gst_codec_picture_unref (output_data.picture);
      gst_video_decoder_release_frame (output_data.decoder, output_data.frame);
    } else if (priv->last_flow == GST_FLOW_OK) {
      priv->last_flow = gst_d3d12_decoder_process_output (self,
          output_data.decoder, output_data.frame, output_data.picture,
          output_data.buffer_flags, output_data.width, output_data.height);

      if (priv->last_flow != GST_FLOW_FLUSHING &&
          priv->last_flow != GST_FLOW_OK) {
        GST_WARNING_OBJECT (self, "Last flow was %s",
            gst_flow_get_name (priv->last_flow));
      }
    } else {
      GST_DEBUG_OBJECT (self, "Dropping framem last flow return was %s",
          gst_flow_get_name (priv->last_flow));
      gst_codec_picture_unref (output_data.picture);
      gst_video_decoder_release_frame (output_data.decoder, output_data.frame);
    }
  }

  GST_DEBUG_OBJECT (self, "Leaving output thread");

  return nullptr;
}

GstFlowReturn
gst_d3d12_decoder_output_picture (GstD3D12Decoder * decoder,
    GstVideoDecoder * videodec, GstVideoCodecFrame * frame,
    GstCodecPicture * picture, GstVideoBufferFlags buffer_flags,
    gint display_width, gint display_height)
{
  g_return_val_if_fail (GST_IS_D3D12_DECODER (decoder), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), GST_FLOW_ERROR);
  g_return_val_if_fail (frame != nullptr, GST_FLOW_ERROR);
  g_return_val_if_fail (picture != nullptr, GST_FLOW_ERROR);

  auto priv = decoder->priv;

  GST_LOG_OBJECT (decoder, "Output picture");

  if (!priv->session) {
    GST_ERROR_OBJECT (decoder, "No session configured");
    gst_codec_picture_unref (picture);
    gst_video_decoder_release_frame (videodec, frame);
    return GST_FLOW_ERROR;
  }

  auto output_data = DecoderOutputData ();
  output_data.decoder = videodec;
  output_data.frame = frame;
  output_data.picture = picture;
  output_data.buffer_flags = buffer_flags;
  output_data.width = display_width;
  output_data.height = display_height;

  std::lock_guard < std::mutex > lk (priv->session->queue_lock);
  gst_vec_deque_push_tail_struct (priv->session->output_queue, &output_data);
  priv->session->queue_cond.notify_one ();

  return priv->last_flow;
}

gboolean
gst_d3d12_decoder_negotiate (GstD3D12Decoder * decoder,
    GstVideoDecoder * videodec)
{
  g_return_val_if_fail (GST_IS_D3D12_DECODER (decoder), FALSE);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), FALSE);

  auto priv = decoder->priv;
  GstStructure *s;
  const gchar *str;

  if (!priv->session) {
    GST_WARNING_OBJECT (decoder, "No configured session");
    return FALSE;
  }

  auto peer_caps =
      gst_pad_get_allowed_caps (GST_VIDEO_DECODER_SRC_PAD (videodec));
  GST_DEBUG_OBJECT (videodec, "Allowed caps %" GST_PTR_FORMAT, peer_caps);

  GstD3D12DecoderOutputType allowed_types = GST_D3D12_DECODER_OUTPUT_UNKNOWN;
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
        allowed_types |= GST_D3D12_DECODER_OUTPUT_D3D12;
      }
#ifdef HAVE_GST_D3D11
      if (gst_caps_features_contains (features,
              GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
        allowed_types |= GST_D3D12_DECODER_OUTPUT_D3D11;
      }
#endif
    }
  }
  gst_clear_caps (&peer_caps);

  GST_DEBUG_OBJECT (videodec, "Downstream feature support 0x%x",
      (guint) allowed_types);

  std::lock_guard < std::recursive_mutex > lk (priv->session->lock);
  auto input_state = priv->session->input_state;
  auto info = &priv->session->output_info;

  /* TODO: add support alternate interlace */
  auto state = gst_video_decoder_set_output_state (videodec,
      GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
      GST_VIDEO_INFO_HEIGHT (info), input_state);

  if (!state) {
    GST_ERROR_OBJECT (decoder, "Couldn't set output state");
    return FALSE;
  }

  if (GST_VIDEO_INFO_IS_INTERLACED (info)) {
    auto in_s = gst_caps_get_structure (input_state->caps, 0);
    if (!gst_structure_has_field (in_s, "interlace-mode")) {
      /* Use our parsed info if missed in upstream */
      GST_VIDEO_INFO_INTERLACE_MODE (&state->info) =
          GST_VIDEO_INFO_INTERLACE_MODE (info);
      GST_VIDEO_INFO_FIELD_ORDER (&state->info) =
          GST_VIDEO_INFO_FIELD_ORDER (info);
    }
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

  g_clear_pointer (&priv->session->output_state, gst_video_codec_state_unref);
  priv->session->output_state = state;

#ifdef HAVE_GST_D3D11
  /* Try d3d11 only if downstream does not support d3d12 but does d3d11,
   * otherwise decoder will create unnecessary d3d11 device */
  if ((allowed_types & GST_D3D12_DECODER_OUTPUT_D3D11) ==
      GST_D3D12_DECODER_OUTPUT_D3D11 &&
      (allowed_types & GST_D3D12_DECODER_OUTPUT_D3D12) !=
      GST_D3D12_DECODER_OUTPUT_D3D12) {
    auto elem = GST_ELEMENT (videodec);
    if (decoder->d3d11_interop) {
      if (gst_d3d11_ensure_element_data_for_adapter_luid (elem,
              decoder->adapter_luid, &decoder->device11)) {
        if (!priv->cmd->device11_5) {
          auto device11 =
              gst_d3d11_device_get_device_handle (decoder->device11);
          device11->QueryInterface (IID_PPV_ARGS (&priv->cmd->device11_5));
        }

        if (priv->cmd->device11_5 && !priv->cmd->context11_4) {
          ComPtr < ID3D11DeviceContext > context11;
          ComPtr < ID3D11DeviceContext4 > context11_4;
          priv->cmd->device11_5->GetImmediateContext (&context11);
          context11.As (&priv->cmd->context11_4);
        }

        if (priv->cmd->context11_4 && !priv->cmd->fence11) {
          priv->cmd->device11_5->CreateFence (0, D3D11_FENCE_FLAG_SHARED,
              IID_PPV_ARGS (&priv->cmd->fence11));
        }

        if (priv->cmd->fence11 && !priv->cmd->fence12) {
          HANDLE handle;
          auto hr = priv->cmd->fence11->CreateSharedHandle (nullptr,
              GENERIC_ALL, nullptr, &handle);
          if (SUCCEEDED (hr)) {
            priv->cmd->device->OpenSharedHandle (handle,
                IID_PPV_ARGS (&priv->cmd->fence12));
            CloseHandle (handle);
          }
        }

        if (!priv->cmd->fence12)
          allowed_types &= ~GST_D3D12_DECODER_OUTPUT_D3D11;
      } else {
        allowed_types &= ~GST_D3D12_DECODER_OUTPUT_D3D11;
      }
    } else if (!gst_d3d11_ensure_element_data (elem, -1, &decoder->device11)) {
      allowed_types &= ~GST_D3D12_DECODER_OUTPUT_D3D11;
    }
  }
#endif

  auto prev_output_type = priv->session->output_type;
  if (prev_output_type != GST_D3D12_DECODER_OUTPUT_UNKNOWN &&
      (prev_output_type & allowed_types) == prev_output_type) {
    priv->session->output_type = prev_output_type;
  } else {
    if ((allowed_types & GST_D3D12_DECODER_OUTPUT_D3D12) != 0)
      priv->session->output_type = GST_D3D12_DECODER_OUTPUT_D3D12;
    else if ((allowed_types & GST_D3D12_DECODER_OUTPUT_D3D11) != 0)
      priv->session->output_type = GST_D3D12_DECODER_OUTPUT_D3D11;
    else
      priv->session->output_type = GST_D3D12_DECODER_OUTPUT_SYSTEM;
  }

  switch (priv->session->output_type) {
    case GST_D3D12_DECODER_OUTPUT_D3D12:
      gst_caps_set_features (state->caps, 0,
          gst_caps_features_new_single_static_str
          (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY));
      break;
#ifdef HAVE_GST_D3D11
    case GST_D3D12_DECODER_OUTPUT_D3D11:
      gst_caps_set_features (state->caps, 0,
          gst_caps_features_new_single_static_str
          (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY));
      break;
#endif
    default:
      break;
  }

  GST_DEBUG_OBJECT (decoder, "Selected output type %d",
      priv->session->output_type);

  return TRUE;
}

gboolean
gst_d3d12_decoder_decide_allocation (GstD3D12Decoder * decoder,
    GstVideoDecoder * videodec, GstQuery * query)
{
  GstD3D12DecoderPrivate *priv = decoder->priv;
  GstCaps *outcaps;
  GstBufferPool *pool = nullptr;
  guint n, size, min = 0, max = 0;
  GstVideoInfo vinfo;
  GstStructure *config;

  g_return_val_if_fail (GST_IS_D3D12_DECODER (decoder), FALSE);
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (videodec), FALSE);
  g_return_val_if_fail (query != nullptr, FALSE);

  if (!priv->session) {
    GST_ERROR_OBJECT (videodec, "Should open decoder first");
    return FALSE;
  }

  gst_query_parse_allocation (query, &outcaps, nullptr);

  if (!outcaps) {
    GST_DEBUG_OBJECT (decoder, "No output caps");
    return FALSE;
  }

  std::lock_guard < std::recursive_mutex > lk (priv->session->lock);
  if (priv->session->output_type == GST_D3D12_DECODER_OUTPUT_D3D12) {
    priv->session->use_crop_meta =
        gst_query_find_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE,
        nullptr);
  } else {
    priv->session->use_crop_meta = false;
  }

  gst_video_info_from_caps (&vinfo, outcaps);
  n = gst_query_get_n_allocation_pools (query);
  if (n > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  if (pool) {
    if (priv->session->output_type == GST_D3D12_DECODER_OUTPUT_D3D12) {
      if (!GST_IS_D3D12_BUFFER_POOL (pool)) {
        GST_DEBUG_OBJECT (videodec,
            "Downstream pool is not d3d12, will create new one");
        gst_clear_object (&pool);
      } else {
        auto dpool = GST_D3D12_BUFFER_POOL (pool);
        if (!gst_d3d12_device_is_equal (dpool->device, decoder->device)) {
          GST_DEBUG_OBJECT (videodec, "Different device, will create new one");
          gst_clear_object (&pool);
        }
      }
    }
#ifdef HAVE_GST_D3D11
    if (priv->session->output_type == GST_D3D12_DECODER_OUTPUT_D3D11) {
      if (!GST_IS_D3D12_BUFFER_POOL (pool)) {
        GST_DEBUG_OBJECT (videodec,
            "Downstream pool is not d3d11, will create new one");
        gst_clear_object (&pool);
      } else if (decoder->d3d11_interop) {
        auto dpool = GST_D3D11_BUFFER_POOL (pool);
        if (dpool->device != decoder->device11) {
          GST_DEBUG_OBJECT (videodec, "Different device, will create new one");
          gst_clear_object (&pool);
        }
      }
    }
#endif
  }

  if (!pool) {
    switch (priv->session->output_type) {
      case GST_D3D12_DECODER_OUTPUT_D3D12:
        pool = gst_d3d12_buffer_pool_new (decoder->device);
        break;
#ifdef HAVE_GST_D3D11
      case GST_D3D12_DECODER_OUTPUT_D3D11:
        pool = gst_d3d11_buffer_pool_new (decoder->device11);
        break;
#endif
      default:
        pool = gst_video_buffer_pool_new ();
        break;
    }

    size = (guint) vinfo.size;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (priv->session->output_type == GST_D3D12_DECODER_OUTPUT_D3D12) {
    GstVideoAlignment align;
    gint width, height;

    gst_video_alignment_reset (&align);

    auto params = gst_buffer_pool_config_get_d3d12_allocation_params (config);
    if (!params) {
      params = gst_d3d12_allocation_params_new (decoder->device, &vinfo,
          GST_D3D12_ALLOCATION_FLAG_DEFAULT,
          D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
          D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS,
          D3D12_HEAP_FLAG_SHARED);
    } else {
      gst_d3d12_allocation_params_set_resource_flags (params,
          D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
          D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
    }

    width = GST_VIDEO_INFO_WIDTH (&vinfo);
    height = GST_VIDEO_INFO_HEIGHT (&vinfo);

    /* need alignment to copy decoder output texture to downstream texture */
    align.padding_right = GST_ROUND_UP_16 (width) - width;
    align.padding_bottom = GST_ROUND_UP_16 (height) - height;
    gst_d3d12_allocation_params_alignment (params, &align);
    gst_buffer_pool_config_set_d3d12_allocation_params (config, params);
    gst_d3d12_allocation_params_free (params);
  }
#ifdef HAVE_GST_D3D11
  else if (priv->session->output_type == GST_D3D12_DECODER_OUTPUT_D3D11) {
    auto params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
    if (!params) {
      params = gst_d3d11_allocation_params_new (decoder->device11, &vinfo,
          GST_D3D11_ALLOCATION_FLAG_DEFAULT,
          D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0);
    }

    gst_buffer_pool_config_set_d3d11_allocation_params (config, params);
    gst_d3d11_allocation_params_free (params);
  }
#endif

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

void
gst_d3d12_decoder_sink_event (GstD3D12Decoder * decoder, GstEvent * event)
{
}

void
gst_d3d12_decoder_set_context (GstD3D12Decoder * decoder, GstElement * element,
    GstContext * context)
{
  auto priv = decoder->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->context_lock);
  gst_d3d12_handle_set_context_for_adapter_luid (element,
      context, decoder->adapter_luid, &decoder->device);
#ifdef HAVE_GST_D3D11
  if (decoder->d3d11_interop) {
    gst_d3d11_handle_set_context_for_adapter_luid (element,
        context, decoder->adapter_luid, &decoder->device11);
  } else {
    /* Since we will do system copy, accept any device */
    gst_d3d11_handle_set_context (element, context, -1, &decoder->device11);
  }
#endif
}

gboolean
gst_d3d12_decoder_handle_query (GstD3D12Decoder * decoder, GstElement * element,
    GstQuery * query)
{
  if (GST_QUERY_TYPE (query) != GST_QUERY_CONTEXT)
    return FALSE;

  auto priv = decoder->priv;
  std::lock_guard < std::recursive_mutex > lk (priv->context_lock);
#ifdef HAVE_GST_D3D11
  if (gst_d3d11_handle_context_query (element, query, decoder->device11))
    return TRUE;
#endif

  return gst_d3d12_handle_context_query (element, query, decoder->device);
}

enum
{
  PROP_DECODER_ADAPTER_LUID = 1,
  PROP_DECODER_DEVICE_ID,
  PROP_DECODER_VENDOR_ID,
};

static void
gst_d3d12_decoder_get_profiles (const GUID & profile,
    std::vector < std::string > &list)
{
  if (profile == D3D12_VIDEO_DECODE_PROFILE_MPEG2 ||
      profile == D3D12_VIDEO_DECODE_PROFILE_MPEG1_AND_MPEG2) {
    list.push_back ("main");
    list.push_back ("simple");
  } else if (profile == D3D12_VIDEO_DECODE_PROFILE_H264) {
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
  } else if (profile == D3D12_VIDEO_DECODE_PROFILE_VP8) {
    /* skip profile field */
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
    ID3D12VideoDevice * video_device, GstDxvaCodec codec)
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

  D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILE_COUNT profile_cnt = { };
  hr = video_device->CheckFeatureSupport
      (D3D12_FEATURE_VIDEO_DECODE_PROFILE_COUNT, &profile_cnt,
      sizeof (profile_cnt));
  if (FAILED (hr) || profile_cnt.ProfileCount == 0) {
    GST_INFO_OBJECT (device, "device does not support decoding");
    return nullptr;
  }

  std::vector < GUID > profile_guids;
  profile_guids.resize (profile_cnt.ProfileCount);
  D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILES profiles_data = { };
  profiles_data.ProfileCount = profile_cnt.ProfileCount;
  profiles_data.pProfiles = profile_guids.data ();
  hr = video_device->CheckFeatureSupport (D3D12_FEATURE_VIDEO_DECODE_PROFILES,
      &profiles_data, sizeof (profiles_data));
  if (!gst_d3d12_result (hr, device))
    return nullptr;

  for (guint i = 0; i < G_N_ELEMENTS (format_list); i++) {
    if (format_list[i].codec != codec)
      continue;

    if (std::find (profile_guids.begin (), profile_guids.end (),
            format_list[i].decode_profile) == profile_guids.end ()) {
      continue;
    }

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
    case GST_DXVA_CODEC_MPEG2:
      sink_caps_string = "video/mpeg, "
          "mpegversion = (int) 2, systemstream = (boolean) false";
      break;
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
    case GST_DXVA_CODEC_VP8:
      sink_caps_string = "video/x-vp8";
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
  if (codec != GST_DXVA_CODEC_VP9 && codec != GST_DXVA_CODEC_VP8) {
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

  auto sink_caps = gst_caps_from_string (sink_caps_string.c_str ());
  auto raw_caps = gst_caps_from_string (src_caps_string.c_str ());
  auto src_caps = gst_caps_copy (raw_caps);
  gst_caps_set_features_simple (src_caps,
      gst_caps_features_new_single_static_str
      (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY));
#ifdef HAVE_GST_D3D11
  auto d3d11_caps = gst_caps_copy (raw_caps);
  gst_caps_set_features_simple (d3d11_caps,
      gst_caps_features_new_single_static_str
      (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY));
  gst_caps_append (src_caps, d3d11_caps);
#endif
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
