/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
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
#include "gstoverlay.h"
#include <gst/video/video.h>

/* elementfactory information */
static GstElementDetails overlay_details = {
  "Video Overlay",
  "Filter/Editor/Video",
  "Overlay multiple video streams",
  "David Schleef <ds@schleef.org>"
};

static GstStaticPadTemplate overlay_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate overlay_sink1_factory =
GST_STATIC_PAD_TEMPLATE ("sink1",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate overlay_sink2_factory =
GST_STATIC_PAD_TEMPLATE ("sink2",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate overlay_sink3_factory =
GST_STATIC_PAD_TEMPLATE ("sink3",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

/* OVERLAY signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};


static void gst_overlay_class_init (GstOverlayClass * klass);
static void gst_overlay_base_init (GstOverlayClass * klass);
static void gst_overlay_init (GstOverlay * overlay);

static void gst_overlay_loop (GstElement * element);

static void gst_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_overlay_signals[LAST_SIGNAL] = { 0 }; */

static GType
gst_overlay_get_type (void)
{
  static GType overlay_type = 0;

  if (!overlay_type) {
    static const GTypeInfo overlay_info = {
      sizeof (GstOverlayClass),
      (GBaseInitFunc) gst_overlay_base_init,
      NULL,
      (GClassInitFunc) gst_overlay_class_init,
      NULL,
      NULL,
      sizeof (GstOverlay),
      0,
      (GInstanceInitFunc) gst_overlay_init,
    };
    overlay_type =
	g_type_register_static (GST_TYPE_ELEMENT, "GstOverlay", &overlay_info,
	0);
  }
  return overlay_type;
}

static void
gst_overlay_base_init (GstOverlayClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&overlay_sink1_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&overlay_sink2_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&overlay_sink3_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&overlay_src_factory));
  gst_element_class_set_details (element_class, &overlay_details);
}

static void
gst_overlay_class_init (GstOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_overlay_set_property;
  gobject_class->get_property = gst_overlay_get_property;

}

#if 0
static GstCaps *
gst_overlay_getcaps (GstPad * pad)
{
  GstCaps *caps;
  GstOverlay *overlay;

  overlay = GST_OVERLAY (gst_pad_get_parent (pad));

  if (overlay->width && overlay->height) {
    caps = GST_STATIC_CAPS ("overlay_sink2",
	"video/raw",
	"format", GST_TYPE_FOURCC (GST_MAKE_FOURCC ('I', '4', '2', '0')),
	"width", G_TYPE_INT (overlay->width),
	"height", G_TYPE_INT (overlay->height)
	);
  } else {
    caps = GST_STATIC_CAPS ("overlay_sink2",
	"video/raw",
	"format", GST_TYPE_FOURCC (GST_MAKE_FOURCC ('I', '4', '2', '0')),
	"width", G_TYPE_INT_RANGE (0, 4096),
	"height", G_TYPE_INT_RANGE (0, 4096)
	);
  }

  return caps;
}
#endif

static gboolean
gst_overlay_sinkconnect (GstPad * pad, const GstCaps * caps)
{
  GstOverlay *overlay;
  GstStructure *structure;

  overlay = GST_OVERLAY (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &overlay->width);
  gst_structure_get_int (structure, "height", &overlay->height);
  gst_structure_get_double (structure, "framerate", &overlay->framerate);

  /* forward to the next plugin */
  return gst_pad_try_set_caps (overlay->srcpad, caps);
}

static void
gst_overlay_init (GstOverlay * overlay)
{
  overlay->sinkpad1 =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&overlay_sink1_factory), "sink1");
  gst_pad_set_link_function (overlay->sinkpad1, gst_overlay_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->sinkpad1);

  overlay->sinkpad2 =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&overlay_sink2_factory), "sink2");
  gst_pad_set_link_function (overlay->sinkpad2, gst_overlay_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->sinkpad2);

  overlay->sinkpad3 =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&overlay_sink3_factory), "sink3");
  gst_pad_set_link_function (overlay->sinkpad3, gst_overlay_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->sinkpad3);

  overlay->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&overlay_src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->srcpad);

  gst_element_set_loop_function (GST_ELEMENT (overlay), gst_overlay_loop);
}

static void
gst_overlay_blend_i420 (guint8 * out, guint8 * in1, guint8 * in2, guint8 * in3,
    gint width, gint height)
{
  int mask;
  int i, j;
  guint8 *in1u, *in1v, *in2u, *in2v, *outu, *outv;
  int lumsize;
  int chromsize;
  int width2 = width / 2;
  int height2 = height / 2;

  lumsize = width * height;
  chromsize = width2 * height2;

  in1u = in1 + lumsize;
  in1v = in1u + chromsize;
  in2u = in2 + lumsize;
  in2v = in2u + chromsize;
  outu = out + lumsize;
  outv = outu + chromsize;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      mask = in3[i * width + j];
      out[i * width + j] = ((in1[i * width + j] * mask) +
	  (in2[i * width + j] * (255 - mask))) >> 8;
    }
  }

  for (i = 0; i < height / 2; i++) {
    for (j = 0; j < width / 2; j++) {
      mask =
	  (in3[(i * 2) * width + (j * 2)] + in3[(i * 2 + 1) * width + (j * 2)] +
	  in3[(i * 2) * width + (j * 2 + 1)] + in3[(i * 2 + 1) * width +
	      (j * 2 + 1)]) / 4;
      outu[i * width2 + j] =
	  ((in1u[i * width2 + j] * mask) + (in2u[i * width2 + j] * (255 -
		  mask))) >> 8;
      outv[i * width2 + j] =
	  ((in1v[i * width2 + j] * mask) + (in2v[i * width2 + j] * (255 -
		  mask))) >> 8;
    }
  }
}

static void
gst_overlay_loop (GstElement * element)
{
  GstOverlay *overlay;
  GstBuffer *out;
  GstBuffer *in1 = NULL, *in2 = NULL, *in3 = NULL;
  int size;

  overlay = GST_OVERLAY (element);

  in1 = GST_BUFFER (gst_pad_pull (overlay->sinkpad1));
  if (GST_IS_EVENT (in1)) {
    gst_pad_push (overlay->srcpad, GST_DATA (in1));
    /* FIXME */
    return;
  }
  in2 = GST_BUFFER (gst_pad_pull (overlay->sinkpad2));
  if (GST_IS_EVENT (in2)) {
    gst_pad_push (overlay->srcpad, GST_DATA (in2));
    /* FIXME */
    return;
  }
  in3 = GST_BUFFER (gst_pad_pull (overlay->sinkpad3));
  if (GST_IS_EVENT (in3)) {
    gst_pad_push (overlay->srcpad, GST_DATA (in3));
    /* FIXME */
    return;
  }

  g_return_if_fail (in1 != NULL);
  g_return_if_fail (in2 != NULL);
  g_return_if_fail (in3 != NULL);

  size = (overlay->width * overlay->height * 3) / 2;
  g_return_if_fail (GST_BUFFER_SIZE (in1) != size);
  g_return_if_fail (GST_BUFFER_SIZE (in2) != size);
  g_return_if_fail (GST_BUFFER_SIZE (in3) != size);

  out = gst_buffer_new_and_alloc (size);

  gst_overlay_blend_i420 (GST_BUFFER_DATA (out),
      GST_BUFFER_DATA (in1),
      GST_BUFFER_DATA (in2),
      GST_BUFFER_DATA (in3), overlay->width, overlay->height);

  GST_BUFFER_TIMESTAMP (out) = GST_BUFFER_TIMESTAMP (in1);
  GST_BUFFER_DURATION (out) = GST_BUFFER_DURATION (in1);

  gst_buffer_unref (in1);
  gst_buffer_unref (in2);
  gst_buffer_unref (in3);

  gst_pad_push (overlay->srcpad, GST_DATA (out));
}

static void
gst_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOverlay *overlay;

  overlay = GST_OVERLAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOverlay *overlay;

  overlay = GST_OVERLAY (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "overlay",
      GST_RANK_NONE, GST_TYPE_OVERLAY);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "overlay",
    "Overlay multiple video streams",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
