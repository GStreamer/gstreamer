/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include <string.h>

/*#define DEBUG_ENABLED*/
#include "gstsmokedec.h"
#include <gst/video/video.h>

/* elementfactory information */
GstElementDetails gst_smokedec_details = {
  "Smoke image decoder",
  "Codec/Decoder/Image",
  "Decode images from Smoke format",
  "Wim Taymans <wim@fluendo.com>",
};

GST_DEBUG_CATEGORY (smokedec_debug);
#define GST_CAT_DEFAULT smokedec_debug

/* SmokeDec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

static void gst_smokedec_base_init (gpointer g_class);
static void gst_smokedec_class_init (GstSmokeDec * klass);
static void gst_smokedec_init (GstSmokeDec * smokedec);

static void gst_smokedec_chain (GstPad * pad, GstData * _data);
static GstPadLinkReturn gst_smokedec_link (GstPad * pad, const GstCaps * caps);

static GstElementClass *parent_class = NULL;

/*static guint gst_smokedec_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_smokedec_get_type (void)
{
  static GType smokedec_type = 0;

  if (!smokedec_type) {
    static const GTypeInfo smokedec_info = {
      sizeof (GstSmokeDecClass),
      gst_smokedec_base_init,
      NULL,
      (GClassInitFunc) gst_smokedec_class_init,
      NULL,
      NULL,
      sizeof (GstSmokeDec),
      0,
      (GInstanceInitFunc) gst_smokedec_init,
    };

    smokedec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstSmokeDec", &smokedec_info,
        0);
  }
  return smokedec_type;
}

static GstStaticPadTemplate gst_smokedec_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_smokedec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/x-smoke, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (double) [ 1, MAX ]")
    );

static void
gst_smokedec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_smokedec_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_smokedec_sink_pad_template));
  gst_element_class_set_details (element_class, &gst_smokedec_details);
}

static void
gst_smokedec_class_init (GstSmokeDec * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  GST_DEBUG_CATEGORY_INIT (smokedec_debug, "smokedec", 0, "Smoke decoder");
}

static void
gst_smokedec_init (GstSmokeDec * smokedec)
{
  GST_DEBUG ("gst_smokedec_init: initializing");
  /* create the sink and src pads */

  smokedec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_smokedec_sink_pad_template), "sink");
  gst_element_add_pad (GST_ELEMENT (smokedec), smokedec->sinkpad);
  gst_pad_set_chain_function (smokedec->sinkpad, gst_smokedec_chain);
  gst_pad_set_link_function (smokedec->sinkpad, gst_smokedec_link);

  smokedec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_smokedec_src_pad_template), "src");
  gst_pad_use_explicit_caps (smokedec->srcpad);
  gst_element_add_pad (GST_ELEMENT (smokedec), smokedec->srcpad);

  /* reset the initial video state */
  smokedec->format = -1;
  smokedec->width = -1;
  smokedec->height = -1;
}

static GstPadLinkReturn
gst_smokedec_link (GstPad * pad, const GstCaps * caps)
{
  GstSmokeDec *smokedec = GST_SMOKEDEC (gst_pad_get_parent (pad));
  GstStructure *structure;
  GstCaps *srccaps;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_double (structure, "framerate", &smokedec->fps);
  gst_structure_get_int (structure, "width", &smokedec->width);
  gst_structure_get_int (structure, "height", &smokedec->height);

  srccaps = gst_caps_new_simple ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
      "width", G_TYPE_INT, smokedec->width,
      "height", G_TYPE_INT, smokedec->height,
      "framerate", G_TYPE_DOUBLE, smokedec->fps, NULL);

  /* at this point, we're pretty sure that this will be the output
   * format, so we'll set it. */
  gst_pad_set_explicit_caps (smokedec->srcpad, srccaps);

  smokecodec_decode_new (&smokedec->info);

  return GST_PAD_LINK_OK;
}

static void
gst_smokedec_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstSmokeDec *smokedec;
  guchar *data, *outdata;
  gulong size, outsize;
  GstBuffer *outbuf;
  SmokeCodecFlags flags;

  /*GstMeta *meta; */
  gint width, height;

  smokedec = GST_SMOKEDEC (GST_OBJECT_PARENT (pad));

  if (!GST_PAD_IS_LINKED (smokedec->srcpad)) {
    gst_buffer_unref (buf);
    return;
  }

  data = (guchar *) GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);
  GST_DEBUG ("gst_smokedec_chain: got buffer of %ld bytes in '%s'", size,
      GST_OBJECT_NAME (smokedec));

  GST_DEBUG ("gst_smokedec_chain: reading header %08lx", *(gulong *) data);
  smokecodec_parse_header (smokedec->info, data, size, &flags, &width, &height);

  outbuf = gst_buffer_new ();
  outsize = GST_BUFFER_SIZE (outbuf) = width * height + width * height / 2;
  outdata = GST_BUFFER_DATA (outbuf) = g_malloc (outsize);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);

  if (smokedec->height != height) {
    GstCaps *caps;

    smokedec->height = height;

    caps = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "framerate", G_TYPE_DOUBLE, smokedec->fps, NULL);
    gst_pad_set_explicit_caps (smokedec->srcpad, caps);
    gst_caps_free (caps);
  }

  smokecodec_decode (smokedec->info, data, size, outdata);

  GST_DEBUG ("gst_smokedec_chain: sending buffer");
  gst_pad_push (smokedec->srcpad, GST_DATA (outbuf));

  gst_buffer_unref (buf);
}
