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
#include <stdlib.h>
#include <string.h>

#include "gsttarkinenc.h"

static GstPadTemplate *enc_src_template, *enc_sink_template;

/* elementfactory information */
GstElementDetails tarkinenc_details = {
  "Ogg Tarkin encoder",
  "Filter/Video/Encoder",
  "Encodes video in OGG Tarkin format",
  "Monty <monty@xiph.org>, " "Wim Taymans <wim.taymans@chello.be>",
};

/* TarkinEnc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_BITRATE,
  ARG_S_MOMENTS,
  ARG_A_MOMENTS,
};

static void gst_tarkinenc_base_init (gpointer g_class);
static void gst_tarkinenc_class_init (TarkinEncClass * klass);
static void gst_tarkinenc_init (TarkinEnc * arkinenc);

static void gst_tarkinenc_chain (GstPad * pad, GstData * _data);
static void gst_tarkinenc_setup (TarkinEnc * tarkinenc);

static void gst_tarkinenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_tarkinenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_tarkinenc_signals[LAST_SIGNAL] = { 0 }; */

GType
tarkinenc_get_type (void)
{
  static GType tarkinenc_type = 0;

  if (!tarkinenc_type) {
    static const GTypeInfo tarkinenc_info = {
      sizeof (TarkinEncClass),
      gst_tarkinenc_base_init,
      NULL,
      (GClassInitFunc) gst_tarkinenc_class_init,
      NULL,
      NULL,
      sizeof (TarkinEnc),
      0,
      (GInstanceInitFunc) gst_tarkinenc_init,
    };

    tarkinenc_type =
	g_type_register_static (GST_TYPE_ELEMENT, "TarkinEnc", &tarkinenc_info,
	0);
  }
  return tarkinenc_type;
}

static GstCaps *
tarkin_caps_factory (void)
{
  return gst_caps_new ("tarkin_tarkin", "application/ogg", NULL);
}

static GstCaps *
raw_caps_factory (void)
{
  return
      GST_CAPS_NEW ("tarkin_raw",
      "video/x-raw-rgb",
      "bpp", GST_PROPS_INT (24),
      "depth", GST_PROPS_INT (24),
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "red_mask", GST_PROPS_INT (0xff0000),
      "green_mask", GST_PROPS_INT (0xff00),
      "blue_mask", GST_PROPS_INT (0xff),
      "width", GST_PROPS_INT_RANGE (0, G_MAXINT),
      "height", GST_PROPS_INT_RANGE (0, G_MAXINT),
      "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT)
      );
}

static void
gst_tarkinenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *tarkin_caps;

  raw_caps = raw_caps_factory ();
  tarkin_caps = tarkin_caps_factory ();

  enc_sink_template = gst_pad_template_new ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS, raw_caps, NULL);
  enc_src_template = gst_pad_template_new ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS, tarkin_caps, NULL);
  gst_element_class_add_pad_template (element_class, enc_sink_template);
  gst_element_class_add_pad_template (element_class, enc_src_template);

  gst_element_class_set_details (element_class, &tarkinenc_details);
}

static void
gst_tarkinenc_class_init (TarkinEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE,
      g_param_spec_int ("bitrate", "bitrate", "bitrate",
	  G_MININT, G_MAXINT, 3000, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_S_MOMENTS,
      g_param_spec_int ("s_moments", "Synthesis Moments",
	  "Number of vanishing moments for the synthesis filter",
	  1, 4, 2, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_A_MOMENTS,
      g_param_spec_int ("a_moments", "Analysis Moments",
	  "Number of vanishing moments for the analysis filter",
	  1, 4, 2, G_PARAM_READWRITE));

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_tarkinenc_set_property;
  gobject_class->get_property = gst_tarkinenc_get_property;
}

static GstPadLinkReturn
gst_tarkinenc_sinkconnect (GstPad * pad, GstCaps * caps)
{
  TarkinEnc *tarkinenc;

  tarkinenc = GST_TARKINENC (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  gst_caps_debug (caps, "caps to be set on tarkin sink pad");

  tarkinenc->layer[0].bitstream_len = tarkinenc->bitrate;
  tarkinenc->layer[0].a_moments = tarkinenc->a_moments;
  tarkinenc->layer[0].s_moments = tarkinenc->s_moments;
  gst_caps_get_int (caps, "width", &tarkinenc->layer[0].width);
  gst_caps_get_int (caps, "height", &tarkinenc->layer[0].height);
  tarkinenc->layer[0].format = TARKIN_RGB24;
  tarkinenc->layer[0].frames_per_buf = TARKIN_RGB24;

  gst_tarkinenc_setup (tarkinenc);

  if (tarkinenc->setup)
    return GST_PAD_LINK_OK;

  return GST_PAD_LINK_REFUSED;
}

static void
gst_tarkinenc_init (TarkinEnc * tarkinenc)
{
  tarkinenc->sinkpad = gst_pad_new_from_template (enc_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (tarkinenc), tarkinenc->sinkpad);
  gst_pad_set_chain_function (tarkinenc->sinkpad, gst_tarkinenc_chain);
  gst_pad_set_link_function (tarkinenc->sinkpad, gst_tarkinenc_sinkconnect);

  tarkinenc->srcpad = gst_pad_new_from_template (enc_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (tarkinenc), tarkinenc->srcpad);

  tarkinenc->bitrate = 3000;
  tarkinenc->s_moments = 2;
  tarkinenc->a_moments = 2;
  tarkinenc->setup = FALSE;
}

TarkinError
free_frame (void *s, void *ptr)
{
  return (TARKIN_OK);
}

TarkinError
packet_out (void *stream, ogg_packet * op)
{
  ogg_page og;
  TarkinStream *s = stream;
  TarkinEnc *te = s->user_ptr;
  GstBuffer *outbuf;

  ogg_stream_packetin (&te->os, op);

  if (op->e_o_s) {
    ogg_stream_flush (&te->os, &og);
    outbuf = gst_buffer_new ();
    GST_BUFFER_DATA (outbuf) = og.header;
    GST_BUFFER_SIZE (outbuf) = og.header_len;
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_DONTFREE);
    gst_pad_push (te->srcpad, GST_DATA (outbuf));
    outbuf = gst_buffer_new ();
    GST_BUFFER_DATA (outbuf) = og.body;
    GST_BUFFER_SIZE (outbuf) = og.body_len;
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_DONTFREE);
    gst_pad_push (te->srcpad, GST_DATA (outbuf));
  } else {
    while (ogg_stream_pageout (&te->os, &og)) {
      outbuf = gst_buffer_new ();
      GST_BUFFER_DATA (outbuf) = og.header;
      GST_BUFFER_SIZE (outbuf) = og.header_len;
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_DONTFREE);
      gst_pad_push (te->srcpad, GST_DATA (outbuf));
      outbuf = gst_buffer_new ();
      GST_BUFFER_DATA (outbuf) = og.body;
      GST_BUFFER_SIZE (outbuf) = og.body_len;
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_DONTFREE);
      gst_pad_push (te->srcpad, GST_DATA (outbuf));
    }
  }
  return (TARKIN_OK);
}


static void
gst_tarkinenc_setup (TarkinEnc * tarkinenc)
{
  gint i;
  GstBuffer *outbuf;

  ogg_stream_init (&tarkinenc->os, 1);
  tarkin_info_init (&tarkinenc->ti);

  tarkinenc->ti.inter.numerator = 1;
  tarkinenc->ti.inter.denominator = 1;

  tarkin_comment_init (&tarkinenc->tc);
  tarkin_comment_add_tag (&tarkinenc->tc, "TITLE", "GStreamer produced file");
  tarkin_comment_add_tag (&tarkinenc->tc, "ARTIST", "C coders ;)");

  tarkinenc->tarkin_stream = tarkin_stream_new ();
  tarkin_analysis_init (tarkinenc->tarkin_stream,
      &tarkinenc->ti, free_frame, packet_out, (void *) tarkinenc);
  tarkin_analysis_add_layer (tarkinenc->tarkin_stream, &tarkinenc->layer[0]);

  tarkin_analysis_headerout (tarkinenc->tarkin_stream, &tarkinenc->tc,
      tarkinenc->op, &tarkinenc->op[1], &tarkinenc->op[2]);
  for (i = 0; i < 3; i++) {
    ogg_stream_packetin (&tarkinenc->os, &tarkinenc->op[i]);
  }

  ogg_stream_flush (&tarkinenc->os, &tarkinenc->og);

  tarkinenc->frame_num = 0;

  outbuf = gst_buffer_new ();
  GST_BUFFER_DATA (outbuf) = tarkinenc->og.header;
  GST_BUFFER_SIZE (outbuf) = tarkinenc->og.header_len;
  GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_DONTFREE);
  gst_pad_push (tarkinenc->srcpad, GST_DATA (outbuf));

  outbuf = gst_buffer_new ();
  GST_BUFFER_DATA (outbuf) = tarkinenc->og.body;
  GST_BUFFER_SIZE (outbuf) = tarkinenc->og.body_len;
  GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_DONTFREE);
  gst_pad_push (tarkinenc->srcpad, GST_DATA (outbuf));

  tarkinenc->setup = TRUE;
}

static void
gst_tarkinenc_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  TarkinEnc *tarkinenc;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  tarkinenc = GST_TARKINENC (gst_pad_get_parent (pad));

  if (!tarkinenc->setup) {
    GST_ELEMENT_ERROR (tarkinenc, CORE, NEGOTIATION, (NULL),
	("encoder not initialized (input is not tarkin?)"));
    if (GST_IS_BUFFER (buf))
      gst_buffer_unref (buf);
    else
      gst_pad_event_default (pad, GST_EVENT (buf));
    return;
  }

  if (GST_IS_EVENT (buf)) {
    switch (GST_EVENT_TYPE (buf)) {
      case GST_EVENT_EOS:
	tarkin_analysis_framein (tarkinenc->tarkin_stream, NULL, 0, NULL);	/* EOS */
	tarkin_comment_clear (&tarkinenc->tc);
	tarkin_stream_destroy (tarkinenc->tarkin_stream);
      default:
	gst_pad_event_default (pad, GST_EVENT (buf));
	break;
    }
  } else {
    gchar *data;
    gulong size;
    TarkinTime date;

    /* data to encode */
    data = GST_BUFFER_DATA (buf);
    size = GST_BUFFER_SIZE (buf);

    date.numerator = tarkinenc->frame_num;
    date.denominator = 1;
    tarkin_analysis_framein (tarkinenc->tarkin_stream, data, 0, &date);
    tarkinenc->frame_num++;

    gst_buffer_unref (buf);
  }
}

static void
gst_tarkinenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  TarkinEnc *tarkinenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TARKINENC (object));

  tarkinenc = GST_TARKINENC (object);

  switch (prop_id) {
    case ARG_BITRATE:
      g_value_set_int (value, tarkinenc->bitrate);
      break;
    case ARG_S_MOMENTS:
      g_value_set_int (value, tarkinenc->s_moments);
      break;
    case ARG_A_MOMENTS:
      g_value_set_int (value, tarkinenc->a_moments);
      break;
    default:
      break;
  }
}

static void
gst_tarkinenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  TarkinEnc *tarkinenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TARKINENC (object));

  tarkinenc = GST_TARKINENC (object);

  switch (prop_id) {
    case ARG_BITRATE:
      tarkinenc->bitrate = g_value_get_int (value);
      break;
    case ARG_S_MOMENTS:
    {
      gint s_moments;

      s_moments = g_value_get_int (value);
      if (s_moments != 1 || s_moments != 2 || s_moments != 4) {
	g_warning ("tarkinenc: s_moments must be 1, 2 or 4");
      } else {
	tarkinenc->s_moments = s_moments;
      }
      break;
    }
    case ARG_A_MOMENTS:
    {
      gint a_moments;

      a_moments = g_value_get_int (value);
      if (a_moments != 1 || a_moments != 2 || a_moments != 4) {
	g_warning ("tarkinenc: a_moments must be 1, 2 or 4");
      } else {
	tarkinenc->a_moments = a_moments;
      }
      break;
    }
    default:
      break;
  }
}
