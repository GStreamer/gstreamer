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

typedef enum
{
  GST_GNOMEVFSSRC_OPEN = GST_ELEMENT_FLAG_LAST,

  GST_GNOMEVFSSRC_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2,
}
GstGnomeVFSSrcFlags;

typedef struct _GstGnomeVFSSrc GstGnomeVFSSrc;
typedef struct _GstGnomeVFSSrcClass GstGnomeVFSSrcClass;

struct _GstGnomeVFSSrc
{
  GstElement element;
  /* pads */
  GstPad *srcpad;

  /* filename */
  gchar *filename;
  /* uri */
  GnomeVFSURI *uri;

  /* handle */
  GnomeVFSHandle *handle;
  /* Seek stuff */
  gboolean need_flush;

  /* details for fallback synchronous read */
  GnomeVFSFileSize size;
  GnomeVFSFileOffset curoffset; /* current offset in file */
  gulong bytes_per_read;        /* bytes per read */
  gboolean new_seek;

  /* icecast/audiocast metadata extraction handling */
  gboolean iradio_mode;
  gboolean http_callbacks_pushed;
  gboolean seekable;

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
};

struct _GstGnomeVFSSrcClass
{
  GstElementClass parent_class;
};

static GstElementDetails gst_gnomevfssrc_details =
GST_ELEMENT_DETAILS ("GnomeVFS Source",
    "Source/File",
    "Read from any GnomeVFS file",
    "Bastien Nocera <hadess@hadess.net>");


static const GstFormat *
gst_gnomevfssrc_get_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_BYTES,
    0,
  };

  return formats;
}

static const GstQueryType *
gst_gnomevfssrc_get_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0,
  };

  return types;
}

static const GstEventMask *
gst_gnomevfssrc_get_event_mask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_CUR |
          GST_SEEK_METHOD_SET | GST_SEEK_METHOD_END | GST_SEEK_FLAG_FLUSH},
    {GST_EVENT_FLUSH, 0},
    {GST_EVENT_SIZE, 0},
    {0, 0},
  };

  return masks;
}

/* GnomeVFSSrc signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_HANDLE,
  ARG_LOCATION,
  ARG_BYTESPERREAD,
  ARG_IRADIO_MODE,
  ARG_IRADIO_NAME,
  ARG_IRADIO_GENRE,
  ARG_IRADIO_URL,
  ARG_IRADIO_TITLE,
  ARG_SEEKABLE,
};

static void gst_gnomevfssrc_base_init (gpointer g_class);
static void gst_gnomevfssrc_class_init (GstGnomeVFSSrcClass * klass);
static void gst_gnomevfssrc_init (GstGnomeVFSSrc * gnomevfssrc);
static void gst_gnomevfssrc_dispose (GObject * object);

static void gst_gnomevfssrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gnomevfssrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstData *gst_gnomevfssrc_get (GstPad * pad);

static GstElementStateReturn
gst_gnomevfssrc_change_state (GstElement * element);

static void gst_gnomevfssrc_close_file (GstGnomeVFSSrc * src);
static gboolean gst_gnomevfssrc_open_file (GstGnomeVFSSrc * src);
static gboolean gst_gnomevfssrc_srcpad_event (GstPad * pad, GstEvent * event);
static gboolean gst_gnomevfssrc_srcpad_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value);

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

    gnomevfssrc_type =
        g_type_register_static (GST_TYPE_ELEMENT,
        "GstGnomeVFSSrc", &gnomevfssrc_info, 0);
  }
  return gnomevfssrc_type;
}

static void
gst_gnomevfssrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_gnomevfssrc_details);
}

static void
gst_gnomevfssrc_class_init (GstGnomeVFSSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gst_element_class_install_std_props (GST_ELEMENT_CLASS (klass),
      "bytesperread", ARG_BYTESPERREAD, G_PARAM_READWRITE,
      "location", ARG_LOCATION, G_PARAM_READWRITE, NULL);

  gobject_class->dispose = gst_gnomevfssrc_dispose;

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
  g_object_class_install_property (gobject_class,
      ARG_SEEKABLE,
      g_param_spec_boolean ("seekable",
          "seekable", "TRUE is stream is seekable", FALSE, G_PARAM_READABLE));

  gstelement_class->set_property = gst_gnomevfssrc_set_property;
  gstelement_class->get_property = gst_gnomevfssrc_get_property;

  gstelement_class->change_state = gst_gnomevfssrc_change_state;
}

static void
gst_gnomevfssrc_init (GstGnomeVFSSrc * gnomevfssrc)
{
  gnomevfssrc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (gnomevfssrc->srcpad, gst_gnomevfssrc_get);
  gst_pad_set_event_mask_function (gnomevfssrc->srcpad,
      gst_gnomevfssrc_get_event_mask);
  gst_pad_set_event_function (gnomevfssrc->srcpad,
      gst_gnomevfssrc_srcpad_event);
  gst_pad_set_query_type_function (gnomevfssrc->srcpad,
      gst_gnomevfssrc_get_query_types);
  gst_pad_set_query_function (gnomevfssrc->srcpad,
      gst_gnomevfssrc_srcpad_query);
  gst_pad_set_formats_function (gnomevfssrc->srcpad,
      gst_gnomevfssrc_get_formats);
  gst_element_add_pad (GST_ELEMENT (gnomevfssrc), gnomevfssrc->srcpad);

  gnomevfssrc->uri = NULL;
  gnomevfssrc->handle = NULL;
  gnomevfssrc->curoffset = 0;
  gnomevfssrc->bytes_per_read = 4096;
  gnomevfssrc->new_seek = FALSE;

  gnomevfssrc->icy_metaint = 0;

  gnomevfssrc->seekable = FALSE;

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
gst_gnomevfssrc_dispose (GObject * object)
{
  g_static_mutex_lock (&count_lock);
  ref_count--;
  if (ref_count == 0 && vfs_owner) {
    if (gnome_vfs_initialized () == TRUE) {
      gnome_vfs_shutdown ();
    }
  }
  g_static_mutex_unlock (&count_lock);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
gst_gnomevfssrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGnomeVFSSrc *src;
  const gchar *location;
  gchar cwd[PATH_MAX];

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_GNOMEVFSSRC (object));

  src = GST_GNOMEVFSSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      /* the element must be stopped or paused in order to do this */
      g_return_if_fail ((GST_STATE (src) < GST_STATE_PLAYING)
          || (GST_STATE (src) == GST_STATE_PAUSED));

      g_free (src->filename);

      /* clear the filename if we get a NULL */
      if (g_value_get_string (value) == NULL) {
        gst_element_set_state (GST_ELEMENT (object), GST_STATE_NULL);
        src->filename = NULL;
      } else {
        /* otherwise set the new filename */
        location = g_value_get_string (value);
        /* if it's not a proper uri, default to file: -- this
         * is a crude test */
        if (!strchr (location, ':')) {
          gchar *newloc = gnome_vfs_escape_path_string (location);

          if (*newloc == '/')
            src->filename = g_strdup_printf ("file://%s", newloc);
          else
            src->filename =
                g_strdup_printf ("file://%s/%s", getcwd (cwd, PATH_MAX),
                newloc);
          g_free (newloc);
        } else
          src->filename = g_strdup (g_value_get_string (value));
      }

      if ((GST_STATE (src) == GST_STATE_PAUSED)
          && (src->filename != NULL)) {
        gst_gnomevfssrc_close_file (src);
        gst_gnomevfssrc_open_file (src);
      }
      break;
    case ARG_HANDLE:
      src->handle = g_value_get_pointer (value);
      break;
    case ARG_BYTESPERREAD:
      src->bytes_per_read = g_value_get_int (value);
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

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_GNOMEVFSSRC (object));

  src = GST_GNOMEVFSSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->filename);
      break;
    case ARG_BYTESPERREAD:
      g_value_set_int (value, src->bytes_per_read);
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
    case ARG_SEEKABLE:
      g_value_set_boolean (value, src->seekable);
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
      read (src->audiocast_thread_die_infd, buf, 1);
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
  write (src->audiocast_thread_die_outfd, "q", 1);
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
    if (!strncmp (data, "icy-metaint:", 12)) {  /* ugh */
      sscanf (data + 12, "%d", &icy_metaint);
      src->icy_metaint = icy_metaint;
      GST_DEBUG ("got icy-metaint %d, killing audiocast thread",
          src->icy_metaint);
      audiocast_thread_kill (src);
      continue;
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

  data = g_new (gchar, metadata_length + 1);
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
  tags = g_strsplit (data, "';", 0);

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

/**
 * gst_gnomevfssrc_get:
 * @pad: #GstPad to push a buffer from
 *
 * Push a new buffer from the gnomevfssrc at the current offset.
 */
static GstData *
gst_gnomevfssrc_get (GstPad * pad)
{
  GstGnomeVFSSrc *src;
  GnomeVFSResult result = 0;
  GstBuffer *buf;
  GnomeVFSFileSize readbytes;
  guint8 *data;

  g_return_val_if_fail (pad != NULL, NULL);
  src = GST_GNOMEVFSSRC (gst_pad_get_parent (pad));
  g_return_val_if_fail (GST_FLAG_IS_SET (src, GST_GNOMEVFSSRC_OPEN), NULL);

  /* deal with EOF state */
  if ((src->curoffset >= src->size) && (src->size != 0)) {
    gst_element_set_eos (GST_ELEMENT (src));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  }

  /* create the buffer */
  /* FIXME: should eventually use a bufferpool for this */
  buf = gst_buffer_new ();
  g_return_val_if_fail (buf, NULL);

  audiocast_do_notifications (src);

  if (src->iradio_mode && src->icy_metaint > 0) {
    GST_BUFFER_DATA (buf) = g_malloc0 (src->icy_metaint);
    data = GST_BUFFER_DATA (buf);
    g_return_val_if_fail (GST_BUFFER_DATA (buf) != NULL, NULL);

    GST_BUFFER_SIZE (buf) = 0;
    GST_DEBUG ("doing read: icy_count: %" G_GINT64_FORMAT, src->icy_count);
    result = gnome_vfs_read (src->handle, data,
        src->icy_metaint - src->icy_count, &readbytes);

    /* EOS? */
    if (readbytes == 0) {
      gst_buffer_unref (buf);
      gst_element_set_eos (GST_ELEMENT (src));
      return GST_DATA (gst_event_new (GST_EVENT_EOS));
    }

    src->icy_count += readbytes;
    GST_BUFFER_OFFSET (buf) = src->curoffset;
    GST_BUFFER_SIZE (buf) += readbytes;
    data += readbytes;
    src->curoffset += readbytes;

    if (src->icy_count == src->icy_metaint) {
      gst_gnomevfssrc_get_icy_metadata (src);
      src->icy_count = 0;
    }
  } else {
    /* allocate the space for the buffer data */
    GST_BUFFER_DATA (buf) = g_malloc (src->bytes_per_read);
    g_return_val_if_fail (GST_BUFFER_DATA (buf) != NULL, NULL);

    if (src->need_flush) {
      GstEvent *event = gst_event_new_flush ();

      src->need_flush = FALSE;
      gst_buffer_unref (buf);
      GST_DEBUG ("gnomevfssrc sending flush");
      return GST_DATA (event);
    }

    if (src->new_seek) {
      GstEvent *event;

      gst_buffer_unref (buf);
      GST_DEBUG ("new seek %" G_GINT64_FORMAT, src->curoffset);
      src->new_seek = FALSE;
      GST_DEBUG ("gnomevfssrc sending discont");
      event =
          gst_event_new_discontinuous (FALSE, GST_FORMAT_BYTES, src->curoffset,
          NULL);
      return GST_DATA (event);
    }

    result = gnome_vfs_read (src->handle, GST_BUFFER_DATA (buf),
        src->bytes_per_read, &readbytes);

    GST_DEBUG ("read: %s, readbytes: %" G_GINT64_FORMAT,
        gnome_vfs_result_to_string (result), readbytes);
    /* deal with EOS */
    if (readbytes == 0) {
      gst_buffer_unref (buf);

      gst_element_set_eos (GST_ELEMENT (src));

      return GST_DATA (gst_event_new (GST_EVENT_EOS));
    }

    GST_BUFFER_OFFSET (buf) = src->curoffset;
    GST_BUFFER_SIZE (buf) = readbytes;
    src->curoffset += readbytes;
  }

  GST_BUFFER_TIMESTAMP (buf) = -1;

  /* we're done, return the buffer */
  return GST_DATA (buf);
}

/* open the file, do stuff necessary to go to READY state */
static gboolean
gst_gnomevfssrc_open_file (GstGnomeVFSSrc * src)
{
  GnomeVFSResult result;
  GnomeVFSFileInfo *info;

  g_return_val_if_fail (!GST_FLAG_IS_SET (src, GST_GNOMEVFSSRC_OPEN), FALSE);

  if (src->filename) {
    /* create the uri */
    src->uri = gnome_vfs_uri_new (src->filename);
    if (!src->uri) {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
          (_("Could not open vfs file \"%s\" for reading."), src->filename),
          GST_ERROR_SYSTEM);
      return FALSE;
    }
  }

  if (!audiocast_init (src))
    return FALSE;

  gst_gnomevfssrc_push_callbacks (src);

  if (src->filename)
    result = gnome_vfs_open_uri (&(src->handle), src->uri, GNOME_VFS_OPEN_READ);
  else
    result = GNOME_VFS_OK;
  if (result != GNOME_VFS_OK) {
    char *escaped;

    gst_gnomevfssrc_pop_callbacks (src);
    audiocast_thread_kill (src);

    escaped = gnome_vfs_unescape_string_for_display (src->filename);
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("Could not open vfs file \"%s\" for reading."), escaped),
        (gnome_vfs_result_to_string (result)));
    g_free (escaped);
    return FALSE;
  }

  info = gnome_vfs_file_info_new ();
  if (gnome_vfs_get_file_info_from_handle (src->handle, info,
          GNOME_VFS_FILE_INFO_DEFAULT)
      == GNOME_VFS_OK) {
    if (info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_SIZE)
      src->size = info->size;
  } else
    GST_DEBUG ("getting info failed: %s", gnome_vfs_result_to_string (result));

  gnome_vfs_file_info_unref (info);

  GST_DEBUG ("size %" G_GINT64_FORMAT, src->size);

  audiocast_do_notifications (src);

  GST_DEBUG ("open result: %s", gnome_vfs_result_to_string (result));

  if (gnome_vfs_seek (src->handle, GNOME_VFS_SEEK_CURRENT, 0)
      == GNOME_VFS_OK) {
    src->seekable = TRUE;
  } else {
    src->seekable = FALSE;
  }

  GST_FLAG_SET (src, GST_GNOMEVFSSRC_OPEN);

  return TRUE;
}

static void
gst_gnomevfssrc_close_file (GstGnomeVFSSrc * src)
{
  g_return_if_fail (GST_FLAG_IS_SET (src, GST_GNOMEVFSSRC_OPEN));

  gst_gnomevfssrc_pop_callbacks (src);
  audiocast_thread_kill (src);

  if (src->filename) {
    gnome_vfs_close (src->handle);

    gnome_vfs_uri_unref (src->uri);
  }
  src->size = 0;
  src->curoffset = 0;
  src->new_seek = FALSE;

  GST_FLAG_UNSET (src, GST_GNOMEVFSSRC_OPEN);
}

static GstElementStateReturn
gst_gnomevfssrc_change_state (GstElement * element)
{
  g_return_val_if_fail (GST_IS_GNOMEVFSSRC (element), GST_STATE_FAILURE);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      if (!GST_FLAG_IS_SET (element, GST_GNOMEVFSSRC_OPEN)) {
        if (!gst_gnomevfssrc_open_file (GST_GNOMEVFSSRC (element)))
          return GST_STATE_FAILURE;
      }
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (GST_FLAG_IS_SET (element, GST_GNOMEVFSSRC_OPEN))
        gst_gnomevfssrc_close_file (GST_GNOMEVFSSRC (element));
      break;
    case GST_STATE_NULL_TO_READY:
    case GST_STATE_READY_TO_NULL:
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
gst_gnomevfssrc_srcpad_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstGnomeVFSSrc *src = GST_GNOMEVFSSRC (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_TOTAL:
      if (*format != GST_FORMAT_BYTES) {
        return FALSE;
      }
      *value = src->size;
      break;
    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_BYTES:
          *value = src->curoffset;
          break;
        case GST_FORMAT_PERCENT:
          if (src->size == 0)
            return FALSE;
          *value = src->curoffset * GST_FORMAT_PERCENT_MAX / src->size;
          break;
        default:
          return FALSE;
      }
      break;
    default:
      return FALSE;
      break;
  }
  return TRUE;
}

static gboolean
gst_gnomevfssrc_srcpad_event (GstPad * pad, GstEvent * event)
{
  GstGnomeVFSSrc *src = GST_GNOMEVFSSRC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gint64 desired_offset;
      GnomeVFSResult result;

      if (GST_EVENT_SEEK_FORMAT (event) != GST_FORMAT_BYTES) {
        gst_event_unref (event);
        return FALSE;
      }
      switch (GST_EVENT_SEEK_METHOD (event)) {
        case GST_SEEK_METHOD_SET:
          desired_offset = (guint64) GST_EVENT_SEEK_OFFSET (event);
          break;
        case GST_SEEK_METHOD_CUR:
          desired_offset = src->curoffset + GST_EVENT_SEEK_OFFSET (event);
          break;
        case GST_SEEK_METHOD_END:
          desired_offset = src->size - ABS (GST_EVENT_SEEK_OFFSET (event));
          break;
        default:
          gst_event_unref (event);
          return FALSE;
          break;
      }

      result = gnome_vfs_seek (src->handle,
          GNOME_VFS_SEEK_START, desired_offset);
      GST_DEBUG ("new_seek: %s", gnome_vfs_result_to_string (result));

      if (result != GNOME_VFS_OK) {
        gst_event_unref (event);
        return FALSE;
      }
      src->curoffset = desired_offset;
      src->new_seek = TRUE;
      src->need_flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;
      break;
    }
    case GST_EVENT_SIZE:
      if (GST_EVENT_SIZE_FORMAT (event) != GST_FORMAT_BYTES) {
        gst_event_unref (event);
        return FALSE;
      }
      src->bytes_per_read = GST_EVENT_SIZE_VALUE (event);
      g_object_notify (G_OBJECT (src), "bytesperread");
      break;

    case GST_EVENT_FLUSH:
      src->need_flush = TRUE;
      break;
    default:
      gst_event_unref (event);
      return FALSE;
      break;
  }
  gst_event_unref (event);

  return TRUE;
}
