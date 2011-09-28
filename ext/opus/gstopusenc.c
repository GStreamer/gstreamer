/* GStreamer Opus Encoder
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/*
 * Based on the speexenc element
 */

/**
 * SECTION:element-opusenc
 * @see_also: opusdec, oggmux
 *
 * This element encodes raw audio to OPUS.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v audiotestsrc wave=sine num-buffers=100 ! audioconvert ! opusenc ! oggmux ! filesink location=sine.ogg
 * ]| Encode a test sine signal to Ogg/OPUS.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <opus/opus.h>

#include <gst/gsttagsetter.h>
#include <gst/tag/tag.h>
#include <gst/audio/audio.h>
#include "gstopusenc.h"

GST_DEBUG_CATEGORY_STATIC (opusenc_debug);
#define GST_CAT_DEFAULT opusenc_debug

#define GST_OPUS_ENC_TYPE_BANDWIDTH (gst_opus_enc_bandwidth_get_type())
static GType
gst_opus_enc_bandwidth_get_type (void)
{
  static const GEnumValue values[] = {
    {OPUS_BANDWIDTH_NARROWBAND, "Narrow band", "narrowband"},
    {OPUS_BANDWIDTH_MEDIUMBAND, "Medium band", "mediumband"},
    {OPUS_BANDWIDTH_WIDEBAND, "Wide band", "wideband"},
    {OPUS_BANDWIDTH_SUPERWIDEBAND, "Super wide band", "superwideband"},
    {OPUS_BANDWIDTH_FULLBAND, "Full band", "fullband"},
    {OPUS_AUTO, "Auto", "auto"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstOpusEncBandwidth", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) { 8000, 12000, 16000, 24000, 48000 }, "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, " "width = (int) 16, " "depth = (int) 16")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-opus, "
        "rate = (int) { 8000, 12000, 16000, 24000, 48000 }, "
        "channels = (int) [ 1, 2 ], " "frame-size = (int) [ 2, 60 ]")
    );

#define DEFAULT_AUDIO           TRUE
#define DEFAULT_BITRATE         64000
#define DEFAULT_BANDWIDTH       OPUS_BANDWIDTH_FULLBAND
#define DEFAULT_FRAMESIZE       20
#define DEFAULT_CBR             TRUE
#define DEFAULT_CONSTRAINED_VBR TRUE
#define DEFAULT_COMPLEXITY      10
#define DEFAULT_INBAND_FEC      FALSE
#define DEFAULT_DTX             FALSE
#define DEFAULT_PACKET_LOSS_PERCENT 0

enum
{
  PROP_0,
  PROP_AUDIO,
  PROP_BITRATE,
  PROP_BANDWIDTH,
  PROP_FRAME_SIZE,
  PROP_CBR,
  PROP_CONSTRAINED_VBR,
  PROP_COMPLEXITY,
  PROP_INBAND_FEC,
  PROP_DTX,
  PROP_PACKET_LOSS_PERCENT
};

static void gst_opus_enc_finalize (GObject * object);

static gboolean gst_opus_enc_sinkevent (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_opus_enc_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_opus_enc_setup (GstOpusEnc * enc);

static void gst_opus_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_opus_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_opus_enc_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_opus_enc_encode (GstOpusEnc * enc, gboolean flush);

static void
gst_opus_enc_setup_interfaces (GType opusenc_type)
{
  static const GInterfaceInfo tag_setter_info = { NULL, NULL, NULL };
  const GInterfaceInfo preset_interface_info = {
    NULL,                       /* interface_init */
    NULL,                       /* interface_finalize */
    NULL                        /* interface_data */
  };

  g_type_add_interface_static (opusenc_type, GST_TYPE_TAG_SETTER,
      &tag_setter_info);
  g_type_add_interface_static (opusenc_type, GST_TYPE_PRESET,
      &preset_interface_info);

  GST_DEBUG_CATEGORY_INIT (opusenc_debug, "opusenc", 0, "Opus encoder");
}

GST_BOILERPLATE_FULL (GstOpusEnc, gst_opus_enc, GstElement, GST_TYPE_ELEMENT,
    gst_opus_enc_setup_interfaces);

static void
gst_opus_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details_simple (element_class, "Opus audio encoder",
      "Codec/Encoder/Audio",
      "Encodes audio in Opus format",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_opus_enc_class_init (GstOpusEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_opus_enc_set_property;
  gobject_class->get_property = gst_opus_enc_get_property;

  g_object_class_install_property (gobject_class, PROP_AUDIO,
      g_param_spec_boolean ("audio", "Audio or voice",
          "Audio or voice", DEFAULT_AUDIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BITRATE,
      g_param_spec_int ("bitrate", "Encoding Bit-rate",
          "Specify an encoding bit-rate (in bps).",
          1, 320000, DEFAULT_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BANDWIDTH,
      g_param_spec_enum ("bandwidth", "Band Width",
          "Audio Band Width", GST_OPUS_ENC_TYPE_BANDWIDTH, DEFAULT_BANDWIDTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FRAME_SIZE,
      g_param_spec_int ("frame-size", "Frame Size",
          "The duration of an audio frame, in ms", 2, 60, DEFAULT_FRAMESIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CBR,
      g_param_spec_boolean ("cbr", "Constant bit rate",
          "Constant bit rate", DEFAULT_CBR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CONSTRAINED_VBR,
      g_param_spec_boolean ("constrained-cbr", "Constrained VBR",
          "Constrained VBR", DEFAULT_CONSTRAINED_VBR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_COMPLEXITY,
      g_param_spec_int ("complexity", "Complexity",
          "Complexity", 0, 10, DEFAULT_COMPLEXITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INBAND_FEC,
      g_param_spec_boolean ("inband-fec", "In-band FEC",
          "Enable forward error correction", DEFAULT_INBAND_FEC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DTX,
      g_param_spec_boolean ("dtx", "DTX",
          "DTX", DEFAULT_DTX, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_PACKET_LOSS_PERCENT, g_param_spec_int ("packet-loss-percentage",
          "Loss percentage", "Packet loss percentage", 0, 100,
          DEFAULT_PACKET_LOSS_PERCENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_opus_enc_finalize);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_opus_enc_change_state);
}

static void
gst_opus_enc_finalize (GObject * object)
{
  GstOpusEnc *enc;

  enc = GST_OPUS_ENC (object);

  g_object_unref (enc->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_opus_enc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstOpusEnc *enc;
  GstStructure *structure;
  GstCaps *otherpadcaps;

  enc = GST_OPUS_ENC (GST_PAD_PARENT (pad));
  enc->setup = FALSE;
  enc->frame_size = DEFAULT_FRAMESIZE;
  otherpadcaps = gst_pad_get_allowed_caps (pad);

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "channels", &enc->n_channels);
  gst_structure_get_int (structure, "rate", &enc->sample_rate);

  if (otherpadcaps) {
    if (!gst_caps_is_empty (otherpadcaps)) {
      GstStructure *ps = gst_caps_get_structure (otherpadcaps, 0);
      gst_structure_get_int (ps, "frame-size", &enc->frame_size);
    }
    gst_caps_unref (otherpadcaps);
  }

  GST_ERROR_OBJECT (pad, "channels=%d rate=%d frame-size=%d",
      enc->n_channels, enc->sample_rate, enc->frame_size);
  switch (enc->frame_size) {
    case 2:
      enc->frame_samples = enc->sample_rate / 400;
      break;
    case 5:
      enc->frame_samples = enc->sample_rate / 200;
      break;
    case 10:
      enc->frame_samples = enc->sample_rate / 100;
      break;
    case 20:
      enc->frame_samples = enc->sample_rate / 50;
      break;
    case 40:
      enc->frame_samples = enc->sample_rate / 20;
      break;
    case 60:
      enc->frame_samples = 3 * enc->sample_rate / 50;
      break;
    default:
      return FALSE;
      break;
  }
  GST_ERROR ("frame_samples %d", enc->frame_samples);

  gst_opus_enc_setup (enc);

  return TRUE;
}


static GstCaps *
gst_opus_enc_sink_getcaps (GstPad * pad)
{
  GstCaps *caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  GstCaps *peercaps = NULL;
  GstOpusEnc *enc = GST_OPUS_ENC (gst_pad_get_parent_element (pad));

  peercaps = gst_pad_peer_get_caps (enc->srcpad);

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
gst_opus_enc_convert_src (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstOpusEnc *enc;
  gint64 avg;

  enc = GST_OPUS_ENC (GST_PAD_PARENT (pad));

  if (enc->samples_in == 0 || enc->bytes_out == 0 || enc->sample_rate == 0)
    return FALSE;

  avg = (enc->bytes_out * enc->sample_rate) / (enc->samples_in);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / avg;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
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
gst_opus_enc_convert_sink (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  gint bytes_per_sample;
  GstOpusEnc *enc;

  enc = GST_OPUS_ENC (GST_PAD_PARENT (pad));

  bytes_per_sample = enc->n_channels * 2;

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
          gint byterate = bytes_per_sample * enc->sample_rate;

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
          if (enc->sample_rate == 0)
            return FALSE;
          *dest_value = src_value * GST_SECOND / enc->sample_rate;
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
          *dest_value = src_value * scale * enc->sample_rate / GST_SECOND;
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
gst_opus_enc_get_latency (GstOpusEnc * enc)
{
  return gst_util_uint64_scale (enc->frame_samples, GST_SECOND,
      enc->sample_rate);
}

static const GstQueryType *
gst_opus_enc_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_opus_enc_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    GST_QUERY_LATENCY,
    0
  };

  return gst_opus_enc_src_query_types;
}

static gboolean
gst_opus_enc_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstOpusEnc *enc;

  enc = GST_OPUS_ENC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat fmt, req_fmt;
      gint64 pos, val;

      gst_query_parse_position (query, &req_fmt, NULL);
      if ((res = gst_pad_query_peer_position (enc->sinkpad, &req_fmt, &val))) {
        gst_query_set_position (query, req_fmt, val);
        break;
      }

      fmt = GST_FORMAT_TIME;
      if (!(res = gst_pad_query_peer_position (enc->sinkpad, &fmt, &pos)))
        break;

      if ((res =
              gst_pad_query_peer_convert (enc->sinkpad, fmt, pos, &req_fmt,
                  &val)))
        gst_query_set_position (query, req_fmt, val);

      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat fmt, req_fmt;
      gint64 dur, val;

      gst_query_parse_duration (query, &req_fmt, NULL);
      if ((res = gst_pad_query_peer_duration (enc->sinkpad, &req_fmt, &val))) {
        gst_query_set_duration (query, req_fmt, val);
        break;
      }

      fmt = GST_FORMAT_TIME;
      if (!(res = gst_pad_query_peer_duration (enc->sinkpad, &fmt, &dur)))
        break;

      if ((res =
              gst_pad_query_peer_convert (enc->sinkpad, fmt, dur, &req_fmt,
                  &val))) {
        gst_query_set_duration (query, req_fmt, val);
      }
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res = gst_opus_enc_convert_src (pad, src_fmt, src_val, &dest_fmt,
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

      if ((res = gst_pad_peer_query (pad, query))) {
        gst_query_parse_latency (query, &live, &min_latency, &max_latency);

        latency = gst_opus_enc_get_latency (enc);

        /* add our latency */
        min_latency += latency;
        if (max_latency != -1)
          max_latency += latency;

        gst_query_set_latency (query, live, min_latency, max_latency);
      }
      break;
    }
    default:
      res = gst_pad_peer_query (pad, query);
      break;
  }

error:

  gst_object_unref (enc);

  return res;
}

static gboolean
gst_opus_enc_sink_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_opus_enc_convert_sink (pad, src_fmt, src_val, &dest_fmt,
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
gst_opus_enc_init (GstOpusEnc * enc, GstOpusEncClass * klass)
{
  enc->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);
  gst_pad_set_event_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_opus_enc_sinkevent));
  gst_pad_set_chain_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_opus_enc_chain));
  gst_pad_set_setcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_opus_enc_sink_setcaps));
  gst_pad_set_getcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_opus_enc_sink_getcaps));
  gst_pad_set_query_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_opus_enc_sink_query));

  enc->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_query_function (enc->srcpad,
      GST_DEBUG_FUNCPTR (gst_opus_enc_src_query));
  gst_pad_set_query_type_function (enc->srcpad,
      GST_DEBUG_FUNCPTR (gst_opus_enc_get_query_types));
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  enc->n_channels = -1;
  enc->sample_rate = -1;
  enc->frame_samples = 0;

  enc->bitrate = DEFAULT_BITRATE;
  enc->bandwidth = DEFAULT_BANDWIDTH;
  enc->frame_size = DEFAULT_FRAMESIZE;
  enc->cbr = DEFAULT_CBR;
  enc->constrained_vbr = DEFAULT_CONSTRAINED_VBR;
  enc->complexity = DEFAULT_COMPLEXITY;
  enc->inband_fec = DEFAULT_INBAND_FEC;
  enc->dtx = DEFAULT_DTX;
  enc->packet_loss_percentage = DEFAULT_PACKET_LOSS_PERCENT;

  enc->setup = FALSE;
  enc->header_sent = FALSE;

  enc->adapter = gst_adapter_new ();
}

#if 0
static GstBuffer *
gst_opus_enc_create_metadata_buffer (GstOpusEnc * enc)
{
  const GstTagList *tags;
  GstTagList *empty_tags = NULL;
  GstBuffer *comments = NULL;

  tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (enc));

  GST_DEBUG_OBJECT (enc, "tags = %" GST_PTR_FORMAT, tags);

  if (tags == NULL) {
    /* FIXME: better fix chain of callers to not write metadata at all,
     * if there is none */
    empty_tags = gst_tag_list_new ();
    tags = empty_tags;
  }
  comments = gst_tag_list_to_vorbiscomment_buffer (tags, NULL,
      0, "Encoded with GStreamer Opusenc");

  GST_BUFFER_OFFSET (comments) = enc->bytes_out;
  GST_BUFFER_OFFSET_END (comments) = 0;

  if (empty_tags)
    gst_tag_list_free (empty_tags);

  return comments;
}
#endif

static gboolean
gst_opus_enc_setup (GstOpusEnc * enc)
{
  int error = OPUS_OK;

  enc->setup = FALSE;

  enc->state = opus_encoder_create (enc->sample_rate, enc->n_channels,
      enc->audio_or_voip ? OPUS_APPLICATION_AUDIO : OPUS_APPLICATION_VOIP,
      &error);
  if (!enc->state || error != OPUS_OK)
    goto encoder_creation_failed;

  opus_encoder_ctl (enc->state, OPUS_SET_BITRATE (enc->bitrate), 0);
  opus_encoder_ctl (enc->state, OPUS_SET_BANDWIDTH (enc->bandwidth), 0);
  opus_encoder_ctl (enc->state, OPUS_SET_VBR (!enc->cbr), 0);
  opus_encoder_ctl (enc->state, OPUS_SET_VBR_CONSTRAINT (enc->constrained_vbr),
      0);
  opus_encoder_ctl (enc->state, OPUS_SET_COMPLEXITY (enc->complexity), 0);
  opus_encoder_ctl (enc->state, OPUS_SET_INBAND_FEC (enc->inband_fec), 0);
  opus_encoder_ctl (enc->state, OPUS_SET_DTX (enc->dtx), 0);
  opus_encoder_ctl (enc->state,
      OPUS_SET_PACKET_LOSS_PERC (enc->packet_loss_percentage), 0);

  GST_LOG_OBJECT (enc, "we have frame size %d", enc->frame_size);

  enc->setup = TRUE;

  return TRUE;

#if 0
mode_initialization_failed:
  GST_ERROR_OBJECT (enc, "Mode initialization failed: %d", error);
  return FALSE;
#endif

encoder_creation_failed:
  GST_ERROR_OBJECT (enc, "Encoder creation failed");
  return FALSE;
}


/* push out the buffer and do internal bookkeeping */
static GstFlowReturn
gst_opus_enc_push_buffer (GstOpusEnc * enc, GstBuffer * buffer)
{
  guint size;

  size = GST_BUFFER_SIZE (buffer);

  enc->bytes_out += size;

  GST_DEBUG_OBJECT (enc, "pushing output buffer of size %u", size);

  return gst_pad_push (enc->srcpad, buffer);
}

#if 0
static GstCaps *
gst_opus_enc_set_header_on_caps (GstCaps * caps, GstBuffer * buf1,
    GstBuffer * buf2)
{
  GstStructure *structure = NULL;
  GstBuffer *buf;
  GValue array = { 0 };
  GValue value = { 0 };

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  g_assert (gst_buffer_is_metadata_writable (buf1));
  g_assert (gst_buffer_is_metadata_writable (buf2));

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
#endif


static gboolean
gst_opus_enc_sinkevent (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstOpusEnc *enc;

  enc = GST_OPUS_ENC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_opus_enc_encode (enc, TRUE);
      res = gst_pad_event_default (pad, event);
      break;
    case GST_EVENT_TAG:
    {
      GstTagList *list;
      GstTagSetter *setter = GST_TAG_SETTER (enc);
      const GstTagMergeMode mode = gst_tag_setter_get_tag_merge_mode (setter);

      gst_event_parse_tag (event, &list);
      gst_tag_setter_merge_tags (setter, list, mode);
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
gst_opus_enc_encode (GstOpusEnc * enc, gboolean flush)
{

  GstFlowReturn ret = GST_FLOW_OK;
  gint bytes = enc->frame_samples * 2 * enc->n_channels;
  gint bytes_per_packet;

  bytes_per_packet =
      (enc->bitrate * enc->frame_samples / enc->sample_rate + 4) / 8;

  if (flush && gst_adapter_available (enc->adapter) % bytes != 0) {
    guint diff = gst_adapter_available (enc->adapter) % bytes;
    GstBuffer *buf = gst_buffer_new_and_alloc (diff);

    memset (GST_BUFFER_DATA (buf), 0, diff);
    gst_adapter_push (enc->adapter, buf);
  }


  while (gst_adapter_available (enc->adapter) >= bytes) {
    gint16 *data;
    gint outsize;
    GstBuffer *outbuf;

    ret = gst_pad_alloc_buffer_and_set_caps (enc->srcpad,
        GST_BUFFER_OFFSET_NONE, bytes_per_packet, GST_PAD_CAPS (enc->srcpad),
        &outbuf);

    if (GST_FLOW_OK != ret)
      goto done;

    data = (gint16 *) gst_adapter_take (enc->adapter, bytes);
    enc->samples_in += enc->frame_samples;

    GST_DEBUG_OBJECT (enc, "encoding %d samples (%d bytes)",
        enc->frame_samples, bytes);

    outsize = opus_encode (enc->state, data, enc->frame_samples,
        GST_BUFFER_DATA (outbuf), bytes_per_packet);

    g_free (data);

    if (outsize < 0) {
      GST_ERROR_OBJECT (enc, "Encoding failed: %d", outsize);
      ret = GST_FLOW_ERROR;
      goto done;
    }

    GST_BUFFER_TIMESTAMP (outbuf) = enc->start_ts +
        gst_util_uint64_scale_int (enc->frameno_out * enc->frame_samples,
        GST_SECOND, enc->sample_rate);
    GST_BUFFER_DURATION (outbuf) =
        gst_util_uint64_scale_int (enc->frame_samples, GST_SECOND,
        enc->sample_rate);
    GST_BUFFER_OFFSET (outbuf) =
        gst_util_uint64_scale_int (GST_BUFFER_OFFSET_END (outbuf), GST_SECOND,
        enc->sample_rate);

    enc->frameno++;
    enc->frameno_out++;

    ret = gst_opus_enc_push_buffer (enc, outbuf);

    if ((GST_FLOW_OK != ret) && (GST_FLOW_NOT_LINKED != ret))
      goto done;
  }

done:

  return ret;
}

static GstFlowReturn
gst_opus_enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstOpusEnc *enc;
  GstFlowReturn ret = GST_FLOW_OK;

  enc = GST_OPUS_ENC (GST_PAD_PARENT (pad));

  if (!enc->setup)
    goto not_setup;

#if 0
  if (!enc->header_sent) {
    /* Opus streams begin with two headers; the initial header (with
       most of the codec setup parameters) which is mandated by the Ogg
       bitstream spec.  The second header holds any comment fields.
       We merely need to make the headers, then pass them to libopus 
       one at a time; libopus handles the additional Ogg bitstream 
       constraints */
    GstBuffer *buf1, *buf2;
    GstCaps *caps;
    guchar data[100];

    /* create header buffer */
    opus_header_to_packet (&enc->header, data, 100);
    buf1 = gst_opus_enc_buffer_from_data (enc, data, 100, 0);

    /* create comment buffer */
    buf2 = gst_opus_enc_create_metadata_buffer (enc);

    /* mark and put on caps */
    caps = gst_pad_get_caps (enc->srcpad);
    caps = gst_opus_enc_set_header_on_caps (caps, buf1, buf2);

    gst_caps_set_simple (caps,
        "rate", G_TYPE_INT, enc->sample_rate,
        "channels", G_TYPE_INT, enc->n_channels,
        "frame-size", G_TYPE_INT, enc->frame_size, NULL);

    /* negotiate with these caps */
    GST_DEBUG_OBJECT (enc, "here are the caps: %" GST_PTR_FORMAT, caps);
    GST_LOG_OBJECT (enc, "rate=%d channels=%d frame-size=%d",
        enc->sample_rate, enc->n_channels, enc->frame_size);
    gst_pad_set_caps (enc->srcpad, caps);

    gst_buffer_set_caps (buf1, caps);
    gst_buffer_set_caps (buf2, caps);
    gst_caps_unref (caps);

    /* push out buffers */
    ret = gst_opus_enc_push_buffer (enc, buf1);

    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (buf2);
      goto done;
    }

    ret = gst_opus_enc_push_buffer (enc, buf2);

    if (ret != GST_FLOW_OK)
      goto done;

    enc->header_sent = TRUE;
  }
#endif

  GST_DEBUG_OBJECT (enc, "received buffer of %u bytes", GST_BUFFER_SIZE (buf));

  /* Save the timestamp of the first buffer. This will be later
   * used as offset for all following buffers */
  if (enc->start_ts == GST_CLOCK_TIME_NONE) {
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
      enc->start_ts = GST_BUFFER_TIMESTAMP (buf);
    } else {
      enc->start_ts = 0;
    }
  }


  /* Check if we have a continous stream, if not drop some samples or the buffer or
   * insert some silence samples */
  if (enc->next_ts != GST_CLOCK_TIME_NONE &&
      GST_BUFFER_TIMESTAMP (buf) < enc->next_ts) {
    guint64 diff = enc->next_ts - GST_BUFFER_TIMESTAMP (buf);
    guint64 diff_bytes;

    GST_WARNING_OBJECT (enc, "Buffer is older than previous "
        "timestamp + duration (%" GST_TIME_FORMAT "< %" GST_TIME_FORMAT
        "), cannot handle. Clipping buffer.",
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (enc->next_ts));

    diff_bytes =
        GST_CLOCK_TIME_TO_FRAMES (diff, enc->sample_rate) * enc->n_channels * 2;
    if (diff_bytes >= GST_BUFFER_SIZE (buf)) {
      gst_buffer_unref (buf);
      return GST_FLOW_OK;
    }
    buf = gst_buffer_make_metadata_writable (buf);
    GST_BUFFER_DATA (buf) += diff_bytes;
    GST_BUFFER_SIZE (buf) -= diff_bytes;

    GST_BUFFER_TIMESTAMP (buf) += diff;
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      GST_BUFFER_DURATION (buf) -= diff;
  }

  if (enc->next_ts != GST_CLOCK_TIME_NONE
      && GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    guint64 max_diff =
        gst_util_uint64_scale (enc->frame_size, GST_SECOND, enc->sample_rate);

    if (GST_BUFFER_TIMESTAMP (buf) != enc->next_ts &&
        GST_BUFFER_TIMESTAMP (buf) - enc->next_ts > max_diff) {
      GST_WARNING_OBJECT (enc,
          "Discontinuity detected: %" G_GUINT64_FORMAT " > %" G_GUINT64_FORMAT,
          GST_BUFFER_TIMESTAMP (buf) - enc->next_ts, max_diff);

      gst_opus_enc_encode (enc, TRUE);

      enc->frameno_out = 0;
      enc->start_ts = GST_BUFFER_TIMESTAMP (buf);
    }
  }

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)
      && GST_BUFFER_DURATION_IS_VALID (buf))
    enc->next_ts = GST_BUFFER_TIMESTAMP (buf) + GST_BUFFER_DURATION (buf);
  else
    enc->next_ts = GST_CLOCK_TIME_NONE;

  /* push buffer to adapter */
  gst_adapter_push (enc->adapter, buf);
  buf = NULL;

  ret = gst_opus_enc_encode (enc, FALSE);

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
gst_opus_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOpusEnc *enc;

  enc = GST_OPUS_ENC (object);

  switch (prop_id) {
    case PROP_AUDIO:
      g_value_set_boolean (value, enc->audio_or_voip);
      break;
    case PROP_BITRATE:
      g_value_set_int (value, enc->bitrate);
      break;
    case PROP_BANDWIDTH:
      g_value_set_int (value, enc->bandwidth);
      break;
    case PROP_FRAME_SIZE:
      g_value_set_int (value, enc->frame_size);
      break;
    case PROP_CBR:
      g_value_set_boolean (value, enc->cbr);
      break;
    case PROP_CONSTRAINED_VBR:
      g_value_set_boolean (value, enc->constrained_vbr);
      break;
    case PROP_COMPLEXITY:
      g_value_set_int (value, enc->complexity);
      break;
    case PROP_INBAND_FEC:
      g_value_set_boolean (value, enc->inband_fec);
      break;
    case PROP_DTX:
      g_value_set_boolean (value, enc->dtx);
      break;
    case PROP_PACKET_LOSS_PERCENT:
      g_value_set_int (value, enc->packet_loss_percentage);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_opus_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOpusEnc *enc;

  enc = GST_OPUS_ENC (object);

  switch (prop_id) {
    case PROP_AUDIO:
      enc->audio_or_voip = g_value_get_boolean (value);
      break;
    case PROP_BITRATE:
      enc->bitrate = g_value_get_int (value);
      break;
    case PROP_BANDWIDTH:
      enc->bandwidth = g_value_get_int (value);
      break;
    case PROP_FRAME_SIZE:
      enc->frame_size = g_value_get_int (value);
      break;
    case PROP_CBR:
      enc->cbr = g_value_get_boolean (value);
      break;
    case PROP_CONSTRAINED_VBR:
      enc->constrained_vbr = g_value_get_boolean (value);
      break;
    case PROP_COMPLEXITY:
      enc->complexity = g_value_get_int (value);
      break;
    case PROP_INBAND_FEC:
      enc->inband_fec = g_value_get_boolean (value);
      break;
    case PROP_DTX:
      enc->dtx = g_value_get_boolean (value);
      break;
    case PROP_PACKET_LOSS_PERCENT:
      enc->packet_loss_percentage = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_opus_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstOpusEnc *enc = GST_OPUS_ENC (element);
  GstStateChangeReturn res;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      enc->frameno = 0;
      enc->samples_in = 0;
      enc->frameno_out = 0;
      enc->start_ts = GST_CLOCK_TIME_NONE;
      enc->next_ts = GST_CLOCK_TIME_NONE;
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
        opus_encoder_destroy (enc->state);
        enc->state = NULL;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_tag_setter_reset_tags (GST_TAG_SETTER (enc));
    default:
      break;
  }

  return res;
}
