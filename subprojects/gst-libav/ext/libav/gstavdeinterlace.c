/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * This file:
 * Copyright (C) 2005 Luca Ognibene <luogni@tin.it>
 * Copyright (C) 2006 Martin Zlomek <martin.zlomek@itonis.tv>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstav.h"
#include "gstavcodecmap.h"
#include "gstavutils.h"


/* Properties */

#define DEFAULT_MODE            GST_FFMPEGDEINTERLACE_MODE_AUTO

enum
{
  PROP_0,
  PROP_MODE,
  PROP_LAST
};

typedef enum
{
  GST_FFMPEGDEINTERLACE_MODE_AUTO,
  GST_FFMPEGDEINTERLACE_MODE_INTERLACED,
  GST_FFMPEGDEINTERLACE_MODE_DISABLED
} GstFFMpegDeinterlaceMode;

#define GST_TYPE_FFMPEGDEINTERLACE_MODES (gst_ffmpegdeinterlace_modes_get_type ())
static GType
gst_ffmpegdeinterlace_modes_get_type (void)
{
  static GType deinterlace_modes_type = 0;

  static const GEnumValue modes_types[] = {
    {GST_FFMPEGDEINTERLACE_MODE_AUTO, "Auto detection", "auto"},
    {GST_FFMPEGDEINTERLACE_MODE_INTERLACED, "Force deinterlacing",
        "interlaced"},
    {GST_FFMPEGDEINTERLACE_MODE_DISABLED, "Run in passthrough mode",
        "disabled"},
    {0, NULL, NULL},
  };

  if (!deinterlace_modes_type) {
    deinterlace_modes_type =
        g_enum_register_static ("GstLibAVDeinterlaceModes", modes_types);
  }
  return deinterlace_modes_type;
}

typedef struct _GstFFMpegDeinterlace
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;
  gint to_size;

  GstFFMpegDeinterlaceMode mode;

  gboolean interlaced;          /* is input interlaced? */
  gboolean passthrough;

  gboolean reconfigure;
  GstFFMpegDeinterlaceMode new_mode;

  enum AVPixelFormat pixfmt;
  AVFrame from_frame, to_frame;

  AVFilterContext *buffersink_ctx;
  AVFilterContext *buffersrc_ctx;
  AVFilterGraph *filter_graph;
  AVFrame *filter_frame;
  int last_width, last_height;
  enum AVPixelFormat last_pixfmt;

} GstFFMpegDeinterlace;

typedef struct _GstFFMpegDeinterlaceClass
{
  GstElementClass parent_class;
} GstFFMpegDeinterlaceClass;

#define GST_TYPE_FFMPEGDEINTERLACE \
  (gst_ffmpegdeinterlace_get_type())
#define GST_FFMPEGDEINTERLACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGDEINTERLACE,GstFFMpegDeinterlace))
#define GST_FFMPEGDEINTERLACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGDEINTERLACE,GstFFMpegDeinterlace))
#define GST_IS_FFMPEGDEINTERLACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGDEINTERLACE))
#define GST_IS_FFMPEGDEINTERLACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGDEINTERLACE))

GType gst_ffmpegdeinterlace_get_type (void);

static void gst_ffmpegdeinterlace_set_property (GObject * self, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ffmpegdeinterlace_get_property (GObject * self, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("I420"))
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("I420"))
    );

G_DEFINE_TYPE (GstFFMpegDeinterlace, gst_ffmpegdeinterlace, GST_TYPE_ELEMENT);

static GstFlowReturn gst_ffmpegdeinterlace_chain (GstPad * pad,
    GstObject * parent, GstBuffer * inbuf);

static void gst_ffmpegdeinterlace_dispose (GObject * obj);

static void
gst_ffmpegdeinterlace_class_init (GstFFMpegDeinterlaceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_ffmpegdeinterlace_set_property;
  gobject_class->get_property = gst_ffmpegdeinterlace_get_property;

  /**
   * GstFFMpegDeinterlace:mode
   *
   * This selects whether the deinterlacing methods should
   * always be applied or if they should only be applied
   * on content that has the "interlaced" flag on the caps.
   *
   * Since: 0.10.13
   */
  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode", "Deinterlace Mode",
          GST_TYPE_FFMPEGDEINTERLACE_MODES,
          DEFAULT_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  gst_element_class_set_static_metadata (element_class,
      "libav Deinterlace element", "Filter/Effect/Video/Deinterlace",
      "Deinterlace video", "Luca Ognibene <luogni@tin.it>");

  gobject_class->dispose = gst_ffmpegdeinterlace_dispose;

  gst_type_mark_as_plugin_api (GST_TYPE_FFMPEGDEINTERLACE_MODES, 0);
}

static void
gst_ffmpegdeinterlace_update_passthrough (GstFFMpegDeinterlace * deinterlace)
{
  deinterlace->passthrough =
      (deinterlace->mode == GST_FFMPEGDEINTERLACE_MODE_DISABLED
      || (!deinterlace->interlaced
          && deinterlace->mode != GST_FFMPEGDEINTERLACE_MODE_INTERLACED));
  GST_DEBUG_OBJECT (deinterlace, "Passthrough: %d", deinterlace->passthrough);
}

static gboolean
gst_ffmpegdeinterlace_sink_setcaps (GstPad * pad, GstObject * parent,
    GstCaps * caps)
{
  GstFFMpegDeinterlace *deinterlace = GST_FFMPEGDEINTERLACE (parent);
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  const gchar *imode;
  AVCodecContext *ctx;
  GstCaps *src_caps;
  gboolean ret;

  /* FIXME: use GstVideoInfo etc. */
  if (!gst_structure_get_int (structure, "width", &deinterlace->width))
    return FALSE;
  if (!gst_structure_get_int (structure, "height", &deinterlace->height))
    return FALSE;

  deinterlace->interlaced = FALSE;
  imode = gst_structure_get_string (structure, "interlace-mode");
  if (imode && (!strcmp (imode, "interleaved") || !strcmp (imode, "mixed"))) {
    deinterlace->interlaced = TRUE;
  }
  gst_ffmpegdeinterlace_update_passthrough (deinterlace);

  ctx = avcodec_alloc_context3 (NULL);
  ctx->width = deinterlace->width;
  ctx->height = deinterlace->height;
  ctx->pix_fmt = AV_PIX_FMT_NB;
  gst_ffmpeg_caps_with_codectype (AVMEDIA_TYPE_VIDEO, caps, ctx);
  if (ctx->pix_fmt == AV_PIX_FMT_NB) {
    gst_ffmpeg_avcodec_close (ctx);
    av_free (ctx);
    return FALSE;
  }

  deinterlace->pixfmt = ctx->pix_fmt;

  av_free (ctx);

  deinterlace->to_size =
      av_image_get_buffer_size (deinterlace->pixfmt, deinterlace->width,
      deinterlace->height, 1);

  src_caps = gst_caps_copy (caps);
  gst_caps_set_simple (src_caps, "interlace-mode", G_TYPE_STRING,
      deinterlace->interlaced ? "progressive" : imode, NULL);
  ret = gst_pad_set_caps (deinterlace->srcpad, src_caps);
  gst_caps_unref (src_caps);

  return ret;
}

static gboolean
gst_ffmpegdeinterlace_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstFFMpegDeinterlace *deinterlace = GST_FFMPEGDEINTERLACE (parent);
  gboolean ret = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_ffmpegdeinterlace_sink_setcaps (pad, parent, caps);
      gst_event_unref (event);
      break;
    }
    default:
      ret = gst_pad_push_event (deinterlace->srcpad, event);
      break;
  }

  return ret;
}

static void
gst_ffmpegdeinterlace_init (GstFFMpegDeinterlace * deinterlace)
{
  deinterlace->sinkpad =
      gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_event_function (deinterlace->sinkpad,
      gst_ffmpegdeinterlace_sink_event);
  gst_pad_set_chain_function (deinterlace->sinkpad,
      gst_ffmpegdeinterlace_chain);
  gst_element_add_pad (GST_ELEMENT (deinterlace), deinterlace->sinkpad);

  deinterlace->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (deinterlace), deinterlace->srcpad);

  deinterlace->pixfmt = AV_PIX_FMT_NB;

  deinterlace->interlaced = FALSE;
  deinterlace->passthrough = FALSE;
  deinterlace->reconfigure = FALSE;
  deinterlace->mode = DEFAULT_MODE;
  deinterlace->new_mode = -1;
  deinterlace->last_width = -1;
  deinterlace->last_height = -1;
  deinterlace->last_pixfmt = AV_PIX_FMT_NONE;
}

static void
delete_filter_graph (GstFFMpegDeinterlace * deinterlace)
{
  if (deinterlace->filter_graph) {
    av_frame_free (&deinterlace->filter_frame);
    avfilter_graph_free (&deinterlace->filter_graph);
  }
}

static void
gst_ffmpegdeinterlace_dispose (GObject * obj)
{
  GstFFMpegDeinterlace *deinterlace = GST_FFMPEGDEINTERLACE (obj);

  delete_filter_graph (deinterlace);

  G_OBJECT_CLASS (gst_ffmpegdeinterlace_parent_class)->dispose (obj);
}

static int
init_filter_graph (GstFFMpegDeinterlace * deinterlace,
    enum AVPixelFormat pixfmt, int width, int height)
{
  AVFilterInOut *inputs = NULL, *outputs = NULL;
  char args[512];
  int res;

  delete_filter_graph (deinterlace);
  deinterlace->filter_graph = avfilter_graph_alloc ();
  snprintf (args, sizeof (args),
      "buffer=video_size=%dx%d:pix_fmt=%d:time_base=1/1:pixel_aspect=0/1[in];"
      "[in]yadif[out];" "[out]buffersink", width, height, pixfmt);
  res =
      avfilter_graph_parse2 (deinterlace->filter_graph, args, &inputs,
      &outputs);
  if (res < 0)
    return res;
  if (inputs || outputs)
    return -1;
  res = avfilter_graph_config (deinterlace->filter_graph, NULL);
  if (res < 0)
    return res;

  deinterlace->buffersrc_ctx =
      avfilter_graph_get_filter (deinterlace->filter_graph, "Parsed_buffer_0");
  deinterlace->buffersink_ctx =
      avfilter_graph_get_filter (deinterlace->filter_graph,
      "Parsed_buffersink_2");
  if (!deinterlace->buffersrc_ctx || !deinterlace->buffersink_ctx)
    return -1;
  deinterlace->filter_frame = av_frame_alloc ();
  deinterlace->last_width = width;
  deinterlace->last_height = height;
  deinterlace->last_pixfmt = pixfmt;

  return 0;
}

static int
process_filter_graph (GstFFMpegDeinterlace * deinterlace, AVFrame * dst,
    const AVFrame * src, enum AVPixelFormat pixfmt, int width, int height)
{
  int res;

  if (!deinterlace->filter_graph || width != deinterlace->last_width ||
      height != deinterlace->last_height
      || pixfmt != deinterlace->last_pixfmt) {
    res = init_filter_graph (deinterlace, pixfmt, width, height);
    if (res < 0)
      return res;
  }

  memcpy (deinterlace->filter_frame->data, src->data, sizeof (src->data));
  memcpy (deinterlace->filter_frame->linesize, src->linesize,
      sizeof (src->linesize));
  deinterlace->filter_frame->width = width;
  deinterlace->filter_frame->height = height;
  deinterlace->filter_frame->format = pixfmt;
  res =
      av_buffersrc_add_frame (deinterlace->buffersrc_ctx,
      deinterlace->filter_frame);
  if (res < 0)
    return res;
  res =
      av_buffersink_get_frame (deinterlace->buffersink_ctx,
      deinterlace->filter_frame);
  if (res < 0)
    return res;
  av_image_copy (dst->data, dst->linesize,
      (const uint8_t **) deinterlace->filter_frame->data,
      deinterlace->filter_frame->linesize, pixfmt, width, height);
  av_frame_unref (deinterlace->filter_frame);

  return 0;
}

static GstFlowReturn
gst_ffmpegdeinterlace_chain (GstPad * pad, GstObject * parent,
    GstBuffer * inbuf)
{
  GstFFMpegDeinterlace *deinterlace = GST_FFMPEGDEINTERLACE (parent);
  GstBuffer *outbuf = NULL;
  GstFlowReturn result;
  GstMapInfo from_map, to_map;

  GST_OBJECT_LOCK (deinterlace);
  if (deinterlace->reconfigure) {
    GstCaps *caps;

    if ((gint) deinterlace->new_mode != -1)
      deinterlace->mode = deinterlace->new_mode;
    deinterlace->new_mode = -1;

    deinterlace->reconfigure = FALSE;
    GST_OBJECT_UNLOCK (deinterlace);
    if ((caps = gst_pad_get_current_caps (deinterlace->srcpad))) {
      gst_ffmpegdeinterlace_sink_setcaps (deinterlace->sinkpad, parent, caps);
      gst_caps_unref (caps);
    }
  } else {
    GST_OBJECT_UNLOCK (deinterlace);
  }

  if (deinterlace->passthrough)
    return gst_pad_push (deinterlace->srcpad, inbuf);

  outbuf = gst_buffer_new_and_alloc (deinterlace->to_size);

  gst_buffer_map (inbuf, &from_map, GST_MAP_READ);
  gst_ffmpeg_avpicture_fill (&deinterlace->from_frame, from_map.data,
      deinterlace->pixfmt, deinterlace->width, deinterlace->height);

  gst_buffer_map (outbuf, &to_map, GST_MAP_WRITE);
  gst_ffmpeg_avpicture_fill (&deinterlace->to_frame, to_map.data,
      deinterlace->pixfmt, deinterlace->width, deinterlace->height);

  process_filter_graph (deinterlace, &deinterlace->to_frame,
      &deinterlace->from_frame, deinterlace->pixfmt, deinterlace->width,
      deinterlace->height);
  gst_buffer_unmap (outbuf, &to_map);
  gst_buffer_unmap (inbuf, &from_map);

  gst_buffer_copy_into (outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  result = gst_pad_push (deinterlace->srcpad, outbuf);

  gst_buffer_unref (inbuf);

  return result;
}

gboolean
gst_ffmpegdeinterlace_register (GstPlugin * plugin)
{
  return gst_element_register (plugin, "avdeinterlace",
      GST_RANK_NONE, GST_TYPE_FFMPEGDEINTERLACE);
}

static void
gst_ffmpegdeinterlace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFFMpegDeinterlace *self;

  g_return_if_fail (GST_IS_FFMPEGDEINTERLACE (object));
  self = GST_FFMPEGDEINTERLACE (object);

  switch (prop_id) {
    case PROP_MODE:{
      gint new_mode;

      GST_OBJECT_LOCK (self);
      new_mode = g_value_get_enum (value);
      if (self->mode != new_mode && gst_pad_has_current_caps (self->srcpad)) {
        self->reconfigure = TRUE;
        self->new_mode = new_mode;
      } else {
        self->mode = new_mode;
        gst_ffmpegdeinterlace_update_passthrough (self);
      }
      GST_OBJECT_UNLOCK (self);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
  }

}

static void
gst_ffmpegdeinterlace_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstFFMpegDeinterlace *self;

  g_return_if_fail (GST_IS_FFMPEGDEINTERLACE (object));
  self = GST_FFMPEGDEINTERLACE (object);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, self->mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, prop_id, pspec);
  }
}
