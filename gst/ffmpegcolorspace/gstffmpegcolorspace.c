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

#define GST_TYPE_FFMPEGCOLORSPACE \
  (gst_ffmpegcolorspace_get_type())
#define GST_FFMPEGCOLORSPACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGCOLORSPACE,GstFFMpegColorspace))
#define GST_FFMPEGCOLORSPACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGCOLORSPACE,GstFFMpegColorspace))
#define GST_IS_FFMPEGCOLORSPACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGCOLORSPACE))
#define GST_IS_FFMPEGCOLORSPACE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGCOLORSPACE))

typedef struct _GstFFMpegColorspace GstFFMpegColorspace;
typedef struct _GstFFMpegColorspaceClass GstFFMpegColorspaceClass;

struct _GstFFMpegColorspace
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;
  gfloat fps;
  enum PixelFormat from_pixfmt, to_pixfmt;
  AVPicture from_frame, to_frame;
  GstCaps *sinkcaps;
};

struct _GstFFMpegColorspaceClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails ffmpegcolorspace_details = {
  "FFMPEG-based colorspace converter in gst-plugins",
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
  ARG_0
};

static GType gst_ffmpegcolorspace_get_type (void);

static void gst_ffmpegcolorspace_base_init (GstFFMpegColorspaceClass * klass);
static void gst_ffmpegcolorspace_class_init (GstFFMpegColorspaceClass * klass);
static void gst_ffmpegcolorspace_init (GstFFMpegColorspace * space);

static void gst_ffmpegcolorspace_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ffmpegcolorspace_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPadLinkReturn
gst_ffmpegcolorspace_pad_link (GstPad * pad, const GstCaps * caps);

static void gst_ffmpegcolorspace_chain (GstPad * pad, GstData * data);
static GstElementStateReturn
gst_ffmpegcolorspace_change_state (GstElement * element);

static GstPadTemplate *srctempl, *sinktempl;
static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegcolorspace_signals[LAST_SIGNAL] = { 0 }; */


static GstCaps *
gst_ffmpegcolorspace_caps_remove_format_info (GstCaps * caps)
{
  int i;
  GstStructure *structure;
  GstCaps *rgbcaps;

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
gst_ffmpegcolorspace_getcaps (GstPad * pad)
{
  GstFFMpegColorspace *space;
  GstCaps *othercaps = NULL;
  GstCaps *caps;
  GstPad *otherpad;

  space = GST_FFMPEGCOLORSPACE (gst_pad_get_parent (pad));

  otherpad = (pad == space->srcpad) ? space->sinkpad : space->srcpad;

  othercaps = gst_pad_get_allowed_caps (otherpad);

  othercaps = gst_ffmpegcolorspace_caps_remove_format_info (othercaps);

  caps = gst_caps_intersect (othercaps, gst_pad_get_pad_template_caps (pad));
  gst_caps_free (othercaps);

  return caps;
}

static GstPadLinkReturn
gst_ffmpegcolorspace_pad_link (GstPad * pad, const GstCaps * caps)
{
  GstFFMpegColorspace *space;
  GstStructure *structure;
  const GstCaps *othercaps;
  GstPad *otherpad;
  GstPadLinkReturn ret;
  enum PixelFormat pix_fmt;
  int height, width;
  double framerate;

  space = GST_FFMPEGCOLORSPACE (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  gst_structure_get_double (structure, "framerate", &framerate);

  otherpad = (pad == space->srcpad) ? space->sinkpad : space->srcpad;

  GST_DEBUG_OBJECT (space, "pad_link on %s:%s with caps %" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);

  /* FIXME attempt and/or check for passthru */

  /* loop over all possibilities and select the first one we can convert and
   * is accepted by the peer */
  pix_fmt = gst_ffmpeg_caps_to_pix_fmt (caps);
  if (pix_fmt == PIX_FMT_NB) {
    /* we disable ourself here */
    if (pad == space->srcpad) {
      space->to_pixfmt = PIX_FMT_NB;
    } else {
      space->from_pixfmt = PIX_FMT_NB;
    }

    return GST_PAD_LINK_REFUSED;
  }


  /* set the size on the otherpad */
  othercaps = gst_pad_get_negotiated_caps (otherpad);
  if (othercaps) {
    GstCaps *newothercaps = gst_caps_copy (othercaps);

    gst_caps_set_simple (newothercaps,
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "framerate", G_TYPE_DOUBLE, framerate, NULL);
    ret = gst_pad_try_set_caps (otherpad, newothercaps);
    if (GST_PAD_LINK_FAILED (ret)) {
      return ret;
    }
  }

  if (pad == space->srcpad) {
    space->to_pixfmt = pix_fmt;
  } else {
    space->from_pixfmt = pix_fmt;
  }

  space->width = width;
  space->height = height;

  return GST_PAD_LINK_OK;
}

static GType
gst_ffmpegcolorspace_get_type (void)
{
  static GType ffmpegcolorspace_type = 0;

  if (!ffmpegcolorspace_type) {
    static const GTypeInfo ffmpegcolorspace_info = {
      sizeof (GstFFMpegColorspaceClass),
      (GBaseInitFunc) gst_ffmpegcolorspace_base_init,
      NULL,
      (GClassInitFunc) gst_ffmpegcolorspace_class_init,
      NULL,
      NULL,
      sizeof (GstFFMpegColorspace),
      0,
      (GInstanceInitFunc) gst_ffmpegcolorspace_init,
    };

    ffmpegcolorspace_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstFFMpegColorspace", &ffmpegcolorspace_info, 0);
  }

  return ffmpegcolorspace_type;
}

static void
gst_ffmpegcolorspace_base_init (GstFFMpegColorspaceClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);
  gst_element_class_set_details (element_class, &ffmpegcolorspace_details);
}

static void
gst_ffmpegcolorspace_class_init (GstFFMpegColorspaceClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_ffmpegcolorspace_set_property;
  gobject_class->get_property = gst_ffmpegcolorspace_get_property;

  gstelement_class->change_state = gst_ffmpegcolorspace_change_state;
}

static void
gst_ffmpegcolorspace_init (GstFFMpegColorspace * space)
{
  space->sinkpad = gst_pad_new_from_template (sinktempl, "sink");
  gst_pad_set_link_function (space->sinkpad, gst_ffmpegcolorspace_pad_link);
  gst_pad_set_getcaps_function (space->sinkpad, gst_ffmpegcolorspace_getcaps);
  gst_pad_set_chain_function (space->sinkpad, gst_ffmpegcolorspace_chain);
  gst_element_add_pad (GST_ELEMENT (space), space->sinkpad);

  space->srcpad = gst_pad_new_from_template (srctempl, "src");
  gst_element_add_pad (GST_ELEMENT (space), space->srcpad);
  gst_pad_set_link_function (space->srcpad, gst_ffmpegcolorspace_pad_link);
  gst_pad_set_getcaps_function (space->srcpad, gst_ffmpegcolorspace_getcaps);

  space->from_pixfmt = space->to_pixfmt = PIX_FMT_NB;
}

static void
gst_ffmpegcolorspace_chain (GstPad * pad, GstData * data)
{
  GstBuffer *inbuf = GST_BUFFER (data);
  GstFFMpegColorspace *space;
  GstBuffer *outbuf = NULL;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (inbuf != NULL);

  space = GST_FFMPEGCOLORSPACE (gst_pad_get_parent (pad));

  g_return_if_fail (space != NULL);
  g_return_if_fail (GST_IS_FFMPEGCOLORSPACE (space));

  if (!GST_PAD_IS_USABLE (space->srcpad)) {
    gst_buffer_unref (inbuf);
    return;
  }

  if (!gst_pad_is_negotiated (space->srcpad)) {
    if (GST_PAD_LINK_FAILED (gst_pad_renegotiate (space->srcpad))) {
      GST_ELEMENT_ERROR (space, CORE, NEGOTIATION, (NULL), GST_ERROR_SYSTEM);
      gst_buffer_unref (inbuf);
      return;
    }
  }

  if (space->from_pixfmt == PIX_FMT_NB || space->to_pixfmt == PIX_FMT_NB) {
    GST_ELEMENT_ERROR (space, CORE, NOT_IMPLEMENTED, (NULL),
        ("attempting to convert colorspaces between unknown formats"));
    gst_buffer_unref (inbuf);
    return;
  }

  if (space->from_pixfmt == space->to_pixfmt) {
    outbuf = inbuf;
  } else {
    /* use bufferpool here */
    guint size = avpicture_get_size (space->to_pixfmt,
        space->width,
        space->height);

    outbuf = gst_pad_alloc_buffer (space->srcpad, GST_BUFFER_OFFSET_NONE, size);

    /* convert */
    avpicture_fill ((AVPicture *) & space->from_frame, GST_BUFFER_DATA (inbuf),
        space->from_pixfmt, space->width, space->height);
    avpicture_fill ((AVPicture *) & space->to_frame, GST_BUFFER_DATA (outbuf),
        space->to_pixfmt, space->width, space->height);
    img_convert ((AVPicture *) & space->to_frame, space->to_pixfmt,
        (AVPicture *) & space->from_frame, space->from_pixfmt,
        space->width, space->height);

    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);

    gst_buffer_unref (inbuf);
  }

  gst_pad_push (space->srcpad, GST_DATA (outbuf));
}

static GstElementStateReturn
gst_ffmpegcolorspace_change_state (GstElement * element)
{
  GstFFMpegColorspace *space;

  space = GST_FFMPEGCOLORSPACE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_ffmpegcolorspace_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFFMpegColorspace *space;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FFMPEGCOLORSPACE (object));
  space = GST_FFMPEGCOLORSPACE (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_ffmpegcolorspace_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFFMpegColorspace *space;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FFMPEGCOLORSPACE (object));
  space = GST_FFMPEGCOLORSPACE (object);

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
  caps = gst_ffmpeg_pix_fmt_to_caps ();

  /* build templates */
  srctempl = gst_pad_template_new ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS, gst_caps_copy (caps));
  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);

  avcodec_init ();
  return gst_element_register (plugin, "ffmpegcolorspace",
      GST_RANK_PRIMARY, GST_TYPE_FFMPEGCOLORSPACE);
}
