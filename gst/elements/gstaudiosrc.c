/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstaudiosrc.c: 
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
#include <sys/ioctl.h>
#include <unistd.h>

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
  ARG_BYTESPERREAD,
  ARG_CUROFFSET,
  ARG_FORMAT,
  ARG_CHANNELS,
  ARG_FREQUENCY,
};


static void 			gst_audiosrc_class_init		(GstAudioSrcClass *klass);
static void 			gst_audiosrc_init		(GstAudioSrc *audiosrc);

static void 			gst_audiosrc_set_arg		(GtkObject *object, GtkArg *arg, guint id);
static void 			gst_audiosrc_get_arg		(GtkObject *object, GtkArg *arg, guint id);
static GstElementStateReturn 	gst_audiosrc_change_state	(GstElement *element);

static void 			gst_audiosrc_close_audio	(GstAudioSrc *src);
static gboolean 		gst_audiosrc_open_audio		(GstAudioSrc *src);
static void 			gst_audiosrc_sync_parms		(GstAudioSrc *audiosrc);

static GstBuffer *		gst_audiosrc_get		(GstPad *pad);

static GstElementClass *parent_class = NULL;
//static guint gst_audiosrc_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_audiosrc_get_type (void) 
{
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
    audiosrc_type = gtk_type_unique (GST_TYPE_ELEMENT, &audiosrc_info);
  }
  return audiosrc_type;
}

static void
gst_audiosrc_class_init (GstAudioSrcClass *klass) 
{
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);

  gtk_object_add_arg_type ("GstAudioSrc::bytes_per_read", GTK_TYPE_ULONG,
                           GTK_ARG_READWRITE, ARG_BYTESPERREAD);
  gtk_object_add_arg_type ("GstAudioSrc::curoffset", GTK_TYPE_ULONG,
                           GTK_ARG_READABLE, ARG_CUROFFSET);
  gtk_object_add_arg_type ("GstAudioSrc::format", GTK_TYPE_INT,
                           GTK_ARG_READWRITE, ARG_FORMAT);
  gtk_object_add_arg_type ("GstAudioSrc::channels", GTK_TYPE_INT,
                           GTK_ARG_READWRITE, ARG_CHANNELS);
  gtk_object_add_arg_type ("GstAudioSrc::frequency", GTK_TYPE_INT,
                           GTK_ARG_READWRITE, ARG_FREQUENCY);

  gtkobject_class->set_arg = gst_audiosrc_set_arg;
  gtkobject_class->get_arg = gst_audiosrc_get_arg;

  gstelement_class->change_state = gst_audiosrc_change_state;
}

static void 
gst_audiosrc_init (GstAudioSrc *audiosrc) 
{
  audiosrc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function(audiosrc->srcpad,gst_audiosrc_get);
  gst_element_add_pad (GST_ELEMENT (audiosrc), audiosrc->srcpad);

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

static GstBuffer *
gst_audiosrc_get (GstPad *pad)
{
  GstAudioSrc *src;
  GstBuffer *buf;
  glong readbytes;

  g_return_val_if_fail (pad != NULL, NULL);
  src = GST_AUDIOSRC(gst_pad_get_parent(pad));

//  g_print("attempting to read something from soundcard\n");

  buf = gst_buffer_new ();
  g_return_val_if_fail (buf, NULL);
  
  GST_BUFFER_DATA (buf) = (gpointer)g_malloc (src->bytes_per_read);

  readbytes = read (src->fd,GST_BUFFER_DATA (buf),
                    src->bytes_per_read);

  if (readbytes == 0) {
    gst_element_signal_eos (GST_ELEMENT (src));
    return NULL;
  }

  GST_BUFFER_SIZE (buf) = readbytes;
  GST_BUFFER_OFFSET (buf) = src->curoffset;
  
  src->curoffset += readbytes;

//  gst_buffer_add_meta(buf,GST_META(newmeta));

//  g_print("pushed buffer from soundcard of %d bytes\n",readbytes);
  return buf;
}

static void 
gst_audiosrc_set_arg (GtkObject *object, GtkArg *arg, guint id) 
{
  GstAudioSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AUDIOSRC (object));
  
  src = GST_AUDIOSRC (object);

  switch (id) {
    case ARG_BYTESPERREAD:
      src->bytes_per_read = GTK_VALUE_INT (*arg);
      break;
    case ARG_FORMAT:
      src->format = GTK_VALUE_INT (*arg);
      break;
    case ARG_CHANNELS:
      src->channels = GTK_VALUE_INT (*arg);
      break;
    case ARG_FREQUENCY:
      src->frequency = GTK_VALUE_INT (*arg);
      break;
    default:
      break;
  }
}

static void 
gst_audiosrc_get_arg (GtkObject *object, GtkArg *arg, guint id) 
{
  GstAudioSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AUDIOSRC (object));
  
  src = GST_AUDIOSRC (object);

  switch (id) {
    case ARG_BYTESPERREAD:
      GTK_VALUE_INT (*arg) = src->bytes_per_read;
      break;
    case ARG_FORMAT:
      GTK_VALUE_INT (*arg) = src->format;
      break;
    case ARG_CHANNELS:
      GTK_VALUE_INT (*arg) = src->channels;
      break;
    case ARG_FREQUENCY:
      GTK_VALUE_INT (*arg) = src->frequency;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

static GstElementStateReturn 
gst_audiosrc_change_state (GstElement *element) 
{
  g_return_val_if_fail (GST_IS_AUDIOSRC (element), FALSE);

  /* if going down into NULL state, close the file if it's open */
  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_AUDIOSRC_OPEN))
      gst_audiosrc_close_audio (GST_AUDIOSRC (element));
  /* otherwise (READY or higher) we need to open the sound card */
  } else {
    if (!GST_FLAG_IS_SET (element, GST_AUDIOSRC_OPEN)) { 
      if (!gst_audiosrc_open_audio (GST_AUDIOSRC (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  
  return GST_STATE_SUCCESS;
}

static gboolean 
gst_audiosrc_open_audio (GstAudioSrc *src) 
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (src, GST_AUDIOSRC_OPEN), FALSE);

  /* first try to open the sound card */
  src->fd = open("/dev/dsp", O_RDONLY);

  /* if we have it, set the default parameters and go have fun */ 
  if (src->fd > 0) {
    int arg = 0x7fff0006;

    if (ioctl (src->fd, SNDCTL_DSP_SETFRAGMENT, &arg)) perror("uh");

    /* set card state */
    gst_audiosrc_sync_parms (src);
    GST_DEBUG (0,"opened audio\n");
    
    GST_FLAG_SET (src, GST_AUDIOSRC_OPEN);
    return TRUE;
  }

  return FALSE;
}

static void 
gst_audiosrc_close_audio (GstAudioSrc *src) 
{
  g_return_if_fail (GST_FLAG_IS_SET (src, GST_AUDIOSRC_OPEN));

  close(src->fd);
  src->fd = -1;

  GST_FLAG_UNSET (src, GST_AUDIOSRC_OPEN);
}

static void 
gst_audiosrc_sync_parms (GstAudioSrc *audiosrc) 
{
  audio_buf_info ospace;
  
  g_return_if_fail (audiosrc != NULL);
  g_return_if_fail (GST_IS_AUDIOSRC (audiosrc));
  g_return_if_fail (audiosrc->fd > 0);
 
  ioctl(audiosrc->fd, SNDCTL_DSP_RESET, 0);
 
  ioctl(audiosrc->fd, SNDCTL_DSP_SETFMT, &audiosrc->format);
  ioctl(audiosrc->fd, SNDCTL_DSP_CHANNELS, &audiosrc->channels);
  ioctl(audiosrc->fd, SNDCTL_DSP_SPEED, &audiosrc->frequency);
  
  ioctl(audiosrc->fd, SNDCTL_DSP_GETOSPACE, &ospace);
 
  g_print("setting sound card to %dKHz %d bit %s (%d bytes buffer)\n",
          audiosrc->frequency,audiosrc->format,
          (audiosrc->channels == 2) ? "stereo" : "mono",ospace.bytes);

//  audiosrc->meta.format = audiosrc->format;
//  audiosrc->meta.channels = audiosrc->channels;
//  audiosrc->meta.frequency = audiosrc->frequency;
//  audiosrc->sentmeta = FALSE;
}
