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

/* Filter signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "width = (int) { 8, 16, 24, 32 }, "
        "channels = (int) { 1, 2 }, "
        "rate = (int) [ 8000, 96000 ], " "framed = (boolean) true")
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
        "width = (int) { 8, 16, 24 }, "
        "depth = (int) { 8, 16, 24 }, "
        "channels = (int) { 1, 2 }, "
        "rate = (int) [ 8000, 96000 ], "
        "endianness = (int) LITTLE_ENDIAN, "
        "signed = (boolean) true;"
        "audio/x-raw-float, "
        "width = (int) 32, "
        "channels = (int) { 1, 2 }, "
        "rate = (int) [ 8000, 96000 ], "
        "endianness = (int) LITTLE_ENDIAN, " "buffer-frames = (int) 0")
    );

static void gst_wavpack_dec_class_init (GstWavpackDecClass * klass);
static void gst_wavpack_dec_base_init (GstWavpackDecClass * klass);
static void gst_wavpack_dec_init (GstWavpackDec * wavpackdec);

static void gst_wavpack_dec_loop (GstElement * element);

static GstElementClass *parent = NULL;

static GstPadLinkReturn
gst_wavpack_dec_link (GstPad * pad, const GstCaps * caps)
{
  GstWavpackDec *wavpackdec = GST_WAVPACK_DEC (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstCaps *srccaps;
  gint bits;

  if (!gst_caps_is_fixed (caps))
    return GST_PAD_LINK_DELAYED;

  gst_structure_get_int (structure, "rate",
      (gint32 *) & wavpackdec->samplerate);
  gst_structure_get_int (structure, "channels",
      (gint *) & wavpackdec->channels);
  gst_structure_get_int (structure, "width", &bits);
  wavpackdec->width = bits;

  if (bits != 32) {
    srccaps = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT, wavpackdec->samplerate,
        "channels", G_TYPE_INT, wavpackdec->channels,
        "depth", G_TYPE_INT, bits,
        "width", G_TYPE_INT, bits,
        "endianness", G_TYPE_INT, LITTLE_ENDIAN,
        "signed", G_TYPE_BOOLEAN, TRUE, NULL);
  } else {
    srccaps = gst_caps_new_simple ("audio/x-raw-float",
        "rate", G_TYPE_INT, wavpackdec->samplerate,
        "channels", G_TYPE_INT, wavpackdec->channels,
        "width", G_TYPE_INT, 32,
        "endianness", G_TYPE_INT, LITTLE_ENDIAN,
        "buffer-frames", G_TYPE_INT, 0, NULL);
  }

  gst_pad_set_explicit_caps (wavpackdec->srcpad, srccaps);

  // blocks are usually 0.5 seconds long, assume that's always the case for now
  wavpackdec->decodebuf =
      (int32_t *) g_malloc ((wavpackdec->samplerate / 2) *
      wavpackdec->channels * sizeof (int32_t));
  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_wavpack_dec_wvclink (GstPad * pad, const GstCaps * caps)
{
  if (!gst_caps_is_fixed (caps))
    return GST_PAD_LINK_DELAYED;

  return GST_PAD_LINK_OK;
}

GType
gst_wavpack_dec_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    static const GTypeInfo plugin_info = {
      sizeof (GstWavpackDecClass),
      (GBaseInitFunc) gst_wavpack_dec_base_init,
      NULL,
      (GClassInitFunc) gst_wavpack_dec_class_init,
      NULL,
      NULL,
      sizeof (GstWavpackDec),
      0,
      (GInstanceInitFunc) gst_wavpack_dec_init,
    };
    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstWavpackDec", &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_wavpack_dec_base_init (GstWavpackDecClass * klass)
{
  static GstElementDetails plugin_details = {
    "WAVPACK decoder",
    "Codec/Decoder/Audio",
    "Decode Wavpack audio data",
    "Arwed v. Merkatz <v.merkatz@gmx.net>"
  };
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

  G_OBJECT_CLASS (parent)->dispose (object);
}

static void
gst_wavpack_dec_class_init (GstWavpackDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->dispose = gst_wavpack_dec_dispose;
}

static gboolean
gst_wavpack_dec_src_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  return gst_pad_query_default (pad, type, format, value);
}

static void
gst_wavpack_dec_init (GstWavpackDec * wavpackdec)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (wavpackdec);

  wavpackdec->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_link_function (wavpackdec->sinkpad, gst_wavpack_dec_link);

  wavpackdec->wvcsinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "wvcsink"), "wvcsink");
  gst_pad_set_link_function (wavpackdec->wvcsinkpad, gst_wavpack_dec_wvclink);

  wavpackdec->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_use_explicit_caps (wavpackdec->srcpad);

  gst_pad_set_query_function (wavpackdec->srcpad, gst_wavpack_dec_src_query);

  gst_element_add_pad (GST_ELEMENT (wavpackdec), wavpackdec->sinkpad);
  gst_element_add_pad (GST_ELEMENT (wavpackdec), wavpackdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (wavpackdec), wavpackdec->wvcsinkpad);
  gst_element_set_loop_function (GST_ELEMENT (wavpackdec),
      gst_wavpack_dec_loop);

  wavpackdec->decodebuf = NULL;
  wavpackdec->stream = (WavpackStream *) g_malloc0 (sizeof (WavpackStream));
  wavpackdec->context = (WavpackContext *) g_malloc0 (sizeof (WavpackContext));
}

static void
gst_wavpack_dec_setup_context (GstWavpackDec * wavpackdec, guchar * data,
    guchar * cdata)
{
  WavpackContext *context = wavpackdec->context;
  WavpackStream *stream = wavpackdec->stream;

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


static void
gst_wavpack_dec_loop (GstElement * element)
{
  GstWavpackDec *wavpackdec = GST_WAVPACK_DEC (element);
  GstData *data, *cdata = NULL;
  GstBuffer *buf, *outbuf, *cbuf = NULL;

  gboolean got_event = FALSE;

  if (!gst_pad_is_linked (wavpackdec->sinkpad)) {
    return;
  }

  /*
     if (!gst_pad_is_linked (wavpackdec->wvcsinkpad)) {
     return;
     }
   */

  data = gst_pad_pull (wavpackdec->sinkpad);

  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        gst_event_unref (event);
        gst_pad_push (wavpackdec->srcpad,
            GST_DATA (gst_event_new (GST_EVENT_EOS)));
        gst_element_set_eos (element);
        break;
      default:
        gst_pad_event_default (wavpackdec->srcpad, event);
        break;
    }
    got_event = TRUE;
  }

  if (gst_pad_is_linked (wavpackdec->wvcsinkpad)) {
    cdata = gst_pad_pull (wavpackdec->wvcsinkpad);
    if (GST_IS_EVENT (cdata)) {
      GstEvent *event = GST_EVENT (cdata);

      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_EOS:
          gst_event_unref (event);
          gst_pad_push (wavpackdec->srcpad,
              GST_DATA (gst_event_new (GST_EVENT_EOS)));
          gst_element_set_eos (element);
          break;
        default:
          gst_pad_event_default (wavpackdec->srcpad, event);
          break;
      }
      got_event = TRUE;
    } else {
      cbuf = GST_BUFFER (cdata);
    }
  }

  if (got_event)
    return;

  buf = GST_BUFFER (data);

  gst_wavpack_dec_setup_context (wavpackdec, GST_BUFFER_DATA (buf),
      cbuf ? GST_BUFFER_DATA (cbuf) : NULL);
  unpack_samples (wavpackdec->context, wavpackdec->decodebuf,
      wavpackdec->context->streams[0]->wphdr.block_samples);
  outbuf =
      gst_wavpack_dec_format_samples (wavpackdec, wavpackdec->decodebuf,
      wavpackdec->context->streams[0]->wphdr.block_samples);

  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);
  gst_buffer_unref (buf);

  gst_pad_push (wavpackdec->srcpad, GST_DATA (outbuf));
}

gboolean
gst_wavpack_dec_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "wavpackdec",
      GST_RANK_PRIMARY, GST_TYPE_WAVPACK_DEC);
}
