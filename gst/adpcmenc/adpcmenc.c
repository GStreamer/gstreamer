/* GStreamer
 * Copyright (C) 2009 Pioneers of the Inevitable <songbird@songbirdnest.com>
 *
 * Authors: Peter van Hardenberg <pvh@songbirdnest.com>
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

/* Based on ADPCM encoders in libsndfile, 
   Copyright (C) 1999-2002 Erik de Castro Lopo <erikd@zip.com.au
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#define GST_TYPE_ADPCM_ENC \
    (adpcmenc_get_type ())

#define GST_TYPE_ADPCMENC_LAYOUT \
    (adpcmenc_layout_get_type ())

#define GST_ADPCM_ENC(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_ADPCM_ENC, ADPCMEnc))

#define GST_CAT_DEFAULT adpcmenc_debug
GST_DEBUG_CATEGORY_STATIC (adpcmenc_debug);

static GstStaticPadTemplate adpcmenc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "depth = (int)16, "
        "width = (int)16, " "channels = (int) [1,2], " "rate = (int)[1, MAX]")
    );

static GstStaticPadTemplate adpcmenc_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-adpcm, "
        " layout=(string){dvi}, "
        " block_align = (int) [64, 8192], "
        " rate = (int)[ 1, MAX ], " "channels = (int)[1,2];")
    );

#define MIN_ADPCM_BLOCK_SIZE 64
#define MAX_ADPCM_BLOCK_SIZE 8192
#define DEFAULT_ADPCM_BLOCK_SIZE 1024
#define DEFAULT_ADPCM_LAYOUT LAYOUT_ADPCM_DVI

static const int ima_indx_adjust[16] = {
  -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

static const int ima_step_size[89] = {
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
  50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
  253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
  1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
  3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
  11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
  32767
};


enum adpcm_properties
{
  ARG_0,
  ARG_BLOCK_SIZE,
  ARG_LAYOUT
};

enum adpcm_layout
{
  LAYOUT_ADPCM_DVI
};

static GType
adpcmenc_layout_get_type (void)
{
  static GType adpcmenc_layout_type = 0;

  if (!adpcmenc_layout_type) {
    static GEnumValue layout_types[] = {
      {LAYOUT_ADPCM_DVI, "DVI/IMA APDCM", "dvi"},
      {0, NULL, NULL},
    };

    adpcmenc_layout_type = g_enum_register_static ("GstADPCMEncLayout",
        layout_types);
  }

  return adpcmenc_layout_type;
}

typedef struct _ADPCMEncClass
{
  GstElementClass parent_class;
} ADPCMEncClass;

typedef struct _ADPCMEnc
{
  GstElement parent;

  GstPad *sinkpad;
  GstPad *srcpad;

  GstCaps *output_caps;

  enum adpcm_layout layout;
  int rate;
  int channels;
  int blocksize;
  int samples_per_block;

  guint8 step_index[2];

  gboolean is_setup;

  GstClockTime timestamp;
  GstClockTime base_timestamp;

  guint64 out_samples;

  GstAdapter *adapter;

} ADPCMEnc;

GType adpcmenc_get_type (void);
GST_BOILERPLATE (ADPCMEnc, adpcmenc, GstElement, GST_TYPE_ELEMENT);
static gboolean
adpcmenc_setup (ADPCMEnc * enc)
{
  const int DVI_IMA_HEADER_SIZE = 4;
  const int ADPCM_SAMPLES_PER_BYTE = 2;
  guint64 sample_bytes;
  const char *layout;

  switch (enc->layout) {
    case LAYOUT_ADPCM_DVI:
      layout = "dvi";
      /* IMA ADPCM includes a 4-byte header per channel, */
      sample_bytes = enc->blocksize - (DVI_IMA_HEADER_SIZE * enc->channels);
      /* two samples per byte, plus a single sample in the header. */
      enc->samples_per_block =
          ((sample_bytes * ADPCM_SAMPLES_PER_BYTE) / enc->channels) + 1;
      break;
    default:
      GST_WARNING_OBJECT (enc, "Invalid layout");
      return FALSE;
  }

  enc->output_caps = gst_caps_new_simple ("audio/x-adpcm",
      "rate", G_TYPE_INT, enc->rate,
      "channels", G_TYPE_INT, enc->channels,
      "layout", G_TYPE_STRING, layout,
      "block_align", G_TYPE_INT, enc->blocksize, NULL);

  if (enc->output_caps) {
    gst_pad_set_caps (enc->srcpad, enc->output_caps);
  }

  enc->is_setup = TRUE;
  enc->timestamp = GST_CLOCK_TIME_NONE;
  enc->base_timestamp = GST_CLOCK_TIME_NONE;
  enc->adapter = gst_adapter_new ();
  enc->out_samples = 0;

  /* Step index state is carried between blocks. */
  enc->step_index[0] = 0;
  enc->step_index[1] = 0;

  return TRUE;
}

static void
adpcmenc_teardown (ADPCMEnc * enc)
{
  if (enc->output_caps) {
    gst_caps_unref (enc->output_caps);
    enc->output_caps = NULL;
  }
  if (enc->adapter) {
    g_object_unref (enc->adapter);
    enc->adapter = NULL;
  }
  enc->is_setup = FALSE;
}

static gboolean
adpcmenc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  ADPCMEnc *enc = (ADPCMEnc *) gst_pad_get_parent (pad);
  GstStructure *structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rate", &enc->rate))
    return FALSE;
  if (!gst_structure_get_int (structure, "channels", &enc->channels))
    return FALSE;

  if (enc->is_setup) {
    adpcmenc_teardown (enc);
  }
  adpcmenc_setup (enc);

  gst_object_unref (enc);

  return TRUE;
}

static void
adpcmenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  ADPCMEnc *enc = GST_ADPCM_ENC (object);

  switch (prop_id) {
    case ARG_BLOCK_SIZE:
      enc->blocksize = g_value_get_int (value);
      break;
    case ARG_LAYOUT:
      enc->layout = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
adpcmenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  ADPCMEnc *enc = GST_ADPCM_ENC (object);

  switch (prop_id) {
    case ARG_BLOCK_SIZE:
      g_value_set_int (value, enc->blocksize);
      break;
    case ARG_LAYOUT:
      g_value_set_enum (value, enc->layout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static guint8
adpcmenc_encode_ima_sample (gint16 sample, gint16 * prev_sample,
    guint8 * stepindex)
{
  const int NEGATIVE_SIGN_BIT = 0x8;
  int diff, vpdiff, mask, step;
  int bytecode = 0x0;
  diff = sample - *prev_sample;
  step = ima_step_size[*stepindex];
  vpdiff = step >> 3;

  if (diff < 0) {
    diff = -diff;
    bytecode = NEGATIVE_SIGN_BIT;
  }

  mask = 0x4;
  while (mask > 0) {
    if (diff >= step) {
      bytecode |= mask;
      diff -= step;
      vpdiff += step;
    }
    step >>= 1;
    mask >>= 1;
  }

  if (bytecode & 8) {
    vpdiff = -vpdiff;
  }

  *prev_sample = CLAMP (*prev_sample + vpdiff, G_MININT16, G_MAXINT16);
  *stepindex = CLAMP (*stepindex + ima_indx_adjust[bytecode], 0, 88);

  return bytecode;
}

static gboolean
adpcmenc_encode_ima_block (ADPCMEnc * enc, const gint16 * samples,
    guint8 * outbuf)
{
  const int HEADER_SIZE = 4;
  gint16 prev_sample[2] = { 0, 0 };
  guint32 write_pos = 0;
  guint32 read_pos = 0;
  guint8 channel = 0;

  /* Write a header for each channel.
   * The header consists of a sixteen-bit predicted sound value,
   * and an eight bit step_index, carried forward from any previous block.
   * These allow seeking within the file.
   */
  for (channel = 0; channel < enc->channels; channel++) {
    write_pos = channel * HEADER_SIZE;
    outbuf[write_pos + 0] = (samples[channel] & 0xFF);
    outbuf[write_pos + 1] = (samples[channel] >> 8) & 0xFF;
    outbuf[write_pos + 2] = enc->step_index[channel];
    outbuf[write_pos + 3] = 0;
    prev_sample[channel] = samples[channel];
  }

  /* raw-audio looks like this for a stereo stream:
   * [ L, R, L, R, L, R ... ]
   * encoded audio is in eight-sample blocks, two samples to a byte thusly:
   * [ LL, LL, LL, LL, RR, RR, RR, RR ... ] 
   */
  write_pos = HEADER_SIZE * enc->channels;
  read_pos = enc->channels;     /* the first sample is in the header. */
  while (write_pos < enc->blocksize) {
    gint8 CHANNEL_CHUNK_SIZE = 8;
    for (channel = 0; channel < enc->channels; channel++) {
      /* convert eight samples (four bytes) per channel, then swap */
      guint32 channel_chunk_base = read_pos + channel;
      gint8 chunk;
      for (chunk = 0; chunk < CHANNEL_CHUNK_SIZE; chunk++) {
        guint8 packed_byte = 0, encoded_sample;
        encoded_sample =
            adpcmenc_encode_ima_sample (samples[channel_chunk_base +
                (chunk * enc->channels)], &prev_sample[channel],
            &enc->step_index[channel]);
        packed_byte |= encoded_sample & 0x0F;

        chunk++;

        encoded_sample =
            adpcmenc_encode_ima_sample (samples[channel_chunk_base +
                (chunk * enc->channels)], &prev_sample[channel],
            &enc->step_index[channel]);
        packed_byte |= encoded_sample << 4 & 0xF0;

        outbuf[write_pos++] = packed_byte;
      }
    }
    /* advance to the next block of 8 samples per channel */
    read_pos += CHANNEL_CHUNK_SIZE * enc->channels;
    if (read_pos > enc->samples_per_block * enc->channels) {
      GST_LOG ("Ran past the end. (Reading %i of %i.)", read_pos,
          enc->samples_per_block);
    }
  }

  return TRUE;
}

static GstFlowReturn
adpcmenc_encode_block (ADPCMEnc * enc, const gint16 * samples, int blocksize)
{
  gboolean res;
  GstBuffer *outbuf = NULL;

  if (enc->layout == LAYOUT_ADPCM_DVI) {
    outbuf = gst_buffer_new_and_alloc (enc->blocksize);
    res = adpcmenc_encode_ima_block (enc, samples, GST_BUFFER_DATA (outbuf));
  } else {
    GST_WARNING_OBJECT (enc, "Unknown layout");
    return GST_FLOW_ERROR;
  }

  if (!res) {
    gst_buffer_unref (outbuf);
    GST_WARNING_OBJECT (enc, "Encode of block failed");
    return GST_FLOW_ERROR;
  }

  gst_buffer_set_caps (outbuf, enc->output_caps);
  GST_BUFFER_TIMESTAMP (outbuf) = enc->timestamp;

  enc->out_samples += enc->samples_per_block;
  enc->timestamp = enc->base_timestamp +
      gst_util_uint64_scale_int (enc->out_samples, GST_SECOND, enc->rate);
  GST_BUFFER_DURATION (outbuf) = enc->timestamp - GST_BUFFER_TIMESTAMP (outbuf);

  return gst_pad_push (enc->srcpad, outbuf);
}

static GstFlowReturn
adpcmenc_chain (GstPad * pad, GstBuffer * buf)
{
  ADPCMEnc *enc = (ADPCMEnc *) gst_pad_get_parent (pad);
  GstFlowReturn ret = GST_FLOW_OK;
  gint16 *samples;
  GstBuffer *databuf = NULL;
  int input_bytes_per_block;
  const int BYTES_PER_SAMPLE = 2;

  if (enc->base_timestamp == GST_CLOCK_TIME_NONE) {
    enc->base_timestamp = GST_BUFFER_TIMESTAMP (buf);
    if (enc->base_timestamp == GST_CLOCK_TIME_NONE)
      enc->base_timestamp = 0;
    enc->timestamp = enc->base_timestamp;
  }

  gst_adapter_push (enc->adapter, buf);

  input_bytes_per_block =
      enc->samples_per_block * BYTES_PER_SAMPLE * enc->channels;

  while (gst_adapter_available (enc->adapter) >= input_bytes_per_block) {
    databuf = gst_adapter_take_buffer (enc->adapter, input_bytes_per_block);
    samples = (gint16 *) GST_BUFFER_DATA (databuf);
    ret = adpcmenc_encode_block (enc, samples, enc->blocksize);
    gst_buffer_unref (databuf);
    if (ret != GST_FLOW_OK)
      goto done;
  }

done:
  gst_object_unref (enc);
  return ret;
}

static gboolean
adpcmenc_sink_event (GstPad * pad, GstEvent * event)
{
  ADPCMEnc *enc = (ADPCMEnc *) gst_pad_get_parent (pad);
  gboolean res;
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      enc->out_samples = 0;
      enc->timestamp = GST_CLOCK_TIME_NONE;
      enc->base_timestamp = GST_CLOCK_TIME_NONE;
      gst_adapter_clear (enc->adapter);
      /* Fall through */
    default:
      res = gst_pad_push_event (enc->srcpad, event);
      break;
  }
  gst_object_unref (enc);
  return res;
}

static GstStateChangeReturn
adpcmenc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  ADPCMEnc *enc = (ADPCMEnc *) element;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      adpcmenc_teardown (enc);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

static void
adpcmenc_dispose (GObject * obj)
{
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
adpcmenc_init (ADPCMEnc * enc, ADPCMEncClass * klass)
{
  enc->sinkpad =
      gst_pad_new_from_static_template (&adpcmenc_sink_template, "sink");
  gst_pad_set_setcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (adpcmenc_sink_setcaps));
  gst_pad_set_chain_function (enc->sinkpad, GST_DEBUG_FUNCPTR (adpcmenc_chain));
  gst_pad_set_event_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (adpcmenc_sink_event));
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);

  enc->srcpad =
      gst_pad_new_from_static_template (&adpcmenc_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  /* Set defaults. */
  enc->blocksize = DEFAULT_ADPCM_BLOCK_SIZE;
  enc->layout = DEFAULT_ADPCM_LAYOUT;
}

static void
adpcmenc_class_init (ADPCMEncClass * klass)
{
  GObjectClass *gobjectclass = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobjectclass->set_property = adpcmenc_set_property;
  gobjectclass->get_property = adpcmenc_get_property;

  g_object_class_install_property (gobjectclass, ARG_LAYOUT,
      g_param_spec_enum ("layout", "Layout",
          "Layout for output stream",
          GST_TYPE_ADPCMENC_LAYOUT, DEFAULT_ADPCM_LAYOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobjectclass, ARG_BLOCK_SIZE,
      g_param_spec_int ("blockalign", "Block Align",
          "Block size for output stream",
          MIN_ADPCM_BLOCK_SIZE, MAX_ADPCM_BLOCK_SIZE,
          DEFAULT_ADPCM_BLOCK_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobjectclass->dispose = adpcmenc_dispose;
  gstelement_class->change_state = adpcmenc_change_state;
} static void

adpcmenc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gst_element_class_add_static_pad_template (element_class,
      &adpcmenc_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &adpcmenc_src_template);
  gst_element_class_set_details_simple (element_class, "ADPCM encoder",
      "Codec/Encoder/Audio",
      "Encode ADPCM audio",
      "Pioneers of the Inevitable <songbird@songbirdnest.com>");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (adpcmenc_debug, "adpcmenc", 0, "ADPCM Encoders");
  if (!gst_element_register (plugin, "adpcmenc", GST_RANK_PRIMARY,
          GST_TYPE_ADPCM_ENC)) {
    return FALSE;
  }
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, "adpcmenc",
    "ADPCM encoder", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);
