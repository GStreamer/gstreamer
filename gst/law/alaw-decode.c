/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * PCM - A-Law conversion
 *   Copyright (C) 2000 by Abramo Bagnara <abramo@alsa-project.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "alaw-decode.h"

extern GstPadTemplate *alawdec_src_template, *alawdec_sink_template;

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

static void gst_alawdec_class_init (GstALawDecClass * klass);
static void gst_alawdec_base_init (GstALawDecClass * klass);
static void gst_alawdec_init (GstALawDec * alawdec);

static GstFlowReturn gst_alawdec_chain (GstPad * pad, GstBuffer * buffer);

static GstElementClass *parent_class = NULL;

/*static guint gst_stereo_signals[LAST_SIGNAL] = { 0 }; */

/*
 * alaw_to_s16() - Convert an A-law value to 16-bit linear PCM
 *
 */
static gint
alaw_to_s16 (guint8 a_val)
{
  gint t;
  gint seg;

  a_val ^= 0x55;
  t = a_val & 0x7f;
  if (t < 16)
    t = (t << 4) + 8;
  else {
    seg = (t >> 4) & 0x07;
    t = ((t & 0x0f) << 4) + 0x108;
    t <<= seg - 1;
  }
  return ((a_val & 0x80) ? t : -t);
}

static GstCaps *
alawdec_getcaps (GstPad * pad)
{
  GstALawDec *alawdec;
  GstPad *otherpad;
  GstCaps *base_caps, *othercaps;

  alawdec = GST_ALAWDEC (GST_PAD_PARENT (pad));

  /*  we can do what our template says */
  base_caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  if (pad == alawdec->sinkpad) {
    otherpad = alawdec->srcpad;
  } else {
    otherpad = alawdec->sinkpad;
  }
  /* now intersect rate and channels from peer caps */
  othercaps = gst_pad_peer_get_caps (otherpad);
  if (othercaps) {
    GstStructure *structure;
    const GValue *orate, *ochans;
    const GValue *rate, *chans;
    GValue irate = { 0 }, ichans = {
    0};

    structure = gst_caps_get_structure (othercaps, 0);
    orate = gst_structure_get_value (structure, "rate");
    ochans = gst_structure_get_value (structure, "channels");
    if (!orate || !ochans)
      goto done;

    structure = gst_caps_get_structure (base_caps, 0);
    rate = gst_structure_get_value (structure, "rate");
    chans = gst_structure_get_value (structure, "channels");
    if (!rate || !chans)
      goto done;

    gst_value_intersect (&irate, orate, rate);
    gst_value_intersect (&ichans, ochans, chans);

    /* Set the samplerate/channels on the to-be-returned caps */
    structure = gst_caps_get_structure (base_caps, 0);
    gst_structure_set_value (structure, "rate", &irate);
    gst_structure_set_value (structure, "channels", &ichans);

    gst_caps_unref (othercaps);
  }

done:
  return base_caps;
}

static gboolean
alawdec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstALawDec *alawdec;
  GstPad *otherpad;
  GstStructure *structure;
  const GValue *rate, *chans;
  GstCaps *base_caps;

  alawdec = GST_ALAWDEC (GST_PAD_PARENT (pad));

  /* take rate and channels */
  structure = gst_caps_get_structure (caps, 0);
  rate = gst_structure_get_value (structure, "rate");
  chans = gst_structure_get_value (structure, "channels");
  if (!rate || !chans)
    return FALSE;

  if (pad == alawdec->srcpad) {
    otherpad = alawdec->sinkpad;
  } else {
    otherpad = alawdec->srcpad;
  }

  /* fill in values for otherpad */
  base_caps = gst_caps_copy (gst_pad_get_pad_template_caps (otherpad));
  structure = gst_caps_get_structure (base_caps, 0);
  gst_structure_set_value (structure, "rate", rate);
  gst_structure_set_value (structure, "channels", chans);

  /* and set on otherpad */
  gst_pad_set_caps (otherpad, base_caps);

  gst_caps_unref (base_caps);

  return TRUE;
}

GType
gst_alawdec_get_type (void)
{
  static GType alawdec_type = 0;

  if (!alawdec_type) {
    static const GTypeInfo alawdec_info = {
      sizeof (GstALawDecClass),
      (GBaseInitFunc) gst_alawdec_base_init,
      NULL,
      (GClassInitFunc) gst_alawdec_class_init,
      NULL,
      NULL,
      sizeof (GstALawDec),
      0,
      (GInstanceInitFunc) gst_alawdec_init,
    };

    alawdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstALawDec", &alawdec_info,
        0);
  }
  return alawdec_type;
}

static void
gst_alawdec_base_init (GstALawDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  const GstElementDetails alawdec_details =
      GST_ELEMENT_DETAILS ("A Law audio decoder",
      "Codec/Decoder/Audio",
      "Convert 8bit A law to 16bit PCM",
      "Zaheer Abbas Merali <zaheerabbas at merali dot org>");

  gst_element_class_add_pad_template (element_class, alawdec_src_template);
  gst_element_class_add_pad_template (element_class, alawdec_sink_template);
  gst_element_class_set_details (element_class, &alawdec_details);
}

static void
gst_alawdec_class_init (GstALawDecClass * klass)
{
  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_alawdec_init (GstALawDec * alawdec)
{
  alawdec->sinkpad = gst_pad_new_from_template (alawdec_sink_template, "sink");
  gst_pad_set_setcaps_function (alawdec->sinkpad, alawdec_setcaps);
  gst_pad_set_getcaps_function (alawdec->sinkpad, alawdec_getcaps);
  gst_pad_set_chain_function (alawdec->sinkpad, gst_alawdec_chain);
  gst_element_add_pad (GST_ELEMENT (alawdec), alawdec->sinkpad);

  alawdec->srcpad = gst_pad_new_from_template (alawdec_src_template, "src");
  gst_pad_set_setcaps_function (alawdec->srcpad, alawdec_setcaps);
  gst_pad_set_getcaps_function (alawdec->srcpad, alawdec_getcaps);
  gst_element_add_pad (GST_ELEMENT (alawdec), alawdec->srcpad);
}

static GstFlowReturn
gst_alawdec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstALawDec *alawdec;
  gint16 *linear_data;
  guint8 *alaw_data;
  GstBuffer *outbuf;
  gint i;

  alawdec = GST_ALAWDEC (GST_OBJECT_PARENT (pad));

  alaw_data = (guint8 *) GST_BUFFER_DATA (buffer);
  outbuf = gst_buffer_new_and_alloc (GST_BUFFER_SIZE (buffer) * 2);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buffer);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (alawdec->srcpad));
  linear_data = (gint16 *) GST_BUFFER_DATA (outbuf);

  for (i = 0; i < GST_BUFFER_SIZE (buffer); i++) {
    *linear_data = alaw_to_s16 (*alaw_data);
    linear_data++;
    alaw_data++;
  }

  gst_buffer_unref (buffer);
  return gst_pad_push (alawdec->srcpad, outbuf);
}
