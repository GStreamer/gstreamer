/*
 *  gstvaapidecoder_vp9.c - VP9 decoder
 *
 *  Copyright (C) 2014-2015 Intel Corporation
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
 * SECTION:gstvaapidecoder_vp9
 * @short_description: VP9 decoder
 */

#include "sysdeps.h"
#include <gst/codecparsers/gstvp9parser.h>
#include "gstvaapidecoder_vp9.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"

#include "gstvaapicompat.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_DECODER_VP9_CAST(decoder) \
  ((GstVaapiDecoderVp9 *)(decoder))

typedef struct _GstVaapiDecoderVp9Private GstVaapiDecoderVp9Private;
typedef struct _GstVaapiDecoderVp9Class GstVaapiDecoderVp9Class;

struct _GstVaapiDecoderVp9Private
{
  GstVaapiProfile profile;
  guint width;
  guint height;
  GstVp9Parser *parser;
  GstVp9FrameHdr frame_hdr;
  GstVaapiPicture *current_picture;
  GstVaapiPicture *ref_frames[GST_VP9_REF_FRAMES];      /* reference frames in ref_slots[max_ref] */

  guint num_frames;             /* number of frames in a super frame */
  guint frame_sizes[8];         /* size of frames in a super frame */
  guint frame_cnt;              /* frame count variable for super frame */
  guint total_idx_size;         /* super frame index size (full block size) */
  guint had_superframe_hdr:1;   /* indicate the presense of super frame */

  guint size_changed:1;
};

/**
 * GstVaapiDecoderVp9:
 *
 * A decoder based on Vp9.
 */
struct _GstVaapiDecoderVp9
{
  /*< private > */
  GstVaapiDecoder parent_instance;

  GstVaapiDecoderVp9Private priv;
};

/**
 * GstVaapiDecoderVp9Class:
 *
 * A decoder class based on Vp9.
 */
struct _GstVaapiDecoderVp9Class
{
  /*< private > */
  GstVaapiDecoderClass parent_class;
};

G_DEFINE_TYPE (GstVaapiDecoderVp9, gst_vaapi_decoder_vp9,
    GST_TYPE_VAAPI_DECODER);

static GstVaapiDecoderStatus
get_status (GstVp9ParserResult result)
{
  GstVaapiDecoderStatus status;

  switch (result) {
    case GST_VP9_PARSER_OK:
      status = GST_VAAPI_DECODER_STATUS_SUCCESS;
      break;
    case GST_VP9_PARSER_ERROR:
      status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
      break;
    default:
      status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
      break;
  }
  return status;
}

static void
gst_vaapi_decoder_vp9_close (GstVaapiDecoderVp9 * decoder)
{
  GstVaapiDecoderVp9Private *const priv = &decoder->priv;
  guint i;

  for (i = 0; i < GST_VP9_REF_FRAMES; i++)
    gst_vaapi_picture_replace (&priv->ref_frames[i], NULL);

  g_clear_pointer (&priv->parser, gst_vp9_parser_free);
}

static gboolean
gst_vaapi_decoder_vp9_open (GstVaapiDecoderVp9 * decoder)
{
  GstVaapiDecoderVp9Private *const priv = &decoder->priv;

  gst_vaapi_decoder_vp9_close (decoder);
  priv->parser = gst_vp9_parser_new ();
  return TRUE;
}

static void
gst_vaapi_decoder_vp9_destroy (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderVp9 *const decoder = GST_VAAPI_DECODER_VP9_CAST (base_decoder);

  gst_vaapi_decoder_vp9_close (decoder);
}

static gboolean
gst_vaapi_decoder_vp9_create (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderVp9 *const decoder = GST_VAAPI_DECODER_VP9_CAST (base_decoder);
  GstVaapiDecoderVp9Private *const priv = &decoder->priv;

  if (!gst_vaapi_decoder_vp9_open (decoder))
    return FALSE;

  priv->profile = GST_VAAPI_PROFILE_UNKNOWN;
  return TRUE;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vp9_reset (GstVaapiDecoder * base_decoder)
{
  gst_vaapi_decoder_vp9_destroy (base_decoder);
  if (gst_vaapi_decoder_vp9_create (base_decoder))
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
  return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
}

/* Returns GstVaapiProfile from VP9 frame_hdr profile value */
static GstVaapiProfile
get_profile (guint profile_idc)
{
  GstVaapiProfile profile;

  switch (profile_idc) {
    case GST_VP9_PROFILE_0:
      profile = GST_VAAPI_PROFILE_VP9_0;
      break;
    case GST_VP9_PROFILE_1:
      profile = GST_VAAPI_PROFILE_VP9_1;
      break;
    case GST_VP9_PROFILE_2:
      profile = GST_VAAPI_PROFILE_VP9_2;
      break;
    case GST_VP9_PROFILE_3:
      profile = GST_VAAPI_PROFILE_VP9_3;
      break;
    default:
      GST_DEBUG ("unsupported profile_idc value");
      profile = GST_VAAPI_PROFILE_UNKNOWN;
      break;
  }
  return profile;
}

static gboolean
get_chroma_type (GstVp9FrameHdr * frame_hdr, GstVp9Parser * parser,
    GstVaapiContextInfo * info)
{
  switch (frame_hdr->profile) {
    case GST_VP9_PROFILE_0:
      info->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
      break;
    case GST_VP9_PROFILE_1:
      if (parser->subsampling_x == 1 && parser->subsampling_y == 0)
        info->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV422;
      else if (parser->subsampling_x == 0 && parser->subsampling_y == 0)
        info->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV444;
      else
        return FALSE;
      break;
    case GST_VP9_PROFILE_2:
      if (parser->bit_depth == 10)
        info->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420_10BPP;
      else
        info->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420_12BPP;
      break;
    case GST_VP9_PROFILE_3:
      if (parser->subsampling_x == 1 && parser->subsampling_y == 0) {
        if (parser->bit_depth == 10)
          info->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV422_10BPP;
        else
          info->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV422_12BPP;
      } else if (parser->subsampling_x == 0 && parser->subsampling_y == 0) {
        if (parser->bit_depth == 10)
          info->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV444_10BPP;
        else
          info->chroma_type = GST_VAAPI_CHROMA_TYPE_YUV444_12BPP;
      } else
        return FALSE;
      break;
    default:
      return FALSE;
      break;
  }
  return TRUE;
}

static GstVaapiDecoderStatus
ensure_context (GstVaapiDecoderVp9 * decoder)
{
  GstVaapiDecoderVp9Private *const priv = &decoder->priv;
  GstVp9FrameHdr *frame_hdr = &priv->frame_hdr;
  GstVp9Parser *parser = priv->parser;
  GstVaapiProfile profile;
  const GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_VLD;
  gboolean reset_context = FALSE;

  profile = get_profile (frame_hdr->profile);

  if (priv->profile != profile) {
    if (!gst_vaapi_display_has_decoder (GST_VAAPI_DECODER_DISPLAY (decoder),
            profile, entrypoint))
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

    priv->profile = profile;
    reset_context = TRUE;
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
      .width = priv->width,
      .height = priv->height,
      .ref_frames = 8,
    };
    /* *INDENT-ON* */

    if (!get_chroma_type (frame_hdr, parser, &info))
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT;

    reset_context =
        gst_vaapi_decoder_ensure_context (GST_VAAPI_DECODER (decoder), &info);

    if (!reset_context)
      return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

    gst_vaapi_context_reset_on_resize (GST_VAAPI_DECODER_CONTEXT (decoder),
        FALSE);
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
init_picture (GstVaapiDecoderVp9 * decoder, GstVaapiPicture * picture)
{
  GstVaapiDecoderVp9Private *const priv = &decoder->priv;
  GstVp9FrameHdr *const frame_hdr = &priv->frame_hdr;

  picture->structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
  picture->type =
      (frame_hdr->frame_type ==
      GST_VP9_KEY_FRAME) ? GST_VAAPI_PICTURE_TYPE_I : GST_VAAPI_PICTURE_TYPE_P;
  picture->pts = GST_VAAPI_DECODER_CODEC_FRAME (decoder)->pts;

  if (!frame_hdr->show_frame)
    GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_SKIPPED);
}

static void
vaapi_fill_ref_frames (GstVaapiDecoderVp9 * decoder, GstVaapiPicture * picture,
    GstVp9FrameHdr * frame_hdr, VADecPictureParameterBufferVP9 * pic_param)
{
  GstVaapiDecoderVp9Private *const priv = &decoder->priv;
  guint i;

  if (frame_hdr->frame_type != GST_VP9_KEY_FRAME) {
    pic_param->pic_fields.bits.last_ref_frame =
        frame_hdr->ref_frame_indices[GST_VP9_REF_FRAME_LAST - 1];
    pic_param->pic_fields.bits.last_ref_frame_sign_bias =
        frame_hdr->ref_frame_sign_bias[GST_VP9_REF_FRAME_LAST - 1];
    pic_param->pic_fields.bits.golden_ref_frame =
        frame_hdr->ref_frame_indices[GST_VP9_REF_FRAME_GOLDEN - 1];
    pic_param->pic_fields.bits.golden_ref_frame_sign_bias =
        frame_hdr->ref_frame_sign_bias[GST_VP9_REF_FRAME_GOLDEN - 1];
    pic_param->pic_fields.bits.alt_ref_frame =
        frame_hdr->ref_frame_indices[GST_VP9_REF_FRAME_ALTREF - 1];
    pic_param->pic_fields.bits.alt_ref_frame_sign_bias =
        frame_hdr->ref_frame_sign_bias[GST_VP9_REF_FRAME_ALTREF - 1];
  }

  for (i = 0; i < G_N_ELEMENTS (priv->ref_frames); i++) {
    pic_param->reference_frames[i] = priv->ref_frames[i] ?
        priv->ref_frames[i]->surface_id : VA_INVALID_SURFACE;
  }
}

static gboolean
fill_picture (GstVaapiDecoderVp9 * decoder, GstVaapiPicture * picture)
{
  GstVaapiDecoderVp9Private *priv = &decoder->priv;
  VADecPictureParameterBufferVP9 *pic_param = picture->param;
  GstVp9Parser *parser = priv->parser;
  GstVp9FrameHdr *frame_hdr = &priv->frame_hdr;

  /* Fill in VAPictureParameterBufferVP9 */
  pic_param->frame_width = frame_hdr->width;
  pic_param->frame_height = frame_hdr->height;

  /* Fill in ReferenceFrames */
  vaapi_fill_ref_frames (decoder, picture, frame_hdr, pic_param);

#define COPY_FIELD(s, f) \
    pic_param->f = (s)->f
#define COPY_BFM(a, s, f) \
    pic_param->a.bits.f = (s)->f

  COPY_BFM (pic_fields, parser, subsampling_x);
  COPY_BFM (pic_fields, parser, subsampling_y);
  COPY_BFM (pic_fields, frame_hdr, frame_type);
  COPY_BFM (pic_fields, frame_hdr, show_frame);
  COPY_BFM (pic_fields, frame_hdr, error_resilient_mode);
  COPY_BFM (pic_fields, frame_hdr, intra_only);
  COPY_BFM (pic_fields, frame_hdr, allow_high_precision_mv);
  COPY_BFM (pic_fields, frame_hdr, mcomp_filter_type);
  COPY_BFM (pic_fields, frame_hdr, frame_parallel_decoding_mode);
  COPY_BFM (pic_fields, frame_hdr, reset_frame_context);
  COPY_BFM (pic_fields, frame_hdr, refresh_frame_context);
  COPY_BFM (pic_fields, frame_hdr, frame_context_idx);
  COPY_BFM (pic_fields, frame_hdr, lossless_flag);

  pic_param->pic_fields.bits.segmentation_enabled =
      frame_hdr->segmentation.enabled;
  pic_param->pic_fields.bits.segmentation_temporal_update =
      frame_hdr->segmentation.temporal_update;
  pic_param->pic_fields.bits.segmentation_update_map =
      frame_hdr->segmentation.update_map;

  COPY_FIELD (&frame_hdr->loopfilter, filter_level);
  COPY_FIELD (&frame_hdr->loopfilter, sharpness_level);
  COPY_FIELD (frame_hdr, log2_tile_rows);
  COPY_FIELD (frame_hdr, log2_tile_columns);
  COPY_FIELD (frame_hdr, frame_header_length_in_bytes);
  COPY_FIELD (frame_hdr, first_partition_size);
  COPY_FIELD (frame_hdr, profile);
#if VA_CHECK_VERSION (0, 39, 0)
  COPY_FIELD (parser, bit_depth);
#endif

  g_assert (G_N_ELEMENTS (pic_param->mb_segment_tree_probs) ==
      G_N_ELEMENTS (parser->mb_segment_tree_probs));
  g_assert (G_N_ELEMENTS (pic_param->segment_pred_probs) ==
      G_N_ELEMENTS (parser->segment_pred_probs));

  memcpy (pic_param->mb_segment_tree_probs, parser->mb_segment_tree_probs,
      sizeof (parser->mb_segment_tree_probs));

  if (frame_hdr->segmentation.temporal_update) {
    memcpy (pic_param->segment_pred_probs, parser->segment_pred_probs,
        sizeof (parser->segment_pred_probs));
  } else {
    memset (pic_param->segment_pred_probs, 255,
        sizeof (pic_param->segment_pred_probs));
  }

  return TRUE;
}

static gboolean
fill_slice (GstVaapiDecoderVp9 * decoder, GstVaapiSlice * slice)
{
  GstVaapiDecoderVp9Private *const priv = &decoder->priv;
  GstVp9Parser *parser = priv->parser;
  VASliceParameterBufferVP9 *const slice_param = slice->param;
  guint i;

#define COPY_SEG_FIELD(s, f) \
    seg_param->f = (s)->f

  /* Fill in VASliceParameterBufferVP9 */
  for (i = 0; i < GST_VP9_MAX_SEGMENTS; i++) {
    VASegmentParameterVP9 *seg_param = &slice_param->seg_param[i];
    GstVp9Segmentation *seg = &parser->segmentation[i];

    memcpy (seg_param->filter_level, seg->filter_level,
        sizeof (seg->filter_level));
    COPY_SEG_FIELD (seg, luma_ac_quant_scale);
    COPY_SEG_FIELD (seg, luma_dc_quant_scale);
    COPY_SEG_FIELD (seg, chroma_ac_quant_scale);
    COPY_SEG_FIELD (seg, chroma_dc_quant_scale);

    seg_param->segment_flags.fields.segment_reference_skipped =
        seg->reference_skip;
    seg_param->segment_flags.fields.segment_reference_enabled =
        seg->reference_frame_enabled;
    seg_param->segment_flags.fields.segment_reference = seg->reference_frame;

  }
  /* Fixme: When segmentation is disabled, only seg_param[0] has valid values,
   * all other entries should be populated with 0  ? */

  return TRUE;
}

static GstVaapiDecoderStatus
decode_slice (GstVaapiDecoderVp9 * decoder, GstVaapiPicture * picture,
    const guchar * buf, guint buf_size)
{
  GstVaapiSlice *slice;

  slice = GST_VAAPI_SLICE_NEW (VP9, decoder, buf, buf_size);
  if (!slice) {
    GST_ERROR ("failed to allocate slice");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  if (!fill_slice (decoder, slice)) {
    gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (slice));
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  gst_vaapi_picture_add_slice (GST_VAAPI_PICTURE_CAST (picture), slice);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
update_ref_frames (GstVaapiDecoderVp9 * decoder)
{
  GstVaapiDecoderVp9Private *const priv = &decoder->priv;
  GstVaapiPicture *picture = priv->current_picture;
  GstVp9FrameHdr *const frame_hdr = &priv->frame_hdr;
  guint8 refresh_frame_flags, mask, i = 0;

  if (frame_hdr->frame_type == GST_VP9_KEY_FRAME)
    refresh_frame_flags = (1 << GST_VP9_REF_FRAMES) - 1;
  else
    refresh_frame_flags = frame_hdr->refresh_frame_flags;

  for (mask = refresh_frame_flags; mask; mask >>= 1, ++i) {
    if (mask & 1)
      gst_vaapi_picture_replace (&priv->ref_frames[i], picture);
  }
}

#ifdef GST_VAAPI_PICTURE_NEW
#undef GST_VAAPI_PICTURE_NEW
#endif

#define GST_VAAPI_PICTURE_NEW(codec, decoder)                   \
  gst_vaapi_picture_new (GST_VAAPI_DECODER_CAST (decoder),      \
      NULL, sizeof (G_PASTE (VADecPictureParameterBuffer, codec)))

static GstVaapiDecoderStatus
decode_picture (GstVaapiDecoderVp9 * decoder, const guchar * buf,
    guint buf_size)
{
  GstVaapiDecoderVp9Private *const priv = &decoder->priv;
  GstVp9FrameHdr *frame_hdr = &priv->frame_hdr;
  GstVaapiPicture *picture;
  GstVaapiDecoderStatus status;
  guint crop_width = 0, crop_height = 0;
  gboolean is_clone_pic = FALSE;

  status = ensure_context (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  /* if show_exising_frame flag is true, we just need to return
   * the existing frame in ref frame array, so creating a clone
   * of already decoded frame */
  if (frame_hdr->show_existing_frame) {
    GstVaapiPicture *existing_frame =
        priv->ref_frames[frame_hdr->frame_to_show];

    if (!existing_frame) {
      GST_ERROR ("Failed to get the existing frame from dpb");
      return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }

    picture = gst_vaapi_picture_new_clone (existing_frame);
    if (!picture) {
      GST_ERROR ("Failed to create clone picture");
      return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    is_clone_pic = TRUE;

    /* for cloned picture we should always unset the skip flag since
     * the previously decoded frame might be decode-only but repeat-frame
     * should make it ready for display */
    GST_VAAPI_PICTURE_FLAG_UNSET (picture, GST_VAAPI_PICTURE_FLAG_SKIPPED);

    /* reset picture pts with whatever set in VideoCodecFrame */
    picture->pts = GST_VAAPI_DECODER_CODEC_FRAME (decoder)->pts;
  } else {
    /* Create new picture */
    picture = GST_VAAPI_PICTURE_NEW (VP9, decoder);
    if (!picture) {
      GST_ERROR ("failed to allocate picture");
      return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
  }
  gst_vaapi_picture_replace (&priv->current_picture, picture);
  gst_vaapi_picture_unref (picture);

  if (is_clone_pic)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  if (priv->width > frame_hdr->width || priv->height > frame_hdr->height) {
    crop_width = frame_hdr->width;
    crop_height = frame_hdr->height;
  }
  if (crop_width || crop_height) {
    GstVaapiRectangle crop_rect;
    crop_rect.x = 0;
    crop_rect.y = 0;
    crop_rect.width = crop_width;
    crop_rect.height = crop_height;
    gst_vaapi_picture_set_crop_rect (picture, &crop_rect);
  }

  init_picture (decoder, picture);
  if (!fill_picture (decoder, picture))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

  return decode_slice (decoder, picture, buf, buf_size);
}


static GstVaapiDecoderStatus
decode_current_picture (GstVaapiDecoderVp9 * decoder)
{
  GstVaapiDecoderVp9Private *const priv = &decoder->priv;
  GstVaapiPicture *const picture = priv->current_picture;
  GstVp9FrameHdr *const frame_hdr = &priv->frame_hdr;

  if (!picture)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  if (frame_hdr->show_existing_frame)
    goto ret;

  if (!gst_vaapi_picture_decode (picture))
    goto error;

  update_ref_frames (decoder);

ret:
  if (!gst_vaapi_picture_output (picture))
    goto error;

  gst_vaapi_picture_replace (&priv->current_picture, NULL);

  return GST_VAAPI_DECODER_STATUS_SUCCESS;

  /* ERRORS */
error:
  {
    /* XXX: fix for cases where first field failed to be decoded */
    gst_vaapi_picture_replace (&priv->current_picture, NULL);
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }
}

static gboolean
parse_super_frame (const guchar * data, guint data_size,
    guint * frame_sizes, guint * frame_count, guint * total_idx_size)
{
  guint8 marker;
  guint32 num_frames = 1, frame_size_length, total_index_size;
  guint i, j;

  if (data_size <= 0)
    return FALSE;

  marker = data[data_size - 1];

  if ((marker & 0xe0) == 0xc0) {

    GST_DEBUG ("Got VP9-Super Frame, size %d", data_size);

    num_frames = (marker & 0x7) + 1;
    frame_size_length = ((marker >> 3) & 0x3) + 1;
    total_index_size = 2 + num_frames * frame_size_length;

    if ((data_size >= total_index_size)
        && (data[data_size - total_index_size] == marker)) {
      const guint8 *x = &data[data_size - total_index_size + 1];

      for (i = 0; i < num_frames; i++) {
        guint32 cur_frame_size = 0;

        for (j = 0; j < frame_size_length; j++)
          cur_frame_size |= (*x++) << (j * 8);

        frame_sizes[i] = cur_frame_size;
      }

      *frame_count = num_frames;
      *total_idx_size = total_index_size;
    } else {
      GST_ERROR ("Failed to parse Super-frame");
      return FALSE;
    }
  } else {
    *frame_count = num_frames;
    frame_sizes[0] = data_size;
    *total_idx_size = 0;
  }

  return TRUE;
}

static GstVaapiDecoderStatus
parse_frame_header (GstVaapiDecoderVp9 * decoder, const guchar * buf,
    guint buf_size, GstVp9FrameHdr * frame_hdr)
{
  GstVaapiDecoderVp9Private *const priv = &decoder->priv;
  GstVp9ParserResult result;
  guint width, height;

  result = gst_vp9_parser_parse_frame_header (priv->parser, frame_hdr,
      buf, buf_size);
  if (result != GST_VP9_PARSER_OK)
    return get_status (result);

  /* Unlike other decoders, vp9 decoder doesn't need to reset the
   * whole context and surfaces for each resolution change. Calling
   * ensure_context() again is only needed if the resolution of any frame
   * is greater than what was previously configured, so that new, larger
   * surfaces can be allocated. There are streams where a bigger
   * resolution set in ivf header or webm header but actual resolution
   * of all frames are less. Also it is possible to have inter-prediction
   * between these multi resolution frames */
  width = GST_VAAPI_DECODER_WIDTH (decoder);
  height = GST_VAAPI_DECODER_HEIGHT (decoder);
  if (priv->width < width || priv->height < height) {
    priv->width = GST_VAAPI_DECODER_WIDTH (decoder);
    priv->height = GST_VAAPI_DECODER_HEIGHT (decoder);
    priv->size_changed = TRUE;
  }
  if ((frame_hdr->width > priv->width || frame_hdr->height > priv->height)) {
    priv->width = frame_hdr->width;
    priv->height = frame_hdr->height;
    priv->size_changed = TRUE;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vp9_parse (GstVaapiDecoder * base_decoder,
    GstAdapter * adapter, gboolean at_eos, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderVp9 *const decoder = GST_VAAPI_DECODER_VP9_CAST (base_decoder);
  GstVaapiDecoderVp9Private *const priv = &decoder->priv;
  guchar *buf;
  guint buf_size, flags = 0;

  buf_size = gst_adapter_available (adapter);
  if (!buf_size)
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
  buf = (guchar *) gst_adapter_map (adapter, buf_size);
  if (!buf)
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

  if (!priv->had_superframe_hdr) {
    if (!parse_super_frame (buf, buf_size, priv->frame_sizes, &priv->num_frames,
            &priv->total_idx_size))
      return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;

    if (priv->num_frames > 1)
      priv->had_superframe_hdr = TRUE;
  }

  unit->size = priv->frame_sizes[priv->frame_cnt++];

  if (priv->frame_cnt == priv->num_frames) {
    priv->num_frames = 0;
    priv->frame_cnt = 0;
    priv->had_superframe_hdr = FALSE;
    unit->size += priv->total_idx_size;
  }

  /* The whole frame is available */
  flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
  flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
  flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END;

  GST_VAAPI_DECODER_UNIT_FLAG_SET (unit, flags);

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_buffer (GstVaapiDecoderVp9 * decoder, const guchar * buf, guint buf_size)
{
  GstVaapiDecoderVp9Private *const priv = &decoder->priv;
  GstVaapiDecoderStatus status;
  guint size = buf_size;

  if (priv->total_idx_size && !priv->had_superframe_hdr) {
    size -= priv->total_idx_size;
    priv->total_idx_size = 0;
  }

  status = parse_frame_header (decoder, buf, size, &priv->frame_hdr);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  return decode_picture (decoder, buf, size);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vp9_decode (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderVp9 *const decoder = GST_VAAPI_DECODER_VP9_CAST (base_decoder);
  GstVaapiDecoderStatus status;
  GstBuffer *const buffer =
      GST_VAAPI_DECODER_CODEC_FRAME (decoder)->input_buffer;
  GstMapInfo map_info;

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
gst_vaapi_decoder_vp9_start_frame (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * base_unit)
{
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vp9_end_frame (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderVp9 *const decoder = GST_VAAPI_DECODER_VP9_CAST (base_decoder);

  return decode_current_picture (decoder);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vp9_flush (GstVaapiDecoder * base_decoder)
{
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
gst_vaapi_decoder_vp9_finalize (GObject * object)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (object);

  gst_vaapi_decoder_vp9_destroy (base_decoder);
  G_OBJECT_CLASS (gst_vaapi_decoder_vp9_parent_class)->finalize (object);
}

static void
gst_vaapi_decoder_vp9_class_init (GstVaapiDecoderVp9Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiDecoderClass *const decoder_class = GST_VAAPI_DECODER_CLASS (klass);

  object_class->finalize = gst_vaapi_decoder_vp9_finalize;

  decoder_class->reset = gst_vaapi_decoder_vp9_reset;
  decoder_class->parse = gst_vaapi_decoder_vp9_parse;
  decoder_class->decode = gst_vaapi_decoder_vp9_decode;
  decoder_class->start_frame = gst_vaapi_decoder_vp9_start_frame;
  decoder_class->end_frame = gst_vaapi_decoder_vp9_end_frame;
  decoder_class->flush = gst_vaapi_decoder_vp9_flush;
}

static void
gst_vaapi_decoder_vp9_init (GstVaapiDecoderVp9 * decoder)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (decoder);

  gst_vaapi_decoder_vp9_create (base_decoder);
}

/**
 * gst_vaapi_decoder_vp9_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps holding codec information
 *
 * Creates a new #GstVaapiDecoder for VP9 decoding.  The @caps can
 * hold extra information like codec-data and pictured coded size.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_vp9_new (GstVaapiDisplay * display, GstCaps * caps)
{
  return g_object_new (GST_TYPE_VAAPI_DECODER_VP9, "display", display,
      "caps", caps, NULL);
}
