/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstafparse.c: 
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <string.h>
#include "gstafparse.h"

/* elementfactory information */
static GstElementDetails afparse_details = {
  "Audiofile Parse",
  "Codec/Demuxer/Audio",
  "Audiofile parser for audio/raw",
  "Steve Baker <stevebaker_org@yahoo.co.uk>",
};


/* AFParse signals and args */
enum
{
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum
{
  ARG_0,
};

/* added a src factory function to force audio/raw MIME type */
static GstStaticPadTemplate afparse_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) { 8, 16 }, "
        "depth = (int) { 8, 16 }, "
        "signed = (boolean) { true, false }, "
        "buffer-frames = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate afparse_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-aiff; " "audio/x-wav; " "audio/x-au")
    );

static void gst_afparse_base_init (gpointer g_class);
static void gst_afparse_class_init (GstAFParseClass * klass);
static void gst_afparse_init (GstAFParse * afparse);

static gboolean gst_afparse_open_file (GstAFParse * afparse);
static void gst_afparse_close_file (GstAFParse * afparse);

static void gst_afparse_loop (GstElement * element);
static void gst_afparse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_afparse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static ssize_t gst_afparse_vf_read (AFvirtualfile * vfile, void *data,
    size_t nbytes);
static long gst_afparse_vf_length (AFvirtualfile * vfile);
static ssize_t gst_afparse_vf_write (AFvirtualfile * vfile, const void *data,
    size_t nbytes);
static void gst_afparse_vf_destroy (AFvirtualfile * vfile);
static long gst_afparse_vf_seek (AFvirtualfile * vfile, long offset,
    int is_relative);
static long gst_afparse_vf_tell (AFvirtualfile * vfile);

GType
gst_afparse_get_type (void)
{
  static GType afparse_type = 0;

  if (!afparse_type) {
    static const GTypeInfo afparse_info = {
      sizeof (GstAFParseClass),
      gst_afparse_base_init,
      NULL,
      (GClassInitFunc) gst_afparse_class_init,
      NULL,
      NULL,
      sizeof (GstAFParse),
      0,
      (GInstanceInitFunc) gst_afparse_init,
    };

    afparse_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstAFParse", &afparse_info,
        0);
  }
  return afparse_type;
}

static void
gst_afparse_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&afparse_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&afparse_sink_factory));

  gst_element_class_set_details (element_class, &afparse_details);
}

static void
gst_afparse_class_init (GstAFParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_afparse_set_property;
  gobject_class->get_property = gst_afparse_get_property;

}

static void
gst_afparse_init (GstAFParse * afparse)
{
  afparse->srcpad =
      gst_pad_new_from_template (gst_element_get_pad_template (GST_ELEMENT
          (afparse), "src"), "src");
  gst_pad_use_explicit_caps (afparse->srcpad);
  gst_element_add_pad (GST_ELEMENT (afparse), afparse->srcpad);

  afparse->sinkpad =
      gst_pad_new_from_template (gst_element_get_pad_template (GST_ELEMENT
          (afparse), "sink"), "sink");
  gst_element_add_pad (GST_ELEMENT (afparse), afparse->sinkpad);

  gst_element_set_loop_function (GST_ELEMENT (afparse), gst_afparse_loop);

  afparse->vfile = af_virtual_file_new ();
  afparse->vfile->closure = NULL;
  afparse->vfile->read = gst_afparse_vf_read;
  afparse->vfile->length = gst_afparse_vf_length;
  afparse->vfile->write = gst_afparse_vf_write;
  afparse->vfile->destroy = gst_afparse_vf_destroy;
  afparse->vfile->seek = gst_afparse_vf_seek;
  afparse->vfile->tell = gst_afparse_vf_tell;

  afparse->frames_per_read = 1024;
  afparse->curoffset = 0;
  afparse->seq = 0;

  afparse->file = NULL;
  /* default values, should never be needed */
  afparse->channels = 2;
  afparse->width = 16;
  afparse->rate = 44100;
  afparse->type = AF_FILE_WAVE;
  afparse->endianness_data = 1234;
  afparse->endianness_wanted = 1234;
  afparse->timestamp = 0LL;
}

static void
gst_afparse_loop (GstElement * element)
{
  GstAFParse *afparse;
  GstBuffer *buf;
  gint numframes = 0, frames_to_bytes, frames_per_read, bytes_per_read;
  guint8 *data;
  gboolean bypass_afread = TRUE;
  GstByteStream *bs;
  int s_format, v_format, s_width, v_width;

  afparse = GST_AFPARSE (element);

  afparse->vfile->closure = bs = gst_bytestream_new (afparse->sinkpad);

  /* just stop if we cannot open the file */
  if (!gst_afparse_open_file (afparse)) {
    gst_bytestream_destroy ((GstByteStream *) afparse->vfile->closure);
    gst_pad_push (afparse->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
    gst_element_set_eos (GST_ELEMENT (afparse));
    return;
  }

  /* if audiofile changes the data in any way, we have to access
   * the audio data via afReadFrames. Otherwise we can just access
   * the data directly. */
  afGetSampleFormat (afparse->file, AF_DEFAULT_TRACK, &s_format, &s_width);
  afGetVirtualSampleFormat (afparse->file, AF_DEFAULT_TRACK, &v_format,
      &v_width);
  if (afGetCompression != AF_COMPRESSION_NONE
      || afGetByteOrder (afparse->file,
          AF_DEFAULT_TRACK) != afGetVirtualByteOrder (afparse->file,
          AF_DEFAULT_TRACK) || s_format != v_format || s_width != v_width) {
    bypass_afread = FALSE;
  }

  if (bypass_afread) {
    GST_DEBUG ("will bypass afReadFrames\n");
  }

  frames_to_bytes = afparse->channels * afparse->width / 8;
  frames_per_read = afparse->frames_per_read;
  bytes_per_read = frames_per_read * frames_to_bytes;

  afSeekFrame (afparse->file, AF_DEFAULT_TRACK, 0);

  if (bypass_afread) {
    GstEvent *event = NULL;
    guint32 waiting;
    guint32 got_bytes;

    do {

      got_bytes = gst_bytestream_read (bs, &buf, bytes_per_read);
      if (got_bytes == 0) {
        /* we need to check for an event. */
        gst_bytestream_get_status (bs, &waiting, &event);
        if (event && GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
          gst_pad_push (afparse->srcpad,
              GST_DATA (gst_event_new (GST_EVENT_EOS)));
          gst_element_set_eos (GST_ELEMENT (afparse));
          break;
        }
      } else {
        GST_BUFFER_TIMESTAMP (buf) = afparse->timestamp;
        gst_pad_push (afparse->srcpad, GST_DATA (buf));
        if (got_bytes != bytes_per_read) {
          /* this shouldn't happen very often */
          /* FIXME calculate the timestamps based on the fewer bytes received */

        } else {
          afparse->timestamp += frames_per_read * 1E9 / afparse->rate;
        }
      }
    }
    while (TRUE);

  } else {
    do {
      buf = gst_buffer_new_and_alloc (bytes_per_read);
      GST_BUFFER_TIMESTAMP (buf) = afparse->timestamp;
      data = GST_BUFFER_DATA (buf);
      numframes =
          afReadFrames (afparse->file, AF_DEFAULT_TRACK, data, frames_per_read);

      /* events are handled in gst_afparse_vf_read so if there are no
       * frames it must be EOS */
      if (numframes < 1) {
        gst_buffer_unref (buf);

        gst_pad_push (afparse->srcpad,
            GST_DATA (gst_event_new (GST_EVENT_EOS)));
        gst_element_set_eos (GST_ELEMENT (afparse));
        break;
      }
      GST_BUFFER_SIZE (buf) = numframes * frames_to_bytes;
      gst_pad_push (afparse->srcpad, GST_DATA (buf));
      afparse->timestamp += numframes * 1E9 / afparse->rate;
    }
    while (TRUE);
  }
  gst_afparse_close_file (afparse);

  gst_bytestream_destroy ((GstByteStream *) afparse->vfile->closure);

}


static void
gst_afparse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAFParse *afparse;

  /* it's not null if we got it, but it might not be ours */
  afparse = GST_AFPARSE (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_afparse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAFParse *afparse;

  g_return_if_fail (GST_IS_AFPARSE (object));

  afparse = GST_AFPARSE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_afparse_plugin_init (GstPlugin * plugin)
{
  /* load audio support library */
  if (!gst_library_load ("gstaudio"))
    return FALSE;

  if (!gst_element_register (plugin, "afparse", GST_RANK_NONE,
          GST_TYPE_AFPARSE))
    return FALSE;

  return TRUE;
}

/* this is where we open the audiofile */
static gboolean
gst_afparse_open_file (GstAFParse * afparse)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (afparse, GST_AFPARSE_OPEN), FALSE);


  /* open the file */
  GST_DEBUG ("opening vfile %p\n", afparse->vfile);
  afparse->file = afOpenVirtualFile (afparse->vfile, "r", AF_NULL_FILESETUP);
  if (afparse->file == AF_NULL_FILEHANDLE) {
    /* this should never happen */
    g_warning ("ERROR: gstafparse: Could not open virtual file for reading\n");
    return FALSE;
  }

  GST_DEBUG ("vfile opened\n");
  /* get the audiofile audio parameters */
  {
    int sampleFormat, sampleWidth;

    afparse->channels = afGetChannels (afparse->file, AF_DEFAULT_TRACK);
    afGetSampleFormat (afparse->file, AF_DEFAULT_TRACK,
        &sampleFormat, &sampleWidth);
    switch (sampleFormat) {
      case AF_SAMPFMT_TWOSCOMP:
        afparse->is_signed = TRUE;
        break;
      case AF_SAMPFMT_UNSIGNED:
        afparse->is_signed = FALSE;
        break;
      case AF_SAMPFMT_FLOAT:
      case AF_SAMPFMT_DOUBLE:
        GST_DEBUG ("ERROR: float data not supported yet !\n");
    }
    afparse->rate = (guint) afGetRate (afparse->file, AF_DEFAULT_TRACK);
    afparse->width = sampleWidth;
    GST_DEBUG ("input file: %d channels, %d width, %d rate, signed %s\n",
        afparse->channels, afparse->width, afparse->rate,
        afparse->is_signed ? "yes" : "no");
  }

  /* set caps on src */
  /*FIXME: add all the possible formats, especially float ! */
  gst_pad_set_explicit_caps (afparse->srcpad,
      gst_caps_new_simple ("audio/x-raw-int",
          "endianness", G_TYPE_INT, G_BYTE_ORDER,
          "signed", G_TYPE_BOOLEAN, afparse->is_signed,
          "width", G_TYPE_INT, afparse->width,
          "depth", G_TYPE_INT, afparse->width,
          "rate", G_TYPE_INT, afparse->rate,
          "channels", G_TYPE_INT, afparse->channels, NULL));

  GST_FLAG_SET (afparse, GST_AFPARSE_OPEN);

  return TRUE;
}

static void
gst_afparse_close_file (GstAFParse * afparse)
{
  g_return_if_fail (GST_FLAG_IS_SET (afparse, GST_AFPARSE_OPEN));
  if (afCloseFile (afparse->file) != 0) {
    g_warning ("afparse: oops, error closing !\n");
  } else {
    GST_FLAG_UNSET (afparse, GST_AFPARSE_OPEN);
  }
}

static ssize_t
gst_afparse_vf_read (AFvirtualfile * vfile, void *data, size_t nbytes)
{
  GstByteStream *bs = (GstByteStream *) vfile->closure;
  guint8 *bytes = NULL;
  GstEvent *event = NULL;
  guint32 waiting;
  guint32 got_bytes;

  /*gchar        *debug_str; */

  got_bytes = gst_bytestream_peek_bytes (bs, &bytes, nbytes);

  while (got_bytes != nbytes) {
    /* handle events */
    gst_bytestream_get_status (bs, &waiting, &event);

    /* FIXME this event handling isn't right yet */
    if (!event) {
      /*g_print("no event found with %u bytes\n", got_bytes); */
      return 0;
    }
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        return 0;
      case GST_EVENT_FLUSH:
        GST_DEBUG ("flush");
        break;
      case GST_EVENT_DISCONTINUOUS:
        GST_DEBUG ("seek done");
        got_bytes = gst_bytestream_peek_bytes (bs, &bytes, nbytes);
        break;
      default:
        g_warning ("unknown event %d", GST_EVENT_TYPE (event));
        got_bytes = gst_bytestream_peek_bytes (bs, &bytes, nbytes);
    }
  }

  memcpy (data, bytes, got_bytes);
  gst_bytestream_flush_fast (bs, got_bytes);

  /*  debug_str = g_strndup((gchar*)bytes, got_bytes);
     g_print("read %u bytes: %s\n", got_bytes, debug_str);
   */
  return got_bytes;
}

static long
gst_afparse_vf_seek (AFvirtualfile * vfile, long offset, int is_relative)
{
  GstByteStream *bs = (GstByteStream *) vfile->closure;
  GstSeekType method;
  guint64 current_offset = gst_bytestream_tell (bs);

  if (!is_relative) {
    if ((guint64) offset == current_offset) {
      /* this seems to happen before every read - bad audiofile */
      return offset;
    }

    method = GST_SEEK_METHOD_SET;
  } else {
    if (offset == 0)
      return current_offset;
    method = GST_SEEK_METHOD_CUR;
  }

  if (gst_bytestream_seek (bs, (gint64) offset, method)) {
    GST_DEBUG ("doing seek to %d", (gint) offset);
    return offset;
  }
  return 0;
}

static long
gst_afparse_vf_length (AFvirtualfile * vfile)
{
  GstByteStream *bs = (GstByteStream *) vfile->closure;
  guint64 length;

  length = gst_bytestream_length (bs);
  GST_DEBUG ("doing length: %" G_GUINT64_FORMAT, length);
  return length;
}

static ssize_t
gst_afparse_vf_write (AFvirtualfile * vfile, const void *data, size_t nbytes)
{
  /* GstByteStream *bs = (GstByteStream*)vfile->closure; */
  g_warning ("shouldn't write to a readonly pad");
  return 0;
}

static void
gst_afparse_vf_destroy (AFvirtualfile * vfile)
{
  /* GstByteStream *bs = (GstByteStream*)vfile->closure; */

  GST_DEBUG ("doing destroy");
}

static long
gst_afparse_vf_tell (AFvirtualfile * vfile)
{
  GstByteStream *bs = (GstByteStream *) vfile->closure;
  guint64 offset;

  offset = gst_bytestream_tell (bs);
  GST_DEBUG ("doing tell: %" G_GUINT64_FORMAT, offset);
  return offset;
}
