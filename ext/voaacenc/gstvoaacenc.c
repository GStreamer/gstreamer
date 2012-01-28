/* GStreamer AAC encoder plugin
 * Copyright (C) 2011 Kan Hu <kan.hu@linaro.org>
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
 * SECTION:element-voaacenc
 *
 * AAC audio encoder based on vo-aacenc library 
 * <ulink url="http://sourceforge.net/projects/opencore-amr/files/vo-aacenc/">vo-aacenc library source file</ulink>.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=abc.wav ! wavparse ! audioresample ! audioconvert ! voaacenc ! filesink location=abc.aac
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/audio/multichannel.h>
#include <gst/pbutils/codec-utils.h>

#include "gstvoaacenc.h"

#define VOAAC_ENC_DEFAULT_BITRATE (128000)
#define VOAAC_ENC_DEFAULT_OUTPUTFORMAT (0)      /* RAW */
#define VOAAC_ENC_MPEGVERSION (4)
#define VOAAC_ENC_CODECDATA_LEN (2)
#define VOAAC_ENC_BITS_PER_SAMPLE (16)

enum
{
  PROP_0,
  PROP_BITRATE
};

#define SAMPLE_RATES " 8000, " \
                    "11025, " \
                    "12000, " \
                    "16000, " \
                    "22050, " \
                    "24000, " \
                    "32000, " \
                    "44100, " \
                    "48000, " \
                    "64000, " \
                    "88200, " \
                    "96000"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "signed = (boolean) TRUE, "
        "endianness = (int) BYTE_ORDER, "
        "rate = (int) { " SAMPLE_RATES " }, " "channels = (int) [1, 2]")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 4, "
        "rate = (int) { " SAMPLE_RATES " }, "
        "channels = (int) [1, 2], "
        "stream-format = (string) { adts, raw }, " "base-profile = (string) lc")
    );

GST_DEBUG_CATEGORY_STATIC (gst_voaacenc_debug);
#define GST_CAT_DEFAULT gst_voaacenc_debug

static gboolean voaacenc_core_init (GstVoAacEnc * voaacenc);
static gboolean voaacenc_core_set_parameter (GstVoAacEnc * voaacenc);
static void voaacenc_core_uninit (GstVoAacEnc * voaacenc);

static gboolean gst_voaacenc_start (GstAudioEncoder * enc);
static gboolean gst_voaacenc_stop (GstAudioEncoder * enc);
static gboolean gst_voaacenc_set_format (GstAudioEncoder * enc,
    GstAudioInfo * info);
static GstFlowReturn gst_voaacenc_handle_frame (GstAudioEncoder * enc,
    GstBuffer * in_buf);
static GstCaps *gst_voaacenc_getcaps (GstAudioEncoder * enc);

GST_BOILERPLATE (GstVoAacEnc, gst_voaacenc, GstAudioEncoder,
    GST_TYPE_AUDIO_ENCODER);

static void
gst_voaacenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVoAacEnc *self = GST_VOAACENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      self->bitrate = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static void
gst_voaacenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVoAacEnc *self = GST_VOAACENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_int (value, self->bitrate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static void
gst_voaacenc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details_simple (element_class, "AAC audio encoder",
      "Codec/Encoder/Audio", "AAC audio encoder", "Kan Hu <kan.hu@linaro.org>");
}

static void
gst_voaacenc_class_init (GstVoAacEncClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstAudioEncoderClass *base_class = GST_AUDIO_ENCODER_CLASS (klass);

  object_class->set_property = GST_DEBUG_FUNCPTR (gst_voaacenc_set_property);
  object_class->get_property = GST_DEBUG_FUNCPTR (gst_voaacenc_get_property);

  base_class->start = GST_DEBUG_FUNCPTR (gst_voaacenc_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_voaacenc_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_voaacenc_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_voaacenc_handle_frame);
  base_class->getcaps = GST_DEBUG_FUNCPTR (gst_voaacenc_getcaps);

  g_object_class_install_property (object_class, PROP_BITRATE,
      g_param_spec_int ("bitrate",
          "Bitrate",
          "Target Audio Bitrate",
          0, G_MAXINT, VOAAC_ENC_DEFAULT_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (gst_voaacenc_debug, "voaacenc", 0, "voaac encoder");
}

static void
gst_voaacenc_init (GstVoAacEnc * voaacenc, GstVoAacEncClass * klass)
{
  voaacenc->bitrate = VOAAC_ENC_DEFAULT_BITRATE;
  voaacenc->output_format = VOAAC_ENC_DEFAULT_OUTPUTFORMAT;

  /* init rest */
  voaacenc->handle = NULL;
}

static gboolean
gst_voaacenc_start (GstAudioEncoder * enc)
{
  GstVoAacEnc *voaacenc = GST_VOAACENC (enc);

  GST_DEBUG_OBJECT (enc, "start");

  if (voaacenc_core_init (voaacenc) == FALSE)
    return FALSE;

  voaacenc->rate = 0;
  voaacenc->channels = 0;

  return TRUE;
}

static gboolean
gst_voaacenc_stop (GstAudioEncoder * enc)
{
  GstVoAacEnc *voaacenc = GST_VOAACENC (enc);

  GST_DEBUG_OBJECT (enc, "stop");
  voaacenc_core_uninit (voaacenc);

  return TRUE;
}

static gpointer
gst_voaacenc_generate_sink_caps (gpointer data)
{
#define VOAAC_ENC_MAX_CHANNELS 6
/* describe the channels position */
  static const GstAudioChannelPosition
      gst_voaacenc_channel_position[][VOAAC_ENC_MAX_CHANNELS] = {
    {                           /* 1 ch: Mono */
        GST_AUDIO_CHANNEL_POSITION_FRONT_MONO},
    {                           /* 2 ch: front left + front right (front stereo) */
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
    {                           /* 3 ch: front center + front stereo */
          GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
    {                           /* 4 ch: front center + front stereo + back center */
          GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_CENTER},
    {                           /* 5 ch: front center + front stereo + back stereo */
          GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT},
    {                           /* 6ch: front center + front stereo + back stereo + LFE */
          GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
          GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_LFE}
  };
  GstCaps *caps = gst_caps_new_empty ();
  gint i, c;
  static const int rates[] = {
    8000, 11025, 12000, 16000, 22050, 24000,
    32000, 44100, 48000, 64000, 88200, 96000
  };
  GValue rates_arr = { 0, };
  GValue tmp = { 0, };

  g_value_init (&rates_arr, GST_TYPE_LIST);
  g_value_init (&tmp, G_TYPE_INT);
  for (i = 0; i < G_N_ELEMENTS (rates); i++) {
    g_value_set_int (&tmp, rates[i]);
    gst_value_list_append_value (&rates_arr, &tmp);
  }
  g_value_unset (&tmp);

  for (i = 0; i < 2 /*VOAAC_ENC_MAX_CHANNELS */ ; i++) {
    GValue chanpos = { 0 };
    GValue pos = { 0 };
    GstStructure *structure;

    g_value_init (&chanpos, GST_TYPE_ARRAY);
    g_value_init (&pos, GST_TYPE_AUDIO_CHANNEL_POSITION);

    for (c = 0; c <= i; c++) {
      g_value_set_enum (&pos, gst_voaacenc_channel_position[i][c]);
      gst_value_array_append_value (&chanpos, &pos);
    }

    g_value_unset (&pos);

    structure = gst_structure_new ("audio/x-raw-int",
        "width", G_TYPE_INT, 16,
        "depth", G_TYPE_INT, 16,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "channels", G_TYPE_INT, i + 1, NULL);

    gst_structure_set_value (structure, "rate", &rates_arr);
    gst_structure_set_value (structure, "channel-positions", &chanpos);
    g_value_unset (&chanpos);

    gst_caps_append_structure (caps, structure);
  }

  g_value_unset (&rates_arr);

  GST_DEBUG ("generated sink caps: %" GST_PTR_FORMAT, caps);
  return caps;
}

static GstCaps *
gst_voaacenc_get_sink_caps (void)
{
  static GOnce g_once = G_ONCE_INIT;
  GstCaps *caps;

  g_once (&g_once, gst_voaacenc_generate_sink_caps, NULL);
  caps = g_once.retval;

  return caps;
}

static GstCaps *
gst_voaacenc_getcaps (GstAudioEncoder * benc)
{
  return gst_audio_encoder_proxy_getcaps (benc, gst_voaacenc_get_sink_caps ());
}

/* check downstream caps to configure format */
static void
gst_voaacenc_negotiate (GstVoAacEnc * voaacenc)
{
  GstCaps *caps;

  caps = gst_pad_get_allowed_caps (GST_AUDIO_ENCODER_SRC_PAD (voaacenc));

  GST_DEBUG_OBJECT (voaacenc, "allowed caps: %" GST_PTR_FORMAT, caps);

  if (caps && gst_caps_get_size (caps) > 0) {
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const gchar *str = NULL;

    if ((str = gst_structure_get_string (s, "stream-format"))) {
      if (strcmp (str, "adts") == 0) {
        GST_DEBUG_OBJECT (voaacenc, "use ADTS format for output");
        voaacenc->output_format = 1;
      } else if (strcmp (str, "raw") == 0) {
        GST_DEBUG_OBJECT (voaacenc, "use RAW format for output");
        voaacenc->output_format = 0;
      } else {
        GST_DEBUG_OBJECT (voaacenc, "unknown stream-format: %s", str);
        voaacenc->output_format = VOAAC_ENC_DEFAULT_OUTPUTFORMAT;
      }
    }
  }

  if (caps)
    gst_caps_unref (caps);
}

static gint
gst_voaacenc_get_rate_index (gint rate)
{
  static const gint rate_table[] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000
  };
  gint i;
  for (i = 0; i < G_N_ELEMENTS (rate_table); ++i) {
    if (rate == rate_table[i]) {
      return i;
    }
  }
  return -1;
}

static GstCaps *
gst_voaacenc_create_source_pad_caps (GstVoAacEnc * voaacenc)
{
  GstCaps *caps = NULL;
  GstBuffer *codec_data;
  gint index;
  guint8 data[VOAAC_ENC_CODECDATA_LEN];

  if ((index = gst_voaacenc_get_rate_index (voaacenc->rate)) >= 0) {
    /* LC profile only */
    data[0] = ((0x02 << 3) | (index >> 1));
    data[1] = ((index & 0x01) << 7) | (voaacenc->channels << 3);

    caps = gst_caps_new_simple ("audio/mpeg",
        "mpegversion", G_TYPE_INT, VOAAC_ENC_MPEGVERSION,
        "channels", G_TYPE_INT, voaacenc->channels,
        "rate", G_TYPE_INT, voaacenc->rate,
        "stream-format", G_TYPE_STRING,
        (voaacenc->output_format ? "adts" : "raw")
        , NULL);

    gst_codec_utils_aac_caps_set_level_and_profile (caps, data, sizeof (data));

    if (!voaacenc->output_format) {
      codec_data = gst_buffer_new_and_alloc (VOAAC_ENC_CODECDATA_LEN);

      memcpy (GST_BUFFER_DATA (codec_data), data, sizeof (data));

      gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, codec_data,
          NULL);

      gst_buffer_unref (codec_data);
    }
  }

  return caps;
}

static gboolean
gst_voaacenc_set_format (GstAudioEncoder * benc, GstAudioInfo * info)
{
  gboolean ret = FALSE;
  GstVoAacEnc *voaacenc;
  GstCaps *src_caps;

  voaacenc = GST_VOAACENC (benc);

  /* get channel count */
  voaacenc->channels = GST_AUDIO_INFO_CHANNELS (info);
  voaacenc->rate = GST_AUDIO_INFO_RATE (info);

  /* precalc buffer size as it's constant now */
  voaacenc->inbuf_size = voaacenc->channels * 2 * 1024;

  gst_voaacenc_negotiate (voaacenc);

  /* create reverse caps */
  src_caps = gst_voaacenc_create_source_pad_caps (voaacenc);

  if (src_caps) {
    gst_pad_set_caps (GST_AUDIO_ENCODER_SRC_PAD (voaacenc), src_caps);
    gst_caps_unref (src_caps);
    ret = voaacenc_core_set_parameter (voaacenc);
  }

  /* report needs to base class */
  gst_audio_encoder_set_frame_samples_min (benc, 1024);
  gst_audio_encoder_set_frame_samples_max (benc, 1024);
  gst_audio_encoder_set_frame_max (benc, 1);

  return ret;
}

static GstFlowReturn
gst_voaacenc_handle_frame (GstAudioEncoder * benc, GstBuffer * buf)
{
  GstVoAacEnc *voaacenc;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *out;
  VO_AUDIO_OUTPUTINFO output_info = { {0} };
  VO_CODECBUFFER input = { 0 };
  VO_CODECBUFFER output = { 0 };

  voaacenc = GST_VOAACENC (benc);

  g_return_val_if_fail (voaacenc->handle, GST_FLOW_NOT_NEGOTIATED);

  if (voaacenc->rate == 0 || voaacenc->channels == 0)
    goto not_negotiated;

  /* we don't deal with squeezing remnants, so simply discard those */
  if (G_UNLIKELY (buf == NULL)) {
    GST_DEBUG_OBJECT (benc, "no data");
    goto exit;
  }

  if (G_UNLIKELY (GST_BUFFER_SIZE (buf) < voaacenc->inbuf_size)) {
    GST_DEBUG_OBJECT (voaacenc, "discarding trailing data %d",
        buf ? GST_BUFFER_SIZE (buf) : 0);
    ret = gst_audio_encoder_finish_frame (benc, NULL, -1);
    goto exit;
  }

  /* max size */
  if ((ret =
          gst_pad_alloc_buffer_and_set_caps (GST_AUDIO_ENCODER_SRC_PAD
              (voaacenc), 0, voaacenc->inbuf_size,
              GST_PAD_CAPS (GST_AUDIO_ENCODER_SRC_PAD (voaacenc)),
              &out)) != GST_FLOW_OK) {
    goto exit;
  }

  output.Buffer = GST_BUFFER_DATA (out);
  output.Length = voaacenc->inbuf_size;

  g_assert (GST_BUFFER_SIZE (buf) == voaacenc->inbuf_size);
  input.Buffer = GST_BUFFER_DATA (buf);
  input.Length = voaacenc->inbuf_size;
  voaacenc->codec_api.SetInputData (voaacenc->handle, &input);

  /* encode */
  if (voaacenc->codec_api.GetOutputData (voaacenc->handle, &output,
          &output_info) != VO_ERR_NONE) {
    gst_buffer_unref (out);
    goto encode_failed;
  }

  GST_LOG_OBJECT (voaacenc, "encoded to %d bytes", output.Length);
  GST_BUFFER_SIZE (out) = output.Length;

  GST_LOG_OBJECT (voaacenc, "Pushing out buffer time: %" GST_TIME_FORMAT
      " duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (out)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (out)));

  ret = gst_audio_encoder_finish_frame (benc, out, 1024);

exit:
  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (voaacenc, STREAM, TYPE_NOT_FOUND,
        (NULL), ("unknown type"));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto exit;
  }
encode_failed:
  {
    GST_ELEMENT_ERROR (voaacenc, STREAM, ENCODE, (NULL), ("encode failed"));
    ret = GST_FLOW_ERROR;
    goto exit;
  }
}

static VO_U32
voaacenc_core_mem_alloc (VO_S32 uID, VO_MEM_INFO * pMemInfo)
{
  if (!pMemInfo)
    return VO_ERR_INVALID_ARG;

  pMemInfo->VBuffer = g_malloc (pMemInfo->Size);
  return 0;
}

static VO_U32
voaacenc_core_mem_free (VO_S32 uID, VO_PTR pMem)
{
  g_free (pMem);
  return 0;
}

static VO_U32
voaacenc_core_mem_set (VO_S32 uID, VO_PTR pBuff, VO_U8 uValue, VO_U32 uSize)
{
  memset (pBuff, uValue, uSize);
  return 0;
}

static VO_U32
voaacenc_core_mem_copy (VO_S32 uID, VO_PTR pDest, VO_PTR pSource, VO_U32 uSize)
{
  memcpy (pDest, pSource, uSize);
  return 0;
}

static VO_U32
voaacenc_core_mem_check (VO_S32 uID, VO_PTR pBuffer, VO_U32 uSize)
{
  return 0;
}

static gboolean
voaacenc_core_init (GstVoAacEnc * voaacenc)
{
  VO_CODEC_INIT_USERDATA user_data = { 0 };
  voGetAACEncAPI (&voaacenc->codec_api);

  voaacenc->mem_operator.Alloc = voaacenc_core_mem_alloc;
  voaacenc->mem_operator.Copy = voaacenc_core_mem_copy;
  voaacenc->mem_operator.Free = voaacenc_core_mem_free;
  voaacenc->mem_operator.Set = voaacenc_core_mem_set;
  voaacenc->mem_operator.Check = voaacenc_core_mem_check;
  user_data.memflag = VO_IMF_USERMEMOPERATOR;
  user_data.memData = &voaacenc->mem_operator;
  voaacenc->codec_api.Init (&voaacenc->handle, VO_AUDIO_CodingAAC, &user_data);

  if (voaacenc->handle == NULL) {
    return FALSE;
  }
  return TRUE;

}

static gboolean
voaacenc_core_set_parameter (GstVoAacEnc * voaacenc)
{
  AACENC_PARAM params = { 0 };
  guint32 ret;

  params.sampleRate = voaacenc->rate;
  params.bitRate = voaacenc->bitrate;
  params.nChannels = voaacenc->channels;
  if (voaacenc->output_format) {
    params.adtsUsed = 1;
  } else {
    params.adtsUsed = 0;
  }

  ret =
      voaacenc->codec_api.SetParam (voaacenc->handle, VO_PID_AAC_ENCPARAM,
      &params);
  if (ret != VO_ERR_NONE) {
    GST_ERROR_OBJECT (voaacenc, "Failed to set encoder parameters");
    return FALSE;
  }
  return TRUE;
}

static void
voaacenc_core_uninit (GstVoAacEnc * voaacenc)
{
  if (voaacenc->handle) {
    voaacenc->codec_api.Uninit (voaacenc->handle);
    voaacenc->handle = NULL;
  }
}
