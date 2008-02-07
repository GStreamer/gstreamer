/*
 * GStreamer DirectShow codecs wrapper
 * Copyright <2006, 2007, 2008> Fluendo <gstreamer@fluendo.com>
 * Copyright <2006, 2007, 2008> Pioneers of the Inevitable <songbird@songbirdnest.com>
 * Copyright <2007,2008> Sebastien Moutte <sebastien@moutte.net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdshowaudiodec.h"
#include <mmreg.h>

GST_DEBUG_CATEGORY_STATIC (dshowaudiodec_debug);
#define GST_CAT_DEFAULT dshowaudiodec_debug

GST_BOILERPLATE (GstDshowAudioDec, gst_dshowaudiodec, GstElement,
    GST_TYPE_ELEMENT);
static const CodecEntry *tmp;

static void gst_dshowaudiodec_dispose (GObject * object);
static GstStateChangeReturn gst_dshowaudiodec_change_state
    (GstElement * element, GstStateChange transition);

/* sink pad overwrites */
static gboolean gst_dshowaudiodec_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_dshowaudiodec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_dshowaudiodec_sink_event (GstPad * pad, GstEvent * event);

/* callback used by directshow to push buffers */
static gboolean gst_dshowaudiodec_push_buffer (byte * buffer, long size,
    byte * src_object, UINT64 start, UINT64 stop);

/* utils */
static gboolean gst_dshowaudiodec_create_graph_and_filters (GstDshowAudioDec *
    adec);
static gboolean gst_dshowaudiodec_destroy_graph_and_filters (GstDshowAudioDec *
    adec);
static gboolean gst_dshowaudiodec_flush (GstDshowAudioDec * adec);
static gboolean gst_dshowaudiodec_get_filter_settings (GstDshowAudioDec * adec);
static gboolean gst_dshowaudiodec_setup_graph (GstDshowAudioDec * adec);

/* gobal variable */
const long bitrates[2][3][16] = {
  /* version 0 */
  {
        /* one list per layer 1-3 */
        {0, 32000, 48000, 56000, 64000, 80000, 96000, 112000, 128000, 144000,
            160000, 176000, 192000, 224000, 256000, 0},
        {0, 8000, 16000, 24000, 32000, 40000, 48000, 56000, 64000, 80000, 96000,
            112000, 128000, 144000, 160000, 0},
        {0, 8000, 16000, 24000, 32000, 40000, 48000, 56000, 64000, 80000, 96000,
            112000, 128000, 144000, 160000, 0},
      },
  /* version 1 */
  {
        /* one list per layer 1-3 */
        {0, 32000, 64000, 96000, 128000, 160000, 192000, 224000, 256000,
            288000, 320000, 352000, 384000, 416000, 448000, 0},
        {0, 32000, 48000, 56000, 64000, 80000, 96000, 112000, 128000,
            160000, 192000, 224000, 256000, 320000, 384000, 0},
        {0, 32000, 40000, 48000, 56000, 64000, 80000, 96000, 112000,
            128000, 160000, 192000, 224000, 256000, 320000, 0},
      }
};

#define GUID_MEDIATYPE_AUDIO    {0x73647561, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_PCM   {0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_WMAV1 {0x00000160, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_WMAV2 {0x00000161, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_WMAV3 {0x00000162, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_WMAV4 {0x00000163, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_WMS   {0x0000000a, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_MP3   {0x00000055, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_MPEG1AudioPayload {0x00000050, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9b, 0x71 }}

static const CodecEntry audio_dec_codecs[] = {
  {"dshowadec_wma1",
        "Windows Media Audio 7",
        "DMO",
        0x00000160,
        GUID_MEDIATYPE_AUDIO, GUID_MEDIASUBTYPE_WMAV1,
        "audio/x-wma, wmaversion = (int) 1",
        GUID_MEDIATYPE_AUDIO, GUID_MEDIASUBTYPE_PCM,
        "audio/x-raw-int, "
        "width = (int) { 1, 8, 16 }, depth = (int) { 1, 8, 16 }, "
        "signed = (boolean) true, endianness = (int) "
        G_STRINGIFY (G_LITTLE_ENDIAN)
      },
  {"dshowadec_wma2",
        "Windows Media Audio 8",
        "DMO",
        0x00000161,
        GUID_MEDIATYPE_AUDIO, GUID_MEDIASUBTYPE_WMAV2,
        "audio/x-wma, wmaversion = (int) 2",
        GUID_MEDIATYPE_AUDIO, GUID_MEDIASUBTYPE_PCM,
        "audio/x-raw-int, "
        "width = (int) { 1, 8, 16 }, depth = (int) { 1, 8, 16 }, "
        "signed = (boolean) true, endianness = (int) "
        G_STRINGIFY (G_LITTLE_ENDIAN)
      },
  {"dshowadec_wma3",
        "Windows Media Audio 9 Professional",
        "DMO",
        0x00000162,
        GUID_MEDIATYPE_AUDIO, GUID_MEDIASUBTYPE_WMAV3,
        "audio/x-wma, wmaversion = (int) 3",
        GUID_MEDIATYPE_AUDIO, GUID_MEDIASUBTYPE_PCM,
        "audio/x-raw-int, "
        "width = (int) { 1, 8, 16 }, depth = (int) { 1, 8, 16 }, "
        "signed = (boolean) true, endianness = (int) "
        G_STRINGIFY (G_LITTLE_ENDIAN)
      },
  {"dshowadec_wma4",
        "Windows Media Audio 9 Lossless",
        "DMO",
        0x00000163,
        GUID_MEDIATYPE_AUDIO, GUID_MEDIASUBTYPE_WMAV4,
        "audio/x-wma, wmaversion = (int) 4",
        GUID_MEDIATYPE_AUDIO, GUID_MEDIASUBTYPE_PCM,
        "audio/x-raw-int, "
        "width = (int) { 1, 8, 16 }, depth = (int) { 1, 8, 16 }, "
        "signed = (boolean) true, endianness = (int) "
        G_STRINGIFY (G_LITTLE_ENDIAN)
      },
  {"dshowadec_wms",
        "Windows Media Audio Voice v9",
        "DMO",
        0x0000000a,
        GUID_MEDIATYPE_AUDIO, GUID_MEDIASUBTYPE_WMS,
        "audio/x-wms",
        GUID_MEDIATYPE_AUDIO, GUID_MEDIASUBTYPE_PCM,
        "audio/x-raw-int, "
        "width = (int) { 1, 8, 16 }, depth = (int) { 1, 8, 16 }, "
        "signed = (boolean) true, endianness = (int) "
        G_STRINGIFY (G_LITTLE_ENDIAN)
      },
  {"dshowadec_mpeg1",
        "MPEG-1 Layer 1,2,3 Audio",
        "MPEG Layer-3 Decoder",
        0x00000055,
        GUID_MEDIATYPE_AUDIO, GUID_MEDIASUBTYPE_MP3,
        "audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) { 1 , 2, 3 }, "
        "rate = (int) [ 8000, 48000 ], "
        "channels = (int) [ 1, 2 ], " "parsed= (boolean) true",
        GUID_MEDIATYPE_AUDIO, GUID_MEDIASUBTYPE_PCM,
        "audio/x-raw-int, "
        "width = (int) { 1, 8, 16 }, depth = (int) { 1, 8, 16 }, "
        "signed = (boolean) true, endianness = (int) "
        G_STRINGIFY (G_LITTLE_ENDIAN)
      }
};

/* Private map used when dshowadec_mpeg is loaded with layer=1 or 2.
 * The problem is that gstreamer don't care about caps like layer when connecting pads.
 * So I've only one element handling mpeg audio in the public codecs map and 
 * when it's loaded for mp3, I'm releasing mpeg audio decoder and replace it by 
 * the one described in this private map.
*/
static const CodecEntry audio_mpeg_1_2[] = { "dshowadec_mpeg_1_2",
  "MPEG-1 Layer 1,2 Audio",
  "MPEG Audio Decoder",
  0x00000050,
  GUID_MEDIATYPE_AUDIO, GUID_MEDIASUBTYPE_MPEG1AudioPayload,
  "audio/mpeg, "
      "mpegversion = (int) 1, "
      "layer = (int) [ 1, 2 ], "
      "rate = (int) [ 8000, 48000 ], "
      "channels = (int) [ 1, 2 ], " "parsed= (boolean) true",
  GUID_MEDIATYPE_AUDIO, GUID_MEDIASUBTYPE_PCM,
  "audio/x-raw-int, "
      "width = (int) { 1, 8, 16 }, depth = (int) { 1, 8, 16 }, "
      "signed = (boolean) true, endianness = (int) "
      G_STRINGIFY (G_LITTLE_ENDIAN)
};

static void
gst_dshowaudiodec_base_init (GstDshowAudioDecClass * klass)
{
  GstPadTemplate *src, *sink;
  GstCaps *srccaps, *sinkcaps;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstElementDetails details;

  klass->entry = tmp;
  details.longname = g_strdup_printf ("DirectShow %s Decoder Wrapper",
      tmp->element_longname);
  details.klass = g_strdup ("Codec/Decoder/Audio");
  details.description = g_strdup_printf ("DirectShow %s Decoder Wrapper",
      tmp->element_longname);
  details.author = "Sebastien Moutte <sebastien@moutte.net>";
  gst_element_class_set_details (element_class, &details);
  g_free (details.longname);
  g_free (details.klass);
  g_free (details.description);

  sinkcaps = gst_caps_from_string (tmp->sinkcaps);
  gst_caps_set_simple (sinkcaps,
      "block_align", GST_TYPE_INT_RANGE, 0, G_MAXINT,
      "bitrate", GST_TYPE_INT_RANGE, 0, G_MAXINT, NULL);

  srccaps = gst_caps_from_string (tmp->srccaps);

  sink = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sinkcaps);
  src = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);

  /* register */
  gst_element_class_add_pad_template (element_class, src);
  gst_element_class_add_pad_template (element_class, sink);
}

static void
gst_dshowaudiodec_class_init (GstDshowAudioDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_dshowaudiodec_dispose);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_dshowaudiodec_change_state);

  if (!parent_class)
    parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  if (!dshowaudiodec_debug) {
    GST_DEBUG_CATEGORY_INIT (dshowaudiodec_debug, "dshowaudiodec", 0,
        "Directshow filter audio decoder");
  }
}

static void
gst_dshowaudiodec_init (GstDshowAudioDec * adec,
    GstDshowAudioDecClass * adec_class)
{
  GstElementClass *element_class = GST_ELEMENT_GET_CLASS (adec);

  /* setup pads */
  adec->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (element_class, "sink"), "sink");

  gst_pad_set_setcaps_function (adec->sinkpad, gst_dshowaudiodec_sink_setcaps);
  gst_pad_set_event_function (adec->sinkpad, gst_dshowaudiodec_sink_event);
  gst_pad_set_chain_function (adec->sinkpad, gst_dshowaudiodec_chain);
  gst_element_add_pad (GST_ELEMENT (adec), adec->sinkpad);

  adec->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (element_class, "src"), "src");
  gst_element_add_pad (GST_ELEMENT (adec), adec->srcpad);

  adec->srcfilter = NULL;
  adec->gstdshowsrcfilter = NULL;
  adec->decfilter = NULL;
  adec->sinkfilter = NULL;
  adec->filtergraph = NULL;
  adec->mediafilter = NULL;
  adec->timestamp = GST_CLOCK_TIME_NONE;
  adec->segment = gst_segment_new ();
  adec->setup = FALSE;
  adec->depth = 0;
  adec->bitrate = 0;
  adec->block_align = 0;
  adec->channels = 0;
  adec->rate = 0;
  adec->layer = 0;
  adec->codec_data = NULL;

  CoInitializeEx (NULL, COINIT_MULTITHREADED);
}

static void
gst_dshowaudiodec_dispose (GObject * object)
{
  GstDshowAudioDec *adec = (GstDshowAudioDec *) (object);

  if (adec->segment) {
    gst_segment_free (adec->segment);
    adec->segment = NULL;
  }

  if (adec->codec_data) {
    gst_buffer_unref (adec->codec_data);
    adec->codec_data = NULL;
  }

  CoUninitialize ();
}


static GstStateChangeReturn
gst_dshowaudiodec_change_state (GstElement * element, GstStateChange transition)
{
  GstDshowAudioDec *adec = (GstDshowAudioDec *) (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_dshowaudiodec_create_graph_and_filters (adec))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      adec->depth = 0;
      adec->bitrate = 0;
      adec->block_align = 0;
      adec->channels = 0;
      adec->rate = 0;
      adec->layer = 0;
      if (adec->codec_data) {
        gst_buffer_unref (adec->codec_data);
        adec->codec_data = NULL;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_dshowaudiodec_destroy_graph_and_filters (adec))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static gboolean
gst_dshowaudiodec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  gboolean ret = FALSE;
  GstDshowAudioDec *adec = (GstDshowAudioDec *) gst_pad_get_parent (pad);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const GValue *v = NULL;

  adec->timestamp = GST_CLOCK_TIME_NONE;

  /* read data, only rate and channels are needed */
  if (!gst_structure_get_int (s, "rate", &adec->rate) ||
      !gst_structure_get_int (s, "channels", &adec->channels)) {
    GST_ELEMENT_ERROR (adec, CORE, NEGOTIATION,
        ("error getting audio specs from caps"), (NULL));
    goto end;
  }

  gst_structure_get_int (s, "depth", &adec->depth);
  gst_structure_get_int (s, "bitrate", &adec->bitrate);
  gst_structure_get_int (s, "block_align", &adec->block_align);
  gst_structure_get_int (s, "layer", &adec->layer);

  if (adec->codec_data) {
    gst_buffer_unref (adec->codec_data);
    adec->codec_data = NULL;
  }

  if ((v = gst_structure_get_value (s, "codec_data")))
    adec->codec_data = gst_buffer_ref (gst_value_get_buffer (v));

  if (adec->layer != 1 && adec->layer != 2) {
    /* setup dshow graph for all formats except for 
     * MPEG-1 layer 1 and 2 for which we need negociate
     * in _chain function.
     */
    ret = gst_dshowaudiodec_setup_graph (adec);
  }

  ret = TRUE;
end:
  gst_object_unref (adec);

  return ret;
}

static GstFlowReturn
gst_dshowaudiodec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstDshowAudioDec *adec = (GstDshowAudioDec *) gst_pad_get_parent (pad);
  gboolean discount = FALSE;

  if (!adec->setup) {
    if (adec->layer != 0) {
      if (adec->codec_data) {
        gst_buffer_unref (adec->codec_data);
        adec->codec_data = NULL;
      }
      /* extract the 3 bytes of MPEG-1 audio frame header */
      adec->codec_data = gst_buffer_create_sub (buffer, 1, 3);
    }

    /* setup dshow graph */
    if (!gst_dshowaudiodec_setup_graph (adec)) {
      return GST_FLOW_ERROR;
    }
  }

  if (!adec->gstdshowsrcfilter) {
    /* we are not setup */
    ret = GST_FLOW_WRONG_STATE;
    goto beach;
  }

  GST_CAT_DEBUG_OBJECT (dshowaudiodec_debug, adec, "chain (size %d)=> pts %"
      GST_TIME_FORMAT " stop %" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (buffer), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer) +
          GST_BUFFER_DURATION (buffer)));

  /* if the incoming buffer has discont flag set => flush decoder data */
  if (buffer && GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    GST_CAT_DEBUG_OBJECT (dshowaudiodec_debug, adec,
        "this buffer has a DISCONT flag (%" GST_TIME_FORMAT "), flushing",
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));
    gst_dshowaudiodec_flush (adec);
    discount = TRUE;
  }

  /* push the buffer to the directshow decoder */
  IGstDshowInterface_gst_push_buffer (adec->gstdshowsrcfilter,
      GST_BUFFER_DATA (buffer), GST_BUFFER_TIMESTAMP (buffer),
      GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer),
      GST_BUFFER_SIZE (buffer), discount);

beach:
  gst_buffer_unref (buffer);
  gst_object_unref (adec);
  return ret;
}

static gboolean
gst_dshowaudiodec_push_buffer (byte * buffer, long size, byte * src_object,
    UINT64 dshow_start, UINT64 dshow_stop)
{
  GstDshowAudioDec *adec = (GstDshowAudioDec *) src_object;
  GstBuffer *out_buf = NULL;
  gboolean in_seg = FALSE;
  gint64 buf_start, buf_stop;
  gint64 clip_start = 0, clip_stop = 0;
  size_t start_offset = 0, stop_offset = size;

  if (!GST_CLOCK_TIME_IS_VALID (adec->timestamp)) {
    adec->timestamp = dshow_start;
  }

  buf_start = adec->timestamp;
  buf_stop = adec->timestamp + (dshow_stop - dshow_start);

  /* save stop position to start next buffer with it */
  adec->timestamp = buf_stop;

  /* check if this buffer is in our current segment */
  in_seg = gst_segment_clip (adec->segment, GST_FORMAT_TIME,
      buf_start, buf_stop, &clip_start, &clip_stop);

  /* if the buffer is out of segment do not push it downstream */
  if (!in_seg) {
    GST_CAT_DEBUG_OBJECT (dshowaudiodec_debug, adec,
        "buffer is out of segment, start %" GST_TIME_FORMAT " stop %"
        GST_TIME_FORMAT, GST_TIME_ARGS (buf_start), GST_TIME_ARGS (buf_stop));
    return FALSE;
  }

  /* buffer is in our segment allocate a new out buffer and clip it if needed */

  /* allocate a new buffer for raw audio */
  gst_pad_alloc_buffer (adec->srcpad, GST_BUFFER_OFFSET_NONE,
      size, GST_PAD_CAPS (adec->srcpad), &out_buf);
  if (!out_buf) {
    GST_CAT_ERROR_OBJECT (dshowaudiodec_debug, adec,
        "can't not allocate a new GstBuffer");
    return FALSE;
  }

  /* set buffer properties */
  GST_BUFFER_SIZE (out_buf) = size;
  GST_BUFFER_TIMESTAMP (out_buf) = buf_start;
  GST_BUFFER_DURATION (out_buf) = buf_stop - buf_start;
  memcpy (GST_BUFFER_DATA (out_buf), buffer, size);

  /* we have to remove some heading samples */
  if (clip_start > buf_start) {
    start_offset = (size_t) gst_util_uint64_scale_int (clip_start - buf_start,
        adec->rate, GST_SECOND) * adec->depth / 8 * adec->channels;
  }
  /* we have to remove some trailing samples */
  if (clip_stop < buf_stop) {
    stop_offset = (size_t) gst_util_uint64_scale_int (buf_stop - clip_stop,
        adec->rate, GST_SECOND) * adec->depth / 8 * adec->channels;
  }

  /* truncating */
  if ((start_offset != 0) || (stop_offset != (size_t) size)) {
    GstBuffer *subbuf = gst_buffer_create_sub (out_buf, start_offset,
        stop_offset - start_offset);

    if (subbuf) {
      gst_buffer_set_caps (subbuf, GST_PAD_CAPS (adec->srcpad));
      gst_buffer_unref (out_buf);
      out_buf = subbuf;
    }
  }

  GST_BUFFER_TIMESTAMP (out_buf) = clip_start;
  GST_BUFFER_DURATION (out_buf) = clip_stop - clip_start;

  /* replace the saved stop position by the clipped one */
  adec->timestamp = clip_stop;

  GST_CAT_DEBUG_OBJECT (dshowaudiodec_debug, adec,
      "push_buffer (size %d)=> pts %" GST_TIME_FORMAT " stop %" GST_TIME_FORMAT
      " duration %" GST_TIME_FORMAT, size,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (out_buf)),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (out_buf) +
          GST_BUFFER_DURATION (out_buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (out_buf)));

  gst_pad_push (adec->srcpad, out_buf);

  return TRUE;
}

static gboolean
gst_dshowaudiodec_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = TRUE;
  GstDshowAudioDec *adec = (GstDshowAudioDec *) gst_pad_get_parent (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:{
      gst_dshowaudiodec_flush (adec);
      ret = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment (event, &update, &rate, &format, &start,
          &stop, &time);

      GST_CAT_DEBUG_OBJECT (dshowaudiodec_debug, adec,
          "received new segment from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
          GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

      if (update) {
        GST_CAT_DEBUG_OBJECT (dshowaudiodec_debug, adec,
            "closing current segment flushing..");
        gst_dshowaudiodec_flush (adec);
      }

      /* save the new segment in our local current segment */
      gst_segment_set_newsegment (adec->segment, update, rate, format, start,
          stop, time);

      ret = gst_pad_event_default (pad, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }
  return ret;
}

static gboolean
gst_dshowaudiodec_flush (GstDshowAudioDec * adec)
{
  if (!adec->gstdshowsrcfilter)
    return FALSE;

  /* flush dshow decoder and reset timestamp */
  IGstDshowInterface_gst_flush (adec->gstdshowsrcfilter);
  adec->timestamp = GST_CLOCK_TIME_NONE;

  return TRUE;
}


static gboolean
gst_dshowaudiodec_setup_graph (GstDshowAudioDec * adec)
{
  gboolean ret = FALSE;
  GstDshowAudioDecClass *klass =
      (GstDshowAudioDecClass *) G_OBJECT_GET_CLASS (adec);
  HRESULT hres;
  gint size = 0;
  GstCaps *out;
  AM_MEDIA_TYPE output_mediatype, input_mediatype;
  WAVEFORMATEX *input_format = NULL, output_format;
  IPin *output_pin = NULL, *input_pin = NULL;
  IGstDshowInterface *gstdshowinterface = NULL;
  CodecEntry *codec_entry = klass->entry;

  if (adec->layer != 0) {
    if (adec->layer == 1 || adec->layer == 2) {
      /* for MPEG-1 layer 1 or 2 we have to release the current 
       * MP3 decoder and create an instance of MPEG Audio Decoder
       */
      IBaseFilter_Release (adec->decfilter);
      adec->decfilter = NULL;
      codec_entry = audio_mpeg_1_2;
      gst_dshow_find_filter (codec_entry->input_majortype,
          codec_entry->input_subtype,
          codec_entry->output_majortype,
          codec_entry->output_subtype,
          codec_entry->prefered_filter_substring, &adec->decfilter);
      IFilterGraph_AddFilter (adec->filtergraph, adec->decfilter, L"decoder");
    } else {
      /* mp3 don't need to negociate with MPEG1WAVEFORMAT */
      adec->layer = 0;
    }
  }

  /* set mediatype on fakesrc filter output pin */
  memset (&input_mediatype, 0, sizeof (AM_MEDIA_TYPE));
  input_mediatype.majortype = codec_entry->input_majortype;
  input_mediatype.subtype = codec_entry->input_subtype;
  input_mediatype.bFixedSizeSamples = TRUE;
  input_mediatype.bTemporalCompression = FALSE;
  if (adec->block_align)
    input_mediatype.lSampleSize = adec->block_align;
  else
    input_mediatype.lSampleSize = 8192; /* need to evaluate it dynamically */
  input_mediatype.formattype = FORMAT_WaveFormatEx;

  if (adec->layer != 0) {
    MPEG1WAVEFORMAT *mpeg1_format;
    BYTE b1, b2, b3;
    gint samples, version, layer;

    size = sizeof (MPEG1WAVEFORMAT);
    input_format = g_malloc0 (size);
    input_format->cbSize = sizeof (MPEG1WAVEFORMAT) - sizeof (WAVEFORMATEX);
    mpeg1_format = (MPEG1WAVEFORMAT *) input_format;

    /* initialize header bytes */
    b1 = *GST_BUFFER_DATA (adec->codec_data);
    b2 = *(GST_BUFFER_DATA (adec->codec_data) + 1);
    b3 = *(GST_BUFFER_DATA (adec->codec_data) + 2);

    /* fill MPEG1WAVEFORMAT using header */
    input_format->wFormatTag = WAVE_FORMAT_MPEG;
    mpeg1_format->wfx.nChannels = 2;
    switch (b3 >> 6) {
      case 0x00:
        mpeg1_format->fwHeadMode = ACM_MPEG_STEREO;
        break;
      case 0x01:
        mpeg1_format->fwHeadMode = ACM_MPEG_JOINTSTEREO;
        break;
      case 0x02:
        mpeg1_format->fwHeadMode = ACM_MPEG_DUALCHANNEL;
        break;
      case 0x03:
        mpeg1_format->fwHeadMode = ACM_MPEG_SINGLECHANNEL;
        mpeg1_format->wfx.nChannels = 1;
        break;
    }

    mpeg1_format->fwHeadModeExt = (WORD) (1 << (b3 >> 4));
    mpeg1_format->wHeadEmphasis = (WORD) ((b3 & 0x03) + 1);
    mpeg1_format->fwHeadFlags = (WORD) (((b2 & 1) ? ACM_MPEG_PRIVATEBIT : 0) +
        ((b3 & 8) ? ACM_MPEG_COPYRIGHT : 0) +
        ((b3 & 4) ? ACM_MPEG_ORIGINALHOME : 0) +
        ((b1 & 1) ? ACM_MPEG_PROTECTIONBIT : 0) + ACM_MPEG_ID_MPEG1);

    layer = (b1 >> 1) & 3;
    switch (layer) {
      case 1:
        mpeg1_format->fwHeadLayer = ACM_MPEG_LAYER3;
        layer = 3;
        break;
      case 2:
        mpeg1_format->fwHeadLayer = ACM_MPEG_LAYER2;
        break;
      case 3:
        mpeg1_format->fwHeadLayer = ACM_MPEG_LAYER1;
        layer = 1;
        break;
    };

    version = (b1 >> 3) & 1;
    if (layer == 1) {
      samples = 384;
    } else {
      if (version == 0) {
        samples = 576;
      } else {
        samples = 1152;
      }
    }
    mpeg1_format->wfx.nBlockAlign = (WORD) samples;
    mpeg1_format->wfx.nSamplesPerSec = adec->rate;
    mpeg1_format->dwHeadBitrate = bitrates[version][layer - 1][b2 >> 4];
    mpeg1_format->wfx.nAvgBytesPerSec = mpeg1_format->dwHeadBitrate / 8;
  } else {
    size = sizeof (WAVEFORMATEX) +
        (adec->codec_data ? GST_BUFFER_SIZE (adec->codec_data) : 0);
    input_format = g_malloc0 (size);
    if (adec->codec_data) {     /* Codec data is appended after our header */
      memcpy (((guchar *) input_format) + sizeof (WAVEFORMATEX),
          GST_BUFFER_DATA (adec->codec_data),
          GST_BUFFER_SIZE (adec->codec_data));
      input_format->cbSize = GST_BUFFER_SIZE (adec->codec_data);
    }

    input_format->wFormatTag = codec_entry->format;
    input_format->nChannels = adec->channels;
    input_format->nSamplesPerSec = adec->rate;
    input_format->nAvgBytesPerSec = adec->bitrate / 8;
    input_format->nBlockAlign = adec->block_align;
    input_format->wBitsPerSample = adec->depth;
  }

  input_mediatype.cbFormat = size;
  input_mediatype.pbFormat = (BYTE *) input_format;

  hres = IBaseFilter_QueryInterface (adec->srcfilter, &IID_IGstDshowInterface,
      (void **) &gstdshowinterface);
  if (hres != S_OK || !gstdshowinterface) {
    GST_ELEMENT_ERROR (adec, CORE, NEGOTIATION,
        ("Can't get IGstDshowInterface interface from dshow fakesrc filter (error=%d)",
            hres), (NULL));
    goto end;
  }

  /* save a reference to IGstDshowInterface to use it processing functions */
  if (!adec->gstdshowsrcfilter) {
    adec->gstdshowsrcfilter = gstdshowinterface;
    IBaseFilter_AddRef (adec->gstdshowsrcfilter);
  }

  IGstDshowInterface_gst_set_media_type (gstdshowinterface, &input_mediatype);
  IGstDshowInterface_Release (gstdshowinterface);
  gstdshowinterface = NULL;

  /* connect our fake source to decoder */
  gst_dshow_get_pin_from_filter (adec->srcfilter, PINDIR_OUTPUT, &output_pin);
  if (!output_pin) {
    GST_ELEMENT_ERROR (adec, CORE, NEGOTIATION,
        ("Can't get output pin from our directshow fakesrc filter"), (NULL));
    goto end;
  }
  gst_dshow_get_pin_from_filter (adec->decfilter, PINDIR_INPUT, &input_pin);
  if (!input_pin) {
    GST_ELEMENT_ERROR (adec, CORE, NEGOTIATION,
        ("Can't get input pin from decoder filter"), (NULL));
    goto end;
  }

  hres =
      IFilterGraph_ConnectDirect (adec->filtergraph, output_pin, input_pin,
      NULL);
  if (hres != S_OK) {
    GST_ELEMENT_ERROR (adec, CORE, NEGOTIATION,
        ("Can't connect fakesrc with decoder (error=%d)", hres), (NULL));
    goto end;
  }

  IPin_Release (input_pin);
  IPin_Release (output_pin);
  input_pin = NULL;
  output_pin = NULL;

  if (!gst_dshowaudiodec_get_filter_settings (adec)) {
    GST_ELEMENT_ERROR (adec, CORE, NEGOTIATION,
        ("Can't get audio depth from decoder"), (NULL));
    goto end;
  }

  /* set mediatype on fake sink input pin */
  memset (&output_format, 0, sizeof (WAVEFORMATEX));
  output_format.wFormatTag = WAVE_FORMAT_PCM;
  output_format.wBitsPerSample = adec->depth;
  output_format.nChannels = adec->channels;
  output_format.nBlockAlign = adec->channels * (adec->depth / 8);
  output_format.nSamplesPerSec = adec->rate;
  output_format.nAvgBytesPerSec = output_format.nBlockAlign * adec->rate;

  memset (&output_mediatype, 0, sizeof (AM_MEDIA_TYPE));
  output_mediatype.majortype = codec_entry->output_majortype;
  output_mediatype.subtype = codec_entry->output_subtype;
  output_mediatype.bFixedSizeSamples = TRUE;
  output_mediatype.bTemporalCompression = FALSE;
  output_mediatype.lSampleSize = output_format.nBlockAlign;
  output_mediatype.formattype = FORMAT_WaveFormatEx;
  output_mediatype.cbFormat = sizeof (WAVEFORMATEX);
  output_mediatype.pbFormat = (char *) &output_format;

  hres = IBaseFilter_QueryInterface (adec->sinkfilter, &IID_IGstDshowInterface,
      (void **) &gstdshowinterface);
  if (hres != S_OK || !gstdshowinterface) {
    GST_ELEMENT_ERROR (adec, CORE, NEGOTIATION,
        ("Can't get IGstDshowInterface interface from dshow fakesink filter (error=%d)",
            hres), (NULL));
    goto end;
  }

  IGstDshowInterface_gst_set_media_type (gstdshowinterface, &output_mediatype);
  IGstDshowInterface_gst_set_buffer_callback (gstdshowinterface,
      gst_dshowaudiodec_push_buffer, (byte *) adec);
  IGstDshowInterface_Release (gstdshowinterface);
  gstdshowinterface = NULL;

  /* negotiate output */
  out = gst_caps_from_string (codec_entry->srccaps);
  gst_caps_set_simple (out,
      "width", G_TYPE_INT, adec->depth,
      "depth", G_TYPE_INT, adec->depth,
      "rate", G_TYPE_INT, adec->rate,
      "channels", G_TYPE_INT, adec->channels, NULL);
  if (!gst_pad_set_caps (adec->srcpad, out)) {
    gst_caps_unref (out);
    GST_ELEMENT_ERROR (adec, CORE, NEGOTIATION,
        ("Failed to negotiate output"), (NULL));
    goto end;
  }
  gst_caps_unref (out);

  /* connect the decoder to our fake sink */
  gst_dshow_get_pin_from_filter (adec->decfilter, PINDIR_OUTPUT, &output_pin);
  if (!output_pin) {
    GST_ELEMENT_ERROR (adec, CORE, NEGOTIATION,
        ("Can't get output pin from our decoder filter"), (NULL));
    goto end;
  }
  gst_dshow_get_pin_from_filter (adec->sinkfilter, PINDIR_INPUT, &input_pin);
  if (!input_pin) {
    GST_ELEMENT_ERROR (adec, CORE, NEGOTIATION,
        ("Can't get input pin from our directshow fakesink filter"), (NULL));
    goto end;
  }

  hres =
      IFilterGraph_ConnectDirect (adec->filtergraph, output_pin, input_pin,
      NULL);
  if (hres != S_OK) {
    GST_ELEMENT_ERROR (adec, CORE, NEGOTIATION,
        ("Can't connect decoder with fakesink (error=%d)", hres), (NULL));
    goto end;
  }

  hres = IMediaFilter_Run (adec->mediafilter, -1);
  if (hres != S_OK) {
    GST_ELEMENT_ERROR (adec, CORE, NEGOTIATION,
        ("Can't run the directshow graph (error=%d)", hres), (NULL));
    goto end;
  }

  ret = TRUE;
  adec->setup = TRUE;
end:
  gst_object_unref (adec);
  if (input_format)
    g_free (input_format);
  if (gstdshowinterface)
    IGstDshowInterface_Release (gstdshowinterface);
  if (input_pin)
    IPin_Release (input_pin);
  if (output_pin)
    IPin_Release (output_pin);

  return ret;
}

static gboolean
gst_dshowaudiodec_get_filter_settings (GstDshowAudioDec * adec)
{
  IPin *output_pin = NULL;
  IEnumMediaTypes *enum_mediatypes = NULL;
  HRESULT hres;
  ULONG fetched;
  BOOL ret = FALSE;

  if (!adec->decfilter)
    return FALSE;

  if (!gst_dshow_get_pin_from_filter (adec->decfilter, PINDIR_OUTPUT,
          &output_pin)) {
    GST_ELEMENT_ERROR (adec, CORE, NEGOTIATION,
        ("failed getting ouput pin from the decoder"), (NULL));
    return FALSE;
  }

  hres = IPin_EnumMediaTypes (output_pin, &enum_mediatypes);
  if (hres == S_OK && enum_mediatypes) {
    AM_MEDIA_TYPE *mediatype = NULL;

    IEnumMediaTypes_Reset (enum_mediatypes);
    while (hres =
        IEnumMoniker_Next (enum_mediatypes, 1, &mediatype, &fetched),
        hres == S_OK) {
      RPC_STATUS rpcstatus;

      if ((UuidCompare (&mediatype->subtype, &MEDIASUBTYPE_PCM, &rpcstatus) == 0
              && rpcstatus == RPC_S_OK) &&
          (UuidCompare (&mediatype->formattype, &FORMAT_WaveFormatEx,
                  &rpcstatus) == 0 && rpcstatus == RPC_S_OK)) {
        WAVEFORMATEX *audio_info = (WAVEFORMATEX *) mediatype->pbFormat;

        adec->channels = audio_info->nChannels;
        adec->depth = audio_info->wBitsPerSample;
        adec->rate = audio_info->nSamplesPerSec;
        ret = TRUE;
      }
      gst_dshow_free_mediatype (mediatype);
      if (ret)
        break;
    }
    IEnumMediaTypes_Release (enum_mediatypes);
  }
  if (output_pin) {
    IPin_Release (output_pin);
  }

  return ret;
}

static gboolean
gst_dshowaudiodec_create_graph_and_filters (GstDshowAudioDec * adec)
{
  BOOL ret = FALSE;
  HRESULT hres = S_FALSE;
  GstDshowAudioDecClass *klass =
      (GstDshowAudioDecClass *) G_OBJECT_GET_CLASS (adec);

  /* create the filter graph manager object */
  hres = CoCreateInstance (&CLSID_FilterGraph, NULL, CLSCTX_INPROC,
      &IID_IFilterGraph, (LPVOID *) & adec->filtergraph);
  if (hres != S_OK || !adec->filtergraph) {
    GST_ELEMENT_ERROR (adec, STREAM, FAILED,
        ("Can't create an instance of the directshow graph manager (error=%d)",
            hres), (NULL));
    goto error;
  }

  hres = IFilterGraph_QueryInterface (adec->filtergraph, &IID_IMediaFilter,
      (void **) &adec->mediafilter);
  if (hres != S_OK || !adec->mediafilter) {
    GST_ELEMENT_ERROR (adec, STREAM, FAILED,
        ("Can't get IMediacontrol interface from the graph manager (error=%d)",
            hres), (NULL));
    goto error;
  }

  /* create fake src filter */
  hres = CoCreateInstance (&CLSID_DshowFakeSrc, NULL, CLSCTX_INPROC,
      &IID_IBaseFilter, (LPVOID *) & adec->srcfilter);
  if (hres != S_OK || !adec->srcfilter) {
    GST_ELEMENT_ERROR (adec, STREAM, FAILED,
        ("Can't create an instance of the directshow fakesrc (error=%d)", hres),
        (NULL));
    goto error;
  }

  /* create decoder filter */
  if (!gst_dshow_find_filter (klass->entry->input_majortype,
          klass->entry->input_subtype,
          klass->entry->output_majortype,
          klass->entry->output_subtype,
          klass->entry->prefered_filter_substring, &adec->decfilter)) {
    GST_ELEMENT_ERROR (adec, STREAM, FAILED,
        ("Can't create an instance of the decoder filter"), (NULL));
    goto error;
  }

  /* create fake sink filter */
  hres = CoCreateInstance (&CLSID_DshowFakeSink, NULL, CLSCTX_INPROC,
      &IID_IBaseFilter, (LPVOID *) & adec->sinkfilter);
  if (hres != S_OK || !adec->sinkfilter) {
    GST_ELEMENT_ERROR (adec, STREAM, FAILED,
        ("Can't create an instance of the directshow fakesink (error=%d)",
            hres), (NULL));
    goto error;
  }

  /* add filters to the graph */
  hres = IFilterGraph_AddFilter (adec->filtergraph, adec->srcfilter, L"src");
  if (hres != S_OK) {
    GST_ELEMENT_ERROR (adec, STREAM, FAILED,
        ("Can't add fakesrc filter to the graph (error=%d)", hres), (NULL));
    goto error;
  }

  hres =
      IFilterGraph_AddFilter (adec->filtergraph, adec->decfilter, L"decoder");
  if (hres != S_OK) {
    GST_ELEMENT_ERROR (adec, STREAM, FAILED,
        ("Can't add decoder filter to the graph (error=%d)", hres), (NULL));
    goto error;
  }

  hres = IFilterGraph_AddFilter (adec->filtergraph, adec->sinkfilter, L"sink");
  if (hres != S_OK) {
    GST_ELEMENT_ERROR (adec, STREAM, FAILED,
        ("Can't add fakesink filter to the graph (error=%d)", hres), (NULL));
    goto error;
  }

  return TRUE;

error:
  if (adec->srcfilter) {
    IBaseFilter_Release (adec->srcfilter);
    adec->srcfilter = NULL;
  }
  if (adec->decfilter) {
    IBaseFilter_Release (adec->decfilter);
    adec->decfilter = NULL;
  }
  if (adec->sinkfilter) {
    IBaseFilter_Release (adec->sinkfilter);
    adec->sinkfilter = NULL;
  }
  if (adec->mediafilter) {
    IMediaFilter_Release (adec->mediafilter);
    adec->mediafilter = NULL;
  }
  if (adec->filtergraph) {
    IFilterGraph_Release (adec->filtergraph);
    adec->filtergraph = NULL;
  }

  return FALSE;
}

static gboolean
gst_dshowaudiodec_destroy_graph_and_filters (GstDshowAudioDec * adec)
{
  if (adec->mediafilter) {
    IMediaFilter_Stop (adec->mediafilter);
  }

  if (adec->gstdshowsrcfilter) {
    IGstDshowInterface_Release (adec->gstdshowsrcfilter);
    adec->gstdshowsrcfilter = NULL;
  }
  if (adec->srcfilter) {
    if (adec->filtergraph)
      IFilterGraph_RemoveFilter (adec->filtergraph, adec->srcfilter);
    IBaseFilter_Release (adec->srcfilter);
    adec->srcfilter = NULL;
  }
  if (adec->decfilter) {
    if (adec->filtergraph)
      IFilterGraph_RemoveFilter (adec->filtergraph, adec->decfilter);
    IBaseFilter_Release (adec->decfilter);
    adec->decfilter = NULL;
  }
  if (adec->sinkfilter) {
    if (adec->filtergraph)
      IFilterGraph_RemoveFilter (adec->filtergraph, adec->sinkfilter);
    IBaseFilter_Release (adec->sinkfilter);
    adec->sinkfilter = NULL;
  }
  if (adec->mediafilter) {
    IMediaFilter_Release (adec->mediafilter);
    adec->mediafilter = NULL;
  }
  if (adec->filtergraph) {
    IFilterGraph_Release (adec->filtergraph);
    adec->filtergraph = NULL;
  }

  adec->setup = FALSE;

  return TRUE;
}

gboolean
dshow_adec_register (GstPlugin * plugin)
{
  GTypeInfo info = {
    sizeof (GstDshowAudioDecClass),
    (GBaseInitFunc) gst_dshowaudiodec_base_init,
    NULL,
    (GClassInitFunc) gst_dshowaudiodec_class_init,
    NULL,
    NULL,
    sizeof (GstDshowAudioDec),
    0,
    (GInstanceInitFunc) gst_dshowaudiodec_init,
  };
  gint i;

  GST_DEBUG_CATEGORY_INIT (dshowaudiodec_debug, "dshowaudiodec", 0,
      "Directshow filter audio decoder");

  CoInitializeEx (NULL, COINIT_MULTITHREADED);
  for (i = 0; i < sizeof (audio_dec_codecs) / sizeof (CodecEntry); i++) {
    GType type;

    if (gst_dshow_find_filter (audio_dec_codecs[i].input_majortype,
            audio_dec_codecs[i].input_subtype,
            audio_dec_codecs[i].output_majortype,
            audio_dec_codecs[i].output_subtype,
            audio_dec_codecs[i].prefered_filter_substring, NULL)) {

      GST_CAT_DEBUG (dshowaudiodec_debug, "Registering %s",
          audio_dec_codecs[i].element_name);

      tmp = &audio_dec_codecs[i];
      type =
          g_type_register_static (GST_TYPE_ELEMENT,
          audio_dec_codecs[i].element_name, &info, 0);
      if (!gst_element_register (plugin, audio_dec_codecs[i].element_name,
              GST_RANK_PRIMARY, type)) {
        return FALSE;
      }
      GST_CAT_DEBUG (dshowaudiodec_debug, "Registered %s",
          audio_dec_codecs[i].element_name);
    } else {
      GST_CAT_DEBUG (dshowaudiodec_debug,
          "Element %s not registered (the format is not supported by the system)",
          audio_dec_codecs[i].element_name);
    }
  }

  CoUninitialize ();
  return TRUE;
}
