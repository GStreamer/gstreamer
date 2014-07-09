/*
 * Copyright (c) <2013-2014>, Intel Corporation.
 * Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
/*SECTION: analyzersink
 * A sink element to generate xml and hex files for each
 * video frame providing by the upstream parser element
 */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstanalyzersink.h"

#include <string.h>

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, mpegversion=2" ";"
        "video/x-h264" ";" "video/x-h265"));

/* AnalyzerSink signals and args */
enum
{
  SIGNAL_NEW_FRAME,
  LAST_SIGNAL
};

#define DEFAULT_SYNC FALSE
#define DEFAULT_DUMP TRUE
#define DEFAULT_NUM_BUFFERS -1

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_SILENT,
  PROP_DUMP,
  PROP_NUM_BUFFERS
};

#define gst_analyzer_sink_parent_class parent_class
G_DEFINE_TYPE (GstAnalyzerSink, gst_analyzer_sink, GST_TYPE_BASE_SINK);

static gboolean gst_analyzer_sink_set_caps (GstBaseSink * sink, GstCaps * caps);
static void gst_analyzer_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_analyzer_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_analyzer_sink_finalize (GObject * obj);

static GstStateChangeReturn gst_analyzer_sink_change_state (GstElement *
    element, GstStateChange transition);

static GstFlowReturn gst_analyzer_sink_render (GstBaseSink * bsink,
    GstBuffer * buffer);
static gboolean gst_analyzer_sink_event (GstBaseSink * bsink, GstEvent * event);
static gboolean gst_analyzer_sink_query (GstBaseSink * bsink, GstQuery * query);

static guint gst_analyzer_sink_signals[LAST_SIGNAL] = { 0 };

static void
gst_analyzer_sink_class_init (GstAnalyzerSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbase_sink_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbase_sink_class = GST_BASE_SINK_CLASS (klass);

  gstbase_sink_class->set_caps = gst_analyzer_sink_set_caps;
  gobject_class->set_property = gst_analyzer_sink_set_property;
  gobject_class->get_property = gst_analyzer_sink_get_property;
  gobject_class->finalize = gst_analyzer_sink_finalize;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "xml/hex files location",
          "Location of the xml/hex folder/files to write", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DUMP,
      g_param_spec_boolean ("dump", "Dump",
          "Dump frame contents as hex to the specified location", DEFAULT_DUMP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_NUM_BUFFERS,
      g_param_spec_int ("num-frames", "num-frames",
          "Number of frames to accept before going EOS", -1, G_MAXINT,
          DEFAULT_NUM_BUFFERS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstAnalyzerSink::new-frame:
   * @analyzersink: the analyzersink instance
   * @buffer: the buffer that just has been received and analysed
   * @frame_num: the frame count
   *
   * This signal gets emitted before unreffing the buffer.
   */
  gst_analyzer_sink_signals[SIGNAL_NEW_FRAME] =
      g_signal_new ("new-frame", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstAnalyzerSinkClass, new_frame), NULL, NULL,
      g_cclosure_marshal_generic, G_TYPE_NONE, 2,
      GST_TYPE_BUFFER | G_SIGNAL_TYPE_STATIC_SCOPE, G_TYPE_INT);

  gst_element_class_set_static_metadata (gstelement_class,
      "Codec Analyzer Sink",
      "Sink",
      "Sink to dump the parsed information",
      "Sreerenj Balachandran<sreerenj.balachandran@intel.com>");
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_analyzer_sink_change_state);

  gstbase_sink_class->event = GST_DEBUG_FUNCPTR (gst_analyzer_sink_event);
  gstbase_sink_class->render = GST_DEBUG_FUNCPTR (gst_analyzer_sink_render);
  gstbase_sink_class->query = GST_DEBUG_FUNCPTR (gst_analyzer_sink_query);
}

static void
gst_analyzer_sink_init (GstAnalyzerSink * analyzersink)
{
  analyzersink->dump = DEFAULT_DUMP;
  analyzersink->num_buffers = DEFAULT_NUM_BUFFERS;
  analyzersink->codec_type = GST_ANALYZER_CODEC_UNKNOWN;
  analyzersink->frame_num = 0;
  analyzersink->location = NULL;
  /* XXX: Add a generic structure to handle different codecs */
  analyzersink->mpeg2_hdrs = g_slice_new0 (Mpeg2Headers);
  gst_base_sink_set_sync (GST_BASE_SINK (analyzersink), DEFAULT_SYNC);
}

static void
gst_analyzer_sink_finalize (GObject * obj)
{
  GstAnalyzerSink *sink = GST_ANALYZER_SINK (obj);

  if (sink->location)
    g_free (sink->location);

  if (sink->mpeg2_hdrs) {
    if (sink->mpeg2_hdrs->sequencehdr)
      g_slice_free (GstMpegVideoSequenceHdr, sink->mpeg2_hdrs->sequencehdr);
    if (sink->mpeg2_hdrs->sequenceext)
      g_slice_free (GstMpegVideoSequenceExt, sink->mpeg2_hdrs->sequenceext);
    if (sink->mpeg2_hdrs->sequencedispext)
      g_slice_free (GstMpegVideoSequenceDisplayExt,
          sink->mpeg2_hdrs->sequencedispext);
    if (sink->mpeg2_hdrs->quantext)
      g_slice_free (GstMpegVideoQuantMatrixExt, sink->mpeg2_hdrs->quantext);
    g_slice_free (Mpeg2Headers, sink->mpeg2_hdrs);
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static gboolean
gst_analyzer_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstStructure *structure;
  GstAnalyzerSink *sink = GST_ANALYZER_SINK (bsink);

  if (!caps)
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);
  const gchar *name = gst_structure_get_name (structure);

  if (!strcmp (name, "video/mpeg"))
    sink->codec_type = GST_ANALYZER_CODEC_MPEG2_VIDEO;
  else
    return FALSE;

  return TRUE;
}

static void
gst_analyzer_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAnalyzerSink *sink;
  const gchar *location;

  sink = GST_ANALYZER_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      location = g_value_get_string (value);
      if (sink->location)
        g_free (sink->location);
      sink->location = g_strdup (location);
      break;
    case PROP_DUMP:
      sink->dump = g_value_get_boolean (value);
      break;
    case PROP_NUM_BUFFERS:
      sink->num_buffers = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_analyzer_sink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAnalyzerSink *sink;

  sink = GST_ANALYZER_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, sink->location);
      break;
    case PROP_DUMP:
      g_value_set_boolean (value, sink->dump);
      break;
    case PROP_NUM_BUFFERS:
      g_value_set_int (value, sink->num_buffers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_analyzer_sink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstAnalyzerSink *sink = GST_ANALYZER_SINK (bsink);

  return GST_BASE_SINK_CLASS (parent_class)->event (bsink, event);
}

static gboolean
gst_analyzer_sink_dump_mem (GstAnalyzerSink * sink, const guchar * mem,
    guint size)
{
  GString *string;
  FILE *fd;
  gchar *name;
  gchar *file_name;
  guint i = 0, j = 0;

  GST_DEBUG ("dump frame content with size = %d", size);

  /* XXX: Add a generic structure to handle different codec name string
   * For now analyzersink can only handle mpeg2meta:*/

  /* create a new hex file for each frame */
  name = g_strdup_printf ("mpeg2-%d.hex", sink->frame_num);
  file_name = g_build_filename (sink->location, "hex", name, NULL);
  GST_LOG ("Created a New hex file %s to dump the content", file_name);
  free (name);

  fd = fopen (file_name, "w");
  if (fd == NULL)
    return FALSE;

  string = g_string_sized_new (50);

  while (i < size) {
    g_string_append_printf (string, "%02x   ", mem[i]);

    j++;
    i++;

    if (j == 32 || i == size) {
      fprintf (fd, "%s \n", string->str);
      g_string_set_size (string, 0);
      j = 0;
    }
  }
  g_string_free (string, TRUE);
  if (file_name)
    g_free (file_name);

  fclose (fd);
  return TRUE;
}

static GstFlowReturn
gst_analyzer_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstAnalyzerSink *sink = GST_ANALYZER_SINK_CAST (bsink);
  GstMpegVideoMeta *mpeg_meta;
  gboolean ret;

  if (sink->num_buffers_left == 0)
    goto eos;

  if (sink->num_buffers_left != -1)
    sink->num_buffers_left--;

  if (sink->dump) {
    GstMapInfo info;

    gst_buffer_map (buf, &info, GST_MAP_READ);
    ret = gst_analyzer_sink_dump_mem (sink, info.data, info.size);
    gst_buffer_unmap (buf, &info);
  }

  switch (sink->codec_type) {
    case GST_ANALYZER_CODEC_MPEG2_VIDEO:
    {
      mpeg_meta = gst_buffer_get_mpeg_video_meta (buf);
      if (!mpeg_meta)
        goto no_mpeg_meta;

      GST_DEBUG_OBJECT (sink,
          "creatin mpeg2video_frame_xml for mpeg2frame with num=%d \n",
          sink->frame_num);
      if (!analyzer_create_mpeg2video_frame_xml (mpeg_meta, sink->location,
              sink->frame_num, sink->mpeg2_hdrs))
        goto error_create_xml;
    }
      break;

    case GST_ANALYZER_CODEC_H264:
    case GST_ANALYZER_CODEC_VC1:
    case GST_ANALYZER_CODEC_MPEG4_PART_TWO:
    case GST_ANALYZER_CODEC_H265:
    {
      GST_WARNING ("No codec support in analyzer sink");
      goto unknown_codec;
    }
      break;

    case GST_ANALYZER_CODEC_UNKNOWN:
    default:
      goto unknown_codec;
  }

  g_signal_emit (sink, gst_analyzer_sink_signals[SIGNAL_NEW_FRAME], 0, buf,
      sink->frame_num);
  sink->frame_num++;

  if (sink->num_buffers_left == 0)
    goto eos;

  return GST_FLOW_OK;

  /* ERRORS */
no_mpeg_meta:
  {
    GST_DEBUG_OBJECT (sink, "no mpeg meta");
    return GST_FLOW_EOS;
  }
unknown_codec:
  {
    GST_DEBUG_OBJECT (sink, "unknown codec");
    return GST_FLOW_EOS;
  }
error_create_xml:
  {
    GST_DEBUG_OBJECT (sink, "failed to create xml for meta");
    return GST_FLOW_EOS;
  }
eos:
  {
    GST_DEBUG_OBJECT (sink, "we are EOS");
    return GST_FLOW_EOS;
  }
}

static gboolean
gst_analyzer_sink_query (GstBaseSink * bsink, GstQuery * query)
{
  gboolean ret;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_SEEKING:{
      GstFormat fmt;

      /* we don't supporting seeking */
      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      gst_query_set_seeking (query, fmt, FALSE, 0, -1);
      ret = TRUE;
      break;
    }
    default:
      ret = GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);
      break;
  }

  return ret;
}

static GstStateChangeReturn
gst_analyzer_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstAnalyzerSink *analyzersink = GST_ANALYZER_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      analyzersink->num_buffers_left = analyzersink->num_buffers;
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
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;

  /* ERROR */
error:
  GST_ELEMENT_ERROR (element, CORE, STATE_CHANGE, (NULL),
      ("Erroring out on state change as requested"));
  return GST_STATE_CHANGE_FAILURE;
}
