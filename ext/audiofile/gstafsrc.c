/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstafsrc.c: 
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

#include "gst/gst-i18n-plugin.h"
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <string.h>
#include <errno.h>

#include "gstafsrc.h"

/* elementfactory information */
static GstElementDetails afsrc_details = {
  "Audiofile Src",
  "Source/Audio",
  "Read audio files from disk using libaudiofile",
  "Thomas <thomas@apestaart.org>",
};


/* AFSrc signals and args */
enum {
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION
};

/* added a src factory function to force audio/raw MIME type */
/* I think the caps can be broader, we need to change that somehow */
static GstStaticPadTemplate afsrc_src_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("audio/x-raw-int, "
    "rate = (int) [ 1, MAX ], "
    "channels = (int) [ 1, MAX ], "
    "endianness = (int) BYTE_ORDER, "
    "width = (int) { 8, 16 }, "
    "depth = (int) { 8, 16 }, "
    "signed = (boolean) { true, false }, "
    "buffer-frames = (int) [ 1, MAX ]"
  )
);

/* we use an enum for the output type arg */

#define GST_TYPE_AFSRC_TYPES (gst_afsrc_types_get_type())

/* FIXME: fix the string ints to be string-converted from the audiofile.h types */
/* defined but not used
static GType
gst_afsrc_types_get_type (void) 
{
  static GType afsrc_types_type = 0;
  static GEnumValue afsrc_types[] = {
    {AF_FILE_RAWDATA, "0", "raw PCM"},
    {AF_FILE_AIFFC,   "1", "AIFFC"},
    {AF_FILE_AIFF,    "2", "AIFF"},
    {AF_FILE_NEXTSND, "3", "Next/SND"},
    {AF_FILE_WAVE,    "4", "Wave"},
    {0, NULL, NULL},
  };
  
  if (!afsrc_types_type) 
  {
    afsrc_types_type = g_enum_register_static ("GstAudiosrcTypes", afsrc_types);
  }
  return afsrc_types_type;
}
*/
static void             gst_afsrc_base_init             (gpointer g_class);
static void		gst_afsrc_class_init		(GstAFSrcClass *klass);
static void		gst_afsrc_init			(GstAFSrc *afsrc);

static gboolean 	gst_afsrc_open_file 		(GstAFSrc *src);
static void 		gst_afsrc_close_file 		(GstAFSrc *src);

static GstData*	gst_afsrc_get			(GstPad *pad);

static void		gst_afsrc_set_property		(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void		gst_afsrc_get_property		(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);

static GstElementStateReturn gst_afsrc_change_state 	(GstElement *element);

static GstElementClass *parent_class = NULL;
static guint gst_afsrc_signals[LAST_SIGNAL] = { 0 };

GType
gst_afsrc_get_type (void) 
{
  static GType afsrc_type = 0;

  if (!afsrc_type) {
    static const GTypeInfo afsrc_info = {
      sizeof (GstAFSrcClass),
      gst_afsrc_base_init,
      NULL,
      (GClassInitFunc) gst_afsrc_class_init,
      NULL,
      NULL,
      sizeof (GstAFSrc),
      0,
      (GInstanceInitFunc) gst_afsrc_init,
    };
    afsrc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstAFSrc", &afsrc_info, 0);
  }
  return afsrc_type;
}

static void
gst_afsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&afsrc_src_factory));
  gst_element_class_set_details (element_class, &afsrc_details);
}

static void
gst_afsrc_class_init (GstAFSrcClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gst_element_class_install_std_props (
         GST_ELEMENT_CLASS (klass),
         "location",     ARG_LOCATION,     G_PARAM_READWRITE,
         NULL);
 
  gst_afsrc_signals[SIGNAL_HANDOFF] =
    g_signal_new ("handoff", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstAFSrcClass, handoff), NULL, NULL,
                   g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);


  gobject_class->set_property = gst_afsrc_set_property;
  gobject_class->get_property = gst_afsrc_get_property;

  gstelement_class->change_state = gst_afsrc_change_state;
}

static void 
gst_afsrc_init (GstAFSrc *afsrc) 
{
  /* no need for a template, caps are set based on file, right ? */
  afsrc->srcpad = gst_pad_new_from_template (
      gst_element_get_pad_template (GST_ELEMENT (afsrc), "src"), "src");
  gst_element_add_pad (GST_ELEMENT (afsrc), afsrc->srcpad);
  gst_pad_use_explicit_caps (afsrc->srcpad);
  gst_pad_set_get_function (afsrc->srcpad, gst_afsrc_get);

  afsrc->bytes_per_read = 4096;
  afsrc->curoffset = 0;
  afsrc->seq = 0;

  afsrc->filename = NULL;
  afsrc->file = NULL;
  /* default values, should never be needed */
  afsrc->channels = 2;
  afsrc->width = 16;
  afsrc->rate = 44100;
  afsrc->type = AF_FILE_WAVE;
  afsrc->endianness_data = 1234;
  afsrc->endianness_wanted = 1234;
  afsrc->framestamp = 0;
}

static GstData *
gst_afsrc_get (GstPad *pad)
{
  GstAFSrc *src;
  GstBuffer *buf;

  glong readbytes, readframes;
  glong frameCount;

  g_return_val_if_fail (pad != NULL, NULL);
  src = GST_AFSRC (gst_pad_get_parent (pad));

  buf = gst_buffer_new ();
  g_return_val_if_fail (buf, NULL);
  
  GST_BUFFER_DATA (buf) = (gpointer) g_malloc (src->bytes_per_read);
 
  /* calculate frameCount to read based on file info */

  frameCount = src->bytes_per_read / (src->channels * src->width / 8);
/*  g_print ("DEBUG: gstafsrc: going to read %ld frames\n", frameCount); */
  readframes = afReadFrames (src->file, AF_DEFAULT_TRACK, GST_BUFFER_DATA (buf),
 	                    frameCount);
  readbytes = readframes * (src->channels * src->width / 8);
  if (readbytes == 0) {
    gst_element_set_eos (GST_ELEMENT (src));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));  
  }
  
  GST_BUFFER_SIZE (buf) = readbytes;
  GST_BUFFER_OFFSET (buf) = src->curoffset;

  src->curoffset += readbytes;

  src->framestamp += gst_audio_frame_length (src->srcpad, buf);
  GST_BUFFER_TIMESTAMP (buf) = src->framestamp * 1E9
                             / gst_audio_frame_rate (src->srcpad);
  printf ("DEBUG: afsrc: timestamp set on output buffer: %f sec\n",
        GST_BUFFER_TIMESTAMP (buf) / 1E9);

/*  g_print("DEBUG: gstafsrc: pushed buffer of %ld bytes\n", readbytes); */
  return GST_DATA (buf);
}

static void
gst_afsrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstAFSrc *src;

  /* it's not null if we got it, but it might not be ours */
  src = GST_AFSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      if (src->filename)
	g_free (src->filename);
      src->filename = g_strdup (g_value_get_string (value));
      break;
    default:
      break;
  }
}

static void   
gst_afsrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstAFSrc *src;
 
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AFSRC (object));
 
  src = GST_AFSRC (object);
  
  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->filename);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_afsrc_plugin_init (GstPlugin *plugin)
{
  /* load audio support library */
  if (!gst_library_load ("gstaudio"))
    return FALSE;

  if (!gst_element_register (plugin, "afsrc", GST_RANK_NONE, GST_TYPE_AFSRC))
    return FALSE;

#ifdef ENABLE_NLS
  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  textdomain (GETTEXT_PACKAGE);
#endif /* ENABLE_NLS */

  return TRUE;
}


/* this is where we open the audiofile */
static gboolean
gst_afsrc_open_file (GstAFSrc *src)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (src, GST_AFSRC_OPEN), FALSE);

  /* open the file */
  src->file = afOpenFile (src->filename, "r", AF_NULL_FILESETUP);
  if (src->file == AF_NULL_FILEHANDLE)
  {
     gst_element_error (src, RESOURCE, OPEN_READ,
                        (_("Could not open file \"%s\" for reading"), src->filename),
                        ("system error: %s", strerror (errno)));
    return FALSE;
  }

  /* get the audiofile audio parameters */
  {
    int sampleFormat, sampleWidth;
    src->channels = afGetChannels (src->file, AF_DEFAULT_TRACK);
    afGetSampleFormat (src->file, AF_DEFAULT_TRACK, 
			&sampleFormat, &sampleWidth);
	switch (sampleFormat)
	{
	  case AF_SAMPFMT_TWOSCOMP:
	    src->is_signed = TRUE;
	    break;
	  case AF_SAMPFMT_UNSIGNED:
	    src->is_signed = FALSE;
	    break;
	  case AF_SAMPFMT_FLOAT:
	  case AF_SAMPFMT_DOUBLE:
	    GST_DEBUG (
	    		   "ERROR: float data not supported yet !\n");
	}
	src->rate = (guint) afGetRate (src->file, AF_DEFAULT_TRACK);		
    src->width = sampleWidth;
    GST_DEBUG (
    		   "input file: %d channels, %d width, %d rate, signed %s\n",
	  			src->channels, src->width, src->rate,
	  			src->is_signed ? "yes" : "no");
  }
  
  /* set caps on src */
  gst_pad_set_explicit_caps (src->srcpad, 
      gst_caps_new_simple ("audio/x-raw-int",
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "signed",     G_TYPE_BOOLEAN, src->is_signed,
        "width",      G_TYPE_INT, src->width,
        "depth",      G_TYPE_INT, src->width,
        "rate",       G_TYPE_INT, src->rate,
        "channels",   G_TYPE_INT, src->channels,
        NULL));

  GST_FLAG_SET (src, GST_AFSRC_OPEN);

  return TRUE;
}

static void
gst_afsrc_close_file (GstAFSrc *src)
{
/*  g_print ("DEBUG: closing srcfile...\n"); */
  g_return_if_fail (GST_FLAG_IS_SET (src, GST_AFSRC_OPEN));
/*  g_print ("DEBUG: past flag test\n"); */
/*  if (fclose (src->file) != 0) 	*/
  if (afCloseFile (src->file) != 0)
  {
    gst_element_error (src, RESOURCE, CLOSE,
                       (_("Error closing file \"%s\""), src->filename),
                       GST_ERROR_SYSTEM);
  } else {
    GST_FLAG_UNSET (src, GST_AFSRC_OPEN);
  }
}

static GstElementStateReturn
gst_afsrc_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_AFSRC (element), GST_STATE_FAILURE);

  /* if going to NULL then close the file */
  if (GST_STATE_PENDING (element) == GST_STATE_NULL) 
  {
/*    printf ("DEBUG: afsrc state change: null pending\n"); */
    if (GST_FLAG_IS_SET (element, GST_AFSRC_OPEN))
    {
/*      g_print ("DEBUG: trying to close the src file\n"); */
      gst_afsrc_close_file (GST_AFSRC (element));
    }
  } 
  else if (GST_STATE_PENDING (element) == GST_STATE_READY) 
  {
/*    g_print ("DEBUG: afsrc: ready state pending.  This shouldn't happen at the *end* of a stream\n"); */
    if (!GST_FLAG_IS_SET (element, GST_AFSRC_OPEN)) 
    {
/*      g_print ("DEBUG: GST_AFSRC_OPEN not set\n"); */
      if (!gst_afsrc_open_file (GST_AFSRC (element)))
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
