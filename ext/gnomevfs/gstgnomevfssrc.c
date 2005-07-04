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

#define BROKEN_SIG 1
/*#undef BROKEN_SIG */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst/gst-i18n-plugin.h"

#include "gstgnomevfs.h"
#include "gstgnomevfsuri.h"

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
#include <gst/base/gstbasesrc.h>
#include <libgnomevfs/gnome-vfs.h>
/* gnome-vfs.h doesn't include the following header, which we need: */
#include <libgnomevfs/gnome-vfs-standard-callbacks.h>

#define GST_TYPE_GNOMEVFSSRC \
  (gst_gnomevfssrc_get_type())
#define GST_GNOMEVFSSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GNOMEVFSSRC,GstGnomeVFSSrc))
#define GST_GNOMEVFSSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GNOMEVFSSRC,GstGnomeVFSSrcClass))
#define GST_IS_GNOMEVFSSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GNOMEVFSSRC))
#define GST_IS_GNOMEVFSSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GNOMEVFSSRC))

static GStaticMutex count_lock = G_STATIC_MUTEX_INIT;
static gint ref_count = 0;
static gboolean vfs_owner = FALSE;

typedef struct _GstGnomeVFSSrc
{
  GstBaseSrc element;

  /* uri, file, ... */
  GnomeVFSURI *uri;
  gchar *uri_name;
  GnomeVFSHandle *handle;
  gboolean own_handle;
  GnomeVFSFileSize size;        /* -1 = unknown */
  GnomeVFSFileOffset curoffset; /* current offset in file */
  gboolean seekable;

  /* icecast/audiocast metadata extraction handling */
  gboolean iradio_mode;
  gboolean http_callbacks_pushed;

  gint icy_metaint;
  GnomeVFSFileSize icy_count;

  gchar *iradio_name;
  gchar *iradio_genre;
  gchar *iradio_url;
  gchar *iradio_title;

  GThread *audiocast_thread;
  GList *audiocast_notify_queue;
  GMutex *audiocast_queue_mutex;
  GMutex *audiocast_udpdata_mutex;
  gint audiocast_thread_die_infd;
  gint audiocast_thread_die_outfd;
  gint audiocast_port;
  gint audiocast_fd;
} GstGnomeVFSSrc;

typedef struct _GstGnomeVFSSrcClass
{
  GstBaseSrcClass parent_class;
} GstGnomeVFSSrcClass;

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

static void gst_gnomevfssrc_base_init (gpointer g_class);
static void gst_gnomevfssrc_class_init (GstGnomeVFSSrcClass * klass);
static void gst_gnomevfssrc_init (GstGnomeVFSSrc * gnomevfssrc);
static void gst_gnomevfssrc_finalize (GObject * object);
static void gst_gnomevfssrc_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void gst_gnomevfssrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gnomevfssrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gnomevfssrc_stop (GstBaseSrc * src);
static gboolean gst_gnomevfssrc_start (GstBaseSrc * src);
static gboolean gst_gnomevfssrc_is_seekable (GstBaseSrc * src);
static gboolean gst_gnomevfssrc_get_size (GstBaseSrc * src, guint64 * size);
static GstFlowReturn gst_gnomevfssrc_create (GstBaseSrc * basesrc,
    guint64 offset, guint size, GstBuffer ** buffer);

static int audiocast_init (GstGnomeVFSSrc * src);
static int audiocast_register_listener (gint * port, gint * fd);
static void audiocast_do_notifications (GstGnomeVFSSrc * src);
static gpointer audiocast_thread_run (GstGnomeVFSSrc * src);
static void audiocast_thread_kill (GstGnomeVFSSrc * src);

static GstElementClass *parent_class = NULL;

GType
gst_gnomevfssrc_get_type (void)
{
  static GType gnomevfssrc_type = 0;

  if (!gnomevfssrc_type) {
    static const GTypeInfo gnomevfssrc_info = {
      sizeof (GstGnomeVFSSrcClass),
      gst_gnomevfssrc_base_init,
      NULL,
      (GClassInitFunc) gst_gnomevfssrc_class_init,
      NULL,
      NULL,
      sizeof (GstGnomeVFSSrc),
      0,
      (GInstanceInitFunc) gst_gnomevfssrc_init,
    };
    static const GInterfaceInfo urihandler_info = {
      gst_gnomevfssrc_uri_handler_init,
      NULL,
      NULL
    };

    gnomevfssrc_type =
        g_type_register_static (GST_TYPE_BASESRC,
        "GstGnomeVFSSrc", &gnomevfssrc_info, 0);
    g_type_add_interface_static (gnomevfssrc_type, GST_TYPE_URI_HANDLER,
        &urihandler_info);
  }
  return gnomevfssrc_type;
}

static void
gst_gnomevfssrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  static GstElementDetails gst_gnomevfssrc_details =
      GST_ELEMENT_DETAILS ("GnomeVFS Source",
      "Source/File",
      "Read from any GnomeVFS-supported file",
      "Bastien Nocera <hadess@hadess.net>\n"
      "Ronald S. Bultje <rbultje@ronald.bitfreak.net>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_set_details (element_class, &gst_gnomevfssrc_details);
}

static void
gst_gnomevfssrc_class_init (GstGnomeVFSSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasesrc_class = GST_BASESRC_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->finalize = gst_gnomevfssrc_finalize;
  gobject_class->set_property = gst_gnomevfssrc_set_property;
  gobject_class->get_property = gst_gnomevfssrc_get_property;

  /* properties */
  gst_element_class_install_std_props (GST_ELEMENT_CLASS (klass),
      "location", ARG_LOCATION, G_PARAM_READWRITE, NULL);
  g_object_class_install_property (gobject_class,
      ARG_HANDLE,
      g_param_spec_pointer ("handle",
          "GnomeVFSHandle", "Handle for GnomeVFS", G_PARAM_READWRITE));

  /* icecast stuff */
  g_object_class_install_property (gobject_class,
      ARG_IRADIO_MODE,
      g_param_spec_boolean ("iradio-mode",
          "iradio-mode",
          "Enable internet radio mode (extraction of icecast/audiocast metadata)",
          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
      ARG_IRADIO_NAME,
      g_param_spec_string ("iradio-name",
          "iradio-name", "Name of the stream", NULL, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
      ARG_IRADIO_GENRE,
      g_param_spec_string ("iradio-genre",
          "iradio-genre", "Genre of the stream", NULL, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
      ARG_IRADIO_URL,
      g_param_spec_string ("iradio-url",
          "iradio-url",
          "Homepage URL for radio stream", NULL, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
      ARG_IRADIO_TITLE,
      g_param_spec_string ("iradio-title",
          "iradio-title",
          "Name of currently playing song", NULL, G_PARAM_READABLE));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_gnomevfssrc_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_gnomevfssrc_stop);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_gnomevfssrc_get_size);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_gnomevfssrc_is_seekable);
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_gnomevfssrc_create);
}

static void
gst_gnomevfssrc_init (GstGnomeVFSSrc * gnomevfssrc)
{
  gnomevfssrc->uri = NULL;
  gnomevfssrc->uri_name = NULL;
  gnomevfssrc->handle = NULL;
  gnomevfssrc->curoffset = 0;
  gnomevfssrc->seekable = FALSE;

  gnomevfssrc->icy_metaint = 0;
  gnomevfssrc->iradio_mode = FALSE;
  gnomevfssrc->http_callbacks_pushed = FALSE;
  gnomevfssrc->icy_count = 0;
  gnomevfssrc->iradio_name = NULL;
  gnomevfssrc->iradio_genre = NULL;
  gnomevfssrc->iradio_url = NULL;
  gnomevfssrc->iradio_title = NULL;

  gnomevfssrc->audiocast_udpdata_mutex = g_mutex_new ();
  gnomevfssrc->audiocast_queue_mutex = g_mutex_new ();
  gnomevfssrc->audiocast_notify_queue = NULL;
  gnomevfssrc->audiocast_thread = NULL;

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
gst_gnomevfssrc_finalize (GObject * object)
{
  GstGnomeVFSSrc *src = GST_GNOMEVFSSRC (object);

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

  if (src->uri_name) {
    g_free (src->uri_name);
    src->uri_name = NULL;
  }

  g_mutex_free (src->audiocast_udpdata_mutex);
  g_mutex_free (src->audiocast_queue_mutex);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/*
 * URI interface support.
 */

static guint
gst_gnomevfssrc_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_gnomevfssrc_uri_get_protocols (void)
{
  static gchar **protocols = NULL;

  if (!protocols)
    protocols = gst_gnomevfs_get_supported_uris ();

  return protocols;
}

static const gchar *
gst_gnomevfssrc_uri_get_uri (GstURIHandler * handler)
{
  GstGnomeVFSSrc *src = GST_GNOMEVFSSRC (handler);

  return src->uri_name;
}

static gboolean
gst_gnomevfssrc_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstGnomeVFSSrc *src = GST_GNOMEVFSSRC (handler);

  if (GST_STATE (src) == GST_STATE_PLAYING ||
      GST_STATE (src) == GST_STATE_PAUSED)
    return FALSE;

  g_object_set (G_OBJECT (src), "location", uri, NULL);

  return TRUE;
}

static void
gst_gnomevfssrc_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_gnomevfssrc_uri_get_type;
  iface->get_protocols = gst_gnomevfssrc_uri_get_protocols;
  iface->get_uri = gst_gnomevfssrc_uri_get_uri;
  iface->set_uri = gst_gnomevfssrc_uri_set_uri;
}

static void
gst_gnomevfssrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGnomeVFSSrc *src;
  gchar cwd[PATH_MAX];

  src = GST_GNOMEVFSSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
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

      if (g_value_get_string (value)) {
        const gchar *location = g_value_get_string (value);

        if (!strchr (location, ':')) {
          gchar *newloc = gnome_vfs_escape_path_string (location);

          if (*newloc == '/')
            src->uri_name = g_strdup_printf ("file://%s", newloc);
          else
            src->uri_name =
                g_strdup_printf ("file://%s/%s", getcwd (cwd, PATH_MAX),
                newloc);
          g_free (newloc);
        } else
          src->uri_name = g_strdup (location);

        src->uri = gnome_vfs_uri_new (src->uri_name);
      }
      break;
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
        src->handle = g_value_get_pointer (value);
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
gst_gnomevfssrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOMEVFSSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->uri_name);
      break;
    case ARG_HANDLE:
      g_value_set_pointer (value, src->handle);
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
      g_mutex_lock (src->audiocast_udpdata_mutex);
      g_value_set_string (value, src->iradio_title);
      g_mutex_unlock (src->audiocast_udpdata_mutex);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static char *
unicodify (const char *str, int len, ...)
{
  char *ret = NULL, *cset;
  va_list args;
  gsize bytes_read, bytes_written;

  if (g_utf8_validate (str, len, NULL))
    return g_strndup (str, len >= 0 ? len : strlen (str));

  va_start (args, len);
  while ((cset = va_arg (args, char *)) != NULL)
  {
    if (!strcmp (cset, "locale"))
      ret = g_locale_to_utf8 (str, len, &bytes_read, &bytes_written, NULL);
    else
      ret = g_convert (str, len, "UTF-8", cset,
          &bytes_read, &bytes_written, NULL);
    if (ret)
      break;
  }
  va_end (args);

  return ret;
}

static char *
gst_gnomevfssrc_unicodify (const char *str)
{
  return unicodify (str, -1, "locale", "ISO-8859-1", NULL);
}

/*
 * icecast/audiocast metadata extraction support code
 */

static int
audiocast_init (GstGnomeVFSSrc * src)
{
  int pipefds[2];
  GError *error = NULL;

  if (!src->iradio_mode)
    return TRUE;
  GST_DEBUG ("audiocast: registering listener");
  if (audiocast_register_listener (&src->audiocast_port,
          &src->audiocast_fd) < 0) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("Unable to listen on UDP port %d", src->audiocast_port));
    close (src->audiocast_fd);
    return FALSE;
  }
  GST_DEBUG ("audiocast: creating pipe");
  src->audiocast_notify_queue = NULL;
  if (pipe (pipefds) < 0) {
    close (src->audiocast_fd);
    return FALSE;
  }
  src->audiocast_thread_die_infd = pipefds[0];
  src->audiocast_thread_die_outfd = pipefds[1];
  GST_DEBUG ("audiocast: creating audiocast thread");
  src->audiocast_thread =
      g_thread_create ((GThreadFunc) audiocast_thread_run, src, TRUE, &error);
  if (error != NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, TOO_LAZY, (NULL),
        ("Unable to create thread: %s", error->message));
    close (src->audiocast_fd);
    return FALSE;
  }
  return TRUE;
}

static int
audiocast_register_listener (gint * port, gint * fd)
{
  struct sockaddr_in sin;
  int sock;
  socklen_t sinlen = sizeof (struct sockaddr_in);

  GST_DEBUG ("audiocast: estabilishing UDP listener");

  if ((sock = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
    goto lose;

  memset (&sin, 0, sinlen);
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = g_htonl (INADDR_ANY);

  if (bind (sock, (struct sockaddr *) &sin, sinlen) < 0)
    goto lose_and_close;

  memset (&sin, 0, sinlen);
  if (getsockname (sock, (struct sockaddr *) &sin, &sinlen) < 0)
    goto lose_and_close;

  GST_DEBUG ("audiocast: listening on local %s:%d", inet_ntoa (sin.sin_addr),
      g_ntohs (sin.sin_port));

  *port = g_ntohs (sin.sin_port);
  *fd = sock;

  return 0;
lose_and_close:
  close (sock);
lose:
  return -1;
}

static void
audiocast_do_notifications (GstGnomeVFSSrc * src)
{
  /* Send any pending notifications we got from the UDP thread. */
  if (src->iradio_mode) {
    GList *entry;

    g_mutex_lock (src->audiocast_queue_mutex);
    for (entry = src->audiocast_notify_queue; entry; entry = entry->next)
      g_object_notify (G_OBJECT (src), (const gchar *) entry->data);
    g_list_free (src->audiocast_notify_queue);
    src->audiocast_notify_queue = NULL;
    g_mutex_unlock (src->audiocast_queue_mutex);
  }
}

static gpointer
audiocast_thread_run (GstGnomeVFSSrc * src)
{
  char buf[1025], **lines;
  gsize len;
  fd_set fdset, readset;
  struct sockaddr_in from;
  socklen_t fromlen = sizeof (struct sockaddr_in);

  FD_ZERO (&fdset);

  FD_SET (src->audiocast_fd, &fdset);
  FD_SET (src->audiocast_thread_die_infd, &fdset);

  while (1) {
    GST_DEBUG ("audiocast thread: dropping into select");
    readset = fdset;
    if (select (FD_SETSIZE, &readset, NULL, NULL, NULL) < 0) {
      perror ("select");
      return NULL;
    }
    if (FD_ISSET (src->audiocast_thread_die_infd, &readset)) {
      char buf[1];

      GST_DEBUG ("audiocast thread: got die character");
      if (read (src->audiocast_thread_die_infd, buf, 1) != 1)
        g_warning ("gnomevfssrc: could not read from audiocast fd");
      close (src->audiocast_thread_die_infd);
      close (src->audiocast_fd);
      return NULL;
    }
    GST_DEBUG ("audiocast thread: reading data");
    len =
        recvfrom (src->audiocast_fd, buf, sizeof (buf) - 1, 0,
        (struct sockaddr *) &from, &fromlen);
    if (len < 0 && errno == EAGAIN)
      continue;
    else if (len >= 0) {
      int i;
      char *valptr, *value;

      buf[len] = '\0';
      lines = g_strsplit (buf, "\n", 0);
      if (!lines)
        continue;

      for (i = 0; lines[i]; i++) {
        while ((lines[i][strlen (lines[i]) - 1] == '\n') ||
            (lines[i][strlen (lines[i]) - 1] == '\r'))
          lines[i][strlen (lines[i]) - 1] = '\0';

        valptr = strchr (lines[i], ':');

        if (!valptr)
          continue;
        else
          valptr++;

        g_strstrip (valptr);
        if (!strlen (valptr))
          continue;

        value = gst_gnomevfssrc_unicodify (valptr);
        if (!value) {
          g_print ("Unable to convert \"%s\" to UTF-8!\n", valptr);
          continue;
        }

        if (!strncmp (lines[i], "x-audiocast-streamtitle", 23)) {
          g_mutex_lock (src->audiocast_udpdata_mutex);
          g_free (src->iradio_title);
          src->iradio_title = value;
          g_mutex_unlock (src->audiocast_udpdata_mutex);

          g_mutex_lock (src->audiocast_queue_mutex);
          src->audiocast_notify_queue =
              g_list_append (src->audiocast_notify_queue, "iradio-title");
          GST_DEBUG ("audiocast title: %s\n", src->iradio_title);
          g_mutex_unlock (src->audiocast_queue_mutex);
        } else if (!strncmp (lines[i], "x-audiocast-streamurl", 21)) {
          g_mutex_lock (src->audiocast_udpdata_mutex);
          g_free (src->iradio_url);
          src->iradio_url = value;
          g_mutex_unlock (src->audiocast_udpdata_mutex);

          g_mutex_lock (src->audiocast_queue_mutex);
          src->audiocast_notify_queue =
              g_list_append (src->audiocast_notify_queue, "iradio-url");
          GST_DEBUG ("audiocast url: %s\n", src->iradio_title);
          g_mutex_unlock (src->audiocast_queue_mutex);
        } else if (!strncmp (lines[i], "x-audiocast-udpseqnr", 20)) {
          gchar outbuf[120];

          sprintf (outbuf, "x-audiocast-ack: %ld \r\n", atol (value));
          g_free (value);

          if (sendto (src->audiocast_fd, outbuf, strlen (outbuf), 0,
                  (struct sockaddr *) &from, fromlen) <= 0) {
            g_print ("Error sending response to server: %s\n",
                strerror (errno));
            continue;
          }
          GST_DEBUG ("sent audiocast ack: %s\n", outbuf);
        }
      }
      g_strfreev (lines);
    }
  }
  return NULL;
}

static void
audiocast_thread_kill (GstGnomeVFSSrc * src)
{
  if (!src->audiocast_thread)
    return;

  /*
     We rely on this hack to kill the
     audiocast thread.  If we get icecast
     metadata, then we don't need the
     audiocast metadata too.
   */
  GST_DEBUG ("audiocast: writing die character");
  if (write (src->audiocast_thread_die_outfd, "q", 1) != 1)
    g_critical ("gnomevfssrc: could not write to audiocast thread fd");
  close (src->audiocast_thread_die_outfd);
  GST_DEBUG ("audiocast: joining thread");
  g_thread_join (src->audiocast_thread);
  src->audiocast_thread = NULL;
}

static void
gst_gnomevfssrc_send_additional_headers_callback (gconstpointer in,
    gsize in_size, gpointer out, gsize out_size, gpointer callback_data)
{
  GstGnomeVFSSrc *src = GST_GNOMEVFSSRC (callback_data);
  GnomeVFSModuleCallbackAdditionalHeadersOut *out_args =
      (GnomeVFSModuleCallbackAdditionalHeadersOut *) out;

  if (!src->iradio_mode)
    return;
  GST_DEBUG ("sending headers\n");

  out_args->headers = g_list_append (out_args->headers,
      g_strdup ("icy-metadata:1\r\n"));
  out_args->headers = g_list_append (out_args->headers,
      g_strdup_printf ("x-audiocast-udpport: %d\r\n", src->audiocast_port));
}

static void
gst_gnomevfssrc_received_headers_callback (gconstpointer in,
    gsize in_size, gpointer out, gsize out_size, gpointer callback_data)
{
  GList *i;
  gint icy_metaint;
  GstGnomeVFSSrc *src = GST_GNOMEVFSSRC (callback_data);
  GnomeVFSModuleCallbackReceivedHeadersIn *in_args =
      (GnomeVFSModuleCallbackReceivedHeadersIn *) in;

  /* This is only used for internet radio stuff right now */
  if (!src->iradio_mode)
    return;

  for (i = in_args->headers; i; i = i->next) {
    char *data = (char *) i->data;
    char *key = data;
    char *value = strchr (data, ':');

    if (!value)
      continue;

    value++;
    g_strstrip (value);
    if (!strlen (value))
      continue;

    /* Icecast stuff */
    if (strncmp (data, "icy-metaint:", 12) == 0) {      /* ugh */
      if (sscanf (data + 12, "%d", &icy_metaint) == 1) {
        src->icy_metaint = icy_metaint;
        GST_DEBUG ("got icy-metaint %d, killing audiocast thread",
            src->icy_metaint);
        audiocast_thread_kill (src);
        continue;
      }
    }

    if (!strncmp (data, "icy-", 4))
      key = data + 4;
    else if (!strncmp (data, "x-audiocast-", 12))
      key = data + 12;
    else
      continue;

    GST_DEBUG ("key: %s", key);
    if (!strncmp (key, "name", 4)) {
      g_free (src->iradio_name);
      src->iradio_name = gst_gnomevfssrc_unicodify (value);
      if (src->iradio_name)
        g_object_notify (G_OBJECT (src), "iradio-name");
    } else if (!strncmp (key, "genre", 5)) {
      g_free (src->iradio_genre);
      src->iradio_genre = gst_gnomevfssrc_unicodify (value);
      if (src->iradio_genre)
        g_object_notify (G_OBJECT (src), "iradio-genre");
    } else if (!strncmp (key, "url", 3)) {
      g_free (src->iradio_url);
      src->iradio_url = gst_gnomevfssrc_unicodify (value);
      if (src->iradio_url)
        g_object_notify (G_OBJECT (src), "iradio-url");
    }
  }
}

static void
gst_gnomevfssrc_push_callbacks (GstGnomeVFSSrc * gnomevfssrc)
{
  if (gnomevfssrc->http_callbacks_pushed)
    return;

  GST_DEBUG ("pushing callbacks");
  gnome_vfs_module_callback_push
      (GNOME_VFS_MODULE_CALLBACK_HTTP_SEND_ADDITIONAL_HEADERS,
      gst_gnomevfssrc_send_additional_headers_callback, gnomevfssrc, NULL);
  gnome_vfs_module_callback_push
      (GNOME_VFS_MODULE_CALLBACK_HTTP_RECEIVED_HEADERS,
      gst_gnomevfssrc_received_headers_callback, gnomevfssrc, NULL);

  gnomevfssrc->http_callbacks_pushed = TRUE;
}

static void
gst_gnomevfssrc_pop_callbacks (GstGnomeVFSSrc * gnomevfssrc)
{
  if (!gnomevfssrc->http_callbacks_pushed)
    return;

  GST_DEBUG ("popping callbacks");
  gnome_vfs_module_callback_pop
      (GNOME_VFS_MODULE_CALLBACK_HTTP_SEND_ADDITIONAL_HEADERS);
  gnome_vfs_module_callback_pop
      (GNOME_VFS_MODULE_CALLBACK_HTTP_RECEIVED_HEADERS);
}

static void
gst_gnomevfssrc_get_icy_metadata (GstGnomeVFSSrc * src)
{
  GnomeVFSFileSize length = 0;
  GnomeVFSResult res;
  gint metadata_length;
  guchar foobyte;
  guchar *data;
  guchar *pos;
  gchar **tags;
  int i;

  GST_DEBUG ("reading icecast metadata");

  while (length == 0) {
    res = gnome_vfs_read (src->handle, &foobyte, 1, &length);
    if (res != GNOME_VFS_OK)
      return;
  }

  metadata_length = foobyte * 16;

  if (metadata_length == 0)
    return;

  data = g_new (guchar, metadata_length + 1);
  pos = data;

  while (pos - data < metadata_length) {
    res = gnome_vfs_read (src->handle, pos,
        metadata_length - (pos - data), &length);
    /* FIXME: better error handling here? */
    if (res != GNOME_VFS_OK) {
      g_free (data);
      return;
    }

    pos += length;
  }

  data[metadata_length] = 0;
  tags = g_strsplit ((gchar *) data, "';", 0);

  for (i = 0; tags[i]; i++) {
    if (!g_ascii_strncasecmp (tags[i], "StreamTitle=", 12)) {
      g_free (src->iradio_title);
      src->iradio_title = gst_gnomevfssrc_unicodify (tags[i] + 13);
      if (src->iradio_title) {
        GST_DEBUG ("sending notification on icecast title");
        g_object_notify (G_OBJECT (src), "iradio-title");
      } else
        g_print ("Unable to convert icecast title \"%s\" to UTF-8!\n",
            tags[i] + 13);

    }
    if (!g_ascii_strncasecmp (tags[i], "StreamUrl=", 10)) {
      g_free (src->iradio_url);
      src->iradio_url = gst_gnomevfssrc_unicodify (tags[i] + 11);
      if (src->iradio_url) {
        GST_DEBUG ("sending notification on icecast url");
        g_object_notify (G_OBJECT (src), "iradio-url");
      } else
        g_print ("Unable to convert icecast url \"%s\" to UTF-8!\n",
            tags[i] + 11);
    }
  }

  g_strfreev (tags);
}

/* end of icecast/audiocast metadata extraction support code */

/*
 * Read a new buffer from src->reqoffset, takes care of events
 * and seeking and such.
 */
static GstFlowReturn
gst_gnomevfssrc_create (GstBaseSrc * basesrc, guint64 offset, guint size,
    GstBuffer ** buffer)
{
  GnomeVFSResult res;
  GstBuffer *buf;
  GnomeVFSFileSize readbytes;
  guint8 *data;
  GstGnomeVFSSrc *src;

  src = GST_GNOMEVFSSRC (basesrc);

  /* seek if required */
  if (src->curoffset != offset) {
    if (src->seekable) {
      res = gnome_vfs_seek (src->handle, GNOME_VFS_SEEK_START, offset);
      if (res != GNOME_VFS_OK) {
        GST_ERROR_OBJECT (src,
            "Failed to seek to requested position %lld: %s",
            offset, gnome_vfs_result_to_string (res));
        return GST_FLOW_ERROR;
      }
      src->curoffset = offset;
    } else {
      GST_ERROR_OBJECT (src,
          "Requested seek from %lld to %lld on non-seekable stream",
          src->curoffset, offset);
      return GST_FLOW_ERROR;
    }
  }

  audiocast_do_notifications (src);

  if (src->iradio_mode && src->icy_metaint > 0) {
    buf = gst_buffer_new_and_alloc (src->icy_metaint);

    data = GST_BUFFER_DATA (buf);

    /* try to read */
    GST_DEBUG ("doing read: icy_count: %" G_GINT64_FORMAT, src->icy_count);

    res = gnome_vfs_read (src->handle, data,
        src->icy_metaint - src->icy_count, &readbytes);

    if (res != GNOME_VFS_OK)
      goto read_failed;

    if (readbytes == 0)
      goto eos;

    src->icy_count += readbytes;
    GST_BUFFER_OFFSET (buf) = src->curoffset;
    GST_BUFFER_SIZE (buf) = readbytes;
    src->curoffset += readbytes;

    if (src->icy_count == src->icy_metaint) {
      gst_gnomevfssrc_get_icy_metadata (src);
      src->icy_count = 0;
    }
  } else {
    buf = gst_buffer_new_and_alloc (size);

    data = GST_BUFFER_DATA (buf);
    GST_BUFFER_OFFSET (buf) = src->curoffset;

    res = gnome_vfs_read (src->handle, data, size, &readbytes);

    GST_BUFFER_SIZE (buf) = readbytes;

    if (res != GNOME_VFS_OK)
      goto read_failed;

    if (readbytes == 0)
      goto eos;

    src->curoffset += readbytes;
  }

  /* we're done, return the buffer */
  *buffer = buf;

  return GST_FLOW_OK;

read_failed:
  {
    gst_buffer_unref (buf);
    GST_ERROR_OBJECT (src, "Failed to read data: %s",
        gnome_vfs_result_to_string (res));
    return GST_FLOW_ERROR;
  }
eos:
  {
    gst_buffer_unref (buf);
    GST_LOG_OBJECT (src, "Reading data gave EOS");
    return GST_FLOW_WRONG_STATE;
  }
}

static gboolean
gst_gnomevfssrc_is_seekable (GstBaseSrc * basesrc)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOMEVFSSRC (basesrc);

  return src->seekable;
}

static gboolean
gst_gnomevfssrc_get_size (GstBaseSrc * basesrc, guint64 * size)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOMEVFSSRC (basesrc);

  GST_DEBUG ("size %lld", src->size);

  if (src->size == (GnomeVFSFileSize) - 1)
    return FALSE;

  *size = src->size;

  return TRUE;
}

/* open the file, do stuff necessary to go to READY state */
static gboolean
gst_gnomevfssrc_start (GstBaseSrc * basesrc)
{
  GnomeVFSResult res;
  GnomeVFSFileInfo *info;
  GstGnomeVFSSrc *src;

  src = GST_GNOMEVFSSRC (basesrc);

  if (!audiocast_init (src))
    return FALSE;

  gst_gnomevfssrc_push_callbacks (src);

  if (src->uri != NULL) {
    if ((res = gnome_vfs_open_uri (&src->handle, src->uri,
                GNOME_VFS_OPEN_READ)) != GNOME_VFS_OK) {
      gchar *filename = gnome_vfs_uri_to_string (src->uri,
          GNOME_VFS_URI_HIDE_PASSWORD);

      gst_gnomevfssrc_pop_callbacks (src);
      audiocast_thread_kill (src);

      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
          ("Could not open vfs file \"%s\" for reading: %s",
              filename, gnome_vfs_result_to_string (res)));
      g_free (filename);
      return FALSE;
    }
    src->own_handle = TRUE;
  } else if (!src->handle) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), ("No filename given"));
    return FALSE;
  } else {
    src->own_handle = FALSE;
  }

  src->size = (GnomeVFSFileSize) - 1;
  info = gnome_vfs_file_info_new ();
  if ((res = gnome_vfs_get_file_info_from_handle (src->handle,
              info, GNOME_VFS_FILE_INFO_DEFAULT)) == GNOME_VFS_OK) {
    if (info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_SIZE) {
      src->size = info->size;
      GST_DEBUG_OBJECT (src, "size: %llu bytes", src->size);
    } else
      GST_LOG_OBJECT (src, "filesize not known");
  } else {
    GST_WARNING_OBJECT (src, "getting info failed: %s",
        gnome_vfs_result_to_string (res));
  }
  gnome_vfs_file_info_unref (info);

  audiocast_do_notifications (src);

  if (gnome_vfs_seek (src->handle, GNOME_VFS_SEEK_CURRENT, 0)
      == GNOME_VFS_OK) {
    src->seekable = TRUE;
  } else {
    src->seekable = FALSE;
  }

  return TRUE;
}

static gboolean
gst_gnomevfssrc_stop (GstBaseSrc * basesrc)
{
  GstGnomeVFSSrc *src;

  src = GST_GNOMEVFSSRC (basesrc);

  gst_gnomevfssrc_pop_callbacks (src);
  audiocast_thread_kill (src);

  if (src->own_handle) {
    gnome_vfs_close (src->handle);
    src->handle = NULL;
  }
  src->size = (GnomeVFSFileSize) - 1;
  src->curoffset = 0;

  return TRUE;
}
