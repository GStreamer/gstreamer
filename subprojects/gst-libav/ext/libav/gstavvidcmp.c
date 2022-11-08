/* GStreamer
 * Copyright (C) 2022 Intel Corporation
 *     Author: U. Artie Eoff <ullysses.a.eoff@intel.com>
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
 * License along with this library; if not, write to the0
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-avvideocompare
 * @title: avvideocompare
 * @short_description: A libav based video compare element
 *
 * avvideocompare accepts two input video streams with the same width, height,
 * framerate and format.  The two incoming buffers are compared to each other
 * via the chosen compare method (e.g. ssim or psnr).
 *
 * If the stats-file property is specified, then the computed result for each
 * frame comparison will be written to the file, or stdout if stats-file is '-'.
 *
 * The first incoming buffer is passed through, unchanged, to the srcpad.
 *
 * ## Sample pipelines
 * ```
 * gst-launch-1.0 videotestsrc num-buffers=100          \
 *   ! video/x-raw,format=NV12                          \
 *   ! videobalance brightness=0.005 hue=0.005          \
 *   ! avvideocompare method=psnr stats-file=- name=cmp \
 *   ! fakesink videotestsrc ! video/x-raw,format=NV12  \
 *   ! cmp.
 * ```
 * ```
 * gst-launch-1.0 videotestsrc num-buffers=100                  \
 *   ! tee name=orig ! queue ! avenc_mjpeg                      \
 *   ! jpegparse ! avdec_mjpeg                                  \
 *   ! avvideocompare method=ssim stats-file=stats.log name=cmp \
 *   ! fakesink orig. ! queue ! cmp.
 * ```
 *
 * Since: 1.24
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>

#include <glib/gprintf.h>

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>
#include <gst/video/video.h>

#include "gstav.h"
#include "gstavcodecmap.h"

#define GST_FFMPEGVIDCMP_FORMATS "{ " \
  "ARGB, BGRA, ABGR, RGBA, xRGB, BGRx, xBGR, RGBx, RGB16, " \
  "GRAY8, NV12, NV21, YUY2, UYVY, I420, Y42B, Y444, VUYA, " \
  "P010_10LE, Y410, P012_LE, Y212_LE, Y412_LE" \
  " }"

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_ffmpegvidcmp_src_tmpl =
  GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_FFMPEGVIDCMP_FORMATS)));
/* *INDENT-ON* */

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_ffmpegvidcmp_sink1_tmpl =
  GST_STATIC_PAD_TEMPLATE ("sink_1",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_FFMPEGVIDCMP_FORMATS)));
/* *INDENT-ON* */

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_ffmpegvidcmp_sink2_tmpl =
  GST_STATIC_PAD_TEMPLATE ("sink_2",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_FFMPEGVIDCMP_FORMATS)));
/* *INDENT-ON* */

typedef enum
{
  GST_FFMPEGVIDCMP_METHOD_SSIM,
  GST_FFMPEGVIDCMP_METHOD_PSNR,
} GstFFMpegVidCmpMethod;

#define GST_FFMPEGVIDCMP_METHOD_TYPE (gst_ffmpegvidcmp_method_get_type())

/**
 * GstFFMpegVidCmpMethod:
 *
 * Since: 1.24
 */
static GType
gst_ffmpegvidcmp_method_get_type (void)
{
  static gsize g_type = 0;

  static const GEnumValue enum_values[] = {
    {GST_FFMPEGVIDCMP_METHOD_SSIM, "SSIM", "ssim"},
    {GST_FFMPEGVIDCMP_METHOD_PSNR, "PSNR", "psnr"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&g_type)) {
    const GType type =
        g_enum_register_static ("GstFFMpegVidCmpMethod", enum_values);
    g_once_init_leave (&g_type, type);
  }
  return g_type;
}

enum
{
  PROP_0,
  PROP_STATS_FILE,
  PROP_METHOD,
};

#define DEFAULT_STATS_FILE NULL
#define DEFAULT_METHOD GST_FFMPEGVIDCMP_METHOD_SSIM

#define GST_TYPE_FFMPEGVIDCMP (gst_ffmpegvidcmp_get_type())

G_DECLARE_FINAL_TYPE (GstFFMpegVidCmp, gst_ffmpegvidcmp,
    GST, FFMPEGVIDCMP, GstElement);

struct _GstFFMpegVidCmp
{
  GstElement element;

  /* pads */
  GstPad *srcpad;
  GstPad *sinkpad1;
  GstPad *sinkpad2;

  GstCollectPads *collect;
  GstCollectData *collect_data1;
  GstCollectData *collect_data2;

  /* negotiated format */
  gint width;
  gint height;
  gint fps_num;
  gint fps_denom;
  GstVideoInfo vinfo1;
  GstVideoInfo vinfo2;

  AVFilterGraph *filter_graph;
  AVFilterContext *in1_ctx;
  AVFilterContext *in2_ctx;
  AVFilterContext *out_ctx;
  enum AVPixelFormat pixfmt;

  gchar *stats_file;

  GstFFMpegVidCmpMethod method;
};

G_DEFINE_TYPE (GstFFMpegVidCmp, gst_ffmpegvidcmp, GST_TYPE_ELEMENT);

static void gst_ffmpegvidcmp_finalize (GObject * object);
static GstFlowReturn gst_ffmpegvidcmp_collected (GstCollectPads * pads,
    GstFFMpegVidCmp * self);
static void gst_ffmpegvidcmp_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ffmpegvidcmp_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_ffmpegvidcmp_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_ffmpegvidcmp_sink_event (GstCollectPads * pads,
    GstCollectData * data, GstEvent * event, gpointer user_data);
static gboolean gst_ffmpegvidcmp_sink_query (GstCollectPads * pads,
    GstCollectData * data, GstQuery * query, gpointer user_data);

static void
gst_ffmpegvidcmp_class_init (GstFFMpegVidCmpClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_ffmpegvidcmp_set_property;
  gobject_class->get_property = gst_ffmpegvidcmp_get_property;
  gobject_class->finalize = (GObjectFinalizeFunc) gst_ffmpegvidcmp_finalize;

  g_object_class_install_property (gobject_class, PROP_STATS_FILE,
      g_param_spec_string ("stats-file", "Stats File Location",
          "Set file where to store per-frame difference information"
          ", '-' for stdout", DEFAULT_STATS_FILE, G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method", "Method", "Method to compare video frames",
          GST_FFMPEGVIDCMP_METHOD_TYPE, DEFAULT_METHOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_type_mark_as_plugin_api (GST_FFMPEGVIDCMP_METHOD_TYPE, 0);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_ffmpegvidcmp_change_state);

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_ffmpegvidcmp_sink1_tmpl);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_ffmpegvidcmp_sink2_tmpl);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_ffmpegvidcmp_src_tmpl);
  gst_element_class_set_static_metadata (gstelement_class,
      "A libav video compare element", "Filter/Compare/Video",
      "Compare Video", "U. Artie Eoff <ullysses.a.eoff@intel.com");
}

static void
gst_ffmpegvidcmp_reset (GstFFMpegVidCmp * self)
{
  GST_OBJECT_LOCK (self);

  self->width = -1;
  self->height = -1;
  self->fps_num = 0;
  self->fps_denom = 1;
  self->pixfmt = AV_PIX_FMT_NONE;

  self->in1_ctx = NULL;
  self->in2_ctx = NULL;
  self->out_ctx = NULL;

  if (self->filter_graph)
    avfilter_graph_free (&self->filter_graph);

  GST_OBJECT_UNLOCK (self);
}

static void
gst_ffmpegvidcmp_init (GstFFMpegVidCmp * self)
{
  gst_ffmpegvidcmp_reset (self);

  self->stats_file = g_strdup (DEFAULT_STATS_FILE);
  self->method = DEFAULT_METHOD;

  self->sinkpad1 =
      gst_pad_new_from_static_template (&gst_ffmpegvidcmp_sink1_tmpl, "sink_1");
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad1);

  self->sinkpad2 =
      gst_pad_new_from_static_template (&gst_ffmpegvidcmp_sink2_tmpl, "sink_2");
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad2);

  self->srcpad =
      gst_pad_new_from_static_template (&gst_ffmpegvidcmp_src_tmpl, "src");
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  self->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (self->collect,
      (GstCollectPadsFunction) GST_DEBUG_FUNCPTR (gst_ffmpegvidcmp_collected),
      self);
  gst_collect_pads_set_event_function (self->collect,
      GST_DEBUG_FUNCPTR (gst_ffmpegvidcmp_sink_event), self);
  gst_collect_pads_set_query_function (self->collect,
      GST_DEBUG_FUNCPTR (gst_ffmpegvidcmp_sink_query), self);

  self->collect_data1 = gst_collect_pads_add_pad (self->collect, self->sinkpad1,
      sizeof (GstCollectData), NULL, TRUE);
  self->collect_data2 = gst_collect_pads_add_pad (self->collect, self->sinkpad2,
      sizeof (GstCollectData), NULL, TRUE);
}

static void
gst_ffmpegvidcmp_finalize (GObject * object)
{
  GstFFMpegVidCmp *self = GST_FFMPEGVIDCMP (object);

  g_free (self->stats_file);

  gst_ffmpegvidcmp_reset (self);

  if (self->collect)
    gst_object_unref (self->collect);

  G_OBJECT_CLASS (gst_ffmpegvidcmp_parent_class)->finalize (object);
}

static void
gst_ffmpegvidcmp_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFFMpegVidCmp *self = GST_FFMPEGVIDCMP (object);

  GST_OBJECT_LOCK (self);
  switch (prop_id) {
    case PROP_STATS_FILE:
    {
      if (self->filter_graph) {
        GST_WARNING_OBJECT (self, "changing the stats file after the filter "
            "graph is initialized is not supported");
        break;
      }
      g_free (self->stats_file);
      self->stats_file = g_value_dup_string (value);
      break;
    }
    case PROP_METHOD:
    {
      if (self->filter_graph) {
        GST_WARNING_OBJECT (self, "changing the method after the filter "
            "graph is initialized is not supported");
        break;
      }
      self->method = g_value_get_enum (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

static void
gst_ffmpegvidcmp_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFFMpegVidCmp *self = GST_FFMPEGVIDCMP (object);

  GST_OBJECT_LOCK (self);
  switch (prop_id) {
    case PROP_STATS_FILE:
      g_value_set_string (value, self->stats_file);
      break;
    case PROP_METHOD:
      g_value_set_enum (value, self->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (self);
}

static gboolean
gst_ffmpegvidcmp_setcaps (GstFFMpegVidCmp * self, GstPad * pad, GstCaps * caps)
{
  GstVideoInfo vinfo;

  g_return_val_if_fail (GST_IS_FFMPEGVIDCMP (self), FALSE);

  gst_video_info_init (&vinfo);
  if (!gst_video_info_from_caps (&vinfo, caps))
    return FALSE;

  GST_OBJECT_LOCK (self);

  self->width = GST_VIDEO_INFO_WIDTH (&vinfo);
  self->height = GST_VIDEO_INFO_HEIGHT (&vinfo);
  self->fps_num = GST_VIDEO_INFO_FPS_N (&vinfo);
  self->fps_denom = GST_VIDEO_INFO_FPS_D (&vinfo);

  if (pad == self->sinkpad1)
    self->vinfo1 = vinfo;
  else
    self->vinfo2 = vinfo;

  self->pixfmt =
      gst_ffmpeg_videoformat_to_pixfmt (GST_VIDEO_INFO_FORMAT (&vinfo));
  if (self->pixfmt == AV_PIX_FMT_NONE) {
    GST_OBJECT_UNLOCK (self);
    GST_ERROR_OBJECT (self, "failed to find suitable ffmpeg pixfmt");
    return FALSE;
  }

  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

static gboolean
gst_ffmpegvidcmp_sink_event (GstCollectPads * pads, GstCollectData * data,
    GstEvent * event, gpointer user_data)
{
  GstFFMpegVidCmp *self = GST_FFMPEGVIDCMP (user_data);
  GstPad *pad = data->pad;
  gboolean ret = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_ffmpegvidcmp_setcaps (self, pad, caps);

      /* forward sinkpad1 caps to downstream */
      if (ret && pad == self->sinkpad1) {
        ret = gst_pad_push_event (self->srcpad, event);
        event = NULL;
        break;
      }

      gst_event_unref (event);
      event = NULL;
      break;
    }
    case GST_EVENT_STREAM_START:
    case GST_EVENT_SEGMENT:
    {
      /* forward the sinkpad1 event to downstream */
      if (pad == self->sinkpad1) {
        ret = gst_pad_push_event (self->srcpad, event);
        event = NULL;
      }
      break;
    }
    default:
      break;
  }

  if (event != NULL)
    return gst_collect_pads_event_default (pads, data, event, FALSE);

  return ret;
}

static gboolean
gst_ffmpegvidcmp_sink_query (GstCollectPads * pads, GstCollectData * data,
    GstQuery * query, gpointer user_data)
{
  GstFFMpegVidCmp *self = GST_FFMPEGVIDCMP (user_data);
  GstPad *pad = data->pad;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
      if (pad == self->sinkpad1)
        return gst_pad_peer_query (self->srcpad, query);
      break;
    default:
      break;
  }
  return gst_collect_pads_query_default (pads, data, query, FALSE);
}

static GstStateChangeReturn
gst_ffmpegvidcmp_change_state (GstElement * element, GstStateChange transition)
{
  GstFFMpegVidCmp *self = GST_FFMPEGVIDCMP (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_ffmpegvidcmp_reset (self);
      gst_collect_pads_start (self->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_collect_pads_stop (self->collect);
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_ffmpegvidcmp_parent_class)->change_state (element,
      transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_ffmpegvidcmp_reset (self);
      break;
    default:
      break;
  }

  return ret;
}

static gint
init_filter_graph (GstFFMpegVidCmp * self)
{
  AVFilterInOut *inputs = NULL;
  AVFilterInOut *outputs = NULL;
  GEnumClass *enum_class;
  GEnumValue *method;
  gchar *args = NULL;
  gchar *f = NULL;
  gint res = -1;

  enum_class = g_type_class_ref (GST_FFMPEGVIDCMP_METHOD_TYPE);
  method = g_enum_get_value (enum_class, self->method);
  g_type_class_unref (enum_class);
  if (!method) {
    GST_ERROR_OBJECT (self, "unknown compare method");
    return -1;
  }

  GST_INFO_OBJECT (self, "    method : %s", method->value_nick);
  GST_INFO_OBJECT (self, "stats-file : %s", self->stats_file);

  if (self->stats_file)
    f = g_strdup_printf ("=f=\\'%s\\'", self->stats_file);
  else
    f = g_strdup ("");

  args =
      g_strdup_printf
      ("buffer=video_size=%dx%d:pix_fmt=%d:time_base=1/1:pixel_aspect=0/1[in1];"
      "buffer=video_size=%dx%d:pix_fmt=%d:time_base=1/1:pixel_aspect=0/1[in2];"
      "[in1][in2]%s%s[out];[out]buffersink", self->width, self->height,
      self->pixfmt, self->width, self->height, self->pixfmt, method->value_nick,
      f);

  g_free (f);

  self->filter_graph = avfilter_graph_alloc ();
  if (!self->filter_graph) {
    GST_ERROR_OBJECT (self, "failed to allocate filter graph");
    g_free (args);
    return -1;
  }

  res = avfilter_graph_parse2 (self->filter_graph, args, &inputs, &outputs);
  g_free (args);
  if (res < 0) {
    GST_ERROR_OBJECT (self, "failed to parse filter graph");
    return res;
  }

  if (inputs || outputs) {
    GST_ERROR_OBJECT (self, "unlinked inputs/outputs in filter graph");
    return -1;
  }

  res = avfilter_graph_config (self->filter_graph, NULL);
  if (res < 0) {
    GST_ERROR_OBJECT (self, "failed to configure filter graph");
    return res;
  }

  self->in1_ctx =
      avfilter_graph_get_filter (self->filter_graph, "Parsed_buffer_0");
  self->in2_ctx =
      avfilter_graph_get_filter (self->filter_graph, "Parsed_buffer_1");
  self->out_ctx =
      avfilter_graph_get_filter (self->filter_graph, "Parsed_buffersink_3");

  if (!self->in1_ctx || !self->in2_ctx || !self->out_ctx) {
    GST_ERROR_OBJECT (self, "failed to get filter contexts");
    return -1;
  }

  return res;
}

static gint
process_filter_graph (GstFFMpegVidCmp * self, AVFrame * in1, AVFrame * in2)
{
  AVFrame *out;
  gint res;

  if (!self->filter_graph) {
    res = init_filter_graph (self);
    if (res < 0)
      return res;
  }

  res = av_buffersrc_add_frame (self->in1_ctx, in1);
  if (res < 0)
    return res;

  res = av_buffersrc_add_frame (self->in2_ctx, in2);
  if (res < 0)
    return res;

  out = av_frame_alloc ();
  out->width = self->width;
  out->height = self->height;
  out->format = self->pixfmt;

  res = av_buffersink_get_frame (self->out_ctx, out);

  av_frame_unref (out);
  av_frame_free (&out);

  return res;
}

static void
_fill_avpicture (GstFFMpegVidCmp * self, AVFrame * picture,
    GstVideoFrame * vframe)
{
  gint i;

  for (i = 0; i < GST_VIDEO_FRAME_N_PLANES (vframe); ++i) {
    picture->data[i] = GST_VIDEO_FRAME_PLANE_DATA (vframe, i);
    picture->linesize[i] = GST_VIDEO_FRAME_PLANE_STRIDE (vframe, i);
  }

  picture->width = GST_VIDEO_FRAME_WIDTH (vframe);
  picture->height = GST_VIDEO_FRAME_HEIGHT (vframe);
  picture->format = self->pixfmt;
}

static GstFlowReturn
gst_ffmpegvidcmp_collected (GstCollectPads * pads, GstFFMpegVidCmp * self)
{
  GstBuffer *buf1 = NULL, *buf2 = NULL;

  GST_OBJECT_LOCK (self);

  if (G_UNLIKELY (self->fps_num == 0))
    goto not_negotiated;

  if (!gst_pad_has_current_caps (self->sinkpad1) ||
      !gst_pad_has_current_caps (self->sinkpad2))
    goto not_negotiated;

  if (GST_VIDEO_INFO_WIDTH (&self->vinfo1) !=
      GST_VIDEO_INFO_WIDTH (&self->vinfo2) ||
      GST_VIDEO_INFO_HEIGHT (&self->vinfo1) !=
      GST_VIDEO_INFO_HEIGHT (&self->vinfo2) ||
      GST_VIDEO_INFO_FORMAT (&self->vinfo1) !=
      GST_VIDEO_INFO_FORMAT (&self->vinfo2) ||
      GST_VIDEO_INFO_FPS_D (&self->vinfo1) !=
      GST_VIDEO_INFO_FPS_D (&self->vinfo2) ||
      GST_VIDEO_INFO_FPS_N (&self->vinfo1) !=
      GST_VIDEO_INFO_FPS_N (&self->vinfo2))
    goto input_formats_do_not_match;

  buf1 = gst_collect_pads_pop (pads, self->collect_data1);
  buf2 = gst_collect_pads_pop (pads, self->collect_data2);

  /* compare */
  if (buf1 && buf2) {
    /* *INDENT-OFF* */
    AVFrame in1 = { {0,} };
    AVFrame in2 = { {0,} };
    /* *INDENT-ON* */
    GstVideoFrame frame1, frame2;

    if (!gst_video_frame_map (&frame1, &self->vinfo1, buf1, GST_MAP_READ))
      goto map_failed;

    if (!gst_video_frame_map (&frame2, &self->vinfo2, buf2, GST_MAP_READ)) {
      gst_video_frame_unmap (&frame1);
      goto map_failed;
    }

    _fill_avpicture (self, &in1, &frame1);
    _fill_avpicture (self, &in2, &frame2);

    if (process_filter_graph (self, &in1, &in2) < 0)
      GST_WARNING_OBJECT (self, "Could not process filter graph");

    gst_video_frame_unmap (&frame1);
    gst_video_frame_unmap (&frame2);
  }

  GST_OBJECT_UNLOCK (self);

  if (buf2)
    gst_buffer_unref (buf2);

  if (!buf1) {
    gst_pad_push_event (self->srcpad, gst_event_new_eos ());
    return GST_FLOW_EOS;
  }

  return gst_pad_push (self->srcpad, buf1);

  /* ERRORS */
not_negotiated:
  {
    GST_OBJECT_UNLOCK (self);
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
        ("No input format negotiated"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
input_formats_do_not_match:
  {
    GstCaps *caps1, *caps2;

    GST_OBJECT_UNLOCK (self);
    caps1 = gst_pad_get_current_caps (self->sinkpad1);
    caps2 = gst_pad_get_current_caps (self->sinkpad2);
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, (NULL),
        ("input formats don't match: %" GST_PTR_FORMAT " vs. %" GST_PTR_FORMAT,
            caps1, caps2));
    gst_caps_unref (caps1);
    gst_caps_unref (caps2);
    return GST_FLOW_ERROR;
  }
map_failed:
  {
    GST_OBJECT_UNLOCK (self);
    GST_DEBUG_OBJECT (self, "Failed to map frame");
    gst_buffer_unref (buf2);
    gst_buffer_unref (buf1);
    return GST_FLOW_ERROR;
  }
}

gboolean
gst_ffmpegvidcmp_register (GstPlugin * plugin)
{
  return gst_element_register (plugin, "avvideocompare", GST_RANK_NONE,
      GST_TYPE_FFMPEGVIDCMP);
}
