/*  GStreamer SBC audio encoder
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "gstsbcutil.h"
#include "gstsbcenc.h"

#include <gst/audio/audio.h>

#define SBC_ENC_DEFAULT_CHANNEL_MODE SBC_MODE_AUTO
#define SBC_ENC_DEFAULT_BLOCKS 0
#define SBC_ENC_DEFAULT_SUB_BANDS 0
#define SBC_ENC_DEFAULT_ALLOCATION_METHOD SBC_AM_AUTO
#define SBC_ENC_DEFAULT_RATE 0
#define SBC_ENC_DEFAULT_CHANNELS 0

#define SBC_ENC_BITPOOL_AUTO 1
#define SBC_ENC_BITPOOL_MIN 2
#define SBC_ENC_BITPOOL_MIN_STR "2"
#define SBC_ENC_BITPOOL_MAX 64
#define SBC_ENC_BITPOOL_MAX_STR "64"

GST_DEBUG_CATEGORY_STATIC (sbc_enc_debug);
#define GST_CAT_DEFAULT sbc_enc_debug

#define GST_TYPE_SBC_CHANNEL_MODE (gst_sbc_channel_mode_get_type())

static GType
gst_sbc_channel_mode_get_type (void)
{
  static GType sbc_channel_mode_type = 0;
  static GEnumValue sbc_channel_modes[] = {
    {SBC_MODE_MONO, "Mono", "mono"},
    {SBC_MODE_DUAL_CHANNEL, "Dual", "dual"},
    {SBC_MODE_STEREO, "Stereo", "stereo"},
    {SBC_MODE_JOINT_STEREO, "Joint Stereo", "joint"},
    {SBC_MODE_AUTO, "Auto", "auto"},
    {-1, NULL, NULL}
  };

  if (!sbc_channel_mode_type) {
    sbc_channel_mode_type =
        g_enum_register_static ("GstSbcChannelMode", sbc_channel_modes);
  }

  return sbc_channel_mode_type;
}

#define GST_TYPE_SBC_ALLOCATION_METHOD (gst_sbc_allocation_method_get_type())

static GType
gst_sbc_allocation_method_get_type (void)
{
  static GType sbc_allocation_method_type = 0;
  static GEnumValue sbc_allocation_methods[] = {
    {SBC_AM_LOUDNESS, "Loudness", "loudness"},
    {SBC_AM_SNR, "SNR", "snr"},
    {SBC_AM_AUTO, "Auto", "auto"},
    {-1, NULL, NULL}
  };

  if (!sbc_allocation_method_type)
    sbc_allocation_method_type =
        g_enum_register_static ("GstSbcAllocationMethod",
        sbc_allocation_methods);

  return sbc_allocation_method_type;
}

#define GST_TYPE_SBC_BLOCKS (gst_sbc_blocks_get_type())

static GType
gst_sbc_blocks_get_type (void)
{
  static GType sbc_blocks_type = 0;
  static GEnumValue sbc_blocks[] = {
    {0, "Auto", "auto"},
    {4, "4", "4"},
    {8, "8", "8"},
    {12, "12", "12"},
    {16, "16", "16"},
    {-1, NULL, NULL}
  };

  if (!sbc_blocks_type)
    sbc_blocks_type = g_enum_register_static ("GstSbcBlocks", sbc_blocks);

  return sbc_blocks_type;
}

#define GST_TYPE_SBC_SUBBANDS (gst_sbc_subbands_get_type())

static GType
gst_sbc_subbands_get_type (void)
{
  static GType sbc_subbands_type = 0;
  static GEnumValue sbc_subbands[] = {
    {0, "Auto", "auto"},
    {4, "4 subbands", "4"},
    {8, "8 subbands", "8"},
    {-1, NULL, NULL}
  };

  if (!sbc_subbands_type)
    sbc_subbands_type = g_enum_register_static ("GstSbcSubbands", sbc_subbands);

  return sbc_subbands_type;
}

enum
{
  PROP_0,
  PROP_MODE,
  PROP_ALLOCATION,
  PROP_BLOCKS,
  PROP_SUBBANDS,
  PROP_BITPOOL
};

/* FIXME: rewrite based on GstAudioEncoder base class */
#define parent_class gst_sbc_enc_parent_class
G_DEFINE_TYPE (GstSbcEnc, gst_sbc_enc, GST_TYPE_ELEMENT);

static GstStaticPadTemplate sbc_enc_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, format=" GST_AUDIO_NE (S16) ", "
        "rate = (int) { 16000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ]"));

static GstStaticPadTemplate sbc_enc_src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sbc, "
        "rate = (int) { 16000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ], "
        "channel-mode = (string) { mono, dual, stereo, joint }, "
        "blocks = (int) { 4, 8, 12, 16 }, "
        "subbands = (int) { 4, 8 }, "
        "allocation-method = (string) { snr, loudness }, "
        "bitpool = (int) [ " SBC_ENC_BITPOOL_MIN_STR
        ", " SBC_ENC_BITPOOL_MAX_STR " ]"));


static gboolean gst_sbc_enc_fill_sbc_params (GstSbcEnc * enc, GstCaps * caps);

static gboolean gst_sbc_enc_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

/* FIXME does this make sense? */
static GstCaps *
sbc_enc_generate_srcpad_caps (GstSbcEnc * enc)
{
  GstCaps *src_caps;
  GstStructure *structure;
  GEnumValue *enum_value;
  GEnumClass *enum_class;
  GValue *value;

  src_caps = gst_caps_copy (gst_pad_get_pad_template_caps (enc->srcpad));
  structure = gst_caps_get_structure (src_caps, 0);

  /* FIXME: use gst_structure_set() */
  value = g_new0 (GValue, 1);

  if (enc->rate != 0)
    gst_sbc_util_set_structure_int_param (structure, "rate", enc->rate, value);

  if (enc->channels != 0)
    gst_sbc_util_set_structure_int_param (structure, "channels",
        enc->channels, value);

  if (enc->subbands != 0)
    gst_sbc_util_set_structure_int_param (structure, "subbands",
        enc->subbands, value);

  if (enc->blocks != 0)
    gst_sbc_util_set_structure_int_param (structure, "blocks",
        enc->blocks, value);

  if (enc->bitpool != SBC_ENC_BITPOOL_AUTO)
    gst_sbc_util_set_structure_int_param (structure, "bitpool",
        enc->bitpool, value);

  if (enc->mode != SBC_ENC_DEFAULT_CHANNEL_MODE) {
    enum_class = g_type_class_ref (GST_TYPE_SBC_CHANNEL_MODE);
    enum_value = g_enum_get_value (enum_class, enc->mode);
    gst_sbc_util_set_structure_string_param (structure, "channel-mode",
        enum_value->value_nick, value);
    g_type_class_unref (enum_class);
  }

  if (enc->allocation != SBC_AM_AUTO) {
    enum_class = g_type_class_ref (GST_TYPE_SBC_ALLOCATION_METHOD);
    enum_value = g_enum_get_value (enum_class, enc->allocation);
    gst_sbc_util_set_structure_string_param (structure, "allocation-method",
        enum_value->value_nick, value);
    g_type_class_unref (enum_class);
  }

  g_free (value);

  return src_caps;
}

static GstCaps *
sbc_enc_src_getcaps (GstPad * pad)
{
  GstSbcEnc *enc;

  enc = GST_SBC_ENC (GST_PAD_PARENT (pad));

  return sbc_enc_generate_srcpad_caps (enc);
}

static gboolean
sbc_enc_negotiate_output_caps (GstSbcEnc * enc, GstCaps * in_caps)
{
  GstStructure *s;
  GstCaps *caps, *filter_caps;
  GstCaps *output_caps;

  GST_INFO_OBJECT (enc, "input caps %" GST_PTR_FORMAT, in_caps);

  caps = gst_pad_get_allowed_caps (enc->srcpad);
  if (caps == NULL)
    caps = sbc_enc_src_getcaps (enc->srcpad);

  if (caps == GST_CAPS_NONE || gst_caps_is_empty (caps)) {
    gst_caps_unref (caps);
    return FALSE;
  }

  /* fixate output caps */
  filter_caps = gst_caps_new_simple ("audio/x-sbc", "rate", G_TYPE_INT,
      enc->rate, "channels", G_TYPE_INT, enc->channels, NULL);
  output_caps = gst_caps_intersect (caps, filter_caps);
  gst_caps_unref (filter_caps);

  if (output_caps == NULL || gst_caps_is_empty (output_caps)) {
    GST_DEBUG_OBJECT (enc, "Couldn't negotiate output caps with input rate %d "
        "and input channels %d and allowed output caps %" GST_PTR_FORMAT,
        enc->rate, enc->channels, caps);
    if (output_caps)
      gst_caps_unref (output_caps);
    gst_caps_unref (caps);
    return FALSE;
  }
  gst_caps_unref (caps);

  GST_DEBUG_OBJECT (enc, "fixating caps %" GST_PTR_FORMAT, output_caps);
  output_caps = gst_caps_truncate (output_caps);
  s = gst_caps_get_structure (output_caps, 0);
  if (enc->channels == 1) {
    if (!gst_structure_fixate_field_string (s, "channel-mode", "mono")) {
      GST_DEBUG_OBJECT (enc, "Failed to fixate channel-mode to mono");
      gst_caps_unref (output_caps);
      return FALSE;
    }
  } else {
    if (gst_structure_fixate_field_string (s, "channel-mode", "joint")
        || gst_structure_fixate_field_string (s, "channel-mode", "stereo")) {
      gst_structure_fixate_field_string (s, "channel-mode", "dual");
    }
  }

  gst_structure_fixate_field_nearest_int (s, "bitpool", 64);
  gst_structure_fixate_field_nearest_int (s, "blocks", 16);
  gst_structure_fixate_field_nearest_int (s, "subbands", 8);
  gst_structure_fixate_field_string (s, "allocation-method", "loudness");
  s = NULL;

  /* in case there's anything else left to fixate */
  output_caps = gst_caps_fixate (output_caps);

  GST_INFO_OBJECT (enc, "output caps %" GST_PTR_FORMAT, output_caps);
  if (!gst_sbc_enc_fill_sbc_params (enc, output_caps)) {
    GST_WARNING_OBJECT (enc, "failed to configure encoder from output "
        "caps %" GST_PTR_FORMAT, output_caps);
    return FALSE;
  }

  gst_pad_send_event (enc->sinkpad, gst_event_new_caps (output_caps));
  gst_caps_unref (output_caps);

  return TRUE;
}

static gboolean
sbc_enc_sink_setcaps (GstSbcEnc * enc, GstCaps * caps)
{
  GstStructure *structure;
  gint rate, channels;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rate", &rate))
    return FALSE;
  if (!gst_structure_get_int (structure, "channels", &channels))
    return FALSE;

  enc->rate = rate;
  enc->channels = channels;

  return sbc_enc_negotiate_output_caps (enc, caps);
}

static gboolean
gst_sbc_enc_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSbcEnc *enc = GST_SBC_ENC (parent);
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:{
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = sbc_enc_sink_setcaps (enc, caps);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

/* configure encoder based on output caps */
static gboolean
gst_sbc_enc_fill_sbc_params (GstSbcEnc * enc, GstCaps * caps)
{
  if (!gst_caps_is_fixed (caps)) {
    GST_DEBUG_OBJECT (enc, "output caps %" GST_PTR_FORMAT " not fixed!", caps);
    return FALSE;
  }

  if (!gst_sbc_util_fill_sbc_params (&enc->sbc, caps))
    return FALSE;

  if (enc->rate != 0 && gst_sbc_parse_rate_from_sbc (enc->sbc.frequency)
      != enc->rate)
    goto fail;

  if (enc->channels != 0 && gst_sbc_get_channel_number (enc->sbc.mode)
      != enc->channels)
    goto fail;

  if (enc->blocks != 0 && gst_sbc_parse_blocks_from_sbc (enc->sbc.blocks)
      != enc->blocks)
    goto fail;

  if (enc->subbands != 0
      && gst_sbc_parse_subbands_from_sbc (enc->sbc.subbands) != enc->subbands)
    goto fail;

  if (enc->mode != SBC_ENC_DEFAULT_CHANNEL_MODE && enc->sbc.mode != enc->mode)
    goto fail;

  if (enc->allocation != SBC_AM_AUTO && enc->sbc.allocation != enc->allocation)
    goto fail;

  if (enc->bitpool != SBC_ENC_BITPOOL_AUTO && enc->sbc.bitpool != enc->bitpool)
    goto fail;

  enc->codesize = sbc_get_codesize (&enc->sbc);
  enc->frame_length = sbc_get_frame_length (&enc->sbc);
  enc->frame_duration = sbc_get_frame_duration (&enc->sbc) * 1000;

  GST_DEBUG_OBJECT (enc, "codesize: %d, frame_length: %d, frame_duration:"
      " %d", enc->codesize, enc->frame_length, enc->frame_duration);

  return TRUE;

fail:
  memset (&enc->sbc, 0, sizeof (sbc_t));
  return FALSE;
}

static GstFlowReturn
sbc_enc_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstSbcEnc *enc = GST_SBC_ENC (parent);
  GstAdapter *adapter = enc->adapter;
  GstFlowReturn res = GST_FLOW_OK;

  if (enc->codesize == 0)
    goto not_negotiated;

  gst_adapter_push (adapter, buffer);

  while (gst_adapter_available (adapter) >= enc->codesize) {
    GstMapInfo out_map;
    GstBuffer *outbuf;
    const guint8 *data;
    gint consumed;

    outbuf = gst_buffer_new_and_alloc (enc->frame_length);
    gst_buffer_map (outbuf, &out_map, GST_MAP_WRITE);

    data = gst_adapter_map (adapter, enc->codesize);

    consumed = sbc_encode (&enc->sbc, (gpointer) data,
        enc->codesize, out_map.data, out_map.size, NULL);

    gst_adapter_unmap (adapter);
    gst_buffer_unmap (outbuf, &out_map);

    if (consumed <= 0) {
      GST_DEBUG_OBJECT (enc, "consumed < 0, codesize: %d", enc->codesize);
      gst_buffer_unref (outbuf);
      break;
    }
    gst_adapter_flush (adapter, consumed);

    /* FIXME: this is not right if we don't have a 1:1 mapping of
     * input and output data */
    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
    /* we have only 1 frame */
    GST_BUFFER_DURATION (outbuf) = enc->frame_duration;

    res = gst_pad_push (enc->srcpad, outbuf);

    if (res != GST_FLOW_OK)
      break;
  }

  return res;

not_negotiated:
  {
    GST_ERROR_OBJECT (enc, "output caps not negotiated yet");
    gst_buffer_unref (buffer);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstStateChangeReturn
sbc_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstSbcEnc *enc = GST_SBC_ENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG ("Setup subband codec");
      sbc_init (&enc->sbc, 0);
      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG ("Finish subband codec");
      sbc_finish (&enc->sbc);
      break;

    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static void
gst_sbc_enc_finalize (GObject * object)
{
  GstSbcEnc *enc = GST_SBC_ENC (object);

  if (enc->adapter != NULL) {
    g_object_unref (enc->adapter);
    enc->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_sbc_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSbcEnc *enc = GST_SBC_ENC (object);

  /* changes to those properties will only happen on the next caps
   * negotiation */

  switch (prop_id) {
    case PROP_MODE:
      enc->mode = g_value_get_enum (value);
      break;
    case PROP_ALLOCATION:
      enc->allocation = g_value_get_enum (value);
      break;
    case PROP_BLOCKS:
      enc->blocks = g_value_get_enum (value);
      break;
    case PROP_SUBBANDS:
      enc->subbands = g_value_get_enum (value);
      break;
    case PROP_BITPOOL:
      enc->bitpool = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_sbc_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSbcEnc *enc = GST_SBC_ENC (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, enc->mode);
      break;
    case PROP_ALLOCATION:
      g_value_set_enum (value, enc->allocation);
      break;
    case PROP_BLOCKS:
      g_value_set_enum (value, enc->blocks);
      break;
    case PROP_SUBBANDS:
      g_value_set_enum (value, enc->subbands);
      break;
    case PROP_BITPOOL:
      g_value_set_int (value, enc->bitpool);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_sbc_enc_class_init (GstSbcEncClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->set_property = gst_sbc_enc_set_property;
  object_class->get_property = gst_sbc_enc_get_property;
  object_class->finalize = gst_sbc_enc_finalize;

  element_class->change_state = GST_DEBUG_FUNCPTR (sbc_enc_change_state);

  g_object_class_install_property (object_class, PROP_MODE,
      g_param_spec_enum ("channel-mode", "Channel Mode",
          "Channel mode", GST_TYPE_SBC_CHANNEL_MODE,
          SBC_ENC_DEFAULT_CHANNEL_MODE, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_ALLOCATION,
      g_param_spec_enum ("allocation-method", "Allocation Method",
          "Allocation method", GST_TYPE_SBC_ALLOCATION_METHOD,
          SBC_ENC_DEFAULT_ALLOCATION_METHOD, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_BLOCKS,
      g_param_spec_enum ("blocks", "Blocks",
          "Blocks", GST_TYPE_SBC_BLOCKS,
          SBC_ENC_DEFAULT_BLOCKS, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_SUBBANDS,
      g_param_spec_enum ("subbands", "Sub bands",
          "Number of sub bands", GST_TYPE_SBC_SUBBANDS,
          SBC_ENC_DEFAULT_SUB_BANDS, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_BITPOOL,
      g_param_spec_int ("bitpool", "Bitpool",
          "Bitpool (use 1 for automatic selection)",
          SBC_ENC_BITPOOL_AUTO, SBC_ENC_BITPOOL_MAX,
          SBC_ENC_BITPOOL_AUTO, G_PARAM_READWRITE));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sbc_enc_sink_factory));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sbc_enc_src_factory));

  gst_element_class_set_static_metadata (element_class,
      "Bluetooth SBC audio encoder", "Codec/Encoder/Audio",
      "Encode an SBC audio stream", "Marcel Holtmann <marcel@holtmann.org>");

  GST_DEBUG_CATEGORY_INIT (sbc_enc_debug, "sbcenc", 0, "SBC encoding element");
}

static void
gst_sbc_enc_init (GstSbcEnc * self)
{
  self->sinkpad =
      gst_pad_new_from_static_template (&sbc_enc_sink_factory, "sink");
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_sbc_enc_sink_event));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&sbc_enc_src_factory, "src");
#if 0
  gst_pad_set_getcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (sbc_enc_src_getcaps));
#endif
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  gst_pad_set_chain_function (self->sinkpad, GST_DEBUG_FUNCPTR (sbc_enc_chain));

  self->subbands = SBC_ENC_DEFAULT_SUB_BANDS;
  self->blocks = SBC_ENC_DEFAULT_BLOCKS;
  self->mode = SBC_ENC_DEFAULT_CHANNEL_MODE;
  self->allocation = SBC_ENC_DEFAULT_ALLOCATION_METHOD;
  self->rate = SBC_ENC_DEFAULT_RATE;
  self->channels = SBC_ENC_DEFAULT_CHANNELS;
  self->bitpool = SBC_ENC_BITPOOL_AUTO;

  self->frame_length = 0;
  self->frame_duration = 0;

  self->adapter = gst_adapter_new ();
}

gboolean
gst_sbc_enc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "sbcenc",
      GST_RANK_NONE, GST_TYPE_SBC_ENC);
}
