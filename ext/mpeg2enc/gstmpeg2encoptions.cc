/* GStreamer mpeg2enc (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstmpeg2encoptions.cc: gobject/mpeg2enc option wrapping class
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmpeg2encoptions.hh"

/*
 * Property enumeration.
 */

enum {
  ARG_0,
  ARG_FORMAT,
  ARG_FRAMERATE,
  ARG_ASPECT,
  ARG_INTERLACE_MODE,
  ARG_BITRATE,
  ARG_NONVIDEO_BITRATE,
  ARG_QUANTISATION,
  ARG_VCD_STILL_SIZE,
  ARG_MOTION_SEARCH_RADIUS,
  ARG_REDUCTION_4_4,
  ARG_REDUCTION_2_2,
  ARG_UNIT_COEFF_ELIM,
  ARG_MIN_GOP_SIZE,
  ARG_MAX_GOP_SIZE,
  ARG_CLOSED_GOP,
  ARG_FORCE_B_B_P,
  ARG_B_PER_REFFRAME,
  ARG_QUANTISATION_REDUCTION,
  ARG_QUANT_REDUCTION_MAX_VAR,
  ARG_INTRA_DC_PRECISION,
  ARG_REDUCE_HF,
  ARG_KEEP_HF,
  ARG_QUANTISATION_MATRIX,
  ARG_BUFSIZE,
  ARG_VIDEO_NORM,
  ARG_SEQUENCE_LENGTH,
  ARG_3_2_PULLDOWN,
  ARG_SEQUENCE_HEADER_EVERY_GOP,
  ARG_PLAYBACK_FIELD_ORDER,
  ARG_DUMMY_SVCD_SOF,
  ARG_CORRECT_SVCD_HDS,
  ARG_ALTSCAN_MPEG2,
  ARG_CONSTRAINTS
  /* FILL ME */
};

/*
 * Property enumeration types.
 */

#define GST_TYPE_MPEG2ENC_FORMAT \
  (gst_mpeg2enc_format_get_type ())

static GType
gst_mpeg2enc_format_get_type (void)
{
  static GType mpeg2enc_format_type = 0;

  if (!mpeg2enc_format_type) {
    static const GEnumValue mpeg2enc_formats[] = {
      { 0, "0", "Generic MPEG-1" },
      { 1, "1", "Standard VCD" },
      { 2, "2", "User VCD" },
      { 3, "3", "Generic MPEG-2" },
      { 4, "4", "Standard SVCD" },
      { 5, "5", "User SVCD" },
      { 6, "6", "VCD Stills sequences" },
      { 7, "7", "SVCD Stills sequences" },
      { 8, "8", "DVD MPEG-2 for dvdauthor" },
      { 9, "9", "DVD MPEG-2" },
      { 0, NULL, NULL },
    };

    mpeg2enc_format_type =
	g_enum_register_static ("GstMpeg2encFormat",
				mpeg2enc_formats);
  }

  return mpeg2enc_format_type;
}

#define GST_TYPE_MPEG2ENC_FRAMERATE \
  (gst_mpeg2enc_framerate_get_type ())

static GType
gst_mpeg2enc_framerate_get_type (void)
{
  static GType mpeg2enc_framerate_type = 0;

  if (!mpeg2enc_framerate_type) {
    static const GEnumValue mpeg2enc_framerates[] = {
      { 0, "0", "Same as input" },
      { 1, "1", "24/1.001 (NTSC 3:2 pulldown converted film)" },
      { 2, "2", "24 (native film)" },
      { 3, "3", "25 (PAL/SECAM video)" },
      { 4, "4", "30/1.001 (NTSC video)" },
      { 5, "5", "30" },
      { 6, "6", "50 (PAL/SECAM fields)" },
      { 7, "7", "60/1.001 (NTSC fields)" },
      { 8, "8", "60" },
      { 0, NULL, NULL },
    };

    mpeg2enc_framerate_type =
	g_enum_register_static ("GstMpeg2encFramerate",
				mpeg2enc_framerates);
  }

  return mpeg2enc_framerate_type;
}

#define GST_TYPE_MPEG2ENC_ASPECT \
  (gst_mpeg2enc_aspect_get_type ())

static GType
gst_mpeg2enc_aspect_get_type (void)
{
  static GType mpeg2enc_aspect_type = 0;

  if (!mpeg2enc_aspect_type) {
    static const GEnumValue mpeg2enc_aspects[] = {
      { 0, "0", "Deduce from input" },
      { 1, "1", "1:1" },
      { 2, "2", "4:3" },
      { 3, "3", "16:9" },
      { 4, "4", "2.21:1" },
      { 0, NULL, NULL },
    };

    mpeg2enc_aspect_type =
	g_enum_register_static ("GstMpeg2encAspect",
				mpeg2enc_aspects);
  }

  return mpeg2enc_aspect_type;
}

#define GST_TYPE_MPEG2ENC_INTERLACE_MODE \
  (gst_mpeg2enc_interlace_mode_get_type ())

static GType
gst_mpeg2enc_interlace_mode_get_type (void)
{
  static GType mpeg2enc_interlace_mode_type = 0;

  if (!mpeg2enc_interlace_mode_type) {
    static const GEnumValue mpeg2enc_interlace_modes[] = {
      { -1, "-1", "Format default mode" },
      { 0,  "0",  "Progressive" },
      { 1,  "1",  "Interlaced, per-frame encoding" },
      { 2,  "2",  "Interlaced, per-field-encoding" },
      { 0, NULL, NULL },
    };

    mpeg2enc_interlace_mode_type =
	g_enum_register_static ("GstMpeg2encInterlaceMode",
				mpeg2enc_interlace_modes);
  }

  return mpeg2enc_interlace_mode_type;
}

#define GST_TYPE_MPEG2ENC_QUANTISATION_MATRIX \
  (gst_mpeg2enc_quantisation_matrix_get_type ())

#define GST_MPEG2ENC_QUANTISATION_MATRIX_DEFAULT 0
#define GST_MPEG2ENC_QUANTISATION_MATRIX_HI_RES  1
#define GST_MPEG2ENC_QUANTISATION_MATRIX_KVCD    2
#define GST_MPEG2ENC_QUANTISATION_MATRIX_TMPGENC 3

static GType
gst_mpeg2enc_quantisation_matrix_get_type (void)
{
  static GType mpeg2enc_quantisation_matrix_type = 0;

  if (!mpeg2enc_quantisation_matrix_type) {
    static const GEnumValue mpeg2enc_quantisation_matrixes[] = {
      { GST_MPEG2ENC_QUANTISATION_MATRIX_DEFAULT,
	"0", "Default" },
      { GST_MPEG2ENC_QUANTISATION_MATRIX_HI_RES,
	"1", "High resolution" },
      { GST_MPEG2ENC_QUANTISATION_MATRIX_KVCD,
	"2", "KVCD" },
      { GST_MPEG2ENC_QUANTISATION_MATRIX_TMPGENC,
	"3", "TMPGEnc" },
      { 0, NULL, NULL },
    };

    mpeg2enc_quantisation_matrix_type =
	g_enum_register_static ("GstMpeg2encQuantisationMatrix",
				mpeg2enc_quantisation_matrixes);
  }

  return mpeg2enc_quantisation_matrix_type;
}

#define GST_TYPE_MPEG2ENC_VIDEO_NORM \
  (gst_mpeg2enc_video_norm_get_type ())

static GType
gst_mpeg2enc_video_norm_get_type (void)
{
  static GType mpeg2enc_video_norm_type = 0;

  if (!mpeg2enc_video_norm_type) {
    static const GEnumValue mpeg2enc_video_norms[] = {
      { 0,   "0", "Unspecified" },
      { 'p', "p", "PAL" },
      { 'n', "n", "NTSC" },
      { 's', "s", "SECAM" },
      { 0, NULL, NULL },
    };

    mpeg2enc_video_norm_type =
	g_enum_register_static ("GstMpeg2encVideoNorm",
				mpeg2enc_video_norms);
  }

  return mpeg2enc_video_norm_type;
}

#define GST_TYPE_MPEG2ENC_PLAYBACK_FIELD_ORDER \
  (gst_mpeg2enc_playback_field_order_get_type ())

static GType
gst_mpeg2enc_playback_field_order_get_type (void)
{
  static GType mpeg2enc_playback_field_order_type = 0;

  if (!mpeg2enc_playback_field_order_type) {
    static const GEnumValue mpeg2enc_playback_field_orders[] = {
      { Y4M_UNKNOWN,            "0", "Unspecified" },
      { Y4M_ILACE_TOP_FIRST,    "1", "Top-field first" },
      { Y4M_ILACE_BOTTOM_FIRST, "2", "Bottom-field first" },
      { 0, NULL, NULL },
    };

    mpeg2enc_playback_field_order_type =
	g_enum_register_static ("GstMpeg2encPlaybackFieldOrders",
				mpeg2enc_playback_field_orders);
  }

  return mpeg2enc_playback_field_order_type;
}

/*
 * Class init stuff.
 */

GstMpeg2EncOptions::GstMpeg2EncOptions () :
  MPEG2EncOptions ()
{
  /* autodetect number of CPUs */
  num_cpus = sysconf (_SC_NPROCESSORS_ONLN);
  if (num_cpus < 0)
    num_cpus = 1;
  if (num_cpus > 32)
    num_cpus = 32;
}

/*
 * Init properties (call once).
 */

void
GstMpeg2EncOptions::initProperties (GObjectClass *klass)
{
  /* encoding profile */
  g_object_class_install_property (klass, ARG_FORMAT,
    g_param_spec_enum ("format", "Format", "Encoding profile format",
                       GST_TYPE_MPEG2ENC_FORMAT, 0,
		       (GParamFlags) G_PARAM_READWRITE));

  /* input/output stream overrides */
  g_object_class_install_property (klass, ARG_FRAMERATE,
    g_param_spec_enum ("framerate", "Framerate", "Output framerate",
                       GST_TYPE_MPEG2ENC_FRAMERATE, 0,
		       (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_ASPECT,
    g_param_spec_enum ("aspect", "Aspect", "Display aspect ratio",
                       GST_TYPE_MPEG2ENC_ASPECT, 0,
		       (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_INTERLACE_MODE,
    g_param_spec_enum ("interlace-mode", "Interlace mode",
		       "MPEG-2 motion estimation and encoding modes",
                       GST_TYPE_MPEG2ENC_INTERLACE_MODE, 0,
		       (GParamFlags) G_PARAM_READWRITE));

  /* general encoding stream options */
  g_object_class_install_property (klass, ARG_BITRATE,
    g_param_spec_int ("bitrate", "Bitrate", "Compressed video bitrate (kbps)",
                      0, 10*1024, 1125, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_NONVIDEO_BITRATE,
    g_param_spec_int ("non-video-bitrate", "Non-video bitrate",
		      "Assumed bitrate of non-video for sequence splitting (kbps)",
                      0, 10*1024, 0, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_QUANTISATION,
    g_param_spec_int ("quantisation", "Quantisation",
		      "Quantisation factor (0=default, 1=best, 31=worst)",
                      0, 31, 0, (GParamFlags) G_PARAM_READWRITE));

  /* stills options */
  g_object_class_install_property (klass, ARG_VCD_STILL_SIZE,
    g_param_spec_int ("vcd-still-size", "VCD stills size",
		      "Size of VCD stills (in kB)",
                      0, 512, 0, (GParamFlags) G_PARAM_READWRITE));

  /* motion estimation options */
  g_object_class_install_property (klass, ARG_MOTION_SEARCH_RADIUS,
    g_param_spec_int ("motion-search-radius", "Motion search radius",
		      "Motion compensation search radius",
                      0, 32, 16, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_REDUCTION_4_4,
    g_param_spec_int ("reduction-4x4", "4x4 reduction",
		      "Reduction factor for 4x4 subsampled candidate motion estimates"
		      " (1=max. quality, 4=max. speed)",
                      1, 4, 2, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_REDUCTION_2_2,
    g_param_spec_int ("reduction-2x2", "2x2 reduction",
		      "Reduction factor for 2x2 subsampled candidate motion estimates"
		      " (1=max. quality, 4=max. speed)",
                      1, 4, 3, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_UNIT_COEFF_ELIM,
    g_param_spec_int ("unit-coeff-elim", "Unit coefficience elimination",
		      "How agressively small-unit picture blocks should be skipped",
                      -40, 40, 0, (GParamFlags) G_PARAM_READWRITE));

  /* GOP options */
  g_object_class_install_property (klass, ARG_MIN_GOP_SIZE,
    g_param_spec_int ("min-gop-size", "Min. GOP size",
		      "Minimal size per Group-of-Pictures (-1=default)",
                      -1, 250, 0, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_MAX_GOP_SIZE,
    g_param_spec_int ("max-gop-size", "Max. GOP size",
		      "Maximal size per Group-of-Pictures (-1=default)",
                      -1, 250, 0, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_CLOSED_GOP,
    g_param_spec_boolean ("closed-gop", "Closed GOP",
			  "All Group-of-Pictures are closed (for multi-angle DVDs)",
			  FALSE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_FORCE_B_B_P,
    g_param_spec_boolean ("force-b-b-p", "Force B-B-P",
			  "Force two B frames between I/P frames when closing GOP boundaries",
			  FALSE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_B_PER_REFFRAME,
    g_param_spec_int ("b-per-refframe", "B per ref. frame",
		      "Number of B frames between each I/P frame",
                      0, 2, 2, (GParamFlags) G_PARAM_READWRITE));

  /* quantisation options */
  g_object_class_install_property (klass, ARG_QUANTISATION_REDUCTION,
    g_param_spec_float ("quantisation-reduction", "Quantisation reduction",
			"Max. quantisation reduction for highly active blocks",
			-4., 10., 0., (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_QUANT_REDUCTION_MAX_VAR,
    g_param_spec_float ("quant-reduction-max-var", "Max. quant. reduction variance",
			"Maximal luma variance below which quantisation boost is used",
			0., 2500., 0., (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_INTRA_DC_PRECISION,
    g_param_spec_int ("intra-dc-prec", "Intra. DC precision",
		      "Number of bits precision for DC (base colour) in MPEG-2 blocks",
                      8, 11, 9, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_REDUCE_HF,
    g_param_spec_float ("reduce-hf", "Reduce HF",
			"How much to reduce high-frequency resolution (by increasing quantisation)",
			0., 2., 0., (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_KEEP_HF,
    g_param_spec_boolean ("keep-hf", "Keep HF",
			  "Maximize high-frequency resolution (for high-quality sources)",
			  FALSE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_QUANTISATION_MATRIX,
    g_param_spec_enum ("quant-matrix", "Quant. matrix",
		       "Quantisation matrix to use for encoding",
                       GST_TYPE_MPEG2ENC_QUANTISATION_MATRIX, 0,
		       (GParamFlags) G_PARAM_READWRITE));

  /* general options */
  g_object_class_install_property (klass, ARG_BUFSIZE,
    g_param_spec_int ("bufsize", "Decoder buf. size",
		      "Target decoders video buffer size (kB)",
                      20, 4000, 46, (GParamFlags) G_PARAM_READWRITE));

  /* header flag settings */
  g_object_class_install_property (klass, ARG_VIDEO_NORM,
    g_param_spec_enum ("norm", "Norm",
		       "Tag output for specific video norm",
                       GST_TYPE_MPEG2ENC_VIDEO_NORM, 0,
		       (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_SEQUENCE_LENGTH,
    g_param_spec_int ("sequence-length", "Sequence length",
		      "Place a sequence boundary after each <num> MB (0=disable)",
                      0, 10*1024, 0, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_3_2_PULLDOWN,
    g_param_spec_boolean ("pulldown-3-2", "3-2 pull down",
			  "Generate header flags for 3-2 pull down 24fps movies",
			  FALSE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_SEQUENCE_HEADER_EVERY_GOP,
    g_param_spec_boolean ("sequence-header-every-gop",
			  "Sequence hdr. every GOP",
			  "Include a sequence header in every GOP",
			  FALSE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_DUMMY_SVCD_SOF,
    g_param_spec_boolean ("dummy-svcd-sof", "Dummy SVCD SOF",
			  "Generate dummy SVCD scan-data (for vcdimager)",
			  TRUE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_PLAYBACK_FIELD_ORDER,
    g_param_spec_enum ("playback-field-order", "Playback field order",
		       "Force specific playback field order",
                       GST_TYPE_MPEG2ENC_PLAYBACK_FIELD_ORDER, Y4M_UNKNOWN,
		       (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_CORRECT_SVCD_HDS,
    g_param_spec_boolean ("correct-svcd-hds", "Correct SVCD hor. size",
			  "Force SVCD width to 480 instead of 540/720",
			  FALSE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (klass, ARG_ALTSCAN_MPEG2,
    g_param_spec_boolean ("altscan-mpeg2", "Alt. MPEG-2 scan",
			  "Alternate MPEG-2 block scanning. Disabling this might "
			  "make buggy players play SVCD streams",
			  TRUE, (GParamFlags) G_PARAM_READWRITE));
#if 0
"--dxr2-hack"
#endif

  /* dangerous/experimental stuff */
  g_object_class_install_property (klass, ARG_CONSTRAINTS,
    g_param_spec_boolean ("constraints", "Constraints",
			  "Use strict video resolution and bitrate checks",
			  TRUE, (GParamFlags) G_PARAM_READWRITE));
}

/*
 * GObject property foo, C++ style.
 */

void
GstMpeg2EncOptions::getProperty (guint   prop_id,
				 GValue *value)
{
  switch (prop_id) {
    case ARG_FORMAT:
      g_value_set_enum (value, format);
      break;
    case ARG_FRAMERATE:
      g_value_set_enum (value, frame_rate);
      break;
    case ARG_ASPECT:
      g_value_set_enum (value, aspect_ratio);
      break;
    case ARG_INTERLACE_MODE:
      g_value_set_enum (value, fieldenc);
      break;
    case ARG_BITRATE:
      g_value_set_int (value, bitrate/1024);
      break;
    case ARG_NONVIDEO_BITRATE:
      g_value_set_int (value, nonvid_bitrate/1024);
      break;
    case ARG_QUANTISATION:
      g_value_set_int (value, quant);
      break;
    case ARG_VCD_STILL_SIZE:
      g_value_set_int (value, still_size / 1024);
      break;
    case ARG_MOTION_SEARCH_RADIUS:
      g_value_set_int (value, searchrad);
      break;
    case ARG_REDUCTION_4_4:
      g_value_set_int (value, me44_red);
      break;
    case ARG_REDUCTION_2_2:
      g_value_set_int (value, me22_red);
      break;
    case ARG_UNIT_COEFF_ELIM:
      g_value_set_int (value, unit_coeff_elim);
      break;
    case ARG_MIN_GOP_SIZE:
      g_value_set_int (value, min_GOP_size);
      break;
    case ARG_MAX_GOP_SIZE:
      g_value_set_int (value, max_GOP_size);
      break;
    case ARG_CLOSED_GOP:
      g_value_set_boolean (value, closed_GOPs);
      break;
    case ARG_FORCE_B_B_P:
      g_value_set_boolean (value, preserve_B);
      break;
    case ARG_B_PER_REFFRAME:
      g_value_set_int (value, Bgrp_size - 1);
      break;
    case ARG_QUANTISATION_REDUCTION:
      g_value_set_float (value, act_boost);
      break;
    case ARG_QUANT_REDUCTION_MAX_VAR:
      g_value_set_float (value, boost_var_ceil);
      break;
    case ARG_INTRA_DC_PRECISION:
      g_value_set_int (value, mpeg2_dc_prec - 8);
      break;
    case ARG_REDUCE_HF:
      g_value_set_float (value, hf_q_boost);
      break;
    case ARG_KEEP_HF:
      g_value_set_boolean (value, hf_quant == 2);
      break;
    case ARG_QUANTISATION_MATRIX:
      switch (hf_quant) {
        case 0:
          g_value_set_enum (value, GST_MPEG2ENC_QUANTISATION_MATRIX_DEFAULT);
          break;
        case 2:
          g_value_set_enum (value, GST_MPEG2ENC_QUANTISATION_MATRIX_HI_RES);
          break;
        case 3:
          g_value_set_enum (value, GST_MPEG2ENC_QUANTISATION_MATRIX_KVCD);
          break;
        case 4:
          g_value_set_enum (value, GST_MPEG2ENC_QUANTISATION_MATRIX_TMPGENC);
          break;
      }
      break;
    case ARG_BUFSIZE:
      g_value_set_int (value, video_buffer_size);
      break;
    case ARG_VIDEO_NORM:
      g_value_set_enum (value, norm);
      break;
    case ARG_SEQUENCE_LENGTH:
      g_value_set_int (value, seq_length_limit);
      break;
    case ARG_3_2_PULLDOWN:
      g_value_set_boolean (value, vid32_pulldown);
      break;
    case ARG_SEQUENCE_HEADER_EVERY_GOP:
      g_value_set_boolean (value, seq_hdr_every_gop);
      break;
    case ARG_DUMMY_SVCD_SOF:
      g_value_set_boolean (value, svcd_scan_data);
      break;
    case ARG_PLAYBACK_FIELD_ORDER:
      g_value_set_enum (value, force_interlacing);
      break;
    case ARG_CORRECT_SVCD_HDS:
      g_value_set_boolean (value, !hack_svcd_hds_bug);
      break;
    case ARG_ALTSCAN_MPEG2:
      g_value_set_boolean (value, !hack_altscan_bug);
      break;
    case ARG_CONSTRAINTS:
      g_value_set_boolean (value, !ignore_constraints);
      break;
    default:
      break;
  }
}

void
GstMpeg2EncOptions::setProperty (guint         prop_id,
				 const GValue *value)
{
  switch (prop_id) {
    case ARG_FORMAT:
      format = g_value_get_enum (value);
      break;
    case ARG_FRAMERATE:
      frame_rate = g_value_get_enum (value);
      break;
    case ARG_ASPECT:
      aspect_ratio = g_value_get_enum (value);
      break;
    case ARG_INTERLACE_MODE:
      fieldenc = g_value_get_enum (value);
      break;
    case ARG_BITRATE:
      bitrate = g_value_get_int (value) * 1024;
      break;
    case ARG_NONVIDEO_BITRATE:
      nonvid_bitrate = g_value_get_int (value) * 1024;
      break;
    case ARG_QUANTISATION:
      quant = g_value_get_int (value);
      break;
    case ARG_VCD_STILL_SIZE:
      still_size = g_value_get_int (value) * 1024;
      break;
    case ARG_MOTION_SEARCH_RADIUS:
      searchrad = g_value_get_int (value);
      break;
    case ARG_REDUCTION_4_4:
      me44_red = g_value_get_int (value);
      break;
    case ARG_REDUCTION_2_2:
      me22_red = g_value_get_int (value);
      break;
    case ARG_UNIT_COEFF_ELIM:
      unit_coeff_elim = g_value_get_int (value);
      break;
    case ARG_MIN_GOP_SIZE:
      min_GOP_size = g_value_get_int (value);
      break;
    case ARG_MAX_GOP_SIZE:
      max_GOP_size = g_value_get_int (value);
      break;
    case ARG_CLOSED_GOP:
      closed_GOPs = g_value_get_boolean (value);
      break;
    case ARG_FORCE_B_B_P:
      preserve_B = g_value_get_boolean (value);
      break;
    case ARG_B_PER_REFFRAME:
      Bgrp_size = g_value_get_int (value) + 1;
      break;
    case ARG_QUANTISATION_REDUCTION:
      act_boost = g_value_get_float (value);
      break;
    case ARG_QUANT_REDUCTION_MAX_VAR:
      boost_var_ceil = g_value_get_float (value);
      break;
    case ARG_INTRA_DC_PRECISION:
      mpeg2_dc_prec = g_value_get_int (value) + 8;
      break;
    case ARG_REDUCE_HF:
      hf_q_boost = g_value_get_float (value);
      if (hf_quant == 0 && hf_q_boost != 0.)
        hf_quant = 1;
      break;
    case ARG_KEEP_HF:
      hf_quant = g_value_get_boolean (value) ? 2 : 0;
      break;
    case ARG_QUANTISATION_MATRIX:
      switch (g_value_get_enum (value)) {
        case GST_MPEG2ENC_QUANTISATION_MATRIX_DEFAULT:
          hf_quant = 0;
          hf_q_boost = 0;
          break;
        case GST_MPEG2ENC_QUANTISATION_MATRIX_HI_RES:
          hf_quant = 2;
          break;
        case GST_MPEG2ENC_QUANTISATION_MATRIX_KVCD:
          hf_quant = 3;
          break;
        case GST_MPEG2ENC_QUANTISATION_MATRIX_TMPGENC:
          hf_quant = 4;
          break;
      }
      break;
    case ARG_BUFSIZE:
      video_buffer_size = g_value_get_int (value);
      break;
    case ARG_VIDEO_NORM:
      norm = g_value_get_enum (value);
      break;
    case ARG_SEQUENCE_LENGTH:
      seq_length_limit = g_value_get_int (value);
      break;
    case ARG_3_2_PULLDOWN:
      vid32_pulldown = g_value_get_boolean (value);
      break;
    case ARG_SEQUENCE_HEADER_EVERY_GOP:
      seq_hdr_every_gop = g_value_get_boolean (value);
      break;
    case ARG_DUMMY_SVCD_SOF:
      svcd_scan_data = g_value_get_boolean (value);
      break;
    case ARG_PLAYBACK_FIELD_ORDER:
      force_interlacing = g_value_get_enum (value);
      break;
    case ARG_CORRECT_SVCD_HDS:
      hack_svcd_hds_bug = !g_value_get_boolean (value);
      break;
    case ARG_ALTSCAN_MPEG2:
      hack_altscan_bug = !g_value_get_boolean (value);
      break;
    case ARG_CONSTRAINTS:
      ignore_constraints = !g_value_get_boolean (value);
      break;
    default:
      break;
  }
}
