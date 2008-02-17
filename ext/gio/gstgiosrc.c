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
 * SECTION:element-giosrc
 * @short_description: Read from any GIO-supported location
 * @see_also: #GstFileSrc, #GstGnomeVFSSrc, #GstGioSink
 *
 * <refsect2>
 * <para>
 * This plugin reads data from a local or remote location specified
 * by an URI. This location can be specified using any protocol supported by
 * the GIO library or it's VFS backends. Common protocols are 'file', 'http',
 * 'ftp', or 'smb'.
 * </para>
 * <para>
 * Example pipeline:
 * <programlisting>
 * gst-launch -v giosrc location=file:///home/joe/foo.xyz ! fakesink
 * </programlisting>
 * The above pipeline will simply read a local file and do nothing with the
 * data read. Instead of giosrc, we could just as well have used the
 * filesrc element here.
 * </para>
 * <para>
 * Another example pipeline:
 * <programlisting>
 * gst-launch -v giosrc location=smb://othercomputer/foo.xyz ! filesink location=/home/joe/foo.xyz
 * </programlisting>
 * The above pipeline will copy a file from a remote host to the local file
 * system using the Samba protocol.
 * </para>
 * <para>
 * Yet another example pipeline:
 * <programlisting>
 * gst-launch -v giosrc location=http://music.foobar.com/demo.mp3 ! mad ! audioconvert ! audioresample ! alsasink
 * </programlisting>
 * The above pipeline will read and decode and play an mp3 file from a
 * web server using the http protocol.
 * </para>
 * </refsect2>
 */

/* FIXME: We would like to mount the enclosing volume of an URL
 *        if it isn't mounted yet but this is possible async-only.
 *        Unfortunately this requires a running main loop from the
 *        default context and we can't guarantuee this!
 *
 *        We would also like to do authentication while mounting.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstgiosrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_gio_src_debug);
#define GST_CAT_DEFAULT gst_gio_src_debug

enum
{
  ARG_0,
  ARG_LOCATION
};

GST_BOILERPLATE_FULL (GstGioSrc, gst_gio_src, GstGioBaseSrc,
    GST_TYPE_GIO_BASE_SRC, gst_gio_uri_handler_do_init);

static void gst_gio_src_finalize (GObject * object);
static void gst_gio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_gio_src_start (GstBaseSrc * base_src);

static void
gst_gio_src_base_init (gpointer gclass)
{
  static GstElementDetails element_details = {
    "GIO source",
    "Source/File",
    "Read from any GIO-supported location",
    "Ren\xc3\xa9 Stadler <mail@renestadler.de>, "
        "Sebastian Dröge <slomo@circular-chaos.org>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  GST_DEBUG_CATEGORY_INIT (gst_gio_src_debug, "gio_src", 0, "GIO source");

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gio_src_class_init (GstGioSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;

  gobject_class->finalize = gst_gio_src_finalize;
  gobject_class->set_property = gst_gio_src_set_property;
  gobject_class->get_property = gst_gio_src_get_property;

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "Location", "URI location to read from",
          NULL, G_PARAM_READWRITE));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_gio_src_start);
}

static void
gst_gio_src_init (GstGioSrc * src, GstGioSrcClass * gclass)
{
}

static void
gst_gio_src_finalize (GObject * object)
{
  GstGioSrc *src = GST_GIO_SRC (object);

  if (src->location) {
    g_free (src->location);
    src->location = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, finalize, (object));
}

static void
gst_gio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGioSrc *src = GST_GIO_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      if (GST_STATE (src) == GST_STATE_PLAYING ||
          GST_STATE (src) == GST_STATE_PAUSED)
        break;

      g_free (src->location);
      src->location = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGioSrc *src = GST_GIO_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gio_src_start (GstBaseSrc * base_src)
{
  GstGioSrc *src = GST_GIO_SRC (base_src);
  GFile *file;
  GError *err = NULL;
  GInputStream *stream;
  GCancellable *cancel = GST_GIO_BASE_SRC (src)->cancel;

  if (src->location == NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), ("No location given"));
    return FALSE;
  }

  file = g_file_new_for_uri (src->location);

  if (file == NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("Malformed URI or protocol not supported (%s)", src->location));
    return FALSE;
  }

  stream = G_INPUT_STREAM (g_file_read (file, cancel, &err));

  g_object_unref (file);

  if (stream == NULL && !gst_gio_error (src, "g_file_read", &err, NULL)) {

    if (GST_GIO_ERROR_MATCHES (err, NOT_FOUND)) {
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
          ("Could not open location %s for reading: %s",
              src->location, err->message));
    } else {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
          ("Could not open location %s for reading: %s",
              src->location, err->message));
    }

    g_clear_error (&err);
    return FALSE;
  } else if (stream == NULL) {
    return FALSE;
  }

  gst_gio_base_src_set_stream (GST_GIO_BASE_SRC (src), stream);

  GST_DEBUG_OBJECT (src, "opened location %s", src->location);

  return GST_BASE_SRC_CLASS (parent_class)->start (base_src);
}
