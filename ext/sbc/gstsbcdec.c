/*
 *
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

#include "gstpragma.h"
#include "gstsbcutil.h"
#include "gstsbcdec.h"

#define BUF_SIZE 8192

GST_DEBUG_CATEGORY_STATIC (sbc_dec_debug);
#define GST_CAT_DEFAULT sbc_dec_debug

static void gst_sbc_dec_finalize (GObject * obj);

GST_BOILERPLATE (GstSbcDec, gst_sbc_dec, GstElement, GST_TYPE_ELEMENT);

static const GstElementDetails sbc_dec_details =
GST_ELEMENT_DETAILS ("Bluetooth SBC decoder",
    "Codec/Decoder/Audio",
    "Decode a SBC audio stream",
    "Marcel Holtmann <marcel@holtmann.org>");

static GstStaticPadTemplate sbc_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sbc"));

static GstStaticPadTemplate sbc_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) { 16000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, " "width = (int) 16, " "depth = (int) 16"));

static GstFlowReturn
gst_sbc_dec_flush (GstSbcDec * dec, GstBuffer * outbuf,
    gint outoffset, gint channels, gint rate)
{
  GstClockTime outtime, duration;

  /* we will reuse the same caps object */
  if (dec->outcaps == NULL) {
    GstCaps *caps;
    GstPadTemplate *template;

    caps = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, channels, NULL);

    template = gst_static_pad_template_get (&sbc_dec_src_factory);

    dec->outcaps = gst_caps_intersect (caps,
        gst_pad_template_get_caps (template));

    gst_caps_unref (caps);
    gst_object_unref (template);
  }

  gst_buffer_set_caps (outbuf, dec->outcaps);

  /* calculate duration */
  outtime = GST_BUFFER_TIMESTAMP (outbuf);
  if (dec->next_timestamp != (guint64) - 1 && outtime != (guint64) - 1) {
    duration = dec->next_timestamp - outtime;
  } else if (outtime != (guint64) - 1) {
    /* otherwise calculate duration based on outbuf size */
    duration = gst_util_uint64_scale_int (outoffset / (2 * channels),
        GST_SECOND, rate) - outtime;
  } else {
    duration = GST_CLOCK_TIME_NONE;
  }
  GST_BUFFER_DURATION (outbuf) = duration;
  GST_BUFFER_SIZE (outbuf) = outoffset;

  return gst_pad_push (dec->srcpad, outbuf);

}

static GstFlowReturn
sbc_dec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSbcDec *dec = GST_SBC_DEC (gst_pad_get_parent (pad));
  GstFlowReturn res = GST_FLOW_OK;
  const guint8 *indata;
  guint insize;
  GstClockTime timestamp;
  gboolean discont;
  GstBuffer *outbuf;
  guint8 *outdata;
  guint inoffset, outoffset;
  gint rate, channels;

  discont = GST_BUFFER_IS_DISCONT (buffer);
  if (discont) {
    /* reset previous buffer */
    gst_adapter_clear (dec->adapter);
    /* we need a new timestamp to lock onto */
    dec->next_sample = -1;
  }

  gst_adapter_push (dec->adapter, buffer);

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_CLOCK_TIME_IS_VALID (timestamp))
    dec->next_timestamp = timestamp;

  insize = gst_adapter_available (dec->adapter);
  indata = gst_adapter_peek (dec->adapter, insize);


  inoffset = 0;
  outbuf = NULL;
  channels = rate = 0;

  while (insize > 0) {
    gint inconsumed, outlen;
    gint outsize;
    size_t outconsumed;

    if (outbuf == NULL) {
      res = gst_pad_alloc_buffer_and_set_caps (dec->srcpad,
          GST_BUFFER_OFFSET_NONE, BUF_SIZE, NULL, &outbuf);

      if (res != GST_FLOW_OK)
        goto done;

      if (discont) {
        GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
        discont = FALSE;
      }

      GST_BUFFER_TIMESTAMP (outbuf) = dec->next_timestamp;
      outdata = GST_BUFFER_DATA (outbuf);
      outsize = GST_BUFFER_SIZE (outbuf);
      outoffset = 0;
    }

    GST_INFO_OBJECT (dec, "inoffset %d/%d, outoffset %d/%d", inoffset,
        insize, outoffset, outsize);

    inconsumed = sbc_decode (&dec->sbc, indata + inoffset, insize,
        outdata + outoffset, outsize, &outconsumed);

    GST_INFO_OBJECT (dec, "consumed %d, produced %d", inconsumed, outconsumed);

    if (inconsumed <= 0) {
      guint frame_len = sbc_get_frame_length (&dec->sbc);
      /* skip a frame */
      if (insize > frame_len) {
        insize -= frame_len;
        inoffset += frame_len;
      } else {
        insize = 0;
      }
      continue;
    }

    inoffset += inconsumed;
    if ((gint) insize > inconsumed)
      insize -= inconsumed;
    else
      insize = 0;
    outoffset += outconsumed;
    outsize -= outconsumed;

    rate = gst_sbc_parse_rate_from_sbc (dec->sbc.frequency);
    channels = gst_sbc_get_channel_number (dec->sbc.mode);

    /* calculate timestamp either from the incomming buffers or
     * from our sample counter */
    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      /* lock onto timestamp when we have one */
      dec->next_sample = gst_util_uint64_scale_int (timestamp,
          rate, GST_SECOND);
      timestamp = GST_CLOCK_TIME_NONE;
    }
    if (dec->next_sample != (guint64) - 1) {
      /* calculate the next sample */
      dec->next_sample += outconsumed / (2 * channels);
      dec->next_timestamp = gst_util_uint64_scale_int (dec->next_sample,
          GST_SECOND, rate);
    }

    /* check for space, push outbuf buffer */
    outlen = sbc_get_codesize (&dec->sbc);
    if (outsize < outlen) {
      res = gst_sbc_dec_flush (dec, outbuf, outoffset, channels, rate);
      if (res != GST_FLOW_OK)
        goto done;

      outbuf = NULL;
    }

  }

  if (outbuf)
    res = gst_sbc_dec_flush (dec, outbuf, outoffset, channels, rate);

  gst_adapter_flush (dec->adapter, inoffset);
done:
  gst_object_unref (dec);

  return res;
}

static GstStateChangeReturn
gst_sbc_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn result;
  GstSbcDec *dec = GST_SBC_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG ("Setup subband codec");
      sbc_init (&dec->sbc, 0);
      dec->outcaps = NULL;
      dec->next_sample = -1;
      break;
    default:
      break;
  }

  result = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG ("Finish subband codec");
      gst_adapter_clear (dec->adapter);
      sbc_finish (&dec->sbc);
      if (dec->outcaps) {
        gst_caps_unref (dec->outcaps);
        dec->outcaps = NULL;
      }
      break;

    default:
      break;
  }

  return result;
}

static void
gst_sbc_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sbc_dec_sink_factory));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sbc_dec_src_factory));

  gst_element_class_set_details (element_class, &sbc_dec_details);
}

static void
gst_sbc_dec_class_init (GstSbcDecClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->finalize = GST_DEBUG_FUNCPTR (gst_sbc_dec_finalize);

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_sbc_dec_change_state);

  GST_DEBUG_CATEGORY_INIT (sbc_dec_debug, "sbcdec", 0, "SBC decoding element");
}

static void
gst_sbc_dec_init (GstSbcDec * self, GstSbcDecClass * klass)
{
  self->sinkpad =
      gst_pad_new_from_static_template (&sbc_dec_sink_factory, "sink");
  gst_pad_set_chain_function (self->sinkpad, GST_DEBUG_FUNCPTR (sbc_dec_chain));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&sbc_dec_src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->adapter = gst_adapter_new ();
  self->outcaps = NULL;
}

static void
gst_sbc_dec_finalize (GObject * obj)
{
  GstSbcDec *self = GST_SBC_DEC (obj);

  g_object_unref (self->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (obj);

}

gboolean
gst_sbc_dec_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "sbcdec", GST_RANK_PRIMARY,
      GST_TYPE_SBC_DEC);
}
