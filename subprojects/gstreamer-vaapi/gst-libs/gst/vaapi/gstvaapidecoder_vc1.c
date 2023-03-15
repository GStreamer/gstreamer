/*
 *  gstvaapidecoder_vc1.c - VC-1 decoder
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
 * SECTION:gstvaapidecoder_vc1
 * @short_description: VC-1 decoder
 */

#include "sysdeps.h"
#include <gst/codecparsers/gstvc1parser.h>
#include "gstvaapidecoder_vc1.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_dpb.h"
#include "gstvaapidecoder_unit.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_DECODER_VC1_CAST(decoder) \
    ((GstVaapiDecoderVC1 *)(decoder))

typedef struct _GstVaapiDecoderVC1Private GstVaapiDecoderVC1Private;
typedef struct _GstVaapiDecoderVC1Class GstVaapiDecoderVC1Class;

/**
 * GstVaapiDecoderVC1:
 *
 * A decoder based on VC1.
 */
struct _GstVaapiDecoderVC1Private
{
  GstVaapiProfile profile;
  guint width;
  guint height;
  GstVC1SeqHdr seq_hdr;
  GstVC1EntryPointHdr entrypoint_hdr;
  GstVC1FrameHdr frame_hdr;
  GstVC1BitPlanes *bitplanes;
  GstVaapiPicture *current_picture;
  GstVaapiPicture *last_non_b_picture;
  GstVaapiDpb *dpb;
  gint32 next_poc;
  guint8 *rbdu_buffer;
  guint8 rndctrl;
  guint rbdu_buffer_size;
  guint is_opened:1;
  guint has_codec_data:1;
  guint has_entrypoint:1;
  guint size_changed:1;
  guint profile_changed:1;
  guint closed_entry:1;
  guint broken_link:1;
};

/**
 * GstVaapiDecoderVC1:
 *
 * A decoder based on VC1.
 */
struct _GstVaapiDecoderVC1
{
  /*< private > */
  GstVaapiDecoder parent_instance;
  GstVaapiDecoderVC1Private priv;
};

/**
 * GstVaapiDecoderVC1Class:
 *
 * A decoder class based on VC1.
 */
struct _GstVaapiDecoderVC1Class
{
  /*< private > */
  GstVaapiDecoderClass parent_class;
};

G_DEFINE_TYPE (GstVaapiDecoderVC1, gst_vaapi_decoder_vc1,
    GST_TYPE_VAAPI_DECODER);

static GstVaapiDecoderStatus
get_status (GstVC1ParserResult result)
{
  GstVaapiDecoderStatus status;

  switch (result) {
    case GST_VC1_PARSER_OK:
      status = GST_VAAPI_DECODER_STATUS_SUCCESS;
      break;
    case GST_VC1_PARSER_NO_BDU_END:
      status = GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
      break;
    case GST_VC1_PARSER_ERROR:
      status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
      break;
    default:
      status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
      break;
  }
  return status;
}

static void
gst_vaapi_decoder_vc1_close (GstVaapiDecoderVC1 * decoder)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;

  gst_vaapi_picture_replace (&priv->last_non_b_picture, NULL);
  gst_vaapi_picture_replace (&priv->current_picture, NULL);
  gst_vaapi_dpb_replace (&priv->dpb, NULL);

  if (priv->bitplanes) {
    gst_vc1_bitplanes_free (priv->bitplanes);
    priv->bitplanes = NULL;
  }
  priv->is_opened = FALSE;
}

static gboolean
gst_vaapi_decoder_vc1_open (GstVaapiDecoderVC1 * decoder)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;

  gst_vaapi_decoder_vc1_close (decoder);

  priv->dpb = gst_vaapi_dpb_new (2);
  if (!priv->dpb)
    return FALSE;

  priv->bitplanes = gst_vc1_bitplanes_new ();
  if (!priv->bitplanes)
    return FALSE;

  memset (&priv->seq_hdr, 0, sizeof (GstVC1SeqHdr));
  memset (&priv->entrypoint_hdr, 0, sizeof (GstVC1EntryPointHdr));
  memset (&priv->frame_hdr, 0, sizeof (GstVC1FrameHdr));

  return TRUE;
}

static void
gst_vaapi_decoder_vc1_destroy (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderVC1 *const decoder = GST_VAAPI_DECODER_VC1_CAST (base_decoder);
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;

  gst_vaapi_decoder_vc1_close (decoder);

  if (priv->rbdu_buffer) {
    g_clear_pointer (&priv->rbdu_buffer, g_free);
    priv->rbdu_buffer_size = 0;
  }
}

static gboolean
gst_vaapi_decoder_vc1_create (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderVC1 *const decoder = GST_VAAPI_DECODER_VC1_CAST (base_decoder);
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;

  priv->has_codec_data = priv->has_entrypoint =
      priv->size_changed = priv->profile_changed =
      priv->closed_entry = priv->broken_link = FALSE;

  priv->profile = GST_VAAPI_PROFILE_UNKNOWN;
  priv->rndctrl = 0;
  priv->width = priv->height = 0;
  return TRUE;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vc1_reset (GstVaapiDecoder * base_decoder)
{
  gst_vaapi_decoder_vc1_destroy (base_decoder);
  gst_vaapi_decoder_vc1_create (base_decoder);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
ensure_context (GstVaapiDecoderVC1 * decoder)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVaapiProfile profiles[2];
  GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_VLD;
  guint i, n_profiles = 0;
  gboolean reset_context = FALSE;

  if (priv->profile_changed) {
    GST_DEBUG ("profile changed");
    priv->profile_changed = FALSE;
    reset_context = TRUE;

    profiles[n_profiles++] = priv->profile;
    if (priv->profile == GST_VAAPI_PROFILE_VC1_SIMPLE)
      profiles[n_profiles++] = GST_VAAPI_PROFILE_VC1_MAIN;

    for (i = 0; i < n_profiles; i++) {
      if (gst_vaapi_display_has_decoder (GST_VAAPI_DECODER_DISPLAY (decoder),
              profiles[i], entrypoint))
        break;
    }
    if (i == n_profiles)
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
    priv->profile = profiles[i];
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
      .profile = priv->profile,
      .entrypoint = entrypoint,
      .chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420,
      .width = priv->width,
      .height = priv->height,
      .ref_frames = 2,
    };
    /* *INDENT-ON* */

    reset_context =
        gst_vaapi_decoder_ensure_context (GST_VAAPI_DECODER (decoder), &info);
    if (!reset_context)
      return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_current_picture (GstVaapiDecoderVC1 * decoder)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVaapiPicture *const picture = priv->current_picture;

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
}

static GstVaapiDecoderStatus
decode_sequence (GstVaapiDecoderVC1 * decoder, GstVC1BDU * rbdu,
    GstVC1BDU * ebdu)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (decoder);
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVC1SeqHdr *const seq_hdr = &priv->seq_hdr;
  GstVC1AdvancedSeqHdr *const adv_hdr = &seq_hdr->advanced;
  GstVC1SeqStructC *const structc = &seq_hdr->struct_c;
  GstVC1ParserResult result;
  GstVaapiProfile profile;
  guint width, height, fps_n, fps_d, par_n, par_d;

  result = gst_vc1_parse_sequence_header (rbdu->data + rbdu->offset,
      rbdu->size, seq_hdr);
  if (result != GST_VC1_PARSER_OK) {
    GST_ERROR ("failed to parse sequence layer");
    return get_status (result);
  }

  priv->has_entrypoint = FALSE;

  /* Reset POC */
  if (priv->last_non_b_picture) {
    if (priv->last_non_b_picture->poc == priv->next_poc)
      priv->next_poc++;
    gst_vaapi_picture_replace (&priv->last_non_b_picture, NULL);
  }

  /* Validate profile */
  switch (seq_hdr->profile) {
    case GST_VC1_PROFILE_SIMPLE:
    case GST_VC1_PROFILE_MAIN:
    case GST_VC1_PROFILE_ADVANCED:
      break;
    default:
      GST_ERROR ("unsupported profile %d", seq_hdr->profile);
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  }

  fps_n = 0;
  fps_d = 0;
  par_n = 0;
  par_d = 0;
  switch (seq_hdr->profile) {
    case GST_VC1_PROFILE_SIMPLE:
    case GST_VC1_PROFILE_MAIN:
      if (structc->wmvp) {
        fps_n = structc->framerate;
        fps_d = 1;
      }
      break;
    case GST_VC1_PROFILE_ADVANCED:
      fps_n = adv_hdr->fps_n;
      fps_d = adv_hdr->fps_d;
      par_n = adv_hdr->par_n;
      par_d = adv_hdr->par_d;
      break;
    default:
      g_assert (0 && "XXX: we already validated the profile above");
      break;
  }

  if (fps_n && fps_d)
    gst_vaapi_decoder_set_framerate (base_decoder, fps_n, fps_d);

  if (par_n > 0 && par_d > 0)
    gst_vaapi_decoder_set_pixel_aspect_ratio (base_decoder, par_n, par_d);

  width = 0;
  height = 0;
  switch (seq_hdr->profile) {
    case GST_VC1_PROFILE_SIMPLE:
    case GST_VC1_PROFILE_MAIN:
      width = seq_hdr->struct_c.coded_width;
      height = seq_hdr->struct_c.coded_height;
      break;
    case GST_VC1_PROFILE_ADVANCED:
      width = seq_hdr->advanced.max_coded_width;
      height = seq_hdr->advanced.max_coded_height;
      break;
    default:
      g_assert (0 && "XXX: we already validated the profile above");
      break;
  }

  if (priv->width != width) {
    priv->width = width;
    priv->size_changed = TRUE;
  }

  if (priv->height != height) {
    priv->height = height;
    priv->size_changed = TRUE;
  }

  profile = GST_VAAPI_PROFILE_UNKNOWN;
  switch (seq_hdr->profile) {
    case GST_VC1_PROFILE_SIMPLE:
      profile = GST_VAAPI_PROFILE_VC1_SIMPLE;
      break;
    case GST_VC1_PROFILE_MAIN:
      profile = GST_VAAPI_PROFILE_VC1_MAIN;
      break;
    case GST_VC1_PROFILE_ADVANCED:
      profile = GST_VAAPI_PROFILE_VC1_ADVANCED;
      break;
    default:
      g_assert (0 && "XXX: we already validated the profile above");
      break;
  }
  if (priv->profile != profile) {
    priv->profile = profile;
    priv->profile_changed = TRUE;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sequence_end (GstVaapiDecoderVC1 * decoder)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVaapiDecoderStatus status;

  status = decode_current_picture (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  gst_vaapi_dpb_flush (priv->dpb);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_entry_point (GstVaapiDecoderVC1 * decoder, GstVC1BDU * rbdu,
    GstVC1BDU * ebdu)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVC1SeqHdr *const seq_hdr = &priv->seq_hdr;
  GstVC1EntryPointHdr *const entrypoint_hdr = &priv->entrypoint_hdr;
  GstVC1ParserResult result;

  result = gst_vc1_parse_entry_point_header (rbdu->data + rbdu->offset,
      rbdu->size, entrypoint_hdr, seq_hdr);
  if (result != GST_VC1_PARSER_OK) {
    GST_ERROR ("failed to parse entrypoint layer");
    return get_status (result);
  }

  if (entrypoint_hdr->coded_size_flag) {
    priv->width = entrypoint_hdr->coded_width;
    priv->height = entrypoint_hdr->coded_height;
    priv->size_changed = TRUE;
  }

  priv->has_entrypoint = TRUE;
  priv->closed_entry = entrypoint_hdr->closed_entry;
  priv->broken_link = entrypoint_hdr->broken_link;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

/* Reconstruct bitstream PTYPE (7.1.1.4, index into Table-35) */
static guint
get_PTYPE (guint ptype)
{
  switch (ptype) {
    case GST_VC1_PICTURE_TYPE_I:
      return 0;
    case GST_VC1_PICTURE_TYPE_P:
      return 1;
    case GST_VC1_PICTURE_TYPE_B:
      return 2;
    case GST_VC1_PICTURE_TYPE_BI:
      return 3;
  }
  return 4;                     /* skipped P-frame */
}

/* Reconstruct bitstream BFRACTION (7.1.1.14, index into Table-40) */
static guint
get_BFRACTION (guint bfraction)
{
  guint i;

  static const struct
  {
    guint16 index;
    guint16 value;
  }
  bfraction_map[] = {
    {
        0, GST_VC1_BFRACTION_BASIS / 2}, {
        1, GST_VC1_BFRACTION_BASIS / 3}, {
        2, (GST_VC1_BFRACTION_BASIS * 2) / 3}, {
        3, GST_VC1_BFRACTION_BASIS / 4}, {
        4, (GST_VC1_BFRACTION_BASIS * 3) / 4}, {
        5, GST_VC1_BFRACTION_BASIS / 5}, {
        6, (GST_VC1_BFRACTION_BASIS * 2) / 5}, {
        7, (GST_VC1_BFRACTION_BASIS * 3) / 5}, {
        8, (GST_VC1_BFRACTION_BASIS * 4) / 5}, {
        9, GST_VC1_BFRACTION_BASIS / 6}, {
        10, (GST_VC1_BFRACTION_BASIS * 5) / 6}, {
        11, GST_VC1_BFRACTION_BASIS / 7}, {
        12, (GST_VC1_BFRACTION_BASIS * 2) / 7}, {
        13, (GST_VC1_BFRACTION_BASIS * 3) / 7}, {
        14, (GST_VC1_BFRACTION_BASIS * 4) / 7}, {
        15, (GST_VC1_BFRACTION_BASIS * 5) / 7}, {
        16, (GST_VC1_BFRACTION_BASIS * 6) / 7}, {
        17, GST_VC1_BFRACTION_BASIS / 8}, {
        18, (GST_VC1_BFRACTION_BASIS * 3) / 8}, {
        19, (GST_VC1_BFRACTION_BASIS * 5) / 8}, {
        20, (GST_VC1_BFRACTION_BASIS * 7) / 8}, {
        21, GST_VC1_BFRACTION_RESERVED}, {
        22, GST_VC1_BFRACTION_PTYPE_BI}
  };

  if (!bfraction)
    return 0;

  for (i = 0; i < G_N_ELEMENTS (bfraction_map); i++) {
    if (bfraction_map[i].value == bfraction)
      return bfraction_map[i].index;
  }
  return 21;                    /* RESERVED */
}

/* Translate GStreamer MV modes to VA-API */
static guint
get_VAMvModeVC1 (guint mvmode)
{
  switch (mvmode) {
    case GST_VC1_MVMODE_1MV_HPEL_BILINEAR:
      return VAMvMode1MvHalfPelBilinear;
    case GST_VC1_MVMODE_1MV:
      return VAMvMode1Mv;
    case GST_VC1_MVMODE_1MV_HPEL:
      return VAMvMode1MvHalfPel;
    case GST_VC1_MVMODE_MIXED_MV:
      return VAMvModeMixedMv;
    case GST_VC1_MVMODE_INTENSITY_COMP:
      return VAMvModeIntensityCompensation;
  }
  return 0;
}

/* Reconstruct bitstream MVMODE (7.1.1.32) */
static guint
get_MVMODE (GstVC1FrameHdr * frame_hdr)
{
  guint mvmode;

  if (frame_hdr->profile == GST_VC1_PROFILE_ADVANCED)
    mvmode = frame_hdr->pic.advanced.mvmode;
  else
    mvmode = frame_hdr->pic.simple.mvmode;

  if (frame_hdr->ptype == GST_VC1_PICTURE_TYPE_P ||
      frame_hdr->ptype == GST_VC1_PICTURE_TYPE_B)
    return get_VAMvModeVC1 (mvmode);
  return 0;
}

/* Reconstruct bitstream MVMODE2 (7.1.1.33) */
static guint
get_MVMODE2 (GstVC1FrameHdr * frame_hdr)
{
  guint mvmode, mvmode2;

  if (frame_hdr->profile == GST_VC1_PROFILE_ADVANCED) {
    mvmode = frame_hdr->pic.advanced.mvmode;
    mvmode2 = frame_hdr->pic.advanced.mvmode2;
  } else {
    mvmode = frame_hdr->pic.simple.mvmode;
    mvmode2 = frame_hdr->pic.simple.mvmode2;
  }

  if (frame_hdr->ptype == GST_VC1_PICTURE_TYPE_P &&
      mvmode == GST_VC1_MVMODE_INTENSITY_COMP)
    return get_VAMvModeVC1 (mvmode2);
  return 0;
}

static inline int
has_MVTYPEMB_bitplane (GstVaapiDecoderVC1 * decoder)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVC1SeqHdr *const seq_hdr = &priv->seq_hdr;
  GstVC1FrameHdr *const frame_hdr = &priv->frame_hdr;
  guint mvmode, mvmode2;

  if (seq_hdr->profile == GST_VC1_PROFILE_ADVANCED) {
    GstVC1PicAdvanced *const pic = &frame_hdr->pic.advanced;
    if (pic->mvtypemb)
      return 0;
    mvmode = pic->mvmode;
    mvmode2 = pic->mvmode2;
  } else {
    GstVC1PicSimpleMain *const pic = &frame_hdr->pic.simple;
    if (pic->mvtypemb)
      return 0;
    mvmode = pic->mvmode;
    mvmode2 = pic->mvmode2;
  }
  return (frame_hdr->ptype == GST_VC1_PICTURE_TYPE_P &&
      (mvmode == GST_VC1_MVMODE_MIXED_MV ||
          (mvmode == GST_VC1_MVMODE_INTENSITY_COMP &&
              mvmode2 == GST_VC1_MVMODE_MIXED_MV)));
}

static inline int
has_SKIPMB_bitplane (GstVaapiDecoderVC1 * decoder)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVC1SeqHdr *const seq_hdr = &priv->seq_hdr;
  GstVC1FrameHdr *const frame_hdr = &priv->frame_hdr;

  if (seq_hdr->profile == GST_VC1_PROFILE_ADVANCED) {
    GstVC1PicAdvanced *const pic = &frame_hdr->pic.advanced;
    if (pic->skipmb)
      return 0;
  } else {
    GstVC1PicSimpleMain *const pic = &frame_hdr->pic.simple;
    if (pic->skipmb)
      return 0;
  }
  return (frame_hdr->ptype == GST_VC1_PICTURE_TYPE_P ||
      frame_hdr->ptype == GST_VC1_PICTURE_TYPE_B);
}

static inline int
has_DIRECTMB_bitplane (GstVaapiDecoderVC1 * decoder)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVC1SeqHdr *const seq_hdr = &priv->seq_hdr;
  GstVC1FrameHdr *const frame_hdr = &priv->frame_hdr;

  if (seq_hdr->profile == GST_VC1_PROFILE_ADVANCED) {
    GstVC1PicAdvanced *const pic = &frame_hdr->pic.advanced;
    if (pic->directmb)
      return 0;
  } else {
    GstVC1PicSimpleMain *const pic = &frame_hdr->pic.simple;
    if (pic->directmb)
      return 0;
  }
  return frame_hdr->ptype == GST_VC1_PICTURE_TYPE_B;
}

static inline int
has_ACPRED_bitplane (GstVaapiDecoderVC1 * decoder)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVC1SeqHdr *const seq_hdr = &priv->seq_hdr;
  GstVC1FrameHdr *const frame_hdr = &priv->frame_hdr;
  GstVC1PicAdvanced *const pic = &frame_hdr->pic.advanced;

  if (seq_hdr->profile != GST_VC1_PROFILE_ADVANCED)
    return 0;
  if (pic->acpred)
    return 0;
  return (frame_hdr->ptype == GST_VC1_PICTURE_TYPE_I ||
      frame_hdr->ptype == GST_VC1_PICTURE_TYPE_BI);
}

static inline int
has_OVERFLAGS_bitplane (GstVaapiDecoderVC1 * decoder)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVC1SeqHdr *const seq_hdr = &priv->seq_hdr;
  GstVC1EntryPointHdr *const entrypoint_hdr = &priv->entrypoint_hdr;
  GstVC1FrameHdr *const frame_hdr = &priv->frame_hdr;
  GstVC1PicAdvanced *const pic = &frame_hdr->pic.advanced;

  if (seq_hdr->profile != GST_VC1_PROFILE_ADVANCED)
    return 0;
  if (pic->overflags)
    return 0;
  return ((frame_hdr->ptype == GST_VC1_PICTURE_TYPE_I ||
          frame_hdr->ptype == GST_VC1_PICTURE_TYPE_BI) &&
      (entrypoint_hdr->overlap && frame_hdr->pquant <= 8) &&
      pic->condover == GST_VC1_CONDOVER_SELECT);
}

static inline void
pack_bitplanes (GstVaapiBitPlane * bitplane, guint n,
    const guint8 * bitplanes[3], guint x, guint y, guint stride)
{
  const guint dst_index = n / 2;
  const guint src_index = y * stride + x;
  guint8 v = 0;

  if (bitplanes[0])
    v |= bitplanes[0][src_index];
  if (bitplanes[1])
    v |= bitplanes[1][src_index] << 1;
  if (bitplanes[2])
    v |= bitplanes[2][src_index] << 2;
  bitplane->data[dst_index] = (bitplane->data[dst_index] << 4) | v;
}

static gboolean
fill_picture_structc (GstVaapiDecoderVC1 * decoder, GstVaapiPicture * picture)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  VAPictureParameterBufferVC1 *const pic_param = picture->param;
  GstVC1SeqStructC *const structc = &priv->seq_hdr.struct_c;
  GstVC1FrameHdr *const frame_hdr = &priv->frame_hdr;
  GstVC1PicSimpleMain *const pic = &frame_hdr->pic.simple;

  /* Fill in VAPictureParameterBufferVC1 (simple/main profile bits) */
  pic_param->sequence_fields.bits.finterpflag = structc->finterpflag;
  pic_param->sequence_fields.bits.multires = structc->multires;
  pic_param->sequence_fields.bits.overlap = structc->overlap;
  pic_param->sequence_fields.bits.syncmarker = structc->syncmarker;
  pic_param->sequence_fields.bits.rangered = structc->rangered;
  pic_param->sequence_fields.bits.max_b_frames = structc->maxbframes;
  pic_param->conditional_overlap_flag = 0;      /* advanced profile only */
  pic_param->fast_uvmc_flag = structc->fastuvmc;
  pic_param->b_picture_fraction = get_BFRACTION (pic->bfraction);
  pic_param->cbp_table = pic->cbptab;
  pic_param->mb_mode_table = 0; /* XXX: interlaced frame */
  pic_param->range_reduction_frame = pic->rangeredfrm;
  pic_param->post_processing = 0;       /* advanced profile only */
  pic_param->picture_resolution_index = pic->respic;
  pic_param->luma_scale = pic->lumscale;
  pic_param->luma_shift = pic->lumshift;
  pic_param->raw_coding.flags.mv_type_mb = pic->mvtypemb;
  pic_param->raw_coding.flags.direct_mb = pic->directmb;
  pic_param->raw_coding.flags.skip_mb = pic->skipmb;
  pic_param->bitplane_present.flags.bp_mv_type_mb =
      has_MVTYPEMB_bitplane (decoder);
  pic_param->bitplane_present.flags.bp_direct_mb =
      has_DIRECTMB_bitplane (decoder);
  pic_param->bitplane_present.flags.bp_skip_mb = has_SKIPMB_bitplane (decoder);
  pic_param->mv_fields.bits.mv_table = pic->mvtab;
  pic_param->mv_fields.bits.extended_mv_flag = structc->extended_mv;
  pic_param->mv_fields.bits.extended_mv_range = pic->mvrange;
  pic_param->transform_fields.bits.variable_sized_transform_flag =
      structc->vstransform;
  pic_param->transform_fields.bits.mb_level_transform_type_flag = pic->ttmbf;
  pic_param->transform_fields.bits.frame_level_transform_type = pic->ttfrm;
  pic_param->transform_fields.bits.transform_ac_codingset_idx2 =
      pic->transacfrm2;

  /* Refer to 8.3.7 Rounding control for Simple and Main Profile  */
  if (frame_hdr->ptype == GST_VC1_PICTURE_TYPE_I ||
      frame_hdr->ptype == GST_VC1_PICTURE_TYPE_BI)
    priv->rndctrl = 1;
  else if (frame_hdr->ptype == GST_VC1_PICTURE_TYPE_P)
    priv->rndctrl ^= 1;

  pic_param->rounding_control = priv->rndctrl;

  return TRUE;
}

static gboolean
fill_picture_advanced (GstVaapiDecoderVC1 * decoder, GstVaapiPicture * picture)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  VAPictureParameterBufferVC1 *const pic_param = picture->param;
  GstVC1AdvancedSeqHdr *const adv_hdr = &priv->seq_hdr.advanced;
  GstVC1EntryPointHdr *const entrypoint_hdr = &priv->entrypoint_hdr;
  GstVC1FrameHdr *const frame_hdr = &priv->frame_hdr;
  GstVC1PicAdvanced *const pic = &frame_hdr->pic.advanced;

  if (!priv->has_entrypoint)
    return FALSE;

  /* Fill in VAPictureParameterBufferVC1 (advanced profile bits) */
  pic_param->sequence_fields.bits.pulldown = adv_hdr->pulldown;
  pic_param->sequence_fields.bits.interlace = adv_hdr->interlace;
  pic_param->sequence_fields.bits.tfcntrflag = adv_hdr->tfcntrflag;
  pic_param->sequence_fields.bits.finterpflag = adv_hdr->finterpflag;
  pic_param->sequence_fields.bits.psf = adv_hdr->psf;
  pic_param->sequence_fields.bits.overlap = entrypoint_hdr->overlap;
  pic_param->entrypoint_fields.bits.broken_link = entrypoint_hdr->broken_link;
  pic_param->entrypoint_fields.bits.closed_entry = entrypoint_hdr->closed_entry;
  pic_param->entrypoint_fields.bits.panscan_flag = entrypoint_hdr->panscan_flag;
  pic_param->entrypoint_fields.bits.loopfilter = entrypoint_hdr->loopfilter;
  pic_param->conditional_overlap_flag = pic->condover;
  pic_param->fast_uvmc_flag = entrypoint_hdr->fastuvmc;
  pic_param->range_mapping_fields.bits.luma_flag =
      entrypoint_hdr->range_mapy_flag;
  pic_param->range_mapping_fields.bits.luma = entrypoint_hdr->range_mapy;
  pic_param->range_mapping_fields.bits.chroma_flag =
      entrypoint_hdr->range_mapuv_flag;
  pic_param->range_mapping_fields.bits.chroma = entrypoint_hdr->range_mapuv;
  pic_param->b_picture_fraction = get_BFRACTION (pic->bfraction);
  pic_param->cbp_table = pic->cbptab;
  pic_param->mb_mode_table = 0; /* XXX: interlaced frame */
  pic_param->range_reduction_frame = 0; /* simple/main profile only */
  pic_param->rounding_control = pic->rndctrl;
  pic_param->post_processing = pic->postproc;
  pic_param->picture_resolution_index = 0;      /* simple/main profile only */
  pic_param->luma_scale = pic->lumscale;
  pic_param->luma_shift = pic->lumshift;
  pic_param->picture_fields.bits.frame_coding_mode = pic->fcm;
  pic_param->picture_fields.bits.top_field_first = pic->tff;
  pic_param->picture_fields.bits.is_first_field = pic->fcm == 0;        /* XXX: interlaced frame */
  pic_param->picture_fields.bits.intensity_compensation =
      pic->mvmode == GST_VC1_MVMODE_INTENSITY_COMP;
  pic_param->raw_coding.flags.mv_type_mb = pic->mvtypemb;
  pic_param->raw_coding.flags.direct_mb = pic->directmb;
  pic_param->raw_coding.flags.skip_mb = pic->skipmb;
  pic_param->raw_coding.flags.ac_pred = pic->acpred;
  pic_param->raw_coding.flags.overflags = pic->overflags;
  pic_param->bitplane_present.flags.bp_mv_type_mb =
      has_MVTYPEMB_bitplane (decoder);
  pic_param->bitplane_present.flags.bp_direct_mb =
      has_DIRECTMB_bitplane (decoder);
  pic_param->bitplane_present.flags.bp_skip_mb = has_SKIPMB_bitplane (decoder);
  pic_param->bitplane_present.flags.bp_ac_pred = has_ACPRED_bitplane (decoder);
  pic_param->bitplane_present.flags.bp_overflags =
      has_OVERFLAGS_bitplane (decoder);
  pic_param->reference_fields.bits.reference_distance_flag =
      entrypoint_hdr->refdist_flag;
  pic_param->mv_fields.bits.mv_table = pic->mvtab;
  pic_param->mv_fields.bits.extended_mv_flag = entrypoint_hdr->extended_mv;
  pic_param->mv_fields.bits.extended_mv_range = pic->mvrange;
  pic_param->mv_fields.bits.extended_dmv_flag = entrypoint_hdr->extended_dmv;
  pic_param->pic_quantizer_fields.bits.dquant = entrypoint_hdr->dquant;
  pic_param->pic_quantizer_fields.bits.quantizer = entrypoint_hdr->quantizer;
  pic_param->transform_fields.bits.variable_sized_transform_flag =
      entrypoint_hdr->vstransform;
  pic_param->transform_fields.bits.mb_level_transform_type_flag = pic->ttmbf;
  pic_param->transform_fields.bits.frame_level_transform_type = pic->ttfrm;
  pic_param->transform_fields.bits.transform_ac_codingset_idx2 =
      pic->transacfrm2;
  return TRUE;
}

static gboolean
fill_picture (GstVaapiDecoderVC1 * decoder, GstVaapiPicture * picture)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  VAPictureParameterBufferVC1 *const pic_param = picture->param;
  GstVC1SeqHdr *const seq_hdr = &priv->seq_hdr;
  GstVC1FrameHdr *const frame_hdr = &priv->frame_hdr;
  GstVC1VopDquant *const vopdquant = &frame_hdr->vopdquant;
  GstVaapiPicture *prev_picture, *next_picture;

  /* Fill in VAPictureParameterBufferVC1 (common fields) */
  pic_param->forward_reference_picture = VA_INVALID_ID;
  pic_param->backward_reference_picture = VA_INVALID_ID;
  pic_param->inloop_decoded_picture = VA_INVALID_ID;
  pic_param->sequence_fields.value = 0;
  pic_param->sequence_fields.bits.profile = seq_hdr->profile;
  pic_param->coded_width = priv->width;
  pic_param->coded_height = priv->height;
  pic_param->entrypoint_fields.value = 0;
  pic_param->range_mapping_fields.value = 0;
  pic_param->picture_fields.value = 0;
  pic_param->picture_fields.bits.picture_type = get_PTYPE (frame_hdr->ptype);
  pic_param->raw_coding.value = 0;
  pic_param->bitplane_present.value = 0;
  pic_param->reference_fields.value = 0;
  pic_param->mv_fields.value = 0;
  pic_param->mv_fields.bits.mv_mode = get_MVMODE (frame_hdr);
  pic_param->mv_fields.bits.mv_mode2 = get_MVMODE2 (frame_hdr);
  pic_param->pic_quantizer_fields.value = 0;
  pic_param->pic_quantizer_fields.bits.half_qp = frame_hdr->halfqp;
  pic_param->pic_quantizer_fields.bits.pic_quantizer_scale = frame_hdr->pquant;
  pic_param->pic_quantizer_fields.bits.pic_quantizer_type =
      frame_hdr->pquantizer;
  pic_param->pic_quantizer_fields.bits.dq_frame = vopdquant->dquantfrm;
  pic_param->pic_quantizer_fields.bits.dq_profile = vopdquant->dqprofile;
  pic_param->pic_quantizer_fields.bits.dq_sb_edge =
      vopdquant->dqprofile ==
      GST_VC1_DQPROFILE_SINGLE_EDGE ? vopdquant->dqbedge : 0;
  pic_param->pic_quantizer_fields.bits.dq_db_edge =
      vopdquant->dqprofile ==
      GST_VC1_DQPROFILE_DOUBLE_EDGES ? vopdquant->dqbedge : 0;
  pic_param->pic_quantizer_fields.bits.dq_binary_level = vopdquant->dqbilevel;
  pic_param->pic_quantizer_fields.bits.alt_pic_quantizer = vopdquant->altpquant;
  pic_param->transform_fields.value = 0;
  pic_param->transform_fields.bits.transform_ac_codingset_idx1 =
      frame_hdr->transacfrm;
  pic_param->transform_fields.bits.intra_transform_dc_table =
      frame_hdr->transdctab;

  if (seq_hdr->profile == GST_VC1_PROFILE_ADVANCED) {
    if (!fill_picture_advanced (decoder, picture))
      return FALSE;
  } else {
    if (!fill_picture_structc (decoder, picture))
      return FALSE;
  }

  gst_vaapi_dpb_get_neighbours (priv->dpb, picture,
      &prev_picture, &next_picture);

  switch (picture->type) {
    case GST_VAAPI_PICTURE_TYPE_B:
      if (next_picture)
        pic_param->backward_reference_picture = next_picture->surface_id;
      if (prev_picture)
        pic_param->forward_reference_picture = prev_picture->surface_id;
      else if (!priv->closed_entry)
        GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_SKIPPED);
      break;
    case GST_VAAPI_PICTURE_TYPE_P:
      if (prev_picture)
        pic_param->forward_reference_picture = prev_picture->surface_id;
      break;
    default:
      break;
  }

  if (pic_param->bitplane_present.value) {
    const guint8 *bitplanes[3];
    guint x, y, n;

    switch (picture->type) {
      case GST_VAAPI_PICTURE_TYPE_P:
        bitplanes[0] = pic_param->bitplane_present.flags.bp_direct_mb ?
            priv->bitplanes->directmb : NULL;
        bitplanes[1] = pic_param->bitplane_present.flags.bp_skip_mb ?
            priv->bitplanes->skipmb : NULL;
        bitplanes[2] = pic_param->bitplane_present.flags.bp_mv_type_mb ?
            priv->bitplanes->mvtypemb : NULL;
        break;
      case GST_VAAPI_PICTURE_TYPE_B:
        bitplanes[0] = pic_param->bitplane_present.flags.bp_direct_mb ?
            priv->bitplanes->directmb : NULL;
        bitplanes[1] = pic_param->bitplane_present.flags.bp_skip_mb ?
            priv->bitplanes->skipmb : NULL;
        bitplanes[2] = NULL;    /* XXX: interlaced frame (FORWARD plane) */
        break;
      case GST_VAAPI_PICTURE_TYPE_BI:
      case GST_VAAPI_PICTURE_TYPE_I:
        bitplanes[0] = NULL;    /* XXX: interlaced frame (FIELDTX plane) */
        bitplanes[1] = pic_param->bitplane_present.flags.bp_ac_pred ?
            priv->bitplanes->acpred : NULL;
        bitplanes[2] = pic_param->bitplane_present.flags.bp_overflags ?
            priv->bitplanes->overflags : NULL;
        break;
      default:
        bitplanes[0] = NULL;
        bitplanes[1] = NULL;
        bitplanes[2] = NULL;
        break;
    }

    picture->bitplane = GST_VAAPI_BITPLANE_NEW (decoder,
        (seq_hdr->mb_width * seq_hdr->mb_height + 1) / 2);
    if (!picture->bitplane)
      return FALSE;

    n = 0;
    for (y = 0; y < seq_hdr->mb_height; y++)
      for (x = 0; x < seq_hdr->mb_width; x++, n++)
        pack_bitplanes (picture->bitplane, n, bitplanes, x, y,
            seq_hdr->mb_stride);
    if (n & 1)                  /* move last nibble to the high order */
      picture->bitplane->data[n / 2] <<= 4;
  }
  return TRUE;
}

static GstVaapiDecoderStatus
decode_slice_chunk (GstVaapiDecoderVC1 * decoder, GstVC1BDU * ebdu,
    guint slice_addr, guint header_size)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVaapiPicture *const picture = priv->current_picture;
  GstVaapiSlice *slice;
  VASliceParameterBufferVC1 *slice_param;

  slice = GST_VAAPI_SLICE_NEW (VC1, decoder,
      ebdu->data + ebdu->sc_offset,
      ebdu->size + ebdu->offset - ebdu->sc_offset);
  if (!slice) {
    GST_ERROR ("failed to allocate slice");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  gst_vaapi_picture_add_slice (picture, slice);

  /* Fill in VASliceParameterBufferVC1 */
  slice_param = slice->param;
  slice_param->macroblock_offset = 8 * (ebdu->offset - ebdu->sc_offset) +
      header_size;
  slice_param->slice_vertical_position = slice_addr;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_frame (GstVaapiDecoderVC1 * decoder, GstVC1BDU * rbdu, GstVC1BDU * ebdu)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVC1FrameHdr *const frame_hdr = &priv->frame_hdr;
  GstVC1ParserResult result;
  GstVaapiPicture *const picture = priv->current_picture;

  memset (frame_hdr, 0, sizeof (*frame_hdr));
  result = gst_vc1_parse_frame_header (rbdu->data + rbdu->offset,
      rbdu->size, frame_hdr, &priv->seq_hdr, priv->bitplanes);
  if (result != GST_VC1_PARSER_OK) {
    GST_ERROR ("failed to parse frame layer");
    return get_status (result);
  }

  /* @FIXME: intel-driver cannot handle interlaced frames */
  if (priv->profile == GST_VAAPI_PROFILE_VC1_ADVANCED
      && frame_hdr->pic.advanced.fcm != GST_VC1_FRAME_PROGRESSIVE) {
    GST_ERROR ("interlaced video not supported");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  }

  switch (frame_hdr->ptype) {
    case GST_VC1_PICTURE_TYPE_I:
      picture->type = GST_VAAPI_PICTURE_TYPE_I;
      GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_REFERENCE);
      break;
    case GST_VC1_PICTURE_TYPE_SKIPPED:
    case GST_VC1_PICTURE_TYPE_P:
      picture->type = GST_VAAPI_PICTURE_TYPE_P;
      GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_REFERENCE);
      break;
    case GST_VC1_PICTURE_TYPE_B:
      picture->type = GST_VAAPI_PICTURE_TYPE_B;
      break;
    case GST_VC1_PICTURE_TYPE_BI:
      picture->type = GST_VAAPI_PICTURE_TYPE_BI;
      break;
    default:
      GST_ERROR ("unsupported picture type %d", frame_hdr->ptype);
      return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  /* Update presentation time */
  if (GST_VAAPI_PICTURE_IS_REFERENCE (picture)) {
    picture->poc = priv->last_non_b_picture ?
        (priv->last_non_b_picture->poc + 1) : priv->next_poc;
    priv->next_poc = picture->poc + 1;
    gst_vaapi_picture_replace (&priv->last_non_b_picture, picture);
  } else if (!priv->last_non_b_picture)
    picture->poc = priv->next_poc++;
  else {                        /* B or BI */
    picture->poc = priv->last_non_b_picture->poc++;
    priv->next_poc = priv->last_non_b_picture->poc + 1;
  }
  picture->pts = GST_VAAPI_DECODER_CODEC_FRAME (decoder)->pts;

  if (!fill_picture (decoder, picture))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  return decode_slice_chunk (decoder, ebdu, 0, frame_hdr->header_size);
}

static GstVaapiDecoderStatus
decode_slice (GstVaapiDecoderVC1 * decoder, GstVC1BDU * rbdu, GstVC1BDU * ebdu)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVC1SliceHdr slice_hdr;
  GstVC1ParserResult result;

  memset (&slice_hdr, 0, sizeof (slice_hdr));
  result = gst_vc1_parse_slice_header (rbdu->data + rbdu->offset,
      rbdu->size, &slice_hdr, &priv->seq_hdr);
  if (result != GST_VC1_PARSER_OK) {
    GST_ERROR ("failed to parse slice layer");
    return get_status (result);
  }
  return decode_slice_chunk (decoder, ebdu, slice_hdr.slice_addr,
      slice_hdr.header_size);
}

static gboolean
decode_rbdu (GstVaapiDecoderVC1 * decoder, GstVC1BDU * rbdu, GstVC1BDU * ebdu)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  guint8 *rbdu_buffer;
  guint i, j, rbdu_buffer_size;

  /* BDU are encapsulated in advanced profile mode only */
  if (priv->profile != GST_VAAPI_PROFILE_VC1_ADVANCED) {
    memcpy (rbdu, ebdu, sizeof (*rbdu));
    return TRUE;
  }

  /* Reallocate unescaped bitstream buffer */
  rbdu_buffer = priv->rbdu_buffer;
  if (!rbdu_buffer || ebdu->size > priv->rbdu_buffer_size) {
    rbdu_buffer = g_realloc (priv->rbdu_buffer, ebdu->size);
    if (!rbdu_buffer)
      return FALSE;
    priv->rbdu_buffer = rbdu_buffer;
    priv->rbdu_buffer_size = ebdu->size;
  }

  /* Unescape bitstream buffer */
  if (ebdu->size < 4) {
    memcpy (rbdu_buffer, ebdu->data + ebdu->offset, ebdu->size);
    rbdu_buffer_size = ebdu->size;
  } else {
    guint8 *const bdu_buffer = ebdu->data + ebdu->offset;
    for (i = 0, j = 0; i < ebdu->size; i++) {
      if (i >= 2 && i < ebdu->size - 1 &&
          bdu_buffer[i - 1] == 0x00 &&
          bdu_buffer[i - 2] == 0x00 &&
          bdu_buffer[i] == 0x03 && bdu_buffer[i + 1] <= 0x03)
        i++;
      rbdu_buffer[j++] = bdu_buffer[i];
    }
    rbdu_buffer_size = j;
  }

  /* Reconstruct RBDU */
  rbdu->type = ebdu->type;
  rbdu->size = rbdu_buffer_size;
  rbdu->sc_offset = 0;
  rbdu->offset = 0;
  rbdu->data = rbdu_buffer;
  return TRUE;
}

static GstVaapiDecoderStatus
decode_ebdu (GstVaapiDecoderVC1 * decoder, GstVC1BDU * ebdu)
{
  GstVaapiDecoderStatus status;
  GstVC1BDU rbdu;

  if (!decode_rbdu (decoder, &rbdu, ebdu))
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;

  switch (ebdu->type) {
    case GST_VC1_SEQUENCE:
      status = decode_sequence (decoder, &rbdu, ebdu);
      break;
    case GST_VC1_ENTRYPOINT:
      status = decode_entry_point (decoder, &rbdu, ebdu);
      break;
    case GST_VC1_FRAME:
      status = decode_frame (decoder, &rbdu, ebdu);
      break;
    case GST_VC1_SLICE:
      status = decode_slice (decoder, &rbdu, ebdu);
      break;
    case GST_VC1_END_OF_SEQ:
      status = decode_sequence_end (decoder);
      break;
    case GST_VC1_FIELD_USER:
    case GST_VC1_FRAME_USER:
    case GST_VC1_ENTRY_POINT_USER:
    case GST_VC1_SEQUENCE_USER:
      /* Let's just ignore them */
      status = GST_VAAPI_DECODER_STATUS_SUCCESS;
      break;
    default:
      GST_WARNING ("unsupported BDU type %d", ebdu->type);
      status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
      break;
  }
  return status;
}

static GstVaapiDecoderStatus
decode_buffer (GstVaapiDecoderVC1 * decoder, guchar * buf, guint buf_size)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVC1BDU ebdu;

  if (priv->has_codec_data) {
    ebdu.type = GST_VC1_FRAME;
    ebdu.sc_offset = 0;
    ebdu.offset = 0;
  } else {
    ebdu.type = buf[3];
    ebdu.sc_offset = 0;
    ebdu.offset = 4;
  }
  ebdu.data = buf;
  ebdu.size = buf_size - ebdu.offset;
  return decode_ebdu (decoder, &ebdu);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vc1_decode_codec_data (GstVaapiDecoder * base_decoder,
    const guchar * buf, guint buf_size)
{
  GstVaapiDecoderVC1 *const decoder = GST_VAAPI_DECODER_VC1_CAST (base_decoder);
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVC1SeqHdr *const seq_hdr = &priv->seq_hdr;
  GstVaapiDecoderStatus status;
  GstVC1ParserResult result;
  GstVC1BDU ebdu;
  GstCaps *caps;
  GstStructure *structure;
  guint ofs;
  gint width, height;
  guint32 format;
  gint version;
  const gchar *s;

  priv->has_codec_data = TRUE;

  width = GST_VAAPI_DECODER_WIDTH (decoder);
  height = GST_VAAPI_DECODER_HEIGHT (decoder);
  if (!width || !height) {
    GST_ERROR ("failed to parse size from codec-data");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  caps = GST_VAAPI_DECODER_CODEC_STATE (decoder)->caps;
  structure = gst_caps_get_structure (caps, 0);
  s = gst_structure_get_string (structure, "format");
  if (s && strlen (s) == 4) {
    format = GST_MAKE_FOURCC (s[0], s[1], s[2], s[3]);
  } else {
    /* Try to determine format from "wmvversion" property */
    if (gst_structure_get_int (structure, "wmvversion", &version))
      format = (version >= 1 && version <= 3) ?
          GST_MAKE_FOURCC ('W', 'M', 'V', ('0' + version)) : 0;
    else
      format = 0;
  }
  if (!format) {
    GST_ERROR ("failed to parse profile from codec-data");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC;
  }

  /* WMV3 -- expecting sequence header */
  if (format == GST_MAKE_FOURCC ('W', 'M', 'V', '3')) {
    seq_hdr->struct_c.coded_width = width;
    seq_hdr->struct_c.coded_height = height;
    ebdu.type = GST_VC1_SEQUENCE;
    ebdu.size = buf_size;
    ebdu.sc_offset = 0;
    ebdu.offset = 0;
    ebdu.data = (guint8 *) buf;
    return decode_ebdu (decoder, &ebdu);
  }

  /* WVC1 -- expecting bitstream data units */
  if (format != GST_MAKE_FOURCC ('W', 'V', 'C', '1'))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  seq_hdr->advanced.max_coded_width = width;
  seq_hdr->advanced.max_coded_height = height;

  ofs = 0;
  do {
    result = gst_vc1_identify_next_bdu (buf + ofs, buf_size - ofs, &ebdu);

    switch (result) {
      case GST_VC1_PARSER_NO_BDU_END:
        /* Assume the EBDU is complete within codec-data bounds */
        ebdu.size = buf_size - ofs - ebdu.offset;
        // fall-through
      case GST_VC1_PARSER_OK:
        status = decode_ebdu (decoder, &ebdu);
        ofs += ebdu.offset + ebdu.size;
        break;
      default:
        status = get_status (result);
        break;
    }
  } while (status == GST_VAAPI_DECODER_STATUS_SUCCESS && ofs < buf_size);
  return status;
}

static GstVaapiDecoderStatus
ensure_decoder (GstVaapiDecoderVC1 * decoder)
{
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVaapiDecoderStatus status;

  if (!priv->is_opened) {
    priv->is_opened = gst_vaapi_decoder_vc1_open (decoder);
    if (!priv->is_opened)
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC;

    status =
        gst_vaapi_decoder_decode_codec_data (GST_VAAPI_DECODER_CAST (decoder));
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
      return status;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline gint
scan_for_start_code (GstAdapter * adapter, guint ofs, guint size, guint32 * scp)
{
  return (gint) gst_adapter_masked_scan_uint32_peek (adapter,
      0xffffff00, 0x00000100, ofs, size, scp);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vc1_parse (GstVaapiDecoder * base_decoder,
    GstAdapter * adapter, gboolean at_eos, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderVC1 *const decoder = GST_VAAPI_DECODER_VC1_CAST (base_decoder);
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVaapiDecoderStatus status;
  guint8 bdu_type;
  guint size, buf_size, flags = 0;
  gint ofs;

  status = ensure_decoder (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  size = gst_adapter_available (adapter);

  if (priv->has_codec_data) {
    // Assume demuxer sends out plain frames
    if (size < 1)
      return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    buf_size = size;
    bdu_type = GST_VC1_FRAME;
  } else {
    if (size < 4)
      return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

    ofs = scan_for_start_code (adapter, 0, size, NULL);
    if (ofs < 0)
      return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    gst_adapter_flush (adapter, ofs);
    size -= ofs;

    ofs = G_UNLIKELY (size < 8) ? -1 :
        scan_for_start_code (adapter, 4, size - 4, NULL);
    if (ofs < 0) {
      // Assume the whole packet is present if end-of-stream
      if (!at_eos)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
      ofs = size;
    }
    buf_size = ofs;
    gst_adapter_copy (adapter, &bdu_type, 3, 1);
  }

  unit->size = buf_size;

  /* Check for new picture layer */
  switch (bdu_type) {
    case GST_VC1_END_OF_SEQ:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END;
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_STREAM_END;
      break;
    case GST_VC1_SEQUENCE:
    case GST_VC1_ENTRYPOINT:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      break;
    case GST_VC1_FRAME:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
      break;
    case GST_VC1_SLICE:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
      break;
    case GST_VC1_FIELD:
      /* @FIXME: intel-driver cannot handle interlaced frames */
      GST_ERROR ("interlaced video not supported");
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  }
  GST_VAAPI_DECODER_UNIT_FLAG_SET (unit, flags);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vc1_decode (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderVC1 *const decoder = GST_VAAPI_DECODER_VC1_CAST (base_decoder);
  GstVaapiDecoderStatus status;
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

  status = decode_buffer (decoder, map_info.data + unit->offset, unit->size);
  gst_buffer_unmap (buffer, &map_info);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vc1_start_frame (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderVC1 *const decoder = GST_VAAPI_DECODER_VC1_CAST (base_decoder);
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;
  GstVaapiDecoderStatus status;
  GstVaapiPicture *picture;

  status = ensure_context (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
    GST_ERROR ("failed to reset context");
    return status;
  }
  status = ensure_decoder (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  picture = GST_VAAPI_PICTURE_NEW (VC1, decoder);
  if (!picture) {
    GST_ERROR ("failed to allocate picture");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  gst_vaapi_picture_replace (&priv->current_picture, picture);
  gst_vaapi_picture_unref (picture);

  /* Update cropping rectangle */
  do {
    GstVC1AdvancedSeqHdr *adv_hdr;
    GstVaapiRectangle crop_rect;

    if (priv->profile != GST_VAAPI_PROFILE_VC1_ADVANCED)
      break;

    adv_hdr = &priv->seq_hdr.advanced;
    if (!adv_hdr->display_ext)
      break;

    crop_rect.x = 0;
    crop_rect.y = 0;
    crop_rect.width = adv_hdr->disp_horiz_size;
    crop_rect.height = adv_hdr->disp_vert_size;
    if (crop_rect.width <= priv->width && crop_rect.height <= priv->height)
      gst_vaapi_picture_set_crop_rect (picture, &crop_rect);
  } while (0);

  if (!gst_vc1_bitplanes_ensure_size (priv->bitplanes, &priv->seq_hdr)) {
    GST_ERROR ("failed to allocate bitplanes");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vc1_end_frame (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderVC1 *const decoder = GST_VAAPI_DECODER_VC1_CAST (base_decoder);

  return decode_current_picture (decoder);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vc1_flush (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderVC1 *const decoder = GST_VAAPI_DECODER_VC1_CAST (base_decoder);
  GstVaapiDecoderVC1Private *const priv = &decoder->priv;

  if (priv->is_opened)
    gst_vaapi_dpb_flush (priv->dpb);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
gst_vaapi_decoder_vc1_finalize (GObject * object)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (object);

  gst_vaapi_decoder_vc1_destroy (base_decoder);
  G_OBJECT_CLASS (gst_vaapi_decoder_vc1_parent_class)->finalize (object);
}

static void
gst_vaapi_decoder_vc1_class_init (GstVaapiDecoderVC1Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiDecoderClass *const decoder_class = GST_VAAPI_DECODER_CLASS (klass);

  object_class->finalize = gst_vaapi_decoder_vc1_finalize;

  decoder_class->reset = gst_vaapi_decoder_vc1_reset;
  decoder_class->parse = gst_vaapi_decoder_vc1_parse;
  decoder_class->decode = gst_vaapi_decoder_vc1_decode;
  decoder_class->start_frame = gst_vaapi_decoder_vc1_start_frame;
  decoder_class->end_frame = gst_vaapi_decoder_vc1_end_frame;
  decoder_class->flush = gst_vaapi_decoder_vc1_flush;

  decoder_class->decode_codec_data = gst_vaapi_decoder_vc1_decode_codec_data;
}

static void
gst_vaapi_decoder_vc1_init (GstVaapiDecoderVC1 * decoder)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (decoder);

  gst_vaapi_decoder_vc1_create (base_decoder);
}

/**
 * gst_vaapi_decoder_vc1_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps holding codec information
 *
 * Creates a new #GstVaapiDecoder for VC-1 decoding.  The @caps can
 * hold extra information like codec-data and pictured coded size.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_vc1_new (GstVaapiDisplay * display, GstCaps * caps)
{
  return g_object_new (GST_TYPE_VAAPI_DECODER_VC1, "display", display,
      "caps", caps, NULL);
}
