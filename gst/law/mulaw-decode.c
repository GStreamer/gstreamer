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

#include <gst/gst.h>
#include "mulaw-decode.h"
#include "mulaw-conversion.h"

extern GstPadTemplate *mulawdec_src_template, *mulawdec_sink_template;


/* Stereo signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0
};

static void		gst_mulawdec_class_init		(GstMuLawDecClass *klass);
static void		gst_mulawdec_init			(GstMuLawDec *mulawdec);

static void		gst_mulawdec_set_property			(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		gst_mulawdec_get_property			(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void		gst_mulawdec_chain			(GstPad *pad, GstBuffer *buf);


static GstElementClass *parent_class = NULL;
/*static guint gst_stereo_signals[LAST_SIGNAL] = { 0 };*/

/*
static GstPadNegotiateReturn
mulawdec_negotiate_sink (GstPad *pad, GstCaps **caps, gint counter)
{
  GstCaps* tempcaps;
  
  GstMuLawDec* mulawdec=GST_MULAWDEC (GST_OBJECT_PARENT (pad));
  
  if (*caps==NULL) 
    return GST_PAD_NEGOTIATE_FAIL;

  tempcaps = gst_caps_copy(*caps);

  gst_caps_set(tempcaps,"format",GST_PROPS_STRING("int"));
  gst_caps_set(tempcaps,"law",GST_PROPS_INT(0));
  gst_caps_set(tempcaps,"depth",GST_PROPS_INT(16));
  gst_caps_set(tempcaps,"width",GST_PROPS_INT(16));
  gst_caps_set(tempcaps,"signed",GST_PROPS_BOOLEAN(TRUE));

  if (gst_pad_try_set_caps (mulawdec->srcpad, tempcaps))
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
gst_mulawdec_get_type(void) {
  static GType mulawdec_type = 0;

  if (!mulawdec_type) {
    static const GTypeInfo mulawdec_info = {
      sizeof(GstMuLawDecClass),      NULL,
      NULL,
      (GClassInitFunc)gst_mulawdec_class_init,
      NULL,
      NULL,
      sizeof(GstMuLawDec),
      0,
      (GInstanceInitFunc)gst_mulawdec_init,
    };
    mulawdec_type = g_type_register_static(GST_TYPE_ELEMENT, "GstMuLawDec", &mulawdec_info, 0);
  }
  return mulawdec_type;
}

static void
gst_mulawdec_class_init (GstMuLawDecClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_mulawdec_set_property;
  gobject_class->get_property = gst_mulawdec_get_property;
}

static void
gst_mulawdec_init (GstMuLawDec *mulawdec)
{
  mulawdec->sinkpad = gst_pad_new_from_template(mulawdec_sink_template,"sink");
  mulawdec->srcpad = gst_pad_new_from_template(mulawdec_src_template,"src");
  /*gst_pad_set_negotiate_function(mulawdec->sinkpad, mulawdec_negotiate_sink);*/

  gst_element_add_pad(GST_ELEMENT(mulawdec),mulawdec->sinkpad);
  gst_pad_set_chain_function(mulawdec->sinkpad,gst_mulawdec_chain);
  gst_element_add_pad(GST_ELEMENT(mulawdec),mulawdec->srcpad);
}

static void
gst_mulawdec_chain (GstPad *pad,GstBuffer *buf)
{
  GstMuLawDec *mulawdec;
  gint16 *linear_data;
  guint8 *mulaw_data;
  GstBuffer* outbuf;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  mulawdec = GST_MULAWDEC(GST_OBJECT_PARENT (pad));
  g_return_if_fail(mulawdec != NULL);
  g_return_if_fail(GST_IS_MULAWDEC(mulawdec));

  mulaw_data = (guint8 *)GST_BUFFER_DATA(buf);
  outbuf=gst_buffer_new();
  GST_BUFFER_DATA(outbuf) = (gchar*)g_new(gint16,GST_BUFFER_SIZE(buf));
  GST_BUFFER_SIZE(outbuf) = GST_BUFFER_SIZE(buf)*2;

  linear_data = (gint16*)GST_BUFFER_DATA(outbuf);
  mulaw_decode(mulaw_data,linear_data,GST_BUFFER_SIZE(buf));

  gst_buffer_unref(buf);
  gst_pad_push(mulawdec->srcpad,outbuf);
}

static void
gst_mulawdec_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstMuLawDec *mulawdec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MULAWDEC(object));
  mulawdec = GST_MULAWDEC(object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_mulawdec_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstMuLawDec *mulawdec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MULAWDEC(object));
  mulawdec = GST_MULAWDEC(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
