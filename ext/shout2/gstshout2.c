/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include "gstshout2.h"
#include <stdlib.h>
#include <string.h>

/* elementfactory information */
static GstElementDetails shout2send_details = {
  "An Icecast plugin",
  "Sink/Network",
  "Sends data to an icecast server",
  "Wim Taymans <wim.taymans@chello.be>\n" "Pedro Corte-Real <typo@netcabo.pt>"
};

unsigned int audio_format = 100;

/* Shout2send signals and args */
enum
{
  /* FILL ME */
  SIGNAL_CONNECTION_PROBLEM,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_IP,                       /* the ip of the server */
  ARG_PORT,                     /* the encoder port number on the server */
  ARG_PASSWORD,                 /* the encoder password on the server */
  ARG_PUBLIC,                   /* is this stream public? */
  ARG_NAME,                     /* Name of the stream */
  ARG_DESCRIPTION,              /* Description of the stream */
  ARG_GENRE,                    /* Genre of the stream */

  ARG_PROTOCOL,                 /* Protocol to connect with */

  ARG_MOUNT,                    /* mountpoint of stream (icecast only) */
  ARG_URL                       /* Url of stream (I'm guessing) */
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/ogg; "
        "audio/mpeg, mpegversion = (int) 1, layer = (int) [ 1, 3 ]")
    );

static void gst_shout2send_class_init (GstShout2sendClass * klass);
static void gst_shout2send_base_init (GstShout2sendClass * klass);
static void gst_shout2send_init (GstShout2send * shout2send);

static void gst_shout2send_chain (GstPad * pad, GstData * _data);
static GstPadLinkReturn gst_shout2send_connect (GstPad * pad,
    const GstCaps * caps);

static void gst_shout2send_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_shout2send_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementStateReturn gst_shout2send_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

static guint gst_shout2send_signals[LAST_SIGNAL] = { 0, 0 };

#define GST_TYPE_SHOUT_PROTOCOL (gst_shout2send_protocol_get_type())
static GType
gst_shout2send_protocol_get_type (void)
{
  static GType shout2send_protocol_type = 0;
  static GEnumValue shout2send_protocol[] = {
    {SHOUT2SEND_PROTOCOL_XAUDIOCAST, "1",
        "Xaudiocast Protocol (icecast 1.3.x)"},
    {SHOUT2SEND_PROTOCOL_ICY, "2", "Icy Protocol (ShoutCast)"},
    {SHOUT2SEND_PROTOCOL_HTTP, "3", "Http Protocol (icecast 2.x)"},
    {0, NULL, NULL},
  };

  if (!shout2send_protocol_type) {
    shout2send_protocol_type =
        g_enum_register_static ("GstShout2SendProtocol", shout2send_protocol);
  }
  return shout2send_protocol_type;
}

GType
gst_shout2send_get_type (void)
{
  static GType shout2send_type = 0;

  if (!shout2send_type) {
    static const GTypeInfo shout2send_info = {
      sizeof (GstShout2sendClass),
      (GBaseInitFunc) gst_shout2send_base_init,
      NULL,
      (GClassInitFunc) gst_shout2send_class_init,
      NULL,
      NULL,
      sizeof (GstShout2send),
      0,
      (GInstanceInitFunc) gst_shout2send_init,
    };

    shout2send_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstShout2send",
        &shout2send_info, 0);
  }
  return shout2send_type;
}

static void
gst_shout2send_base_init (GstShout2sendClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_details (element_class, &shout2send_details);
}

static void
gst_shout2send_class_init (GstShout2sendClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_IP, g_param_spec_string ("ip", "ip", "ip", NULL, G_PARAM_READWRITE));    /* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PORT, g_param_spec_int ("port", "port", "port", 1, G_MAXUSHORT, 8000, G_PARAM_READWRITE));       /* CHECKME */

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PASSWORD, g_param_spec_string ("password", "password", "password", NULL, G_PARAM_READWRITE));    /* CHECKME */

  /* metadata */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NAME, g_param_spec_string ("name", "name", "name", NULL, G_PARAM_READWRITE));    /* CHECKME */

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DESCRIPTION, g_param_spec_string ("description", "description", "description", NULL, G_PARAM_READWRITE));        /* CHECKME */

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_GENRE, g_param_spec_string ("genre", "genre", "genre", NULL, G_PARAM_READWRITE));        /* CHECKME */

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PROTOCOL,
      g_param_spec_enum ("protocol", "protocol", "Connection Protocol to use",
          GST_TYPE_SHOUT_PROTOCOL, SHOUT2SEND_PROTOCOL_HTTP,
          G_PARAM_READWRITE));


  /* icecast only */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MOUNT, g_param_spec_string ("mount", "mount", "mount", NULL, G_PARAM_READWRITE));        /* CHECKME */

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_URL, g_param_spec_string ("url", "url", "url", NULL, G_PARAM_READWRITE));        /* CHECKME */


  /* signals */
  gst_shout2send_signals[SIGNAL_CONNECTION_PROBLEM] =
      g_signal_new ("connection-problem", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_CLEANUP, G_STRUCT_OFFSET (GstShout2sendClass,
          connection_problem), NULL, NULL, g_cclosure_marshal_VOID__INT,
      G_TYPE_NONE, 1, G_TYPE_INT);
  gobject_class->set_property = gst_shout2send_set_property;
  gobject_class->get_property = gst_shout2send_get_property;

  gstelement_class->change_state = gst_shout2send_change_state;
}

static void
gst_shout2send_init (GstShout2send * shout2send)
{
  shout2send->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_element_add_pad (GST_ELEMENT (shout2send), shout2send->sinkpad);
  gst_pad_set_chain_function (shout2send->sinkpad, gst_shout2send_chain);

  gst_pad_set_link_function (shout2send->sinkpad, gst_shout2send_connect);

  shout2send->ip = g_strdup ("127.0.0.1");
  shout2send->port = 8000;
  shout2send->password = g_strdup ("hackme");
  shout2send->name = g_strdup ("");
  shout2send->description = g_strdup ("");
  shout2send->genre = g_strdup ("");
  shout2send->mount = g_strdup ("");
  shout2send->url = g_strdup ("");
  shout2send->protocol = SHOUT2SEND_PROTOCOL_HTTP;
}

static void
gst_shout2send_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstShout2send *shout2send;
  glong ret;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  shout2send = GST_SHOUT2SEND (GST_OBJECT_PARENT (pad));

  g_return_if_fail (shout2send != NULL);
  g_return_if_fail (GST_IS_SHOUT2SEND (shout2send));

  ret = shout_send (shout2send->conn, GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));
  if (ret != SHOUTERR_SUCCESS) {
    GST_WARNING ("send error: %s...\n", shout_get_error (shout2send->conn));
    g_signal_emit (G_OBJECT (shout2send),
        gst_shout2send_signals[SIGNAL_CONNECTION_PROBLEM], 0,
        shout_get_errno (shout2send->conn));
  }

  shout_sync (shout2send->conn);

  gst_buffer_unref (buf);
}

static void
gst_shout2send_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstShout2send *shout2send;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SHOUT2SEND (object));
  shout2send = GST_SHOUT2SEND (object);

  switch (prop_id) {

    case ARG_IP:
      if (shout2send->ip)
        g_free (shout2send->ip);
      shout2send->ip = g_strdup (g_value_get_string (value));
      break;

    case ARG_PORT:
      shout2send->port = g_value_get_int (value);
      break;

    case ARG_PASSWORD:
      if (shout2send->password)
        g_free (shout2send->password);
      shout2send->password = g_strdup (g_value_get_string (value));
      break;

    case ARG_NAME:             /* Name of the stream */
      if (shout2send->name)
        g_free (shout2send->name);
      shout2send->name = g_strdup (g_value_get_string (value));
      break;

    case ARG_DESCRIPTION:      /* Description of the stream */
      if (shout2send->description)
        g_free (shout2send->description);
      shout2send->description = g_strdup (g_value_get_string (value));
      break;

    case ARG_GENRE:            /* Genre of the stream */
      if (shout2send->genre)
        g_free (shout2send->genre);
      shout2send->genre = g_strdup (g_value_get_string (value));
      break;

    case ARG_PROTOCOL:         /* protocol to connect with */
      shout2send->protocol = g_value_get_enum (value);
      break;

    case ARG_MOUNT:            /* mountpoint of stream (icecast only) */
      if (shout2send->mount)
        g_free (shout2send->mount);
      shout2send->mount = g_strdup (g_value_get_string (value));
      break;

    case ARG_URL:              /* Url of the stream (I'm guessing) */
      if (shout2send->url)
        g_free (shout2send->url);
      shout2send->url = g_strdup (g_value_get_string (value));
      break;

    default:
      break;
  }
}

static void
gst_shout2send_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstShout2send *shout2send;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SHOUT2SEND (object));
  shout2send = GST_SHOUT2SEND (object);

  switch (prop_id) {

    case ARG_IP:
      g_value_set_string (value, shout2send->ip);
      break;
    case ARG_PORT:
      g_value_set_int (value, shout2send->port);
      break;
    case ARG_PASSWORD:
      g_value_set_string (value, shout2send->password);
      break;

    case ARG_NAME:             /* Name of the stream */
      g_value_set_string (value, shout2send->name);
      break;

    case ARG_DESCRIPTION:      /* Description of the stream */
      g_value_set_string (value, shout2send->description);
      break;

    case ARG_GENRE:            /* Genre of the stream */
      g_value_set_string (value, shout2send->genre);
      break;

    case ARG_PROTOCOL:         /* protocol to connect with */
      g_value_set_enum (value, shout2send->protocol);
      break;

    case ARG_MOUNT:            /* mountpoint of stream (icecast only) */
      g_value_set_string (value, shout2send->mount);
      break;

    case ARG_URL:              /* Url of stream (I'm guessing) */
      g_value_set_string (value, shout2send->url);
      break;


    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPadLinkReturn
gst_shout2send_connect (GstPad * pad, const GstCaps * caps)
{
  const gchar *mimetype;

  mimetype = gst_structure_get_name (gst_caps_get_structure (caps, 0));
  if (!strcmp (mimetype, "audio/mpeg")) {
    audio_format = SHOUT_FORMAT_MP3;
    return GST_PAD_LINK_OK;
  }

  if (!strcmp (mimetype, "application/ogg")) {
    audio_format = SHOUT_FORMAT_VORBIS;
    return GST_PAD_LINK_OK;
  } else {
    return GST_PAD_LINK_REFUSED;
  }

}

static GstElementStateReturn
gst_shout2send_change_state (GstElement * element)
{
  GstShout2send *shout2send;

  guint major, minor, micro;
  gshort proto = 3;

  gchar *version_string;

  g_return_val_if_fail (GST_IS_SHOUT2SEND (element), GST_STATE_FAILURE);

  shout2send = GST_SHOUT2SEND (element);

  GST_DEBUG ("state pending %d", GST_STATE_PENDING (element));

  /* if going down into NULL state, close the file if it's open */
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      shout2send->conn = shout_new ();

      switch (shout2send->protocol) {
        case SHOUT2SEND_PROTOCOL_XAUDIOCAST:
          proto = SHOUT_PROTOCOL_XAUDIOCAST;
          break;
        case SHOUT2SEND_PROTOCOL_ICY:
          proto = SHOUT_PROTOCOL_ICY;
          break;
        case SHOUT2SEND_PROTOCOL_HTTP:
          proto = SHOUT_PROTOCOL_HTTP;
          break;
      }

      if (shout_set_protocol (shout2send->conn, proto) != SHOUTERR_SUCCESS) {
        g_error ("Error setting protocol: %s\n",
            shout_get_error (shout2send->conn));
      }

      /* --- FIXME: shout requires an ip, and fails if it is given a host. */
      /* may want to put convert_to_ip(shout2send->ip) here */


      if (shout_set_host (shout2send->conn, shout2send->ip) != SHOUTERR_SUCCESS) {
        g_error ("Error setting host: %s\n",
            shout_get_error (shout2send->conn));
      }
      /* --- */

      if (shout_set_port (shout2send->conn,
              shout2send->port) != SHOUTERR_SUCCESS) {
        g_error ("Error setting port: %s\n",
            shout_get_error (shout2send->conn));
      }

      if (shout_set_password (shout2send->conn,
              shout2send->password) != SHOUTERR_SUCCESS) {
        g_error ("Error setting password: %s\n",
            shout_get_error (shout2send->conn));
      }

      if (shout_set_name (shout2send->conn,
              shout2send->name) != SHOUTERR_SUCCESS) {
        g_error ("Error setting name: %s\n",
            shout_get_error (shout2send->conn));
      }

      if (shout_set_description (shout2send->conn,
              shout2send->description) != SHOUTERR_SUCCESS) {
        g_error ("Error setting name: %s\n",
            shout_get_error (shout2send->conn));
      }

      if (shout_set_genre (shout2send->conn,
              shout2send->genre) != SHOUTERR_SUCCESS) {
        g_error ("Error setting name: %s\n",
            shout_get_error (shout2send->conn));
      }

      if (shout_set_mount (shout2send->conn,
              shout2send->mount) != SHOUTERR_SUCCESS) {
        g_error ("Error setting mount point: %s\n",
            shout_get_error (shout2send->conn));
      }

      if (shout_set_user (shout2send->conn, "source") != SHOUTERR_SUCCESS) {
        g_error ("Error setting user: %s\n",
            shout_get_error (shout2send->conn));
      }

      gst_version (&major, &minor, &micro);

      version_string =
          g_strdup_printf ("GStreamer %d.%d.%d", major, minor, micro);

      if (shout_set_agent (shout2send->conn,
              version_string) != SHOUTERR_SUCCESS) {
        g_error ("Error setting agent: %s\n",
            shout_get_error (shout2send->conn));
      }

      g_free (version_string);



      break;
    case GST_STATE_READY_TO_PAUSED:

      /* This sets the format acording to the capabilities of what
         we are being given as input. */

      if (shout_set_format (shout2send->conn, audio_format) != SHOUTERR_SUCCESS) {
        g_error ("Error setting connection format: %s\n",
            shout_get_error (shout2send->conn));
      }

      if (shout_open (shout2send->conn) == SHOUTERR_SUCCESS) {
        g_print ("connected to server...\n");
      } else {
        g_warning ("Couldn't connect to server: %s",
            shout_get_error (shout2send->conn));
        shout_close (shout2send->conn);
        shout_free (shout2send->conn);
        return GST_STATE_FAILURE;
      }
      break;
    case GST_STATE_PAUSED_TO_READY:
      shout_close (shout2send->conn);
      shout_free (shout2send->conn);
      break;
    default:
      break;
  }

  /* if we haven't failed already, give the parent class a chance to ;-) */
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "shout2send", GST_RANK_NONE,
      GST_TYPE_SHOUT2SEND);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "shout2send",
    "Sends data to an icecast server using libshout2",
    plugin_init,
    VERSION, "LGPL", "libshout2", "http://www.icecast.org/download.html")
