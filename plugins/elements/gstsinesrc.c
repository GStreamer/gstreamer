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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include <math.h>
#include <stdlib.h>

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
  ARG_FREQ,
  ARG_FORMAT,
  ARG_CHANNELS,
  ARG_FREQUENCY,
};


static void gst_sinesrc_class_init(GstSineSrcClass *klass);
static void gst_sinesrc_init(GstSineSrc *sinesrc);
static void gst_sinesrc_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_sinesrc_get_arg(GtkObject *object,GtkArg *arg,guint id);
//static gboolean gst_sinesrc_change_state(GstElement *element,
//                                          GstElementState state);
//static void gst_sinesrc_close_audio(GstSineSrc *src);
//static gboolean gst_sinesrc_open_audio(GstSineSrc *src);
void gst_sinesrc_sync_parms(GstSineSrc *sinesrc);


static GstSrcClass *parent_class = NULL;
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
    sinesrc_type = gtk_type_unique(GST_TYPE_SRC,&sinesrc_info);
  }
  return sinesrc_type;
}

static void
gst_sinesrc_class_init(GstSineSrcClass *klass) {
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;
  GstSrcClass *gstsrc_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;
  gstsrc_class = (GstSrcClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_SRC);

  gtk_object_add_arg_type("GstSineSrc::volume", GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE, ARG_VOLUME);
  gtk_object_add_arg_type("GstSineSrc::freq", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_FREQ);
  gtk_object_add_arg_type("GstSineSrc::format", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_FORMAT);
  gtk_object_add_arg_type("GstSineSrc::channels", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_CHANNELS);
  gtk_object_add_arg_type("GstSineSrc::frequency", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_FREQUENCY);

  gtkobject_class->set_arg = gst_sinesrc_set_arg;
  gtkobject_class->get_arg = gst_sinesrc_get_arg;

//  gstelement_class->change_state = gst_sinesrc_change_state;

  gstsrc_class->push = gst_sinesrc_push;
}

static void gst_sinesrc_init(GstSineSrc *sinesrc) {
  sinesrc->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(sinesrc),sinesrc->srcpad);

  sinesrc->volume = 1.0;
  sinesrc->freq = 512;

  sinesrc->format = AFMT_S16_LE;
  sinesrc->channels = 2;
  sinesrc->frequency = 44100;

  sinesrc->seq = 0;

  sinesrc->sentmeta = FALSE;
}

GstElement *gst_sinesrc_new(gchar *name) {
  GstElement *sinesrc = GST_ELEMENT(gtk_type_new(GST_TYPE_SINESRC));
  gst_element_set_name(GST_ELEMENT(sinesrc),name);
  return sinesrc;
}

GstElement *gst_sinesrc_new_with_fd(gchar *name,gchar *filename) {
  GstElement *sinesrc = gst_sinesrc_new(name);
  gtk_object_set(GTK_OBJECT(sinesrc),"location",filename,NULL);
  return sinesrc;
}

void gst_sinesrc_push(GstSrc *src) {
  GstSineSrc *sinesrc;
  GstBuffer *buf;
  gint16 *samples;
  gint i;
  gint volume;
  gdouble val;

  g_return_if_fail(src != NULL);
  g_return_if_fail(GST_IS_SINESRC(src));
  sinesrc = GST_SINESRC(src);

  buf = gst_buffer_new();
  g_return_if_fail(buf);
  GST_BUFFER_DATA(buf) = (gpointer)malloc(4096);
  samples = (gint16*)GST_BUFFER_DATA(buf);
  GST_BUFFER_SIZE(buf) = 4096;

  volume = 65535 * sinesrc->volume;
  for (i=0;i<1024;i++) {
    val = sin((gdouble)i/sinesrc->frequency);
    samples[i] = val * volume;
    samples[i+1] = samples[i];
  }

  if (!sinesrc->sentmeta) {
    MetaAudioRaw *newmeta = g_new(MetaAudioRaw,1);
    memcpy(newmeta,&sinesrc->meta,sizeof(MetaAudioRaw));
    gst_buffer_add_meta(buf,GST_META(newmeta));
    sinesrc->sentmeta = TRUE;
  }

  gst_pad_push(sinesrc->srcpad,buf);
  g_print(">");
}

static void gst_sinesrc_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstSineSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SINESRC(object));
  src = GST_SINESRC(object);

  switch (id) {
    case ARG_VOLUME:
      src->volume = GTK_VALUE_DOUBLE(*arg);
      break;
    case ARG_FREQ:
      src->freq = GTK_VALUE_INT(*arg);
      break;
    case ARG_FORMAT:
      src->format = GTK_VALUE_INT(*arg);
      break;
    case ARG_CHANNELS:
      src->channels = GTK_VALUE_INT(*arg);
      break;
    case ARG_FREQUENCY:
      src->frequency = GTK_VALUE_INT(*arg);
      break;
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
    case ARG_FREQ:
      GTK_VALUE_INT(*arg) = src->freq;
      break;
    case ARG_FORMAT:
      GTK_VALUE_INT(*arg) = src->format;
      break;
    case ARG_CHANNELS:
      GTK_VALUE_INT(*arg) = src->channels;
      break;
    case ARG_FREQUENCY:
      GTK_VALUE_INT(*arg) = src->frequency;
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

void gst_sinesrc_sync_parms(GstSineSrc *sinesrc) {
  sinesrc->meta.format = sinesrc->format;
  sinesrc->meta.channels = sinesrc->channels;
  sinesrc->meta.frequency = sinesrc->frequency;
  sinesrc->sentmeta = FALSE;
}
