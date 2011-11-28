/* GStreamer Wavpack encoder plugin
 * Copyright (c) 2006 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstwavpackdec.c: Wavpack audio encoder
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
 * SECTION:element-wavpackenc
 *
 * WavpackEnc encodes raw audio into a framed Wavpack stream.
 * <ulink url="http://www.wavpack.com/">Wavpack</ulink> is an open-source
 * audio codec that features both lossless and lossy encoding.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch audiotestsrc num-buffers=500 ! audioconvert ! wavpackenc ! filesink location=sinewave.wv
 * ]| This pipeline encodes audio from audiotestsrc into a Wavpack file. The audioconvert element is needed
 * as the Wavpack encoder only accepts input with 32 bit width (and every depth between 1 and 32 bits).
 * |[
 * gst-launch cdda://1 ! audioconvert ! wavpackenc ! filesink location=track1.wv
 * ]| This pipeline encodes audio from an audio CD into a Wavpack file using
 * lossless encoding (the file output will be fairly large).
 * |[
 * gst-launch cdda://1 ! audioconvert ! wavpackenc bitrate=128000 ! filesink location=track1.wv
 * ]| This pipeline encodes audio from an audio CD into a Wavpack file using
 * lossy encoding at a certain bitrate (the file will be fairly small).
 * </refsect2>
 */

/*
 * TODO: - add 32 bit float mode. CONFIG_FLOAT_DATA
 */

#include <string.h>
#include <gst/gst.h>
#include <glib/gprintf.h>

#include <wavpack/wavpack.h>
#include "gstwavpackenc.h"
#include "gstwavpackcommon.h"

static GstFlowReturn gst_wavpack_enc_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_wavpack_enc_sink_set_caps (GstPad * pad, GstCaps * caps);
static int gst_wavpack_enc_push_block (void *id, void *data, int32_t count);
static gboolean gst_wavpack_enc_sink_event (GstPad * pad, GstEvent * event);
static GstStateChangeReturn gst_wavpack_enc_change_state (GstElement * element,
    GstStateChange transition);
static void gst_wavpack_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_wavpack_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

enum
{
  ARG_0,
  ARG_MODE,
  ARG_BITRATE,
  ARG_BITSPERSAMPLE,
  ARG_CORRECTION_MODE,
  ARG_MD5,
  ARG_EXTRA_PROCESSING,
  ARG_JOINT_STEREO_MODE
};

GST_DEBUG_CATEGORY_STATIC (gst_wavpack_enc_debug);
#define GST_CAT_DEFAULT gst_wavpack_enc_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 32, "
        "depth = (int) [ 1, 32], "
        "endianness = (int) BYTE_ORDER, "
        "channels = (int) [ 1, 8 ], "
        "rate = (int) [ 6000, 192000 ]," "signed = (boolean) TRUE")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "width = (int) [ 1, 32 ], "
        "channels = (int) [ 1, 2 ], "
        "rate = (int) [ 6000, 192000 ], " "framed = (boolean) TRUE")
    );

static GstStaticPadTemplate wvcsrc_factory = GST_STATIC_PAD_TEMPLATE ("wvcsrc",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-wavpack-correction, " "framed = (boolean) TRUE")
    );

enum
{
  GST_WAVPACK_ENC_MODE_VERY_FAST = 0,
  GST_WAVPACK_ENC_MODE_FAST,
  GST_WAVPACK_ENC_MODE_DEFAULT,
  GST_WAVPACK_ENC_MODE_HIGH,
  GST_WAVPACK_ENC_MODE_VERY_HIGH
};

#define GST_TYPE_WAVPACK_ENC_MODE (gst_wavpack_enc_mode_get_type ())
static GType
gst_wavpack_enc_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
#if 0
      /* Very Fast Compression is not supported yet, but will be supported
       * in future wavpack versions */
      {GST_WAVPACK_ENC_MODE_VERY_FAST, "Very Fast Compression", "veryfast"},
#endif
      {GST_WAVPACK_ENC_MODE_FAST, "Fast Compression", "fast"},
      {GST_WAVPACK_ENC_MODE_DEFAULT, "Normal Compression", "normal"},
      {GST_WAVPACK_ENC_MODE_HIGH, "High Compression", "high"},
#ifndef WAVPACK_OLD_API
      {GST_WAVPACK_ENC_MODE_VERY_HIGH, "Very High Compression", "veryhigh"},
#endif
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstWavpackEncMode", values);
  }
  return qtype;
}

enum
{
  GST_WAVPACK_CORRECTION_MODE_OFF = 0,
  GST_WAVPACK_CORRECTION_MODE_ON,
  GST_WAVPACK_CORRECTION_MODE_OPTIMIZED
};

#define GST_TYPE_WAVPACK_ENC_CORRECTION_MODE (gst_wavpack_enc_correction_mode_get_type ())
static GType
gst_wavpack_enc_correction_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {GST_WAVPACK_CORRECTION_MODE_OFF, "Create no correction file", "off"},
      {GST_WAVPACK_CORRECTION_MODE_ON, "Create correction file", "on"},
      {GST_WAVPACK_CORRECTION_MODE_OPTIMIZED,
          "Create optimized correction file", "optimized"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstWavpackEncCorrectionMode", values);
  }
  return qtype;
}

enum
{
  GST_WAVPACK_JS_MODE_AUTO = 0,
  GST_WAVPACK_JS_MODE_LEFT_RIGHT,
  GST_WAVPACK_JS_MODE_MID_SIDE
};

#define GST_TYPE_WAVPACK_ENC_JOINT_STEREO_MODE (gst_wavpack_enc_joint_stereo_mode_get_type ())
static GType
gst_wavpack_enc_joint_stereo_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {GST_WAVPACK_JS_MODE_AUTO, "auto", "auto"},
      {GST_WAVPACK_JS_MODE_LEFT_RIGHT, "left/right", "leftright"},
      {GST_WAVPACK_JS_MODE_MID_SIDE, "mid/side", "midside"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstWavpackEncJSMode", values);
  }
  return qtype;
}

static void
_do_init (GType object_type)
{
  const GInterfaceInfo preset_interface_info = {
    NULL,                       /* interface_init */
    NULL,                       /* interface_finalize */
    NULL                        /* interface_data */
  };

  g_type_add_interface_static (object_type, GST_TYPE_PRESET,
      &preset_interface_info);
}

GST_BOILERPLATE_FULL (GstWavpackEnc, gst_wavpack_enc, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_wavpack_enc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* add pad templates */
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class,
      &wvcsrc_factory);

  /* set element details */
  gst_element_class_set_details_simple (element_class, "Wavpack audio encoder",
      "Codec/Encoder/Audio",
      "Encodes audio with the Wavpack lossless/lossy audio codec",
      "Sebastian Dröge <slomo@circular-chaos.org>");
}


static void
gst_wavpack_enc_class_init (GstWavpackEncClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  /* set state change handler */
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_wavpack_enc_change_state);

  /* set property handlers */
  gobject_class->set_property = gst_wavpack_enc_set_property;
  gobject_class->get_property = gst_wavpack_enc_get_property;

  /* install all properties */
  g_object_class_install_property (gobject_class, ARG_MODE,
      g_param_spec_enum ("mode", "Encoding mode",
          "Speed versus compression tradeoff.",
          GST_TYPE_WAVPACK_ENC_MODE, GST_WAVPACK_ENC_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate",
          "Try to encode with this average bitrate (bits/sec). "
          "This enables lossy encoding, values smaller than 24000 disable it again.",
          0, 9600000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_BITSPERSAMPLE,
      g_param_spec_double ("bits-per-sample", "Bits per sample",
          "Try to encode with this amount of bits per sample. "
          "This enables lossy encoding, values smaller than 2.0 disable it again.",
          0.0, 24.0, 0.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_CORRECTION_MODE,
      g_param_spec_enum ("correction-mode", "Correction stream mode",
          "Use this mode for the correction stream. Only works in lossy mode!",
          GST_TYPE_WAVPACK_ENC_CORRECTION_MODE, GST_WAVPACK_CORRECTION_MODE_OFF,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_MD5,
      g_param_spec_boolean ("md5", "MD5",
          "Store MD5 hash of raw samples within the file.", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_EXTRA_PROCESSING,
      g_param_spec_uint ("extra-processing", "Extra processing",
          "Use better but slower filters for better compression/quality.",
          0, 6, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_JOINT_STEREO_MODE,
      g_param_spec_enum ("joint-stereo-mode", "Joint-Stereo mode",
          "Use this joint-stereo mode.", GST_TYPE_WAVPACK_ENC_JOINT_STEREO_MODE,
          GST_WAVPACK_JS_MODE_AUTO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_wavpack_enc_reset (GstWavpackEnc * enc)
{
  /* close and free everything stream related if we already did something */
  if (enc->wp_context) {
    WavpackCloseFile (enc->wp_context);
    enc->wp_context = NULL;
  }
  if (enc->wp_config) {
    g_free (enc->wp_config);
    enc->wp_config = NULL;
  }
  if (enc->first_block) {
    g_free (enc->first_block);
    enc->first_block = NULL;
  }
  enc->first_block_size = 0;
  if (enc->md5_context) {
    g_checksum_free (enc->md5_context);
    enc->md5_context = NULL;
  }

  if (enc->pending_buffer) {
    gst_buffer_unref (enc->pending_buffer);
    enc->pending_buffer = NULL;
    enc->pending_offset = 0;
  }

  /* reset the last returns to GST_FLOW_OK. This is only set to something else
   * while WavpackPackSamples() or more specific gst_wavpack_enc_push_block()
   * so not valid anymore */
  enc->srcpad_last_return = enc->wvcsrcpad_last_return = GST_FLOW_OK;

  /* reset stream information */
  enc->samplerate = 0;
  enc->depth = 0;
  enc->channels = 0;
  enc->channel_mask = 0;
  enc->need_channel_remap = FALSE;

  enc->timestamp_offset = GST_CLOCK_TIME_NONE;
  enc->next_ts = GST_CLOCK_TIME_NONE;
}

static void
gst_wavpack_enc_init (GstWavpackEnc * enc, GstWavpackEncClass * gclass)
{
  enc->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_enc_sink_set_caps));
  gst_pad_set_chain_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_enc_chain));
  gst_pad_set_event_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_enc_sink_event));
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);

  /* setup src pad */
  enc->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  /* initialize object attributes */
  enc->wp_config = NULL;
  enc->wp_context = NULL;
  enc->first_block = NULL;
  enc->md5_context = NULL;
  gst_wavpack_enc_reset (enc);

  enc->wv_id.correction = FALSE;
  enc->wv_id.wavpack_enc = enc;
  enc->wv_id.passthrough = FALSE;
  enc->wvc_id.correction = TRUE;
  enc->wvc_id.wavpack_enc = enc;
  enc->wvc_id.passthrough = FALSE;

  /* set default values of params */
  enc->mode = GST_WAVPACK_ENC_MODE_DEFAULT;
  enc->bitrate = 0;
  enc->bps = 0.0;
  enc->correction_mode = GST_WAVPACK_CORRECTION_MODE_OFF;
  enc->md5 = FALSE;
  enc->extra_processing = 0;
  enc->joint_stereo_mode = GST_WAVPACK_JS_MODE_AUTO;
}

static gboolean
gst_wavpack_enc_sink_set_caps (GstPad * pad, GstCaps * caps)
{
  GstWavpackEnc *enc = GST_WAVPACK_ENC (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstAudioChannelPosition *pos;

  if (!gst_structure_get_int (structure, "channels", &enc->channels) ||
      !gst_structure_get_int (structure, "rate", &enc->samplerate) ||
      !gst_structure_get_int (structure, "depth", &enc->depth)) {
    GST_ELEMENT_ERROR (enc, LIBRARY, INIT, (NULL),
        ("got invalid caps: %" GST_PTR_FORMAT, caps));
    gst_object_unref (enc);
    return FALSE;
  }

  pos = gst_audio_get_channel_positions (structure);
  /* If one channel is NONE they'll be all undefined */
  if (pos != NULL && pos[0] == GST_AUDIO_CHANNEL_POSITION_NONE) {
    g_free (pos);
    pos = NULL;
  }

  if (pos == NULL) {
    GST_ELEMENT_ERROR (enc, STREAM, FORMAT, (NULL),
        ("input has no valid channel layout"));

    gst_object_unref (enc);
    return FALSE;
  }

  enc->channel_mask =
      gst_wavpack_get_channel_mask_from_positions (pos, enc->channels);
  enc->need_channel_remap =
      gst_wavpack_set_channel_mapping (pos, enc->channels,
      enc->channel_mapping);
  g_free (pos);

  /* set fixed src pad caps now that we know what we will get */
  caps = gst_caps_new_simple ("audio/x-wavpack",
      "channels", G_TYPE_INT, enc->channels,
      "rate", G_TYPE_INT, enc->samplerate,
      "width", G_TYPE_INT, enc->depth, "framed", G_TYPE_BOOLEAN, TRUE, NULL);

  if (!gst_wavpack_set_channel_layout (caps, enc->channel_mask))
    GST_WARNING_OBJECT (enc, "setting channel layout failed");

  if (!gst_pad_set_caps (enc->srcpad, caps)) {
    GST_ELEMENT_ERROR (enc, LIBRARY, INIT, (NULL),
        ("setting caps failed: %" GST_PTR_FORMAT, caps));
    gst_caps_unref (caps);
    gst_object_unref (enc);
    return FALSE;
  }
  gst_pad_use_fixed_caps (enc->srcpad);

  gst_caps_unref (caps);
  gst_object_unref (enc);
  return TRUE;
}

static void
gst_wavpack_enc_set_wp_config (GstWavpackEnc * enc)
{
  enc->wp_config = g_new0 (WavpackConfig, 1);
  /* set general stream informations in the WavpackConfig */
  enc->wp_config->bytes_per_sample = GST_ROUND_UP_8 (enc->depth) / 8;
  enc->wp_config->bits_per_sample = enc->depth;
  enc->wp_config->num_channels = enc->channels;
  enc->wp_config->channel_mask = enc->channel_mask;
  enc->wp_config->sample_rate = enc->samplerate;

  /*
   * Set parameters in WavpackConfig
   */

  /* Encoding mode */
  switch (enc->mode) {
#if 0
    case GST_WAVPACK_ENC_MODE_VERY_FAST:
      enc->wp_config->flags |= CONFIG_VERY_FAST_FLAG;
      enc->wp_config->flags |= CONFIG_FAST_FLAG;
      break;
#endif
    case GST_WAVPACK_ENC_MODE_FAST:
      enc->wp_config->flags |= CONFIG_FAST_FLAG;
      break;
    case GST_WAVPACK_ENC_MODE_DEFAULT:
      break;
    case GST_WAVPACK_ENC_MODE_HIGH:
      enc->wp_config->flags |= CONFIG_HIGH_FLAG;
      break;
#ifndef WAVPACK_OLD_API
    case GST_WAVPACK_ENC_MODE_VERY_HIGH:
      enc->wp_config->flags |= CONFIG_HIGH_FLAG;
      enc->wp_config->flags |= CONFIG_VERY_HIGH_FLAG;
      break;
#endif
  }

  /* Bitrate, enables lossy mode */
  if (enc->bitrate) {
    enc->wp_config->flags |= CONFIG_HYBRID_FLAG;
    enc->wp_config->flags |= CONFIG_BITRATE_KBPS;
    enc->wp_config->bitrate = enc->bitrate / 1000.0;
  } else if (enc->bps) {
    enc->wp_config->flags |= CONFIG_HYBRID_FLAG;
    enc->wp_config->bitrate = enc->bps;
  }

  /* Correction Mode, only in lossy mode */
  if (enc->wp_config->flags & CONFIG_HYBRID_FLAG) {
    if (enc->correction_mode > GST_WAVPACK_CORRECTION_MODE_OFF) {
      GstCaps *caps = gst_caps_new_simple ("audio/x-wavpack-correction",
          "framed", G_TYPE_BOOLEAN, TRUE, NULL);

      enc->wvcsrcpad =
          gst_pad_new_from_static_template (&wvcsrc_factory, "wvcsrc");

      /* try to add correction src pad, don't set correction mode on failure */
      GST_DEBUG_OBJECT (enc, "Adding correction pad with caps %"
          GST_PTR_FORMAT, caps);
      if (!gst_pad_set_caps (enc->wvcsrcpad, caps)) {
        enc->correction_mode = 0;
        GST_WARNING_OBJECT (enc, "setting correction caps failed");
      } else {
        gst_pad_use_fixed_caps (enc->wvcsrcpad);
        gst_pad_set_active (enc->wvcsrcpad, TRUE);
        gst_element_add_pad (GST_ELEMENT (enc), enc->wvcsrcpad);
        enc->wp_config->flags |= CONFIG_CREATE_WVC;
        if (enc->correction_mode == GST_WAVPACK_CORRECTION_MODE_OPTIMIZED) {
          enc->wp_config->flags |= CONFIG_OPTIMIZE_WVC;
        }
      }
      gst_caps_unref (caps);
    }
  } else {
    if (enc->correction_mode > GST_WAVPACK_CORRECTION_MODE_OFF) {
      enc->correction_mode = 0;
      GST_WARNING_OBJECT (enc, "setting correction mode only has "
          "any effect if a bitrate is provided.");
    }
  }
  gst_element_no_more_pads (GST_ELEMENT (enc));

  /* MD5, setup MD5 context */
  if ((enc->md5) && !(enc->md5_context)) {
    enc->wp_config->flags |= CONFIG_MD5_CHECKSUM;
    enc->md5_context = g_checksum_new (G_CHECKSUM_MD5);
  }

  /* Extra encode processing */
  if (enc->extra_processing) {
    enc->wp_config->flags |= CONFIG_EXTRA_MODE;
    enc->wp_config->xmode = enc->extra_processing;
  }

  /* Joint stereo mode */
  switch (enc->joint_stereo_mode) {
    case GST_WAVPACK_JS_MODE_AUTO:
      break;
    case GST_WAVPACK_JS_MODE_LEFT_RIGHT:
      enc->wp_config->flags |= CONFIG_JOINT_OVERRIDE;
      enc->wp_config->flags &= ~CONFIG_JOINT_STEREO;
      break;
    case GST_WAVPACK_JS_MODE_MID_SIDE:
      enc->wp_config->flags |= (CONFIG_JOINT_OVERRIDE | CONFIG_JOINT_STEREO);
      break;
  }
}

static int
gst_wavpack_enc_push_block (void *id, void *data, int32_t count)
{
  GstWavpackEncWriteID *wid = (GstWavpackEncWriteID *) id;
  GstWavpackEnc *enc = GST_WAVPACK_ENC (wid->wavpack_enc);
  GstFlowReturn *flow;
  GstBuffer *buffer;
  GstPad *pad;
  guchar *block = (guchar *) data;

  pad = (wid->correction) ? enc->wvcsrcpad : enc->srcpad;
  flow =
      (wid->correction) ? &enc->wvcsrcpad_last_return : &enc->
      srcpad_last_return;

  *flow = gst_pad_alloc_buffer_and_set_caps (pad, GST_BUFFER_OFFSET_NONE,
      count, GST_PAD_CAPS (pad), &buffer);

  if (*flow != GST_FLOW_OK) {
    GST_WARNING_OBJECT (enc, "flow on %s:%s = %s",
        GST_DEBUG_PAD_NAME (pad), gst_flow_get_name (*flow));
    return FALSE;
  }

  g_memmove (GST_BUFFER_DATA (buffer), block, count);

  if (count > sizeof (WavpackHeader) && memcmp (block, "wvpk", 4) == 0) {
    /* if it's a Wavpack block set buffer timestamp and duration, etc */
    WavpackHeader wph;

    GST_LOG_OBJECT (enc, "got %d bytes of encoded wavpack %sdata",
        count, (wid->correction) ? "correction " : "");

    gst_wavpack_read_header (&wph, block);

    /* Only set when pushing the first buffer again, in that case
     * we don't want to delay the buffer or push newsegment events
     */
    if (!wid->passthrough) {
      /* Only push complete blocks */
      if (enc->pending_buffer == NULL) {
        enc->pending_buffer = buffer;
        enc->pending_offset = wph.block_index;
      } else if (enc->pending_offset == wph.block_index) {
        enc->pending_buffer = gst_buffer_join (enc->pending_buffer, buffer);
      } else {
        GST_ERROR ("Got incomplete block, dropping");
        gst_buffer_unref (enc->pending_buffer);
        enc->pending_buffer = buffer;
        enc->pending_offset = wph.block_index;
      }

      if (!(wph.flags & FINAL_BLOCK))
        return TRUE;

      buffer = enc->pending_buffer;
      enc->pending_buffer = NULL;
      enc->pending_offset = 0;

      /* if it's the first wavpack block, send a NEW_SEGMENT event */
      if (wph.block_index == 0) {
        gst_pad_push_event (pad,
            gst_event_new_new_segment (FALSE,
                1.0, GST_FORMAT_TIME, 0, GST_BUFFER_OFFSET_NONE, 0));

        /* save header for later reference, so we can re-send it later on
         * EOS with fixed up values for total sample count etc. */
        if (enc->first_block == NULL && !wid->correction) {
          enc->first_block =
              g_memdup (GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));
          enc->first_block_size = GST_BUFFER_SIZE (buffer);
        }
      }
    }

    /* set buffer timestamp, duration, offset, offset_end from
     * the wavpack header */
    GST_BUFFER_TIMESTAMP (buffer) = enc->timestamp_offset +
        gst_util_uint64_scale_int (GST_SECOND, wph.block_index,
        enc->samplerate);
    GST_BUFFER_DURATION (buffer) =
        gst_util_uint64_scale_int (GST_SECOND, wph.block_samples,
        enc->samplerate);
    GST_BUFFER_OFFSET (buffer) = wph.block_index;
    GST_BUFFER_OFFSET_END (buffer) = wph.block_index + wph.block_samples;
  } else {
    /* if it's something else set no timestamp and duration on the buffer */
    GST_DEBUG_OBJECT (enc, "got %d bytes of unknown data", count);

    GST_BUFFER_TIMESTAMP (buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  }

  /* push the buffer and forward errors */
  GST_DEBUG_OBJECT (enc, "pushing buffer with %d bytes",
      GST_BUFFER_SIZE (buffer));
  *flow = gst_pad_push (pad, buffer);

  if (*flow != GST_FLOW_OK) {
    GST_WARNING_OBJECT (enc, "flow on %s:%s = %s",
        GST_DEBUG_PAD_NAME (pad), gst_flow_get_name (*flow));
    return FALSE;
  }

  return TRUE;
}

static void
gst_wavpack_enc_fix_channel_order (GstWavpackEnc * enc, gint32 * data,
    gint nsamples)
{
  gint i, j;
  gint32 tmp[8];

  for (i = 0; i < nsamples / enc->channels; i++) {
    for (j = 0; j < enc->channels; j++) {
      tmp[enc->channel_mapping[j]] = data[j];
    }
    for (j = 0; j < enc->channels; j++) {
      data[j] = tmp[j];
    }
    data += enc->channels;
  }
}

static GstFlowReturn
gst_wavpack_enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstWavpackEnc *enc = GST_WAVPACK_ENC (gst_pad_get_parent (pad));
  uint32_t sample_count = GST_BUFFER_SIZE (buf) / 4;
  GstFlowReturn ret;

  /* reset the last returns to GST_FLOW_OK. This is only set to something else
   * while WavpackPackSamples() or more specific gst_wavpack_enc_push_block()
   * so not valid anymore */
  enc->srcpad_last_return = enc->wvcsrcpad_last_return = GST_FLOW_OK;

  GST_DEBUG ("got %u raw samples", sample_count);

  /* check if we already have a valid WavpackContext, otherwise make one */
  if (!enc->wp_context) {
    /* create raw context */
    enc->wp_context =
        WavpackOpenFileOutput (gst_wavpack_enc_push_block, &enc->wv_id,
        (enc->correction_mode > 0) ? &enc->wvc_id : NULL);
    if (!enc->wp_context) {
      GST_ELEMENT_ERROR (enc, LIBRARY, INIT, (NULL),
          ("error creating Wavpack context"));
      gst_object_unref (enc);
      gst_buffer_unref (buf);
      return GST_FLOW_ERROR;
    }

    /* set the WavpackConfig according to our parameters */
    gst_wavpack_enc_set_wp_config (enc);

    /* set the configuration to the context now that we know everything
     * and initialize the encoder */
    if (!WavpackSetConfiguration (enc->wp_context,
            enc->wp_config, (uint32_t) (-1))
        || !WavpackPackInit (enc->wp_context)) {
      GST_ELEMENT_ERROR (enc, LIBRARY, SETTINGS, (NULL),
          ("error setting up wavpack encoding context"));
      WavpackCloseFile (enc->wp_context);
      gst_object_unref (enc);
      gst_buffer_unref (buf);
      return GST_FLOW_ERROR;
    }
    GST_DEBUG ("setup of encoding context successfull");
  }

  /* Save the timestamp of the first buffer. This will be later
   * used as offset for all following buffers */
  if (enc->timestamp_offset == GST_CLOCK_TIME_NONE) {
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
      enc->timestamp_offset = GST_BUFFER_TIMESTAMP (buf);
      enc->next_ts = GST_BUFFER_TIMESTAMP (buf);
    } else {
      enc->timestamp_offset = 0;
      enc->next_ts = 0;
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
        GST_CLOCK_TIME_TO_FRAMES (diff, enc->samplerate) * enc->channels * 2;
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

  /* Allow a diff of at most 5 ms */
  if (enc->next_ts != GST_CLOCK_TIME_NONE
      && GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    if (GST_BUFFER_TIMESTAMP (buf) != enc->next_ts &&
        GST_BUFFER_TIMESTAMP (buf) - enc->next_ts > 5 * GST_MSECOND) {
      GST_WARNING_OBJECT (enc,
          "Discontinuity detected: %" G_GUINT64_FORMAT " > %" G_GUINT64_FORMAT,
          GST_BUFFER_TIMESTAMP (buf) - enc->next_ts, 5 * GST_MSECOND);

      WavpackFlushSamples (enc->wp_context);
      enc->timestamp_offset += (GST_BUFFER_TIMESTAMP (buf) - enc->next_ts);
    }
  }

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)
      && GST_BUFFER_DURATION_IS_VALID (buf))
    enc->next_ts = GST_BUFFER_TIMESTAMP (buf) + GST_BUFFER_DURATION (buf);
  else
    enc->next_ts = GST_CLOCK_TIME_NONE;

  if (enc->need_channel_remap) {
    buf = gst_buffer_make_writable (buf);
    gst_wavpack_enc_fix_channel_order (enc, (gint32 *) GST_BUFFER_DATA (buf),
        sample_count);
  }

  /* if we want to append the MD5 sum to the stream update it here
   * with the current raw samples */
  if (enc->md5) {
    g_checksum_update (enc->md5_context, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
  }

  /* encode and handle return values from encoding */
  if (WavpackPackSamples (enc->wp_context, (int32_t *) GST_BUFFER_DATA (buf),
          sample_count / enc->channels)) {
    GST_DEBUG ("encoding samples successful");
    ret = GST_FLOW_OK;
  } else {
    if ((enc->srcpad_last_return == GST_FLOW_RESEND) ||
        (enc->wvcsrcpad_last_return == GST_FLOW_RESEND)) {
      ret = GST_FLOW_RESEND;
    } else if ((enc->srcpad_last_return == GST_FLOW_OK) ||
        (enc->wvcsrcpad_last_return == GST_FLOW_OK)) {
      ret = GST_FLOW_OK;
    } else if ((enc->srcpad_last_return == GST_FLOW_NOT_LINKED) &&
        (enc->wvcsrcpad_last_return == GST_FLOW_NOT_LINKED)) {
      ret = GST_FLOW_NOT_LINKED;
    } else if ((enc->srcpad_last_return == GST_FLOW_WRONG_STATE) &&
        (enc->wvcsrcpad_last_return == GST_FLOW_WRONG_STATE)) {
      ret = GST_FLOW_WRONG_STATE;
    } else {
      GST_ELEMENT_ERROR (enc, LIBRARY, ENCODE, (NULL),
          ("encoding samples failed"));
      ret = GST_FLOW_ERROR;
    }
  }

  gst_buffer_unref (buf);
  gst_object_unref (enc);
  return ret;
}

static void
gst_wavpack_enc_rewrite_first_block (GstWavpackEnc * enc)
{
  GstEvent *event = gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_BYTES,
      0, GST_BUFFER_OFFSET_NONE, 0);
  gboolean ret;

  g_return_if_fail (enc);
  g_return_if_fail (enc->first_block);

  /* update the sample count in the first block */
  WavpackUpdateNumSamples (enc->wp_context, enc->first_block);

  /* try to seek to the beginning of the output */
  ret = gst_pad_push_event (enc->srcpad, event);
  if (ret) {
    /* try to rewrite the first block */
    GST_DEBUG_OBJECT (enc, "rewriting first block ...");
    enc->wv_id.passthrough = TRUE;
    ret = gst_wavpack_enc_push_block (&enc->wv_id,
        enc->first_block, enc->first_block_size);
    enc->wv_id.passthrough = FALSE;
  } else {
    GST_WARNING_OBJECT (enc, "rewriting of first block failed. "
        "Seeking to first block failed!");
  }
}

static gboolean
gst_wavpack_enc_sink_event (GstPad * pad, GstEvent * event)
{
  GstWavpackEnc *enc = GST_WAVPACK_ENC (gst_pad_get_parent (pad));
  gboolean ret = TRUE;

  GST_DEBUG ("Received %s event on sinkpad", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* Encode all remaining samples and flush them to the src pads */
      WavpackFlushSamples (enc->wp_context);

      /* Drop all remaining data, this is no complete block otherwise
       * it would've been pushed already */
      if (enc->pending_buffer) {
        gst_buffer_unref (enc->pending_buffer);
        enc->pending_buffer = NULL;
        enc->pending_offset = 0;
      }

      /* write the MD5 sum if we have to write one */
      if ((enc->md5) && (enc->md5_context)) {
        guint8 md5_digest[16];
        gsize digest_len = sizeof (md5_digest);

        g_checksum_get_digest (enc->md5_context, md5_digest, &digest_len);
        if (digest_len == sizeof (md5_digest))
          WavpackStoreMD5Sum (enc->wp_context, md5_digest);
        else
          GST_WARNING_OBJECT (enc, "Calculating MD5 digest failed");
      }

      /* Try to rewrite the first frame with the correct sample number */
      if (enc->first_block)
        gst_wavpack_enc_rewrite_first_block (enc);

      /* close the context if not already happened */
      if (enc->wp_context) {
        WavpackCloseFile (enc->wp_context);
        enc->wp_context = NULL;
      }

      ret = gst_pad_event_default (pad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
      if (enc->wp_context) {
        GST_WARNING_OBJECT (enc, "got NEWSEGMENT after encoding "
            "already started");
      }
      /* drop NEWSEGMENT events, we create our own when pushing
       * the first buffer to the pads */
      gst_event_unref (event);
      ret = TRUE;
      break;
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (enc);
  return ret;
}

static GstStateChangeReturn
gst_wavpack_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstWavpackEnc *enc = GST_WAVPACK_ENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* set the last returned GstFlowReturns of the two pads to GST_FLOW_OK
       * as they're only set to something else in WavpackPackSamples() or more
       * specific gst_wavpack_enc_push_block() and nothing happened there yet */
      enc->srcpad_last_return = enc->wvcsrcpad_last_return = GST_FLOW_OK;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_wavpack_enc_reset (enc);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_wavpack_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWavpackEnc *enc = GST_WAVPACK_ENC (object);

  switch (prop_id) {
    case ARG_MODE:
      enc->mode = g_value_get_enum (value);
      break;
    case ARG_BITRATE:{
      guint val = g_value_get_uint (value);

      if ((val >= 24000) && (val <= 9600000)) {
        enc->bitrate = val;
        enc->bps = 0.0;
      } else {
        enc->bitrate = 0;
        enc->bps = 0.0;
      }
      break;
    }
    case ARG_BITSPERSAMPLE:{
      gdouble val = g_value_get_double (value);

      if ((val >= 2.0) && (val <= 24.0)) {
        enc->bps = val;
        enc->bitrate = 0;
      } else {
        enc->bps = 0.0;
        enc->bitrate = 0;
      }
      break;
    }
    case ARG_CORRECTION_MODE:
      enc->correction_mode = g_value_get_enum (value);
      break;
    case ARG_MD5:
      enc->md5 = g_value_get_boolean (value);
      break;
    case ARG_EXTRA_PROCESSING:
      enc->extra_processing = g_value_get_uint (value);
      break;
    case ARG_JOINT_STEREO_MODE:
      enc->joint_stereo_mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_wavpack_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstWavpackEnc *enc = GST_WAVPACK_ENC (object);

  switch (prop_id) {
    case ARG_MODE:
      g_value_set_enum (value, enc->mode);
      break;
    case ARG_BITRATE:
      if (enc->bps == 0.0) {
        g_value_set_uint (value, enc->bitrate);
      } else {
        g_value_set_uint (value, 0);
      }
      break;
    case ARG_BITSPERSAMPLE:
      if (enc->bitrate == 0) {
        g_value_set_double (value, enc->bps);
      } else {
        g_value_set_double (value, 0.0);
      }
      break;
    case ARG_CORRECTION_MODE:
      g_value_set_enum (value, enc->correction_mode);
      break;
    case ARG_MD5:
      g_value_set_boolean (value, enc->md5);
      break;
    case ARG_EXTRA_PROCESSING:
      g_value_set_uint (value, enc->extra_processing);
      break;
    case ARG_JOINT_STEREO_MODE:
      g_value_set_enum (value, enc->joint_stereo_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_wavpack_enc_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "wavpackenc",
          GST_RANK_NONE, GST_TYPE_WAVPACK_ENC))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_wavpack_enc_debug, "wavpack_enc", 0,
      "Wavpack encoder");

  return TRUE;
}
