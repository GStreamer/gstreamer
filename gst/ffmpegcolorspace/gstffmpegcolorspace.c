/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * This file:
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include "config.h"
#endif

#include <gst/gst.h>
#include <avcodec.h>

#include "gstffmpegcodecmap.h"

GST_DEBUG_CATEGORY (ffmpegcolorspace_debug);
#define GST_CAT_DEFAULT ffmpegcolorspace_debug

#define GST_TYPE_FFMPEGCSP \
  (gst_ffmpegcsp_get_type())
#define GST_FFMPEGCSP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGCSP,GstFFMpegCsp))
#define GST_FFMPEGCSP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGCSP,GstFFMpegCsp))
#define GST_IS_FFMPEGCSP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGCSP))
#define GST_IS_FFMPEGCSP_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGCSP))

typedef struct _GstFFMpegCsp GstFFMpegCsp;
typedef struct _GstFFMpegCspClass GstFFMpegCspClass;

struct _GstFFMpegCsp
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;
  gfloat fps;
  enum PixelFormat from_pixfmt, to_pixfmt;
  AVPicture from_frame, to_frame;
  AVPaletteControl *palette;

  GstCaps *src_prefered;
  GstCaps *sink_prefered;
};

struct _GstFFMpegCspClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails ffmpegcsp_details = {
  "FFMPEG Colorspace converter",
  "Filter/Converter/Video",
  "Converts video from one colorspace to another",
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
};


/* Stereo signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};

static GType gst_ffmpegcsp_get_type (void);

static void gst_ffmpegcsp_base_init (GstFFMpegCspClass * klass);
static void gst_ffmpegcsp_class_init (GstFFMpegCspClass * klass);
static void gst_ffmpegcsp_init (GstFFMpegCsp * space);

static void gst_ffmpegcsp_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ffmpegcsp_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_ffmpegcsp_chain (GstPad * pad, GstBuffer * buffer);
static GstElementStateReturn gst_ffmpegcsp_change_state (GstElement * element);

static GstPadTemplate *srctempl, *sinktempl;
static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegcsp_signals[LAST_SIGNAL] = { 0 }; */


static GstCaps *
gst_ffmpegcsp_caps_remove_format_info (GstCaps * caps)
{
  int i;
  GstStructure *structure;
  GstCaps *rgbcaps;

  caps = gst_caps_make_writable (caps);

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

    gst_structure_set_name (structure, "video/x-raw-yuv");
    gst_structure_remove_field (structure, "format");
    gst_structure_remove_field (structure, "endianness");
    gst_structure_remove_field (structure, "depth");
    gst_structure_remove_field (structure, "bpp");
    gst_structure_remove_field (structure, "red_mask");
    gst_structure_remove_field (structure, "green_mask");
    gst_structure_remove_field (structure, "blue_mask");
    gst_structure_remove_field (structure, "alpha_mask");
  }

  gst_caps_do_simplify (caps);
  rgbcaps = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (rgbcaps); i++) {
    structure = gst_caps_get_structure (rgbcaps, i);

    gst_structure_set_name (structure, "video/x-raw-rgb");
  }

  gst_caps_append (caps, rgbcaps);

  return caps;
}

static GstCaps *
gst_ffmpegcsp_getcaps (GstPad * pad)
{
  GstFFMpegCsp *space;
  GstCaps *othercaps;
  GstCaps *caps;
  GstPad *otherpad;

  space = GST_FFMPEGCSP (GST_PAD_PARENT (pad));

  otherpad = (pad == space->srcpad) ? space->sinkpad : space->srcpad;
  /* we can do whatever the peer can */
  othercaps = gst_pad_peer_get_caps (otherpad);
  if (othercaps != NULL) {
    /* without the format info */
    othercaps = gst_ffmpegcsp_caps_remove_format_info (othercaps);
    /* and filtered against our padtemplate */
    caps = gst_caps_intersect (othercaps, gst_pad_get_pad_template_caps (pad));
    gst_caps_unref (othercaps);
  } else {
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  return caps;
}

static gboolean
gst_ffmpegcsp_configure_context (GstPad * pad, const GstCaps * caps, gint width,
    gint height)
{
  AVCodecContext *ctx;
  GstFFMpegCsp *space;

  space = GST_FFMPEGCSP (GST_PAD_PARENT (pad));

  /* loop over all possibilities and select the first one we can convert and
   * is accepted by the peer */
  ctx = avcodec_alloc_context ();

  ctx->width = width;
  ctx->height = height;
  ctx->pix_fmt = PIX_FMT_NB;
  gst_ffmpegcsp_caps_with_codectype (CODEC_TYPE_VIDEO, caps, ctx);
  if (ctx->pix_fmt == PIX_FMT_NB) {
    av_free (ctx);

    /* we disable ourself here */
    if (pad == space->srcpad) {
      space->to_pixfmt = PIX_FMT_NB;
    } else {
      space->from_pixfmt = PIX_FMT_NB;
    }

    return FALSE;
  } else {
    if (pad == space->srcpad) {
      space->to_pixfmt = ctx->pix_fmt;
    } else {
      space->from_pixfmt = ctx->pix_fmt;

      /* palette */
      if (space->palette)
        av_free (space->palette);
      space->palette = ctx->palctrl;
    }
    av_free (ctx);
  }
  return TRUE;
}

/* configureing the caps on a pad means that we should check if we
 * can get a fic format for that caps. Then we need to figure out
 * how we can convert that to the peer format */
static gboolean
gst_ffmpegcsp_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstFFMpegCsp *space;
  GstPad *otherpeer;
  GstPad *otherpad;
  int height, width;
  double framerate;
  const GValue *par = NULL;
  GstCaps **other_prefered, **prefered;

  space = GST_FFMPEGCSP (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (space, "setcaps on %s:%s with caps %" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);

  otherpad = (pad == space->srcpad) ? space->sinkpad : space->srcpad;
  prefered =
      (pad == space->srcpad) ? &space->src_prefered : &space->sink_prefered;
  other_prefered =
      (pad == space->srcpad) ? &space->sink_prefered : &space->src_prefered;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  gst_structure_get_double (structure, "framerate", &framerate);
  par = gst_structure_get_value (structure, "pixel-aspect-ratio");

  if (!gst_ffmpegcsp_configure_context (pad, caps, width, height))
    goto configure_error;

  *prefered = caps;

  otherpeer = gst_pad_get_peer (otherpad);
  if (otherpeer) {
    /* check passthrough */
    //if (gst_pad_accept_caps (otherpeer, caps)) {
    if (FALSE) {
      *other_prefered = gst_caps_ref (caps);
    } else {
      GstCaps *othercaps;

      /* set the size on the otherpad */
      othercaps = gst_pad_get_caps (otherpeer);
      if (othercaps) {
        GstCaps *targetcaps = gst_caps_copy_nth (othercaps, 0);

        gst_caps_unref (othercaps);

        gst_caps_set_simple (targetcaps,
            "width", G_TYPE_INT, width,
            "height", G_TYPE_INT, height,
            "framerate", G_TYPE_DOUBLE, framerate, NULL);

        if (par) {
          gst_caps_set_simple (targetcaps,
              "pixel-aspect-ratio", GST_TYPE_FRACTION,
              gst_value_get_fraction_numerator (par),
              gst_value_get_fraction_denominator (par), NULL);
        }
        *other_prefered = targetcaps;
      }
    }
    gst_object_unref (GST_OBJECT (otherpeer));
  }

  space->width = width;
  space->height = height;

  return TRUE;

configure_error:
  {
    GST_DEBUG ("could not configure context");
    return FALSE;
  }
}

static GType
gst_ffmpegcsp_get_type (void)
{
  static GType ffmpegcsp_type = 0;

  if (!ffmpegcsp_type) {
    static const GTypeInfo ffmpegcsp_info = {
      sizeof (GstFFMpegCspClass),
      (GBaseInitFunc) gst_ffmpegcsp_base_init,
      NULL,
      (GClassInitFunc) gst_ffmpegcsp_class_init,
      NULL,
      NULL,
      sizeof (GstFFMpegCsp),
      0,
      (GInstanceInitFunc) gst_ffmpegcsp_init,
    };

    ffmpegcsp_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstFFMpegColorspace", &ffmpegcsp_info, 0);
  }

  return ffmpegcsp_type;
}

static void
gst_ffmpegcsp_base_init (GstFFMpegCspClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);
  gst_element_class_set_details (element_class, &ffmpegcsp_details);
}

static void
gst_ffmpegcsp_class_init (GstFFMpegCspClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_ffmpegcsp_set_property;
  gobject_class->get_property = gst_ffmpegcsp_get_property;

  gstelement_class->change_state = gst_ffmpegcsp_change_state;

  GST_DEBUG_CATEGORY_INIT (ffmpegcolorspace_debug, "ffmpegcolorspace", 0,
      "FFMPEG-based colorspace converter");
}

static void
gst_ffmpegcsp_init (GstFFMpegCsp * space)
{
  space->sinkpad = gst_pad_new_from_template (sinktempl, "sink");
  gst_pad_set_getcaps_function (space->sinkpad, gst_ffmpegcsp_getcaps);
  gst_pad_set_setcaps_function (space->sinkpad, gst_ffmpegcsp_setcaps);
  gst_pad_set_chain_function (space->sinkpad, gst_ffmpegcsp_chain);
  gst_element_add_pad (GST_ELEMENT (space), space->sinkpad);

  space->srcpad = gst_pad_new_from_template (srctempl, "src");
  gst_element_add_pad (GST_ELEMENT (space), space->srcpad);
  gst_pad_set_getcaps_function (space->srcpad, gst_ffmpegcsp_getcaps);
  gst_pad_set_setcaps_function (space->srcpad, gst_ffmpegcsp_setcaps);

  space->from_pixfmt = space->to_pixfmt = PIX_FMT_NB;
  space->palette = NULL;
}

static GstFlowReturn
gst_ffmpegcsp_chain (GstPad * pad, GstBuffer * buffer)
{
  GstBuffer *inbuf = GST_BUFFER (buffer);
  GstFFMpegCsp *space;
  GstBuffer *outbuf = NULL;

  //GstCaps *outcaps;

  space = GST_FFMPEGCSP (GST_PAD_PARENT (pad));

  if (!GST_PAD_IS_USABLE (space->srcpad)) {
    gst_buffer_unref (inbuf);
    return GST_FLOW_ERROR;
  }

  /* asume passthrough */
  guint size =
      avpicture_get_size (space->from_pixfmt, space->width, space->height);

  outbuf = gst_pad_alloc_buffer (space->srcpad, GST_BUFFER_OFFSET_NONE, size,
      space->src_prefered);
  if (outbuf == NULL) {
    return GST_FLOW_ERROR;
  }

  if (space->from_pixfmt == PIX_FMT_NB || space->to_pixfmt == PIX_FMT_NB) {
    GST_ELEMENT_ERROR (space, CORE, NOT_IMPLEMENTED, (NULL),
        ("attempting to convert colorspaces between unknown formats"));
    gst_buffer_unref (inbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (space->from_pixfmt == space->to_pixfmt) {
    GST_DEBUG ("passthrough conversion %" GST_PTR_FORMAT,
        GST_PAD_CAPS (space->srcpad));
    outbuf = inbuf;
  } else {
    /* convert */
    gst_ffmpegcsp_avpicture_fill (&space->from_frame,
        GST_BUFFER_DATA (inbuf),
        space->from_pixfmt, space->width, space->height);
    if (space->palette)
      space->from_frame.data[1] = (uint8_t *) space->palette;
    gst_ffmpegcsp_avpicture_fill (&space->to_frame,
        GST_BUFFER_DATA (outbuf),
        space->to_pixfmt, space->width, space->height);
    img_convert (&space->to_frame, space->to_pixfmt,
        &space->from_frame, space->from_pixfmt, space->width, space->height);

    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);
    gst_buffer_unref (inbuf);
  }

  return gst_pad_push (space->srcpad, outbuf);
}

static GstElementStateReturn
gst_ffmpegcsp_change_state (GstElement * element)
{
  GstFFMpegCsp *space;

  space = GST_FFMPEGCSP (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      if (space->palette)
        av_free (space->palette);
      space->palette = NULL;
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_ffmpegcsp_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFFMpegCsp *space;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FFMPEGCSP (object));
  space = GST_FFMPEGCSP (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_ffmpegcsp_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFFMpegCsp *space;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FFMPEGCSP (object));
  space = GST_FFMPEGCSP (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_ffmpegcolorspace_register (GstPlugin * plugin)
{
  GstCaps *caps;

  /* template caps */
  caps = gst_ffmpegcsp_codectype_to_caps (CODEC_TYPE_VIDEO, NULL);

  /* build templates */
  srctempl = gst_pad_template_new ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS, gst_caps_copy (caps));

  /* the sink template will do palette handling as well... */
  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);

  return gst_element_register (plugin, "ffmpegcolorspace",
      GST_RANK_NONE, GST_TYPE_FFMPEGCSP);
}
