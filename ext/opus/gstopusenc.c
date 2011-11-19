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
#include <gst/base/gstbytewriter.h>
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

#define GST_OPUS_ENC_TYPE_FRAME_SIZE (gst_opus_enc_frame_size_get_type())
static GType
gst_opus_enc_frame_size_get_type (void)
{
  static const GEnumValue values[] = {
    {2, "2.5", "2.5"},
    {5, "5", "5"},
    {10, "10", "10"},
    {20, "20", "20"},
    {40, "40", "40"},
    {60, "60", "60"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstOpusEncFrameSize", values);

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
    GST_STATIC_CAPS ("audio/x-opus")
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

static gboolean gst_opus_enc_sink_event (GstAudioEncoder * benc,
    GstEvent * event);
static gboolean gst_opus_enc_setup (GstOpusEnc * enc);

static void gst_opus_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_opus_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static gboolean gst_opus_enc_start (GstAudioEncoder * benc);
static gboolean gst_opus_enc_stop (GstAudioEncoder * benc);
static gboolean gst_opus_enc_set_format (GstAudioEncoder * benc,
    GstAudioInfo * info);
static GstFlowReturn gst_opus_enc_handle_frame (GstAudioEncoder * benc,
    GstBuffer * buf);
static GstFlowReturn gst_opus_enc_pre_push (GstAudioEncoder * benc,
    GstBuffer ** buffer);
static gint64 gst_opus_enc_get_latency (GstOpusEnc * enc);

static GstFlowReturn gst_opus_enc_encode (GstOpusEnc * enc, GstBuffer * buffer);

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

GST_BOILERPLATE_FULL (GstOpusEnc, gst_opus_enc, GstAudioEncoder,
    GST_TYPE_AUDIO_ENCODER, gst_opus_enc_setup_interfaces);

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
  GstAudioEncoderClass *base_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  base_class = (GstAudioEncoderClass *) klass;

  gobject_class->set_property = gst_opus_enc_set_property;
  gobject_class->get_property = gst_opus_enc_get_property;

  base_class->start = GST_DEBUG_FUNCPTR (gst_opus_enc_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_opus_enc_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_opus_enc_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_opus_enc_handle_frame);
  base_class->pre_push = GST_DEBUG_FUNCPTR (gst_opus_enc_pre_push);
  base_class->event = GST_DEBUG_FUNCPTR (gst_opus_enc_sink_event);

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
      g_param_spec_enum ("frame-size", "Frame Size",
          "The duration of an audio frame, in ms",
          GST_OPUS_ENC_TYPE_FRAME_SIZE, DEFAULT_FRAMESIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CBR,
      g_param_spec_boolean ("cbr", "Constant bit rate",
          "Constant bit rate", DEFAULT_CBR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CONSTRAINED_VBR,
      g_param_spec_boolean ("constrained-vbr", "Constrained VBR",
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
}

static void
gst_opus_enc_finalize (GObject * object)
{
  GstOpusEnc *enc;

  enc = GST_OPUS_ENC (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_opus_enc_init (GstOpusEnc * enc, GstOpusEncClass * klass)
{
  GstAudioEncoder *benc = GST_AUDIO_ENCODER (enc);

  GST_DEBUG_OBJECT (enc, "init");

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

  /* arrange granulepos marking (and required perfect ts) */
  gst_audio_encoder_set_mark_granule (benc, TRUE);
  gst_audio_encoder_set_perfect_timestamp (benc, TRUE);
}

static gboolean
gst_opus_enc_start (GstAudioEncoder * benc)
{
  GstOpusEnc *enc = GST_OPUS_ENC (benc);

  GST_DEBUG_OBJECT (enc, "start");
  enc->tags = gst_tag_list_new ();
  enc->header_sent = FALSE;
  return TRUE;
}

static gboolean
gst_opus_enc_stop (GstAudioEncoder * benc)
{
  GstOpusEnc *enc = GST_OPUS_ENC (benc);

  GST_DEBUG_OBJECT (enc, "stop");
  enc->header_sent = FALSE;
  if (enc->state) {
    opus_encoder_destroy (enc->state);
    enc->state = NULL;
  }
  gst_tag_list_free (enc->tags);
  enc->tags = NULL;
  g_slist_foreach (enc->headers, (GFunc) gst_buffer_unref, NULL);
  enc->headers = NULL;

  return TRUE;
}

static gint64
gst_opus_enc_get_latency (GstOpusEnc * enc)
{
  gint64 latency = gst_util_uint64_scale (enc->frame_samples, GST_SECOND,
      enc->sample_rate);
  GST_DEBUG_OBJECT (enc, "Latency: %" GST_TIME_FORMAT, GST_TIME_ARGS (latency));
  return latency;
}

static gint
gst_opus_enc_get_frame_samples (GstOpusEnc * enc)
{
  gint frame_samples = 0;
  switch (enc->frame_size) {
    case 2:
      frame_samples = enc->sample_rate / 400;
      break;
    case 5:
      frame_samples = enc->sample_rate / 200;
      break;
    case 10:
      frame_samples = enc->sample_rate / 100;
      break;
    case 20:
      frame_samples = enc->sample_rate / 50;
      break;
    case 40:
      frame_samples = enc->sample_rate / 25;
      break;
    case 60:
      frame_samples = 3 * enc->sample_rate / 50;
      break;
    default:
      GST_WARNING_OBJECT (enc, "Unsupported frame size: %d", enc->frame_size);
      frame_samples = 0;
      break;
  }
  return frame_samples;
}

static gboolean
gst_opus_enc_set_format (GstAudioEncoder * benc, GstAudioInfo * info)
{
  GstOpusEnc *enc;

  enc = GST_OPUS_ENC (benc);

  enc->n_channels = GST_AUDIO_INFO_CHANNELS (info);
  enc->sample_rate = GST_AUDIO_INFO_RATE (info);
  GST_DEBUG_OBJECT (benc, "Setup with %d channels, %d Hz", enc->n_channels,
      enc->sample_rate);

  /* handle reconfigure */
  if (enc->state) {
    opus_encoder_destroy (enc->state);
    enc->state = NULL;
  }
  if (!gst_opus_enc_setup (enc))
    return FALSE;

  enc->frame_samples = gst_opus_enc_get_frame_samples (enc);

  /* feedback to base class */
  gst_audio_encoder_set_latency (benc,
      gst_opus_enc_get_latency (enc), gst_opus_enc_get_latency (enc));
  gst_audio_encoder_set_frame_samples_min (benc,
      enc->frame_samples * enc->n_channels * 2);
  gst_audio_encoder_set_frame_samples_max (benc,
      enc->frame_samples * enc->n_channels * 2);
  gst_audio_encoder_set_frame_max (benc, 0);

  return TRUE;
}

static GstBuffer *
gst_opus_enc_create_id_buffer (GstOpusEnc * enc)
{
  GstBuffer *buffer;
  GstByteWriter bw;

  gst_byte_writer_init (&bw);

  /* See http://wiki.xiph.org/OggOpus */
  gst_byte_writer_put_data (&bw, (const guint8 *) "OpusHead", 8);
  gst_byte_writer_put_uint8 (&bw, 0);   /* version number */
  gst_byte_writer_put_uint8 (&bw, enc->n_channels);
  gst_byte_writer_put_uint16_le (&bw, 0);       /* pre-skip *//* TODO: endianness ? */
  gst_byte_writer_put_uint32_le (&bw, enc->sample_rate);
  gst_byte_writer_put_uint16_le (&bw, 0);       /* output gain *//* TODO: endianness ? */
  gst_byte_writer_put_uint8 (&bw, 0);   /* channel mapping *//* TODO: what is this ? */

  buffer = gst_byte_writer_reset_and_get_buffer (&bw);

  GST_BUFFER_OFFSET (buffer) = 0;
  GST_BUFFER_OFFSET_END (buffer) = 0;

  return buffer;
}

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
  comments =
      gst_tag_list_to_vorbiscomment_buffer (tags, (const guint8 *) "OpusTags",
      8, "Encoded with GStreamer Opusenc");

  GST_BUFFER_OFFSET (comments) = 0;
  GST_BUFFER_OFFSET_END (comments) = 0;

  if (empty_tags)
    gst_tag_list_free (empty_tags);

  return comments;
}

static gboolean
gst_opus_enc_setup (GstOpusEnc * enc)
{
  int error = OPUS_OK;

  GST_DEBUG_OBJECT (enc, "setup");

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

encoder_creation_failed:
  GST_ERROR_OBJECT (enc, "Encoder creation failed");
  return FALSE;
}

static gboolean
gst_opus_enc_sink_event (GstAudioEncoder * benc, GstEvent * event)
{
  GstOpusEnc *enc;

  enc = GST_OPUS_ENC (benc);

  GST_DEBUG_OBJECT (enc, "sink event: %s", GST_EVENT_TYPE_NAME (event));
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
    {
      GstTagList *list;
      GstTagSetter *setter = GST_TAG_SETTER (enc);
      const GstTagMergeMode mode = gst_tag_setter_get_tag_merge_mode (setter);

      gst_event_parse_tag (event, &list);
      gst_tag_setter_merge_tags (setter, list, mode);
      break;
    }
    default:
      break;
  }

  return FALSE;
}

static GstFlowReturn
gst_opus_enc_pre_push (GstAudioEncoder * benc, GstBuffer ** buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstOpusEnc *enc;

  enc = GST_OPUS_ENC (benc);

  /* FIXME 0.11 ? get rid of this special ogg stuff and have it
   * put and use 'codec data' in caps like anything else,
   * with all the usual out-of-band advantage etc */
  if (G_UNLIKELY (enc->headers)) {
    GSList *header = enc->headers;

    /* try to push all of these, if we lose one, might as well lose all */
    while (header) {
      if (ret == GST_FLOW_OK)
        ret = gst_pad_push (GST_AUDIO_ENCODER_SRC_PAD (enc), header->data);
      else
        gst_pad_push (GST_AUDIO_ENCODER_SRC_PAD (enc), header->data);
      header = g_slist_next (header);
    }

    g_slist_free (enc->headers);
    enc->headers = NULL;
  }

  return ret;
}

static GstFlowReturn
gst_opus_enc_encode (GstOpusEnc * enc, GstBuffer * buf)
{
  guint8 *bdata, *data, *mdata = NULL;
  gsize bsize, size;
  gsize bytes = enc->frame_samples * enc->n_channels * 2;
  gsize bytes_per_packet =
      (enc->bitrate * enc->frame_samples / enc->sample_rate + 4) / 8;
  gint ret = GST_FLOW_OK;

  if (G_LIKELY (buf)) {
    bdata = GST_BUFFER_DATA (buf);
    bsize = GST_BUFFER_SIZE (buf);
    if (G_UNLIKELY (bsize % bytes)) {
      GST_DEBUG_OBJECT (enc, "draining; adding silence samples");

      size = ((bsize / bytes) + 1) * bytes;
      mdata = g_malloc0 (size);
      memcpy (mdata, bdata, bsize);
      bdata = NULL;
      data = mdata;
    } else {
      data = bdata;
      size = bsize;
    }
  } else {
    GST_DEBUG_OBJECT (enc, "nothing to drain");
    goto done;
  }


  while (size) {
    gint outsize;
    GstBuffer *outbuf;

    ret = gst_pad_alloc_buffer_and_set_caps (GST_AUDIO_ENCODER_SRC_PAD (enc),
        GST_BUFFER_OFFSET_NONE, bytes_per_packet,
        GST_PAD_CAPS (GST_AUDIO_ENCODER_SRC_PAD (enc)), &outbuf);

    if (GST_FLOW_OK != ret)
      goto done;

    GST_DEBUG_OBJECT (enc, "encoding %d samples (%d bytes) to %d bytes",
        enc->frame_samples, bytes, bytes_per_packet);

    outsize =
        opus_encode (enc->state, (const gint16 *) data, enc->frame_samples,
        GST_BUFFER_DATA (outbuf), bytes_per_packet);

    if (outsize < 0) {
      GST_ERROR_OBJECT (enc, "Encoding failed: %d", outsize);
      ret = GST_FLOW_ERROR;
      goto done;
    } else if (outsize > bytes_per_packet) {
      GST_WARNING_OBJECT (enc,
          "Encoded size %d is different from %d bytes per packet", outsize,
          bytes_per_packet);
      ret = GST_FLOW_ERROR;
      goto done;
    }

    GST_BUFFER_SIZE (outbuf) = outsize;

    ret =
        gst_audio_encoder_finish_frame (GST_AUDIO_ENCODER (enc), outbuf,
        enc->frame_samples);

    if ((GST_FLOW_OK != ret) && (GST_FLOW_NOT_LINKED != ret))
      goto done;

    data += bytes;
    size -= bytes;
  }

done:

  if (mdata)
    g_free (mdata);

  return ret;
}

/*
 * (really really) FIXME: move into core (dixit tpm)
 */
/**
 * _gst_caps_set_buffer_array:
 * @caps: a #GstCaps
 * @field: field in caps to set
 * @buf: header buffers
 *
 * Adds given buffers to an array of buffers set as the given @field
 * on the given @caps.  List of buffer arguments must be NULL-terminated.
 *
 * Returns: input caps with a streamheader field added, or NULL if some error
 */
static GstCaps *
_gst_caps_set_buffer_array (GstCaps * caps, const gchar * field,
    GstBuffer * buf, ...)
{
  GstStructure *structure = NULL;
  va_list va;
  GValue array = { 0 };
  GValue value = { 0 };

  g_return_val_if_fail (caps != NULL, NULL);
  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);
  g_return_val_if_fail (field != NULL, NULL);

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  g_value_init (&array, GST_TYPE_ARRAY);

  va_start (va, buf);
  /* put buffers in a fixed list */
  while (buf) {
    g_assert (gst_buffer_is_writable (buf));

    /* mark buffer */
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);

    g_value_init (&value, GST_TYPE_BUFFER);
    buf = gst_buffer_copy (buf);
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);
    gst_value_set_buffer (&value, buf);
    gst_buffer_unref (buf);
    gst_value_array_append_value (&array, &value);
    g_value_unset (&value);

    buf = va_arg (va, GstBuffer *);
  }

  gst_structure_set_value (structure, field, &array);
  g_value_unset (&array);

  return caps;
}

static GstFlowReturn
gst_opus_enc_handle_frame (GstAudioEncoder * benc, GstBuffer * buf)
{
  GstOpusEnc *enc;
  GstFlowReturn ret = GST_FLOW_OK;

  enc = GST_OPUS_ENC (benc);
  GST_DEBUG_OBJECT (enc, "handle_frame");

  if (!enc->header_sent) {
    /* Opus streams in Ogg begin with two headers; the initial header (with
       most of the codec setup parameters) which is mandated by the Ogg
       bitstream spec.  The second header holds any comment fields. */
    GstBuffer *buf1, *buf2;
    GstCaps *caps;

    /* create header buffers */
    buf1 = gst_opus_enc_create_id_buffer (enc);
    buf2 = gst_opus_enc_create_metadata_buffer (enc);

    /* mark and put on caps */
    caps = gst_caps_from_string ("audio/x-opus");
    caps = _gst_caps_set_buffer_array (caps, "streamheader", buf1, buf2, NULL);

    /* negotiate with these caps */
    GST_DEBUG_OBJECT (enc, "here are the caps: %" GST_PTR_FORMAT, caps);

    gst_pad_set_caps (GST_AUDIO_ENCODER_SRC_PAD (enc), caps);

    /* push out buffers */
    /* store buffers for later pre_push sending */
    g_slist_foreach (enc->headers, (GFunc) gst_buffer_unref, NULL);
    enc->headers = NULL;
    GST_DEBUG_OBJECT (enc, "storing header buffers");
    enc->headers = g_slist_prepend (enc->headers, buf2);
    enc->headers = g_slist_prepend (enc->headers, buf1);
    enc->header_sent = TRUE;
  }

  GST_DEBUG_OBJECT (enc, "received buffer %p of %u bytes", buf,
      buf ? GST_BUFFER_SIZE (buf) : 0);

  ret = gst_opus_enc_encode (enc, buf);

  return ret;
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
      g_value_set_enum (value, enc->bandwidth);
      break;
    case PROP_FRAME_SIZE:
      g_value_set_enum (value, enc->frame_size);
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
      enc->bandwidth = g_value_get_enum (value);
      break;
    case PROP_FRAME_SIZE:
      enc->frame_size = g_value_get_enum (value);
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
