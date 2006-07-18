/* GStreamer Wavpack plugin
 * Copyright (c) 2005 Arwed v. Merkatz <v.merkatz@gmx.net>
 * Copyright (c) 2006 Edward Hervey <bilboed@gmail.com>
 * Copyright (c) 2006 Sebastian Dröge <slomo@circular-chaos.org>
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
#include <gst/audio/audio.h>

#include <math.h>
#include <string.h>

#include <wavpack/wavpack.h>
#include "gstwavpackdec.h"
#include "gstwavpackcommon.h"
#include "gstwavpackstreamreader.h"


#define WAVPACK_DEC_MAX_ERRORS 16

GST_DEBUG_CATEGORY_STATIC (gst_wavpack_dec_debug);
#define GST_CAT_DEFAULT gst_wavpack_dec_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "width = (int) { 8, 16, 24, 32 }, "
        "channels = (int) [ 1, 2 ], "
        "rate = (int) [ 6000, 192000 ], " "framed = (boolean) true")
    );

#if 0
static GstStaticPadTemplate wvc_sink_factory =
GST_STATIC_PAD_TEMPLATE ("wvcsink",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/x-wavpack-correction, " "framed = (boolean) true")
    );
#endif

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) { 8, 16, 24, 32 }, "
        "depth = (int) { 8, 16, 24, 32 }, "
        "channels = (int) [ 1, 2 ], "
        "rate = (int) [ 6000, 192000 ], "
        "endianness = (int) LITTLE_ENDIAN, " "signed = (boolean) true")
    );

static GstFlowReturn gst_wavpack_dec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_wavpack_dec_sink_event (GstPad * pad, GstEvent * event);
static void gst_wavpack_dec_finalize (GObject * object);
static GstStateChangeReturn gst_wavpack_dec_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_wavpack_dec_sink_event (GstPad * pad, GstEvent * event);

#if 0
static GstPad *gst_wavpack_dec_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
#endif

GST_BOILERPLATE (GstWavpackDec, gst_wavpack_dec, GstElement, GST_TYPE_ELEMENT);

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
  static const GstElementDetails plugin_details =
      GST_ELEMENT_DETAILS ("WavePack audio decoder",
      "Codec/Decoder/Audio",
      "Decode Wavpack audio data",
      "Arwed v. Merkatz <v.merkatz@gmx.net>, "
      "Sebastian Dröge <slomo@circular-chaos.org>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
#if 0
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&wvc_sink_factory));
#endif
  gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_wavpack_dec_class_init (GstWavpackDecClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_change_state);
#if 0
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_request_new_pad);
#endif
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_wavpack_dec_finalize);
}

static void
gst_wavpack_dec_init (GstWavpackDec * wavpackdec, GstWavpackDecClass * gklass)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (wavpackdec);

  wavpackdec->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_chain_function (wavpackdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_chain));
  gst_pad_set_event_function (wavpackdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_sink_event));
  gst_element_add_pad (GST_ELEMENT (wavpackdec), wavpackdec->sinkpad);

  wavpackdec->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_use_fixed_caps (wavpackdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (wavpackdec), wavpackdec->srcpad);

  wavpackdec->context = NULL;
  wavpackdec->stream_reader = gst_wavpack_stream_reader_new ();

  wavpackdec->wv_id.buffer = NULL;
  wavpackdec->wv_id.position = wavpackdec->wv_id.length = 0;

/*
  wavpackdec->wvc_id.buffer = NULL;
  wavpackdec->wvc_id.position = wavpackdec->wvc_id.length = 0;
  wavpackdec->wvcsinkpad = NULL;
*/

  wavpackdec->error_count = 0;


  wavpackdec->channels = 0;
  wavpackdec->sample_rate = 0;
  wavpackdec->width = 0;

  gst_segment_init (&wavpackdec->segment, GST_FORMAT_UNDEFINED);
}

static void
gst_wavpack_dec_finalize (GObject * object)
{
  GstWavpackDec *wavpackdec = GST_WAVPACK_DEC (object);

  g_free (wavpackdec->stream_reader);
  wavpackdec->stream_reader = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_wavpack_dec_format_samples (GstWavpackDec * wavpackdec, guint8 * dst,
    int32_t * samples, guint num_samples)
{
  gint i;
  int32_t temp;

  switch (wavpackdec->width) {
    case 8:
      for (i = 0; i < num_samples * wavpackdec->channels; ++i)
        *dst++ = (guint8) (*samples++);
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
}

static gboolean
gst_wavpack_dec_clip_outgoing_buffer (GstWavpackDec * wavpackdec,
    GstBuffer * buf)
{
  gint64 start, stop, cstart, cstop, diff;

  if (wavpackdec->segment.format != GST_FORMAT_TIME)
    return TRUE;

  start = GST_BUFFER_TIMESTAMP (buf);
  stop = start + GST_BUFFER_DURATION (buf);

  if (gst_segment_clip (&wavpackdec->segment, GST_FORMAT_TIME,
          start, stop, &cstart, &cstop)) {

    diff = cstart - start;
    if (diff > 0) {
      GST_BUFFER_TIMESTAMP (buf) = cstart;
      GST_BUFFER_DURATION (buf) -= diff;

      diff = ((wavpackdec->width + 7) >> 3) * wavpackdec->channels
          * GST_CLOCK_TIME_TO_FRAMES (diff, wavpackdec->sample_rate);
      GST_BUFFER_DATA (buf) += diff;
      GST_BUFFER_SIZE (buf) -= diff;
    }

    diff = cstop - stop;
    if (diff > 0) {
      GST_BUFFER_DURATION (buf) -= diff;

      diff = ((wavpackdec->width + 7) >> 3) * wavpackdec->channels
          * GST_CLOCK_TIME_TO_FRAMES (diff, wavpackdec->sample_rate);
      GST_BUFFER_SIZE (buf) -= diff;
    }
  } else {
    GST_DEBUG_OBJECT (wavpackdec, "buffer is outside configured segment");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_wavpack_dec_chain (GstPad * pad, GstBuffer * buf)
{

  GstWavpackDec *wavpackdec;
  GstBuffer *outbuf;
  GstBuffer *cbuf = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  WavpackHeader wph;
  int32_t *unpack_buf;
  int32_t unpacked_sample_count;

  wavpackdec = GST_WAVPACK_DEC (GST_PAD_PARENT (pad));

  /* we only accept framed input with complete chunks */
  g_assert (GST_BUFFER_SIZE (buf) >= sizeof (WavpackHeader));
  gst_wavpack_read_header (&wph, GST_BUFFER_DATA (buf));
  g_assert (GST_BUFFER_SIZE (buf) ==
      wph.ckSize + 4 * sizeof (char) + sizeof (uint32_t));

  wavpackdec->wv_id.buffer = GST_BUFFER_DATA (buf);
  wavpackdec->wv_id.length = GST_BUFFER_SIZE (buf);
  wavpackdec->wv_id.position = 0;

#if 0
  /* check whether the correction pad is linked and we can get
   * the correction chunk that corresponds to our current data */
  if (gst_pad_is_linked (wavpackdec->wvcsinkpad)) {
    if (GST_FLOW_OK != gst_pad_pull_range (wavpackdec->wvcsinkpad,
            GST_BUFFER_OFFSET (buf), -1, &cbuf)) {
      cbuf = NULL;
    } else {
      /* this won't work (tpm) */
      if (!(GST_BUFFER_TIMESTAMP (cbuf) == GST_BUFFER_TIMESTAMP (buf)) ||
          !(GST_BUFFER_DURATION (cbuf) == GST_BUFFER_DURATION (buf)) ||
          !(GST_BUFFER_OFFSET (cbuf) == GST_BUFFER_OFFSET (buf)) ||
          !(GST_BUFFER_OFFSET_END (cbuf) == GST_BUFFER_OFFSET (buf))) {
        gst_buffer_unref (cbuf);
        cbuf = NULL;
      } else {
        wavpackdec->wvc_id.buffer = GST_BUFFER_DATA (cbuf);
        wavpackdec->wvc_id.length = GST_BUFFER_SIZE (cbuf);
        wavpackdec->wvc_id.position = 0;
      }
    }
  }
#endif

  /* create a new wavpack context if there is none yet but if there
   * was already one (i.e. caps were set on the srcpad) check whether
   * the new one has the same caps */
  if (!wavpackdec->context) {
    gchar error_msg[80];

/*
    wavpackdec->context =
        WavpackOpenFileInputEx (wavpackdec->stream_reader, &wavpackdec->wv_id,
        (cbuf) ? &wavpackdec->wvc_id : NULL, error_msg, OPEN_STREAMING, 0);
*/

    wavpackdec->context = WavpackOpenFileInputEx (wavpackdec->stream_reader,
        &wavpackdec->wv_id, NULL, error_msg, OPEN_STREAMING, 0);

    if (!wavpackdec->context) {
      wavpackdec->error_count++;
      GST_ELEMENT_WARNING (wavpackdec, LIBRARY, INIT, (NULL),
          ("Couldn't open buffer for decoding: %s", error_msg));
      if (wavpackdec->error_count <= WAVPACK_DEC_MAX_ERRORS) {
        ret = GST_FLOW_OK;
      } else {
        ret = GST_FLOW_ERROR;
      }
      gst_buffer_unref (buf);
      if (cbuf) {
        gst_buffer_unref (cbuf);
      }
      return ret;
    }

    if (GST_PAD_CAPS (wavpackdec->srcpad)) {
      if ((wavpackdec->sample_rate !=
              WavpackGetSampleRate (wavpackdec->context))
          || (wavpackdec->channels !=
              WavpackGetNumChannels (wavpackdec->context))
          || (wavpackdec->width !=
              WavpackGetBitsPerSample (wavpackdec->context))) {
        gst_buffer_unref (buf);
        if (cbuf) {
          gst_buffer_unref (cbuf);
        }

        /* FIXME: use the right error */
        GST_ELEMENT_ERROR (wavpackdec, LIBRARY, INIT, (NULL),
            ("Got Wavpack chunk with changed format settings!"));
        return GST_FLOW_ERROR;
      }
    }
  }
  wavpackdec->error_count = 0;

  if (!GST_PAD_CAPS (wavpackdec->srcpad)) {
    GstCaps *caps;

    g_assert (wavpackdec->context);

    wavpackdec->sample_rate = WavpackGetSampleRate (wavpackdec->context);
    wavpackdec->channels = WavpackGetNumChannels (wavpackdec->context);
    wavpackdec->width = WavpackGetBitsPerSample (wavpackdec->context);
    caps = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT, wavpackdec->sample_rate,
        "channels", G_TYPE_INT, wavpackdec->channels,
        "depth", G_TYPE_INT, wavpackdec->width,
        "width", G_TYPE_INT, wavpackdec->width,
        "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
        "signed", G_TYPE_BOOLEAN, TRUE, NULL);

    if (!gst_pad_set_caps (wavpackdec->srcpad, caps)) {
      gst_caps_unref (caps);
      WavpackCloseFile (wavpackdec->context);
      wavpackdec->context = NULL;
      gst_buffer_unref (buf);
      if (cbuf) {
        gst_buffer_unref (cbuf);
      }
      /* FIXME: use the right error */
      GST_ELEMENT_ERROR (wavpackdec, LIBRARY, INIT, (NULL),
          ("Couldn't set caps on source pad: %" GST_PTR_FORMAT, caps));
      return GST_FLOW_ERROR;
    }
    gst_caps_unref (caps);
    gst_pad_use_fixed_caps (wavpackdec->srcpad);
  }

  g_assert (wavpackdec->context);
  unpack_buf =
      (int32_t *) g_malloc (sizeof (int32_t) * wph.block_samples *
      wavpackdec->channels);
  unpacked_sample_count =
      WavpackUnpackSamples (wavpackdec->context, unpack_buf, wph.block_samples);
  g_assert (unpacked_sample_count == wph.block_samples);

  ret =
      gst_pad_alloc_buffer_and_set_caps (wavpackdec->srcpad,
      GST_BUFFER_OFFSET (buf),
      wph.block_samples * ((wavpackdec->width +
              7) >> 3) * wavpackdec->channels,
      GST_PAD_CAPS (wavpackdec->srcpad), &outbuf);

  if (GST_FLOW_IS_FATAL (ret)) {
    WavpackCloseFile (wavpackdec->context);
    wavpackdec->context = NULL;
    g_free (unpack_buf);
    gst_buffer_unref (buf);
    if (cbuf) {
      gst_buffer_unref (cbuf);
    }
    return ret;
  } else if (ret != GST_FLOW_OK) {
    g_free (unpack_buf);
    gst_buffer_unref (buf);
    if (cbuf) {
      gst_buffer_unref (cbuf);
    }
    return ret;
  }

  gst_wavpack_dec_format_samples (wavpackdec, GST_BUFFER_DATA (outbuf),
      unpack_buf, wph.block_samples);
  g_free (unpack_buf);
  gst_buffer_stamp (outbuf, buf);
  gst_buffer_unref (buf);
  if (cbuf) {
    gst_buffer_unref (cbuf);
  }

  if (gst_wavpack_dec_clip_outgoing_buffer (wavpackdec, outbuf)) {
    GST_LOG_OBJECT (wavpackdec, "pushing buffer with time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));
    ret = gst_pad_push (wavpackdec->srcpad, outbuf);
    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (wavpackdec, "pad_push: %s", gst_flow_get_name (ret));
    }
  } else {
    gst_buffer_unref (outbuf);
  }

  return ret;
}

static gboolean
gst_wavpack_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstWavpackDec *wavpackdec = GST_WAVPACK_DEC (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (wavpackdec, "Received %s event", GST_EVENT_TYPE_NAME (event));
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      GstFormat fmt;
      gboolean is_update;
      gint64 start, end, base;
      gdouble rate;

      gst_event_parse_new_segment (event, &is_update, &rate, &fmt, &start,
          &end, &base);
      if (fmt == GST_FORMAT_TIME) {
        GST_DEBUG ("Got NEWSEGMENT event in GST_FORMAT_TIME, passing on (%"
            GST_TIME_FORMAT " - %" GST_TIME_FORMAT ")", GST_TIME_ARGS (start),
            GST_TIME_ARGS (end));
        gst_segment_set_newsegment (&wavpackdec->segment, is_update, rate, fmt,
            start, end, base);
      } else {
        gst_segment_init (&wavpackdec->segment, GST_FORMAT_UNDEFINED);
      }
      break;
    }
    default:
      break;
  }

  gst_object_unref (wavpackdec);
  return gst_pad_event_default (pad, event);
}

static GstStateChangeReturn
gst_wavpack_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstWavpackDec *wavpackdec = GST_WAVPACK_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_segment_init (&wavpackdec->segment, GST_FORMAT_UNDEFINED);
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
      if (wavpackdec->context) {
        WavpackCloseFile (wavpackdec->context);
        wavpackdec->context = NULL;
      }
      wavpackdec->wv_id.buffer = NULL;
      wavpackdec->wv_id.position = 0;
      wavpackdec->wv_id.length = 0;
      /*
         wavpackdec->wvc_id.buffer = NULL;
         wavpackdec->wvc_id.position = 0;
         wavpackdec->wvc_id.length = 0;
         wavpackdec->error_count = 0;
         wavpackdec->wvcsinkpad = NULL;
       */
      wavpackdec->channels = 0;
      wavpackdec->sample_rate = 0;
      wavpackdec->width = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

#if 0
static GstPad *
gst_wavpack_dec_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name)
{
  GstWavpackDec *wavpackdec = GST_WAVPACK_DEC (element);
  GstPad *pad;

  if (wavpackdec->wvcsinkpad == NULL) {
    wavpackdec->wvcsinkpad = gst_pad_new_from_template (template, name);
    gst_pad_set_link_function (wavpackdec->wvcsinkpad, gst_wavpack_dec_wvclink);
    gst_pad_use_fixed_caps (wavpackdec->wvcsinkpad);
    gst_element_add_pad (GST_ELEMENT (wavpackdec), wavpackdec->wvcsinkpad);
    gst_element_no_more_pads (GST_ELEMENT (wavpackdec));
  } else {
    pad = NULL;
  }

  return pad;
}
#endif

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
