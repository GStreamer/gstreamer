/* GStreamer
 * Copyright (C) 2004 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2004 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
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

#include <gst/video/video.h>

#include "gstdiracdec.h"

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
static void gst_diracdec_dispose (GObject * object);

static void gst_diracdec_chain (GstPad * pad, GstData * data);
static GstStateChangeReturn gst_diracdec_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

/*static guint gst_diracdec_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_diracdec_get_type (void)
{
  static GType diracdec_type = 0;

  if (!diracdec_type) {
    static const GTypeInfo diracdec_info = {
      sizeof (GstDiracDecClass),
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
    /* FIXME: 444 (planar? packed?), 411 (Y41B? Y41P?) */
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ I420, YUY2, Y800 }"))
    );

static GstStaticPadTemplate gst_diracdec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dirac")
    );

static void
gst_diracdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_diracdec_src_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_diracdec_sink_pad_template);
  gst_element_class_set_details_simple (element_class, "Dirac stream decoder",
      "Codec/Decoder/Video", "Decode DIRAC streams",
      "David Schleef <ds@schleef.org>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
}

static void
gst_diracdec_class_init (GstDiracDec * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = GST_ELEMENT_CLASS (g_type_class_ref (GST_TYPE_ELEMENT));

  gobject_class->dispose = gst_diracdec_dispose;
  element_class->change_state = gst_diracdec_change_state;

  GST_DEBUG_CATEGORY_INIT (diracdec_debug, "diracdec", 0, "DIRAC decoder");
}

static void
gst_diracdec_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_diracdec_init (GstDiracDec * diracdec)
{
  GST_DEBUG ("gst_diracdec_init: initializing");
  /* create the sink and src pads */

  diracdec->sinkpad =
      gst_pad_new_from_static_template (&gst_diracdec_sink_pad_template,
      "sink");
  gst_pad_set_chain_function (diracdec->sinkpad, gst_diracdec_chain);
  gst_element_add_pad (GST_ELEMENT (diracdec), diracdec->sinkpad);

  diracdec->srcpad =
      gst_pad_new_from_static_template (&gst_diracdec_src_pad_template, "src");
  gst_pad_use_explicit_caps (diracdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (diracdec), diracdec->srcpad);

  /* no capsnego done yet */
  diracdec->width = -1;
  diracdec->height = -1;
  diracdec->fps = 0;
  diracdec->fcc = 0;
}

static guint32
gst_diracdec_chroma_to_fourcc (dirac_chroma_t chroma)
{
  guint32 fourcc = 0;

  switch (chroma) {
    case Yonly:
      fourcc = GST_MAKE_FOURCC ('Y', '8', '0', '0');
      break;
    case format422:
      fourcc = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
      break;
      /* planar? */
    case format420:
    case format444:
    case format411:
    default:
      break;
  }

  return fourcc;
}

static gboolean
gst_diracdec_link (GstDiracDec * diracdec,
    gint width, gint height, gdouble fps, guint32 fourcc)
{
  GstCaps *caps;

  if (width == diracdec->width &&
      height == diracdec->height &&
      fps == diracdec->fps && fourcc == diracdec->fcc) {
    return TRUE;
  }

  if (!fourcc) {
    g_warning ("Chroma not supported\n");
    return FALSE;
  }

  caps = gst_caps_new_simple ("video/x-raw-yuv",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "format", GST_TYPE_FOURCC, fourcc, "framerate", G_TYPE_DOUBLE, fps, NULL);

  if (gst_pad_set_explicit_caps (diracdec->srcpad, caps)) {
    diracdec->width = width;
    diracdec->height = height;
    switch (fourcc) {
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
        diracdec->size = width * height * 2;
        break;
      case GST_MAKE_FOURCC ('Y', '8', '0', '0'):
        diracdec->size = width * height;
        break;
    }
    diracdec->fcc = fourcc;
    diracdec->fps = fps;

    return TRUE;
  }

  return FALSE;
}

static void
gst_diracdec_chain (GstPad * pad, GstData * _data)
{
  GstDiracDec *diracdec = GST_DIRACDEC (gst_pad_get_parent (pad));
  GstBuffer *buf = GST_BUFFER (_data), *out;
  gboolean c = TRUE;

  /* get state and do something */
  while (c) {
    switch (dirac_parse (diracdec->decoder)) {
      case STATE_BUFFER:
        if (buf) {
          /* provide data to decoder */
          dirac_buffer (diracdec->decoder, GST_BUFFER_DATA (buf),
              GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf));
          gst_buffer_unref (buf);
          buf = NULL;
        } else {
          /* need more data */
          c = FALSE;
        }
        break;

      case STATE_SEQUENCE:{
        guint8 *buf[3];
        gint fps_num, fps_denom;

        fps_num = diracdec->decoder->seq_params.frame_rate.numerator;
        fps_denom = diracdec->decoder->seq_params.frame_rate.denominator;

        /* start-of-sequence - allocate buffer */
        if (!gst_diracdec_link (diracdec,
                diracdec->decoder->seq_params.width,
                diracdec->decoder->seq_params.height,
                (gdouble) fps_num / (gdouble) fps_denom,
                gst_diracdec_chroma_to_fourcc (diracdec->decoder->
                    seq_params.chroma))) {
          GST_ELEMENT_ERROR (diracdec, CORE, NEGOTIATION, (NULL),
              ("Failed to set caps to %dx%d @ %d fps (format=" GST_FOURCC_FORMAT
                  "/%d)", diracdec->decoder->seq_params.width,
                  diracdec->decoder->seq_params.height,
                  diracdec->decoder->seq_params.frame_rate,
                  gst_diracdec_chroma_to_fourcc (diracdec->decoder->
                      seq_params.chroma),
                  diracdec->decoder->seq_params.chroma));
          c = FALSE;
          break;
        }

        g_free (diracdec->decoder->fbuf->buf[0]);
        g_free (diracdec->decoder->fbuf->buf[1]);
        g_free (diracdec->decoder->fbuf->buf[2]);
        buf[0] = (guchar *) g_malloc (diracdec->decoder->seq_params.width *
            diracdec->decoder->seq_params.height);
        if (diracdec->decoder->seq_params.chroma != Yonly) {
          buf[1] =
              (guchar *) g_malloc (diracdec->decoder->seq_params.chroma_width *
              diracdec->decoder->seq_params.chroma_height);
          buf[2] =
              (guchar *) g_malloc (diracdec->decoder->seq_params.chroma_width *
              diracdec->decoder->seq_params.chroma_height);
        }
        dirac_set_buf (diracdec->decoder, buf, NULL);
        break;
      }

      case STATE_SEQUENCE_END:
        /* end-of-sequence - free buffer */
        g_free (diracdec->decoder->fbuf->buf[0]);
        diracdec->decoder->fbuf->buf[0] = NULL;
        g_free (diracdec->decoder->fbuf->buf[1]);
        diracdec->decoder->fbuf->buf[1] = NULL;
        g_free (diracdec->decoder->fbuf->buf[2]);
        diracdec->decoder->fbuf->buf[2] = NULL;
        break;

      case STATE_PICTURE_START:
        /* start of one picture */
        break;

      case STATE_PICTURE_AVAIL:
        /* one picture is decoded */
        out = gst_pad_alloc_buffer (diracdec->srcpad, 0, diracdec->size);
        memcpy (GST_BUFFER_DATA (out), diracdec->decoder->fbuf->buf[0],
            diracdec->width * diracdec->height);
        if (diracdec->fcc != GST_MAKE_FOURCC ('Y', '8', '0', '0')) {
          memcpy (GST_BUFFER_DATA (out) + (diracdec->width *
                  diracdec->height), diracdec->decoder->fbuf->buf[1],
              diracdec->decoder->seq_params.chroma_width *
              diracdec->decoder->seq_params.chroma_height);
          memcpy (GST_BUFFER_DATA (out) +
              (diracdec->decoder->seq_params.chroma_width *
                  diracdec->decoder->seq_params.chroma_height) +
              (diracdec->width * diracdec->height),
              diracdec->decoder->fbuf->buf[2],
              diracdec->decoder->seq_params.chroma_width *
              diracdec->decoder->seq_params.chroma_height);
        }
        GST_BUFFER_TIMESTAMP (out) = (guint64) (GST_SECOND *
            diracdec->decoder->frame_params.fnum / diracdec->fps);
        GST_BUFFER_DURATION (out) = (guint64) (GST_SECOND / diracdec->fps);
        gst_pad_push (diracdec->srcpad, GST_DATA (out));
        break;

      case STATE_INVALID:
      default:
        GST_ELEMENT_ERROR (diracdec, LIBRARY, TOO_LAZY, (NULL), (NULL));
        c = FALSE;
        break;
    }
  }
}

static GstStateChangeReturn
gst_diracdec_change_state (GstElement * element, GstStateChange transition)
{
  GstDiracDec *diracdec = GST_DIRACDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!(diracdec->decoder = dirac_decoder_init (0)))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      dirac_decoder_close (diracdec->decoder);
      diracdec->width = diracdec->height = -1;
      diracdec->fps = 0.;
      diracdec->fcc = 0;
      break;
    default:
      break;
  }

  return parent_class->change_state (element, transition);
}
