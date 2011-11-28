/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2006> Mark Nauwelaerts <mnauw@users.sourceforge.net>
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
 * SECTION:element-y4menc
 *
 * <refsect2>
 * <para>
 * Creates a YU4MPEG2 raw video stream as defined by the mjpegtools project.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * (write everything in one line, without the backslash characters)
 * <programlisting>
 * gst-launch-0.10 videotestsrc num-buffers=250 \
 * ! 'video/x-raw-yuv,format=(fourcc)I420,width=320,height=240,framerate=(fraction)25/1' \
 * ! y4menc ! filesink location=test.yuv
 * </programlisting>
 * </para>
 * </refsect2>
 *
 */

/* see mjpegtools/yuv4mpeg.h for yuv4mpeg format */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include "gsty4mencode.h"

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate y4mencode_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-yuv4mpeg, " "y4mversion = (int) 2")
    );

static GstStaticPadTemplate y4mencode_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ IYUV, I420, Y42B, Y41B, Y444 }"))
    );


static void gst_y4m_encode_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_y4m_encode_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_y4m_encode_reset (GstY4mEncode * filter);

static gboolean gst_y4m_encode_setcaps (GstPad * pad, GstCaps * vscaps);
static GstFlowReturn gst_y4m_encode_chain (GstPad * pad, GstBuffer * buf);
static GstStateChangeReturn gst_y4m_encode_change_state (GstElement * element,
    GstStateChange transition);

GST_BOILERPLATE (GstY4mEncode, gst_y4m_encode, GstElement, GST_TYPE_ELEMENT);


static void
gst_y4m_encode_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &y4mencode_src_factory);
  gst_element_class_add_static_pad_template (element_class,
      &y4mencode_sink_factory);
  gst_element_class_set_details_simple (element_class, "YUV4MPEG video encoder",
      "Codec/Encoder/Video",
      "Encodes a YUV frame into the yuv4mpeg format (mjpegtools)",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_y4m_encode_class_init (GstY4mEncodeClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_y4m_encode_change_state);

  gobject_class->set_property = gst_y4m_encode_set_property;
  gobject_class->get_property = gst_y4m_encode_get_property;
}

static void
gst_y4m_encode_init (GstY4mEncode * filter, GstY4mEncodeClass * klass)
{
  filter->sinkpad =
      gst_pad_new_from_static_template (&y4mencode_sink_factory, "sink");
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_y4m_encode_chain));
  gst_pad_set_setcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_y4m_encode_setcaps));

  filter->srcpad =
      gst_pad_new_from_static_template (&y4mencode_src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  gst_pad_use_fixed_caps (filter->srcpad);

  /* init properties */
  gst_y4m_encode_reset (filter);
}

static void
gst_y4m_encode_reset (GstY4mEncode * filter)
{
  filter->width = filter->height = -1;
  filter->fps_num = filter->fps_den = 1;
  filter->par_num = filter->par_den = 1;
  filter->colorspace = "unknown";
}

static gboolean
gst_y4m_encode_setcaps (GstPad * pad, GstCaps * vscaps)
{
  GstY4mEncode *filter;
  GstStructure *structure;
  gboolean res;
  gint w, h;
  guint32 fourcc;
  const GValue *fps, *par, *interlaced;

  filter = GST_Y4M_ENCODE (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (vscaps, 0);

  res = gst_structure_get_int (structure, "width", &w);
  res &= gst_structure_get_int (structure, "height", &h);
  res &= ((fps = gst_structure_get_value (structure, "framerate")) != NULL);
  res &= gst_structure_get_fourcc (structure, "format", &fourcc);

  switch (fourcc) {             /* Translate fourcc to Y4M colorspace code */
    case GST_MAKE_FOURCC ('I', '4', '2', '0'):
    case GST_MAKE_FOURCC ('I', 'Y', 'U', 'V'):
      filter->colorspace = "420";
      break;
    case GST_MAKE_FOURCC ('Y', '4', '2', 'B'):
      filter->colorspace = "422";
      break;
    case GST_MAKE_FOURCC ('Y', '4', '1', 'B'):
      filter->colorspace = "411";
      break;
    case GST_MAKE_FOURCC ('Y', '4', '4', '4'):
      filter->colorspace = "444";
      break;
    default:
      res = FALSE;
      break;
  }

  if (!res || w <= 0 || h <= 0 || !GST_VALUE_HOLDS_FRACTION (fps))
    return FALSE;

  /* optional interlaced info */
  interlaced = gst_structure_get_value (structure, "interlaced");

  /* optional par info */
  par = gst_structure_get_value (structure, "pixel-aspect-ratio");

  filter->width = w;
  filter->height = h;
  filter->fps_num = gst_value_get_fraction_numerator (fps);
  filter->fps_den = gst_value_get_fraction_denominator (fps);
  if ((par != NULL) && GST_VALUE_HOLDS_FRACTION (par)) {
    filter->par_num = gst_value_get_fraction_numerator (par);
    filter->par_den = gst_value_get_fraction_denominator (par);
  } else {                      /* indicates unknown */
    filter->par_num = 0;
    filter->par_den = 0;
  }
  if ((interlaced != NULL) && G_VALUE_HOLDS (interlaced, G_TYPE_BOOLEAN)) {
    filter->interlaced = g_value_get_boolean (interlaced);
  } else {
    /* assume progressive if no interlaced property in caps */
    filter->interlaced = FALSE;
  }
  /* the template caps will do for the src pad, should always accept */
  return gst_pad_set_caps (filter->srcpad,
      gst_static_pad_template_get_caps (&y4mencode_src_factory));
}

static inline GstBuffer *
gst_y4m_encode_get_stream_header (GstY4mEncode * filter)
{
  gpointer header;
  GstBuffer *buf;
  gchar interlaced;

  interlaced = 'p';

  if (filter->interlaced && filter->top_field_first)
    interlaced = 't';
  else if (filter->interlaced)
    interlaced = 'b';

  header = g_strdup_printf ("YUV4MPEG2 C%s W%d H%d I%c F%d:%d A%d:%d\n",
      filter->colorspace, filter->width, filter->height, interlaced,
      filter->fps_num, filter->fps_den, filter->par_num, filter->par_den);

  buf = gst_buffer_new ();
  gst_buffer_set_data (buf, header, strlen (header));
  /* so it gets free'd when needed */
  GST_BUFFER_MALLOCDATA (buf) = header;

  return buf;
}

static inline GstBuffer *
gst_y4m_encode_get_frame_header (GstY4mEncode * filter)
{
  gpointer header;
  GstBuffer *buf;

  header = g_strdup_printf ("FRAME\n");

  buf = gst_buffer_new ();
  gst_buffer_set_data (buf, header, strlen (header));
  /* so it gets free'd when needed */
  GST_BUFFER_MALLOCDATA (buf) = header;

  return buf;
}

static GstFlowReturn
gst_y4m_encode_chain (GstPad * pad, GstBuffer * buf)
{
  GstY4mEncode *filter = GST_Y4M_ENCODE (GST_PAD_PARENT (pad));
  GstBuffer *outbuf;
  GstClockTime timestamp;

  /* check we got some decent info from caps */
  if (filter->width < 0) {
    GST_ELEMENT_ERROR ("filter", CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before chain function"));
    gst_buffer_unref (buf);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  timestamp = GST_BUFFER_TIMESTAMP (buf);

  if (G_UNLIKELY (!filter->header)) {
    if (filter->interlaced == TRUE) {
      if (GST_BUFFER_FLAG_IS_SET (buf, GST_VIDEO_BUFFER_TFF)) {
        filter->top_field_first = TRUE;
      } else {
        filter->top_field_first = FALSE;
      }
    }
    outbuf = gst_y4m_encode_get_stream_header (filter);
    filter->header = TRUE;
    outbuf = gst_buffer_join (outbuf, gst_y4m_encode_get_frame_header (filter));
  } else {
    outbuf = gst_y4m_encode_get_frame_header (filter);
  }
  /* join with data */
  outbuf = gst_buffer_join (outbuf, buf);
  /* decorate */
  gst_buffer_make_metadata_writable (outbuf);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (filter->srcpad));

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

  return gst_pad_push (filter->srcpad, outbuf);
}

static void
gst_y4m_encode_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstY4mEncode G_GNUC_UNUSED *filter;

  g_return_if_fail (GST_IS_Y4M_ENCODE (object));
  filter = GST_Y4M_ENCODE (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_y4m_encode_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstY4mEncode G_GNUC_UNUSED *filter;

  g_return_if_fail (GST_IS_Y4M_ENCODE (object));
  filter = GST_Y4M_ENCODE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_y4m_encode_change_state (GstElement * element, GstStateChange transition)
{
  GstY4mEncode *filter = GST_Y4M_ENCODE (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  ret = GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element, transition), GST_STATE_CHANGE_SUCCESS);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_y4m_encode_reset (filter);
      break;
    default:
      break;
  }

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "y4menc", GST_RANK_PRIMARY,
      GST_TYPE_Y4M_ENCODE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "y4menc",
    "Encodes a YUV frame into the yuv4mpeg format (mjpegtools)",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
