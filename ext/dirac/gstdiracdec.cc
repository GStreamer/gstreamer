/* GStreamer
 * Copyright (C) 2004 David A. Schleef <ds@schleef.org>
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
#include <gst/video/video.h>

#include <seq_decompress.h>
#include <pic_io.h>

#define GST_TYPE_DIRACDEC \
  (gst_diracdec_get_type())
#define GST_DIRACDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DIRACDEC,GstDiracDec))
#define GST_DIRACDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DIRACDEC,GstDiracDec))
#define GST_IS_DIRACDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DIRACDEC))
#define GST_IS_DIRACDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DIRACDEC))

typedef struct _GstDiracDec GstDiracDec;
typedef struct _GstDiracDecClass GstDiracDecClass;

struct _GstDiracDec
{
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;

  SequenceDecompressor *decompress;

  PicOutput *output_image;

};

struct _GstDiracDecClass
{
  GstElementClass parent_class;
};

GType gst_diracdec_get_type (void);


/* elementfactory information */
GstElementDetails gst_diracdec_details = {
  "Dirac stream decoder",
  "Codec/Decoder/Video",
  "Decode DIRAC streams",
  "David Schleef <ds@schleef.org>",
};

GST_DEBUG_CATEGORY (diracdec_debug);
#define GST_CAT_DEFAULT diracdec_debug

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

static void gst_diracdec_base_init (gpointer g_class);
static void gst_diracdec_class_init (GstDiracDec * klass);
static void gst_diracdec_init (GstDiracDec * diracdec);

static void gst_diracdec_chain (GstPad * pad, GstData * _data);
static GstPadLinkReturn gst_diracdec_link (GstPad * pad, const GstCaps * caps);

static GstElementClass *parent_class = NULL;

/*static guint gst_diracdec_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_diracdec_get_type (void)
{
  static GType diracdec_type = 0;

  if (!diracdec_type) {
    static const GTypeInfo diracdec_info = {
      sizeof (GstDiracDec),
      gst_diracdec_base_init,
      NULL,
      (GClassInitFunc) gst_diracdec_class_init,
      NULL,
      NULL,
      sizeof (GstDiracDec),
      0,
      (GInstanceInitFunc) gst_diracdec_init,
    };

    diracdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstDiracDec", &diracdec_info,
        (GTypeFlags) 0);
  }
  return diracdec_type;
}

static GstStaticPadTemplate gst_diracdec_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_diracdec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/dirac, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (double) [ 1, MAX ]")
    );

static void
gst_diracdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_diracdec_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_diracdec_sink_pad_template));
  gst_element_class_set_details (element_class, &gst_diracdec_details);
}

static void
gst_diracdec_class_init (GstDiracDec * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  parent_class = GST_ELEMENT_CLASS (g_type_class_ref (GST_TYPE_ELEMENT));

  GST_DEBUG_CATEGORY_INIT (diracdec_debug, "diracdec", 0, "DIRAC decoder");
}

static void
gst_diracdec_init (GstDiracDec * diracdec)
{
  GST_DEBUG ("gst_diracdec_init: initializing");
  /* create the sink and src pads */

  diracdec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_diracdec_sink_pad_template), "sink");
  gst_element_add_pad (GST_ELEMENT (diracdec), diracdec->sinkpad);
  gst_pad_set_chain_function (diracdec->sinkpad, gst_diracdec_chain);
  gst_pad_set_link_function (diracdec->sinkpad, gst_diracdec_link);

  diracdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_diracdec_src_pad_template), "src");
  gst_pad_use_explicit_caps (diracdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (diracdec), diracdec->srcpad);

  diracdec->decompress = new SequenceDecompressor;
  diracdec->output_image = new PicOutput;
}

static GstPadLinkReturn
gst_diracdec_link (GstPad * pad, const GstCaps * caps)
{
  //GstDiracDec *diracdec = GST_DIRACDEC (gst_pad_get_parent (pad));
  GstStructure *structure;

  //GstCaps *srccaps;

  structure = gst_caps_get_structure (caps, 0);

  return GST_PAD_LINK_OK;
}

static void
gst_diracdec_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstDiracDec *diracdec;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  diracdec = GST_DIRACDEC (GST_OBJECT_PARENT (pad));

  gst_buffer_unref (buf);
}
