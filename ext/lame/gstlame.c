/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include "string.h"
#include "gstlame.h"

/* elementfactory information */
static GstElementDetails gst_lame_details = 
{
  "L.A.M.E. mp3 encoder",
  "Codec/Audio/Encoder",
  "GPL",
  "High-quality free MP3 encoder",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 2000",
};

GST_PAD_TEMPLATE_FACTORY (gst_lame_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "gstlame_sink",
    "audio/raw",
      "format",     GST_PROPS_STRING ("int"),
      "law",        GST_PROPS_INT (0),
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "signed",     GST_PROPS_BOOLEAN (TRUE),
      "width",      GST_PROPS_INT (16),
      "depth",      GST_PROPS_INT (16),
      "rate",       GST_PROPS_LIST (
	              GST_PROPS_INT (8000), 
	              GST_PROPS_INT (11025), 
	              GST_PROPS_INT (12000), 
	              GST_PROPS_INT (16000), 
	              GST_PROPS_INT (22050),
	              GST_PROPS_INT (24000),
	              GST_PROPS_INT (32000),
	              GST_PROPS_INT (44100),
	              GST_PROPS_INT (48000)
	            ),
      "channels",   GST_PROPS_INT_RANGE (1, 2)
  )
)

GST_PAD_TEMPLATE_FACTORY (gst_lame_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "gstlame_src",
    "audio/x-mp3",
    NULL
  )
)

/********** Define useful types for non-programmatic interfaces **********/
#define GST_TYPE_LAME_MODE (gst_lame_mode_get_type())
static GType
gst_lame_mode_get_type (void)
{
  static GType lame_mode_type = 0;
  static GEnumValue lame_modes[] = {
    { 0, "0", "Stereo" },
    { 1, "1", "Joint-Stereo" },
    { 2, "2", "Dual channel" },
    { 3, "3", "Mono" },
    { 4, "4", "Auto" },
    { 0, NULL, NULL },
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
    { 0, "0", "0 - Best" },
    { 1, "1", "1" },
    { 2, "2", "2" },
    { 3, "3", "3" },
    { 4, "4", "4" },
    { 5, "5", "5 - Default" },
    { 6, "6", "6" },
    { 7, "7", "7" },
    { 8, "8", "8" },
    { 9, "9", "9 - Worst" },
    { 0, NULL, NULL },
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
    { 0, "0", "No Padding" },
    { 1, "1", "Always Pad" },
    { 2, "2", "Adjust Padding" },
    { 0, NULL, NULL },
  };
  if (!lame_padding_type) {
    lame_padding_type = g_enum_register_static ("GstLamePadding", lame_padding);
  }
  return lame_padding_type;
}

/********** Standard stuff for signals and arguments **********/
/* GstLame signals and args */
enum {
  /* FILL_ME */
  LAST_SIGNAL
};

enum {
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
  ARG_PADDING_TYPE,
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
  ARG_CWLIMIT,
  ARG_ALLOW_DIFF_SHORT,
  ARG_NO_SHORT_BLOCKS,
  ARG_EMPHASIS,
  ARG_METADATA,
};


static void			gst_lame_class_init	(GstLameClass *klass);
static void			gst_lame_init		(GstLame *gst_lame);

static void			gst_lame_set_property	(GObject *object, guint prop_id, 
		 					 const GValue *value, GParamSpec *pspec);
static void			gst_lame_get_property	(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);
static void			gst_lame_chain		(GstPad *pad, GstBuffer *buf);
static gboolean 		gst_lame_setup 		(GstLame *lame);
static GstElementStateReturn 	gst_lame_change_state 	(GstElement *element);

static GstElementClass *parent_class = NULL;
/* static guint gst_lame_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_lame_get_type (void)
{
  static GType gst_lame_type = 0;

  if (!gst_lame_type) {
    static const GTypeInfo gst_lame_info = {
      sizeof (GstLameClass),      
      NULL,
      NULL,
      (GClassInitFunc) gst_lame_class_init,
      NULL,
      NULL,
      sizeof(GstLame),
      0,
      (GInstanceInitFunc) gst_lame_init,
    };
    gst_lame_type = g_type_register_static (GST_TYPE_ELEMENT, "GstLame", &gst_lame_info, 0);
  }
  return gst_lame_type;
}

static void
gst_lame_class_init (GstLameClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE,
    g_param_spec_int("bitrate", "Bitrate (kb/s)", "Bitrate in kbit/sec",
                     8, 320, 128, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_COMPRESSION_RATIO,
    g_param_spec_float ("compression_ratio", "Compression Ratio",
			"choose bitrate to achive selected compression ratio",
                        1.0, 200.0, 11.0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUALITY,
    g_param_spec_enum ("quality", "Quality", "Encoding Quality",
                       GST_TYPE_LAME_QUALITY, 5, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MODE,
    g_param_spec_enum ("mode", "Mode", "Encoding mode",
                       GST_TYPE_LAME_MODE, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FORCE_MS,
    g_param_spec_boolean ("force_ms","Force ms","Force ms_stereo on all frames",
                          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FREE_FORMAT,
    g_param_spec_boolean ("free_format","Free format","Produce a free format bitstream",
                          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_COPYRIGHT,
    g_param_spec_boolean ("copyright","Copyright","Mark as copyright",
                          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_ORIGINAL,
    g_param_spec_boolean("original", "Original", "Mark as non-original",
                         TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ERROR_PROTECTION,
    g_param_spec_boolean ("error_protection","Error protection",
	    		  "Adds 16 bit checksum to every frame",
                          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PADDING_TYPE,
    g_param_spec_enum ("padding_type", "Padding type", "Padding type",
                       GST_TYPE_LAME_PADDING, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_EXTENSION,
    g_param_spec_boolean ("extension", "Extension", "Extension",
                          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_STRICT_ISO,
    g_param_spec_boolean ("strict_iso", "Strict ISO",
	    		  "Comply as much as possible to ISO MPEG spec",
                          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DISABLE_RESERVOIR,
    g_param_spec_boolean ("disable_reservoir", "Disable reservoir", "Disable the bit reservoir",
                          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR,
    g_param_spec_boolean ("vbr", "VBR", "Use variable bitrate",
                          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR_MEAN_BITRATE,
    g_param_spec_int ("vbr_mean_bitrate", "VBR mean bitrate", "Specify mean bitrate",
                      0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR_MIN_BITRATE,
    g_param_spec_int ("vbr_min_bitrate", "VBR min bitrate", "Specify min bitrate",
                      0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_VBR_MAX_BITRATE,
    g_param_spec_int ("vbr_max_bitrate", "VBR max bitrate", "Specify max bitrate",
                      0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_VBR_HARD_MIN,
    g_param_spec_int ("vbr_hard_min", "VBR hard min", "Specify hard min bitrate",
                      0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOWPASS_FREQ,
    g_param_spec_int ("lowpass_freq", "Lowpass freq", 
	                "frequency(kHz), lowpass filter cutoff above freq",
                        0, 50000, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOWPASS_WIDTH,
    g_param_spec_int ("lowpass_width", "Lowpass width", 
	              "frequency(kHz) - default 15% of lowpass freq",
                      0, G_MAXINT, 0, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HIGHPASS_FREQ,
    g_param_spec_int ("highpass_freq", "Highpass freq", 
	              "frequency(kHz), highpass filter cutoff below freq",
                      0, 50000, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HIGHPASS_WIDTH,
    g_param_spec_int ("highpass_width", "Highpass width",
	              "frequency(kHz) - default 15% of highpass freq",
                      0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ATH_ONLY,
    g_param_spec_boolean ("ath_only", "ATH only", 
	                  "Ignore GPSYCHO completely, use ATH only",
                          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ATH_SHORT,
    g_param_spec_boolean ("ath_short", "ATH short", 
	                  "Ignore GPSYCHO for short blocks, use ATH only",
                          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NO_ATH,
    g_param_spec_boolean ("no_ath", "No ath", "turns ATH down to a flat noise floor",
                          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ATH_LOWER,
    g_param_spec_int ("ath_lower", "ATH lower", "lowers ATH by x dB",
                      G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CWLIMIT,
    g_param_spec_int ("cwlimit", "Cwlimit", "Compute tonality up to freq (in kHz) default 8.8717",
                      0, 50000, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ALLOW_DIFF_SHORT,
    g_param_spec_boolean ("allow_diff_short", "Allow diff short", "Allow diff short",
                          TRUE, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NO_SHORT_BLOCKS,
    g_param_spec_boolean ("no_short_blocks", "No short blocks", "Do not use short blocks",
                          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_EMPHASIS,
    g_param_spec_boolean ("emphasis", "Emphasis", "Emphasis",
                          TRUE, G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_METADATA,
    g_param_spec_boxed ("metadata", "Metadata", "Metadata to add to the stream,",
            GST_TYPE_CAPS, G_PARAM_READWRITE));


  gobject_class->set_property = gst_lame_set_property;
  gobject_class->get_property = gst_lame_get_property;

  gstelement_class->change_state = gst_lame_change_state;
}

static GstPadLinkReturn
gst_lame_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstLame *lame;

  lame = GST_LAME (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    GST_DEBUG ("caps on lame pad %s:%s not fixed, delayed",
	       GST_DEBUG_PAD_NAME (pad));
    return GST_PAD_LINK_DELAYED;
  }

  gst_caps_get_int (caps, "rate", &lame->samplerate);
  gst_caps_get_int (caps, "channels", &lame->num_channels);

  if (!gst_lame_setup (lame)) {
    gst_element_error (GST_ELEMENT (lame), 
	               "could not initialize encoder (wrong parameters?)");
    return GST_PAD_LINK_REFUSED;
  }

  return GST_PAD_LINK_OK;
}

static void
gst_lame_init (GstLame *lame)
{
  GST_DEBUG_OBJECT (lame, "starting initialization");

  lame->sinkpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (gst_lame_sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (lame), lame->sinkpad);
  gst_pad_set_chain_function (lame->sinkpad, gst_lame_chain);
  gst_pad_set_link_function (lame->sinkpad, gst_lame_sinkconnect);

  lame->srcpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (gst_lame_src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (lame), lame->srcpad);

  GST_FLAG_SET (lame, GST_ELEMENT_EVENT_AWARE);

  GST_DEBUG ("setting up lame encoder");
  lame->lgf = lame_init ();

  lame->samplerate = 44100;
  lame->num_channels = 2;
  lame->initialized = FALSE;

  lame->bitrate = lame_get_brate (lame->lgf);
  lame->compression_ratio = lame_get_compression_ratio (lame->lgf);
  lame->quality = lame_get_quality (lame->lgf);
  lame->mode = lame_get_mode (lame->lgf);
  lame->force_ms = lame_get_force_ms (lame->lgf);
  lame->free_format = lame_get_free_format (lame->lgf);
  lame->copyright = lame_get_copyright (lame->lgf);
  lame->original = lame_get_original (lame->lgf);
  lame->error_protection = lame_get_error_protection (lame->lgf);
  lame->padding_type = lame_get_padding_type (lame->lgf);
  lame->extension = lame_get_extension (lame->lgf);
  lame->strict_iso = lame_get_strict_ISO (lame->lgf);
  lame->disable_reservoir = lame_get_disable_reservoir (lame->lgf);
  lame->vbr = lame_get_VBR_q (lame->lgf);
  lame->vbr_mean_bitrate = lame_get_VBR_mean_bitrate_kbps (lame->lgf);
  lame->vbr_min_bitrate = lame_get_VBR_min_bitrate_kbps (lame->lgf);
  lame->vbr_max_bitrate = lame_get_VBR_max_bitrate_kbps (lame->lgf);
  lame->vbr_hard_min = lame_get_VBR_hard_min (lame->lgf);
  lame->lowpass_freq = lame_get_lowpassfreq (lame->lgf);
  lame->lowpass_width = lame_get_lowpasswidth (lame->lgf);
  lame->highpass_freq = lame_get_highpassfreq (lame->lgf);
  lame->highpass_width = lame_get_highpasswidth (lame->lgf);
  lame->ath_only = lame_get_ATHonly (lame->lgf);
  lame->ath_short = lame_get_ATHshort (lame->lgf);
  lame->no_ath = lame_get_noATH (lame->lgf);
  /*  lame->ath_type = lame_get_ATHtype (lame->lgf); */
  lame->ath_lower = lame_get_ATHlower (lame->lgf);
  lame->cwlimit = lame_get_cwlimit (lame->lgf);
  lame->allow_diff_short = lame_get_allow_diff_short (lame->lgf);
  lame->no_short_blocks = lame_get_no_short_blocks (lame->lgf);
  lame->emphasis = lame_get_emphasis (lame->lgf);

  lame->metadata = GST_CAPS_NEW (
      "lame_metadata",
      "application/x-gst-metadata",
      "comment",     GST_PROPS_STRING ("Track encoded with GStreamer"),
      "year",        GST_PROPS_STRING (""),
      "tracknumber", GST_PROPS_STRING (""),
      "title",       GST_PROPS_STRING (""),
      "artist",      GST_PROPS_STRING (""),
      "album",       GST_PROPS_STRING (""),
      "genre",       GST_PROPS_STRING ("")
  );

  id3tag_init (lame->lgf);
  
  GST_DEBUG_OBJECT (lame, "done initializing");
}

static void
gst_lame_add_metadata (GstLame *lame, GstCaps *caps)
{
  GList *props;
  GstPropsEntry *prop;

  if (caps == NULL)
    return;

  props = gst_caps_get_props (caps)->properties;
  while (props) {
    prop = (GstPropsEntry*)(props->data);
    props = g_list_next(props);

    if (gst_props_entry_get_type (prop) == GST_PROPS_STRING_TYPE) {
      const gchar *name = gst_props_entry_get_name (prop);
      const gchar *value;

      gst_props_entry_get_string (prop, &value);

      if (!value || strlen (value) == 0)
        continue;

      if (strcmp (name, "comment") == 0) {
        id3tag_set_comment (lame->lgf, value);
      } else if (strcmp (name, "date") == 0) {
        id3tag_set_year (lame->lgf, value);
      } else if (strcmp (name, "tracknumber") == 0) {
        id3tag_set_track (lame->lgf, value);
      } else if (strcmp (name, "title") == 0) {
        id3tag_set_title (lame->lgf, value);
      } else if (strcmp (name, "artist") == 0) {
        id3tag_set_artist (lame->lgf, value);
      } else if (strcmp (name, "album") == 0) {
        id3tag_set_album (lame->lgf, value);
      } else if (strcmp (name, "genre") == 0) {
        id3tag_set_genre (lame->lgf, value);
      }
    }
  }
}

static void
gst_lame_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstLame *lame;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_LAME (object));

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
      lame->mode = g_value_get_enum (value);
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
      lame->padding_type = g_value_get_int (value);
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
      lame->vbr = g_value_get_boolean (value);
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
      lame->cwlimit = g_value_get_int (value);
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
    case ARG_METADATA:
      lame->metadata = g_value_get_boxed (value);
      break;
    default:
      break;
  }

}

static void
gst_lame_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstLame *lame;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_LAME (object));

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
      g_value_set_enum (value, lame->mode);
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
      g_value_set_enum (value, lame->padding_type);
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
      g_value_set_boolean (value, lame->vbr);
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
      g_value_set_int (value, lame->cwlimit);
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
    case ARG_METADATA:
      g_value_set_static_boxed (value, lame->metadata);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_lame_chain (GstPad *pad, GstBuffer *buf)
{
  GstLame *lame;
  GstBuffer *outbuf;
  gchar *mp3_data = NULL;
  gint mp3_buffer_size, mp3_size = 0;
  gboolean eos = FALSE;

  lame = GST_LAME (gst_pad_get_parent (pad));

  GST_DEBUG ("entered chain");

  if (GST_IS_EVENT (buf)) {
    switch (GST_EVENT_TYPE (buf)) {
      case GST_EVENT_EOS:
	eos = TRUE;
      case GST_EVENT_FLUSH:
        mp3_buffer_size = 7200;
        mp3_data = g_malloc (mp3_buffer_size);

        mp3_size = lame_encode_flush (lame->lgf, mp3_data, mp3_buffer_size);
	gst_event_unref (GST_EVENT (buf));
        break;	
      default:
	gst_pad_event_default (pad, GST_EVENT (buf));
	break;
    }
  }
  else {
    gint64 duration;

    if (!lame->initialized) {
      gst_buffer_unref (buf);
      gst_element_error (GST_ELEMENT (lame), "encoder not initialized (input is not audio?)");
      return;
    }

    /* allocate space for output */
    mp3_buffer_size = ((GST_BUFFER_SIZE(buf) / (2+lame->num_channels)) * 1.25) + 7200;
    mp3_data = g_malloc (mp3_buffer_size);

    if (lame->num_channels == 2) {
      mp3_size = lame_encode_buffer_interleaved (lame->lgf, 
		    (short int *) (GST_BUFFER_DATA (buf)),
               	    GST_BUFFER_SIZE (buf) / 4, 
		    mp3_data, mp3_buffer_size);
    }
    else {
      mp3_size = lame_encode_buffer (lame->lgf, 
		    (short int *) (GST_BUFFER_DATA (buf)),
    		    (short int *) (GST_BUFFER_DATA (buf)),
               	    GST_BUFFER_SIZE (buf) / 2, 
		    mp3_data, mp3_buffer_size);
    }

    GST_DEBUG (
	       "encoded %d bytes of audio to %d bytes of mp3", 
	       GST_BUFFER_SIZE (buf), mp3_size);

    duration = (GST_SECOND * GST_BUFFER_SIZE (buf) /
		(2 * lame->samplerate * lame->num_channels));

    if (GST_BUFFER_DURATION (buf) != GST_CLOCK_TIME_NONE &&
	GST_BUFFER_DURATION (buf) != duration)
      GST_DEBUG (
		 "mad: incoming buffer had incorrect duration %lld, "
		 "outgoing buffer will have correct duration %lld",
		 GST_BUFFER_DURATION (buf), duration);

    if (lame->last_ts == GST_CLOCK_TIME_NONE) {
      lame->last_ts       = GST_BUFFER_TIMESTAMP (buf);
      lame->last_offs     = GST_BUFFER_OFFSET (buf);
      lame->last_duration = duration;
    } else {
      lame->last_duration += duration;
    }

    gst_buffer_unref (buf);
  }
  
  if (mp3_size > 0) {
    outbuf = gst_buffer_new ();
    GST_BUFFER_DATA (outbuf)      = mp3_data;
    GST_BUFFER_SIZE (outbuf)      = mp3_size;
    GST_BUFFER_TIMESTAMP (outbuf) = lame->last_ts;
    GST_BUFFER_OFFSET (outbuf)    = lame->last_offs;
    GST_BUFFER_DURATION (outbuf)  = lame->last_duration;

    gst_pad_push (lame->srcpad,outbuf);

    lame->last_ts = GST_CLOCK_TIME_NONE;
  }
  else { 
    g_free (mp3_data);
  }

  if (eos) {
    gst_pad_push (lame->srcpad, GST_BUFFER (gst_event_new (GST_EVENT_EOS)));
    gst_element_set_eos (GST_ELEMENT (lame));
  }
}

/* transition to the READY state by configuring the gst_lame encoder */
static gboolean
gst_lame_setup (GstLame *lame)
{
  GST_DEBUG_OBJECT (lame, "starting setup");

  /* check if we're already initialized; if we are, we might want to check
   * if this initialization is compatible with the previous one */
  /* FIXME: do this */
  if (lame->initialized)
    g_warning ("already initialized");

  /* copy the parameters over */
  lame_set_in_samplerate (lame->lgf, lame->samplerate);

  /* force mono encoding if we only have one channel */
  if (lame->num_channels == 1) 
    lame->mode = 3;

  lame_set_brate (lame->lgf, lame->bitrate);
  lame_set_compression_ratio (lame->lgf, lame->compression_ratio);
  lame_set_quality (lame->lgf, lame->quality);
  lame_set_mode (lame->lgf, lame->mode);
  lame_set_force_ms (lame->lgf, lame->force_ms);
  lame_set_free_format (lame->lgf, lame->free_format);
  lame_set_copyright (lame->lgf, lame->copyright);
  lame_set_original (lame->lgf, lame->original);
  lame_set_error_protection (lame->lgf, lame->error_protection);
  lame_set_padding_type (lame->lgf, lame->padding_type);
  lame_set_extension (lame->lgf, lame->extension);
  lame_set_strict_ISO (lame->lgf, lame->strict_iso);
  lame_set_disable_reservoir (lame->lgf, lame->disable_reservoir);
  lame_set_VBR_q (lame->lgf, lame->vbr);
  lame_set_VBR_mean_bitrate_kbps (lame->lgf, lame->vbr_mean_bitrate);
  lame_set_VBR_min_bitrate_kbps (lame->lgf, lame->vbr_min_bitrate);
  lame_set_VBR_max_bitrate_kbps (lame->lgf, lame->vbr_max_bitrate);
  lame_set_VBR_hard_min (lame->lgf, lame->vbr_hard_min);
  lame_set_lowpassfreq (lame->lgf, lame->lowpass_freq);
  lame_set_lowpasswidth (lame->lgf, lame->lowpass_width);
  lame_set_highpassfreq (lame->lgf, lame->highpass_freq);
  lame_set_highpasswidth (lame->lgf, lame->highpass_width);
  lame_set_ATHonly (lame->lgf, lame->ath_only);
  lame_set_ATHshort (lame->lgf, lame->ath_short);
  lame_set_noATH (lame->lgf, lame->no_ath);
  lame_set_ATHlower (lame->lgf, lame->ath_lower);
  lame_set_cwlimit (lame->lgf, lame->cwlimit);
  lame_set_allow_diff_short (lame->lgf, lame->allow_diff_short);
  lame_set_no_short_blocks (lame->lgf, lame->no_short_blocks);
  lame_set_emphasis (lame->lgf, lame->emphasis);

  gst_lame_add_metadata (lame, lame->metadata);

  /* initialize the lame encoder */
  if (lame_init_params (lame->lgf) < 0) {
    lame->initialized = FALSE;
  }
  else {
    lame->initialized = TRUE;
    /* FIXME: it would be nice to print out the mode here */
    GST_INFO ( 
	      "lame encoder initialized (%d kbit/s, %d Hz, %d channels)", 
	      lame->bitrate, lame->samplerate, lame->num_channels);
  }

  GST_DEBUG_OBJECT (lame, "done with setup");

  return lame->initialized;
}

static GstElementStateReturn
gst_lame_change_state (GstElement *element)
{
  GstLame *lame;
  
  g_return_val_if_fail (GST_IS_LAME (element), GST_STATE_FAILURE);

  lame = GST_LAME (element);

  GST_DEBUG ("state pending %d", GST_STATE_PENDING (element));

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      lame->last_ts = GST_CLOCK_TIME_NONE;
      break;
    case GST_STATE_READY_TO_NULL:
      if (lame->initialized) {
        lame_close (lame->lgf);
	lame->initialized = FALSE;
      }
      break;
    default:
      break;
  }

  /* if we haven't failed already, give the parent class a chance to ;-) */
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the gst_lame element */
  factory = gst_element_factory_new ("lame", GST_TYPE_LAME,
                                    &gst_lame_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  /* register the source's padtemplate */
  gst_element_factory_add_pad_template (factory, 
  		GST_PAD_TEMPLATE_GET (gst_lame_src_factory));

  /* register the sink's padtemplate */
  gst_element_factory_add_pad_template (factory, 
  		GST_PAD_TEMPLATE_GET (gst_lame_sink_factory));

  /* and add the gst_lame element factory to the plugin */
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "lame",
  plugin_init
};
