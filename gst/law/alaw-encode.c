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
#include "alaw-encode.h"

extern GstPadTemplate *alawenc_src_template, *alawenc_sink_template;

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

static void gst_alawenc_class_init (GstALawEncClass * klass);
static void gst_alawenc_base_init (GstALawEncClass * klass);
static void gst_alawenc_init (GstALawEnc * alawenc);

static void gst_alawenc_chain (GstPad * pad, GstData * _data);

/*
 * s16_to_alaw() - Convert a 16-bit linear PCM value to 8-bit A-law
 *
 * s16_to_alaw() accepts an 16-bit integer and encodes it as A-law data.
 *
 *		Linear Input Code	Compressed Code
 *	------------------------	---------------
 *	0000000wxyza			000wxyz
 *	0000001wxyza			001wxyz
 *	000001wxyzab			010wxyz
 *	00001wxyzabc			011wxyz
 *	0001wxyzabcd			100wxyz
 *	001wxyzabcde			101wxyz
 *	01wxyzabcdef			110wxyz
 *	1wxyzabcdefg			111wxyz
 *
 * For further information see John C. Bellamy's Digital Telephony, 1982,
 * John Wiley & Sons, pps 98-111 and 472-476.
 */

static inline gint
val_seg (gint val)
{
  gint r = 1;

  val >>= 8;
  if (val & 0xf0) {
    val >>= 4;
    r += 4;
  }
  if (val & 0x0c) {
    val >>= 2;
    r += 2;
  }
  if (val & 0x02)
    r += 1;
  return r;
}

static guint8
s16_to_alaw (gint pcm_val)
{
  gint seg;
  guint8 mask;
  guint8 aval;

  if (pcm_val >= 0) {
    mask = 0xD5;
  } else {
    mask = 0x55;
    pcm_val = -pcm_val;
    if (pcm_val > 0x7fff)
      pcm_val = 0x7fff;
  }

  if (pcm_val < 256)
    aval = pcm_val >> 4;
  else {
    /* Convert the scaled magnitude to segment number. */
    seg = val_seg (pcm_val);
    aval = (seg << 4) | ((pcm_val >> (seg + 3)) & 0x0f);
  }
  return aval ^ mask;
}

static GstElementClass *parent_class = NULL;

/*static guint gst_stereo_signals[LAST_SIGNAL] = { 0 }; */

static GstCaps *
alawenc_getcaps (GstPad * pad)
{
  GstALawEnc *alawenc = GST_ALAWENC (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstCaps *base_caps, *othercaps;
  GstStructure *structure;
  const GValue *rate, *chans;

  if (pad == alawenc->srcpad) {
    otherpad = alawenc->sinkpad;
    base_caps = gst_caps_new_simple ("audio/x-alaw", NULL);
  } else {
    otherpad = alawenc->srcpad;
    base_caps = gst_caps_new_simple ("audio/x-raw-int",
        "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16,
        "endianness", G_TYPE_INT, G_BYTE_ORDER, NULL);
  }
  othercaps = gst_pad_get_allowed_caps (otherpad);

  /* Not fully correct, but usually, all structures in a caps have
   * the same samplerate and channels range. */
  structure = gst_caps_get_structure (othercaps, 0);
  rate = gst_structure_get_value (structure, "rate");
  chans = gst_structure_get_value (structure, "channels");
  if (!rate || !chans)
    return gst_caps_new_empty ();

  /* Set the samplerate/channels on the to-be-returned caps */
  structure = gst_caps_get_structure (base_caps, 0);
  gst_structure_set_value (structure, "rate", rate);
  gst_structure_set_value (structure, "channels", chans);
  gst_caps_free (othercaps);

  return base_caps;
}

static GstPadLinkReturn
alawenc_link (GstPad * pad, const GstCaps * caps)
{
  GstALawEnc *alawenc = GST_ALAWENC (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstStructure *structure;
  const GValue *rate, *chans;
  GstCaps *base_caps;

  structure = gst_caps_get_structure (caps, 0);
  rate = gst_structure_get_value (structure, "rate");
  chans = gst_structure_get_value (structure, "channels");
  if (!rate || !chans)
    return GST_PAD_LINK_REFUSED;

  if (pad == alawenc->sinkpad) {
    otherpad = alawenc->srcpad;
    base_caps = gst_caps_new_simple ("audio/x-alaw", NULL);
  } else {
    otherpad = alawenc->sinkpad;
    base_caps = gst_caps_new_simple ("audio/x-raw-int",
        "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16,
        "endianness", G_TYPE_INT, G_BYTE_ORDER, NULL);
  }

  structure = gst_caps_get_structure (base_caps, 0);
  gst_structure_set_value (structure, "rate", rate);
  gst_structure_set_value (structure, "channels", chans);

  return gst_pad_try_set_caps (otherpad, base_caps);
}

GType
gst_alawenc_get_type (void)
{
  static GType alawenc_type = 0;

  if (!alawenc_type) {
    static const GTypeInfo alawenc_info = {
      sizeof (GstALawEncClass),
      (GBaseInitFunc) gst_alawenc_base_init,
      NULL,
      (GClassInitFunc) gst_alawenc_class_init,
      NULL,
      NULL,
      sizeof (GstALawEnc),
      0,
      (GInstanceInitFunc) gst_alawenc_init,
    };

    alawenc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstALawEnc", &alawenc_info,
        0);
  }
  return alawenc_type;
}

static void
gst_alawenc_base_init (GstALawEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstElementDetails alawenc_details = {
    "PCM to A Law conversion",
    "Codec/Encoder/Audio",
    "Convert 16bit PCM to 8bit A law",
    "Zaheer Merali <zaheer@bellworldwide.net>"
  };

  gst_element_class_add_pad_template (element_class, alawenc_src_template);
  gst_element_class_add_pad_template (element_class, alawenc_sink_template);
  gst_element_class_set_details (element_class, &alawenc_details);
}

static void
gst_alawenc_class_init (GstALawEncClass * klass)
{
  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

static void
gst_alawenc_init (GstALawEnc * alawenc)
{
  alawenc->sinkpad = gst_pad_new_from_template (alawenc_sink_template, "sink");
  gst_pad_set_link_function (alawenc->sinkpad, alawenc_link);
  gst_pad_set_getcaps_function (alawenc->sinkpad, alawenc_getcaps);
  gst_pad_set_chain_function (alawenc->sinkpad, gst_alawenc_chain);
  gst_element_add_pad (GST_ELEMENT (alawenc), alawenc->sinkpad);

  alawenc->srcpad = gst_pad_new_from_template (alawenc_src_template, "src");
  gst_pad_set_link_function (alawenc->srcpad, alawenc_link);
  gst_pad_set_getcaps_function (alawenc->srcpad, alawenc_getcaps);
  gst_element_add_pad (GST_ELEMENT (alawenc), alawenc->srcpad);
}

static void
gst_alawenc_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstALawEnc *alawenc;
  gint16 *linear_data;
  guint8 *alaw_data;
  GstBuffer *outbuf;
  gint i;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  alawenc = GST_ALAWENC (GST_OBJECT_PARENT (pad));
  g_return_if_fail (alawenc != NULL);
  g_return_if_fail (GST_IS_ALAWENC (alawenc));

  linear_data = (gint16 *) GST_BUFFER_DATA (buf);
  outbuf = gst_buffer_new_and_alloc (GST_BUFFER_SIZE (buf) / 2);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);
  alaw_data = (guint8 *) GST_BUFFER_DATA (outbuf);

  for (i = 0; i < GST_BUFFER_SIZE (outbuf); i++) {
    *alaw_data = s16_to_alaw (*linear_data);
    alaw_data++;
    linear_data++;
  }

  gst_buffer_unref (buf);
  gst_pad_push (alawenc->srcpad, GST_DATA (outbuf));
}
