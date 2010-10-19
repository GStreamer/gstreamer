/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Wim Taymans <wim@fluendo.com>
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
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

/**
 * SECTION:element-lame
 * @see_also: lamemp3enc, mad, vorbisenc
 *
 * This element encodes raw integer audio into an MPEG-1 layer 3 (MP3) stream.
 * Note that <ulink url="http://en.wikipedia.org/wiki/MP3">MP3</ulink> is not
 * a free format, there are licensing and patent issues to take into
 * consideration. See <ulink url="http://www.vorbis.com/">Ogg/Vorbis</ulink>
 * for a royalty free (and often higher quality) alternative.
 *
 * <refsect2>
 * <title>Note</title>
 * This element is deprecated, use the lamemp3enc element instead
 * which provides a much simpler interface and results in better MP3 files.
 * </refsect2>
 *
 * <refsect2>
 * <title>Output sample rate</title>
 * If no fixed output sample rate is negotiated on the element's src pad,
 * the element will choose an optimal sample rate to resample to internally.
 * For example, a 16-bit 44.1 KHz mono audio stream encoded at 48 kbit will
 * get resampled to 32 KHz.  Use filter caps on the src pad to force a
 * particular sample rate.
 * </refsect2>
 * <refsect2>
 * <title>Writing metadata (tags)</title>
 * Whilst the lame encoder element does claim to implement the GstTagSetter
 * interface, it does so only for backwards compatibility reasons. Tag writing
 * has been removed from lame. Use external elements like id3v2mux or apev2mux
 * to add tags to your MP3 streams. The same goes for XING headers: use the
 * xingmux element to add XING headers to your VBR mp3 file.
 * </refsect2>
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v audiotestsrc wave=sine num-buffers=100 ! audioconvert ! lame ! filesink location=sine.mp3
 * ]| Encode a test sine signal to MP3.
 * |[
 * gst-launch -v alsasrc ! audioconvert ! lame bitrate=192 ! filesink location=alsasrc.mp3
 * ]| Record from a sound card using ALSA and encode to MP3
 * |[
 * gst-launch -v filesrc location=music.wav ! decodebin ! audioconvert ! audioresample ! lame bitrate=192 ! id3v2mux ! filesink location=music.mp3
 * ]| Transcode from a .wav file to MP3 (the id3v2mux element is optional)
 * |[
 * gst-launch -v cdda://5 ! audioconvert ! lame bitrate=192 ! filesink location=track5.mp3
 * ]| Encode Audio CD track 5 to MP3
 * |[
 * gst-launch -v audiotestsrc num-buffers=10 ! audio/x-raw-int,rate=44100,channels=1 ! lame bitrate=48 mode=3 ! filesink location=test.mp3
 * ]| Encode to a fixed sample rate
 * </refsect2>
 *
 * Last reviewed on 2007-07-24 (0.10.7)
 */

/* FIXME 0.11: Remove all properties except the useful ones. Nobody knows what most
 * properties are doing and they're intended for LAME developers only.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "string.h"
#include "gstlame.h"
#include "gst/gst-i18n-plugin.h"

#ifdef lame_set_preset
#define GST_LAME_PRESET
#endif

GST_DEBUG_CATEGORY_STATIC (debug);
#define GST_CAT_DEFAULT debug

/* LAME can do MPEG-1, MPEG-2, and MPEG-2.5, so it has 9 possible
 * sample rates it supports */
static GstStaticPadTemplate gst_lame_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ]")
    );

static GstStaticPadTemplate gst_lame_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) 3, "
        "rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ]")
    );

static struct
{
  gint bitrate;
  gfloat compression_ratio;
  gint quality;
  gint mode;
  gboolean force_ms;
  gboolean free_format;
  gboolean copyright;
  gboolean original;
  gboolean error_protection;
  gboolean extension;
  gboolean strict_iso;
  gboolean disable_reservoir;
  gint vbr;
  gint vbr_quality;
  gint vbr_mean_bitrate;
  gint vbr_min_bitrate;
  gint vbr_max_bitrate;
  gint vbr_hard_min;
  gint lowpass_freq;
  gint lowpass_width;
  gint highpass_freq;
  gint highpass_width;
  gboolean ath_only;
  gboolean ath_short;
  gboolean no_ath;
  gint ath_type;
  gint ath_lower;
  gboolean allow_diff_short;
  gboolean no_short_blocks;
  gboolean emphasis;
  gint preset;
} gst_lame_default_settings;

/********** Define useful types for non-programmatic interfaces **********/
#define GST_TYPE_LAME_MODE (gst_lame_mode_get_type())
static GType
gst_lame_mode_get_type (void)
{
  static GType lame_mode_type = 0;
  static GEnumValue lame_modes[] = {
    {0, "Stereo", "stereo"},
    {1, "Joint Stereo", "joint"},
    {2, "Dual Channel", "dual"},
    {3, "Mono", "mono"},
    {4, "Auto", "auto"},
    {0, NULL, NULL}
  };

  if (!lame_mode_type) {
    lame_mode_type = g_enum_register_static ("GstLameMode", lame_modes);
  }
  return lame_mode_type;
}

#define GST_TYPE_LAME_QUALITY (gst_lame_quality_get_type())
static GType
gst_lame_quality_get_type (void)
{
  static GType lame_quality_type = 0;
  static GEnumValue lame_quality[] = {
    {0, "0 - Best", "0"},
    {1, "1", "1"},
    {2, "2", "2"},
    {3, "3", "3"},
    {4, "4", "4"},
    {5, "5 - Default", "5"},
    {6, "6", "6"},
    {7, "7", "7"},
    {8, "8", "8"},
    {9, "9 - Worst", "9"},
    {0, NULL, NULL}
  };

  if (!lame_quality_type) {
    lame_quality_type = g_enum_register_static ("GstLameQuality", lame_quality);
  }
  return lame_quality_type;
}

#define GST_TYPE_LAME_PADDING (gst_lame_padding_get_type())
static GType
gst_lame_padding_get_type (void)
{
  static GType lame_padding_type = 0;
  static GEnumValue lame_padding[] = {
    {0, "No Padding", "never"},
    {1, "Always Pad", "always"},
    {2, "Adjust Padding", "adjust"},
    {0, NULL, NULL}
  };

  if (!lame_padding_type) {
    lame_padding_type = g_enum_register_static ("GstLamePadding", lame_padding);
  }
  return lame_padding_type;
}

#define GST_TYPE_LAME_VBRMODE (gst_lame_vbrmode_get_type())
static GType
gst_lame_vbrmode_get_type (void)
{
  static GType lame_vbrmode_type = 0;
  static GEnumValue lame_vbrmode[] = {
    {vbr_off, "No VBR (Constant Bitrate)", "none"},
    {vbr_rh, "Lame's old VBR algorithm", "old"},
    {vbr_abr, "VBR Average Bitrate", "abr"},
    {vbr_mtrh, "Lame's new VBR algorithm", "new"},
    {0, NULL, NULL}
  };

  if (!lame_vbrmode_type) {
    lame_vbrmode_type = g_enum_register_static ("GstLameVbrmode", lame_vbrmode);
  }

  return lame_vbrmode_type;
}

#ifdef GSTLAME_PRESET
#define GST_TYPE_LAME_PRESET (gst_lame_preset_get_type())
static GType
gst_lame_preset_get_type (void)
{
  static GType gst_lame_preset = 0;
  static GEnumValue gst_lame_presets[] = {
    {0, "None", "none"},
    {MEDIUM, "Medium", "medium"},
    {STANDARD, "Standard", "standard"},
    {EXTREME, "Extreme", "extreme"},
    {INSANE, "Insane", "insane"},
    {0, NULL, NULL}
  };

  if (!gst_lame_preset) {
    gst_lame_preset =
        g_enum_register_static ("GstLamePreset", gst_lame_presets);
  }

  return gst_lame_preset;
}
#endif

/********** Standard stuff for signals and arguments **********/

enum
{
  ARG_0,
  ARG_BITRATE,
  ARG_COMPRESSION_RATIO,
  ARG_QUALITY,
  ARG_MODE,
  ARG_FORCE_MS,
  ARG_FREE_FORMAT,
  ARG_COPYRIGHT,
  ARG_ORIGINAL,
  ARG_ERROR_PROTECTION,
  ARG_PADDING_TYPE,             /* FIXME: remove in 0.11 */
  ARG_EXTENSION,
  ARG_STRICT_ISO,
  ARG_DISABLE_RESERVOIR,
  ARG_VBR,
  ARG_VBR_MEAN_BITRATE,
  ARG_VBR_MIN_BITRATE,
  ARG_VBR_MAX_BITRATE,
  ARG_VBR_HARD_MIN,
  ARG_LOWPASS_FREQ,
  ARG_LOWPASS_WIDTH,
  ARG_HIGHPASS_FREQ,
  ARG_HIGHPASS_WIDTH,
  ARG_ATH_ONLY,
  ARG_ATH_SHORT,
  ARG_NO_ATH,
  ARG_ATH_LOWER,
  ARG_CWLIMIT,                  /* FIXME: remove in 0.11 */
  ARG_ALLOW_DIFF_SHORT,
  ARG_NO_SHORT_BLOCKS,
  ARG_EMPHASIS,
  ARG_VBR_QUALITY,
#ifdef GSTLAME_PRESET
  ARG_XINGHEADER,               /* FIXME: remove in 0.11 */
  ARG_PRESET
#else
  ARG_XINGHEADER                /* FIXME: remove in 0.11 */
#endif
};

static void gst_lame_base_init (gpointer g_class);
static void gst_lame_class_init (GstLameClass * klass);
static void gst_lame_init (GstLame * gst_lame);

static void gst_lame_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_lame_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_lame_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_lame_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_lame_setup (GstLame * lame);
static GstStateChangeReturn gst_lame_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

GType
gst_lame_get_type (void)
{
  static GType gst_lame_type = 0;

  if (!gst_lame_type) {
    static const GTypeInfo gst_lame_info = {
      sizeof (GstLameClass),
      gst_lame_base_init,
      NULL,
      (GClassInitFunc) gst_lame_class_init,
      NULL,
      NULL,
      sizeof (GstLame),
      0,
      (GInstanceInitFunc) gst_lame_init,
    };

    /* FIXME: remove support for the GstTagSetter interface in 0.11 */
    static const GInterfaceInfo tag_setter_info = {
      NULL,
      NULL,
      NULL
    };
    static const GInterfaceInfo preset_info = {
      NULL,
      NULL,
      NULL
    };

    gst_lame_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstLame", &gst_lame_info, 0);
    g_type_add_interface_static (gst_lame_type, GST_TYPE_TAG_SETTER,
        &tag_setter_info);
    g_type_add_interface_static (gst_lame_type, GST_TYPE_PRESET, &preset_info);
  }
  return gst_lame_type;
}

static void
gst_lame_release_memory (GstLame * lame)
{
  if (lame->lgf) {
    lame_close (lame->lgf);
    lame->lgf = NULL;
  }
}

static void
gst_lame_finalize (GObject * obj)
{
  gst_lame_release_memory (GST_LAME (obj));

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_lame_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_lame_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_lame_sink_template));
  gst_element_class_set_details_simple (element_class, "L.A.M.E. mp3 encoder",
      "Codec/Encoder/Audio",
      "High-quality free MP3 encoder (deprecated)",
      "Erik Walthinsen <omega@cse.ogi.edu>, " "Wim Taymans <wim@fluendo.com>");
}

static void
gst_lame_class_init (GstLameClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_lame_set_property;
  gobject_class->get_property = gst_lame_get_property;
  gobject_class->finalize = gst_lame_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE,
      g_param_spec_int ("bitrate", "Bitrate (kb/s)",
          "Bitrate in kbit/sec (8, 16, 24, 32, 40, 48, 56, 64, 80, 96, "
          "112, 128, 160, 192, 224, 256 or 320)",
          0, 320, gst_lame_default_settings.bitrate,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /* compression ratio set to 0.0 by default otherwise it overrides the bitrate setting */
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      ARG_COMPRESSION_RATIO, g_param_spec_float ("compression-ratio",
          "Compression Ratio",
          "let lame choose bitrate to achieve selected compression ratio", 0.0,
          200.0, gst_lame_default_settings.compression_ratio,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUALITY,
      g_param_spec_enum ("quality", "Quality",
          "Quality of algorithm used for encoding", GST_TYPE_LAME_QUALITY,
          gst_lame_default_settings.quality,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MODE,
      g_param_spec_enum ("mode", "Mode", "Encoding mode", GST_TYPE_LAME_MODE,
          gst_lame_default_settings.mode,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FORCE_MS,
      g_param_spec_boolean ("force-ms", "Force ms",
          "Force ms_stereo on all frames", gst_lame_default_settings.force_ms,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FREE_FORMAT,
      g_param_spec_boolean ("free-format", "Free format",
          "Produce a free format bitstream",
          gst_lame_default_settings.free_format,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_COPYRIGHT,
      g_param_spec_boolean ("copyright", "Copyright", "Mark as copyright",
          gst_lame_default_settings.copyright,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ORIGINAL,
      g_param_spec_boolean ("original", "Original", "Mark as original",
          gst_lame_default_settings.original,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ERROR_PROTECTION,
      g_param_spec_boolean ("error-protection", "Error protection",
          "Adds 16 bit checksum to every frame",
          gst_lame_default_settings.error_protection,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PADDING_TYPE,
      g_param_spec_enum ("padding-type", "Padding type",
          "Padding type " "(DEPRECATED: this setting has no effect)",
          GST_TYPE_LAME_PADDING, FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_EXTENSION,
      g_param_spec_boolean ("extension", "Extension", "Extension",
          gst_lame_default_settings.extension,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_STRICT_ISO,
      g_param_spec_boolean ("strict-iso", "Strict ISO",
          "Comply as much as possible to ISO MPEG spec", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      ARG_DISABLE_RESERVOIR, g_param_spec_boolean ("disable-reservoir",
          "Disable reservoir", "Disable the bit reservoir", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR,
      g_param_spec_enum ("vbr", "VBR", "Specify bitrate mode",
          GST_TYPE_LAME_VBRMODE, gst_lame_default_settings.vbr,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR_QUALITY,
      g_param_spec_enum ("vbr-quality", "VBR Quality", "VBR Quality",
          GST_TYPE_LAME_QUALITY, gst_lame_default_settings.vbr_quality,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR_MEAN_BITRATE,
      g_param_spec_int ("vbr-mean-bitrate", "VBR mean bitrate",
          "Specify mean VBR bitrate", 0, 320,
          gst_lame_default_settings.vbr_mean_bitrate,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR_MIN_BITRATE,
      g_param_spec_int ("vbr-min-bitrate", "VBR min bitrate",
          "Specify minimum VBR bitrate (8, 16, 24, 32, 40, 48, 56, 64, 80, 96, "
          "112, 128, 160, 192, 224, 256 or 320)", 0, 320,
          gst_lame_default_settings.vbr_min_bitrate,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR_MAX_BITRATE,
      g_param_spec_int ("vbr-max-bitrate", "VBR max bitrate",
          "Specify maximum VBR bitrate (8, 16, 24, 32, 40, 48, 56, 64, 80, 96, "
          "112, 128, 160, 192, 224, 256 or 320)", 0, 320,
          gst_lame_default_settings.vbr_max_bitrate,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR_HARD_MIN,
      g_param_spec_int ("vbr-hard-min", "VBR hard min",
          "Specify whether min VBR bitrate is a hard limit. Normally, "
          "it can be violated for silence", 0, 1,
          gst_lame_default_settings.vbr_hard_min,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOWPASS_FREQ,
      g_param_spec_int ("lowpass-freq", "Lowpass freq",
          "frequency(kHz), lowpass filter cutoff above freq", 0, 50000,
          gst_lame_default_settings.lowpass_freq,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOWPASS_WIDTH,
      g_param_spec_int ("lowpass-width", "Lowpass width",
          "frequency(kHz) - default 15% of lowpass freq", -1, G_MAXINT,
          gst_lame_default_settings.lowpass_width,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HIGHPASS_FREQ,
      g_param_spec_int ("highpass-freq", "Highpass freq",
          "frequency(kHz), highpass filter cutoff below freq", 0, 50000,
          gst_lame_default_settings.highpass_freq,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HIGHPASS_WIDTH,
      g_param_spec_int ("highpass-width", "Highpass width",
          "frequency(kHz) - default 15% of highpass freq", -1, G_MAXINT,
          gst_lame_default_settings.highpass_width,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ATH_ONLY,
      g_param_spec_boolean ("ath-only", "ATH only",
          "Ignore GPSYCHO completely, use ATH only",
          gst_lame_default_settings.ath_only,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ATH_SHORT,
      g_param_spec_boolean ("ath-short", "ATH short",
          "Ignore GPSYCHO for short blocks, use ATH only",
          gst_lame_default_settings.ath_short,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NO_ATH,
      g_param_spec_boolean ("no-ath", "No ath",
          "turns ATH down to a flat noise floor",
          gst_lame_default_settings.no_ath,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ATH_LOWER,
      g_param_spec_int ("ath-lower", "ATH lower", "lowers ATH by x dB",
          G_MININT, G_MAXINT, gst_lame_default_settings.ath_lower,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CWLIMIT,
      g_param_spec_int ("cwlimit", "Cwlimit",
          "Compute tonality up to freq (in kHz) default 8.8717 "
          "(DEPRECATED: this setting has no effect)", 0, 50000, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ALLOW_DIFF_SHORT,
      g_param_spec_boolean ("allow-diff-short", "Allow diff short",
          "Allow diff short", gst_lame_default_settings.allow_diff_short,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NO_SHORT_BLOCKS,
      g_param_spec_boolean ("no-short-blocks", "No short blocks",
          "Do not use short blocks", gst_lame_default_settings.no_short_blocks,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_EMPHASIS,
      g_param_spec_boolean ("emphasis", "Emphasis", "Emphasis",
          gst_lame_default_settings.emphasis,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_XINGHEADER,
      g_param_spec_boolean ("xingheader", "Output Xing Header",
          "Output Xing Header (BROKEN, use xingmux instead)", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#ifdef GSTLAME_PRESET
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PRESET,
      g_param_spec_enum ("preset", "Lame Preset", "Lame Preset",
          GST_TYPE_LAME_PRESET, gst_lame_default_settings.preset,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_lame_change_state);
}

static gboolean
gst_lame_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GST_DEBUG_OBJECT (pad, "caps: %" GST_PTR_FORMAT, caps);
  return TRUE;
}

static gboolean
gst_lame_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstLame *lame;
  gint out_samplerate;
  gint version;
  GstStructure *structure;
  GstCaps *othercaps;

  lame = GST_LAME (GST_PAD_PARENT (pad));
  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rate", &lame->samplerate))
    goto no_rate;
  if (!gst_structure_get_int (structure, "channels", &lame->num_channels))
    goto no_channels;

  GST_DEBUG_OBJECT (lame, "setting up lame");
  if (!gst_lame_setup (lame))
    goto setup_failed;


  out_samplerate = lame_get_out_samplerate (lame->lgf);
  if (out_samplerate == 0)
    goto zero_output_rate;
  if (out_samplerate != lame->samplerate) {
    GST_WARNING_OBJECT (lame,
        "output samplerate %d is different from incoming samplerate %d",
        out_samplerate, lame->samplerate);
  }

  version = lame_get_version (lame->lgf);
  if (version == 0)
    version = 2;
  else if (version == 1)
    version = 1;
  else if (version == 2)
    version = 3;

  othercaps =
      gst_caps_new_simple ("audio/mpeg",
      "mpegversion", G_TYPE_INT, 1,
      "mpegaudioversion", G_TYPE_INT, version,
      "layer", G_TYPE_INT, 3,
      "channels", G_TYPE_INT, lame->mode == MONO ? 1 : lame->num_channels,
      "rate", G_TYPE_INT, out_samplerate, NULL);

  /* and use these caps */
  gst_pad_set_caps (lame->srcpad, othercaps);
  gst_caps_unref (othercaps);

  return TRUE;

no_rate:
  {
    GST_ERROR_OBJECT (lame, "input caps have no sample rate field");
    return FALSE;
  }
no_channels:
  {
    GST_ERROR_OBJECT (lame, "input caps have no channels field");
    return FALSE;
  }
zero_output_rate:
  {
    GST_ELEMENT_ERROR (lame, LIBRARY, SETTINGS, (NULL),
        ("LAME decided on a zero sample rate"));
    return FALSE;
  }
setup_failed:
  {
    GST_ELEMENT_ERROR (lame, LIBRARY, SETTINGS,
        (_("Failed to configure LAME encoder. Check your encoding parameters.")), (NULL));
    return FALSE;
  }
}

static void
gst_lame_init (GstLame * lame)
{
  GST_DEBUG_OBJECT (lame, "starting initialization");

  lame->sinkpad =
      gst_pad_new_from_static_template (&gst_lame_sink_template, "sink");
  gst_pad_set_event_function (lame->sinkpad,
      GST_DEBUG_FUNCPTR (gst_lame_sink_event));
  gst_pad_set_chain_function (lame->sinkpad,
      GST_DEBUG_FUNCPTR (gst_lame_chain));
  gst_pad_set_setcaps_function (lame->sinkpad,
      GST_DEBUG_FUNCPTR (gst_lame_sink_setcaps));
  gst_element_add_pad (GST_ELEMENT (lame), lame->sinkpad);

  lame->srcpad =
      gst_pad_new_from_static_template (&gst_lame_src_template, "src");
  gst_pad_set_setcaps_function (lame->srcpad,
      GST_DEBUG_FUNCPTR (gst_lame_src_setcaps));
  gst_element_add_pad (GST_ELEMENT (lame), lame->srcpad);

  lame->samplerate = 44100;
  lame->num_channels = 2;
  lame->setup = FALSE;

  /* Set default settings */
  lame->bitrate = gst_lame_default_settings.bitrate;
  lame->compression_ratio = gst_lame_default_settings.compression_ratio;
  lame->quality = gst_lame_default_settings.quality;
  lame->mode = gst_lame_default_settings.mode;
  lame->requested_mode = lame->mode;
  lame->force_ms = gst_lame_default_settings.force_ms;
  lame->free_format = gst_lame_default_settings.free_format;
  lame->copyright = gst_lame_default_settings.copyright;
  lame->original = gst_lame_default_settings.original;
  lame->error_protection = gst_lame_default_settings.error_protection;
  lame->extension = gst_lame_default_settings.extension;
  lame->strict_iso = gst_lame_default_settings.strict_iso;
  lame->disable_reservoir = gst_lame_default_settings.disable_reservoir;
  lame->vbr = gst_lame_default_settings.vbr;
  lame->vbr_quality = gst_lame_default_settings.vbr_quality;
  lame->vbr_mean_bitrate = gst_lame_default_settings.vbr_mean_bitrate;
  lame->vbr_min_bitrate = gst_lame_default_settings.vbr_min_bitrate;
  lame->vbr_max_bitrate = gst_lame_default_settings.vbr_max_bitrate;
  lame->vbr_hard_min = gst_lame_default_settings.vbr_hard_min;
  lame->lowpass_freq = gst_lame_default_settings.lowpass_freq;
  lame->lowpass_width = gst_lame_default_settings.lowpass_width;
  lame->highpass_freq = gst_lame_default_settings.highpass_freq;
  lame->highpass_width = gst_lame_default_settings.highpass_width;
  lame->ath_only = gst_lame_default_settings.ath_only;
  lame->ath_short = gst_lame_default_settings.ath_short;
  lame->no_ath = gst_lame_default_settings.no_ath;
  lame->ath_lower = gst_lame_default_settings.ath_lower;
  lame->allow_diff_short = gst_lame_default_settings.allow_diff_short;
  lame->no_short_blocks = gst_lame_default_settings.no_short_blocks;
  lame->emphasis = gst_lame_default_settings.emphasis;
  lame->preset = gst_lame_default_settings.preset;

  GST_DEBUG_OBJECT (lame, "done initializing");
}

/* <php-emulation-mode>three underscores for ___rate is really really really
 * private as opposed to one underscore<php-emulation-mode> */
/* call this MACRO outside of the NULL state so that we have a higher chance
 * of actually having a pipeline and bus to get the message through */

#define CHECK_AND_FIXUP_BITRATE(obj,param,rate,free_format) 		  \
G_STMT_START {                                                            \
  gint ___rate = rate;                                                    \
  gint maxrate = 320;							  \
  gint multiplier = 64;							  \
  if (!free_format) {                                                     \
    if (rate == 0) {                                                      \
      ___rate = rate;                                                     \
    } else if (rate <= 64) {				                  \
      maxrate = 64; multiplier = 8;                                       \
      if ((rate % 8) != 0) ___rate = GST_ROUND_UP_8 (rate); 		  \
    } else if (rate <= 128) {						  \
      maxrate = 128; multiplier = 16;                                     \
      if ((rate % 16) != 0) ___rate = GST_ROUND_UP_16 (rate);             \
    } else if (rate <= 256) {						  \
      maxrate = 256; multiplier = 32;                                     \
      if ((rate % 32) != 0) ___rate = GST_ROUND_UP_32 (rate);             \
    } else if (rate <= 320) { 						  \
      maxrate = 320; multiplier = 64;                                     \
      if ((rate % 64) != 0) ___rate = GST_ROUND_UP_64 (rate);             \
    }                                                                     \
    if (___rate != rate) {                                                \
      GST_ELEMENT_WARNING (obj, LIBRARY, SETTINGS,			  \
          (_("The requested bitrate %d kbit/s for property '%s' "         \
             "is not allowed. "  					  \
            "The bitrate was changed to %d kbit/s."), rate,		  \
	    param,  ___rate), 					          \
          ("A bitrate below %d should be a multiple of %d.", 		  \
              maxrate, multiplier));		  			  \
      rate = ___rate;                                                     \
    }                                                                     \
  }                                                                       \
} G_STMT_END

static void
gst_lame_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstLame *lame;

  lame = GST_LAME (object);

  switch (prop_id) {
    case ARG_BITRATE:
      lame->bitrate = g_value_get_int (value);
      break;
    case ARG_COMPRESSION_RATIO:
      lame->compression_ratio = g_value_get_float (value);
      break;
    case ARG_QUALITY:
      lame->quality = g_value_get_enum (value);
      break;
    case ARG_MODE:
      lame->requested_mode = g_value_get_enum (value);
      break;
    case ARG_FORCE_MS:
      lame->force_ms = g_value_get_boolean (value);
      break;
    case ARG_FREE_FORMAT:
      lame->free_format = g_value_get_boolean (value);
      break;
    case ARG_COPYRIGHT:
      lame->copyright = g_value_get_boolean (value);
      break;
    case ARG_ORIGINAL:
      lame->original = g_value_get_boolean (value);
      break;
    case ARG_ERROR_PROTECTION:
      lame->error_protection = g_value_get_boolean (value);
      break;
    case ARG_PADDING_TYPE:
      break;
    case ARG_EXTENSION:
      lame->extension = g_value_get_boolean (value);
      break;
    case ARG_STRICT_ISO:
      lame->strict_iso = g_value_get_boolean (value);
      break;
    case ARG_DISABLE_RESERVOIR:
      lame->disable_reservoir = g_value_get_boolean (value);
      break;
    case ARG_VBR:
      lame->vbr = g_value_get_enum (value);
      break;
    case ARG_VBR_QUALITY:
      lame->vbr_quality = g_value_get_enum (value);
      break;
    case ARG_VBR_MEAN_BITRATE:
      lame->vbr_mean_bitrate = g_value_get_int (value);
      break;
    case ARG_VBR_MIN_BITRATE:
      lame->vbr_min_bitrate = g_value_get_int (value);
      break;
    case ARG_VBR_MAX_BITRATE:
      lame->vbr_max_bitrate = g_value_get_int (value);
      break;
    case ARG_VBR_HARD_MIN:
      lame->vbr_hard_min = g_value_get_int (value);
      break;
    case ARG_LOWPASS_FREQ:
      lame->lowpass_freq = g_value_get_int (value);
      break;
    case ARG_LOWPASS_WIDTH:
      lame->lowpass_width = g_value_get_int (value);
      break;
    case ARG_HIGHPASS_FREQ:
      lame->highpass_freq = g_value_get_int (value);
      break;
    case ARG_HIGHPASS_WIDTH:
      lame->highpass_width = g_value_get_int (value);
      break;
    case ARG_ATH_ONLY:
      lame->ath_only = g_value_get_boolean (value);
      break;
    case ARG_ATH_SHORT:
      lame->ath_short = g_value_get_boolean (value);
      break;
    case ARG_NO_ATH:
      lame->no_ath = g_value_get_boolean (value);
      break;
    case ARG_ATH_LOWER:
      lame->ath_lower = g_value_get_int (value);
      break;
    case ARG_CWLIMIT:
      break;
    case ARG_ALLOW_DIFF_SHORT:
      lame->allow_diff_short = g_value_get_boolean (value);
      break;
    case ARG_NO_SHORT_BLOCKS:
      lame->no_short_blocks = g_value_get_boolean (value);
      break;
    case ARG_EMPHASIS:
      lame->emphasis = g_value_get_boolean (value);
      break;
    case ARG_XINGHEADER:
      break;
#ifdef GSTLAME_PRESET
    case ARG_PRESET:
      lame->preset = g_value_get_enum (value);
      break;
#endif
    default:
      break;
  }

}

static void
gst_lame_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstLame *lame;

  lame = GST_LAME (object);

  switch (prop_id) {
    case ARG_BITRATE:
      g_value_set_int (value, lame->bitrate);
      break;
    case ARG_COMPRESSION_RATIO:
      g_value_set_float (value, lame->compression_ratio);
      break;
    case ARG_QUALITY:
      g_value_set_enum (value, lame->quality);
      break;
    case ARG_MODE:
      g_value_set_enum (value, lame->requested_mode);
      break;
    case ARG_FORCE_MS:
      g_value_set_boolean (value, lame->force_ms);
      break;
    case ARG_FREE_FORMAT:
      g_value_set_boolean (value, lame->free_format);
      break;
    case ARG_COPYRIGHT:
      g_value_set_boolean (value, lame->copyright);
      break;
    case ARG_ORIGINAL:
      g_value_set_boolean (value, lame->original);
      break;
    case ARG_ERROR_PROTECTION:
      g_value_set_boolean (value, lame->error_protection);
      break;
    case ARG_PADDING_TYPE:
      break;
    case ARG_EXTENSION:
      g_value_set_boolean (value, lame->extension);
      break;
    case ARG_STRICT_ISO:
      g_value_set_boolean (value, lame->strict_iso);
      break;
    case ARG_DISABLE_RESERVOIR:
      g_value_set_boolean (value, lame->disable_reservoir);
      break;
    case ARG_VBR:
      g_value_set_enum (value, lame->vbr);
      break;
    case ARG_VBR_QUALITY:
      g_value_set_enum (value, lame->vbr_quality);
      break;
    case ARG_VBR_MEAN_BITRATE:
      g_value_set_int (value, lame->vbr_mean_bitrate);
      break;
    case ARG_VBR_MIN_BITRATE:
      g_value_set_int (value, lame->vbr_min_bitrate);
      break;
    case ARG_VBR_MAX_BITRATE:
      g_value_set_int (value, lame->vbr_max_bitrate);
      break;
    case ARG_VBR_HARD_MIN:
      g_value_set_int (value, lame->vbr_hard_min);
      break;
    case ARG_LOWPASS_FREQ:
      g_value_set_int (value, lame->lowpass_freq);
      break;
    case ARG_LOWPASS_WIDTH:
      g_value_set_int (value, lame->lowpass_width);
      break;
    case ARG_HIGHPASS_FREQ:
      g_value_set_int (value, lame->highpass_freq);
      break;
    case ARG_HIGHPASS_WIDTH:
      g_value_set_int (value, lame->highpass_width);
      break;
    case ARG_ATH_ONLY:
      g_value_set_boolean (value, lame->ath_only);
      break;
    case ARG_ATH_SHORT:
      g_value_set_boolean (value, lame->ath_short);
      break;
    case ARG_NO_ATH:
      g_value_set_boolean (value, lame->no_ath);
      break;
    case ARG_ATH_LOWER:
      g_value_set_int (value, lame->ath_lower);
      break;
    case ARG_CWLIMIT:
      break;
    case ARG_ALLOW_DIFF_SHORT:
      g_value_set_boolean (value, lame->allow_diff_short);
      break;
    case ARG_NO_SHORT_BLOCKS:
      g_value_set_boolean (value, lame->no_short_blocks);
      break;
    case ARG_EMPHASIS:
      g_value_set_boolean (value, lame->emphasis);
      break;
    case ARG_XINGHEADER:
      break;
#ifdef GSTLAME_PRESET
    case ARG_PRESET:
      g_value_set_enum (value, lame->preset);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_lame_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret;
  GstLame *lame;

  lame = GST_LAME (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:{
      GST_DEBUG_OBJECT (lame, "handling EOS event");

      if (lame->lgf != NULL) {
        GstBuffer *buf;
        gint size;

        buf = gst_buffer_new_and_alloc (7200);
        size = lame_encode_flush (lame->lgf, GST_BUFFER_DATA (buf), 7200);

        if (size > 0 && lame->last_flow == GST_FLOW_OK) {
          gint64 duration;

          duration = gst_util_uint64_scale (size, 8 * GST_SECOND,
              1000 * lame->bitrate);

          if (lame->last_ts == GST_CLOCK_TIME_NONE) {
            lame->last_ts = lame->eos_ts;
            lame->last_duration = duration;
          } else {
            lame->last_duration += duration;
          }

          GST_BUFFER_TIMESTAMP (buf) = lame->last_ts;
          GST_BUFFER_DURATION (buf) = lame->last_duration;
          lame->last_ts = GST_CLOCK_TIME_NONE;
          GST_BUFFER_SIZE (buf) = size;
          GST_DEBUG_OBJECT (lame, "pushing final packet of %u bytes", size);
          gst_buffer_set_caps (buf, GST_PAD_CAPS (lame->srcpad));
          gst_pad_push (lame->srcpad, buf);
        } else {
          GST_DEBUG_OBJECT (lame, "no final packet (size=%d, last_flow=%s)",
              size, gst_flow_get_name (lame->last_flow));
          gst_buffer_unref (buf);
        }
      }

      ret = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (lame, "handling FLUSH start event");
      /* forward event */
      ret = gst_pad_push_event (lame->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
    {
      guchar *mp3_data = NULL;
      gint mp3_buffer_size;

      GST_DEBUG_OBJECT (lame, "handling FLUSH stop event");

      if (lame->lgf) {
        /* clear buffers if we already have lame set up */
        mp3_buffer_size = 7200;
        mp3_data = g_malloc (mp3_buffer_size);
        lame_encode_flush (lame->lgf, mp3_data, mp3_buffer_size);
        g_free (mp3_data);
      }

      ret = gst_pad_push_event (lame->srcpad, event);
      break;
    }
    case GST_EVENT_TAG:
      GST_DEBUG_OBJECT (lame, "ignoring TAG event, passing it on");
      ret = gst_pad_push_event (lame->srcpad, event);
      break;
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }
  gst_object_unref (lame);
  return ret;
}

static GstFlowReturn
gst_lame_chain (GstPad * pad, GstBuffer * buf)
{
  GstLame *lame;
  guchar *mp3_data;
  gint mp3_buffer_size, mp3_size;
  gint64 duration;
  GstFlowReturn result;
  gint num_samples;
  guint8 *data;
  guint size;

  lame = GST_LAME (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (lame, "entered chain");

  if (!lame->setup)
    goto not_setup;

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  num_samples = size / 2;

  /* allocate space for output */
  mp3_buffer_size = 1.25 * num_samples + 7200;
  mp3_data = g_malloc (mp3_buffer_size);

  /* lame seems to be too stupid to get mono interleaved going */
  if (lame->num_channels == 1) {
    mp3_size = lame_encode_buffer (lame->lgf,
        (short int *) data,
        (short int *) data, num_samples, mp3_data, mp3_buffer_size);
  } else {
    mp3_size = lame_encode_buffer_interleaved (lame->lgf,
        (short int *) data,
        num_samples / lame->num_channels, mp3_data, mp3_buffer_size);
  }

  GST_LOG_OBJECT (lame, "encoded %d bytes of audio to %d bytes of mp3",
      size, mp3_size);

  duration = gst_util_uint64_scale_int (size, GST_SECOND,
      2 * lame->samplerate * lame->num_channels);

  if (GST_BUFFER_DURATION (buf) != GST_CLOCK_TIME_NONE &&
      GST_BUFFER_DURATION (buf) != duration) {
    GST_DEBUG_OBJECT (lame, "incoming buffer had incorrect duration %"
        GST_TIME_FORMAT ", outgoing buffer will have correct duration %"
        GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_TIME_ARGS (duration));
  }

  if (lame->last_ts == GST_CLOCK_TIME_NONE) {
    lame->last_ts = GST_BUFFER_TIMESTAMP (buf);
    lame->last_offs = GST_BUFFER_OFFSET (buf);
    lame->last_duration = duration;
  } else {
    lame->last_duration += duration;
  }

  gst_buffer_unref (buf);

  if (mp3_size < 0) {
    g_warning ("error %d", mp3_size);
  }

  if (mp3_size > 0) {
    GstBuffer *outbuf;

    outbuf = gst_buffer_new ();
    GST_BUFFER_DATA (outbuf) = mp3_data;
    GST_BUFFER_MALLOCDATA (outbuf) = mp3_data;
    GST_BUFFER_SIZE (outbuf) = mp3_size;
    GST_BUFFER_TIMESTAMP (outbuf) = lame->last_ts;
    GST_BUFFER_OFFSET (outbuf) = lame->last_offs;
    GST_BUFFER_DURATION (outbuf) = lame->last_duration;
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (lame->srcpad));

    result = gst_pad_push (lame->srcpad, outbuf);
    lame->last_flow = result;
    if (result != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (lame, "flow return: %s", gst_flow_get_name (result));
    }

    if (GST_CLOCK_TIME_IS_VALID (lame->last_ts))
      lame->eos_ts = lame->last_ts + lame->last_duration;
    else
      lame->eos_ts = GST_CLOCK_TIME_NONE;
    lame->last_ts = GST_CLOCK_TIME_NONE;
  } else {
    g_free (mp3_data);
    result = GST_FLOW_OK;
  }

  return result;

  /* ERRORS */
not_setup:
  {
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (lame, CORE, NEGOTIATION, (NULL),
        ("encoder not initialized (input is not audio?)"));
    return GST_FLOW_ERROR;
  }
}

/* set up the encoder state */
static gboolean
gst_lame_setup (GstLame * lame)
{

#define CHECK_ERROR(command) G_STMT_START {\
  if ((command) < 0) { \
    GST_ERROR_OBJECT (lame, "setup failed: " G_STRINGIFY (command)); \
    return FALSE; \
  } \
}G_STMT_END

  int retval;
  GstCaps *allowed_caps;

  GST_DEBUG_OBJECT (lame, "starting setup");

  /* check if we're already setup; if we are, we might want to check
   * if this initialization is compatible with the previous one */
  /* FIXME: do this */
  if (lame->setup) {
    GST_WARNING_OBJECT (lame, "already setup");
    lame->setup = FALSE;
  }

  lame->lgf = lame_init ();

  if (lame->lgf == NULL)
    return FALSE;

  /* copy the parameters over */
  lame_set_in_samplerate (lame->lgf, lame->samplerate);

  /* let lame choose default samplerate unless outgoing sample rate is fixed */
  allowed_caps = gst_pad_get_allowed_caps (lame->srcpad);

  if (allowed_caps != NULL) {
    GstStructure *structure;
    gint samplerate;

    structure = gst_caps_get_structure (allowed_caps, 0);

    if (gst_structure_get_int (structure, "rate", &samplerate)) {
      GST_DEBUG_OBJECT (lame, "Setting sample rate to %d as fixed in src caps",
          samplerate);
      lame_set_out_samplerate (lame->lgf, samplerate);
    } else {
      GST_DEBUG_OBJECT (lame, "Letting lame choose sample rate");
      lame_set_out_samplerate (lame->lgf, 0);
    }
    gst_caps_unref (allowed_caps);
    allowed_caps = NULL;
  } else {
    GST_DEBUG_OBJECT (lame, "No peer yet, letting lame choose sample rate");
    lame_set_out_samplerate (lame->lgf, 0);
  }

  /* force mono encoding if we only have one channel */
  if (lame->num_channels == 1)
    lame->mode = 3;
  else
    lame->mode = lame->requested_mode;

  CHECK_ERROR (lame_set_num_channels (lame->lgf, lame->num_channels));
  CHECK_AND_FIXUP_BITRATE (lame, "bitrate", lame->bitrate, lame->free_format);
  CHECK_ERROR (lame_set_brate (lame->lgf, lame->bitrate));
  CHECK_ERROR (lame_set_compression_ratio (lame->lgf, lame->compression_ratio));
  CHECK_ERROR (lame_set_quality (lame->lgf, lame->quality));
  CHECK_ERROR (lame_set_mode (lame->lgf, lame->mode));
  CHECK_ERROR (lame_set_force_ms (lame->lgf, lame->force_ms));
  CHECK_ERROR (lame_set_free_format (lame->lgf, lame->free_format));
  CHECK_ERROR (lame_set_copyright (lame->lgf, lame->copyright));
  CHECK_ERROR (lame_set_original (lame->lgf, lame->original));
  CHECK_ERROR (lame_set_error_protection (lame->lgf, lame->error_protection));
  CHECK_ERROR (lame_set_extension (lame->lgf, lame->extension));
  CHECK_ERROR (lame_set_strict_ISO (lame->lgf, lame->strict_iso));
  CHECK_ERROR (lame_set_disable_reservoir (lame->lgf, lame->disable_reservoir));
  CHECK_ERROR (lame_set_VBR (lame->lgf, lame->vbr));
  CHECK_ERROR (lame_set_VBR_q (lame->lgf, lame->vbr_quality));
  CHECK_ERROR (lame_set_VBR_mean_bitrate_kbps (lame->lgf,
          lame->vbr_mean_bitrate));
  CHECK_AND_FIXUP_BITRATE (lame, "vbr-min-bitrate", lame->vbr_min_bitrate,
      lame->free_format);
  CHECK_ERROR (lame_set_VBR_min_bitrate_kbps (lame->lgf,
          lame->vbr_min_bitrate));
  CHECK_AND_FIXUP_BITRATE (lame, "vbr-max-bitrate", lame->vbr_max_bitrate,
      lame->free_format);
  CHECK_ERROR (lame_set_VBR_max_bitrate_kbps (lame->lgf,
          lame->vbr_max_bitrate));
  CHECK_ERROR (lame_set_VBR_hard_min (lame->lgf, lame->vbr_hard_min));
  CHECK_ERROR (lame_set_lowpassfreq (lame->lgf, lame->lowpass_freq));
  CHECK_ERROR (lame_set_lowpasswidth (lame->lgf, lame->lowpass_width));
  CHECK_ERROR (lame_set_highpassfreq (lame->lgf, lame->highpass_freq));
  CHECK_ERROR (lame_set_highpasswidth (lame->lgf, lame->highpass_width));
  CHECK_ERROR (lame_set_ATHonly (lame->lgf, lame->ath_only));
  CHECK_ERROR (lame_set_ATHshort (lame->lgf, lame->ath_short));
  CHECK_ERROR (lame_set_noATH (lame->lgf, lame->no_ath));
  CHECK_ERROR (lame_set_ATHlower (lame->lgf, lame->ath_lower));
  CHECK_ERROR (lame_set_allow_diff_short (lame->lgf, lame->allow_diff_short));
  CHECK_ERROR (lame_set_no_short_blocks (lame->lgf, lame->no_short_blocks));
  CHECK_ERROR (lame_set_emphasis (lame->lgf, lame->emphasis));
  CHECK_ERROR (lame_set_bWriteVbrTag (lame->lgf, 0));
#ifdef GSTLAME_PRESET
  if (lame->preset > 0) {
    CHECK_ERROR (lame_set_preset (lame->lgf, lame->preset));
  }
#endif

  /* initialize the lame encoder */
  if ((retval = lame_init_params (lame->lgf)) >= 0) {
    lame->setup = TRUE;
    /* FIXME: it would be nice to print out the mode here */
    GST_INFO ("lame encoder setup (%d kbit/s, %d Hz, %d channels)",
        lame->bitrate, lame->samplerate, lame->num_channels);
  } else {
    GST_ERROR_OBJECT (lame, "lame_init_params returned %d", retval);
  }

  GST_DEBUG_OBJECT (lame, "done with setup");

  return lame->setup;
#undef CHECK_ERROR
}

static GstStateChangeReturn
gst_lame_change_state (GstElement * element, GstStateChange transition)
{
  GstLame *lame;
  GstStateChangeReturn result;

  lame = GST_LAME (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      lame->last_flow = GST_FLOW_OK;
      lame->last_ts = GST_CLOCK_TIME_NONE;
      lame->eos_ts = GST_CLOCK_TIME_NONE;
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_lame_release_memory (lame);
      break;
    default:
      break;
  }

  return result;
}

static gboolean
gst_lame_get_default_settings (void)
{
  lame_global_flags *lgf = NULL;

  lgf = lame_init ();
  if (lgf == NULL) {
    GST_ERROR ("Error initializing LAME");
    return FALSE;
  }

  if (lame_init_params (lgf) < 0) {
    GST_ERROR ("Error getting default settings");
    return FALSE;
  }

  gst_lame_default_settings.bitrate = lame_get_brate (lgf);
  gst_lame_default_settings.compression_ratio = 0.0;    /* lame_get_compression_ratio (lgf); */
  gst_lame_default_settings.quality = lame_get_quality (lgf);
  gst_lame_default_settings.mode = lame_get_mode (lgf);
  gst_lame_default_settings.force_ms = lame_get_force_ms (lgf);
  gst_lame_default_settings.free_format = lame_get_free_format (lgf);
  gst_lame_default_settings.copyright = lame_get_copyright (lgf);
  gst_lame_default_settings.original = lame_get_original (lgf);
  gst_lame_default_settings.error_protection = lame_get_error_protection (lgf);
  gst_lame_default_settings.extension = lame_get_extension (lgf);
  gst_lame_default_settings.strict_iso = lame_get_strict_ISO (lgf);
  gst_lame_default_settings.disable_reservoir =
      lame_get_disable_reservoir (lgf);
  gst_lame_default_settings.vbr = lame_get_VBR (lgf);
  gst_lame_default_settings.vbr_quality = lame_get_VBR_q (lgf);
  gst_lame_default_settings.vbr_mean_bitrate =
      lame_get_VBR_mean_bitrate_kbps (lgf);
  gst_lame_default_settings.vbr_min_bitrate =
      lame_get_VBR_min_bitrate_kbps (lgf);
  gst_lame_default_settings.vbr_max_bitrate =
      lame_get_VBR_max_bitrate_kbps (lgf);
  gst_lame_default_settings.vbr_hard_min = lame_get_VBR_hard_min (lgf);
  gst_lame_default_settings.lowpass_freq = lame_get_lowpassfreq (lgf);
  gst_lame_default_settings.lowpass_width = lame_get_lowpasswidth (lgf);
  gst_lame_default_settings.highpass_freq = lame_get_highpassfreq (lgf);
  gst_lame_default_settings.highpass_width = lame_get_highpasswidth (lgf);
  gst_lame_default_settings.ath_only = lame_get_ATHonly (lgf);
  gst_lame_default_settings.ath_short = lame_get_ATHshort (lgf);
  gst_lame_default_settings.no_ath = lame_get_noATH (lgf);
  gst_lame_default_settings.ath_type = lame_get_ATHtype (lgf);
  gst_lame_default_settings.ath_lower = lame_get_ATHlower (lgf);
  gst_lame_default_settings.allow_diff_short = lame_get_allow_diff_short (lgf);
  gst_lame_default_settings.no_short_blocks = lame_get_no_short_blocks (lgf);
  gst_lame_default_settings.emphasis = lame_get_emphasis (lgf);
  gst_lame_default_settings.preset = 0;

  lame_close (lgf);

  return TRUE;
}

gboolean
gst_lame_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (debug, "lame", 0, "lame mp3 encoder");

  if (!gst_lame_get_default_settings ())
    return FALSE;

  if (!gst_element_register (plugin, "lame", GST_RANK_MARGINAL, GST_TYPE_LAME))
    return FALSE;

  return TRUE;
}
