/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d12h265enc.h"
#include "gstd3d12encoder.h"
#include "gstd3d12dpbstorage.h"
#include "gstd3d12pluginutils.h"
#include <gst/base/gstqueuearray.h>
#include <gst/codecparsers/gsth265parser.h>
#include <gst/codecparsers/gsth265bitwriter.h>
#include <directx/d3dx12.h>
#include <wrl.h>
#include <string.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <array>
#include <string>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <memory>
#include <set>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_h265_enc_debug);
#define GST_CAT_DEFAULT gst_d3d12_h265_enc_debug

enum
{
  PROP_0,
  PROP_RATE_CONTROL_SUPPORT,
  PROP_SLICE_MODE_SUPPORT,
  PROP_AUD,
  PROP_GOP_SIZE,
  PROP_REF_FRAMES,
  PROP_FRAME_ANALYSIS,
  PROP_RATE_CONTROL,
  PROP_BITRATE,
  PROP_MAX_BITRATE,
  PROP_QVBR_QUALITY,
  PROP_QP_INIT,
  PROP_QP_MIN,
  PROP_QP_MAX,
  PROP_QP_I,
  PROP_QP_P,
  PROP_QP_B,
  PROP_SLICE_MODE,
  PROP_SLICE_PARTITION,
  PROP_CC_INSERT,
};

#define DEFAULT_AUD TRUE
#define DEFAULT_FRAME_ANALYSIS FALSE
#define DEFAULT_GOP_SIZE 60
#define DEFAULT_RATE_CONTROL D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR
#define DEFAULT_BITRATE 2000
#define DEFAULT_MAX_BITRATE 4000
#define DEFAULT_QVBR_QUALITY 23
#define DEFAULT_QP 0
#define DEFAULT_CQP 23
#define DEFAULT_REF_FRAMES 1
#define DEFAULT_SLICE_MODE D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME
#define DEFAULT_SLICE_PARTITION 0
#define DEFAULT_CC_INSERT GST_D3D12_ENCODER_SEI_INSERT

struct GstD3D12H265EncClassData
{
  gint64 luid;
  guint device_id;
  guint vendor_id;
  gchar *description;
  GstCaps *sink_caps;
  GstCaps *src_caps;
  guint rc_support;
  guint slice_mode_support;
  guint width_multiple;
  guint height_multiple;
  D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC config_support[2];
};

/* *INDENT-OFF* */
class GstD3D12H265EncGop
{
public:
  void Init (guint gop_length)
  {
    if (gop_length == 1)
      gop_struct_.PPicturePeriod = 0;
    else
      gop_struct_.PPicturePeriod = 1;

    /* 0 means infinite */
    if (gop_length == 0) {
      gop_struct_.GOPLength = 0;
      gop_struct_.log2_max_pic_order_cnt_lsb_minus4 = 12;
    } else {
      /* count bits */
      guint val = gop_length;
      guint num_bits = 0;
      while (val) {
        num_bits++;
        val >>= 1;
      }

      if (num_bits < 4)
        gop_struct_.log2_max_pic_order_cnt_lsb_minus4 = 0;
      else if (num_bits > 16)
        gop_struct_.log2_max_pic_order_cnt_lsb_minus4 = 12;
      else
        gop_struct_.log2_max_pic_order_cnt_lsb_minus4 = num_bits - 4;

      gop_struct_.GOPLength = gop_length;
    }

    MaxPicOrderCnt_ = 1 << (gop_struct_.log2_max_pic_order_cnt_lsb_minus4 + 4);
    gop_start_ = true;
    pic_order_cnt_ = 0;
    encode_order_ = 0;
  }

  D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_HEVC
  GetGopStruct ()
  {
    return gop_struct_;
  }

  void
  FillPicCtrl (D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC & pic_ctrl)
  {
    if (gop_start_) {
      pic_ctrl.FrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_IDR_FRAME;
      pic_ctrl.PictureOrderCountNumber = 0;
      pic_ctrl.TemporalLayerIndex = 0;
      gop_start_ = false;
    } else {
      pic_ctrl.FrameType = D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_P_FRAME;
      pic_ctrl.PictureOrderCountNumber = pic_order_cnt_;
      pic_ctrl.TemporalLayerIndex = 0;
    }

    /* And increase frame num */
    pic_order_cnt_ = (pic_order_cnt_ + 1) % MaxPicOrderCnt_;
    encode_order_++;
    if (gop_struct_.GOPLength != 0 && encode_order_ >= gop_struct_.GOPLength) {
      pic_order_cnt_ = 0;
      encode_order_ = 0;
      gop_start_ = true;
    }
  }

  void ForceKeyUnit ()
  {
    pic_order_cnt_ = 0;
    encode_order_ = 0;
    gop_start_ = true;
  }

private:
  D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_HEVC gop_struct_ = { };
  guint pic_order_cnt_ = 0;
  guint MaxPicOrderCnt_ = 16;
  guint64 encode_order_ = 0;
  bool gop_start_ = false;
};

class GstD3D12H265EncDpb
{
public:
  GstD3D12H265EncDpb (GstD3D12Device * device, DXGI_FORMAT format,
      UINT width, UINT height, UINT max_dpb_size, bool array_of_textures)
  {
    max_dpb_size_ = max_dpb_size;
    if (max_dpb_size_ > 0) {
      storage_ = gst_d3d12_dpb_storage_new (device, max_dpb_size + 1,
          array_of_textures, format, width, height,
          D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY |
          D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
    }
  }

  ~GstD3D12H265EncDpb ()
  {
    gst_clear_object (&storage_);
  }

  bool IsValid ()
  {
    if (max_dpb_size_ > 0 && !storage_)
      return false;

    return true;
  }

  bool StartFrame (bool is_reference,
      D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC * ctrl_data,
      D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE * recon_pic,
      D3D12_VIDEO_ENCODE_REFERENCE_FRAMES * ref_frames,
      UINT64 display_order)
  {
    ctrl_data_ = *ctrl_data;
    cur_display_order_ = display_order;
    cur_frame_is_ref_ = is_reference;

    recon_pic_.pReconstructedPicture = nullptr;
    recon_pic_.ReconstructedPictureSubresource = 0;

    if (max_dpb_size_ > 0 &&
      ctrl_data_.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_IDR_FRAME) {
      ref_pic_desc_.clear();
      ref_pic_display_order_.clear ();
      gst_d3d12_dpb_storage_clear_dpb (storage_);
    }

    if (is_reference) {
      g_assert (max_dpb_size_ > 0);
      if (!gst_d3d12_dpb_storage_acquire_frame (storage_, &recon_pic_))
        return false;
    }

    *recon_pic = recon_pic_;

    switch (ctrl_data_.FrameType) {
      case D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_P_FRAME:
      case D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_B_FRAME:
        g_assert (max_dpb_size_ > 0);
        gst_d3d12_dpb_storage_get_reference_frames (storage_,
            ref_frames);
        break;
      default:
        ref_frames->NumTexture2Ds = 0;
        ref_frames->ppTexture2Ds = nullptr;
        ref_frames->pSubresources = nullptr;
        break;
    }

    list0_.clear ();
    list1_.clear ();

    bool build_l0 = (ctrl_data_.FrameType ==
      D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_P_FRAME) ||
      (ctrl_data_.FrameType ==
      D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_B_FRAME);
    bool build_l1 = ctrl_data_.FrameType ==
        D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_B_FRAME;

    if (build_l0) {
      for (UINT i = 0; i < (UINT) ref_pic_display_order_.size (); i++) {
        if (ref_pic_display_order_[i] < display_order)
          list0_.push_back (i);
      }
    }

    if (build_l1) {
      for (UINT i = 0; i < (UINT) ref_pic_display_order_.size (); i++) {
        if (ref_pic_display_order_[i] > display_order)
          list1_.push_back (i);
      }
    }

    ctrl_data->List0ReferenceFramesCount = list0_.size ();
    ctrl_data->pList0ReferenceFrames =
        list0_.empty () ? nullptr : list0_.data ();

    ctrl_data->List1ReferenceFramesCount = list1_.size ();
    ctrl_data->pList1ReferenceFrames =
        list1_.empty () ? nullptr : list1_.data ();

    ctrl_data->ReferenceFramesReconPictureDescriptorsCount =
        ref_pic_desc_.size ();
    ctrl_data->pReferenceFramesReconPictureDescriptors =
        ref_pic_desc_.empty () ? nullptr : ref_pic_desc_.data ();

    return true;
  }

  void EndFrame ()
  {
    if (!cur_frame_is_ref_ || max_dpb_size_ == 0)
      return;

    if (gst_d3d12_dpb_storage_get_dpb_size (storage_) == max_dpb_size_) {
      gst_d3d12_dpb_storage_remove_oldest_frame (storage_);
      ref_pic_display_order_.pop_back ();
      ref_pic_desc_.pop_back ();
    }

    gst_d3d12_dpb_storage_add_frame (storage_, &recon_pic_);

    D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_HEVC desc = { };
    desc.ReconstructedPictureResourceIndex = 0;
    desc.IsRefUsedByCurrentPic = TRUE;
    desc.IsLongTermReference = FALSE;
    desc.PictureOrderCountNumber = ctrl_data_.PictureOrderCountNumber;
    desc.TemporalLayerIndex = 0;

    ref_pic_display_order_.insert (ref_pic_display_order_.begin (),
        cur_display_order_);
    ref_pic_desc_.insert (ref_pic_desc_.begin(), desc);
    for (UINT i = 1; i < ref_pic_desc_.size (); i++)
      ref_pic_desc_[i].ReconstructedPictureResourceIndex = i;
  }

private:
  std::vector<D3D12_VIDEO_ENCODER_REFERENCE_PICTURE_DESCRIPTOR_HEVC> ref_pic_desc_;
  std::vector<UINT64> ref_pic_display_order_;
  D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE recon_pic_ = { };
  D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC ctrl_data_ = { };
  std::vector<UINT> list0_;
  std::vector<UINT> list1_;
  UINT max_dpb_size_ = 0;
  UINT64 cur_display_order_ = 0;
  bool cur_frame_is_ref_ = false;
  GstD3D12DpbStorage *storage_ = nullptr;
};

struct GstD3D12H265VPS
{
  void Clear ()
  {
    memset (&vps, 0, sizeof (GstH265VPS));
    bytes.clear ();
  }

  GstH265VPS vps;
  std::vector<guint8> bytes;
};

struct GstD3D12H265SPS
{
  void Clear ()
  {
    memset (&sps, 0, sizeof (GstH265SPS));
    bytes.clear ();
  }

  GstH265SPS sps;
  std::vector<guint8> bytes;
};

struct GstD3D12H265PPS
{
  void Clear ()
  {
    memset (&pps, 0, sizeof (GstH265PPS));
    bytes.clear ();
  }

  GstH265PPS pps;
  std::vector<guint8> bytes;
};

struct GstD3D12H265EncPrivate
{
  GstD3D12H265EncPrivate ()
  {
    cc_sei = g_array_new (FALSE, FALSE, sizeof (GstH265SEIMessage));
    g_array_set_clear_func (cc_sei, (GDestroyNotify) gst_h265_sei_free);
  }

  ~GstD3D12H265EncPrivate ()
  {
    g_array_unref (cc_sei);
  }

  GstVideoInfo info;
  GstH265ProfileTierLevel ptl = { };
  GstD3D12H265VPS vps;
  GstD3D12H265SPS sps;
  std::vector<GstD3D12H265PPS> pps;
  GstD3D12H265EncGop gop;
  std::unique_ptr<GstD3D12H265EncDpb> dpb;
  guint last_pps_id = 0;
  guint64 display_order = 0;
  GArray *cc_sei;

  std::mutex prop_lock;

  GstD3D12EncoderConfig encoder_config = { };

  D3D12_VIDEO_ENCODER_PROFILE_HEVC profile_hevc =
      D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN;
  D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC config_hevc = { };
  D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC level_tier = { };
  D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES
      layout_slices = { };
  D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_HEVC gop_struct_hevc = { };
  D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC pic_control_hevc = { };

  D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE selected_rc_mode =
      D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_ABSOLUTE_QP_MAP;
  D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE selected_slice_mode =
      DEFAULT_SLICE_MODE;
  guint selected_ref_frames = 0;
  D3D12_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT_HEVC pic_ctrl_support = { };
  D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC config_support;

  /* properties */
  gboolean aud = DEFAULT_AUD;

  /* gop struct related */
  guint gop_size = DEFAULT_GOP_SIZE;
  guint ref_frames = DEFAULT_REF_FRAMES;
  gboolean gop_updated = FALSE;

  /* rate control config */
  D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE rc_mode = DEFAULT_RATE_CONTROL;
  gboolean frame_analysis = DEFAULT_FRAME_ANALYSIS;
  gboolean rc_flag_updated = FALSE;
  guint bitrate = DEFAULT_BITRATE;
  guint max_bitrate = DEFAULT_MAX_BITRATE;
  guint qvbr_quality = DEFAULT_QVBR_QUALITY;
  guint qp_init = DEFAULT_QP;
  guint qp_min = DEFAULT_QP;
  guint qp_max = DEFAULT_QP;
  guint qp_i = DEFAULT_CQP;
  guint qp_p = DEFAULT_CQP;
  guint qp_b = DEFAULT_CQP;
  gboolean rc_updated = FALSE;

  /* slice mode */
  D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE slice_mode =
      DEFAULT_SLICE_MODE;
  guint slice_partition = DEFAULT_SLICE_PARTITION;
  gboolean slice_updated = FALSE;

  GstD3D12EncoderSeiInsertMode cc_insert = DEFAULT_CC_INSERT;
};
/* *INDENT-ON* */

struct GstD3D12H265Enc
{
  GstD3D12Encoder parent;

  GstD3D12H265EncPrivate *priv;
};

struct GstD3D12H265EncClass
{
  GstD3D12EncoderClass parent_class;

  GstD3D12H265EncClassData *cdata;
};

static inline GstD3D12H265Enc *
GST_D3D12_H265_ENC (gpointer ptr)
{
  return (GstD3D12H265Enc *) ptr;
}

static inline GstD3D12H265EncClass *
GST_D3D12_H265_ENC_GET_CLASS (gpointer ptr)
{
  return G_TYPE_INSTANCE_GET_CLASS (ptr, G_TYPE_FROM_INSTANCE (ptr),
      GstD3D12H265EncClass);
}

static void gst_d3d12_h265_enc_finalize (GObject * object);
static void gst_d3d12_h265_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d12_h265_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_d3d12_h265_enc_start (GstVideoEncoder * encoder);
static gboolean gst_d3d12_h265_enc_stop (GstVideoEncoder * encoder);
static gboolean gst_d3d12_h265_enc_transform_meta (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame, GstMeta * meta);
static GstCaps *gst_d3d12_h264_enc_getcaps (GstVideoEncoder * encoder,
    GstCaps * filter);
static gboolean gst_d3d12_h265_enc_new_sequence (GstD3D12Encoder * encoder,
    ID3D12VideoDevice * video_device, GstVideoCodecState * state,
    GstD3D12EncoderConfig * config);
static gboolean gst_d3d12_h265_enc_start_frame (GstD3D12Encoder * encoder,
    ID3D12VideoDevice * video_device, GstVideoCodecFrame * frame,
    D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_DESC * seq_ctrl,
    D3D12_VIDEO_ENCODER_PICTURE_CONTROL_DESC * picture_ctrl,
    D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE * recon_pic,
    GstD3D12EncoderConfig * config, gboolean * need_new_session);
static gboolean gst_d3d12_h265_enc_end_frame (GstD3D12Encoder * encoder);
static guint64 gst_d3d12_h265_enc_get_bitstream_size (GstD3D12Encoder * encoder,
    const GstD3D12EncoderConfig * config);

static GstElementClass *parent_class = nullptr;

static void
gst_d3d12_h265_enc_class_init (GstD3D12H265EncClass * klass, gpointer data)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  auto d3d12enc_class = GST_D3D12_ENCODER_CLASS (klass);
  auto cdata = (GstD3D12H265EncClassData *) data;
  GstPadTemplate *pad_templ;
  auto read_only_params = (GParamFlags) (GST_PARAM_DOC_SHOW_DEFAULT |
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  auto rw_params = (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);
  klass->cdata = cdata;

  object_class->finalize = gst_d3d12_h265_enc_finalize;
  object_class->set_property = gst_d3d12_h265_enc_set_property;
  object_class->get_property = gst_d3d12_h265_enc_get_property;

  g_object_class_install_property (object_class, PROP_RATE_CONTROL_SUPPORT,
      g_param_spec_flags ("rate-control-support", "Rate Control Support",
          "Supported rate control modes",
          GST_TYPE_D3D12_ENCODER_RATE_CONTROL_SUPPORT, 0, read_only_params));

  g_object_class_install_property (object_class, PROP_SLICE_MODE_SUPPORT,
      g_param_spec_flags ("slice-mode-support", "Slice Mode Support",
          "Supported slice partition modes",
          GST_TYPE_D3D12_ENCODER_SUBREGION_LAYOUT_SUPPORT, 1,
          read_only_params));

  g_object_class_install_property (object_class, PROP_AUD,
      g_param_spec_boolean ("aud", "AUD", "Use AU delimiter", DEFAULT_AUD,
          rw_params));

  g_object_class_install_property (object_class, PROP_GOP_SIZE,
      g_param_spec_uint ("gop-size", "GOP Size", "Size of GOP (0 = infinite)",
          0, G_MAXUINT32, DEFAULT_GOP_SIZE, rw_params));

  g_object_class_install_property (object_class, PROP_REF_FRAMES,
      g_param_spec_uint ("ref-frames", "Ref frames",
          "Preferred number of reference frames. Actual number of reference "
          "frames can be limited depending on hardware (0 = unspecified)",
          0, 16, DEFAULT_REF_FRAMES, rw_params));

  g_object_class_install_property (object_class, PROP_FRAME_ANALYSIS,
      g_param_spec_boolean ("frame-analysis", "Frame Analysis",
          "Enable 2 pass encoding if supported by hardware",
          DEFAULT_FRAME_ANALYSIS, rw_params));

  g_object_class_install_property (object_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control", "Rate Control",
          "Rate Control Method", GST_TYPE_D3D12_ENCODER_RATE_CONTROL,
          DEFAULT_RATE_CONTROL, rw_params));

  g_object_class_install_property (object_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate",
          "Target bitrate in kbits/second. "
          "Used for \"cbr\", \"vbr\", and \"qvbr\" rate control",
          0, G_MAXUINT, DEFAULT_BITRATE, rw_params));

  g_object_class_install_property (object_class, PROP_MAX_BITRATE,
      g_param_spec_uint ("max-bitrate", "Max Bitrate",
          "Peak bitrate in kbits/second. "
          "Used for \"vbr\", and \"qvbr\" rate control",
          0, G_MAXUINT, DEFAULT_MAX_BITRATE, rw_params));

  g_object_class_install_property (object_class, PROP_QVBR_QUALITY,
      g_param_spec_uint ("qvbr-quality", "QVBR Quality",
          "Constant quality target value for \"qvbr\" rate control",
          0, 51, DEFAULT_QVBR_QUALITY, rw_params));

  g_object_class_install_property (object_class, PROP_QP_INIT,
      g_param_spec_uint ("qp-init", "QP Init",
          "Initial QP value. "
          "Used for \"cbr\", \"vbr\", and \"qvbr\" rate control",
          0, 51, DEFAULT_QP, rw_params));

  g_object_class_install_property (object_class, PROP_QP_MIN,
      g_param_spec_uint ("qp-min", "QP Min",
          "Minimum QP value for \"cbr\", \"vbr\", and \"qvbr\" rate control. "
          "To enable min/max QP setting, \"qp-max >= qp-min > 0\" "
          "condition should be satisfied", 0, 51, DEFAULT_QP, rw_params));

  g_object_class_install_property (object_class, PROP_QP_MAX,
      g_param_spec_uint ("qp-max", "QP Max",
          "Maximum QP value for \"cbr\", \"vbr\", and \"qvbr\" rate control. "
          "To enable min/max QP setting, \"qp-max >= qp-min > 0\" "
          "condition should be satisfied", 0, 51, DEFAULT_QP, rw_params));

  g_object_class_install_property (object_class, PROP_QP_I,
      g_param_spec_uint ("qp-i", "QP I",
          "Constant QP value for I frames. Used for \"cqp\" rate control",
          1, 51, DEFAULT_CQP, rw_params));

  g_object_class_install_property (object_class, PROP_QP_P,
      g_param_spec_uint ("qp-p", "QP P",
          "Constant QP value for P frames. Used for \"cqp\" rate control",
          1, 51, DEFAULT_CQP, rw_params));

  g_object_class_install_property (object_class, PROP_QP_I,
      g_param_spec_uint ("qp-b", "QP B",
          "Constant QP value for B frames. Used for \"cqp\" rate control",
          1, 51, DEFAULT_CQP, rw_params));

  g_object_class_install_property (object_class, PROP_SLICE_MODE,
      g_param_spec_enum ("slice-mode", "Slice Mode",
          "Slice partiton mode", GST_TYPE_D3D12_ENCODER_SUBREGION_LAYOUT,
          DEFAULT_SLICE_MODE, rw_params));

  g_object_class_install_property (object_class, PROP_SLICE_PARTITION,
      g_param_spec_uint ("slice-partition", "Slice partition",
          "Slice partition threshold interpreted depending on \"slice-mode\". "
          "If set zero, full frame encoding will be selected without "
          "partitioning regardless of requested \"slice-mode\"",
          0, G_MAXUINT, DEFAULT_SLICE_PARTITION, rw_params));

  g_object_class_install_property (object_class, PROP_CC_INSERT,
      g_param_spec_enum ("cc-insert", "Closed Caption Insert",
          "Closed Caption insert mode", GST_TYPE_D3D12_ENCODER_SEI_INSERT_MODE,
          DEFAULT_CC_INSERT, rw_params));

  std::string long_name = "Direct3D12 H.265 " + std::string (cdata->description)
      + " Encoder";
  gst_element_class_set_metadata (element_class, long_name.c_str (),
      "Codec/Encoder/Video/Hardware", "Direct3D12 H.265 Video Encoder",
      "Seungha Yang <seungha@centricular.com>");

  pad_templ = gst_pad_template_new ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS, cdata->sink_caps);
  gst_element_class_add_pad_template (element_class, pad_templ);

  pad_templ = gst_pad_template_new ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS, cdata->src_caps);
  gst_element_class_add_pad_template (element_class, pad_templ);

  encoder_class->start = GST_DEBUG_FUNCPTR (gst_d3d12_h265_enc_start);
  encoder_class->stop = GST_DEBUG_FUNCPTR (gst_d3d12_h265_enc_stop);
  encoder_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_d3d12_h265_enc_transform_meta);
  encoder_class->getcaps = GST_DEBUG_FUNCPTR (gst_d3d12_h264_enc_getcaps);

  d3d12enc_class->codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
  d3d12enc_class->adapter_luid = cdata->luid;
  d3d12enc_class->device_id = cdata->device_id;
  d3d12enc_class->vendor_id = cdata->vendor_id;
  d3d12enc_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_d3d12_h265_enc_new_sequence);
  d3d12enc_class->start_frame =
      GST_DEBUG_FUNCPTR (gst_d3d12_h265_enc_start_frame);
  d3d12enc_class->end_frame = GST_DEBUG_FUNCPTR (gst_d3d12_h265_enc_end_frame);
  d3d12enc_class->get_bitstream_size =
      GST_DEBUG_FUNCPTR (gst_d3d12_h265_enc_get_bitstream_size);
}

static void
gst_d3d12_h265_enc_init (GstD3D12H265Enc * self)
{
  self->priv = new GstD3D12H265EncPrivate ();
}

static void
gst_d3d12_h265_enc_finalize (GObject * object)
{
  auto self = GST_D3D12_H265_ENC (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_h265_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_H265_ENC (object);
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->prop_lock);
  switch (prop_id) {
    case PROP_AUD:
      priv->aud = g_value_get_boolean (value);
      break;
    case PROP_GOP_SIZE:
    {
      auto gop_size = g_value_get_uint (value);
      if (gop_size != priv->gop_size) {
        priv->gop_size = gop_size;
        priv->gop_updated = TRUE;
      }
      break;
    }
    case PROP_REF_FRAMES:
    {
      auto ref_frames = g_value_get_uint (value);
      if (ref_frames != priv->ref_frames) {
        priv->ref_frames = ref_frames;
        priv->gop_updated = TRUE;
      }
      break;
    }
    case PROP_FRAME_ANALYSIS:
    {
      auto frame_analysis = g_value_get_boolean (value);
      if (frame_analysis != priv->frame_analysis) {
        priv->frame_analysis = frame_analysis;
        priv->rc_updated = TRUE;
      }
      break;
    }
    case PROP_RATE_CONTROL:
    {
      auto mode = (D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE)
          g_value_get_enum (value);
      if (mode != priv->rc_mode) {
        priv->rc_mode = mode;
        priv->rc_updated = TRUE;
      }
      break;
    }
    case PROP_BITRATE:
    {
      auto bitrate = g_value_get_uint (value);
      if (bitrate == 0)
        bitrate = DEFAULT_BITRATE;

      if (bitrate != priv->bitrate) {
        priv->bitrate = bitrate;
        if (priv->selected_rc_mode != D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP)
          priv->rc_updated = TRUE;
      }
      break;
    }
    case PROP_MAX_BITRATE:
    {
      auto max_bitrate = g_value_get_uint (value);
      if (max_bitrate == 0)
        max_bitrate = DEFAULT_MAX_BITRATE;

      if (max_bitrate != priv->max_bitrate) {
        priv->max_bitrate = max_bitrate;
        switch (priv->selected_rc_mode) {
          case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
          case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
            priv->rc_updated = TRUE;
            break;
          default:
            break;
        }
      }
      break;
    }
    case PROP_QVBR_QUALITY:
    {
      auto qvbr_quality = g_value_get_uint (value);
      if (qvbr_quality != priv->qvbr_quality) {
        priv->qvbr_quality = qvbr_quality;
        if (priv->selected_rc_mode ==
            D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR) {
          priv->rc_updated = TRUE;
        }
      }
      break;
    }
    case PROP_QP_INIT:
    {
      auto qp_init = g_value_get_uint (value);
      if (qp_init != priv->qp_init) {
        priv->qp_init = qp_init;
        switch (priv->selected_rc_mode) {
          case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
          case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
          case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
            priv->rc_updated = TRUE;
            break;
          default:
            break;
        }
      }
      break;
    }
    case PROP_QP_MIN:
    {
      auto qp_min = g_value_get_uint (value);
      if (qp_min != priv->qp_min) {
        priv->qp_min = qp_min;
        switch (priv->selected_rc_mode) {
          case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
          case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
          case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
            priv->rc_updated = TRUE;
            break;
          default:
            break;
        }
      }
      break;
    }
    case PROP_QP_MAX:
    {
      auto qp_max = g_value_get_uint (value);
      if (qp_max != priv->qp_max) {
        priv->qp_max = qp_max;
        switch (priv->selected_rc_mode) {
          case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
          case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
          case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
            priv->rc_updated = TRUE;
            break;
          default:
            break;
        }
      }
      break;
    }
    case PROP_QP_I:
    {
      auto qp_i = g_value_get_uint (value);
      if (qp_i != priv->qp_i) {
        priv->qp_i = qp_i;
        if (priv->selected_rc_mode == D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP)
          priv->rc_updated = TRUE;
      }
      break;
    }
    case PROP_QP_P:
    {
      auto qp_p = g_value_get_uint (value);
      if (qp_p != priv->qp_p) {
        priv->qp_p = qp_p;
        if (priv->selected_rc_mode == D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP)
          priv->rc_updated = TRUE;
      }
      break;
    }
    case PROP_QP_B:
    {
      auto qp_b = g_value_get_uint (value);
      if (qp_b != priv->qp_b) {
        priv->qp_b = qp_b;
        if (priv->selected_rc_mode == D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP)
          priv->rc_updated = TRUE;
      }
      break;
    }
    case PROP_SLICE_MODE:
    {
      auto slice_mode = (D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE)
          g_value_get_enum (value);
      if (slice_mode != priv->slice_mode) {
        priv->slice_mode = slice_mode;
        if (priv->selected_slice_mode != slice_mode)
          priv->slice_updated = TRUE;
      }
      break;
    }
    case PROP_SLICE_PARTITION:
    {
      auto slice_partition = g_value_get_uint (value);
      if (slice_partition != priv->slice_partition) {
        priv->slice_partition = slice_partition;
        if (priv->selected_slice_mode !=
            D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME) {
          priv->slice_updated = TRUE;
        }
      }
      break;
    }
    case PROP_CC_INSERT:
      priv->cc_insert = (GstD3D12EncoderSeiInsertMode) g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_h265_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_H265_ENC (object);
  auto priv = self->priv;
  auto klass = GST_D3D12_H265_ENC_GET_CLASS (self);
  const auto cdata = klass->cdata;

  std::lock_guard < std::mutex > lk (priv->prop_lock);
  switch (prop_id) {
    case PROP_RATE_CONTROL_SUPPORT:
      g_value_set_flags (value, cdata->rc_support);
      break;
    case PROP_SLICE_MODE_SUPPORT:
      g_value_set_flags (value, cdata->slice_mode_support);
      break;
    case PROP_AUD:
      g_value_set_boolean (value, priv->aud);
      break;
    case PROP_GOP_SIZE:
      g_value_set_uint (value, priv->gop_size);
      break;
    case PROP_REF_FRAMES:
      g_value_set_uint (value, priv->ref_frames);
      break;
    case PROP_FRAME_ANALYSIS:
      g_value_set_boolean (value, priv->frame_analysis);
      break;
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, priv->rc_mode);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, priv->bitrate);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_uint (value, priv->max_bitrate);
      break;
    case PROP_QVBR_QUALITY:
      g_value_set_uint (value, priv->qvbr_quality);
      break;
    case PROP_QP_INIT:
      g_value_set_uint (value, priv->qp_init);
      break;
    case PROP_QP_MIN:
      g_value_set_uint (value, priv->qp_min);
      break;
    case PROP_QP_MAX:
      g_value_set_uint (value, priv->qp_max);
      break;
    case PROP_QP_I:
      g_value_set_uint (value, priv->qp_i);
      break;
    case PROP_QP_P:
      g_value_set_uint (value, priv->qp_p);
      break;
    case PROP_QP_B:
      g_value_set_uint (value, priv->qp_p);
      break;
    case PROP_SLICE_MODE:
      g_value_set_enum (value, priv->slice_mode);
      break;
    case PROP_SLICE_PARTITION:
      g_value_set_uint (value, priv->slice_partition);
      break;
    case PROP_CC_INSERT:
      g_value_set_enum (value, priv->cc_insert);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_d3d12_h265_enc_start (GstVideoEncoder * encoder)
{
  auto self = GST_D3D12_H265_ENC (encoder);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Start");

  priv->display_order = 0;

  return GST_VIDEO_ENCODER_CLASS (parent_class)->start (encoder);
}

static gboolean
gst_d3d12_h265_enc_stop (GstVideoEncoder * encoder)
{
  auto self = GST_D3D12_H265_ENC (encoder);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (self, "Stop");

  priv->dpb = nullptr;

  return GST_VIDEO_ENCODER_CLASS (parent_class)->stop (encoder);
}

static gboolean
gst_d3d12_h265_enc_transform_meta (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame, GstMeta * meta)
{
  auto self = GST_D3D12_H265_ENC (encoder);
  auto priv = self->priv;

  if (meta->info->api == GST_VIDEO_CAPTION_META_API_TYPE) {
    std::lock_guard < std::mutex > lk (priv->prop_lock);
    if (priv->cc_insert == GST_D3D12_ENCODER_SEI_INSERT_AND_DROP) {
      auto cc_meta = (GstVideoCaptionMeta *) meta;
      if (cc_meta->caption_type == GST_VIDEO_CAPTION_TYPE_CEA708_RAW)
        return FALSE;
    }
  }

  return GST_VIDEO_ENCODER_CLASS (parent_class)->transform_meta (encoder,
      frame, meta);
}

static GstCaps *
gst_d3d12_h264_enc_getcaps (GstVideoEncoder * encoder, GstCaps * filter)
{
  auto self = GST_D3D12_H265_ENC (encoder);

  auto allowed_caps = gst_pad_get_allowed_caps (encoder->srcpad);
  if (!allowed_caps || gst_caps_is_empty (allowed_caps) ||
      gst_caps_is_any (allowed_caps)) {
    gst_clear_caps (&allowed_caps);

    return gst_video_encoder_proxy_getcaps (encoder, nullptr, filter);
  }

  GST_DEBUG_OBJECT (self, "Allowed caps %" GST_PTR_FORMAT, allowed_caps);

  std::set < std::string > downstream_profiles;
  /* Check if downstream specified profile explicitly, then filter out
   * incompatible raw video format */
  for (guint i = 0; i < gst_caps_get_size (allowed_caps); i++) {
    auto s = gst_caps_get_structure (allowed_caps, i);
    auto profile_value = gst_structure_get_value (s, "profile");
    if (!profile_value)
      continue;

    if (GST_VALUE_HOLDS_LIST (profile_value)) {
      for (guint j = 0; j < gst_value_list_get_size (profile_value); j++) {
        const GValue *p = gst_value_list_get_value (profile_value, j);

        if (!G_VALUE_HOLDS_STRING (p))
          continue;

        auto profile = g_value_get_string (p);
        if (g_strcmp0 (profile, "main") == 0 ||
            g_strcmp0 (profile, "main-10") == 0) {
          downstream_profiles.insert (profile);
        }
      }
    } else if (G_VALUE_HOLDS_STRING (profile_value)) {
      auto profile = g_value_get_string (profile_value);
      if (g_strcmp0 (profile, "main") == 0 ||
          g_strcmp0 (profile, "main-10") == 0) {
        downstream_profiles.insert (profile);
      }
    }
  }

  GST_DEBUG_OBJECT (self, "Downstream specified %" G_GSIZE_FORMAT " profiles",
      downstream_profiles.size ());

  /* Caps returned by gst_pad_get_allowed_caps() should hold profile field
   * already */
  if (downstream_profiles.size () == 0) {
    GST_WARNING_OBJECT (self,
        "Allowed caps holds no profile field %" GST_PTR_FORMAT, allowed_caps);

    gst_clear_caps (&allowed_caps);

    return gst_video_encoder_proxy_getcaps (encoder, nullptr, filter);
  }

  gst_clear_caps (&allowed_caps);

  auto template_caps = gst_pad_get_pad_template_caps (encoder->sinkpad);
  template_caps = gst_caps_make_writable (template_caps);

  if (downstream_profiles.size () == 1) {
    std::string format;
    const auto & profile = *downstream_profiles.begin ();

    if (profile == "main") {
      format = "NV12";
    } else if (profile == "main-10") {
      format = "P010_10LE";
    } else {
      gst_clear_caps (&template_caps);
      g_assert_not_reached ();
      return nullptr;
    }

    gst_caps_set_simple (template_caps, "format", G_TYPE_STRING,
        format.c_str (), nullptr);
  } else {
    GValue formats = G_VALUE_INIT;

    g_value_init (&formats, GST_TYPE_LIST);

    auto iter = downstream_profiles.begin ();
    for (; iter != downstream_profiles.end (); iter++) {
      GValue val = G_VALUE_INIT;
      g_value_init (&val, G_TYPE_STRING);
      if (*iter == "main") {
        g_value_set_static_string (&val, "NV12");
      } else if (*iter == "main-10") {
        g_value_set_static_string (&val, "P010_10LE");
      } else {
        g_value_unset (&val);
        gst_clear_caps (&template_caps);
        g_assert_not_reached ();
        return nullptr;
      }

      gst_value_list_append_and_take_value (&formats, &val);
    }

    gst_caps_set_value (template_caps, "format", &formats);
    g_value_unset (&formats);
  }

  auto supported_caps = gst_video_encoder_proxy_getcaps (encoder,
      template_caps, filter);
  gst_caps_unref (template_caps);

  GST_DEBUG_OBJECT (self, "Returning %" GST_PTR_FORMAT, supported_caps);

  return supported_caps;
}

static void
gst_d3d12_h265_enc_build_profile_tier_level (GstD3D12H265Enc * self)
{
  auto priv = self->priv;
  auto & ptl = priv->ptl;
  /* *INDENT-OFF* */
  static const std::unordered_map<D3D12_VIDEO_ENCODER_LEVELS_HEVC, guint8>
      level_map = {
    {D3D12_VIDEO_ENCODER_LEVELS_HEVC_1, GST_H265_LEVEL_L1},
    {D3D12_VIDEO_ENCODER_LEVELS_HEVC_2, GST_H265_LEVEL_L2},
    {D3D12_VIDEO_ENCODER_LEVELS_HEVC_21, GST_H265_LEVEL_L2_1},
    {D3D12_VIDEO_ENCODER_LEVELS_HEVC_3, GST_H265_LEVEL_L3},
    {D3D12_VIDEO_ENCODER_LEVELS_HEVC_31, GST_H265_LEVEL_L3_1},
    {D3D12_VIDEO_ENCODER_LEVELS_HEVC_4, GST_H265_LEVEL_L4},
    {D3D12_VIDEO_ENCODER_LEVELS_HEVC_41, GST_H265_LEVEL_L4_1},
    {D3D12_VIDEO_ENCODER_LEVELS_HEVC_5, GST_H265_LEVEL_L5},
    {D3D12_VIDEO_ENCODER_LEVELS_HEVC_51, GST_H265_LEVEL_L5_1},
    {D3D12_VIDEO_ENCODER_LEVELS_HEVC_52, GST_H265_LEVEL_L5_2},
    {D3D12_VIDEO_ENCODER_LEVELS_HEVC_6, GST_H265_LEVEL_L6},
    {D3D12_VIDEO_ENCODER_LEVELS_HEVC_61, GST_H265_LEVEL_L6_1},
    {D3D12_VIDEO_ENCODER_LEVELS_HEVC_62, GST_H265_LEVEL_L6_2},
  };
  /* *INDENT-ON* */

  ptl = { };
  ptl.profile_space = 0;
  ptl.tier_flag = priv->level_tier.Tier;
  ptl.profile_idc = 1;
  if (priv->profile_hevc == D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN10)
    ptl.profile_idc = 2;

  ptl.profile_compatibility_flag[1] = 1;
  ptl.profile_compatibility_flag[2] = 1;
  ptl.progressive_source_flag = 1;
  ptl.interlaced_source_flag = 0;
  ptl.non_packed_constraint_flag = 0;
  ptl.frame_only_constraint_flag = 1;
  ptl.level_idc = level_map.at (priv->level_tier.Level);
}

static gboolean
gst_d3d12_h265_enc_build_vps (GstD3D12H265Enc * self)
{
  auto priv = self->priv;
  guint8 vps_buf[1024] = { 0, };

  priv->vps.Clear ();
  auto & vps = priv->vps.vps;

  vps.id = 0;
  vps.base_layer_internal_flag = 1;
  vps.base_layer_available_flag = 1;
  vps.max_layers_minus1 = 0;
  vps.max_sub_layers_minus1 = 0;
  vps.temporal_id_nesting_flag = 1;
  vps.profile_tier_level = priv->ptl;
  vps.sub_layer_ordering_info_present_flag = 0;
  vps.max_dec_pic_buffering_minus1[0] = priv->selected_ref_frames;
  /* TODO: increase if B frame is enabled */
  vps.max_num_reorder_pics[0] = 0;
  vps.max_latency_increase_plus1[0] = 0;
  vps.max_layer_id = 0;
  vps.num_layer_sets_minus1 = 0;
  /* We use VUI in SPS */
  vps.timing_info_present_flag = 0;
  vps.vps_extension = 0;

  guint nal_size = G_N_ELEMENTS (vps_buf);
  GstH265BitWriterResult write_ret =
      gst_h265_bit_writer_vps (&vps, TRUE, vps_buf, &nal_size);
  if (write_ret != GST_H265_BIT_WRITER_OK) {
    GST_ERROR_OBJECT (self, "Couldn't build SPS");
    return FALSE;
  }

  priv->vps.bytes.resize (G_N_ELEMENTS (vps_buf));
  guint written_size = priv->vps.bytes.size ();
  write_ret = gst_h265_bit_writer_convert_to_nal (4, FALSE, TRUE, FALSE,
      vps_buf, nal_size * 8, priv->vps.bytes.data (), &written_size);
  if (write_ret != GST_H265_BIT_WRITER_OK) {
    GST_ERROR_OBJECT (self, "Couldn't build SPS bytes");
    return FALSE;
  }
  priv->vps.bytes.resize (written_size);

  return TRUE;
}

static gboolean
gst_d3d12_h265_enc_build_sps (GstD3D12H265Enc * self, const GstVideoInfo * info,
    const D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC * resolution,
    guint num_ref)
{
  auto priv = self->priv;
  const auto & config_hevc = priv->config_hevc;
  guint8 sps_buf[4096] = { 0, };
  /* *INDENT-OFF* */
  static const std::array<std::pair<gint, gint>, 17> par_map {{
    {0, 0},
    {1, 1},
    {12, 11},
    {10, 11},
    {16, 11},
    {40, 33},
    {24, 11},
    {20, 11},
    {32, 11},
    {80, 33},
    {18, 11},
    {15, 11},
    {64, 33},
    {160, 99},
    {4, 3},
    {3, 2},
    {2, 1}
  }};
  /* *INDENT-ON* */

  priv->sps.Clear ();
  auto & sps = priv->sps.sps;
  auto vps = &priv->vps.vps;
  sps.id = 0;
  sps.vps_id = 0;
  sps.vps = vps;
  sps.max_sub_layers_minus1 = 0;
  sps.temporal_id_nesting_flag = 1;
  sps.profile_tier_level = vps->profile_tier_level;
  sps.chroma_format_idc = 1;
  sps.separate_colour_plane_flag = 0;
  sps.pic_width_in_luma_samples = resolution->Width;
  sps.pic_height_in_luma_samples = resolution->Height;
  sps.conformance_window_flag = 0;
  if (resolution->Width != (UINT) info->width ||
      resolution->Height != (UINT) info->height) {
    sps.conformance_window_flag = 1;
    sps.conf_win_left_offset = 0;
    sps.conf_win_right_offset = (resolution->Width - info->width) / 2;
    sps.conf_win_top_offset = 0;
    sps.conf_win_bottom_offset = (resolution->Height - info->height) / 2;
  }

  sps.bit_depth_luma_minus8 = GST_VIDEO_INFO_COMP_DEPTH (info, 0) - 8;
  sps.bit_depth_chroma_minus8 = sps.bit_depth_luma_minus8;
  auto gop = priv->gop.GetGopStruct ();
  sps.log2_max_pic_order_cnt_lsb_minus4 = gop.log2_max_pic_order_cnt_lsb_minus4;
  sps.sub_layer_ordering_info_present_flag = 0;
  sps.max_dec_pic_buffering_minus1[0] = vps->max_dec_pic_buffering_minus1[0];
  sps.max_num_reorder_pics[0] = vps->max_num_reorder_pics[0];
  sps.max_latency_increase_plus1[0] = vps->max_latency_increase_plus1[0];
  sps.log2_min_luma_coding_block_size_minus3 =
      (guint8) config_hevc.MinLumaCodingUnitSize;
  sps.log2_diff_max_min_luma_coding_block_size =
      (guint8) config_hevc.MaxLumaCodingUnitSize -
      (guint8) config_hevc.MinLumaCodingUnitSize;
  sps.log2_min_transform_block_size_minus2 =
      (guint8) config_hevc.MinLumaTransformUnitSize;
  sps.log2_diff_max_min_transform_block_size =
      (guint8) config_hevc.MaxLumaTransformUnitSize -
      (guint8) config_hevc.MinLumaTransformUnitSize;
  sps.max_transform_hierarchy_depth_inter =
      config_hevc.max_transform_hierarchy_depth_inter;
  sps.max_transform_hierarchy_depth_intra =
      config_hevc.max_transform_hierarchy_depth_intra;
  sps.scaling_list_enabled_flag = 0;
  if ((config_hevc.ConfigurationFlags &
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_USE_ASYMETRIC_MOTION_PARTITION)
      != 0) {
    sps.amp_enabled_flag = 1;
  } else {
    sps.amp_enabled_flag = 0;
  }

  if ((config_hevc.ConfigurationFlags &
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ENABLE_SAO_FILTER)
      != 0) {
    sps.sample_adaptive_offset_enabled_flag = 1;
  } else {
    sps.sample_adaptive_offset_enabled_flag = 0;
  }

  sps.pcm_enabled_flag = 0;
  sps.num_short_term_ref_pic_sets = 0;
  sps.long_term_ref_pics_present_flag = 0;
  sps.temporal_mvp_enabled_flag = 0;
  sps.strong_intra_smoothing_enabled_flag = 0;

  sps.vui_parameters_present_flag = 1;
  auto & vui = sps.vui_params;
  const auto colorimetry = &info->colorimetry;

  if (info->par_n > 0 && info->par_d > 0) {
    const auto it = std::find_if (par_map.begin (),
        par_map.end (),[&](const auto & par) {
          return par.first == info->par_n && par.second == info->par_d;
        }
    );

    if (it != par_map.end ()) {
      vui.aspect_ratio_info_present_flag = 1;
      vui.aspect_ratio_idc = (guint8) std::distance (par_map.begin (), it);
    } else if (info->par_n <= G_MAXUINT16 && info->par_d <= G_MAXUINT16) {
      vui.aspect_ratio_info_present_flag = 1;
      vui.aspect_ratio_idc = 0xff;
      vui.sar_width = info->par_n;
      vui.sar_height = info->par_d;
    }
  }

  vui.overscan_info_present_flag = 0;
  vui.video_signal_type_present_flag = 1;
  /* Unspecified */
  vui.video_format = 5;
  vui.video_full_range_flag =
      colorimetry->range == GST_VIDEO_COLOR_RANGE_0_255 ? 1 : 0;
  vui.colour_description_present_flag = 1;
  vui.colour_primaries =
      gst_video_color_primaries_to_iso (colorimetry->primaries);
  vui.transfer_characteristics =
      gst_video_transfer_function_to_iso (colorimetry->transfer);
  vui.matrix_coefficients = gst_video_color_matrix_to_iso (colorimetry->matrix);

  vui.chroma_loc_info_present_flag = 0;
  vui.neutral_chroma_indication_flag = 0;
  vui.field_seq_flag = 0;
  vui.frame_field_info_present_flag = 0;
  vui.default_display_window_flag = 0;
  if (info->fps_n > 0 && info->fps_d > 0) {
    vui.timing_info_present_flag = 1;
    vui.time_scale = info->fps_n;
    vui.num_units_in_tick = info->fps_d;
    vui.poc_proportional_to_timing_flag = 0;
    vui.hrd_parameters_present_flag = 0;
  }
  vui.bitstream_restriction_flag = 0;

  sps.sps_extension_flag = 0;

  guint nal_size = G_N_ELEMENTS (sps_buf);
  GstH265BitWriterResult write_ret =
      gst_h265_bit_writer_sps (&sps, TRUE, sps_buf, &nal_size);
  if (write_ret != GST_H265_BIT_WRITER_OK) {
    GST_ERROR_OBJECT (self, "Couldn't build SPS");
    return FALSE;
  }

  priv->sps.bytes.resize (G_N_ELEMENTS (sps_buf));
  guint written_size = priv->sps.bytes.size ();
  write_ret = gst_h265_bit_writer_convert_to_nal (4, FALSE, TRUE, FALSE,
      sps_buf, nal_size * 8, priv->sps.bytes.data (), &written_size);
  if (write_ret != GST_H265_BIT_WRITER_OK) {
    GST_ERROR_OBJECT (self, "Couldn't build SPS bytes");
    return FALSE;
  }
  priv->sps.bytes.resize (written_size);

  return TRUE;
}

static gboolean
gst_d3d12_h265_enc_build_pps (GstD3D12H265Enc * self, guint num_ref)
{
  auto priv = self->priv;
  const auto & config_hevc = priv->config_hevc;

  /* Driver does not seem to use num_ref_idx_active_override_flag.
   * Needs multiple PPS to signal ref pics */
  /* TODO: make more PPS for L1 ref pics */
  guint num_pps = MAX (1, num_ref);
  priv->pps.resize (num_pps);
  for (size_t i = 0; i < priv->pps.size (); i++) {
    guint8 pps_buf[1024] = { 0, };
    auto & d3d12_pps = priv->pps[i];
    d3d12_pps.Clear ();

    auto & pps = d3d12_pps.pps;

    pps.id = i;
    pps.sps_id = 0;
    pps.sps = &priv->sps.sps;
    pps.dependent_slice_segments_enabled_flag = 0;
    pps.output_flag_present_flag = 0;
    pps.num_extra_slice_header_bits = 0;
    pps.sign_data_hiding_enabled_flag = 0;
    pps.cabac_init_present_flag = 1;
    pps.num_ref_idx_l0_default_active_minus1 = i;
    /* FIXME: support B frame */
    pps.num_ref_idx_l1_default_active_minus1 = 0;
    pps.init_qp_minus26 = 0;
    if ((config_hevc.ConfigurationFlags &
            D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_USE_CONSTRAINED_INTRAPREDICTION)
        != 0) {
      pps.constrained_intra_pred_flag = 1;
    } else {
      pps.constrained_intra_pred_flag = 0;
    }

    if ((config_hevc.ConfigurationFlags &
            D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ENABLE_TRANSFORM_SKIPPING)
        != 0) {
      pps.transform_skip_enabled_flag = 1;
    } else {
      pps.transform_skip_enabled_flag = 0;
    }

    pps.cu_qp_delta_enabled_flag = 1;
    pps.diff_cu_qp_delta_depth = 0;
    pps.cb_qp_offset = 0;
    pps.cr_qp_offset = 0;
    pps.slice_chroma_qp_offsets_present_flag = 1;
    pps.weighted_pred_flag = 0;
    pps.weighted_bipred_flag = 0;
    pps.transquant_bypass_enabled_flag = 0;
    pps.tiles_enabled_flag = 0;
    pps.entropy_coding_sync_enabled_flag = 0;

    if ((config_hevc.ConfigurationFlags &
            D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_DISABLE_LOOP_FILTER_ACROSS_SLICES)
        != 0) {
      pps.loop_filter_across_slices_enabled_flag = 0;
    } else {
      pps.loop_filter_across_slices_enabled_flag = 1;
    }

    pps.deblocking_filter_control_present_flag = 1;
    pps.deblocking_filter_override_enabled_flag = 0;
    pps.deblocking_filter_disabled_flag = 0;
    pps.beta_offset_div2 = 0;
    pps.tc_offset_div2 = 0;

    pps.scaling_list_data_present_flag = 0;
    /* TODO: need modification if B frame is enabled ? */
    pps.lists_modification_present_flag = 0;
    pps.log2_parallel_merge_level_minus2 = 0;
    pps.slice_segment_header_extension_present_flag = 0;
    pps.pps_extension_flag = 0;

    guint nal_size = G_N_ELEMENTS (pps_buf);
    d3d12_pps.bytes.resize (nal_size);
    GstH265BitWriterResult write_ret =
        gst_h265_bit_writer_pps (&pps, TRUE, pps_buf, &nal_size);
    if (write_ret != GST_H265_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Couldn't build PPS");
      return FALSE;
    }

    guint written_size = d3d12_pps.bytes.size ();
    write_ret = gst_h265_bit_writer_convert_to_nal (4, FALSE, TRUE, FALSE,
        pps_buf, nal_size * 8, d3d12_pps.bytes.data (), &written_size);
    if (write_ret != GST_H265_BIT_WRITER_OK) {
      GST_ERROR_OBJECT (self, "Couldn't build PPS bytes");
      return FALSE;
    }

    d3d12_pps.bytes.resize (written_size);
  }

  return TRUE;
}

static guint
gst_d3d12_h265_enc_get_max_ref_frames (GstD3D12H265Enc * self)
{
  auto priv = self->priv;
  const auto & pic_ctrl_support = priv->pic_ctrl_support;

  guint max_ref_frames = MIN (pic_ctrl_support.MaxL0ReferencesForP,
      pic_ctrl_support.MaxDPBCapacity);
  guint ref_frames = priv->ref_frames;

  if (max_ref_frames == 0) {
    GST_INFO_OBJECT (self,
        "Hardware does not support inter prediction, forcing all-intra");
    ref_frames = 0;
  } else if (priv->gop_size == 1) {
    GST_INFO_OBJECT (self, "User requested all-intra coding");
    ref_frames = 0;
  } else {
    /* TODO: at least 2 ref frames if B frame is enabled */
    if (ref_frames != 0)
      ref_frames = MIN (ref_frames, max_ref_frames);
    else
      ref_frames = 1;
  }

  return ref_frames;
}

static gboolean
gst_d3d12_h265_enc_update_gop (GstD3D12H265Enc * self,
    ID3D12VideoDevice * video_device, GstD3D12EncoderConfig * config,
    D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAGS * seq_flags)
{
  auto priv = self->priv;

  if (seq_flags && !priv->gop_updated)
    return TRUE;

  auto ref_frames = gst_d3d12_h265_enc_get_max_ref_frames (self);
  auto gop_size = priv->gop_size;
  if (ref_frames == 0)
    gop_size = 1;

  priv->last_pps_id = 0;

  auto prev_gop_struct = priv->gop.GetGopStruct ();
  auto prev_ref_frames = priv->selected_ref_frames;

  priv->selected_ref_frames = ref_frames;
  priv->gop.Init (gop_size);
  priv->gop_struct_hevc = priv->gop.GetGopStruct ();

  if (seq_flags) {
    if (prev_ref_frames != ref_frames ||
        memcmp (&prev_gop_struct, &priv->gop_struct_hevc,
            sizeof (priv->gop_struct_hevc)) != 0) {
      GST_DEBUG_OBJECT (self, "Gop struct updated");
      *seq_flags |=
          D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_GOP_SEQUENCE_CHANGE;
    }
  }

  GST_DEBUG_OBJECT (self,
      "Configured GOP struct, GOPLength: %u, PPicturePeriod: %u, "
      "log2_max_pic_order_cnt_lsb_minus4: %d", priv->gop_struct_hevc.GOPLength,
      priv->gop_struct_hevc.PPicturePeriod,
      priv->gop_struct_hevc.log2_max_pic_order_cnt_lsb_minus4);

  priv->gop_updated = FALSE;

  return TRUE;
}

/* called with prop_lock taken */
static gboolean
gst_d3d12_h265_enc_update_rate_control (GstD3D12H265Enc * self,
    ID3D12VideoDevice * video_device, GstD3D12EncoderConfig * config,
    D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAGS * seq_flags)
{
  auto priv = self->priv;
  const D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE rc_modes[] = {
    D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR,
    D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR,
    D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR,
    D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP,
  };

  if (seq_flags && !priv->rc_updated)
    return TRUE;

  GstD3D12EncoderConfig prev_config = *config;

  config->rate_control.Flags = D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_NONE;
  UINT64 bitrate = priv->bitrate;
  if (bitrate == 0)
    bitrate = DEFAULT_BITRATE;

  UINT64 max_bitrate = priv->max_bitrate;
  if (max_bitrate < bitrate) {
    if (bitrate >= G_MAXUINT64 / 2)
      max_bitrate = bitrate;
    else
      max_bitrate = bitrate * 2;
  }

  /* Property uses kbps, and API uses bps */
  bitrate *= 1000;
  max_bitrate *= 1000;

  /* Fill every rate control struct and select later */
  config->cqp.ConstantQP_FullIntracodedFrame = priv->qp_i;
  config->cqp.ConstantQP_InterPredictedFrame_PrevRefOnly = priv->qp_p;
  config->cqp.ConstantQP_InterPredictedFrame_BiDirectionalRef = priv->qp_b;

  config->cbr.InitialQP = priv->qp_init;
  config->cbr.MinQP = priv->qp_min;
  config->cbr.MaxQP = priv->qp_max;
  config->cbr.TargetBitRate = bitrate;

  config->vbr.InitialQP = priv->qp_init;
  config->vbr.MinQP = priv->qp_min;
  config->vbr.MaxQP = priv->qp_max;
  config->vbr.TargetAvgBitRate = bitrate;
  config->vbr.PeakBitRate = max_bitrate;

  config->qvbr.InitialQP = priv->qp_init;
  config->qvbr.MinQP = priv->qp_min;
  config->qvbr.MaxQP = priv->qp_max;
  config->qvbr.TargetAvgBitRate = bitrate;
  config->qvbr.PeakBitRate = max_bitrate;
  config->qvbr.ConstantQualityTarget = priv->qvbr_quality;

  D3D12_FEATURE_DATA_VIDEO_ENCODER_RATE_CONTROL_MODE feature_data = { };
  feature_data.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
  feature_data.RateControlMode = priv->rc_mode;

  auto hr = video_device->CheckFeatureSupport
      (D3D12_FEATURE_VIDEO_ENCODER_RATE_CONTROL_MODE,
      &feature_data, sizeof (feature_data));
  if (SUCCEEDED (hr) && feature_data.IsSupported) {
    priv->selected_rc_mode = priv->rc_mode;
  } else {
    GST_INFO_OBJECT (self, "Requested rate control mode is not supported");

    for (guint i = 0; i < G_N_ELEMENTS (rc_modes); i++) {
      feature_data.RateControlMode = rc_modes[i];
      hr = video_device->CheckFeatureSupport
          (D3D12_FEATURE_VIDEO_ENCODER_RATE_CONTROL_MODE, &feature_data,
          sizeof (feature_data));
      if (SUCCEEDED (hr) && feature_data.IsSupported) {
        priv->selected_rc_mode = rc_modes[i];
        break;
      } else {
        feature_data.IsSupported = FALSE;
      }
    }

    if (!feature_data.IsSupported) {
      GST_ERROR_OBJECT (self, "Couldn't find support rate control mode");
      return FALSE;
    }
  }

  GST_INFO_OBJECT (self, "Requested rate control mode %d, selected %d",
      priv->rc_mode, priv->selected_rc_mode);

  config->rate_control.Mode = priv->selected_rc_mode;
  switch (priv->selected_rc_mode) {
    case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP:
      config->rate_control.ConfigParams.DataSize = sizeof (config->cqp);
      config->rate_control.ConfigParams.pConfiguration_CQP = &config->cqp;
      break;
    case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
      config->rate_control.ConfigParams.DataSize = sizeof (config->cbr);
      config->rate_control.ConfigParams.pConfiguration_CBR = &config->cbr;
      break;
    case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
      config->rate_control.ConfigParams.DataSize = sizeof (config->vbr);
      config->rate_control.ConfigParams.pConfiguration_VBR = &config->vbr;
      break;
    case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
      config->rate_control.ConfigParams.DataSize = sizeof (config->qvbr);
      config->rate_control.ConfigParams.pConfiguration_QVBR = &config->qvbr;
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  if (seq_flags) {
    if (prev_config.rate_control.Mode != config->rate_control.Mode) {
      GST_DEBUG_OBJECT (self, "Rate control mode changed %d -> %d",
          prev_config.rate_control.Mode, config->rate_control.Mode);
      *seq_flags |=
          D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_RATE_CONTROL_CHANGE;
    } else {
      void *prev, *cur;
      switch (config->rate_control.Mode) {
        case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP:
          prev = &prev_config.cqp;
          cur = &config->cqp;
          break;
        case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
          prev = &prev_config.cbr;
          cur = &config->cbr;
          break;
        case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
          prev = &prev_config.vbr;
          cur = &config->vbr;
          break;
        case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
          prev = &prev_config.qvbr;
          cur = &config->qvbr;
          break;
        default:
          g_assert_not_reached ();
          return FALSE;
      }

      if (memcmp (prev, cur, config->rate_control.ConfigParams.DataSize) != 0) {
        GST_DEBUG_OBJECT (self, "Rate control params updated");
        *seq_flags |=
            D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_RATE_CONTROL_CHANGE;
      }
    }
  }

  priv->rc_updated = FALSE;

  return TRUE;
}

static gboolean
gst_d3d12_h265_enc_update_slice (GstD3D12H265Enc * self,
    ID3D12VideoDevice * video_device, GstD3D12EncoderConfig * config,
    D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAGS * seq_flags,
    D3D12_VIDEO_ENCODER_SUPPORT_FLAGS * support_flags)
{
  auto priv = self->priv;

  if (seq_flags && !priv->slice_updated)
    return TRUE;

  auto encoder = GST_D3D12_ENCODER (self);
  auto prev_mode = priv->selected_slice_mode;
  auto prev_slice = priv->layout_slices;

  priv->selected_slice_mode =
      D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME;
  priv->layout_slices.NumberOfSlicesPerFrame = 1;
  config->max_subregions = 1;

  D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT support = { };
  D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOLUTION_SUPPORT_LIMITS limits = { };
  D3D12_VIDEO_ENCODER_PROFILE_HEVC suggested_profile = priv->profile_hevc;
  D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC suggested_level;

  support.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
  support.InputFormat = config->encoder_format;
  support.CodecConfiguration = config->codec_config;
  support.CodecGopSequence = config->gop_struct;
  support.RateControl = config->rate_control;
  /* TODO: add intra-refresh support */
  support.IntraRefresh = D3D12_VIDEO_ENCODER_INTRA_REFRESH_MODE_NONE;
  support.ResolutionsListCount = 1;
  support.pResolutionList = &config->resolution;
  support.MaxReferenceFramesInDPB = priv->selected_ref_frames;
  support.pResolutionDependentSupport = &limits;
  support.SuggestedProfile.DataSize = sizeof (suggested_profile);
  support.SuggestedProfile.pHEVCProfile = &suggested_profile;
  support.SuggestedLevel.DataSize = sizeof (suggested_level);
  support.SuggestedLevel.pHEVCLevelSetting = &suggested_level;

  HRESULT hr;
  if (priv->slice_mode !=
      D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME
      && priv->slice_partition > 0) {
    /* TODO: fallback to other mode if possible */
    D3D12_FEATURE_DATA_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE
        feature_layout = { };
    feature_layout.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
    feature_layout.Profile = config->profile_desc;
    feature_layout.Level = config->level;
    feature_layout.SubregionMode = priv->slice_mode;
    hr = video_device->CheckFeatureSupport
        (D3D12_FEATURE_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE,
        &feature_layout, sizeof (feature_layout));
    if (!gst_d3d12_result (hr, encoder->device) || !feature_layout.IsSupported) {
      GST_WARNING_OBJECT (self, "Requested slice mode is not supported");
    } else {
      support.SubregionFrameEncoding = priv->slice_mode;
      hr = video_device->CheckFeatureSupport
          (D3D12_FEATURE_VIDEO_ENCODER_SUPPORT, &support, sizeof (support));
      if (gst_d3d12_result (hr, encoder->device)
          && CHECK_SUPPORT_FLAG (support.SupportFlags, GENERAL_SUPPORT_OK)
          && support.ValidationFlags == D3D12_VIDEO_ENCODER_VALIDATION_FLAG_NONE
          && limits.MaxSubregionsNumber > 1
          && limits.SubregionBlockPixelsSize > 0) {
        switch (priv->slice_mode) {
          case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_BYTES_PER_SUBREGION:
            priv->selected_slice_mode =
                priv->slice_mode;
            priv->layout_slices.MaxBytesPerSlice = priv->slice_partition;
            /* Don't know how many slices would be generated */
            config->max_subregions = limits.MaxSubregionsNumber;
            *support_flags = support.SupportFlags;
            break;
          case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_SQUARE_UNITS_PER_SUBREGION_ROW_UNALIGNED:
          {
            auto total_mbs =
                ((config->resolution.Width + limits.SubregionBlockPixelsSize -
                    1) / limits.SubregionBlockPixelsSize) *
                ((config->resolution.Height + limits.SubregionBlockPixelsSize -
                    1) / limits.SubregionBlockPixelsSize);
            if (priv->slice_partition >= total_mbs) {
              GST_DEBUG_OBJECT (self,
                  "Requested MBs per slice exceeds total MBs per frame");
            } else {
              priv->selected_slice_mode = priv->slice_mode;

              auto min_mbs_per_slice = (guint) std::ceil ((float) total_mbs
                  / limits.MaxSubregionsNumber);

              if (min_mbs_per_slice > priv->slice_partition) {
                GST_WARNING_OBJECT (self, "Too small number of MBs per slice");
                priv->layout_slices.NumberOfCodingUnitsPerSlice =
                    min_mbs_per_slice;
                config->max_subregions = limits.MaxSubregionsNumber;
              } else {
                priv->layout_slices.NumberOfCodingUnitsPerSlice =
                    priv->slice_partition;
                config->max_subregions = std::ceil ((float) total_mbs
                    / priv->slice_partition);
              }

              *support_flags = support.SupportFlags;
            }
            break;
          }
          case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_ROWS_PER_SUBREGION:
          {
            auto total_rows = (config->resolution.Height +
                limits.SubregionBlockPixelsSize - 1) /
                limits.SubregionBlockPixelsSize;
            if (priv->slice_partition >= total_rows) {
              GST_DEBUG_OBJECT (self,
                  "Requested rows per slice exceeds total rows per frame");
            } else {
              priv->selected_slice_mode = priv->slice_mode;

              auto min_rows_per_slice = (guint) std::ceil ((float) total_rows
                  / limits.MaxSubregionsNumber);

              if (min_rows_per_slice > priv->slice_partition) {
                GST_WARNING_OBJECT (self, "Too small number of rows per slice");
                priv->layout_slices.NumberOfRowsPerSlice = min_rows_per_slice;
                config->max_subregions = limits.MaxSubregionsNumber;
              } else {
                priv->layout_slices.NumberOfRowsPerSlice =
                    priv->slice_partition;
                config->max_subregions = (guint) std::ceil ((float) total_rows
                    / priv->slice_partition);
              }

              *support_flags = support.SupportFlags;
            }
            break;
          }
          case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME:
          {
            if (priv->slice_partition > 1) {
              priv->selected_slice_mode = priv->slice_mode;

              if (priv->slice_partition > limits.MaxSubregionsNumber) {
                GST_WARNING_OBJECT (self, "Too many slices per frame");
                priv->layout_slices.NumberOfSlicesPerFrame =
                    limits.MaxSubregionsNumber;
                config->max_subregions = limits.MaxSubregionsNumber;
              } else {
                priv->layout_slices.NumberOfSlicesPerFrame =
                    priv->slice_partition;
                config->max_subregions = priv->slice_partition;
              }

              *support_flags = support.SupportFlags;
            }
            break;
          }
          default:
            break;
        }
      }
    }
  }

  if (seq_flags && (prev_mode != priv->selected_slice_mode ||
          prev_slice.NumberOfSlicesPerFrame !=
          priv->layout_slices.NumberOfSlicesPerFrame)) {
    GST_DEBUG_OBJECT (self, "Slice mode updated");
    *seq_flags |=
        D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_SUBREGION_LAYOUT_CHANGE;
  }

  priv->slice_updated = FALSE;

  return TRUE;
}

static gboolean
gst_d3d12_h265_enc_reconfigure (GstD3D12H265Enc * self,
    ID3D12VideoDevice * video_device, GstD3D12EncoderConfig * config,
    D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAGS * seq_flags)
{
  auto encoder = GST_D3D12_ENCODER (self);
  auto priv = self->priv;
  auto prev_config = *config;
  auto prev_level = priv->level_tier;

  if (!gst_d3d12_h265_enc_update_gop (self, video_device, config, seq_flags))
    return FALSE;

  if (!gst_d3d12_h265_enc_update_rate_control (self,
          video_device, config, seq_flags)) {
    return FALSE;
  }

  D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT support = { };
  D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOLUTION_SUPPORT_LIMITS limits = { };
  D3D12_VIDEO_ENCODER_PROFILE_HEVC suggested_profile = priv->profile_hevc;

  support.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
  support.InputFormat = config->encoder_format;
  support.CodecConfiguration = config->codec_config;
  support.CodecGopSequence = config->gop_struct;
  support.RateControl = config->rate_control;
  /* TODO: add intra-refresh support */
  support.IntraRefresh = D3D12_VIDEO_ENCODER_INTRA_REFRESH_MODE_NONE;
  support.SubregionFrameEncoding = priv->selected_slice_mode;
  support.ResolutionsListCount = 1;
  support.pResolutionList = &config->resolution;
  support.MaxReferenceFramesInDPB = priv->selected_ref_frames;
  support.pResolutionDependentSupport = &limits;
  support.SuggestedProfile.DataSize = sizeof (suggested_profile);
  support.SuggestedProfile.pHEVCProfile = &suggested_profile;
  support.SuggestedLevel = config->level;

  auto hr = video_device->CheckFeatureSupport
      (D3D12_FEATURE_VIDEO_ENCODER_SUPPORT, &support, sizeof (support));

  /* This is our minimum/simplest configuration
   * TODO: negotiate again depending on validation flags */
  if (!gst_d3d12_result (hr, encoder->device) ||
      !CHECK_SUPPORT_FLAG (support.SupportFlags, GENERAL_SUPPORT_OK) ||
      (support.ValidationFlags != D3D12_VIDEO_ENCODER_VALIDATION_FLAG_NONE)) {
    GST_ERROR_OBJECT (self, "Couldn't query encoder support, 0x%x, 0x%x, 0x%x",
        (guint) hr, support.SupportFlags, support.ValidationFlags);

    return FALSE;
  }

  if (limits.SubregionBlockPixelsSize == 0) {
    GST_ERROR_OBJECT (self, "Unknown subregion block pixel size");
    return FALSE;
  }

  /* Bitstream alignment is independent of PictureTargetResolution */
  auto coded_resolution = config->resolution;
  coded_resolution.Width = ((coded_resolution.Width +
          limits.SubregionBlockPixelsSize - 1) /
      limits.SubregionBlockPixelsSize) * limits.SubregionBlockPixelsSize;
  coded_resolution.Height = ((coded_resolution.Height +
          limits.SubregionBlockPixelsSize - 1) /
      limits.SubregionBlockPixelsSize) * limits.SubregionBlockPixelsSize;

  /* Update rate control flags based on support flags */
  if (priv->frame_analysis) {
    if (CHECK_SUPPORT_FLAG (support.SupportFlags,
            RATE_CONTROL_FRAME_ANALYSIS_AVAILABLE)) {
      GST_INFO_OBJECT (self, "Frame analysis is enabled as requested");
      config->rate_control.Flags |=
          D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_FRAME_ANALYSIS;
    } else {
      GST_INFO_OBJECT (self, "Frame analysis is not supported");
    }
  }

  if (priv->qp_init > 0) {
    switch (priv->selected_rc_mode) {
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
        if (CHECK_SUPPORT_FLAG (support.SupportFlags,
                RATE_CONTROL_INITIAL_QP_AVAILABLE)) {
          GST_INFO_OBJECT (self, "Initial QP %d is enabled as requested",
              priv->qp_init);
          config->rate_control.Flags |=
              D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_INITIAL_QP;
        } else {
          GST_INFO_OBJECT (self, "Initial QP is not supported");
        }
        break;
      default:
        break;
    }
  }

  if (priv->qp_max >= priv->qp_min && priv->qp_min > 0) {
    switch (priv->selected_rc_mode) {
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
        if (CHECK_SUPPORT_FLAG (support.SupportFlags,
                RATE_CONTROL_ADJUSTABLE_QP_RANGE_AVAILABLE)) {
          GST_INFO_OBJECT (self, "QP range [%d, %d] is enabled as requested",
              priv->qp_min, priv->qp_max);
          config->rate_control.Flags |=
              D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QP_RANGE;
        } else {
          GST_INFO_OBJECT (self, "QP range is not supported");
        }
        break;
      default:
        break;
    }
  }

  if (seq_flags) {
    if (prev_config.rate_control.Flags != config->rate_control.Flags) {
      GST_DEBUG_OBJECT (self, "Rate control flag updated");
      *seq_flags |=
          D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_RATE_CONTROL_CHANGE;
    }
  }

  if (!gst_d3d12_h265_enc_update_slice (self, video_device, config,
          seq_flags, &support.SupportFlags)) {
    return FALSE;
  }

  config->support_flags = support.SupportFlags;

  bool gop_updated = !seq_flags ||
      (*seq_flags &
      D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_GOP_SEQUENCE_CHANGE) != 0;

  if (gop_updated || priv->level_tier.Level != prev_level.Level ||
      priv->level_tier.Tier != prev_level.Tier) {
    priv->gop.ForceKeyUnit ();
    gst_d3d12_h265_enc_build_profile_tier_level (self);
    gst_d3d12_h265_enc_build_vps (self);
    gst_d3d12_h265_enc_build_sps (self, &priv->info, &coded_resolution,
        priv->selected_ref_frames);
    gst_d3d12_h265_enc_build_pps (self, priv->selected_ref_frames);
  }

  if (gop_updated) {
    bool array_of_textures = !CHECK_SUPPORT_FLAG (config->support_flags,
        RECONSTRUCTED_FRAMES_REQUIRE_TEXTURE_ARRAYS);
    auto dpb = std::make_unique < GstD3D12H265EncDpb > (encoder->device,
        config->encoder_format, config->resolution.Width,
        config->resolution.Height, priv->selected_ref_frames,
        array_of_textures);
    if (!dpb->IsValid ()) {
      GST_ERROR_OBJECT (self, "Couldn't create dpb");
      return FALSE;
    }

    GST_DEBUG_OBJECT (self, "New DPB configured");

    priv->dpb = nullptr;
    priv->dpb = std::move (dpb);
  }

  return TRUE;
}

static gboolean
gst_d3d12_h265_enc_new_sequence (GstD3D12Encoder * encoder,
    ID3D12VideoDevice * video_device, GstVideoCodecState * state,
    GstD3D12EncoderConfig * config)
{
  auto self = GST_D3D12_H265_ENC (encoder);
  auto klass = GST_D3D12_H265_ENC_GET_CLASS (self);
  auto cdata = klass->cdata;
  auto priv = self->priv;
  auto info = &state->info;

  std::lock_guard < std::mutex > lk (priv->prop_lock);

  priv->dpb = nullptr;
  priv->info = state->info;

  config->profile_desc.DataSize = sizeof (D3D12_VIDEO_ENCODER_PROFILE_HEVC);
  config->profile_desc.pHEVCProfile = &priv->profile_hevc;

  config->codec_config.DataSize =
      sizeof (D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC);
  config->codec_config.pHEVCConfig = &priv->config_hevc;

  config->level.DataSize =
      sizeof (D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC);
  config->level.pHEVCLevelSetting = &priv->level_tier;

  config->layout.DataSize =
      sizeof
      (D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES);
  config->layout.pSlicesPartition_HEVC = &priv->layout_slices;

  config->gop_struct.DataSize =
      sizeof (D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_HEVC);
  config->gop_struct.pHEVCGroupOfPictures = &priv->gop_struct_hevc;

  const gchar *profile_str = "main";
  priv->profile_hevc = D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN;
  priv->config_support = cdata->config_support[0];
  config->encoder_format = DXGI_FORMAT_NV12;
  if (GST_VIDEO_INFO_FORMAT (info) == GST_VIDEO_FORMAT_P010_10LE) {
    profile_str = "main-10";
    priv->profile_hevc = D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN10;
    priv->config_support = cdata->config_support[1];
    config->encoder_format = DXGI_FORMAT_P010;
  }

  auto caps = gst_caps_new_simple ("video/x-h265",
      "alignment", G_TYPE_STRING, "au", "profile", G_TYPE_STRING, profile_str,
      "stream-format", G_TYPE_STRING, "byte-stream", nullptr);
  auto output_state =
      gst_video_encoder_set_output_state (GST_VIDEO_ENCODER (self), caps,
      state);
  gst_video_codec_state_unref (output_state);

  priv->config_hevc.ConfigurationFlags =
      D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_NONE;
  priv->config_hevc.MinLumaCodingUnitSize =
      priv->config_support.MinLumaCodingUnitSize;
  priv->config_hevc.MaxLumaCodingUnitSize =
      priv->config_support.MaxLumaCodingUnitSize;
  priv->config_hevc.MinLumaTransformUnitSize =
      priv->config_support.MinLumaTransformUnitSize;
  priv->config_hevc.MaxLumaTransformUnitSize =
      priv->config_support.MaxLumaTransformUnitSize;
  priv->config_hevc.max_transform_hierarchy_depth_inter =
      priv->config_support.max_transform_hierarchy_depth_inter;
  priv->config_hevc.max_transform_hierarchy_depth_intra =
      priv->config_support.max_transform_hierarchy_depth_intra;

  if ((priv->config_support.SupportFlags &
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_ASYMETRIC_MOTION_PARTITION_REQUIRED)
      != 0) {
    priv->config_hevc.ConfigurationFlags |=
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_USE_ASYMETRIC_MOTION_PARTITION;
  }

  GST_DEBUG_OBJECT (self, "Codec config, "
      "MinCU: %d, MaxCU: %d, MinTU: %d, MaxTU: %d, "
      "max-transform-depth-inter: %d, max-transform-depth-intra: %d",
      priv->config_hevc.MinLumaCodingUnitSize,
      priv->config_hevc.MaxLumaCodingUnitSize,
      priv->config_hevc.MinLumaTransformUnitSize,
      priv->config_hevc.MaxLumaTransformUnitSize,
      priv->config_hevc.max_transform_hierarchy_depth_inter,
      priv->config_hevc.max_transform_hierarchy_depth_intra);

  D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT
      feature_pic_ctrl = { };
  feature_pic_ctrl.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
  feature_pic_ctrl.Profile.DataSize = sizeof (priv->profile_hevc);
  feature_pic_ctrl.Profile.pHEVCProfile = &priv->profile_hevc;
  feature_pic_ctrl.PictureSupport.DataSize = sizeof (priv->pic_ctrl_support);
  feature_pic_ctrl.PictureSupport.pHEVCSupport = &priv->pic_ctrl_support;
  auto hr = video_device->CheckFeatureSupport
      (D3D12_FEATURE_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT,
      &feature_pic_ctrl, sizeof (feature_pic_ctrl));
  if (!gst_d3d12_result (hr, encoder->device) || !feature_pic_ctrl.IsSupported) {
    GST_ERROR_OBJECT (self, "Couldn't query picture control support");
    return FALSE;
  }

  config->resolution.Width = ((info->width + cdata->width_multiple - 1) /
      cdata->width_multiple) * cdata->width_multiple;
  config->resolution.Height = ((info->height + cdata->height_multiple - 1) /
      cdata->height_multiple) * cdata->height_multiple;

  if (info->fps_n > 0 && info->fps_d > 0) {
    config->rate_control.TargetFrameRate.Numerator = info->fps_n;
    config->rate_control.TargetFrameRate.Denominator = info->fps_d;
  } else {
    config->rate_control.TargetFrameRate.Numerator = 30;
    config->rate_control.TargetFrameRate.Denominator = 1;
  }

  return gst_d3d12_h265_enc_reconfigure (self, video_device, config, nullptr);
}

static gboolean
gst_d3d12_h265_enc_foreach_caption_meta (GstBuffer * buffer, GstMeta ** meta,
    GArray * cc_sei)
{
  if ((*meta)->info->api != GST_VIDEO_CAPTION_META_API_TYPE)
    return TRUE;

  auto cc_meta = (GstVideoCaptionMeta *) (*meta);
  if (cc_meta->caption_type != GST_VIDEO_CAPTION_TYPE_CEA708_RAW)
    return TRUE;

  GstH265SEIMessage sei = { };
  sei.payloadType = GST_H265_SEI_REGISTERED_USER_DATA;
  auto rud = &sei.payload.registered_user_data;

  rud->country_code = 181;
  rud->size = cc_meta->size + 10;

  auto data = (guint8 *) g_malloc (rud->size);
  data[0] = 0;                  /* 16-bits itu_t_t35_provider_code */
  data[1] = 49;
  data[2] = 'G';                /* 32-bits ATSC_user_identifier */
  data[3] = 'A';
  data[4] = '9';
  data[5] = '4';
  data[6] = 3;                  /* 8-bits ATSC1_data_user_data_type_code */
  /* 8-bits:
   * 1 bit process_em_data_flag (0)
   * 1 bit process_cc_data_flag (1)
   * 1 bit additional_data_flag (0)
   * 5-bits cc_count
   */
  data[7] = ((cc_meta->size / 3) & 0x1f) | 0x40;
  data[8] = 255;                /* 8 bits em_data, unused */
  memcpy (data + 9, cc_meta->data, cc_meta->size);
  data[cc_meta->size + 9] = 255;        /* 8 marker bits */

  rud->data = data;

  g_array_append_val (cc_sei, sei);

  return TRUE;
}

static gboolean
gst_d3d12_h265_enc_start_frame (GstD3D12Encoder * encoder,
    ID3D12VideoDevice * video_device, GstVideoCodecFrame * frame,
    D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_DESC * seq_ctrl,
    D3D12_VIDEO_ENCODER_PICTURE_CONTROL_DESC * picture_ctrl,
    D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE * recon_pic,
    GstD3D12EncoderConfig * config, gboolean * need_new_session)
{
  auto self = GST_D3D12_H265_ENC (encoder);
  auto priv = self->priv;
  static guint8 aud_data[] = {
    0x00, 0x00, 0x00, 0x01, 0x46, 0x01, 0x50
  };

  *need_new_session = FALSE;

  std::lock_guard < std::mutex > lk (priv->prop_lock);
  seq_ctrl->Flags = D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_NONE;

  /* Reset GOP struct on force-keyunit */
  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
    GST_DEBUG_OBJECT (self, "Force keyframe requested");
    priv->gop.ForceKeyUnit ();
  }

  auto prev_level = priv->level_tier;
  if (!gst_d3d12_h265_enc_reconfigure (self, video_device, config,
          &seq_ctrl->Flags)) {
    GST_ERROR_OBJECT (self, "Reconfigure failed");
    return FALSE;
  }

  if (seq_ctrl->Flags != D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_NONE) {
    *need_new_session =
        gst_d3d12_encoder_check_needs_new_session (config->support_flags,
        seq_ctrl->Flags);
  }

  if (priv->level_tier.Level != prev_level.Level ||
      priv->level_tier.Tier != prev_level.Tier) {
    *need_new_session = TRUE;
  }

  if (*need_new_session) {
    seq_ctrl->Flags = D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_NONE;

    GST_DEBUG_OBJECT (self, "Needs new session, forcing IDR");
    priv->gop.ForceKeyUnit ();
  }

  priv->gop.FillPicCtrl (priv->pic_control_hevc);

  if (priv->pic_control_hevc.FrameType ==
      D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_IDR_FRAME) {
    GST_LOG_OBJECT (self, "Sync point at frame %" G_GUINT64_FORMAT,
        priv->display_order);
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
  }

  seq_ctrl->IntraRefreshConfig.Mode =
      D3D12_VIDEO_ENCODER_INTRA_REFRESH_MODE_NONE;
  seq_ctrl->IntraRefreshConfig.IntraRefreshDuration = 0;
  seq_ctrl->RateControl = config->rate_control;
  seq_ctrl->PictureTargetResolution = config->resolution;
  seq_ctrl->SelectedLayoutMode = priv->selected_slice_mode;
  seq_ctrl->FrameSubregionsLayoutData = config->layout;
  seq_ctrl->CodecGopSequence = config->gop_struct;

  picture_ctrl->IntraRefreshFrameIndex = 0;
  /* TODO: b frame can be non-reference picture */
  picture_ctrl->Flags = priv->selected_ref_frames > 0 ?
      D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAG_USED_AS_REFERENCE_PICTURE :
      D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAG_NONE;
  picture_ctrl->PictureControlCodecData.DataSize =
      sizeof (priv->pic_control_hevc);
  picture_ctrl->PictureControlCodecData.pHEVCPicData = &priv->pic_control_hevc;

  if (!priv->dpb->StartFrame (picture_ctrl->Flags ==
          D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAG_USED_AS_REFERENCE_PICTURE,
          &priv->pic_control_hevc, recon_pic, &picture_ctrl->ReferenceFrames,
          priv->display_order)) {
    GST_ERROR_OBJECT (self, "Start frame failed");
    return FALSE;
  }

  priv->display_order++;

  priv->pic_control_hevc.Flags =
      D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_NONE;
  /* FIXME: count L1 too */
  priv->pic_control_hevc.slice_pic_parameter_set_id =
      priv->pic_control_hevc.List0ReferenceFramesCount > 1 ?
      priv->pic_control_hevc.List0ReferenceFramesCount - 1 : 0;
  priv->pic_control_hevc.List0RefPicModificationsCount = 0;
  priv->pic_control_hevc.pList0RefPicModifications = nullptr;
  priv->pic_control_hevc.List1RefPicModificationsCount = 0;
  priv->pic_control_hevc.pList1RefPicModifications = nullptr;
  priv->pic_control_hevc.QPMapValuesCount = 0;
  priv->pic_control_hevc.pRateControlQPMap = nullptr;

  if (priv->pic_control_hevc.FrameType ==
      D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_IDR_FRAME) {
    auto buf_size = priv->vps.bytes.size () + priv->sps.bytes.size () +
        priv->pps[0].bytes.size ();
    if (priv->aud)
      buf_size += sizeof (aud_data);

    auto output_buf = gst_buffer_new_and_alloc (buf_size);
    GstMapInfo map_info;
    gst_buffer_map (output_buf, &map_info, GST_MAP_WRITE);
    auto data = (guint8 *) map_info.data;

    if (priv->aud) {
      memcpy (data, aud_data, sizeof (aud_data));
      data += sizeof (aud_data);
    }

    memcpy (data, priv->vps.bytes.data (), priv->vps.bytes.size ());
    data += priv->vps.bytes.size ();

    memcpy (data, priv->sps.bytes.data (), priv->sps.bytes.size ());
    data += priv->sps.bytes.size ();

    memcpy (data, priv->pps[0].bytes.data (), priv->pps[0].bytes.size ());
    gst_buffer_unmap (output_buf, &map_info);
    frame->output_buffer = output_buf;

    priv->last_pps_id = 0;
  } else if (priv->pic_control_hevc.slice_pic_parameter_set_id !=
      priv->last_pps_id) {
    const auto & cur_pps =
        priv->pps[priv->pic_control_hevc.slice_pic_parameter_set_id];
    auto buf_size = cur_pps.bytes.size ();

    if (priv->aud)
      buf_size += sizeof (aud_data);

    auto output_buf = gst_buffer_new_and_alloc (buf_size);
    GstMapInfo map_info;
    gst_buffer_map (output_buf, &map_info, GST_MAP_WRITE);
    auto data = (guint8 *) map_info.data;

    if (priv->aud) {
      memcpy (data, aud_data, sizeof (aud_data));
      data += sizeof (aud_data);
    }

    memcpy (data, cur_pps.bytes.data (), cur_pps.bytes.size ());
    gst_buffer_unmap (output_buf, &map_info);
    frame->output_buffer = output_buf;

    priv->last_pps_id = priv->pic_control_hevc.slice_pic_parameter_set_id;
  } else if (priv->aud) {
    auto buf_size = sizeof (aud_data);
    auto output_buf = gst_buffer_new_and_alloc (buf_size);
    GstMapInfo map_info;
    gst_buffer_map (output_buf, &map_info, GST_MAP_WRITE);
    memcpy (map_info.data, aud_data, sizeof (aud_data));
    gst_buffer_unmap (output_buf, &map_info);
    frame->output_buffer = output_buf;
  }

  if (priv->cc_insert != GST_D3D12_ENCODER_SEI_DISABLED) {
    g_array_set_size (priv->cc_sei, 0);
    gst_buffer_foreach_meta (frame->input_buffer,
        (GstBufferForeachMetaFunc) gst_d3d12_h265_enc_foreach_caption_meta,
        priv->cc_sei);
    if (priv->cc_sei->len > 0) {
      auto mem = gst_h265_create_sei_memory (0, 1, 4, priv->cc_sei);
      if (mem) {
        GST_TRACE_OBJECT (self, "Inserting CC SEI");

        if (!frame->output_buffer)
          frame->output_buffer = gst_buffer_new ();

        gst_buffer_append_memory (frame->output_buffer, mem);
      }
    }
  }

  return TRUE;
}

static gboolean
gst_d3d12_h265_enc_end_frame (GstD3D12Encoder * encoder)
{
  auto self = GST_D3D12_H265_ENC (encoder);
  auto priv = self->priv;

  priv->dpb->EndFrame ();

  return TRUE;
}

static guint64
gst_d3d12_h265_enc_get_bitstream_size (GstD3D12Encoder * encoder,
    const GstD3D12EncoderConfig * config)
{
  auto self = GST_D3D12_H265_ENC (encoder);
  const auto & sps = self->priv->sps.sps;
  guint min_cr;

  switch (sps.profile_tier_level.level_idc) {
    case GST_H265_LEVEL_L1:
    case GST_H265_LEVEL_L2:
    case GST_H265_LEVEL_L2_1:
    case GST_H265_LEVEL_L3:
    case GST_H265_LEVEL_L3_1:
      min_cr = 2;
      break;
    case GST_H265_LEVEL_L4:
    case GST_H265_LEVEL_L4_1:
      min_cr = 4;
      break;
    case GST_H265_LEVEL_L5:
    case GST_H265_LEVEL_L6_2:
      min_cr = 6;
      break;
    case GST_H265_LEVEL_L5_1:
    case GST_H265_LEVEL_L5_2:
    case GST_H265_LEVEL_L6:
    case GST_H265_LEVEL_L6_1:
    default:
      min_cr = 8;
      break;
  }

  guint64 samples = (guint64) sps.pic_width_in_luma_samples *
      sps.pic_height_in_luma_samples;
  guint64 size = gst_util_uint64_scale_ceil (samples,
      3 * (sps.bit_depth_luma_minus8 + 8), 8 * min_cr);

  /* See vah265enc, use similar upper bound for slice header size */
  const guint max_slice_header_size = 4352;
  size += (guint64) config->max_subregions * max_slice_header_size;

  /* Add 10% margin */
  return gst_util_uint64_scale_ceil (size, 110, 100);
}

void
gst_d3d12_h265_enc_register (GstPlugin * plugin, GstD3D12Device * device,
    ID3D12VideoDevice * video_device, guint rank)
{
  HRESULT hr;
  std::vector < std::string > profiles;

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_h265_enc_debug,
      "d3d12h265enc", 0, "d3d12h265enc");

  D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC feature_codec = { };
  feature_codec.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
  hr = video_device->CheckFeatureSupport (D3D12_FEATURE_VIDEO_ENCODER_CODEC,
      &feature_codec, sizeof (feature_codec));

  if (!gst_d3d12_result (hr, device) || !feature_codec.IsSupported) {
    GST_INFO_OBJECT (device, "Device does not support H.265 encoding");
    return;
  }

  D3D12_FEATURE_DATA_VIDEO_ENCODER_PROFILE_LEVEL feature_profile_level = { };
  D3D12_VIDEO_ENCODER_PROFILE_HEVC profile_hevc =
      D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN;
  D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC level_hevc_min;
  D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC level_hevc_max;

  feature_profile_level.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
  feature_profile_level.Profile.DataSize = sizeof (profile_hevc);
  feature_profile_level.Profile.pHEVCProfile = &profile_hevc;
  feature_profile_level.MinSupportedLevel.DataSize = sizeof (level_hevc_min);
  feature_profile_level.MinSupportedLevel.pHEVCLevelSetting = &level_hevc_min;
  feature_profile_level.MaxSupportedLevel.DataSize = sizeof (level_hevc_max);
  feature_profile_level.MaxSupportedLevel.pHEVCLevelSetting = &level_hevc_max;

  D3D12_FEATURE_DATA_VIDEO_ENCODER_INPUT_FORMAT feature_input_format = { };
  feature_input_format.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
  feature_input_format.Profile = feature_profile_level.Profile;

  hr = video_device->CheckFeatureSupport
      (D3D12_FEATURE_VIDEO_ENCODER_PROFILE_LEVEL, &feature_profile_level,
      sizeof (feature_profile_level));
  if (!gst_d3d12_result (hr, device) || !feature_profile_level.IsSupported) {
    GST_WARNING_OBJECT (device, "Main profile is not supported");
    return;
  }

  feature_input_format.Format = DXGI_FORMAT_NV12;
  hr = video_device->CheckFeatureSupport
      (D3D12_FEATURE_VIDEO_ENCODER_INPUT_FORMAT, &feature_input_format,
      sizeof (feature_input_format));
  if (!gst_d3d12_result (hr, device) || !feature_input_format.IsSupported) {
    GST_WARNING_OBJECT (device, "NV12 format is not supported");
    return;
  }

  static const D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC config_set[]
      = {
    {
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_NONE,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_8x8,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_64x64,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_4x4,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_32x32,
        4, 4},
    {
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_NONE,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_8x8,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_64x64,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_4x4,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_32x32,
        3, 3},
    {
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_NONE,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_8x8,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_32x32,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_4x4,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_32x32,
        3, 3},
    {
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_NONE,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_8x8,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_32x32,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_4x4,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_32x32,
        2, 2},
    {
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_NONE,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_8x8,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_32x32,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_4x4,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_32x32,
        1, 1},
    {
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_NONE,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_8x8,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE_32x32,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_4x4,
          D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE_32x32,
        0, 0}
  };

  D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC config_main = { };
  D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC config_main10 = { };
  D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT
      config_support = { };
  config_support.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
  config_support.Profile.DataSize = sizeof (profile_hevc);
  config_support.Profile.pHEVCProfile = &profile_hevc;
  config_support.CodecSupportLimits.DataSize = sizeof (config_main);

  for (guint i = 0; i < G_N_ELEMENTS (config_set); i++) {
    auto test_config = config_set[i];
    config_support.CodecSupportLimits.pHEVCSupport = &test_config;
    hr = video_device->CheckFeatureSupport
        (D3D12_FEATURE_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT,
        &config_support, sizeof (config_support));
    if (SUCCEEDED (hr) && config_support.IsSupported) {
      GST_INFO_OBJECT (device, "Supported config for main profile, "
          "MinCU: %d, MaxCU: %d, MinTU: %d, MaxTU: %d, max-transform-depth: %d",
          test_config.MinLumaCodingUnitSize, test_config.MaxLumaCodingUnitSize,
          test_config.MinLumaTransformUnitSize,
          test_config.MaxLumaTransformUnitSize,
          test_config.max_transform_hierarchy_depth_inter);
      config_main = test_config;
      break;
    }
  }

  if (!config_support.IsSupported) {
    GST_WARNING_OBJECT (device, "Couldn't find supported config");
    return;
  }

  profiles.push_back ("main");
  GST_INFO_OBJECT (device, "Main profile is supported, level [%d, %d]",
      level_hevc_min.Level, level_hevc_max.Level);

  D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC main10_level_hevc_min;
  D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC main10_level_hevc_max;
  profile_hevc = D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN10;
  feature_profile_level.MinSupportedLevel.pHEVCLevelSetting =
      &main10_level_hevc_min;
  feature_profile_level.MaxSupportedLevel.pHEVCLevelSetting =
      &main10_level_hevc_max;
  hr = video_device->CheckFeatureSupport
      (D3D12_FEATURE_VIDEO_ENCODER_PROFILE_LEVEL, &feature_profile_level,
      sizeof (feature_profile_level));
  if (SUCCEEDED (hr) && feature_profile_level.IsSupported) {
    feature_input_format.Format = DXGI_FORMAT_P010;
    hr = video_device->CheckFeatureSupport
        (D3D12_FEATURE_VIDEO_ENCODER_INPUT_FORMAT, &feature_input_format,
        sizeof (feature_input_format));
    if (SUCCEEDED (hr) && feature_input_format.IsSupported) {
      config_support.IsSupported = FALSE;
      for (guint i = 0; i < G_N_ELEMENTS (config_set); i++) {
        auto test_config = config_set[i];
        config_support.CodecSupportLimits.pHEVCSupport = &test_config;
        hr = video_device->CheckFeatureSupport
            (D3D12_FEATURE_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT,
            &config_support, sizeof (config_support));
        if (SUCCEEDED (hr) && config_support.IsSupported) {
          GST_INFO_OBJECT (device, "Supported config for main10 profile, "
              "MinCU: %d, MaxCU: %d, MinTU: %d, MaxTU: %d, "
              "max-transform-depth: %d", test_config.MinLumaCodingUnitSize,
              test_config.MaxLumaCodingUnitSize,
              test_config.MinLumaTransformUnitSize,
              test_config.MaxLumaTransformUnitSize,
              test_config.max_transform_hierarchy_depth_inter);
          config_main10 = test_config;
          break;
        }
      }

      if (config_support.IsSupported) {
        profiles.push_back ("main-10");
        GST_INFO_OBJECT (device, "Main10 profile is supported, level [%d, %d]",
            main10_level_hevc_min.Level, main10_level_hevc_max.Level);
      }
    }
  }

  if (profiles.empty ()) {
    GST_WARNING_OBJECT (device, "Couldn't find supported profile");
    return;
  }

  D3D12_FEATURE_DATA_VIDEO_ENCODER_OUTPUT_RESOLUTION_RATIOS_COUNT ratios_count
      = { };
  ratios_count.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
  hr = video_device->CheckFeatureSupport
      (D3D12_FEATURE_VIDEO_ENCODER_OUTPUT_RESOLUTION_RATIOS_COUNT,
      &ratios_count, sizeof (ratios_count));
  if (!gst_d3d12_result (hr, device)) {
    GST_WARNING_OBJECT (device,
        "Couldn't query output resolution ratios count");
    return;
  }

  std::vector < D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_RATIO_DESC > ratios;

  D3D12_FEATURE_DATA_VIDEO_ENCODER_OUTPUT_RESOLUTION feature_resolution = { };
  feature_resolution.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
  feature_resolution.ResolutionRatiosCount = ratios_count.ResolutionRatiosCount;
  if (ratios_count.ResolutionRatiosCount > 0) {
    ratios.resize (ratios_count.ResolutionRatiosCount);
    feature_resolution.pResolutionRatios = &ratios[0];
  }

  hr = video_device->CheckFeatureSupport
      (D3D12_FEATURE_VIDEO_ENCODER_OUTPUT_RESOLUTION, &feature_resolution,
      sizeof (feature_resolution));
  if (!gst_d3d12_result (hr, device) || !feature_resolution.IsSupported) {
    GST_WARNING_OBJECT (device, "Couldn't query output resolution");
    return;
  }

  GST_INFO_OBJECT (device,
      "Device supported resolution %ux%u - %ux%u, align requirement %u, %u",
      feature_resolution.MinResolutionSupported.Width,
      feature_resolution.MinResolutionSupported.Height,
      feature_resolution.MaxResolutionSupported.Width,
      feature_resolution.MaxResolutionSupported.Height,
      feature_resolution.ResolutionWidthMultipleRequirement,
      feature_resolution.ResolutionHeightMultipleRequirement);

  guint rc_support = 0;
  D3D12_FEATURE_DATA_VIDEO_ENCODER_RATE_CONTROL_MODE feature_rate_control = { };
  feature_rate_control.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
  feature_rate_control.RateControlMode =
      D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP;

  hr = video_device->CheckFeatureSupport
      (D3D12_FEATURE_VIDEO_ENCODER_RATE_CONTROL_MODE, &feature_rate_control,
      sizeof (feature_rate_control));
  if (SUCCEEDED (hr) && feature_rate_control.IsSupported) {
    GST_INFO_OBJECT (device, "CQP suported");
    rc_support |= (1 << D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP);
  }

  feature_rate_control.RateControlMode =
      D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR;
  hr = video_device->CheckFeatureSupport
      (D3D12_FEATURE_VIDEO_ENCODER_RATE_CONTROL_MODE, &feature_rate_control,
      sizeof (feature_rate_control));
  if (SUCCEEDED (hr) && feature_rate_control.IsSupported) {
    GST_INFO_OBJECT (device, "CBR suported");
    rc_support |= (1 << D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR);
  }

  feature_rate_control.RateControlMode =
      D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR;
  hr = video_device->CheckFeatureSupport
      (D3D12_FEATURE_VIDEO_ENCODER_RATE_CONTROL_MODE, &feature_rate_control,
      sizeof (feature_rate_control));
  if (SUCCEEDED (hr) && feature_rate_control.IsSupported) {
    GST_INFO_OBJECT (device, "VBR suported");
    rc_support |= (1 << D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR);
  }

  feature_rate_control.RateControlMode =
      D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR;
  hr = video_device->CheckFeatureSupport
      (D3D12_FEATURE_VIDEO_ENCODER_RATE_CONTROL_MODE, &feature_rate_control,
      sizeof (feature_rate_control));
  if (SUCCEEDED (hr) && feature_rate_control.IsSupported) {
    GST_INFO_OBJECT (device, "VBR suported");
    rc_support |= (1 << D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR);
  }

  if (!rc_support) {
    GST_WARNING_OBJECT (device, "Couldn't find supported rate control mode");
    return;
  }

  profile_hevc = D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN;
  D3D12_FEATURE_DATA_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE
      feature_layout = { };
  feature_layout.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
  feature_layout.Profile.DataSize = sizeof (profile_hevc);
  feature_layout.Profile.pHEVCProfile = &profile_hevc;
  feature_layout.Level.DataSize =
      sizeof (D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC);

  D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE layout_modes[] = {
    D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME,
    D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_BYTES_PER_SUBREGION,
    D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_SQUARE_UNITS_PER_SUBREGION_ROW_UNALIGNED,
    D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_ROWS_PER_SUBREGION,
    D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME,
  };

  guint slice_mode_support = 0;
  for (guint i = 0; i < G_N_ELEMENTS (layout_modes); i++) {
    feature_layout.SubregionMode = layout_modes[i];
    for (guint level = (guint) level_hevc_min.Level;
        level <= (guint) level_hevc_max.Level; level++) {
      D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC level_hevc;
      level_hevc.Level = (D3D12_VIDEO_ENCODER_LEVELS_HEVC) level;
      level_hevc.Tier = D3D12_VIDEO_ENCODER_TIER_HEVC_MAIN;
      feature_layout.Level.pHEVCLevelSetting = &level_hevc;
      hr = video_device->CheckFeatureSupport
          (D3D12_FEATURE_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE,
          &feature_layout, sizeof (feature_layout));
      if (SUCCEEDED (hr) && feature_layout.IsSupported) {
        slice_mode_support |= (1 << layout_modes[i]);
        break;
      }
    }
  }

  if (!slice_mode_support) {
    GST_WARNING_OBJECT (device, "No supported subregion layout");
    return;
  }

  if ((slice_mode_support & (1 <<
              D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME))
      == 0) {
    GST_WARNING_OBJECT (device, "Full frame encoding is not supported");
    return;
  }

  auto subregions =
      g_flags_to_string (GST_TYPE_D3D12_ENCODER_SUBREGION_LAYOUT_SUPPORT,
      slice_mode_support);
  GST_INFO_OBJECT (device, "Supported subregion modes: \"%s\"", subregions);
  g_free (subregions);

  D3D12_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT_HEVC picture_ctrl_hevc;
  D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT
      feature_pic_ctrl = { };

  feature_pic_ctrl.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
  feature_pic_ctrl.Profile.DataSize = sizeof (profile_hevc);
  feature_pic_ctrl.Profile.pHEVCProfile = &profile_hevc;
  feature_pic_ctrl.PictureSupport.DataSize = sizeof (picture_ctrl_hevc);
  feature_pic_ctrl.PictureSupport.pHEVCSupport = &picture_ctrl_hevc;

  hr = video_device->CheckFeatureSupport
      (D3D12_FEATURE_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT,
      &feature_pic_ctrl, sizeof (feature_pic_ctrl));
  if (!gst_d3d12_result (hr, device) || !feature_pic_ctrl.IsSupported) {
    GST_WARNING_OBJECT (device, "Couldn't query picture control support");
    return;
  }

  GST_INFO_OBJECT (device, "MaxL0ReferencesForP: %u, MaxL0ReferencesForB: %u, "
      "MaxL1ReferencesForB: %u, MaxLongTermReferences: %u, MaxDPBCapacity %u",
      picture_ctrl_hevc.MaxL0ReferencesForP,
      picture_ctrl_hevc.MaxL0ReferencesForB,
      picture_ctrl_hevc.MaxL1ReferencesForB,
      picture_ctrl_hevc.MaxLongTermReferences,
      picture_ctrl_hevc.MaxDPBCapacity);

  std::string resolution_str = "width = (int) [" +
      std::to_string (feature_resolution.MinResolutionSupported.Width) + ", " +
      std::to_string (feature_resolution.MaxResolutionSupported.Width) +
      "], height = (int) [" +
      std::to_string (feature_resolution.MinResolutionSupported.Height) + ", " +
      std::to_string (feature_resolution.MaxResolutionSupported.Height) + " ]";
  std::string format_str = "format = (string) ";
  if (profiles.size () == 1)
    format_str += "NV12, ";
  else
    format_str += " { NV12, P010_10LE }, ";
  std::string sink_caps_str = "video/x-raw, " + format_str +
      resolution_str + ", interlace-mode = (string) progressive";

  std::string src_caps_str = "video/x-h265, " + resolution_str +
      ", stream-format = (string) byte-stream, alignment = (string) au, ";
  if (profiles.size () == 1) {
    src_caps_str += "profile = (string) " + profiles[0];
  } else {
    src_caps_str += "profile = (string) { ";
    for (size_t i = 0; i < profiles.size (); i++) {
      if (i != 0)
        src_caps_str += ", ";
      src_caps_str += profiles[i];
    }
    src_caps_str += " }";
  }

  auto sysmem_caps = gst_caps_from_string (sink_caps_str.c_str ());
  auto sink_caps = gst_caps_copy (sysmem_caps);
  gst_caps_set_features_simple (sink_caps,
      gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY, nullptr));
  gst_caps_append (sink_caps, sysmem_caps);
  auto src_caps = gst_caps_from_string (src_caps_str.c_str ());

  GST_MINI_OBJECT_FLAG_SET (sink_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (src_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  GstD3D12H265EncClassData *cdata = new GstD3D12H265EncClassData ();
  g_object_get (device, "adapter-luid", &cdata->luid,
      "device-id", &cdata->device_id, "vendor-id", &cdata->vendor_id,
      "description", &cdata->description, nullptr);
  cdata->sink_caps = sink_caps;
  cdata->src_caps = src_caps;
  cdata->rc_support = rc_support;
  cdata->slice_mode_support = slice_mode_support;
  cdata->width_multiple =
      MAX (1, feature_resolution.ResolutionWidthMultipleRequirement);
  cdata->height_multiple =
      MAX (1, feature_resolution.ResolutionHeightMultipleRequirement);

  /* Ensure even numbers for subsampled formats */
  if (cdata->width_multiple % 2)
    cdata->width_multiple *= 2;
  if (cdata->height_multiple % 2)
    cdata->height_multiple *= 2;

  cdata->config_support[0] = config_main;
  cdata->config_support[1] = config_main10;

  GType type;
  gchar *type_name;
  gchar *feature_name;
  guint index = 0;
  GTypeInfo type_info = {
    sizeof (GstD3D12H265EncClass),
    nullptr,
    nullptr,
    (GClassInitFunc) gst_d3d12_h265_enc_class_init,
    nullptr,
    nullptr,
    sizeof (GstD3D12H265Enc),
    0,
    (GInstanceInitFunc) gst_d3d12_h265_enc_init,
  };

  type_info.class_data = cdata;

  type_name = g_strdup ("GstD3D12H265Enc");
  feature_name = g_strdup ("d3d12h265enc");
  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstD3D12H265Device%dEnc", index);
    feature_name = g_strdup_printf ("d3d12h265device%denc", index);
  }

  type = g_type_register_static (GST_TYPE_D3D12_ENCODER,
      type_name, &type_info, (GTypeFlags) 0);

  if (rank > 0 && index != 0)
    rank--;

  if (index != 0)
    gst_element_type_set_skip_documentation (type);

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
