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
#include <gst/gst.h>
#include "mulaw-encode.h"
#include "mulaw-conversion.h"

extern GstPadTemplate *mulawenc_src_template, *mulawenc_sink_template;

/* elementfactory information */
static GstElementDetails mulawenc_details = {
  "PCM to Mu Law conversion",
  "Codec/Encoder/Audio",
  "Convert 16bit PCM to 8bit mu law",
  "Zaheer Merali <zaheer@bellworldwide.net>"
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

static void gst_mulawenc_class_init (GstMuLawEncClass * klass);
static void gst_mulawenc_base_init (GstMuLawEncClass * klass);
static void gst_mulawenc_init (GstMuLawEnc * mulawenc);

static void gst_mulawenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mulawenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_mulawenc_chain (GstPad * pad, GstData * _data);


static GstElementClass *parent_class = NULL;

/*static guint gst_stereo_signals[LAST_SIGNAL] = { 0 }; */

static GstPadLinkReturn
mulawenc_link (GstPad * pad, const GstCaps * caps)
{
  GstCaps *tempcaps;
  gint rate, channels;
  GstStructure *structure;
  gboolean ret;

  GstMuLawEnc *mulawenc = GST_MULAWENC (GST_OBJECT_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "rate", &rate);
  ret = gst_structure_get_int (structure, "channels", &channels);
  if (!ret)
    return GST_PAD_LINK_REFUSED;

  tempcaps = gst_caps_new_simple ("audio/x-mulaw",
      "depth", G_TYPE_INT, 8,
      "width", G_TYPE_INT, 8,
      "signed", G_TYPE_BOOLEAN, FALSE,
      "rate", G_TYPE_INT, rate, "channels", G_TYPE_INT, channels, NULL);

  return gst_pad_try_set_caps (mulawenc->srcpad, tempcaps);
}

GType
gst_mulawenc_get_type (void)
{
  static GType mulawenc_type = 0;

  if (!mulawenc_type) {
    static const GTypeInfo mulawenc_info = {
      sizeof (GstMuLawEncClass),
      (GBaseInitFunc) gst_mulawenc_base_init,
      NULL,
      (GClassInitFunc) gst_mulawenc_class_init,
      NULL,
      NULL,
      sizeof (GstMuLawEnc),
      0,
      (GInstanceInitFunc) gst_mulawenc_init,
    };

    mulawenc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstMuLawEnc", &mulawenc_info,
        0);
  }
  return mulawenc_type;
}

static void
gst_mulawenc_base_init (GstMuLawEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class, mulawenc_src_template);
  gst_element_class_add_pad_template (element_class, mulawenc_sink_template);
  gst_element_class_set_details (element_class, &mulawenc_details);
}

static void
gst_mulawenc_class_init (GstMuLawEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_mulawenc_set_property;
  gobject_class->get_property = gst_mulawenc_get_property;
}

static void
gst_mulawenc_init (GstMuLawEnc * mulawenc)
{
  mulawenc->sinkpad =
      gst_pad_new_from_template (mulawenc_sink_template, "sink");
  mulawenc->srcpad = gst_pad_new_from_template (mulawenc_src_template, "src");
  gst_pad_set_link_function (mulawenc->sinkpad, mulawenc_link);

  gst_element_add_pad (GST_ELEMENT (mulawenc), mulawenc->sinkpad);
  gst_pad_set_chain_function (mulawenc->sinkpad, gst_mulawenc_chain);
  gst_element_add_pad (GST_ELEMENT (mulawenc), mulawenc->srcpad);
}

static void
gst_mulawenc_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstMuLawEnc *mulawenc;
  gint16 *linear_data;
  guint8 *mulaw_data;
  GstBuffer *outbuf;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  mulawenc = GST_MULAWENC (GST_OBJECT_PARENT (pad));
  g_return_if_fail (mulawenc != NULL);
  g_return_if_fail (GST_IS_MULAWENC (mulawenc));

  linear_data = (gint16 *) GST_BUFFER_DATA (buf);
  outbuf = gst_buffer_new ();
  GST_BUFFER_DATA (outbuf) =
      (gchar *) g_new (gint16, GST_BUFFER_SIZE (buf) / 4);
  GST_BUFFER_SIZE (outbuf) = GST_BUFFER_SIZE (buf) / 2;

  mulaw_data = (gint8 *) GST_BUFFER_DATA (outbuf);
  mulaw_encode (linear_data, mulaw_data, GST_BUFFER_SIZE (outbuf));

  gst_buffer_unref (buf);
  gst_pad_push (mulawenc->srcpad, GST_DATA (outbuf));
}

static void
gst_mulawenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMuLawEnc *mulawenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MULAWENC (object));
  mulawenc = GST_MULAWENC (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_mulawenc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMuLawEnc *mulawenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MULAWENC (object));
  mulawenc = GST_MULAWENC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
