/*
 * Copyright (C) 2013 Sebastian Dröge <slomo@circular-chaos.org>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/gst.h>
#include <gio/gio.h>

typedef struct
{
  gchar *name;
  GSocketConnection *connection;
  GSocket *socket;
  GInputStream *istream;
  GOutputStream *ostream;
  GSource *isource, *tosource;
  GByteArray *current_message;
} Client;

static GMainLoop *loop = NULL;
G_LOCK_DEFINE_STATIC (clients);
static GList *clients = NULL;
static GstElement *pipeline = NULL;
static GstElement *multisocketsink = NULL;
static gboolean started = FALSE;

static void
remove_client (Client * client)
{
  g_print ("Removing connection %s\n", client->name);

  g_free (client->name);

  if (client->isource) {
    g_source_destroy (client->isource);
    g_source_unref (client->isource);
  }
  if (client->tosource) {
    g_source_destroy (client->tosource);
    g_source_unref (client->tosource);
  }
  g_object_unref (client->connection);
  g_byte_array_unref (client->current_message);

  G_LOCK (clients);
  clients = g_list_remove (clients, client);
  G_UNLOCK (clients);

  g_slice_free (Client, client);
}

static void
write_bytes (Client * client, const gchar * data, guint len)
{
  gssize w;
  GError *err = NULL;

  /* TODO: We assume this never blocks */
  do {
    w = g_output_stream_write (client->ostream, data, len, NULL, &err);
    if (w > 0) {
      len -= w;
      data += w;
    }
  } while (w > 0 && len > 0);

  if (w <= 0) {
    if (err) {
      g_print ("Write error %s\n", err->message);
      g_clear_error (&err);
    }
    remove_client (client);
  }
}

static void
client_message (Client * client, const gchar * data, guint len)
{
  gchar **lines = g_strsplit_set (data, "\r\n", -1);

  if (g_str_has_prefix (lines[0], "HEAD")) {
    gchar **parts = g_strsplit (lines[0], " ", -1);
    gchar *response;
    const gchar *http_version;

    if (parts[1] && parts[2] && *parts[2] != '\0')
      http_version = parts[2];
    else
      http_version = "HTTP/1.0";

    if (parts[1] && strcmp (parts[1], "/") == 0) {
      response = g_strdup_printf ("%s 200 OK\r\n" "\r\n", http_version);
    } else {
      response = g_strdup_printf ("%s 404 Not Found\r\n\r\n", http_version);
    }
    write_bytes (client, response, strlen (response));
    g_free (response);
    g_strfreev (parts);
  } else if (g_str_has_prefix (lines[0], "GET")) {
    gchar **parts = g_strsplit (lines[0], " ", -1);
    gchar *response;
    const gchar *http_version;
    gboolean ok = FALSE;

    if (parts[1] && parts[2] && *parts[2] != '\0')
      http_version = parts[2];
    else
      http_version = "HTTP/1.0";

    if (parts[1] && strcmp (parts[1], "/") == 0) {
      response = g_strdup_printf ("%s 200 OK\r\n" "\r\n", http_version);
      ok = TRUE;
    } else {
      response = g_strdup_printf ("%s 404 Not Found\r\n\r\n", http_version);
    }
    write_bytes (client, response, strlen (response));
    g_free (response);
    g_strfreev (parts);

    if (ok) {
      g_source_destroy (client->isource);
      g_source_unref (client->isource);
      client->isource = NULL;
      g_source_destroy (client->tosource);
      g_source_unref (client->tosource);
      client->tosource = NULL;
      g_print ("Starting to stream to %s\n", client->name);
      g_signal_emit_by_name (multisocketsink, "add", client->socket);

      if (!started) {
        g_print ("Starting pipeline\n");
        if (gst_element_set_state (pipeline,
                GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
          g_print ("Failed to start pipeline\n");
          g_main_loop_quit (loop);
        }
        started = TRUE;
      }
    }
  } else {
    gchar **parts = g_strsplit (lines[0], " ", -1);
    gchar *response;
    const gchar *http_version;

    if (parts[1] && parts[2] && *parts[2] != '\0')
      http_version = parts[2];
    else
      http_version = "HTTP/1.0";

    response = g_strdup_printf ("%s 400 Bad Request\r\n\r\n", http_version);
    write_bytes (client, response, strlen (response));
    g_free (response);
    g_strfreev (parts);
    remove_client (client);
  }

  g_strfreev (lines);
}

static gboolean
on_timeout (Client * client)
{
  g_print ("Timeout\n");
  remove_client (client);

  return FALSE;
}

static gboolean
on_read_bytes (GPollableInputStream * stream, Client * client)
{
  gssize r;
  gchar data[4096];
  GError *err = NULL;

  do {
    r = g_pollable_input_stream_read_nonblocking (G_POLLABLE_INPUT_STREAM
        (client->istream), data, sizeof (data), NULL, &err);
    if (r > 0)
      g_byte_array_append (client->current_message, (guint8 *) data, r);
  } while (r > 0);

  if (r == 0) {
    remove_client (client);
    return FALSE;
  } else if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
    guint8 *tmp = client->current_message->data;

    g_clear_error (&err);

    while (client->current_message->len > 3) {
      if (tmp[0] == 0x0d && tmp[1] == 0x0a && tmp[2] == 0x0d && tmp[3] == 0x0a) {
        guint len;

        g_byte_array_append (client->current_message, (const guint8 *) "\0", 1);
        len = tmp - client->current_message->data + 5;
        client_message (client, (gchar *) client->current_message->data, len);
        g_byte_array_remove_range (client->current_message, 0, len);
        tmp = client->current_message->data;
      } else {
        tmp++;
      }
    }

    if (client->current_message->len >= 1024 * 1024) {
      g_print ("No complete request after 1MB of data\n");
      remove_client (client);
      return FALSE;
    }

    return TRUE;
  } else {
    g_print ("Read error %s\n", err->message);
    g_clear_error (&err);
    remove_client (client);
    return FALSE;
  }

  return FALSE;
}

static gboolean
on_new_connection (GSocketService * service, GSocketConnection * connection,
    GObject * source_object, gpointer user_data)
{
  Client *client = g_slice_new0 (Client);
  GSocketAddress *addr;
  GInetAddress *iaddr;
  gchar *ip;
  guint16 port;

  addr = g_socket_connection_get_remote_address (connection, NULL);
  iaddr = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (addr));
  port = g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (addr));
  ip = g_inet_address_to_string (iaddr);
  client->name = g_strdup_printf ("%s:%u", ip, port);
  g_free (ip);
  g_object_unref (addr);

  g_print ("New connection %s\n", client->name);

  client->connection = g_object_ref (connection);
  client->socket = g_socket_connection_get_socket (connection);
  client->istream =
      g_io_stream_get_input_stream (G_IO_STREAM (client->connection));
  client->ostream =
      g_io_stream_get_output_stream (G_IO_STREAM (client->connection));
  client->current_message = g_byte_array_sized_new (1024);

  client->tosource = g_timeout_source_new_seconds (5);
  g_source_set_callback (client->tosource, (GSourceFunc) on_timeout, client,
      NULL);
  g_source_attach (client->tosource, NULL);

  client->isource =
      g_pollable_input_stream_create_source (G_POLLABLE_INPUT_STREAM
      (client->istream), NULL);
  g_source_set_callback (client->isource, (GSourceFunc) on_read_bytes, client,
      NULL);
  g_source_attach (client->isource, NULL);

  G_LOCK (clients);
  clients = g_list_prepend (clients, client);
  G_UNLOCK (clients);

  return TRUE;
}

static gboolean
on_message (GstBus * bus, GstMessage * message, gpointer user_data)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *err;

      gst_message_parse_error (message, &err, &debug);
      g_print ("Error %s\n", err->message);
      g_error_free (err);
      g_free (debug);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_WARNING:{
      gchar *debug;
      GError *err;

      gst_message_parse_error (message, &err, &debug);
      g_print ("Warning %s\n", err->message);
      g_error_free (err);
      g_free (debug);
      break;
    }
    case GST_MESSAGE_EOS:{
      g_print ("EOS\n");
      g_main_loop_quit (loop);
    }
    default:
      break;
  }

  return TRUE;
}

static void
on_client_socket_removed (GstElement * element, GSocket * socket,
    gpointer user_data)
{
  GList *l;
  Client *client = NULL;

  G_LOCK (clients);
  for (l = clients; l; l = l->next) {
    Client *tmp = l->data;
    if (socket == tmp->socket) {
      client = tmp;
      break;
    }
  }
  G_UNLOCK (clients);

  if (client)
    remove_client (client);
}

int
main (gint argc, gchar ** argv)
{
  GSocketService *service;
  GstElement *bin, *stream;
  GstPad *srcpad, *ghostpad, *sinkpad;
  GError *err = NULL;
  GstBus *bus;

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_print ("usage: %s <launch line>\n"
        "example: %s ( videotestsrc ! theoraenc ! oggmux name=stream )\n",
        argv[0], argv[0]);
    return -1;
  }

  bin = gst_parse_launchv ((const gchar **) argv + 1, &err);
  if (!bin) {
    g_print ("invalid pipeline: %s\n", err->message);
    g_clear_error (&err);
    return -2;
  }

  stream = gst_bin_get_by_name (GST_BIN (bin), "stream");
  if (!stream) {
    g_print ("no element with name \"stream\" found\n");
    gst_object_unref (bin);
    return -3;
  }

  srcpad = gst_element_get_static_pad (stream, "src");
  if (!srcpad) {
    g_print ("no \"src\" pad in element \"stream\" found\n");
    gst_object_unref (stream);
    gst_object_unref (bin);
    return -4;
  }

  ghostpad = gst_ghost_pad_new ("src", srcpad);
  gst_element_add_pad (GST_ELEMENT (bin), ghostpad);
  gst_object_unref (srcpad);

  pipeline = gst_pipeline_new (NULL);

  multisocketsink = gst_element_factory_make ("multisocketsink", NULL);
  g_object_set (multisocketsink,
      "unit-format", GST_FORMAT_TIME,
      "units-max", (gint64) 7 * GST_SECOND,
      "units-soft-max", (gint64) 3 * GST_SECOND,
      "recover-policy", 3 /* keyframe */ ,
      "timeout", (guint64) 10 * GST_SECOND,
      "sync-method", 1 /* next-keyframe */ ,
      NULL);

  gst_bin_add_many (GST_BIN (pipeline), bin, multisocketsink, NULL);

  sinkpad = gst_element_get_static_pad (multisocketsink, "sink");
  gst_pad_link (ghostpad, sinkpad);
  gst_object_unref (sinkpad);

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (on_message), NULL);
  gst_object_unref (bus);

  g_signal_connect (multisocketsink, "client-socket-removed",
      G_CALLBACK (on_client_socket_removed), NULL);

  loop = g_main_loop_new (NULL, FALSE);

  if (gst_element_set_state (pipeline,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE) {
    gst_object_unref (pipeline);
    g_main_loop_unref (loop);
    g_print ("Failed to set pipeline to ready\n");
    return -5;
  }

  service = g_socket_service_new ();
  g_socket_listener_add_inet_port (G_SOCKET_LISTENER (service), 8080, NULL,
      NULL);

  g_signal_connect (service, "incoming", G_CALLBACK (on_new_connection), NULL);

  g_socket_service_start (service);

  g_print ("Listening on http://127.0.0.1:8080/\n");

  g_main_loop_run (loop);

  g_socket_service_stop (service);
  g_object_unref (service);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  g_main_loop_unref (loop);

  return 0;
}
