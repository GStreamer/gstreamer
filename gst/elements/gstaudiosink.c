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
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include <unistd.h>

//#define DEBUG_ENABLED

#include <gstaudiosink.h>
#include <gst/meta/audioraw.h>


GstElementDetails gst_audiosink_details = {  
  "Audio Sink (OSS)",
  "Sink/Audio",
  "Output to a sound card via OSS",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


static gboolean gst_audiosink_open_audio(GstAudioSink *sink);
static void gst_audiosink_close_audio(GstAudioSink *sink);
static GstElementStateReturn gst_audiosink_change_state(GstElement *element);

static void gst_audiosink_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_audiosink_get_arg(GtkObject *object,GtkArg *arg,guint id);

void gst_audiosink_chain(GstPad *pad,GstBuffer *buf);

/* AudioSink signals and args */
enum {
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_MUTE,
  ARG_FORMAT,
  ARG_CHANNELS,
  ARG_FREQUENCY,
  /* FILL ME */
};

#define GST_TYPE_AUDIOSINK_FORMATS (gst_audiosink_formats_get_type())

GtkType 
gst_audiosink_formats_get_type(void) {
  static GtkType audiosink_formats_type = 0;
  static GtkEnumValue audiosink_formats[] = {
    {8, "8", "8 Bits"},
    {16, "16", "16 Bits"},
    {0, NULL, NULL},
  };
  if (!audiosink_formats_type) {
    audiosink_formats_type = gtk_type_register_enum("GstAudiosinkFormats", audiosink_formats);
  }
  return audiosink_formats_type;
}

#define GST_TYPE_AUDIOSINK_CHANNELS (gst_audiosink_channels_get_type())

GtkType 
gst_audiosink_channels_get_type(void) {
  static GtkType audiosink_channels_type = 0;
  static GtkEnumValue audiosink_channels[] = {
    {1, "1", "Mono"},
    {2, "2", "Stereo"},
    {0, NULL, NULL},
  };
  if (!audiosink_channels_type) {
    audiosink_channels_type = gtk_type_register_enum("GstAudiosinkChannels", audiosink_channels);
  }
  return audiosink_channels_type;
}

static void gst_audiosink_class_init(GstAudioSinkClass *klass);
static void gst_audiosink_init(GstAudioSink *audiosink);


static GstSinkClass *parent_class = NULL;
static guint gst_audiosink_signals[LAST_SIGNAL] = { 0 };

static guint16 gst_audiosink_type_audio = 0;

GtkType
gst_audiosink_get_type(void) {
  static GtkType audiosink_type = 0;

  if (!audiosink_type) {
    static const GtkTypeInfo audiosink_info = {
      "GstAudioSink",
      sizeof(GstAudioSink),
      sizeof(GstAudioSinkClass),
      (GtkClassInitFunc)gst_audiosink_class_init,
      (GtkObjectInitFunc)gst_audiosink_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    audiosink_type = gtk_type_unique(GST_TYPE_SINK,&audiosink_info);
  }

  if (!gst_audiosink_type_audio)
    gst_audiosink_type_audio = gst_type_find_by_mime("audio/raw");

  return audiosink_type;
}

static void
gst_audiosink_class_init(GstAudioSinkClass *klass) {
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_FILTER);

  gtk_object_add_arg_type("GstAudioSink::mute", GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE, ARG_MUTE);
  gtk_object_add_arg_type("GstAudioSink::format", GST_TYPE_AUDIOSINK_FORMATS,
                           GTK_ARG_READWRITE, ARG_FORMAT);
  gtk_object_add_arg_type("GstAudioSink::channels", GST_TYPE_AUDIOSINK_CHANNELS,
                           GTK_ARG_READWRITE, ARG_CHANNELS);
  gtk_object_add_arg_type("GstAudioSink::frequency", GTK_TYPE_INT,
                           GTK_ARG_READWRITE, ARG_FREQUENCY);

  gtkobject_class->set_arg = gst_audiosink_set_arg;
  gtkobject_class->get_arg = gst_audiosink_get_arg;

  gst_audiosink_signals[SIGNAL_HANDOFF] =
    gtk_signal_new("handoff",GTK_RUN_LAST,gtkobject_class->type,
                   GTK_SIGNAL_OFFSET(GstAudioSinkClass,handoff),
                   gtk_marshal_NONE__POINTER,GTK_TYPE_NONE,1,
                   GST_TYPE_AUDIOSINK);
  gtk_object_class_add_signals(gtkobject_class,gst_audiosink_signals,
                               LAST_SIGNAL);

  gstelement_class->change_state = gst_audiosink_change_state;
}

static void gst_audiosink_init(GstAudioSink *audiosink) {
  audiosink->sinkpad = gst_pad_new("sink",GST_PAD_SINK);
  gst_element_add_pad(GST_ELEMENT(audiosink),audiosink->sinkpad);

  gst_pad_set_type_id(audiosink->sinkpad,gst_audiosink_type_audio);

  gst_pad_set_chain_function(audiosink->sinkpad,gst_audiosink_chain);

  audiosink->fd = -1;
  audiosink->clock = gst_clock_get_system();
  gst_clock_register(audiosink->clock, GST_OBJECT(audiosink));
  //audiosink->clocktime = 0LL;

  GST_FLAG_SET(audiosink, GST_ELEMENT_THREAD_SUGGESTED);
}

void gst_audiosink_sync_parms(GstAudioSink *audiosink) {
  audio_buf_info ospace;
  int frag;

  g_return_if_fail(audiosink != NULL);
  g_return_if_fail(GST_IS_AUDIOSINK(audiosink));

  if (audiosink->fd == -1) return;

  ioctl(audiosink->fd,SNDCTL_DSP_RESET,0);

  ioctl(audiosink->fd,SNDCTL_DSP_SETFMT,&audiosink->format);
  ioctl(audiosink->fd,SNDCTL_DSP_CHANNELS,&audiosink->channels);
  ioctl(audiosink->fd,SNDCTL_DSP_SPEED,&audiosink->frequency);
  ioctl(audiosink->fd,SNDCTL_DSP_GETBLKSIZE, &frag);

  ioctl(audiosink->fd,SNDCTL_DSP_GETOSPACE,&ospace);

  g_print("audiosink: setting sound card to %dKHz %d bit %s (%d bytes buffer, %d fragment)\n",
          audiosink->frequency,audiosink->format,
          (audiosink->channels == 2) ? "stereo" : "mono",ospace.bytes, frag);

}

GstElement *gst_audiosink_new(gchar *name) {
  GstElement *audiosink = GST_ELEMENT(gtk_type_new(GST_TYPE_AUDIOSINK));
  gst_element_set_name(GST_ELEMENT(audiosink),name);
  return audiosink;
}

void gst_audiosink_chain(GstPad *pad,GstBuffer *buf) {
  GstAudioSink *audiosink;
  MetaAudioRaw *meta;
  gboolean in_flush;
  audio_buf_info ospace;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);


  /* this has to be an audio buffer */
//  g_return_if_fail(((GstMeta *)buf->meta)->type !=
//gst_audiosink_type_audio);
  audiosink = GST_AUDIOSINK(pad->parent);
//  g_return_if_fail(GST_FLAG_IS_SET(audiosink,GST_STATE_RUNNING));

  if ((in_flush = GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLUSH))) {
    DEBUG("audiosink: flush\n");
    ioctl(audiosink->fd,SNDCTL_DSP_RESET,0);
  }


  meta = (MetaAudioRaw *)gst_buffer_get_first_meta(buf);
  if (meta != NULL) {
    if ((meta->format != audiosink->format) ||
        (meta->channels != audiosink->channels) ||
        (meta->frequency != audiosink->frequency)) {
      audiosink->format = meta->format;
      audiosink->channels = meta->channels;
      audiosink->frequency = meta->frequency;
      gst_audiosink_sync_parms(audiosink);
      g_print("audiosink: sound device set to format %d, %d channels, %dHz\n",
              audiosink->format,audiosink->channels,audiosink->frequency);
    }
  }

  gtk_signal_emit(GTK_OBJECT(audiosink),gst_audiosink_signals[SIGNAL_HANDOFF],
                  audiosink);

  if (GST_BUFFER_DATA(buf) != NULL) {
    gst_trace_add_entry(NULL,0,buf,"audiosink: writing to soundcard");
    //g_print("audiosink: writing to soundcard\n");
    if (audiosink->fd > 2) {
      if (!audiosink->mute) {
        gst_clock_wait(audiosink->clock, GST_BUFFER_TIMESTAMP(buf), GST_OBJECT(audiosink));
        ioctl(audiosink->fd,SNDCTL_DSP_GETOSPACE,&ospace);
        DEBUG("audiosink: (%d bytes buffer) %d %p %d\n", ospace.bytes, audiosink->fd, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
        write(audiosink->fd,GST_BUFFER_DATA(buf),GST_BUFFER_SIZE(buf));
        //write(STDOUT_FILENO,GST_BUFFER_DATA(buf),GST_BUFFER_SIZE(buf));
      }
    }
  }
  gst_buffer_unref(buf);
}

static void gst_audiosink_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstAudioSink *audiosink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_AUDIOSINK(object));
  audiosink = GST_AUDIOSINK(object);

  switch(id) {
    case ARG_MUTE:
      audiosink->mute = GTK_VALUE_BOOL(*arg);
      break;
    case ARG_FORMAT:
      audiosink->format = GTK_VALUE_ENUM(*arg);
      gst_audiosink_sync_parms(audiosink);
      break;
    case ARG_CHANNELS:
      audiosink->channels = GTK_VALUE_ENUM(*arg);
      gst_audiosink_sync_parms(audiosink);
      break;
    case ARG_FREQUENCY:
      audiosink->frequency = GTK_VALUE_INT(*arg);
      gst_audiosink_sync_parms(audiosink);
      break;
    default:
      break;
  }
}

static void gst_audiosink_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstAudioSink *audiosink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_AUDIOSINK(object));
  audiosink = GST_AUDIOSINK(object);

  switch(id) {
    case ARG_MUTE:
      GTK_VALUE_BOOL(*arg) = audiosink->mute;
      break;
    case ARG_FORMAT:
      GTK_VALUE_ENUM(*arg) = audiosink->format;
      break;
    case ARG_CHANNELS:
      GTK_VALUE_ENUM(*arg) = audiosink->channels;
      break;
    case ARG_FREQUENCY:
      GTK_VALUE_INT(*arg) = audiosink->frequency;
      break;
    default:
      break;
  }
}

static gboolean gst_audiosink_open_audio(GstAudioSink *sink) {
  g_return_val_if_fail(sink->fd == -1, FALSE);

  g_print("audiosink: attempting to open sound device\n");

  /* first try to open the sound card */
  sink->fd = open("/dev/dsp",O_WRONLY);

  /* if we have it, set the default parameters and go have fun */
  if (sink->fd > 0) {
    /* set card state */
    sink->format = AFMT_S16_LE;
    sink->channels = 2; /* stereo */
    sink->frequency = 44100;
    gst_audiosink_sync_parms(sink);
    ioctl(sink->fd,SNDCTL_DSP_GETCAPS,&sink->caps);

    g_print("audiosink: Capabilities\n");
    if (sink->caps & DSP_CAP_DUPLEX)   g_print("audiosink:   Full duplex\n");
    if (sink->caps & DSP_CAP_REALTIME) g_print("audiosink:   Realtime\n");
    if (sink->caps & DSP_CAP_BATCH)    g_print("audiosink:   Batch\n");
    if (sink->caps & DSP_CAP_COPROC)   g_print("audiosink:   Has coprocessor\n");
    if (sink->caps & DSP_CAP_TRIGGER)  g_print("audiosink:   Trigger\n");
    if (sink->caps & DSP_CAP_MMAP)     g_print("audiosink:   Direct access\n");
    g_print("audiosink: opened audio with fd=%d\n", sink->fd);
    GST_FLAG_SET(sink,GST_AUDIOSINK_OPEN);
    return TRUE;
  }

  return FALSE;
}

static void gst_audiosink_close_audio(GstAudioSink *sink) {
  if (sink->fd < 0) return;

  close(sink->fd);
  sink->fd = -1;
  GST_FLAG_UNSET(sink,GST_AUDIOSINK_OPEN);
  g_print("audiosink: closed sound device\n");
}

static GstElementStateReturn gst_audiosink_change_state(GstElement *element) {
  g_return_val_if_fail(GST_IS_AUDIOSINK(element), FALSE);

  /* if going down into NULL state, close the file if it's open */ 
  if (GST_STATE_PENDING(element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET(element,GST_AUDIOSINK_OPEN))
      gst_audiosink_close_audio(GST_AUDIOSINK(element));
  /* otherwise (READY or higher) we need to open the sound card */
  } else {
    if (!GST_FLAG_IS_SET(element,GST_AUDIOSINK_OPEN)) {
      if (!gst_audiosink_open_audio(GST_AUDIOSINK(element)))
        return GST_STATE_FAILURE;
    }
  }
      
  if (GST_ELEMENT_CLASS(parent_class)->change_state)
    return GST_ELEMENT_CLASS(parent_class)->change_state(element);
  return GST_STATE_SUCCESS;
}

gboolean gst_audiosink_factory_init(GstElementFactory *factory) {
 
  if (!gst_audiosink_type_audio)
    gst_audiosink_type_audio = gst_type_find_by_mime("audio/raw");

  gst_type_add_sink(gst_audiosink_type_audio, factory);

  return TRUE;
}

