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

#include "gstdshowvideodec.h"

GST_DEBUG_CATEGORY_STATIC (dshowvideodec_debug);
#define GST_CAT_DEFAULT dshowvideodec_debug

GST_BOILERPLATE (GstDshowVideoDec, gst_dshowvideodec, GstElement,
    GST_TYPE_ELEMENT);
static const CodecEntry *tmp;

static void gst_dshowvideodec_dispose (GObject * object);
static GstStateChangeReturn gst_dshowvideodec_change_state
    (GstElement * element, GstStateChange transition);

/* sink pad overwrites */
static gboolean gst_dshowvideodec_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_dshowvideodec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_dshowvideodec_chain (GstPad * pad, GstBuffer * buffer);

/* src pad overwrites */
static GstCaps *gst_dshowvideodec_src_getcaps (GstPad * pad);
static gboolean gst_dshowvideodec_src_setcaps (GstPad * pad, GstCaps * caps);

/* callback called by our directshow fake sink when it receives a buffer */
static gboolean gst_dshowvideodec_push_buffer (byte * buffer, long size,
    byte * src_object, UINT64 start, UINT64 stop);

/* utils */
static gboolean gst_dshowvideodec_create_graph_and_filters (GstDshowVideoDec *
    vdec);
static gboolean gst_dshowvideodec_destroy_graph_and_filters (GstDshowVideoDec *
    vdec);
static gboolean gst_dshowvideodec_flush (GstDshowVideoDec * adec);
static gboolean gst_dshowvideodec_get_filter_output_format (GstDshowVideoDec *
    vdec, GUID * subtype, VIDEOINFOHEADER ** format, guint * size);


#define GUID_MEDIATYPE_VIDEO    {0x73646976, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_WMVV1 {0x31564d57, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_WMVV2 {0x32564d57, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_WMVV3 {0x33564d57, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_WMVP  {0x50564d57, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_WMVA  {0x41564d57, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_CVID  {0x64697663, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_MP4S  {0x5334504d, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_MP42  {0x3234504d, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_MP43  {0x3334504d, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_M4S2  {0x3253344d, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_XVID  {0x44495658, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_DX50  {0x30355844, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_DIVX  {0x58564944, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_DIV3  {0x33564944, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}

#define GUID_MEDIASUBTYPE_MPG4          {0x3447504d, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_MPEG1Payload  {0xe436eb81, 0x524f, 0x11ce, {0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70}}


/* output types */
#define GUID_MEDIASUBTYPE_YUY2    {0x32595559, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_YV12    {0x32315659, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }}
#define GUID_MEDIASUBTYPE_RGB32   {0xe436eb7e, 0x524f, 0x11ce, { 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 }}
#define GUID_MEDIASUBTYPE_RGB565  {0xe436eb7b, 0x524f, 0x11ce, { 0x9f, 0x53, 0x00, 0x20, 0xaf, 0x0b, 0xa7, 0x70 }}

/* video codecs array */
static const CodecEntry video_dec_codecs[] = {
  {"dshowvdec_wmv1",
        "Windows Media Video 7",
        "DMO",
        GST_MAKE_FOURCC ('W', 'M', 'V', '1'),
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_WMVV1,
        "video/x-wmv, wmvversion = (int) 1",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
      "video/x-raw-yuv, format=(fourcc)YUY2"},
  {"dshowvdec_wmv2",
        "Windows Media Video 8",
        "DMO",
        GST_MAKE_FOURCC ('W', 'M', 'V', '2'),
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_WMVV2,
        "video/x-wmv, wmvversion = (int) 2",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
      "video/x-raw-yuv, format=(fourcc)YUY2"},
  {"dshowvdec_wmv3",
        "Windows Media Video 9",
        "DMO",
        GST_MAKE_FOURCC ('W', 'M', 'V', '3'),
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_WMVV3,
        "video/x-wmv, wmvversion = (int) 3, " "format = (fourcc) WMV3",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
      "video/x-raw-yuv, format=(fourcc)YUY2"},
  {"dshowvdec_wmvp",
        "Windows Media Video 9 Image",
        "DMO",
        GST_MAKE_FOURCC ('W', 'M', 'V', 'P'),
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_WMVP,
        "video/x-wmv, wmvversion = (int) 3, " "format = (fourcc) WMVP",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
      "video/x-raw-yuv, format=(fourcc)YUY2"},
  {"dshowvdec_wmva",
        "Windows Media Video 9 Advanced",
        "DMO",
        GST_MAKE_FOURCC ('W', 'M', 'V', 'A'),
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_WMVA,
        "video/x-wmv, wmvversion = (int) 3, " "format = (fourcc) WMVA",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
      "video/x-raw-yuv, format=(fourcc)YUY2"},
  {"dshowvdec_cinepak",
        "Cinepack",
        "AVI Decompressor",
        0x64697663,
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_CVID,
        "video/x-cinepak",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_RGB32,
      "video/x-raw-rgb, bpp=(int)32, depth=(int)24, "
        "endianness=(int)4321, red_mask=(int)65280, "
        "green_mask=(int)16711680, blue_mask=(int)-16777216"},
  {"dshowvdec_msmpeg41",
        "Microsoft ISO MPEG-4 version 1",
        "DMO",
        GST_MAKE_FOURCC ('M', 'P', '4', 'S'),
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_MP4S,
        "video/x-msmpeg, msmpegversion=(int)41",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
      "video/x-raw-yuv, format=(fourcc)YUY2"},
  {"dshowvdec_msmpeg42",
        "Microsoft ISO MPEG-4 version 2",
        "DMO",
        GST_MAKE_FOURCC ('M', 'P', '4', '2'),
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_MP42,
        "video/x-msmpeg, msmpegversion=(int)42",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
      "video/x-raw-yuv, format=(fourcc)YUY2"},
  {"dshowvdec_msmpeg43",
        "Microsoft ISO MPEG-4 version 3",
        "DMO",
        GST_MAKE_FOURCC ('M', 'P', '4', '3'),
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_MP43,
        "video/x-msmpeg, msmpegversion=(int)43",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
      "video/x-raw-yuv, format=(fourcc)YUY2"},
  {"dshowvdec_msmpeg4",
        "Microsoft ISO MPEG-4 version 1.1",
        "DMO",
        GST_MAKE_FOURCC ('M', '4', 'S', '2'),
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_M4S2,
        "video/x-msmpeg, msmpegversion=(int)4",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
      "video/x-raw-yuv, format=(fourcc)YUY2"},
  {"dshowvdec_mpeg1",
        "MPEG-1 Video",
        "MPEG Video Decoder",
        GST_MAKE_FOURCC ('M', 'P', 'E', 'G'),
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_MPEG1Payload,
        "video/mpeg, mpegversion= (int) 1, "
        "parsed= (boolean) true, " "systemstream= (boolean) false",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
      "video/x-raw-yuv, format=(fourcc)YUY2"},
  {"dshowvdec_xvid",
        "XVID Video",
        "ffdshow",
        GST_MAKE_FOURCC ('X', 'V', 'I', 'D'),
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_XVID,
        "video/x-xvid",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
      "video/x-raw-yuv, format=(fourcc)YUY2"},
  {"dshowvdec_divx5",
        "DIVX 5.0 Video",
        "ffdshow",
        GST_MAKE_FOURCC ('D', 'X', '5', '0'),
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_DX50,
        "video/x-divx, divxversion=(int)5",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
      "video/x-raw-yuv, format=(fourcc)YUY2"},
  {"dshowvdec_divx4",
        "DIVX 4.0 Video",
        "ffdshow",
        GST_MAKE_FOURCC ('D', 'I', 'V', 'X'),
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_DIVX,
        "video/x-divx, divxversion=(int)4",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
      "video/x-raw-yuv, format=(fourcc)YUY2"},
  {"dshowvdec_divx3",
        "DIVX 3.0 Video",
        "ffdshow",
        GST_MAKE_FOURCC ('D', 'I', 'V', '3'),
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_DIV3,
        "video/x-divx, divxversion=(int)3",
        GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
      "video/x-raw-yuv, format=(fourcc)YUY2"}
  /*,
     { "dshowvdec_mpeg4",
     "DMO",
     GST_MAKE_FOURCC ('M', 'P', 'G', '4'),
     GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_MPG4,
     "video/mpeg, msmpegversion=(int)4",
     GUID_MEDIATYPE_VIDEO, GUID_MEDIASUBTYPE_YUY2,
     "video/x-raw-yuv, format=(fourcc)YUY2"
     } */
};

static void
gst_dshowvideodec_base_init (GstDshowVideoDecClass * klass)
{
  GstPadTemplate *src, *sink;
  GstCaps *srccaps, *sinkcaps;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstElementDetails details;

  klass->entry = tmp;

  details.longname = g_strdup_printf ("DirectShow %s Decoder Wrapper",
      tmp->element_longname);
  details.klass = g_strdup ("Codec/Decoder/Video");
  details.description = g_strdup_printf ("DirectShow %s Decoder Wrapper",
      tmp->element_longname);
  details.author = "Sebastien Moutte <sebastien@moutte.net>";
  gst_element_class_set_details (element_class, &details);
  g_free (details.longname);
  g_free (details.klass);
  g_free (details.description);

  sinkcaps = gst_caps_from_string (tmp->sinkcaps);
  gst_caps_set_simple (sinkcaps,
      "width", GST_TYPE_INT_RANGE, 16, 4096,
      "height", GST_TYPE_INT_RANGE, 16, 4096,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

  srccaps = gst_caps_from_string (tmp->srccaps);

  sink = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sinkcaps);
  src = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);

  gst_element_class_add_pad_template (element_class, src);
  gst_element_class_add_pad_template (element_class, sink);
}

static void
gst_dshowvideodec_class_init (GstDshowVideoDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_dshowvideodec_dispose);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_dshowvideodec_change_state);

  if (!parent_class)
    parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  if (!dshowvideodec_debug) {
    GST_DEBUG_CATEGORY_INIT (dshowvideodec_debug, "dshowvideodec", 0,
        "Directshow filter video decoder");
  }
}

static void
gst_dshowvideodec_init (GstDshowVideoDec * vdec,
    GstDshowVideoDecClass * vdec_class)
{
  GstElementClass *element_class = GST_ELEMENT_GET_CLASS (vdec);

  /* setup pads */
  vdec->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (element_class, "sink"), "sink");

  gst_pad_set_setcaps_function (vdec->sinkpad, gst_dshowvideodec_sink_setcaps);
  gst_pad_set_event_function (vdec->sinkpad, gst_dshowvideodec_sink_event);
  gst_pad_set_chain_function (vdec->sinkpad, gst_dshowvideodec_chain);
  gst_element_add_pad (GST_ELEMENT (vdec), vdec->sinkpad);

  vdec->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (element_class, "src"), "src");
/* needed to implement caps negociation on our src pad */
/*  gst_pad_set_getcaps_function (vdec->srcpad, gst_dshowvideodec_src_getcaps);
  gst_pad_set_setcaps_function (vdec->srcpad, gst_dshowvideodec_src_setcaps);*/
  gst_element_add_pad (GST_ELEMENT (vdec), vdec->srcpad);

  vdec->srcfilter = NULL;
  vdec->decfilter = NULL;
  vdec->sinkfilter = NULL;

  vdec->last_ret = GST_FLOW_OK;

  vdec->filtergraph = NULL;
  vdec->mediafilter = NULL;
  vdec->gstdshowsrcfilter = NULL;
  vdec->srccaps = NULL;
  vdec->segment = gst_segment_new ();

  CoInitializeEx (NULL, COINIT_MULTITHREADED);
}

static void
gst_dshowvideodec_dispose (GObject * object)
{
  GstDshowVideoDec *vdec = (GstDshowVideoDec *) (object);

  if (vdec->segment) {
    gst_segment_free (vdec->segment);
    vdec->segment = NULL;
  }

  CoUninitialize ();

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstStateChangeReturn
gst_dshowvideodec_change_state (GstElement * element, GstStateChange transition)
{
  GstDshowVideoDec *vdec = (GstDshowVideoDec *) (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_dshowvideodec_create_graph_and_filters (vdec))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_dshowvideodec_destroy_graph_and_filters (vdec))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static gboolean
gst_dshowvideodec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  gboolean ret = FALSE;
  HRESULT hres;
  GstStructure *s = gst_caps_get_structure (caps, 0);
  GstDshowVideoDec *vdec = (GstDshowVideoDec *) gst_pad_get_parent (pad);
  GstDshowVideoDecClass *klass =
      (GstDshowVideoDecClass *) G_OBJECT_GET_CLASS (vdec);
  GstBuffer *extradata = NULL;
  const GValue *v = NULL;
  gint size = 0;
  GstCaps *caps_out;
  AM_MEDIA_TYPE output_mediatype, input_mediatype;
  VIDEOINFOHEADER *input_vheader = NULL, *output_vheader = NULL;
  IPin *output_pin = NULL, *input_pin = NULL;
  IGstDshowInterface *gstdshowinterface = NULL;
  const GValue *fps;

  /* read data */
  if (!gst_structure_get_int (s, "width", &vdec->width) ||
      !gst_structure_get_int (s, "height", &vdec->height)) {
    GST_ELEMENT_ERROR (vdec, CORE, NEGOTIATION,
        ("error getting video width or height from caps"), (NULL));
    goto end;
  }
  fps = gst_structure_get_value (s, "framerate");
  if (!fps) {
    GST_ELEMENT_ERROR (vdec, CORE, NEGOTIATION,
        ("error getting video framerate from caps"), (NULL));
    goto end;
  }
  vdec->fps_n = gst_value_get_fraction_numerator (fps);
  vdec->fps_d = gst_value_get_fraction_denominator (fps);

  if ((v = gst_structure_get_value (s, "codec_data")))
    extradata = gst_value_get_buffer (v);

  /* define the input type format */
  memset (&input_mediatype, 0, sizeof (AM_MEDIA_TYPE));
  input_mediatype.majortype = klass->entry->input_majortype;
  input_mediatype.subtype = klass->entry->input_subtype;
  input_mediatype.bFixedSizeSamples = FALSE;
  input_mediatype.bTemporalCompression = TRUE;

  if (strstr (klass->entry->sinkcaps, "video/mpeg, mpegversion= (int) 1")) {
    size =
        sizeof (MPEG1VIDEOINFO) + (extradata ? GST_BUFFER_SIZE (extradata) -
        1 : 0);
    input_vheader = g_malloc0 (size);

    input_vheader->bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    if (extradata) {
      MPEG1VIDEOINFO *mpeg_info = (MPEG1VIDEOINFO *) input_vheader;

      memcpy (mpeg_info->bSequenceHeader,
          GST_BUFFER_DATA (extradata), GST_BUFFER_SIZE (extradata));
      mpeg_info->cbSequenceHeader = GST_BUFFER_SIZE (extradata);
    }
    input_mediatype.formattype = FORMAT_MPEGVideo;
  } else {
    size =
        sizeof (VIDEOINFOHEADER) +
        (extradata ? GST_BUFFER_SIZE (extradata) : 0);
    input_vheader = g_malloc0 (size);

    input_vheader->bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    if (extradata) {            /* Codec data is appended after our header */
      memcpy (((guchar *) input_vheader) + sizeof (VIDEOINFOHEADER),
          GST_BUFFER_DATA (extradata), GST_BUFFER_SIZE (extradata));
      input_vheader->bmiHeader.biSize += GST_BUFFER_SIZE (extradata);
    }
    input_mediatype.formattype = FORMAT_VideoInfo;
  }
  input_vheader->rcSource.top = input_vheader->rcSource.left = 0;
  input_vheader->rcSource.right = vdec->width;
  input_vheader->rcSource.bottom = vdec->height;
  input_vheader->rcTarget = input_vheader->rcSource;
  input_vheader->bmiHeader.biWidth = vdec->width;
  input_vheader->bmiHeader.biHeight = vdec->height;
  input_vheader->bmiHeader.biPlanes = 1;
  input_vheader->bmiHeader.biBitCount = 16;
  input_vheader->bmiHeader.biCompression = klass->entry->format;
  input_vheader->bmiHeader.biSizeImage =
      (vdec->width * vdec->height) * (input_vheader->bmiHeader.biBitCount / 8);

  input_mediatype.cbFormat = size;
  input_mediatype.pbFormat = (BYTE *) input_vheader;
  input_mediatype.lSampleSize = input_vheader->bmiHeader.biSizeImage;

  hres = IBaseFilter_QueryInterface (vdec->srcfilter, &IID_IGstDshowInterface,
      (void **) &gstdshowinterface);
  if (hres != S_OK || !gstdshowinterface) {
    GST_ELEMENT_ERROR (vdec, CORE, NEGOTIATION,
        ("Can't get IGstDshowInterface interface from dshow fakesrc filter (error=%d)",
            hres), (NULL));
    goto end;
  }

  /* save a reference to IGstDshowInterface to use it processing functions */
  if (!vdec->gstdshowsrcfilter) {
    vdec->gstdshowsrcfilter = gstdshowinterface;
    IBaseFilter_AddRef (vdec->gstdshowsrcfilter);
  }

  IGstDshowInterface_gst_set_media_type (gstdshowinterface, &input_mediatype);
  IGstDshowInterface_Release (gstdshowinterface);
  gstdshowinterface = NULL;

  /* set the sample size for fakesrc filter to the output buffer size */
  IGstDshowInterface_gst_set_sample_size (vdec->gstdshowsrcfilter,
      input_mediatype.lSampleSize);

  /* connect our fake src to decoder */
  gst_dshow_get_pin_from_filter (vdec->srcfilter, PINDIR_OUTPUT, &output_pin);
  if (!output_pin) {
    GST_ELEMENT_ERROR (vdec, CORE, NEGOTIATION,
        ("Can't get output pin from our directshow fakesrc filter"), (NULL));
    goto end;
  }
  gst_dshow_get_pin_from_filter (vdec->decfilter, PINDIR_INPUT, &input_pin);
  if (!input_pin) {
    GST_ELEMENT_ERROR (vdec, CORE, NEGOTIATION,
        ("Can't get input pin from decoder filter"), (NULL));
    goto end;
  }

  hres =
      IFilterGraph_ConnectDirect (vdec->filtergraph, output_pin, input_pin,
      NULL);
  if (hres != S_OK) {
    GST_ELEMENT_ERROR (vdec, CORE, NEGOTIATION,
        ("Can't connect fakesrc with decoder (error=%d)", hres), (NULL));
    goto end;
  }

  IPin_Release (input_pin);
  IPin_Release (output_pin);
  input_pin = NULL;
  output_pin = NULL;

  /* get decoder output video format */
  if (!gst_dshowvideodec_get_filter_output_format (vdec,
          &klass->entry->output_subtype, &output_vheader, &size)) {
    GST_ELEMENT_ERROR (vdec, CORE, NEGOTIATION,
        ("Can't get decoder output video format"), (NULL));
    goto end;
  }

  memset (&output_mediatype, 0, sizeof (AM_MEDIA_TYPE));
  output_mediatype.majortype = klass->entry->output_majortype;
  output_mediatype.subtype = klass->entry->output_subtype;
  output_mediatype.bFixedSizeSamples = TRUE;
  output_mediatype.bTemporalCompression = FALSE;
  output_mediatype.lSampleSize = output_vheader->bmiHeader.biSizeImage;
  output_mediatype.formattype = FORMAT_VideoInfo;
  output_mediatype.cbFormat = size;
  output_mediatype.pbFormat = (char *) output_vheader;

  hres = IBaseFilter_QueryInterface (vdec->sinkfilter, &IID_IGstDshowInterface,
      (void **) &gstdshowinterface);
  if (hres != S_OK || !gstdshowinterface) {
    GST_ELEMENT_ERROR (vdec, CORE, NEGOTIATION,
        ("Can't get IGstDshowInterface interface from dshow fakesink filter (error=%d)",
            hres), (NULL));
    goto end;
  }

  IGstDshowInterface_gst_set_media_type (gstdshowinterface, &output_mediatype);
  IGstDshowInterface_gst_set_buffer_callback (gstdshowinterface,
      gst_dshowvideodec_push_buffer, (byte *) vdec);
  IGstDshowInterface_Release (gstdshowinterface);
  gstdshowinterface = NULL;

  /* connect decoder to our fake sink */
  gst_dshow_get_pin_from_filter (vdec->decfilter, PINDIR_OUTPUT, &output_pin);
  if (!output_pin) {
    GST_ELEMENT_ERROR (vdec, CORE, NEGOTIATION,
        ("Can't get output pin from our decoder filter"), (NULL));
    goto end;
  }

  gst_dshow_get_pin_from_filter (vdec->sinkfilter, PINDIR_INPUT, &input_pin);
  if (!input_pin) {
    GST_ELEMENT_ERROR (vdec, CORE, NEGOTIATION,
        ("Can't get input pin from our directshow fakesink filter"), (NULL));
    goto end;
  }

  hres =
      IFilterGraph_ConnectDirect (vdec->filtergraph, output_pin, input_pin,
      &output_mediatype);
  if (hres != S_OK) {
    GST_ELEMENT_ERROR (vdec, CORE, NEGOTIATION,
        ("Can't connect decoder with fakesink (error=%d)", hres), (NULL));
    goto end;
  }

  /* negotiate output */
  caps_out = gst_caps_from_string (klass->entry->srccaps);
  gst_caps_set_simple (caps_out,
      "width", G_TYPE_INT, vdec->width,
      "height", G_TYPE_INT, vdec->height,
      "framerate", GST_TYPE_FRACTION, vdec->fps_n, vdec->fps_d, NULL);
  if (!gst_pad_set_caps (vdec->srcpad, caps_out)) {
    gst_caps_unref (caps_out);
    GST_ELEMENT_ERROR (vdec, CORE, NEGOTIATION,
        ("Failed to negotiate output"), (NULL));
    goto end;
  }
  gst_caps_unref (caps_out);

  hres = IMediaFilter_Run (vdec->mediafilter, -1);
  if (hres != S_OK) {
    GST_ELEMENT_ERROR (vdec, CORE, NEGOTIATION,
        ("Can't run the directshow graph (error=%d)", hres), (NULL));
    goto end;
  }

  ret = TRUE;
end:
  gst_object_unref (vdec);
  if (input_vheader)
    g_free (input_vheader);
  if (gstdshowinterface)
    IGstDshowInterface_Release (gstdshowinterface);
  if (input_pin)
    IPin_Release (input_pin);
  if (output_pin)
    IPin_Release (output_pin);

  return ret;
}

static gboolean
gst_dshowvideodec_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = TRUE;
  GstDshowVideoDec *vdec = (GstDshowVideoDec *) gst_pad_get_parent (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_dshowvideodec_flush (vdec);
      ret = gst_pad_event_default (pad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment (event, &update, &rate, &format, &start,
          &stop, &time);

      /* save the new segment in our local current segment */
      gst_segment_set_newsegment (vdec->segment, update, rate, format, start,
          stop, time);

      GST_CAT_DEBUG_OBJECT (dshowvideodec_debug, vdec,
          "new segment received => start=%" GST_TIME_FORMAT " stop=%"
          GST_TIME_FORMAT, GST_TIME_ARGS (vdec->segment->start),
          GST_TIME_ARGS (vdec->segment->stop));

      if (update) {
        GST_CAT_DEBUG_OBJECT (dshowvideodec_debug, vdec,
            "closing current segment flushing..");
        gst_dshowvideodec_flush (vdec);
      }

      ret = gst_pad_event_default (pad, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (vdec);

  return ret;
}

static GstFlowReturn
gst_dshowvideodec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstDshowVideoDec *vdec = (GstDshowVideoDec *) gst_pad_get_parent (pad);
  gboolean discount = FALSE;
  GstClockTime stop;

  if (!vdec->gstdshowsrcfilter) {
    /* we are not setup */
    vdec->last_ret = GST_FLOW_WRONG_STATE;
    goto beach;
  }

  if (GST_FLOW_IS_FATAL (vdec->last_ret)) {
    GST_DEBUG_OBJECT (vdec, "last decoding iteration generated a fatal error "
        "%s", gst_flow_get_name (vdec->last_ret));
    goto beach;
  }

  /* check if duration is valid and use duration only when it's valid
     /* because dshow is not decoding frames having stop smaller than start */
  if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
    stop = GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
  } else {
    stop = GST_BUFFER_TIMESTAMP (buffer);
  }

  GST_CAT_LOG_OBJECT (dshowvideodec_debug, vdec,
      "chain (size %d)=> pts %" GST_TIME_FORMAT " stop %" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (buffer), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (stop));

  /* if the incoming buffer has discont flag set => flush decoder data */
  if (buffer && GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    GST_CAT_DEBUG_OBJECT (dshowvideodec_debug, vdec,
        "this buffer has a DISCONT flag (%" GST_TIME_FORMAT "), flushing",
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));
    gst_dshowvideodec_flush (vdec);
    discount = TRUE;
  }

  /* push the buffer to the directshow decoder */
  IGstDshowInterface_gst_push_buffer (vdec->gstdshowsrcfilter,
      GST_BUFFER_DATA (buffer), GST_BUFFER_TIMESTAMP (buffer), stop,
      GST_BUFFER_SIZE (buffer), discount);

beach:
  gst_buffer_unref (buffer);
  gst_object_unref (vdec);

  return vdec->last_ret;
}

static gboolean
gst_dshowvideodec_push_buffer (byte * buffer, long size, byte * src_object,
    UINT64 start, UINT64 stop)
{
  GstDshowVideoDec *vdec = (GstDshowVideoDec *) src_object;
  GstDshowVideoDecClass *klass =
      (GstDshowVideoDecClass *) G_OBJECT_GET_CLASS (vdec);
  GstBuffer *buf = NULL;
  gboolean in_seg = FALSE;
  gint64 clip_start = 0, clip_stop = 0;

  /* check if this buffer is in our current segment */
  in_seg = gst_segment_clip (vdec->segment, GST_FORMAT_TIME,
      start, stop, &clip_start, &clip_stop);

  /* if the buffer is out of segment do not push it downstream */
  if (!in_seg) {
    GST_CAT_DEBUG_OBJECT (dshowvideodec_debug, vdec,
        "buffer is out of segment, start %" GST_TIME_FORMAT " stop %"
        GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (stop));
    return FALSE;
  }

  /* buffer is in our segment allocate a new out buffer and clip its
   * timestamps */
  vdec->last_ret = gst_pad_alloc_buffer (vdec->srcpad, GST_BUFFER_OFFSET_NONE,
      size, GST_PAD_CAPS (vdec->srcpad), &buf);
  if (!buf) {
    GST_CAT_WARNING_OBJECT (dshowvideodec_debug, vdec,
        "can't not allocate a new GstBuffer");
    return FALSE;
  }

  /* set buffer properties */
  GST_BUFFER_TIMESTAMP (buf) = clip_start;
  GST_BUFFER_DURATION (buf) = clip_stop - clip_start;

  if (strstr (klass->entry->srccaps, "rgb")) {
    /* FOR RGB directshow decoder will return bottom-up BITMAP 
     * There is probably a way to get top-bottom video frames from
     * the decoder...
     */
    gint line = 0;
    guint stride = vdec->width * 4;

    for (; line < vdec->height; line++) {
      memcpy (GST_BUFFER_DATA (buf) + (line * stride),
          buffer + (size - ((line + 1) * (stride))), stride);
    }
  } else {
    memcpy (GST_BUFFER_DATA (buf), buffer, MIN (size, GST_BUFFER_SIZE (buf)));
  }

  GST_CAT_LOG_OBJECT (dshowvideodec_debug, vdec,
      "push_buffer (size %d)=> pts %" GST_TIME_FORMAT " stop %" GST_TIME_FORMAT
      " duration %" GST_TIME_FORMAT, size,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf) + GST_BUFFER_DURATION (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  /* push the buffer downstream */
  vdec->last_ret = gst_pad_push (vdec->srcpad, buf);

  return TRUE;
}


static GstCaps *
gst_dshowvideodec_src_getcaps (GstPad * pad)
{
  GstDshowVideoDec *vdec = (GstDshowVideoDec *) gst_pad_get_parent (pad);
  GstCaps *caps = NULL;

  if (!vdec->srccaps)
    vdec->srccaps = gst_caps_new_empty ();

  if (vdec->decfilter) {
    IPin *output_pin = NULL;
    IEnumMediaTypes *enum_mediatypes = NULL;
    HRESULT hres;
    ULONG fetched;

    if (!gst_dshow_get_pin_from_filter (vdec->decfilter, PINDIR_OUTPUT,
            &output_pin)) {
      GST_ELEMENT_ERROR (vdec, STREAM, FAILED,
          ("failed getting ouput pin from the decoder"), (NULL));
      goto beach;
    }

    hres = IPin_EnumMediaTypes (output_pin, &enum_mediatypes);
    if (hres == S_OK && enum_mediatypes) {
      AM_MEDIA_TYPE *mediatype = NULL;

      IEnumMediaTypes_Reset (enum_mediatypes);
      while (hres =
          IEnumMoniker_Next (enum_mediatypes, 1, &mediatype, &fetched),
          hres == S_OK) {
        RPC_STATUS rpcstatus;
        VIDEOINFOHEADER *video_info;
        GstCaps *mediacaps = NULL;

        /* RGB24 */
        if ((UuidCompare (&mediatype->subtype, &MEDIASUBTYPE_RGB24,
                    &rpcstatus) == 0 && rpcstatus == RPC_S_OK)
            && (UuidCompare (&mediatype->formattype, &FORMAT_VideoInfo,
                    &rpcstatus) == 0 && rpcstatus == RPC_S_OK)) {
          video_info = (VIDEOINFOHEADER *) mediatype->pbFormat;

          /* ffmpegcolorspace handles RGB24 in BIG_ENDIAN */
          mediacaps = gst_caps_new_simple ("video/x-raw-rgb",
              "bpp", G_TYPE_INT, 24,
              "depth", G_TYPE_INT, 24,
              "width", G_TYPE_INT, video_info->bmiHeader.biWidth,
              "height", G_TYPE_INT, video_info->bmiHeader.biHeight,
              "framerate", GST_TYPE_FRACTION,
              (int) (10000000 / video_info->AvgTimePerFrame), 1, "endianness",
              G_TYPE_INT, G_BIG_ENDIAN, "red_mask", G_TYPE_INT, 255,
              "green_mask", G_TYPE_INT, 65280, "blue_mask", G_TYPE_INT,
              16711680, NULL);

          if (mediacaps) {
            vdec->mediatypes = g_list_append (vdec->mediatypes, mediatype);
            gst_caps_append (vdec->srccaps, mediacaps);
          } else {
            gst_dshow_free_mediatype (mediatype);
          }
        } else {
          gst_dshow_free_mediatype (mediatype);
        }

      }
      IEnumMediaTypes_Release (enum_mediatypes);
    }
    if (output_pin) {
      IPin_Release (output_pin);
    }
  }

  if (vdec->srccaps)
    caps = gst_caps_ref (vdec->srccaps);

beach:
  gst_object_unref (vdec);

  return caps;
}

static gboolean
gst_dshowvideodec_src_setcaps (GstPad * pad, GstCaps * caps)
{
  gboolean ret = FALSE;

  return ret;
}

static gboolean
gst_dshowvideodec_flush (GstDshowVideoDec * vdec)
{
  if (!vdec->gstdshowsrcfilter)
    return FALSE;

  /* flush dshow decoder and reset timestamp */
  IGstDshowInterface_gst_flush (vdec->gstdshowsrcfilter);

  return TRUE;
}

static gboolean
gst_dshowvideodec_get_filter_output_format (GstDshowVideoDec * vdec,
    GUID * subtype, VIDEOINFOHEADER ** format, guint * size)
{
  IPin *output_pin = NULL;
  IEnumMediaTypes *enum_mediatypes = NULL;
  HRESULT hres;
  ULONG fetched;
  BOOL ret = FALSE;

  if (!vdec->decfilter)
    return FALSE;

  if (!gst_dshow_get_pin_from_filter (vdec->decfilter, PINDIR_OUTPUT,
          &output_pin)) {
    GST_ELEMENT_ERROR (vdec, CORE, NEGOTIATION,
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

      if ((UuidCompare (&mediatype->subtype, subtype, &rpcstatus) == 0
              && rpcstatus == RPC_S_OK) &&
          (UuidCompare (&mediatype->formattype, &FORMAT_VideoInfo,
                  &rpcstatus) == 0 && rpcstatus == RPC_S_OK)) {
        *size = mediatype->cbFormat;
        *format = g_malloc0 (*size);
        memcpy (*format, mediatype->pbFormat, *size);
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
gst_dshowvideodec_create_graph_and_filters (GstDshowVideoDec * vdec)
{
  HRESULT hres = S_FALSE;
  GstDshowVideoDecClass *klass =
      (GstDshowVideoDecClass *) G_OBJECT_GET_CLASS (vdec);

  /* create the filter graph manager object */
  hres = CoCreateInstance (&CLSID_FilterGraph, NULL, CLSCTX_INPROC,
      &IID_IFilterGraph, (LPVOID *) & vdec->filtergraph);
  if (hres != S_OK || !vdec->filtergraph) {
    GST_ELEMENT_ERROR (vdec, STREAM, FAILED, ("Can't create an instance "
            "of the directshow graph manager (error=%d)", hres), (NULL));
    goto error;
  }

  hres = IFilterGraph_QueryInterface (vdec->filtergraph, &IID_IMediaFilter,
      (void **) &vdec->mediafilter);
  if (hres != S_OK || !vdec->mediafilter) {
    GST_ELEMENT_ERROR (vdec, STREAM, FAILED,
        ("Can't get IMediacontrol interface "
            "from the graph manager (error=%d)", hres), (NULL));
    goto error;
  }

  /* create fake src filter */
  hres = CoCreateInstance (&CLSID_DshowFakeSrc, NULL, CLSCTX_INPROC,
      &IID_IBaseFilter, (LPVOID *) & vdec->srcfilter);
  if (hres != S_OK || !vdec->srcfilter) {
    GST_ELEMENT_ERROR (vdec, STREAM, FAILED, ("Can't create an instance "
            "of the directshow fakesrc (error=%d)", hres), (NULL));
    goto error;
  }

  /* search a decoder filter and create it */
  if (!gst_dshow_find_filter (klass->entry->input_majortype,
          klass->entry->input_subtype,
          klass->entry->output_majortype,
          klass->entry->output_subtype,
          klass->entry->prefered_filter_substring, &vdec->decfilter)) {
    GST_ELEMENT_ERROR (vdec, STREAM, FAILED, ("Can't create an instance "
            "of the decoder filter"), (NULL));
    goto error;
  }

  /* create fake sink filter */
  hres = CoCreateInstance (&CLSID_DshowFakeSink, NULL, CLSCTX_INPROC,
      &IID_IBaseFilter, (LPVOID *) & vdec->sinkfilter);
  if (hres != S_OK || !vdec->sinkfilter) {
    GST_ELEMENT_ERROR (vdec, STREAM, FAILED, ("Can't create an instance "
            "of the directshow fakesink (error=%d)", hres), (NULL));
    goto error;
  }

  /* add filters to the graph */
  hres = IFilterGraph_AddFilter (vdec->filtergraph, vdec->srcfilter, L"src");
  if (hres != S_OK) {
    GST_ELEMENT_ERROR (vdec, STREAM, FAILED, ("Can't add fakesrc filter "
            "to the graph (error=%d)", hres), (NULL));
    goto error;
  }

  hres =
      IFilterGraph_AddFilter (vdec->filtergraph, vdec->decfilter, L"decoder");
  if (hres != S_OK) {
    GST_ELEMENT_ERROR (vdec, STREAM, FAILED, ("Can't add decoder filter "
            "to the graph (error=%d)", hres), (NULL));
    goto error;
  }

  hres = IFilterGraph_AddFilter (vdec->filtergraph, vdec->sinkfilter, L"sink");
  if (hres != S_OK) {
    GST_ELEMENT_ERROR (vdec, STREAM, FAILED, ("Can't add fakesink filter "
            "to the graph (error=%d)", hres), (NULL));
    goto error;
  }

  return TRUE;

error:
  if (vdec->srcfilter) {
    IBaseFilter_Release (vdec->srcfilter);
    vdec->srcfilter = NULL;
  }
  if (vdec->decfilter) {
    IBaseFilter_Release (vdec->decfilter);
    vdec->decfilter = NULL;
  }
  if (vdec->sinkfilter) {
    IBaseFilter_Release (vdec->sinkfilter);
    vdec->sinkfilter = NULL;
  }
  if (vdec->mediafilter) {
    IMediaFilter_Release (vdec->mediafilter);
    vdec->mediafilter = NULL;
  }
  if (vdec->filtergraph) {
    IFilterGraph_Release (vdec->filtergraph);
    vdec->filtergraph = NULL;
  }

  return FALSE;
}

static gboolean
gst_dshowvideodec_destroy_graph_and_filters (GstDshowVideoDec * vdec)
{
  if (vdec->mediafilter) {
    IMediaFilter_Stop (vdec->mediafilter);
  }

  if (vdec->gstdshowsrcfilter) {
    IGstDshowInterface_Release (vdec->gstdshowsrcfilter);
    vdec->gstdshowsrcfilter = NULL;
  }
  if (vdec->srcfilter) {
    if (vdec->filtergraph)
      IFilterGraph_RemoveFilter (vdec->filtergraph, vdec->srcfilter);
    IBaseFilter_Release (vdec->srcfilter);
    vdec->srcfilter = NULL;
  }
  if (vdec->decfilter) {
    if (vdec->filtergraph)
      IFilterGraph_RemoveFilter (vdec->filtergraph, vdec->decfilter);
    IBaseFilter_Release (vdec->decfilter);
    vdec->decfilter = NULL;
  }
  if (vdec->sinkfilter) {
    if (vdec->filtergraph)
      IFilterGraph_RemoveFilter (vdec->filtergraph, vdec->sinkfilter);
    IBaseFilter_Release (vdec->sinkfilter);
    vdec->sinkfilter = NULL;
  }
  if (vdec->mediafilter) {
    IMediaFilter_Release (vdec->mediafilter);
    vdec->mediafilter = NULL;
  }
  if (vdec->filtergraph) {
    IFilterGraph_Release (vdec->filtergraph);
    vdec->filtergraph = NULL;
  }

  return TRUE;
}

gboolean
dshow_vdec_register (GstPlugin * plugin)
{
  GTypeInfo info = {
    sizeof (GstDshowVideoDecClass),
    (GBaseInitFunc) gst_dshowvideodec_base_init,
    NULL,
    (GClassInitFunc) gst_dshowvideodec_class_init,
    NULL,
    NULL,
    sizeof (GstDshowVideoDec),
    0,
    (GInstanceInitFunc) gst_dshowvideodec_init,
  };
  gint i;

  GST_DEBUG_CATEGORY_INIT (dshowvideodec_debug, "dshowvideodec", 0,
      "Directshow filter video decoder");

  CoInitializeEx (NULL, COINIT_MULTITHREADED);
  for (i = 0; i < sizeof (video_dec_codecs) / sizeof (CodecEntry); i++) {
    GType type;

    if (gst_dshow_find_filter (video_dec_codecs[i].input_majortype,
            video_dec_codecs[i].input_subtype,
            video_dec_codecs[i].output_majortype,
            video_dec_codecs[i].output_subtype,
            video_dec_codecs[i].prefered_filter_substring, NULL)) {

      GST_CAT_DEBUG (dshowvideodec_debug, "Registering %s",
          video_dec_codecs[i].element_name);

      tmp = &video_dec_codecs[i];
      type =
          g_type_register_static (GST_TYPE_ELEMENT,
          video_dec_codecs[i].element_name, &info, 0);
      if (!gst_element_register (plugin, video_dec_codecs[i].element_name,
              GST_RANK_PRIMARY, type)) {
        return FALSE;
      }
      GST_CAT_DEBUG (dshowvideodec_debug, "Registered %s",
          video_dec_codecs[i].element_name);
    } else {
      GST_CAT_DEBUG (dshowvideodec_debug,
          "Element %s not registered (the format is not supported by the system)",
          video_dec_codecs[i].element_name);
    }
  }

  CoUninitialize ();
  return TRUE;
}
