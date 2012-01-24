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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_FFMPEG_UNINSTALLED
#  include <avcodec.h>
#else
#  include <libavcodec/avcodec.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"
#include "gstffmpegutils.h"


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
        g_enum_register_static ("GstFFMpegDeinterlaceModes", modes_types);
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

  enum PixelFormat pixfmt;
  AVPicture from_frame, to_frame;
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

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details_simple (element_class,
      "FFMPEG Deinterlace element", "Filter/Effect/Video/Deinterlace",
      "Deinterlace video", "Luca Ognibene <luogni@tin.it>");
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
gst_ffmpegdeinterlace_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstFFMpegDeinterlace *deinterlace =
      GST_FFMPEGDEINTERLACE (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  AVCodecContext *ctx;
  GstCaps *src_caps;
  gboolean ret;

  if (!gst_structure_get_int (structure, "width", &deinterlace->width))
    return FALSE;
  if (!gst_structure_get_int (structure, "height", &deinterlace->height))
    return FALSE;

  deinterlace->interlaced = FALSE;
  gst_structure_get_boolean (structure, "interlaced", &deinterlace->interlaced);
  gst_ffmpegdeinterlace_update_passthrough (deinterlace);

  ctx = avcodec_alloc_context ();
  ctx->width = deinterlace->width;
  ctx->height = deinterlace->height;
  ctx->pix_fmt = PIX_FMT_NB;
  gst_ffmpeg_caps_with_codectype (AVMEDIA_TYPE_VIDEO, caps, ctx);
  if (ctx->pix_fmt == PIX_FMT_NB) {
    av_free (ctx);
    return FALSE;
  }

  deinterlace->pixfmt = ctx->pix_fmt;

  av_free (ctx);

  deinterlace->to_size =
      avpicture_get_size (deinterlace->pixfmt, deinterlace->width,
      deinterlace->height);

  src_caps = gst_caps_copy (caps);
  gst_caps_set_simple (src_caps, "interlaced", G_TYPE_BOOLEAN,
      deinterlace->interlaced, NULL);
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
      ret = gst_ffmpegdeinterlace_sink_setcaps (pad, caps);
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

  deinterlace->pixfmt = PIX_FMT_NB;

  deinterlace->interlaced = FALSE;
  deinterlace->passthrough = FALSE;
  deinterlace->reconfigure = FALSE;
  deinterlace->mode = DEFAULT_MODE;
  deinterlace->new_mode = -1;
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
    if (deinterlace->new_mode != -1)
      deinterlace->mode = deinterlace->new_mode;
    deinterlace->new_mode = -1;

    deinterlace->reconfigure = FALSE;
    GST_OBJECT_UNLOCK (deinterlace);
    if (gst_pad_has_current_caps (deinterlace->srcpad)) {
      GstCaps *caps;

      caps = gst_pad_get_current_caps (deinterlace->sinkpad);
      gst_ffmpegdeinterlace_sink_setcaps (deinterlace->sinkpad, caps);
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

  avpicture_deinterlace (&deinterlace->to_frame, &deinterlace->from_frame,
      deinterlace->pixfmt, deinterlace->width, deinterlace->height);
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
  return gst_element_register (plugin, "ffdeinterlace",
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
