/* GStreamer
 * Copyright (C) 2009 Pioneers of the Inevitable <songbird@songbirdnest.com>
 *
 * Authors: Michael Smith <msmith@songbirdnest.com>
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

/* Based on ADPCM decoders in libsndfile, 
   Copyright (C) 1999-2002 Erik de Castro Lopo <erikd@zip.com.au
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#define GST_TYPE_ADPCM_DEC \
    (adpcmdec_get_type ())

#define GST_ADPCM_DEC(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_ADPCM_DEC, ADPCMDec))

#define GST_CAT_DEFAULT adpcmdec_debug
GST_DEBUG_CATEGORY_STATIC (adpcmdec_debug);

static const GstElementDetails adpcmdec_details =
GST_ELEMENT_DETAILS ("ADPCM decoder",
    "Codec/Decoder/Audio",
    "Decode MS and IMA ADPCM audio",
    "Pioneers of the Inevitable <songbird@songbirdnest.com");

static GstStaticPadTemplate adpcmdec_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-adpcm, "
        " layout=(string){microsoft, dvi}, "
        " block_align = (int) [64, 8096], "
        " rate = (int)[ 1, MAX ], " "channels = (int)[1,2];")
    );

static GstStaticPadTemplate adpcmdec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "depth = (int)16, "
        "width = (int)16, "
        "endianness = (int)" G_STRINGIFY (G_BYTE_ORDER) ", "
        "signed = (boolean)TRUE, "
        "channels = (int) [1,2], " "rate = (int)[1, MAX]")
    );

enum adpcm_layout
{
  LAYOUT_ADPCM_MICROSOFT,
  LAYOUT_ADPCM_DVI
};

typedef struct _ADPCMDecClass
{
  GstElementClass parent_class;
} ADPCMDecClass;

typedef struct _ADPCMDec
{
  GstElement parent;

  GstPad *sinkpad;
  GstPad *srcpad;

  GstCaps *output_caps;

  enum adpcm_layout layout;
  int rate;
  int channels;
  int blocksize;

  gboolean is_setup;

  GstClockTime timestamp;

  guint64 out_samples;

  GstAdapter *adapter;

} ADPCMDec;

GST_BOILERPLATE (ADPCMDec, adpcmdec, GstElement, GST_TYPE_ELEMENT);
static gboolean
adpcmdec_setup (ADPCMDec * dec)
{
  dec->output_caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, dec->rate,
      "channels", G_TYPE_INT, dec->channels,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE, NULL);

  if (dec->output_caps) {
    gst_pad_set_caps (dec->srcpad, dec->output_caps);
  }

  dec->is_setup = TRUE;
  dec->timestamp = GST_CLOCK_TIME_NONE;
  dec->adapter = gst_adapter_new ();
  dec->out_samples = 0;

  return gst_pad_push_event (dec->srcpad, gst_event_new_new_segment (FALSE,
          1.0, GST_FORMAT_TIME, 0, -1, 0));
}

static void
adpcmdec_teardown (ADPCMDec * dec)
{
  if (dec->output_caps) {
    gst_caps_unref (dec->output_caps);
    dec->output_caps = NULL;
  }
  if (dec->adapter) {
    g_object_unref (dec->adapter);
    dec->adapter = NULL;
  }
  dec->is_setup = FALSE;
}

static gboolean
adpcmdec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  ADPCMDec *dec = (ADPCMDec *) gst_pad_get_parent (pad);
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  const gchar *layout;

  layout = gst_structure_get_string (structure, "layout");
  if (!layout)
    return FALSE;

  if (g_str_equal (layout, "microsoft"))
    dec->layout = LAYOUT_ADPCM_MICROSOFT;
  else if (g_str_equal (layout, "dvi"))
    dec->layout = LAYOUT_ADPCM_DVI;
  else
    return FALSE;

  if (!gst_structure_get_int (structure, "block_align", &dec->blocksize))
    return FALSE;
  if (!gst_structure_get_int (structure, "rate", &dec->rate))
    return FALSE;
  if (!gst_structure_get_int (structure, "channels", &dec->channels))
    return FALSE;

  if (dec->is_setup)
    adpcmdec_teardown (dec);
  gst_object_unref (dec);

  return TRUE;
}


/*=====================================================================
 * From libsndfile:
 *
 *  MS ADPCM Block Layout.
 *  ======================
 *  Block is usually 256, 512 or 1024 bytes depending on sample rate.
 *  For a mono file, the block is laid out as follows:
 *      byte    purpose
 *      0       block predictor [0..6]
 *      1,2     initial idelta (positive)
 *      3,4     sample 1
 *      5,6     sample 0
 *      7..n    packed bytecodes
 *
 *  For a stereo file, the block is laid out as follows:
 *      byte    purpose
 *      0       block predictor [0..6] for left channel
 *      1       block predictor [0..6] for right channel
 *      2,3     initial idelta (positive) for left channel
 *      4,5     initial idelta (positive) for right channel
 *      6,7     sample 1 for left channel
 *      8,9     sample 1 for right channel
 *      10,11   sample 0 for left channel
 *      12,13   sample 0 for right channel
 *      14..n   packed bytecodes
 *
 *=====================================================================
*/
static int AdaptationTable[] = {
  230, 230, 230, 230, 307, 409, 512, 614,
  768, 614, 512, 409, 307, 230, 230, 230
};

static int AdaptCoeff1[] = {
  256, 512, 0, 192, 240, 460, 392
};

static int AdaptCoeff2[] = {
  0, -256, 0, 64, 0, -208, -232
};

static gint16
read_sample (guint8 * data)
{
  guint16 val = data[0] | (data[1] << 8);
  return *((gint16 *) & val);
}

/* Decode a single block of data from 'data', storing 'n_samples' decoded 16 bit
   samples in 'samples'.

   All buffer lengths have been verified by the caller 
 */
static gboolean
adpcmdec_decode_ms_block (ADPCMDec * dec, int n_samples, guint8 * data,
    gint16 * samples)
{
  gint16 pred[2];
  gint16 idelta[2];
  int idx;                      /* Current byte offset in 'data' */
  int i;                        /* Current sample index in 'samples' */

  /* Read the block header, verify for sanity */
  if (dec->channels == 1) {
    pred[0] = data[0];
    idelta[0] = read_sample (data + 1);
    samples[1] = read_sample (data + 3);
    samples[0] = read_sample (data + 5);
    idx = 7;
    i = 2;
    if (pred[0] < 0 || pred[0] > 6) {
      GST_WARNING_OBJECT (dec, "Invalid block predictor");
      return FALSE;
    }
  }

  else {
    pred[0] = data[0];
    pred[1] = data[1];
    idelta[0] = read_sample (data + 2);
    idelta[1] = read_sample (data + 4);
    samples[2] = read_sample (data + 6);
    samples[3] = read_sample (data + 8);
    samples[0] = read_sample (data + 10);
    samples[1] = read_sample (data + 12);
    idx = 14;
    i = 4;
    if (pred[0] < 0 || pred[0] > 6 || pred[1] < 0 || pred[1] > 6) {
      GST_WARNING_OBJECT (dec, "Invalid block predictor");
      return FALSE;
    }
  }
  for (; i < n_samples; i++) {
    int chan = i % dec->channels;
    int bytecode;
    int delta;
    int current;
    int predict;
    if (i % 2 == 0) {
      bytecode = (data[idx] >> 4) & 0x0F;
    } else {
      bytecode = data[idx] & 0x0F;
      idx++;
    }

    delta = idelta[chan];
    idelta[chan] = (AdaptationTable[bytecode] * delta) >> 8;
    if (idelta[chan] < 16)
      idelta[chan] = 16;

    /* Bytecode is used above as an index into the table. Below, it's used
       as a signed 4-bit value; convert appropriately */
    if (bytecode & 0x8)
      bytecode -= 0x10;

    predict = ((samples[i - dec->channels] * AdaptCoeff1[pred[chan]]) +
        (samples[i - 2 * dec->channels] * AdaptCoeff2[pred[chan]])
        ) >> 8;

    current = (bytecode * delta) + predict;

    /* Clamp to 16 bits, store decoded sample */
    samples[i] = CLAMP (current, G_MININT16, G_MAXINT16);
  }
  return TRUE;
}

static int ima_indx_adjust[16] = { -1, -1, -1, -1,      /* +0 - +3, decrease the step size */
  2, 4, 6, 8,                   /* +4 - +7, increase the step size */
  -1, -1, -1, -1,               /* -0 - -3, decrease the step size */
  2, 4, 6, 8,                   /* -4 - -7, increase the step size */
};

static int ima_step_size[89] = {
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
  50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
  253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
  1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
  3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
  11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
  32767
};

/* Decode a single block of data from 'data', storing 'n_samples' decoded 16 bit
   samples in 'samples'.

   All buffer lengths have been verified by the caller 
 */
static gboolean
adpcmdec_decode_ima_block (ADPCMDec * dec, int n_samples, guint8 * data,
    gint16 * samples)
{
  gint16 stepindex[2];
  int channel;
  int idx;                      /* Current byte offset in 'data' */
  int i;                        /* Current sample */

  for (channel = 0; channel < dec->channels; channel++) {
    samples[channel] = read_sample (data + channel * 4);
    stepindex[channel] = CLAMP (data[channel * 4 + 2], 0, 88);

    if (data[channel * 4 + 3] != 0) {
      GST_WARNING_OBJECT (dec, "Synchronisation error");
      return FALSE;
    }
  }

  i = dec->channels;
  idx = 4 * dec->channels;

  GST_DEBUG ("Decoding %d samples", n_samples);
  for (; i < n_samples; i++) {
    int chan = i % dec->channels;
    int bytecode;
    int step;
    int val;
    int difference;

#if 1
    if (i % 2 == 0) {
      bytecode = (data[idx] >> 4) & 0x0F;
    } else {
      bytecode = data[idx] & 0x0F;
      idx++;
    }
#endif
    step = ima_step_size[stepindex[chan]];
#if 1

    difference = (2 * (bytecode & 0x7) * step + step) / 8;
    if (bytecode & 8)
      difference = -difference;
#else
    val = step >> 3;
    if (bytecode & 4)
      val += step;
    if (bytecode & 2)
      val += step >> 1;
    if (bytecode & 1)
      val += step >> 2;
    if (bytecode & 8)
      val = -val;
    difference = val;
#endif

    val = (int) samples[i - dec->channels] + difference;
    samples[i] = CLAMP (val, G_MININT16, G_MAXINT16);
    stepindex[channel] = CLAMP (stepindex[channel] + ima_indx_adjust[bytecode],
        0, 88);
  }
  return TRUE;
}

static GstFlowReturn
adpcmdec_chain (GstPad * pad, GstBuffer * buf)
{
  ADPCMDec *dec = (ADPCMDec *) gst_pad_get_parent (pad);
  GstFlowReturn ret = GST_FLOW_OK;
  guint8 *data;
  GstBuffer *outbuf = NULL;
  GstBuffer *databuf = NULL;
  int outsize;
  int samples;
  gboolean res;

  if (!dec->is_setup)
    adpcmdec_setup (dec);

  if (dec->timestamp == GST_CLOCK_TIME_NONE)
    dec->timestamp = GST_BUFFER_TIMESTAMP (buf);

  gst_adapter_push (dec->adapter, buf);

  while (gst_adapter_available (dec->adapter) >= dec->blocksize) {
    databuf = gst_adapter_take_buffer (dec->adapter, dec->blocksize);
    data = GST_BUFFER_DATA (databuf);

    if (dec->layout == LAYOUT_ADPCM_MICROSOFT) {
      /* Each block has a 3 byte header per channel, plus 4 bytes per channel to
         give two initial sample values per channel. Then the remainder gives
         two samples per byte */
      samples = (dec->blocksize - 7 * dec->channels) * 2 + 2 * dec->channels;
      outsize = 2 * samples;
      outbuf = gst_buffer_new_and_alloc (outsize);

      res = adpcmdec_decode_ms_block (dec, samples, data,
          (gint16 *) (GST_BUFFER_DATA (outbuf)));
    } else if (dec->layout == LAYOUT_ADPCM_DVI) {
      /* Each block has a 4 byte header per channel, include an initial sample.
         Then the remainder gives two samples per byte */
      samples = (dec->blocksize - 4 * dec->channels) * 2 + dec->channels;
      outsize = 2 * samples;
      outbuf = gst_buffer_new_and_alloc (outsize);

      res = adpcmdec_decode_ima_block (dec, samples, data,
          (gint16 *) (GST_BUFFER_DATA (outbuf)));
    } else {
      GST_WARNING_OBJECT (dec, "Unknown layout");
      ret = GST_FLOW_ERROR;
      goto done;
    }

    /* Done with input data, free it */
    gst_buffer_unref (databuf);

    if (!res) {
      gst_buffer_unref (outbuf);
      GST_WARNING_OBJECT (dec, "Decode of block failed");
      ret = GST_FLOW_ERROR;
      goto done;
    }

    gst_buffer_set_caps (outbuf, dec->output_caps);
    GST_BUFFER_TIMESTAMP (outbuf) = dec->timestamp;
    dec->out_samples += samples / dec->channels;
    dec->timestamp =
        gst_util_uint64_scale_int (dec->out_samples, GST_SECOND, dec->rate);
    GST_BUFFER_DURATION (outbuf) =
        dec->timestamp - GST_BUFFER_TIMESTAMP (outbuf);

    ret = gst_pad_push (dec->srcpad, outbuf);
    if (ret != GST_FLOW_OK)
      goto done;
  }

done:
  gst_object_unref (dec);

  return ret;
}

static gboolean
adpcmdec_sink_event (GstPad * pad, GstEvent * event)
{
  ADPCMDec *dec = (ADPCMDec *) gst_pad_get_parent (pad);
  gboolean res;
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_adapter_clear (dec->adapter);
      /* Fall through */
    default:
      res = gst_pad_push_event (dec->srcpad, event);
      break;
  }
  gst_object_unref (dec);
  return res;
}

static GstStateChangeReturn
adpcmdec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  ADPCMDec *dec = (ADPCMDec *) element;

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
      adpcmdec_teardown (dec);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

static void
adpcmdec_dispose (GObject * obj)
{
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
adpcmdec_init (ADPCMDec * dec, ADPCMDecClass * klass)
{
  dec->sinkpad =
      gst_pad_new_from_static_template (&adpcmdec_sink_template, "sink");
  gst_pad_set_setcaps_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (adpcmdec_sink_setcaps));
  gst_pad_set_chain_function (dec->sinkpad, GST_DEBUG_FUNCPTR (adpcmdec_chain));
  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (adpcmdec_sink_event));
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);
  dec->srcpad =
      gst_pad_new_from_static_template (&adpcmdec_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);
}

static void
adpcmdec_class_init (ADPCMDecClass * klass)
{
  GObjectClass *gobjectclass = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  gobjectclass->dispose = adpcmdec_dispose;
  gstelement_class->change_state = adpcmdec_change_state;
} static void

adpcmdec_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&adpcmdec_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&adpcmdec_src_template));
  gst_element_class_set_details (element_class, &adpcmdec_details);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (adpcmdec_debug, "adpcmdec", 0, "ADPCM Decoders");
  if (!gst_element_register (plugin, "adpcmdec", GST_RANK_PRIMARY,
          GST_TYPE_ADPCM_DEC)) {
    return FALSE;
  }
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, "adpcmdec",
    "ADPCM decoder", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);
