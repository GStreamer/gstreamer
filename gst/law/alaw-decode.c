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
#include "alaw-decode.h"
#include "mulaw-conversion.h"
#include "alaw-conversion.h"

extern GstPadTemplate *alawdec_src_template, *alawdec_sink_template;


/* Stereo signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0
};

static void		gst_alawdec_class_init		(GstALawDecClass *klass);
static void		gst_alawdec_init			(GstALawDec *alawdec);

static void		gst_alawdec_set_property			(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		gst_alawdec_get_property			(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void		gst_alawdec_chain			(GstPad *pad, GstBuffer *buf);


static GstElementClass *parent_class = NULL;
//static guint gst_stereo_signals[LAST_SIGNAL] = { 0 };

static GstPadNegotiateReturn
alawdec_negotiate_sink (GstPad *pad, GstCaps **caps, gint counter)
{
  GstCaps* tempcaps;
  
  GstALawDec* alawdec=GST_ALAWDEC (GST_OBJECT_PARENT (pad));
  
  if (*caps==NULL) 
    return GST_PAD_NEGOTIATE_FAIL;

  tempcaps = gst_caps_copy(*caps);

  gst_caps_set(tempcaps,"format",GST_PROPS_STRING("int"));
  gst_caps_set(tempcaps,"law",GST_PROPS_INT(0));
  gst_caps_set(tempcaps,"depth",GST_PROPS_INT(16));
  gst_caps_set(tempcaps,"width",GST_PROPS_INT(16));
  gst_caps_set(tempcaps,"signed",GST_PROPS_BOOLEAN(TRUE));

  if (gst_pad_set_caps (alawdec->srcpad, tempcaps))
  {
    return GST_PAD_NEGOTIATE_AGREE;
  }
  else {
    gst_caps_unref (tempcaps);
    return GST_PAD_NEGOTIATE_FAIL;
  }
}		

GType
gst_alawdec_get_type(void) {
  static GType alawdec_type = 0;

  if (!alawdec_type) {
    static const GTypeInfo alawdec_info = {
      sizeof(GstALawDecClass),      NULL,
      NULL,
      (GClassInitFunc)gst_alawdec_class_init,
      NULL,
      NULL,
      sizeof(GstALawDec),
      0,
      (GInstanceInitFunc)gst_alawdec_init,
    };
    alawdec_type = g_type_register_static(GST_TYPE_ELEMENT, "GstALawDec", &alawdec_info, 0);
  }
  return alawdec_type;
}

static void
gst_alawdec_class_init (GstALawDecClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_alawdec_set_property;
  gobject_class->get_property = gst_alawdec_get_property;
}

static void
gst_alawdec_init (GstALawDec *alawdec)
{
  alawdec->sinkpad = gst_pad_new_from_template(alawdec_sink_template,"sink");
  alawdec->srcpad = gst_pad_new_from_template(alawdec_src_template,"src");
  gst_pad_set_negotiate_function(alawdec->sinkpad, alawdec_negotiate_sink);

  gst_element_add_pad(GST_ELEMENT(alawdec),alawdec->sinkpad);
  gst_pad_set_chain_function(alawdec->sinkpad,gst_alawdec_chain);
  gst_element_add_pad(GST_ELEMENT(alawdec),alawdec->srcpad);
}

static void
gst_alawdec_chain (GstPad *pad,GstBuffer *buf)
{
  GstALawDec *alawdec;
  gint16 *linear_data;
  guint8 *alaw_data;
  GstBuffer* outbuf;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  alawdec = GST_ALAWDEC(GST_OBJECT_PARENT (pad));
  g_return_if_fail(alawdec != NULL);
  g_return_if_fail(GST_IS_ALAWDEC(alawdec));

  alaw_data = (guint8 *)GST_BUFFER_DATA(buf);
  outbuf=gst_buffer_new();
  GST_BUFFER_DATA(outbuf) = (gchar*)g_new(gint16,GST_BUFFER_SIZE(buf));
  GST_BUFFER_SIZE(outbuf) = GST_BUFFER_SIZE(buf)*2;

  linear_data = (gint16*)GST_BUFFER_DATA(outbuf);

  isdn_audio_alaw2ulaw(alaw_data,GST_BUFFER_SIZE(buf));
  mulaw_decode(alaw_data,linear_data,GST_BUFFER_SIZE(buf));
  
  gst_buffer_unref(buf);
  gst_pad_push(alawdec->srcpad,outbuf);
}

static void
gst_alawdec_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstALawDec *alawdec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_ALAWDEC(object));
  alawdec = GST_ALAWDEC(object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_alawdec_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstALawDec *alawdec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_ALAWDEC(object));
  alawdec = GST_ALAWDEC(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


