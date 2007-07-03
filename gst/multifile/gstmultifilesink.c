/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2006 Wim Taymans <wim@fluendo.com>
 *                    2006 David A. Schleef <ds@schleef.org>
 *
 * gstmultifilesink.c:
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
/**
 * SECTION:element-multifilesink
 * @short_description: write buffers to sequentially-named files
 * @see_also: #GstFileSrc
 *
 * Write incoming data to a series of files in the local file system.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstmultifilesink.h"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_multi_file_sink_debug);
#define GST_CAT_DEFAULT gst_multi_file_sink_debug

static const GstElementDetails gst_multi_file_sink_details =
GST_ELEMENT_DETAILS ("Multi-File Sink",
    "Sink/File",
    "Write stream to a file",
    "David Schleef <ds@schleef.org>");

enum
{
  ARG_0,
  ARG_LOCATION
};

static void gst_multi_file_sink_dispose (GObject * object);

static void gst_multi_file_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multi_file_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

//static gboolean gst_multi_file_sink_open_file (GstMultiFileSink * sink);
//static void gst_multi_file_sink_close_file (GstMultiFileSink * sink);

//static gboolean gst_multi_file_sink_start (GstBaseSink * sink);
//static gboolean gst_multi_file_sink_stop (GstBaseSink * sink);
//static gboolean gst_multi_file_sink_event (GstBaseSink * sink, GstEvent * event);
static GstFlowReturn gst_multi_file_sink_render (GstBaseSink * sink,
    GstBuffer * buffer);

//static gboolean gst_multi_file_sink_do_seek (GstMultiFileSink * filesink,
//    guint64 new_offset);

//static gboolean gst_multi_file_sink_query (GstPad * pad, GstQuery * query);



GST_BOILERPLATE (GstMultiFileSink, gst_multi_file_sink, GstBaseSink,
    GST_TYPE_BASE_SINK);

static void
gst_multi_file_sink_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (gst_multi_file_sink_debug, "multifilesink", 0,
      "multifilesink element");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details (gstelement_class,
      &gst_multi_file_sink_details);
}

static void
gst_multi_file_sink_class_init (GstMultiFileSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_multi_file_sink_set_property;
  gobject_class->get_property = gst_multi_file_sink_get_property;

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Location of the file to write", NULL, G_PARAM_READWRITE));

  gobject_class->dispose = gst_multi_file_sink_dispose;

  gstbasesink_class->get_times = NULL;
  //gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_multi_file_sink_start);
  //gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_multi_file_sink_stop);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_multi_file_sink_render);
  //gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_multi_file_sink_event);

  if (sizeof (off_t) < 8) {
    GST_LOG ("No large file support, sizeof (off_t) = %" G_GSIZE_FORMAT,
        sizeof (off_t));
  }
}

static void
gst_multi_file_sink_init (GstMultiFileSink * filesink,
    GstMultiFileSinkClass * g_class)
{
  GstPad *pad;

  pad = GST_BASE_SINK_PAD (filesink);

  //gst_pad_set_query_function (pad, GST_DEBUG_FUNCPTR (gst_multi_file_sink_query));

  filesink->filename = g_strdup ("output-%05d");

  GST_BASE_SINK (filesink)->sync = FALSE;
}

static void
gst_multi_file_sink_dispose (GObject * object)
{
  GstMultiFileSink *sink = GST_MULTI_FILE_SINK (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  g_free (sink->filename);
  sink->filename = NULL;
}

static gboolean
gst_multi_file_sink_set_location (GstMultiFileSink * sink,
    const gchar * location)
{
  g_free (sink->filename);
  if (location != NULL) {
    sink->filename = g_strdup (location);
  } else {
    sink->filename = NULL;
  }

  return TRUE;
}
static void
gst_multi_file_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiFileSink *sink = GST_MULTI_FILE_SINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      gst_multi_file_sink_set_location (sink, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multi_file_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMultiFileSink *sink = GST_MULTI_FILE_SINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, sink->filename);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#ifdef G_OS_UNIX
# define __GST_STDIO_SEEK_FUNCTION "lseek"
#else
# define __GST_STDIO_SEEK_FUNCTION "fseek"
#endif

static gchar *
gst_multi_file_sink_get_filename (GstMultiFileSink * filesink)
{
  gchar *filename;

  filename = g_strdup_printf (filesink->filename, filesink->index);

  return filename;
}

static GstFlowReturn
gst_multi_file_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstMultiFileSink *filesink;
  guint size;
  gchar *filename;
  FILE *file;

  size = GST_BUFFER_SIZE (buffer);

  filesink = GST_MULTI_FILE_SINK (sink);

  filename = gst_multi_file_sink_get_filename (filesink);

  file = fopen (filename, "wb");
  if (!file) {
    goto handle_error;
  }

  g_free (filename);

  if (size > 0 && GST_BUFFER_DATA (buffer) != NULL) {
    if (fwrite (GST_BUFFER_DATA (buffer), size, 1, file) != 1)
      goto handle_error;
  }

  filesink->index++;

  fclose (file);

  return GST_FLOW_OK;

handle_error:
  {
    switch (errno) {
      case ENOSPC:{
        GST_ELEMENT_ERROR (filesink, RESOURCE, NO_SPACE_LEFT, (NULL), (NULL));
        break;
      }
      default:{
        GST_ELEMENT_ERROR (filesink, RESOURCE, WRITE,
            ("Error while writing to file \"%s\".", filesink->filename),
            ("%s", g_strerror (errno)));
      }
    }
    return GST_FLOW_ERROR;
  }
}
