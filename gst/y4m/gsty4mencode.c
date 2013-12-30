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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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
 * ! 'video/x-raw,format=(string)I420,width=320,height=240,framerate=(fraction)25/1' \
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
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ IYUV, I420, Y42B, Y41B, Y444 }"))
    );


static void gst_y4m_encode_reset (GstY4mEncode * filter);

static gboolean gst_y4m_encode_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_y4m_encode_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static GstStateChangeReturn gst_y4m_encode_change_state (GstElement * element,
    GstStateChange transition);

#define gst_y4m_encode_parent_class parent_class
G_DEFINE_TYPE (GstY4mEncode, gst_y4m_encode, GST_TYPE_ELEMENT);

static void
gst_y4m_encode_class_init (GstY4mEncodeClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_y4m_encode_change_state);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&y4mencode_src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&y4mencode_sink_factory));

  gst_element_class_set_static_metadata (gstelement_class,
      "YUV4MPEG video encoder", "Codec/Encoder/Video",
      "Encodes a YUV frame into the yuv4mpeg format (mjpegtools)",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_y4m_encode_init (GstY4mEncode * filter)
{
  filter->sinkpad =
      gst_pad_new_from_static_template (&y4mencode_sink_factory, "sink");
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_y4m_encode_chain));
  gst_pad_set_event_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_y4m_encode_sink_event));

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
  filter->negotiated = FALSE;
}

static gboolean
gst_y4m_encode_setcaps (GstPad * pad, GstCaps * vscaps)
{
  gboolean ret;
  GstY4mEncode *filter;
  GstVideoInfo info;

  filter = GST_Y4M_ENCODE (GST_PAD_PARENT (pad));

  if (!gst_video_info_from_caps (&info, vscaps))
    goto invalid_format;

  switch (GST_VIDEO_INFO_FORMAT (&info)) {
    case GST_VIDEO_FORMAT_I420:
      filter->colorspace = "420";
      break;
    case GST_VIDEO_FORMAT_Y42B:
      filter->colorspace = "422";
      break;
    case GST_VIDEO_FORMAT_Y41B:
      filter->colorspace = "411";
      break;
    case GST_VIDEO_FORMAT_Y444:
      filter->colorspace = "444";
      break;
    default:
      goto invalid_format;
  }

  filter->info = info;

  /* the template caps will do for the src pad, should always accept */
  ret = gst_pad_set_caps (filter->srcpad,
      gst_static_pad_template_get_caps (&y4mencode_src_factory));

  filter->negotiated = ret;

  return ret;

  /* ERRORS */
invalid_format:
  {
    GST_ERROR_OBJECT (filter, "got invalid caps");
    return FALSE;
  }
}

static gboolean
gst_y4m_encode_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_y4m_encode_setcaps (pad, caps);
      gst_event_unref (event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }
  return res;
}

static inline GstBuffer *
gst_y4m_encode_get_stream_header (GstY4mEncode * filter, gboolean tff)
{
  gpointer header;
  GstBuffer *buf;
  gchar interlaced;
  gsize len;

  if (GST_VIDEO_INFO_IS_INTERLACED (&filter->info)) {
    if (tff)
      interlaced = 't';
    else
      interlaced = 'b';
  } else {
    interlaced = 'p';
  }

  header = g_strdup_printf ("YUV4MPEG2 C%s W%d H%d I%c F%d:%d A%d:%d\n",
      filter->colorspace, GST_VIDEO_INFO_WIDTH (&filter->info),
      GST_VIDEO_INFO_HEIGHT (&filter->info), interlaced,
      GST_VIDEO_INFO_FPS_N (&filter->info),
      GST_VIDEO_INFO_FPS_D (&filter->info),
      GST_VIDEO_INFO_PAR_N (&filter->info),
      GST_VIDEO_INFO_PAR_D (&filter->info));
  len = strlen (header);

  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, header, len, 0, len, header, g_free));

  return buf;
}

static inline GstBuffer *
gst_y4m_encode_get_frame_header (GstY4mEncode * filter)
{
  gpointer header;
  GstBuffer *buf;
  gsize len;

  header = g_strdup_printf ("FRAME\n");
  len = strlen (header);

  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, header, len, 0, len, header, g_free));

  return buf;
}

static GstFlowReturn
gst_y4m_encode_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstY4mEncode *filter = GST_Y4M_ENCODE (parent);
  GstBuffer *outbuf;
  GstClockTime timestamp;

  /* check we got some decent info from caps */
  if (GST_VIDEO_INFO_FORMAT (&filter->info) == GST_VIDEO_FORMAT_UNKNOWN)
    goto not_negotiated;

  timestamp = GST_BUFFER_TIMESTAMP (buf);

  if (G_UNLIKELY (!filter->header)) {
    gboolean tff = FALSE;

    if (GST_VIDEO_INFO_IS_INTERLACED (&filter->info)) {
      tff = GST_BUFFER_FLAG_IS_SET (buf, GST_VIDEO_BUFFER_FLAG_TFF);
    }
    outbuf = gst_y4m_encode_get_stream_header (filter, tff);
    filter->header = TRUE;
    outbuf =
        gst_buffer_append (outbuf, gst_y4m_encode_get_frame_header (filter));
  } else {
    outbuf = gst_y4m_encode_get_frame_header (filter);
  }
  /* join with data, FIXME, strides are all wrong etc */
  outbuf = gst_buffer_append (outbuf, buf);
  /* decorate */
  outbuf = gst_buffer_make_writable (outbuf);

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

  return gst_pad_push (filter->srcpad, outbuf);

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (filter, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before chain function"));
    gst_buffer_unref (buf);
    return GST_FLOW_NOT_NEGOTIATED;
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
    y4menc,
    "Encodes a YUV frame into the yuv4mpeg format (mjpegtools)",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
