/* GStreamer AIFF muxer
 * Copyright (C) 2009 Robert Swain <robert.swain@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-aiffmux
 *
 * Format an audio stream into the Audio Interchange File Format
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <math.h>
#include <gst/gst.h>
#include <gst/base/gstbytewriter.h>

#include "aiffmux.h"

GST_DEBUG_CATEGORY (aiffmux_debug);
#define GST_CAT_DEFAULT aiffmux_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 8, "
        "depth = (int) [ 1, 8 ], "
        "signed = (boolean) true, "
        "endianness = (int) BIG_ENDIAN, "
        "channels = (int) [ 1, MAX ], "
        "rate = (int) [ 1, MAX ];"
        "audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) [ 9, 16 ], "
        "signed = (boolean) true, "
        "endianness = (int) BIG_ENDIAN, "
        "channels = (int) [ 1, MAX ], "
        "rate = (int) [ 1, MAX ];"
        "audio/x-raw-int, "
        "width = (int) 24, "
        "depth = (int) [ 17, 24 ], "
        "signed = (boolean) true, "
        "endianness = (int) BIG_ENDIAN, "
        "channels = (int) [ 1, MAX ], "
        "rate = (int) [ 1, MAX ];"
        "audio/x-raw-int, "
        "width = (int) 32, "
        "depth = (int) [ 25, 32 ], "
        "signed = (boolean) true, "
        "endianness = (int) BIG_ENDIAN, "
        "channels = (int) [ 1, MAX ], " "rate = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-aiff")
    );

GST_BOILERPLATE (GstAiffMux, gst_aiff_mux, GstElement, GST_TYPE_ELEMENT);

static void
gst_aiff_mux_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "AIFF audio muxer", "Muxer/Audio", "Multiplex raw audio into AIFF",
      "Robert Swain <robert.swain@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

static GstStateChangeReturn
gst_aiff_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstAiffMux *aiffmux = GST_AIFF_MUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      aiffmux->width = 0;
      aiffmux->depth = 0;
      aiffmux->channels = 0;
      aiffmux->length = 0;
      aiffmux->rate = 0.0;
      aiffmux->sent_header = FALSE;
      aiffmux->overflow = FALSE;
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  return ret;
}

static void
gst_aiff_mux_class_init (GstAiffMuxClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_aiff_mux_change_state);
}

#define AIFF_FORM_HEADER_LEN 8 + 4
#define AIFF_COMM_HEADER_LEN 8 + 18
#define AIFF_SSND_HEADER_LEN 8 + 8
#define AIFF_HEADER_LEN \
  (AIFF_FORM_HEADER_LEN + AIFF_COMM_HEADER_LEN + AIFF_SSND_HEADER_LEN)

static void
gst_aiff_mux_write_form_header (GstAiffMux * aiffmux, guint32 audio_data_size,
    GstByteWriter * writer)
{
  /* ckID == 'FORM' */
  gst_byte_writer_put_uint32_le (writer, GST_MAKE_FOURCC ('F', 'O', 'R', 'M'));
  /* ckSize is currently bogus but we'll know what it is later */
  gst_byte_writer_put_uint32_be (writer, audio_data_size + AIFF_HEADER_LEN - 8);
  /* formType == 'AIFF' */
  gst_byte_writer_put_uint32_le (writer, GST_MAKE_FOURCC ('A', 'I', 'F', 'F'));
}

/*
 * BEGIN: Code borrowed from FFmpeg's libavutil/intfloat_readwrite.{c,h}
 * Copyright (c) 2005 Michael Niedermayer <michaelni@gmx.at>
 */

/* IEEE 80 bits extended float */
typedef struct AVExtFloat
{
  guint8 exponent[2];
  guint8 mantissa[8];
} AVExtFloat;

/* Courtesy http://www.devx.com/tips/Tip/42853 */
static inline gint
gst_aiff_mux_isinf (gdouble x)
{
  volatile gdouble temp = x;
  if ((temp == x) && ((temp - x) != 0.0))
    return (x < 0.0 ? -1 : 1);
  else
    return 0;
}

static void
gst_aiff_mux_write_ext (GstByteWriter * writer, double d)
{
  struct AVExtFloat ext = { {0} };
  gint e, i;
  gdouble f;
  guint64 m;

  f = fabs (frexp (d, &e));
  if (f >= 0.5 && f < 1) {
    e += 16382;
    ext.exponent[0] = e >> 8;
    ext.exponent[1] = e;
    m = (guint64) ldexp (f, 64);
    for (i = 0; i < 8; i++)
      ext.mantissa[i] = m >> (56 - (i << 3));
  } else if (f != 0.0) {
    ext.exponent[0] = 0x7f;
    ext.exponent[1] = 0xff;
    if (!gst_aiff_mux_isinf (f))
      ext.mantissa[0] = ~0;
  }
  if (d < 0)
    ext.exponent[0] |= 0x80;

  gst_byte_writer_put_data (writer, ext.exponent, 2);
  gst_byte_writer_put_data (writer, ext.mantissa, 8);
}

/*
 * END: Code borrowed from FFmpeg's libavutil/intfloat_readwrite.{c,h}
 */

static void
gst_aiff_mux_write_comm_header (GstAiffMux * aiffmux, guint32 audio_data_size,
    GstByteWriter * writer)
{
  gst_byte_writer_put_uint32_le (writer, GST_MAKE_FOURCC ('C', 'O', 'M', 'M'));
  gst_byte_writer_put_uint32_be (writer, 18);
  gst_byte_writer_put_uint16_be (writer, aiffmux->channels);
  /* numSampleFrames value will be overwritten when known */
  gst_byte_writer_put_uint32_be (writer,
      audio_data_size / (aiffmux->width / 8 * aiffmux->channels));
  gst_byte_writer_put_uint16_be (writer, aiffmux->depth);
  gst_aiff_mux_write_ext (writer, aiffmux->rate);
}

static void
gst_aiff_mux_write_ssnd_header (GstAiffMux * aiffmux, guint32 audio_data_size,
    GstByteWriter * writer)
{
  gst_byte_writer_put_uint32_le (writer, GST_MAKE_FOURCC ('S', 'S', 'N', 'D'));
  /* ckSize will be overwritten when known */
  gst_byte_writer_put_uint32_be (writer,
      audio_data_size + AIFF_SSND_HEADER_LEN - 8);
  /* offset and blockSize are set to 0 as we don't support block-aligned sample data yet */
  gst_byte_writer_put_uint32_be (writer, 0);
  gst_byte_writer_put_uint32_be (writer, 0);
}

static GstFlowReturn
gst_aiff_mux_push_header (GstAiffMux * aiffmux, guint32 audio_data_size)
{
  GstFlowReturn ret;
  GstBuffer *outbuf;
  GstByteWriter *writer;

  /* seek to beginning of file */
  if (gst_pad_push_event (aiffmux->srcpad,
          gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES,
              0, GST_BUFFER_OFFSET_NONE, 0)) == FALSE) {
    GST_ELEMENT_WARNING (aiffmux, STREAM, MUX,
        ("An output stream seeking error occurred when multiplexing."),
        ("Failed to seek to beginning of stream to write header."));
  }

  GST_DEBUG_OBJECT (aiffmux, "writing header with datasize=%u",
      audio_data_size);

  writer = gst_byte_writer_new_with_size (AIFF_HEADER_LEN, TRUE);

  gst_aiff_mux_write_form_header (aiffmux, audio_data_size, writer);
  gst_aiff_mux_write_comm_header (aiffmux, audio_data_size, writer);
  gst_aiff_mux_write_ssnd_header (aiffmux, audio_data_size, writer);

  outbuf = gst_byte_writer_free_and_get_buffer (writer);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (aiffmux->srcpad));
  ret = gst_pad_push (aiffmux->srcpad, outbuf);

  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (aiffmux, "push header failed: flow = %s",
        gst_flow_get_name (ret));
  }

  return ret;
}

static GstFlowReturn
gst_aiff_mux_chain (GstPad * pad, GstBuffer * buf)
{
  GstAiffMux *aiffmux = GST_AIFF_MUX (GST_PAD_PARENT (pad));
  GstFlowReturn flow = GST_FLOW_OK;
  guint64 cur_size;

  if (!aiffmux->channels) {
    gst_buffer_unref (buf);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (G_UNLIKELY (aiffmux->overflow))
    goto overflow;

  if (!aiffmux->sent_header) {
    /* use bogus size initially, we'll write the real
     * header when we get EOS and know the exact length */
    flow = gst_aiff_mux_push_header (aiffmux, 0x7FFF0000);

    if (flow != GST_FLOW_OK) {
      gst_buffer_unref (buf);
      return flow;
    }

    GST_DEBUG_OBJECT (aiffmux, "wrote dummy header");
    aiffmux->sent_header = TRUE;
  }

  /* AIFF has an audio data size limit of slightly under 4 GB.
     A value of audiosize + AIFF_HEADER_LEN - 8 is written, so
     I'll error out if writing data that makes this overflow. */
  cur_size = aiffmux->length + AIFF_HEADER_LEN - 8;
  if (G_UNLIKELY (cur_size + GST_BUFFER_SIZE (buf) >= G_MAXUINT32)) {
    GST_ERROR_OBJECT (aiffmux, "AIFF only supports about 4 GB worth of "
        "audio data, dropping any further data on the floor");
    GST_ELEMENT_WARNING (aiffmux, STREAM, MUX, ("AIFF has a 4GB size limit"),
        ("AIFF only supports about 4 GB worth of audio data, "
            "dropping any further data on the floor"));
    aiffmux->overflow = TRUE;
    goto overflow;
  }

  GST_LOG_OBJECT (aiffmux, "pushing %u bytes raw audio, ts=%" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  buf = gst_buffer_make_metadata_writable (buf);

  gst_buffer_set_caps (buf, GST_PAD_CAPS (aiffmux->srcpad));
  GST_BUFFER_OFFSET (buf) = AIFF_HEADER_LEN + aiffmux->length;
  GST_BUFFER_OFFSET_END (buf) = GST_BUFFER_OFFSET_NONE;

  aiffmux->length += GST_BUFFER_SIZE (buf);

  flow = gst_pad_push (aiffmux->srcpad, buf);

  return flow;

overflow:
  {
    GST_WARNING_OBJECT (aiffmux, "output file too large, dropping buffer");
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }
}

static gboolean
gst_aiff_mux_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstAiffMux *aiffmux;

  aiffmux = GST_AIFF_MUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:{
      GST_DEBUG_OBJECT (aiffmux, "got EOS");

      /* write header with correct length values */
      gst_aiff_mux_push_header (aiffmux, aiffmux->length);

      /* and forward the EOS event */
      res = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_NEWSEGMENT:
      /* Just drop it, it's probably in TIME format
       * anyway. We'll send our own newsegment event */
      gst_event_unref (event);
      break;
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (aiffmux);
  return res;
}

static gboolean
gst_aiff_mux_set_caps (GstPad * pad, GstCaps * caps)
{
  GstAiffMux *aiffmux;
  GstStructure *structure;
  gint chans, rate, depth;

  aiffmux = GST_AIFF_MUX (GST_PAD_PARENT (pad));

  if (aiffmux->sent_header) {
    GST_WARNING_OBJECT (aiffmux, "cannot change format mid-stream");
    return FALSE;
  }

  GST_DEBUG_OBJECT (aiffmux, "got caps: %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "channels", &chans) ||
      !gst_structure_get_int (structure, "rate", &rate) ||
      !gst_structure_get_int (structure, "depth", &depth)) {
    GST_WARNING_OBJECT (aiffmux, "caps incomplete");
    return FALSE;
  }

  aiffmux->channels = chans;
  aiffmux->rate = rate;
  aiffmux->depth = depth;
  aiffmux->width = GST_ROUND_UP_8 (aiffmux->depth);

  GST_LOG_OBJECT (aiffmux,
      "accepted caps: chans=%u depth=%u rate=%lf",
      aiffmux->channels, aiffmux->depth, aiffmux->rate);

  return TRUE;
}

static void
gst_aiff_mux_init (GstAiffMux * aiffmux, GstAiffMuxClass * gclass)
{
  aiffmux->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (aiffmux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_aiff_mux_chain));
  gst_pad_set_event_function (aiffmux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_aiff_mux_event));
  gst_pad_set_setcaps_function (aiffmux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_aiff_mux_set_caps));
  gst_element_add_pad (GST_ELEMENT (aiffmux), aiffmux->sinkpad);

  aiffmux->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (aiffmux->srcpad);
  gst_pad_set_caps (aiffmux->srcpad,
      gst_static_pad_template_get_caps (&src_factory));
  gst_element_add_pad (GST_ELEMENT (aiffmux), aiffmux->srcpad);
}
