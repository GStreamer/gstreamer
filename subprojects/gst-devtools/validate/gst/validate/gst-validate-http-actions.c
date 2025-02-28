/* GStreamer
 *
 * Copyright (C) 2024 Igalia S.L
 *  Author: Thibault Saunier <tsaunier@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include <glib.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

gboolean run_http_request (const GstStructure * args, GError ** error);

typedef struct
{
  const gchar *method;
  const gchar *host;
  gint port;
  const gchar *path;
  const gchar *content_type;
  const gchar *body;
  gsize body_length;
} HttpRequestParams;

typedef struct
{
  gchar *body;
  gsize body_length;
  gint status_code;
} HttpResponse;

static void
http_response_clear (HttpResponse * response)
{
  if (response) {
    g_clear_pointer (&response->body, g_free);
    response->body_length = 0;
    response->status_code = 0;
  }
}

static gboolean
parse_uri (const gchar * uri, gchar ** host, gint * port, gchar ** path,
    GError ** error)
{
  GUri *guri;
  gboolean ret = FALSE;

  guri = g_uri_parse (uri, 0 /* G_URI_FLAGS_NONE in 2.66 */ , error);
  if (!guri)
    return FALSE;

  *host = g_strdup (g_uri_get_host (guri));
  if (!*host) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
        "Invalid URI: missing host");
    goto cleanup;
  }

  *port = g_uri_get_port (guri);
  if (*port == -1) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
        "Invalid URI: missing port");
    goto cleanup;
  }

  *path = g_strdup (g_uri_get_path (guri));
  if (!*path) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
        "Invalid URI: missing path");
    goto cleanup;
  }

  ret = TRUE;

cleanup:
  if (!ret) {
    g_clear_pointer (host, g_free);
    g_clear_pointer (path, g_free);
  }
  g_uri_unref (guri);
  return ret;
}

static gboolean
send_http_request (const HttpRequestParams * params, HttpResponse * response,
    GError ** error)
{
  GSocketClient *client = NULL;
  GSocketConnection *connection = NULL;
  GOutputStream *output_stream;
  GInputStream *input_stream;
  GString *request = NULL;
  gchar *host_port = NULL;
  gboolean success = FALSE;
  GString *response_str = NULL;
  gchar buffer[4096];
  gssize bytes_read;

  // Construct request without leading newlines
  request = g_string_new (NULL);
  g_string_append_printf (request,
      "%s %s HTTP/1.1\r\n"
      "Host: %s:%d\r\n"
      "Content-Type: %s\r\n",
      params->method,
      params->path, params->host, params->port, params->content_type);

  if (params->body) {
    g_string_append_printf (request,
        "Content-Length: %zu\r\n\r\n%s\r\n", params->body_length, params->body);
  } else {
    g_string_append (request, "\r\n");
  }

  // Create client and establish connection
  client = g_socket_client_new ();
  host_port = g_strdup_printf ("%s:%d", params->host, params->port);
  connection = g_socket_client_connect_to_host (client,
      host_port, params->port, NULL, error);

  if (!connection) {
    goto cleanup;
  }
  output_stream = g_io_stream_get_output_stream (G_IO_STREAM (connection));
  if (!g_output_stream_write_all (output_stream,
          request->str, request->len, NULL, NULL, error)) {
    goto cleanup;
  }
  // Read response
  response_str = g_string_new (NULL);
  input_stream = g_io_stream_get_input_stream (G_IO_STREAM (connection));
  while ((bytes_read = g_input_stream_read (input_stream,
              buffer, sizeof (buffer), NULL, error)) > 0) {
    g_string_append_len (response_str, buffer, bytes_read);
  }

  if (*error) {
    goto cleanup;
  }
  // Parse HTTP response
  gchar **lines = g_strsplit (response_str->str, "\r\n", -1);
  if (lines && lines[0]) {
    gchar **status_parts = g_strsplit (lines[0], " ", 3);
    if (status_parts && status_parts[1]) {
      response->status_code = atoi (status_parts[1]);
    }
    g_strfreev (status_parts);

    gint i;
    for (i = 0; lines[i] != NULL; i++) {
      if (strlen (lines[i]) == 0 && lines[i + 1] != NULL) {
        response->body = g_strdup (lines[i + 1]);
        response->body_length = strlen (response->body);
        break;
      }
    }
  }
  g_strfreev (lines);

  // Check if the status code indicates success (2xx)
  if (response->status_code < 200 || response->status_code >= 300) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
        "HTTP request failed with status %d: %s",
        response->status_code,
        response->body ? response->body : "No error message");
    goto cleanup;
  }

  success = TRUE;

cleanup:
  g_clear_pointer (&host_port, g_free);
  if (request)
    g_string_free (request, TRUE);
  if (response_str)
    g_string_free (response_str, TRUE);
  g_clear_object (&connection);
  g_clear_object (&client);

  return success;
}

gboolean
run_http_request (const GstStructure * args, GError ** error)
{
  const gchar *uri;
  const gchar *method;
  const gchar *body;
  const gchar *headers;
  gchar *host = NULL;
  gchar *path = NULL;
  gint port;
  HttpRequestParams params = { 0 };
  HttpResponse response = { 0 };
  gboolean ret = FALSE;

  g_return_val_if_fail (args != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  // Get required parameters
  uri = gst_structure_get_string (args, "uri");
  if (!uri) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
        "Missing 'uri' parameter");
    return FALSE;
  }

  method = gst_structure_get_string (args, "method");
  if (!method) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
        "Missing 'method' parameter");
    return FALSE;
  }
  // Parse URI
  if (!parse_uri (uri, &host, &port, &path, error))
    return FALSE;

  // Get optional parameters
  body = gst_structure_get_string (args, "body");
  if (!gst_structure_has_field (args, "headers"))
    headers = "application/json";
  else
    headers = gst_structure_get_string (args, "headers");

  // Prepare request parameters
  params.method = method;
  params.host = host;
  params.port = port;
  params.path = path;
  params.content_type = headers;
  params.body = body;
  params.body_length = body ? strlen (body) : 0;

  // Send request
  ret = send_http_request (&params, &response, error);

  const gchar *expected_response =
      gst_structure_get_string (args, "expected-response");
  if (expected_response) {
    if (g_strcmp0 (response.body, expected_response) != 0) {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
          "Expected response '%s' but got '%s'",
          expected_response,
          response.body ? response.body : "No error message");
      ret = FALSE;
    }
  }
  // Cleanup
  g_free (host);
  g_free (path);
  http_response_clear (&response);

  return ret;
}
