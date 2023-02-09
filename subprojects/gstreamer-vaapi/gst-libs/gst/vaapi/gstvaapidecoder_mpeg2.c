/*
 *  gstvaapidecoder_mpeg2.c - MPEG-2 decoder
 *
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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
 * SECTION:gstvaapidecoder_mpeg2
 * @short_description: MPEG-2 decoder
 */

#include "sysdeps.h"
#include <gst/base/gstbitreader.h>
#include <gst/codecparsers/gstmpegvideoparser.h>
#include "gstvaapidecoder_mpeg2.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_dpb.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* ------------------------------------------------------------------------- */
/* --- PTS Generator                                                     --- */
/* ------------------------------------------------------------------------- */

typedef struct _PTSGenerator PTSGenerator;
struct _PTSGenerator
{
  GstClockTime gop_pts;         // Current GOP PTS
  GstClockTime max_pts;         // Max picture PTS
  guint gop_tsn;                // Absolute GOP TSN
  guint max_tsn;                // Max picture TSN, relative to last GOP TSN
  guint ovl_tsn;                // How many times TSN overflowed since GOP
  guint lst_tsn;                // Last picture TSN
  guint fps_n;
  guint fps_d;
};

static void
pts_init (PTSGenerator * tsg)
{
  tsg->gop_pts = GST_CLOCK_TIME_NONE;
  tsg->max_pts = GST_CLOCK_TIME_NONE;
  tsg->gop_tsn = 0;
  tsg->max_tsn = 0;
  tsg->ovl_tsn = 0;
  tsg->lst_tsn = 0;
  tsg->fps_n = 0;
  tsg->fps_d = 0;
}

static inline GstClockTime
pts_get_duration (PTSGenerator * tsg, guint num_frames)
{
  return gst_util_uint64_scale (num_frames,
      GST_SECOND * tsg->fps_d, tsg->fps_n);
}

static inline guint
pts_get_poc (PTSGenerator * tsg)
{
  return tsg->gop_tsn + tsg->ovl_tsn * 1024 + tsg->lst_tsn;
}

static void
pts_set_framerate (PTSGenerator * tsg, guint fps_n, guint fps_d)
{
  tsg->fps_n = fps_n;
  tsg->fps_d = fps_d;
}

static void
pts_sync (PTSGenerator * tsg, GstClockTime gop_pts)
{
  guint gop_tsn;

  if (!GST_CLOCK_TIME_IS_VALID (gop_pts) ||
      (GST_CLOCK_TIME_IS_VALID (tsg->max_pts) && tsg->max_pts >= gop_pts)) {
    /* Invalid GOP PTS, interpolate from the last known picture PTS */
    if (GST_CLOCK_TIME_IS_VALID (tsg->max_pts)) {
      gop_pts = tsg->max_pts + pts_get_duration (tsg, 1);
      gop_tsn = tsg->gop_tsn + tsg->ovl_tsn * 1024 + tsg->max_tsn + 1;
    } else {
      gop_pts = 0;
      gop_tsn = 0;
    }
  } else {
    /* Interpolate GOP TSN from this valid PTS */
    if (GST_CLOCK_TIME_IS_VALID (tsg->gop_pts))
      gop_tsn =
          tsg->gop_tsn + gst_util_uint64_scale (gop_pts - tsg->gop_pts +
          pts_get_duration (tsg, 1) - 1, tsg->fps_n, GST_SECOND * tsg->fps_d);
    else
      gop_tsn = 0;
  }

  tsg->gop_pts = gop_pts;
  tsg->gop_tsn = gop_tsn;
  tsg->max_tsn = 0;
  tsg->ovl_tsn = 0;
  tsg->lst_tsn = 0;
}

static GstClockTime
pts_eval (PTSGenerator * tsg, GstClockTime pic_pts, guint pic_tsn)
{
  GstClockTime pts;

  if (!GST_CLOCK_TIME_IS_VALID (tsg->gop_pts))
    tsg->gop_pts = pts_get_duration (tsg, pic_tsn);

  pts = pic_pts;
  if (!GST_CLOCK_TIME_IS_VALID (pts))
    pts = tsg->gop_pts + pts_get_duration (tsg, tsg->ovl_tsn * 1024 + pic_tsn);
  else if (pts == tsg->gop_pts) {
    /* The picture following the GOP header shall be an I-frame.
       So we can compensate for the GOP start time from here */
    tsg->gop_pts -= pts_get_duration (tsg, pic_tsn);
  }

  if (!GST_CLOCK_TIME_IS_VALID (tsg->max_pts) || tsg->max_pts < pts)
    tsg->max_pts = pts;

  if (tsg->max_tsn < pic_tsn)
    tsg->max_tsn = pic_tsn;
  else if (tsg->max_tsn == 1023 && pic_tsn < tsg->lst_tsn) {    /* TSN wrapped */
    tsg->max_tsn = pic_tsn;
    tsg->ovl_tsn++;
  }
  tsg->lst_tsn = pic_tsn;

  return pts;
}

/* ------------------------------------------------------------------------- */
/* --- MPEG-2 Parser Info                                                --- */
/* ------------------------------------------------------------------------- */

typedef struct _GstVaapiParserInfoMpeg2 GstVaapiParserInfoMpeg2;
struct _GstVaapiParserInfoMpeg2
{
  GstVaapiMiniObject parent_instance;
  GstMpegVideoPacket packet;
  guint8 extension_type;        /* for Extension packets */
  union
  {
    GstMpegVideoSequenceHdr seq_hdr;
    GstMpegVideoSequenceExt seq_ext;
    GstMpegVideoSequenceDisplayExt seq_display_ext;
    GstMpegVideoSequenceScalableExt seq_scalable_ext;
    GstMpegVideoGop gop;
    GstMpegVideoQuantMatrixExt quant_matrix;
    GstMpegVideoPictureHdr pic_hdr;
    GstMpegVideoPictureExt pic_ext;
    GstMpegVideoSliceHdr slice_hdr;
  } data;
};

static inline const GstVaapiMiniObjectClass *
gst_vaapi_parser_info_mpeg2_class (void)
{
  static const GstVaapiMiniObjectClass GstVaapiParserInfoMpeg2Class = {
    sizeof (GstVaapiParserInfoMpeg2),
    NULL
  };
  return &GstVaapiParserInfoMpeg2Class;
}

static inline GstVaapiParserInfoMpeg2 *
gst_vaapi_parser_info_mpeg2_new (void)
{
  return (GstVaapiParserInfoMpeg2 *)
      gst_vaapi_mini_object_new (gst_vaapi_parser_info_mpeg2_class ());
}

static inline GstVaapiParserInfoMpeg2 *
gst_vaapi_parser_info_mpeg2_ensure (GstVaapiParserInfoMpeg2 ** pi_ptr)
{
  GstVaapiParserInfoMpeg2 *pi = *pi_ptr;

  if (G_LIKELY (pi != NULL))
    return pi;

  *pi_ptr = pi = gst_vaapi_parser_info_mpeg2_new ();
  return pi;
}

#define gst_vaapi_parser_info_mpeg2_ref(pi) \
    gst_vaapi_mini_object_ref(GST_VAAPI_MINI_OBJECT(pi))

#define gst_vaapi_parser_info_mpeg2_unref(pi) \
    gst_vaapi_mini_object_unref(GST_VAAPI_MINI_OBJECT(pi))

#define gst_vaapi_parser_info_mpeg2_replace(old_pi_ptr, new_pi)         \
    gst_vaapi_mini_object_replace((GstVaapiMiniObject **)(old_pi_ptr),  \
        (GstVaapiMiniObject *)(new_pi))

/* ------------------------------------------------------------------------- */
/* --- MPEG-2 Decoder                                                    --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_DECODER_MPEG2_CAST(decoder) \
    ((GstVaapiDecoderMpeg2 *)(decoder))

typedef struct _GstVaapiDecoderMpeg2Private GstVaapiDecoderMpeg2Private;
typedef struct _GstVaapiDecoderMpeg2Class GstVaapiDecoderMpeg2Class;

typedef enum
{
  GST_MPEG_VIDEO_STATE_GOT_SEQ_HDR = 1 << 0,
  GST_MPEG_VIDEO_STATE_GOT_SEQ_EXT = 1 << 1,
  GST_MPEG_VIDEO_STATE_GOT_PIC_HDR = 1 << 2,
  GST_MPEG_VIDEO_STATE_GOT_PIC_EXT = 1 << 3,
  GST_MPEG_VIDEO_STATE_GOT_SLICE = 1 << 4,

  GST_MPEG_VIDEO_STATE_VALID_SEQ_HEADERS = (GST_MPEG_VIDEO_STATE_GOT_SEQ_HDR |
      GST_MPEG_VIDEO_STATE_GOT_SEQ_EXT),
  GST_MPEG_VIDEO_STATE_VALID_PIC_HEADERS = (GST_MPEG_VIDEO_STATE_GOT_PIC_HDR |
      GST_MPEG_VIDEO_STATE_GOT_PIC_EXT),
  GST_MPEG_VIDEO_STATE_VALID_PICTURE = (GST_MPEG_VIDEO_STATE_VALID_SEQ_HEADERS |
      GST_MPEG_VIDEO_STATE_VALID_PIC_HEADERS | GST_MPEG_VIDEO_STATE_GOT_SLICE)
} GstMpegVideoState;

struct _GstVaapiDecoderMpeg2Private
{
  GstVaapiProfile profile;
  GstVaapiProfile hw_profile;
  guint width;
  guint height;
  guint fps_n;
  guint fps_d;
  guint state;
  GstVaapiRectangle crop_rect;
  GstVaapiParserInfoMpeg2 *seq_hdr;
  GstVaapiParserInfoMpeg2 *seq_ext;
  GstVaapiParserInfoMpeg2 *seq_display_ext;
  GstVaapiParserInfoMpeg2 *seq_scalable_ext;
  GstVaapiParserInfoMpeg2 *gop;
  GstVaapiParserInfoMpeg2 *pic_hdr;
  GstVaapiParserInfoMpeg2 *pic_ext;
  GstVaapiParserInfoMpeg2 *pic_display_ext;
  GstVaapiParserInfoMpeg2 *quant_matrix;
  GstVaapiParserInfoMpeg2 *slice_hdr;
  GstVaapiPicture *current_picture;
  GstVaapiDpb *dpb;
  PTSGenerator tsg;
  guint is_opened:1;
  guint size_changed:1;
  guint profile_changed:1;
  guint quant_matrix_changed:1;
  guint progressive_sequence:1;
  guint closed_gop:1;
  guint broken_link:1;
};

/**
 * GstVaapiDecoderMpeg2:
 *
 * A decoder based on Mpeg2.
 */
struct _GstVaapiDecoderMpeg2
{
  /*< private > */
  GstVaapiDecoder parent_instance;
  GstVaapiDecoderMpeg2Private priv;
};

/**
 * GstVaapiDecoderMpeg2Class:
 *
 * A decoder class based on Mpeg2.
 */
struct _GstVaapiDecoderMpeg2Class
{
  /*< private > */
  GstVaapiDecoderClass parent_class;
};

G_DEFINE_TYPE (GstVaapiDecoderMpeg2, gst_vaapi_decoder_mpeg2,
    GST_TYPE_VAAPI_DECODER);

static void
gst_vaapi_decoder_mpeg2_close (GstVaapiDecoderMpeg2 * decoder)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;

  gst_vaapi_picture_replace (&priv->current_picture, NULL);

  gst_vaapi_parser_info_mpeg2_replace (&priv->seq_hdr, NULL);
  gst_vaapi_parser_info_mpeg2_replace (&priv->seq_ext, NULL);
  gst_vaapi_parser_info_mpeg2_replace (&priv->seq_display_ext, NULL);
  gst_vaapi_parser_info_mpeg2_replace (&priv->seq_scalable_ext, NULL);
  gst_vaapi_parser_info_mpeg2_replace (&priv->gop, NULL);
  gst_vaapi_parser_info_mpeg2_replace (&priv->pic_hdr, NULL);
  gst_vaapi_parser_info_mpeg2_replace (&priv->pic_ext, NULL);
  gst_vaapi_parser_info_mpeg2_replace (&priv->pic_display_ext, NULL);
  gst_vaapi_parser_info_mpeg2_replace (&priv->quant_matrix, NULL);
  gst_vaapi_parser_info_mpeg2_replace (&priv->slice_hdr, NULL);

  priv->state = 0;

  gst_vaapi_dpb_replace (&priv->dpb, NULL);

  priv->is_opened = FALSE;
}

static gboolean
gst_vaapi_decoder_mpeg2_open (GstVaapiDecoderMpeg2 * decoder)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;

  gst_vaapi_decoder_mpeg2_close (decoder);

  priv->dpb = gst_vaapi_dpb_new (2);
  if (!priv->dpb)
    return FALSE;

  pts_init (&priv->tsg);
  return TRUE;
}

static void
gst_vaapi_decoder_mpeg2_destroy (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderMpeg2 *const decoder =
      GST_VAAPI_DECODER_MPEG2_CAST (base_decoder);

  gst_vaapi_decoder_mpeg2_close (decoder);
}

static gboolean
gst_vaapi_decoder_mpeg2_create (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderMpeg2 *const decoder =
      GST_VAAPI_DECODER_MPEG2_CAST (base_decoder);
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;

  priv->hw_profile = GST_VAAPI_PROFILE_UNKNOWN;
  priv->profile = GST_VAAPI_PROFILE_MPEG2_SIMPLE;
  priv->profile_changed = TRUE; /* Allow fallbacks to work */
  return TRUE;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_mpeg2_reset (GstVaapiDecoder * base_decoder)
{
  gst_vaapi_decoder_mpeg2_destroy (base_decoder);
  gst_vaapi_decoder_mpeg2_create (base_decoder);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline void
copy_quant_matrix (guint8 dst[64], const guint8 src[64])
{
  memcpy (dst, src, 64);
}

static const char *
get_profile_str (GstVaapiProfile profile)
{
  const char *str;

  switch (profile) {
    case GST_VAAPI_PROFILE_MPEG2_SIMPLE:
      str = "simple";
      break;
    case GST_VAAPI_PROFILE_MPEG2_MAIN:
      str = "main";
      break;
    case GST_VAAPI_PROFILE_MPEG2_HIGH:
      str = "high";
      break;
    default:
      str = "<unknown>";
      break;
  }
  return str;
}

static GstVaapiProfile
get_profile (GstVaapiDecoderMpeg2 * decoder, GstVaapiEntrypoint entrypoint)
{
  GstVaapiDisplay *const va_display = GST_VAAPI_DECODER_DISPLAY (decoder);
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstVaapiProfile profile = priv->profile;

  do {
    /* Return immediately if the exact same profile was found */
    if (gst_vaapi_display_has_decoder (va_display, profile, entrypoint))
      break;

    /* Otherwise, try to map to a higher profile */
    switch (profile) {
      case GST_VAAPI_PROFILE_MPEG2_SIMPLE:
        profile = GST_VAAPI_PROFILE_MPEG2_MAIN;
        break;
      case GST_VAAPI_PROFILE_MPEG2_MAIN:
        profile = GST_VAAPI_PROFILE_MPEG2_HIGH;
        break;
      case GST_VAAPI_PROFILE_MPEG2_HIGH:
        // Try to map to main profile if no high profile specific bits used
        if (priv->profile == profile &&
            !priv->seq_scalable_ext &&
            (priv->seq_ext && priv->seq_ext->data.seq_ext.chroma_format == 1)) {
          profile = GST_VAAPI_PROFILE_MPEG2_MAIN;
          break;
        }
        // fall-through
      default:
        profile = GST_VAAPI_PROFILE_UNKNOWN;
        break;
    }
  } while (profile != GST_VAAPI_PROFILE_UNKNOWN);

  if (profile != priv->profile)
    GST_INFO ("forced %s profile to %s profile",
        get_profile_str (priv->profile), get_profile_str (profile));
  return profile;
}

static GstVaapiDecoderStatus
ensure_context (GstVaapiDecoderMpeg2 * decoder)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_VLD;
  gboolean reset_context = FALSE;

  if (priv->profile_changed) {
    GST_DEBUG ("profile changed");
    priv->profile_changed = FALSE;
    reset_context = TRUE;

    priv->hw_profile = get_profile (decoder, entrypoint);
    if (priv->hw_profile == GST_VAAPI_PROFILE_UNKNOWN)
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  }

  if (priv->size_changed) {
    GST_DEBUG ("size changed");
    priv->size_changed = FALSE;
    reset_context = TRUE;
  }

  if (reset_context) {
    GstVaapiContextInfo info;
    /* *INDENT-OFF* */
    info = (GstVaapiContextInfo) {
      .profile = priv->hw_profile,
      .entrypoint = entrypoint,
      .chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420,
      .width = priv->width,
      .height = priv->height,
      .ref_frames = 2,
    };
    /* *INDENT-ON* */

    reset_context =
        gst_vaapi_decoder_ensure_context (GST_VAAPI_DECODER_CAST (decoder),
        &info);
    if (!reset_context)
      return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
ensure_quant_matrix (GstVaapiDecoderMpeg2 * decoder, GstVaapiPicture * picture)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoSequenceHdr *const seq_hdr = &priv->seq_hdr->data.seq_hdr;
  VAIQMatrixBufferMPEG2 *iq_matrix;
  guint8 *intra_quant_matrix = NULL;
  guint8 *non_intra_quant_matrix = NULL;
  guint8 *chroma_intra_quant_matrix = NULL;
  guint8 *chroma_non_intra_quant_matrix = NULL;

  if (!priv->quant_matrix_changed)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  priv->quant_matrix_changed = FALSE;

  picture->iq_matrix = GST_VAAPI_IQ_MATRIX_NEW (MPEG2, decoder);
  if (!picture->iq_matrix) {
    GST_ERROR ("failed to allocate IQ matrix");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  iq_matrix = picture->iq_matrix->param;

  intra_quant_matrix = seq_hdr->intra_quantizer_matrix;
  non_intra_quant_matrix = seq_hdr->non_intra_quantizer_matrix;

  if (priv->quant_matrix) {
    GstMpegVideoQuantMatrixExt *const quant_matrix =
        &priv->quant_matrix->data.quant_matrix;
    if (quant_matrix->load_intra_quantiser_matrix)
      intra_quant_matrix = quant_matrix->intra_quantiser_matrix;
    if (quant_matrix->load_non_intra_quantiser_matrix)
      non_intra_quant_matrix = quant_matrix->non_intra_quantiser_matrix;
    if (quant_matrix->load_chroma_intra_quantiser_matrix)
      chroma_intra_quant_matrix = quant_matrix->chroma_intra_quantiser_matrix;
    if (quant_matrix->load_chroma_non_intra_quantiser_matrix)
      chroma_non_intra_quant_matrix =
          quant_matrix->chroma_non_intra_quantiser_matrix;
  }

  iq_matrix->load_intra_quantiser_matrix = intra_quant_matrix != NULL;
  if (intra_quant_matrix)
    copy_quant_matrix (iq_matrix->intra_quantiser_matrix, intra_quant_matrix);

  iq_matrix->load_non_intra_quantiser_matrix = non_intra_quant_matrix != NULL;
  if (non_intra_quant_matrix)
    copy_quant_matrix (iq_matrix->non_intra_quantiser_matrix,
        non_intra_quant_matrix);

  iq_matrix->load_chroma_intra_quantiser_matrix =
      chroma_intra_quant_matrix != NULL;
  if (chroma_intra_quant_matrix)
    copy_quant_matrix (iq_matrix->chroma_intra_quantiser_matrix,
        chroma_intra_quant_matrix);

  iq_matrix->load_chroma_non_intra_quantiser_matrix =
      chroma_non_intra_quant_matrix != NULL;
  if (chroma_non_intra_quant_matrix)
    copy_quant_matrix (iq_matrix->chroma_non_intra_quantiser_matrix,
        chroma_non_intra_quant_matrix);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline gboolean
is_valid_state (GstVaapiDecoderMpeg2 * decoder, guint state)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;

  return (priv->state & state) == state;
}

static GstVaapiDecoderStatus
decode_current_picture (GstVaapiDecoderMpeg2 * decoder)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstVaapiPicture *const picture = priv->current_picture;

  if (!is_valid_state (decoder, GST_MPEG_VIDEO_STATE_VALID_PICTURE))
    goto drop_frame;
  priv->state &= GST_MPEG_VIDEO_STATE_VALID_SEQ_HEADERS;

  if (!picture)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  if (!gst_vaapi_picture_decode (picture))
    goto error;
  if (GST_VAAPI_PICTURE_IS_COMPLETE (picture)) {
    if (!gst_vaapi_dpb_add (priv->dpb, picture))
      goto error;
    gst_vaapi_picture_replace (&priv->current_picture, NULL);
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;

  /* ERRORS */
error:
  {
    /* XXX: fix for cases where first field failed to be decoded */
    gst_vaapi_picture_replace (&priv->current_picture, NULL);
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

drop_frame:
  {
    priv->state &= GST_MPEG_VIDEO_STATE_VALID_SEQ_HEADERS;
    return (GstVaapiDecoderStatus) GST_VAAPI_DECODER_STATUS_DROP_FRAME;
  }
}

static GstVaapiDecoderStatus
parse_sequence (GstVaapiDecoderMpeg2 * decoder,
    GstVaapiDecoderUnit * unit, const GstMpegVideoPacket * packet)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoSequenceHdr *seq_hdr;

  priv->state = 0;

  if (!gst_vaapi_parser_info_mpeg2_ensure (&priv->seq_hdr)) {
    GST_ERROR ("failed to allocate parser info for sequence header");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  seq_hdr = &priv->seq_hdr->data.seq_hdr;

  if (!gst_mpeg_video_packet_parse_sequence_header (packet, seq_hdr)) {
    GST_ERROR ("failed to parse sequence header");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  gst_vaapi_decoder_unit_set_parsed_info (unit, seq_hdr, NULL);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sequence (GstVaapiDecoderMpeg2 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER_CAST (decoder);
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoSequenceHdr *const seq_hdr = unit->parsed_info;

  gst_vaapi_parser_info_mpeg2_replace (&priv->seq_ext, NULL);
  gst_vaapi_parser_info_mpeg2_replace (&priv->seq_display_ext, NULL);
  gst_vaapi_parser_info_mpeg2_replace (&priv->seq_scalable_ext, NULL);
  gst_vaapi_parser_info_mpeg2_replace (&priv->quant_matrix, NULL);
  gst_vaapi_parser_info_mpeg2_replace (&priv->pic_display_ext, NULL);

  priv->fps_n = seq_hdr->fps_n;
  priv->fps_d = seq_hdr->fps_d;
  pts_set_framerate (&priv->tsg, priv->fps_n, priv->fps_d);
  gst_vaapi_decoder_set_framerate (base_decoder, priv->fps_n, priv->fps_d);

  priv->width = seq_hdr->width;
  priv->height = seq_hdr->height;
  priv->size_changed = TRUE;
  priv->quant_matrix_changed = TRUE;
  priv->progressive_sequence = TRUE;

  priv->state |= GST_MPEG_VIDEO_STATE_GOT_SEQ_HDR;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_sequence_ext (GstVaapiDecoderMpeg2 * decoder,
    GstVaapiDecoderUnit * unit, const GstMpegVideoPacket * packet)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoSequenceExt *seq_ext;

  priv->state &= GST_MPEG_VIDEO_STATE_GOT_SEQ_HDR;

  if (!gst_vaapi_parser_info_mpeg2_ensure (&priv->seq_ext)) {
    GST_ERROR ("failed to allocate parser info for sequence extension");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  seq_ext = &priv->seq_ext->data.seq_ext;

  if (!gst_mpeg_video_packet_parse_sequence_extension (packet, seq_ext)) {
    GST_ERROR ("failed to parse sequence-extension");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  gst_vaapi_decoder_unit_set_parsed_info (unit, seq_ext, NULL);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sequence_ext (GstVaapiDecoderMpeg2 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER_CAST (decoder);
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoSequenceExt *const seq_ext = unit->parsed_info;
  GstVaapiProfile profile;
  guint width, height;

  if (!is_valid_state (decoder, GST_MPEG_VIDEO_STATE_GOT_SEQ_HDR))
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  priv->progressive_sequence = seq_ext->progressive;
  gst_vaapi_decoder_set_interlaced (base_decoder, !priv->progressive_sequence);

  width = (priv->width & 0x0fff) | ((guint32) seq_ext->horiz_size_ext << 12);
  height = (priv->height & 0x0fff) | ((guint32) seq_ext->vert_size_ext << 12);
  GST_DEBUG ("video resolution %ux%u", width, height);

  if (seq_ext->fps_n_ext && seq_ext->fps_d_ext) {
    priv->fps_n *= seq_ext->fps_n_ext + 1;
    priv->fps_d *= seq_ext->fps_d_ext + 1;
    pts_set_framerate (&priv->tsg, priv->fps_n, priv->fps_d);
    gst_vaapi_decoder_set_framerate (base_decoder, priv->fps_n, priv->fps_d);
  }

  if (priv->width != width) {
    priv->width = width;
    priv->size_changed = TRUE;
  }

  if (priv->height != height) {
    priv->height = height;
    priv->size_changed = TRUE;
  }

  switch (seq_ext->profile) {
    case GST_MPEG_VIDEO_PROFILE_SIMPLE:
      profile = GST_VAAPI_PROFILE_MPEG2_SIMPLE;
      break;
    case GST_MPEG_VIDEO_PROFILE_MAIN:
      profile = GST_VAAPI_PROFILE_MPEG2_MAIN;
      break;
    case GST_MPEG_VIDEO_PROFILE_HIGH:
      profile = GST_VAAPI_PROFILE_MPEG2_HIGH;
      break;
    default:
      GST_ERROR ("unsupported profile %d", seq_ext->profile);
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  }
  if (priv->profile != profile) {
    priv->profile = profile;
    priv->profile_changed = TRUE;
  }

  priv->state |= GST_MPEG_VIDEO_STATE_GOT_SEQ_EXT;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_sequence_display_ext (GstVaapiDecoderMpeg2 * decoder,
    GstVaapiDecoderUnit * unit, const GstMpegVideoPacket * packet)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoSequenceDisplayExt *seq_display_ext;

  if (!gst_vaapi_parser_info_mpeg2_ensure (&priv->seq_display_ext)) {
    GST_ERROR ("failed to allocate parser info for sequence display extension");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  seq_display_ext = &priv->seq_display_ext->data.seq_display_ext;

  if (!gst_mpeg_video_packet_parse_sequence_display_extension (packet,
          seq_display_ext)) {
    GST_ERROR ("failed to parse sequence-display-extension");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  gst_vaapi_decoder_unit_set_parsed_info (unit, seq_display_ext, NULL);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sequence_display_ext (GstVaapiDecoderMpeg2 * decoder,
    GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoSequenceDisplayExt *seq_display_ext;

  seq_display_ext = priv->seq_display_ext ?
      &priv->seq_display_ext->data.seq_display_ext : NULL;

  /* Update cropping rectangle */
  if (seq_display_ext) {
    GstVaapiRectangle *const crop_rect = &priv->crop_rect;
    crop_rect->x = 0;
    crop_rect->y = 0;
    crop_rect->width = seq_display_ext->display_horizontal_size;
    crop_rect->height = seq_display_ext->display_vertical_size;
  }

  /* XXX: handle color primaries */
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_sequence_scalable_ext (GstVaapiDecoderMpeg2 * decoder,
    GstVaapiDecoderUnit * unit, const GstMpegVideoPacket * packet)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoSequenceScalableExt *seq_scalable_ext;

  if (!gst_vaapi_parser_info_mpeg2_ensure (&priv->seq_scalable_ext)) {
    GST_ERROR
        ("failed to allocate parser info for sequence scalable extension");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  seq_scalable_ext = &priv->seq_scalable_ext->data.seq_scalable_ext;

  if (!gst_mpeg_video_packet_parse_sequence_scalable_extension (packet,
          seq_scalable_ext)) {
    GST_ERROR ("failed to parse sequence-scalable-extension");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  gst_vaapi_decoder_unit_set_parsed_info (unit, seq_scalable_ext, NULL);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sequence_scalable_ext (GstVaapiDecoderMpeg2 * decoder,
    GstVaapiDecoderUnit * unit)
{
  /* XXX: unsupported header -- ignore */
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sequence_end (GstVaapiDecoderMpeg2 * decoder)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;

  if (priv->dpb)
    gst_vaapi_dpb_flush (priv->dpb);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_quant_matrix_ext (GstVaapiDecoderMpeg2 * decoder,
    GstVaapiDecoderUnit * unit, const GstMpegVideoPacket * packet)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoQuantMatrixExt *quant_matrix;

  if (!gst_vaapi_parser_info_mpeg2_ensure (&priv->quant_matrix)) {
    GST_ERROR ("failed to allocate parser info for quantization matrix");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  quant_matrix = &priv->quant_matrix->data.quant_matrix;

  if (!gst_mpeg_video_packet_parse_quant_matrix_extension (packet,
          quant_matrix)) {
    GST_ERROR ("failed to parse quant-matrix-extension");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  gst_vaapi_decoder_unit_set_parsed_info (unit, quant_matrix, NULL);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_quant_matrix_ext (GstVaapiDecoderMpeg2 * decoder,
    GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;

  priv->quant_matrix_changed = TRUE;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_gop (GstVaapiDecoderMpeg2 * decoder,
    GstVaapiDecoderUnit * unit, const GstMpegVideoPacket * packet)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoGop *gop;

  if (!gst_vaapi_parser_info_mpeg2_ensure (&priv->gop)) {
    GST_ERROR ("failed to allocate parser info for GOP");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  gop = &priv->gop->data.gop;

  if (!gst_mpeg_video_packet_parse_gop (packet, gop)) {
    GST_ERROR ("failed to parse GOP");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  gst_vaapi_decoder_unit_set_parsed_info (unit, gop, NULL);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_gop (GstVaapiDecoderMpeg2 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoGop *const gop = unit->parsed_info;

  priv->closed_gop = gop->closed_gop;
  priv->broken_link = gop->broken_link;

  GST_DEBUG ("GOP %02u:%02u:%02u:%02u (closed_gop %d, broken_link %d)",
      gop->hour, gop->minute, gop->second, gop->frame,
      priv->closed_gop, priv->broken_link);

  pts_sync (&priv->tsg, GST_VAAPI_DECODER_CODEC_FRAME (decoder)->pts);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_picture (GstVaapiDecoderMpeg2 * decoder,
    GstVaapiDecoderUnit * unit, const GstMpegVideoPacket * packet)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoPictureHdr *pic_hdr;

  priv->state &= (GST_MPEG_VIDEO_STATE_GOT_SEQ_HDR |
      GST_MPEG_VIDEO_STATE_GOT_SEQ_EXT);

  if (!gst_vaapi_parser_info_mpeg2_ensure (&priv->pic_hdr)) {
    GST_ERROR ("failed to allocate parser info for picture header");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  pic_hdr = &priv->pic_hdr->data.pic_hdr;

  if (!gst_mpeg_video_packet_parse_picture_header (packet, pic_hdr)) {
    GST_ERROR ("failed to parse picture header");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  gst_vaapi_decoder_unit_set_parsed_info (unit, pic_hdr, NULL);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_picture (GstVaapiDecoderMpeg2 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;

  if (!is_valid_state (decoder, GST_MPEG_VIDEO_STATE_VALID_SEQ_HEADERS))
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  gst_vaapi_parser_info_mpeg2_replace (&priv->pic_ext, NULL);

  priv->state |= GST_MPEG_VIDEO_STATE_GOT_PIC_HDR;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
parse_picture_ext (GstVaapiDecoderMpeg2 * decoder,
    GstVaapiDecoderUnit * unit, const GstMpegVideoPacket * packet)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoPictureExt *pic_ext;

  priv->state &= (GST_MPEG_VIDEO_STATE_GOT_SEQ_HDR |
      GST_MPEG_VIDEO_STATE_GOT_SEQ_EXT | GST_MPEG_VIDEO_STATE_GOT_PIC_HDR);

  if (!gst_vaapi_parser_info_mpeg2_ensure (&priv->pic_ext)) {
    GST_ERROR ("failed to allocate parser info for picture extension");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  pic_ext = &priv->pic_ext->data.pic_ext;

  if (!gst_mpeg_video_packet_parse_picture_extension (packet, pic_ext)) {
    GST_ERROR ("failed to parse picture-extension");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  gst_vaapi_decoder_unit_set_parsed_info (unit, pic_ext, NULL);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_picture_ext (GstVaapiDecoderMpeg2 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoPictureExt *const pic_ext = unit->parsed_info;

  if (!is_valid_state (decoder, GST_MPEG_VIDEO_STATE_GOT_PIC_HDR))
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  if (priv->progressive_sequence && !pic_ext->progressive_frame) {
    GST_WARNING ("invalid interlaced frame in progressive sequence, fixing");
    pic_ext->progressive_frame = 1;
  }

  if (pic_ext->picture_structure == 0 ||
      (pic_ext->progressive_frame &&
          pic_ext->picture_structure !=
          GST_MPEG_VIDEO_PICTURE_STRUCTURE_FRAME)) {
    GST_WARNING ("invalid picture_structure %d, replacing with \"frame\"",
        pic_ext->picture_structure);
    pic_ext->picture_structure = GST_MPEG_VIDEO_PICTURE_STRUCTURE_FRAME;
  }

  priv->state |= GST_MPEG_VIDEO_STATE_GOT_PIC_EXT;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline guint32
pack_f_code (guint8 f_code[2][2])
{
  return (((guint32) f_code[0][0] << 12) |
      ((guint32) f_code[0][1] << 8) |
      ((guint32) f_code[1][0] << 4) | (f_code[1][1]));
}

static GstVaapiDecoderStatus
init_picture (GstVaapiDecoderMpeg2 * decoder, GstVaapiPicture * picture)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoPictureHdr *const pic_hdr = &priv->pic_hdr->data.pic_hdr;
  GstMpegVideoPictureExt *const pic_ext = &priv->pic_ext->data.pic_ext;

  switch (pic_hdr->pic_type) {
    case GST_MPEG_VIDEO_PICTURE_TYPE_I:
      GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_REFERENCE);
      picture->type = GST_VAAPI_PICTURE_TYPE_I;
      break;
    case GST_MPEG_VIDEO_PICTURE_TYPE_P:
      GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_REFERENCE);
      picture->type = GST_VAAPI_PICTURE_TYPE_P;
      break;
    case GST_MPEG_VIDEO_PICTURE_TYPE_B:
      picture->type = GST_VAAPI_PICTURE_TYPE_B;
      break;
    default:
      GST_ERROR ("unsupported picture type %d", pic_hdr->pic_type);
      return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  if (!priv->progressive_sequence && !pic_ext->progressive_frame) {
    GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_INTERLACED);
    if (pic_ext->top_field_first)
      GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_TFF);
  }

  switch (pic_ext->picture_structure) {
    case GST_MPEG_VIDEO_PICTURE_STRUCTURE_TOP_FIELD:
      picture->structure = GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD;
      break;
    case GST_MPEG_VIDEO_PICTURE_STRUCTURE_BOTTOM_FIELD:
      picture->structure = GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD;
      break;
    case GST_MPEG_VIDEO_PICTURE_STRUCTURE_FRAME:
      picture->structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
      break;
  }

  /* Allocate dummy picture for first field based I-frame */
  if (picture->type == GST_VAAPI_PICTURE_TYPE_I &&
      !GST_VAAPI_PICTURE_IS_FRAME (picture) &&
      gst_vaapi_dpb_size (priv->dpb) == 0) {
    GstVaapiPicture *dummy_picture;
    gboolean success;

    dummy_picture = GST_VAAPI_PICTURE_NEW (MPEG2, decoder);
    if (!dummy_picture) {
      GST_ERROR ("failed to allocate dummy picture");
      return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }

    dummy_picture->type = GST_VAAPI_PICTURE_TYPE_I;
    dummy_picture->pts = GST_CLOCK_TIME_NONE;
    dummy_picture->poc = -1;
    dummy_picture->structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;

    GST_VAAPI_PICTURE_FLAG_SET (dummy_picture,
        (GST_VAAPI_PICTURE_FLAG_SKIPPED |
            GST_VAAPI_PICTURE_FLAG_OUTPUT | GST_VAAPI_PICTURE_FLAG_REFERENCE)
        );

    success = gst_vaapi_dpb_add (priv->dpb, dummy_picture);
    gst_vaapi_picture_unref (dummy_picture);
    if (!success) {
      GST_ERROR ("failed to add dummy picture into DPB");
      return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }
    GST_INFO ("allocated dummy picture for first field based I-frame");
  }

  /* Update presentation time */
  picture->pts = pts_eval (&priv->tsg,
      GST_VAAPI_DECODER_CODEC_FRAME (decoder)->pts, pic_hdr->tsn);
  picture->poc = pts_get_poc (&priv->tsg);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
fill_picture (GstVaapiDecoderMpeg2 * decoder, GstVaapiPicture * picture)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  VAPictureParameterBufferMPEG2 *const pic_param = picture->param;
  GstMpegVideoPictureHdr *const pic_hdr = &priv->pic_hdr->data.pic_hdr;
  GstMpegVideoPictureExt *const pic_ext = &priv->pic_ext->data.pic_ext;
  GstVaapiPicture *prev_picture, *next_picture;

  /* Fill in VAPictureParameterBufferMPEG2 */
  pic_param->horizontal_size = priv->width;
  pic_param->vertical_size = priv->height;
  pic_param->forward_reference_picture = VA_INVALID_ID;
  pic_param->backward_reference_picture = VA_INVALID_ID;
  pic_param->picture_coding_type = pic_hdr->pic_type;
  pic_param->f_code = pack_f_code (pic_ext->f_code);

#define COPY_FIELD(a, b, f) \
    pic_param->a.b.f = pic_ext->f
  pic_param->picture_coding_extension.value = 0;
  pic_param->picture_coding_extension.bits.is_first_field =
      GST_VAAPI_PICTURE_IS_FIRST_FIELD (picture);
  COPY_FIELD (picture_coding_extension, bits, intra_dc_precision);
  COPY_FIELD (picture_coding_extension, bits, picture_structure);
  COPY_FIELD (picture_coding_extension, bits, top_field_first);
  COPY_FIELD (picture_coding_extension, bits, frame_pred_frame_dct);
  COPY_FIELD (picture_coding_extension, bits, concealment_motion_vectors);
  COPY_FIELD (picture_coding_extension, bits, q_scale_type);
  COPY_FIELD (picture_coding_extension, bits, intra_vlc_format);
  COPY_FIELD (picture_coding_extension, bits, alternate_scan);
  COPY_FIELD (picture_coding_extension, bits, repeat_first_field);
  COPY_FIELD (picture_coding_extension, bits, progressive_frame);

  gst_vaapi_dpb_get_neighbours (priv->dpb, picture,
      &prev_picture, &next_picture);

  switch (pic_hdr->pic_type) {
    case GST_MPEG_VIDEO_PICTURE_TYPE_B:
      if (next_picture)
        pic_param->backward_reference_picture = next_picture->surface_id;
      if (prev_picture)
        pic_param->forward_reference_picture = prev_picture->surface_id;
      else if (!priv->closed_gop)
        GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_SKIPPED);
      break;
    case GST_MPEG_VIDEO_PICTURE_TYPE_P:
      if (prev_picture)
        pic_param->forward_reference_picture = prev_picture->surface_id;
      break;
  }
}

static GstVaapiDecoderStatus
parse_slice (GstVaapiDecoderMpeg2 * decoder,
    GstVaapiDecoderUnit * unit, const GstMpegVideoPacket * packet)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoSliceHdr *slice_hdr;
  GstMpegVideoSequenceHdr *seq_hdr;
  GstMpegVideoSequenceScalableExt *seq_scalable_ext;

  priv->state &= (GST_MPEG_VIDEO_STATE_GOT_SEQ_HDR |
      GST_MPEG_VIDEO_STATE_GOT_SEQ_EXT |
      GST_MPEG_VIDEO_STATE_GOT_PIC_HDR | GST_MPEG_VIDEO_STATE_GOT_PIC_EXT);

  if (!is_valid_state (decoder, GST_MPEG_VIDEO_STATE_VALID_PIC_HEADERS))
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  if (!gst_vaapi_parser_info_mpeg2_ensure (&priv->slice_hdr)) {
    GST_ERROR ("failed to allocate parser info for slice header");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  slice_hdr = &priv->slice_hdr->data.slice_hdr;
  seq_hdr = &priv->seq_hdr->data.seq_hdr;
  seq_scalable_ext = priv->seq_scalable_ext ?
      &priv->seq_scalable_ext->data.seq_scalable_ext : NULL;

  if (!gst_mpeg_video_packet_parse_slice_header (packet, slice_hdr,
          seq_hdr, seq_scalable_ext)) {
    GST_ERROR ("failed to parse slice header");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  gst_vaapi_decoder_unit_set_parsed_info (unit, slice_hdr, NULL);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_slice (GstVaapiDecoderMpeg2 * decoder, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstVaapiPicture *const picture = priv->current_picture;
  GstVaapiSlice *slice;
  VASliceParameterBufferMPEG2 *slice_param;
  GstMpegVideoSliceHdr *const slice_hdr = unit->parsed_info;
  GstBuffer *const buffer =
      GST_VAAPI_DECODER_CODEC_FRAME (decoder)->input_buffer;
  GstMapInfo map_info;

  if (!is_valid_state (decoder, GST_MPEG_VIDEO_STATE_VALID_PIC_HEADERS))
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  if (!gst_buffer_map (buffer, &map_info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  GST_DEBUG ("slice %d (%u bytes)", slice_hdr->mb_row, unit->size);

  slice = GST_VAAPI_SLICE_NEW (MPEG2, decoder,
      (map_info.data + unit->offset), unit->size);
  gst_buffer_unmap (buffer, &map_info);
  if (!slice) {
    GST_ERROR ("failed to allocate slice");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  gst_vaapi_picture_add_slice (picture, slice);

  /* Fill in VASliceParameterBufferMPEG2 */
  slice_param = slice->param;
  slice_param->macroblock_offset = slice_hdr->header_size + 32;
  slice_param->slice_horizontal_position = slice_hdr->mb_column;
  slice_param->slice_vertical_position = slice_hdr->mb_row;
  slice_param->quantiser_scale_code = slice_hdr->quantiser_scale_code;
  slice_param->intra_slice_flag = slice_hdr->intra_slice;

  priv->state |= GST_MPEG_VIDEO_STATE_GOT_SLICE;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline gint
scan_for_start_code (const guchar * buf, guint buf_size,
    GstMpegVideoPacketTypeCode * type_ptr)
{
  guint i = 0;

  while (i <= (buf_size - 4)) {
    if (buf[i + 2] > 1)
      i += 3;
    else if (buf[i + 1])
      i += 2;
    else if (buf[i] || buf[i + 2] != 1)
      i++;
    else
      break;
  }

  if (i <= (buf_size - 4)) {
    if (type_ptr)
      *type_ptr = buf[i + 3];
    return i;
  }
  return -1;
}

static GstVaapiDecoderStatus
parse_unit (GstVaapiDecoderMpeg2 * decoder, GstVaapiDecoderUnit * unit,
    GstMpegVideoPacket * packet)
{
  GstMpegVideoPacketTypeCode type;
  GstMpegVideoPacketExtensionCode ext_type;
  GstVaapiDecoderStatus status;

  type = packet->type;
  switch (type) {
    case GST_MPEG_VIDEO_PACKET_PICTURE:
      status = parse_picture (decoder, unit, packet);
      break;
    case GST_MPEG_VIDEO_PACKET_SEQUENCE:
      status = parse_sequence (decoder, unit, packet);
      break;
    case GST_MPEG_VIDEO_PACKET_EXTENSION:
      ext_type = packet->data[4] >> 4;
      switch (ext_type) {
        case GST_MPEG_VIDEO_PACKET_EXT_SEQUENCE:
          status = parse_sequence_ext (decoder, unit, packet);
          break;
        case GST_MPEG_VIDEO_PACKET_EXT_SEQUENCE_DISPLAY:
          status = parse_sequence_display_ext (decoder, unit, packet);
          break;
        case GST_MPEG_VIDEO_PACKET_EXT_SEQUENCE_SCALABLE:
          status = parse_sequence_scalable_ext (decoder, unit, packet);
          break;
        case GST_MPEG_VIDEO_PACKET_EXT_QUANT_MATRIX:
          status = parse_quant_matrix_ext (decoder, unit, packet);
          break;
        case GST_MPEG_VIDEO_PACKET_EXT_PICTURE:
          status = parse_picture_ext (decoder, unit, packet);
          break;
        default:
          status = GST_VAAPI_DECODER_STATUS_SUCCESS;
          break;
      }
      break;
    case GST_MPEG_VIDEO_PACKET_GOP:
      status = parse_gop (decoder, unit, packet);
      break;
    default:
      if (type >= GST_MPEG_VIDEO_PACKET_SLICE_MIN &&
          type <= GST_MPEG_VIDEO_PACKET_SLICE_MAX) {
        status = parse_slice (decoder, unit, packet);
        break;
      }
      status = GST_VAAPI_DECODER_STATUS_SUCCESS;
      break;
  }
  return status;
}

static GstVaapiDecoderStatus
decode_unit (GstVaapiDecoderMpeg2 * decoder, GstVaapiDecoderUnit * unit,
    GstMpegVideoPacket * packet)
{
  GstMpegVideoPacketTypeCode type;
  GstMpegVideoPacketExtensionCode ext_type;
  GstVaapiDecoderStatus status;

  type = packet->type;
  switch (type) {
    case GST_MPEG_VIDEO_PACKET_PICTURE:
      status = decode_picture (decoder, unit);
      break;
    case GST_MPEG_VIDEO_PACKET_SEQUENCE:
      status = decode_sequence (decoder, unit);
      break;
    case GST_MPEG_VIDEO_PACKET_EXTENSION:
      ext_type = packet->data[4] >> 4;
      switch (ext_type) {
        case GST_MPEG_VIDEO_PACKET_EXT_SEQUENCE:
          status = decode_sequence_ext (decoder, unit);
          break;
        case GST_MPEG_VIDEO_PACKET_EXT_SEQUENCE_DISPLAY:
          status = decode_sequence_display_ext (decoder, unit);
          break;
        case GST_MPEG_VIDEO_PACKET_EXT_SEQUENCE_SCALABLE:
          status = decode_sequence_scalable_ext (decoder, unit);
          break;
        case GST_MPEG_VIDEO_PACKET_EXT_QUANT_MATRIX:
          status = decode_quant_matrix_ext (decoder, unit);
          break;
        case GST_MPEG_VIDEO_PACKET_EXT_PICTURE:
          status = decode_picture_ext (decoder, unit);
          break;
        default:
          // Ignore unknown start-code extensions
          GST_WARNING ("unsupported packet extension type 0x%02x", ext_type);
          status = GST_VAAPI_DECODER_STATUS_SUCCESS;
          break;
      }
      break;
    case GST_MPEG_VIDEO_PACKET_SEQUENCE_END:
      status = decode_sequence_end (decoder);
      break;
    case GST_MPEG_VIDEO_PACKET_GOP:
      status = decode_gop (decoder, unit);
      break;
    default:
      if (type >= GST_MPEG_VIDEO_PACKET_SLICE_MIN &&
          type <= GST_MPEG_VIDEO_PACKET_SLICE_MAX) {
        status = decode_slice (decoder, unit);
        break;
      }
      GST_WARNING ("unsupported packet type 0x%02x", type);
      status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
      break;
  }
  return status;
}

static GstVaapiDecoderStatus
ensure_decoder (GstVaapiDecoderMpeg2 * decoder)
{
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;

  if (!priv->is_opened) {
    priv->is_opened = gst_vaapi_decoder_mpeg2_open (decoder);
    if (!priv->is_opened)
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_mpeg2_parse (GstVaapiDecoder * base_decoder,
    GstAdapter * adapter, gboolean at_eos, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderMpeg2 *const decoder =
      GST_VAAPI_DECODER_MPEG2_CAST (base_decoder);
  GstVaapiParserState *const ps = GST_VAAPI_PARSER_STATE (base_decoder);
  GstVaapiDecoderStatus status;
  GstMpegVideoPacketTypeCode type, type2 = GST_MPEG_VIDEO_PACKET_NONE;
  const guchar *buf;
  guint buf_size, flags;
  gint ofs, ofs1, ofs2;

  status = ensure_decoder (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  buf_size = gst_adapter_available (adapter);
  if (buf_size < 4)
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

  buf = gst_adapter_map (adapter, buf_size);
  if (!buf)
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

  ofs = scan_for_start_code (buf, buf_size, &type);
  if (ofs < 0)
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
  ofs1 = ofs;

  ofs2 = ps->input_offset2 - 4;
  if (ofs2 < ofs1 + 4)
    ofs2 = ofs1 + 4;

  ofs = G_UNLIKELY (buf_size < ofs2 + 4) ? -1 :
      scan_for_start_code (&buf[ofs2], buf_size - ofs2, &type2);
  if (ofs < 0) {
    // Assume the whole packet is present if end-of-stream
    if (!at_eos) {
      ps->input_offset2 = buf_size;
      return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    }
    ofs = buf_size - ofs2;
  }
  ofs2 += ofs;

  unit->size = ofs2 - ofs1;
  gst_adapter_flush (adapter, ofs1);
  ps->input_offset2 = 4;

  /* Check for start of new picture */
  flags = 0;
  switch (type) {
    case GST_MPEG_VIDEO_PACKET_SEQUENCE_END:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END;
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_STREAM_END;
      break;
    case GST_MPEG_VIDEO_PACKET_USER_DATA:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SKIP;
      /* fall-through */
    case GST_MPEG_VIDEO_PACKET_SEQUENCE:
    case GST_MPEG_VIDEO_PACKET_GOP:
    case GST_MPEG_VIDEO_PACKET_PICTURE:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      break;
    case GST_MPEG_VIDEO_PACKET_EXTENSION:
      if (G_UNLIKELY (unit->size < 5))
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
      break;
    default:
      if (type >= GST_MPEG_VIDEO_PACKET_SLICE_MIN &&
          type <= GST_MPEG_VIDEO_PACKET_SLICE_MAX) {
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
        switch (type2) {
          case GST_MPEG_VIDEO_PACKET_USER_DATA:
          case GST_MPEG_VIDEO_PACKET_SEQUENCE:
          case GST_MPEG_VIDEO_PACKET_GOP:
          case GST_MPEG_VIDEO_PACKET_PICTURE:
            flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END;
            break;
          default:
            break;
        }
      }
      // Ignore system start codes (PES headers)
      else if (type >= 0xb9 && type <= 0xff)
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_SKIP;
      break;
  }
  GST_VAAPI_DECODER_UNIT_FLAG_SET (unit, flags);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_mpeg2_decode (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderMpeg2 *const decoder =
      GST_VAAPI_DECODER_MPEG2_CAST (base_decoder);
  GstVaapiDecoderStatus status;
  GstMpegVideoPacket packet;
  GstBuffer *const buffer =
      GST_VAAPI_DECODER_CODEC_FRAME (decoder)->input_buffer;
  GstMapInfo map_info;

  status = ensure_decoder (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  if (!gst_buffer_map (buffer, &map_info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  packet.data = map_info.data + unit->offset;
  packet.size = unit->size;
  packet.type = packet.data[3];
  packet.offset = 4;

  status = parse_unit (decoder, unit, &packet);
  gst_buffer_unmap (buffer, &map_info);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;
  return decode_unit (decoder, unit, &packet);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_mpeg2_start_frame (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * base_unit)
{
  GstVaapiDecoderMpeg2 *const decoder =
      GST_VAAPI_DECODER_MPEG2_CAST (base_decoder);
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;
  GstMpegVideoSequenceHdr *seq_hdr;
  GstMpegVideoSequenceExt *seq_ext;
  GstMpegVideoSequenceDisplayExt *seq_display_ext;
  GstVaapiPicture *picture;
  GstVaapiDecoderStatus status;

  if (!is_valid_state (decoder, GST_MPEG_VIDEO_STATE_VALID_PIC_HEADERS))
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
  priv->state &= ~GST_MPEG_VIDEO_STATE_VALID_PIC_HEADERS;

  seq_hdr = &priv->seq_hdr->data.seq_hdr;
  seq_ext = priv->seq_ext ? &priv->seq_ext->data.seq_ext : NULL;
  seq_display_ext = priv->seq_display_ext ?
      &priv->seq_display_ext->data.seq_display_ext : NULL;
  if (gst_mpeg_video_finalise_mpeg2_sequence_header (seq_hdr, seq_ext,
          seq_display_ext))
    gst_vaapi_decoder_set_pixel_aspect_ratio (base_decoder,
        seq_hdr->par_w, seq_hdr->par_h);

  status = ensure_context (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
    GST_ERROR ("failed to reset context");
    return status;
  }

  if (priv->current_picture) {
    /* Re-use current picture where the first field was decoded */
    picture = gst_vaapi_picture_new_field (priv->current_picture);
    if (!picture) {
      GST_ERROR ("failed to allocate field picture");
      return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
  } else {
    /* Create new picture */
    picture = GST_VAAPI_PICTURE_NEW (MPEG2, decoder);
    if (!picture) {
      GST_ERROR ("failed to allocate picture");
      return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
  }
  gst_vaapi_picture_replace (&priv->current_picture, picture);
  gst_vaapi_picture_unref (picture);

  /* Update cropping rectangle */
  /* XXX: handle picture_display_extension() */
  if (seq_display_ext && priv->pic_display_ext) {
    GstVaapiRectangle *const crop_rect = &priv->crop_rect;
    if (crop_rect->x + crop_rect->width <= priv->width &&
        crop_rect->y + crop_rect->height <= priv->height)
      gst_vaapi_picture_set_crop_rect (picture, crop_rect);
  }

  status = ensure_quant_matrix (decoder, picture);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
    GST_ERROR ("failed to reset quantizer matrix");
    return status;
  }

  status = init_picture (decoder, picture);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  fill_picture (decoder, picture);

  priv->state |= GST_MPEG_VIDEO_STATE_VALID_PIC_HEADERS;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_mpeg2_end_frame (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderMpeg2 *const decoder =
      GST_VAAPI_DECODER_MPEG2_CAST (base_decoder);

  return decode_current_picture (decoder);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_mpeg2_flush (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderMpeg2 *const decoder =
      GST_VAAPI_DECODER_MPEG2_CAST (base_decoder);
  GstVaapiDecoderMpeg2Private *const priv = &decoder->priv;

  if (priv->dpb)
    gst_vaapi_dpb_flush (priv->dpb);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
gst_vaapi_decoder_mpeg2_finalize (GObject * object)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (object);

  gst_vaapi_decoder_mpeg2_destroy (base_decoder);
  G_OBJECT_CLASS (gst_vaapi_decoder_mpeg2_parent_class)->finalize (object);
}

static void
gst_vaapi_decoder_mpeg2_class_init (GstVaapiDecoderMpeg2Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiDecoderClass *const decoder_class = GST_VAAPI_DECODER_CLASS (klass);

  object_class->finalize = gst_vaapi_decoder_mpeg2_finalize;

  decoder_class->reset = gst_vaapi_decoder_mpeg2_reset;
  decoder_class->parse = gst_vaapi_decoder_mpeg2_parse;
  decoder_class->decode = gst_vaapi_decoder_mpeg2_decode;
  decoder_class->start_frame = gst_vaapi_decoder_mpeg2_start_frame;
  decoder_class->end_frame = gst_vaapi_decoder_mpeg2_end_frame;
  decoder_class->flush = gst_vaapi_decoder_mpeg2_flush;
}

static void
gst_vaapi_decoder_mpeg2_init (GstVaapiDecoderMpeg2 * decoder)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (decoder);

  gst_vaapi_decoder_mpeg2_create (base_decoder);
}

/**
 * gst_vaapi_decoder_mpeg2_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps holding codec information
 *
 * Creates a new #GstVaapiDecoder for MPEG-2 decoding.  The @caps can
 * hold extra information like codec-data and pictured coded size.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_mpeg2_new (GstVaapiDisplay * display, GstCaps * caps)
{
  return g_object_new (GST_TYPE_VAAPI_DECODER_MPEG2, "display", display,
      "caps", caps, NULL);
}
