/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Bastien Nocera <hadess@hadess.net>
 *                    2003 Colin Walters <walters@verbum.org>
 *
 * gstgnomevfssink.c: 
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

#include "gstgnomevfs.h"

#include <gst/gst.h>
#include <libgnomevfs/gnome-vfs.h>
#include <string.h>
#include <errno.h>


#define GST_TYPE_GNOMEVFSSINK \
  (gst_gnomevfssink_get_type())
#define GST_GNOMEVFSSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GNOMEVFSSINK,GstGnomeVFSSink))
#define GST_GNOMEVFSSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GNOMEVFSSINK,GstGnomeVFSSinkClass))
#define GST_IS_GNOMEVFSSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GNOMEVFSSINK))
#define GST_IS_GNOMEVFSSINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GNOMEVFSSINK))

typedef struct _GstGnomeVFSSink GstGnomeVFSSink;
typedef struct _GstGnomeVFSSinkClass GstGnomeVFSSinkClass;

typedef enum
{
  GST_GNOMEVFSSINK_OPEN = GST_ELEMENT_FLAG_LAST,

  GST_GNOMEVFSSINK_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2,
}
GstGnomeVFSSinkFlags;

struct _GstGnomeVFSSink
{
  GstElement element;

  /* uri */
  GnomeVFSURI *uri;

  /* handle */
  GnomeVFSHandle *handle;

  /* whether we opened the handle ourselves */
  gboolean own_handle;
};

struct _GstGnomeVFSSinkClass
{
  GstElementClass parent_class;

  /* signals */
    gboolean (*erase_ask) (GstElement * element, GnomeVFSURI * uri);
};

/* GnomeVFSSink signals and args */
enum
{
  /* FILL ME */
  SIGNAL_ERASE_ASK,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LOCATION,
  ARG_URI,
  ARG_HANDLE
};

static void gst_gnomevfssink_base_init (gpointer g_class);
static void gst_gnomevfssink_class_init (GstGnomeVFSSinkClass * klass);
static void gst_gnomevfssink_init (GstGnomeVFSSink * gnomevfssink);
static void gst_gnomevfssink_dispose (GObject * obj);

static void gst_gnomevfssink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gnomevfssink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gnomevfssink_open_file (GstGnomeVFSSink * sink);
static void gst_gnomevfssink_close_file (GstGnomeVFSSink * sink);

static void gst_gnomevfssink_chain (GstPad * pad, GstData * _data);

static GstElementStateReturn gst_gnomevfssink_change_state (GstElement *
    element);

static GstElementClass *parent_class = NULL;
static guint gst_gnomevfssink_signals[LAST_SIGNAL] = { 0 };

GType
gst_gnomevfssink_get_type (void)
{
  static GType gnomevfssink_type = 0;

  if (!gnomevfssink_type) {
    static const GTypeInfo gnomevfssink_info = {
      sizeof (GstGnomeVFSSinkClass),
      gst_gnomevfssink_base_init,
      NULL,
      (GClassInitFunc) gst_gnomevfssink_class_init,
      NULL,
      NULL,
      sizeof (GstGnomeVFSSink),
      0,
      (GInstanceInitFunc) gst_gnomevfssink_init,
    };

    gnomevfssink_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstGnomeVFSSink",
        &gnomevfssink_info, 0);
  }
  return gnomevfssink_type;
}

static void
gst_gnomevfssink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  static GstElementDetails gst_gnomevfssink_details =
      GST_ELEMENT_DETAILS ("GnomeVFS Sink",
      "Sink/File",
      "Write stream to a GnomeVFS URI",
      "Bastien Nocera <hadess@hadess.net>");

  gst_element_class_set_details (element_class, &gst_gnomevfssink_details);
}

static gboolean
_gst_boolean_allow_overwrite_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  gboolean allow_overwrite;

  allow_overwrite = g_value_get_boolean (handler_return);
  g_value_set_boolean (return_accu, allow_overwrite);

  /* stop emission if signal doesn't allow overwriting */
  return allow_overwrite;
}

static void
gst_gnomevfssink_class_init (GstGnomeVFSSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);


  gst_element_class_install_std_props (GST_ELEMENT_CLASS (klass),
      "location", ARG_LOCATION, G_PARAM_READWRITE, NULL);
  g_object_class_install_property (gobject_class, ARG_URI,
      g_param_spec_pointer ("uri", "GnomeVFSURI", "URI for GnomeVFS",
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_HANDLE,
      g_param_spec_pointer ("handle",
          "GnomeVFSHandle", "Handle for GnomeVFS", G_PARAM_READWRITE));

  gst_gnomevfssink_signals[SIGNAL_ERASE_ASK] =
      g_signal_new ("allow-overwrite", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstGnomeVFSSinkClass, erase_ask),
      _gst_boolean_allow_overwrite_accumulator, NULL,
      gst_marshal_BOOLEAN__POINTER, G_TYPE_BOOLEAN, 1, G_TYPE_POINTER);


  gobject_class->set_property = gst_gnomevfssink_set_property;
  gobject_class->get_property = gst_gnomevfssink_get_property;
  gobject_class->dispose = gst_gnomevfssink_dispose;

  gstelement_class->change_state = gst_gnomevfssink_change_state;

  /* gnome vfs engine init */
  if (gnome_vfs_initialized () == FALSE)
    gnome_vfs_init ();
}

static void
gst_gnomevfssink_dispose (GObject * obj)
{
  GstGnomeVFSSink *sink = GST_GNOMEVFSSINK (obj);

  if (sink->uri) {
    gnome_vfs_uri_unref (sink->uri);
    sink->uri = NULL;
  }
}

static void
gst_gnomevfssink_init (GstGnomeVFSSink * gnomevfssink)
{
  GstPad *pad;

  pad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (gnomevfssink), pad);
  gst_pad_set_chain_function (pad, gst_gnomevfssink_chain);

  gnomevfssink->uri = NULL;
  gnomevfssink->handle = NULL;
  gnomevfssink->own_handle = FALSE;
}

static void
gst_gnomevfssink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGnomeVFSSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = GST_GNOMEVFSSINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      if (sink->uri) {
        gnome_vfs_uri_unref (sink->uri);
        sink->uri = NULL;
      }
      if (g_value_get_string (value)) {
        sink->uri = gnome_vfs_uri_new (g_value_get_string (value));
      }
      break;
    case ARG_URI:
      if (sink->uri) {
        gnome_vfs_uri_unref (sink->uri);
        sink->uri = NULL;
      }
      if (g_value_get_pointer (value)) {
        sink->uri = gnome_vfs_uri_ref (g_value_get_pointer (value));
      }
      break;
    case ARG_HANDLE:
      if (GST_STATE (sink) == GST_STATE_NULL ||
          GST_STATE (sink) == GST_STATE_READY) {
        sink->handle = g_value_get_pointer (value);
      }
      break;
    default:
      break;
  }
}

static void
gst_gnomevfssink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstGnomeVFSSink *sink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_GNOMEVFSSINK (object));

  sink = GST_GNOMEVFSSINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      if (sink->uri) {
        gchar *filename = gnome_vfs_uri_to_string (sink->uri,
            GNOME_VFS_URI_HIDE_PASSWORD);

        g_value_set_string (value, filename);
        g_free (filename);
      } else {
        g_value_set_string (value, NULL);
      }
      break;
    case ARG_URI:
      g_value_set_pointer (value, sink->uri);
      break;
    case ARG_HANDLE:
      g_value_set_pointer (value, sink->handle);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gnomevfssink_open_file (GstGnomeVFSSink * sink)
{
  GnomeVFSResult result;

  g_return_val_if_fail (!GST_FLAG_IS_SET (sink, GST_GNOMEVFSSINK_OPEN), FALSE);

  if (sink->uri) {
    /* open the file */
    result = gnome_vfs_create_uri (&(sink->handle), sink->uri,
        GNOME_VFS_OPEN_WRITE, TRUE,
        GNOME_VFS_PERM_USER_READ | GNOME_VFS_PERM_USER_WRITE
        | GNOME_VFS_PERM_GROUP_READ);
    /* if the file existed and the property says to ask, then ask! */
    if (result == GNOME_VFS_ERROR_FILE_EXISTS) {
      gboolean erase_anyway = FALSE;

      g_signal_emit (G_OBJECT (sink),
          gst_gnomevfssink_signals[SIGNAL_ERASE_ASK], 0, sink->uri,
          &erase_anyway);
      if (erase_anyway) {
        result = gnome_vfs_create_uri (&(sink->handle), sink->uri,
            GNOME_VFS_OPEN_WRITE, FALSE,
            GNOME_VFS_PERM_USER_READ | GNOME_VFS_PERM_USER_WRITE
            | GNOME_VFS_PERM_GROUP_READ);
      }
    }
    GST_DEBUG ("open: %s", gnome_vfs_result_to_string (result));
    if (result != GNOME_VFS_OK) {
      gchar *filename = gnome_vfs_uri_to_string (sink->uri,
          GNOME_VFS_URI_HIDE_PASSWORD);

      GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
          (_("Could not open vfs file \"%s\" for writing."), filename),
          GST_ERROR_SYSTEM);
      g_free (filename);
      return FALSE;
    }
    sink->own_handle = TRUE;
  } else if (!sink->handle) {
    GST_ELEMENT_ERROR (sink, RESOURCE, FAILED, (_("No filename given")), NULL);
    return FALSE;
  }

  GST_FLAG_SET (sink, GST_GNOMEVFSSINK_OPEN);

  return TRUE;
}

static void
gst_gnomevfssink_close_file (GstGnomeVFSSink * sink)
{
  GnomeVFSResult result;

  g_return_if_fail (GST_FLAG_IS_SET (sink, GST_GNOMEVFSSINK_OPEN));

  if (sink->own_handle) {
    /* close the file */
    result = gnome_vfs_close (sink->handle);

    if (result != GNOME_VFS_OK) {
      gchar *filename = gnome_vfs_uri_to_string (sink->uri,
          GNOME_VFS_URI_HIDE_PASSWORD);

      GST_ELEMENT_ERROR (sink, RESOURCE, CLOSE,
          (_("Could not close vfs file \"%s\"."), filename), GST_ERROR_SYSTEM);
      g_free (filename);
    }

    sink->own_handle = FALSE;
  }

  GST_FLAG_UNSET (sink, GST_GNOMEVFSSINK_OPEN);
}

/**
 * gst_gnomevfssink_chain:
 * @pad: the pad this gnomevfssink is connected to
 * @buf: the buffer that has to be absorbed
 *
 * take the buffer from the pad and write to file if it's open
 */
static void
gst_gnomevfssink_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstGnomeVFSSink *sink;
  GnomeVFSResult result;
  GnomeVFSFileSize bytes_written;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  sink = GST_GNOMEVFSSINK (gst_pad_get_parent (pad));

  if (GST_FLAG_IS_SET (sink, GST_GNOMEVFSSINK_OPEN)) {
    result =
        gnome_vfs_write (sink->handle, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf), &bytes_written);
    GST_DEBUG ("write: %s, written_bytes: %" G_GUINT64_FORMAT,
        gnome_vfs_result_to_string (result), bytes_written);
    if (bytes_written < GST_BUFFER_SIZE (buf)) {
      printf ("gnomevfssink : Warning : %d bytes should be written, only %"
          G_GUINT64_FORMAT " bytes written\n", GST_BUFFER_SIZE (buf),
          bytes_written);
    }
  }
  gst_buffer_unref (buf);
}

static GstElementStateReturn
gst_gnomevfssink_change_state (GstElement * element)
{
  g_return_val_if_fail (GST_IS_GNOMEVFSSINK (element), GST_STATE_FAILURE);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      if (!GST_FLAG_IS_SET (element, GST_GNOMEVFSSINK_OPEN)) {
        if (!gst_gnomevfssink_open_file (GST_GNOMEVFSSINK (element)))
          return GST_STATE_FAILURE;
      }
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (GST_FLAG_IS_SET (element, GST_GNOMEVFSSINK_OPEN))
        gst_gnomevfssink_close_file (GST_GNOMEVFSSINK (element));
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
