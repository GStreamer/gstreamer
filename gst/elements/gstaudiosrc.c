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

#include <gstaudiosrc.h>


GstElementDetails gst_audiosrc_details = {
  "Audio (OSS) Source",
  "Source/Audio",
  "Read from the sound card",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* AudioSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION,
  ARG_BYTESPERREAD,
  ARG_CUROFFSET,
  ARG_FORMAT,
  ARG_CHANNELS,
  ARG_FREQUENCY,
};


static void gst_audiosrc_class_init(GstAudioSrcClass *klass);
static void gst_audiosrc_init(GstAudioSrc *audiosrc);
static void gst_audiosrc_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_audiosrc_get_arg(GtkObject *object,GtkArg *arg,guint id);
static gboolean gst_audiosrc_change_state(GstElement *element,
                                          GstElementState state);
static void gst_audiosrc_close_audio(GstAudioSrc *src);
static gboolean gst_audiosrc_open_audio(GstAudioSrc *src);
void gst_audiosrc_sync_parms(GstAudioSrc *audiosrc);


static GstSrcClass *parent_class = NULL;
static guint gst_audiosrc_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_audiosrc_get_type(void) {
  static GtkType audiosrc_type = 0;

  if (!audiosrc_type) {
    static const GtkTypeInfo audiosrc_info = {
      "GstAudioSrc",
      sizeof(GstAudioSrc),
      sizeof(GstAudioSrcClass),
      (GtkClassInitFunc)gst_audiosrc_class_init,
      (GtkObjectInitFunc)gst_audiosrc_init,
      (GtkArgSetFunc)gst_audiosrc_set_arg,
      (GtkArgGetFunc)gst_audiosrc_get_arg,
      (GtkClassInitFunc)NULL,
    };
    audiosrc_type = gtk_type_unique(GST_TYPE_SRC,&audiosrc_info);
  }
  return audiosrc_type;
}

static void
gst_audiosrc_class_init(GstAudioSrcClass *klass) {
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;
  GstSrcClass *gstsrc_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;
  gstsrc_class = (GstSrcClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_SRC);

  gtk_object_add_arg_type("GstAudioSrc::location", GTK_TYPE_STRING,
                          GTK_ARG_READWRITE, ARG_LOCATION);
  gtk_object_add_arg_type("GstAudioSrc::bytes_per_read", GTK_TYPE_ULONG,
                          GTK_ARG_READWRITE, ARG_BYTESPERREAD);
  gtk_object_add_arg_type("GstAudioSrc::curoffset", GTK_TYPE_ULONG,
                          GTK_ARG_READABLE, ARG_CUROFFSET);
  gtk_object_add_arg_type("GstAudioSrc::format", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_FORMAT);
  gtk_object_add_arg_type("GstAudioSrc::channels", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_CHANNELS);
  gtk_object_add_arg_type("GstAudioSrc::frequency", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_FREQUENCY);

  gtkobject_class->set_arg = gst_audiosrc_set_arg;
  gtkobject_class->get_arg = gst_audiosrc_get_arg;

  gstelement_class->change_state = gst_audiosrc_change_state;

  gstsrc_class->push = gst_audiosrc_push;
}

static void gst_audiosrc_init(GstAudioSrc *audiosrc) {
  audiosrc->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(audiosrc),audiosrc->srcpad);

  audiosrc->filename = g_strdup("/dev/dsp");
  audiosrc->fd = -1;

//  audiosrc->meta = (MetaAudioRaw *)gst_meta_new();
//  audiosrc->meta->format = AFMT_S16_LE;
//  audiosrc->meta->channels = 2;
//  audiosrc->meta->frequency = 44100;
//  audiosrc->meta->bps = 4;

  audiosrc->bytes_per_read = 4096;
  audiosrc->curoffset = 0;
  audiosrc->seq = 0;
}

GstElement *gst_audiosrc_new(gchar *name) {
  GstElement *audiosrc = GST_ELEMENT(gtk_type_new(GST_TYPE_AUDIOSRC));
  gst_element_set_name(GST_ELEMENT(audiosrc),name);
  return audiosrc;
}

GstElement *gst_audiosrc_new_with_fd(gchar *name,gchar *filename) {
  GstElement *audiosrc = gst_audiosrc_new(name);
  gtk_object_set(GTK_OBJECT(audiosrc),"location",filename,NULL);
  return audiosrc;
}

void gst_audiosrc_push(GstSrc *src) {
  GstAudioSrc *audiosrc;
  GstBuffer *buf;
  glong readbytes;

  g_return_if_fail(src != NULL);
  g_return_if_fail(GST_IS_AUDIOSRC(src));
  audiosrc = GST_AUDIOSRC(src);

//  g_print("attempting to read something from soundcard\n");

  buf = gst_buffer_new();
  g_return_if_fail(buf);
  GST_BUFFER_DATA(buf) = (gpointer)g_malloc(audiosrc->bytes_per_read);
  readbytes = read(audiosrc->fd,GST_BUFFER_DATA(buf),
                   audiosrc->bytes_per_read);
  if (readbytes == 0) {
    gst_src_signal_eos(GST_SRC(audiosrc));
    return;
  }

  GST_BUFFER_SIZE(buf) = readbytes;
  GST_BUFFER_OFFSET(buf) = audiosrc->curoffset;
  audiosrc->curoffset += readbytes;

//  gst_buffer_add_meta(buf,GST_META(newmeta));

  gst_pad_push(audiosrc->srcpad,buf);
//  g_print("pushed buffer from soundcard of %d bytes\n",readbytes);
}

static void gst_audiosrc_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstAudioSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_AUDIOSRC(object));
  src = GST_AUDIOSRC(object);

  switch (id) {
    case ARG_LOCATION:
      if (src->filename) g_free(src->filename);
      if (GTK_VALUE_STRING(*arg) == NULL) {
        src->filename = NULL;
        gst_element_set_state(GST_ELEMENT(object),~GST_STATE_COMPLETE);
      } else {
        src->filename = g_strdup(GTK_VALUE_STRING(*arg));
        gst_element_set_state(GST_ELEMENT(object),GST_STATE_COMPLETE);
      }
      break;
    case ARG_BYTESPERREAD:
      src->bytes_per_read = GTK_VALUE_INT(*arg);
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

static void gst_audiosrc_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstAudioSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_AUDIOSRC(object));
  src = GST_AUDIOSRC(object);

  switch (id) {
    case ARG_LOCATION:
      GTK_VALUE_STRING(*arg) = g_strdup(src->filename);
      break;
    case ARG_BYTESPERREAD:
      GTK_VALUE_INT(*arg) = src->bytes_per_read;
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

static gboolean gst_audiosrc_change_state(GstElement *element,
                                          GstElementState state) {
  g_return_if_fail(GST_IS_AUDIOSRC(element));

  switch (state) {
    case GST_STATE_RUNNING:
      if (!gst_audiosrc_open_audio(GST_AUDIOSRC(element)))
        return FALSE;
      break;
    case ~GST_STATE_RUNNING:
      gst_audiosrc_close_audio(GST_AUDIOSRC(element));
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element,state);
  return TRUE;
}

static gboolean gst_audiosrc_open_audio(GstAudioSrc *src) {
  g_return_if_fail(src->fd == -1);

  /* first try to open the sound card */
  src->fd = open("/dev/dsp",O_RDONLY);

  /* if we have it, set the default parameters and go have fun */ 
  if (src->fd > 0) {
    int arg = 0x7fff0006;

    if (ioctl(src->fd, SNDCTL_DSP_SETFRAGMENT, &arg)) perror("uh");

    /* set card state */
    gst_audiosrc_sync_parms(src);
    DEBUG("opened audio\n");
    return TRUE;
  }

  return FALSE;
}

static void gst_audiosrc_close_audio(GstAudioSrc *src) {
  g_return_if_fail(src->fd >= 0);

  close(src->fd);
  src->fd = -1;
}

void gst_audiosrc_sync_parms(GstAudioSrc *audiosrc) {
  audio_buf_info ospace;
  
  g_return_if_fail(audiosrc != NULL);
  g_return_if_fail(GST_IS_AUDIOSRC(audiosrc));
  g_return_if_fail(audiosrc->fd > 0);
 
  ioctl(audiosrc->fd,SNDCTL_DSP_RESET,0);
 
  ioctl(audiosrc->fd,SNDCTL_DSP_SETFMT,&audiosrc->format);
  ioctl(audiosrc->fd,SNDCTL_DSP_CHANNELS,&audiosrc->channels);
  ioctl(audiosrc->fd,SNDCTL_DSP_SPEED,&audiosrc->frequency);
  
  ioctl(audiosrc->fd,SNDCTL_DSP_GETOSPACE,&ospace);
 
  g_print("setting sound card to %dKHz %d bit %s (%d bytes buffer)\n",
          audiosrc->frequency,audiosrc->format,
          (audiosrc->channels == 2) ? "stereo" : "mono",ospace.bytes);

//  audiosrc->meta.format = audiosrc->format;
//  audiosrc->meta.channels = audiosrc->channels;
//  audiosrc->meta.frequency = audiosrc->frequency;
//  audiosrc->sentmeta = FALSE;
}
