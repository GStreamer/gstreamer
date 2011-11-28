/* GStreamer unit tests for playbin2 compressed stream support
 *
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <gst/base/gstpushsrc.h>
#include <gst/base/gstbasesink.h>
#include <gst/interfaces/streamvolume.h>

#ifndef GST_DISABLE_REGISTRY

#define NBUFFERS 100

static GType gst_caps_src_get_type (void);
static GType gst_codec_demuxer_get_type (void);
static GType gst_codec_sink_get_type (void);
static GType gst_audio_codec_sink_get_type (void);
static GType gst_video_codec_sink_get_type (void);

#undef parent_class
#define parent_class caps_src_parent_class
typedef struct _GstCapsSrc GstCapsSrc;
typedef GstPushSrcClass GstCapsSrcClass;

struct _GstCapsSrc
{
  GstPushSrc parent;

  GstCaps *caps;
  gchar *uri;
  gint nbuffers;
};

static GstURIType
gst_caps_src_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_caps_src_uri_get_protocols (void)
{
  static gchar *protocols[] = { (char *) "caps", NULL };

  return protocols;
}

static const gchar *
gst_caps_src_uri_get_uri (GstURIHandler * handler)
{
  GstCapsSrc *src = (GstCapsSrc *) handler;

  return src->uri;
}

static gboolean
gst_caps_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstCapsSrc *src = (GstCapsSrc *) handler;

  if (uri == NULL || !g_str_has_prefix (uri, "caps:"))
    return FALSE;

  g_free (src->uri);
  src->uri = g_strdup (uri);

  if (src->caps)
    gst_caps_unref (src->caps);
  src->caps = NULL;

  return TRUE;
}

static void
gst_caps_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_caps_src_uri_get_type;
  iface->get_protocols = gst_caps_src_uri_get_protocols;
  iface->get_uri = gst_caps_src_uri_get_uri;
  iface->set_uri = gst_caps_src_uri_set_uri;
}

static void
gst_caps_src_init_type (GType type)
{
  static const GInterfaceInfo uri_hdlr_info = {
    gst_caps_src_uri_handler_init, NULL, NULL
  };

  g_type_add_interface_static (type, GST_TYPE_URI_HANDLER, &uri_hdlr_info);
}

GST_BOILERPLATE_FULL (GstCapsSrc, gst_caps_src, GstPushSrc,
    GST_TYPE_PUSH_SRC, gst_caps_src_init_type);

static void
gst_caps_src_base_init (gpointer klass)
{
  static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_templ);
  gst_element_class_set_details_simple (element_class,
      "CapsSource", "Source/Generic", "yep", "me");
}

static void
gst_caps_src_finalize (GObject * object)
{
  GstCapsSrc *src = (GstCapsSrc *) object;

  if (src->caps)
    gst_caps_unref (src->caps);
  src->caps = NULL;
  g_free (src->uri);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstFlowReturn
gst_caps_src_create (GstPushSrc * psrc, GstBuffer ** p_buf)
{
  GstCapsSrc *src = (GstCapsSrc *) psrc;
  GstBuffer *buf;

  if (src->nbuffers >= NBUFFERS) {
    return GST_FLOW_UNEXPECTED;
  }

  if (!src->caps) {
    if (!src->uri) {
      return GST_FLOW_ERROR;
    }

    src->caps = gst_caps_from_string (src->uri + sizeof ("caps"));
    if (!src->caps) {
      return GST_FLOW_ERROR;
    }
  }

  buf = gst_buffer_new ();
  gst_buffer_set_caps (buf, src->caps);
  GST_BUFFER_TIMESTAMP (buf) =
      gst_util_uint64_scale (src->nbuffers, GST_SECOND, 25);
  src->nbuffers++;

  *p_buf = buf;
  return GST_FLOW_OK;
}

static void
gst_caps_src_class_init (GstCapsSrcClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstPushSrcClass *pushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->finalize = gst_caps_src_finalize;
  pushsrc_class->create = gst_caps_src_create;
}

static void
gst_caps_src_init (GstCapsSrc * src, GstCapsSrcClass * klass)
{
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
}

#undef parent_class
#define parent_class codec_sink_parent_class

typedef struct _GstCodecSink GstCodecSink;
typedef GstBaseSinkClass GstCodecSinkClass;

struct _GstCodecSink
{
  GstBaseSink parent;

  gboolean audio;
  gboolean raw;
  gint n_raw, n_compressed;
};

GST_BOILERPLATE (GstCodecSink, gst_codec_sink, GstBaseSink, GST_TYPE_BASE_SINK);

static void
gst_codec_sink_base_init (gpointer klass)
{
}

static gboolean
gst_codec_sink_start (GstBaseSink * bsink)
{
  GstCodecSink *sink = (GstCodecSink *) bsink;

  sink->n_raw = 0;
  sink->n_compressed = 0;

  return TRUE;
}

static GstFlowReturn
gst_codec_sink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstCodecSink *sink = (GstCodecSink *) bsink;

  if (sink->raw)
    sink->n_raw++;
  else
    sink->n_compressed++;

  return GST_FLOW_OK;
}

static void
gst_codec_sink_class_init (GstCodecSinkClass * klass)
{
  GstBaseSinkClass *basesink_class = (GstBaseSinkClass *) klass;

  basesink_class->start = gst_codec_sink_start;
  basesink_class->render = gst_codec_sink_render;
}

static void
gst_codec_sink_init (GstCodecSink * sink, GstCodecSinkClass * klass)
{
}

#undef parent_class
#define parent_class audio_codec_sink_parent_class

typedef GstCodecSink GstAudioCodecSink;
typedef GstCodecSinkClass GstAudioCodecSinkClass;

static void
gst_audio_codec_sink_init_type (GType type)
{
  static const GInterfaceInfo svol_iface_info = {
    NULL, NULL, NULL
  };

  g_type_add_interface_static (type, GST_TYPE_STREAM_VOLUME, &svol_iface_info);
}

GST_BOILERPLATE_FULL (GstAudioCodecSink, gst_audio_codec_sink, GstBaseSink,
    gst_codec_sink_get_type (), gst_audio_codec_sink_init_type);

static void
gst_audio_codec_sink_base_init (gpointer klass)
{
  static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("audio/x-raw-int; audio/x-compressed")
      );
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &sink_templ);
  gst_element_class_set_details_simple (element_class,
      "AudioCodecSink", "Sink/Audio", "yep", "me");
}

static void
gst_audio_codec_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case 1:
    case 2:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_codec_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{

  switch (prop_id) {
    case 1:
      g_value_set_double (value, 1.0);
      break;
    case 2:
      g_value_set_boolean (value, FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_audio_codec_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstAudioCodecSink *sink = (GstAudioCodecSink *) bsink;
  GstStructure *s;

  s = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (s, "audio/x-raw-int")) {
    sink->raw = TRUE;
  } else if (gst_structure_has_name (s, "audio/x-compressed")) {
    sink->raw = FALSE;
  } else {
    fail_unless (gst_structure_has_name (s, "audio/x-raw-int")
        || gst_structure_has_name (s, "audio/x-compressed"));
    return FALSE;
  }

  return TRUE;
}

static void
gst_audio_codec_sink_class_init (GstAudioCodecSinkClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseSinkClass *basesink_class = (GstBaseSinkClass *) klass;

  gobject_class->set_property = gst_audio_codec_sink_set_property;
  gobject_class->get_property = gst_audio_codec_sink_get_property;

  g_object_class_install_property (gobject_class,
      1,
      g_param_spec_double ("volume", "Volume",
          "Linear volume of this stream, 1.0=100%", 0.0, 10.0,
          1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      2,
      g_param_spec_boolean ("mute", "Mute",
          "Mute", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  basesink_class->set_caps = gst_audio_codec_sink_set_caps;
}

static void
gst_audio_codec_sink_init (GstAudioCodecSink * sink,
    GstAudioCodecSinkClass * klass)
{
  sink->audio = TRUE;
}

#undef parent_class
#define parent_class video_codec_sink_parent_class

typedef GstCodecSink GstVideoCodecSink;
typedef GstCodecSinkClass GstVideoCodecSinkClass;

GST_BOILERPLATE (GstVideoCodecSink, gst_video_codec_sink, GstBaseSink,
    gst_codec_sink_get_type ());

static void
gst_video_codec_sink_base_init (gpointer klass)
{
  static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw-yuv; video/x-compressed")
      );
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &sink_templ);
  gst_element_class_set_details_simple (element_class,
      "VideoCodecSink", "Sink/Video", "yep", "me");
}

static gboolean
gst_video_codec_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstVideoCodecSink *sink = (GstVideoCodecSink *) bsink;
  GstStructure *s;

  s = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (s, "video/x-raw-yuv")) {
    sink->raw = TRUE;
  } else if (gst_structure_has_name (s, "video/x-compressed")) {
    sink->raw = FALSE;
  } else {
    fail_unless (gst_structure_has_name (s, "video/x-raw-yuv")
        || gst_structure_has_name (s, "video/x-compressed"));
    return FALSE;
  }

  return TRUE;
}

static void
gst_video_codec_sink_class_init (GstVideoCodecSinkClass * klass)
{
  GstBaseSinkClass *basesink_class = (GstBaseSinkClass *) klass;

  basesink_class->set_caps = gst_video_codec_sink_set_caps;
}

static void
gst_video_codec_sink_init (GstVideoCodecSink * sink,
    GstVideoCodecSinkClass * klass)
{
  sink->audio = FALSE;
}

#undef parent_class
#define parent_class codec_demuxer_parent_class
typedef struct _GstCodecDemuxer GstCodecDemuxer;
typedef GstElementClass GstCodecDemuxerClass;

struct _GstCodecDemuxer
{
  GstElement parent;

  GstPad *sinkpad;
  GstPad *srcpad0, *srcpad1;

  GstEvent *newseg_event;
};

GST_BOILERPLATE (GstCodecDemuxer, gst_codec_demuxer, GstElement,
    GST_TYPE_ELEMENT);

#define STREAM_TYPES "{ " \
    "none, " \
    "raw-audio, " \
    "compressed-audio, " \
    "raw-video, " \
    "compressed-video " \
    "}"

static void
gst_codec_demuxer_base_init (gpointer klass)
{
  static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("application/x-container,"
          " stream0 = (string)" STREAM_TYPES " ,"
          " stream1 = (string)" STREAM_TYPES)
      );
  static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src_%d",
      GST_PAD_SRC, GST_PAD_SOMETIMES,
      GST_STATIC_CAPS ("audio/x-raw-int; audio/x-compressed; "
          "video/x-raw-yuv; video/x-compressed")
      );
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &sink_templ);
  gst_element_class_add_static_pad_template (element_class, &src_templ);
  gst_element_class_set_details_simple (element_class,
      "CodecDemuxer", "Codec/Demuxer", "yep", "me");
}

static void
gst_codec_demuxer_finalize (GObject * object)
{
  GstCodecDemuxer *demux = (GstCodecDemuxer *) object;

  if (demux->newseg_event)
    gst_event_unref (demux->newseg_event);
  demux->newseg_event = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_codec_demuxer_class_init (GstCodecDemuxerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_codec_demuxer_finalize;
}

static GstFlowReturn
gst_codec_demuxer_chain (GstPad * pad, GstBuffer * buf)
{
  GstCodecDemuxer *demux = (GstCodecDemuxer *) GST_PAD_PARENT (pad);
  GstFlowReturn ret0 = GST_FLOW_OK, ret1 = GST_FLOW_OK;

  if (demux->srcpad0) {
    GstBuffer *outbuf = gst_buffer_new ();

    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (demux->srcpad0));
    ret0 = gst_pad_push (demux->srcpad0, outbuf);
  }
  if (demux->srcpad1) {
    GstBuffer *outbuf = gst_buffer_new ();

    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (demux->srcpad1));
    ret1 = gst_pad_push (demux->srcpad1, outbuf);
  }
  gst_buffer_unref (buf);

  if (ret0 == GST_FLOW_NOT_LINKED && ret1 == GST_FLOW_NOT_LINKED)
    return GST_FLOW_NOT_LINKED;
  if (ret0 == GST_FLOW_OK && ret1 == GST_FLOW_OK)
    return GST_FLOW_OK;

  return MIN (ret0, ret1);
}

static gboolean
gst_codec_demuxer_event (GstPad * pad, GstEvent * event)
{
  GstCodecDemuxer *demux = (GstCodecDemuxer *) gst_pad_get_parent (pad);
  gboolean ret = TRUE;

  /* The single newsegment event is pushed when the pads are created */
  if (GST_EVENT_TYPE (event) != GST_EVENT_NEWSEGMENT) {
    if (demux->srcpad0)
      ret = ret && gst_pad_push_event (demux->srcpad0, gst_event_ref (event));
    if (demux->srcpad1)
      ret = ret && gst_pad_push_event (demux->srcpad1, gst_event_ref (event));
  } else {
    gst_event_replace (&demux->newseg_event, event);
  }

  gst_event_unref (event);

  gst_object_unref (demux);
  return ret;
}

static void
gst_codec_demuxer_setup_pad (GstCodecDemuxer * demux, GstPad ** pad,
    const gchar * streaminfo)
{
  if (g_str_equal (streaminfo, "none")) {
    if (*pad) {
      gst_pad_set_active (*pad, FALSE);
      gst_element_remove_pad (GST_ELEMENT (demux), *pad);
      *pad = NULL;
    }
  } else {
    GstCaps *caps;

    if (!*pad) {
      GstPadTemplate *templ;

      templ =
          gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (demux),
          "src_%d");
      if (pad == &demux->srcpad0)
        *pad = gst_pad_new_from_template (templ, "src_0");
      else
        *pad = gst_pad_new_from_template (templ, "src_1");
      gst_pad_set_active (*pad, TRUE);
      gst_pad_use_fixed_caps (*pad);
      gst_element_add_pad (GST_ELEMENT (demux), *pad);
    }

    if (g_str_equal (streaminfo, "raw-video")) {
      caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
          "width", G_TYPE_INT, 320,
          "height", G_TYPE_INT, 240,
          "framerate", GST_TYPE_FRACTION, 25, 1,
          "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);
    } else if (g_str_equal (streaminfo, "compressed-video")) {
      caps = gst_caps_new_simple ("video/x-compressed", NULL);
    } else if (g_str_equal (streaminfo, "raw-audio")) {
      caps = gst_caps_new_simple ("audio/x-raw-int",
          "rate", G_TYPE_INT, 48000,
          "channels", G_TYPE_INT, 2,
          "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
          "width", G_TYPE_INT, 16,
          "depth", G_TYPE_INT, 16, "signed", G_TYPE_BOOLEAN, TRUE, NULL);
    } else {
      caps = gst_caps_new_simple ("audio/x-compressed", NULL);
    }
    gst_pad_set_caps (*pad, caps);
    gst_caps_unref (caps);

    if (demux->newseg_event)
      gst_pad_push_event (*pad, gst_event_ref (demux->newseg_event));
  }
}

static gboolean
gst_codec_demuxer_setcaps (GstPad * pad, GstCaps * caps)
{
  GstCodecDemuxer *demux = (GstCodecDemuxer *) gst_pad_get_parent (pad);
  GstStructure *s;
  const gchar *streaminfo;

  s = gst_caps_get_structure (caps, 0);

  streaminfo = gst_structure_get_string (s, "stream0");
  gst_codec_demuxer_setup_pad (demux, &demux->srcpad0, streaminfo);

  streaminfo = gst_structure_get_string (s, "stream1");
  gst_codec_demuxer_setup_pad (demux, &demux->srcpad1, streaminfo);

  gst_object_unref (demux);
  return TRUE;
}

static void
gst_codec_demuxer_init (GstCodecDemuxer * demux, GstCodecDemuxerClass * klass)
{
  GstPadTemplate *templ;

  templ = gst_element_class_get_pad_template (klass, "sink");
  demux->sinkpad = gst_pad_new_from_template (templ, "sink");
  gst_pad_set_setcaps_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_codec_demuxer_setcaps));
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_codec_demuxer_chain));
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_codec_demuxer_event));
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);
}

/****
 * Start of the tests
 ***/

static GstElement *
create_playbin (const gchar * uri, gboolean set_sink)
{
  GstElement *playbin, *sink;

  playbin = gst_element_factory_make ("playbin2", "playbin2");
  fail_unless (playbin != NULL, "Failed to create playbin element");

  if (set_sink) {
    sink = gst_element_factory_make ("videocodecsink", NULL);
    fail_unless (sink != NULL, "Failed to create videocodecsink element");

    /* Set sync to FALSE to prevent buffers from being dropped because
     * they're too late */
    g_object_set (sink, "sync", FALSE, NULL);

    g_object_set (playbin, "video-sink", sink, NULL);

    sink = gst_element_factory_make ("audiocodecsink", NULL);
    fail_unless (sink != NULL, "Failed to create audiocodecsink");

    /* Set sync to FALSE to prevent buffers from being dropped because
     * they're too late */
    g_object_set (sink, "sync", FALSE, NULL);

    g_object_set (playbin, "audio-sink", sink, NULL);
  }

  g_object_set (playbin, "uri", uri, NULL);

  return playbin;
}

GST_START_TEST (test_raw_single_video_stream_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin =
      create_playbin
      ("caps:video/x-raw-yuv, "
      "format=(fourcc)I420, "
      "width=(int)320, "
      "height=(int)240, "
      "framerate=(fraction)0/1, " "pixel-aspect-ratio=(fraction)1/1", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless (csink->raw == TRUE);
    fail_unless_equals_int (csink->n_raw, NBUFFERS);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless (csink->raw == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_compressed_single_video_stream_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:video/x-compressed", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless (csink->raw == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, NBUFFERS);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless (csink->raw == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_raw_single_video_stream_demuxer_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)raw-video, " "stream1=(string)none", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless (csink->raw == TRUE);
    fail_unless_equals_int (csink->n_raw, NBUFFERS);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless (csink->raw == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_compressed_single_video_stream_demuxer_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)compressed-video, " "stream1=(string)none", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless (csink->raw == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, NBUFFERS);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless (csink->raw == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_raw_single_audio_stream_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin =
      create_playbin
      ("caps:audio/x-raw-int,"
      " rate=(int)48000, "
      " channels=(int)2, "
      " endianness=(int)LITTLE_ENDIAN, "
      " width=(int)16, " " depth=(int)16, " " signed=(bool)TRUE", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless (csink->raw == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless (csink->raw == TRUE);
    fail_unless_equals_int (csink->n_raw, NBUFFERS);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_compressed_single_audio_stream_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:audio/x-compressed", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless (csink->raw == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless (csink->raw == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, NBUFFERS);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_raw_single_audio_stream_demuxer_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)raw-audio, " "stream1=(string)none", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless (csink->raw == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless (csink->raw == TRUE);
    fail_unless_equals_int (csink->n_raw, NBUFFERS);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_compressed_single_audio_stream_demuxer_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)compressed-audio, " "stream1=(string)none", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless (csink->raw == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless (csink->raw == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, NBUFFERS);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_raw_audio_video_stream_demuxer_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)raw-audio, " "stream1=(string)raw-video", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless (csink->raw == TRUE);
    fail_unless_equals_int (csink->n_raw, NBUFFERS);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless (csink->raw == TRUE);
    fail_unless_equals_int (csink->n_raw, NBUFFERS);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_compressed_audio_video_stream_demuxer_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)compressed-audio, " "stream1=(string)compressed-video",
      TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless (csink->raw == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, NBUFFERS);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless (csink->raw == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, NBUFFERS);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_raw_compressed_video_stream_demuxer_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)raw-video, " "stream1=(string)compressed-video", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless_equals_int (csink->n_raw + csink->n_compressed, NBUFFERS);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_raw_compressed_audio_stream_demuxer_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)raw-audio, " "stream1=(string)compressed-audio", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless_equals_int (csink->n_raw + csink->n_compressed, NBUFFERS);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

#if 0
typedef struct
{
  GstElement *playbin;
  GstElement *sink;
} SwitchBufferProbeCtx;

static gboolean
switch_video_buffer_probe (GstPad * pad, GstBuffer * buffer,
    SwitchBufferProbeCtx * ctx)
{
  GstElement *playbin = ctx->playbin;
  GstVideoCodecSink *sink = (GstVideoCodecSink *) ctx->sink;

  if (sink->n_raw + sink->n_compressed == NBUFFERS / 2) {
    gint cur_video;

    g_object_get (G_OBJECT (playbin), "current-video", &cur_video, NULL);
    cur_video = (cur_video == 0) ? 1 : 0;
    g_object_set (G_OBJECT (playbin), "current-video", cur_video, NULL);
  }

  return TRUE;
}

GST_START_TEST (test_raw_compressed_video_stream_demuxer_switch_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;
  GstPad *pad;
  SwitchBufferProbeCtx switch_ctx;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)raw-video, " "stream1=(string)compressed-video", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  pad = gst_element_get_static_pad (sink, "sink");
  fail_unless (pad != NULL);
  switch_ctx.playbin = playbin;
  switch_ctx.sink = sink;
  gst_pad_add_buffer_probe (pad, G_CALLBACK (switch_video_buffer_probe),
      &switch_ctx);
  gst_object_unref (pad);
  gst_object_unref (sink);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless (csink->n_raw > 0);
    fail_unless (csink->n_compressed > 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_compressed_raw_video_stream_demuxer_switch_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;
  GstPad *pad;
  SwitchBufferProbeCtx switch_ctx;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)compressed-video, " "stream1=(string)raw-video", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  pad = gst_element_get_static_pad (sink, "sink");
  fail_unless (pad != NULL);
  switch_ctx.playbin = playbin;
  switch_ctx.sink = sink;
  gst_pad_add_buffer_probe (pad, G_CALLBACK (switch_video_buffer_probe),
      &switch_ctx);
  gst_object_unref (pad);
  gst_object_unref (sink);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless (csink->n_raw > 0);
    fail_unless (csink->n_compressed > 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_raw_raw_video_stream_demuxer_switch_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;
  GstPad *pad;
  SwitchBufferProbeCtx switch_ctx;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)raw-video, " "stream1=(string)raw-video", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  pad = gst_element_get_static_pad (sink, "sink");
  fail_unless (pad != NULL);
  switch_ctx.playbin = playbin;
  switch_ctx.sink = sink;
  gst_pad_add_buffer_probe (pad, G_CALLBACK (switch_video_buffer_probe),
      &switch_ctx);
  gst_object_unref (pad);
  gst_object_unref (sink);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless (csink->n_raw > 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST
    (test_compressed_compressed_video_stream_demuxer_switch_manual_sink) {
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;
  GstPad *pad;
  SwitchBufferProbeCtx switch_ctx;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)compressed-video, " "stream1=(string)compressed-video",
      TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  pad = gst_element_get_static_pad (sink, "sink");
  fail_unless (pad != NULL);
  switch_ctx.playbin = playbin;
  switch_ctx.sink = sink;
  gst_pad_add_buffer_probe (pad, G_CALLBACK (switch_video_buffer_probe),
      &switch_ctx);
  gst_object_unref (pad);
  gst_object_unref (sink);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless (csink->n_compressed > 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

static gboolean
switch_audio_buffer_probe (GstPad * pad, GstBuffer * buffer,
    SwitchBufferProbeCtx * ctx)
{
  GstElement *playbin = ctx->playbin;
  GstAudioCodecSink *sink = (GstAudioCodecSink *) ctx->sink;

  if (sink->n_raw + sink->n_compressed == NBUFFERS / 3) {
    gint cur_audio;

    g_object_get (G_OBJECT (playbin), "current-audio", &cur_audio, NULL);
    cur_audio = (cur_audio == 0) ? 1 : 0;
    g_object_set (G_OBJECT (playbin), "current-audio", cur_audio, NULL);
  }

  return TRUE;
}

GST_START_TEST (test_raw_compressed_audio_stream_demuxer_switch_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;
  GstPad *pad;
  SwitchBufferProbeCtx switch_ctx;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)raw-audio, " "stream1=(string)compressed-audio", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  pad = gst_element_get_static_pad (sink, "sink");
  fail_unless (pad != NULL);
  switch_ctx.playbin = playbin;
  switch_ctx.sink = sink;
  gst_pad_add_buffer_probe (pad, G_CALLBACK (switch_audio_buffer_probe),
      &switch_ctx);
  gst_object_unref (pad);
  gst_object_unref (sink);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless (csink->n_raw > 0);
    fail_unless (csink->n_compressed > 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_compressed_raw_audio_stream_demuxer_switch_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;
  GstPad *pad;
  SwitchBufferProbeCtx switch_ctx;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)compressed-audio, " "stream1=(string)raw-audio", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  pad = gst_element_get_static_pad (sink, "sink");
  fail_unless (pad != NULL);
  switch_ctx.playbin = playbin;
  switch_ctx.sink = sink;
  gst_pad_add_buffer_probe (pad, G_CALLBACK (switch_audio_buffer_probe),
      &switch_ctx);
  gst_object_unref (pad);
  gst_object_unref (sink);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless (csink->n_raw > 0);
    fail_unless (csink->n_compressed > 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST (test_raw_raw_audio_stream_demuxer_switch_manual_sink)
{
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;
  GstPad *pad;
  SwitchBufferProbeCtx switch_ctx;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)raw-audio, " "stream1=(string)raw-audio", TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  pad = gst_element_get_static_pad (sink, "sink");
  fail_unless (pad != NULL);
  switch_ctx.playbin = playbin;
  switch_ctx.sink = sink;
  gst_pad_add_buffer_probe (pad, G_CALLBACK (switch_audio_buffer_probe),
      &switch_ctx);
  gst_object_unref (pad);
  gst_object_unref (sink);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless (csink->n_raw > 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;

GST_START_TEST
    (test_compressed_compressed_audio_stream_demuxer_switch_manual_sink) {
  GstMessage *msg;
  GstElement *playbin;
  GstElement *sink;
  GstBus *bus;
  gboolean done = FALSE;
  GstPad *pad;
  SwitchBufferProbeCtx switch_ctx;

  fail_unless (gst_element_register (NULL, "capssrc", GST_RANK_PRIMARY,
          gst_caps_src_get_type ()));
  fail_unless (gst_element_register (NULL, "codecdemuxer",
          GST_RANK_PRIMARY + 100, gst_codec_demuxer_get_type ()));
  fail_unless (gst_element_register (NULL, "audiocodecsink",
          GST_RANK_PRIMARY + 100, gst_audio_codec_sink_get_type ()));
  fail_unless (gst_element_register (NULL, "videocodecsink",
          GST_RANK_PRIMARY + 100, gst_video_codec_sink_get_type ()));

  playbin = create_playbin ("caps:application/x-container, "
      "stream0=(string)compressed-audio, " "stream1=(string)compressed-audio",
      TRUE);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  pad = gst_element_get_static_pad (sink, "sink");
  fail_unless (pad != NULL);
  switch_ctx.playbin = playbin;
  switch_ctx.sink = sink;
  gst_pad_add_buffer_probe (pad, G_CALLBACK (switch_audio_buffer_probe),
      &switch_ctx);
  gst_object_unref (pad);
  gst_object_unref (sink);

  fail_unless_equals_int (gst_element_set_state (playbin, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  bus = gst_element_get_bus (playbin);

  while (!done) {
    msg = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_EOS:
        done = TRUE;
        break;
      case GST_MESSAGE_ERROR:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);
        break;
      case GST_MESSAGE_WARNING:
        fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_WARNING);
        break;
      default:
        break;
    }
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  g_object_get (G_OBJECT (playbin), "video-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstVideoCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_video_codec_sink_get_type ());
    csink = (GstVideoCodecSink *) sink;
    fail_unless (csink->audio == FALSE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless_equals_int (csink->n_compressed, 0);
    gst_object_unref (sink);
  }

  g_object_get (G_OBJECT (playbin), "audio-sink", &sink, NULL);
  fail_unless (sink != NULL);
  {
    GstAudioCodecSink *csink;

    fail_unless (G_TYPE_FROM_INSTANCE (sink) ==
        gst_audio_codec_sink_get_type ());
    csink = (GstAudioCodecSink *) sink;
    fail_unless (csink->audio == TRUE);
    fail_unless_equals_int (csink->n_raw, 0);
    fail_unless (csink->n_compressed > 0);
    gst_object_unref (sink);
  }

  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
}

GST_END_TEST;
#endif
#endif

static Suite *
playbin2_compressed_suite (void)
{
  Suite *s = suite_create ("playbin2-compressed");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

#ifndef GST_DISABLE_REGISTRY
  tcase_add_test (tc_chain, test_raw_single_video_stream_manual_sink);
  tcase_add_test (tc_chain, test_raw_single_audio_stream_manual_sink);
  tcase_add_test (tc_chain, test_compressed_single_video_stream_manual_sink);
  tcase_add_test (tc_chain, test_compressed_single_audio_stream_manual_sink);

  tcase_add_test (tc_chain, test_raw_single_video_stream_demuxer_manual_sink);
  tcase_add_test (tc_chain, test_raw_single_audio_stream_demuxer_manual_sink);
  tcase_add_test (tc_chain,
      test_compressed_single_video_stream_demuxer_manual_sink);
  tcase_add_test (tc_chain,
      test_compressed_single_audio_stream_demuxer_manual_sink);

  tcase_add_test (tc_chain, test_raw_audio_video_stream_demuxer_manual_sink);
  tcase_add_test (tc_chain,
      test_compressed_audio_video_stream_demuxer_manual_sink);

  tcase_add_test (tc_chain,
      test_raw_compressed_audio_stream_demuxer_manual_sink);
  tcase_add_test (tc_chain,
      test_raw_compressed_video_stream_demuxer_manual_sink);

  /* These tests need something like the stream-activate event
   * and are racy otherwise */
#if 0
  tcase_add_test (tc_chain,
      test_raw_raw_audio_stream_demuxer_switch_manual_sink);
  tcase_add_test (tc_chain,
      test_raw_compressed_audio_stream_demuxer_switch_manual_sink);
  tcase_add_test (tc_chain,
      test_compressed_raw_audio_stream_demuxer_switch_manual_sink);
  tcase_add_test (tc_chain,
      test_compressed_compressed_audio_stream_demuxer_switch_manual_sink);
  tcase_add_test (tc_chain,
      test_raw_raw_video_stream_demuxer_switch_manual_sink);
  tcase_add_test (tc_chain,
      test_raw_compressed_video_stream_demuxer_switch_manual_sink);
  tcase_add_test (tc_chain,
      test_compressed_raw_video_stream_demuxer_switch_manual_sink);
  tcase_add_test (tc_chain,
      test_compressed_compressed_video_stream_demuxer_switch_manual_sink);
#endif
#endif

  return s;
}

GST_CHECK_MAIN (playbin2_compressed);
