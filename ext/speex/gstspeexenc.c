/* GStreamer Speex Encoder
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

/**
 * SECTION:element-speexenc
 * @see_also: speexdec, oggmux
 *
 * This element encodes audio as a Speex stream.
 * <ulink url="http://www.speex.org/">Speex</ulink> is a royalty-free
 * audio codec maintained by the <ulink url="http://www.xiph.org/">Xiph.org
 * Foundation</ulink>.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch audiotestsrc num-buffers=100 ! speexenc ! oggmux ! filesink location=beep.ogg
 * ]| Encode an Ogg/Speex file.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <speex/speex.h>
#include <speex/speex_stereo.h>

#include <gst/gsttagsetter.h>
#include <gst/tag/tag.h>
#include <gst/audio/audio.h>
#include "gstspeexenc.h"

GST_DEBUG_CATEGORY_STATIC (speexenc_debug);
#define GST_CAT_DEFAULT speexenc_debug

#define FORMAT_STR GST_AUDIO_NE(S16)

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " FORMAT_STR ", "
        "rate = (int) [ 6000, 48000 ], " "channels = (int) [ 1, 2 ]")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-speex, "
        "rate = (int) [ 6000, 48000 ], " "channels = (int) [ 1, 2]")
    );

#define DEFAULT_QUALITY         8.0
#define DEFAULT_BITRATE         0
#define DEFAULT_MODE            GST_SPEEX_ENC_MODE_AUTO
#define DEFAULT_VBR             FALSE
#define DEFAULT_ABR             0
#define DEFAULT_VAD             FALSE
#define DEFAULT_DTX             FALSE
#define DEFAULT_COMPLEXITY      3
#define DEFAULT_NFRAMES         1

enum
{
  PROP_0,
  PROP_QUALITY,
  PROP_BITRATE,
  PROP_MODE,
  PROP_VBR,
  PROP_ABR,
  PROP_VAD,
  PROP_DTX,
  PROP_COMPLEXITY,
  PROP_NFRAMES,
  PROP_LAST_MESSAGE
};

#define GST_TYPE_SPEEX_ENC_MODE (gst_speex_enc_mode_get_type())
static GType
gst_speex_enc_mode_get_type (void)
{
  static GType speex_enc_mode_type = 0;
  static const GEnumValue speex_enc_modes[] = {
    {GST_SPEEX_ENC_MODE_AUTO, "Auto", "auto"},
    {GST_SPEEX_ENC_MODE_UWB, "Ultra Wide Band", "uwb"},
    {GST_SPEEX_ENC_MODE_WB, "Wide Band", "wb"},
    {GST_SPEEX_ENC_MODE_NB, "Narrow Band", "nb"},
    {0, NULL, NULL},
  };
  if (G_UNLIKELY (speex_enc_mode_type == 0)) {
    speex_enc_mode_type = g_enum_register_static ("GstSpeexEncMode",
        speex_enc_modes);
  }
  return speex_enc_mode_type;
}

#if 0
static const GstFormat *
gst_speex_enc_get_formats (GstPad * pad)
{
  static const GstFormat src_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_TIME,
    0
  };
  static const GstFormat sink_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,
    GST_FORMAT_TIME,
    0
  };

  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
}
#endif

static void gst_speex_enc_finalize (GObject * object);

static gboolean gst_speex_enc_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_speex_enc_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_speex_enc_setup (GstSpeexEnc * enc);

static void gst_speex_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_speex_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_speex_enc_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_speex_enc_encode (GstSpeexEnc * enc, gboolean flush);

#define gst_speex_enc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstSpeexEnc, gst_speex_enc, GST_TYPE_ELEMENT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_TAG_SETTER, NULL);
    G_IMPLEMENT_INTERFACE (GST_TYPE_PRESET, NULL));

static void
gst_speex_enc_class_init (GstSpeexEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_speex_enc_finalize;
  gobject_class->set_property = gst_speex_enc_set_property;
  gobject_class->get_property = gst_speex_enc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_QUALITY,
      g_param_spec_float ("quality", "Quality", "Encoding quality",
          0.0, 10.0, DEFAULT_QUALITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BITRATE,
      g_param_spec_int ("bitrate", "Encoding Bit-rate",
          "Specify an encoding bit-rate (in bps). (0 = automatic)",
          0, G_MAXINT, DEFAULT_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode", "The encoding mode",
          GST_TYPE_SPEEX_ENC_MODE, GST_SPEEX_ENC_MODE_AUTO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VBR,
      g_param_spec_boolean ("vbr", "VBR",
          "Enable variable bit-rate", DEFAULT_VBR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ABR,
      g_param_spec_int ("abr", "ABR",
          "Enable average bit-rate (0 = disabled)",
          0, G_MAXINT, DEFAULT_ABR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VAD,
      g_param_spec_boolean ("vad", "VAD",
          "Enable voice activity detection", DEFAULT_VAD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DTX,
      g_param_spec_boolean ("dtx", "DTX",
          "Enable discontinuous transmission", DEFAULT_DTX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COMPLEXITY,
      g_param_spec_int ("complexity", "Complexity",
          "Set encoding complexity",
          0, G_MAXINT, DEFAULT_COMPLEXITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_NFRAMES,
      g_param_spec_int ("nframes", "NFrames",
          "Number of frames per buffer",
          0, G_MAXINT, DEFAULT_NFRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LAST_MESSAGE,
      g_param_spec_string ("last-message", "last-message",
          "The last status message", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_speex_enc_change_state);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details_simple (gstelement_class, "Speex audio encoder",
      "Codec/Encoder/Audio",
      "Encodes audio in Speex format", "Wim Taymans <wim@fluendo.com>");

  GST_DEBUG_CATEGORY_INIT (speexenc_debug, "speexenc", 0, "Speex encoder");
}

static void
gst_speex_enc_finalize (GObject * object)
{
  GstSpeexEnc *enc;

  enc = GST_SPEEX_ENC (object);

  g_free (enc->last_message);
  g_object_unref (enc->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_speex_enc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSpeexEnc *enc;
  GstStructure *structure;

  enc = GST_SPEEX_ENC (GST_PAD_PARENT (pad));
  enc->setup = FALSE;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "channels", &enc->channels);
  gst_structure_get_int (structure, "rate", &enc->rate);

  gst_speex_enc_setup (enc);

  return enc->setup;
}


static GstCaps *
gst_speex_enc_sink_getcaps (GstPad * pad, GstCaps * filter)
{
  GstCaps *caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  GstCaps *peercaps = NULL;
  GstSpeexEnc *enc = GST_SPEEX_ENC (gst_pad_get_parent_element (pad));

  peercaps = gst_pad_peer_get_caps (enc->srcpad, filter);

  if (peercaps) {
    if (!gst_caps_is_empty (peercaps) && !gst_caps_is_any (peercaps)) {
      GstStructure *ps = gst_caps_get_structure (peercaps, 0);
      GstStructure *s = gst_caps_get_structure (caps, 0);
      gint rate, channels;

      if (gst_structure_get_int (ps, "rate", &rate)) {
        gst_structure_fixate_field_nearest_int (s, "rate", rate);
      }

      if (gst_structure_get_int (ps, "channels", &channels)) {
        gst_structure_fixate_field_nearest_int (s, "channels", channels);
      }
    }
    gst_caps_unref (peercaps);
  }

  gst_object_unref (enc);

  return caps;
}


static gboolean
gst_speex_enc_convert_src (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstSpeexEnc *enc;
  gint64 avg;

  enc = GST_SPEEX_ENC (GST_PAD_PARENT (pad));

  if (enc->samples_in == 0 || enc->bytes_out == 0 || enc->rate == 0)
    return FALSE;

  avg = (enc->bytes_out * enc->rate) / (enc->samples_in);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / avg;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * avg / GST_SECOND;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}

static gboolean
gst_speex_enc_convert_sink (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  gint bytes_per_sample;
  GstSpeexEnc *enc;

  enc = GST_SPEEX_ENC (GST_PAD_PARENT (pad));

  bytes_per_sample = enc->channels * 2;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          if (bytes_per_sample == 0)
            return FALSE;
          *dest_value = src_value / bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
        {
          gint byterate = bytes_per_sample * enc->rate;

          if (byterate == 0)
            return FALSE;
          *dest_value = src_value * GST_SECOND / byterate;
          break;
        }
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
          if (enc->rate == 0)
            return FALSE;
          *dest_value = src_value * GST_SECOND / enc->rate;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = bytes_per_sample;
          /* fallthrough */
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * scale * enc->rate / GST_SECOND;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}

static gint64
gst_speex_enc_get_latency (GstSpeexEnc * enc)
{
  /* See the Speex manual section "Latency and algorithmic delay" */
  if (enc->rate == 8000)
    return 30 * GST_MSECOND;
  else
    return 34 * GST_MSECOND;
}

static const GstQueryType *
gst_speex_enc_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_speex_enc_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    GST_QUERY_LATENCY,
    0
  };

  return gst_speex_enc_src_query_types;
}

static gboolean
gst_speex_enc_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstSpeexEnc *enc;

  enc = GST_SPEEX_ENC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat req_fmt;
      gint64 pos, val;

      gst_query_parse_position (query, &req_fmt, NULL);
      if ((res = gst_pad_query_peer_position (enc->sinkpad, req_fmt, &val))) {
        gst_query_set_position (query, req_fmt, val);
        break;
      }

      res = gst_pad_query_peer_position (enc->sinkpad, GST_FORMAT_TIME, &pos);
      if (!res)
        break;

      if ((res =
              gst_pad_query_peer_convert (enc->sinkpad, GST_FORMAT_TIME, pos,
                  req_fmt, &val))) {
        gst_query_set_position (query, req_fmt, val);
      }
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat req_fmt;
      gint64 dur, val;

      gst_query_parse_duration (query, &req_fmt, NULL);
      if ((res = gst_pad_query_peer_duration (enc->sinkpad, req_fmt, &val))) {
        gst_query_set_duration (query, req_fmt, val);
        break;
      }

      res = gst_pad_query_peer_duration (enc->sinkpad, GST_FORMAT_TIME, &dur);
      if (!res)
        break;

      if ((res =
              gst_pad_query_peer_convert (enc->sinkpad, GST_FORMAT_TIME, dur,
                  req_fmt, &val))) {
        gst_query_set_duration (query, req_fmt, val);
      }
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res = gst_speex_enc_convert_src (pad, src_fmt, src_val, dest_fmt,
                  &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    case GST_QUERY_LATENCY:
    {
      gboolean live;
      GstClockTime min_latency, max_latency;
      gint64 latency;

      if ((res = gst_pad_peer_query (enc->sinkpad, query))) {
        gst_query_parse_latency (query, &live, &min_latency, &max_latency);
        GST_LOG_OBJECT (pad, "Upstream latency: %" GST_PTR_FORMAT, query);

        latency = gst_speex_enc_get_latency (enc);

        /* add our latency */
        min_latency += latency;
        if (max_latency != -1)
          max_latency += latency;

        gst_query_set_latency (query, live, min_latency, max_latency);
        GST_LOG_OBJECT (pad, "Adjusted latency: %" GST_PTR_FORMAT, query);
      }
      break;
    }
    default:
      res = gst_pad_peer_query (enc->sinkpad, query);
      break;
  }

error:

  gst_object_unref (enc);

  return res;
}

static gboolean
gst_speex_enc_sink_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_speex_enc_convert_sink (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

error:
  return res;
}

static void
gst_speex_enc_init (GstSpeexEnc * enc)
{
  enc->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);
  gst_pad_set_event_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_speex_enc_sink_event));
  gst_pad_set_chain_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_speex_enc_chain));
  gst_pad_set_getcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_speex_enc_sink_getcaps));
  gst_pad_set_query_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_speex_enc_sink_query));

  enc->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_query_function (enc->srcpad,
      GST_DEBUG_FUNCPTR (gst_speex_enc_src_query));
  gst_pad_set_query_type_function (enc->srcpad,
      GST_DEBUG_FUNCPTR (gst_speex_enc_get_query_types));
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  enc->channels = -1;
  enc->rate = -1;

  enc->quality = DEFAULT_QUALITY;
  enc->bitrate = DEFAULT_BITRATE;
  enc->mode = DEFAULT_MODE;
  enc->vbr = DEFAULT_VBR;
  enc->abr = DEFAULT_ABR;
  enc->vad = DEFAULT_VAD;
  enc->dtx = DEFAULT_DTX;
  enc->complexity = DEFAULT_COMPLEXITY;
  enc->nframes = DEFAULT_NFRAMES;

  enc->setup = FALSE;
  enc->header_sent = FALSE;

  enc->adapter = gst_adapter_new ();
}

static GstBuffer *
gst_speex_enc_create_metadata_buffer (GstSpeexEnc * enc)
{
  const GstTagList *user_tags;
  GstTagList *merged_tags;
  GstBuffer *comments = NULL;

  user_tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (enc));

  GST_DEBUG_OBJECT (enc, "upstream tags = %" GST_PTR_FORMAT, enc->tags);
  GST_DEBUG_OBJECT (enc, "user-set tags = %" GST_PTR_FORMAT, user_tags);

  /* gst_tag_list_merge() will handle NULL for either or both lists fine */
  merged_tags = gst_tag_list_merge (user_tags, enc->tags,
      gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (enc)));

  if (merged_tags == NULL)
    merged_tags = gst_tag_list_new ();

  GST_DEBUG_OBJECT (enc, "merged   tags = %" GST_PTR_FORMAT, merged_tags);
  comments = gst_tag_list_to_vorbiscomment_buffer (merged_tags, NULL,
      0, "Encoded with GStreamer Speexenc");
  gst_tag_list_free (merged_tags);

  GST_BUFFER_OFFSET (comments) = enc->bytes_out;
  GST_BUFFER_OFFSET_END (comments) = 0;

  return comments;
}

static void
gst_speex_enc_set_last_msg (GstSpeexEnc * enc, const gchar * msg)
{
  g_free (enc->last_message);
  enc->last_message = g_strdup (msg);
  GST_WARNING_OBJECT (enc, "%s", msg);
  g_object_notify (G_OBJECT (enc), "last-message");
}

static gboolean
gst_speex_enc_setup (GstSpeexEnc * enc)
{
  enc->setup = FALSE;

  switch (enc->mode) {
    case GST_SPEEX_ENC_MODE_UWB:
      GST_LOG_OBJECT (enc, "configuring for requested UWB mode");
      enc->speex_mode = speex_lib_get_mode (SPEEX_MODEID_UWB);
      break;
    case GST_SPEEX_ENC_MODE_WB:
      GST_LOG_OBJECT (enc, "configuring for requested WB mode");
      enc->speex_mode = speex_lib_get_mode (SPEEX_MODEID_WB);
      break;
    case GST_SPEEX_ENC_MODE_NB:
      GST_LOG_OBJECT (enc, "configuring for requested NB mode");
      enc->speex_mode = speex_lib_get_mode (SPEEX_MODEID_NB);
      break;
    case GST_SPEEX_ENC_MODE_AUTO:
      /* fall through */
      GST_LOG_OBJECT (enc, "finding best mode");
    default:
      break;
  }

  if (enc->rate > 25000) {
    if (enc->mode == GST_SPEEX_ENC_MODE_AUTO) {
      GST_LOG_OBJECT (enc, "selected UWB mode for samplerate %d", enc->rate);
      enc->speex_mode = speex_lib_get_mode (SPEEX_MODEID_UWB);
    } else {
      if (enc->speex_mode != speex_lib_get_mode (SPEEX_MODEID_UWB)) {
        gst_speex_enc_set_last_msg (enc,
            "Warning: suggest to use ultra wide band mode for this rate");
      }
    }
  } else if (enc->rate > 12500) {
    if (enc->mode == GST_SPEEX_ENC_MODE_AUTO) {
      GST_LOG_OBJECT (enc, "selected WB mode for samplerate %d", enc->rate);
      enc->speex_mode = speex_lib_get_mode (SPEEX_MODEID_WB);
    } else {
      if (enc->speex_mode != speex_lib_get_mode (SPEEX_MODEID_WB)) {
        gst_speex_enc_set_last_msg (enc,
            "Warning: suggest to use wide band mode for this rate");
      }
    }
  } else {
    if (enc->mode == GST_SPEEX_ENC_MODE_AUTO) {
      GST_LOG_OBJECT (enc, "selected NB mode for samplerate %d", enc->rate);
      enc->speex_mode = speex_lib_get_mode (SPEEX_MODEID_NB);
    } else {
      if (enc->speex_mode != speex_lib_get_mode (SPEEX_MODEID_NB)) {
        gst_speex_enc_set_last_msg (enc,
            "Warning: suggest to use narrow band mode for this rate");
      }
    }
  }

  if (enc->rate != 8000 && enc->rate != 16000 && enc->rate != 32000) {
    gst_speex_enc_set_last_msg (enc,
        "Warning: speex is optimized for 8, 16 and 32 KHz");
  }

  speex_init_header (&enc->header, enc->rate, 1, enc->speex_mode);
  enc->header.frames_per_packet = enc->nframes;
  enc->header.vbr = enc->vbr;
  enc->header.nb_channels = enc->channels;

  /*Initialize Speex encoder */
  enc->state = speex_encoder_init (enc->speex_mode);

  speex_encoder_ctl (enc->state, SPEEX_GET_FRAME_SIZE, &enc->frame_size);
  speex_encoder_ctl (enc->state, SPEEX_SET_COMPLEXITY, &enc->complexity);
  speex_encoder_ctl (enc->state, SPEEX_SET_SAMPLING_RATE, &enc->rate);

  if (enc->vbr)
    speex_encoder_ctl (enc->state, SPEEX_SET_VBR_QUALITY, &enc->quality);
  else {
    gint tmp = floor (enc->quality);

    speex_encoder_ctl (enc->state, SPEEX_SET_QUALITY, &tmp);
  }
  if (enc->bitrate) {
    if (enc->quality >= 0.0 && enc->vbr) {
      gst_speex_enc_set_last_msg (enc,
          "Warning: bitrate option is overriding quality");
    }
    speex_encoder_ctl (enc->state, SPEEX_SET_BITRATE, &enc->bitrate);
  }
  if (enc->vbr) {
    gint tmp = 1;

    speex_encoder_ctl (enc->state, SPEEX_SET_VBR, &tmp);
  } else if (enc->vad) {
    gint tmp = 1;

    speex_encoder_ctl (enc->state, SPEEX_SET_VAD, &tmp);
  }

  if (enc->dtx) {
    gint tmp = 1;

    speex_encoder_ctl (enc->state, SPEEX_SET_DTX, &tmp);
  }

  if (enc->dtx && !(enc->vbr || enc->abr || enc->vad)) {
    gst_speex_enc_set_last_msg (enc,
        "Warning: dtx is useless without vad, vbr or abr");
  } else if ((enc->vbr || enc->abr) && (enc->vad)) {
    gst_speex_enc_set_last_msg (enc,
        "Warning: vad is already implied by vbr or abr");
  }

  if (enc->abr) {
    speex_encoder_ctl (enc->state, SPEEX_SET_ABR, &enc->abr);
  }

  speex_encoder_ctl (enc->state, SPEEX_GET_LOOKAHEAD, &enc->lookahead);

  GST_LOG_OBJECT (enc, "we have frame size %d, lookahead %d", enc->frame_size,
      enc->lookahead);

  enc->setup = TRUE;

  return TRUE;
}

/* prepare a buffer for transmission */
static GstBuffer *
gst_speex_enc_buffer_from_data (GstSpeexEnc * enc, guchar * data,
    gint data_len, guint64 granulepos)
{
  GstBuffer *outbuf;

  outbuf = gst_buffer_new_and_alloc (data_len);
  gst_buffer_fill (outbuf, 0, data, data_len);
  GST_BUFFER_OFFSET (outbuf) = enc->bytes_out;
  GST_BUFFER_OFFSET_END (outbuf) = granulepos;

  GST_LOG_OBJECT (enc, "encoded buffer of %d bytes", data_len);
  return outbuf;
}


/* push out the buffer and do internal bookkeeping */
static GstFlowReturn
gst_speex_enc_push_buffer (GstSpeexEnc * enc, GstBuffer * buffer)
{
  guint size;

  size = gst_buffer_get_size (buffer);
  enc->bytes_out += size;

  GST_DEBUG_OBJECT (enc, "pushing output buffer of size %u", size);

  return gst_pad_push (enc->srcpad, buffer);
}

static GstCaps *
gst_speex_enc_set_header_on_caps (GstCaps * caps, GstBuffer * buf1,
    GstBuffer * buf2)
{
  GstStructure *structure = NULL;
  GstBuffer *buf;
  GValue array = { 0 };
  GValue value = { 0 };

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  g_assert (gst_buffer_is_writable (buf1));
  g_assert (gst_buffer_is_writable (buf2));

  /* mark buffers */
  GST_BUFFER_FLAG_SET (buf1, GST_BUFFER_FLAG_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf2, GST_BUFFER_FLAG_IN_CAPS);

  /* put buffers in a fixed list */
  g_value_init (&array, GST_TYPE_ARRAY);
  g_value_init (&value, GST_TYPE_BUFFER);
  buf = gst_buffer_copy (buf1);
  gst_value_set_buffer (&value, buf);
  gst_buffer_unref (buf);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_BUFFER);
  buf = gst_buffer_copy (buf2);
  gst_value_set_buffer (&value, buf);
  gst_buffer_unref (buf);
  gst_value_array_append_value (&array, &value);
  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&value);
  g_value_unset (&array);

  return caps;
}


static gboolean
gst_speex_enc_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstSpeexEnc *enc;

  enc = GST_SPEEX_ENC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_speex_enc_sink_setcaps (pad, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_EOS:
      if (enc->setup)
        gst_speex_enc_encode (enc, TRUE);
      res = gst_pad_event_default (pad, event);
      break;
    case GST_EVENT_TAG:
    {
      if (enc->tags) {
        GstTagList *list;

        gst_event_parse_tag (event, &list);
        gst_tag_list_insert (enc->tags, list,
            gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (enc)));
      } else {
        g_assert_not_reached ();
      }
      res = gst_pad_event_default (pad, event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (enc);

  return res;
}

static GstFlowReturn
gst_speex_enc_encode (GstSpeexEnc * enc, gboolean flush)
{
  gint frame_size = enc->frame_size;
  gint bytes = frame_size * 2 * enc->channels;
  GstFlowReturn ret = GST_FLOW_OK;

  if (flush && gst_adapter_available (enc->adapter) % bytes != 0) {
    guint diff = gst_adapter_available (enc->adapter) % bytes;
    GstBuffer *buf = gst_buffer_new_and_alloc (diff);
    gst_buffer_memset (buf, 0, 0, diff);
    gst_adapter_push (enc->adapter, buf);
  }

  while (gst_adapter_available (enc->adapter) >= bytes) {
    gint16 *data;
    gint outsize, written, dtx_ret;
    GstBuffer *outbuf;
    gchar *outdata;

    data = (gint16 *) gst_adapter_take (enc->adapter, bytes);

    enc->samples_in += frame_size;

    GST_DEBUG_OBJECT (enc, "encoding %d samples (%d bytes)", frame_size, bytes);

    if (enc->channels == 2) {
      speex_encode_stereo_int (data, frame_size, &enc->bits);
    }
    dtx_ret = speex_encode_int (enc->state, data, &enc->bits);

    g_free (data);

    enc->frameno++;
    enc->frameno_out++;

    if ((enc->frameno % enc->nframes) != 0)
      continue;

    speex_bits_insert_terminator (&enc->bits);
    outsize = speex_bits_nbytes (&enc->bits);

#if 0
    ret = gst_pad_alloc_buffer_and_set_caps (enc->srcpad,
        GST_BUFFER_OFFSET_NONE, outsize, GST_PAD_CAPS (enc->srcpad), &outbuf);
    if ((GST_FLOW_OK != ret))
      goto done;
#endif
    outbuf = gst_buffer_new_allocate (NULL, outsize, 0);

    outdata = gst_buffer_map (outbuf, NULL, NULL, GST_MAP_WRITE);
    written = speex_bits_write (&enc->bits, outdata, outsize);

    if (G_UNLIKELY (written != outsize)) {
      GST_ERROR_OBJECT (enc, "short write: %d < %d bytes", written, outsize);
    }
    gst_buffer_unmap (outbuf, outdata, written);

    speex_bits_reset (&enc->bits);

    if (!dtx_ret)
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_GAP);

    GST_BUFFER_TIMESTAMP (outbuf) = enc->start_ts +
        gst_util_uint64_scale_int ((enc->frameno_out -
            enc->nframes) * frame_size - enc->lookahead, GST_SECOND, enc->rate);
    GST_BUFFER_DURATION (outbuf) =
        gst_util_uint64_scale_int (frame_size * enc->nframes, GST_SECOND,
        enc->rate);
    /* set gp time and granulepos; see gst-plugins-base/ext/ogg/README */
    GST_BUFFER_OFFSET_END (outbuf) = enc->granulepos_offset +
        ((enc->frameno_out) * frame_size - enc->lookahead);
    GST_BUFFER_OFFSET (outbuf) =
        gst_util_uint64_scale_int (GST_BUFFER_OFFSET_END (outbuf), GST_SECOND,
        enc->rate);

    ret = gst_speex_enc_push_buffer (enc, outbuf);

    if ((GST_FLOW_OK != ret) && (GST_FLOW_NOT_LINKED != ret))
      goto done;
  }

done:

  return ret;
}

static GstFlowReturn
gst_speex_enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstSpeexEnc *enc;
  GstFlowReturn ret = GST_FLOW_OK;

  enc = GST_SPEEX_ENC (GST_PAD_PARENT (pad));

  if (!enc->setup)
    goto not_setup;

  if (!enc->header_sent) {
    /* Speex streams begin with two headers; the initial header (with
       most of the codec setup parameters) which is mandated by the Ogg
       bitstream spec.  The second header holds any comment fields.
       We merely need to make the headers, then pass them to libspeex 
       one at a time; libspeex handles the additional Ogg bitstream 
       constraints */
    GstBuffer *buf1, *buf2;
    GstCaps *caps;
    guchar *data;
    gint data_len;

    /* create header buffer */
    data = (guint8 *) speex_header_to_packet (&enc->header, &data_len);
    buf1 = gst_speex_enc_buffer_from_data (enc, data, data_len, 0);
    free (data);

    /* create comment buffer */
    buf2 = gst_speex_enc_create_metadata_buffer (enc);

    /* mark and put on caps */
    caps = gst_pad_get_caps (enc->srcpad, NULL);
    caps = gst_speex_enc_set_header_on_caps (caps, buf1, buf2);

    gst_caps_set_simple (caps,
        "rate", G_TYPE_INT, enc->rate,
        "channels", G_TYPE_INT, enc->channels, NULL);

    /* negotiate with these caps */
    GST_DEBUG_OBJECT (enc, "here are the caps: %" GST_PTR_FORMAT, caps);
    gst_pad_set_caps (enc->srcpad, caps);
    gst_caps_unref (caps);

    /* push out buffers */
    ret = gst_speex_enc_push_buffer (enc, buf1);

    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (buf2);
      goto done;
    }

    ret = gst_speex_enc_push_buffer (enc, buf2);

    if (ret != GST_FLOW_OK)
      goto done;

    speex_bits_reset (&enc->bits);

    enc->header_sent = TRUE;
  }

  /* Save the timestamp of the first buffer. This will be later
   * used as offset for all following buffers */
  if (enc->start_ts == GST_CLOCK_TIME_NONE) {
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
      enc->start_ts = GST_BUFFER_TIMESTAMP (buf);
      enc->granulepos_offset = gst_util_uint64_scale
          (GST_BUFFER_TIMESTAMP (buf), enc->rate, GST_SECOND);
    } else {
      enc->start_ts = 0;
      enc->granulepos_offset = 0;
    }
  }

  /* Check if we have a continous stream, if not drop some samples or the buffer or
   * insert some silence samples */
  if (enc->next_ts != GST_CLOCK_TIME_NONE &&
      GST_BUFFER_TIMESTAMP_IS_VALID (buf) &&
      GST_BUFFER_TIMESTAMP (buf) < enc->next_ts) {
    guint64 diff = enc->next_ts - GST_BUFFER_TIMESTAMP (buf);
    guint64 diff_bytes;

    GST_WARNING_OBJECT (enc, "Buffer is older than previous "
        "timestamp + duration (%" GST_TIME_FORMAT "< %" GST_TIME_FORMAT
        "), cannot handle. Clipping buffer.",
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (enc->next_ts));

    diff_bytes = GST_CLOCK_TIME_TO_FRAMES (diff, enc->rate) * enc->channels * 2;
    if (diff_bytes >= gst_buffer_get_size (buf)) {
      gst_buffer_unref (buf);
      return GST_FLOW_OK;
    }
    buf = gst_buffer_make_writable (buf);
    gst_buffer_resize (buf, diff_bytes, -1);

    GST_BUFFER_TIMESTAMP (buf) += diff;
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      GST_BUFFER_DURATION (buf) -= diff;
  }

  if (enc->next_ts != GST_CLOCK_TIME_NONE
      && GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    guint64 max_diff =
        gst_util_uint64_scale (enc->frame_size, GST_SECOND, enc->rate);

    if (GST_BUFFER_TIMESTAMP (buf) != enc->next_ts &&
        GST_BUFFER_TIMESTAMP (buf) - enc->next_ts > max_diff) {
      GST_WARNING_OBJECT (enc,
          "Discontinuity detected: %" G_GUINT64_FORMAT " > %" G_GUINT64_FORMAT,
          GST_BUFFER_TIMESTAMP (buf) - enc->next_ts, max_diff);

      gst_speex_enc_encode (enc, TRUE);

      enc->frameno_out = 0;
      enc->start_ts = GST_BUFFER_TIMESTAMP (buf);
      enc->granulepos_offset = gst_util_uint64_scale
          (GST_BUFFER_TIMESTAMP (buf), enc->rate, GST_SECOND);
    }
  }

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)
      && GST_BUFFER_DURATION_IS_VALID (buf))
    enc->next_ts = GST_BUFFER_TIMESTAMP (buf) + GST_BUFFER_DURATION (buf);
  else
    enc->next_ts = GST_CLOCK_TIME_NONE;

  GST_DEBUG_OBJECT (enc, "received buffer of %u bytes",
      gst_buffer_get_size (buf));

  /* push buffer to adapter */
  gst_adapter_push (enc->adapter, buf);
  buf = NULL;

  ret = gst_speex_enc_encode (enc, FALSE);

done:

  if (buf)
    gst_buffer_unref (buf);

  return ret;

  /* ERRORS */
not_setup:
  {
    GST_ELEMENT_ERROR (enc, CORE, NEGOTIATION, (NULL),
        ("encoder not initialized (input is not audio?)"));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }

}


static void
gst_speex_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSpeexEnc *enc;

  enc = GST_SPEEX_ENC (object);

  switch (prop_id) {
    case PROP_QUALITY:
      g_value_set_float (value, enc->quality);
      break;
    case PROP_BITRATE:
      g_value_set_int (value, enc->bitrate);
      break;
    case PROP_MODE:
      g_value_set_enum (value, enc->mode);
      break;
    case PROP_VBR:
      g_value_set_boolean (value, enc->vbr);
      break;
    case PROP_ABR:
      g_value_set_int (value, enc->abr);
      break;
    case PROP_VAD:
      g_value_set_boolean (value, enc->vad);
      break;
    case PROP_DTX:
      g_value_set_boolean (value, enc->dtx);
      break;
    case PROP_COMPLEXITY:
      g_value_set_int (value, enc->complexity);
      break;
    case PROP_NFRAMES:
      g_value_set_int (value, enc->nframes);
      break;
    case PROP_LAST_MESSAGE:
      g_value_set_string (value, enc->last_message);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_speex_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSpeexEnc *enc;

  enc = GST_SPEEX_ENC (object);

  switch (prop_id) {
    case PROP_QUALITY:
      enc->quality = g_value_get_float (value);
      break;
    case PROP_BITRATE:
      enc->bitrate = g_value_get_int (value);
      break;
    case PROP_MODE:
      enc->mode = g_value_get_enum (value);
      break;
    case PROP_VBR:
      enc->vbr = g_value_get_boolean (value);
      break;
    case PROP_ABR:
      enc->abr = g_value_get_int (value);
      break;
    case PROP_VAD:
      enc->vad = g_value_get_boolean (value);
      break;
    case PROP_DTX:
      enc->dtx = g_value_get_boolean (value);
      break;
    case PROP_COMPLEXITY:
      enc->complexity = g_value_get_int (value);
      break;
    case PROP_NFRAMES:
      enc->nframes = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_speex_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstSpeexEnc *enc = GST_SPEEX_ENC (element);
  GstStateChangeReturn res;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      enc->tags = gst_tag_list_new ();
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      speex_bits_init (&enc->bits);
      enc->frameno = 0;
      enc->frameno_out = 0;
      enc->samples_in = 0;
      enc->start_ts = GST_CLOCK_TIME_NONE;
      enc->next_ts = GST_CLOCK_TIME_NONE;
      enc->granulepos_offset = 0;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* fall through */
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (res == GST_STATE_CHANGE_FAILURE)
    return res;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      enc->setup = FALSE;
      enc->header_sent = FALSE;
      if (enc->state) {
        speex_encoder_destroy (enc->state);
        enc->state = NULL;
      }
      speex_bits_destroy (&enc->bits);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_tag_list_free (enc->tags);
      enc->tags = NULL;
    default:
      break;
  }

  return res;
}
