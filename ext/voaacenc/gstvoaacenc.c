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

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "signed = (boolean) TRUE, "
        "endianness = (int) BYTE_ORDER, "
        "rate = (int) [8000, 96000], " "channels = (int) [1, 2]")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 4, "
        "rate = (int) [8000, 96000], "
        "channels = (int) [1, 2], "
        "stream-format = (string) { adts, raw }, " "base-profile = (string) lc")
    );

GST_DEBUG_CATEGORY_STATIC (gst_voaacenc_debug);
#define GST_CAT_DEFAULT gst_voaacenc_debug

static void gst_voaacenc_finalize (GObject * object);

static GstFlowReturn gst_voaacenc_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_voaacenc_setcaps (GstPad * pad, GstCaps * caps);
static GstStateChangeReturn gst_voaacenc_state_change (GstElement * element,
    GstStateChange transition);
static gboolean voaacenc_core_init (GstVoAacEnc * voaacenc);
static gboolean voaacenc_core_set_parameter (GstVoAacEnc * voaacenc);
static void voaacenc_core_uninit (GstVoAacEnc * voaacenc);
static GstCaps *gst_voaacenc_getcaps (GstPad * pad);
static GstCaps *gst_voaacenc_create_source_pad_caps (GstVoAacEnc * voaacenc);
static gint voaacenc_get_rate_index (gint rate);

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

  for (i = 0; i < VOAAC_ENC_MAX_CHANNELS; i++) {
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
        "rate", GST_TYPE_INT_RANGE, 8000, 96000, "channels", G_TYPE_INT, i + 1,
        NULL);

    gst_structure_set_value (structure, "channel-positions", &chanpos);
    g_value_unset (&chanpos);

    gst_caps_append_structure (caps, structure);
  }

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

  return gst_caps_ref (caps);
}

static void
_do_init (GType object_type)
{
  const GInterfaceInfo preset_interface_info = {
    NULL,                       /* interface init */
    NULL,                       /* interface finalize */
    NULL                        /* interface_data */
  };

  g_type_add_interface_static (object_type, GST_TYPE_PRESET,
      &preset_interface_info);

  GST_DEBUG_CATEGORY_INIT (gst_voaacenc_debug, "voaacenc", 0,
      "AAC audio encoder");
}

GST_BOILERPLATE_FULL (GstVoAacEnc, gst_voaacenc, GstElement, GST_TYPE_ELEMENT,
    _do_init);

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
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  object_class->set_property = GST_DEBUG_FUNCPTR (gst_voaacenc_set_property);
  object_class->get_property = GST_DEBUG_FUNCPTR (gst_voaacenc_get_property);
  object_class->finalize = GST_DEBUG_FUNCPTR (gst_voaacenc_finalize);

  g_object_class_install_property (object_class, PROP_BITRATE,
      g_param_spec_int ("bitrate",
          "Bitrate",
          "Target Audio Bitrate",
          0, G_MAXINT, VOAAC_ENC_DEFAULT_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_voaacenc_state_change);
}

static void
gst_voaacenc_init (GstVoAacEnc * voaacenc, GstVoAacEncClass * klass)
{
  /* create the sink pad */
  voaacenc->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_setcaps_function (voaacenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_voaacenc_setcaps));
  gst_pad_set_getcaps_function (voaacenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_voaacenc_getcaps));
  gst_pad_set_chain_function (voaacenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_voaacenc_chain));
  gst_element_add_pad (GST_ELEMENT (voaacenc), voaacenc->sinkpad);

  /* create the src pad */
  voaacenc->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (voaacenc->srcpad);
  gst_element_add_pad (GST_ELEMENT (voaacenc), voaacenc->srcpad);

  voaacenc->adapter = gst_adapter_new ();

  voaacenc->bitrate = VOAAC_ENC_DEFAULT_BITRATE;
  voaacenc->output_format = VOAAC_ENC_DEFAULT_OUTPUTFORMAT;

  /* init rest */
  voaacenc->handle = NULL;
}

static void
gst_voaacenc_finalize (GObject * object)
{
  GstVoAacEnc *voaacenc;

  voaacenc = GST_VOAACENC (object);

  g_object_unref (G_OBJECT (voaacenc->adapter));
  voaacenc->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* check downstream caps to configure format */
static void
gst_voaacenc_negotiate (GstVoAacEnc * voaacenc)
{
  GstCaps *caps;

  caps = gst_pad_get_allowed_caps (voaacenc->srcpad);

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

static GstCaps *
gst_voaacenc_getcaps (GstPad * pad)
{
  return gst_voaacenc_get_sink_caps ();
}


static gboolean
gst_voaacenc_setcaps (GstPad * pad, GstCaps * caps)
{
  gboolean ret = FALSE;
  GstStructure *structure;
  GstVoAacEnc *voaacenc;
  GstCaps *src_caps;

  voaacenc = GST_VOAACENC (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  /* get channel count */
  gst_structure_get_int (structure, "channels", &voaacenc->channels);
  gst_structure_get_int (structure, "rate", &voaacenc->rate);

  /* precalc duration as it's constant now */
  voaacenc->duration =
      gst_util_uint64_scale_int (1024, GST_SECOND, voaacenc->rate);
  voaacenc->inbuf_size = voaacenc->channels * 2 * 1024;

  gst_voaacenc_negotiate (voaacenc);

  /* create reverse caps */
  src_caps = gst_voaacenc_create_source_pad_caps (voaacenc);

  if (src_caps) {
    gst_pad_set_caps (voaacenc->srcpad, src_caps);
    gst_caps_unref (src_caps);
    ret = voaacenc_core_set_parameter (voaacenc);
  }
  return ret;
}

static GstFlowReturn
gst_voaacenc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVoAacEnc *voaacenc;
  GstFlowReturn ret;
  guint64 timestamp, distance = 0;

  voaacenc = GST_VOAACENC (GST_PAD_PARENT (pad));

  g_return_val_if_fail (voaacenc->handle, GST_FLOW_WRONG_STATE);

  if (voaacenc->rate == 0 || voaacenc->channels == 0)
    goto not_negotiated;

  /* discontinuity clears adapter, FIXME, maybe we can set some
   * encoder flag to mask the discont. */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    gst_adapter_clear (voaacenc->adapter);
    voaacenc->discont = TRUE;
  }

  ret = GST_FLOW_OK;
  gst_adapter_push (voaacenc->adapter, buffer);

  /* Collect samples until we have enough for an output frame */
  while (gst_adapter_available (voaacenc->adapter) >= voaacenc->inbuf_size) {
    GstBuffer *out;
    guint8 *data;
    VO_CODECBUFFER input = { 0 }
    , output = {
    0};
    VO_AUDIO_OUTPUTINFO output_info = { {0}
    };


    /* max size */
    if ((ret =
            gst_pad_alloc_buffer_and_set_caps (voaacenc->srcpad, 0,
                voaacenc->inbuf_size, GST_PAD_CAPS (voaacenc->srcpad),
                &out)) != GST_FLOW_OK) {
      return ret;
    }

    output.Buffer = GST_BUFFER_DATA (out);
    output.Length = voaacenc->inbuf_size;

    if (voaacenc->discont) {
      GST_BUFFER_FLAG_SET (out, GST_BUFFER_FLAG_DISCONT);
      voaacenc->discont = FALSE;
    }

    data =
        (guint8 *) gst_adapter_peek (voaacenc->adapter, voaacenc->inbuf_size);
    input.Buffer = data;
    input.Length = voaacenc->inbuf_size;
    voaacenc->codec_api.SetInputData (voaacenc->handle, &input);

    /* encode */
    if (voaacenc->codec_api.GetOutputData (voaacenc->handle, &output,
            &output_info) != VO_ERR_NONE) {
      gst_buffer_unref (out);
      return GST_FLOW_ERROR;
    }

    /* get timestamp from adapter */
    timestamp = gst_adapter_prev_timestamp (voaacenc->adapter, &distance);

    if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (timestamp))) {
      GST_BUFFER_TIMESTAMP (out) =
          timestamp +
          GST_FRAMES_TO_CLOCK_TIME (distance / voaacenc->channels /
          VOAAC_ENC_BITS_PER_SAMPLE, voaacenc->rate);
    }

    GST_BUFFER_DURATION (out) =
        GST_FRAMES_TO_CLOCK_TIME (voaacenc->inbuf_size / voaacenc->channels /
        VOAAC_ENC_BITS_PER_SAMPLE, voaacenc->rate);

    GST_LOG_OBJECT (voaacenc, "Pushing out buffer time: %" GST_TIME_FORMAT
        " duration: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (out)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (out)));

    GST_BUFFER_SIZE (out) = output.Length;

    /* flush the among of data we have peek */
    gst_adapter_flush (voaacenc->adapter, voaacenc->inbuf_size);

    /* play */
    if ((ret = gst_pad_push (voaacenc->srcpad, out)) != GST_FLOW_OK)
      break;
  }
  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (voaacenc, STREAM, TYPE_NOT_FOUND,
        (NULL), ("unknown type"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstStateChangeReturn
gst_voaacenc_state_change (GstElement * element, GstStateChange transition)
{
  GstVoAacEnc *voaacenc;
  GstStateChangeReturn ret;

  voaacenc = GST_VOAACENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (voaacenc_core_init (voaacenc) == FALSE)
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      voaacenc->rate = 0;
      voaacenc->channels = 0;
      voaacenc->discont = FALSE;
      gst_adapter_clear (voaacenc->adapter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      voaacenc_core_uninit (voaacenc);
      gst_adapter_clear (voaacenc->adapter);
      break;
    default:
      break;
  }
  return ret;
}

static GstCaps *
gst_voaacenc_create_source_pad_caps (GstVoAacEnc * voaacenc)
{
  GstCaps *caps = NULL;
  GstBuffer *codec_data;
  gint index;
  guint8 data[VOAAC_ENC_CODECDATA_LEN];

  if ((index = voaacenc_get_rate_index (voaacenc->rate)) >= 0) {
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

static gint
voaacenc_get_rate_index (gint rate)
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
