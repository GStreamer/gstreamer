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

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 6000, 48000 ], "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, " "width = (int) 16, " "depth = (int) 16")
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

static void gst_speex_enc_finalize (GObject * object);

static gboolean gst_speex_enc_setup (GstSpeexEnc * enc);

static void gst_speex_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_speex_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_speex_enc_encode (GstSpeexEnc * enc, GstBuffer * buf);

static gboolean gst_speex_enc_start (GstAudioEncoder * enc);
static gboolean gst_speex_enc_stop (GstAudioEncoder * enc);
static gboolean gst_speex_enc_set_format (GstAudioEncoder * enc,
    GstAudioInfo * info);
static GstFlowReturn gst_speex_enc_handle_frame (GstAudioEncoder * enc,
    GstBuffer * in_buf);
static gboolean gst_speex_enc_sink_event (GstAudioEncoder * enc,
    GstEvent * event);
static GstFlowReturn
gst_speex_enc_pre_push (GstAudioEncoder * benc, GstBuffer ** buffer);

static void
gst_speex_enc_setup_interfaces (GType speexenc_type)
{
  static const GInterfaceInfo tag_setter_info = { NULL, NULL, NULL };

  g_type_add_interface_static (speexenc_type, GST_TYPE_TAG_SETTER,
      &tag_setter_info);

  GST_DEBUG_CATEGORY_INIT (speexenc_debug, "speexenc", 0, "Speex encoder");
}

GST_BOILERPLATE_FULL (GstSpeexEnc, gst_speex_enc, GstAudioEncoder,
    GST_TYPE_AUDIO_ENCODER, gst_speex_enc_setup_interfaces);

static void
gst_speex_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_set_details_simple (element_class, "Speex audio encoder",
      "Codec/Encoder/Audio",
      "Encodes audio in Speex format", "Wim Taymans <wim@fluendo.com>");
}

static void
gst_speex_enc_class_init (GstSpeexEncClass * klass)
{
  GObjectClass *gobject_class;
  GstAudioEncoderClass *base_class;

  gobject_class = (GObjectClass *) klass;
  base_class = (GstAudioEncoderClass *) klass;

  gobject_class->set_property = gst_speex_enc_set_property;
  gobject_class->get_property = gst_speex_enc_get_property;

  base_class->start = GST_DEBUG_FUNCPTR (gst_speex_enc_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_speex_enc_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_speex_enc_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_speex_enc_handle_frame);
  base_class->event = GST_DEBUG_FUNCPTR (gst_speex_enc_sink_event);
  base_class->pre_push = GST_DEBUG_FUNCPTR (gst_speex_enc_pre_push);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_QUALITY,
      g_param_spec_float ("quality", "Quality", "Encoding quality",
          0.0, 10.0, DEFAULT_QUALITY,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BITRATE,
      g_param_spec_int ("bitrate", "Encoding Bit-rate",
          "Specify an encoding bit-rate (in bps). (0 = automatic)",
          0, G_MAXINT, DEFAULT_BITRATE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode", "The encoding mode",
          GST_TYPE_SPEEX_ENC_MODE, GST_SPEEX_ENC_MODE_AUTO,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VBR,
      g_param_spec_boolean ("vbr", "VBR",
          "Enable variable bit-rate", DEFAULT_VBR,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ABR,
      g_param_spec_int ("abr", "ABR",
          "Enable average bit-rate (0 = disabled)",
          0, G_MAXINT, DEFAULT_ABR,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VAD,
      g_param_spec_boolean ("vad", "VAD",
          "Enable voice activity detection", DEFAULT_VAD,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DTX,
      g_param_spec_boolean ("dtx", "DTX",
          "Enable discontinuous transmission", DEFAULT_DTX,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COMPLEXITY,
      g_param_spec_int ("complexity", "Complexity",
          "Set encoding complexity",
          0, G_MAXINT, DEFAULT_COMPLEXITY,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_NFRAMES,
      g_param_spec_int ("nframes", "NFrames",
          "Number of frames per buffer",
          0, G_MAXINT, DEFAULT_NFRAMES,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LAST_MESSAGE,
      g_param_spec_string ("last-message", "last-message",
          "The last status message", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_speex_enc_finalize;
}

static void
gst_speex_enc_finalize (GObject * object)
{
  GstSpeexEnc *enc;

  enc = GST_SPEEX_ENC (object);

  g_free (enc->last_message);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_speex_enc_init (GstSpeexEnc * enc, GstSpeexEncClass * klass)
{
  GstAudioEncoder *benc = GST_AUDIO_ENCODER (enc);

  /* arrange granulepos marking (and required perfect ts) */
  gst_audio_encoder_set_mark_granule (benc, TRUE);
  gst_audio_encoder_set_perfect_timestamp (benc, TRUE);
}

static gboolean
gst_speex_enc_start (GstAudioEncoder * benc)
{
  GstSpeexEnc *enc = GST_SPEEX_ENC (benc);

  GST_DEBUG_OBJECT (enc, "start");
  speex_bits_init (&enc->bits);
  enc->tags = gst_tag_list_new ();
  enc->header_sent = FALSE;

  return TRUE;
}

static gboolean
gst_speex_enc_stop (GstAudioEncoder * benc)
{
  GstSpeexEnc *enc = GST_SPEEX_ENC (benc);

  GST_DEBUG_OBJECT (enc, "stop");
  enc->header_sent = FALSE;
  if (enc->state) {
    speex_encoder_destroy (enc->state);
    enc->state = NULL;
  }
  speex_bits_destroy (&enc->bits);
  gst_tag_list_free (enc->tags);
  enc->tags = NULL;
  g_slist_foreach (enc->headers, (GFunc) gst_buffer_unref, NULL);
  enc->headers = NULL;

  gst_tag_setter_reset_tags (GST_TAG_SETTER (enc));

  return TRUE;
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

static gboolean
gst_speex_enc_set_format (GstAudioEncoder * benc, GstAudioInfo * info)
{
  GstSpeexEnc *enc;

  enc = GST_SPEEX_ENC (benc);

  enc->channels = GST_AUDIO_INFO_CHANNELS (info);
  enc->rate = GST_AUDIO_INFO_RATE (info);

  /* handle reconfigure */
  if (enc->state) {
    speex_encoder_destroy (enc->state);
    enc->state = NULL;
  }

  if (!gst_speex_enc_setup (enc))
    return FALSE;

  /* feedback to base class */
  gst_audio_encoder_set_latency (benc,
      gst_speex_enc_get_latency (enc), gst_speex_enc_get_latency (enc));
  gst_audio_encoder_set_lookahead (benc, enc->lookahead);

  if (enc->nframes == 0) {
    /* as many frames as available input allows */
    gst_audio_encoder_set_frame_samples_min (benc, enc->frame_size);
    gst_audio_encoder_set_frame_samples_max (benc, enc->frame_size);
    gst_audio_encoder_set_frame_max (benc, 0);
  } else {
    /* exactly as many frames as configured */
    gst_audio_encoder_set_frame_samples_min (benc,
        enc->frame_size * enc->nframes);
    gst_audio_encoder_set_frame_samples_max (benc,
        enc->frame_size * enc->nframes);
    gst_audio_encoder_set_frame_max (benc, 1);
  }

  return TRUE;
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

  GST_BUFFER_OFFSET (comments) = 0;
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

  return TRUE;
}

/* push out the buffer */
static GstFlowReturn
gst_speex_enc_push_buffer (GstSpeexEnc * enc, GstBuffer * buffer)
{
  guint size;

  size = GST_BUFFER_SIZE (buffer);
  GST_DEBUG_OBJECT (enc, "pushing output buffer of size %u", size);

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (GST_AUDIO_ENCODER_SRC_PAD (enc)));
  return gst_pad_push (GST_AUDIO_ENCODER_SRC_PAD (enc), buffer);
}

static gboolean
gst_speex_enc_sink_event (GstAudioEncoder * benc, GstEvent * event)
{
  GstSpeexEnc *enc;

  enc = GST_SPEEX_ENC (benc);

  switch (GST_EVENT_TYPE (event)) {
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
      break;
    }
    default:
      break;
  }

  /* we only peeked, let base class handle it */
  return FALSE;
}

static GstFlowReturn
gst_speex_enc_encode (GstSpeexEnc * enc, GstBuffer * buf)
{
  gint frame_size = enc->frame_size;
  gint bytes = frame_size * 2 * enc->channels, samples, size;
  gint outsize, written, dtx_ret = 0;
  guint8 *data, *data0 = NULL;
  GstBuffer *outbuf;
  GstFlowReturn ret = GST_FLOW_OK;

  if (G_LIKELY (buf)) {
    data = GST_BUFFER_DATA (buf);
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

  samples = size / (2 * enc->channels);
  speex_bits_reset (&enc->bits);

  /* FIXME what about dropped samples if DTS enabled ?? */

  while (size) {
    GST_DEBUG_OBJECT (enc, "encoding %d samples (%d bytes)", frame_size, bytes);

    if (enc->channels == 2) {
      speex_encode_stereo_int ((gint16 *) data, frame_size, &enc->bits);
    }
    dtx_ret += speex_encode_int (enc->state, (gint16 *) data, &enc->bits);

    data += bytes;
    size -= bytes;
  }

  speex_bits_insert_terminator (&enc->bits);
  outsize = speex_bits_nbytes (&enc->bits);

  ret = gst_pad_alloc_buffer_and_set_caps (GST_AUDIO_ENCODER_SRC_PAD (enc),
      GST_BUFFER_OFFSET_NONE, outsize,
      GST_PAD_CAPS (GST_AUDIO_ENCODER_SRC_PAD (enc)), &outbuf);

  if ((GST_FLOW_OK != ret))
    goto done;

  written = speex_bits_write (&enc->bits,
      (gchar *) GST_BUFFER_DATA (outbuf), outsize);

  if (G_UNLIKELY (written < outsize)) {
    GST_ERROR_OBJECT (enc, "short write: %d < %d bytes", written, outsize);
    GST_BUFFER_SIZE (outbuf) = written;
  } else if (G_UNLIKELY (written > outsize)) {
    GST_ERROR_OBJECT (enc, "overrun: %d > %d bytes", written, outsize);
  }

  if (!dtx_ret)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_GAP);

  ret = gst_audio_encoder_finish_frame (GST_AUDIO_ENCODER (enc),
      outbuf, samples);

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
gst_speex_enc_handle_frame (GstAudioEncoder * benc, GstBuffer * buf)
{
  GstSpeexEnc *enc;
  GstFlowReturn ret = GST_FLOW_OK;

  enc = GST_SPEEX_ENC (benc);

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
    buf1 = gst_buffer_new ();
    GST_BUFFER_DATA (buf1) = GST_BUFFER_MALLOCDATA (buf1) = data;
    GST_BUFFER_SIZE (buf1) = data_len;
    GST_BUFFER_OFFSET_END (buf1) = 0;
    GST_BUFFER_OFFSET (buf1) = 0;

    /* create comment buffer */
    buf2 = gst_speex_enc_create_metadata_buffer (enc);

    /* mark and put on caps */
    caps = gst_caps_new_simple ("audio/x-speex", "rate", G_TYPE_INT, enc->rate,
        "channels", G_TYPE_INT, enc->channels, NULL);
    caps = _gst_caps_set_buffer_array (caps, "streamheader", buf1, buf2, NULL);

    /* negotiate with these caps */
    GST_DEBUG_OBJECT (enc, "here are the caps: %" GST_PTR_FORMAT, caps);

    gst_buffer_set_caps (buf1, caps);
    gst_buffer_set_caps (buf2, caps);
    gst_pad_set_caps (GST_AUDIO_ENCODER_SRC_PAD (enc), caps);
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

  ret = gst_speex_enc_encode (enc, buf);

  return ret;
}

static GstFlowReturn
gst_speex_enc_pre_push (GstAudioEncoder * benc, GstBuffer ** buffer)
{
  GstSpeexEnc *enc;
  GstFlowReturn ret = GST_FLOW_OK;

  enc = GST_SPEEX_ENC (benc);

  /* FIXME 0.11 ? get rid of this special ogg stuff and have it
   * put and use 'codec data' in caps like anything else,
   * with all the usual out-of-band advantage etc */
  if (G_UNLIKELY (enc->headers)) {
    GSList *header = enc->headers;

    /* try to push all of these, if we lose one, might as well lose all */
    while (header) {
      if (ret == GST_FLOW_OK)
        ret = gst_speex_enc_push_buffer (enc, header->data);
      else
        gst_speex_enc_push_buffer (enc, header->data);
      header = g_slist_next (header);
    }

    g_slist_free (enc->headers);
    enc->headers = NULL;
  }

  return ret;
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
