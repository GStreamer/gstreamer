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
 * <refsect2>
 * <para>
 * dvdec decodes DV video into raw video. The element expects a full DV frame
 * as input, which is 120000 bytes for NTSC and 144000 for PAL video.
 * </para>
 * <para>
 * This element can perform simple frame dropping with the drop-factor
 * property. Setting this property to a value N > 1 will only decode every 
 * Nth frame.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch filesrc location=test.dv ! dvdemux name=demux ! dvdec ! xvimagesink
 * </programlisting>
 * This pipeline decodes and renders the raw DV stream to a videosink.
 * </para>
 * Last reviewed on 2006-02-28 (0.10.3)
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <math.h>

#include "gstdvdec.h"


static GstElementDetails dvdec_details =
GST_ELEMENT_DETAILS ("DV (smpte314) decoder plugin",
    "Codec/Decoder/Video",
    "Uses libdv to decode DV video (libdv.sourceforge.net)",
    "Erik Walthinsen <omega@cse.ogi.edu>," "Wim Taymans <wim@fluendo.com>");

/* sizes of one input buffer */
#define NTSC_BUFFER 120000
#define PAL_BUFFER 144000

#define DV_DEFAULT_QUALITY DV_QUALITY_BEST
#define DV_DEFAULT_DECODE_NTH 1

GST_DEBUG_CATEGORY (dvdec_debug);
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
GType
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

static gboolean gst_dvdec_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_dvdec_chain (GstPad * pad, GstBuffer * buffer);

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

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_temp));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_temp));

  gst_element_class_set_details (element_class, &dvdec_details);

  GST_DEBUG_CATEGORY_INIT (dvdec_debug, "dvdec", 0, "DV decoding element");
}

static void
gst_dvdec_class_init (GstDVDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_dvdec_set_property;
  gobject_class->get_property = gst_dvdec_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CLAMP_LUMA,
      g_param_spec_boolean ("clamp_luma", "Clamp luma", "Clamp luma",
          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CLAMP_CHROMA,
      g_param_spec_boolean ("clamp_chroma", "Clamp chroma", "Clamp chroma",
          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_QUALITY,
      g_param_spec_enum ("quality", "Quality", "Decoding quality",
          GST_TYPE_DVDEC_QUALITY, DV_DEFAULT_QUALITY, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DECODE_NTH,
      g_param_spec_int ("drop-factor", "Drop Factor", "Only decode Nth frame",
          1, G_MAXINT, DV_DEFAULT_DECODE_NTH, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_dvdec_change_state;

  /* table initialization, only do once */
  dv_init (0, 0);
}

static void
gst_dvdec_init (GstDVDec * dvdec, GstDVDecClass * g_class)
{
  dvdec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_temp),
      "sink");
  gst_pad_set_setcaps_function (dvdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvdec_sink_setcaps));
  gst_pad_set_chain_function (dvdec->sinkpad, gst_dvdec_chain);
  gst_element_add_pad (GST_ELEMENT (dvdec), dvdec->sinkpad);

  dvdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_temp),
      "src");

  gst_pad_use_fixed_caps (dvdec->srcpad);

  gst_element_add_pad (GST_ELEMENT (dvdec), dvdec->srcpad);

  dvdec->framerate_numerator = 0;
  dvdec->framerate_denominator = 0;
  dvdec->height = 0;
  dvdec->wide = FALSE;
  dvdec->drop_factor = 1;

  dvdec->clamp_luma = FALSE;
  dvdec->clamp_chroma = FALSE;
  dvdec->quality = DV_DEFAULT_QUALITY;
}

static gboolean
gst_dvdec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstDVDec *dvdec;
  GstStructure *s;
  GstCaps *othercaps;
  gboolean gotpar = FALSE;
  const GValue *par = NULL, *rate = NULL;

  dvdec = GST_DVDEC (gst_pad_get_parent (pad));

  /* first parse the caps */
  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "height", &dvdec->height))
    goto error;
  if ((par = gst_structure_get_value (s, "pixel-aspect-ratio")))
    gotpar = TRUE;
  if (!(rate = gst_structure_get_value (s, "framerate")))
    goto error;

  dvdec->framerate_numerator = gst_value_get_fraction_numerator (rate);
  dvdec->framerate_denominator = gst_value_get_fraction_denominator (rate);

  /* ignoring rgb, bgr0 for now */

  dvdec->bpp = 2;

  othercaps = gst_caps_new_simple ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, GST_STR_FOURCC ("YUY2"),
      "width", G_TYPE_INT, 720,
      "height", G_TYPE_INT, dvdec->height,
      "framerate", GST_TYPE_FRACTION, dvdec->framerate_numerator,
      dvdec->framerate_denominator, NULL);
  if (gotpar)
    gst_structure_set_value (gst_caps_get_structure (othercaps, 0),
        "pixel-aspect-ratio", par);

  gst_pad_set_caps (dvdec->srcpad, othercaps);

  gst_caps_unref (othercaps);

  gst_object_unref (dvdec);

  return TRUE;

error:
  {
    gst_object_unref (dvdec);

    return FALSE;
  }
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

  dvdec = GST_DVDEC (gst_pad_get_parent (pad));
  inframe = GST_BUFFER_DATA (buf);

  /* buffer should be at least the size of one NTSC frame, this should
   * be enough to decode the header. */
  if (G_UNLIKELY (GST_BUFFER_SIZE (buf) < NTSC_BUFFER))
    goto wrong_size;

  if (G_UNLIKELY (dv_parse_header (dvdec->decoder, inframe) < 0))
    goto parse_header_error;

  /* check the buffer is of right size after we know if we are
   * dealing with PAL or NTSC */
  length = (dvdec->PAL ? PAL_BUFFER : NTSC_BUFFER);
  if (G_UNLIKELY (GST_BUFFER_SIZE (buf) < length))
    goto wrong_size;

  dv_parse_packs (dvdec->decoder, inframe);

  if (dvdec->video_offset % dvdec->drop_factor != 0)
    goto skip;

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

  gst_buffer_stamp (outbuf, buf);

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
no_buffer:
  {
    GST_DEBUG_OBJECT (dvdec, "could not allocate buffer");
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
