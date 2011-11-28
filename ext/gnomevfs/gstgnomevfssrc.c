/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Bastien Nocera <hadess@hadess.net>
 *                    2002 Kristian Rietveld <kris@gtk.org>
 *                    2002,2003 Colin Walters <walters@gnu.org>
 *
 * gnomevfssrc.c:
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
 * SECTION:element-gnomevfssrc
 * @see_also: #GstFileSrc, #GstGnomeVFSSink
 *
 * This plugin reads data from a local or remote location specified
 * by an URI. This location can be specified using any protocol supported by
 * the GnomeVFS library. Common protocols are 'file', 'http', 'ftp', or 'smb'.
 *
 * In case the #GstGnomeVFSSrc:iradio-mode property is set and the
 * location is a http resource, gnomevfssrc will send special icecast http
 * headers to the server to request additional icecast metainformation. If
 * the server is not an icecast server, it will display the same behaviour
 * as if the #GstGnomeVFSSrc:iradio-mode property was not set. However,
 * if the server is in fact an icecast server, gnomevfssrc will output
 * data with a media type of application/x-icy, in which case you will
 * need to use the #GstICYDemux element as follow-up element to extract
 * the icecast meta data and to determine the underlying media type.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch -v gnomevfssrc location=file:///home/joe/foo.xyz ! fakesink
 * ]| The above pipeline will simply read a local file and do nothing with the
 * data read. Instead of gnomevfssrc, we could just as well have used the
 * filesrc element here.
 * |[
 * gst-launch -v gnomevfssrc location=smb://othercomputer/foo.xyz ! filesink location=/home/joe/foo.xyz
 * ]| The above pipeline will copy a file from a remote host to the local file
 * system using the Samba protocol.
 * |[
 * gst-launch -v gnomevfssrc location=http://music.foobar.com/demo.mp3 ! mad ! audioconvert ! audioresample ! alsasink
 * ]| The above pipeline will read and decode and play an mp3 file from a
 * web server using the http protocol.
 * </refsect2>
 */


#define BROKEN_SIG 1
/*#undef BROKEN_SIG */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst/gst-i18n-plugin.h"

#include "gstgnomevfssrc.h"
#include <gnome-vfs-module-2.0/libgnomevfs/gnome-vfs-cancellable-ops.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/tag/tag.h>

/* gnome-vfs.h doesn't include the following header, which we need: */
#include <libgnomevfs/gnome-vfs-standard-callbacks.h>

GST_DEBUG_CATEGORY_STATIC (gnomevfssrc_debug);
#define GST_CAT_DEFAULT gnomevfssrc_debug

static GStaticMutex count_lock = G_STATIC_MUTEX_INIT;
static gint ref_count = 0;
static gboolean vfs_owner = FALSE;

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  ARG_0,
  ARG_HANDLE,
  ARG_LOCATION,
  ARG_IRADIO_MODE,
  ARG_IRADIO_NAME,
  ARG_IRADIO_GENRE,
  ARG_IRADIO_URL,
  ARG_IRADIO_TITLE
};

static void gst_gnome_vfs_src_base_init (gpointer g_class);
static void gst_gnome_vfs_src_class_init (GstGnomeVFSSrcClass * klass);
static void gst_gnome_vfs_src_init (GstGnomeVFSSrc * gnomevfssrc);
static void gst_gnome_vfs_src_finalize (GObject * object);
static void gst_gnome_vfs_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void gst_gnome_vfs_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gnome_vfs_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gnome_vfs_src_stop (GstBaseSrc * src);
static gboolean gst_gnome_vfs_src_start (GstBaseSrc * src);
static gboolean gst_gnome_vfs_src_is_seekable (GstBaseSrc * src);
static gboolean gst_gnome_vfs_src_check_get_range (GstBaseSrc * src);
static gboolean gst_gnome_vfs_src_unlock (GstBaseSrc * basesrc);
static gboolean gst_gnome_vfs_src_unlock_stop (GstBaseSrc * basesrc);
static gboolean gst_gnome_vfs_src_get_size (GstBaseSrc * src, guint64 * size);
static GstFlowReturn gst_gnome_vfs_src_create (GstBaseSrc * basesrc,
    guint64 offset, guint size, GstBuffer ** buffer);
static gboolean gst_gnome_vfs_src_query (GstBaseSrc * src, GstQuery * query);

static GstElementClass *parent_class = NULL;

GType
gst_gnome_vfs_src_get_type (void)
{
  static GType gnomevfssrc_type = 0;

  if (!gnomevfssrc_type) {
    static const GTypeInfo gnomevfssrc_info = {
      sizeof (GstGnomeVFSSrcClass),
      gst_gnome_vfs_src_base_init,
      NULL,
      (GClassInitFunc) gst_gnome_vfs_src_class_init,
      NULL,
      NULL,
      sizeof (GstGnomeVFSSrc),
      0,
      (GInstanceInitFunc) gst_gnome_vfs_src_init,
    };
    static const GInterfaceInfo urihandler_info = {
      gst_gnome_vfs_src_uri_handler_init,
      NULL,
      NULL
    };

    gnomevfssrc_type =
        g_type_register_static (GST_TYPE_BASE_SRC,
        "GstGnomeVFSSrc", &gnomevfssrc_info, 0);
    g_type_add_interface_static (gnomevfssrc_type, GST_TYPE_URI_HANDLER,
        &urihandler_info);
  }
  return gnomevfssrc_type;
}

static void
gst_gnome_vfs_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &srctemplate);
  gst_element_class_set_details_simple (element_class,
      "GnomeVFS Source", "Source/File",
      "Read from any GnomeVFS-supported file",
      "Bastien Nocera <hadess@hadess.net>, "
      "GStreamer maintainers <gstreamer-devel@lists.sourceforge.net>");

  GST_DEBUG_CATEGORY_INIT (gnomevfssrc_debug, "gnomevfssrc", 0,
      "Gnome-VFS Source");
}

static void
gst_gnome_vfs_src_class_init (GstGnomeVFSSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstbasesrc_class = GST_BASE_SRC_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_gnome_vfs_src_finalize;
  gobject_class->set_property = gst_gnome_vfs_src_set_property;
  gobject_class->get_property = gst_gnome_vfs_src_get_property;

  /* properties */
  gst_element_class_install_std_props (GST_ELEMENT_CLASS (klass),
      "location", ARG_LOCATION, G_PARAM_READWRITE, NULL);
  g_object_class_install_property (gobject_class,
      ARG_HANDLE,
      g_param_spec_boxed ("handle",
          "GnomeVFSHandle", "Handle for GnomeVFS",
          GST_TYPE_GNOME_VFS_HANDLE,
          GST_PARAM_MUTABLE_READY | G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  /* icecast stuff */
  g_object_class_install_property (gobject_class,
      ARG_IRADIO_MODE,
      g_param_spec_boolean ("iradio-mode",
          "iradio-mode",
          "Enable internet radio mode (extraction of shoutcast/icecast metadata)",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      ARG_IRADIO_NAME,
      g_param_spec_string ("iradio-name",
          "iradio-name", "Name of the stream", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_IRADIO_GENRE,
      g_param_spec_string ("iradio-genre", "iradio-genre",
          "Genre of the stream", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_IRADIO_URL,
      g_param_spec_string ("iradio-url", "iradio-url",
          "Homepage URL for radio stream", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_IRADIO_TITLE,
      g_param_spec_string ("iradio-title", "iradio-title",
          "Name of currently playing song", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_unlock);
  gstbasesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_unlock_stop);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_get_size);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_is_seekable);
  gstbasesrc_class->check_get_range =
      GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_check_get_range);
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_create);
  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (gst_gnome_vfs_src_query);
}

static void
gst_gnome_vfs_src_init (GstGnomeVFSSrc * gnomevfssrc)
{
  gnomevfssrc->uri = NULL;
  gnomevfssrc->uri_name = NULL;
  gnomevfssrc->context = NULL;
  gnomevfssrc->handle = NULL;
  gnomevfssrc->interrupted = FALSE;
  gnomevfssrc->curoffset = 0;
  gnomevfssrc->seekable = FALSE;

  gnomevfssrc->iradio_mode = FALSE;
  gnomevfssrc->http_callbacks_pushed = FALSE;
  gnomevfssrc->iradio_name = NULL;
  gnomevfssrc->iradio_genre = NULL;
  gnomevfssrc->iradio_url = NULL;
  gnomevfssrc->iradio_title = NULL;

  g_static_mutex_lock (&count_lock);
  if (ref_count == 0) {
    /* gnome vfs engine init */
    if (gnome_vfs_initialized () == FALSE) {
      gnome_vfs_init ();
      vfs_owner = TRUE;
    }
  }
  ref_count++;
  g_static_mutex_unlock (&count_lock);
}

static void
gst_gnome_vfs_src_finalize (GObject * object)
{
  GstGnomeVFSSrc *src = GST_GNOME_VFS_SRC (object);

  g_static_mutex_lock (&count_lock);
  ref_count--;
  if (ref_count == 0 && vfs_owner) {
    if (gnome_vfs_initialized () == TRUE) {
      gnome_vfs_shutdown ();
    }
  }
  g_static_mutex_unlock (&count_lock);

  if (src->uri) {
    gnome_vfs_uri_unref (src->uri);
    src->uri = NULL;
  }

  g_free (src->uri_name);
  src->uri_name = NULL;

  g_free (src->iradio_name);
  src->iradio_name = NULL;

  g_free (src->iradio_genre);
  src->iradio_genre = NULL;

  g_free (src->iradio_url);
  src->iradio_url = NULL;

  g_free (src->iradio_title);
  src->iradio_title = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/*
 * URI interface support.
 */

static GstURIType
gst_gnome_vfs_src_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_gnome_vfs_src_uri_get_protocols (void)
{
  return gst_gnomevfs_get_supported_uris ();
}

static const gchar *
gst_gnome_vfs_src_uri_get_uri (GstURIHandler * handler)
{
  GstGnomeVFSSrc *src = GST_GNOME_VFS_SRC (handler);

  return src->uri_name;
}

static gboolean
gst_gnome_vfs_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstGnomeVFSSrc *src = GST_GNOME_VFS_SRC (handler);

  if (GST_STATE (src) == GST_STATE_PLAYING ||
      GST_STATE (src) == GST_STATE_PAUSED)
    return FALSE;

  g_object_set (G_OBJECT (src), "location", uri, NULL);

  return TRUE;
}

static void
gst_gnome_vfs_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_gnome_vfs_src_uri_get_type;
  iface->get_protocols = gst_gnome_vfs_src_uri_get_protocols;
  iface->get_uri = gst_gnome_vfs_src_uri_get_uri;
  iface->set_uri = gst_gnome_vfs_src_uri_set_uri;
}

static void
gst_gnome_vfs_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOME_VFS_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:{
      const gchar *new_location;

      /* the element must be stopped or paused in order to do this */
      if (GST_STATE (src) == GST_STATE_PLAYING ||
          GST_STATE (src) == GST_STATE_PAUSED)
        break;

      if (src->uri) {
        gnome_vfs_uri_unref (src->uri);
        src->uri = NULL;
      }
      if (src->uri_name) {
        g_free (src->uri_name);
        src->uri_name = NULL;
      }

      new_location = g_value_get_string (value);
      if (new_location) {
        src->uri_name = gst_gnome_vfs_location_to_uri_string (new_location);
        src->uri = gnome_vfs_uri_new (src->uri_name);
      }
      break;
    }
    case ARG_HANDLE:
      if (GST_STATE (src) == GST_STATE_NULL ||
          GST_STATE (src) == GST_STATE_READY) {
        if (src->uri) {
          gnome_vfs_uri_unref (src->uri);
          src->uri = NULL;
        }
        if (src->uri_name) {
          g_free (src->uri_name);
          src->uri_name = NULL;
        }
        src->handle = g_value_get_boxed (value);
      }
      break;
    case ARG_IRADIO_MODE:
      src->iradio_mode = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gnome_vfs_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOME_VFS_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->uri_name);
      break;
    case ARG_HANDLE:
      g_value_set_boxed (value, src->handle);
      break;
    case ARG_IRADIO_MODE:
      g_value_set_boolean (value, src->iradio_mode);
      break;
    case ARG_IRADIO_NAME:
      g_value_set_string (value, src->iradio_name);
      break;
    case ARG_IRADIO_GENRE:
      g_value_set_string (value, src->iradio_genre);
      break;
    case ARG_IRADIO_URL:
      g_value_set_string (value, src->iradio_url);
      break;
    case ARG_IRADIO_TITLE:
      g_value_set_string (value, src->iradio_title);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static char *
gst_gnome_vfs_src_unicodify (const char *str)
{
  const gchar *env_vars[] = { "GST_ICY_TAG_ENCODING",
    "GST_TAG_ENCODING", NULL
  };

  return gst_tag_freeform_string_to_utf8 (str, -1, env_vars);
}

static void
gst_gnome_vfs_src_send_additional_headers_callback (gconstpointer in,
    gsize in_size, gpointer out, gsize out_size, gpointer callback_data)
{
  GstGnomeVFSSrc *src = GST_GNOME_VFS_SRC (callback_data);
  GnomeVFSModuleCallbackAdditionalHeadersOut *out_args =
      (GnomeVFSModuleCallbackAdditionalHeadersOut *) out;

  if (!src->iradio_mode)
    return;
  GST_DEBUG_OBJECT (src, "sending headers\n");

  out_args->headers = g_list_append (out_args->headers,
      g_strdup ("icy-metadata:1\r\n"));
}

static void
gst_gnome_vfs_src_received_headers_callback (gconstpointer in,
    gsize in_size, gpointer out, gsize out_size, gpointer callback_data)
{
  GList *i;
  gint icy_metaint;
  GstGnomeVFSSrc *src = GST_GNOME_VFS_SRC (callback_data);
  GnomeVFSModuleCallbackReceivedHeadersIn *in_args =
      (GnomeVFSModuleCallbackReceivedHeadersIn *) in;

  /* This is only used for internet radio stuff right now */
  if (!src->iradio_mode)
    return;

  GST_DEBUG_OBJECT (src, "receiving internet radio metadata\n");

  /* FIXME: Could we use "Accept-Ranges: bytes"
   * http://www.w3.org/Protocols/rfc2616/rfc2616-sec14.html#sec14.5
   * to enable pull-mode?
   */

  for (i = in_args->headers; i; i = i->next) {
    char *data = (char *) i->data;
    char *value = strchr (data, ':');
    char *key;

    if (!value)
      continue;

    value++;
    g_strstrip (value);
    if (!strlen (value))
      continue;

    GST_LOG_OBJECT (src, "data %s", data);

    /* Icecast stuff */
    if (strncmp (data, "icy-metaint:", 12) == 0) {      /* ugh */
      if (sscanf (data + 12, "%d", &icy_metaint) == 1) {
        if (icy_metaint > 0) {
          GstCaps *icy_caps;

          icy_caps = gst_caps_new_simple ("application/x-icy",
              "metadata-interval", G_TYPE_INT, icy_metaint, NULL);
          gst_pad_set_caps (GST_BASE_SRC_PAD (src), icy_caps);
          gst_caps_unref (icy_caps);
        }
      }
      continue;
    }

    if (!strncmp (data, "icy-", 4))
      key = data + 4;
    else
      continue;

    GST_DEBUG_OBJECT (src, "key: %s", key);
    if (!strncmp (key, "name", 4)) {
      g_free (src->iradio_name);
      src->iradio_name = gst_gnome_vfs_src_unicodify (value);
      if (src->iradio_name)
        g_object_notify (G_OBJECT (src), "iradio-name");
    } else if (!strncmp (key, "genre", 5)) {
      g_free (src->iradio_genre);
      src->iradio_genre = gst_gnome_vfs_src_unicodify (value);
      if (src->iradio_genre)
        g_object_notify (G_OBJECT (src), "iradio-genre");
    } else if (!strncmp (key, "url", 3)) {
      g_free (src->iradio_url);
      src->iradio_url = gst_gnome_vfs_src_unicodify (value);
      if (src->iradio_url)
        g_object_notify (G_OBJECT (src), "iradio-url");
    }
  }
}

static void
gst_gnome_vfs_src_push_callbacks (GstGnomeVFSSrc * src)
{
  if (src->http_callbacks_pushed)
    return;

  GST_DEBUG_OBJECT (src, "pushing callbacks");
  gnome_vfs_module_callback_push
      (GNOME_VFS_MODULE_CALLBACK_HTTP_SEND_ADDITIONAL_HEADERS,
      gst_gnome_vfs_src_send_additional_headers_callback, src, NULL);
  gnome_vfs_module_callback_push
      (GNOME_VFS_MODULE_CALLBACK_HTTP_RECEIVED_HEADERS,
      gst_gnome_vfs_src_received_headers_callback, src, NULL);

  src->http_callbacks_pushed = TRUE;
}

static void
gst_gnome_vfs_src_pop_callbacks (GstGnomeVFSSrc * src)
{
  if (!src->http_callbacks_pushed)
    return;

  GST_DEBUG_OBJECT (src, "popping callbacks");
  gnome_vfs_module_callback_pop
      (GNOME_VFS_MODULE_CALLBACK_HTTP_SEND_ADDITIONAL_HEADERS);
  gnome_vfs_module_callback_pop
      (GNOME_VFS_MODULE_CALLBACK_HTTP_RECEIVED_HEADERS);

  src->http_callbacks_pushed = FALSE;
}

/*
 * Read a new buffer from src->reqoffset, takes care of events
 * and seeking and such.
 */
static GstFlowReturn
gst_gnome_vfs_src_create (GstBaseSrc * basesrc, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  GnomeVFSResult res;
  GstBuffer *buf;
  GnomeVFSFileSize readbytes;
  guint8 *data;
  guint todo;
  GstGnomeVFSSrc *src;
  gboolean interrupted = FALSE;

  src = GST_GNOME_VFS_SRC (basesrc);

  GST_DEBUG ("now at %" G_GINT64_FORMAT ", reading from %" G_GUINT64_FORMAT
      ", size %u", src->curoffset, offset, size);

  /* seek if required */
  if (G_UNLIKELY (src->curoffset != offset)) {
    GST_DEBUG ("need to seek");
    if (src->seekable) {
      GST_DEBUG ("seeking to %" G_GUINT64_FORMAT, offset);
      res = gnome_vfs_seek (src->handle, GNOME_VFS_SEEK_START, offset);
      if (res != GNOME_VFS_OK)
        goto seek_failed;
      src->curoffset = offset;
    } else {
      goto cannot_seek;
    }
  }

  buf = gst_buffer_try_new_and_alloc (size);
  if (G_UNLIKELY (buf == NULL)) {
    GST_ERROR_OBJECT (src, "Failed to allocate %u bytes", size);
    return GST_FLOW_ERROR;
  }

  data = GST_BUFFER_DATA (buf);

  todo = size;
  while (!src->interrupted && todo > 0) {
    /* this can return less that we ask for */
    res =
        gnome_vfs_read_cancellable (src->handle, data, todo, &readbytes,
        src->context);

    if (G_UNLIKELY (res == GNOME_VFS_ERROR_CANCELLED)) {
      GST_DEBUG_OBJECT (src, "interrupted");

      /* Just take what we've so far gotten and return */
      size = size - todo;
      GST_BUFFER_SIZE (buf) = size;
      todo = 0;
      interrupted = TRUE;
      break;
    }

    if (G_UNLIKELY (res == GNOME_VFS_ERROR_EOF || (res == GNOME_VFS_OK
                && readbytes == 0)))
      goto eos;

    if (G_UNLIKELY (res != GNOME_VFS_OK))
      goto read_failed;

    if (readbytes < todo) {
      data = &data[readbytes];
      todo -= readbytes;
    } else {
      todo = 0;
    }
    GST_LOG ("  got size %" G_GUINT64_FORMAT, readbytes);
  }

  if (interrupted)
    goto interrupted;

  GST_BUFFER_OFFSET (buf) = src->curoffset;
  src->curoffset += size;

  /* we're done, return the buffer */
  *buffer = buf;

  return GST_FLOW_OK;

seek_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SEEK, (NULL),
        ("Failed to seek to requested position %" G_GINT64_FORMAT ": %s",
            offset, gnome_vfs_result_to_string (res)));
    return GST_FLOW_ERROR;
  }
cannot_seek:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SEEK, (NULL),
        ("Requested seek from %" G_GINT64_FORMAT " to %" G_GINT64_FORMAT
            " on non-seekable stream", src->curoffset, offset));
    return GST_FLOW_ERROR;
  }
read_failed:
  {
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Failed to read data: %s", gnome_vfs_result_to_string (res)));
    return GST_FLOW_ERROR;
  }
interrupted:
  {
    gst_buffer_unref (buf);
    return GST_FLOW_WRONG_STATE;
  }
eos:
  {
    gst_buffer_unref (buf);
    GST_DEBUG_OBJECT (src, "Reading data gave EOS");
    return GST_FLOW_UNEXPECTED;
  }
}

static gboolean
gst_gnome_vfs_src_query (GstBaseSrc * basesrc, GstQuery * query)
{
  gboolean ret = FALSE;
  GstGnomeVFSSrc *src = GST_GNOME_VFS_SRC (basesrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_URI:
      gst_query_set_uri (query, src->uri_name);
      ret = TRUE;
      break;
    default:
      ret = FALSE;
      break;
  }

  if (!ret)
    ret = GST_BASE_SRC_CLASS (parent_class)->query (basesrc, query);

  return ret;
}

static gboolean
gst_gnome_vfs_src_is_seekable (GstBaseSrc * basesrc)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOME_VFS_SRC (basesrc);

  return src->seekable;
}

static gboolean
gst_gnome_vfs_src_check_get_range (GstBaseSrc * basesrc)
{
  GstGnomeVFSSrc *src;
  const gchar *protocol;

  src = GST_GNOME_VFS_SRC (basesrc);

  if (src->uri == NULL) {
    GST_WARNING_OBJECT (src, "no URI set yet");
    return FALSE;
  }

  if (gnome_vfs_uri_is_local (src->uri)) {
    GST_LOG_OBJECT (src, "local URI (%s), assuming random access is possible",
        GST_STR_NULL (src->uri_name));
    return TRUE;
  }

  /* blacklist certain protocols we know won't work getrange-based */
  protocol = gnome_vfs_uri_get_scheme (src->uri);
  if (protocol == NULL)
    goto undecided;

  if (strcmp (protocol, "http") == 0 || strcmp (protocol, "https") == 0) {
    GST_LOG_OBJECT (src, "blacklisted protocol '%s', no random access possible"
        " (URI=%s)", protocol, GST_STR_NULL (src->uri_name));
    return FALSE;
  }

  /* fall through to undecided */

undecided:
  {
    /* don't know what to do, let the basesrc class decide for us */
    GST_LOG_OBJECT (src, "undecided about URI '%s', let base class handle it",
        GST_STR_NULL (src->uri_name));

    if (GST_BASE_SRC_CLASS (parent_class)->check_get_range)
      return GST_BASE_SRC_CLASS (parent_class)->check_get_range (basesrc);

    return FALSE;
  }
}

/* Interrupt a blocking request. */
static gboolean
gst_gnome_vfs_src_unlock (GstBaseSrc * basesrc)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOME_VFS_SRC (basesrc);
  GST_DEBUG_OBJECT (src, "unlock()");
  src->interrupted = TRUE;
  if (src->context) {
    GnomeVFSCancellation *cancel =
        gnome_vfs_context_get_cancellation (src->context);
    if (cancel)
      gnome_vfs_cancellation_cancel (cancel);
  }
  return TRUE;
}

/* Interrupt interrupt. */
static gboolean
gst_gnome_vfs_src_unlock_stop (GstBaseSrc * basesrc)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOME_VFS_SRC (basesrc);
  GST_DEBUG_OBJECT (src, "unlock_stop()");

  src->interrupted = FALSE;
  return TRUE;
}

static gboolean
gst_gnome_vfs_src_get_size (GstBaseSrc * basesrc, guint64 * size)
{
  GstGnomeVFSSrc *src;
  GnomeVFSFileInfo *info;
  GnomeVFSFileInfoOptions options;
  GnomeVFSResult res;

  src = GST_GNOME_VFS_SRC (basesrc);

  *size = -1;
  info = gnome_vfs_file_info_new ();
  options = GNOME_VFS_FILE_INFO_DEFAULT | GNOME_VFS_FILE_INFO_FOLLOW_LINKS;
  res = gnome_vfs_get_file_info_from_handle (src->handle, info, options);
  if (res == GNOME_VFS_OK) {
    if ((info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_SIZE) != 0) {
      *size = info->size;
      GST_DEBUG_OBJECT (src, "from handle: %" G_GUINT64_FORMAT " bytes", *size);
    } else if (src->own_handle && gnome_vfs_uri_is_local (src->uri)) {
      GST_DEBUG_OBJECT (src,
          "file size not known, file local, trying fallback");
      res = gnome_vfs_get_file_info_uri (src->uri, info, options);
      if (res == GNOME_VFS_OK &&
          (info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_SIZE) != 0) {
        *size = info->size;
        GST_DEBUG_OBJECT (src, "from uri: %" G_GUINT64_FORMAT " bytes", *size);
      }
    }
  } else {
    GST_WARNING_OBJECT (src, "getting info failed: %s",
        gnome_vfs_result_to_string (res));
  }
  gnome_vfs_file_info_unref (info);

  if (*size == (GnomeVFSFileSize) - 1)
    return FALSE;

  GST_DEBUG_OBJECT (src, "return size %" G_GUINT64_FORMAT, *size);

  return TRUE;
}

/* open the file, do stuff necessary to go to PAUSED state */
static gboolean
gst_gnome_vfs_src_start (GstBaseSrc * basesrc)
{
  GnomeVFSResult res;
  GstGnomeVFSSrc *src;

  src = GST_GNOME_VFS_SRC (basesrc);

  gst_gnome_vfs_src_push_callbacks (src);

  src->context = gnome_vfs_context_new ();
  if (src->uri != NULL) {
    GnomeVFSOpenMode mode = GNOME_VFS_OPEN_READ;

    /* this can block... */
    res = gnome_vfs_open_uri (&src->handle, src->uri, mode);
    if (res != GNOME_VFS_OK)
      goto open_failed;
    src->own_handle = TRUE;
  } else if (!src->handle) {
    goto no_filename;
  } else {
    src->own_handle = FALSE;
  }

  if (gnome_vfs_seek (src->handle, GNOME_VFS_SEEK_CURRENT, 0) == GNOME_VFS_OK) {
    src->seekable = TRUE;
  } else {
    src->seekable = FALSE;
  }

  return TRUE;

  /* ERRORS */
open_failed:
  {
    gchar *filename = gnome_vfs_uri_to_string (src->uri,
        GNOME_VFS_URI_HIDE_PASSWORD);

    gst_gnome_vfs_src_pop_callbacks (src);

    if (res == GNOME_VFS_ERROR_NOT_FOUND ||
        res == GNOME_VFS_ERROR_HOST_NOT_FOUND ||
        res == GNOME_VFS_ERROR_SERVICE_NOT_AVAILABLE) {
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
          ("Could not open vfs file \"%s\" for reading: %s (%d)",
              filename, gnome_vfs_result_to_string (res), res));
    } else {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
          ("Could not open vfs file \"%s\" for reading: %s (%d)",
              filename, gnome_vfs_result_to_string (res), res));
    }
    g_free (filename);
    return FALSE;
  }
no_filename:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), ("No filename given"));
    return FALSE;
  }
}

static gboolean
gst_gnome_vfs_src_stop (GstBaseSrc * basesrc)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOME_VFS_SRC (basesrc);

  gst_gnome_vfs_src_pop_callbacks (src);

  if (src->own_handle) {
    GnomeVFSResult res;

    res = gnome_vfs_close (src->handle);
    if (res != GNOME_VFS_OK) {
      GST_ELEMENT_ERROR (src, RESOURCE, CLOSE, (NULL),
          ("Could not close vfs handle: %s", gnome_vfs_result_to_string (res)));
    }
    src->handle = NULL;
  }
  src->curoffset = 0;
  src->interrupted = FALSE;
  gnome_vfs_context_free (src->context);
  src->context = NULL;

  return TRUE;
}
