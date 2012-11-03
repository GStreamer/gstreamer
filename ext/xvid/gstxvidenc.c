/* GStreamer xvid encoder plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *           (C) 2006 Mark Nauwelaerts <manauw@skynet.be>
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

/* based on:
 * - the original xvidenc (by Ronald Bultje)
 * - transcode/mplayer's xvid encoder (by Edouard Gomez)
 *
 * TODO some documentation (e.g. on properties)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <xvid.h>

#include <gst/video/video.h>
#include "gstxvidenc.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YUY2, YV12, YVYU, UYVY }")
        "; " RGB_24_32_STATIC_CAPS (32, 0x00ff0000, 0x0000ff00,
            0x000000ff) "; " RGB_24_32_STATIC_CAPS (32, 0xff000000, 0x00ff0000,
            0x0000ff00) "; " RGB_24_32_STATIC_CAPS (32, 0x0000ff00, 0x00ff0000,
            0xff000000) "; " RGB_24_32_STATIC_CAPS (32, 0x000000ff, 0x0000ff00,
            0x00ff0000) "; " RGB_24_32_STATIC_CAPS (24, 0x0000ff, 0x00ff00,
            0xff0000) "; " GST_VIDEO_CAPS_RGB_15 "; " GST_VIDEO_CAPS_RGB_16)
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) 4, "
        "systemstream = (boolean) FALSE, "
        "width = (int) [ 0, MAX ], "
        "height = (int) [ 0, MAX ], "
        "framerate = (fraction) [ 0/1, MAX ], "
        "profile = (string) simple, "
        "level = (string) { 0, 1, 2, 3, 4a, 5, 6 };"
        "video/mpeg, "
        "mpegversion = (int) 4, "
        "systemstream = (boolean) FALSE, "
        "width = (int) [ 0, MAX ], "
        "height = (int) [ 0, MAX ], "
        "framerate = (fraction) [ 0/1, MAX ], "
        "profile = (string) advanced-real-time-simple, "
        "level = (string) { 1, 2, 3, 4 };"
        "video/mpeg, "
        "mpegversion = (int) 4, "
        "systemstream = (boolean) FALSE, "
        "width = (int) [ 0, MAX ], "
        "height = (int) [ 0, MAX ], "
        "framerate = (fraction) [ 0/1, MAX ], "
        "profile = (string) advanced-simple, "
        "level = (string) { 0, 1, 2, 3, 4 };"
        "video/mpeg, "
        "mpegversion = (int) 4, "
        "systemstream = (boolean) FALSE, "
        "width = (int) [ 0, MAX ], " "height = (int) [ 0, MAX ]; "
        "video/x-xvid, "
        "width = (int) [ 0, MAX ], "
        "height = (int) [ 0, MAX ], " "framerate = (fraction) [ 0/1, MAX ];")
    );


/* XvidEnc properties */

/* maximum property-id */
static int xvidenc_prop_count;

/* quark used for named pointer on param specs */
static GQuark xvidenc_pspec_quark;

GST_DEBUG_CATEGORY_STATIC (xvidenc_debug);
#define GST_CAT_DEFAULT xvidenc_debug

static void gst_xvidenc_base_init (GstXvidEncClass * klass);
static void gst_xvidenc_class_init (GstXvidEncClass * klass);
static void gst_xvidenc_init (GstXvidEnc * xvidenc);
static void gst_xvidenc_finalize (GObject * object);
static GstFlowReturn gst_xvidenc_chain (GstPad * pad, GstBuffer * data);
static gboolean gst_xvidenc_setcaps (GstPad * pad, GstCaps * vscapslist);
static GstCaps *gst_xvidenc_getcaps (GstPad * pad);
static void gst_xvidenc_flush_buffers (GstXvidEnc * xvidenc, gboolean send);
static gboolean gst_xvidenc_handle_sink_event (GstPad * pad, GstEvent * event);

/* properties */
static void gst_xvidenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_xvidenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_xvidenc_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

#define GST_TYPE_XVIDENC_PROFILE (gst_xvidenc_profile_get_type ())
static GType
gst_xvidenc_profile_get_type (void)
{
  static GType xvidenc_profile_type = 0;

  if (!xvidenc_profile_type) {
    static const GEnumValue xvidenc_profiles[] = {
      {0, "UNP", "Unrestricted profile"},
      {XVID_PROFILE_S_L0, "S_L0", "Simple profile, L0"},
      {XVID_PROFILE_S_L1, "S_L1", "Simple profile, L1"},
      {XVID_PROFILE_S_L2, "S_L2", "Simple profile, L2"},
      {XVID_PROFILE_S_L3, "S_L3", "Simple profile, L3"},
      {XVID_PROFILE_S_L4a, "S_L4a", "Simple profile, L4a"},
      {XVID_PROFILE_S_L5, "S_L5", "Simple profile, L5"},
      {XVID_PROFILE_S_L6, "S_L6", "Simple profile, L6"},
      {XVID_PROFILE_ARTS_L1, "ARTS_L1",
          "Advanced real-time simple profile, L1"},
      {XVID_PROFILE_ARTS_L2, "ARTS_L2",
          "Advanced real-time simple profile, L2"},
      {XVID_PROFILE_ARTS_L3, "ARTS_L3",
          "Advanced real-time simple profile, L3"},
      {XVID_PROFILE_ARTS_L4, "ARTS_L4",
          "Advanced real-time simple profile, L4"},
      {XVID_PROFILE_AS_L0, "AS_L0", "Advanced simple profile, L0"},
      {XVID_PROFILE_AS_L1, "AS_L1", "Advanced simple profile, L1"},
      {XVID_PROFILE_AS_L2, "AS_L2", "Advanced simple profile, L2"},
      {XVID_PROFILE_AS_L3, "AS_L3", "Advanced simple profile, L3"},
      {XVID_PROFILE_AS_L4, "AS_L4", "Advanced simple profile, L4"},
      {0, NULL, NULL},
    };

    xvidenc_profile_type =
        g_enum_register_static ("GstXvidEncProfiles", xvidenc_profiles);
  }

  return xvidenc_profile_type;
}

#define GST_TYPE_XVIDENC_QUANT_TYPE (gst_xvidenc_quant_type_get_type ())
static GType
gst_xvidenc_quant_type_get_type (void)
{
  static GType xvidenc_quant_type_type = 0;

  if (!xvidenc_quant_type_type) {
    static const GEnumValue xvidenc_quant_types[] = {
      {0, "H263 quantization", "h263"},
      {XVID_VOL_MPEGQUANT, "MPEG quantization", "mpeg"},
      {0, NULL, NULL},
    };

    xvidenc_quant_type_type =
        g_enum_register_static ("GstXvidEncQuantTypes", xvidenc_quant_types);
  }

  return xvidenc_quant_type_type;
}


enum
{
  XVIDENC_CBR,
  XVIDENC_VBR_PASS1,
  XVIDENC_VBR_PASS2,
  XVIDENC_QUANT
};

#define GST_TYPE_XVIDENC_PASS (gst_xvidenc_pass_get_type ())
static GType
gst_xvidenc_pass_get_type (void)
{
  static GType xvidenc_pass_type = 0;

  if (!xvidenc_pass_type) {
    static const GEnumValue xvidenc_passes[] = {
      {XVIDENC_CBR, "Constant Bitrate Encoding", "cbr"},
      {XVIDENC_QUANT, "Constant Quantizer", "quant"},
      {XVIDENC_VBR_PASS1, "VBR Encoding - Pass 1", "pass1"},
      {XVIDENC_VBR_PASS2, "VBR Encoding - Pass 2", "pass2"},
      {0, NULL, NULL},
    };

    xvidenc_pass_type =
        g_enum_register_static ("GstXvidEncPasses", xvidenc_passes);
  }

  return xvidenc_pass_type;
}


GType
gst_xvidenc_get_type (void)
{
  static GType xvidenc_type = 0;

  if (!xvidenc_type) {
    static const GTypeInfo xvidenc_info = {
      sizeof (GstXvidEncClass),
      (GBaseInitFunc) gst_xvidenc_base_init,
      NULL,
      (GClassInitFunc) gst_xvidenc_class_init,
      NULL,
      NULL,
      sizeof (GstXvidEnc),
      0,
      (GInstanceInitFunc) gst_xvidenc_init,
    };
    const GInterfaceInfo preset_interface_info = {
      NULL,                     /* interface_init */
      NULL,                     /* interface_finalize */
      NULL                      /* interface_data */
    };

    xvidenc_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstXvidEnc", &xvidenc_info, 0);

    g_type_add_interface_static (xvidenc_type, GST_TYPE_PRESET,
        &preset_interface_info);
  }
  return xvidenc_type;
}

static void
gst_xvidenc_base_init (GstXvidEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_set_static_metadata (element_class, "XviD video encoder",
      "Codec/Encoder/Video",
      "XviD encoder based on xvidcore",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
}

/* add property pspec to klass using the counter count,
 * and place info based on struct_type and member as a named pointer
 * specified by quark */
#define gst_xvidenc_add_pspec_full(klass, pspec, count, quark,          \
    struct_type, member)                                                \
G_STMT_START {                                                          \
  guint _offset = G_STRUCT_OFFSET (struct_type, member);                \
  g_param_spec_set_qdata (pspec, quark,                                 \
      GUINT_TO_POINTER (_offset));                                      \
  g_object_class_install_property (klass, ++count, pspec);              \
} G_STMT_END

#define gst_xvidenc_add_pspec(klass, pspec, member)                     \
  gst_xvidenc_add_pspec_full (klass, pspec, xvidenc_prop_count,         \
      xvidenc_pspec_quark, GstXvidEnc, member)

/* using the above system, property maintenance is centralized here
 * (_get_property, _set_property and setting of default value in _init)
 * follow automatically, it only remains to actually use it in the code
 * (which may include free-ing in finalize) */

static void
gst_xvidenc_class_init (GstXvidEncClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;
  GParamSpec *pspec;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (xvidenc_debug, "xvidenc", 0, "XviD encoder");

  gobject_class->finalize = gst_xvidenc_finalize;

  gobject_class->set_property = gst_xvidenc_set_property;
  gobject_class->get_property = gst_xvidenc_get_property;

  /* prop handling */
  xvidenc_prop_count = 0;
  xvidenc_pspec_quark = g_quark_from_static_string ("xvid-enc-param-spec-data");

  pspec = g_param_spec_enum ("profile", "Profile",
      "XviD/MPEG-4 encoding profile",
      GST_TYPE_XVIDENC_PROFILE, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, profile);

  pspec = g_param_spec_enum ("quant-type", "Quantizer Type",
      "Quantizer type", GST_TYPE_XVIDENC_QUANT_TYPE, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, quant_type);

  pspec = g_param_spec_enum ("pass", "Encoding pass/type",
      "Encoding pass/type",
      GST_TYPE_XVIDENC_PASS, XVIDENC_CBR,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, pass);

  pspec = g_param_spec_int ("bitrate", "Bitrate",
      "[CBR|PASS2] Target video bitrate (bps)",
      0, G_MAXINT, 1800000, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, bitrate);

  pspec = g_param_spec_int ("quantizer", "Quantizer",
      "[QUANT] Quantizer to apply for constant quantizer mode",
      2, 31, 2, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, quant);

  pspec = g_param_spec_string ("statsfile", "Statistics Filename",
      "[PASS1|PASS2] Filename to store data for 2-pass encoding",
      "xvid-stats.log", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, filename);

  pspec = g_param_spec_int ("max-key-interval", "Max. Key Interval",
      "Maximum number of frames between two keyframes (< 0 is in sec)",
      -100, G_MAXINT, -10, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, max_key_interval);

  pspec = g_param_spec_boolean ("closed-gop", "Closed GOP",
      "Closed GOP", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, closed_gop);

  pspec = g_param_spec_int ("motion", "ME Quality",
      "Quality of Motion Estimation", 0, 6, 6,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, motion);

  pspec = g_param_spec_boolean ("me-chroma", "ME Chroma",
      "Enable use of Chroma planes for Motion Estimation",
      TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, me_chroma);

  pspec = g_param_spec_int ("me-vhq", "ME DCT/Frequency",
      "Extent in which to use DCT to minimize encoding length",
      0, 4, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, me_vhq);

  pspec = g_param_spec_boolean ("me-quarterpel", "ME Quarterpel",
      "Use quarter pixel precision for motion vector search",
      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, me_quarterpel);

  pspec = g_param_spec_boolean ("lumimasking", "Lumimasking",
      "Enable lumimasking - apply more compression to dark or bright areas",
      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, lumimasking);

  pspec = g_param_spec_int ("max-bframes", "Max B-Frames",
      "Maximum B-frames in a row", 0, G_MAXINT, 1,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, max_bframes);

  pspec = g_param_spec_int ("bquant-ratio", "B-quantizer ratio",
      "Ratio in B-frame quantizer computation", 0, 200, 150,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, bquant_ratio);

  pspec = g_param_spec_int ("bquant-offset", "B-quantizer offset",
      "Offset in B-frame quantizer computation",
      0, 200, 100, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, bquant_offset);

  pspec = g_param_spec_int ("bframe-threshold", "B-Frame Threshold",
      "Higher threshold yields more chance that B-frame is used",
      -255, 255, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, bframe_threshold);

  pspec = g_param_spec_boolean ("gmc", "Global Motion Compensation",
      "Allow generation of Sprite Frames for Pan/Zoom/Rotating images",
      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, gmc);

  pspec = g_param_spec_boolean ("trellis", "Trellis Quantization",
      "Enable Trellis Quantization", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, trellis);

  pspec = g_param_spec_boolean ("interlaced", "Interlaced Material",
      "Enable for interlaced video material", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, interlaced);

  pspec = g_param_spec_boolean ("cartoon", "Cartoon Material",
      "Adjust thresholds for flat looking cartoons", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, cartoon);

  pspec = g_param_spec_boolean ("greyscale", "Disable Chroma",
      "Do not write chroma data in encoded video", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, greyscale);

  pspec = g_param_spec_boolean ("hqacpred", "High quality AC prediction",
      "Enable high quality AC prediction", TRUE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, hqacpred);

  pspec = g_param_spec_int ("max-iquant", "Max Quant I-Frames",
      "Upper bound for I-frame quantization", 0, 31, 31,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, max_iquant);

  pspec = g_param_spec_int ("min-iquant", "Min Quant I-Frames",
      "Lower bound for I-frame quantization", 0, 31, 2,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, min_iquant);

  pspec = g_param_spec_int ("max-pquant", "Max Quant P-Frames",
      "Upper bound for P-frame quantization", 0, 31, 31,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, max_pquant);

  pspec = g_param_spec_int ("min-pquant", "Min Quant P-Frames",
      "Lower bound for P-frame quantization", 0, 31, 2,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, min_pquant);

  pspec = g_param_spec_int ("max-bquant", "Max Quant B-Frames",
      "Upper bound for B-frame quantization", 0, 31, 31,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, max_bquant);

  pspec = g_param_spec_int ("min-bquant", "Min Quant B-Frames",
      "Lower bound for B-frame quantization", 0, 31, 2,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, min_bquant);

  pspec = g_param_spec_int ("reaction-delay-factor", "Reaction Delay Factor",
      "[CBR] Reaction delay factor", -1, 100, -1,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, reaction_delay_factor);

  pspec = g_param_spec_int ("averaging-period", "Averaging Period",
      "[CBR] Number of frames for which XviD averages bitrate",
      -1, 100, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, averaging_period);

  pspec = g_param_spec_int ("buffer", "Buffer Size",
      "[CBR] Size of the video buffers", -1, G_MAXINT, -1,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, buffer);

  pspec = g_param_spec_int ("keyframe-boost", "Keyframe boost",
      "[PASS2] Bitrate boost for keyframes", 0, 100, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, keyframe_boost);

  pspec = g_param_spec_int ("curve-compression-high", "Curve Compression High",
      "[PASS2] Shrink factor for upper part of bitrate curve",
      0, 100, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, curve_compression_high);

  pspec = g_param_spec_int ("curve-compression-low", "Curve Compression Low",
      "[PASS2] Growing factor for lower part of bitrate curve",
      0, 100, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, curve_compression_low);

  pspec = g_param_spec_int ("flow-control-strength", "Flow Control Strength",
      "[PASS2] Overflow control strength per frame",
      -1, 100, 5, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, overflow_control_strength);

  pspec =
      g_param_spec_int ("max-overflow-improvement", "Max Overflow Improvement",
      "[PASS2] Amount in % that flow control can increase frame size compared to ideal curve",
      -1, 100, 5, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, max_overflow_improvement);

  pspec =
      g_param_spec_int ("max-overflow-degradation", "Max Overflow Degradation",
      "[PASS2] Amount in % that flow control can decrease frame size compared to ideal curve",
      -1, 100, 5, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, max_overflow_degradation);

  pspec = g_param_spec_int ("keyframe-reduction", "Keyframe Reduction",
      "[PASS2] Keyframe size reduction in % of those within threshold",
      -1, 100, 20, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, kfreduction);

  pspec = g_param_spec_int ("keyframe-threshold", "Keyframe Threshold",
      "[PASS2] Distance between keyframes not to be subject to reduction",
      -1, 100, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, kfthreshold);

  pspec =
      g_param_spec_int ("container-frame-overhead", "Container Frame Overhead",
      "[PASS2] Average container overhead per frame", -1, 100, -1,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  gst_xvidenc_add_pspec (gobject_class, pspec, container_frame_overhead);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_xvidenc_change_state);
}


static void
gst_xvidenc_init (GstXvidEnc * xvidenc)
{
  GParamSpec **pspecs;
  guint i, num_props;

  /* create the sink pad */
  xvidenc->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (xvidenc), xvidenc->sinkpad);

  gst_pad_set_chain_function (xvidenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xvidenc_chain));
  gst_pad_set_setcaps_function (xvidenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xvidenc_setcaps));
  gst_pad_set_getcaps_function (xvidenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xvidenc_getcaps));
  gst_pad_set_event_function (xvidenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_xvidenc_handle_sink_event));

  /* create the src pad */
  xvidenc->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_element_add_pad (GST_ELEMENT (xvidenc), xvidenc->srcpad);
  gst_pad_use_fixed_caps (xvidenc->srcpad);

  /* init properties. */
  xvidenc->width = xvidenc->height = xvidenc->csp = -1;
  xvidenc->par_width = xvidenc->par_height = 1;

  /* set defaults for user properties */
  pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (xvidenc),
      &num_props);

  for (i = 0; i < num_props; ++i) {
    GValue val = { 0, };
    GParamSpec *pspec = pspecs[i];

    /* only touch those that are really ours; i.e. should have some qdata */
    if (!g_param_spec_get_qdata (pspec, xvidenc_pspec_quark))
      continue;
    g_value_init (&val, G_PARAM_SPEC_VALUE_TYPE (pspec));
    g_param_value_set_default (pspec, &val);
    g_object_set_property (G_OBJECT (xvidenc), g_param_spec_get_name (pspec),
        &val);
    g_value_unset (&val);
  }

  g_free (pspecs);

  /* set xvid handle to NULL */
  xvidenc->handle = NULL;

  /* get a queue to keep time info if frames get delayed */
  xvidenc->delay = NULL;

  /* cache some xvid data so need not rebuild for each frame */
  xvidenc->xframe_cache = NULL;
}


static void
gst_xvidenc_finalize (GObject * object)
{

  GstXvidEnc *xvidenc = GST_XVIDENC (object);

  g_free (xvidenc->filename);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_xvidenc_handle_sink_event (GstPad * pad, GstEvent * event)
{
  GstXvidEnc *xvidenc = GST_XVIDENC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_xvidenc_flush_buffers (xvidenc, TRUE);
      break;
      /* no flushing if flush received,
         buffers in encoder are considered (in the) past */
    default:
      break;
  }

  return gst_pad_push_event (xvidenc->srcpad, event);
}

static gboolean
gst_xvidenc_setup (GstXvidEnc * xvidenc)
{
  xvid_enc_create_t xenc;
  xvid_enc_plugin_t xplugin[2];
  gint ret;
  GstCaps *allowed_caps;
  gint profile = -1;

  /* Negotiate profile/level with downstream */
  allowed_caps = gst_pad_get_allowed_caps (xvidenc->srcpad);
  if (allowed_caps && !gst_caps_is_empty (allowed_caps)) {
    const gchar *profile_str, *level_str;

    allowed_caps = gst_caps_make_writable (allowed_caps);
    gst_caps_truncate (allowed_caps);

    profile_str =
        gst_structure_get_string (gst_caps_get_structure (allowed_caps, 0),
        "profile");
    level_str =
        gst_structure_get_string (gst_caps_get_structure (allowed_caps, 0),
        "level");
    if (profile_str) {
      if (g_str_equal (profile_str, "simple")) {
        if (!level_str) {
          profile = XVID_PROFILE_S_L0;
        } else if (g_str_equal (level_str, "0")) {
          profile = XVID_PROFILE_S_L0;
        } else if (g_str_equal (level_str, "1")) {
          profile = XVID_PROFILE_S_L1;
        } else if (g_str_equal (level_str, "2")) {
          profile = XVID_PROFILE_S_L2;
        } else if (g_str_equal (level_str, "3")) {
          profile = XVID_PROFILE_S_L3;
        } else if (g_str_equal (level_str, "4a")) {
          profile = XVID_PROFILE_S_L4a;
        } else if (g_str_equal (level_str, "5")) {
          profile = XVID_PROFILE_S_L5;
        } else if (g_str_equal (level_str, "6")) {
          profile = XVID_PROFILE_S_L6;
        } else {
          GST_ERROR_OBJECT (xvidenc,
              "Invalid profile/level combination (%s %s)", profile_str,
              level_str);
        }
      } else if (g_str_equal (profile_str, "advanced-real-time-simple")) {
        if (!level_str) {
          profile = XVID_PROFILE_ARTS_L1;
        } else if (g_str_equal (level_str, "1")) {
          profile = XVID_PROFILE_ARTS_L1;
        } else if (g_str_equal (level_str, "2")) {
          profile = XVID_PROFILE_ARTS_L2;
        } else if (g_str_equal (level_str, "3")) {
          profile = XVID_PROFILE_ARTS_L3;
        } else if (g_str_equal (level_str, "4")) {
          profile = XVID_PROFILE_ARTS_L4;
        } else {
          GST_ERROR_OBJECT (xvidenc,
              "Invalid profile/level combination (%s %s)", profile_str,
              level_str);
        }
      } else if (g_str_equal (profile_str, "advanced-simple")) {
        if (!level_str) {
          profile = XVID_PROFILE_AS_L0;
        } else if (g_str_equal (level_str, "0")) {
          profile = XVID_PROFILE_AS_L0;
        } else if (g_str_equal (level_str, "1")) {
          profile = XVID_PROFILE_AS_L1;
        } else if (g_str_equal (level_str, "2")) {
          profile = XVID_PROFILE_AS_L2;
        } else if (g_str_equal (level_str, "3")) {
          profile = XVID_PROFILE_AS_L3;
        } else if (g_str_equal (level_str, "4")) {
          profile = XVID_PROFILE_AS_L4;
        } else {
          GST_ERROR_OBJECT (xvidenc,
              "Invalid profile/level combination (%s %s)", profile_str,
              level_str);
        }
      } else {
        GST_ERROR_OBJECT (xvidenc, "Invalid profile (%s)", profile_str);
      }
    }
  }
  if (allowed_caps)
    gst_caps_unref (allowed_caps);

  if (profile != -1) {
    xvidenc->profile = profile;
    g_object_notify (G_OBJECT (xvidenc), "profile");
  }

  /* see xvid.h for the meaning of all this. */
  gst_xvid_init_struct (xenc);

  xenc.profile = xvidenc->used_profile = xvidenc->profile;
  xenc.width = xvidenc->width;
  xenc.height = xvidenc->height;
  xenc.max_bframes = xvidenc->max_bframes;
  xenc.global = XVID_GLOBAL_PACKED
      | (xvidenc->closed_gop ? XVID_GLOBAL_CLOSED_GOP : 0);

  xenc.bquant_ratio = xvidenc->bquant_ratio;
  xenc.bquant_offset = xvidenc->bquant_offset;

  xenc.fbase = xvidenc->fbase;
  xenc.fincr = xvidenc->fincr;
  xenc.max_key_interval = (xvidenc->max_key_interval < 0) ?
      (-xvidenc->max_key_interval * xenc.fbase /
      xenc.fincr) : xvidenc->max_key_interval;
  xenc.handle = NULL;

  /* quantizer ranges */
  xenc.min_quant[0] = xvidenc->min_iquant;
  xenc.min_quant[1] = xvidenc->min_pquant;
  xenc.min_quant[2] = xvidenc->min_bquant;
  xenc.max_quant[0] = xvidenc->max_iquant;
  xenc.max_quant[1] = xvidenc->max_pquant;
  xenc.max_quant[2] = xvidenc->max_bquant;

  /* cbr, vbr or constant quantizer */
  xenc.num_plugins = 1;
  xenc.plugins = xplugin;
  switch (xvidenc->pass) {
    case XVIDENC_CBR:
    case XVIDENC_QUANT:
    {
      xvid_plugin_single_t xsingle;
      xvid_enc_zone_t xzone;

      gst_xvid_init_struct (xsingle);

      xenc.plugins[0].func = xvid_plugin_single;
      xenc.plugins[0].param = &xsingle;

      xsingle.bitrate = xvidenc->bitrate;
      xsingle.reaction_delay_factor = MAX (0, xvidenc->reaction_delay_factor);
      xsingle.averaging_period = MAX (0, xvidenc->averaging_period);
      xsingle.buffer = MAX (0, xvidenc->buffer);

      if (xvidenc->pass == XVIDENC_CBR)
        break;

      /* set up a const quantizer zone */
      xzone.mode = XVID_ZONE_QUANT;
      xzone.frame = 0;
      xzone.increment = xvidenc->quant;
      xzone.base = 1;
      xenc.zones = &xzone;
      xenc.num_zones++;

      break;
    }
    case XVIDENC_VBR_PASS1:
    {
      xvid_plugin_2pass1_t xpass;

      gst_xvid_init_struct (xpass);

      xenc.plugins[0].func = xvid_plugin_2pass1;
      xenc.plugins[0].param = &xpass;

      xpass.filename = xvidenc->filename;
      break;
    }
    case XVIDENC_VBR_PASS2:
    {
      xvid_plugin_2pass2_t xpass;

      gst_xvid_init_struct (xpass);

      xenc.plugins[0].func = xvid_plugin_2pass2;
      xenc.plugins[0].param = &xpass;

      xpass.bitrate = xvidenc->bitrate;
      xpass.filename = xvidenc->filename;
      xpass.keyframe_boost = xvidenc->keyframe_boost;
      xpass.curve_compression_high = xvidenc->curve_compression_high;
      xpass.curve_compression_low = xvidenc->curve_compression_low;
      xpass.overflow_control_strength =
          MAX (0, xvidenc->overflow_control_strength);
      xpass.max_overflow_improvement =
          MAX (0, xvidenc->max_overflow_improvement);
      xpass.max_overflow_degradation =
          MAX (0, xvidenc->max_overflow_degradation);
      xpass.kfreduction = MAX (0, xvidenc->kfreduction);
      xpass.kfthreshold = MAX (0, xvidenc->kfthreshold);
      xpass.container_frame_overhead =
          MAX (0, xvidenc->container_frame_overhead);
      break;
    }
  }

  if (xvidenc->lumimasking) {
    xenc.plugins[1].func = xvid_plugin_lumimasking;
    xenc.plugins[1].param = NULL;
    xenc.num_plugins++;
  }

  if ((ret = xvid_encore (NULL, XVID_ENC_CREATE, &xenc, NULL)) < 0) {
    GST_DEBUG_OBJECT (xvidenc, "Error setting up xvid encoder: %s (%d)",
        gst_xvid_error (ret), ret);
    return FALSE;
  }

  xvidenc->handle = xenc.handle;

  return TRUE;
}

static gboolean
gst_xvidenc_setcaps (GstPad * pad, GstCaps * vscaps)
{
  GstXvidEnc *xvidenc;
  GstStructure *structure;
  gint w, h;
  const GValue *fps, *par;
  gint xvid_cs = -1;

  xvidenc = GST_XVIDENC (GST_PAD_PARENT (pad));

  /* if there's something old around, remove it */
  if (xvidenc->handle) {
    gst_xvidenc_flush_buffers (xvidenc, TRUE);
    xvid_encore (xvidenc->handle, XVID_ENC_DESTROY, NULL, NULL);
    xvidenc->handle = NULL;
  }

  structure = gst_caps_get_structure (vscaps, 0);

  if (!gst_structure_get_int (structure, "width", &w) ||
      !gst_structure_get_int (structure, "height", &h)) {
    return FALSE;
  }

  fps = gst_structure_get_value (structure, "framerate");
  if (fps == NULL || !GST_VALUE_HOLDS_FRACTION (fps)) {
    GST_WARNING_OBJECT (pad, "no framerate specified, or not a GstFraction");
    return FALSE;
  }

  /* optional par info */
  par = gst_structure_get_value (structure, "pixel-aspect-ratio");

  xvid_cs = gst_xvid_structure_to_csp (structure);
  if (xvid_cs == -1) {
    gchar *sstr;

    sstr = gst_structure_to_string (structure);
    GST_DEBUG_OBJECT (xvidenc, "Did not find xvid colourspace for caps %s",
        sstr);
    g_free (sstr);
    return FALSE;
  }

  xvidenc->csp = xvid_cs;
  xvidenc->width = w;
  xvidenc->height = h;
  xvidenc->fbase = gst_value_get_fraction_numerator (fps);
  xvidenc->fincr = gst_value_get_fraction_denominator (fps);
  if ((par != NULL) && GST_VALUE_HOLDS_FRACTION (par)) {
    xvidenc->par_width = gst_value_get_fraction_numerator (par);
    xvidenc->par_height = gst_value_get_fraction_denominator (par);
  } else {
    xvidenc->par_width = 1;
    xvidenc->par_height = 1;
  }

  /* wipe xframe cache given possible change caps properties */
  g_free (xvidenc->xframe_cache);
  xvidenc->xframe_cache = NULL;

  if (gst_xvidenc_setup (xvidenc)) {
    gboolean ret = FALSE;
    GstCaps *new_caps = NULL, *allowed_caps;

    /* please downstream with preferred caps */
    allowed_caps = gst_pad_get_allowed_caps (xvidenc->srcpad);
    GST_DEBUG_OBJECT (xvidenc, "allowed caps: %" GST_PTR_FORMAT, allowed_caps);

    if (allowed_caps && !gst_caps_is_empty (allowed_caps)) {
      new_caps = gst_caps_copy_nth (allowed_caps, 0);
    } else {
      new_caps = gst_caps_new_simple ("video/x-xvid", NULL);
    }
    if (allowed_caps)
      gst_caps_unref (allowed_caps);

    gst_caps_set_simple (new_caps,
        "width", G_TYPE_INT, w, "height", G_TYPE_INT, h,
        "framerate", GST_TYPE_FRACTION, xvidenc->fbase, xvidenc->fincr,
        "pixel-aspect-ratio", GST_TYPE_FRACTION,
        xvidenc->par_width, xvidenc->par_height, NULL);
    /* just to be sure */
    gst_pad_fixate_caps (xvidenc->srcpad, new_caps);

    if (xvidenc->used_profile != 0) {
      switch (xvidenc->used_profile) {
        case XVID_PROFILE_S_L0:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING, "simple",
              "level", G_TYPE_STRING, "0", NULL);
          break;
        case XVID_PROFILE_S_L1:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING, "simple",
              "level", G_TYPE_STRING, "1", NULL);
          break;
        case XVID_PROFILE_S_L2:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING, "simple",
              "level", G_TYPE_STRING, "2", NULL);
          break;
        case XVID_PROFILE_S_L3:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING, "simple",
              "level", G_TYPE_STRING, "3", NULL);
          break;
        case XVID_PROFILE_S_L4a:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING, "simple",
              "level", G_TYPE_STRING, "4a", NULL);
          break;
        case XVID_PROFILE_S_L5:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING, "simple",
              "level", G_TYPE_STRING, "5", NULL);
          break;
        case XVID_PROFILE_S_L6:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING, "simple",
              "level", G_TYPE_STRING, "6", NULL);
          break;
        case XVID_PROFILE_ARTS_L1:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING,
              "advanced-real-time-simple", "level", G_TYPE_STRING, "1", NULL);
          break;
        case XVID_PROFILE_ARTS_L2:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING,
              "advanced-real-time-simple", "level", G_TYPE_STRING, "2", NULL);
          break;
        case XVID_PROFILE_ARTS_L3:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING,
              "advanced-real-time-simple", "level", G_TYPE_STRING, "3", NULL);
          break;
        case XVID_PROFILE_ARTS_L4:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING,
              "advanced-real-time-simple", "level", G_TYPE_STRING, "4", NULL);
          break;
        case XVID_PROFILE_AS_L0:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING,
              "advanced-simple", "level", G_TYPE_STRING, "0", NULL);
          break;
        case XVID_PROFILE_AS_L1:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING,
              "advanced-simple", "level", G_TYPE_STRING, "1", NULL);
          break;
        case XVID_PROFILE_AS_L2:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING,
              "advanced-simple", "level", G_TYPE_STRING, "2", NULL);
          break;
        case XVID_PROFILE_AS_L3:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING,
              "advanced-simple", "level", G_TYPE_STRING, "3", NULL);
          break;
        case XVID_PROFILE_AS_L4:
          gst_caps_set_simple (new_caps, "profile", G_TYPE_STRING,
              "advanced-simple", "level", G_TYPE_STRING, "4", NULL);
          break;
        default:
          g_assert_not_reached ();
          break;
      }
    }

    /* src pad should accept anyway */
    ret = gst_pad_set_caps (xvidenc->srcpad, new_caps);
    gst_caps_unref (new_caps);

    if (!ret && xvidenc->handle) {
      xvid_encore (xvidenc->handle, XVID_ENC_DESTROY, NULL, NULL);
      xvidenc->handle = NULL;
    }
    return ret;

  } else                        /* setup did not work out */
    return FALSE;
}

static GstCaps *
gst_xvidenc_getcaps (GstPad * pad)
{
  GstXvidEnc *xvidenc;
  GstPad *peer;
  GstCaps *caps;

  /* If we already have caps return them */
  if (GST_PAD_CAPS (pad))
    return gst_caps_ref (GST_PAD_CAPS (pad));

  xvidenc = GST_XVIDENC (gst_pad_get_parent (pad));
  if (!xvidenc)
    return gst_caps_new_empty ();

  peer = gst_pad_get_peer (xvidenc->srcpad);
  if (peer) {
    const GstCaps *templcaps;
    GstCaps *peercaps;
    guint i, n;

    peercaps = gst_pad_get_caps (peer);

    /* Translate peercaps to YUV */
    peercaps = gst_caps_make_writable (peercaps);
    n = gst_caps_get_size (peercaps);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (peercaps, i);

      gst_structure_set_name (s, "video/x-raw-yuv");
      gst_structure_remove_field (s, "mpegversion");
      gst_structure_remove_field (s, "systemstream");
    }

    templcaps = gst_pad_get_pad_template_caps (pad);

    caps = gst_caps_intersect (peercaps, templcaps);
    gst_caps_unref (peercaps);
    gst_object_unref (peer);
    peer = NULL;
  } else {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  gst_object_unref (xvidenc);

  return caps;
}

/* encodes frame according to info in xframe;
   - buf is input buffer, can be NULL if dummy
   - buf is disposed of prior to exit
   - resulting buffer is returned, NULL if no encoder output or error
*/
static inline GstBuffer *
gst_xvidenc_encode (GstXvidEnc * xvidenc, GstBuffer * buf,
    xvid_enc_frame_t xframe)
{
  GstBuffer *outbuf;
  gint ret;

  /* compressed frame should fit in the rough size of an uncompressed one */
  outbuf = gst_buffer_new_and_alloc (gst_xvid_image_get_size (xvidenc->csp,
          xvidenc->width, xvidenc->height));

  xframe.bitstream = (void *) GST_BUFFER_DATA (outbuf);
  xframe.length = GST_BUFFER_SIZE (outbuf);

  /* now provide input image data where-abouts, if needed */
  if (buf)
    gst_xvid_image_fill (&xframe.input, GST_BUFFER_DATA (buf), xvidenc->csp,
        xvidenc->width, xvidenc->height);

  GST_DEBUG_OBJECT (xvidenc, "encoding frame into buffer of size %d",
      GST_BUFFER_SIZE (outbuf));
  ret = xvid_encore (xvidenc->handle, XVID_ENC_ENCODE, &xframe, NULL);

  if (ret < 0) {
    /* things can be nasty if we are trying to flush, so don't signal error then */
    if (buf) {
      GST_ELEMENT_WARNING (xvidenc, LIBRARY, ENCODE, (NULL),
          ("Error encoding xvid frame: %s (%d)", gst_xvid_error (ret), ret));
      gst_buffer_unref (buf);
    }
    gst_buffer_unref (outbuf);
    return NULL;
  } else if (ret > 0) {         /* make sub-buffer */
    GstBuffer *sub;

    GST_DEBUG_OBJECT (xvidenc, "xvid produced output of size %d", ret);
    sub = gst_buffer_create_sub (outbuf, 0, ret);

    /* parent no longer needed, will go away with child buffer */
    gst_buffer_unref (outbuf);
    outbuf = sub;
  } else {                      /* encoder did not yet produce something */
    GST_DEBUG_OBJECT (xvidenc, "xvid produced no output");
    gst_buffer_unref (outbuf);
    g_queue_push_tail (xvidenc->delay, buf);
    return NULL;
  }

  /* finish decoration and return */
  if (!(xframe.out_flags & XVID_KEYFRAME))
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (xvidenc->srcpad));

  /* now we need the right buf to take timestamps from;
     note that timestamps from a display order input buffer can end up with
     another encode order output buffer, but other than this permutation,
     the overall time progress is tracked,
     and keyframes should have the correct stamp */
  if (!g_queue_is_empty (xvidenc->delay)) {
    if (buf)
      g_queue_push_tail (xvidenc->delay, buf);
    buf = g_queue_pop_head (xvidenc->delay);
  }
  if (buf) {
    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);
    gst_buffer_unref (buf);
  }

  return outbuf;
}

static GstFlowReturn
gst_xvidenc_chain (GstPad * pad, GstBuffer * buf)
{
  GstXvidEnc *xvidenc = GST_XVIDENC (GST_PAD_PARENT (pad));
  GstBuffer *outbuf;
  xvid_enc_frame_t xframe;

  const gint motion_presets[] = {
    0, 0, 0, 0,
    XVID_ME_HALFPELREFINE16,
    XVID_ME_HALFPELREFINE16 | XVID_ME_ADVANCEDDIAMOND16,
    XVID_ME_HALFPELREFINE16 | XVID_ME_EXTSEARCH16
        | XVID_ME_HALFPELREFINE8 | XVID_ME_USESQUARES16
  };

  if (!xvidenc->handle) {
    GST_ELEMENT_ERROR (xvidenc, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before chain function"));
    gst_buffer_unref (buf);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  GST_DEBUG_OBJECT (xvidenc,
      "Received buffer of time %" GST_TIME_FORMAT ", size %d",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), GST_BUFFER_SIZE (buf));

  if (xvidenc->xframe_cache)
    memcpy (&xframe, xvidenc->xframe_cache, sizeof (xframe));
  else {                        /* need to build some inital xframe to be cached */
    /* encode and so ... */
    gst_xvid_init_struct (xframe);

    if (xvidenc->par_width == xvidenc->par_height)
      xframe.par = XVID_PAR_11_VGA;
    else {
      xframe.par = XVID_PAR_EXT;
      xframe.par_width = xvidenc->par_width;
      xframe.par_height = xvidenc->par_height;
    }

    /* handle options */
    xframe.vol_flags |= xvidenc->quant_type;
    xframe.vop_flags = XVID_VOP_HALFPEL;
    xframe.motion = motion_presets[xvidenc->motion];

    if (xvidenc->me_chroma) {
      xframe.motion |= XVID_ME_CHROMA_PVOP;
      xframe.motion |= XVID_ME_CHROMA_BVOP;
    }

    if (xvidenc->me_vhq >= 1) {
      xframe.vop_flags |= XVID_VOP_MODEDECISION_RD;
    }
    if (xvidenc->me_vhq >= 2) {
      xframe.motion |= XVID_ME_HALFPELREFINE16_RD;
      xframe.motion |= XVID_ME_QUARTERPELREFINE16_RD;
    }
    if (xvidenc->me_vhq >= 3) {
      xframe.motion |= XVID_ME_HALFPELREFINE8_RD;
      xframe.motion |= XVID_ME_QUARTERPELREFINE8_RD;
      xframe.motion |= XVID_ME_CHECKPREDICTION_RD;
    }
    if (xvidenc->me_vhq >= 4) {
      xframe.motion |= XVID_ME_EXTSEARCH_RD;
    }

    /* no motion estimation, then only intra */
    if (xvidenc->motion == 0) {
      xframe.type = XVID_TYPE_IVOP;
    } else {
      xframe.type = XVID_TYPE_AUTO;
    }

    if (xvidenc->motion > 4) {
      xframe.vop_flags |= XVID_VOP_INTER4V;
    }

    if (xvidenc->me_quarterpel) {
      xframe.vol_flags |= XVID_VOL_QUARTERPEL;
      xframe.motion |= XVID_ME_QUARTERPELREFINE16;
      xframe.motion |= XVID_ME_QUARTERPELREFINE8;
    }

    if (xvidenc->gmc) {
      xframe.vol_flags |= XVID_VOL_GMC;
      xframe.motion |= XVID_ME_GME_REFINE;
    }

    if (xvidenc->interlaced) {
      xframe.vol_flags |= XVID_VOL_INTERLACING;
    }

    if (xvidenc->trellis) {
      xframe.vop_flags |= XVID_VOP_TRELLISQUANT;
    }

    if (xvidenc->hqacpred) {
      xframe.vop_flags |= XVID_VOP_HQACPRED;
    }

    if (xvidenc->greyscale) {
      xframe.vop_flags |= XVID_VOP_GREYSCALE;
    }

    if (xvidenc->cartoon) {
      xframe.vop_flags |= XVID_VOP_CARTOON;
      xframe.motion |= XVID_ME_DETECT_STATIC_MOTION;
    }

    xframe.bframe_threshold = xvidenc->bframe_threshold;
    xframe.input.csp = xvidenc->csp;

    /* save in cache */
    xvidenc->xframe_cache = g_memdup (&xframe, sizeof (xframe));
  }

  outbuf = gst_xvidenc_encode (xvidenc, buf, xframe);

  if (!outbuf)                  /* error or no data yet */
    return GST_FLOW_OK;

  /* go out, multiply! */
  return gst_pad_push (xvidenc->srcpad, outbuf);
}

/* flush xvid encoder buffers caused by bframe usage */
static void
gst_xvidenc_flush_buffers (GstXvidEnc * xvidenc, gboolean send)
{
  GstBuffer *outbuf;
  xvid_enc_frame_t xframe;

  /* no need to flush if no handle */
  if (!xvidenc->handle)
    return;

  gst_xvid_init_struct (xframe);

  /* init a fake frame to force flushing */
  xframe.input.csp = XVID_CSP_NULL;
  xframe.input.plane[0] = NULL;
  xframe.input.plane[1] = NULL;
  xframe.input.plane[2] = NULL;
  xframe.input.stride[0] = 0;
  xframe.input.stride[1] = 0;
  xframe.input.stride[2] = 0;
  xframe.quant = 0;

  GST_DEBUG ("flushing buffers with sending %d", send);

  while (!g_queue_is_empty (xvidenc->delay)) {
    outbuf = gst_xvidenc_encode (xvidenc, NULL, xframe);

    if (outbuf) {
      if (send)
        gst_pad_push (xvidenc->srcpad, outbuf);
      else
        gst_buffer_unref (outbuf);
    } else                      /* hm, there should have been something in there */
      break;
  }

  /* our queue should be empty anyway if we did not have to break out ... */
  while (!g_queue_is_empty (xvidenc->delay))
    gst_buffer_unref (g_queue_pop_head (xvidenc->delay));
}

static void
gst_xvidenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstXvidEnc *xvidenc;
  guint offset;

  xvidenc = GST_XVIDENC (object);

  if (prop_id > xvidenc_prop_count) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    return;
  }

  /* our param specs should have such qdata */
  offset =
      GPOINTER_TO_UINT (g_param_spec_get_qdata (pspec, xvidenc_pspec_quark));

  if (offset == 0)
    return;

  switch (G_PARAM_SPEC_VALUE_TYPE (pspec)) {
    case G_TYPE_BOOLEAN:
      G_STRUCT_MEMBER (gboolean, xvidenc, offset) = g_value_get_boolean (value);
      break;
    case G_TYPE_INT:
      G_STRUCT_MEMBER (gint, xvidenc, offset) = g_value_get_int (value);
      break;
    case G_TYPE_STRING:
      g_free (G_STRUCT_MEMBER (gchar *, xvidenc, offset));
      G_STRUCT_MEMBER (gchar *, xvidenc, offset) = g_value_dup_string (value);
      break;
    default:                   /* must be enum, given the check above */
      if (G_IS_PARAM_SPEC_ENUM (pspec)) {
        G_STRUCT_MEMBER (gint, xvidenc, offset) = g_value_get_enum (value);
      } else {
        G_STRUCT_MEMBER (guint, xvidenc, offset) = g_value_get_flags (value);
      }
      break;
  }
}

static void
gst_xvidenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstXvidEnc *xvidenc;
  guint offset;

  xvidenc = GST_XVIDENC (object);

  if (prop_id > xvidenc_prop_count) {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    return;
  }

  /* our param specs should have such qdata */
  offset =
      GPOINTER_TO_UINT (g_param_spec_get_qdata (pspec, xvidenc_pspec_quark));

  if (offset == 0)
    return;

  switch (G_PARAM_SPEC_VALUE_TYPE (pspec)) {
    case G_TYPE_BOOLEAN:
      g_value_set_boolean (value, G_STRUCT_MEMBER (gboolean, xvidenc, offset));
      break;
    case G_TYPE_INT:
      g_value_set_int (value, G_STRUCT_MEMBER (gint, xvidenc, offset));
      break;
    case G_TYPE_STRING:
      g_value_take_string (value,
          g_strdup (G_STRUCT_MEMBER (gchar *, xvidenc, offset)));
      break;
    default:                   /* must be enum, given the check above */
      if (G_IS_PARAM_SPEC_ENUM (pspec)) {
        g_value_set_enum (value, G_STRUCT_MEMBER (gint, xvidenc, offset));
      } else if (G_IS_PARAM_SPEC_FLAGS (pspec)) {
        g_value_set_flags (value, G_STRUCT_MEMBER (guint, xvidenc, offset));
      } else {                  /* oops, bit lazy we don't cover this case yet */
        g_critical ("%s does not yet support type %s", GST_FUNCTION,
            g_type_name (G_PARAM_SPEC_VALUE_TYPE (pspec)));
      }
      break;
  }
}

static GstStateChangeReturn
gst_xvidenc_change_state (GstElement * element, GstStateChange transition)
{
  GstXvidEnc *xvidenc = GST_XVIDENC (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_xvid_init ())
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      xvidenc->delay = g_queue_new ();
      break;
    default:
      break;
  }


  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (xvidenc->handle) {
        gst_xvidenc_flush_buffers (xvidenc, FALSE);
        xvid_encore (xvidenc->handle, XVID_ENC_DESTROY, NULL, NULL);
        xvidenc->handle = NULL;
      }
      g_queue_free (xvidenc->delay);
      xvidenc->delay = NULL;
      g_free (xvidenc->xframe_cache);
      xvidenc->xframe_cache = NULL;
      break;
    default:
      break;
  }

done:
  return ret;
}
