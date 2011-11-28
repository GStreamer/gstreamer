/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2005> Wim Taymans <wim@fluendo.com>
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
 * SECTION:element-dvdec
 *
 * dvdec decodes DV video into raw video. The element expects a full DV frame
 * as input, which is 120000 bytes for NTSC and 144000 for PAL video.
 *
 * This element can perform simple frame dropping with the #GstDVDec:drop-factor
 * property. Setting this property to a value N > 1 will only decode every 
 * Nth frame.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=test.dv ! dvdemux name=demux ! dvdec ! xvimagesink
 * ]| This pipeline decodes and renders the raw DV stream to a videosink.
 * </refsect2>
 *
 * Last reviewed on 2006-02-28 (0.10.3)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <math.h>
#include <gst/video/video.h>

#include "gstdvdec.h"

/* sizes of one input buffer */
#define NTSC_HEIGHT 480
#define NTSC_BUFFER 120000
#define NTSC_FRAMERATE_NUMERATOR 30000
#define NTSC_FRAMERATE_DENOMINATOR 1001

#define PAL_HEIGHT 576
#define PAL_BUFFER 144000
#define PAL_FRAMERATE_NUMERATOR 25
#define PAL_FRAMERATE_DENOMINATOR 1

#define PAL_NORMAL_PAR_X        59
#define PAL_NORMAL_PAR_Y        54
#define PAL_WIDE_PAR_X          118
#define PAL_WIDE_PAR_Y          81

#define NTSC_NORMAL_PAR_X       10
#define NTSC_NORMAL_PAR_Y       11
#define NTSC_WIDE_PAR_X         40
#define NTSC_WIDE_PAR_Y         33

#define DV_DEFAULT_QUALITY DV_QUALITY_BEST
#define DV_DEFAULT_DECODE_NTH 1

GST_DEBUG_CATEGORY_STATIC (dvdec_debug);
#define GST_CAT_DEFAULT dvdec_debug

enum
{
  PROP_0,
  PROP_CLAMP_LUMA,
  PROP_CLAMP_CHROMA,
  PROP_QUALITY,
  PROP_DECODE_NTH
};

const gint qualities[] = {
  DV_QUALITY_DC,
  DV_QUALITY_AC_1,
  DV_QUALITY_AC_2,
  DV_QUALITY_DC | DV_QUALITY_COLOR,
  DV_QUALITY_AC_1 | DV_QUALITY_COLOR,
  DV_QUALITY_AC_2 | DV_QUALITY_COLOR
};

static GstStaticPadTemplate sink_temp = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dv, systemstream = (boolean) false")
    );

static GstStaticPadTemplate src_temp = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) YUY2, "
        "width = (int) 720, "
        "framerate = (fraction) [ 1/1, 60/1 ];"
        "video/x-raw-rgb, "
        "bpp = (int) 32, "
        "depth = (int) 24, "
        "endianness = (int) " G_STRINGIFY (G_BIG_ENDIAN) ", "
        "red_mask =   (int) 0x0000ff00, "
        "green_mask = (int) 0x00ff0000, "
        "blue_mask =  (int) 0xff000000, "
        "width = (int) 720, "
        "framerate = (fraction) [ 1/1, 60/1 ];"
        "video/x-raw-rgb, "
        "bpp = (int) 24, "
        "depth = (int) 24, "
        "endianness = (int) " G_STRINGIFY (G_BIG_ENDIAN) ", "
        "red_mask =   (int) 0x00ff0000, "
        "green_mask = (int) 0x0000ff00, "
        "blue_mask =  (int) 0x000000ff, "
        "width = (int) 720, " "framerate = (fraction) [ 1/1, 60/1 ]")
    );

#define GST_TYPE_DVDEC_QUALITY (gst_dvdec_quality_get_type())
static GType
gst_dvdec_quality_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {0, "Monochrome, DC (Fastest)", "fastest"},
      {1, "Monochrome, first AC coefficient", "monochrome-ac"},
      {2, "Monochrome, highest quality", "monochrome-best"},
      {3, "Colour, DC, fastest", "colour-fastest"},
      {4, "Colour, using only the first AC coefficient", "colour-ac"},
      {5, "Highest quality colour decoding", "best"},
      {0, NULL, NULL},
    };

    qtype = g_enum_register_static ("GstDVDecQualityEnum", values);
  }
  return qtype;
}

GST_BOILERPLATE (GstDVDec, gst_dvdec, GstElement, GST_TYPE_ELEMENT);

static void gst_dvdec_finalize (GObject * object);
static gboolean gst_dvdec_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_dvdec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_dvdec_sink_event (GstPad * pad, GstEvent * event);

static GstStateChangeReturn gst_dvdec_change_state (GstElement * element,
    GstStateChange transition);

static void gst_dvdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dvdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_dvdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &sink_temp);
  gst_element_class_add_static_pad_template (element_class, &src_temp);

  gst_element_class_set_details_simple (element_class, "DV video decoder",
      "Codec/Decoder/Video",
      "Uses libdv to decode DV video (smpte314) (libdv.sourceforge.net)",
      "Erik Walthinsen <omega@cse.ogi.edu>," "Wim Taymans <wim@fluendo.com>");

  GST_DEBUG_CATEGORY_INIT (dvdec_debug, "dvdec", 0, "DV decoding element");
}

static void
gst_dvdec_class_init (GstDVDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_dvdec_finalize;
  gobject_class->set_property = gst_dvdec_set_property;
  gobject_class->get_property = gst_dvdec_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CLAMP_LUMA,
      g_param_spec_boolean ("clamp-luma", "Clamp luma", "Clamp luma",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CLAMP_CHROMA,
      g_param_spec_boolean ("clamp-chroma", "Clamp chroma", "Clamp chroma",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_QUALITY,
      g_param_spec_enum ("quality", "Quality", "Decoding quality",
          GST_TYPE_DVDEC_QUALITY, DV_DEFAULT_QUALITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DECODE_NTH,
      g_param_spec_int ("drop-factor", "Drop Factor", "Only decode Nth frame",
          1, G_MAXINT, DV_DEFAULT_DECODE_NTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_dvdec_change_state;
}

static void
gst_dvdec_init (GstDVDec * dvdec, GstDVDecClass * g_class)
{
  dvdec->sinkpad = gst_pad_new_from_static_template (&sink_temp, "sink");
  gst_pad_set_setcaps_function (dvdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvdec_sink_setcaps));
  gst_pad_set_chain_function (dvdec->sinkpad, gst_dvdec_chain);
  gst_pad_set_event_function (dvdec->sinkpad, gst_dvdec_sink_event);
  gst_element_add_pad (GST_ELEMENT (dvdec), dvdec->sinkpad);

  dvdec->srcpad = gst_pad_new_from_static_template (&src_temp, "src");
  gst_pad_use_fixed_caps (dvdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (dvdec), dvdec->srcpad);

  dvdec->framerate_numerator = 0;
  dvdec->framerate_denominator = 0;
  dvdec->wide = FALSE;
  dvdec->drop_factor = 1;

  dvdec->clamp_luma = FALSE;
  dvdec->clamp_chroma = FALSE;
  dvdec->quality = DV_DEFAULT_QUALITY;
  dvdec->segment = gst_segment_new ();
}

static void
gst_dvdec_finalize (GObject * object)
{
  GstDVDec *dvdec = GST_DVDEC (object);

  gst_segment_free (dvdec->segment);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_dvdec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstDVDec *dvdec;
  GstStructure *s;
  const GValue *par = NULL, *rate = NULL;

  dvdec = GST_DVDEC (gst_pad_get_parent (pad));

  /* first parse the caps */
  s = gst_caps_get_structure (caps, 0);

  /* we allow framerate and PAR to be overwritten. framerate is mandatory. */
  if (!(rate = gst_structure_get_value (s, "framerate")))
    goto no_framerate;
  par = gst_structure_get_value (s, "pixel-aspect-ratio");

  if (par) {
    dvdec->par_x = gst_value_get_fraction_numerator (par);
    dvdec->par_y = gst_value_get_fraction_denominator (par);
    dvdec->need_par = FALSE;
  } else {
    dvdec->par_x = 0;
    dvdec->par_y = 0;
    dvdec->need_par = TRUE;
  }
  dvdec->framerate_numerator = gst_value_get_fraction_numerator (rate);
  dvdec->framerate_denominator = gst_value_get_fraction_denominator (rate);
  dvdec->sink_negotiated = TRUE;
  dvdec->src_negotiated = FALSE;

  gst_object_unref (dvdec);

  return TRUE;

  /* ERRORS */
no_framerate:
  {
    GST_DEBUG_OBJECT (dvdec, "no framerate specified in caps");
    gst_object_unref (dvdec);
    return FALSE;
  }
}

static gboolean
gst_dvdec_src_negotiate (GstDVDec * dvdec)
{
  GstCaps *othercaps;

  /* no PAR was specified in input, derive from encoded data */
  if (dvdec->need_par) {
    if (dvdec->PAL) {
      if (dvdec->wide) {
        dvdec->par_x = PAL_WIDE_PAR_X;
        dvdec->par_y = PAL_WIDE_PAR_Y;
      } else {
        dvdec->par_x = PAL_NORMAL_PAR_X;
        dvdec->par_y = PAL_NORMAL_PAR_Y;
      }
    } else {
      if (dvdec->wide) {
        dvdec->par_x = NTSC_WIDE_PAR_X;
        dvdec->par_y = NTSC_WIDE_PAR_Y;
      } else {
        dvdec->par_x = NTSC_NORMAL_PAR_X;
        dvdec->par_y = NTSC_NORMAL_PAR_Y;
      }
    }
    GST_DEBUG_OBJECT (dvdec, "Inferred PAR %d/%d from video format",
        dvdec->par_x, dvdec->par_y);
  }

  /* ignoring rgb, bgr0 for now */
  dvdec->bpp = 2;

  othercaps = gst_caps_new_simple ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, GST_STR_FOURCC ("YUY2"),
      "width", G_TYPE_INT, 720,
      "height", G_TYPE_INT, dvdec->height,
      "framerate", GST_TYPE_FRACTION, dvdec->framerate_numerator,
      dvdec->framerate_denominator,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, dvdec->par_x,
      dvdec->par_y, "interlaced", G_TYPE_BOOLEAN, dvdec->interlaced, NULL);

  gst_pad_set_caps (dvdec->srcpad, othercaps);
  gst_caps_unref (othercaps);

  dvdec->src_negotiated = TRUE;

  return TRUE;
}

static gboolean
gst_dvdec_sink_event (GstPad * pad, GstEvent * event)
{
  GstDVDec *dvdec;
  gboolean res = TRUE;

  dvdec = GST_DVDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (dvdec->segment, GST_FORMAT_UNDEFINED);
      break;
    case GST_EVENT_NEWSEGMENT:{
      gboolean update;
      gdouble rate, applied_rate;
      GstFormat format;
      gint64 start, stop, position;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &position);

      GST_DEBUG_OBJECT (dvdec, "Got NEWSEGMENT [%" GST_TIME_FORMAT
          " - %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT "]",
          GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
          GST_TIME_ARGS (position));

      gst_segment_set_newsegment_full (dvdec->segment, update, rate,
          applied_rate, format, start, stop, position);
      break;
    }
    default:
      break;
  }

  res = gst_pad_push_event (dvdec->srcpad, event);

  return res;
}

static GstFlowReturn
gst_dvdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstDVDec *dvdec;
  guint8 *inframe;
  guint8 *outframe;
  guint8 *outframe_ptrs[3];
  gint outframe_pitches[3];
  GstBuffer *outbuf;
  GstFlowReturn ret = GST_FLOW_OK;
  guint length;
  gint64 cstart, cstop;
  gboolean PAL, wide;

  dvdec = GST_DVDEC (gst_pad_get_parent (pad));
  inframe = GST_BUFFER_DATA (buf);

  /* buffer should be at least the size of one NTSC frame, this should
   * be enough to decode the header. */
  if (G_UNLIKELY (GST_BUFFER_SIZE (buf) < NTSC_BUFFER))
    goto wrong_size;

  /* preliminary dropping. unref and return if outside of configured segment */
  if ((dvdec->segment->format == GST_FORMAT_TIME) &&
      (!(gst_segment_clip (dvdec->segment, GST_FORMAT_TIME,
                  GST_BUFFER_TIMESTAMP (buf),
                  GST_BUFFER_TIMESTAMP (buf) + GST_BUFFER_DURATION (buf),
                  &cstart, &cstop))))
    goto dropping;

  if (G_UNLIKELY (dv_parse_header (dvdec->decoder, inframe) < 0))
    goto parse_header_error;

  /* get size */
  PAL = dv_system_50_fields (dvdec->decoder);
  wide = dv_format_wide (dvdec->decoder);

  /* check the buffer is of right size after we know if we are
   * dealing with PAL or NTSC */
  length = (PAL ? PAL_BUFFER : NTSC_BUFFER);
  if (G_UNLIKELY (GST_BUFFER_SIZE (buf) < length))
    goto wrong_size;

  dv_parse_packs (dvdec->decoder, inframe);

  if (dvdec->video_offset % dvdec->drop_factor != 0)
    goto skip;

  /* renegotiate on change */
  if (PAL != dvdec->PAL || wide != dvdec->wide) {
    dvdec->src_negotiated = FALSE;
    dvdec->PAL = PAL;
    dvdec->wide = wide;
  }

  dvdec->height = (dvdec->PAL ? PAL_HEIGHT : NTSC_HEIGHT);

  dvdec->interlaced = !dv_is_progressive (dvdec->decoder);

  /* negotiate if not done yet */
  if (!dvdec->src_negotiated) {
    if (!gst_dvdec_src_negotiate (dvdec))
      goto not_negotiated;
  }

  ret =
      gst_pad_alloc_buffer_and_set_caps (dvdec->srcpad, 0,
      (720 * dvdec->height) * dvdec->bpp,
      GST_PAD_CAPS (dvdec->srcpad), &outbuf);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto no_buffer;

  outframe = GST_BUFFER_DATA (outbuf);

  outframe_ptrs[0] = outframe;
  outframe_pitches[0] = 720 * dvdec->bpp;

  /* the rest only matters for YUY2 */
  if (dvdec->bpp < 3) {
    outframe_ptrs[1] = outframe_ptrs[0] + 720 * dvdec->height;
    outframe_ptrs[2] = outframe_ptrs[1] + 360 * dvdec->height;

    outframe_pitches[1] = dvdec->height / 2;
    outframe_pitches[2] = outframe_pitches[1];
  }

  GST_DEBUG_OBJECT (dvdec, "decoding and pushing buffer");
  dv_decode_full_frame (dvdec->decoder, inframe,
      e_dv_color_yuv, outframe_ptrs, outframe_pitches);

  GST_BUFFER_FLAG_UNSET (outbuf, GST_VIDEO_BUFFER_TFF);

  GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET (buf);
  GST_BUFFER_OFFSET_END (outbuf) = GST_BUFFER_OFFSET_END (buf);
  GST_BUFFER_TIMESTAMP (outbuf) = cstart;
  GST_BUFFER_DURATION (outbuf) = cstop - cstart;

  ret = gst_pad_push (dvdec->srcpad, outbuf);

skip:
  dvdec->video_offset++;

done:
  gst_buffer_unref (buf);
  gst_object_unref (dvdec);

  return ret;

  /* ERRORS */
wrong_size:
  {
    GST_ELEMENT_ERROR (dvdec, STREAM, DECODE,
        (NULL), ("Input buffer too small"));
    ret = GST_FLOW_ERROR;
    goto done;
  }
parse_header_error:
  {
    GST_ELEMENT_ERROR (dvdec, STREAM, DECODE,
        (NULL), ("Error parsing DV header"));
    ret = GST_FLOW_ERROR;
    goto done;
  }
not_negotiated:
  {
    GST_DEBUG_OBJECT (dvdec, "could not negotiate output");
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
no_buffer:
  {
    GST_DEBUG_OBJECT (dvdec, "could not allocate buffer");
    goto done;
  }

dropping:
  {
    GST_DEBUG_OBJECT (dvdec,
        "dropping buffer since it's out of the configured segment");
    goto done;
  }
}

static GstStateChangeReturn
gst_dvdec_change_state (GstElement * element, GstStateChange transition)
{
  GstDVDec *dvdec = GST_DVDEC (element);
  GstStateChangeReturn ret;


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      dvdec->decoder =
          dv_decoder_new (0, dvdec->clamp_luma, dvdec->clamp_chroma);
      dvdec->decoder->quality = qualities[dvdec->quality];
      dv_set_error_log (dvdec->decoder, NULL);
      gst_segment_init (dvdec->segment, GST_FORMAT_UNDEFINED);
      dvdec->src_negotiated = FALSE;
      dvdec->sink_negotiated = FALSE;
      /* 
       * Enable this function call when libdv2 0.100 or higher is more
       * common
       */
      /* dv_set_quality (dvdec->decoder, qualities [dvdec->quality]); */
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      dv_decoder_free (dvdec->decoder);
      dvdec->decoder = NULL;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

static void
gst_dvdec_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstDVDec *dvdec = GST_DVDEC (object);

  switch (prop_id) {
    case PROP_CLAMP_LUMA:
      dvdec->clamp_luma = g_value_get_boolean (value);
      break;
    case PROP_CLAMP_CHROMA:
      dvdec->clamp_chroma = g_value_get_boolean (value);
      break;
    case PROP_QUALITY:
      dvdec->quality = g_value_get_enum (value);
      if ((dvdec->quality < 0) || (dvdec->quality > 5))
        dvdec->quality = 0;
      break;
    case PROP_DECODE_NTH:
      dvdec->drop_factor = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dvdec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstDVDec *dvdec = GST_DVDEC (object);

  switch (prop_id) {
    case PROP_CLAMP_LUMA:
      g_value_set_boolean (value, dvdec->clamp_luma);
      break;
    case PROP_CLAMP_CHROMA:
      g_value_set_boolean (value, dvdec->clamp_chroma);
      break;
    case PROP_QUALITY:
      g_value_set_enum (value, dvdec->quality);
      break;
    case PROP_DECODE_NTH:
      g_value_set_int (value, dvdec->drop_factor);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
