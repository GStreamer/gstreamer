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


#include <gst/gst.h>
#include <gst/audio/audio.h>
#include "gstafparse.h"


static GstElementDetails afparse_details = {
  "Audiofile Parse",
  "Src",
  "Audiofile parser for audio/raw",
  VERSION,
  "Steve Baker <stevebaker_org@yahoo.co.uk>",
  "(C) 2002"
};


/* AFParse signals and args */
enum {
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum {
  ARG_0,
};

/* added a src factory function to force audio/raw MIME type */
GST_PAD_TEMPLATE_FACTORY (afparse_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (  
    "audiofile_src",
    "audio/raw",
      "format",             GST_PROPS_STRING ("int"),
        "law",              GST_PROPS_INT (0),
        "endianness",       GST_PROPS_INT (G_BYTE_ORDER),
        "signed",           GST_PROPS_LIST (GST_PROPS_BOOLEAN (TRUE), GST_PROPS_BOOLEAN (FALSE)),
        "width",            GST_PROPS_INT_RANGE (8, 16),
        "depth",            GST_PROPS_INT_RANGE (8, 16),
        "rate",             GST_PROPS_INT_RANGE (1, G_MAXINT),
        "channels",         GST_PROPS_INT_RANGE (1, 2)
  )
)

GST_PAD_TEMPLATE_FACTORY (afparse_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "afparse_sink",
    "audio/audiofile",
    NULL
  )
)

static void gst_afparse_class_init(GstAFParseClass *klass);
static void gst_afparse_init (GstAFParse *afparse);

static gboolean gst_afparse_open_file(GstAFParse *afparse);
static void  gst_afparse_close_file(GstAFParse *afparse);

static void gst_afparse_loop(GstElement *element);
static void gst_afparse_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_afparse_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static ssize_t gst_afparse_vf_read (AFvirtualfile *vfile, void *data, size_t nbytes);
static long gst_afparse_vf_length (AFvirtualfile *vfile);
static ssize_t gst_afparse_vf_write (AFvirtualfile *vfile, const void *data, size_t nbytes);
static void gst_afparse_vf_destroy(AFvirtualfile *vfile);
static long gst_afparse_vf_seek   (AFvirtualfile *vfile, long offset, int is_relative);
static long gst_afparse_vf_tell   (AFvirtualfile *vfile);

static GstCaps* gst_afparse_type_find(GstBuffer *buf, gpointer private);
static GstElementStateReturn gst_afparse_change_state (GstElement *element);

static GstElementClass *parent_class = NULL;

static GstTypeDefinition aftype_definitions[] = {
  { "aftypes audio/audiofile", "audio/audiofile", ".wav .aiff .aif .aifc", gst_afparse_type_find },
  { NULL, NULL, NULL, NULL },
};

GType
gst_afparse_get_type (void) 
{
  static GType afparse_type = 0;

  if (!afparse_type) {
    static const GTypeInfo afparse_info = {
      sizeof (GstAFParseClass),      NULL,
      NULL,
      (GClassInitFunc) gst_afparse_class_init,
      NULL,
      NULL,
      sizeof (GstAFParse),
      0,
      (GInstanceInitFunc) gst_afparse_init,
    };
    afparse_type = g_type_register_static (GST_TYPE_ELEMENT, "GstAFParse", &afparse_info, 0);
  }
  return afparse_type;
}

static void
gst_afparse_class_init (GstAFParseClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  gobject_class->set_property = gst_afparse_set_property;
  gobject_class->get_property = gst_afparse_get_property;

  gstelement_class->change_state = gst_afparse_change_state;
}

static void 
gst_afparse_init (GstAFParse *afparse) 
{
  afparse->srcpad = gst_pad_new_from_template (afparse_src_factory (), "src");
  gst_element_add_pad (GST_ELEMENT (afparse), afparse->srcpad);

  afparse->sinkpad = gst_pad_new_from_template (afparse_sink_factory (), "sink");
  gst_element_add_pad (GST_ELEMENT (afparse), afparse->sinkpad);

  gst_element_set_loop_function (GST_ELEMENT (afparse), gst_afparse_loop);

  afparse->vfile = af_virtual_file_new();
  afparse->vfile->closure = NULL;
  afparse->vfile->read = gst_afparse_vf_read;
  afparse->vfile->length = gst_afparse_vf_length;
  afparse->vfile->write = gst_afparse_vf_write;
  afparse->vfile->destroy = gst_afparse_vf_destroy;
  afparse->vfile->seek = gst_afparse_vf_seek;
  afparse->vfile->tell = gst_afparse_vf_tell;

  /* default to low latency */
  afparse->frames_per_read = 64;
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
gst_afparse_loop(GstElement *element)
{
  GstAFParse *afparse;
  GstBuffer *buf;
  GstBufferPool *bufpool;
  gint numframes, frames_to_bytes, frames_per_read, bytes_per_read;
  guint8 *data;

  afparse = GST_AFPARSE(element);

  frames_to_bytes = afparse->channels * afparse->width / 8;
  frames_per_read = afparse->frames_per_read;
  bytes_per_read = frames_per_read * frames_to_bytes;
  
  bufpool = gst_buffer_pool_get_default (bytes_per_read, 8);
  
  do {
    buf = gst_buffer_new_from_pool (bufpool, 0, 0);
    GST_BUFFER_TIMESTAMP(buf) = afparse->timestamp;
    data = GST_BUFFER_DATA(buf); 
    numframes = afReadFrames (afparse->file, AF_DEFAULT_TRACK, data, frames_per_read);

    /* events are handled in gst_afparse_vf_read so if there are no
     * frames it must be EOS */
    if (numframes < 1){
      gst_buffer_unref(buf);

      gst_pad_push (afparse->srcpad, GST_BUFFER(gst_event_new (GST_EVENT_EOS)));  
      gst_element_set_eos (GST_ELEMENT (afparse));
      break;
    }

    GST_BUFFER_SIZE(buf) = numframes * frames_to_bytes;
    gst_pad_push (afparse->srcpad, buf);
    afparse->timestamp += numframes * 1E9 / afparse->rate;
  }
  while (TRUE);
  
  gst_bytestream_destroy((GstByteStream*)afparse->vfile->closure);
  gst_buffer_pool_unref(bufpool);

}

static GstBuffer *
gst_afparse_get (GstPad *pad)
{
  GstAFParse *afparse;
  GstBuffer *buf;

  glong readbytes, readframes;
  glong frameCount;

  g_return_val_if_fail (pad != NULL, NULL);
  afparse = GST_AFPARSE (gst_pad_get_parent (pad));

  buf = gst_buffer_new ();
  g_return_val_if_fail (buf, NULL);
  
  GST_BUFFER_DATA (buf) = (gpointer) g_malloc (afparse->bytes_per_read);
 
  /* calculate frameCount to read based on file info */

  frameCount = afparse->bytes_per_read / (afparse->channels * afparse->width / 8);
/*  g_print ("DEBUG: gstafparse: going to read %ld frames\n", frameCount); */
  readframes = afReadFrames (afparse->file, AF_DEFAULT_TRACK, GST_BUFFER_DATA (buf), frameCount);
  readbytes = readframes * (afparse->channels * afparse->width / 8);
  if (readbytes == 0) {
    gst_element_set_eos (GST_ELEMENT (afparse));
    return GST_BUFFER (gst_event_new (GST_EVENT_EOS));  
  }
  
  GST_BUFFER_SIZE (buf) = readbytes;
  GST_BUFFER_OFFSET (buf) = afparse->curoffset;

  afparse->curoffset += readbytes;

  afparse->timestamp += gst_audio_frame_length (afparse->srcpad, buf);
  GST_BUFFER_TIMESTAMP (buf) = afparse->timestamp * 1E9 / afparse->rate;

  return buf;
}

static void
gst_afparse_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
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
gst_afparse_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstAFParse *afparse;
 
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AFPARSE (object));
 
  afparse = GST_AFPARSE (object);
  
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  gint i=0;
  
  factory = gst_element_factory_new ("afparse", GST_TYPE_AFPARSE,
                                    &afparse_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (afparse_src_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (afparse_sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  while (aftype_definitions[i].name) {
    GstTypeFactory *type;

    type = gst_type_factory_new (&aftype_definitions[i]);
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));
    i++;
  }
    
  /* load audio support library */
  if (!gst_library_load ("gstaudio"))
  {
    gst_info ("gstafparse/sink: could not load support library: 'gstaudio'\n");
    return FALSE;
  }
  if (!gst_library_load ("gstbytestream"))
  {
    gst_info ("gstafparse/sink: could not load support library: 'gstbytestream'\n");
    return FALSE;
  }
  return TRUE;
}


GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "afparse",
  plugin_init
};

/* this is where we open the audiofile */
static gboolean
gst_afparse_open_file (GstAFParse *afparse)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (afparse, GST_AFPARSE_OPEN), FALSE);

  afparse->vfile->closure = gst_bytestream_new(afparse->srcpad);

  /* open the file */
  g_print("opening vfile %p\n", afparse->vfile);
  afparse->file = afOpenVirtualFile (afparse->vfile, "r", AF_NULL_FILESETUP);
  if (afparse->file == AF_NULL_FILEHANDLE)
  {
    /* this should never happen */
    g_warning ("ERROR: gstafparse: Could not open virtual file for reading\n");
    return FALSE;
  }

  g_print("vfile opened\n");
  /* get the audiofile audio parameters */
  {
    int sampleFormat, sampleWidth;
    afparse->channels = afGetChannels (afparse->file, AF_DEFAULT_TRACK);
    afGetSampleFormat (afparse->file, AF_DEFAULT_TRACK, 
    &sampleFormat, &sampleWidth);
    switch (sampleFormat)
    {
      case AF_SAMPFMT_TWOSCOMP:
        afparse->is_signed = TRUE;
        break;
      case AF_SAMPFMT_UNSIGNED:
        afparse->is_signed = FALSE;
        break;
      case AF_SAMPFMT_FLOAT:
      case AF_SAMPFMT_DOUBLE:
        GST_DEBUG (GST_CAT_PLUGIN_INFO, "ERROR: float data not supported yet !\n");
    }
    afparse->rate = (guint) afGetRate (afparse->file, AF_DEFAULT_TRACK);
    afparse->width = sampleWidth;
    GST_DEBUG (GST_CAT_PLUGIN_INFO, 
       "input file: %d channels, %d width, %d rate, signed %s\n",
        afparse->channels, afparse->width, afparse->rate,
        afparse->is_signed ? "yes" : "no");
  }
  
  /* set caps on src */
  /*FIXME: add all the possible formats, especially float ! */ 
  gst_pad_try_set_caps (afparse->srcpad, 
    GST_CAPS_NEW (
      "af_src",
      "audio/raw",
      "format",     GST_PROPS_STRING ("int"),
      "law",        GST_PROPS_INT (0),              /*FIXME */
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),   /*FIXME */
      "signed",     GST_PROPS_BOOLEAN (afparse->is_signed),
      "width",      GST_PROPS_INT (afparse->width),
      "depth",      GST_PROPS_INT (afparse->width),
      "rate",       GST_PROPS_INT (afparse->rate),
      "channels",   GST_PROPS_INT (afparse->channels)
    )
  );

  GST_FLAG_SET (afparse, GST_AFPARSE_OPEN);

  return TRUE;
}

static void
gst_afparse_close_file (GstAFParse *afparse)
{
  g_return_if_fail (GST_FLAG_IS_SET (afparse, GST_AFPARSE_OPEN));
  if (afCloseFile (afparse->file) != 0)
  {
    g_warning ("afparse: oops, error closing !\n");
  }
  else {
    GST_FLAG_UNSET (afparse, GST_AFPARSE_OPEN);
  }
}

static GstElementStateReturn
gst_afparse_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_AFPARSE (element), GST_STATE_FAILURE);

  /* if going to NULL then close the file */
  if (GST_STATE_PENDING (element) == GST_STATE_NULL) 
  {
/*    printf ("DEBUG: afparse state change: null pending\n"); */
    if (GST_FLAG_IS_SET (element, GST_AFPARSE_OPEN))
    {
/*      g_print ("DEBUG: trying to close the src file\n"); */
      gst_afparse_close_file (GST_AFPARSE (element));
    }
  } 
  else if (GST_STATE_PENDING (element) == GST_STATE_READY) 
  {
/*    g_print ("DEBUG: afparse: ready state pending.  This shouldn't happen at the *end* of a stream\n"); */
    if (!GST_FLAG_IS_SET (element, GST_AFPARSE_OPEN)) 
    {
/*      g_print ("DEBUG: GST_AFPARSE_OPEN not set\n"); */
      if (!gst_afparse_open_file (GST_AFPARSE (element)))
      {
/*        g_print ("DEBUG: element tries to open file\n"); */
        return GST_STATE_FAILURE;
      }
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static ssize_t 
gst_afparse_vf_read (AFvirtualfile *vfile, void *data, size_t nbytes)
{
  GstByteStream *bs = (GstByteStream*)vfile->closure;
  guint8 *bytes;
  g_print("doing read\n");
  bytes = gst_bytestream_peek_bytes(bs, nbytes);
  if (!bytes){
    /* handle events */

    return 0;
  }
  
  gst_bytestream_flush_fast(bs, nbytes);
  data = bytes;
  return nbytes;
}

static long 
gst_afparse_vf_seek   (AFvirtualfile *vfile, long offset, int is_relative)
{
  GstByteStream *bs = (GstByteStream*)vfile->closure;
  GstSeekType type;
  
  g_print("doing seek\n");
  type = is_relative ? GST_SEEK_BYTEOFFSET_CUR : GST_SEEK_BYTEOFFSET_SET;
  if (gst_bytestream_seek(bs, type, (gint64)offset)){
    return offset;
  }
  return 0;
}

static long 
gst_afparse_vf_length (AFvirtualfile *vfile)
{
  /*GstByteStream *bs = (GstByteStream*)vfile->closure;*/
  /* FIXME there is currently no practical way to do this.
   * wait for the events rewrite to drop */
  g_warning("cannot get length at the moment");
  return G_MAXLONG;
}

static ssize_t 
gst_afparse_vf_write (AFvirtualfile *vfile, const void *data, size_t nbytes)
{
  /* GstByteStream *bs = (GstByteStream*)vfile->closure;*/
  g_warning("shouldn't write to a readonly pad");
  return 0;
}

static void 
gst_afparse_vf_destroy(AFvirtualfile *vfile)
{
  /* GstByteStream *bs = (GstByteStream*)vfile->closure;*/

  g_print("doing destroy\n");
}

static long 
gst_afparse_vf_tell   (AFvirtualfile *vfile)
{
  /*GstByteStream *bs = (GstByteStream*)vfile->closure;*/
  g_print("doing tell\n");
  /* return gst_bytestream_tell(bs);*/
  return 0;
}

static GstCaps* 
gst_afparse_type_find(GstBuffer *buf, gpointer private)
{
  
  return NULL;
}

