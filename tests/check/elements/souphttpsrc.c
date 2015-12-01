/* GStreamer unit tests for the souphttpsrc element
 * Copyright (C) 2006-2007 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2008 Wouter Cloetens <wouter@mind.be>
 * Copyright (C) 2001-2003, Ximian, Inc.
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
# include "config.h"
#endif

#include <stdlib.h>

#include <glib.h>
#include <glib/gprintf.h>

#define SOUP_VERSION_MIN_REQUIRED (SOUP_VERSION_2_40)
#include <libsoup/soup.h>
#include <gst/check/gstcheck.h>

#if !defined(SOUP_MINOR_VERSION) || SOUP_MINOR_VERSION < 44
#define SoupStatus SoupKnownStatusCode
#endif

static guint http_port = 0, https_port = 0;

gboolean redirect = TRUE;

static const char **cookies = NULL;

/* Variables for authentication tests */
static const char *user_id = NULL;
static const char *user_pw = NULL;
static const char *good_user = "good_user";
static const char *bad_user = "bad_user";
static const char *good_pw = "good_pw";
static const char *bad_pw = "bad_pw";
static const char *realm = "SOUPHTTPSRC_REALM";
static const char *basic_auth_path = "/basic_auth";
static const char *digest_auth_path = "/digest_auth";

static gboolean run_server (guint * http_port, guint * https_port);
static void stop_server (void);

static void
handoff_cb (GstElement * fakesink, GstBuffer * buf, GstPad * pad,
    GstBuffer ** p_outbuf)
{
  GST_LOG ("handoff, buf = %p", buf);
  if (*p_outbuf == NULL)
    *p_outbuf = gst_buffer_ref (buf);
}

static gboolean
basic_auth_cb (SoupAuthDomain * domain, SoupMessage * msg,
    const char *username, const char *password, gpointer user_data)
{
  /* There is only one good login for testing */
  return (strcmp (username, good_user) == 0)
      && (strcmp (password, good_pw) == 0);
}


static char *
digest_auth_cb (SoupAuthDomain * domain, SoupMessage * msg,
    const char *username, gpointer user_data)
{
  /* There is only one good login for testing */
  if (strcmp (username, good_user) == 0)
    return soup_auth_domain_digest_encode_password (good_user, realm, good_pw);
  return NULL;
}

static int
run_test (const char *format, ...)
{
  GstStateChangeReturn ret;

  GstElement *pipe, *src, *sink;

  GstBuffer *buf = NULL;

  GstMessage *msg;

  gchar *url;

  va_list args;

  int rc = -1;

  pipe = gst_pipeline_new (NULL);

  src = gst_element_factory_make ("souphttpsrc", NULL);
  fail_unless (src != NULL);

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink != NULL);

  gst_bin_add (GST_BIN (pipe), src);
  gst_bin_add (GST_BIN (pipe), sink);
  fail_unless (gst_element_link (src, sink));

  if (http_port == 0) {
    GST_DEBUG ("failed to start soup http server");
  }
  fail_unless (http_port != 0);
  va_start (args, format);
  g_vasprintf (&url, format, args);
  va_end (args);
  fail_unless (url != NULL);
  g_object_set (src, "location", url, NULL);
  g_free (url);

  g_object_set (src, "automatic-redirect", redirect, NULL);
  g_object_set (src, "ssl-ca-file", GST_TEST_FILES_PATH "/test-cert.pem", NULL);
  if (cookies != NULL)
    g_object_set (src, "cookies", cookies, NULL);
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "preroll-handoff", G_CALLBACK (handoff_cb), &buf);

  if (user_id != NULL)
    g_object_set (src, "user-id", user_id, NULL);
  if (user_pw != NULL)
    g_object_set (src, "user-pw", user_pw, NULL);

  ret = gst_element_set_state (pipe, GST_STATE_PAUSED);
  if (ret != GST_STATE_CHANGE_ASYNC) {
    GST_DEBUG ("failed to start up soup http src, ret = %d", ret);
    goto done;
  }

  gst_element_set_state (pipe, GST_STATE_PLAYING);
  msg = gst_bus_poll (GST_ELEMENT_BUS (pipe),
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    gchar *debug = NULL;

    GError *err = NULL;

    gst_message_parse_error (msg, &err, &debug);
    GST_INFO ("error: %s", err->message);
    if (g_str_has_suffix (err->message, "Not Found"))
      rc = 404;
    else if (g_str_has_suffix (err->message, "Forbidden"))
      rc = 403;
    else if (g_str_has_suffix (err->message, "Unauthorized"))
      rc = 401;
    else if (g_str_has_suffix (err->message, "Found"))
      rc = 302;
    GST_INFO ("debug: %s", debug);
    /* should not've gotten any output in case of a 40x error. Wait a bit
     * to give the streaming thread a chance to push out a buffer and trigger
     * our callback before shutting down the pipeline */
    g_usleep (G_USEC_PER_SEC / 2);
    fail_unless (buf == NULL);
    g_error_free (err);
    g_free (debug);
    gst_message_unref (msg);
    goto done;
  }
  gst_message_unref (msg);

  /* don't wait for more than 10 seconds */
  ret = gst_element_get_state (pipe, NULL, NULL, 10 * GST_SECOND);
  GST_LOG ("ret = %u", ret);

  if (buf == NULL) {
    /* we want to test the buffer offset, nothing else; if there's a failure
     * it might be for lots of reasons (no network connection, whatever), we're
     * not interested in those */
    GST_DEBUG ("didn't manage to get data within 10 seconds, skipping test");
    goto done;
  }

  GST_DEBUG ("buffer offset = %" G_GUINT64_FORMAT, GST_BUFFER_OFFSET (buf));

  /* first buffer should have a 0 offset */
  fail_unless (GST_BUFFER_OFFSET (buf) == 0);
  gst_buffer_unref (buf);
  rc = 0;

done:

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
  return rc;
}

GST_START_TEST (test_first_buffer_has_offset)
{
  fail_unless (run_test ("http://127.0.0.1:%u/", http_port) == 0);
}

GST_END_TEST;

GST_START_TEST (test_not_found)
{
  fail_unless (run_test ("http://127.0.0.1:%u/404", http_port) == 404);
  fail_unless (run_test ("http://127.0.0.1:%u/404-with-data",
          http_port) == 404);
}

GST_END_TEST;

GST_START_TEST (test_forbidden)
{
  fail_unless (run_test ("http://127.0.0.1:%u/403", http_port) == 403);
}

GST_END_TEST;

GST_START_TEST (test_redirect_no)
{
  redirect = FALSE;
  fail_unless (run_test ("http://127.0.0.1:%u/302", http_port) == 302);
}

GST_END_TEST;

GST_START_TEST (test_redirect_yes)
{
  redirect = TRUE;
  fail_unless (run_test ("http://127.0.0.1:%u/302", http_port) == 0);
}

GST_END_TEST;

GST_START_TEST (test_https)
{
  if (!https_port)
    GST_INFO ("Failed to start an HTTPS server; let's just skip this test.");
  else
    fail_unless (run_test ("https://127.0.0.1:%u/", https_port) == 0);
}

GST_END_TEST;

GST_START_TEST (test_cookies)
{
  static const char *biscotti[] = { "delacre=yummie", "koekje=lu", NULL };
  int rc;

  cookies = biscotti;
  rc = run_test ("http://127.0.0.1:%u/", http_port);
  cookies = NULL;
  fail_unless (rc == 0);
}

GST_END_TEST;

GST_START_TEST (test_good_user_basic_auth)
{
  int res;

  user_id = good_user;
  user_pw = good_pw;
  res = run_test ("http://127.0.0.1:%u%s", http_port, basic_auth_path);
  GST_DEBUG ("Basic Auth user %s password %s res = %d", user_id, user_pw, res);
  user_id = user_pw = NULL;
  fail_unless (res == 0);
}

GST_END_TEST;

GST_START_TEST (test_bad_user_basic_auth)
{
  int res;

  user_id = bad_user;
  user_pw = good_pw;
  res = run_test ("http://127.0.0.1:%u%s", http_port, basic_auth_path);
  GST_DEBUG ("Basic Auth user %s password %s res = %d", user_id, user_pw, res);
  user_id = user_pw = NULL;
  fail_unless (res == 401);
}

GST_END_TEST;

GST_START_TEST (test_bad_password_basic_auth)
{
  int res;

  user_id = good_user;
  user_pw = bad_pw;
  res = run_test ("http://127.0.0.1:%u%s", http_port, basic_auth_path);
  GST_DEBUG ("Basic Auth user %s password %s res = %d", user_id, user_pw, res);
  user_id = user_pw = NULL;
  fail_unless (res == 401);
}

GST_END_TEST;

GST_START_TEST (test_good_user_digest_auth)
{
  int res;

  user_id = good_user;
  user_pw = good_pw;
  res = run_test ("http://127.0.0.1:%u%s", http_port, digest_auth_path);
  GST_DEBUG ("Digest Auth user %s password %s res = %d", user_id, user_pw, res);
  user_id = user_pw = NULL;
  fail_unless (res == 0);
}

GST_END_TEST;

GST_START_TEST (test_bad_user_digest_auth)
{
  int res;

  user_id = bad_user;
  user_pw = good_pw;
  res = run_test ("http://127.0.0.1:%u%s", http_port, digest_auth_path);
  GST_DEBUG ("Digest Auth user %s password %s res = %d", user_id, user_pw, res);
  user_id = user_pw = NULL;
  fail_unless (res == 401);
}

GST_END_TEST;

GST_START_TEST (test_bad_password_digest_auth)
{
  int res;

  user_id = good_user;
  user_pw = bad_pw;
  res = run_test ("http://127.0.0.1:%u%s", http_port, digest_auth_path);
  GST_DEBUG ("Digest Auth user %s password %s res = %d", user_id, user_pw, res);
  user_id = user_pw = NULL;
  fail_unless (res == 401);
}

GST_END_TEST;

static gboolean icy_caps = FALSE;

static void
got_buffer (GstElement * fakesink, GstBuffer * buf, GstPad * pad,
    gpointer user_data)
{
  GstStructure *s;
  GstCaps *caps;

  /* Caps can be anything if we don't except icy caps */
  if (!icy_caps)
    return;

  /* Otherwise they _must_ be "application/x-icy" */
  caps = gst_pad_get_current_caps (pad);
  fail_unless (caps != NULL);
  s = gst_caps_get_structure (caps, 0);
  fail_unless_equals_string (gst_structure_get_name (s), "application/x-icy");
  gst_caps_unref (caps);
}

GST_START_TEST (test_icy_stream)
{
  GstElement *pipe, *src, *sink;

  GstMessage *msg;

  pipe = gst_pipeline_new (NULL);

  src = gst_element_factory_make ("souphttpsrc", NULL);
  fail_unless (src != NULL);

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink != NULL);
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", G_CALLBACK (got_buffer), NULL);

  gst_bin_add (GST_BIN (pipe), src);
  gst_bin_add (GST_BIN (pipe), sink);
  fail_unless (gst_element_link (src, sink));

  /* Radionomy Hot40Music shoutcast stream */
  g_object_set (src, "location",
      "http://streaming.radionomy.com:80/Hot40Music", NULL);

  /* EOS after the first buffer */
  g_object_set (src, "num-buffers", 1, NULL);
  icy_caps = TRUE;
  gst_element_set_state (pipe, GST_STATE_PLAYING);
  msg = gst_bus_poll (GST_ELEMENT_BUS (pipe),
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      GST_DEBUG ("success, we're done here");
      gst_message_unref (msg);
      break;
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;

      gst_message_parse_error (msg, &err, NULL);
      GST_INFO ("Error with ICY mp3 shoutcast stream: %s", err->message);
      gst_message_unref (msg);
      g_clear_error (&err);
      break;
    }
    default:
      break;
  }

  icy_caps = FALSE;

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_END_TEST;

static SoupServer *server;      /* NULL */
static SoupServer *ssl_server;  /* NULL */

static Suite *
souphttpsrc_suite (void)
{
  TCase *tc_chain, *tc_internet;
  Suite *s;

  /* we don't support exceptions from the proxy, so just unset the environment
   * variable - in case it's set in the test environment it would otherwise
   * prevent us from connecting to localhost (like jenkins.qa.ubuntu.com) */
  g_unsetenv ("http_proxy");

  s = suite_create ("souphttpsrc");
  tc_chain = tcase_create ("general");
  tc_internet = tcase_create ("internet");

  suite_add_tcase (s, tc_chain);
  if (run_server (&http_port, &https_port)) {
    atexit (stop_server);
    tcase_add_test (tc_chain, test_first_buffer_has_offset);
    tcase_add_test (tc_chain, test_redirect_yes);
    tcase_add_test (tc_chain, test_redirect_no);
    tcase_add_test (tc_chain, test_not_found);
    tcase_add_test (tc_chain, test_forbidden);
    tcase_add_test (tc_chain, test_cookies);
    tcase_add_test (tc_chain, test_good_user_basic_auth);
    tcase_add_test (tc_chain, test_bad_user_basic_auth);
    tcase_add_test (tc_chain, test_bad_password_basic_auth);
    tcase_add_test (tc_chain, test_good_user_digest_auth);
    tcase_add_test (tc_chain, test_bad_user_digest_auth);
    tcase_add_test (tc_chain, test_bad_password_digest_auth);

    if (ssl_server != NULL)
      tcase_add_test (tc_chain, test_https);
  } else {
    g_print ("Skipping 12 souphttpsrc tests, couldn't start or connect to "
        "local http server\n");
  }

  suite_add_tcase (s, tc_internet);
  tcase_set_timeout (tc_internet, 250);
  tcase_add_test (tc_internet, test_icy_stream);

  return s;
}

GST_CHECK_MAIN (souphttpsrc);

static void
do_get (SoupMessage * msg, const char *path)
{
  gboolean send_error_doc = FALSE;
  char *uri;

  int buflen = 4096;

  SoupStatus status = SOUP_STATUS_OK;

  uri = soup_uri_to_string (soup_message_get_uri (msg), FALSE);
  GST_DEBUG ("request: \"%s\"", uri);

  if (!strcmp (path, "/301"))
    status = SOUP_STATUS_MOVED_PERMANENTLY;
  else if (!strcmp (path, "/302"))
    status = SOUP_STATUS_MOVED_TEMPORARILY;
  else if (!strcmp (path, "/307"))
    status = SOUP_STATUS_TEMPORARY_REDIRECT;
  else if (!strcmp (path, "/403"))
    status = SOUP_STATUS_FORBIDDEN;
  else if (!strcmp (path, "/404"))
    status = SOUP_STATUS_NOT_FOUND;
  else if (!strcmp (path, "/404-with-data")) {
    status = SOUP_STATUS_NOT_FOUND;
    send_error_doc = TRUE;
  }

  if (SOUP_STATUS_IS_REDIRECTION (status)) {
    char *redir_uri;

    redir_uri = g_strdup_printf ("%s-redirected", uri);
    soup_message_headers_append (msg->response_headers, "Location", redir_uri);
    g_free (redir_uri);
  }
  if (status != (SoupStatus) SOUP_STATUS_OK && !send_error_doc)
    goto leave;

  if (msg->method == SOUP_METHOD_GET) {
    char *buf;

    buf = g_malloc (buflen);
    memset (buf, 0, buflen);
    soup_message_body_append (msg->response_body, SOUP_MEMORY_TAKE,
        buf, buflen);
  } else {                      /* msg->method == SOUP_METHOD_HEAD */

    char *length;

    /* We could just use the same code for both GET and
     * HEAD. But we'll optimize and avoid the extra
     * malloc.
     */
    length = g_strdup_printf ("%lu", (gulong) buflen);
    soup_message_headers_append (msg->response_headers,
        "Content-Length", length);
    g_free (length);
  }

leave:
  soup_message_set_status (msg, status);
  g_free (uri);
}

static void
print_header (const char *name, const char *value, gpointer data)
{
  GST_DEBUG ("header: %s: %s", name, value);
}

static void
server_callback (SoupServer * server, SoupMessage * msg,
    const char *path, GHashTable * query,
    SoupClientContext * context, gpointer data)
{
  GST_DEBUG ("%s %s HTTP/1.%d", msg->method, path,
      soup_message_get_http_version (msg));
  soup_message_headers_foreach (msg->request_headers, print_header, NULL);
  if (msg->request_body->length)
    GST_DEBUG ("%s", msg->request_body->data);

  if (msg->method == SOUP_METHOD_GET || msg->method == SOUP_METHOD_HEAD)
    do_get (msg, path);
  else
    soup_message_set_status (msg, SOUP_STATUS_NOT_IMPLEMENTED);

  GST_DEBUG ("  -> %d %s", msg->status_code, msg->reason_phrase);
}

static gboolean
run_server (guint * http_port, guint * https_port)
{
  guint port = SOUP_ADDRESS_ANY_PORT;
  guint ssl_port = SOUP_ADDRESS_ANY_PORT;
  const char *ssl_cert_file = GST_TEST_FILES_PATH "/test-cert.pem";
  const char *ssl_key_file = GST_TEST_FILES_PATH "/test-key.pem";
  static int server_running = 0;

  SoupAuthDomain *domain = NULL;

  if (server_running)
    return TRUE;

  server_running = 1;

  *http_port = *https_port = 0;

  /* The G_ENABLE_DIAGNOSTIC is temporarily overriden to avoid
   * property deprecation warnings (for the SOUP_SERVER_PORT
   * property) */
  g_setenv ("G_ENABLE_DIAGNOSTIC", "0", TRUE);
  server = soup_server_new (SOUP_SERVER_PORT, port, NULL);
  g_setenv ("G_ENABLE_DIAGNOSTIC", "1", TRUE);
  if (!server) {
    GST_DEBUG ("Unable to bind to server port %u", port);
    return FALSE;
  }
  *http_port = soup_server_get_port (server);
  GST_INFO ("HTTP server listening on port %u", *http_port);
  soup_server_add_handler (server, NULL, server_callback, NULL, NULL);
  domain = soup_auth_domain_basic_new (SOUP_AUTH_DOMAIN_REALM, realm,
      SOUP_AUTH_DOMAIN_BASIC_AUTH_CALLBACK, basic_auth_cb,
      SOUP_AUTH_DOMAIN_ADD_PATH, basic_auth_path, NULL);
  soup_server_add_auth_domain (server, domain);
  g_object_unref (domain);
  domain = soup_auth_domain_digest_new (SOUP_AUTH_DOMAIN_REALM, realm,
      SOUP_AUTH_DOMAIN_DIGEST_AUTH_CALLBACK, digest_auth_cb,
      SOUP_AUTH_DOMAIN_ADD_PATH, digest_auth_path, NULL);
  soup_server_add_auth_domain (server, domain);
  g_object_unref (domain);
  soup_server_run_async (server);

  if (ssl_cert_file && ssl_key_file) {
    GTlsBackend *backend = g_tls_backend_get_default ();

    if (backend != NULL && g_tls_backend_supports_tls (backend)) {
      ssl_server = soup_server_new (SOUP_SERVER_PORT, ssl_port,
          SOUP_SERVER_SSL_CERT_FILE, ssl_cert_file,
          SOUP_SERVER_SSL_KEY_FILE, ssl_key_file, NULL);
    } else {
      GST_INFO ("No TLS support");
    }

    if (ssl_server) {
      *https_port = soup_server_get_port (ssl_server);
      GST_INFO ("HTTPS server listening on port %u", *https_port);
      soup_server_add_handler (ssl_server, NULL, server_callback, NULL, NULL);
      soup_server_run_async (ssl_server);
    }
  }

  /* check if we can connect to our local http server */
  {
    GSocketConnection *conn;
    GSocketClient *client;

    client = g_socket_client_new ();
    g_socket_client_set_timeout (client, 2);
    conn = g_socket_client_connect_to_host (client, "127.0.0.1", *http_port,
        NULL, NULL);
    if (conn == NULL) {
      GST_INFO ("Couldn't connect to http server 127.0.0.1:%u", *http_port);
      g_object_unref (client);
      stop_server ();
      return FALSE;
    }
    g_object_unref (conn);

    if (ssl_server == NULL)
      goto skip_https_check;

    conn = g_socket_client_connect_to_host (client, "127.0.0.1", *https_port,
        NULL, NULL);
    if (conn == NULL) {
      GST_INFO ("Couldn't connect to https server 127.0.0.1:%u", *https_port);
      g_object_unref (client);
      stop_server ();
      return FALSE;
    }
    g_object_unref (conn);

  skip_https_check:

    g_object_unref (client);
  }

  return TRUE;
}

static void
stop_server (void)
{
  GST_INFO ("cleaning up");

  if (server) {
    g_object_unref (server);
    server = NULL;
  }
  if (ssl_server) {
    g_object_unref (ssl_server);
    ssl_server = NULL;
  }
}
