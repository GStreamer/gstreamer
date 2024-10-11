/* GStreamer
 *  Copyright (C) 2024 Intel Corporation
 *     Author: He Junyan <junyan.he@intel.com>
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

/**
 * SECTION:element-vajpegenc
 * @title: vajpegenc
 * @short_description: A VA-API based JPEG video encoder
 *
 * vajpegenc encodes raw video VA surfaces into JPEG bitstreams using
 * the installed and chosen [VA-API](https://01.org/linuxmedia/vaapi)
 * driver.
 *
 * The raw video frames in main memory can be imported into VA surfaces.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc num-buffers=60 ! timeoverlay ! vajpegenc ! jpegparse ! filesink location=test.mjpeg
 * ```
 *
 * Since: 1.26
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvajpegenc.h"

#include <gst/codecparsers/gstjpegbitwriter.h>
#include <gst/va/gstva.h>
#include <gst/va/gstvavideoformat.h>
#include <gst/video/video.h>
#include <va/va_drmcommon.h>

#include "vacompat.h"
#include "gstvabaseenc.h"
#include "gstvaencoder.h"
#include "gstvacaps.h"
#include "gstvaprofile.h"
#include "gstvadisplay_priv.h"
#include "gstvapluginutils.h"

GST_DEBUG_CATEGORY_STATIC (gst_va_jpegenc_debug);
#define GST_CAT_DEFAULT gst_va_jpegenc_debug

#define GST_VA_JPEG_ENC(obj)            ((GstVaJpegEnc *) obj)
#define GST_VA_JPEG_ENC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_TYPE_FROM_INSTANCE (obj), GstVaJpegEncClass))
#define GST_VA_JPEG_ENC_CLASS(klass)    ((GstVaJpegEncClass *) klass)

typedef struct _GstVaJpegEnc GstVaJpegEnc;
typedef struct _GstVaJpegEncClass GstVaJpegEncClass;

enum
{
  PROP_QUALITY = 1,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES];

static GstElementClass *parent_class = NULL;

/* Maximum sizes for common segment (in bytes) */
#define MAX_APP_HDR_SIZE 20
#define MAX_FRAME_HDR_SIZE 19
#define MAX_QUANT_TABLE_SIZE 138
#define MAX_HUFFMAN_TABLE_SIZE 432
#define MAX_SCAN_HDR_SIZE 14

struct _GstVaJpegEncClass
{
  GstVaBaseEncClass parent_class;
};

struct _GstVaJpegEnc
{
  /*< private > */
  GstVaBaseEnc parent;

  /* JPEG fields */
  guint32 quality;

  guint32 packed_headers;

  gint cwidth[GST_VIDEO_MAX_COMPONENTS];
  gint cheight[GST_VIDEO_MAX_COMPONENTS];
  gint h_samp[GST_VIDEO_MAX_COMPONENTS];
  gint v_samp[GST_VIDEO_MAX_COMPONENTS];
  gint h_max_samp;
  gint v_max_samp;
  guint n_components;
  GstJpegQuantTables quant_tables;
  GstJpegQuantTables scaled_quant_tables;
  gboolean has_quant_tables;
  GstJpegHuffmanTables huff_tables;
  gboolean has_huff_tables;
};

static void
gst_va_jpeg_enc_frame_free (gpointer pframe)
{
  GstVaEncFrame *frame = pframe;

  g_clear_pointer (&frame->picture, gst_va_encode_picture_free);
  g_free (frame);
}

static gboolean
gst_va_jpeg_enc_new_frame (GstVaBaseEnc * base, GstVideoCodecFrame * frame)
{
  GstVaEncFrame *frame_in;

  frame_in = g_new0 (GstVaEncFrame, 1);
  gst_va_set_enc_frame (frame, frame_in, gst_va_jpeg_enc_frame_free);

  return TRUE;
}

static inline GstVaEncFrame *
_enc_frame (GstVideoCodecFrame * frame)
{
  GstVaEncFrame *enc_frame = gst_video_codec_frame_get_user_data (frame);
  g_assert (enc_frame);

  return enc_frame;
}

static gboolean
_ensure_profile (GstVaJpegEnc * self)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);

  if (!gst_va_encoder_has_profile (base->encoder, VAProfileJPEGBaseline)) {
    GST_ERROR_OBJECT (self, "No jpeg profile found");
    return FALSE;
  }

  return TRUE;
}

static void
_jpeg_generate_sampling_factors (GstVaJpegEnc * self)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  const GstVideoInfo *vinfo;
  gint i;

  vinfo = &base->in_info;

  self->n_components = GST_VIDEO_INFO_N_COMPONENTS (vinfo);

  self->h_max_samp = 0;
  self->v_max_samp = 0;
  for (i = 0; i < self->n_components; ++i) {
    self->cwidth[i] = GST_VIDEO_INFO_COMP_WIDTH (vinfo, i);
    self->cheight[i] = GST_VIDEO_INFO_COMP_HEIGHT (vinfo, i);
    self->h_samp[i] =
        GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (vinfo)) / self->cwidth[i];
    self->h_max_samp = MAX (self->h_max_samp, self->h_samp[i]);
    self->v_samp[i] =
        GST_ROUND_UP_4 (GST_VIDEO_INFO_HEIGHT (vinfo)) / self->cheight[i];
    self->v_max_samp = MAX (self->v_max_samp, self->v_samp[i]);
  }
  /* samp should only be 1, 2 or 4 */
  g_assert (self->h_max_samp <= 4);
  g_assert (self->v_max_samp <= 4);

  /* now invert */
  /* maximum is invariant, as one of the components should have samp 1 */
  for (i = 0; i < self->n_components; ++i) {
    self->h_samp[i] = self->h_max_samp / self->h_samp[i];
    self->v_samp[i] = self->v_max_samp / self->v_samp[i];
    GST_DEBUG_OBJECT (self, "sampling factors: %d %d", self->h_samp[i],
        self->v_samp[i]);
  }
}

static void
_jpeg_calculate_coded_size (GstVaJpegEnc * self)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  guint codedbuf_size = 0;

  /* Just set a conservative size */
  codedbuf_size = GST_ROUND_UP_16 (base->width) *
      GST_ROUND_UP_16 (base->height) * 3;

  codedbuf_size += MAX_APP_HDR_SIZE + MAX_FRAME_HDR_SIZE +
      MAX_QUANT_TABLE_SIZE + MAX_HUFFMAN_TABLE_SIZE + MAX_SCAN_HDR_SIZE;

  base->codedbuf_size = codedbuf_size;
  GST_DEBUG_OBJECT (self, "Calculate codedbuf size: %u", base->codedbuf_size);
}

static gboolean
_jpeg_init_packed_headers (GstVaJpegEnc * self)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  guint32 packed_headers;
  /* JPEG segments info */
  guint32 desired_packed_headers = VA_ENC_PACKED_HEADER_RAW_DATA;

  self->packed_headers = 0;

  if (!gst_va_encoder_get_packed_headers (base->encoder, base->profile,
          GST_VA_BASE_ENC_ENTRYPOINT (base), &packed_headers))
    return FALSE;

  if (desired_packed_headers & ~packed_headers) {
    GST_INFO_OBJECT (self, "Driver does not support some wanted packed headers "
        "(wanted %#x, found %#x)", desired_packed_headers, packed_headers);
  }

  self->packed_headers = desired_packed_headers & packed_headers;

  return TRUE;
}

static gboolean
_jpeg_get_capability_attribute (GstVaJpegEnc * self)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);

  VAStatus status;
  VAConfigAttrib attrib = {.type = VAConfigAttribEncJPEG };
  VAConfigAttribValEncJPEG jpeg_attrib_val;

  status = vaGetConfigAttributes (gst_va_display_get_va_dpy (base->display),
      base->profile, GST_VA_BASE_ENC_ENTRYPOINT (base), &attrib, 1);
  if (status != VA_STATUS_SUCCESS) {
    GST_INFO_OBJECT (self, "Failed to query encoding features: %s",
        vaErrorStr (status));
    /* If no such attribute, we just assume that everything is OK. */
    return TRUE;
  }

  jpeg_attrib_val.value = attrib.value;

  GST_DEBUG_OBJECT (self, "Get jpeg attribute, arithmatic_coding_mode: %d, "
      "progressive_dct_mode: %d, non_interleaved_mode: %d, "
      "differential_mode %d, max_num_components %d, max_num_scans %d, "
      "max_num_huffman_tables %d, max_num_quantization_tables %d",
      jpeg_attrib_val.bits.arithmatic_coding_mode,
      jpeg_attrib_val.bits.progressive_dct_mode,
      jpeg_attrib_val.bits.non_interleaved_mode,
      jpeg_attrib_val.bits.differential_mode,
      jpeg_attrib_val.bits.max_num_components,
      jpeg_attrib_val.bits.max_num_scans,
      jpeg_attrib_val.bits.max_num_huffman_tables,
      jpeg_attrib_val.bits.max_num_quantization_tables);

  if (jpeg_attrib_val.bits.arithmatic_coding_mode) {
    GST_ERROR_OBJECT (self, "arithmatic_coding_mode is not supported");
    return FALSE;
  }

  if (jpeg_attrib_val.bits.progressive_dct_mode) {
    GST_ERROR_OBJECT (self, "progressive_dct_mode is not supported");
    return FALSE;
  }

  /* It seems that we need to do nothing to switch the
     non_interleaved_mode/interleaved_mode in our code, so both
     modes are OK for us. */

  if (jpeg_attrib_val.bits.differential_mode) {
    GST_ERROR_OBJECT (self, "differential_mode is not supported");
    return FALSE;
  }

  if (jpeg_attrib_val.bits.max_num_huffman_tables < 1) {
    GST_ERROR_OBJECT (self, "need at least 1 huffman table.");
    return FALSE;
  }

  if (jpeg_attrib_val.bits.max_num_quantization_tables < 2) {
    GST_ERROR_OBJECT (self, "need at least 2 quantization tables for "
        "luma and chroma.");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_va_jpeg_enc_reconfig (GstVaBaseEnc * base)
{
  GstVaBaseEncClass *klass = GST_VA_BASE_ENC_GET_CLASS (base);
  GstVideoEncoder *venc = GST_VIDEO_ENCODER (base);
  GstVaJpegEnc *self = GST_VA_JPEG_ENC (base);
  GstCaps *out_caps, *reconf_caps = NULL;
  GstVideoCodecState *output_state = NULL;
  gboolean do_renegotiation = TRUE, do_reopen, need_negotiation;
  gint width, height;
  GstVideoFormat format, reconf_format = GST_VIDEO_FORMAT_UNKNOWN;
  guint rt_format = 0, codedbuf_size, latency_num,
      max_surfaces = 0, max_cached_frames;
  const char *colorspace, *sampling;

  width = GST_VIDEO_INFO_WIDTH (&base->in_info);
  height = GST_VIDEO_INFO_HEIGHT (&base->in_info);
  format = GST_VIDEO_INFO_FORMAT (&base->in_info);
  codedbuf_size = base->codedbuf_size;
  latency_num = base->preferred_output_delay;

  need_negotiation =
      !gst_va_encoder_get_reconstruct_pool_config (base->encoder, &reconf_caps,
      &max_surfaces);
  if (!need_negotiation && reconf_caps) {
    GstVideoInfo vi;
    if (!gst_video_info_from_caps (&vi, reconf_caps))
      return FALSE;
    reconf_format = GST_VIDEO_INFO_FORMAT (&vi);
  }

  rt_format = gst_va_chroma_from_video_format (format);
  if (!rt_format) {
    GST_ERROR_OBJECT (self, "unrecognized input format.");
    return FALSE;
  }

  if (!_ensure_profile (self))
    return FALSE;

  /* first check */
  do_reopen = !(base->profile == VAProfileJPEGBaseline
      && base->rt_format == rt_format
      && format == reconf_format && width == base->width
      && height == base->height);

  if (do_reopen && gst_va_encoder_is_open (base->encoder))
    gst_va_encoder_close (base->encoder);

  gst_va_base_enc_reset_state (base);

  if (base->is_live) {
    base->preferred_output_delay = 0;
  } else {
    /* FIXME: An experience value for most of the platforms. */
    base->preferred_output_delay = 4;
  }

  base->profile = VAProfileJPEGBaseline;
  base->rt_format = rt_format;
  base->width = width;
  base->height = height;
  GST_DEBUG_OBJECT (self, "resolution: %dx%d", base->width, base->height);

  _jpeg_generate_sampling_factors (self);

  _jpeg_calculate_coded_size (self);

  if (!_jpeg_init_packed_headers (self))
    return FALSE;

  /* Let the downstream know the new latency. */
  if (latency_num != base->preferred_output_delay) {
    need_negotiation = TRUE;
    latency_num = base->preferred_output_delay;
  }

  /* Unknown frame rate is allowed for jpeg, such as a single still image. */
  if (GST_VIDEO_INFO_FPS_N (&base->in_info) == 0
      || GST_VIDEO_INFO_FPS_D (&base->in_info) == 0) {
    GST_DEBUG_OBJECT (self, "Unknown framerate");
    GST_VIDEO_INFO_FPS_N (&base->in_info) = 0;
    GST_VIDEO_INFO_FPS_D (&base->in_info) = 1;
    base->frame_duration = GST_CLOCK_TIME_NONE;
  } else {
    GstClockTime latency;

    base->frame_duration = gst_util_uint64_scale (GST_SECOND,
        GST_VIDEO_INFO_FPS_D (&base->in_info),
        GST_VIDEO_INFO_FPS_N (&base->in_info));
    GST_DEBUG_OBJECT (self, "frame duration is %" GST_TIME_FORMAT,
        GST_TIME_ARGS (base->frame_duration));

    /* Set the latency */
    latency = gst_util_uint64_scale (latency_num,
        GST_VIDEO_INFO_FPS_D (&base->input_state->info) * GST_SECOND,
        GST_VIDEO_INFO_FPS_N (&base->input_state->info));
    gst_video_encoder_set_latency (venc, latency, latency);
  }

  max_cached_frames = base->preferred_output_delay;
  base->min_buffers = max_cached_frames;
  max_cached_frames += 3 /* scratch frames */ ;

  /* second check after calculations */
  do_reopen |= !(max_cached_frames == max_surfaces &&
      codedbuf_size == base->codedbuf_size);
  if (do_reopen && gst_va_encoder_is_open (base->encoder))
    gst_va_encoder_close (base->encoder);

  /* Just use driver's capability attribute, we do not change them. */
  if (!_jpeg_get_capability_attribute (self)) {
    GST_ERROR_OBJECT (self, "Failed to satisfy the jpeg capability.");
    return FALSE;
  }

  if (!gst_va_encoder_is_open (base->encoder)
      && !gst_va_encoder_open (base->encoder, base->profile, format,
          base->rt_format, base->width, base->height, base->codedbuf_size,
          1, VA_RC_NONE, self->packed_headers)) {
    GST_ERROR_OBJECT (self, "Failed to open the VA encoder.");
    return FALSE;
  }

  /* Add some tags */
  gst_va_base_enc_add_codec_tag (base, "JPEG");

  out_caps = gst_va_profile_caps (base->profile, klass->entrypoint);
  g_assert (out_caps);
  GST_WARNING ("caps: %" GST_PTR_FORMAT, out_caps);
  out_caps = gst_caps_fixate (out_caps);

  if (GST_VIDEO_INFO_IS_YUV (&base->in_info)) {
    gint w_sub, h_sub;

    colorspace = "sYUV";

    w_sub = 1 << GST_VIDEO_FORMAT_INFO_W_SUB (base->in_info.finfo, 1);
    h_sub = 1 << GST_VIDEO_FORMAT_INFO_H_SUB (base->in_info.finfo, 1);

    if (w_sub == 1 && h_sub == 1) {
      sampling = "YCbCr-4:4:4";
    } else if (w_sub == 2 && h_sub == 1) {
      sampling = "YCbCr-4:2:2";
    } else if (w_sub == 2 && h_sub == 2) {
      sampling = "YCbCr-4:2:0";
    } else {
      sampling = NULL;
    }
  } else if (GST_VIDEO_INFO_IS_RGB (&base->in_info)) {
    colorspace = "sRGB";
    switch (GST_VIDEO_INFO_FORMAT (&base->in_info)) {
      case GST_VIDEO_FORMAT_BGRA:
      case GST_VIDEO_FORMAT_BGR:
      case GST_VIDEO_FORMAT_ABGR:
      case GST_VIDEO_FORMAT_xBGR:
      case GST_VIDEO_FORMAT_BGRx:
        sampling = "BGR";
        break;
      case GST_VIDEO_FORMAT_RGBA:
      case GST_VIDEO_FORMAT_ARGB:
      case GST_VIDEO_FORMAT_RGBx:
      case GST_VIDEO_FORMAT_xRGB:
      case GST_VIDEO_FORMAT_RGB:
        sampling = "RGB";
        break;
      default:
        sampling = NULL;
    }
  } else if (GST_VIDEO_INFO_IS_GRAY (&base->in_info)) {
    colorspace = "GRAY";
    sampling = "GRAYSCALE";
  } else {
    colorspace = sampling = NULL;
  }

  gst_caps_set_simple (out_caps, "width", G_TYPE_INT, base->width,
      "height", G_TYPE_INT, base->height, "interlace-mode", G_TYPE_STRING,
      "progressive", NULL);

  if (colorspace) {
    gst_caps_set_simple (out_caps, "colorspace", G_TYPE_STRING, colorspace,
        NULL);
  }
  if (sampling)
    gst_caps_set_simple (out_caps, "sampling", G_TYPE_STRING, sampling, NULL);

  if (!need_negotiation) {
    output_state = gst_video_encoder_get_output_state (venc);
    do_renegotiation = TRUE;
    if (output_state) {
      do_renegotiation = !gst_caps_is_subset (output_state->caps, out_caps);
      gst_video_codec_state_unref (output_state);
    }
    if (!do_renegotiation) {
      gst_caps_unref (out_caps);
      return TRUE;
    }
  }

  GST_DEBUG_OBJECT (self, "output caps is %" GST_PTR_FORMAT, out_caps);

  output_state =
      gst_video_encoder_set_output_state (venc, out_caps, base->input_state);
  gst_video_codec_state_unref (output_state);

  if (!gst_video_encoder_negotiate (venc)) {
    GST_ERROR_OBJECT (self, "Failed to negotiate with the downstream");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_va_jpeg_enc_reorder_frame (GstVaBaseEnc * base, GstVideoCodecFrame * frame,
    gboolean bump_all, GstVideoCodecFrame ** out_frame)
{
  *out_frame = frame;
  return TRUE;
}

static void
gst_va_jpeg_enc_reset_state (GstVaBaseEnc * base)
{
  GstVaJpegEnc *self = GST_VA_JPEG_ENC (base);

  GST_VA_BASE_ENC_CLASS (parent_class)->reset_state (base);

  self->packed_headers = 0;
  memset (self->cwidth, 0, sizeof (self->cwidth));
  memset (self->cheight, 0, sizeof (self->cheight));
  memset (self->h_samp, 0, sizeof (self->h_samp));
  memset (self->v_samp, 0, sizeof (self->v_samp));
  self->h_max_samp = 0;
  self->v_max_samp = 0;
  self->n_components = 0;
  memset (&self->quant_tables, 0, sizeof (GstJpegQuantTables));
  memset (&self->scaled_quant_tables, 0, sizeof (GstJpegQuantTables));
  self->has_quant_tables = FALSE;
  memset (&self->huff_tables, 0, sizeof (GstJpegHuffmanTables));
  self->has_huff_tables = FALSE;
}

static void
_jpeg_fill_picture (GstVaJpegEnc * self, GstVaEncFrame * frame,
    VAEncPictureParameterBufferJPEG * pic_param, guint32 quality)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  guint i;

  /* *INDENT-OFF* */
  *pic_param = (VAEncPictureParameterBufferJPEG) {
    .reconstructed_picture =
        gst_va_encode_picture_get_reconstruct_surface (frame->picture),
    .picture_width = base->width,
    .picture_height = base->height,
    .coded_buf = frame->picture->coded_buffer,
    /* Profile = Baseline */
    .pic_flags.bits.profile = 0,
    /* Sequential encoding */
    .pic_flags.bits.progressive = 0,
    /* Uses Huffman coding */
    .pic_flags.bits.huffman = 1,
    /* Input format is non interleaved (YUV) */
    .pic_flags.bits.interleaved = 0,
    /* non-Differential Encoding */
    .pic_flags.bits.differential = 0,
    .sample_bit_depth = 8,
    .num_scan = 1,
    .num_components = self->n_components,
    .quality = quality,
  };
  /* *INDENT-ON* */

  for (i = 0; i < pic_param->num_components; i++) {
    pic_param->component_id[i] = i + 1;
    if (i != 0)
      pic_param->quantiser_table_selector[i] = 1;
  }
}

static gboolean
_jpeg_add_picture_parameter (GstVaJpegEnc * self, GstVaEncFrame * frame,
    VAEncPictureParameterBufferJPEG * pic_param)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);

  if (!gst_va_encoder_add_param (base->encoder, frame->picture,
          VAEncPictureParameterBufferType, pic_param,
          sizeof (VAEncPictureParameterBufferJPEG))) {
    GST_ERROR_OBJECT (self, "Failed to create the picture parameter");
    return FALSE;
  }

  return TRUE;
}

/* Normalize the quality factor and scale QM values. */
static void
_jpeg_generate_scaled_qm (GstJpegQuantTables * quant_tables,
    GstJpegQuantTables * scaled_quant_tables, guint quality, guint shift)
{
  guint qt_val, nm_quality, i;

  nm_quality = quality == 0 ? 1 : quality;
  nm_quality =
      (nm_quality < 50) ? (5000 / nm_quality) : (200 - (nm_quality * 2));

  g_assert (quant_tables != NULL);
  g_assert (scaled_quant_tables != NULL);

  scaled_quant_tables->quant_tables[0].quant_precision =
      quant_tables->quant_tables[0].quant_precision;
  scaled_quant_tables->quant_tables[0].valid =
      quant_tables->quant_tables[0].valid;
  scaled_quant_tables->quant_tables[1].quant_precision =
      quant_tables->quant_tables[1].quant_precision;
  scaled_quant_tables->quant_tables[1].valid =
      quant_tables->quant_tables[1].valid;

  for (i = 0; i < GST_JPEG_MAX_QUANT_ELEMENTS; i++) {
    /* Luma QM */
    qt_val =
        (quant_tables->quant_tables[0].quant_table[i] * nm_quality +
        shift) / 100;
    scaled_quant_tables->quant_tables[0].quant_table[i] =
        CLAMP (qt_val, 1, 255);
    /* Chroma QM */
    qt_val =
        (quant_tables->quant_tables[1].quant_table[i] * nm_quality +
        shift) / 100;
    scaled_quant_tables->quant_tables[1].quant_table[i] =
        CLAMP (qt_val, 1, 255);
  }
}

static void
_jpeg_fill_quantization_table (GstVaJpegEnc * self,
    VAQMatrixBufferJPEG * q_matrix, guint32 quality)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  guint i;

  if (!self->has_quant_tables) {
    guint shift = 0;

    if (gst_va_display_is_implementation (base->display,
            GST_VA_IMPLEMENTATION_INTEL_IHD))
      shift = 50;

    gst_jpeg_get_default_quantization_tables (&self->quant_tables);
    /* Just use table 0 and 1 */
    self->quant_tables.quant_tables[2].valid = FALSE;
    self->quant_tables.quant_tables[3].valid = FALSE;

    _jpeg_generate_scaled_qm (&self->quant_tables,
        &self->scaled_quant_tables, quality, shift);

    self->has_quant_tables = TRUE;
  }

  q_matrix->load_lum_quantiser_matrix = 1;
  for (i = 0; i < GST_JPEG_MAX_QUANT_ELEMENTS; i++) {
    q_matrix->lum_quantiser_matrix[i] =
        self->quant_tables.quant_tables[0].quant_table[i];
  }

  q_matrix->load_chroma_quantiser_matrix = 1;
  for (i = 0; i < GST_JPEG_MAX_QUANT_ELEMENTS; i++) {
    q_matrix->chroma_quantiser_matrix[i] =
        self->quant_tables.quant_tables[1].quant_table[i];
  }
}

static gboolean
_jpeg_add_quantization_table (GstVaJpegEnc * self, GstVaEncFrame * frame,
    VAQMatrixBufferJPEG * q_matrix)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);

  if (!gst_va_encoder_add_param (base->encoder, frame->picture,
          VAQMatrixBufferType, q_matrix, sizeof (VAQMatrixBufferJPEG))) {
    GST_ERROR_OBJECT (self, "Failed to create the quantization table");
    return FALSE;
  }

  return TRUE;
}

static void
_jpeg_fill_huffman_table (GstVaJpegEnc * self,
    VAHuffmanTableBufferJPEGBaseline * huffman_table)
{
  guint i, num_tables;

  num_tables = MIN (G_N_ELEMENTS (huffman_table->huffman_table),
      GST_JPEG_MAX_SCAN_COMPONENTS);

  if (!self->has_huff_tables) {
    gst_jpeg_get_default_huffman_tables (&self->huff_tables);
    self->has_huff_tables = TRUE;
  }

  for (i = 0; i < num_tables; i++) {
    huffman_table->load_huffman_table[i] =
        self->huff_tables.dc_tables[i].valid
        && self->huff_tables.ac_tables[i].valid;
    if (!huffman_table->load_huffman_table[i])
      continue;

    memcpy (huffman_table->huffman_table[i].num_dc_codes,
        self->huff_tables.dc_tables[i].huf_bits,
        sizeof (huffman_table->huffman_table[i].num_dc_codes));
    memcpy (huffman_table->huffman_table[i].dc_values,
        self->huff_tables.dc_tables[i].huf_values,
        sizeof (huffman_table->huffman_table[i].dc_values));
    memcpy (huffman_table->huffman_table[i].num_ac_codes,
        self->huff_tables.ac_tables[i].huf_bits,
        sizeof (huffman_table->huffman_table[i].num_ac_codes));
    memcpy (huffman_table->huffman_table[i].ac_values,
        self->huff_tables.ac_tables[i].huf_values,
        sizeof (huffman_table->huffman_table[i].ac_values));
    memset (huffman_table->huffman_table[i].pad,
        0, sizeof (huffman_table->huffman_table[i].pad));
  }
}

static gboolean
_jpeg_add_huffman_table (GstVaJpegEnc * self, GstVaEncFrame * frame,
    VAHuffmanTableBufferJPEGBaseline * huffman_table)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);

  if (!gst_va_encoder_add_param (base->encoder, frame->picture,
          VAHuffmanTableBufferType, huffman_table,
          sizeof (VAHuffmanTableBufferJPEGBaseline))) {
    GST_ERROR_OBJECT (self, "Failed to create the huffman table");
    return FALSE;
  }

  return TRUE;
}

static void
_jpeg_fill_slice (GstVaJpegEnc * self,
    VAEncPictureParameterBufferJPEG * pic_param,
    VAEncSliceParameterBufferJPEG * slice_param)
{
  /* *INDENT-OFF* */
  *slice_param = (VAEncSliceParameterBufferJPEG) {
    .restart_interval = 0,
    .num_components = pic_param->num_components,
    .components[0].component_selector = 1,
    .components[0].dc_table_selector = 0,
    .components[0].ac_table_selector = 0,
    .components[1].component_selector = 2,
    .components[1].dc_table_selector = 1,
    .components[1].ac_table_selector = 1,
    .components[2].component_selector = 3,
    .components[2].dc_table_selector = 1,
    .components[2].ac_table_selector = 1,
  };
  /* *INDENT-ON* */
}

static gboolean
_jpeg_add_slice_parameter (GstVaJpegEnc * self, GstVaEncFrame * frame,
    VAEncSliceParameterBufferJPEG * slice_param)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);

  if (!gst_va_encoder_add_param (base->encoder, frame->picture,
          VAEncSliceParameterBufferType, slice_param,
          sizeof (VAEncSliceParameterBufferJPEG))) {
    GST_ERROR_OBJECT (self, "Failed to create the slice parameter");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_jpeg_create_and_add_packed_segments (GstVaJpegEnc * self,
    GstVaEncFrame * frame, VAEncPictureParameterBufferJPEG * pic_param,
    VAEncSliceParameterBufferJPEG * slice_param)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  GstJpegBitWriterResult writer_res;
  guint8 data[2048] = { 0, };
  guint8 app_data[14] = {
    0x4A /* J */ ,
    0x46 /* F */ ,
    0x49 /* I */ ,
    0x46 /* F */ ,
    0x00 /* 0 */ ,
    0x01 /* Major Version */ ,
    0x02 /* Minor Version */ ,
    0x00 /* Density units 0:no units, 1:pixels per inch, 2: pixels per cm */ ,
    0x00, 0x01 /* X density (pixel-aspect-ratio) */ ,
    0x00, 0x01 /* Y density (pixel-aspect-ratio) */ ,
    0x00 /* Thumbnail width */ ,
    0x00 /* Thumbnail height */ ,
  };
  GstJpegFrameHdr frame_hdr;
  GstJpegScanHdr scan_hdr = { 0, };
  guint i;
  guint size, offset;

  /* SOI */
  offset = 0;
  size = sizeof (data);
  writer_res = gst_jpeg_bit_writer_segment_with_data (GST_JPEG_MARKER_SOI,
      NULL, 0, data, &size);
  if (writer_res != GST_JPEG_BIT_WRITER_OK)
    return FALSE;

  /* APP0 */
  offset += size;
  size = sizeof (data) - offset;
  writer_res = gst_jpeg_bit_writer_segment_with_data (GST_JPEG_MARKER_APP_MIN,
      app_data, sizeof (app_data), data + offset, &size);
  if (writer_res != GST_JPEG_BIT_WRITER_OK)
    return FALSE;

  /* Quantization tables */
  g_assert (self->has_quant_tables);
  offset += size;
  size = sizeof (data) - offset;
  writer_res =
      gst_jpeg_bit_writer_quantization_table (&self->scaled_quant_tables,
      data + offset, &size);
  if (writer_res != GST_JPEG_BIT_WRITER_OK)
    return FALSE;

  /* SOF */
  /* *INDENT-OFF* */
  frame_hdr = (GstJpegFrameHdr) {
    .sample_precision = 8,
    .width = pic_param->picture_width,
    .height = pic_param->picture_height,
    .num_components = pic_param->num_components,
  };
  /* *INDENT-ON* */
  for (i = 0; i < frame_hdr.num_components; i++) {
    frame_hdr.components[i].identifier = pic_param->component_id[i];
    frame_hdr.components[i].horizontal_factor = self->h_samp[i];
    frame_hdr.components[i].vertical_factor = self->v_samp[i];
    frame_hdr.components[i].quant_table_selector =
        pic_param->quantiser_table_selector[i];
  };

  offset += size;
  size = sizeof (data) - offset;
  writer_res = gst_jpeg_bit_writer_frame_header (&frame_hdr,
      GST_JPEG_MARKER_SOF_MIN, data + offset, &size);
  if (writer_res != GST_JPEG_BIT_WRITER_OK)
    return FALSE;

  /* huffman tables */
  g_assert (self->has_huff_tables);
  offset += size;
  size = sizeof (data) - offset;
  writer_res = gst_jpeg_bit_writer_huffman_table (&self->huff_tables,
      data + offset, &size);
  if (writer_res != GST_JPEG_BIT_WRITER_OK)
    return FALSE;

  /* Scan header */
  scan_hdr.num_components = slice_param->num_components;
  for (i = 0; i < frame_hdr.num_components; i++) {
    scan_hdr.components[i].component_selector =
        slice_param->components[i].component_selector;
    scan_hdr.components[i].dc_selector =
        slice_param->components[i].dc_table_selector;
    scan_hdr.components[i].ac_selector =
        slice_param->components[i].ac_table_selector;
  }

  offset += size;
  size = sizeof (data) - offset;
  writer_res = gst_jpeg_bit_writer_scan_header (&scan_hdr,
      data + offset, &size);
  if (writer_res != GST_JPEG_BIT_WRITER_OK)
    return FALSE;

  offset += size;

  if (!gst_va_encoder_add_packed_header (base->encoder, frame->picture,
          VAEncPackedHeaderRawData, data, offset * 8, FALSE)) {
    GST_ERROR_OBJECT (self, "Failed to add packed segment data");
    return FALSE;
  }

  return TRUE;
}

static gboolean
_jpeg_encode_one_frame (GstVaJpegEnc * self, GstVideoCodecFrame * gst_frame)
{
  GstVaBaseEnc *base = GST_VA_BASE_ENC (self);
  GstVaEncFrame *frame;
  VAEncPictureParameterBufferJPEG pic_param;
  VAQMatrixBufferJPEG q_matrix;
  VAHuffmanTableBufferJPEGBaseline huffman_table;
  VAEncSliceParameterBufferJPEG slice_param;
  guint32 quality;

  g_return_val_if_fail (gst_frame, FALSE);

  frame = _enc_frame (gst_frame);

  GST_OBJECT_LOCK (self);
  quality = self->quality;
  GST_OBJECT_UNLOCK (self);

  _jpeg_fill_quantization_table (self, &q_matrix, quality);
  if (!_jpeg_add_quantization_table (self, frame, &q_matrix))
    return FALSE;

  _jpeg_fill_huffman_table (self, &huffman_table);
  if (!_jpeg_add_huffman_table (self, frame, &huffman_table))
    return FALSE;

  _jpeg_fill_picture (self, frame, &pic_param, quality);
  if (!_jpeg_add_picture_parameter (self, frame, &pic_param))
    return FALSE;

  _jpeg_fill_slice (self, &pic_param, &slice_param);
  if (!_jpeg_add_slice_parameter (self, frame, &slice_param))
    return FALSE;

  if (!_jpeg_create_and_add_packed_segments (self, frame, &pic_param,
          &slice_param)) {
    GST_ERROR_OBJECT (self, "Failed to create packed segments");
    return FALSE;
  }

  if (!gst_va_encoder_encode (base->encoder, frame->picture)) {
    GST_ERROR_OBJECT (self, "Encode frame error");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_va_jpeg_enc_encode_frame (GstVaBaseEnc * base,
    GstVideoCodecFrame * gst_frame, gboolean is_last)
{
  GstVaJpegEnc *self = GST_VA_JPEG_ENC (base);
  GstVaEncFrame *frame;

  frame = _enc_frame (gst_frame);

  g_assert (frame->picture == NULL);
  frame->picture = gst_va_encode_picture_new (base->encoder,
      gst_frame->input_buffer);

  if (!frame->picture) {
    GST_ERROR_OBJECT (self, "Failed to create the encode picture");
    return GST_FLOW_ERROR;
  }

  if (!_jpeg_encode_one_frame (self, gst_frame)) {
    GST_ERROR_OBJECT (self, "Failed to encode the frame");
    return GST_FLOW_ERROR;
  }

  g_queue_push_tail (&base->output_list, gst_video_codec_frame_ref (gst_frame));

  return GST_FLOW_OK;
}

static gboolean
gst_va_jpeg_enc_prepare_output (GstVaBaseEnc * base,
    GstVideoCodecFrame * frame, gboolean * complete)
{
  GstVaEncFrame *frame_enc;
  GstBuffer *buf;

  frame_enc = _enc_frame (frame);

  buf = gst_va_base_enc_create_output_buffer (base,
      frame_enc->picture, NULL, 0);
  if (!buf) {
    GST_ERROR_OBJECT (base, "Failed to create output buffer");
    return FALSE;
  }

  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_MARKER);
  GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
  GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

  gst_buffer_replace (&frame->output_buffer, buf);
  gst_clear_buffer (&buf);

  *complete = TRUE;
  return TRUE;
}

/* *INDENT-OFF* */
static const gchar *sink_caps_str =
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_VA,
        "{ NV12 }") " ;"
    GST_VIDEO_CAPS_MAKE ("{ NV12 }");
/* *INDENT-ON* */

static const gchar *src_caps_str = "image/jpeg";

static gpointer
_register_debug_category (gpointer data)
{
  GST_DEBUG_CATEGORY_INIT (gst_va_jpegenc_debug, "vajpegenc", 0,
      "VA jpeg encoder");

  return NULL;
}

static void
gst_va_jpeg_enc_init (GTypeInstance * instance, gpointer g_class)
{
  GstVaJpegEnc *self = GST_VA_JPEG_ENC (instance);

  self->quality = 50;
}

static void
gst_va_jpeg_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaJpegEnc *self = GST_VA_JPEG_ENC (object);

  GST_OBJECT_LOCK (self);

  switch (prop_id) {
    case PROP_QUALITY:
      self->quality = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_va_jpeg_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaJpegEnc *const self = GST_VA_JPEG_ENC (object);

  GST_OBJECT_LOCK (self);

  switch (prop_id) {
    case PROP_QUALITY:
      g_value_set_uint (value, self->quality);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_va_jpeg_enc_class_init (gpointer g_klass, gpointer class_data)
{
  GstCaps *src_doc_caps, *sink_doc_caps;
  GstPadTemplate *sink_pad_templ, *src_pad_templ;
  GObjectClass *object_class = G_OBJECT_CLASS (g_klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_klass);
  GstVaBaseEncClass *va_enc_class = GST_VA_BASE_ENC_CLASS (g_klass);
  struct CData *cdata = class_data;
  gchar *long_name;
  const gchar *name, *desc;
  gint n_props = N_PROPERTIES;

  desc = "VA-API based JPEG video encoder";
  name = "VA-API JPEG Encoder";

  if (cdata->description)
    long_name = g_strdup_printf ("%s in %s", name, cdata->description);
  else
    long_name = g_strdup (name);

  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Encoder/Video/Hardware", desc, "He Junyan <junyan.he@intel.com>");

  sink_doc_caps = gst_caps_from_string (sink_caps_str);
  src_doc_caps = gst_caps_from_string (src_caps_str);

  parent_class = g_type_class_peek_parent (g_klass);

  va_enc_class->codec = JPEG;
  va_enc_class->entrypoint = cdata->entrypoint;
  va_enc_class->render_device_path = g_strdup (cdata->render_device_path);

  sink_pad_templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      cdata->sink_caps);
  gst_element_class_add_pad_template (element_class, sink_pad_templ);

  gst_pad_template_set_documentation_caps (sink_pad_templ, sink_doc_caps);
  gst_caps_unref (sink_doc_caps);

  src_pad_templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      cdata->src_caps);
  gst_element_class_add_pad_template (element_class, src_pad_templ);

  gst_pad_template_set_documentation_caps (src_pad_templ, src_doc_caps);
  gst_caps_unref (src_doc_caps);

  object_class->set_property = gst_va_jpeg_enc_set_property;
  object_class->get_property = gst_va_jpeg_enc_get_property;

  va_enc_class->reconfig = GST_DEBUG_FUNCPTR (gst_va_jpeg_enc_reconfig);
  va_enc_class->reset_state = GST_DEBUG_FUNCPTR (gst_va_jpeg_enc_reset_state);
  va_enc_class->reorder_frame =
      GST_DEBUG_FUNCPTR (gst_va_jpeg_enc_reorder_frame);
  va_enc_class->new_frame = GST_DEBUG_FUNCPTR (gst_va_jpeg_enc_new_frame);
  va_enc_class->encode_frame = GST_DEBUG_FUNCPTR (gst_va_jpeg_enc_encode_frame);
  va_enc_class->prepare_output =
      GST_DEBUG_FUNCPTR (gst_va_jpeg_enc_prepare_output);

  g_free (long_name);
  g_free (cdata->description);
  g_free (cdata->render_device_path);
  gst_caps_unref (cdata->src_caps);
  gst_caps_unref (cdata->sink_caps);
  g_free (cdata);

  /**
   * GstVaJpegEnc:quality:
   *
   * Quality factor.
   */
  properties[PROP_QUALITY] = g_param_spec_uint ("quality",
      "Quality factor", "Quality factor for encoding", 0, 100, 50,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  g_object_class_install_properties (object_class, n_props, properties);
}

static gboolean
_is_supported_format (GstVideoFormat gst_format)
{
  guint chroma;

  /* Only support depth == 8 */
  chroma = gst_va_chroma_from_video_format (gst_format);
  if (chroma >= VA_RT_FORMAT_YUV420 && chroma <= VA_RT_FORMAT_YUV400)
    return TRUE;

  /* And the special RGB case */
  if (chroma == VA_RT_FORMAT_RGB32)
    return TRUE;

  return FALSE;
}

static void
_generate_supported_formats (GPtrArray * supported_formats,
    GValue * supported_value)
{
  guint i;

  if (supported_formats->len == 1) {
    g_value_init (supported_value, G_TYPE_STRING);
    g_value_set_string (supported_value,
        g_ptr_array_index (supported_formats, 0));
  } else {
    GValue item = G_VALUE_INIT;

    gst_value_list_init (supported_value, supported_formats->len);

    for (i = 0; i < supported_formats->len; i++) {
      g_value_init (&item, G_TYPE_STRING);
      g_value_set_string (&item, g_ptr_array_index (supported_formats, i));
      gst_value_list_append_value (supported_value, &item);
      g_value_unset (&item);
    }
  }
}

static GstCaps *
_filter_sink_caps (GstCaps * sinkcaps)
{
  GPtrArray *supported_formats;
  const gchar *format_str;
  GstVideoFormat gst_format;
  const GValue *val;
  GValue supported_value = G_VALUE_INIT;
  GstCaps *ret;
  guint num_structures;
  guint i, j;

  supported_formats = g_ptr_array_new ();
  ret = gst_caps_new_empty ();

  num_structures = gst_caps_get_size (sinkcaps);

  for (i = 0; i < num_structures; i++) {
    GstStructure *st;
    GstCapsFeatures *features;

    g_ptr_array_set_size (supported_formats, 0);

    st = gst_caps_get_structure (sinkcaps, i);
    st = gst_structure_copy (st);

    features = gst_caps_get_features (sinkcaps, i);

    if (gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_DMABUF)) {
      guint32 fourcc;

      val = gst_structure_get_value (st, "drm-format");
      if (!val) {
        gst_structure_free (st);
        continue;
      }

      if (G_VALUE_HOLDS_STRING (val)) {
        format_str = g_value_get_string (val);
        fourcc = gst_video_dma_drm_fourcc_from_string (format_str, NULL);
        gst_format = gst_va_video_format_from_drm_fourcc (fourcc);

        if (_is_supported_format (gst_format))
          g_ptr_array_add (supported_formats, (gpointer) format_str);
      } else if (GST_VALUE_HOLDS_LIST (val)) {
        guint num_values = gst_value_list_get_size (val);

        for (j = 0; j < num_values; j++) {
          const GValue *v = gst_value_list_get_value (val, j);

          format_str = g_value_get_string (v);
          fourcc = gst_video_dma_drm_fourcc_from_string (format_str, NULL);
          gst_format = gst_va_video_format_from_drm_fourcc (fourcc);

          if (_is_supported_format (gst_format))
            g_ptr_array_add (supported_formats, (gpointer) format_str);
        }
      }

      if (!supported_formats->len) {
        gst_structure_free (st);
        continue;
      }

      _generate_supported_formats (supported_formats, &supported_value);
      gst_structure_take_value (st, "drm-format", &supported_value);

      gst_caps_append_structure_full (ret, st,
          gst_caps_features_copy (features));
    } else {
      val = gst_structure_get_value (st, "format");
      if (!val) {
        gst_structure_free (st);
        continue;
      }

      if (G_VALUE_HOLDS_STRING (val)) {
        format_str = g_value_get_string (val);
        gst_format = gst_video_format_from_string (format_str);

        if (_is_supported_format (gst_format))
          g_ptr_array_add (supported_formats, (gpointer) format_str);
      } else if (GST_VALUE_HOLDS_LIST (val)) {
        guint num_values = gst_value_list_get_size (val);

        for (j = 0; j < num_values; j++) {
          const GValue *v = gst_value_list_get_value (val, j);

          format_str = g_value_get_string (v);
          gst_format = gst_video_format_from_string (format_str);

          if (_is_supported_format (gst_format))
            g_ptr_array_add (supported_formats, (gpointer) format_str);
        }
      }

      if (!supported_formats->len) {
        gst_structure_free (st);
        continue;
      }

      _generate_supported_formats (supported_formats, &supported_value);
      gst_structure_take_value (st, "format", &supported_value);

      gst_caps_append_structure_full (ret, st,
          gst_caps_features_copy (features));
    }
  }

  g_ptr_array_unref (supported_formats);

  if (gst_caps_is_empty (ret)) {
    gst_caps_unref (ret);
    ret = NULL;
  }

  return ret;
}

gboolean
gst_va_jpeg_enc_register (GstPlugin * plugin, GstVaDevice * device,
    GstCaps * sink_caps, GstCaps * src_caps, guint rank,
    VAEntrypoint entrypoint)
{
  static GOnce debug_once = G_ONCE_INIT;
  GType type;
  GTypeInfo type_info = {
    .class_size = sizeof (GstVaJpegEncClass),
    .class_init = gst_va_jpeg_enc_class_init,
    .instance_size = sizeof (GstVaJpegEnc),
    .instance_init = gst_va_jpeg_enc_init,
  };
  struct CData *cdata;
  gboolean ret;
  gchar *type_name, *feature_name;

  g_return_val_if_fail (GST_IS_PLUGIN (plugin), FALSE);
  g_return_val_if_fail (GST_IS_VA_DEVICE (device), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (sink_caps), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (src_caps), FALSE);
  g_return_val_if_fail (entrypoint == VAEntrypointEncPicture, FALSE);

  sink_caps = _filter_sink_caps (sink_caps);

  cdata = g_new (struct CData, 1);
  cdata->entrypoint = entrypoint;
  cdata->description = NULL;
  cdata->render_device_path = g_strdup (device->render_device_path);
  cdata->sink_caps = sink_caps;
  cdata->src_caps = gst_caps_ref (src_caps);

  /* class data will be leaked if the element never gets instantiated */
  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (cdata->src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  type_info.class_data = cdata;
  gst_va_create_feature_name (device, "GstVaJpegEnc", "GstVa%sJpegEnc",
      &type_name, "vajpegenc", "va%sjpegenc", &feature_name,
      &cdata->description, &rank);

  g_once (&debug_once, _register_debug_category, NULL);
  type = g_type_register_static (GST_TYPE_VA_BASE_ENC,
      type_name, &type_info, 0);
  ret = gst_element_register (plugin, feature_name, rank, type);

  g_free (type_name);
  g_free (feature_name);

  return ret;
}
