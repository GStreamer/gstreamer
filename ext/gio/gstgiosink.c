/* GStreamer
 *
 * Copyright (C) 2007 Rene Stadler <mail@renestadler.de>
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
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
 * SECTION:element-giosink
 * @short_description: Write a stream to any GIO-supported location
 * @see_also: #GstFileSink, #GstGnomeVFSSink, #GstGioSrc
 *
 * <refsect2>
 * <para>
 * This plugin writes incoming data to a local or remote location specified
 * by an URI. This location can be specified using any protocol supported by
 * the GIO library or it's VFS backends. Common protocols are 'file', 'ftp',
 * or 'smb'.
 * </para>
 * <para>
 * Example pipeline:
 * <programlisting>
 * gst-launch -v filesrc location=input.xyz ! giosink location=file:///home/joe/out.xyz
 * </programlisting>
 * The above pipeline will simply copy a local file. Instead of giosink,
 * we could just as well have used the filesink element here.
 * </para>
 * <para>
 * Another example pipeline:
 * <programlisting>
 * gst-launch -v filesrc location=foo.mp3 ! mad ! flacenc ! giosink location=smb://othercomputer/foo.flac
 * </programlisting>
 * The above pipeline will re-encode an mp3 file into FLAC format and store
 * it on a remote host using the Samba protocol.
 * </para>
 * <para>
 * Another example pipeline:
 * <programlisting>
 * gst-launch -v audiotestsrc num-buffers=100 ! vorbisenc ! oggmux ! giosink location=file:///home/foo/bar.ogg
 * </programlisting>
 * The above pipeline will encode a 440Hz sine wave to Ogg Vorbis and stores
 * it in the home directory of user foo.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstgiosink.h"

GST_DEBUG_CATEGORY_STATIC (gst_gio_sink_debug);
#define GST_CAT_DEFAULT gst_gio_sink_debug

/* Filter signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LOCATION
};

GST_BOILERPLATE_FULL (GstGioSink, gst_gio_sink, GstGioBaseSink,
    GST_TYPE_GIO_BASE_SINK, gst_gio_uri_handler_do_init);

static void gst_gio_sink_finalize (GObject * object);
static void gst_gio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_gio_sink_start (GstBaseSink * base_sink);

static void
gst_gio_sink_base_init (gpointer gclass)
{
  static GstElementDetails element_details = {
    "GIO sink",
    "Sink/File",
    "Write to any GIO-supported location",
    "Ren\xc3\xa9 Stadler <mail@renestadler.de>, "
        "Sebastian Dröge <slomo@circular-chaos.org>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  GST_DEBUG_CATEGORY_INIT (gst_gio_sink_debug, "gio_sink", 0, "GIO sink");

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gio_sink_class_init (GstGioSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->finalize = gst_gio_sink_finalize;
  gobject_class->set_property = gst_gio_sink_set_property;
  gobject_class->get_property = gst_gio_sink_get_property;

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "Location", "URI location to write to",
          NULL, G_PARAM_READWRITE));

  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_gio_sink_start);
}

static void
gst_gio_sink_init (GstGioSink * sink, GstGioSinkClass * gclass)
{
}

static void
gst_gio_sink_finalize (GObject * object)
{
  GstGioSink *sink = GST_GIO_SINK (object);

  if (sink->location) {
    g_free (sink->location);
    sink->location = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gst_gio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGioSink *sink = GST_GIO_SINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      if (GST_STATE (sink) == GST_STATE_PLAYING ||
          GST_STATE (sink) == GST_STATE_PAUSED)
        break;

      g_free (sink->location);
      sink->location = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGioSink *sink = GST_GIO_SINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, sink->location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gio_sink_start (GstBaseSink * base_sink)
{
  GstGioSink *sink = GST_GIO_SINK (base_sink);
  GFile *file;
  GOutputStream *stream;
  GCancellable *cancel = GST_GIO_BASE_SINK (sink)->cancel;
  gboolean success;
  GError *err = NULL;

  if (sink->location == NULL) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
        ("No location given"));
    return FALSE;
  }

  file = g_file_new_for_uri (sink->location);

  if (file == NULL) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
        ("Malformed URI or protocol not supported (%s)", sink->location));
    return FALSE;
  }

  stream =
      G_OUTPUT_STREAM (g_file_create (file, G_FILE_CREATE_NONE, cancel, &err));
  success = (stream != NULL);

  g_object_unref (file);

  if (!success && !gst_gio_error (sink, "g_file_create", &err, NULL)) {

    /*if (GST_GIO_ERROR_MATCHES (err, EXISTS)) */
    /* FIXME: Retry with replace if overwrite == TRUE! */

    if (GST_GIO_ERROR_MATCHES (err, NOT_FOUND))
      GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND, (NULL),
          ("Could not open location %s for writing: %s",
              sink->location, err->message));
    else
      GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_READ, (NULL),
          ("Could not open location %s for writing: %s",
              sink->location, err->message));

    g_clear_error (&err);
  }

  if (!success)
    return FALSE;

  GST_DEBUG_OBJECT (sink, "opened location %s", sink->location);

  gst_gio_base_sink_set_stream (GST_GIO_BASE_SINK (sink), stream);

  return GST_BASE_SINK_CLASS (parent_class)->start (base_sink);
}
