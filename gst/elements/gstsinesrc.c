/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * gstsinesrc.c: 
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

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <gstsinesrc.h>


GstElementDetails gst_sinesrc_details = {
  "Sine-wave src",
  "Source/Audio",
  "Create a sine wave of a given frequency and volume",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* SineSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_VOLUME,
  ARG_FORMAT,
  ARG_SAMPLERATE,
  ARG_FREQ,
  ARG_TABLESIZE,
  ARG_BUFFER_SIZE,
};

static GstPadTemplate*
sinesrc_src_factory (void)
{
  return 
    gst_padtemplate_new (
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    gst_caps_new (
    "sinesrc_src",
      "audio/raw",
      gst_props_new (
      "format",   GST_PROPS_STRING ("int"),
        "law",     GST_PROPS_INT (0),
        "endianness",     GST_PROPS_INT (G_BYTE_ORDER),
        "signed",   GST_PROPS_BOOLEAN (TRUE),
        "width",   GST_PROPS_INT (16),
        "depth",    GST_PROPS_INT (16),
      "rate",     GST_PROPS_INT_RANGE (8000, 48000),
      "channels", GST_PROPS_INT (1),
    NULL)),
  NULL);
}

static GstPadTemplate *src_temp;

static void gst_sinesrc_class_init(GstSineSrcClass *klass);
static void gst_sinesrc_init(GstSineSrc *src);
static GstPadNegotiateReturn gst_sinesrc_negotiate (GstPad *pad, GstCaps **caps, gpointer *data); 
static void gst_sinesrc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_sinesrc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
//static gboolean gst_sinesrc_change_state(GstElement *element,
//                                          GstElementState state);
//static void gst_sinesrc_close_audio(GstSineSrc *src);
//static gboolean gst_sinesrc_open_audio(GstSineSrc *src);
static void gst_sinesrc_populate_sinetable(GstSineSrc *src);
static inline void gst_sinesrc_update_table_inc(GstSineSrc *src);
static inline void gst_sinesrc_update_vol_scale(GstSineSrc *src);
void gst_sinesrc_force_caps(GstSineSrc *src);

static GstBuffer * gst_sinesrc_get(GstPad *pad);

static GstElementClass *parent_class = NULL;
//static guint gst_sinesrc_signals[LAST_SIGNAL] = { 0 };

GType
gst_sinesrc_get_type(void) {
  static GType sinesrc_type = 0;

  if (!sinesrc_type) {
    static const GTypeInfo sinesrc_info = {
      sizeof(GstSineSrcClass),      NULL,
      NULL,
      (GClassInitFunc)gst_sinesrc_class_init,
      NULL,
      NULL,
      sizeof(GstSineSrc),
      0,
      (GInstanceInitFunc)gst_sinesrc_init,
    };
    sinesrc_type = g_type_register_static(GST_TYPE_ELEMENT, "GstSineSrc", &sinesrc_info, 0);
  }
  return sinesrc_type;
}

static void
gst_sinesrc_class_init(GstSineSrcClass *klass) {
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

//  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_VOLUME,
//    g_param_spec_double("volume","volume","volume",
//                        G_MINDOUBLE,G_MAXDOUBLE,0.0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FORMAT,
    g_param_spec_int("format","format","format",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SAMPLERATE,
    g_param_spec_int("samplerate","samplerate","samplerate",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_TABLESIZE,
    g_param_spec_int("tablesize","tablesize","tablesize",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
//  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FREQ,
//    g_param_spec_double("freq","freq","freq",
//                        G_MINDOUBLE,G_MAXDOUBLE,0.0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFFER_SIZE,
    g_param_spec_int("buffersize","buffersize","buffersize",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
                          
  gobject_class->set_property = gst_sinesrc_set_property;
  gobject_class->get_property = gst_sinesrc_get_property;

//  gstelement_class->change_state = gst_sinesrc_change_state;
}

static void 
gst_sinesrc_init(GstSineSrc *src) {

  src->srcpad = gst_pad_new_from_template (src_temp, "src");

  gst_element_add_pad(GST_ELEMENT(src), src->srcpad);
  gst_pad_set_negotiate_function (src->srcpad, gst_sinesrc_negotiate);
  
  gst_pad_set_get_function(src->srcpad, gst_sinesrc_get);

  src->volume = 1.0;
  gst_sinesrc_update_vol_scale(src);

  src->format = 16;
  src->samplerate = 44100;
  src->freq = 440.0;
  src->newcaps = TRUE;
  
  src->table_pos = 0.0;
  src->table_size = 1024;
  gst_sinesrc_populate_sinetable(src);
  gst_sinesrc_update_table_inc(src);
  src->buffer_size=1024;
  
  src->seq = 0;

}

static GstPadNegotiateReturn 
gst_sinesrc_negotiate (GstPad *pad, GstCaps **caps, gpointer *data) 
{
  GstSineSrc *src;

  if (*caps) {
    g_return_val_if_fail (pad != NULL, GST_PAD_NEGOTIATE_FAIL);
    src = GST_SINESRC(gst_pad_get_parent (pad));
    src->samplerate = gst_caps_get_int (*caps, "rate");
    gst_sinesrc_update_table_inc(src);
    return GST_PAD_NEGOTIATE_AGREE;
  }

  return GST_PAD_NEGOTIATE_FAIL;
}

static GstBuffer *
gst_sinesrc_get(GstPad *pad)
{
  GstSineSrc *src;
  GstBuffer *buf;
  gint16 *samples;
  gint i;
  
  g_return_val_if_fail (pad != NULL, NULL);
  src = GST_SINESRC(gst_pad_get_parent (pad));

  buf = gst_buffer_new();
  g_return_val_if_fail (buf, NULL);
  samples = g_new(gint16, src->buffer_size);
  GST_BUFFER_DATA(buf) = (gpointer) samples;
  GST_BUFFER_SIZE(buf) = 2 * src->buffer_size;
  
  for (i=0 ; i < src->buffer_size; i++) {
    src->table_lookup = (gint)(src->table_pos);
    src->table_lookup_next = src->table_lookup + 1;
    src->table_interp = src->table_pos - src->table_lookup;
    
    // wrap the array lookups if we're out of bounds
    if (src->table_lookup_next >= src->table_size){
      src->table_lookup_next -= src->table_size;
      if (src->table_lookup >= src->table_size){
        src->table_lookup -= src->table_size;
        src->table_pos -= src->table_size;
      }
    }
    
    src->table_pos += src->table_inc;

    //no interpolation
    //samples[i] = src->table_data[src->table_lookup]
    //               * src->vol_scale;
                  	
    //linear interpolation
    samples[i] = ((src->table_interp
                   *(src->table_data[src->table_lookup_next]
                    -src->table_data[src->table_lookup]
                    )
                  )+src->table_data[src->table_lookup]
                 )* src->vol_scale;
  }

  if (src->newcaps) {
    gst_sinesrc_force_caps(src);
  }

  return buf;
}

static void 
gst_sinesrc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
  GstSineSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SINESRC(object));
  src = GST_SINESRC(object);

  switch (prop_id) {
    case ARG_VOLUME:
      if (g_value_get_double (value) < 0.0 || g_value_get_double (value) > 1.0)
        break;
      src->volume = g_value_get_double (value);
      gst_sinesrc_update_vol_scale(src);
      break;
    case ARG_FORMAT:
      src->format = g_value_get_int (value);
      src->newcaps=TRUE;
      break;
    case ARG_SAMPLERATE:
      src->samplerate = g_value_get_int (value);
      src->newcaps=TRUE;
      gst_sinesrc_update_table_inc(src);
      break;
    case ARG_FREQ: {
      if (g_value_get_double (value) <= 0.0 || g_value_get_double (value) > src->samplerate/2)
        break;
      src->freq = g_value_get_double (value);
      gst_sinesrc_update_table_inc(src);
      break;
    case ARG_TABLESIZE:
      src->table_size = g_value_get_int (value);
      gst_sinesrc_populate_sinetable(src);
      gst_sinesrc_update_table_inc(src);
      break;
    case ARG_BUFFER_SIZE:
      src->buffer_size = g_value_get_int (value);
      break;
    }
    default:
      break;
  }
}

static void 
gst_sinesrc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
  GstSineSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SINESRC(object));
  src = GST_SINESRC(object);

  switch (prop_id) {
    case ARG_VOLUME:
      g_value_set_double (value, src->volume);
      break;
    case ARG_FORMAT:
      g_value_set_int (value, src->format);
      break;
    case ARG_SAMPLERATE:
      g_value_set_int (value, src->samplerate);
      break;
    case ARG_FREQ:
      g_value_set_double (value, src->freq);
      break;
    case ARG_TABLESIZE:
      g_value_set_int (value, src->table_size);
      break;
    case ARG_BUFFER_SIZE:
      g_value_set_int (value, src->buffer_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/*
static gboolean gst_sinesrc_change_state(GstElement *element,
                                          GstElementState state) {
  g_return_if_fail(GST_IS_SINESRC(element));

  switch (state) {
    case GST_STATE_RUNNING:
      if (!gst_sinesrc_open_audio(GST_SINESRC(element)))
        return FALSE;
      break;
    case ~GST_STATE_RUNNING:
      gst_sinesrc_close_audio(GST_SINESRC(element));
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element,state);
  return TRUE;
}
*/

static void 
gst_sinesrc_populate_sinetable(GstSineSrc *src)
{
  gint i;
  gdouble pi2scaled = M_PI * 2 / src->table_size;
  gfloat *table = g_new(gfloat, src->table_size);

  for(i=0 ; i < src->table_size ; i++){
    table[i] = (gfloat)sin(i * pi2scaled);
  }
  
  g_free(src->table_data);
  src->table_data = table;
}

static inline void 
gst_sinesrc_update_table_inc(GstSineSrc *src)
{
  src->table_inc = src->table_size * src->freq / src->samplerate;
}

static inline void 
gst_sinesrc_update_vol_scale(GstSineSrc *src)
{
  src->vol_scale = 32767 * src->volume;
}

void 
gst_sinesrc_force_caps(GstSineSrc *src) {
  GstCaps *caps;

  if (!src->newcaps)
    return;
  
  src->newcaps=FALSE;

  caps = gst_caps_new (
		    "sinesrc_src_caps",
		    "audio/raw",
		     gst_props_new (
		           "format", 		GST_PROPS_STRING ("int"),
    			     "law",     	GST_PROPS_INT (0),
    			     "endianness",     	GST_PROPS_INT (G_BYTE_ORDER),
    			     "signed",   	GST_PROPS_BOOLEAN (TRUE),
    			     "width",   	GST_PROPS_INT (16),
			     "depth", 		GST_PROPS_INT (16),
			     "rate", 		GST_PROPS_INT (src->samplerate),
			     "channels", 	GST_PROPS_INT (1),
			     NULL
			     )
		    );
  
  gst_pad_set_caps (src->srcpad, caps);
}

gboolean 
gst_sinesrc_factory_init (GstElementFactory *factory) 
{
  src_temp = sinesrc_src_factory();
  gst_elementfactory_add_padtemplate (factory, src_temp);
  
  return TRUE;
}
