/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstafsink.c: 
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
#include "gstafsink.h"

/* elementfactory information */
static GstElementDetails afsink_details = {
  "Audiofile Sink",
  "Sink/Audio",
  "Write audio streams to disk using libaudiofile",
  "Thomas <thomas@apestaart.org>",
};


/* AFSink signals and args */
enum {
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_TYPE,
  ARG_OUTPUT_ENDIANNESS,
  ARG_LOCATION
};

/* added a sink factory function to force audio/raw MIME type */
/* I think the caps can be broader, we need to change that somehow */
static GstStaticPadTemplate afsink_sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("audio/x-raw-int, "
     "rate = (int) [ 1, MAX ], "
     "channels = (int) [ 1, 2 ], "
     "endianness = (int) BYTE_ORDER, "
     "width = (int) { 8, 16 }, "
     "depth = (int) { 8, 16 }, "
     "signed = (boolean) { true, false }, "
     "buffer-frames = (int) [ 1, MAX ]"
  )
);

/* we use an enum for the output type arg */

#define GST_TYPE_AFSINK_TYPES (gst_afsink_types_get_type())
/* FIXME: fix the string ints to be string-converted from the audiofile.h types */
static GType
gst_afsink_types_get_type (void) 
{
  static GType afsink_types_type = 0;
  static GEnumValue afsink_types[] = {
    {AF_FILE_RAWDATA, "0", "raw PCM"},
    {AF_FILE_AIFFC,   "1", "AIFFC"},
    {AF_FILE_AIFF,    "2", "AIFF"},
    {AF_FILE_NEXTSND, "3", "Next/SND"},
    {AF_FILE_WAVE,    "4", "Wave"},
    {0, NULL, NULL},
  };
  
  if (!afsink_types_type) 
  {
    afsink_types_type = g_enum_register_static ("GstAudiosinkTypes", afsink_types);
  }
  return afsink_types_type;
}

static void             gst_afsink_base_init    (gpointer g_class);
static void		gst_afsink_class_init	(GstAFSinkClass *klass);
static void		gst_afsink_init		(GstAFSink *afsink);

static gboolean 	gst_afsink_open_file 	(GstAFSink *sink);
static void 		gst_afsink_close_file 	(GstAFSink *sink);

static void		gst_afsink_chain	(GstPad *pad,GstData *_data);

static void		gst_afsink_set_property	(GObject *object, guint prop_id, const GValue *value, 
						 GParamSpec *pspec);
static void		gst_afsink_get_property	(GObject *object, guint prop_id, GValue *value, 
						 GParamSpec *pspec);

static gboolean		gst_afsink_handle_event (GstPad *pad, GstEvent *event);

static GstElementStateReturn gst_afsink_change_state (GstElement *element);

static GstElementClass *parent_class = NULL;
static guint gst_afsink_signals[LAST_SIGNAL] = { 0 };

GType
gst_afsink_get_type (void) 
{
  static GType afsink_type = 0;

  if (!afsink_type) {
    static const GTypeInfo afsink_info = {
      sizeof (GstAFSinkClass),
      gst_afsink_base_init,
      NULL,
      (GClassInitFunc) gst_afsink_class_init,
      NULL,
      NULL,
      sizeof (GstAFSink),
      0,
      (GInstanceInitFunc) gst_afsink_init,
    };
    afsink_type = g_type_register_static (GST_TYPE_ELEMENT, "GstAFSink", &afsink_info, 0);
  }
  return afsink_type;
}

static void
gst_afsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&afsink_sink_factory));
  gst_element_class_set_details (element_class, &afsink_details);
}

static void
gst_afsink_class_init (GstAFSinkClass *klass) 
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

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_TYPE,
    g_param_spec_enum("type","type","type",
                      GST_TYPE_AFSINK_TYPES,0,G_PARAM_READWRITE)); /* CHECKME! */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_OUTPUT_ENDIANNESS,
    g_param_spec_int("endianness","endianness","endianness",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
 
  gst_afsink_signals[SIGNAL_HANDOFF] =
    g_signal_new ("handoff", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstAFSinkClass, handoff), NULL, NULL,
                   g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);


  gobject_class->set_property = gst_afsink_set_property;
  gobject_class->get_property = gst_afsink_get_property;

  gstelement_class->change_state = gst_afsink_change_state;
}

static void 
gst_afsink_init (GstAFSink *afsink) 
{
  /* GstPad *pad;   this is now done in the struct */

  afsink->sinkpad = gst_pad_new_from_template (
      gst_element_get_pad_template (GST_ELEMENT (afsink), "sink"), "sink");
  gst_element_add_pad (GST_ELEMENT (afsink), afsink->sinkpad);

  gst_pad_set_chain_function (afsink->sinkpad, gst_afsink_chain);
  gst_pad_set_event_function (afsink->sinkpad, gst_afsink_handle_event);

  afsink->filename = NULL;
  afsink->file = NULL;
  /* default values, should never be needed */
  afsink->channels = 2;
  afsink->width = 16;
  afsink->rate = 44100;
  afsink->type = AF_FILE_WAVE;
  afsink->endianness_data = 1234;
  afsink->endianness_wanted = 1234;
}

static void
gst_afsink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstAFSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = GST_AFSINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      /* the element must be stopped or paused in order to do this */
      g_return_if_fail ((GST_STATE (sink) < GST_STATE_PLAYING)
                      || (GST_STATE (sink) == GST_STATE_PAUSED));
      if (sink->filename)
	g_free (sink->filename);
      sink->filename = g_strdup (g_value_get_string (value));
      if ( (GST_STATE (sink) == GST_STATE_PAUSED)
        && (sink->filename != NULL))
      {
              gst_afsink_close_file (sink);
              gst_afsink_open_file (sink);
      }

      break;
    case ARG_TYPE:
      sink->type = g_value_get_enum (value);
      break;
    case ARG_OUTPUT_ENDIANNESS:
      {
        int end = g_value_get_int (value);
        if (end == 1234 || end == 4321)
          sink->endianness_output = end;
      }
      break;
    default:
      break;
  }
}

static void   
gst_afsink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstAFSink *sink;
 
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AFSINK (object));
 
  sink = GST_AFSINK (object);
  
  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, sink->filename);
      break;
    case ARG_TYPE:
      g_value_set_enum (value, sink->type);
      break;
    case ARG_OUTPUT_ENDIANNESS:
      g_value_set_int (value, sink->endianness_output);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_afsink_plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "afsink", GST_RANK_NONE, GST_TYPE_AFSINK))
    return FALSE;
  
  return TRUE;
}

/* this is where we open the audiofile */
static gboolean
gst_afsink_open_file (GstAFSink *sink)
{
  AFfilesetup outfilesetup;
  const GstCaps *caps;
  GstStructure *structure;
  int sample_format;			/* audiofile's sample format, look in audiofile.h */
  int byte_order = 0;			/* audiofile's byte order defines */
  
  g_return_val_if_fail (!GST_FLAG_IS_SET (sink, GST_AFSINK_OPEN), FALSE);

  /* open the file */
/* we use audiofile now
  sink->file = fopen (sink->filename, "w");
  if (sink->file == NULL) {
    perror ("open");
    gst_element_error (GST_ELEMENT (sink), g_strconcat("opening file \"", sink->filename, "\"", NULL));
    return FALSE;
  } 
*/

  /* get the audio parameters */
  g_return_val_if_fail (GST_IS_PAD (sink->sinkpad), FALSE);
  caps = GST_PAD_CAPS (sink->sinkpad);
  
  if (caps == NULL) {
    g_critical ("gstafsink chain : Could not get caps of pad !\n");
  } else {
    structure = gst_caps_get_structure (caps, 0);
    gst_structure_get_int (structure, "channels",   &sink->channels);
    gst_structure_get_int (structure, "width",      &sink->width);
    gst_structure_get_int (structure, "rate",       &sink->rate);
    gst_structure_get_boolean (structure, "signed", &sink->is_signed);
    gst_structure_get_int (structure, "endianness", &sink->endianness_data);
  }
  GST_DEBUG ("channels %d, width %d, rate %d, signed %s",
  	   		sink->channels, sink->width, sink->rate,
  	   		sink->is_signed ? "yes" : "no");
  GST_DEBUG ("endianness: data %d, output %d", 
	   sink->endianness_data, sink->endianness_output);
  /* setup the output file */
  if (sink->is_signed)
    sample_format = AF_SAMPFMT_TWOSCOMP;
  else
    sample_format = AF_SAMPFMT_UNSIGNED;
  /* FIXME : this check didn't seem to work, so let the output endianness be set */
  /*
  if (sink->endianness_data == sink->endianness_wanted)
    byte_order = AF_BYTEORDER_LITTLEENDIAN;
  else
    byte_order = AF_BYTEORDER_BIGENDIAN;
  */
  if (sink->endianness_output == 1234)
    byte_order = AF_BYTEORDER_LITTLEENDIAN;
  else
    byte_order = AF_BYTEORDER_BIGENDIAN;

  outfilesetup = afNewFileSetup ();
  afInitFileFormat (outfilesetup, sink->type);
  afInitChannels (outfilesetup, AF_DEFAULT_TRACK, sink->channels);
  afInitRate (outfilesetup, AF_DEFAULT_TRACK, sink->rate);
  afInitSampleFormat (outfilesetup, AF_DEFAULT_TRACK, 
  					  sample_format, sink->width);

  /* open it */
  sink->file = afOpenFile (sink->filename, "w", outfilesetup);
  if (sink->file == AF_NULL_FILEHANDLE)
  {
    perror ("open");
    gst_element_error (GST_ELEMENT (sink), g_strconcat("opening file \"", sink->filename, "\"", NULL));
    return FALSE;
  } 

  afFreeFileSetup (outfilesetup);
/*  afSetVirtualByteOrder (sink->file, AF_DEFAULT_TRACK, byte_order); */
  
  GST_FLAG_SET (sink, GST_AFSINK_OPEN);

  return TRUE;
}

static void
gst_afsink_close_file (GstAFSink *sink)
{
/*  g_print ("DEBUG: closing sinkfile...\n"); */
  g_return_if_fail (GST_FLAG_IS_SET (sink, GST_AFSINK_OPEN));
/*  g_print ("DEBUG: past flag test\n"); */
/*  if (fclose (sink->file) != 0) */
  if (afCloseFile (sink->file) != 0)
  {
    g_print ("WARNING: afsink: oops, error closing !\n");
    perror ("close");
    gst_element_error (GST_ELEMENT (sink), g_strconcat("closing file \"", sink->filename, "\"", NULL));
  }
  else {
    GST_FLAG_UNSET (sink, GST_AFSINK_OPEN);
  }
}

/**
 * gst_afsink_chain:
 * @pad: the pad this afsink is connected to
 * @buf: the buffer that has to be absorbed
 *
 * take the buffer from the pad and write to file if it's open
 */
static void 
gst_afsink_chain (GstPad *pad, GstData *_data) 
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstAFSink *afsink;
  int ret = 0;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  afsink = GST_AFSINK (gst_pad_get_parent (pad));
/* we use audiofile now
  if (GST_FLAG_IS_SET (afsink, GST_AFSINK_OPEN))
  {
    bytes_written = fwrite (GST_BUFFER_DATA (buf), 1, GST_BUFFER_SIZE (buf), afsink->file);
    if (bytes_written < GST_BUFFER_SIZE (buf))
    {
      printf ("afsink : Warning : %d bytes should be written, only %d bytes written\n",
      		  GST_BUFFER_SIZE (buf), bytes_written);
    }
  }
*/

  if (!GST_FLAG_IS_SET (afsink, GST_AFSINK_OPEN))
  {
    /* it's not open yet, open it */
    if (!gst_afsink_open_file (afsink))
          g_print ("WARNING: gstafsink: can't open file !\n");
/*        return FALSE;    Can't return value */
  }

  if (GST_FLAG_IS_SET (afsink, GST_AFSINK_OPEN))
  {
    int frameCount = 0;

    frameCount = GST_BUFFER_SIZE (buf) / ((afsink->width / 8) * afsink->channels);
 /*   g_print ("DEBUG: writing %d frames ", frameCount); */
    ret = afWriteFrames (afsink->file, AF_DEFAULT_TRACK, 
	                 GST_BUFFER_DATA (buf), frameCount);
    if (ret == AF_BAD_WRITE || ret == AF_BAD_LSEEK)
    {
      printf ("afsink : Warning : afWriteFrames returned an error (%d)\n", ret);
    }
  }

  gst_buffer_unref (buf);

  g_signal_emit (G_OBJECT (afsink), gst_afsink_signals[SIGNAL_HANDOFF], 0);
}

static GstElementStateReturn
gst_afsink_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_AFSINK (element), GST_STATE_FAILURE);

  /* if going to NULL? then close the file */
  if (GST_STATE_PENDING (element) == GST_STATE_NULL) 
  {
/*    printf ("DEBUG: afsink state change: null pending\n"); */
    if (GST_FLAG_IS_SET (element, GST_AFSINK_OPEN))
    {
/*      g_print ("DEBUG: trying to close the sink file\n"); */
      gst_afsink_close_file (GST_AFSINK (element));
    }
  } 
/*

  else
  this has been moved to the chain function, since it's only then that 
 the caps are set and can be known 
 {
    g_print ("DEBUG: it's not going to null\n"); 
    if (!GST_FLAG_IS_SET (element, GST_AFSINK_OPEN)) 
    {
      g_print ("DEBUG: GST_AFSINK_OPEN not set\n"); 
      if (!gst_afsink_open_file (GST_AFSINK (element)))
      {
        g_print ("DEBUG: element tries to open file\n"); 
        return GST_STATE_FAILURE;
      }
    }
  }
*/

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

/* this function was copied from sinesrc */

static gboolean
gst_afsink_handle_event (GstPad *pad, GstEvent *event)
{
  GstAFSink *afsink;

  afsink = GST_AFSINK (gst_pad_get_parent (pad));
  GST_DEBUG ("DEBUG: afsink: got event");
  gst_afsink_close_file (afsink);

  return TRUE;
}

/*
gboolean
gst_afsink_factory_init (GstElementFactory *factory)
{
  GstPadTemplate *sink_pt;
  sink_pt = afsink_sink_factory();
  gst_element_factory_add_pad_template (factory, sink_pt);
    
  return TRUE;  

}
*/

