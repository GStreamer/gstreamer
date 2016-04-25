/* GStreamer
 *
 * FFMpeg Configuration
 *
 * Copyright (C) <2006> Mark Nauwelaerts <manauw@skynet.be>
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

#include "gstav.h"
#include "gstavvidenc.h"
#include "gstavcfg.h"

#include <string.h>

/* some enums used in property declarations */

#define GST_TYPE_FFMPEG_PASS (gst_ffmpeg_pass_get_type ())
static GType
gst_ffmpeg_pass_get_type (void)
{
  static GType ffmpeg_pass_type = 0;

  if (!ffmpeg_pass_type) {
    static const GEnumValue ffmpeg_passes[] = {
      {0, "Constant Bitrate Encoding", "cbr"},
      {CODEC_FLAG_QSCALE, "Constant Quantizer", "quant"},
      {CODEC_FLAG_PASS1, "VBR Encoding - Pass 1", "pass1"},
      {CODEC_FLAG_PASS2, "VBR Encoding - Pass 2", "pass2"},
      {0, NULL, NULL},
    };

    ffmpeg_pass_type =
        g_enum_register_static ("GstLibAVEncPass", ffmpeg_passes);
  }

  return ffmpeg_pass_type;
}

#if 0
/* some do not support 2-pass */
#define GST_TYPE_FFMPEG_LIM_PASS (gst_ffmpeg_lim_pass_get_type ())
static GType
gst_ffmpeg_lim_pass_get_type (void)
{
  static GType ffmpeg_lim_pass_type = 0;

  if (!ffmpeg_lim_pass_type) {
    static const GEnumValue ffmpeg_lim_passes[] = {
      {0, "Constant Bitrate Encoding", "cbr"},
      {CODEC_FLAG_QSCALE, "Constant Quantizer", "quant"},
      {0, NULL, NULL},
    };

    ffmpeg_lim_pass_type =
        g_enum_register_static ("GstLibAVEncLimPass", ffmpeg_lim_passes);
  }

  return ffmpeg_lim_pass_type;
}
#endif

#define GST_TYPE_FFMPEG_MB_DECISION (gst_ffmpeg_mb_decision_get_type ())
static GType
gst_ffmpeg_mb_decision_get_type (void)
{
  static GType ffmpeg_mb_decision_type = 0;

  if (!ffmpeg_mb_decision_type) {
    static const GEnumValue ffmpeg_mb_decisions[] = {
      {FF_MB_DECISION_SIMPLE, "Use method set by mb-cmp", "simple"},
      {FF_MB_DECISION_BITS,
          "Chooses the one which needs the fewest bits aka vhq mode", "bits"},
      {FF_MB_DECISION_RD, "Rate Distortion", "rd"},
      {0, NULL, NULL},
    };

    ffmpeg_mb_decision_type =
        g_enum_register_static ("GstLibAVEncMBDecision", ffmpeg_mb_decisions);
  }

  return ffmpeg_mb_decision_type;
}

#define GST_TYPE_FFMPEG_CMP_FUNCTION (gst_ffmpeg_mb_cmp_get_type ())
static GType
gst_ffmpeg_mb_cmp_get_type (void)
{
  static GType ffmpeg_mb_cmp_type = 0;

  /* TODO fill out remaining values */
  if (!ffmpeg_mb_cmp_type) {
    static const GEnumValue ffmpeg_mb_cmps[] = {
      {FF_CMP_SAD, "Sum of Absolute Differences", "sad"},
      {FF_CMP_SSE, "Sum of Squared Errors", "sse"},
      {FF_CMP_SATD, "Sum of Absolute Hadamard Transformed Differences", "satd"},
      {FF_CMP_DCT, "Sum of Absolute DCT Transformed Differences", "dct"},
      {FF_CMP_PSNR, "Sum of the Squared Quantization Errors", "psnr"},
      {FF_CMP_BIT, "Sum of the Bits needed for the block", "bit"},
      {FF_CMP_RD, "Rate Distortion optimal", "rd"},
      {FF_CMP_ZERO, "ZERO", "zero"},
      {FF_CMP_VSAD, "VSAD", "vsad"},
      {FF_CMP_VSSE, "VSSE", "vsse"},
#if 0
/* economize a bit for now */
      {FF_CMP_NSSE, "NSSE", "nsse"},
      {FF_CMP_W53, "W53", "w53"},
      {FF_CMP_W97, "W97", "w97"},
#endif
      {0, NULL, NULL},
    };

    ffmpeg_mb_cmp_type =
        g_enum_register_static ("GstLibAVCMPFunction", ffmpeg_mb_cmps);
  }

  return ffmpeg_mb_cmp_type;
}

#define GST_TYPE_FFMPEG_DCT_ALGO (gst_ffmpeg_dct_algo_get_type ())
static GType
gst_ffmpeg_dct_algo_get_type (void)
{
  static GType ffmpeg_dct_algo_type = 0;

  if (!ffmpeg_dct_algo_type) {
    static const GEnumValue ffmpeg_dct_algos[] = {
      {FF_DCT_AUTO, "Automatically select a good one", "auto"},
      {FF_DCT_FASTINT, "Fast Integer", "fastint"},
      {FF_DCT_INT, "Accurate Integer", "int"},
      {FF_DCT_MMX, "MMX", "mmx"},
      {FF_DCT_ALTIVEC, "ALTIVEC", "altivec"},
      {FF_DCT_FAAN, "FAAN", "faan"},
      {0, NULL, NULL},
    };

    ffmpeg_dct_algo_type =
        g_enum_register_static ("GstLibAVDCTAlgo", ffmpeg_dct_algos);
  }

  return ffmpeg_dct_algo_type;
}

#define GST_TYPE_FFMPEG_IDCT_ALGO (gst_ffmpeg_idct_algo_get_type ())
static GType
gst_ffmpeg_idct_algo_get_type (void)
{
  static GType ffmpeg_idct_algo_type = 0;

  if (!ffmpeg_idct_algo_type) {
    static const GEnumValue ffmpeg_idct_algos[] = {
      {FF_IDCT_AUTO, "Automatically select a good one", "auto"},
      {FF_IDCT_INT, "JPEG reference Integer", "int"},
      {FF_IDCT_SIMPLE, "Simple", "simple"},
      {FF_IDCT_SIMPLEMMX, "Simple MMX", "simplemmx"},
      {FF_IDCT_ARM, "ARM", "arm"},
      {FF_IDCT_ALTIVEC, "Altivec", "altivec"},
      {FF_IDCT_SIMPLEARM, "Simple ARM", "simplearm"},
      {FF_IDCT_XVID, "XVID", "xvid"},
      {FF_IDCT_SIMPLEARMV5TE, "Simple ARMV5TE", "simplearmv5te"},
      {FF_IDCT_SIMPLEARMV6, "Simple ARMV6", "simplearmv6"},
      {FF_IDCT_FAAN, "FAAN", "faan"},
      {FF_IDCT_SIMPLENEON, "Simple NEON", "simpleneon"},
      {0, NULL, NULL},
    };

    ffmpeg_idct_algo_type =
        g_enum_register_static ("GstLibAVIDCTAlgo", ffmpeg_idct_algos);
  }

  return ffmpeg_idct_algo_type;
}

#define GST_TYPE_FFMPEG_QUANT_TYPE (gst_ffmpeg_quant_type_get_type ())
static GType
gst_ffmpeg_quant_type_get_type (void)
{
  static GType ffmpeg_quant_type_type = 0;

  if (!ffmpeg_quant_type_type) {
    static const GEnumValue ffmpeg_quant_types[] = {
      {0, "H263 quantization", "h263"},
      {1, "MPEG quantization", "mpeg"},
      {0, NULL, NULL},
    };

    ffmpeg_quant_type_type =
        g_enum_register_static ("GstLibAVEncQuantTypes", ffmpeg_quant_types);
  }

  return ffmpeg_quant_type_type;
}

#define GST_TYPE_FFMPEG_PRE_ME (gst_ffmpeg_pre_me_get_type ())
static GType
gst_ffmpeg_pre_me_get_type (void)
{
  static GType ffmpeg_pre_me_type = 0;

  if (!ffmpeg_pre_me_type) {
    static const GEnumValue ffmpeg_pre_mes[] = {
      {0, "Disabled", "off"},
      {1, "Only after I-frames", "key"},
      {2, "Always", "all"},
      {0, NULL, NULL}
    };

    ffmpeg_pre_me_type =
        g_enum_register_static ("GstLibAVEncPreME", ffmpeg_pre_mes);
  }

  return ffmpeg_pre_me_type;
}

#define GST_TYPE_FFMPEG_PRED_METHOD (gst_ffmpeg_pred_method_get_type ())
static GType
gst_ffmpeg_pred_method_get_type (void)
{
  static GType ffmpeg_pred_method = 0;

  if (!ffmpeg_pred_method) {
    static const GEnumValue ffmpeg_pred_methods[] = {
      {FF_PRED_LEFT, "Left", "left"},
      {FF_PRED_PLANE, "Plane", "plane"},
      {FF_PRED_MEDIAN, "Median", "median"},
      {0, NULL, NULL}
    };

    ffmpeg_pred_method =
        g_enum_register_static ("GstLibAVEncPredMethod", ffmpeg_pred_methods);
  }

  return ffmpeg_pred_method;
}

#define GST_TYPE_FFMPEG_FLAGS (gst_ffmpeg_flags_get_type())
static GType
gst_ffmpeg_flags_get_type (void)
{
  static GType ffmpeg_flags_type = 0;

  /* FIXME: This needs some serious resyncing with avcodec.h */
  if (!ffmpeg_flags_type) {
    static const GFlagsValue ffmpeg_flags[] = {
      {CODEC_FLAG_QSCALE, "Use fixed qscale", "qscale"},
      {CODEC_FLAG_4MV, "Allow 4 MV per MB", "4mv"},
      {CODEC_FLAG_QPEL, "Quartel Pel Motion Compensation", "qpel"},
      {CODEC_FLAG_GMC, "GMC", "gmc"},
      {CODEC_FLAG_MV0, "Always try a MB with MV (0,0)", "mv0"},
      {CODEC_FLAG_LOOP_FILTER, "Loop filter", "loop-filter"},
      {CODEC_FLAG_GRAY, "Only decode/encode grayscale", "gray"},
      {CODEC_FLAG_NORMALIZE_AQP,
          "Normalize Adaptive Quantization (masking, etc)", "aqp"},
      {CODEC_FLAG_GLOBAL_HEADER,
            "Global headers in extradata instead of every keyframe",
          "global-headers"},
      {CODEC_FLAG_AC_PRED, "H263 Advanced Intra Coding / MPEG4 AC prediction",
          "aic"},
      {CODEC_FLAG_CLOSED_GOP, "Closed GOP", "closedgop"},
      {0, NULL, NULL},
    };

    ffmpeg_flags_type = g_flags_register_static ("GstLibAVFlags", ffmpeg_flags);
  }

  return ffmpeg_flags_type;
}

/* provides additional info to attach to a property */

typedef struct _GParamSpecData GParamSpecData;

struct _GParamSpecData
{
  /* offset of member in the element struct that stores the property */
  guint offset;

  /* size of the above member */
  guint size;

  /* if TRUE, try to get the default from lavc and ignore the paramspec default */
  gboolean lavc_default;

  /* these lists are arrays terminated by AV_CODEC_ID_NONE entry:
   * property applies to a codec if it's not in the exclude_list
   * and in exclude_list (or the latter is NULL) */
  gint *include_list;
  gint *exclude_list;
};

/* properties whose member offset is higher than the config base
 * can be copied directly at context configuration time;
 * and can also retrieve a default value from lavc */
#define CONTEXT_CONFIG_OFFSET   G_STRUCT_OFFSET (GstFFMpegVidEnc, config)

/* additional info is named pointer specified by the quark */
static GQuark quark;

/* central configuration store:
 * list of GParamSpec's with GParamSpecData attached as named pointer */
static GList *property_list;

/* add the GParamSpec pspec to store with GParamSpecData
 * constructed from struct_type, member, default and include and exclude */
#define gst_ffmpeg_add_pspec_full(pspec, store, struct_type, member,    \
    default, include, exclude)                                          \
G_STMT_START {                                                          \
  GParamSpecData *_qdata = g_new0 (GParamSpecData, 1);                  \
  GstFFMpegVidEnc _enc;                                                    \
  _qdata->offset = G_STRUCT_OFFSET (struct_type, member);               \
  _qdata->size = sizeof (_enc.member);                                  \
  _qdata->lavc_default = default;                                       \
  _qdata->include_list = include;                                       \
  _qdata->exclude_list = exclude;                                       \
  g_param_spec_set_qdata_full (pspec, quark, _qdata, g_free);           \
  store = g_list_append (store, pspec);                                 \
} G_STMT_END

#define gst_ffmpeg_add_pspec(pspec, member, default, include, exclude)       \
  gst_ffmpeg_add_pspec_full (pspec, property_list, GstFFMpegVidEnc, member,     \
      default, include, exclude)

/* ==== BEGIN CONFIGURATION SECTION ==== */

/* some typical include and exclude lists; modify and/or add where needed */

static gint mpeg[] = {
  AV_CODEC_ID_MPEG4,
  AV_CODEC_ID_MSMPEG4V1,
  AV_CODEC_ID_MSMPEG4V2,
  AV_CODEC_ID_MSMPEG4V3,
  AV_CODEC_ID_MPEG1VIDEO,
  AV_CODEC_ID_MPEG2VIDEO,
  AV_CODEC_ID_H263P,
  AV_CODEC_ID_FLV1,
  AV_CODEC_ID_H263,
  AV_CODEC_ID_NONE
};

static gint huffyuv[] = {
  AV_CODEC_ID_HUFFYUV,
  AV_CODEC_ID_FFVHUFF,
  AV_CODEC_ID_NONE
};

/* Properties should be added here for registration into the config store.
 * Note that some may occur more than once, with different include/exclude lists,
 * as some may require different defaults for different codecs,
 * or some may have slightly varying enum-types with more or less options.
 * The enum-types themselves should be declared above. */
void
gst_ffmpeg_cfg_init (void)
{
  GParamSpec *pspec;

  /* initialize global config vars */
  quark = g_quark_from_static_string ("ffmpeg-cfg-param-spec-data");
  property_list = NULL;

  /* list properties here */
  pspec = g_param_spec_enum ("pass", "Encoding pass/type",
      "Encoding pass/type", GST_TYPE_FFMPEG_PASS, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, pass, FALSE, mpeg, NULL);

  pspec = g_param_spec_float ("quantizer", "Constant Quantizer",
      "Constant Quantizer", 0, 30, 0.01f,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, quantizer, FALSE, mpeg, NULL);

  pspec = g_param_spec_string ("multipass-cache-file", "Multipass Cache File",
      "Filename for multipass cache file", "stats.log",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, filename, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("bitrate-tolerance", "Bitrate Tolerance",
      "Number of bits the bitstream is allowed to diverge from the reference",
      0, 100000000, 8000000, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.bit_rate_tolerance, FALSE, mpeg, NULL);

  pspec = g_param_spec_enum ("mb-decision", "Macroblock Decision",
      "Macroblok Decision Mode",
      GST_TYPE_FFMPEG_MB_DECISION, FF_CMP_SAD,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.mb_decision, FALSE, mpeg, NULL);

  pspec = g_param_spec_enum ("mb-cmp", "Macroblock Compare Function",
      "Macroblok Compare Function",
      GST_TYPE_FFMPEG_CMP_FUNCTION, FF_CMP_SAD,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.mb_cmp, FALSE, mpeg, NULL);

  pspec =
      g_param_spec_enum ("me-pre-cmp",
      "Motion Estimation Pre Pass Compare Function",
      "Motion Estimation Pre Pass Compare Function",
      GST_TYPE_FFMPEG_CMP_FUNCTION, FF_CMP_SAD,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.me_pre_cmp, FALSE, mpeg, NULL);

  pspec = g_param_spec_enum ("me-cmp", "Motion Estimation Compare Function",
      "Motion Estimation Compare Function",
      GST_TYPE_FFMPEG_CMP_FUNCTION, FF_CMP_SAD,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.me_cmp, FALSE, mpeg, NULL);

  pspec = g_param_spec_enum ("me-sub-cmp",
      "Subpixel Motion Estimation Compare Function",
      "Subpixel Motion Estimation Compare Function",
      GST_TYPE_FFMPEG_CMP_FUNCTION, FF_CMP_SAD,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.me_sub_cmp, FALSE, mpeg, NULL);

  pspec = g_param_spec_enum ("ildct-cmp", "Interlaced DCT Compare Function",
      "Interlaced DCT Compare Function",
      GST_TYPE_FFMPEG_CMP_FUNCTION, FF_CMP_VSAD,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.ildct_cmp, FALSE, mpeg, NULL);

  pspec = g_param_spec_enum ("dct-algo", "DCT Algorithm",
      "DCT Algorithm",
      GST_TYPE_FFMPEG_DCT_ALGO, FF_DCT_AUTO,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.dct_algo, FALSE, mpeg, NULL);

  pspec = g_param_spec_enum ("idct-algo", "IDCT Algorithm",
      "IDCT Algorithm",
      GST_TYPE_FFMPEG_IDCT_ALGO, FF_IDCT_AUTO,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.idct_algo, FALSE, mpeg, NULL);

  pspec = g_param_spec_enum ("quant-type", "Quantizer Type",
      "Quantizer Type", GST_TYPE_FFMPEG_QUANT_TYPE, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.mpeg_quant, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("qmin", "Minimum Quantizer",
      "Minimum Quantizer", 1, 31, 2,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.qmin, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("qmax", "Maximum Quantizer",
      "Maximum Quantizer", 1, 31, 31,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.qmax, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("max-qdiff", "Maximum Quantizer Difference",
      "Maximum Quantizer Difference between frames",
      1, 31, 3, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.max_qdiff, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("lmin", "Minimum Lagrange Multiplier",
      "Minimum Lagrange Multiplier", 1, 31, 2,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, lmin, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("lmax", "Maximum Lagrange Multiplier",
      "Maximum Lagrange Multiplier", 1, 31, 31,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, lmax, FALSE, mpeg, NULL);

  pspec = g_param_spec_float ("qcompress", "Quantizer Change",
      "Quantizer Change between easy and hard scenes",
      0, 1.0f, 0.5f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.qcompress, FALSE, mpeg, NULL);

  pspec = g_param_spec_float ("qblur", "Quantizer Smoothing",
      "Quantizer Smoothing over time", 0, 1.0f, 0.5f,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.qblur, FALSE, mpeg, NULL);

  pspec = g_param_spec_float ("rc-qsquish", "Ratecontrol Limiting Method",
      "0 means limit by clipping, otherwise use nice continuous function",
      0, 99.0f, 1.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.rc_qsquish, FALSE, mpeg, NULL);

  pspec = g_param_spec_float ("rc-qmod-amp", "Ratecontrol Mod",
      "Ratecontrol Mod", 0, 99.0f, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.rc_qmod_amp, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("rc-qmod-freq", "Ratecontrol Freq",
      "Ratecontrol Freq", 0, 0, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.rc_qmod_freq, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("rc-buffer-size", "Ratecontrol Buffer Size",
      "Decoder bitstream buffer size", 0, G_MAXINT, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.rc_buffer_size, FALSE, mpeg, NULL);

  pspec =
      g_param_spec_float ("rc-buffer-aggressivity",
      "Ratecontrol Buffer Aggressivity", "Ratecontrol Buffer Aggressivity", 0,
      99.0f, 1.0f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.rc_buffer_aggressivity, FALSE, mpeg,
      NULL);

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT (57, 3, 0)
  pspec = g_param_spec_int ("rc-max-rate", "Ratecontrol Maximum Bitrate",
      "Ratecontrol Maximum Bitrate", 0, G_MAXINT, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
#else
  pspec = g_param_spec_int64 ("rc-max-rate", "Ratecontrol Maximum Bitrate",
      "Ratecontrol Maximum Bitrate", 0, G_MAXINT64, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
#endif
  gst_ffmpeg_add_pspec (pspec, config.rc_max_rate, FALSE, mpeg, NULL);

  pspec = g_param_spec_int64 ("rc-min-rate", "Ratecontrol Minimum Bitrate",
      "Ratecontrol Minimum Bitrate", 0, G_MAXINT64, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.rc_min_rate, FALSE, mpeg, NULL);

  pspec =
      g_param_spec_float ("rc-initial-cplx",
      "Initial Complexity for Pass 1 Ratecontrol",
      "Initial Complexity for Pass 1 Ratecontrol", 0, 9999999.0f, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.rc_initial_cplx, FALSE, mpeg, NULL);

  pspec = g_param_spec_string ("rc-eq", "Ratecontrol Equation",
      "Ratecontrol Equation", "tex^qComp",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.rc_eq, FALSE, mpeg, NULL);

  pspec = g_param_spec_float ("b-quant-factor", "B-Quantizer Factor",
      "Factor in B-Frame Quantizer Computation",
      -31.0f, 31.0f, 1.25f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.b_quant_factor, FALSE, mpeg, NULL);

  pspec = g_param_spec_float ("b-quant-offset", "B-Quantizer Offset",
      "Offset in B-Frame Quantizer Computation",
      0.0f, 31.0f, 1.25f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.b_quant_offset, FALSE, mpeg, NULL);

  pspec = g_param_spec_float ("i-quant-factor", "I-Quantizer Factor",
      "Factor in P-Frame Quantizer Computation",
      -31.0f, 31.0f, 0.8f, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.i_quant_factor, FALSE, mpeg, NULL);

  pspec = g_param_spec_float ("i-quant-offset", "I-Quantizer Offset",
      "Offset in P-Frame Quantizer Computation",
      0.0f, 31.0f, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.i_quant_offset, FALSE, mpeg, NULL);

  /* note overlap with gop-size; 0 means do not override */
  pspec = g_param_spec_int ("max-key-interval", "Maximum Key Interval",
      "Maximum number of frames between two keyframes (< 0 is in sec)",
      -100, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, max_key_interval, FALSE, mpeg, NULL);

  pspec = g_param_spec_float ("lumi-masking", "Luminance Masking",
      "Luminance Masking", -1.0f, 1.0f, 0.0f,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.lumi_masking, FALSE, mpeg, NULL);

  pspec = g_param_spec_float ("dark-masking", "Darkness Masking",
      "Darkness Masking", -1.0f, 1.0f, 0.0f,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.dark_masking, FALSE, mpeg, NULL);

  pspec = g_param_spec_float ("temporal-cplx-masking",
      "Temporal Complexity Masking",
      "Temporal Complexity Masking", -1.0f, 1.0f, 0.0f,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.temporal_cplx_masking, FALSE, mpeg, NULL);

  pspec = g_param_spec_float ("spatial-cplx-masking",
      "Spatial Complexity Masking",
      "Spatial Complexity Masking", -1.0f, 1.0f, 0.0f,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.spatial_cplx_masking, FALSE, mpeg, NULL);

  pspec = g_param_spec_float ("p-masking", "P Block Masking",
      "P Block  Masking", -1.0f, 1.0f, 0.0f,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.p_masking, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("dia-size",
      "Motion Estimation Diamond Size/Shape",
      "Motion Estimation Diamond Size/Shape",
      -2000, 2000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.dia_size, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("pre-dia-size",
      "Motion Estimation Pre Pass Diamond Size/Shape",
      "Motion Estimation Diamond Size/Shape",
      -2000, 2000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.pre_dia_size, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("last-predictor-count",
      "Last Predictor Count",
      "Amount of previous Motion Vector predictors",
      0, 2000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.last_predictor_count, FALSE, mpeg, NULL);

  pspec = g_param_spec_enum ("pre-me",
      "Pre Pass for Motion Estimation",
      "Pre Pass for Motion Estimation",
      GST_TYPE_FFMPEG_PRE_ME, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.pre_me, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("me-subpel-quality",
      "Motion Estimation Subpixel Quality",
      "Motion Estimation Subpixel Refinement Quality",
      0, 8, 8, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.me_subpel_quality, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("me-range",
      "Motion Estimation Range",
      "Motion Estimation search range in subpel units",
      0, 16000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.me_range, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("intra-quant-bias",
      "Intra Quantizer Bias",
      "Intra Quantizer Bias",
      -1000000, 1000000, FF_DEFAULT_QUANT_BIAS,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.intra_quant_bias, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("inter-quant-bias",
      "Inter Quantizer Bias",
      "Inter Quantizer Bias",
      -1000000, 1000000, FF_DEFAULT_QUANT_BIAS,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.inter_quant_bias, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("noise-reduction",
      "Noise Reduction",
      "Noise Reduction Strength", 0, 1000000, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.noise_reduction, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("intra-dc-precision",
      "Intra DC precision",
      "Precision of the Intra DC coefficient - 8", 0, 16, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.intra_dc_precision, FALSE, mpeg, NULL);

  /* TODO skipped coder_type, context_model, inter_threshold, scenechange_threshold */

  pspec = g_param_spec_flags ("flags", "Flags",
      "Flags", GST_TYPE_FFMPEG_FLAGS, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.flags, FALSE, mpeg, NULL);

  pspec = g_param_spec_boolean ("interlaced", "Interlaced Material",
      "Interlaced Material", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, interlaced, FALSE, mpeg, NULL);

  pspec = g_param_spec_int ("max-bframes", "Max B-Frames",
      "Maximum B-frames in a row", 0, INT_MAX, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.max_b_frames, FALSE, mpeg, NULL);

  pspec = g_param_spec_enum ("prediction-method", "Prediction Method",
      "Prediction Method",
      GST_TYPE_FFMPEG_PRED_METHOD, FF_PRED_LEFT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.prediction_method, FALSE, huffyuv, NULL);
  pspec = g_param_spec_int ("trellis", "Trellis Quantization",
      "Trellis RD quantization", 0, 1, 1,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_ffmpeg_add_pspec (pspec, config.trellis, FALSE, mpeg, NULL);
}

/* ==== END CONFIGURATION SECTION ==== */


/* return TRUE if property described by pspec applies to the codec with codec_id */
static gboolean
gst_ffmpeg_cfg_codec_has_pspec (enum AVCodecID codec_id, GParamSpec * pspec)
{
  GParamSpecData *qdata;
  gint *codec;
  gboolean ret = FALSE;

  qdata = g_param_spec_get_qdata (pspec, quark);

  /* check if excluded first */
  if ((codec = qdata->exclude_list)) {
    for (; *codec != AV_CODEC_ID_NONE; ++codec) {
      if (*codec == codec_id)
        return FALSE;
    }
  }

  /* no include list means it is accepted */
  if ((codec = qdata->include_list)) {
    for (; *codec != AV_CODEC_ID_NONE; ++codec) {
      if (*codec == codec_id)
        ret = TRUE;
    }
  } else {
    ret = TRUE;
  }

  return ret;
}

/* install all properties for klass that have been registered in property_list */
void
gst_ffmpeg_cfg_install_property (GstFFMpegVidEncClass * klass, guint base)
{
  GParamSpec *pspec;
  GList *list;
  gint prop_id;
  AVCodecContext *ctx;

  prop_id = base;
  g_return_if_fail (base > 0);

  ctx = avcodec_alloc_context3 (klass->in_plugin);
  if (!ctx)
    g_warning ("could not get context");

  for (list = property_list; list; list = list->next) {
    pspec = G_PARAM_SPEC (list->data);
    if (gst_ffmpeg_cfg_codec_has_pspec (klass->in_plugin->id, pspec)) {
      /* 'clone' the paramspec for the various codecs,
       * since a single paramspec cannot be owned by distinct types */

      const gchar *name = g_param_spec_get_name (pspec);
      const gchar *nick = g_param_spec_get_nick (pspec);
      const gchar *blurb = g_param_spec_get_blurb (pspec);
      GParamSpecData *qdata = g_param_spec_get_qdata (pspec, quark);
      gint ctx_offset = 0;
      gboolean lavc_default;

      /* cannot obtain lavc default if no context */
      if (!ctx)
        lavc_default = FALSE;
      else {
        ctx_offset = qdata->offset - CONTEXT_CONFIG_OFFSET;
        /* safety check; is it really member of the avcodec context */
        if (ctx_offset < 0)
          lavc_default = FALSE;
        else
          lavc_default = qdata->lavc_default;
      }

      switch (G_PARAM_SPEC_VALUE_TYPE (pspec)) {
        case G_TYPE_STRING:{
          GParamSpecString *pstring = G_PARAM_SPEC_STRING (pspec);

          pspec = g_param_spec_string (name, nick, blurb,
              lavc_default ? G_STRUCT_MEMBER (gchar *, ctx, ctx_offset)
              : pstring->default_value, pspec->flags);
          break;
        }
        case G_TYPE_INT:{
          GParamSpecInt *pint = G_PARAM_SPEC_INT (pspec);

          pspec = g_param_spec_int (name, nick, blurb,
              pint->minimum, pint->maximum,
              lavc_default ? G_STRUCT_MEMBER (gint, ctx, ctx_offset)
              : pint->default_value, pspec->flags);
          break;
        }
        case G_TYPE_INT64:{
          GParamSpecInt64 *pint = G_PARAM_SPEC_INT64 (pspec);

          pspec = g_param_spec_int64 (name, nick, blurb,
              pint->minimum, pint->maximum,
              lavc_default ? G_STRUCT_MEMBER (gint64, ctx, ctx_offset)
              : pint->default_value, pspec->flags);
          break;
        }
        case G_TYPE_UINT:{
          GParamSpecUInt *puint = G_PARAM_SPEC_UINT (pspec);

          pspec = g_param_spec_uint (name, nick, blurb,
              puint->minimum, puint->maximum,
              lavc_default ? G_STRUCT_MEMBER (guint, ctx, ctx_offset)
              : puint->default_value, pspec->flags);
          break;
        }
        case G_TYPE_FLOAT:{
          GParamSpecFloat *pfloat = G_PARAM_SPEC_FLOAT (pspec);

          pspec = g_param_spec_float (name, nick, blurb,
              pfloat->minimum, pfloat->maximum,
              lavc_default ? G_STRUCT_MEMBER (gfloat, ctx, ctx_offset)
              : pfloat->default_value, pspec->flags);
          break;
        }
        case G_TYPE_BOOLEAN:{
          GParamSpecBoolean *pboolean = G_PARAM_SPEC_BOOLEAN (pspec);

          pspec = g_param_spec_boolean (name, nick, blurb,
              lavc_default ? G_STRUCT_MEMBER (gboolean, ctx, ctx_offset)
              : pboolean->default_value, pspec->flags);
          break;
        }
        default:
          if (G_IS_PARAM_SPEC_ENUM (pspec)) {
            GParamSpecEnum *penum = G_PARAM_SPEC_ENUM (pspec);

            pspec = g_param_spec_enum (name, nick, blurb,
                pspec->value_type,
                lavc_default ? G_STRUCT_MEMBER (gint, ctx, ctx_offset)
                : penum->default_value, pspec->flags);
          } else if (G_IS_PARAM_SPEC_FLAGS (pspec)) {
            GParamSpecFlags *pflags = G_PARAM_SPEC_FLAGS (pspec);

            pspec = g_param_spec_flags (name, nick, blurb,
                pspec->value_type,
                lavc_default ? G_STRUCT_MEMBER (guint, ctx, ctx_offset)
                : pflags->default_value, pspec->flags);
          } else {
            g_critical ("%s does not yet support type %s", GST_FUNCTION,
                g_type_name (G_PARAM_SPEC_VALUE_TYPE (pspec)));
            continue;
          }
          break;
      }
      g_param_spec_set_qdata (pspec, quark, qdata);
      g_object_class_install_property (G_OBJECT_CLASS (klass), prop_id, pspec);
      ++prop_id;
    }
  }

  if (ctx) {
    gst_ffmpeg_avcodec_close (ctx);
    av_free (ctx);
  }
}

/* returns TRUE if it is a known property for this config system,
 * FALSE otherwise */
gboolean
gst_ffmpeg_cfg_set_property (GObject * object,
    const GValue * value, GParamSpec * pspec)
{
  GstFFMpegVidEnc *ffmpegenc = (GstFFMpegVidEnc *) (object);
  GParamSpecData *qdata;

  qdata = g_param_spec_get_qdata (pspec, quark);

  /* our param specs should have such qdata */
  if (!qdata)
    return FALSE;

  /* set the member using the offset, also mild type check based on size */
  switch (G_PARAM_SPEC_VALUE_TYPE (pspec)) {
    case G_TYPE_BOOLEAN:
      g_return_val_if_fail (qdata->size == sizeof (gboolean), TRUE);
      G_STRUCT_MEMBER (gboolean, ffmpegenc, qdata->offset) =
          g_value_get_boolean (value);
      break;
    case G_TYPE_UINT:
      g_return_val_if_fail (qdata->size == sizeof (guint), TRUE);
      G_STRUCT_MEMBER (guint, ffmpegenc, qdata->offset) =
          g_value_get_uint (value);
      break;
    case G_TYPE_INT:
      g_return_val_if_fail (qdata->size == sizeof (gint), TRUE);
      G_STRUCT_MEMBER (gint, ffmpegenc, qdata->offset) =
          g_value_get_int (value);
      break;
    case G_TYPE_INT64:
      g_return_val_if_fail (qdata->size == sizeof (gint64), TRUE);
      G_STRUCT_MEMBER (gint64, ffmpegenc, qdata->offset) =
          g_value_get_int64 (value);
      break;
    case G_TYPE_FLOAT:
      g_return_val_if_fail (qdata->size == sizeof (gfloat), TRUE);
      G_STRUCT_MEMBER (gfloat, ffmpegenc, qdata->offset) =
          g_value_get_float (value);
      break;
    case G_TYPE_STRING:
      g_return_val_if_fail (qdata->size == sizeof (gchar *), TRUE);
      g_free (G_STRUCT_MEMBER (gchar *, ffmpegenc, qdata->offset));
      G_STRUCT_MEMBER (gchar *, ffmpegenc, qdata->offset) =
          g_value_dup_string (value);
      break;
    default:                   /* must be enum, given the check above */
      if (G_IS_PARAM_SPEC_ENUM (pspec)) {
        g_return_val_if_fail (qdata->size == sizeof (gint), TRUE);
        G_STRUCT_MEMBER (gint, ffmpegenc, qdata->offset) =
            g_value_get_enum (value);
      } else if (G_IS_PARAM_SPEC_FLAGS (pspec)) {
        g_return_val_if_fail (qdata->size == sizeof (guint), TRUE);
        G_STRUCT_MEMBER (guint, ffmpegenc, qdata->offset) =
            g_value_get_flags (value);
      } else {                  /* oops, bit lazy we don't cover this case yet */
        g_critical ("%s does not yet support type %s", GST_FUNCTION,
            g_type_name (G_PARAM_SPEC_VALUE_TYPE (pspec)));
      }

      break;
  }

  return TRUE;
}

/* returns TRUE if it is a known property for this config system,
 * FALSE otherwise */
gboolean
gst_ffmpeg_cfg_get_property (GObject * object,
    GValue * value, GParamSpec * pspec)
{
  GstFFMpegVidEnc *ffmpegenc = (GstFFMpegVidEnc *) (object);
  GParamSpecData *qdata;

  qdata = g_param_spec_get_qdata (pspec, quark);

  /* our param specs should have such qdata */
  if (!qdata)
    return FALSE;

  /* get the member using the offset, also mild type check based on size */
  switch (G_PARAM_SPEC_VALUE_TYPE (pspec)) {
    case G_TYPE_BOOLEAN:
      g_return_val_if_fail (qdata->size == sizeof (gboolean), TRUE);
      g_value_set_boolean (value,
          G_STRUCT_MEMBER (gboolean, ffmpegenc, qdata->offset));
      break;
    case G_TYPE_UINT:
      g_return_val_if_fail (qdata->size == sizeof (guint), TRUE);
      g_value_set_uint (value,
          G_STRUCT_MEMBER (guint, ffmpegenc, qdata->offset));
      break;
    case G_TYPE_INT:
      g_return_val_if_fail (qdata->size == sizeof (gint), TRUE);
      g_value_set_int (value, G_STRUCT_MEMBER (gint, ffmpegenc, qdata->offset));
      break;
    case G_TYPE_INT64:
      g_return_val_if_fail (qdata->size == sizeof (gint64), TRUE);
      g_value_set_int64 (value, G_STRUCT_MEMBER (gint64, ffmpegenc,
              qdata->offset));
      break;
    case G_TYPE_FLOAT:
      g_return_val_if_fail (qdata->size == sizeof (gfloat), TRUE);
      g_value_set_float (value,
          G_STRUCT_MEMBER (gfloat, ffmpegenc, qdata->offset));
      break;
    case G_TYPE_STRING:
      g_return_val_if_fail (qdata->size == sizeof (gchar *), TRUE);
      g_value_take_string (value,
          g_strdup (G_STRUCT_MEMBER (gchar *, ffmpegenc, qdata->offset)));
      break;
    default:                   /* must be enum, given the check above */
      if (G_IS_PARAM_SPEC_ENUM (pspec)) {
        g_return_val_if_fail (qdata->size == sizeof (gint), TRUE);
        g_value_set_enum (value,
            G_STRUCT_MEMBER (gint, ffmpegenc, qdata->offset));
      } else if (G_IS_PARAM_SPEC_FLAGS (pspec)) {
        g_return_val_if_fail (qdata->size == sizeof (guint), TRUE);
        g_value_set_flags (value,
            G_STRUCT_MEMBER (guint, ffmpegenc, qdata->offset));
      } else {                  /* oops, bit lazy we don't cover this case yet */
        g_critical ("%s does not yet support type %s", GST_FUNCTION,
            g_type_name (G_PARAM_SPEC_VALUE_TYPE (pspec)));
      }
      break;
  }

  return TRUE;
}

void
gst_ffmpeg_cfg_set_defaults (GstFFMpegVidEnc * ffmpegenc)
{
  GParamSpec **pspecs;
  guint num_props, i;

  pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (ffmpegenc),
      &num_props);

  for (i = 0; i < num_props; ++i) {
    GValue val = { 0, };
    GParamSpec *pspec = pspecs[i];

    /* only touch those that are really ours; i.e. should have some qdata */
    if (!g_param_spec_get_qdata (pspec, quark))
      continue;
    g_value_init (&val, G_PARAM_SPEC_VALUE_TYPE (pspec));
    g_param_value_set_default (pspec, &val);
    g_object_set_property (G_OBJECT (ffmpegenc),
        g_param_spec_get_name (pspec), &val);
    g_value_unset (&val);
  }

  g_free (pspecs);

}


void
gst_ffmpeg_cfg_fill_context (GstFFMpegVidEnc * ffmpegenc,
    AVCodecContext * context)
{
  GstFFMpegVidEncClass *klass
      = (GstFFMpegVidEncClass *) G_OBJECT_GET_CLASS (ffmpegenc);
  GParamSpec *pspec;
  GParamSpecData *qdata;
  GList *list;

  list = property_list;

  while (list) {
    gint context_offset;

    pspec = G_PARAM_SPEC (list->data);
    qdata = g_param_spec_get_qdata (pspec, quark);
    context_offset = qdata->offset - CONTEXT_CONFIG_OFFSET;
    if (gst_ffmpeg_cfg_codec_has_pspec (klass->in_plugin->id, pspec)
        && context_offset >= 0) {
      if (G_PARAM_SPEC_VALUE_TYPE (pspec) == G_TYPE_STRING) {
        /* make a copy for ffmpeg, it will likely free only some,
         * but in any case safer than a potential double free */
        G_STRUCT_MEMBER (gchar *, context, context_offset) =
            av_strdup (G_STRUCT_MEMBER (gchar *, ffmpegenc, qdata->offset));
      } else {
        /* memcpy a bit heavy for a small copy,
         * but hardly part of 'inner loop' */
        memcpy (G_STRUCT_MEMBER_P (context, context_offset),
            G_STRUCT_MEMBER_P (ffmpegenc, qdata->offset), qdata->size);
      }
    }
    list = list->next;
  }
}

void
gst_ffmpeg_cfg_finalize (GstFFMpegVidEnc * ffmpegenc)
{
  GParamSpec **pspecs;
  guint num_props, i;

  pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (ffmpegenc),
      &num_props);

  for (i = 0; i < num_props; ++i) {
    GParamSpec *pspec = pspecs[i];
    GParamSpecData *qdata;

    qdata = g_param_spec_get_qdata (pspec, quark);

    /* our param specs should have such qdata */
    if (!qdata)
      continue;

    switch (G_PARAM_SPEC_VALUE_TYPE (pspec)) {
      case G_TYPE_STRING:
        if (qdata->size == sizeof (gchar *)) {
          g_free (G_STRUCT_MEMBER (gchar *, ffmpegenc, qdata->offset));
          G_STRUCT_MEMBER (gchar *, ffmpegenc, qdata->offset) = NULL;
        }
        break;
      default:
        break;
    }
  }
  g_free (pspecs);
}
