/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfilesink.c: 
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
#include <errno.h>
#include "gstfilesink.h"
#include <string.h>

GstElementDetails gst_filesink_details = {
  "File Sink",
  "Sink/File",
  "Write stream to a file",
  VERSION,
  "Thomas <thomas@apestaart.org>",
  "(C) 2001"
};


/* FileSink signals and args */
enum {
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION,
  ARG_MAXFILESIZE,
};


static void	gst_filesink_class_init		(GstFileSinkClass *klass);
static void	gst_filesink_init		(GstFileSink *filesink);

static void	gst_filesink_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void	gst_filesink_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static gboolean gst_filesink_open_file 		(GstFileSink *sink);
static void 	gst_filesink_close_file 	(GstFileSink *sink);

static gboolean gst_filesink_handle_event       (GstPad *pad, GstEvent *event);
static void	gst_filesink_chain		(GstPad *pad,GstBuffer *buf);

static GstElementStateReturn gst_filesink_change_state (GstElement *element);

static GstElementClass *parent_class = NULL;
static guint gst_filesink_signals[LAST_SIGNAL] = { 0 };

GType
gst_filesink_get_type (void) 
{
  static GType filesink_type = 0;

  if (!filesink_type) {
    static const GTypeInfo filesink_info = {
      sizeof(GstFileSinkClass),      NULL,
      NULL,
      (GClassInitFunc)gst_filesink_class_init,
      NULL,
      NULL,
      sizeof(GstFileSink),
      0,
      (GInstanceInitFunc)gst_filesink_init,
    };
    filesink_type = g_type_register_static (GST_TYPE_ELEMENT, "GstFileSink", &filesink_info, 0);
  }
  return filesink_type;
}

static void
gst_filesink_class_init (GstFileSinkClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gst_element_class_install_std_props (
	  GST_ELEMENT_CLASS (klass),
	  "location", ARG_LOCATION, G_PARAM_READWRITE,
	  NULL);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MAXFILESIZE,
    g_param_spec_int("maxfilesize","MaxFileSize","Maximum Size Per File",
    G_MININT,G_MAXINT,0,G_PARAM_READWRITE));

  gst_filesink_signals[SIGNAL_HANDOFF] =
    g_signal_new ("handoff", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstFileSinkClass, handoff), NULL, NULL,
                    g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gobject_class->set_property = gst_filesink_set_property;
  gobject_class->get_property = gst_filesink_get_property;

  gstelement_class->change_state = gst_filesink_change_state;
}

static void 
gst_filesink_init (GstFileSink *filesink) 
{
  GstPad *pad;

  pad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (filesink), pad);
  gst_pad_set_chain_function (pad, gst_filesink_chain);

  GST_FLAG_SET (GST_ELEMENT(filesink), GST_ELEMENT_EVENT_AWARE);
  gst_pad_set_event_function(pad, gst_filesink_handle_event);

  filesink->filename = NULL;
  filesink->file = NULL;
  filesink->filenum = 0;

  filesink->maxfilesize = -1;
}

static char *
gst_filesink_getcurrentfilename (GstFileSink *filesink)
{
  g_return_val_if_fail(filesink != NULL, NULL);
  g_return_val_if_fail(GST_IS_FILESINK(filesink), NULL);
  if (filesink->filename == NULL) return NULL;
  g_return_val_if_fail(filesink->filenum >= 0, NULL);

  if (!strstr(filesink->filename, "%"))
  {
    if (!filesink->filenum)
      return g_strdup(filesink->filename);
    else
      return NULL;
  }

  return g_strdup_printf(filesink->filename, filesink->filenum);
}

static void
gst_filesink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstFileSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = GST_FILESINK (object);

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
              gst_filesink_close_file (sink);
              gst_filesink_open_file (sink);   
      }
 
      break;
    case ARG_MAXFILESIZE:
      sink->maxfilesize = g_value_get_int(value);
      break;
    default:
      break;
  }
}

static void   
gst_filesink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstFileSink *sink;
 
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FILESINK (object));
 
  sink = GST_FILESINK (object);
  
  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, gst_filesink_getcurrentfilename(sink));
      break;
    case ARG_MAXFILESIZE:
      g_value_set_int (value, sink->maxfilesize);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_filesink_open_file (GstFileSink *sink)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (sink, GST_FILESINK_OPEN), FALSE);

  /* open the file */
  if (!gst_filesink_getcurrentfilename(sink))
  {
    /* Out of files */
    gst_element_set_eos(GST_ELEMENT(sink));
    return FALSE;
  }
  sink->file = fopen (gst_filesink_getcurrentfilename(sink), "w");
  if (sink->file == NULL) {
    perror ("open");
    gst_element_error (GST_ELEMENT (sink), g_strconcat("Error opening file \"",
      gst_filesink_getcurrentfilename(sink), "\": ", sys_errlist[errno], NULL));
    return FALSE;
  } 

  GST_FLAG_SET (sink, GST_FILESINK_OPEN);

  sink->data_written = 0;

  return TRUE;
}

static void
gst_filesink_close_file (GstFileSink *sink)
{
  g_return_if_fail (GST_FLAG_IS_SET (sink, GST_FILESINK_OPEN));

  if (fclose (sink->file) != 0)
  {
    perror ("close");
    gst_element_error (GST_ELEMENT (sink), g_strconcat("Error closing file \"",
      gst_filesink_getcurrentfilename(sink), "\": ", sys_errlist[errno], NULL));
  }
  else {
    GST_FLAG_UNSET (sink, GST_FILESINK_OPEN);
  }
}

/* handle events (search) */
static gboolean
gst_filesink_handle_event (GstPad *pad, GstEvent *event)
{
  GstEventType type;
  GstFileSink *filesink;

  filesink = GST_FILESINK (gst_pad_get_parent (pad));

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_SEEK:
      /* we need to seek */
      if (GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH)
        if (fflush(filesink->file))
          gst_element_error(GST_ELEMENT(filesink),
            "Error flushing the buffer cache of file \'%s\' to disk: %s",
            gst_filesink_getcurrentfilename(filesink), sys_errlist[errno]);

      if (GST_EVENT_SEEK_FORMAT (event) != GST_FORMAT_BYTES) {
        g_warning("Any other then byte-offset seeking is not supported!\n");
      }

      switch (GST_EVENT_SEEK_METHOD(event))
      {
        case GST_SEEK_METHOD_SET:
          fseek(filesink->file, GST_EVENT_SEEK_OFFSET(event), SEEK_SET);
          break;
        case GST_SEEK_METHOD_CUR:
          fseek(filesink->file, GST_EVENT_SEEK_OFFSET(event), SEEK_CUR);
          break;
        case GST_SEEK_METHOD_END:
          fseek(filesink->file, GST_EVENT_SEEK_OFFSET(event), SEEK_END);
          break;
        default:
          g_warning("unkown seek method!\n");
          break;
      }
      break;
    case GST_EVENT_DISCONTINUOUS:
    {
      gint64 offset;
      
      if (gst_event_discont_get_value (event, GST_FORMAT_BYTES, &offset))
        fseek(filesink->file, offset, SEEK_SET);

      gst_event_free (event);
      break;
    }
    case GST_EVENT_NEW_MEDIA:
      /* we need to open a new file! */
      gst_filesink_close_file(filesink);
      filesink->filenum++;
      if (!gst_filesink_open_file(filesink)) return FALSE;
      break;
    case GST_EVENT_FLUSH:
      if (fflush(filesink->file))
        gst_element_error(GST_ELEMENT(filesink),
          "Error flushing the buffer cache of file \'%s\' to disk: %s",
          gst_filesink_getcurrentfilename(filesink), sys_errlist[errno]);
      break;
    default:
      gst_pad_event_default (pad, event);
      break;
  }

  return TRUE;
}

/**
 * gst_filesink_chain:
 * @pad: the pad this filesink is connected to
 * @buf: the buffer that has to be absorbed
 *
 * take the buffer from the pad and write to file if it's open
 */
static void 
gst_filesink_chain (GstPad *pad, GstBuffer *buf) 
{
  GstFileSink *filesink;
  gint bytes_written = 0;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  filesink = GST_FILESINK (gst_pad_get_parent (pad));

  if (GST_IS_EVENT(buf))
  {
    gst_filesink_handle_event(pad, GST_EVENT(buf));
    return;
  }

  if (filesink->maxfilesize > 0)
  {
    if ((filesink->data_written + GST_BUFFER_SIZE(buf))/(1024*1024) > filesink->maxfilesize)
    {
      if (GST_ELEMENT_IS_EVENT_AWARE(GST_ELEMENT(filesink)))
      {
        GstEvent *event;
        event = gst_event_new(GST_EVENT_NEW_MEDIA);
        gst_pad_send_event(pad, event);
      }
    }
  }

  if (GST_FLAG_IS_SET (filesink, GST_FILESINK_OPEN))
  {
    bytes_written = fwrite (GST_BUFFER_DATA (buf), 1, GST_BUFFER_SIZE (buf), filesink->file);
    if (bytes_written < GST_BUFFER_SIZE (buf))
    {
      printf ("filesink : Warning : %d bytes should be written, only %d bytes written\n",
      		  GST_BUFFER_SIZE (buf), bytes_written);
    }
  }
  filesink->data_written += GST_BUFFER_SIZE(buf);

  gst_buffer_unref (buf);

  g_signal_emit (G_OBJECT (filesink), gst_filesink_signals[SIGNAL_HANDOFF], 0,
	                      filesink);
}

static GstElementStateReturn
gst_filesink_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_FILESINK (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_FILESINK_OPEN))
      gst_filesink_close_file (GST_FILESINK (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_FILESINK_OPEN)) {
      if (!gst_filesink_open_file (GST_FILESINK (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

