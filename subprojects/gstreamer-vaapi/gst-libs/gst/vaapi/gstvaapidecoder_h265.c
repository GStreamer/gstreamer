/*
 *  gstvaapidecoder_h265.c - H.265 decoder
 *
 *  Copyright (C) 2015 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapidecoder_h265
 * @short_description: H.265 decoder
 */

#include "sysdeps.h"
#include <math.h>
#include <gst/base/gstadapter.h>
#include <gst/codecparsers/gsth265parser.h>
#include "gstvaapicompat.h"
#include "gstvaapidecoder_h265.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiutils_h265_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* Defined to 1 if strict ordering of DPB is needed. Only useful for debug */
#define USE_STRICT_DPB_ORDERING 0

typedef struct _GstVaapiDecoderH265Private GstVaapiDecoderH265Private;
typedef struct _GstVaapiDecoderH265Class GstVaapiDecoderH265Class;
typedef struct _GstVaapiFrameStore GstVaapiFrameStore;
typedef struct _GstVaapiFrameStoreClass GstVaapiFrameStoreClass;
typedef struct _GstVaapiParserInfoH265 GstVaapiParserInfoH265;
typedef struct _GstVaapiPictureH265 GstVaapiPictureH265;

static gboolean nal_is_slice (guint8 nal_type);

/* ------------------------------------------------------------------------- */
/* --- H.265 Parser Info                                                 --- */
/* ------------------------------------------------------------------------- */

/*
 * Extended decoder unit flags:
 *
 * @GST_VAAPI_DECODER_UNIT_AU_START: marks the start of an access unit.
 * @GST_VAAPI_DECODER_UNIT_AU_END: marks the end of an access unit.
 */
enum
{
  GST_VAAPI_DECODER_UNIT_FLAG_AU_START =
      (GST_VAAPI_DECODER_UNIT_FLAG_LAST << 0),
  GST_VAAPI_DECODER_UNIT_FLAG_AU_END = (GST_VAAPI_DECODER_UNIT_FLAG_LAST << 1),

  GST_VAAPI_DECODER_UNIT_FLAGS_AU = (GST_VAAPI_DECODER_UNIT_FLAG_AU_START |
      GST_VAAPI_DECODER_UNIT_FLAG_AU_END),
};

#define GST_VAAPI_PARSER_INFO_H265(obj) \
    ((GstVaapiParserInfoH265 *)(obj))

struct _GstVaapiParserInfoH265
{
  GstVaapiMiniObject parent_instance;
  GstH265NalUnit nalu;
  union
  {
    GstH265VPS vps;
    GstH265SPS sps;
    GstH265PPS pps;
    GArray *sei;
    GstH265SliceHdr slice_hdr;
  } data;
  guint state;
  guint flags;                  // Same as decoder unit flags (persistent)
};

static void
gst_vaapi_parser_info_h265_finalize (GstVaapiParserInfoH265 * pi)
{
  if (nal_is_slice (pi->nalu.type))
    gst_h265_slice_hdr_free (&pi->data.slice_hdr);
  else {
    switch (pi->nalu.type) {
      case GST_H265_NAL_VPS:
      case GST_H265_NAL_SPS:
      case GST_H265_NAL_PPS:
        break;
      case GST_H265_NAL_PREFIX_SEI:
      case GST_H265_NAL_SUFFIX_SEI:
        if (pi->data.sei) {
          g_array_unref (pi->data.sei);
          pi->data.sei = NULL;
        }
        break;
    }
  }
}

static inline const GstVaapiMiniObjectClass *
gst_vaapi_parser_info_h265_class (void)
{
  static const GstVaapiMiniObjectClass GstVaapiParserInfoH265Class = {
    .size = sizeof (GstVaapiParserInfoH265),
    .finalize = (GDestroyNotify) gst_vaapi_parser_info_h265_finalize
  };
  return &GstVaapiParserInfoH265Class;
}

static inline GstVaapiParserInfoH265 *
gst_vaapi_parser_info_h265_new (void)
{
  return (GstVaapiParserInfoH265 *)
      gst_vaapi_mini_object_new (gst_vaapi_parser_info_h265_class ());
}

#define gst_vaapi_parser_info_h265_ref(pi) \
    gst_vaapi_mini_object_ref(GST_VAAPI_MINI_OBJECT(pi))

#define gst_vaapi_parser_info_h265_unref(pi) \
    gst_vaapi_mini_object_unref(GST_VAAPI_MINI_OBJECT(pi))

#define gst_vaapi_parser_info_h265_replace(old_pi_ptr, new_pi)          \
    gst_vaapi_mini_object_replace((GstVaapiMiniObject **)(old_pi_ptr),  \
        (GstVaapiMiniObject *)(new_pi))

/* ------------------------------------------------------------------------- */
/* --- H.265 Pictures                                                    --- */
/* ------------------------------------------------------------------------- */

/*
 * Extended picture flags:
 *
 * @GST_VAAPI_PICTURE_FLAG_IDR: flag that specifies an IDR picture
 * @GST_VAAPI_PICTURE_FLAG_AU_START: flag that marks the start of an
 *   access unit (AU)
 * @GST_VAAPI_PICTURE_FLAG_AU_END: flag that marks the end of an
 *   access unit (AU)
 * @GST_VAAPI_PICTURE_FLAG_RPS_ST_CURR_BEFORE: flag indicate the inclusion
 *   of picture in RefPicSetStCurrBefore reference list
 * @GST_VAAPI_PICTURE_FLAG_RPS_ST_CURR_AFTER: flag indicate the inclusion
 *   of picture in RefPictSetStCurrAfter reference list
 * @GST_VAAPI_PICTURE_FLAG_RPS_ST_FOLL: flag indicate the inclusion
 *   of picture in RefPicSetStFoll reference list
 * @GST_VAAPI_PICTURE_FLAG_RPS_LT_CURR: flag indicate the inclusion
 *   of picture in RefPicSetLtCurr reference list
 * @GST_VAAPI_PICTURE_FLAG_RPS_LT_FOLL: flag indicate the inclusion
 *   of picture in RefPicSetLtFoll reference list
 * @GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE: flag that specifies
 *     "used for short-term reference"
 * @GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE: flag that specifies
 *     "used for long-term reference"
 * @GST_VAAPI_PICTURE_FLAGS_REFERENCE: mask covering any kind of
 *     reference picture (short-term reference or long-term reference)
 */
enum
{
  GST_VAAPI_PICTURE_FLAG_IDR = (GST_VAAPI_PICTURE_FLAG_LAST << 0),
  GST_VAAPI_PICTURE_FLAG_REFERENCE2 = (GST_VAAPI_PICTURE_FLAG_LAST << 1),
  GST_VAAPI_PICTURE_FLAG_AU_START = (GST_VAAPI_PICTURE_FLAG_LAST << 4),
  GST_VAAPI_PICTURE_FLAG_AU_END = (GST_VAAPI_PICTURE_FLAG_LAST << 5),
  GST_VAAPI_PICTURE_FLAG_RPS_ST_CURR_BEFORE =
      (GST_VAAPI_PICTURE_FLAG_LAST << 6),
  GST_VAAPI_PICTURE_FLAG_RPS_ST_CURR_AFTER = (GST_VAAPI_PICTURE_FLAG_LAST << 7),
  GST_VAAPI_PICTURE_FLAG_RPS_ST_FOLL = (GST_VAAPI_PICTURE_FLAG_LAST << 8),
  GST_VAAPI_PICTURE_FLAG_RPS_LT_CURR = (GST_VAAPI_PICTURE_FLAG_LAST << 9),
  GST_VAAPI_PICTURE_FLAG_RPS_LT_FOLL = (GST_VAAPI_PICTURE_FLAG_LAST << 10),

  GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE =
      (GST_VAAPI_PICTURE_FLAG_REFERENCE),
  GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE =
      (GST_VAAPI_PICTURE_FLAG_REFERENCE | GST_VAAPI_PICTURE_FLAG_REFERENCE2),
  GST_VAAPI_PICTURE_FLAGS_REFERENCE =
      (GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE |
      GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE),

  GST_VAAPI_PICTURE_FLAGS_RPS_ST =
      (GST_VAAPI_PICTURE_FLAG_RPS_ST_CURR_BEFORE |
      GST_VAAPI_PICTURE_FLAG_RPS_ST_CURR_AFTER |
      GST_VAAPI_PICTURE_FLAG_RPS_ST_FOLL),
  GST_VAAPI_PICTURE_FLAGS_RPS_LT =
      (GST_VAAPI_PICTURE_FLAG_RPS_LT_CURR | GST_VAAPI_PICTURE_FLAG_RPS_LT_FOLL),
};

#define GST_VAAPI_PICTURE_IS_IDR(picture) \
    (GST_VAAPI_PICTURE_FLAG_IS_SET(picture, GST_VAAPI_PICTURE_FLAG_IDR))

#define GST_VAAPI_PICTURE_IS_SHORT_TERM_REFERENCE(picture)      \
    ((GST_VAAPI_PICTURE_FLAGS(picture) &                        \
      GST_VAAPI_PICTURE_FLAGS_REFERENCE) ==                     \
     GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE)

#define GST_VAAPI_PICTURE_IS_LONG_TERM_REFERENCE(picture)       \
    ((GST_VAAPI_PICTURE_FLAGS(picture) &                        \
      GST_VAAPI_PICTURE_FLAGS_REFERENCE) ==                     \
     GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE)

#define GST_VAAPI_PICTURE_H265(picture) \
    ((GstVaapiPictureH265 *)(picture))

struct _GstVaapiPictureH265
{
  GstVaapiPicture base;
  GstH265SliceHdr *last_slice_hdr;
  guint structure;
  gint32 poc;                   // PicOrderCntVal (8.3.1)
  gint32 poc_lsb;               // slice_pic_order_cnt_lsb
  guint32 pic_latency_cnt;      // PicLatencyCount
  guint output_flag:1;
  guint output_needed:1;
  guint NoRaslOutputFlag:1;
  guint NoOutputOfPriorPicsFlag:1;
  guint RapPicFlag:1;           // nalu type between 16 and 21
  guint IntraPicFlag:1;         // Intra pic (only Intra slices)
};

GST_VAAPI_CODEC_DEFINE_TYPE (GstVaapiPictureH265, gst_vaapi_picture_h265);

void
gst_vaapi_picture_h265_destroy (GstVaapiPictureH265 * picture)
{
  gst_vaapi_picture_destroy (GST_VAAPI_PICTURE (picture));
}

gboolean
gst_vaapi_picture_h265_create (GstVaapiPictureH265 * picture,
    const GstVaapiCodecObjectConstructorArgs * args)
{
  if (!gst_vaapi_picture_create (GST_VAAPI_PICTURE (picture), args))
    return FALSE;

  picture->structure = picture->base.structure;
  picture->poc = G_MAXINT32;
  picture->output_needed = FALSE;
  return TRUE;
}

static inline void
gst_vaapi_picture_h265_set_reference (GstVaapiPictureH265 * picture,
    guint reference_flags)
{
  if (!picture)
    return;
  GST_VAAPI_PICTURE_FLAG_UNSET (picture,
      GST_VAAPI_PICTURE_FLAGS_RPS_ST | GST_VAAPI_PICTURE_FLAGS_RPS_LT);
  GST_VAAPI_PICTURE_FLAG_UNSET (picture, GST_VAAPI_PICTURE_FLAGS_REFERENCE);
  GST_VAAPI_PICTURE_FLAG_SET (picture, reference_flags);
}

/* ------------------------------------------------------------------------- */
/* --- Frame Buffers (DPB)                                               --- */
/* ------------------------------------------------------------------------- */

struct _GstVaapiFrameStore
{
  /*< private > */
  GstVaapiMiniObject parent_instance;

  GstVaapiPictureH265 *buffer;
};

static void
gst_vaapi_frame_store_finalize (gpointer object)
{
  GstVaapiFrameStore *const fs = object;

  gst_vaapi_picture_replace (&fs->buffer, NULL);
}

static GstVaapiFrameStore *
gst_vaapi_frame_store_new (GstVaapiPictureH265 * picture)
{
  GstVaapiFrameStore *fs;

  static const GstVaapiMiniObjectClass GstVaapiFrameStoreClass = {
    sizeof (GstVaapiFrameStore),
    gst_vaapi_frame_store_finalize
  };

  fs = (GstVaapiFrameStore *)
      gst_vaapi_mini_object_new (&GstVaapiFrameStoreClass);
  if (!fs)
    return NULL;

  fs->buffer = gst_vaapi_picture_ref (picture);

  return fs;
}

static inline gboolean
gst_vaapi_frame_store_has_reference (GstVaapiFrameStore * fs)
{
  if (GST_VAAPI_PICTURE_IS_REFERENCE (fs->buffer))
    return TRUE;
  return FALSE;
}

#define gst_vaapi_frame_store_ref(fs) \
    gst_vaapi_mini_object_ref(GST_VAAPI_MINI_OBJECT(fs))

#define gst_vaapi_frame_store_unref(fs) \
    gst_vaapi_mini_object_unref(GST_VAAPI_MINI_OBJECT(fs))

#define gst_vaapi_frame_store_replace(old_fs_p, new_fs)                 \
    gst_vaapi_mini_object_replace((GstVaapiMiniObject **)(old_fs_p),    \
        (GstVaapiMiniObject *)(new_fs))

/* ------------------------------------------------------------------------- */
/* --- H.265 Decoder                                                     --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_DECODER_H265_CAST(decoder) \
    ((GstVaapiDecoderH265 *)(decoder))

typedef enum
{
  GST_H265_VIDEO_STATE_GOT_VPS = 1 << 0,
  GST_H265_VIDEO_STATE_GOT_SPS = 1 << 1,
  GST_H265_VIDEO_STATE_GOT_PPS = 1 << 2,
  GST_H265_VIDEO_STATE_GOT_SLICE = 1 << 3,
  GST_H265_VIDEO_STATE_GOT_I_FRAME = 1 << 4,    /* persistent across SPS */
  GST_H265_VIDEO_STATE_GOT_P_SLICE = 1 << 5,    /* predictive (all non-intra) */

  GST_H265_VIDEO_STATE_VALID_PICTURE_HEADERS =
      (GST_H265_VIDEO_STATE_GOT_SPS | GST_H265_VIDEO_STATE_GOT_PPS),
  GST_H265_VIDEO_STATE_VALID_PICTURE =
      (GST_H265_VIDEO_STATE_VALID_PICTURE_HEADERS |
      GST_H265_VIDEO_STATE_GOT_SLICE)
} GstH265VideoState;

struct _GstVaapiDecoderH265Private
{
  GstH265Parser *parser;
  guint parser_state;
  guint decoder_state;
  GstVaapiStreamAlignH265 stream_alignment;
  GstVaapiPictureH265 *current_picture;
  GstVaapiParserInfoH265 *vps[GST_H265_MAX_VPS_COUNT];
  GstVaapiParserInfoH265 *active_vps;
  GstVaapiParserInfoH265 *sps[GST_H265_MAX_SPS_COUNT];
  GstVaapiParserInfoH265 *active_sps;
  GstVaapiParserInfoH265 *pps[GST_H265_MAX_PPS_COUNT];
  GstVaapiParserInfoH265 *active_pps;
  GstVaapiParserInfoH265 *prev_pi;
  GstVaapiParserInfoH265 *prev_slice_pi;
  GstVaapiParserInfoH265 *prev_independent_slice_pi;
  GstVaapiFrameStore **dpb;
  guint dpb_count;
  guint dpb_size;
  guint dpb_size_max;
  GstVaapiProfile profile;
  GstVaapiEntrypoint entrypoint;
  GstVaapiChromaType chroma_type;

  GstVaapiPictureH265 *RefPicSetStCurrBefore[16];
  GstVaapiPictureH265 *RefPicSetStCurrAfter[16];
  GstVaapiPictureH265 *RefPicSetStFoll[16];
  GstVaapiPictureH265 *RefPicSetLtCurr[16];
  GstVaapiPictureH265 *RefPicSetLtFoll[16];

  GstVaapiPictureH265 *RefPicList0[16];
  guint RefPicList0_count;
  GstVaapiPictureH265 *RefPicList1[16];
  guint RefPicList1_count;

  guint32 SpsMaxLatencyPictures;
  gint32 WpOffsetHalfRangeC;

  guint nal_length_size;

  guint pic_width_in_luma_samples;      //sps->pic_width_in_luma_samples
  guint pic_height_in_luma_samples;     //sps->pic_height_in_luma_samples
  guint pic_structure;          // pic_struct (from SEI pic_timing() or inferred)
  gint32 poc;                   // PicOrderCntVal
  gint32 poc_msb;               // PicOrderCntMsb
  gint32 poc_lsb;               // pic_order_cnt_lsb (from slice_header())
  gint32 prev_poc_msb;          // prevPicOrderCntMsb
  gint32 prev_poc_lsb;          // prevPicOrderCntLsb
  gint32 prev_tid0pic_poc_lsb;
  gint32 prev_tid0pic_poc_msb;
  gint32 PocStCurrBefore[16];
  gint32 PocStCurrAfter[16];
  gint32 PocStFoll[16];
  gint32 PocLtCurr[16];
  gint32 PocLtFoll[16];
  guint NumPocStCurrBefore;
  guint NumPocStCurrAfter;
  guint NumPocStFoll;
  guint NumPocLtCurr;
  guint NumPocLtFoll;
  guint NumPocTotalCurr;
  guint is_opened:1;
  guint is_hvcC:1;
  guint has_context:1;
  guint progressive_sequence:1;
  guint new_bitstream:1;
  guint prev_nal_is_eos:1;      /*previous nal type is EOS */
  guint associated_irap_NoRaslOutputFlag:1;
};

/**
 * GstVaapiDecoderH265:
 *
 * A decoder based on H265.
 */
struct _GstVaapiDecoderH265
{
  /*< private > */
  GstVaapiDecoder parent_instance;
  GstVaapiDecoderH265Private priv;
};

/**
 * GstVaapiDecoderH265Class:
 *
 * A decoder class based on H265.
 */
struct _GstVaapiDecoderH265Class
{
  /*< private > */
  GstVaapiDecoderClass parent_class;
};

G_DEFINE_TYPE (GstVaapiDecoderH265, gst_vaapi_decoder_h265,
    GST_TYPE_VAAPI_DECODER);

#define RSV_VCL_N10 10
#define RSV_VCL_N12 12
#define RSV_VCL_N14 14

static gboolean
nal_is_idr (guint8 nal_type)
{
  if ((nal_type == GST_H265_NAL_SLICE_IDR_W_RADL) ||
      (nal_type == GST_H265_NAL_SLICE_IDR_N_LP))
    return TRUE;
  return FALSE;
}

static gboolean
nal_is_irap (guint8 nal_type)
{
  if ((nal_type >= GST_H265_NAL_SLICE_BLA_W_LP) &&
      (nal_type <= RESERVED_IRAP_NAL_TYPE_MAX))
    return TRUE;
  return FALSE;
}

static gboolean
nal_is_bla (guint8 nal_type)
{
  if ((nal_type >= GST_H265_NAL_SLICE_BLA_W_LP) &&
      (nal_type <= GST_H265_NAL_SLICE_BLA_N_LP))
    return TRUE;
  return FALSE;
}

static gboolean
nal_is_cra (guint8 nal_type)
{
  if (nal_type == GST_H265_NAL_SLICE_CRA_NUT)
    return TRUE;
  return FALSE;
}

static gboolean
nal_is_radl (guint8 nal_type)
{
  if ((nal_type >= GST_H265_NAL_SLICE_RADL_N) &&
      (nal_type <= GST_H265_NAL_SLICE_RADL_R))
    return TRUE;
  return FALSE;
}

static gboolean
nal_is_rasl (guint8 nal_type)
{
  if ((nal_type >= GST_H265_NAL_SLICE_RASL_N) &&
      (nal_type <= GST_H265_NAL_SLICE_RASL_R))
    return TRUE;
  return FALSE;
}

static gboolean
nal_is_slice (guint8 nal_type)
{
  if ((nal_type <= GST_H265_NAL_SLICE_CRA_NUT))
    return TRUE;
  return FALSE;
}

static gboolean
nal_is_ref (guint8 nal_type)
{
  gboolean ret = FALSE;
  switch (nal_type) {
    case GST_H265_NAL_SLICE_TRAIL_N:
    case GST_H265_NAL_SLICE_TSA_N:
    case GST_H265_NAL_SLICE_STSA_N:
    case GST_H265_NAL_SLICE_RADL_N:
    case GST_H265_NAL_SLICE_RASL_N:
    case RSV_VCL_N10:
    case RSV_VCL_N12:
    case RSV_VCL_N14:
      ret = FALSE;
      break;
    default:
      ret = TRUE;
      break;
  }
  return ret;
}

static gboolean
is_range_extension_profile (GstVaapiProfile profile)
{
  if (profile == GST_VAAPI_PROFILE_H265_MAIN_422_10
      || profile == GST_VAAPI_PROFILE_H265_MAIN_444
      || profile == GST_VAAPI_PROFILE_H265_MAIN_444_10
      || profile == GST_VAAPI_PROFILE_H265_MAIN12
      || profile == GST_VAAPI_PROFILE_H265_MAIN_444_12
      || profile == GST_VAAPI_PROFILE_H265_MAIN_422_12)
    return TRUE;
  return FALSE;
}

static gboolean
is_scc_profile (GstVaapiProfile profile)
{
#if VA_CHECK_VERSION(1,2,0)
  if (profile == GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN
      || profile == GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_10
      || profile == GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_444
#if VA_CHECK_VERSION(1,8,0)
      || profile == GST_VAAPI_PROFILE_H265_SCREEN_EXTENDED_MAIN_444_10
#endif
      )
    return TRUE;
#endif
  return FALSE;
}

static inline GstVaapiPictureH265 *
gst_vaapi_picture_h265_new (GstVaapiDecoderH265 * decoder)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  if (is_range_extension_profile (priv->profile)
      || is_scc_profile (priv->profile)) {
#if VA_CHECK_VERSION(1,2,0)
    return (GstVaapiPictureH265 *)
        gst_vaapi_codec_object_new (&GstVaapiPictureH265Class,
        GST_VAAPI_CODEC_BASE (decoder), NULL,
        sizeof (VAPictureParameterBufferHEVCExtension), NULL, 0, 0);
#endif
    return NULL;
  } else {
    return (GstVaapiPictureH265 *)
        gst_vaapi_codec_object_new (&GstVaapiPictureH265Class,
        GST_VAAPI_CODEC_BASE (decoder), NULL,
        sizeof (VAPictureParameterBufferHEVC), NULL, 0, 0);
  }
}

/* Activates the supplied PPS */
static GstH265PPS *
ensure_pps (GstVaapiDecoderH265 * decoder, GstH265PPS * pps)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const pi = priv->pps[pps->id];

  gst_vaapi_parser_info_h265_replace (&priv->active_pps, pi);

  /* Ensure our copy is up-to-date */
  if (pi) {
    pi->data.pps = *pps;
    pi->data.pps.sps = NULL;
  }

  return pi ? &pi->data.pps : NULL;
}

/* Returns the active PPS */
static inline GstH265PPS *
get_pps (GstVaapiDecoderH265 * decoder)
{
  GstVaapiParserInfoH265 *const pi = decoder->priv.active_pps;

  return pi ? &pi->data.pps : NULL;
}

/* Activate the supplied SPS */
static GstH265SPS *
ensure_sps (GstVaapiDecoderH265 * decoder, GstH265SPS * sps)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const pi = priv->sps[sps->id];

  /* Propagate "got I-frame" state to the next SPS unit if the current
   * sequence was not ended */
  if (pi && priv->active_sps)
    pi->state |= (priv->active_sps->state & GST_H265_VIDEO_STATE_GOT_I_FRAME);

  /* Ensure our copy is up-to-date */
  if (pi)
    pi->data.sps = *sps;

  gst_vaapi_parser_info_h265_replace (&priv->active_sps, pi);
  return pi ? &pi->data.sps : NULL;
}

/* Returns the active SPS */
static inline GstH265SPS *
get_sps (GstVaapiDecoderH265 * decoder)
{
  GstVaapiParserInfoH265 *const pi = decoder->priv.active_sps;

  return pi ? &pi->data.sps : NULL;
}

/* VPS nal is not necessary to decode the base layers, so this is not
 * needed at the moment. But in future we need this, especially when
 * dealing with MVC and scalable layer decoding.
 * See https://bugzilla.gnome.org/show_bug.cgi?id=754250
 */
#if 0
/* Activate the supplied VPS */
static GstH265VPS *
ensure_vps (GstVaapiDecoderH265 * decoder, GstH265VPS * vps)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const pi = priv->vps[vps->id];

  gst_vaapi_parser_info_h265_replace (&priv->active_vps, pi);
  return pi ? &pi->data.vps : NULL;
}

/* Returns the active VPS */
static inline GstH265VPS *
get_vps (GstVaapiDecoderH265 * decoder)
{
  GstVaapiParserInfoH265 *const pi = decoder->priv.active_vps;
  return pi ? &pi->data.vps : NULL;
}
#endif

/* Get number of reference frames to use */
static guint
get_max_dec_frame_buffering (GstH265SPS * sps)
{
  G_GNUC_UNUSED guint max_dec_frame_buffering;  /* FIXME */
  GstVaapiLevelH265 level;
  const GstVaapiH265LevelLimits *level_limits;

  level = gst_vaapi_utils_h265_get_level (sps->profile_tier_level.level_idc);
  level_limits = gst_vaapi_utils_h265_get_level_limits (level);
  if (G_UNLIKELY (!level_limits)) {
    GST_FIXME ("unsupported level_idc value (%d)",
        sps->profile_tier_level.level_idc);
    max_dec_frame_buffering = 16;
  }

  /* FIXME: Add limit check based on Annex A */

  /* Assuming HighestTid as sps_max_sub_layers_minus1 */
  return MAX (1,
      (sps->max_dec_pic_buffering_minus1[sps->max_sub_layers_minus1] + 1));
}

static void
dpb_remove_all (GstVaapiDecoderH265 * decoder)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;

  while (priv->dpb_count > 0)
    gst_vaapi_frame_store_replace (&priv->dpb[--priv->dpb_count], NULL);
}

static void
dpb_remove_index (GstVaapiDecoderH265 * decoder, gint index)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  guint i, num_frames = --priv->dpb_count;

  if (USE_STRICT_DPB_ORDERING) {
    for (i = index; i < num_frames; i++)
      gst_vaapi_frame_store_replace (&priv->dpb[i], priv->dpb[i + 1]);
  } else if (index != num_frames)
    gst_vaapi_frame_store_replace (&priv->dpb[index], priv->dpb[num_frames]);
  gst_vaapi_frame_store_replace (&priv->dpb[num_frames], NULL);
}

static gboolean
dpb_output (GstVaapiDecoderH265 * decoder, GstVaapiFrameStore * fs)
{
  GstVaapiPictureH265 *picture;

  g_return_val_if_fail (fs != NULL, FALSE);

  picture = fs->buffer;
  if (!picture)
    return FALSE;

  picture->output_needed = FALSE;
  return gst_vaapi_picture_output (GST_VAAPI_PICTURE_CAST (picture));
}

/* Get the dpb picture having the specifed poc or poc_lsb */
static GstVaapiPictureH265 *
dpb_get_picture (GstVaapiDecoderH265 * decoder, gint poc, gboolean match_lsb)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  guint i;

  for (i = 0; i < priv->dpb_count; i++) {
    GstVaapiPictureH265 *const picture = priv->dpb[i]->buffer;

    if (picture && GST_VAAPI_PICTURE_FLAG_IS_SET (picture,
            GST_VAAPI_PICTURE_FLAGS_REFERENCE)) {
      if (match_lsb) {
        if (picture->poc_lsb == poc)
          return picture;
      } else {
        if (picture->poc == poc)
          return picture;
      }
    }
  }
  return NULL;
}

/* Get the dpb picture having the specifed poc and shor/long ref flags */
static GstVaapiPictureH265 *
dpb_get_ref_picture (GstVaapiDecoderH265 * decoder, gint poc, gboolean is_short)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  guint i;

  for (i = 0; i < priv->dpb_count; i++) {
    GstVaapiPictureH265 *const picture = priv->dpb[i]->buffer;

    if (picture && picture->poc == poc) {
      if (is_short && GST_VAAPI_PICTURE_IS_SHORT_TERM_REFERENCE (picture))
        return picture;
      else if (GST_VAAPI_PICTURE_IS_LONG_TERM_REFERENCE (picture))
        return picture;
    }
  }

  return NULL;
}

/* Finds the picture with the lowest POC that needs to be output */
static gint
dpb_find_lowest_poc (GstVaapiDecoderH265 * decoder,
    GstVaapiPictureH265 ** found_picture_ptr)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiPictureH265 *found_picture = NULL;
  guint i, found_index = -1;

  for (i = 0; i < priv->dpb_count; i++) {
    GstVaapiPictureH265 *const picture = priv->dpb[i]->buffer;
    if (picture && !picture->output_needed)
      continue;
    if (picture && (!found_picture || found_picture->poc > picture->poc)) {
      found_picture = picture;
      found_index = i;
    }
  }

  if (found_picture_ptr)
    *found_picture_ptr = found_picture;
  return found_index;
}

static gboolean
dpb_bump (GstVaapiDecoderH265 * decoder, GstVaapiPictureH265 * picture)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiPictureH265 *found_picture;
  gint found_index;
  gboolean success;

  found_index = dpb_find_lowest_poc (decoder, &found_picture);
  if (found_index < 0)
    return FALSE;

  success = dpb_output (decoder, priv->dpb[found_index]);

  if (!gst_vaapi_frame_store_has_reference (priv->dpb[found_index]))
    dpb_remove_index (decoder, found_index);

  return success;
}

static void
dpb_clear (GstVaapiDecoderH265 * decoder, gboolean hard_flush)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiPictureH265 *pic;
  guint i;

  if (hard_flush) {
    dpb_remove_all (decoder);
  } else {
    /* Remove unused pictures from DPB */
    i = 0;
    while (i < priv->dpb_count) {
      GstVaapiFrameStore *const fs = priv->dpb[i];
      pic = fs->buffer;
      if (!pic->output_needed && !gst_vaapi_frame_store_has_reference (fs))
        dpb_remove_index (decoder, i);
      else
        i++;
    }
  }
}

static void
dpb_flush (GstVaapiDecoderH265 * decoder)
{
  /* Output any frame remaining in DPB */
  while (dpb_bump (decoder, NULL));
  dpb_clear (decoder, TRUE);
}

static gint
dpb_get_num_need_output (GstVaapiDecoderH265 * decoder)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  guint i = 0, n_output_needed = 0;

  while (i < priv->dpb_count) {
    GstVaapiFrameStore *const fs = priv->dpb[i];
    if (fs->buffer->output_needed)
      n_output_needed++;
    i++;
  }

  return n_output_needed;
}

static gboolean
check_latency_cnt (GstVaapiDecoderH265 * decoder)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiPictureH265 *tmp_pic;
  guint i = 0;

  while (i < priv->dpb_count) {
    GstVaapiFrameStore *const fs = priv->dpb[i];
    tmp_pic = fs->buffer;
    if (tmp_pic->output_needed) {
      if (tmp_pic->pic_latency_cnt >= priv->SpsMaxLatencyPictures)
        return TRUE;
    }
    i++;
  }

  return FALSE;
}

static gboolean
dpb_add (GstVaapiDecoderH265 * decoder, GstVaapiPictureH265 * picture)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstH265SPS *const sps = get_sps (decoder);
  GstVaapiFrameStore *fs;
  GstVaapiPictureH265 *tmp_pic;
  guint i = 0;

  /* C.5.2.3 */
  if (picture->output_flag) {
    while (i < priv->dpb_count) {
      GstVaapiFrameStore *const fs = priv->dpb[i];
      tmp_pic = fs->buffer;
      if (tmp_pic->output_needed)
        tmp_pic->pic_latency_cnt += 1;
      i++;
    }
  }

  /* Create new frame store */
  fs = gst_vaapi_frame_store_new (picture);
  if (!fs)
    return FALSE;
  gst_vaapi_frame_store_replace (&priv->dpb[priv->dpb_count++], fs);
  gst_vaapi_frame_store_unref (fs);

  if (picture->output_flag) {
    picture->output_needed = 1;
    picture->pic_latency_cnt = 0;
  } else
    picture->output_needed = 0;

  /* set pic as short_term_ref */
  gst_vaapi_picture_h265_set_reference (picture,
      GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE);

  /* C.5.2.4 "Bumping" process */
  while ((dpb_get_num_need_output (decoder) >
          sps->max_num_reorder_pics[sps->max_sub_layers_minus1])
      || (sps->max_latency_increase_plus1[sps->max_sub_layers_minus1]
          && check_latency_cnt (decoder)))
    dpb_bump (decoder, picture);

  return TRUE;
}


/* C.5.2.2 */
static gboolean
dpb_init (GstVaapiDecoderH265 * decoder, GstVaapiPictureH265 * picture,
    GstVaapiParserInfoH265 * pi)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstH265SliceHdr *const slice_hdr = &pi->data.slice_hdr;
  GstH265SPS *const sps = get_sps (decoder);

  if (nal_is_irap (pi->nalu.type)
      && picture->NoRaslOutputFlag && !priv->new_bitstream) {

    if (pi->nalu.type == GST_H265_NAL_SLICE_CRA_NUT)
      picture->NoOutputOfPriorPicsFlag = 1;
    else
      picture->NoOutputOfPriorPicsFlag =
          slice_hdr->no_output_of_prior_pics_flag;

    if (picture->NoOutputOfPriorPicsFlag)
      dpb_clear (decoder, TRUE);
    else {
      dpb_clear (decoder, FALSE);
      while (dpb_bump (decoder, NULL));
    }
  } else {
    dpb_clear (decoder, FALSE);
    while ((dpb_get_num_need_output (decoder) >
            sps->max_num_reorder_pics[sps->max_sub_layers_minus1])
        || (sps->max_latency_increase_plus1[sps->max_sub_layers_minus1]
            && check_latency_cnt (decoder))
        || (priv->dpb_count >=
            (sps->max_dec_pic_buffering_minus1[sps->max_sub_layers_minus1] +
                1))) {
      dpb_bump (decoder, picture);
    }
  }

  return TRUE;
}

static gboolean
dpb_reset (GstVaapiDecoderH265 * decoder, guint dpb_size)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;

  if (dpb_size > priv->dpb_size_max) {
    priv->dpb = g_try_realloc_n (priv->dpb, dpb_size, sizeof (*priv->dpb));
    if (!priv->dpb)
      return FALSE;
    memset (&priv->dpb[priv->dpb_size_max], 0,
        (dpb_size - priv->dpb_size_max) * sizeof (*priv->dpb));
    priv->dpb_size_max = dpb_size;
  }
  priv->dpb_size = dpb_size;
  GST_DEBUG ("DPB size %u", priv->dpb_size);
  return TRUE;
}

static GstVaapiDecoderStatus
get_status (GstH265ParserResult result)
{
  GstVaapiDecoderStatus status;

  switch (result) {
    case GST_H265_PARSER_OK:
      status = GST_VAAPI_DECODER_STATUS_SUCCESS;
      break;
    case GST_H265_PARSER_NO_NAL_END:
      status = GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
      break;
    case GST_H265_PARSER_ERROR:
      status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
      break;
    default:
      status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
      break;
  }
  return status;
}

static void
gst_vaapi_decoder_h265_close (GstVaapiDecoderH265 * decoder)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;

  gst_vaapi_picture_replace (&priv->current_picture, NULL);
  gst_vaapi_parser_info_h265_replace (&priv->prev_slice_pi, NULL);
  gst_vaapi_parser_info_h265_replace (&priv->prev_independent_slice_pi, NULL);
  gst_vaapi_parser_info_h265_replace (&priv->prev_pi, NULL);

  dpb_clear (decoder, TRUE);

  if (priv->parser) {
    gst_h265_parser_free (priv->parser);
    priv->parser = NULL;
  }

  priv->is_opened = FALSE;
}

static gboolean
gst_vaapi_decoder_h265_open (GstVaapiDecoderH265 * decoder)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;

  gst_vaapi_decoder_h265_close (decoder);
  priv->parser = gst_h265_parser_new ();
  if (!priv->parser)
    return FALSE;
  return TRUE;
}

static void
gst_vaapi_decoder_h265_destroy (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderH265 *const decoder =
      GST_VAAPI_DECODER_H265_CAST (base_decoder);
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  guint i;

  gst_vaapi_decoder_h265_close (decoder);
  g_clear_pointer (&priv->dpb, g_free);
  priv->dpb_count = priv->dpb_size_max = priv->dpb_size = 0;

  for (i = 0; i < G_N_ELEMENTS (priv->pps); i++)
    gst_vaapi_parser_info_h265_replace (&priv->pps[i], NULL);
  gst_vaapi_parser_info_h265_replace (&priv->active_pps, NULL);
  for (i = 0; i < G_N_ELEMENTS (priv->sps); i++)
    gst_vaapi_parser_info_h265_replace (&priv->sps[i], NULL);
  gst_vaapi_parser_info_h265_replace (&priv->active_sps, NULL);
  for (i = 0; i < G_N_ELEMENTS (priv->vps); i++)
    gst_vaapi_parser_info_h265_replace (&priv->vps[i], NULL);
  gst_vaapi_parser_info_h265_replace (&priv->active_vps, NULL);
}

static gboolean
gst_vaapi_decoder_h265_create (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderH265 *const decoder =
      GST_VAAPI_DECODER_H265_CAST (base_decoder);
  GstVaapiDecoderH265Private *const priv = &decoder->priv;

  priv->profile = GST_VAAPI_PROFILE_UNKNOWN;
  priv->entrypoint = GST_VAAPI_ENTRYPOINT_VLD;
  priv->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
  priv->progressive_sequence = TRUE;
  priv->new_bitstream = TRUE;
  priv->prev_nal_is_eos = FALSE;
  return TRUE;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_h265_reset (GstVaapiDecoder * base_decoder)
{
  gst_vaapi_decoder_h265_destroy (base_decoder);
  gst_vaapi_decoder_h265_create (base_decoder);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
fill_profiles (GstVaapiProfile profiles[], guint * n_profiles_ptr,
    GstVaapiProfile profile)
{
  guint n_profiles = *n_profiles_ptr;

  profiles[n_profiles++] = profile;
  switch (profile) {
    case GST_VAAPI_PROFILE_H265_MAIN:
      profiles[n_profiles++] = GST_VAAPI_PROFILE_H265_MAIN10;
      break;
    case GST_VAAPI_PROFILE_H265_MAIN_STILL_PICTURE:
      profiles[n_profiles++] = GST_VAAPI_PROFILE_H265_MAIN;
      profiles[n_profiles++] = GST_VAAPI_PROFILE_H265_MAIN10;
      break;
    default:
      break;
  }
  *n_profiles_ptr = n_profiles;
}

static GstVaapiProfile
get_profile (GstVaapiDecoderH265 * decoder, GstH265SPS * sps, guint dpb_size)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiDisplay *const display = GST_VAAPI_DECODER_DISPLAY (decoder);
  GstVaapiProfile profile, profiles[3];
  guint i, n_profiles = 0;

  profile = gst_vaapi_utils_h265_get_profile (sps);
  if (!profile) {
    /* HACK: This is a work-around to identify some main profile streams having wrong profile_idc.
     * There are some wrongly encoded main profile streams(eg: ENTP_C_LG_3.bin) which doesn't
     * have any of the profile_idc values mentioned in Annex-A, instead general_profile_idc
     * has been set as zero and having general_profile_compatibility_flag[general_profile_idc]
     * is TRUE. Assuming them as MAIN profile for now */
    if (sps->profile_tier_level.profile_space == 0 &&
        sps->profile_tier_level.profile_idc == 0 &&
        sps->profile_tier_level.profile_compatibility_flag[0] == 1) {
      GST_WARNING ("Wrong profile_idc, blindly setting it as main profile !!");
      profile = GST_VAAPI_PROFILE_H265_MAIN;
    } else
      return GST_VAAPI_PROFILE_UNKNOWN;
  }

  fill_profiles (profiles, &n_profiles, profile);
  switch (profile) {
    case GST_VAAPI_PROFILE_H265_MAIN10:
      if (sps->profile_tier_level.profile_compatibility_flag[1]) {      // A.2.3.2 (main profile)
        fill_profiles (profiles, &n_profiles, GST_VAAPI_PROFILE_H265_MAIN);
      }
      break;
    default:
      break;
  }

  /* If the preferred profile (profiles[0]) matches one that we already
     found, then just return it now instead of searching for it again */
  if (profiles[0] == priv->profile)
    return priv->profile;
  for (i = 0; i < n_profiles; i++) {
    if (gst_vaapi_display_has_decoder (display, profiles[i], priv->entrypoint))
      return profiles[i];
  }
  return GST_VAAPI_PROFILE_UNKNOWN;
}

static GstVaapiDecoderStatus
ensure_context (GstVaapiDecoderH265 * decoder, GstH265SPS * sps)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER_CAST (decoder);
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiContextInfo info;
  GstVaapiProfile profile;
  GstVaapiChromaType chroma_type;
  gboolean reset_context = FALSE;
  guint dpb_size;

  dpb_size = get_max_dec_frame_buffering (sps);
  if (priv->dpb_size < dpb_size) {
    GST_DEBUG ("DPB size increased");
    reset_context = TRUE;
  }

  profile = get_profile (decoder, sps, dpb_size);
  if (!profile) {
    GST_ERROR ("unsupported profile_idc %u",
        sps->profile_tier_level.profile_idc);
    return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  }

  if (!priv->profile || (priv->profile != profile)) {
    GST_DEBUG ("profile changed");
    reset_context = TRUE;
    priv->profile = profile;
  }

  chroma_type =
      gst_vaapi_utils_h265_get_chroma_type (sps->chroma_format_idc,
      sps->bit_depth_luma_minus8 + 8, sps->bit_depth_chroma_minus8 + 8);
  if (!chroma_type) {
    GST_ERROR ("unsupported chroma_format_idc %u", sps->chroma_format_idc);
    return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT;
  }

  if (priv->chroma_type != chroma_type) {
    GST_DEBUG ("chroma format changed");
    reset_context = TRUE;
    priv->chroma_type = chroma_type;
  }

  if (priv->pic_width_in_luma_samples != sps->pic_width_in_luma_samples ||
      priv->pic_height_in_luma_samples != sps->pic_height_in_luma_samples) {
    GST_DEBUG ("size changed");
    reset_context = TRUE;
    priv->pic_width_in_luma_samples = sps->pic_width_in_luma_samples;
    priv->pic_height_in_luma_samples = sps->pic_height_in_luma_samples;
  }

  priv->progressive_sequence = 1;       /* FIXME */
  gst_vaapi_decoder_set_interlaced (base_decoder, !priv->progressive_sequence);
  gst_vaapi_decoder_set_pixel_aspect_ratio (base_decoder,
      sps->vui_params.par_n, sps->vui_params.par_d);
  if (!reset_context && priv->has_context)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  /* XXX: fix surface size when cropping is implemented */
  /* *INDENT-OFF* */
  info = (GstVaapiContextInfo) {
    .profile = priv->profile,
    .entrypoint = priv->entrypoint,
    .chroma_type = priv->chroma_type,
    .width = sps->width,
    .height = sps->height,
    .ref_frames = dpb_size,
  };
  /* *INDENT-ON* */

  if (!gst_vaapi_decoder_ensure_context (GST_VAAPI_DECODER (decoder), &info))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  priv->has_context = TRUE;

  /* Reset DPB */
  if (!dpb_reset (decoder, dpb_size))
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
fill_iq_matrix_4x4 (VAIQMatrixBufferHEVC * iq_matrix,
    GstH265ScalingList * scaling_list)
{
  guint i;

  g_assert (G_N_ELEMENTS (iq_matrix->ScalingList4x4) == 6);
  g_assert (G_N_ELEMENTS (iq_matrix->ScalingList4x4[0]) == 16);
  for (i = 0; i < G_N_ELEMENTS (iq_matrix->ScalingList4x4); i++) {
    gst_h265_quant_matrix_4x4_get_raster_from_uprightdiagonal
        (iq_matrix->ScalingList4x4[i], scaling_list->scaling_lists_4x4[i]);
  }
}

static void
fill_iq_matrix_8x8 (VAIQMatrixBufferHEVC * iq_matrix,
    GstH265ScalingList * scaling_list)
{
  guint i;

  g_assert (G_N_ELEMENTS (iq_matrix->ScalingList8x8) == 6);
  g_assert (G_N_ELEMENTS (iq_matrix->ScalingList8x8[0]) == 64);
  for (i = 0; i < G_N_ELEMENTS (iq_matrix->ScalingList8x8); i++) {
    gst_h265_quant_matrix_8x8_get_raster_from_uprightdiagonal
        (iq_matrix->ScalingList8x8[i], scaling_list->scaling_lists_8x8[i]);
  }
}

static void
fill_iq_matrix_16x16 (VAIQMatrixBufferHEVC * iq_matrix,
    GstH265ScalingList * scaling_list)
{
  guint i;

  g_assert (G_N_ELEMENTS (iq_matrix->ScalingList16x16) == 6);
  g_assert (G_N_ELEMENTS (iq_matrix->ScalingList16x16[0]) == 64);
  for (i = 0; i < G_N_ELEMENTS (iq_matrix->ScalingList16x16); i++) {
    gst_h265_quant_matrix_16x16_get_raster_from_uprightdiagonal
        (iq_matrix->ScalingList16x16[i], scaling_list->scaling_lists_16x16[i]);
  }
}

static void
fill_iq_matrix_32x32 (VAIQMatrixBufferHEVC * iq_matrix,
    GstH265ScalingList * scaling_list)
{
  guint i;

  g_assert (G_N_ELEMENTS (iq_matrix->ScalingList32x32) == 2);
  g_assert (G_N_ELEMENTS (iq_matrix->ScalingList32x32[0]) == 64);
  for (i = 0; i < G_N_ELEMENTS (iq_matrix->ScalingList32x32); i++) {
    gst_h265_quant_matrix_32x32_get_raster_from_uprightdiagonal
        (iq_matrix->ScalingList32x32[i], scaling_list->scaling_lists_32x32[i]);
  }
}

static void
fill_iq_matrix_dc_16x16 (VAIQMatrixBufferHEVC * iq_matrix,
    GstH265ScalingList * scaling_list)
{
  guint i;

  for (i = 0; i < 6; i++)
    iq_matrix->ScalingListDC16x16[i] =
        scaling_list->scaling_list_dc_coef_minus8_16x16[i] + 8;
}

static void
fill_iq_matrix_dc_32x32 (VAIQMatrixBufferHEVC * iq_matrix,
    GstH265ScalingList * scaling_list)
{
  guint i;

  for (i = 0; i < 2; i++)
    iq_matrix->ScalingListDC32x32[i] =
        scaling_list->scaling_list_dc_coef_minus8_32x32[i] + 8;
}

static GstVaapiDecoderStatus
ensure_quant_matrix (GstVaapiDecoderH265 * decoder,
    GstVaapiPictureH265 * picture)
{
  GstVaapiPicture *const base_picture = &picture->base;
  GstH265PPS *const pps = get_pps (decoder);
  GstH265SPS *const sps = get_sps (decoder);
  GstH265ScalingList *scaling_list = NULL;
  VAIQMatrixBufferHEVC *iq_matrix;

  if (pps &&
      (pps->scaling_list_data_present_flag ||
          (sps->scaling_list_enabled_flag
              && !sps->scaling_list_data_present_flag)))
    scaling_list = &pps->scaling_list;
  else if (sps && sps->scaling_list_enabled_flag
      && sps->scaling_list_data_present_flag)
    scaling_list = &sps->scaling_list;
  else
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  base_picture->iq_matrix = GST_VAAPI_IQ_MATRIX_NEW (HEVC, decoder);
  if (!base_picture->iq_matrix) {
    GST_ERROR ("failed to allocate IQ matrix");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  iq_matrix = base_picture->iq_matrix->param;

  fill_iq_matrix_4x4 (iq_matrix, scaling_list);
  fill_iq_matrix_8x8 (iq_matrix, scaling_list);
  fill_iq_matrix_16x16 (iq_matrix, scaling_list);
  fill_iq_matrix_32x32 (iq_matrix, scaling_list);
  fill_iq_matrix_dc_16x16 (iq_matrix, scaling_list);
  fill_iq_matrix_dc_32x32 (iq_matrix, scaling_list);

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline gboolean
is_valid_state (guint state, guint ref_state)
{
  return (state & ref_state) == ref_state;
}

static GstVaapiDecoderStatus
decode_current_picture (GstVaapiDecoderH265 * decoder)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const sps_pi = decoder->priv.active_sps;
  GstVaapiPictureH265 *const picture = priv->current_picture;

  if (!is_valid_state (priv->decoder_state, GST_H265_VIDEO_STATE_VALID_PICTURE)) {
    goto drop_frame;
  }

  priv->decoder_state |= sps_pi->state;
  if (!(priv->decoder_state & GST_H265_VIDEO_STATE_GOT_I_FRAME)) {
    const GstH265PPS *pps = get_pps (decoder);
    /* 7.4.3.3.3: the picture is an IRAP picture, nuh_layer_id is equal to 0,
       and pps_curr_pic_ref_enabled_flag is equal to 0, slice_type shall be
       equal to 2(I Slice).
       And F.8.3.4: Decoding process for reference picture lists construction
       is invoked at the beginning of the decoding process for each P or B
       slice.
       so if pps_curr_pic_ref_enabled_flag is set, which means the picture can
       ref to itself, the IRAP picture may be set to P/B slice, in order to
       generate the ref lists. If the slice_type is I, no ref list will be
       constructed and no MV data for that slice according to the syntax.
       That kind of CVS may start with P/B slice, but in fact it is a intra
       frame. */
    if (priv->decoder_state & GST_H265_VIDEO_STATE_GOT_P_SLICE &&
        !pps->pps_scc_extension_params.pps_curr_pic_ref_enabled_flag)
      goto drop_frame;
    sps_pi->state |= GST_H265_VIDEO_STATE_GOT_I_FRAME;
  }

  priv->decoder_state = 0;
  /* FIXME: Use SEI header values */
  priv->pic_structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;

  if (!picture)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  if (!gst_vaapi_picture_decode (GST_VAAPI_PICTURE_CAST (picture)))
    goto error;

  if (!dpb_add (decoder, picture))
    goto error;

  gst_vaapi_picture_replace (&priv->current_picture, NULL);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;

  /* ERRORS */
error:
  {
    gst_vaapi_picture_replace (&priv->current_picture, NULL);
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }
drop_frame:
  {
    priv->decoder_state = 0;
    priv->pic_structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
    return (GstVaapiDecoderStatus) GST_VAAPI_DECODER_STATUS_DROP_FRAME;
  }
}

static GstVaapiDecoderStatus
parse_vps (GstVaapiDecoderH265 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const pi = unit->parsed_info;
  GstH265VPS *const vps = &pi->data.vps;
  GstH265ParserResult result;

  GST_DEBUG ("parse VPS");
  priv->parser_state = 0;

  memset (vps, 0, sizeof (GstH265VPS));

  result = gst_h265_parser_parse_vps (priv->parser, &pi->nalu, vps);
  if (result != GST_H265_PARSER_OK)
    return get_status (result);

  priv->parser_state |= GST_H265_VIDEO_STATE_GOT_VPS;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_sps (GstVaapiDecoderH265 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const pi = unit->parsed_info;
  GstH265SPS *const sps = &pi->data.sps;
  GstH265ParserResult result;

  GST_DEBUG ("parse SPS");
  priv->parser_state = 0;

  memset (sps, 0, sizeof (GstH265SPS));

  result = gst_h265_parser_parse_sps (priv->parser, &pi->nalu, sps, TRUE);
  if (result != GST_H265_PARSER_OK)
    return get_status (result);

  priv->parser_state |= GST_H265_VIDEO_STATE_GOT_SPS;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_pps (GstVaapiDecoderH265 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const pi = unit->parsed_info;
  GstH265PPS *const pps = &pi->data.pps;
  GstH265ParserResult result;

  GST_DEBUG ("parse PPS");
  priv->parser_state &= GST_H265_VIDEO_STATE_GOT_SPS;

  memset (pps, 0, sizeof (GstH265PPS));

  result = gst_h265_parser_parse_pps (priv->parser, &pi->nalu, pps);
  if (result != GST_H265_PARSER_OK)
    return get_status (result);

  priv->parser_state |= GST_H265_VIDEO_STATE_GOT_PPS;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_sei (GstVaapiDecoderH265 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const pi = unit->parsed_info;
  GArray **const sei_ptr = &pi->data.sei;
  GstH265ParserResult result;

  GST_DEBUG ("parse SEI");

  result = gst_h265_parser_parse_sei (priv->parser, &pi->nalu, sei_ptr);
  if (result != GST_H265_PARSER_OK) {
    GST_WARNING ("failed to parse SEI messages");
    return get_status (result);
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_slice (GstVaapiDecoderH265 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const pi = unit->parsed_info;
  GstH265SliceHdr *const slice_hdr = &pi->data.slice_hdr;
  GstH265ParserResult result;

  GST_DEBUG ("parse slice");
  priv->parser_state &= (GST_H265_VIDEO_STATE_GOT_SPS |
      GST_H265_VIDEO_STATE_GOT_PPS);

  slice_hdr->short_term_ref_pic_set_idx = 0;

  memset (slice_hdr, 0, sizeof (GstH265SliceHdr));

  result = gst_h265_parser_parse_slice_hdr (priv->parser, &pi->nalu, slice_hdr);
  if (result != GST_H265_PARSER_OK)
    return get_status (result);

  priv->parser_state |= GST_H265_VIDEO_STATE_GOT_SLICE;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_vps (GstVaapiDecoderH265 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const pi = unit->parsed_info;
  GstH265VPS *const vps = &pi->data.vps;

  GST_DEBUG ("decode VPS");

  gst_vaapi_parser_info_h265_replace (&priv->vps[vps->id], pi);

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sps (GstVaapiDecoderH265 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const pi = unit->parsed_info;
  GstH265SPS *const sps = &pi->data.sps;
  guint high_precision_offsets_enabled_flag = 0, bitdepthC = 0;

  GST_DEBUG ("decode SPS");

  if (sps->max_latency_increase_plus1[sps->max_sub_layers_minus1])
    priv->SpsMaxLatencyPictures =
        sps->max_num_reorder_pics[sps->max_sub_layers_minus1] +
        sps->max_latency_increase_plus1[sps->max_sub_layers_minus1] - 1;

  /* Calculate WpOffsetHalfRangeC: (7-34)
   * FIXME: We don't have parser API for sps_range_extension, so
   * assuming high_precision_offsets_enabled_flag as zero */
  bitdepthC = sps->bit_depth_chroma_minus8 + 8;
  priv->WpOffsetHalfRangeC =
      1 << (high_precision_offsets_enabled_flag ? (bitdepthC - 1) : 7);

  gst_vaapi_parser_info_h265_replace (&priv->sps[sps->id], pi);

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_pps (GstVaapiDecoderH265 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const pi = unit->parsed_info;
  GstH265PPS *const pps = &pi->data.pps;

  GST_DEBUG ("decode PPS");

  gst_vaapi_parser_info_h265_replace (&priv->pps[pps->id], pi);

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sei (GstVaapiDecoderH265 * decoder, GstVaapiDecoderUnit * unit)
{

  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const pi = unit->parsed_info;
  guint i;

  GST_DEBUG ("decode SEI messages");

  for (i = 0; i < pi->data.sei->len; i++) {
    const GstH265SEIMessage *const sei =
        &g_array_index (pi->data.sei, GstH265SEIMessage, i);

    switch (sei->payloadType) {
      case GST_H265_SEI_PIC_TIMING:{
        const GstH265PicTiming *const pic_timing = &sei->payload.pic_timing;
        priv->pic_structure = pic_timing->pic_struct;
        break;
      }
      default:
        break;
    }
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sequence_end (GstVaapiDecoderH265 * decoder)
{
  GstVaapiDecoderStatus status;
  GstVaapiParserInfoH265 *const sps_pi = decoder->priv.active_sps;

  GST_DEBUG ("decode sequence-end");

  /* Sequence ended, don't try to propagate "got I-frame" state beyond
   * this point */
  if (sps_pi)
    sps_pi->state &= ~GST_H265_VIDEO_STATE_GOT_I_FRAME;

  status = decode_current_picture (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

/* 8.3.1 - Decoding process for picture order count */
static void
init_picture_poc (GstVaapiDecoderH265 * decoder,
    GstVaapiPictureH265 * picture, GstVaapiParserInfoH265 * pi)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstH265SliceHdr *const slice_hdr = &pi->data.slice_hdr;
  GstH265SPS *const sps = get_sps (decoder);
  const gint32 MaxPicOrderCntLsb =
      1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
  guint8 nal_type = pi->nalu.type;
  guint8 temporal_id = pi->nalu.temporal_id_plus1 - 1;

  GST_DEBUG ("decode PicOrderCntVal");

  priv->prev_poc_lsb = priv->poc_lsb;
  priv->prev_poc_msb = priv->poc_msb;

  if (!(nal_is_irap (nal_type) && picture->NoRaslOutputFlag)) {
    priv->prev_poc_lsb = priv->prev_tid0pic_poc_lsb;
    priv->prev_poc_msb = priv->prev_tid0pic_poc_msb;
  }

  /* Finding PicOrderCntMsb */
  if (nal_is_irap (nal_type) && picture->NoRaslOutputFlag)
    priv->poc_msb = 0;
  else {
    /* (8-1) */
    if ((slice_hdr->pic_order_cnt_lsb < priv->prev_poc_lsb) &&
        ((priv->prev_poc_lsb - slice_hdr->pic_order_cnt_lsb) >=
            (MaxPicOrderCntLsb / 2)))
      priv->poc_msb = priv->prev_poc_msb + MaxPicOrderCntLsb;

    else if ((slice_hdr->pic_order_cnt_lsb > priv->prev_poc_lsb) &&
        ((slice_hdr->pic_order_cnt_lsb - priv->prev_poc_lsb) >
            (MaxPicOrderCntLsb / 2)))
      priv->poc_msb = priv->prev_poc_msb - MaxPicOrderCntLsb;

    else
      priv->poc_msb = priv->prev_poc_msb;
  }

  /* (8-2) */
  priv->poc = picture->poc = priv->poc_msb + slice_hdr->pic_order_cnt_lsb;
  priv->poc_lsb = picture->poc_lsb = slice_hdr->pic_order_cnt_lsb;

  if (nal_is_idr (nal_type)) {
    picture->poc = 0;
    picture->poc_lsb = 0;
    priv->poc_lsb = 0;
    priv->poc_msb = 0;
    priv->prev_poc_lsb = 0;
    priv->prev_poc_msb = 0;
    priv->prev_tid0pic_poc_lsb = 0;
    priv->prev_tid0pic_poc_msb = 0;
  }

  picture->base.poc = picture->poc;
  GST_DEBUG ("PicOrderCntVal %d", picture->base.poc);

  if (!temporal_id && !nal_is_rasl (nal_type) &&
      !nal_is_radl (nal_type) && nal_is_ref (nal_type)) {
    priv->prev_tid0pic_poc_lsb = slice_hdr->pic_order_cnt_lsb;
    priv->prev_tid0pic_poc_msb = priv->poc_msb;
  }
}

static void
init_picture_refs (GstVaapiDecoderH265 * decoder,
    GstVaapiPictureH265 * picture, GstH265SliceHdr * slice_hdr)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  guint32 NumRpsCurrTempList0 = 0, NumRpsCurrTempList1 = 0;
  GstVaapiPictureH265 *RefPicListTemp0[16] = { NULL, };
  GstVaapiPictureH265 *RefPicListTemp1[16] = { NULL, };
  guint i, rIdx = 0;
  guint num_ref_idx_l0_active_minus1 = 0;
  guint num_ref_idx_l1_active_minus1 = 0;
  GstH265RefPicListModification *ref_pic_list_modification;
  GstH265PPS *const pps = get_pps (decoder);
  guint type;

  memset (priv->RefPicList0, 0, sizeof (GstVaapiPictureH265 *) * 16);
  memset (priv->RefPicList1, 0, sizeof (GstVaapiPictureH265 *) * 16);
  priv->RefPicList0_count = priv->RefPicList1_count = 0;

  num_ref_idx_l0_active_minus1 = slice_hdr->num_ref_idx_l0_active_minus1;
  num_ref_idx_l1_active_minus1 = slice_hdr->num_ref_idx_l1_active_minus1;
  ref_pic_list_modification = &slice_hdr->ref_pic_list_modification;
  type = slice_hdr->type;

  /* decoding process for reference picture list construction needs to be
   * invoked only for P and B slice */
  if (type == GST_H265_I_SLICE)
    return;

  NumRpsCurrTempList0 =
      MAX ((num_ref_idx_l0_active_minus1 + 1), priv->NumPocTotalCurr);
  NumRpsCurrTempList1 =
      MAX ((num_ref_idx_l1_active_minus1 + 1), priv->NumPocTotalCurr);

  /* (8-8) */
  while (rIdx < NumRpsCurrTempList0) {
    for (i = 0; i < priv->NumPocStCurrBefore && rIdx < NumRpsCurrTempList0;
        rIdx++, i++)
      RefPicListTemp0[rIdx] = priv->RefPicSetStCurrBefore[i];
    for (i = 0; i < priv->NumPocStCurrAfter && rIdx < NumRpsCurrTempList0;
        rIdx++, i++)
      RefPicListTemp0[rIdx] = priv->RefPicSetStCurrAfter[i];
    for (i = 0; i < priv->NumPocLtCurr && rIdx < NumRpsCurrTempList0;
        rIdx++, i++)
      RefPicListTemp0[rIdx] = priv->RefPicSetLtCurr[i];
    if (pps->pps_scc_extension_params.pps_curr_pic_ref_enabled_flag)
      RefPicListTemp0[rIdx++] = picture;
  }

  /* construct RefPicList0 (8-9) */
  for (rIdx = 0; rIdx <= num_ref_idx_l0_active_minus1; rIdx++)
    priv->RefPicList0[rIdx] =
        ref_pic_list_modification->ref_pic_list_modification_flag_l0 ?
        RefPicListTemp0[ref_pic_list_modification->list_entry_l0[rIdx]] :
        RefPicListTemp0[rIdx];
  if (pps->pps_scc_extension_params.pps_curr_pic_ref_enabled_flag
      && !ref_pic_list_modification->ref_pic_list_modification_flag_l0
      && (NumRpsCurrTempList0 > num_ref_idx_l0_active_minus1 + 1))
    priv->RefPicList0[num_ref_idx_l0_active_minus1] = picture;
  priv->RefPicList0_count = rIdx;

  if (type == GST_H265_B_SLICE) {
    rIdx = 0;

    /* (8-10) */
    while (rIdx < NumRpsCurrTempList1) {
      for (i = 0; i < priv->NumPocStCurrAfter && rIdx < NumRpsCurrTempList1;
          rIdx++, i++)
        RefPicListTemp1[rIdx] = priv->RefPicSetStCurrAfter[i];
      for (i = 0; i < priv->NumPocStCurrBefore && rIdx < NumRpsCurrTempList1;
          rIdx++, i++)
        RefPicListTemp1[rIdx] = priv->RefPicSetStCurrBefore[i];
      for (i = 0; i < priv->NumPocLtCurr && rIdx < NumRpsCurrTempList1;
          rIdx++, i++)
        RefPicListTemp1[rIdx] = priv->RefPicSetLtCurr[i];
      if (pps->pps_scc_extension_params.pps_curr_pic_ref_enabled_flag)
        RefPicListTemp1[rIdx++] = picture;
    }

    /* construct RefPicList1 (8-10) */
    for (rIdx = 0; rIdx <= num_ref_idx_l1_active_minus1; rIdx++)
      priv->RefPicList1[rIdx] =
          ref_pic_list_modification->ref_pic_list_modification_flag_l1 ?
          RefPicListTemp1[ref_pic_list_modification->list_entry_l1
          [rIdx]] : RefPicListTemp1[rIdx];
    priv->RefPicList1_count = rIdx;
  }
}

static gboolean
init_picture (GstVaapiDecoderH265 * decoder,
    GstVaapiPictureH265 * picture, GstVaapiParserInfoH265 * pi)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiPicture *const base_picture = &picture->base;
  GstH265SliceHdr *const slice_hdr = &pi->data.slice_hdr;

  base_picture->pts = GST_VAAPI_DECODER_CODEC_FRAME (decoder)->pts;
  base_picture->type = GST_VAAPI_PICTURE_TYPE_NONE;

  if (nal_is_idr (pi->nalu.type)) {
    GST_DEBUG ("<IDR>");
    GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_IDR);
  }

  if (pi->nalu.type >= GST_H265_NAL_SLICE_BLA_W_LP &&
      pi->nalu.type <= GST_H265_NAL_SLICE_CRA_NUT)
    picture->RapPicFlag = TRUE;

  /* FIXME: Use SEI header values */
  base_picture->structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
  picture->structure = base_picture->structure;

  /*NoRaslOutputFlag ==1 if the current picture is
     1) an IDR picture
     2) a BLA picture
     3) a CRA picture that is the first access unit in the bitstream
     4) first picture that follows an end of sequence NAL unit in decoding order
     5) has HandleCraAsBlaFlag == 1 (set by external means, so not considering )
   */
  if (nal_is_idr (pi->nalu.type) || nal_is_bla (pi->nalu.type) ||
      (nal_is_cra (pi->nalu.type) && priv->new_bitstream)
      || priv->prev_nal_is_eos) {
    picture->NoRaslOutputFlag = 1;
  }

  if (nal_is_irap (pi->nalu.type)) {
    picture->IntraPicFlag = TRUE;
    priv->associated_irap_NoRaslOutputFlag = picture->NoRaslOutputFlag;
  }

  if (nal_is_rasl (pi->nalu.type) && priv->associated_irap_NoRaslOutputFlag)
    picture->output_flag = FALSE;
  else
    picture->output_flag = slice_hdr->pic_output_flag;

  init_picture_poc (decoder, picture, pi);

  return TRUE;
}

static void
vaapi_init_picture (VAPictureHEVC * pic)
{
  pic->picture_id = VA_INVALID_SURFACE;
  pic->pic_order_cnt = 0;
  pic->flags = VA_PICTURE_HEVC_INVALID;
}

static void
vaapi_fill_picture (VAPictureHEVC * pic, GstVaapiPictureH265 * picture,
    guint picture_structure)
{

  if (!picture_structure)
    picture_structure = picture->structure;

  pic->picture_id = picture->base.surface_id;
  pic->pic_order_cnt = picture->poc;
  pic->flags = 0;

  /* Set the VAPictureHEVC flags */
  if (GST_VAAPI_PICTURE_IS_LONG_TERM_REFERENCE (picture))
    pic->flags |= VA_PICTURE_HEVC_LONG_TERM_REFERENCE;

  if (GST_VAAPI_PICTURE_FLAG_IS_SET (picture,
          GST_VAAPI_PICTURE_FLAG_RPS_ST_CURR_BEFORE))
    pic->flags |= VA_PICTURE_HEVC_RPS_ST_CURR_BEFORE;

  else if (GST_VAAPI_PICTURE_FLAG_IS_SET (picture,
          GST_VAAPI_PICTURE_FLAG_RPS_ST_CURR_AFTER))
    pic->flags |= VA_PICTURE_HEVC_RPS_ST_CURR_AFTER;

  else if (GST_VAAPI_PICTURE_FLAG_IS_SET (picture,
          GST_VAAPI_PICTURE_FLAG_RPS_LT_CURR))
    pic->flags |= VA_PICTURE_HEVC_RPS_LT_CURR;

  switch (picture_structure) {
    case GST_VAAPI_PICTURE_STRUCTURE_FRAME:
      break;
    case GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD:
      pic->flags |= VA_PICTURE_HEVC_FIELD_PIC;
      break;
    case GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD:
      pic->flags |= VA_PICTURE_HEVC_FIELD_PIC;
      pic->flags |= VA_PICTURE_HEVC_BOTTOM_FIELD;
      break;
    default:
      break;
  }
}

static guint
get_index_for_RefPicListX (VAPictureHEVC * ReferenceFrames,
    GstVaapiPictureH265 * pic)
{
  gint i;

  for (i = 0; i < 15; i++) {
    if ((ReferenceFrames[i].picture_id != VA_INVALID_ID) && pic) {
      if ((ReferenceFrames[i].pic_order_cnt == pic->poc) &&
          (ReferenceFrames[i].picture_id == pic->base.surface_id)) {
        return i;
      }
    }
  }
  return 0xff;
}

static gboolean
fill_picture (GstVaapiDecoderH265 * decoder, GstVaapiPictureH265 * picture,
    GstVaapiParserInfoH265 * pi)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiPicture *const base_picture = &picture->base;
  GstH265SliceHdr *const slice_hdr = &pi->data.slice_hdr;
  GstH265PPS *const pps = get_pps (decoder);
  GstH265SPS *const sps = get_sps (decoder);
  VAPictureParameterBufferHEVC *pic_param = base_picture->param;
  guint i, n;

#if VA_CHECK_VERSION(1,2,0)
  VAPictureParameterBufferHEVCRext *pic_rext_param = NULL;
  VAPictureParameterBufferHEVCScc *pic_scc_param = NULL;
  if (is_range_extension_profile (priv->profile)) {
    VAPictureParameterBufferHEVCExtension *param = base_picture->param;
    pic_param = &param->base;
    pic_rext_param = &param->rext;
  }
  if (is_scc_profile (priv->profile)) {
    VAPictureParameterBufferHEVCExtension *param = base_picture->param;
    pic_param = &param->base;
    pic_rext_param = &param->rext;
    pic_scc_param = &param->scc;
  }
#endif

  pic_param->pic_fields.value = 0;
  pic_param->slice_parsing_fields.value = 0;

  /* Fill in VAPictureHEVC */
  vaapi_fill_picture (&pic_param->CurrPic, picture, 0);
  /* Fill in ReferenceFrames */
  for (i = 0, n = 0; i < priv->dpb_count; i++) {
    GstVaapiFrameStore *const fs = priv->dpb[i];
    if ((gst_vaapi_frame_store_has_reference (fs)))
      vaapi_fill_picture (&pic_param->ReferenceFrames[n++], fs->buffer,
          fs->buffer->structure);
    if (n >= G_N_ELEMENTS (pic_param->ReferenceFrames))
      break;
  }
  /* 7.4.3.3.3, the current decoded picture is marked as "used for
     long-term reference", no matter TwoVersionsOfCurrDecPicFlag */
  if (pps->pps_scc_extension_params.pps_curr_pic_ref_enabled_flag
      && n < G_N_ELEMENTS (pic_param->ReferenceFrames) - 1) {
    gst_vaapi_picture_h265_set_reference (picture,
        GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE);
    vaapi_fill_picture (&pic_param->ReferenceFrames[n++], picture,
        picture->structure);
    gst_vaapi_picture_h265_set_reference (picture, 0);
  }

  for (; n < G_N_ELEMENTS (pic_param->ReferenceFrames); n++)
    vaapi_init_picture (&pic_param->ReferenceFrames[n]);


#define COPY_FIELD(s, f) \
    pic_param->f = (s)->f
#define COPY_BFM(a, s, f) \
    pic_param->a.bits.f = (s)->f

  COPY_FIELD (sps, pic_width_in_luma_samples);
  COPY_FIELD (sps, pic_height_in_luma_samples);
  COPY_BFM (pic_fields, sps, chroma_format_idc);
  COPY_BFM (pic_fields, sps, separate_colour_plane_flag);
  COPY_BFM (pic_fields, sps, pcm_enabled_flag);
  COPY_BFM (pic_fields, sps, scaling_list_enabled_flag);
  COPY_BFM (pic_fields, pps, transform_skip_enabled_flag);
  COPY_BFM (pic_fields, sps, amp_enabled_flag);
  COPY_BFM (pic_fields, sps, strong_intra_smoothing_enabled_flag);
  COPY_BFM (pic_fields, pps, sign_data_hiding_enabled_flag);
  COPY_BFM (pic_fields, pps, constrained_intra_pred_flag);
  COPY_BFM (pic_fields, pps, cu_qp_delta_enabled_flag);
  COPY_BFM (pic_fields, pps, weighted_pred_flag);
  COPY_BFM (pic_fields, pps, weighted_bipred_flag);
  COPY_BFM (pic_fields, pps, transquant_bypass_enabled_flag);
  COPY_BFM (pic_fields, pps, tiles_enabled_flag);
  COPY_BFM (pic_fields, pps, entropy_coding_sync_enabled_flag);
  pic_param->pic_fields.bits.pps_loop_filter_across_slices_enabled_flag =
      pps->loop_filter_across_slices_enabled_flag;
  COPY_BFM (pic_fields, pps, loop_filter_across_tiles_enabled_flag);
  COPY_BFM (pic_fields, sps, pcm_loop_filter_disabled_flag);
  /* Fix: Assign value based on sps_max_num_reorder_pics */
  pic_param->pic_fields.bits.NoPicReorderingFlag = 0;
  /* Fix: Enable if picture has no B slices */
  pic_param->pic_fields.bits.NoBiPredFlag = 0;

  pic_param->sps_max_dec_pic_buffering_minus1 =
      sps->max_dec_pic_buffering_minus1[0];
  COPY_FIELD (sps, bit_depth_luma_minus8);
  COPY_FIELD (sps, bit_depth_chroma_minus8);
  COPY_FIELD (sps, pcm_sample_bit_depth_luma_minus1);
  COPY_FIELD (sps, pcm_sample_bit_depth_chroma_minus1);
  COPY_FIELD (sps, log2_min_luma_coding_block_size_minus3);
  COPY_FIELD (sps, log2_diff_max_min_luma_coding_block_size);
  COPY_FIELD (sps, log2_min_transform_block_size_minus2);
  COPY_FIELD (sps, log2_diff_max_min_transform_block_size);
  COPY_FIELD (sps, log2_min_pcm_luma_coding_block_size_minus3);
  COPY_FIELD (sps, log2_diff_max_min_pcm_luma_coding_block_size);
  COPY_FIELD (sps, max_transform_hierarchy_depth_intra);
  COPY_FIELD (sps, max_transform_hierarchy_depth_inter);
  COPY_FIELD (pps, init_qp_minus26);
  COPY_FIELD (pps, diff_cu_qp_delta_depth);
  pic_param->pps_cb_qp_offset = pps->cb_qp_offset;
  pic_param->pps_cr_qp_offset = pps->cr_qp_offset;
  COPY_FIELD (pps, log2_parallel_merge_level_minus2);
  COPY_FIELD (pps, num_tile_columns_minus1);
  COPY_FIELD (pps, num_tile_rows_minus1);
  for (i = 0; i <= pps->num_tile_columns_minus1; i++)
    pic_param->column_width_minus1[i] = pps->column_width_minus1[i];
  for (; i < 19; i++)
    pic_param->column_width_minus1[i] = 0;
  for (i = 0; i <= pps->num_tile_rows_minus1; i++)
    pic_param->row_height_minus1[i] = pps->row_height_minus1[i];
  for (; i < 21; i++)
    pic_param->row_height_minus1[i] = 0;

  COPY_BFM (slice_parsing_fields, pps, lists_modification_present_flag);
  COPY_BFM (slice_parsing_fields, sps, long_term_ref_pics_present_flag);
  pic_param->slice_parsing_fields.bits.sps_temporal_mvp_enabled_flag =
      sps->temporal_mvp_enabled_flag;
  COPY_BFM (slice_parsing_fields, pps, cabac_init_present_flag);
  COPY_BFM (slice_parsing_fields, pps, output_flag_present_flag);
  COPY_BFM (slice_parsing_fields, pps, dependent_slice_segments_enabled_flag);
  pic_param->slice_parsing_fields.bits.
      pps_slice_chroma_qp_offsets_present_flag =
      pps->slice_chroma_qp_offsets_present_flag;
  COPY_BFM (slice_parsing_fields, sps, sample_adaptive_offset_enabled_flag);
  COPY_BFM (slice_parsing_fields, pps, deblocking_filter_override_enabled_flag);
  pic_param->slice_parsing_fields.bits.pps_disable_deblocking_filter_flag =
      pps->deblocking_filter_disabled_flag;
  COPY_BFM (slice_parsing_fields, pps,
      slice_segment_header_extension_present_flag);
  pic_param->slice_parsing_fields.bits.RapPicFlag = picture->RapPicFlag;
  pic_param->slice_parsing_fields.bits.IdrPicFlag =
      GST_VAAPI_PICTURE_FLAG_IS_SET (picture, GST_VAAPI_PICTURE_FLAG_IDR);
  pic_param->slice_parsing_fields.bits.IntraPicFlag = picture->IntraPicFlag;

  COPY_FIELD (sps, log2_max_pic_order_cnt_lsb_minus4);
  COPY_FIELD (sps, num_short_term_ref_pic_sets);
  pic_param->num_long_term_ref_pic_sps = sps->num_long_term_ref_pics_sps;
  COPY_FIELD (pps, num_ref_idx_l0_default_active_minus1);
  COPY_FIELD (pps, num_ref_idx_l1_default_active_minus1);
  pic_param->pps_beta_offset_div2 = pps->beta_offset_div2;
  pic_param->pps_tc_offset_div2 = pps->tc_offset_div2;
  COPY_FIELD (pps, num_extra_slice_header_bits);

  if (slice_hdr->short_term_ref_pic_set_sps_flag == 0)
    pic_param->st_rps_bits = slice_hdr->short_term_ref_pic_set_size;
  else
    pic_param->st_rps_bits = 0;

#if VA_CHECK_VERSION(1,2,0)
  if (pic_rext_param) {
    pic_rext_param->range_extension_pic_fields.value = 0;

#define COPY_REXT_FIELD(s, f) \
		pic_rext_param->f = s.f
#define COPY_REXT_BFM(a, s, f) \
		pic_rext_param->a.bits.f = s.f

    COPY_REXT_BFM (range_extension_pic_fields, sps->sps_extension_params,
        transform_skip_rotation_enabled_flag);
    COPY_REXT_BFM (range_extension_pic_fields, sps->sps_extension_params,
        transform_skip_context_enabled_flag);
    COPY_REXT_BFM (range_extension_pic_fields, sps->sps_extension_params,
        implicit_rdpcm_enabled_flag);
    COPY_REXT_BFM (range_extension_pic_fields, sps->sps_extension_params,
        explicit_rdpcm_enabled_flag);
    COPY_REXT_BFM (range_extension_pic_fields, sps->sps_extension_params,
        extended_precision_processing_flag);
    COPY_REXT_BFM (range_extension_pic_fields, sps->sps_extension_params,
        intra_smoothing_disabled_flag);
    COPY_REXT_BFM (range_extension_pic_fields, sps->sps_extension_params,
        high_precision_offsets_enabled_flag);
    COPY_REXT_BFM (range_extension_pic_fields, sps->sps_extension_params,
        persistent_rice_adaptation_enabled_flag);
    COPY_REXT_BFM (range_extension_pic_fields, sps->sps_extension_params,
        cabac_bypass_alignment_enabled_flag);

    COPY_REXT_BFM (range_extension_pic_fields, pps->pps_extension_params,
        cross_component_prediction_enabled_flag);
    COPY_REXT_BFM (range_extension_pic_fields, pps->pps_extension_params,
        chroma_qp_offset_list_enabled_flag);

    COPY_REXT_FIELD (pps->pps_extension_params, diff_cu_chroma_qp_offset_depth);
    COPY_REXT_FIELD (pps->pps_extension_params,
        chroma_qp_offset_list_len_minus1);
    COPY_REXT_FIELD (pps->pps_extension_params, log2_sao_offset_scale_luma);
    COPY_REXT_FIELD (pps->pps_extension_params, log2_sao_offset_scale_chroma);
    COPY_REXT_FIELD (pps->pps_extension_params,
        log2_max_transform_skip_block_size_minus2);

    memcpy (pic_rext_param->cb_qp_offset_list,
        pps->pps_extension_params.cb_qp_offset_list,
        sizeof (pic_rext_param->cb_qp_offset_list));
    memcpy (pic_rext_param->cr_qp_offset_list,
        pps->pps_extension_params.cr_qp_offset_list,
        sizeof (pic_rext_param->cr_qp_offset_list));
  }

  if (pic_scc_param) {
#define COPY_SCC_FIELD(s, f) \
    pic_scc_param->f = s->f
#define COPY_SCC_BFM(a, s, f) \
    pic_scc_param->a.bits.f = s->f

    const GstH265PPSSccExtensionParams *pps_scc =
        &pps->pps_scc_extension_params;
    const GstH265SPSSccExtensionParams *sps_scc =
        &sps->sps_scc_extension_params;
    guint32 num_comps;

    pic_scc_param->screen_content_pic_fields.value = 0;

    COPY_SCC_BFM (screen_content_pic_fields, pps_scc,
        pps_curr_pic_ref_enabled_flag);
    COPY_SCC_BFM (screen_content_pic_fields, sps_scc,
        palette_mode_enabled_flag);
    COPY_SCC_BFM (screen_content_pic_fields, sps_scc,
        motion_vector_resolution_control_idc);
    COPY_SCC_BFM (screen_content_pic_fields, sps_scc,
        intra_boundary_filtering_disabled_flag);
    COPY_SCC_BFM (screen_content_pic_fields, pps_scc,
        residual_adaptive_colour_transform_enabled_flag);
    COPY_SCC_BFM (screen_content_pic_fields, pps_scc,
        pps_slice_act_qp_offsets_present_flag);

    COPY_SCC_FIELD (sps_scc, palette_max_size);
    COPY_SCC_FIELD (sps_scc, delta_palette_max_predictor_size);
    COPY_SCC_FIELD (pps_scc, pps_act_y_qp_offset_plus5);
    COPY_SCC_FIELD (pps_scc, pps_act_cb_qp_offset_plus5);
    COPY_SCC_FIELD (pps_scc, pps_act_cr_qp_offset_plus3);

    /* firstly use the pps, then sps */
    num_comps = sps->chroma_format_idc ? 3 : 1;

    if (pps_scc->pps_palette_predictor_initializers_present_flag) {
      pic_scc_param->predictor_palette_size =
          pps_scc->pps_num_palette_predictor_initializer;
      for (n = 0; n < num_comps; n++)
        for (i = 0; i < pps_scc->pps_num_palette_predictor_initializer; i++)
          pic_scc_param->predictor_palette_entries[n][i] =
              (uint16_t) pps_scc->pps_palette_predictor_initializer[n][i];
    } else if (sps_scc->sps_palette_predictor_initializers_present_flag) {
      pic_scc_param->predictor_palette_size =
          sps_scc->sps_num_palette_predictor_initializer_minus1 + 1;
      for (n = 0; n < num_comps; n++)
        for (i = 0;
            i < sps_scc->sps_num_palette_predictor_initializer_minus1 + 1; i++)
          pic_scc_param->predictor_palette_entries[n][i] =
              (uint16_t) sps_scc->sps_palette_predictor_initializer[n][i];
    }
  }
#endif
  return TRUE;
}

/* Detection of the first VCL NAL unit of a coded picture (7.4.2.4.5 ) */
static gboolean
is_new_picture (GstVaapiParserInfoH265 * pi, GstVaapiParserInfoH265 * prev_pi)
{
  GstH265SliceHdr *const slice_hdr = &pi->data.slice_hdr;

  if (!prev_pi)
    return TRUE;

  if (slice_hdr->first_slice_segment_in_pic_flag)
    return TRUE;

  return FALSE;
}

/* Detection of a new access unit, assuming we are already in presence
   of a new picture */
static inline gboolean
is_new_access_unit (GstVaapiParserInfoH265 * pi,
    GstVaapiParserInfoH265 * prev_pi)
{
  if (!prev_pi)
    return TRUE;

  return FALSE;
}

static gboolean
has_entry_in_rps (GstVaapiPictureH265 * dpb_pic,
    GstVaapiPictureH265 ** rps_list, guint rps_list_length)
{
  guint i;

  if (!dpb_pic || !rps_list || !rps_list_length)
    return FALSE;

  for (i = 0; i < rps_list_length; i++) {
    if (rps_list[i] && rps_list[i]->poc == dpb_pic->poc)
      return TRUE;
  }
  return FALSE;
}

/* the derivation process for the RPS and the picture marking */
static void
derive_and_mark_rps (GstVaapiDecoderH265 * decoder,
    GstVaapiPictureH265 * picture, GstVaapiParserInfoH265 * pi,
    gint32 * CurrDeltaPocMsbPresentFlag, gint32 * FollDeltaPocMsbPresentFlag)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiPictureH265 *dpb_pic = NULL;
  guint i;

  memset (priv->RefPicSetLtCurr, 0, sizeof (GstVaapiPictureH265 *) * 16);
  memset (priv->RefPicSetLtFoll, 0, sizeof (GstVaapiPictureH265 *) * 16);
  memset (priv->RefPicSetStCurrBefore, 0, sizeof (GstVaapiPictureH265 *) * 16);
  memset (priv->RefPicSetStCurrAfter, 0, sizeof (GstVaapiPictureH265 *) * 16);
  memset (priv->RefPicSetStFoll, 0, sizeof (GstVaapiPictureH265 *) * 16);

  /* (8-6) */
  for (i = 0; i < priv->NumPocLtCurr; i++) {
    if (!CurrDeltaPocMsbPresentFlag[i]) {
      dpb_pic = dpb_get_picture (decoder, priv->PocLtCurr[i], TRUE);
      if (dpb_pic)
        priv->RefPicSetLtCurr[i] = dpb_pic;
      else
        priv->RefPicSetLtCurr[i] = NULL;
    } else {
      dpb_pic = dpb_get_picture (decoder, priv->PocLtCurr[i], FALSE);
      if (dpb_pic)
        priv->RefPicSetLtCurr[i] = dpb_pic;
      else
        priv->RefPicSetLtCurr[i] = NULL;
    }
  }
  for (; i < 16; i++)
    priv->RefPicSetLtCurr[i] = NULL;

  for (i = 0; i < priv->NumPocLtFoll; i++) {
    if (!FollDeltaPocMsbPresentFlag[i]) {
      dpb_pic = dpb_get_picture (decoder, priv->PocLtFoll[i], TRUE);
      if (dpb_pic)
        priv->RefPicSetLtFoll[i] = dpb_pic;
      else
        priv->RefPicSetLtFoll[i] = NULL;
    } else {
      dpb_pic = dpb_get_picture (decoder, priv->PocLtFoll[i], FALSE);
      if (dpb_pic)
        priv->RefPicSetLtFoll[i] = dpb_pic;
      else
        priv->RefPicSetLtFoll[i] = NULL;
    }
  }
  for (; i < 16; i++)
    priv->RefPicSetLtFoll[i] = NULL;

  /* Mark all ref pics in RefPicSetLtCurr and RefPicSetLtFol as long_term_refs */
  for (i = 0; i < priv->NumPocLtCurr; i++) {
    if (priv->RefPicSetLtCurr[i])
      gst_vaapi_picture_h265_set_reference (priv->RefPicSetLtCurr[i],
          GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE |
          GST_VAAPI_PICTURE_FLAG_RPS_LT_CURR);
  }
  for (i = 0; i < priv->NumPocLtFoll; i++) {
    if (priv->RefPicSetLtFoll[i])
      gst_vaapi_picture_h265_set_reference (priv->RefPicSetLtFoll[i],
          GST_VAAPI_PICTURE_FLAG_LONG_TERM_REFERENCE |
          GST_VAAPI_PICTURE_FLAG_RPS_LT_FOLL);
  }

  /* (8-7) */
  for (i = 0; i < priv->NumPocStCurrBefore; i++) {
    dpb_pic = dpb_get_ref_picture (decoder, priv->PocStCurrBefore[i], TRUE);
    if (dpb_pic) {
      gst_vaapi_picture_h265_set_reference (dpb_pic,
          GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE |
          GST_VAAPI_PICTURE_FLAG_RPS_ST_CURR_BEFORE);
      priv->RefPicSetStCurrBefore[i] = dpb_pic;
    } else
      priv->RefPicSetStCurrBefore[i] = NULL;
  }
  for (; i < 16; i++)
    priv->RefPicSetStCurrBefore[i] = NULL;

  for (i = 0; i < priv->NumPocStCurrAfter; i++) {
    dpb_pic = dpb_get_ref_picture (decoder, priv->PocStCurrAfter[i], TRUE);
    if (dpb_pic) {
      gst_vaapi_picture_h265_set_reference (dpb_pic,
          GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE |
          GST_VAAPI_PICTURE_FLAG_RPS_ST_CURR_AFTER);
      priv->RefPicSetStCurrAfter[i] = dpb_pic;
    } else
      priv->RefPicSetStCurrAfter[i] = NULL;
  }
  for (; i < 16; i++)
    priv->RefPicSetStCurrAfter[i] = NULL;

  for (i = 0; i < priv->NumPocStFoll; i++) {
    dpb_pic = dpb_get_ref_picture (decoder, priv->PocStFoll[i], TRUE);
    if (dpb_pic) {
      gst_vaapi_picture_h265_set_reference (dpb_pic,
          GST_VAAPI_PICTURE_FLAG_SHORT_TERM_REFERENCE |
          GST_VAAPI_PICTURE_FLAG_RPS_ST_FOLL);
      priv->RefPicSetStFoll[i] = dpb_pic;
    } else
      priv->RefPicSetStFoll[i] = NULL;
  }
  for (; i < 16; i++)
    priv->RefPicSetStFoll[i] = NULL;

  /* Mark all dpb pics not beloging to RefPicSet*[] as unused for ref */
  for (i = 0; i < priv->dpb_count; i++) {
    dpb_pic = priv->dpb[i]->buffer;
    if (dpb_pic &&
        !has_entry_in_rps (dpb_pic, priv->RefPicSetLtCurr, priv->NumPocLtCurr)
        && !has_entry_in_rps (dpb_pic, priv->RefPicSetLtFoll,
            priv->NumPocLtFoll)
        && !has_entry_in_rps (dpb_pic, priv->RefPicSetStCurrAfter,
            priv->NumPocStCurrAfter)
        && !has_entry_in_rps (dpb_pic, priv->RefPicSetStCurrBefore,
            priv->NumPocStCurrBefore)
        && !has_entry_in_rps (dpb_pic, priv->RefPicSetStFoll,
            priv->NumPocStFoll))
      gst_vaapi_picture_h265_set_reference (dpb_pic, 0);
  }

}

/* Decoding process for reference picture set (8.3.2) */
static gboolean
decode_ref_pic_set (GstVaapiDecoderH265 * decoder,
    GstVaapiPictureH265 * picture, GstVaapiParserInfoH265 * pi)
{
  guint i, j, k;
  gint32 CurrDeltaPocMsbPresentFlag[16] = { 0, };
  gint32 FollDeltaPocMsbPresentFlag[16] = { 0, };
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstH265SliceHdr *const slice_hdr = &pi->data.slice_hdr;
  GstH265SPS *const sps = get_sps (decoder);
  GstH265PPS *const pps = get_pps (decoder);
  const gint32 MaxPicOrderCntLsb =
      1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);

  /* if it is an irap pic, set all ref pics in dpb as unused for ref */
  if (nal_is_irap (pi->nalu.type) && picture->NoRaslOutputFlag) {
    for (i = 0; i < priv->dpb_count; i++) {
      GstVaapiFrameStore *const fs = priv->dpb[i];
      gst_vaapi_picture_h265_set_reference (fs->buffer, 0);
    }
  }

  /* Reset everything for IDR */
  if (nal_is_idr (pi->nalu.type)) {
    memset (priv->PocStCurrBefore, 0, sizeof (guint) * 16);
    memset (priv->PocStCurrAfter, 0, sizeof (guint) * 16);
    memset (priv->PocStFoll, 0, sizeof (guint) * 16);
    memset (priv->PocLtCurr, 0, sizeof (guint) * 16);
    memset (priv->PocLtFoll, 0, sizeof (guint) * 16);
    priv->NumPocStCurrBefore = priv->NumPocStCurrAfter = priv->NumPocStFoll = 0;
    priv->NumPocLtCurr = priv->NumPocLtFoll = 0;
    priv->NumPocTotalCurr = 0;
  } else {
    GstH265ShortTermRefPicSet *stRefPic = NULL;
    gint32 num_lt_pics, pocLt;
    gint32 PocLsbLt[16] = { 0, };
    gint32 UsedByCurrPicLt[16] = { 0, };
    gint32 DeltaPocMsbCycleLt[16] = { 0, };
    gint numtotalcurr = 0;

    /* this is based on CurrRpsIdx described in spec */
    if (!slice_hdr->short_term_ref_pic_set_sps_flag)
      stRefPic = &slice_hdr->short_term_ref_pic_sets;
    else if (sps->num_short_term_ref_pic_sets)
      stRefPic =
          &sps->short_term_ref_pic_set[slice_hdr->short_term_ref_pic_set_idx];

    g_assert (stRefPic != NULL);

    for (i = 0, j = 0, k = 0; i < stRefPic->NumNegativePics; i++) {
      if (stRefPic->UsedByCurrPicS0[i]) {
        priv->PocStCurrBefore[j++] = picture->poc + stRefPic->DeltaPocS0[i];
        numtotalcurr++;
      } else
        priv->PocStFoll[k++] = picture->poc + stRefPic->DeltaPocS0[i];
    }
    priv->NumPocStCurrBefore = j;
    for (i = 0, j = 0; i < stRefPic->NumPositivePics; i++) {
      if (stRefPic->UsedByCurrPicS1[i]) {
        priv->PocStCurrAfter[j++] = picture->poc + stRefPic->DeltaPocS1[i];
        numtotalcurr++;
      } else
        priv->PocStFoll[k++] = picture->poc + stRefPic->DeltaPocS1[i];
    }
    priv->NumPocStCurrAfter = j;
    priv->NumPocStFoll = k;
    num_lt_pics = slice_hdr->num_long_term_sps + slice_hdr->num_long_term_pics;
    /* The variables PocLsbLt[i] and UsedByCurrPicLt[i] are derived as follows: */
    for (i = 0; i < num_lt_pics; i++) {
      if (i < slice_hdr->num_long_term_sps) {
        PocLsbLt[i] = sps->lt_ref_pic_poc_lsb_sps[slice_hdr->lt_idx_sps[i]];
        UsedByCurrPicLt[i] =
            sps->used_by_curr_pic_lt_sps_flag[slice_hdr->lt_idx_sps[i]];
      } else {
        PocLsbLt[i] = slice_hdr->poc_lsb_lt[i];
        UsedByCurrPicLt[i] = slice_hdr->used_by_curr_pic_lt_flag[i];
      }
      if (UsedByCurrPicLt[i])
        numtotalcurr++;
    }

    if (pps->pps_scc_extension_params.pps_curr_pic_ref_enabled_flag)
      numtotalcurr++;
    priv->NumPocTotalCurr = numtotalcurr;

    /* The variable DeltaPocMsbCycleLt[i] is derived as follows: (7-38) */
    for (i = 0; i < num_lt_pics; i++) {
      if (i == 0 || i == slice_hdr->num_long_term_sps)
        DeltaPocMsbCycleLt[i] = slice_hdr->delta_poc_msb_cycle_lt[i];
      else
        DeltaPocMsbCycleLt[i] =
            slice_hdr->delta_poc_msb_cycle_lt[i] + DeltaPocMsbCycleLt[i - 1];
    }

    /* (8-5) */
    for (i = 0, j = 0, k = 0; i < num_lt_pics; i++) {
      pocLt = PocLsbLt[i];
      if (slice_hdr->delta_poc_msb_present_flag[i])
        pocLt +=
            picture->poc - DeltaPocMsbCycleLt[i] * MaxPicOrderCntLsb -
            slice_hdr->pic_order_cnt_lsb;
      if (UsedByCurrPicLt[i]) {
        priv->PocLtCurr[j] = pocLt;
        CurrDeltaPocMsbPresentFlag[j++] =
            slice_hdr->delta_poc_msb_present_flag[i];
      } else {
        priv->PocLtFoll[k] = pocLt;
        FollDeltaPocMsbPresentFlag[k++] =
            slice_hdr->delta_poc_msb_present_flag[i];
      }
    }
    priv->NumPocLtCurr = j;
    priv->NumPocLtFoll = k;

  }

  /* the derivation process for the RPS and the picture marking */
  derive_and_mark_rps (decoder, picture, pi, CurrDeltaPocMsbPresentFlag,
      FollDeltaPocMsbPresentFlag);

  return TRUE;
}

static GstVaapiDecoderStatus
decode_picture (GstVaapiDecoderH265 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *pi = unit->parsed_info;
  GstH265SliceHdr *const slice_hdr = &pi->data.slice_hdr;
  GstH265PPS *const pps = ensure_pps (decoder, slice_hdr->pps);
  GstH265SPS *const sps = ensure_sps (decoder, slice_hdr->pps->sps);
  GstVaapiPictureH265 *picture;
  GstVaapiDecoderStatus status;

  if (!(pps && sps))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

  status = ensure_context (decoder, sps);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  priv->decoder_state = 0;

  /* Create new picture */
  picture = gst_vaapi_picture_h265_new (decoder);
  if (!picture) {
    GST_ERROR ("failed to allocate picture");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  gst_vaapi_picture_replace (&priv->current_picture, picture);
  gst_vaapi_picture_unref (picture);

  /* Update cropping rectangle */
  if (sps->conformance_window_flag) {
    GstVaapiRectangle crop_rect;
    crop_rect.x = sps->crop_rect_x;
    crop_rect.y = sps->crop_rect_y;
    crop_rect.width = sps->crop_rect_width;
    crop_rect.height = sps->crop_rect_height;
    gst_vaapi_picture_set_crop_rect (&picture->base, &crop_rect);
  }

  status = ensure_quant_matrix (decoder, picture);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
    GST_ERROR ("failed to reset quantizer matrix");
    return status;
  }

  if (!init_picture (decoder, picture, pi))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

  /* Drop all RASL pictures having NoRaslOutputFlag is TRUE for the
   * associated IRAP picture */
  if (nal_is_rasl (pi->nalu.type) && priv->associated_irap_NoRaslOutputFlag) {
    gst_vaapi_picture_replace (&priv->current_picture, NULL);
    return (GstVaapiDecoderStatus) GST_VAAPI_DECODER_STATUS_DROP_FRAME;
  }

  if (!decode_ref_pic_set (decoder, picture, pi))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

  if (!dpb_init (decoder, picture, pi))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

  if (!fill_picture (decoder, picture, pi))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

  priv->decoder_state = pi->state;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline guint
get_slice_data_byte_offset (GstH265SliceHdr * slice_hdr, guint nal_header_bytes)
{
  guint epb_count;

  epb_count = slice_hdr->n_emulation_prevention_bytes;
  return nal_header_bytes + (slice_hdr->header_size + 7) / 8 - epb_count;
}

static gboolean
fill_pred_weight_table (GstVaapiDecoderH265 * decoder,
    GstVaapiSlice * slice, GstH265SliceHdr * slice_hdr)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  VASliceParameterBufferHEVC *slice_param = slice->param;
  GstH265PPS *const pps = get_pps (decoder);
  GstH265SPS *const sps = get_sps (decoder);
  GstH265PredWeightTable *const w = &slice_hdr->pred_weight_table;
  gint chroma_weight, chroma_log2_weight_denom;
  gint i, j;

#if VA_CHECK_VERSION(1,2,0)
  VASliceParameterBufferHEVCRext *slice_rext_param = NULL;
  if (is_range_extension_profile (priv->profile)) {
    VASliceParameterBufferHEVCExtension *param = slice->param;
    slice_param = &param->base;
    slice_rext_param = &param->rext;
  }
#endif

  slice_param->luma_log2_weight_denom = 0;
  slice_param->delta_chroma_log2_weight_denom = 0;

  if ((pps->weighted_pred_flag && GST_H265_IS_P_SLICE (slice_hdr)) ||
      (pps->weighted_bipred_flag && GST_H265_IS_B_SLICE (slice_hdr))) {

    /* FIXME: This should be done in parser apis */
    memset (slice_param->delta_luma_weight_l0, 0,
        sizeof (slice_param->delta_luma_weight_l0));
    memset (slice_param->luma_offset_l0, 0,
        sizeof (slice_param->luma_offset_l0));
    memset (slice_param->delta_luma_weight_l1, 0,
        sizeof (slice_param->delta_luma_weight_l1));
    memset (slice_param->luma_offset_l1, 0,
        sizeof (slice_param->luma_offset_l1));
    memset (slice_param->delta_chroma_weight_l0, 0,
        sizeof (slice_param->delta_chroma_weight_l0));
    memset (slice_param->ChromaOffsetL0, 0,
        sizeof (slice_param->ChromaOffsetL0));
    memset (slice_param->delta_chroma_weight_l1, 0,
        sizeof (slice_param->delta_chroma_weight_l1));
    memset (slice_param->ChromaOffsetL1, 0,
        sizeof (slice_param->ChromaOffsetL1));

#if VA_CHECK_VERSION(1,2,0)
    if (slice_rext_param) {
      memset (slice_rext_param->luma_offset_l0, 0,
          sizeof (slice_rext_param->luma_offset_l0));
      memset (slice_rext_param->luma_offset_l1, 0,
          sizeof (slice_rext_param->luma_offset_l1));
      memset (slice_rext_param->ChromaOffsetL0, 0,
          sizeof (slice_rext_param->ChromaOffsetL0));
      memset (slice_rext_param->ChromaOffsetL1, 0,
          sizeof (slice_rext_param->ChromaOffsetL1));
    }
#endif

    slice_param->luma_log2_weight_denom = w->luma_log2_weight_denom;
    if (sps->chroma_array_type != 0)
      slice_param->delta_chroma_log2_weight_denom =
          w->delta_chroma_log2_weight_denom;

    chroma_log2_weight_denom =
        slice_param->luma_log2_weight_denom +
        slice_param->delta_chroma_log2_weight_denom;

    for (i = 0; i <= slice_param->num_ref_idx_l0_active_minus1; i++) {
      if (slice_hdr->pred_weight_table.luma_weight_l0_flag[i]) {
        slice_param->delta_luma_weight_l0[i] = w->delta_luma_weight_l0[i];
        slice_param->luma_offset_l0[i] = w->luma_offset_l0[i];
#if VA_CHECK_VERSION(1,2,0)
        if (slice_rext_param)
          slice_rext_param->luma_offset_l0[i] = w->luma_offset_l0[i];
#endif
      }
      if (slice_hdr->pred_weight_table.chroma_weight_l0_flag[i]) {
        for (j = 0; j < 2; j++) {
          slice_param->delta_chroma_weight_l0[i][j] =
              w->delta_chroma_weight_l0[i][j];
          /* Find  ChromaWeightL0 */
          chroma_weight =
              (1 << chroma_log2_weight_denom) + w->delta_chroma_weight_l0[i][j];
          /* 7-56 */
          slice_param->ChromaOffsetL0[i][j] = CLAMP (
              (priv->WpOffsetHalfRangeC + w->delta_chroma_offset_l0[i][j] -
                  ((priv->WpOffsetHalfRangeC *
                          chroma_weight) >> chroma_log2_weight_denom)),
              -priv->WpOffsetHalfRangeC, priv->WpOffsetHalfRangeC - 1);
#if VA_CHECK_VERSION(1,2,0)
          if (slice_rext_param)
            slice_rext_param->ChromaOffsetL0[i][j] = CLAMP (
                (priv->WpOffsetHalfRangeC + w->delta_chroma_offset_l0[i][j] -
                    ((priv->WpOffsetHalfRangeC *
                            chroma_weight) >> chroma_log2_weight_denom)),
                -priv->WpOffsetHalfRangeC, priv->WpOffsetHalfRangeC - 1);
#endif
        }
      }
    }

    if (GST_H265_IS_B_SLICE (slice_hdr)) {
      for (i = 0; i <= slice_param->num_ref_idx_l1_active_minus1; i++) {
        if (slice_hdr->pred_weight_table.luma_weight_l1_flag[i]) {
          slice_param->delta_luma_weight_l1[i] = w->delta_luma_weight_l1[i];
          slice_param->luma_offset_l1[i] = w->luma_offset_l1[i];
#if VA_CHECK_VERSION(1,2,0)
          if (slice_rext_param)
            slice_rext_param->luma_offset_l1[i] = w->luma_offset_l1[i];
#endif
        }
        if (slice_hdr->pred_weight_table.chroma_weight_l1_flag[i]) {
          for (j = 0; j < 2; j++) {
            slice_param->delta_chroma_weight_l1[i][j] =
                w->delta_chroma_weight_l1[i][j];
            /* Find  ChromaWeightL1 */
            chroma_weight =
                (1 << chroma_log2_weight_denom) +
                w->delta_chroma_weight_l1[i][j];
            /* 7-56 */
            slice_param->ChromaOffsetL1[i][j] =
                CLAMP ((priv->WpOffsetHalfRangeC +
                    w->delta_chroma_offset_l1[i][j] -
                    ((priv->WpOffsetHalfRangeC *
                            chroma_weight) >> chroma_log2_weight_denom)),
                -priv->WpOffsetHalfRangeC, priv->WpOffsetHalfRangeC - 1);
#if VA_CHECK_VERSION(1,2,0)
            if (slice_rext_param)
              slice_rext_param->ChromaOffsetL1[i][j] =
                  CLAMP ((priv->WpOffsetHalfRangeC +
                      w->delta_chroma_offset_l1[i][j] -
                      ((priv->WpOffsetHalfRangeC *
                              chroma_weight) >> chroma_log2_weight_denom)),
                  -priv->WpOffsetHalfRangeC, priv->WpOffsetHalfRangeC - 1);
#endif
          }
        }
      }
    }
  }
  return TRUE;
}

static gboolean
fill_RefPicList (GstVaapiDecoderH265 * decoder,
    GstVaapiPictureH265 * picture, GstVaapiSlice * slice,
    GstH265SliceHdr * slice_hdr)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  VASliceParameterBufferHEVC *const slice_param = slice->param;
  GstVaapiPicture *const base_picture = &picture->base;
  VAPictureParameterBufferHEVC *const pic_param = base_picture->param;
  guint i, num_ref_lists = 0, j;

  slice_param->num_ref_idx_l0_active_minus1 = 0;
  slice_param->num_ref_idx_l1_active_minus1 = 0;
  for (j = 0; j < 2; j++)
    for (i = 0; i < 15; i++)
      slice_param->RefPicList[j][i] = 0xFF;

  if (GST_H265_IS_B_SLICE (slice_hdr))
    num_ref_lists = 2;
  else if (GST_H265_IS_I_SLICE (slice_hdr))
    num_ref_lists = 0;
  else
    num_ref_lists = 1;

  if (num_ref_lists < 1)
    return TRUE;

  slice_param->num_ref_idx_l0_active_minus1 =
      slice_hdr->num_ref_idx_l0_active_minus1;
  slice_param->num_ref_idx_l1_active_minus1 =
      slice_hdr->num_ref_idx_l1_active_minus1;

  for (i = 0; i < priv->RefPicList0_count; i++)
    slice_param->RefPicList[0][i] =
        get_index_for_RefPicListX (pic_param->ReferenceFrames,
        priv->RefPicList0[i]);
  for (; i < 15; i++)
    slice_param->RefPicList[0][i] = 0xFF;

  if (num_ref_lists < 2)
    return TRUE;

  for (i = 0; i < priv->RefPicList1_count; i++)
    slice_param->RefPicList[1][i] =
        get_index_for_RefPicListX (pic_param->ReferenceFrames,
        priv->RefPicList1[i]);
  for (; i < 15; i++)
    slice_param->RefPicList[1][i] = 0xFF;

  return TRUE;
}

static gboolean
fill_slice (GstVaapiDecoderH265 * decoder,
    GstVaapiPictureH265 * picture, GstVaapiSlice * slice,
    GstVaapiParserInfoH265 * pi, GstVaapiDecoderUnit * unit)
{
  GstH265SliceHdr *slice_hdr = &pi->data.slice_hdr;
  VASliceParameterBufferHEVC *slice_param = slice->param;

#if VA_CHECK_VERSION(1,2,0)
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  VASliceParameterBufferHEVCRext *slice_rext_param = NULL;
  if (is_range_extension_profile (priv->profile)
      || is_scc_profile (priv->profile)) {
    VASliceParameterBufferHEVCExtension *param = slice->param;
    slice_param = &param->base;
    slice_rext_param = &param->rext;
  }
#endif

  /* Fill in VASliceParameterBufferH265 */
  slice_param->LongSliceFlags.value = 0;
  slice_param->slice_data_byte_offset =
      get_slice_data_byte_offset (slice_hdr, pi->nalu.header_bytes);

  slice_param->slice_segment_address = slice_hdr->segment_address;

#define COPY_LFF(f) \
    slice_param->LongSliceFlags.fields.f = (slice_hdr)->f

  if (GST_VAAPI_PICTURE_FLAG_IS_SET (picture, GST_VAAPI_PICTURE_FLAG_AU_END))
    slice_param->LongSliceFlags.fields.LastSliceOfPic = 1;
  else
    slice_param->LongSliceFlags.fields.LastSliceOfPic = 0;

  COPY_LFF (dependent_slice_segment_flag);

  COPY_LFF (mvd_l1_zero_flag);
  COPY_LFF (cabac_init_flag);
  COPY_LFF (collocated_from_l0_flag);
  slice_param->LongSliceFlags.fields.color_plane_id =
      slice_hdr->colour_plane_id;
  slice_param->LongSliceFlags.fields.slice_type = slice_hdr->type;
  slice_param->LongSliceFlags.fields.slice_sao_luma_flag =
      slice_hdr->sao_luma_flag;
  slice_param->LongSliceFlags.fields.slice_sao_chroma_flag =
      slice_hdr->sao_chroma_flag;
  slice_param->LongSliceFlags.fields.slice_temporal_mvp_enabled_flag =
      slice_hdr->temporal_mvp_enabled_flag;
  slice_param->LongSliceFlags.fields.slice_deblocking_filter_disabled_flag =
      slice_hdr->deblocking_filter_disabled_flag;
  slice_param->LongSliceFlags.fields.
      slice_loop_filter_across_slices_enabled_flag =
      slice_hdr->loop_filter_across_slices_enabled_flag;

  if (!slice_hdr->temporal_mvp_enabled_flag)
    slice_param->collocated_ref_idx = 0xFF;
  else
    slice_param->collocated_ref_idx = slice_hdr->collocated_ref_idx;

  slice_param->num_ref_idx_l0_active_minus1 =
      slice_hdr->num_ref_idx_l0_active_minus1;
  slice_param->num_ref_idx_l1_active_minus1 =
      slice_hdr->num_ref_idx_l1_active_minus1;
  slice_param->slice_qp_delta = slice_hdr->qp_delta;
  slice_param->slice_cb_qp_offset = slice_hdr->cb_qp_offset;
  slice_param->slice_cr_qp_offset = slice_hdr->cr_qp_offset;
  slice_param->slice_beta_offset_div2 = slice_hdr->beta_offset_div2;
  slice_param->slice_tc_offset_div2 = slice_hdr->tc_offset_div2;
  slice_param->five_minus_max_num_merge_cand =
      slice_hdr->five_minus_max_num_merge_cand;

#if VA_CHECK_VERSION(1,2,0)
  if (slice_rext_param) {
    slice_rext_param->slice_ext_flags.bits.cu_chroma_qp_offset_enabled_flag =
        slice_hdr->cu_chroma_qp_offset_enabled_flag;
    slice_rext_param->slice_ext_flags.bits.use_integer_mv_flag =
        slice_hdr->use_integer_mv_flag;

    slice_rext_param->slice_act_y_qp_offset = slice_hdr->slice_act_y_qp_offset;
    slice_rext_param->slice_act_cb_qp_offset =
        slice_hdr->slice_act_cb_qp_offset;
    slice_rext_param->slice_act_cr_qp_offset =
        slice_hdr->slice_act_cr_qp_offset;
  }
#endif

  if (!fill_RefPicList (decoder, picture, slice, slice_hdr))
    return FALSE;

  if (!fill_pred_weight_table (decoder, slice, slice_hdr))
    return FALSE;

  return TRUE;
}

static GstVaapiDecoderStatus
decode_slice (GstVaapiDecoderH265 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const pi = unit->parsed_info;
  GstVaapiPictureH265 *const picture = priv->current_picture;
  GstH265SliceHdr *const slice_hdr = &pi->data.slice_hdr;
  GstVaapiSlice *slice = NULL;
  GstBuffer *const buffer =
      GST_VAAPI_DECODER_CODEC_FRAME (decoder)->input_buffer;
  GstMapInfo map_info;

  GST_DEBUG ("slice (%u bytes)", pi->nalu.size);
  if (!is_valid_state (pi->state, GST_H265_VIDEO_STATE_VALID_PICTURE_HEADERS)) {
    GST_WARNING ("failed to receive enough headers to decode slice");
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
  }

  if (!ensure_pps (decoder, slice_hdr->pps)) {
    GST_ERROR ("failed to activate PPS");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  if (!ensure_sps (decoder, slice_hdr->pps->sps)) {
    GST_ERROR ("failed to activate SPS");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  if (!gst_buffer_map (buffer, &map_info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  /* Check wether this is the first/last slice in the current access unit */
  if (pi->flags & GST_VAAPI_DECODER_UNIT_FLAG_AU_START)
    GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_AU_START);

  if (pi->flags & GST_VAAPI_DECODER_UNIT_FLAG_AU_END)
    GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_AU_END);

  if (is_range_extension_profile (priv->profile)
      || is_scc_profile (priv->profile)) {
#if VA_CHECK_VERSION(1,2,0)
    slice = GST_VAAPI_SLICE_NEW (HEVCExtension, decoder,
        (map_info.data + unit->offset + pi->nalu.offset), pi->nalu.size);
#endif
  } else {
    slice = GST_VAAPI_SLICE_NEW (HEVC, decoder,
        (map_info.data + unit->offset + pi->nalu.offset), pi->nalu.size);
  }
  gst_buffer_unmap (buffer, &map_info);
  if (!slice) {
    GST_ERROR ("failed to allocate slice");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  init_picture_refs (decoder, picture, slice_hdr);

  if (!fill_slice (decoder, picture, slice, pi, unit)) {
    gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (slice));
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  gst_vaapi_picture_add_slice (GST_VAAPI_PICTURE_CAST (picture), slice);
  picture->last_slice_hdr = slice_hdr;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline gint
scan_for_start_code (GstAdapter * adapter, guint ofs, guint size, guint32 * scp)
{
  if (size == 0)
    return -1;

  return (gint) gst_adapter_masked_scan_uint32_peek (adapter,
      0xffffff00, 0x00000100, ofs, size, scp);
}

static GstVaapiDecoderStatus
decode_unit (GstVaapiDecoderH265 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserInfoH265 *const pi = unit->parsed_info;
  GstVaapiDecoderStatus status;
  priv->decoder_state |= pi->state;
  switch (pi->nalu.type) {
    case GST_H265_NAL_VPS:
      status = decode_vps (decoder, unit);
      break;
    case GST_H265_NAL_SPS:
      status = decode_sps (decoder, unit);
      break;
    case GST_H265_NAL_PPS:
      status = decode_pps (decoder, unit);
      break;
    case GST_H265_NAL_SLICE_TRAIL_N:
    case GST_H265_NAL_SLICE_TRAIL_R:
    case GST_H265_NAL_SLICE_TSA_N:
    case GST_H265_NAL_SLICE_TSA_R:
    case GST_H265_NAL_SLICE_STSA_N:
    case GST_H265_NAL_SLICE_STSA_R:
    case GST_H265_NAL_SLICE_RADL_N:
    case GST_H265_NAL_SLICE_RADL_R:
    case GST_H265_NAL_SLICE_RASL_N:
    case GST_H265_NAL_SLICE_RASL_R:
    case GST_H265_NAL_SLICE_BLA_W_LP:
    case GST_H265_NAL_SLICE_BLA_W_RADL:
    case GST_H265_NAL_SLICE_BLA_N_LP:
    case GST_H265_NAL_SLICE_IDR_W_RADL:
    case GST_H265_NAL_SLICE_IDR_N_LP:
    case GST_H265_NAL_SLICE_CRA_NUT:
      /* slice decoding will get started only after completing all the
         initialization routines for each picture which is hanlding in
         start_frame() call back, so the new_bitstream and prev_nal_is_eos
         flags will have effects starting from the next frame onwards */
      priv->new_bitstream = FALSE;
      priv->prev_nal_is_eos = FALSE;
      status = decode_slice (decoder, unit);
      break;
    case GST_H265_NAL_EOB:
      priv->new_bitstream = TRUE;
      GST_DEBUG
          ("Next AU(if there is any) will be the begining of new bitstream");
      status = decode_sequence_end (decoder);
      break;
    case GST_H265_NAL_EOS:
      priv->prev_nal_is_eos = TRUE;
      status = decode_sequence_end (decoder);
      break;
    case GST_H265_NAL_SUFFIX_SEI:
    case GST_H265_NAL_PREFIX_SEI:
      status = decode_sei (decoder, unit);
      break;
    default:
      GST_WARNING ("unsupported NAL unit type %d", pi->nalu.type);
      status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
      break;
  }
  return status;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_h265_decode_codec_data (GstVaapiDecoder *
    base_decoder, const guchar * buf, guint buf_size)
{
  GstVaapiDecoderH265 *const decoder =
      GST_VAAPI_DECODER_H265_CAST (base_decoder);
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiDecoderStatus status;
  GstVaapiDecoderUnit unit;
  GstVaapiParserInfoH265 *pi = NULL;
  GstH265ParserResult result;
  guint num_nal_arrays, num_nals;
  guint i, j, ofs;

  if (!priv->is_opened)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  unit.parsed_info = NULL;
  if (buf_size < 23)
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
  if (buf[0] != 1) {
    GST_ERROR ("failed to decode codec-data, not in hvcC format");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  priv->nal_length_size = (buf[21] & 0x03) + 1;
  GST_DEBUG ("nal length size %u", priv->nal_length_size);
  num_nal_arrays = buf[22];
  ofs = 23;
  for (i = 0; i < num_nal_arrays; i++) {
    if (ofs + 1 > buf_size)
      return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    num_nals = GST_READ_UINT16_BE (buf + ofs + 1);
    /* the max number of nals is GST_H265_MAX_PPS_COUNT (64) */
    if (num_nals > 64)
      return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    ofs += 3;

    for (j = 0; j < num_nals; j++) {
      pi = gst_vaapi_parser_info_h265_new ();
      if (!pi)
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
      unit.parsed_info = pi;
      result = gst_h265_parser_identify_nalu_hevc (priv->parser,
          buf, ofs, buf_size, 2, &pi->nalu);
      if (result != GST_H265_PARSER_OK) {
        status = get_status (result);
        goto cleanup;
      }

      pi->state = priv->parser_state;
      pi->flags = 0;

      switch (pi->nalu.type) {
        case GST_H265_NAL_VPS:
          status = parse_vps (decoder, &unit);
          if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            goto cleanup;
          status = decode_vps (decoder, &unit);
          if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            goto cleanup;
          break;
        case GST_H265_NAL_SPS:
          status = parse_sps (decoder, &unit);
          if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            goto cleanup;
          status = decode_sps (decoder, &unit);
          if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            goto cleanup;
          break;
        case GST_H265_NAL_PPS:
          status = parse_pps (decoder, &unit);
          if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            goto cleanup;
          status = decode_pps (decoder, &unit);
          if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            goto cleanup;
          break;
        case GST_H265_NAL_SUFFIX_SEI:
        case GST_H265_NAL_PREFIX_SEI:
          status = parse_sei (decoder, &unit);
          if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            goto cleanup;
          status = decode_sei (decoder, &unit);
          if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            goto cleanup;
          break;

      }
      ofs = pi->nalu.offset + pi->nalu.size;
      gst_vaapi_parser_info_h265_replace (&pi, NULL);
    }
  }

  priv->is_hvcC = TRUE;
  status = GST_VAAPI_DECODER_STATUS_SUCCESS;
cleanup:
  gst_vaapi_parser_info_h265_replace (&pi, NULL);
  return status;
}

static GstVaapiDecoderStatus
ensure_decoder (GstVaapiDecoderH265 * decoder)
{
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiDecoderStatus status;

  if (!priv->is_opened) {
    priv->is_opened = gst_vaapi_decoder_h265_open (decoder);
    if (!priv->is_opened)
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC;
    status =
        gst_vaapi_decoder_decode_codec_data (GST_VAAPI_DECODER_CAST (decoder));
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
      return status;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
populate_dependent_slice_hdr (GstVaapiParserInfoH265 * pi,
    GstVaapiParserInfoH265 * indep_pi)
{
  GstH265SliceHdr *slice_hdr = &pi->data.slice_hdr;
  GstH265SliceHdr *indep_slice_hdr = &indep_pi->data.slice_hdr;

  memcpy (&slice_hdr->type, &indep_slice_hdr->type,
      offsetof (GstH265SliceHdr, num_entry_point_offsets) -
      offsetof (GstH265SliceHdr, type));
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_h265_parse (GstVaapiDecoder * base_decoder,
    GstAdapter * adapter, gboolean at_eos, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH265 *const decoder =
      GST_VAAPI_DECODER_H265_CAST (base_decoder);
  GstVaapiDecoderH265Private *const priv = &decoder->priv;
  GstVaapiParserState *const ps = GST_VAAPI_PARSER_STATE (base_decoder);
  GstVaapiParserInfoH265 *pi;
  GstVaapiDecoderStatus status;
  GstH265ParserResult result;
  guchar *buf;
  guint i, size, buf_size, nalu_size, flags;
  guint32 start_code;
  gint ofs, ofs2;
  gboolean at_au_end = FALSE;

  status = ensure_decoder (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  switch (priv->stream_alignment) {
    case GST_VAAPI_STREAM_ALIGN_H265_NALU:
    case GST_VAAPI_STREAM_ALIGN_H265_AU:
      size = gst_adapter_available_fast (adapter);
      break;
    default:
      size = gst_adapter_available (adapter);
      break;
  }

  if (priv->is_hvcC) {
    if (size < priv->nal_length_size)
      return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    buf = (guchar *) & start_code;
    g_assert (priv->nal_length_size <= sizeof (start_code));
    gst_adapter_copy (adapter, buf, 0, priv->nal_length_size);
    nalu_size = 0;
    for (i = 0; i < priv->nal_length_size; i++)
      nalu_size = (nalu_size << 8) | buf[i];
    buf_size = priv->nal_length_size + nalu_size;
    if (size < buf_size)
      return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    else if (priv->stream_alignment == GST_VAAPI_STREAM_ALIGN_H265_AU)
      at_au_end = (buf_size == size);
  } else {
    if (size < 4)
      return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    if (priv->stream_alignment == GST_VAAPI_STREAM_ALIGN_H265_NALU) {
      buf_size = size;
      ofs = scan_for_start_code (adapter, 4, size - 4, NULL);
      if (ofs > 0)
        buf_size = ofs;
    } else {
      ofs = scan_for_start_code (adapter, 0, size, NULL);
      if (ofs < 0)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
      if (ofs > 0) {
        gst_adapter_flush (adapter, ofs);
        size -= ofs;
      }

      ofs2 = ps->input_offset2 - ofs - 4;
      if (ofs2 < 4)
        ofs2 = 4;
      ofs = G_UNLIKELY (size < ofs2 + 4) ? -1 :
          scan_for_start_code (adapter, ofs2, size - ofs2, NULL);
      if (ofs < 0) {
        // Assume the whole NAL unit is present if end-of-stream
        // or stream buffers aligned on access unit boundaries
        if (priv->stream_alignment == GST_VAAPI_STREAM_ALIGN_H265_AU)
          at_au_end = TRUE;
        else if (!at_eos) {
          ps->input_offset2 = size;
          return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
        }
        ofs = size;
      }
      buf_size = ofs;
    }
  }
  ps->input_offset2 = 0;
  buf = (guchar *) gst_adapter_map (adapter, buf_size);
  if (!buf)
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
  unit->size = buf_size;
  pi = gst_vaapi_parser_info_h265_new ();
  if (!pi)
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  gst_vaapi_decoder_unit_set_parsed_info (unit,
      pi, (GDestroyNotify) gst_vaapi_mini_object_unref);
  if (priv->is_hvcC)
    result = gst_h265_parser_identify_nalu_hevc (priv->parser,
        buf, 0, buf_size, priv->nal_length_size, &pi->nalu);
  else
    result = gst_h265_parser_identify_nalu_unchecked (priv->parser,
        buf, 0, buf_size, &pi->nalu);
  status = get_status (result);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    goto exit;
  switch (pi->nalu.type) {
    case GST_H265_NAL_VPS:
      status = parse_vps (decoder, unit);
      break;
    case GST_H265_NAL_SPS:
      status = parse_sps (decoder, unit);
      break;
    case GST_H265_NAL_PPS:
      status = parse_pps (decoder, unit);
      break;
    case GST_H265_NAL_PREFIX_SEI:
    case GST_H265_NAL_SUFFIX_SEI:
      status = parse_sei (decoder, unit);
      break;
    case GST_H265_NAL_SLICE_TRAIL_N:
    case GST_H265_NAL_SLICE_TRAIL_R:
    case GST_H265_NAL_SLICE_TSA_N:
    case GST_H265_NAL_SLICE_TSA_R:
    case GST_H265_NAL_SLICE_STSA_N:
    case GST_H265_NAL_SLICE_STSA_R:
    case GST_H265_NAL_SLICE_RADL_N:
    case GST_H265_NAL_SLICE_RADL_R:
    case GST_H265_NAL_SLICE_RASL_N:
    case GST_H265_NAL_SLICE_RASL_R:
    case GST_H265_NAL_SLICE_BLA_W_LP:
    case GST_H265_NAL_SLICE_BLA_W_RADL:
    case GST_H265_NAL_SLICE_BLA_N_LP:
    case GST_H265_NAL_SLICE_IDR_W_RADL:
    case GST_H265_NAL_SLICE_IDR_N_LP:
    case GST_H265_NAL_SLICE_CRA_NUT:
      status = parse_slice (decoder, unit);
      break;
    default:
      status = GST_VAAPI_DECODER_STATUS_SUCCESS;
      break;
  }
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    goto exit;
  flags = 0;
  if (at_au_end) {
    flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END |
        GST_VAAPI_DECODER_UNIT_FLAG_AU_END;
  }

  switch (pi->nalu.type) {
    case GST_H265_NAL_AUD:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_AU_START;
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      /* fall-through */
    case GST_H265_NAL_FD:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SKIP;
      break;
    case GST_H265_NAL_EOB:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_STREAM_END;
      /* fall-through */
    case GST_H265_NAL_SUFFIX_SEI:
    case GST_H265_NAL_EOS:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END;
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_AU_END;
      break;
    case GST_H265_NAL_VPS:
    case GST_H265_NAL_SPS:
    case GST_H265_NAL_PPS:
    case GST_H265_NAL_PREFIX_SEI:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_AU_START;
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      break;
    case GST_H265_NAL_SLICE_TRAIL_N:
    case GST_H265_NAL_SLICE_TRAIL_R:
    case GST_H265_NAL_SLICE_TSA_N:
    case GST_H265_NAL_SLICE_TSA_R:
    case GST_H265_NAL_SLICE_STSA_N:
    case GST_H265_NAL_SLICE_STSA_R:
    case GST_H265_NAL_SLICE_RADL_N:
    case GST_H265_NAL_SLICE_RADL_R:
    case GST_H265_NAL_SLICE_RASL_N:
    case GST_H265_NAL_SLICE_RASL_R:
    case GST_H265_NAL_SLICE_BLA_W_LP:
    case GST_H265_NAL_SLICE_BLA_W_RADL:
    case GST_H265_NAL_SLICE_BLA_N_LP:
    case GST_H265_NAL_SLICE_IDR_W_RADL:
    case GST_H265_NAL_SLICE_IDR_N_LP:
    case GST_H265_NAL_SLICE_CRA_NUT:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
      if (priv->prev_pi &&
          (priv->prev_pi->flags & GST_VAAPI_DECODER_UNIT_FLAG_AU_END)) {
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_AU_START |
            GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      } else if (is_new_picture (pi, priv->prev_slice_pi)) {
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
        if (is_new_access_unit (pi, priv->prev_slice_pi))
          flags |= GST_VAAPI_DECODER_UNIT_FLAG_AU_START;
      }
      gst_vaapi_parser_info_h265_replace (&priv->prev_slice_pi, pi);
      if (!pi->data.slice_hdr.dependent_slice_segment_flag)
        gst_vaapi_parser_info_h265_replace (&priv->prev_independent_slice_pi,
            pi);
      else
        populate_dependent_slice_hdr (pi, priv->prev_independent_slice_pi);
      if (!GST_H265_IS_I_SLICE (&pi->data.slice_hdr))
        priv->parser_state |= GST_H265_VIDEO_STATE_GOT_P_SLICE;
      break;
    default:
      /* Fix */
      break;
  }
  if ((flags & GST_VAAPI_DECODER_UNIT_FLAGS_AU) && priv->prev_slice_pi)
    priv->prev_slice_pi->flags |= GST_VAAPI_DECODER_UNIT_FLAG_AU_END;
  GST_VAAPI_DECODER_UNIT_FLAG_SET (unit, flags);
  pi->nalu.data = NULL;
  pi->state = priv->parser_state;
  pi->flags = flags;
  gst_vaapi_parser_info_h265_replace (&priv->prev_pi, pi);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;

exit:
  gst_adapter_flush (adapter, unit->size);
  gst_vaapi_parser_info_h265_unref (pi);
  return status;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_h265_decode (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH265 *const decoder =
      GST_VAAPI_DECODER_H265_CAST (base_decoder);
  GstVaapiDecoderStatus status;

  status = ensure_decoder (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;
  return decode_unit (decoder, unit);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_h265_start_frame (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderH265 *const decoder =
      GST_VAAPI_DECODER_H265_CAST (base_decoder);

  return decode_picture (decoder, unit);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_h265_end_frame (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderH265 *const decoder =
      GST_VAAPI_DECODER_H265_CAST (base_decoder);

  return decode_current_picture (decoder);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_h265_flush (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderH265 *const decoder =
      GST_VAAPI_DECODER_H265_CAST (base_decoder);

  dpb_flush (decoder);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
gst_vaapi_decoder_h265_finalize (GObject * object)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (object);

  gst_vaapi_decoder_h265_destroy (base_decoder);
  G_OBJECT_CLASS (gst_vaapi_decoder_h265_parent_class)->finalize (object);
}

static void
gst_vaapi_decoder_h265_class_init (GstVaapiDecoderH265Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiDecoderClass *const decoder_class = GST_VAAPI_DECODER_CLASS (klass);

  object_class->finalize = gst_vaapi_decoder_h265_finalize;

  decoder_class->reset = gst_vaapi_decoder_h265_reset;
  decoder_class->parse = gst_vaapi_decoder_h265_parse;
  decoder_class->decode = gst_vaapi_decoder_h265_decode;
  decoder_class->start_frame = gst_vaapi_decoder_h265_start_frame;
  decoder_class->end_frame = gst_vaapi_decoder_h265_end_frame;
  decoder_class->flush = gst_vaapi_decoder_h265_flush;
  decoder_class->decode_codec_data = gst_vaapi_decoder_h265_decode_codec_data;
}

static void
gst_vaapi_decoder_h265_init (GstVaapiDecoderH265 * decoder)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (decoder);

  gst_vaapi_decoder_h265_create (base_decoder);
}

/**
 * gst_vaapi_decoder_h265_set_alignment:
 * @decoder: a #GstVaapiDecoderH265
 * @alignment: the #GstVaapiStreamAlignH265
 *
 * Specifies how stream buffers are aligned / fed, i.e. the boundaries
 * of each buffer that is supplied to the decoder. This could be no
 * specific alignment, NAL unit boundaries, or access unit boundaries.
 */
void
gst_vaapi_decoder_h265_set_alignment (GstVaapiDecoderH265 * decoder,
    GstVaapiStreamAlignH265 alignment)
{
  g_return_if_fail (decoder != NULL);
  decoder->priv.stream_alignment = alignment;
}

/**
 * gst_vaapi_decoder_h265_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps holding codec information
 *
 * Creates a new #GstVaapiDecoder for MPEG-2 decoding.  The @caps can
 * hold extra information like codec-data and pictured coded size.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_h265_new (GstVaapiDisplay * display, GstCaps * caps)
{
  return g_object_new (GST_TYPE_VAAPI_DECODER_H265, "display", display,
      "caps", caps, NULL);
}
