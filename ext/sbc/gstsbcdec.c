/*  GStreamer SBC audio decoder
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
#include "gstsbcdec.h"
#include <gst/audio/audio.h>

/* FIXME: where does this come from? how is it derived? */
#define BUF_SIZE 8192

GST_DEBUG_CATEGORY_STATIC (sbc_dec_debug);
#define GST_CAT_DEFAULT sbc_dec_debug

static void gst_sbc_dec_finalize (GObject * obj);

/* FIXME: port to GstAudioDecoder base class */
#define parent_class gst_sbc_dec_parent_class
G_DEFINE_TYPE (GstSbcDec, gst_sbc_dec, GST_TYPE_ELEMENT);

static GstStaticPadTemplate sbc_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sbc"));

static GstStaticPadTemplate sbc_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, format=" GST_AUDIO_NE (S16) ", "
        "rate = (int) { 16000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ], layout=interleaved"));

static GstFlowReturn
gst_sbc_dec_flush (GstSbcDec * dec, GstBuffer * outbuf,
    gint outoffset, gint channels, gint rate)
{
  GstClockTime outtime, duration;

  /* we will reuse the same caps object */
  if (dec->send_caps) {
    GstCaps *caps;

    caps = gst_caps_new_simple ("audio/x-raw",
        "format", G_TYPE_STRING, GST_AUDIO_NE (S16),
        "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, channels,
        "layout", G_TYPE_STRING, "interleaved", NULL);

    gst_pad_push_event (dec->srcpad, gst_event_new_caps (caps));

    gst_caps_unref (caps);
  }

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
  gst_buffer_resize (outbuf, 0, outoffset);

  return gst_pad_push (dec->srcpad, outbuf);

}

static GstFlowReturn
sbc_dec_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstSbcDec *dec = GST_SBC_DEC (parent);
  GstFlowReturn res = GST_FLOW_OK;
  const guint8 *indata;
  guint insize;
  GstClockTime timestamp;
  gboolean discont;
  GstMapInfo out_map;
  GstBuffer *outbuf;
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
  indata = gst_adapter_map (dec->adapter, insize);

  inoffset = 0;
  outbuf = NULL;
  channels = rate = 0;

  while (insize > 0) {
    gint inconsumed, outlen;
    gint outsize;
    size_t outconsumed;

    if (outbuf == NULL) {
      outbuf = gst_buffer_new_and_alloc (BUF_SIZE);

      if (discont) {
        GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
        discont = FALSE;
      }

      GST_BUFFER_TIMESTAMP (outbuf) = dec->next_timestamp;

      gst_buffer_map (outbuf, &out_map, GST_MAP_WRITE);
      outsize = out_map.size;
      outoffset = 0;
    }

    GST_INFO_OBJECT (dec, "inoffset %d/%d, outoffset %d/%d", inoffset,
        insize, outoffset, outsize);

    inconsumed = sbc_decode (&dec->sbc, indata + inoffset, insize,
        out_map.data + outoffset, outsize, &outconsumed);

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
      gst_buffer_unmap (outbuf, &out_map);

      res = gst_sbc_dec_flush (dec, outbuf, outoffset, channels, rate);

      outbuf = NULL;

      if (res != GST_FLOW_OK)
        goto done;
    }
  }

  if (outbuf) {
    gst_buffer_unmap (outbuf, &out_map);

    res = gst_sbc_dec_flush (dec, outbuf, outoffset, channels, rate);
  }

done:

  gst_adapter_unmap (dec->adapter);
  gst_adapter_flush (dec->adapter, inoffset);

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
      dec->send_caps = TRUE;
      dec->next_sample = -1;
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG ("Finish subband codec");
      gst_adapter_clear (dec->adapter);
      sbc_finish (&dec->sbc);
      dec->send_caps = TRUE;
      break;

    default:
      break;
  }

  return result;
}

static void
gst_sbc_dec_class_init (GstSbcDecClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  object_class->finalize = gst_sbc_dec_finalize;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_sbc_dec_change_state);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sbc_dec_sink_factory));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sbc_dec_src_factory));

  gst_element_class_set_static_metadata (element_class,
      "Bluetooth SBC audio decoder", "Codec/Decoder/Audio",
      "Decode an SBC audio stream", "Marcel Holtmann <marcel@holtmann.org>");

  GST_DEBUG_CATEGORY_INIT (sbc_dec_debug, "sbcdec", 0, "SBC decoding element");
}

static void
gst_sbc_dec_init (GstSbcDec * self)
{
  self->sinkpad =
      gst_pad_new_from_static_template (&sbc_dec_sink_factory, "sink");
  gst_pad_set_chain_function (self->sinkpad, GST_DEBUG_FUNCPTR (sbc_dec_chain));
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  self->srcpad = gst_pad_new_from_static_template (&sbc_dec_src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->adapter = gst_adapter_new ();
  self->send_caps = TRUE;
}

static void
gst_sbc_dec_finalize (GObject * obj)
{
  GstSbcDec *self = GST_SBC_DEC (obj);

  g_object_unref (self->adapter);
  self->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}
