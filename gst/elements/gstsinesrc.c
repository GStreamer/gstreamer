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


static void gst_sinesrc_class_init(GstSineSrcClass *klass);
static void gst_sinesrc_init(GstSineSrc *src);
static void gst_sinesrc_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_sinesrc_get_arg(GtkObject *object,GtkArg *arg,guint id);
//static gboolean gst_sinesrc_change_state(GstElement *element,
//                                          GstElementState state);
//static void gst_sinesrc_close_audio(GstSineSrc *src);
//static gboolean gst_sinesrc_open_audio(GstSineSrc *src);
static void gst_sinesrc_populate_sinetable(GstSineSrc *src);
static inline void gst_sinesrc_update_table_inc(GstSineSrc *src);
static inline void gst_sinesrc_update_vol_scale(GstSineSrc *src);
void gst_sinesrc_sync_parms(GstSineSrc *src);

static GstBuffer * gst_sinesrc_get(GstPad *pad);

static GstElementClass *parent_class = NULL;
//static guint gst_sinesrc_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_sinesrc_get_type(void) {
  static GtkType sinesrc_type = 0;

  if (!sinesrc_type) {
    static const GtkTypeInfo sinesrc_info = {
      "GstSineSrc",
      sizeof(GstSineSrc),
      sizeof(GstSineSrcClass),
      (GtkClassInitFunc)gst_sinesrc_class_init,
      (GtkObjectInitFunc)gst_sinesrc_init,
      (GtkArgSetFunc)gst_sinesrc_set_arg,
      (GtkArgGetFunc)gst_sinesrc_get_arg,
      (GtkClassInitFunc)NULL,
    };
    sinesrc_type = gtk_type_unique(GST_TYPE_ELEMENT,&sinesrc_info);
  }
  return sinesrc_type;
}

static void
gst_sinesrc_class_init(GstSineSrcClass *klass) {
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_ELEMENT);

  gtk_object_add_arg_type("GstSineSrc::volume", GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE, ARG_VOLUME);
  gtk_object_add_arg_type("GstSineSrc::format", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_FORMAT);
  gtk_object_add_arg_type("GstSineSrc::samplerate", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_SAMPLERATE);
  gtk_object_add_arg_type("GstSineSrc::tablesize", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_TABLESIZE);
  gtk_object_add_arg_type("GstSineSrc::freq", GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE, ARG_FREQ);
  gtk_object_add_arg_type("GstSineSrc::buffersize", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_BUFFER_SIZE);
                          
  gtkobject_class->set_arg = gst_sinesrc_set_arg;
  gtkobject_class->get_arg = gst_sinesrc_get_arg;

//  gstelement_class->change_state = gst_sinesrc_change_state;
}

static void gst_sinesrc_init(GstSineSrc *src) {
	
  src->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_pad_set_get_function(src->srcpad, gst_sinesrc_get);
  gst_element_add_pad(GST_ELEMENT(src), src->srcpad);

  src->volume = 1.0;
  gst_sinesrc_update_vol_scale(src);

  src->format = 16;
  src->samplerate = 44100;
  src->freq = 100.0;
  src->newcaps = FALSE;
  
  src->table_pos = 0.0;
  src->table_size = 1024;
  gst_sinesrc_populate_sinetable(src);
  gst_sinesrc_update_table_inc(src);
  gst_sinesrc_sync_parms(src);
  src->buffer_size=1024;
  
  src->seq = 0;

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
    src->newcaps = FALSE;
  }

  //g_print(">");
  return buf;
}

static void gst_sinesrc_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstSineSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SINESRC(object));
  src = GST_SINESRC(object);

  switch (id) {
    case ARG_VOLUME:
      if (GTK_VALUE_DOUBLE(*arg) < 0.0 || GTK_VALUE_DOUBLE(*arg) > 1.0)
        break;
      src->volume = GTK_VALUE_DOUBLE(*arg);
      gst_sinesrc_update_vol_scale(src);
      break;
    case ARG_FORMAT:
      src->format = GTK_VALUE_INT(*arg);
      gst_sinesrc_sync_parms(src);
      break;
    case ARG_SAMPLERATE:
      src->samplerate = GTK_VALUE_INT(*arg);
      gst_sinesrc_sync_parms(src);
      gst_sinesrc_update_table_inc(src);
      break;
    case ARG_FREQ: {
      if (GTK_VALUE_DOUBLE(*arg) <= 0.0 || GTK_VALUE_DOUBLE(*arg) > src->samplerate/2)
        break;
      src->freq = GTK_VALUE_DOUBLE(*arg);
      gst_sinesrc_update_table_inc(src);
      break;
    case ARG_TABLESIZE:
      src->table_size = GTK_VALUE_INT(*arg);
      gst_sinesrc_populate_sinetable(src);
      gst_sinesrc_update_table_inc(src);
      break;
    case ARG_BUFFER_SIZE:
      src->buffer_size = GTK_VALUE_INT(*arg);
      break;
    }
    default:
      break;
  }
}

static void gst_sinesrc_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstSineSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SINESRC(object));
  src = GST_SINESRC(object);

  switch (id) {
    case ARG_VOLUME:
      GTK_VALUE_DOUBLE(*arg) = src->volume;
      break;
    case ARG_FORMAT:
      GTK_VALUE_INT(*arg) = src->format;
      break;
    case ARG_SAMPLERATE:
      GTK_VALUE_INT(*arg) = src->samplerate;
      break;
    case ARG_FREQ:
      GTK_VALUE_DOUBLE(*arg) = src->freq;
      break;
    case ARG_TABLESIZE:
      GTK_VALUE_INT(*arg) = src->table_size;
      break;
    case ARG_BUFFER_SIZE:
      GTK_VALUE_INT(*arg) = src->buffer_size;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
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

static void gst_sinesrc_populate_sinetable(GstSineSrc *src)
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

static inline void gst_sinesrc_update_table_inc(GstSineSrc *src)
{
  src->table_inc = src->table_size * src->freq / src->samplerate;
}

static inline void gst_sinesrc_update_vol_scale(GstSineSrc *src)
{
  src->vol_scale = 32767 * src->volume;
}

void gst_sinesrc_sync_parms(GstSineSrc *src) {
  src->newcaps = TRUE;
}
