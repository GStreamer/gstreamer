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

/* elementfactory information */
static GstElementDetails alawenc_details = {
  "PCM to A Law conversion",
  "Codec/Encoder/Audio",
  "Convert 16bit PCM to 8bit A law",
  "Zaheer Merali <zaheer@bellworldwide.net>"
};

/* Stereo signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0
};

static void		gst_alawenc_class_init		(GstALawEncClass *klass);
static void		gst_alawenc_base_init		(GstALawEncClass *klass);
static void		gst_alawenc_init			(GstALawEnc *alawenc);

static void		gst_alawenc_set_property			(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		gst_alawenc_get_property			(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void		gst_alawenc_chain			(GstPad *pad, GstData *_data);

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

static inline gint val_seg(gint val)
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

static guint8 s16_to_alaw(gint pcm_val)
{
	gint		seg;
	guint8	mask;
	guint8	aval;

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
		seg = val_seg(pcm_val);
		aval = (seg << 4) | ((pcm_val >> (seg + 3)) & 0x0f);
	}
	return aval ^ mask;
}

static GstElementClass *parent_class = NULL;
/*static guint gst_stereo_signals[LAST_SIGNAL] = { 0 }; */

static GstPadLinkReturn
alawenc_link (GstPad *pad, const GstCaps *caps)
{
  GstCaps* tempcaps;
  gint rate, channels;
  GstStructure *structure;
  gboolean ret;
  
  GstALawEnc* alawenc = GST_ALAWENC (GST_OBJECT_PARENT (pad));
  
  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "rate", &rate);
  ret &= gst_structure_get_int (structure, "channels", &channels);

  if (!ret) return GST_PAD_LINK_REFUSED;
  
  tempcaps = gst_caps_new_simple ("audio/x-alaw",
      "depth",    G_TYPE_INT, 8,
      "width",    G_TYPE_INT, 8,
      "signed",   G_TYPE_BOOLEAN, FALSE,
      "rate",     G_TYPE_INT, rate,
      "channels", G_TYPE_INT, channels,
      NULL);
  
  return gst_pad_try_set_caps (alawenc->srcpad, tempcaps);
}		

GType
gst_alawenc_get_type(void) {
  static GType alawenc_type = 0;

  if (!alawenc_type) {
    static const GTypeInfo alawenc_info = {
      sizeof(GstALawEncClass),
      (GBaseInitFunc)gst_alawenc_base_init,
      NULL,
      (GClassInitFunc)gst_alawenc_class_init,
      NULL,
      NULL,
      sizeof(GstALawEnc),
      0,
      (GInstanceInitFunc)gst_alawenc_init,
    };
    alawenc_type = g_type_register_static(GST_TYPE_ELEMENT, "GstALawEnc", &alawenc_info, 0);
  }
  return alawenc_type;
}

static void
gst_alawenc_base_init (GstALawEncClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class, alawenc_src_template);
  gst_element_class_add_pad_template (element_class, alawenc_sink_template);
  gst_element_class_set_details (element_class, &alawenc_details);
}

static void
gst_alawenc_class_init (GstALawEncClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_alawenc_set_property;
  gobject_class->get_property = gst_alawenc_get_property;
}

static void
gst_alawenc_init (GstALawEnc *alawenc)
{
  alawenc->sinkpad = gst_pad_new_from_template(alawenc_sink_template,"sink");
  alawenc->srcpad = gst_pad_new_from_template(alawenc_src_template,"src");
  gst_pad_set_link_function (alawenc->sinkpad, alawenc_link);

  gst_element_add_pad(GST_ELEMENT(alawenc),alawenc->sinkpad);
  gst_pad_set_chain_function(alawenc->sinkpad,gst_alawenc_chain);
  gst_element_add_pad(GST_ELEMENT(alawenc),alawenc->srcpad);
}

static void
gst_alawenc_chain (GstPad *pad,GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstALawEnc *alawenc;
  gint16 *linear_data;
  guint8 *alaw_data;
  GstBuffer* outbuf;
  gint i;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  alawenc = GST_ALAWENC(GST_OBJECT_PARENT (pad));
  g_return_if_fail(alawenc != NULL);
  g_return_if_fail(GST_IS_ALAWENC(alawenc));

  linear_data = (gint16 *)GST_BUFFER_DATA(buf);
  outbuf=gst_buffer_new();
  GST_BUFFER_DATA(outbuf) = (gchar*)g_new(guint8,GST_BUFFER_SIZE(buf)/2);
  GST_BUFFER_SIZE(outbuf) = GST_BUFFER_SIZE(buf)/2;

  
  alaw_data = (guint8*)GST_BUFFER_DATA(outbuf);
  for (i = 0; i < GST_BUFFER_SIZE(outbuf); i++) {
    *alaw_data = s16_to_alaw (*linear_data);
    alaw_data++;
    linear_data++;
  }
  gst_buffer_unref(buf);
  gst_pad_push(alawenc->srcpad,GST_DATA (outbuf));
}

static void
gst_alawenc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstALawEnc *alawenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_ALAWENC(object));
  alawenc = GST_ALAWENC(object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_alawenc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstALawEnc *alawenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_ALAWENC(object));
  alawenc = GST_ALAWENC(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
