/* Gnome-Streamer
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

#include <gst/gst.h>
#include "alaw-encode.h"
#include "mulaw-conversion.h"
#include "alaw-conversion.h"

extern GstPadTemplate *alawenc_src_template, *alawenc_sink_template;


/* Stereo signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0
};

static void		gst_alawenc_class_init		(GstALawEncClass *klass);
static void		gst_alawenc_init			(GstALawEnc *alawenc);

static void		gst_alawenc_set_property			(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		gst_alawenc_get_property			(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void		gst_alawenc_chain			(GstPad *pad, GstBuffer *buf);


static GstElementClass *parent_class = NULL;
//static guint gst_stereo_signals[LAST_SIGNAL] = { 0 };

/*
static GstPadNegotiateReturn
alawenc_negotiate_sink (GstPad *pad, GstCaps **caps, gint counter)
{
  GstCaps* tempcaps;
  
  GstALawEnc* alawenc=GST_ALAWENC (GST_OBJECT_PARENT (pad));
  
  if (*caps==NULL) 
    return GST_PAD_NEGOTIATE_FAIL;

  tempcaps = gst_caps_copy(*caps);

  gst_caps_set(tempcaps,"format",GST_PROPS_STRING("int"));
  gst_caps_set(tempcaps,"law",GST_PROPS_INT(2));
  gst_caps_set(tempcaps,"depth",GST_PROPS_INT(8));
  gst_caps_set(tempcaps,"width",GST_PROPS_INT(8));
  gst_caps_set(tempcaps,"signed",GST_PROPS_BOOLEAN(FALSE));

  if (gst_pad_try_set_caps (alawenc->srcpad, tempcaps))
  {
    return GST_PAD_NEGOTIATE_AGREE;
  }
  else {
    gst_caps_unref (tempcaps);
    return GST_PAD_NEGOTIATE_FAIL;
  }
}		
*/

GType
gst_alawenc_get_type(void) {
  static GType alawenc_type = 0;

  if (!alawenc_type) {
    static const GTypeInfo alawenc_info = {
      sizeof(GstALawEncClass),      NULL,
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
  //gst_pad_set_negotiate_function(alawenc->sinkpad, alawenc_negotiate_sink);

  gst_element_add_pad(GST_ELEMENT(alawenc),alawenc->sinkpad);
  gst_pad_set_chain_function(alawenc->sinkpad,gst_alawenc_chain);
  gst_element_add_pad(GST_ELEMENT(alawenc),alawenc->srcpad);
}

static void
gst_alawenc_chain (GstPad *pad,GstBuffer *buf)
{
  GstALawEnc *alawenc;
  gint16 *linear_data;
  guint8 *alaw_data;
  GstBuffer* outbuf;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  alawenc = GST_ALAWENC(GST_OBJECT_PARENT (pad));
  g_return_if_fail(alawenc != NULL);
  g_return_if_fail(GST_IS_ALAWENC(alawenc));

  linear_data = (gint16 *)GST_BUFFER_DATA(buf);
  outbuf=gst_buffer_new();
  GST_BUFFER_DATA(outbuf) = (gchar*)g_new(gint16,GST_BUFFER_SIZE(buf)/4);
  GST_BUFFER_SIZE(outbuf) = GST_BUFFER_SIZE(buf)/2;

  alaw_data = (guint8*)GST_BUFFER_DATA(outbuf);
  mulaw_encode(linear_data,alaw_data,GST_BUFFER_SIZE(outbuf));
  isdn_audio_ulaw2alaw(alaw_data,GST_BUFFER_SIZE(outbuf));
  gst_buffer_unref(buf);
  gst_pad_push(alawenc->srcpad,outbuf);
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
