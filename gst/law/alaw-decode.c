/* GStreamer A-Law to PCM conversion
 * Copyright (C) 2000 by Abramo Bagnara <abramo@alsa-project.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
 * SECTION:element-alawdec
 *
 * This element decodes alaw audio. Alaw coding is also known as G.711.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "alaw-decode.h"

extern GstStaticPadTemplate alaw_dec_src_factory;
extern GstStaticPadTemplate alaw_dec_sink_factory;

GST_DEBUG_CATEGORY_STATIC (alaw_dec_debug);
#define GST_CAT_DEFAULT alaw_dec_debug

static GstStateChangeReturn
gst_alaw_dec_change_state (GstElement * element, GstStateChange transition);
static GstFlowReturn gst_alaw_dec_chain (GstPad * pad, GstBuffer * buffer);

GST_BOILERPLATE (GstALawDec, gst_alaw_dec, GstElement, GST_TYPE_ELEMENT);

/* some day we might have defines in gstconfig.h that tell us about the
 * desired cpu/memory/binary size trade-offs */
#define GST_ALAW_DEC_USE_TABLE

#ifdef GST_ALAW_DEC_USE_TABLE

static const gint alaw_to_s16_table[256] = {
  -5504, -5248, -6016, -5760, -4480, -4224, -4992, -4736,
  -7552, -7296, -8064, -7808, -6528, -6272, -7040, -6784,
  -2752, -2624, -3008, -2880, -2240, -2112, -2496, -2368,
  -3776, -3648, -4032, -3904, -3264, -3136, -3520, -3392,
  -22016, -20992, -24064, -23040, -17920, -16896, -19968, -18944,
  -30208, -29184, -32256, -31232, -26112, -25088, -28160, -27136,
  -11008, -10496, -12032, -11520, -8960, -8448, -9984, -9472,
  -15104, -14592, -16128, -15616, -13056, -12544, -14080, -13568,
  -344, -328, -376, -360, -280, -264, -312, -296,
  -472, -456, -504, -488, -408, -392, -440, -424,
  -88, -72, -120, -104, -24, -8, -56, -40,
  -216, -200, -248, -232, -152, -136, -184, -168,
  -1376, -1312, -1504, -1440, -1120, -1056, -1248, -1184,
  -1888, -1824, -2016, -1952, -1632, -1568, -1760, -1696,
  -688, -656, -752, -720, -560, -528, -624, -592,
  -944, -912, -1008, -976, -816, -784, -880, -848,
  5504, 5248, 6016, 5760, 4480, 4224, 4992, 4736,
  7552, 7296, 8064, 7808, 6528, 6272, 7040, 6784,
  2752, 2624, 3008, 2880, 2240, 2112, 2496, 2368,
  3776, 3648, 4032, 3904, 3264, 3136, 3520, 3392,
  22016, 20992, 24064, 23040, 17920, 16896, 19968, 18944,
  30208, 29184, 32256, 31232, 26112, 25088, 28160, 27136,
  11008, 10496, 12032, 11520, 8960, 8448, 9984, 9472,
  15104, 14592, 16128, 15616, 13056, 12544, 14080, 13568,
  344, 328, 376, 360, 280, 264, 312, 296,
  472, 456, 504, 488, 408, 392, 440, 424,
  88, 72, 120, 104, 24, 8, 56, 40,
  216, 200, 248, 232, 152, 136, 184, 168,
  1376, 1312, 1504, 1440, 1120, 1056, 1248, 1184,
  1888, 1824, 2016, 1952, 1632, 1568, 1760, 1696,
  688, 656, 752, 720, 560, 528, 624, 592,
  944, 912, 1008, 976, 816, 784, 880, 848
};

static inline gint
alaw_to_s16 (guint8 a_val)
{
  return alaw_to_s16_table[a_val];
}

#else /* GST_ALAW_DEC_USE_TABLE */

static inline gint
alaw_to_s16 (guint8 a_val)
{
  gint t;
  gint seg;

  a_val ^= 0x55;
  t = a_val & 0x7f;
  if (t < 16)
    t = (t << 4) + 8;
  else {
    seg = (t >> 4) & 0x07;
    t = ((t & 0x0f) << 4) + 0x108;
    t <<= seg - 1;
  }
  return ((a_val & 0x80) ? t : -t);
}

#endif /* GST_ALAW_DEC_USE_TABLE */

static gboolean
gst_alaw_dec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstALawDec *alawdec;
  GstStructure *structure;
  int rate, channels;
  gboolean ret;
  GstCaps *outcaps;

  alawdec = GST_ALAW_DEC (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "rate", &rate);
  ret &= gst_structure_get_int (structure, "channels", &channels);
  if (!ret)
    return FALSE;

  outcaps = gst_caps_new_simple ("audio/x-raw-int",
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, channels, NULL);

  ret = gst_pad_set_caps (alawdec->srcpad, outcaps);
  gst_caps_unref (outcaps);

  if (ret) {
    GST_DEBUG_OBJECT (alawdec, "rate=%d, channels=%d", rate, channels);
    alawdec->rate = rate;
    alawdec->channels = channels;
  }
  return ret;
}

static GstCaps *
gst_alaw_dec_getcaps (GstPad * pad)
{
  GstALawDec *alawdec;
  GstPad *otherpad;
  GstCaps *othercaps, *result;
  const GstCaps *templ;
  const gchar *name;
  gint i;

  alawdec = GST_ALAW_DEC (GST_PAD_PARENT (pad));

  /* figure out the name of the caps we are going to return */
  if (pad == alawdec->srcpad) {
    name = "audio/x-raw-int";
    otherpad = alawdec->sinkpad;
  } else {
    name = "audio/x-alaw";
    otherpad = alawdec->srcpad;
  }
  /* get caps from the peer, this can return NULL when there is no peer */
  othercaps = gst_pad_peer_get_caps (otherpad);

  /* get the template caps to make sure we return something acceptable */
  templ = gst_pad_get_pad_template_caps (pad);

  if (othercaps) {
    /* there was a peer */
    othercaps = gst_caps_make_writable (othercaps);

    /* go through the caps and remove the fields we don't want */
    for (i = 0; i < gst_caps_get_size (othercaps); i++) {
      GstStructure *structure;

      structure = gst_caps_get_structure (othercaps, i);

      /* adjust the name */
      gst_structure_set_name (structure, name);

      if (pad == alawdec->sinkpad) {
        /* remove the fields we don't want */
        gst_structure_remove_fields (structure, "width", "depth", "endianness",
            "signed", NULL);
      } else {
        /* add fixed fields */
        gst_structure_set (structure, "width", G_TYPE_INT, 16,
            "depth", G_TYPE_INT, 16,
            "endianness", G_TYPE_INT, G_BYTE_ORDER,
            "signed", G_TYPE_BOOLEAN, TRUE, NULL);
      }
    }
    /* filter against the allowed caps of the pad to return our result */
    result = gst_caps_intersect (othercaps, templ);
    gst_caps_unref (othercaps);
  } else {
    /* there was no peer, return the template caps */
    result = gst_caps_copy (templ);
  }

  return result;
}

static void
gst_alaw_dec_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &alaw_dec_src_factory);
  gst_element_class_add_static_pad_template (element_class,
      &alaw_dec_sink_factory);

  gst_element_class_set_details_simple (element_class, "A Law audio decoder",
      "Codec/Decoder/Audio", "Convert 8bit A law to 16bit PCM",
      "Zaheer Abbas Merali <zaheerabbas at merali dot org>");

  GST_DEBUG_CATEGORY_INIT (alaw_dec_debug, "alawdec", 0, "A Law audio decoder");
}

static void
gst_alaw_dec_class_init (GstALawDecClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_alaw_dec_change_state);
}

static void
gst_alaw_dec_init (GstALawDec * alawdec, GstALawDecClass * klass)
{
  alawdec->sinkpad =
      gst_pad_new_from_static_template (&alaw_dec_sink_factory, "sink");
  gst_pad_set_setcaps_function (alawdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_alaw_dec_sink_setcaps));
  gst_pad_set_getcaps_function (alawdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_alaw_dec_getcaps));
  gst_pad_set_chain_function (alawdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_alaw_dec_chain));
  gst_element_add_pad (GST_ELEMENT (alawdec), alawdec->sinkpad);

  alawdec->srcpad =
      gst_pad_new_from_static_template (&alaw_dec_src_factory, "src");
  gst_pad_use_fixed_caps (alawdec->srcpad);
  gst_pad_set_getcaps_function (alawdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_alaw_dec_getcaps));
  gst_element_add_pad (GST_ELEMENT (alawdec), alawdec->srcpad);
}

static GstFlowReturn
gst_alaw_dec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstALawDec *alawdec;
  gint16 *linear_data;
  guint8 *alaw_data;
  guint alaw_size;
  GstBuffer *outbuf;
  gint i;
  GstFlowReturn ret;

  alawdec = GST_ALAW_DEC (GST_PAD_PARENT (pad));

  if (G_UNLIKELY (alawdec->rate == 0))
    goto not_negotiated;

  GST_LOG_OBJECT (alawdec, "buffer with ts=%" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

  alaw_data = GST_BUFFER_DATA (buffer);
  alaw_size = GST_BUFFER_SIZE (buffer);

  ret =
      gst_pad_alloc_buffer_and_set_caps (alawdec->srcpad,
      GST_BUFFER_OFFSET_NONE, alaw_size * 2, GST_PAD_CAPS (alawdec->srcpad),
      &outbuf);
  if (ret != GST_FLOW_OK)
    goto alloc_failed;

  linear_data = (gint16 *) GST_BUFFER_DATA (outbuf);

  /* copy discont flag */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);

  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buffer);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (alawdec->srcpad));

  for (i = 0; i < alaw_size; i++) {
    linear_data[i] = alaw_to_s16 (alaw_data[i]);
  }
  gst_buffer_unref (buffer);

  ret = gst_pad_push (alawdec->srcpad, outbuf);

  return ret;

not_negotiated:
  {
    gst_buffer_unref (buffer);
    GST_WARNING_OBJECT (alawdec, "no input format set: not-negotiated");
    return GST_FLOW_NOT_NEGOTIATED;
  }
alloc_failed:
  {
    gst_buffer_unref (buffer);
    GST_DEBUG_OBJECT (alawdec, "pad alloc failed, flow: %s",
        gst_flow_get_name (ret));
    return ret;
  }
}

static GstStateChangeReturn
gst_alaw_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstALawDec *dec = GST_ALAW_DEC (element);

  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      dec->rate = 0;
      dec->channels = 0;
      break;
    default:
      break;
  }

  return ret;
}
