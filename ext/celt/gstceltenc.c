/* GStreamer Celt Encoder
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
 * Based on the celtenc element
 */

/**
 * SECTION:element-celtenc
 * @see_also: celtdec, oggmux
 *
 * This element raw audio to CELT.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v audiotestsrc wave=sine num-buffers=100 ! audioconvert ! celtenc ! oggmux ! filesink location=sine.ogg
 * ]| Encode a test sine signal to Ogg/CELT.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <celt/celt.h>
#include <celt/celt_header.h>

#include <gst/gsttagsetter.h>
#include <gst/tag/tag.h>
#include <gst/audio/audio.h>
#include "gstceltenc.h"

GST_DEBUG_CATEGORY_STATIC (celtenc_debug);
#define GST_CAT_DEFAULT celtenc_debug

#define GST_CELT_ENC_TYPE_PREDICTION (gst_celt_enc_prediction_get_type())
static GType
gst_celt_enc_prediction_get_type (void)
{
  static const GEnumValue values[] = {
    {0, "Independent frames", "idependent"},
    {1, "Short term interframe prediction", "short-term"},
    {2, "Long term interframe prediction", "long-term"},
    {0, NULL, NULL}
  };
  static volatile GType id = 0;

  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;

    _id = g_enum_register_static ("GstCeltEncPrediction", values);

    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 32000, 64000 ], "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, " "width = (int) 16, " "depth = (int) 16")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-celt, "
        "rate = (int) [ 32000, 64000 ], "
        "channels = (int) [ 1, 2 ], " "frame-size = (int) [ 64, 512 ]")
    );

#define DEFAULT_BITRATE         64000
#define DEFAULT_FRAMESIZE       480
#define DEFAULT_CBR             TRUE
#define DEFAULT_COMPLEXITY      9
#define DEFAULT_MAX_BITRATE     64000
#define DEFAULT_PREDICTION      0
#define DEFAULT_START_BAND      0

enum
{
  PROP_0,
  PROP_BITRATE,
  PROP_FRAMESIZE,
  PROP_CBR,
  PROP_COMPLEXITY,
  PROP_MAX_BITRATE,
  PROP_PREDICTION,
  PROP_START_BAND
};

static void gst_celt_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_celt_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static gboolean gst_celt_enc_start (GstAudioEncoder * enc);
static gboolean gst_celt_enc_stop (GstAudioEncoder * enc);
static gboolean gst_celt_enc_set_format (GstAudioEncoder * enc,
    GstAudioInfo * info);
static GstFlowReturn gst_celt_enc_handle_frame (GstAudioEncoder * enc,
    GstBuffer * in_buf);
static gboolean gst_celt_enc_sink_event (GstAudioEncoder * enc,
    GstEvent * event);
static GstFlowReturn gst_celt_enc_pre_push (GstAudioEncoder * benc,
    GstBuffer ** buffer);

static void
gst_celt_enc_setup_interfaces (GType celtenc_type)
{
  static const GInterfaceInfo tag_setter_info = { NULL, NULL, NULL };

  g_type_add_interface_static (celtenc_type, GST_TYPE_TAG_SETTER,
      &tag_setter_info);

  GST_DEBUG_CATEGORY_INIT (celtenc_debug, "celtenc", 0, "Celt encoder");
}

GST_BOILERPLATE_FULL (GstCeltEnc, gst_celt_enc, GstAudioEncoder,
    GST_TYPE_AUDIO_ENCODER, gst_celt_enc_setup_interfaces);

static void
gst_celt_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_set_details_simple (element_class, "Celt audio encoder",
      "Codec/Encoder/Audio",
      "Encodes audio in Celt format",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_celt_enc_class_init (GstCeltEncClass * klass)
{
  GObjectClass *gobject_class;
  GstAudioEncoderClass *gstbase_class;

  gobject_class = (GObjectClass *) klass;
  gstbase_class = (GstAudioEncoderClass *) klass;

  gobject_class->set_property = gst_celt_enc_set_property;
  gobject_class->get_property = gst_celt_enc_get_property;

  gstbase_class->start = GST_DEBUG_FUNCPTR (gst_celt_enc_start);
  gstbase_class->stop = GST_DEBUG_FUNCPTR (gst_celt_enc_stop);
  gstbase_class->set_format = GST_DEBUG_FUNCPTR (gst_celt_enc_set_format);
  gstbase_class->handle_frame = GST_DEBUG_FUNCPTR (gst_celt_enc_handle_frame);
  gstbase_class->event = GST_DEBUG_FUNCPTR (gst_celt_enc_sink_event);
  gstbase_class->pre_push = GST_DEBUG_FUNCPTR (gst_celt_enc_pre_push);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BITRATE,
      g_param_spec_int ("bitrate", "Encoding Bit-rate",
          "Specify an encoding bit-rate (in bps).",
          10000, 320000, DEFAULT_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FRAMESIZE,
      g_param_spec_int ("framesize", "Frame Size",
          "The number of samples per frame", 64, 512, DEFAULT_FRAMESIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CBR,
      g_param_spec_boolean ("cbr", "Constant bit rate",
          "Constant bit rate", DEFAULT_CBR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_COMPLEXITY,
      g_param_spec_int ("complexity", "Complexity",
          "Complexity", 0, 10, DEFAULT_COMPLEXITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MAX_BITRATE,
      g_param_spec_int ("max-bitrate", "Maximum Encoding Bit-rate",
          "Specify a maximum encoding bit rate (in bps) for variable bit rate encoding.",
          10000, 320000, DEFAULT_MAX_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PREDICTION,
      g_param_spec_enum ("prediction", "Interframe Prediction",
          "Controls the use of interframe prediction.",
          GST_CELT_ENC_TYPE_PREDICTION, DEFAULT_PREDICTION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_START_BAND,
      g_param_spec_int ("start-band", "Start Band",
          "Controls the start band that should be used",
          0, G_MAXINT, DEFAULT_START_BAND,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_celt_enc_init (GstCeltEnc * enc, GstCeltEncClass * klass)
{
  enc->bitrate = DEFAULT_BITRATE;
  enc->frame_size = DEFAULT_FRAMESIZE;
  enc->cbr = DEFAULT_CBR;
  enc->complexity = DEFAULT_COMPLEXITY;
  enc->max_bitrate = DEFAULT_MAX_BITRATE;
  enc->prediction = DEFAULT_PREDICTION;
}

static gboolean
gst_celt_enc_start (GstAudioEncoder * benc)
{
  GstCeltEnc *enc = GST_CELT_ENC (benc);

  GST_DEBUG_OBJECT (enc, "start");
  enc->channels = -1;
  enc->rate = -1;
  enc->header_sent = FALSE;

  return TRUE;
}

static gboolean
gst_celt_enc_stop (GstAudioEncoder * benc)
{
  GstCeltEnc *enc = GST_CELT_ENC (benc);

  GST_DEBUG_OBJECT (enc, "stop");
  enc->header_sent = FALSE;
  if (enc->state) {
    celt_encoder_destroy (enc->state);
    enc->state = NULL;
  }
  if (enc->mode) {
    celt_mode_destroy (enc->mode);
    enc->mode = NULL;
  }
  memset (&enc->header, 0, sizeof (enc->header));

  g_slist_foreach (enc->headers, (GFunc) gst_buffer_unref, NULL);
  enc->headers = NULL;

  gst_tag_setter_reset_tags (GST_TAG_SETTER (enc));

  return TRUE;
}

static gboolean
gst_celt_enc_setup (GstCeltEnc * enc)
{
  gint error = CELT_OK;

#ifdef HAVE_CELT_0_7
  enc->mode = celt_mode_create (enc->rate, enc->frame_size, &error);
#else
  enc->mode =
      celt_mode_create (enc->rate, enc->channels, enc->frame_size, &error);
#endif
  if (!enc->mode)
    goto mode_initialization_failed;

#ifdef HAVE_CELT_0_11
  celt_header_init (&enc->header, enc->mode, enc->frame_size, enc->channels);
#else
#ifdef HAVE_CELT_0_7
  celt_header_init (&enc->header, enc->mode, enc->channels);
#else
  celt_header_init (&enc->header, enc->mode);
#endif
#endif
  enc->header.nb_channels = enc->channels;

#ifdef HAVE_CELT_0_8
  enc->frame_size = enc->header.frame_size;
#else
  celt_mode_info (enc->mode, CELT_GET_FRAME_SIZE, &enc->frame_size);
#endif

#ifdef HAVE_CELT_0_11
  enc->state = celt_encoder_create_custom (enc->mode, enc->channels, &error);
#else
#ifdef HAVE_CELT_0_7
  enc->state = celt_encoder_create (enc->mode, enc->channels, &error);
#else
  enc->state = celt_encoder_create (enc->mode);
#endif
#endif
  if (!enc->state)
    goto encoder_creation_failed;

#ifdef CELT_SET_VBR_RATE
  if (!enc->cbr) {
    celt_encoder_ctl (enc->state, CELT_SET_VBR_RATE (enc->bitrate / 1000), 0);
  }
#endif
#ifdef CELT_SET_COMPLEXITY
  celt_encoder_ctl (enc->state, CELT_SET_COMPLEXITY (enc->complexity), 0);
#endif
#ifdef CELT_SET_PREDICTION
  celt_encoder_ctl (enc->state, CELT_SET_PREDICTION (enc->prediction), 0);
#endif
#ifdef CELT_SET_START_BAND
  celt_encoder_ctl (enc->state, CELT_SET_START_BAND (enc->start_band), 0);
#endif

  GST_LOG_OBJECT (enc, "we have frame size %d", enc->frame_size);

  return TRUE;

mode_initialization_failed:
  GST_ERROR_OBJECT (enc, "Mode initialization failed: %d", error);
  return FALSE;

encoder_creation_failed:
#ifdef HAVE_CELT_0_7
  GST_ERROR_OBJECT (enc, "Encoder creation failed: %d", error);
#else
  GST_ERROR_OBJECT (enc, "Encoder creation failed");
#endif
  return FALSE;
}

static gint64
gst_celt_enc_get_latency (GstCeltEnc * enc)
{
  return gst_util_uint64_scale (enc->frame_size, GST_SECOND, enc->rate);
}

static gboolean
gst_celt_enc_set_format (GstAudioEncoder * benc, GstAudioInfo * info)
{
  GstCeltEnc *enc;
  GstCaps *otherpadcaps;

  enc = GST_CELT_ENC (benc);

  enc->channels = GST_AUDIO_INFO_CHANNELS (info);
  enc->rate = GST_AUDIO_INFO_RATE (info);

  /* handle reconfigure */
  if (enc->state) {
    celt_encoder_destroy (enc->state);
    enc->state = NULL;
  }
  if (enc->mode) {
    celt_mode_destroy (enc->mode);
    enc->mode = NULL;
  }
  memset (&enc->header, 0, sizeof (enc->header));

  otherpadcaps = gst_pad_get_allowed_caps (GST_AUDIO_ENCODER_SRC_PAD (enc));
  if (otherpadcaps) {
    if (!gst_caps_is_empty (otherpadcaps)) {
      GstStructure *ps = gst_caps_get_structure (otherpadcaps, 0);
      gst_structure_get_int (ps, "frame-size", &enc->frame_size);
    }
    gst_caps_unref (otherpadcaps);
  }

  if (enc->requested_frame_size > 0)
    enc->frame_size = enc->requested_frame_size;

  GST_DEBUG_OBJECT (enc, "channels=%d rate=%d frame-size=%d",
      enc->channels, enc->rate, enc->frame_size);

  if (!gst_celt_enc_setup (enc))
    return FALSE;

  /* feedback to base class */
  gst_audio_encoder_set_latency (benc,
      gst_celt_enc_get_latency (enc), gst_celt_enc_get_latency (enc));
  gst_audio_encoder_set_frame_samples_min (benc, enc->frame_size);
  gst_audio_encoder_set_frame_samples_max (benc, enc->frame_size);
  gst_audio_encoder_set_frame_max (benc, 1);

  return TRUE;
}

static gboolean
gst_celt_enc_sink_event (GstAudioEncoder * benc, GstEvent * event)
{
  GstCeltEnc *enc;

  enc = GST_CELT_ENC (benc);

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

  /* we only peeked, let base class handle it */
  return FALSE;
}

static GstBuffer *
gst_celt_enc_create_metadata_buffer (GstCeltEnc * enc)
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
      0, "Encoded with GStreamer Celtenc");

  GST_BUFFER_OFFSET (comments) = 0;
  GST_BUFFER_OFFSET_END (comments) = 0;

  if (empty_tags)
    gst_tag_list_free (empty_tags);

  return comments;
}

static GstFlowReturn
gst_celt_enc_encode (GstCeltEnc * enc, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gint frame_size = enc->frame_size;
  gint bytes = frame_size * 2 * enc->channels;
  gint bytes_per_packet;
  gint16 *data, *data0 = NULL;
  gint outsize, size;
  GstBuffer *outbuf;

  if (G_LIKELY (buf)) {
    data = (gint16 *) GST_BUFFER_DATA (buf);
    size = GST_BUFFER_SIZE (buf);

    if (G_UNLIKELY (size % bytes)) {
      GST_DEBUG_OBJECT (enc, "draining; adding silence samples");
      size = ((size / bytes) + 1) * bytes;
      data0 = data = g_malloc0 (size);
      memcpy (data, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
    }
  } else {
    GST_DEBUG_OBJECT (enc, "nothing to drain");
    goto done;
  }

  frame_size = size / (2 * enc->channels);
  if (enc->cbr) {
    bytes_per_packet = (enc->bitrate * frame_size / enc->rate + 4) / 8;
  } else {
    bytes_per_packet = (enc->max_bitrate * frame_size / enc->rate + 4) / 8;
  }

  ret = gst_pad_alloc_buffer_and_set_caps (GST_AUDIO_ENCODER_SRC_PAD (enc),
      GST_BUFFER_OFFSET_NONE, bytes_per_packet,
      GST_PAD_CAPS (GST_AUDIO_ENCODER_SRC_PAD (enc)), &outbuf);

  if (GST_FLOW_OK != ret)
    goto done;

  GST_DEBUG_OBJECT (enc, "encoding %d samples (%d bytes)", frame_size, bytes);

#ifdef HAVE_CELT_0_8
  outsize =
      celt_encode (enc->state, data, frame_size,
      GST_BUFFER_DATA (outbuf), bytes_per_packet);
#else
  outsize =
      celt_encode (enc->state, data, NULL,
      GST_BUFFER_DATA (outbuf), bytes_per_packet);
#endif

  if (outsize < 0) {
    GST_ELEMENT_ERROR (enc, STREAM, ENCODE, (NULL),
        ("encoding failed: %d", outsize));
    ret = GST_FLOW_ERROR;
    goto done;
  }

  GST_DEBUG_OBJECT (enc, "encoding %d bytes", bytes);

  ret = gst_audio_encoder_finish_frame (GST_AUDIO_ENCODER (enc),
      outbuf, frame_size);

done:
  g_free (data0);
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
    g_assert (gst_buffer_is_metadata_writable (buf));

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
gst_celt_enc_handle_frame (GstAudioEncoder * benc, GstBuffer * buf)
{
  GstCeltEnc *enc;
  GstFlowReturn ret = GST_FLOW_OK;

  enc = GST_CELT_ENC (benc);

  if (!enc->header_sent) {
    /* Celt streams begin with two headers; the initial header (with
       most of the codec setup parameters) which is mandated by the Ogg
       bitstream spec.  The second header holds any comment fields.
       We merely need to make the headers, then pass them to libcelt 
       one at a time; libcelt handles the additional Ogg bitstream 
       constraints */
    GstBuffer *buf1, *buf2;
    GstCaps *caps;
    /* libcelt has a bug which underestimates header size by 4... */
    unsigned int header_size = enc->header.header_size + 4;
    unsigned char *data = g_malloc (header_size);

    /* create header buffer */
    int error = celt_header_to_packet (&enc->header, data, header_size);
    if (error < 0) {
      g_free (data);
      goto no_header;
    }
    buf1 = gst_buffer_new ();
    GST_BUFFER_DATA (buf1) = GST_BUFFER_MALLOCDATA (buf1) = data;
    GST_BUFFER_SIZE (buf1) = header_size;
    GST_BUFFER_OFFSET_END (buf1) = 0;
    GST_BUFFER_OFFSET (buf1) = 0;

    /* create comment buffer */
    buf2 = gst_celt_enc_create_metadata_buffer (enc);

    /* mark and put on caps */
    caps = gst_pad_get_caps (GST_AUDIO_ENCODER_SRC_PAD (enc));
    gst_caps_set_simple (caps,
        "rate", G_TYPE_INT, enc->rate,
        "channels", G_TYPE_INT, enc->channels,
        "frame-size", G_TYPE_INT, enc->frame_size, NULL);
    caps = _gst_caps_set_buffer_array (caps, "streamheader", buf1, buf2, NULL);

    /* negotiate with these caps */
    GST_DEBUG_OBJECT (enc, "here are the caps: %" GST_PTR_FORMAT, caps);
    GST_LOG_OBJECT (enc, "rate=%d channels=%d frame-size=%d",
        enc->rate, enc->channels, enc->frame_size);
    gst_pad_set_caps (GST_AUDIO_ENCODER_SRC_PAD (enc), caps);

    gst_buffer_set_caps (buf1, caps);
    gst_buffer_set_caps (buf2, caps);
    gst_caps_unref (caps);

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

  ret = gst_celt_enc_encode (enc, buf);

done:
  return ret;

  /* ERRORS */
no_header:
  {
    GST_ELEMENT_ERROR (enc, STREAM, ENCODE, (NULL),
        ("Failed to encode header"));
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

/* push out the buffer */
static GstFlowReturn
gst_celt_enc_push_buffer (GstCeltEnc * enc, GstBuffer * buffer)
{
  guint size;

  size = GST_BUFFER_SIZE (buffer);
  GST_DEBUG_OBJECT (enc, "pushing output buffer of size %u", size);

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (GST_AUDIO_ENCODER_SRC_PAD (enc)));
  return gst_pad_push (GST_AUDIO_ENCODER_SRC_PAD (enc), buffer);
}

static GstFlowReturn
gst_celt_enc_pre_push (GstAudioEncoder * benc, GstBuffer ** buffer)
{
  GstCeltEnc *enc;
  GstFlowReturn ret = GST_FLOW_OK;

  enc = GST_CELT_ENC (benc);

  /* FIXME 0.11 ? get rid of this special ogg stuff and have it
   * put and use 'codec data' in caps like anything else,
   * with all the usual out-of-band advantage etc */
  if (G_UNLIKELY (enc->headers)) {
    GSList *header = enc->headers;

    /* try to push all of these, if we lose one, might as well lose all */
    while (header) {
      if (ret == GST_FLOW_OK)
        ret = gst_celt_enc_push_buffer (enc, header->data);
      else
        gst_celt_enc_push_buffer (enc, header->data);
      header = g_slist_next (header);
    }

    g_slist_free (enc->headers);
    enc->headers = NULL;
  }

  return ret;
}

static void
gst_celt_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstCeltEnc *enc;

  enc = GST_CELT_ENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_int (value, enc->bitrate);
      break;
    case PROP_FRAMESIZE:
      g_value_set_int (value, enc->frame_size);
      break;
    case PROP_CBR:
      g_value_set_boolean (value, enc->cbr);
      break;
    case PROP_COMPLEXITY:
      g_value_set_int (value, enc->complexity);
      break;
    case PROP_MAX_BITRATE:
      g_value_set_int (value, enc->max_bitrate);
      break;
    case PROP_PREDICTION:
      g_value_set_enum (value, enc->prediction);
      break;
    case PROP_START_BAND:
      g_value_set_int (value, enc->start_band);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_celt_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCeltEnc *enc;

  enc = GST_CELT_ENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      enc->bitrate = g_value_get_int (value);
      break;
    case PROP_FRAMESIZE:
      enc->requested_frame_size = g_value_get_int (value);
      enc->frame_size = enc->requested_frame_size;
      break;
    case PROP_CBR:
      enc->cbr = g_value_get_boolean (value);
      break;
    case PROP_COMPLEXITY:
      enc->complexity = g_value_get_int (value);
      break;
    case PROP_MAX_BITRATE:
      enc->max_bitrate = g_value_get_int (value);
      break;
    case PROP_PREDICTION:
      enc->prediction = g_value_get_enum (value);
      break;
    case PROP_START_BAND:
      enc->start_band = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
