/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * Copyright (C) 2023 Pengutronix e.K. - www.pengutronix.de
 *
 */

/**
 * SECTION:plugin-uvcgadget
 * @title: uvcgadget
 *
 * Since: 1.24
 */

/**
 * SECTION:element-uvcsink
 * @title: uvcsink
 *
 * uvcsink can be used to push frames to the Linux UVC Gadget
 * driver.
 *
 * ## Example launch lines
 * |[
 * gst-launch videotestsrc ! uvcsink v4l2sink::device=/dev/video1
 * ]|
 * This pipeline streams a test pattern on UVC gadget /dev/video1.
 *
 * Before starting the pipeline, the linux system needs an uvc gadget
 * configured on the udc (usb device controller)
 *
 * Either by using the legacy g_webcam gadget or by preconfiguring it
 * with configfs. One way to configure is to use the example script from
 * uvc-gadget:
 *
 * https://git.ideasonboard.org/uvc-gadget.git/blob/HEAD:/scripts/uvc-gadget.sh
 *
 * A modern way of configuring the gadget with an scheme file that gets
 * loaded by gadget-tool (gt) using libusbgx.
 *
 * https://github.com/linux-usb-gadgets/libusbgx
 * https://github.com/linux-usb-gadgets/gt
 *
 * Since: 1.24
 */
#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include "gstuvcsink.h"

GST_DEBUG_CATEGORY (uvcsink_debug);
#define GST_CAT_DEFAULT uvcsink_debug

enum
{
  ARG_0,
  PROP_STREAMING,
  PROP_LAST
};

#define gst_uvc_sink_parent_class parent_class
G_DEFINE_TYPE (GstUvcSink, gst_uvc_sink, GST_TYPE_BIN);
GST_ELEMENT_REGISTER_DEFINE (uvcsink,
    "uvcsink", GST_RANK_NONE, GST_TYPE_UVCSINK);

/* GstElement methods: */
static GstStateChangeReturn gst_uvc_sink_change_state (GstElement *
    element, GstStateChange transition);

static void gst_uvc_sink_dispose (GObject * object);
static void gst_uvc_sink_finalize (GObject * object);
static gboolean gst_uvc_sink_prepare_configfs (GstUvcSink * self);

static void
gst_uvc_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstUvcSink *self = GST_UVCSINK (object);

  switch (prop_id) {
    case PROP_STREAMING:
      UVCSINK_STATE_LOCK (self);
      g_value_set_boolean (value, self->streaming);
      UVCSINK_STATE_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstVideoFormat
gst_v4l2_object_v4l2fourcc_to_video_format (guint32 fourcc)
{
  GstVideoFormat format;

  switch (fourcc) {
    case V4L2_PIX_FMT_GREY:    /*  8  Greyscale     */
      format = GST_VIDEO_FORMAT_GRAY8;
      break;
    case V4L2_PIX_FMT_Y16:
      format = GST_VIDEO_FORMAT_GRAY16_LE;
      break;
    case V4L2_PIX_FMT_Y16_BE:
      format = GST_VIDEO_FORMAT_GRAY16_BE;
      break;
    case V4L2_PIX_FMT_XRGB555:
    case V4L2_PIX_FMT_RGB555:
      format = GST_VIDEO_FORMAT_RGB15;
      break;
    case V4L2_PIX_FMT_XRGB555X:
    case V4L2_PIX_FMT_RGB555X:
      format = GST_VIDEO_FORMAT_BGR15;
      break;
    case V4L2_PIX_FMT_RGB565:
      format = GST_VIDEO_FORMAT_RGB16;
      break;
    case V4L2_PIX_FMT_RGB24:
      format = GST_VIDEO_FORMAT_RGB;
      break;
    case V4L2_PIX_FMT_BGR24:
      format = GST_VIDEO_FORMAT_BGR;
      break;
    case V4L2_PIX_FMT_XRGB32:
    case V4L2_PIX_FMT_RGB32:
      format = GST_VIDEO_FORMAT_xRGB;
      break;
    case V4L2_PIX_FMT_RGBX32:
      format = GST_VIDEO_FORMAT_RGBx;
      break;
    case V4L2_PIX_FMT_XBGR32:
    case V4L2_PIX_FMT_BGR32:
      format = GST_VIDEO_FORMAT_BGRx;
      break;
    case V4L2_PIX_FMT_BGRX32:
      format = GST_VIDEO_FORMAT_xBGR;
      break;
    case V4L2_PIX_FMT_ABGR32:
      format = GST_VIDEO_FORMAT_BGRA;
      break;
    case V4L2_PIX_FMT_BGRA32:
      format = GST_VIDEO_FORMAT_ABGR;
      break;
    case V4L2_PIX_FMT_RGBA32:
      format = GST_VIDEO_FORMAT_RGBA;
      break;
    case V4L2_PIX_FMT_ARGB32:
      format = GST_VIDEO_FORMAT_ARGB;
      break;
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV12M:
      format = GST_VIDEO_FORMAT_NV12;
      break;
    case V4L2_PIX_FMT_NV12MT:
      format = GST_VIDEO_FORMAT_NV12_64Z32;
      break;
    case V4L2_PIX_FMT_MM21:
      format = GST_VIDEO_FORMAT_NV12_16L32S;
      break;
    case V4L2_PIX_FMT_NV12M_8L128:
      format = GST_VIDEO_FORMAT_NV12_8L128;
      break;
    case V4L2_PIX_FMT_NV12M_10BE_8L128:
      format = GST_VIDEO_FORMAT_NV12_10BE_8L128;
      break;
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV21M:
      format = GST_VIDEO_FORMAT_NV21;
      break;
    case V4L2_PIX_FMT_YVU410:
      format = GST_VIDEO_FORMAT_YVU9;
      break;
    case V4L2_PIX_FMT_YUV410:
      format = GST_VIDEO_FORMAT_YUV9;
      break;
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YUV420M:
      format = GST_VIDEO_FORMAT_I420;
      break;
    case V4L2_PIX_FMT_YUYV:
      format = GST_VIDEO_FORMAT_YUY2;
      break;
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_YVU420M:
      format = GST_VIDEO_FORMAT_YV12;
      break;
    case V4L2_PIX_FMT_UYVY:
      format = GST_VIDEO_FORMAT_UYVY;
      break;
    case V4L2_PIX_FMT_VYUY:
      format = GST_VIDEO_FORMAT_VYUY;
      break;
    case V4L2_PIX_FMT_YUV411P:
      format = GST_VIDEO_FORMAT_Y41B;
      break;
    case V4L2_PIX_FMT_YUV422P:
      format = GST_VIDEO_FORMAT_Y42B;
      break;
    case V4L2_PIX_FMT_YVYU:
      format = GST_VIDEO_FORMAT_YVYU;
      break;
    case V4L2_PIX_FMT_NV16:
    case V4L2_PIX_FMT_NV16M:
      format = GST_VIDEO_FORMAT_NV16;
      break;
    case V4L2_PIX_FMT_NV61:
    case V4L2_PIX_FMT_NV61M:
      format = GST_VIDEO_FORMAT_NV61;
      break;
    case V4L2_PIX_FMT_NV24:
      format = GST_VIDEO_FORMAT_NV24;
      break;
    default:
      format = GST_VIDEO_FORMAT_UNKNOWN;
      break;
  }

  return format;
}

static GstStructure *
gst_v4l2uvc_fourcc_to_bare_struct (guint32 fourcc)
{
  GstStructure *structure = NULL;
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;

  switch (fourcc) {
    case V4L2_PIX_FMT_MJPEG:   /* Motion-JPEG */
    case V4L2_PIX_FMT_JPEG:    /* JFIF JPEG */
      structure = gst_structure_new_empty ("image/jpeg");
      break;
    default:
      format = gst_v4l2_object_v4l2fourcc_to_video_format (fourcc);
      if (format == GST_VIDEO_FORMAT_UNKNOWN) {
        GST_DEBUG ("Unsupported fourcc 0x%08x %" GST_FOURCC_FORMAT,
            fourcc, GST_FOURCC_ARGS (fourcc));
        break;
      }
      structure = gst_structure_new ("video/x-raw",
          "format", G_TYPE_STRING, gst_video_format_to_string (format), NULL);
      break;
  }

  return structure;
}

/* The UVC EVENT_DATA from the host, which is committing the currently
 * selected configuration (format+resolution+framerate) is only selected
 * by some index values (except the framerate). We have to transform
 * those values to an valid caps string that we can return on the caps
 * query.
 */
static GstCaps *
gst_uvc_sink_get_configured_caps (GstUvcSink * self)
{
  struct v4l2_fmtdesc format;
  struct v4l2_frmsizeenum size;
  struct v4l2_frmivalenum ival;
  guint32 width, height;
  gint device_fd;
  GstStructure *s;
  gint numerator, denominator;
  GValue framerate = { 0, };

  g_object_get (G_OBJECT (self->v4l2sink), "device-fd", &device_fd, NULL);

  format.index = self->cur.bFormatIndex - 1;
  format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

  if (ioctl (device_fd, VIDIOC_ENUM_FMT, &format) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, READ, ("Linux kernel error"),
        ("VIDIOC_ENUM_FMT failed: %s (%d)", g_strerror (errno), errno));
    return NULL;
  }

  s = gst_v4l2uvc_fourcc_to_bare_struct (format.pixelformat);
  if (!s)
    return NULL;

  memset (&size, 0, sizeof (struct v4l2_frmsizeenum));
  size.index = self->cur.bFrameIndex - 1;
  size.pixel_format = format.pixelformat;

  if (ioctl (device_fd, VIDIOC_ENUM_FRAMESIZES, &size) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, READ, ("Linux kernel error"),
        ("VIDIOC_ENUM_FRAMESIZES failed: %s (%d)", g_strerror (errno), errno));
    return NULL;
  }

  GST_LOG_OBJECT (self, "got discrete frame size %dx%d",
      size.discrete.width, size.discrete.height);

  width = MIN (size.discrete.width, G_MAXINT);
  height = MIN (size.discrete.height, G_MAXINT);

  if (!width || !height)
    return NULL;

  g_value_init (&framerate, GST_TYPE_FRACTION);

  memset (&ival, 0, sizeof (struct v4l2_frmivalenum));

  ival.index = 0;
  ival.pixel_format = format.pixelformat;
  ival.width = width;
  ival.height = height;

#define SIMPLIFY_FRACTION_N_TERMS   8
#define SIMPLIFY_FRACTION_THRESHOLD 333

  numerator = self->cur.dwFrameInterval;
  denominator = 10000000;
  gst_util_simplify_fraction (&numerator, &denominator,
      SIMPLIFY_FRACTION_N_TERMS, SIMPLIFY_FRACTION_THRESHOLD);

  if (ioctl (device_fd, VIDIOC_ENUM_FRAMEINTERVALS, &ival) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, READ, ("Linux kernel error"),
        ("VIDIOC_ENUM_FRAMEINTERVALS failed: %s (%d)",
            g_strerror (errno), errno));
    return NULL;
  }

  do {
    gint inumerator = ival.discrete.numerator;
    gint idenominator = ival.discrete.denominator;

    gst_util_simplify_fraction (&inumerator, &idenominator,
        SIMPLIFY_FRACTION_N_TERMS, SIMPLIFY_FRACTION_THRESHOLD);

    if (numerator == inumerator && denominator == idenominator)
      /* swap to get the framerate */
      gst_value_set_fraction (&framerate, denominator, numerator);

    ival.index++;
  } while (ioctl (device_fd, VIDIOC_ENUM_FRAMEINTERVALS, &ival) >= 0);

  gst_structure_set (s, "width", G_TYPE_INT, (gint) width,
      "height", G_TYPE_INT, (gint) height, NULL);

  gst_structure_take_value (s, "framerate", &framerate);

  return gst_caps_new_full (s, NULL);
}

static gboolean gst_uvc_sink_to_fakesink (GstUvcSink * self);
static gboolean gst_uvc_sink_to_v4l2sink (GstUvcSink * self);

/* Called from the streaming thread to handle a STREAMOFF from the host */
static void
gst_uvc_sink_stop_streaming (GstUvcSink * self)
{
  /* Nothing to renegotiate when the host stops streaming. Switch back to
   * the fakesink right away: this stops the v4l2sink, which does the
   * VIDIOC_STREAMOFF the gadget driver waits for before it accepts the next
   * STREAMON from the host. */
  GST_DEBUG_OBJECT (self, "Stopping stream");
  gst_uvc_sink_to_fakesink (self);
  self->renegotiating = FALSE;

  UVCSINK_STATE_LOCK (self);
  self->streamoff = FALSE;
  UVCSINK_STATE_UNLOCK (self);
}

static void
gst_uvc_sink_update_streaming (GstUvcSink * self)
{
  gboolean streaming, streamon, streamoff;

  UVCSINK_STATE_LOCK (self);
  streaming = self->streaming;
  streamon = self->streamon;
  streamoff = self->streamoff;
  self->streamon = FALSE;
  UVCSINK_STATE_UNLOCK (self);

  if (streamon && !streaming)
    GST_ERROR_OBJECT (self, "Unexpected STREAMON");
  if (streamoff && streaming)
    GST_ERROR_OBJECT (self, "Unexpected STREAMOFF");

  self->renegotiating = FALSE;

  /* A STREAMOFF still pending happened before the STREAMON */
  if (streamoff)
    gst_uvc_sink_stop_streaming (self);

  if (!streamon)
    return;

  gst_uvc_sink_to_v4l2sink (self);

  /* The event thread only flushes the v4l2sink on STREAMOFF if it is linked
   * at that time. A STREAMOFF that arrived while switching would thus not
   * unblock it, so handle it before passing any buffer to the v4l2sink. */
  UVCSINK_STATE_LOCK (self);
  streamoff = self->streamoff;
  UVCSINK_STATE_UNLOCK (self);

  if (streamoff)
    gst_uvc_sink_stop_streaming (self);
}

/* When the host stops streaming, the v4l2sink gets flushed to unblock it.
 * That is internal to this element, so don't return the resulting FLUSHING
 * upstream, it would stop the streaming thread of the source. */
static GstFlowReturn
gst_uvc_sink_fix_flow_return (GstUvcSink * self, GstPad * pad,
    GstFlowReturn ret)
{
  gboolean streamoff;

  if (ret != GST_FLOW_FLUSHING || GST_PAD_IS_FLUSHING (pad))
    return ret;

  UVCSINK_STATE_LOCK (self);
  streamoff = self->streamoff;
  UVCSINK_STATE_UNLOCK (self);

  if (!streamoff)
    return ret;

  GST_DEBUG_OBJECT (self, "v4l2sink flushed for STREAMOFF, ignoring");
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_uvc_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstUvcSink *self = GST_UVCSINK (parent);
  GstFlowReturn ret;

  ret = gst_proxy_pad_chain_default (pad, parent, buffer);

  return gst_uvc_sink_fix_flow_return (self, pad, ret);
}

static GstFlowReturn
gst_uvc_sink_chain_list (GstPad * pad, GstObject * parent, GstBufferList * list)
{
  GstUvcSink *self = GST_UVCSINK (parent);
  GstFlowReturn ret;

  ret = gst_proxy_pad_chain_list_default (pad, parent, list);

  return gst_uvc_sink_fix_flow_return (self, pad, ret);
}

/* The colorimetry the host uses is announced by the color matching
 * descriptor of the gadget configuration. The gadget driver ignores the
 * colorimetry set on the video device and always reports sRGB, which
 * v4l2sink translates to 2:4:7:1 and then refuses to set any caps with a
 * different colorimetry, e.g. bt601 as set by jpegenc. Remove the field
 * from the caps passed to v4l2sink, as it has no effect on the stream. */
static GstEvent *
gst_uvc_sink_strip_colorimetry (GstUvcSink * self, GstEvent * event)
{
  GstCaps *caps;
  GstEvent *new_event;
  guint i;

  gst_event_parse_caps (event, &caps);

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    if (gst_structure_has_field (gst_caps_get_structure (caps, i),
            "colorimetry"))
      break;
  }
  if (i == gst_caps_get_size (caps))
    return event;

  caps = gst_caps_copy (caps);
  for (i = 0; i < gst_caps_get_size (caps); i++)
    gst_structure_remove_field (gst_caps_get_structure (caps, i),
        "colorimetry");

  GST_DEBUG_OBJECT (self, "Passing on caps without colorimetry: %"
      GST_PTR_FORMAT, caps);

  new_event = gst_event_new_caps (caps);
  gst_event_set_seqnum (new_event, gst_event_get_seqnum (event));
  gst_caps_unref (caps);
  gst_event_unref (event);

  return new_event;
}

static gboolean
gst_uvc_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstUvcSink *self = GST_UVCSINK (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      gboolean caps_changed;

      GST_DEBUG_OBJECT (self, "Handling %" GST_PTR_FORMAT, event);

      UVCSINK_STATE_LOCK (self);
      /* If the UVC host did not yet commit a format, the cur_caps may contain
       * all probed caps. In this case, the element is not able to detect, if
       * the caps have changed when the stream is enabled. Take the caps from
       * upstream to be able to detect a change.
       */
      if (!GST_CAPS_IS_SIMPLE (self->cur_caps)) {
        GstCaps *caps;

        gst_event_parse_caps (event, &caps);
        gst_caps_replace (&self->cur_caps, caps);

        GST_DEBUG_OBJECT (self,
            "UVC host didn't select a format, yet. Using upstream %"
            GST_PTR_FORMAT, caps);
      }

      /* EVENT CAPS signals that the buffers after the event will use new caps.
       * If the UVC host requested a new format, we now must start the stream.
       */
      caps_changed = self->caps_changed;
      if (caps_changed && (self->streamon || self->streamoff))
        self->caps_changed = FALSE;
      UVCSINK_STATE_UNLOCK (self);

      if (caps_changed)
        gst_uvc_sink_update_streaming (self);

      event = gst_uvc_sink_strip_colorimetry (self, event);
      break;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_uvc_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstUvcSink *self = GST_UVCSINK (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *cur_caps, *result;

      UVCSINK_STATE_LOCK (self);
      cur_caps = gst_caps_ref (self->cur_caps);
      UVCSINK_STATE_UNLOCK (self);

      if (gst_caps_is_empty (cur_caps)) {
        gst_caps_unref (cur_caps);
        return gst_pad_query_default (pad, parent, query);
      }

      /* Callers (e.g. basesrc) rely on the result being a subset of the
       * filter. Before the host committed a format, cur_caps holds all the
       * probed caps of the device, including memory:DMABuf/DMA_DRM ones that
       * many upstream elements can't produce. */
      gst_query_parse_caps (query, &filter);
      if (filter) {
        result = gst_caps_intersect_full (filter, cur_caps,
            GST_CAPS_INTERSECT_FIRST);
      } else {
        result = gst_caps_ref (cur_caps);
      }

      GST_DEBUG_OBJECT (self, "caps %" GST_PTR_FORMAT ", filter %"
          GST_PTR_FORMAT ", result %" GST_PTR_FORMAT, cur_caps, filter, result);
      gst_query_set_caps_result (query, result);
      gst_caps_unref (result);
      gst_caps_unref (cur_caps);

      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
      /* As long as the host is not streaming, the ghost pad targets the
       * fakesink, which accepts any caps. Ask the v4l2sink instead: caps that
       * it would only refuse once the host starts streaming (e.g. image/jpeg
       * without parsed=true) then already fail negotiation at startup. */
      return gst_pad_query (self->v4l2sinkpad, query);
    case GST_QUERY_ALLOCATION:
      return TRUE;
    default:
      return gst_pad_query_default (pad, parent, query);
  }
  return TRUE;
}

static void
gst_uvc_sink_class_init (GstUvcSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (klass);

  element_class->change_state = gst_uvc_sink_change_state;

  gst_element_class_set_static_metadata (element_class,
      "UVC Sink", "Sink/Video",
      "Streams Video via UVC Gadget", "Michael Grzeschik <mgr@pengutronix.de>");

  GST_DEBUG_CATEGORY_INIT (uvcsink_debug, "uvcsink", 0, "uvc sink element");

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose = gst_uvc_sink_dispose;
  gobject_class->finalize = gst_uvc_sink_finalize;
  gobject_class->get_property = gst_uvc_sink_get_property;

  /**
   * uvcsink:streaming:
   *
   * The stream status of the host.
   *
   * Since: 1.24
   */
  g_object_class_install_property (gobject_class, PROP_STREAMING,
      g_param_spec_boolean ("streaming", "streaming",
          "The stream status of the host", 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static gboolean
gst_uvc_sink_to_fakesink (GstUvcSink * self)
{
  if (gst_pad_is_linked (self->fakesinkpad)) {
    GST_DEBUG_OBJECT (self, "v4l2sink already disabled");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "switching to fakesink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (self->sinkpad), self->fakesinkpad);
  gst_element_set_state (GST_ELEMENT (self->fakesink), GST_STATE_PLAYING);

  /* going to state READY makes v4l2sink lose its reference to the clock */
  self->v4l2_clock = gst_element_get_clock (self->v4l2sink);

  gst_pad_query (self->v4l2sinkpad, gst_query_new_drain ());

  gst_element_set_state (GST_ELEMENT (self->v4l2sink), GST_STATE_READY);

  return TRUE;
}

static gboolean
gst_uvc_sink_to_v4l2sink (GstUvcSink * self)
{
  if (gst_pad_is_linked (self->v4l2sinkpad)) {
    GST_DEBUG_OBJECT (self, "fakesink already disabled");
    return FALSE;
  }

  if (self->v4l2_clock) {
    gst_element_set_clock (self->v4l2sink, self->v4l2_clock);
    gst_object_unref (self->v4l2_clock);
  }

  GST_DEBUG_OBJECT (self, "switching to v4l2sink");

  gst_ghost_pad_set_target (GST_GHOST_PAD (self->sinkpad), self->v4l2sinkpad);
  gst_element_set_state (GST_ELEMENT (self->v4l2sink), GST_STATE_PLAYING);

  gst_pad_query (self->fakesinkpad, gst_query_new_drain ());

  gst_element_set_state (GST_ELEMENT (self->fakesink), GST_STATE_READY);

  return TRUE;
}

static GstPadProbeReturn
gst_uvc_sink_sinkpad_idle_probe (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data)
{
  GstUvcSink *self = user_data;
  gboolean streamon, streamoff, caps_changed;

  UVCSINK_STATE_LOCK (self);
  streamon = self->streamon;
  streamoff = self->streamoff;
  caps_changed = self->caps_changed;
  UVCSINK_STATE_UNLOCK (self);

  if (streamoff) {
    gst_uvc_sink_stop_streaming (self);
    return GST_PAD_PROBE_PASS;
  }

  if (!streamon)
    return GST_PAD_PROBE_PASS;

  if (!caps_changed) {
    /* The negotiated caps already match the format selected by the host,
     * so there is nothing to renegotiate. Switch to the v4l2sink right away
     * instead of waiting for a CAPS query triggered by a RECONFIGURE event:
     * that query is not guaranteed to reach us, e.g. parsers answer it
     * without asking downstream. */
    GST_DEBUG_OBJECT (self, "Caps unchanged, starting stream");
    gst_uvc_sink_update_streaming (self);
    return GST_PAD_PROBE_PASS;
  }

  if (!self->renegotiating) {
    /* Ask upstream to renegotiate to the format selected by the host. The
     * CAPS event with the new caps then switches to the v4l2sink. Until
     * then, buffers in the old format keep going to the fakesink. They are
     * not dropped: with a non-live source, that would make upstream run
     * ahead of the clock, and as no data would arrive at the pad anymore,
     * this probe would not be called again to handle a STREAMOFF. */
    GST_DEBUG_OBJECT (self, "Send reconfigure");
    self->renegotiating = TRUE;
    gst_pad_push_event (self->sinkpad, gst_event_new_reconfigure ());
  }

  return GST_PAD_PROBE_PASS;
}

static void
gst_uvc_sink_create_idle_probe (GstUvcSink * self)
{
  GstPad *peerpad = gst_pad_get_peer (self->sinkpad);
  if (peerpad) {
    self->idle_probe_id =
        gst_pad_add_probe (peerpad, GST_PAD_PROBE_TYPE_IDLE,
        gst_uvc_sink_sinkpad_idle_probe, self, NULL);
    gst_object_unref (peerpad);
  }
}

static void
gst_uvc_sink_remove_idle_probe (GstUvcSink * self)
{
  GstPad *peerpad = gst_pad_get_peer (self->sinkpad);
  if (peerpad && self->idle_probe_id) {
    gst_pad_remove_probe (peerpad, self->idle_probe_id);
    gst_object_unref (peerpad);
    self->idle_probe_id = 0;
  }
}

static gboolean
gst_uvc_sink_open (GstUvcSink * self)
{
  if (!self->v4l2sink) {
    gst_element_post_message (GST_ELEMENT_CAST (self),
        gst_missing_element_message_new (GST_ELEMENT_CAST (self), "v4l2sink"));

    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("No v4l2sink element found"), ("Check your plugin installation"));
    return FALSE;
  }

  if (!self->fakesink) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("No fakesink element found"), ("Check your plugin installation"));
    return FALSE;
  }

  return TRUE;
}

static void
gst_uvc_sink_init (GstUvcSink * self)
{
  /* add the v4l2sink element */
  self->v4l2sink = gst_element_factory_make ("v4l2sink", "v4l2sink");
  if (!self->v4l2sink)
    return;

  g_object_set (G_OBJECT (self->v4l2sink), "async", FALSE, NULL);
  gst_bin_add (GST_BIN (self), self->v4l2sink);

  /* add the fakesink element */
  self->fakesink = gst_element_factory_make ("fakesink", "fakesink");
  if (!self->fakesink)
    return;

  g_object_set (G_OBJECT (self->fakesink), "sync", TRUE, NULL);
  gst_bin_add (GST_BIN (self), self->fakesink);

  self->v4l2sinkpad = gst_element_get_static_pad (self->v4l2sink, "sink");
  g_return_if_fail (self->v4l2sinkpad != NULL);

  self->fakesinkpad = gst_element_get_static_pad (self->fakesink, "sink");
  g_return_if_fail (self->fakesinkpad != NULL);

  /* create ghost pad sink */
  self->sinkpad = gst_ghost_pad_new ("sink", self->fakesinkpad);
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  g_mutex_init (&self->state_lock);
  self->streaming = FALSE;
  self->streamon = FALSE;
  self->streamoff = FALSE;
  self->caps_changed = FALSE;

  gst_pad_set_query_function (self->sinkpad, gst_uvc_sink_query);
  gst_pad_set_event_function (self->sinkpad, gst_uvc_sink_event);
  gst_pad_set_chain_function (self->sinkpad, gst_uvc_sink_chain);
  gst_pad_set_chain_list_function (self->sinkpad, gst_uvc_sink_chain_list);

  self->request_error_code = REQUEST_ERROR_CODE_NO_ERROR;

  self->cur_caps = gst_caps_new_empty ();
}

static void
gst_uvc_sink_dispose (GObject * object)
{
  GstUvcSink *self = GST_UVCSINK (object);

  if (self->sinkpad) {
    gst_pad_set_active (self->sinkpad, FALSE);
    gst_element_remove_pad (GST_ELEMENT (self), self->sinkpad);
    self->sinkpad = NULL;
  }

  gst_clear_object (&self->fakesinkpad);
  gst_clear_object (&self->v4l2sinkpad);
  gst_clear_caps (&self->cur_caps);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_uvc_sink_finalize (GObject * object)
{
  GstUvcSink *self = GST_UVCSINK (object);

  g_mutex_clear (&self->state_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean gst_uvc_sink_unwatch (GstUvcSink * self);

/* the thread where everything happens */
static void
gst_uvc_sink_task (gpointer data)
{
  GstUvcSink *self = GST_UVCSINK (data);
  GstClockTime timeout = GST_CLOCK_TIME_NONE;
  struct uvc_request_data resp;
  gboolean ret = TRUE;

  /* Since the plugin needs to be able to start immidiatly in PLAYING
     state we ensure the pipeline is not blocked while we poll for
     events.
   */
  GST_PAD_STREAM_UNLOCK (self->sinkpad);

  ret = gst_poll_wait (self->poll, timeout);
  if (G_UNLIKELY (ret < 0))
    return;

  timeout = GST_CLOCK_TIME_NONE;

  if (gst_poll_fd_has_closed (self->poll, &self->pollfd)) {
    GST_ELEMENT_ERROR (self, RESOURCE, READ, ("videofd was closed"), NULL);
    gst_uvc_sink_unwatch (self);
    gst_element_set_state (GST_ELEMENT (self), GST_STATE_NULL);
    return;
  }

  if (gst_poll_fd_has_error (self->poll, &self->pollfd)) {
    GST_ELEMENT_ERROR (self, RESOURCE, READ, ("videofd has error"), NULL);
    gst_uvc_sink_unwatch (self);
    gst_element_set_state (GST_ELEMENT (self), GST_STATE_NULL);
    return;
  }

  /* PRI is used to signal that events are available */
  if (gst_poll_fd_has_pri (self->poll, &self->pollfd)) {
    struct v4l2_event event = { 0, };
    struct uvc_event *uvc_event = (void *) &event.u.data;

    if (ioctl (self->pollfd.fd, VIDIOC_DQEVENT, &event) < 0) {
      GST_ELEMENT_ERROR (self, RESOURCE, READ,
          ("could not dequeue event"), NULL);
      gst_uvc_sink_unwatch (self);
      gst_element_set_state (GST_ELEMENT (self), GST_STATE_NULL);
      return;
    }

    switch (event.type) {
      case UVC_EVENT_STREAMON:
        GST_DEBUG_OBJECT (self, "UVC_EVENT_STREAMON");
        UVCSINK_STATE_LOCK (self);
        self->streaming = TRUE;
        self->streamon = TRUE;
        UVCSINK_STATE_UNLOCK (self);
        g_object_notify (G_OBJECT (self), "streaming");
        break;
      case UVC_EVENT_STREAMOFF:
      case UVC_EVENT_DISCONNECT:
        GST_DEBUG_OBJECT (self, "UVC_EVENT_STREAMOFF");
        UVCSINK_STATE_LOCK (self);
        self->streaming = FALSE;
        /* If the STREAMON wasn't handled yet, it is obsolete now. */
        self->streamon = FALSE;
        self->streamoff = TRUE;
        UVCSINK_STATE_UNLOCK (self);

        /* The streaming thread switches back to the fakesink once the current
         * buffer is done. But the v4l2sink might never finish it: once all
         * its buffers are queued, it waits for the driver to return one,
         * which may not happen anymore now that the host stopped reading.
         * Flush the v4l2sink to unblock it. The resulting FLUSHING flow
         * return is not passed upstream, see gst_uvc_sink_chain().
         *
         * streamoff is set before checking the link, and the streaming thread
         * checks streamoff after linking the v4l2sink, so a v4l2sink that is
         * being switched to concurrently is either flushed here or switched
         * away from again before it gets any buffer. A v4l2sink that was just
         * deactivated ignores the flush. */
        if (gst_pad_is_linked (self->v4l2sinkpad)) {
          GST_DEBUG_OBJECT (self, "Flushing v4l2sink");
          gst_pad_send_event (self->v4l2sinkpad, gst_event_new_flush_start ());
        }

        g_object_notify (G_OBJECT (self), "streaming");
        break;
      case UVC_EVENT_SETUP:
        GST_DEBUG_OBJECT (self, "UVC_EVENT_SETUP");

        memset (&resp, 0, sizeof (resp));
        resp.length = -EL2HLT;

        uvc_events_process_setup (self, &uvc_event->req, &resp);

        if (ioctl (self->pollfd.fd, UVCIOC_SEND_RESPONSE, &resp) < 0) {
          GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
              ("UVCIOC_SEND_RESPONSE failed"),
              ("UVCIOC_SEND_RESPONSE on UVC_EVENT_SETUP failed"));
          gst_uvc_sink_unwatch (self);
          gst_element_set_state (GST_ELEMENT (self), GST_STATE_NULL);
          return;
        }
        break;
      case UVC_EVENT_DATA:
        GST_DEBUG_OBJECT (self, "UVC_EVENT_DATA");
        uvc_events_process_data (self, &uvc_event->data);
        if (self->control == UVC_VS_COMMIT_CONTROL) {
          GstCaps *configured_caps;
          GstCaps *cur_caps;
          GstCaps *prev_caps;
          gboolean caps_changed;

          /* The configured caps are not sufficient for negotiation. Select caps
           * from the probed caps that match the configured caps.
           */
          configured_caps = gst_uvc_sink_get_configured_caps (self);
          if (!configured_caps) {
            GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
                ("gst_uvc_sink_get_configured_caps failed"),
                ("gst_uvc_sink_get_configured_caps on current format failed"));
            gst_uvc_sink_unwatch (self);
            gst_element_set_state (GST_ELEMENT (self), GST_STATE_NULL);
            return;
          }

          cur_caps =
              gst_caps_intersect_full (self->probed_caps, configured_caps,
              GST_CAPS_INTERSECT_FIRST);
          GST_INFO_OBJECT (self, "UVC host selected %" GST_PTR_FORMAT,
              cur_caps);
          gst_caps_unref (configured_caps);

          prev_caps = gst_pad_get_current_caps (self->sinkpad);

          /* Re-evaluate on every commit, so a stale result of an earlier
           * commit, that never got streamed, doesn't stick. */
          caps_changed = !prev_caps
              || !gst_caps_is_subset (prev_caps, cur_caps);
          if (caps_changed)
            GST_DEBUG_OBJECT (self,
                "caps changed from %" GST_PTR_FORMAT, prev_caps);

          UVCSINK_STATE_LOCK (self);
          gst_caps_replace (&self->cur_caps, cur_caps);
          self->caps_changed = caps_changed;
          UVCSINK_STATE_UNLOCK (self);

          gst_clear_caps (&prev_caps);
          gst_caps_unref (cur_caps);
        }
        break;
      default:
        break;
    }
  }
}

static gboolean
gst_uvc_sink_watch (GstUvcSink * self)
{
  struct v4l2_event_subscription sub = {.type = UVC_EVENT_STREAMON };
  gboolean ret = TRUE;
  gint device_fd;
  gint fd;

  ret = gst_uvc_sink_prepare_configfs (self);
  if (!ret) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("could not parse configfs"), ("Check your configfs setup"));
    return FALSE;
  }

  g_object_get (G_OBJECT (self->v4l2sink), "device-fd", &device_fd, NULL);

  fd = dup3 (device_fd, device_fd + 1, O_CLOEXEC);
  if (fd < 0)
    return FALSE;

  self->poll = gst_poll_new (TRUE);
  gst_poll_fd_init (&self->pollfd);
  self->pollfd.fd = fd;
  gst_poll_add_fd (self->poll, &self->pollfd);
  gst_poll_fd_ctl_pri (self->poll, &self->pollfd, TRUE);

  if (ioctl (device_fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Failed to subscribe event"),
        ("UVC_EVENT_STREAMON could not be subscribed"));
    return FALSE;
  }

  sub.type = UVC_EVENT_STREAMOFF;
  if (ioctl (device_fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Failed to subscribe event"),
        ("UVC_EVENT_STREAMOFF could not be subscribed"));
    return FALSE;
  }

  sub.type = UVC_EVENT_DISCONNECT;
  if (ioctl (device_fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Failed to subscribe event"),
        ("UVC_EVENT_DISCONNECT could not be subscribed"));
    return FALSE;
  }

  sub.type = UVC_EVENT_SETUP;
  if (ioctl (device_fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Failed to subscribe event"),
        ("UVC_EVENT_SETUP could not be subscribed"));
    return FALSE;
  }

  sub.type = UVC_EVENT_DATA;
  if (ioctl (device_fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Failed to subscribe event"),
        ("UVC_EVENT_DATA could not be subscribed"));
    return FALSE;
  }

  if (!gst_pad_start_task (GST_PAD (self->sinkpad),
          (GstTaskFunction) gst_uvc_sink_task, self, NULL)) {
    GST_ELEMENT_ERROR (self, CORE, THREAD, ("Could not start pad task"),
        ("Could not start pad task"));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_uvc_sink_unwatch (GstUvcSink * self)
{
  struct v4l2_event_subscription sub = {.type = UVC_EVENT_DATA };
  gint device_fd;

  gst_poll_set_flushing (self->poll, TRUE);
  gst_pad_stop_task (GST_PAD (self->sinkpad));

  g_object_get (G_OBJECT (self->v4l2sink), "device-fd", &device_fd, NULL);

  if (ioctl (device_fd, VIDIOC_UNSUBSCRIBE_EVENT, &sub) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Failed to unsubscribe event"),
        ("UVC_EVENT_DATA could not be unsubscribed"));
    return FALSE;
  }

  sub.type = UVC_EVENT_SETUP;
  if (ioctl (device_fd, VIDIOC_UNSUBSCRIBE_EVENT, &sub) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Failed to unsubscribe event"),
        ("UVC_EVENT_SETUP could not be unsubscribed"));
    return FALSE;
  }

  sub.type = UVC_EVENT_STREAMON;
  if (ioctl (device_fd, VIDIOC_UNSUBSCRIBE_EVENT, &sub) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Failed to unsubscribe event"),
        ("UVC_EVENT_STREAMON could not be unsubscribed"));
    return FALSE;
  }

  sub.type = UVC_EVENT_STREAMOFF;
  if (ioctl (device_fd, VIDIOC_UNSUBSCRIBE_EVENT, &sub) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Failed to unsubscribe event"),
        ("UVC_EVENT_STREAMOFF could not be unsubscribed"));
    return FALSE;
  }

  sub.type = UVC_EVENT_DISCONNECT;
  if (ioctl (device_fd, VIDIOC_UNSUBSCRIBE_EVENT, &sub) < 0) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Failed to unsubscribe event"),
        ("UVC_EVENT_DISCONNECT could not be unsubscribed"));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_uvc_sink_prepare_configfs (GstUvcSink * self)
{
  gint device_fd;
  GValue device = G_VALUE_INIT;

  g_object_get (G_OBJECT (self->v4l2sink), "device-fd", &device_fd, NULL);
  g_object_get_property (G_OBJECT (self->v4l2sink), "device", &device);

  self->fc = configfs_parse_uvc_videodev (device_fd,
      g_value_get_string (&device));
  if (!self->fc) {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE,
        ("Failed to identify function configuration"),
        ("Check your configfs setup"));
    return FALSE;
  }

  uvc_fill_streaming_control (self, &self->probe, self->cur.bFrameIndex,
      self->cur.bFormatIndex, self->cur.dwFrameInterval);
  uvc_fill_streaming_control (self, &self->commit, self->cur.bFrameIndex,
      self->cur.bFormatIndex, self->cur.dwFrameInterval);

  return TRUE;
}

static gboolean
gst_uvc_sink_query_probed_caps (GstUvcSink * self)
{
  GstQuery *caps_query = gst_query_new_caps (NULL);
  GstCaps *query_caps;

  gst_clear_caps (&self->probed_caps);
  if (!gst_pad_query (self->v4l2sinkpad, caps_query))
    return FALSE;

  gst_query_parse_caps_result (caps_query, &query_caps);
  gst_query_unref (caps_query);

  self->probed_caps = gst_caps_copy (query_caps);

  UVCSINK_STATE_LOCK (self);
  gst_caps_replace (&self->cur_caps, self->probed_caps);
  UVCSINK_STATE_UNLOCK (self);

  return TRUE;
}

static GstStateChangeReturn
gst_uvc_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstUvcSink *self = GST_UVCSINK (element);
  int bret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG_OBJECT (self, "%s -> %s",
      gst_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_uvc_sink_open (self))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_uvc_sink_create_idle_probe (self);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_element_sync_state_with_parent (GST_ELEMENT (self->fakesink));
      gst_uvc_sink_remove_idle_probe (self);
      self->request_error_code = REQUEST_ERROR_CODE_NO_ERROR;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_uvc_sink_unwatch (self))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  bret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_uvc_sink_to_fakesink (self);
      break;
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_uvc_sink_watch (self))
        return GST_STATE_CHANGE_FAILURE;
      if (!gst_uvc_sink_query_probed_caps (self))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return bret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return GST_ELEMENT_REGISTER (uvcsink, plugin);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    uvcgadget,
    "gstuvcgadget plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
