/* GStreamer Wavpack plugin
 * (c) 2005 Arwed v. Merkatz <v.merkatz@gmx.net>
 *
 * gstwavpackdec.c: raw Wavpack bitstream decoder
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

#include <gst/gst.h>

#include <math.h>
#include <string.h>

#include <wavpack/wavpack.h>
#include "gstwavpackdec.h"
#include "gstwavpackcommon.h"

GST_DEBUG_CATEGORY_STATIC (gst_wavpack_dec_debug);
#define GST_CAT_DEFAULT gst_wavpack_dec_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "width = (int) { 8, 16, 24, 32 }, "
        "channels = (int) { 1, 2 }, "
        "rate = (int) [ 6000, 192000 ], " "framed = (boolean) true")
    );

static GstStaticPadTemplate wvc_sink_factory =
GST_STATIC_PAD_TEMPLATE ("wvcsink",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/x-wavpack-correction, " "framed = (boolean) true")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) { 8, 16, 24, 32 }, "
        "depth = (int) { 8, 16, 24, 32 }, "
        "channels = (int) { 1, 2 }, "
        "rate = (int) [ 6000, 192000 ], "
        "endianness = (int) LITTLE_ENDIAN, " "signed = (boolean) true")
/*
        "audio/x-raw-float, "
        "width = (int) 32, "
        "channels = (int) { 1, 2 }, "
        "rate = (int) [ 6000, 192000 ], " "endianness = (int) LITTLE_ENDIAN"
*/
    );

static GstFlowReturn gst_wavpack_dec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_wavpack_dec_sink_event (GstPad * pad, GstEvent * event);

GST_BOILERPLATE (GstWavpackDec, gst_wavpack_dec, GstElement, GST_TYPE_ELEMENT)

     static gboolean gst_wavpack_dec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstWavpackDec *wavpackdec = GST_WAVPACK_DEC (gst_pad_get_parent (pad));
  GstStructure *structure;
  GstCaps *srccaps;
  gint bits, rate, channels;

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "rate", &rate) ||
      !gst_structure_get_int (structure, "channels", &channels) ||
      !gst_structure_get_int (structure, "width", &bits)) {
    return FALSE;
  }

  wavpackdec->samplerate = rate;
  wavpackdec->channels = channels;
  wavpackdec->width = bits;

/* 32-bit output seems to be in fact 32 bit int (e.g. Prod_Girls.wv) */
/*  if (bits != 32) { */
  srccaps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, wavpackdec->samplerate,
      "channels", G_TYPE_INT, wavpackdec->channels,
      "depth", G_TYPE_INT, bits,
      "width", G_TYPE_INT, bits,
      "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
      "signed", G_TYPE_BOOLEAN, TRUE, NULL);
/*
  } else {
    srccaps = gst_caps_new_simple ("audio/x-raw-float",
        "rate", G_TYPE_INT, wavpackdec->samplerate,
        "channels", G_TYPE_INT, wavpackdec->channels,
        "width", G_TYPE_INT, 32,
        "endianness", G_TYPE_INT, G_LITTLE_ENDIAN, NULL);
  }
*/
/*  gst_pad_set_caps (wavpackdec->sinkpad, caps); */

  gst_pad_set_caps (wavpackdec->srcpad, srccaps);
  gst_pad_use_fixed_caps (wavpackdec->srcpad);

  return TRUE;
}

#if 0
static GstPadLinkReturn
gst_wavpack_dec_wvclink (GstPad * pad, GstPad * peer)
{
  if (!gst_caps_is_fixed (GST_PAD_CAPS (peer)))
    return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;
}
#endif

static void
gst_wavpack_dec_base_init (gpointer klass)
{
  static GstElementDetails plugin_details =
      GST_ELEMENT_DETAILS ("WavePack audio decoder",
      "Codec/Decoder/Audio",
      "Decode Wavpack audio data",
      "Arwed v. Merkatz <v.merkatz@gmx.net>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&wvc_sink_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_wavpack_dec_dispose (GObject * object)
{
  GstWavpackDec *wavpackdec = GST_WAVPACK_DEC (object);

  g_free (wavpackdec->decodebuf);
  wavpackdec->decodebuf = NULL;

  /* FIXME: what about wavpackdec->stream and wavpackdec->context? (tpm) */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_wavpack_dec_class_init (GstWavpackDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->dispose = gst_wavpack_dec_dispose;
}

static gboolean
gst_wavpack_dec_src_query (GstPad * pad, GstQuery * query)
{
  return gst_pad_query_default (pad, query);
}

static gboolean
gst_wavpack_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstWavpackDec *dec;

  dec = GST_WAVPACK_DEC (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (dec, "Received %s event", GST_EVENT_TYPE_NAME (event));
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      /* TODO: save current segment so we can do clipping, for now
       * we'll just leave the clipping to the audio sink */
      break;
    }
    default:
      break;
  }

  gst_object_unref (dec);

  return gst_pad_event_default (pad, event);
}

static void
gst_wavpack_dec_init (GstWavpackDec * wavpackdec, GstWavpackDecClass * gklass)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (wavpackdec);

  wavpackdec->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_element_add_pad (GST_ELEMENT (wavpackdec), wavpackdec->sinkpad);

  gst_pad_set_chain_function (wavpackdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_chain));
  gst_pad_set_setcaps_function (wavpackdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_setcaps));
  gst_pad_set_event_function (wavpackdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_sink_event));

#if 0
  wavpackdec->wvcsinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "wvcsink"), "wvcsink");
  gst_pad_set_link_function (wavpackdec->wvcsinkpad, gst_wavpack_dec_wvclink);
  gst_element_add_pad (GST_ELEMENT (wavpackdec), wavpackdec->wvcsinkpad);
#endif


  wavpackdec->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_use_fixed_caps (wavpackdec->srcpad);

  gst_pad_set_query_function (wavpackdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_src_query));

  gst_element_add_pad (GST_ELEMENT (wavpackdec), wavpackdec->srcpad);

  wavpackdec->decodebuf = NULL;
  wavpackdec->decodebuf_size = 0;
  wavpackdec->stream = (WavpackStream *) g_malloc0 (sizeof (WavpackStream));
  wavpackdec->context = (WavpackContext *) g_malloc0 (sizeof (WavpackContext));
}

static void
gst_wavpack_dec_setup_context (GstWavpackDec * wavpackdec, guchar * data,
    guchar * cdata)
{
  WavpackContext *context = wavpackdec->context;
  WavpackStream *stream = wavpackdec->stream;
  guint buffer_size;

  memset (context, 0, sizeof (context));

  context->open_flags = 0;
  context->current_stream = 0;
  context->num_streams = 1;

  memset (stream, 0, sizeof (stream));
  context->streams[0] = stream;

  gst_wavpack_read_header (&stream->wphdr, data);
  stream->blockbuff = data;

  if (cdata) {
    context->wvc_flag = TRUE;
    gst_wavpack_read_header (&stream->wphdr, cdata);
    stream->block2buff = cdata;
  } else {
    context->wvc_flag = FALSE;
  }

  buffer_size =
      stream->wphdr.block_samples * wavpackdec->channels * sizeof (int32_t);
  if (wavpackdec->decodebuf_size < buffer_size) {
    wavpackdec->decodebuf =
        (int32_t *) g_realloc (wavpackdec->decodebuf, buffer_size);
    wavpackdec->decodebuf_size = buffer_size;
  }

  unpack_init (context);
}

static GstBuffer *
gst_wavpack_dec_format_samples (GstWavpackDec * wavpackdec, int32_t * samples,
    guint num_samples)
{
  GstBuffer *buf;
  gint i;
  guint8 *dst;
  int32_t temp;

  buf =
      gst_buffer_new_and_alloc (num_samples * wavpackdec->width / 8 *
      wavpackdec->channels);

  dst = (guint8 *) GST_BUFFER_DATA (buf);

  switch (wavpackdec->width) {
    case 8:
      for (i = 0; i < num_samples * wavpackdec->channels; ++i)
        *dst++ = *samples++ + 128;
      break;
    case 16:
      for (i = 0; i < num_samples * wavpackdec->channels; ++i) {
        *dst++ = (guint8) (temp = *samples++);
        *dst++ = (guint8) (temp >> 8);
      }
      break;
    case 24:
      for (i = 0; i < num_samples * wavpackdec->channels; ++i) {
        *dst++ = (guint8) (temp = *samples++);
        *dst++ = (guint8) (temp >> 8);
        *dst++ = (guint8) (temp >> 16);
      }
      break;
    case 32:
      for (i = 0; i < num_samples * wavpackdec->channels; ++i) {
        *dst++ = (guint8) (temp = *samples++);
        *dst++ = (guint8) (temp >> 8);
        *dst++ = (guint8) (temp >> 16);
        *dst++ = (guint8) (temp >> 24);
      }
      break;
    default:
      break;
  }

  return buf;
}

static GstFlowReturn
gst_wavpack_dec_chain (GstPad * pad, GstBuffer * buf)
{

  GstWavpackDec *wavpackdec = GST_WAVPACK_DEC (gst_pad_get_parent (pad));
  GstBuffer *outbuf, *cbuf = NULL;
  GstFlowReturn ret = GST_FLOW_OK;

#if 0
  if (gst_pad_is_linked (wavpackdec->wvcsinkpad)) {
    if (GST_FLOW_OK != gst_pad_pull_range (wavpackdec->wvcsinkpad,
            wavpackdec->wvcflushed_bytes, -1, &cbuf)) {
      cbuf = NULL;
    } else {
      wavpackdec->wvcflushed_bytes += GST_BUFFER_SIZE (cbuf);
    }

  }
#endif

  gst_wavpack_dec_setup_context (wavpackdec, GST_BUFFER_DATA (buf),
      cbuf ? GST_BUFFER_DATA (cbuf) : NULL);
  unpack_samples (wavpackdec->context, wavpackdec->decodebuf,
      wavpackdec->context->streams[0]->wphdr.block_samples);
  outbuf =
      gst_wavpack_dec_format_samples (wavpackdec, wavpackdec->decodebuf,
      wavpackdec->context->streams[0]->wphdr.block_samples);

  gst_buffer_stamp (outbuf, buf);

  gst_buffer_unref (buf);
  if (cbuf) {
    gst_buffer_unref (cbuf);
  }

  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (wavpackdec->srcpad));

  GST_LOG_OBJECT (wavpackdec, "pushing buffer with time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));

  ret = gst_pad_push (wavpackdec->srcpad, outbuf);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (wavpackdec, "pad_push: %s", gst_flow_get_name (ret));
  }

  return ret;
}

gboolean
gst_wavpack_dec_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "wavpackdec",
          GST_RANK_PRIMARY, GST_TYPE_WAVPACK_DEC))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_wavpack_dec_debug, "wavpackdec", 0,
      "wavpack decoder");

  return TRUE;
}
