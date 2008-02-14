/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2008  Marcel Holtmann <marcel@holtmann.org>
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

#include "ipc.h"
#include "gstsbcenc.h"
#include "gstsbcutil.h"

#define SBC_ENC_DEFAULT_MODE BT_A2DP_CHANNEL_MODE_JOINT_STEREO
#define SBC_ENC_DEFAULT_BLOCKS 0
#define SBC_ENC_DEFAULT_SUB_BANDS 0
#define SBC_ENC_DEFAULT_ALLOCATION BT_A2DP_ALLOCATION_LOUDNESS
#define SBC_ENC_DEFAULT_RATE 0
#define SBC_ENC_DEFAULT_CHANNELS 0

#define SBC_ENC_BITPOOL_AUTO 1
#define SBC_ENC_BITPOOL_MIN 2
#define SBC_ENC_BITPOOL_MIN_STR "2"
#define SBC_ENC_BITPOOL_MAX 64
#define SBC_ENC_BITPOOL_MAX_STR "64"

GST_DEBUG_CATEGORY_STATIC (sbc_enc_debug);
#define GST_CAT_DEFAULT sbc_enc_debug

#define GST_TYPE_SBC_MODE (gst_sbc_mode_get_type())

static GType
gst_sbc_mode_get_type (void)
{
  static GType sbc_mode_type = 0;
  static GEnumValue sbc_modes[] = {
    {0, "Auto", "auto"},
    {1, "Mono", "mono"},
    {2, "Dual Channel", "dual"},
    {3, "Stereo", "stereo"},
    {4, "Joint Stereo", "joint"},
    {-1, NULL, NULL}
  };

  if (!sbc_mode_type)
    sbc_mode_type = g_enum_register_static ("GstSbcMode", sbc_modes);

  return sbc_mode_type;
}

#define GST_TYPE_SBC_ALLOCATION (gst_sbc_allocation_get_type())

static GType
gst_sbc_allocation_get_type (void)
{
  static GType sbc_allocation_type = 0;
  static GEnumValue sbc_allocations[] = {
    {BT_A2DP_ALLOCATION_LOUDNESS, "Loudness", "loudness"},
    {BT_A2DP_ALLOCATION_SNR, "SNR", "snr"},
    {-1, NULL, NULL}
  };

  if (!sbc_allocation_type)
    sbc_allocation_type =
        g_enum_register_static ("GstSbcAllocation", sbc_allocations);

  return sbc_allocation_type;
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

GST_BOILERPLATE (GstSbcEnc, gst_sbc_enc, GstElement, GST_TYPE_ELEMENT);

static const GstElementDetails sbc_enc_details =
GST_ELEMENT_DETAILS ("Bluetooth SBC encoder",
    "Codec/Encoder/Audio",
    "Encode a SBC audio stream",
    "Marcel Holtmann <marcel@holtmann.org>");

static GstStaticPadTemplate sbc_enc_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) { 16000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) LITTLE_ENDIAN, "
        "signed = (boolean) true, " "width = (int) 16, " "depth = (int) 16"));

static GstStaticPadTemplate sbc_enc_src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sbc, "
        "rate = (int) { 16000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ], "
        "mode = (string) { mono, dual, stereo, joint }, "
        "blocks = (int) { 4, 8, 12, 16 }, "
        "subbands = (int) { 4, 8 }, "
        "allocation = (string) { snr, loudness },"
        "bitpool = (int) [ " SBC_ENC_BITPOOL_MIN_STR
        ", " SBC_ENC_BITPOOL_MAX_STR " ]"));

gboolean gst_sbc_enc_fill_sbc_params (GstSbcEnc * enc, GstCaps * caps);

static GstCaps *
sbc_enc_generate_srcpad_caps (GstSbcEnc * enc)
{
  GstCaps *src_caps;
  GstStructure *structure;
  GEnumValue *enum_value;
  GEnumClass *enum_class;
  gchar *temp;
  GValue *value;

  src_caps = gst_caps_copy (gst_pad_get_pad_template_caps (enc->srcpad));
  structure = gst_caps_get_structure (src_caps, 0);

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

  if (enc->mode != SBC_ENC_DEFAULT_MODE) {
    enum_class = g_type_class_ref (GST_TYPE_SBC_MODE);
    enum_value = g_enum_get_value (enum_class, enc->mode);
    gst_sbc_util_set_structure_string_param (structure, "mode",
        enum_value->value_nick, value);
    g_type_class_unref (enum_class);
  }

  if (enc->allocation != SBC_ENC_DEFAULT_ALLOCATION) {
    enum_class = g_type_class_ref (GST_TYPE_SBC_ALLOCATION);
    enum_value = g_enum_get_value (enum_class, enc->allocation);
    gst_sbc_util_set_structure_string_param (structure, "allocation",
        enum_value->value_nick, value);
    g_type_class_unref (enum_class);
  }

  temp = gst_caps_to_string (src_caps);
  GST_DEBUG_OBJECT (enc, "Srcpad caps: %s", temp);
  g_free (temp);
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
sbc_enc_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSbcEnc *enc = GST_SBC_ENC (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (enc, "setting srcpad caps");

  return gst_sbc_enc_fill_sbc_params (enc, caps);
}

static GstCaps *
sbc_enc_src_caps_fixate (GstSbcEnc * enc, GstCaps * caps)
{

  gchar *error_message = NULL;
  GstCaps *result;

  result = gst_sbc_util_caps_fixate (caps, &error_message);

  if (!result) {
    GST_WARNING_OBJECT (enc, "Invalid input caps caused parsing "
        "error: %s", error_message);
    g_free (error_message);
    return NULL;
  }

  return result;
}

static GstCaps *
sbc_enc_get_fixed_srcpad_caps (GstSbcEnc * enc)
{
  GstCaps *caps;
  gboolean res = TRUE;
  GstCaps *result_caps = NULL;

  caps = gst_pad_get_allowed_caps (enc->srcpad);
  if (caps == NULL)
    caps = sbc_enc_src_getcaps (enc->srcpad);

  if (caps == GST_CAPS_NONE || gst_caps_is_empty (caps)) {
    res = FALSE;
    goto done;
  }

  result_caps = sbc_enc_src_caps_fixate (enc, caps);

done:
  gst_caps_unref (caps);

  if (!res)
    return NULL;

  return result_caps;
}

static gboolean
sbc_enc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSbcEnc *enc;
  GstStructure *structure;
  GstCaps *src_caps;
  gint rate, channels;
  gboolean res;

  enc = GST_SBC_ENC (GST_PAD_PARENT (pad));
  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rate", &rate))
    return FALSE;
  if (!gst_structure_get_int (structure, "channels", &channels))
    return FALSE;

  enc->rate = rate;
  enc->channels = channels;

  src_caps = sbc_enc_get_fixed_srcpad_caps (enc);
  if (!src_caps)
    return FALSE;
  res = gst_pad_set_caps (enc->srcpad, src_caps);
  gst_caps_unref (src_caps);

  return res;
}

gboolean
gst_sbc_enc_fill_sbc_params (GstSbcEnc * enc, GstCaps * caps)
{
  gint mode;

  if (!gst_caps_is_fixed (caps)) {
    GST_DEBUG_OBJECT (enc, "didn't receive fixed caps, " "returning false");
    return FALSE;
  }

  if (!gst_sbc_util_fill_sbc_params (&enc->sbc, caps))
    return FALSE;

  if (enc->rate != 0 && enc->sbc.rate != enc->rate)
    goto fail;

  if (enc->channels != 0 && enc->sbc.channels != enc->channels)
    goto fail;

  if (enc->blocks != 0 && enc->sbc.blocks != enc->blocks)
    goto fail;

  if (enc->subbands != 0 && enc->sbc.subbands != enc->subbands)
    goto fail;

  mode = gst_sbc_get_mode_int_from_sbc_t (&enc->sbc);
  if (enc->mode != SBC_ENC_DEFAULT_MODE && mode != enc->mode)
    goto fail;

  if (enc->allocation != SBC_ENC_DEFAULT_ALLOCATION &&
      enc->sbc.allocation != enc->allocation)
    goto fail;

  if (enc->bitpool != SBC_ENC_BITPOOL_AUTO && enc->sbc.bitpool != enc->bitpool)
    goto fail;

  enc->codesize = sbc_get_codesize (&enc->sbc);
  enc->frame_length = sbc_get_frame_length (&enc->sbc);
  enc->frame_duration = sbc_get_frame_duration (&enc->sbc);

  GST_DEBUG ("codesize: %d, frame_length: %d, frame_duration: %d",
      enc->codesize, enc->frame_length, enc->frame_duration);

  return TRUE;

fail:
  memset (&enc->sbc, 0, sizeof (sbc_t));
  return FALSE;
}

static GstFlowReturn
sbc_enc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSbcEnc *enc = GST_SBC_ENC (gst_pad_get_parent (pad));
  GstAdapter *adapter = enc->adapter;
  GstFlowReturn res = GST_FLOW_OK;

  gst_adapter_push (adapter, buffer);

  while (gst_adapter_available (adapter) >= enc->codesize && res == GST_FLOW_OK) {
    GstBuffer *output;
    GstCaps *caps;
    const guint8 *data;
    int consumed;

    caps = GST_PAD_CAPS (enc->srcpad);
    res = gst_pad_alloc_buffer_and_set_caps (enc->srcpad,
        GST_BUFFER_OFFSET_NONE, enc->frame_length, caps, &output);
    if (res != GST_FLOW_OK)
      goto done;

    data = gst_adapter_peek (adapter, enc->codesize);
    consumed = sbc_encode (&enc->sbc, (gpointer) data,
        enc->codesize,
        GST_BUFFER_DATA (output), GST_BUFFER_SIZE (output), NULL);
    if (consumed <= 0) {
      GST_ERROR ("comsumed < 0, codesize: %d", enc->codesize);
      break;
    }
    gst_adapter_flush (adapter, consumed);

    GST_BUFFER_TIMESTAMP (output) = GST_BUFFER_TIMESTAMP (buffer);
    /* we have only 1 frame */
    GST_BUFFER_DURATION (output) = enc->frame_duration;

    res = gst_pad_push (enc->srcpad, output);
    if (res != GST_FLOW_OK)
      goto done;
  }

done:
  gst_object_unref (enc);

  return res;
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

  return parent_class->change_state (element, transition);
}

static void
gst_sbc_enc_dispose (GObject * object)
{
  GstSbcEnc *enc = GST_SBC_ENC (object);

  if (enc->adapter != NULL)
    g_object_unref (G_OBJECT (enc->adapter));

  enc->adapter = NULL;
}

static void
gst_sbc_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sbc_enc_sink_factory));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sbc_enc_src_factory));

  gst_element_class_set_details (element_class, &sbc_enc_details);
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

  object_class->set_property = GST_DEBUG_FUNCPTR (gst_sbc_enc_set_property);
  object_class->get_property = GST_DEBUG_FUNCPTR (gst_sbc_enc_get_property);
  object_class->dispose = GST_DEBUG_FUNCPTR (gst_sbc_enc_dispose);

  element_class->change_state = GST_DEBUG_FUNCPTR (sbc_enc_change_state);

  g_object_class_install_property (object_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode",
          "Encoding mode", GST_TYPE_SBC_MODE,
          SBC_ENC_DEFAULT_MODE, G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_ALLOCATION,
      g_param_spec_enum ("allocation", "Allocation",
          "Allocation method", GST_TYPE_SBC_ALLOCATION,
          SBC_ENC_DEFAULT_ALLOCATION, G_PARAM_READWRITE));

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

  GST_DEBUG_CATEGORY_INIT (sbc_enc_debug, "sbcenc", 0, "SBC encoding element");
}

static void
gst_sbc_enc_init (GstSbcEnc * self, GstSbcEncClass * klass)
{
  self->sinkpad =
      gst_pad_new_from_static_template (&sbc_enc_sink_factory, "sink");
  gst_pad_set_setcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (sbc_enc_sink_setcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&sbc_enc_src_factory, "src");
  gst_pad_set_getcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (sbc_enc_src_getcaps));
  gst_pad_set_setcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (sbc_enc_src_setcaps));
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  gst_pad_set_chain_function (self->sinkpad, GST_DEBUG_FUNCPTR (sbc_enc_chain));

  self->subbands = SBC_ENC_DEFAULT_SUB_BANDS;
  self->blocks = SBC_ENC_DEFAULT_BLOCKS;
  self->mode = SBC_ENC_DEFAULT_MODE;
  self->allocation = SBC_ENC_DEFAULT_ALLOCATION;
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
