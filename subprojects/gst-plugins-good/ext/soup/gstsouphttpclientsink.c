/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@entropywave.com>
 * Copyright (C) 2021 Igalia S.L.
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstsouphttpclientsink
 * @title: gstsouphttpclientsink
 *
 * The souphttpclientsink element sends pipeline data to an HTTP server
 * using HTTP PUT commands.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v videotestsrc num-buffers=300 ! theoraenc ! oggmux !
 *   souphttpclientsink location=http://server/filename.ogv
 * ]|
 *
 * This example encodes 10 seconds of video and sends it to the HTTP
 * server "server" using HTTP PUT commands.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <gio/gio.h>

#include "gstsoupelements.h"
#include "gstsouphttpclientsink.h"
#include "gstsouputils.h"

GST_DEBUG_CATEGORY_STATIC (souphttpclientsink_dbg);
#define GST_CAT_DEFAULT souphttpclientsink_dbg

/* prototypes */


static void gst_soup_http_client_sink_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_soup_http_client_sink_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_soup_http_client_sink_dispose (GObject * object);
static void gst_soup_http_client_sink_finalize (GObject * object);

static gboolean gst_soup_http_client_sink_set_caps (GstBaseSink * sink,
    GstCaps * caps);
static gboolean gst_soup_http_client_sink_start (GstBaseSink * sink);
static gboolean gst_soup_http_client_sink_stop (GstBaseSink * sink);
static gboolean gst_soup_http_client_sink_unlock (GstBaseSink * sink);
static GstFlowReturn gst_soup_http_client_sink_render (GstBaseSink * sink,
    GstBuffer * buffer);
static void gst_soup_http_client_sink_reset (GstSoupHttpClientSink *
    souphttpsink);

static gboolean authenticate (SoupMessage * msg, SoupAuth * auth,
    gboolean retrying, gpointer user_data);
static void restarted (SoupMessage * msg, GBytes * body);
static gboolean send_handle_status (SoupMessage * msg, GError * error,
    GstSoupHttpClientSink * sink);

static gboolean
gst_soup_http_client_sink_set_proxy (GstSoupHttpClientSink * souphttpsink,
    const gchar * uri);

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_USER_AGENT,
  PROP_AUTOMATIC_REDIRECT,
  PROP_PROXY,
  PROP_USER_ID,
  PROP_USER_PW,
  PROP_PROXY_ID,
  PROP_PROXY_PW,
  PROP_COOKIES,
  PROP_SESSION,
  PROP_SOUP_LOG_LEVEL,
  PROP_RETRY_DELAY,
  PROP_RETRIES
};

#define DEFAULT_USER_AGENT           "GStreamer souphttpclientsink "
#define DEFAULT_SOUP_LOG_LEVEL       SOUP_LOGGER_LOG_NONE

/* pad templates */

static GstStaticPadTemplate gst_soup_http_client_sink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


/* class initialization */

#define gst_soup_http_client_sink_parent_class parent_class
G_DEFINE_TYPE (GstSoupHttpClientSink, gst_soup_http_client_sink,
    GST_TYPE_BASE_SINK);

static gboolean souphttpclientsink_element_init (GstPlugin * plugin);
GST_ELEMENT_REGISTER_DEFINE_CUSTOM (souphttpclientsink,
    souphttpclientsink_element_init);


static void
gst_soup_http_client_sink_class_init (GstSoupHttpClientSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_soup_http_client_sink_set_property;
  gobject_class->get_property = gst_soup_http_client_sink_get_property;
  gobject_class->dispose = gst_soup_http_client_sink_dispose;
  gobject_class->finalize = gst_soup_http_client_sink_finalize;

  g_object_class_install_property (gobject_class,
      PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "URI to send to", "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_USER_AGENT,
      g_param_spec_string ("user-agent", "User-Agent",
          "Value of the User-Agent HTTP request header field",
          DEFAULT_USER_AGENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_AUTOMATIC_REDIRECT,
      g_param_spec_boolean ("automatic-redirect", "automatic-redirect",
          "Automatically follow HTTP redirects (HTTP Status Code 3xx)",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_PROXY,
      g_param_spec_string ("proxy", "Proxy",
          "HTTP proxy server URI", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_USER_ID,
      g_param_spec_string ("user-id", "user-id",
          "user id for authentication", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_USER_PW,
      g_param_spec_string ("user-pw", "user-pw",
          "user password for authentication", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PROXY_ID,
      g_param_spec_string ("proxy-id", "proxy-id",
          "user id for proxy authentication", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PROXY_PW,
      g_param_spec_string ("proxy-pw", "proxy-pw",
          "user password for proxy authentication", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SESSION,
      g_param_spec_object ("session", "session",
          "SoupSession object to use for communication",
          _soup_session_get_type (),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_COOKIES,
      g_param_spec_boxed ("cookies", "Cookies", "HTTP request cookies",
          G_TYPE_STRV, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RETRY_DELAY,
      g_param_spec_int ("retry-delay", "Retry Delay",
          "Delay in seconds between retries after a failure", 1, G_MAXINT, 5,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RETRIES,
      g_param_spec_int ("retries", "Retries",
          "Maximum number of retries, zero to disable, -1 to retry forever",
          -1, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
 /**
   * GstSoupHttpClientSink::http-log-level:
   *
   * If set and > 0, captures and dumps HTTP session data as
   * log messages if log level >= GST_LEVEL_TRACE
   *
   * Since: 1.4
   */
  g_object_class_install_property (gobject_class, PROP_SOUP_LOG_LEVEL,
      g_param_spec_enum ("http-log-level", "HTTP log level",
          "Set log level for soup's HTTP session log",
          _soup_logger_log_level_get_type (),
          DEFAULT_SOUP_LOG_LEVEL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_soup_http_client_sink_sink_template);

  gst_element_class_set_static_metadata (gstelement_class, "HTTP client sink",
      "Generic", "Sends streams to HTTP server via PUT",
      "David Schleef <ds@entropywave.com>");

  base_sink_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_soup_http_client_sink_set_caps);
  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_soup_http_client_sink_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_soup_http_client_sink_stop);
  base_sink_class->unlock =
      GST_DEBUG_FUNCPTR (gst_soup_http_client_sink_unlock);
  base_sink_class->render =
      GST_DEBUG_FUNCPTR (gst_soup_http_client_sink_render);
}

static void
gst_soup_http_client_sink_init (GstSoupHttpClientSink * souphttpsink)
{
  const char *proxy;

  g_mutex_init (&souphttpsink->mutex);
  g_cond_init (&souphttpsink->cond);

  souphttpsink->location = NULL;
  souphttpsink->automatic_redirect = TRUE;
  souphttpsink->user_agent = g_strdup (DEFAULT_USER_AGENT);
  souphttpsink->user_id = NULL;
  souphttpsink->user_pw = NULL;
  souphttpsink->proxy_id = NULL;
  souphttpsink->proxy_pw = NULL;
  souphttpsink->prop_session = NULL;
  souphttpsink->timeout = 1;
  souphttpsink->log_level = DEFAULT_SOUP_LOG_LEVEL;
  souphttpsink->retry_delay = 5;
  souphttpsink->retries = 0;
  souphttpsink->sent_buffers = NULL;
  proxy = g_getenv ("http_proxy");
  if (proxy && !gst_soup_http_client_sink_set_proxy (souphttpsink, proxy)) {
    GST_WARNING_OBJECT (souphttpsink,
        "The proxy in the http_proxy env var (\"%s\") cannot be parsed.",
        proxy);
  }

  gst_soup_http_client_sink_reset (souphttpsink);
}

static void
gst_soup_http_client_sink_reset (GstSoupHttpClientSink * souphttpsink)
{
  g_list_free_full (souphttpsink->queued_buffers,
      (GDestroyNotify) gst_buffer_unref);
  souphttpsink->queued_buffers = NULL;
  g_free (souphttpsink->reason_phrase);
  souphttpsink->reason_phrase = NULL;
  souphttpsink->status_code = 0;
  souphttpsink->offset = 0;
  souphttpsink->failures = 0;

  g_list_free_full (souphttpsink->streamheader_buffers,
      (GDestroyNotify) gst_buffer_unref);
  souphttpsink->streamheader_buffers = NULL;
  g_list_free_full (souphttpsink->sent_buffers,
      (GDestroyNotify) gst_buffer_unref);
  souphttpsink->sent_buffers = NULL;
}

static gboolean
gst_soup_http_client_sink_set_proxy (GstSoupHttpClientSink * souphttpsink,
    const gchar * uri)
{
  if (souphttpsink->proxy) {
    gst_soup_uri_free (souphttpsink->proxy);
    souphttpsink->proxy = NULL;
  }
  if (g_str_has_prefix (uri, "http://")) {
    souphttpsink->proxy = gst_soup_uri_new (uri);
  } else {
    gchar *new_uri = g_strconcat ("http://", uri, NULL);

    souphttpsink->proxy = gst_soup_uri_new (new_uri);
    g_free (new_uri);
  }

  return TRUE;
}

void
gst_soup_http_client_sink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSoupHttpClientSink *souphttpsink = GST_SOUP_HTTP_CLIENT_SINK (object);

  g_mutex_lock (&souphttpsink->mutex);
  switch (property_id) {
    case PROP_SESSION:
      if (souphttpsink->prop_session) {
        g_object_unref (souphttpsink->prop_session);
      }
      souphttpsink->prop_session = g_value_dup_object (value);
      break;
    case PROP_LOCATION:
      g_free (souphttpsink->location);
      souphttpsink->location = g_value_dup_string (value);
      souphttpsink->offset = 0;
      if ((souphttpsink->location == NULL)
          || !gst_uri_is_valid (souphttpsink->location)) {
        GST_WARNING_OBJECT (souphttpsink,
            "The location (\"%s\") set, is not a valid uri.",
            souphttpsink->location);
        g_free (souphttpsink->location);
        souphttpsink->location = NULL;
      }
      break;
    case PROP_USER_AGENT:
      g_free (souphttpsink->user_agent);
      souphttpsink->user_agent = g_value_dup_string (value);
      break;
    case PROP_AUTOMATIC_REDIRECT:
      souphttpsink->automatic_redirect = g_value_get_boolean (value);
      break;
    case PROP_USER_ID:
      g_free (souphttpsink->user_id);
      souphttpsink->user_id = g_value_dup_string (value);
      break;
    case PROP_USER_PW:
      g_free (souphttpsink->user_pw);
      souphttpsink->user_pw = g_value_dup_string (value);
      break;
    case PROP_PROXY_ID:
      g_free (souphttpsink->proxy_id);
      souphttpsink->proxy_id = g_value_dup_string (value);
      break;
    case PROP_PROXY_PW:
      g_free (souphttpsink->proxy_pw);
      souphttpsink->proxy_pw = g_value_dup_string (value);
      break;
    case PROP_PROXY:
    {
      const gchar *proxy;

      proxy = g_value_get_string (value);

      if (proxy == NULL) {
        GST_WARNING ("proxy property cannot be NULL");
        goto done;
      }
      if (!gst_soup_http_client_sink_set_proxy (souphttpsink, proxy)) {
        GST_WARNING ("badly formatted proxy URI");
        goto done;
      }
      break;
    }
    case PROP_COOKIES:
      g_strfreev (souphttpsink->cookies);
      souphttpsink->cookies = g_strdupv (g_value_get_boxed (value));
      break;
    case PROP_SOUP_LOG_LEVEL:
      souphttpsink->log_level = g_value_get_enum (value);
      break;
    case PROP_RETRY_DELAY:
      souphttpsink->retry_delay = g_value_get_int (value);
      break;
    case PROP_RETRIES:
      souphttpsink->retries = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
done:
  g_mutex_unlock (&souphttpsink->mutex);
}

void
gst_soup_http_client_sink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstSoupHttpClientSink *souphttpsink = GST_SOUP_HTTP_CLIENT_SINK (object);

  switch (property_id) {
    case PROP_SESSION:
      g_value_set_object (value, souphttpsink->prop_session);
      break;
    case PROP_LOCATION:
      g_value_set_string (value, souphttpsink->location);
      break;
    case PROP_AUTOMATIC_REDIRECT:
      g_value_set_boolean (value, souphttpsink->automatic_redirect);
      break;
    case PROP_USER_AGENT:
      g_value_set_string (value, souphttpsink->user_agent);
      break;
    case PROP_USER_ID:
      g_value_set_string (value, souphttpsink->user_id);
      break;
    case PROP_USER_PW:
      g_value_set_string (value, souphttpsink->user_pw);
      break;
    case PROP_PROXY_ID:
      g_value_set_string (value, souphttpsink->proxy_id);
      break;
    case PROP_PROXY_PW:
      g_value_set_string (value, souphttpsink->proxy_pw);
      break;
    case PROP_PROXY:
      if (souphttpsink->proxy == NULL)
        g_value_set_static_string (value, "");
      else {
        char *proxy = gst_soup_uri_to_string (souphttpsink->proxy);

        g_value_set_string (value, proxy);
        g_free (proxy);
      }
      break;
    case PROP_COOKIES:
      g_value_set_boxed (value, g_strdupv (souphttpsink->cookies));
      break;
    case PROP_SOUP_LOG_LEVEL:
      g_value_set_enum (value, souphttpsink->log_level);
      break;
    case PROP_RETRY_DELAY:
      g_value_set_int (value, souphttpsink->retry_delay);
      break;
    case PROP_RETRIES:
      g_value_set_int (value, souphttpsink->retries);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_soup_http_client_sink_dispose (GObject * object)
{
  GstSoupHttpClientSink *souphttpsink = GST_SOUP_HTTP_CLIENT_SINK (object);

  /* clean up as possible.  may be called multiple times */
  if (souphttpsink->prop_session)
    g_object_unref (souphttpsink->prop_session);
  souphttpsink->prop_session = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_soup_http_client_sink_finalize (GObject * object)
{
  GstSoupHttpClientSink *souphttpsink = GST_SOUP_HTTP_CLIENT_SINK (object);

  /* clean up object here */

  g_free (souphttpsink->user_agent);
  g_free (souphttpsink->user_id);
  g_free (souphttpsink->user_pw);
  g_free (souphttpsink->proxy_id);
  g_free (souphttpsink->proxy_pw);
  if (souphttpsink->proxy)
    gst_soup_uri_free (souphttpsink->proxy);
  g_free (souphttpsink->location);
  g_strfreev (souphttpsink->cookies);

  g_cond_clear (&souphttpsink->cond);
  g_mutex_clear (&souphttpsink->mutex);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_soup_http_client_sink_set_caps (GstBaseSink * sink, GstCaps * caps)
{
  GstSoupHttpClientSink *souphttpsink = GST_SOUP_HTTP_CLIENT_SINK (sink);
  GstStructure *structure;
  const GValue *value_array;
  int i, n;

  GST_DEBUG_OBJECT (souphttpsink, "new stream headers set");
  structure = gst_caps_get_structure (caps, 0);
  value_array = gst_structure_get_value (structure, "streamheader");
  if (value_array) {
    g_list_free_full (souphttpsink->streamheader_buffers,
        (GDestroyNotify) gst_buffer_unref);
    souphttpsink->streamheader_buffers = NULL;

    n = gst_value_array_get_size (value_array);
    for (i = 0; i < n; i++) {
      const GValue *value;
      GstBuffer *buffer;
      value = gst_value_array_get_value (value_array, i);
      buffer = GST_BUFFER (gst_value_get_buffer (value));
      souphttpsink->streamheader_buffers =
          g_list_append (souphttpsink->streamheader_buffers,
          gst_buffer_ref (buffer));
    }
  }

  return TRUE;
}

static gboolean
thread_ready_idle_cb (gpointer data)
{
  GstSoupHttpClientSink *souphttpsink = GST_SOUP_HTTP_CLIENT_SINK (data);

  GST_LOG_OBJECT (souphttpsink, "thread ready");

  g_mutex_lock (&souphttpsink->mutex);
  g_cond_signal (&souphttpsink->cond);
  g_mutex_unlock (&souphttpsink->mutex);

  return FALSE;                 /* only run once */
}

static gpointer
thread_func (gpointer ptr)
{
  GstSoupHttpClientSink *souphttpsink = GST_SOUP_HTTP_CLIENT_SINK (ptr);
  GProxyResolver *proxy_resolver;
  GMainContext *context;

  GST_DEBUG ("thread start");

  context = souphttpsink->context;
  g_main_context_push_thread_default (context);

  if (souphttpsink->proxy != NULL) {
    char *proxy_string = gst_soup_uri_to_string (souphttpsink->proxy);
    proxy_resolver = g_simple_proxy_resolver_new (proxy_string, NULL);
    g_free (proxy_string);
  } else
    proxy_resolver = g_object_ref (g_proxy_resolver_get_default ());

  souphttpsink->session =
      _soup_session_new_with_options ("user-agent", souphttpsink->user_agent,
      "timeout", souphttpsink->timeout, "proxy-resolver", proxy_resolver, NULL);

  g_object_unref (proxy_resolver);

  if (gst_soup_loader_get_api_version () < 3) {
    g_signal_connect (souphttpsink->session, "authenticate",
        G_CALLBACK (authenticate), souphttpsink);
  }

  GST_DEBUG ("created session");

  g_main_loop_run (souphttpsink->loop);

  g_main_context_pop_thread_default (context);

  GST_DEBUG ("thread quit");

  return NULL;
}

static gboolean
gst_soup_http_client_sink_start (GstBaseSink * sink)
{
  GstSoupHttpClientSink *souphttpsink = GST_SOUP_HTTP_CLIENT_SINK (sink);

  if (souphttpsink->prop_session) {
    souphttpsink->session = souphttpsink->prop_session;
  } else {
    GSource *source;
    GError *error = NULL;

    souphttpsink->context = g_main_context_new ();

    /* set up idle source to signal when the main loop is running and
     * it's safe for ::stop() to call g_main_loop_quit() */
    source = g_idle_source_new ();
    g_source_set_callback (source, thread_ready_idle_cb, sink, NULL);
    g_source_attach (source, souphttpsink->context);
    g_source_unref (source);

    souphttpsink->loop = g_main_loop_new (souphttpsink->context, FALSE);

    g_mutex_lock (&souphttpsink->mutex);

    souphttpsink->thread = g_thread_try_new ("souphttpclientsink-thread",
        thread_func, souphttpsink, &error);

    if (error != NULL) {
      GST_DEBUG_OBJECT (souphttpsink, "failed to start thread, %s",
          error->message);
      g_error_free (error);
      g_mutex_unlock (&souphttpsink->mutex);
      return FALSE;
    }

    GST_LOG_OBJECT (souphttpsink, "waiting for main loop thread to start up");
    while (!g_main_loop_is_running (souphttpsink->loop))
      g_cond_wait (&souphttpsink->cond, &souphttpsink->mutex);
    g_mutex_unlock (&souphttpsink->mutex);
    GST_LOG_OBJECT (souphttpsink, "main loop thread running");
  }

  /* Set up logging */
  gst_soup_util_log_setup (souphttpsink->session, souphttpsink->log_level,
      G_OBJECT (souphttpsink));

  return TRUE;
}

static gboolean
gst_soup_http_client_sink_stop (GstBaseSink * sink)
{
  GstSoupHttpClientSink *souphttpsink = GST_SOUP_HTTP_CLIENT_SINK (sink);

  GST_DEBUG ("stop");

  if (souphttpsink->prop_session == NULL) {
    _soup_session_abort (souphttpsink->session);
    g_object_unref (souphttpsink->session);
  }

  g_mutex_lock (&souphttpsink->mutex);
  if (souphttpsink->timer) {
    g_source_destroy (souphttpsink->timer);
    g_source_unref (souphttpsink->timer);
    souphttpsink->timer = NULL;
  }
  g_mutex_unlock (&souphttpsink->mutex);

  if (souphttpsink->loop) {
    g_main_loop_quit (souphttpsink->loop);
    g_mutex_lock (&souphttpsink->mutex);
    g_cond_signal (&souphttpsink->cond);
    g_mutex_unlock (&souphttpsink->mutex);
    g_thread_join (souphttpsink->thread);
    g_main_loop_unref (souphttpsink->loop);
    souphttpsink->loop = NULL;
  }
  if (souphttpsink->context) {
    g_main_context_unref (souphttpsink->context);
    souphttpsink->context = NULL;
  }

  gst_soup_http_client_sink_reset (souphttpsink);

  return TRUE;
}

static gboolean
gst_soup_http_client_sink_unlock (GstBaseSink * sink)
{
  GST_DEBUG ("unlock");

  return TRUE;
}

static void
send_message_locked (GstSoupHttpClientSink * souphttpsink)
{
  GList *g;
  guint64 n;
  GByteArray *array;
  GInputStream *in_stream;

  if (souphttpsink->queued_buffers == NULL || souphttpsink->message) {
    return;
  }

  /* If the URI went away, drop all these buffers */
  if (souphttpsink->location == NULL) {
    GST_DEBUG_OBJECT (souphttpsink, "URI went away, dropping queued buffers");
    g_list_free_full (souphttpsink->queued_buffers,
        (GDestroyNotify) gst_buffer_unref);
    souphttpsink->queued_buffers = NULL;
    return;
  }

  souphttpsink->message = _soup_message_new ("PUT", souphttpsink->location);
  if (souphttpsink->message == NULL) {
    GST_WARNING_OBJECT (souphttpsink,
        "URI could not be parsed while creating message.");
    g_list_free_full (souphttpsink->queued_buffers,
        (GDestroyNotify) gst_buffer_unref);
    souphttpsink->queued_buffers = NULL;
    return;
  }

  g_signal_connect (souphttpsink->message, "restarted", G_CALLBACK (restarted),
      souphttpsink->request_body);

  _soup_message_set_flags (souphttpsink->message,
      (souphttpsink->automatic_redirect ? 0 : SOUP_MESSAGE_NO_REDIRECT));

  if (souphttpsink->cookies) {
    gchar **cookie;

    for (cookie = souphttpsink->cookies; *cookie != NULL; cookie++) {
      _soup_message_headers_append (_soup_message_get_request_headers
          (souphttpsink->message), "Cookie", *cookie);
    }
  }
  array = g_byte_array_new ();
  n = 0;
  if (souphttpsink->offset == 0) {
    for (g = souphttpsink->streamheader_buffers; g; g = g_list_next (g)) {
      GstBuffer *buffer = g->data;
      GstMapInfo map;

      GST_DEBUG_OBJECT (souphttpsink, "queueing stream headers");
      gst_buffer_map (buffer, &map, GST_MAP_READ);
      g_byte_array_append (array, map.data, map.size);
      n += map.size;
      gst_buffer_unmap (buffer, &map);
    }
  }

  for (g = souphttpsink->queued_buffers; g; g = g_list_next (g)) {
    GstBuffer *buffer = g->data;
    if (!GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_HEADER)) {
      GstMapInfo map;

      gst_buffer_map (buffer, &map, GST_MAP_READ);
      g_byte_array_append (array, map.data, map.size);
      n += map.size;
      gst_buffer_unmap (buffer, &map);
    }
  }

  {
    souphttpsink->request_body = g_byte_array_free_to_bytes (array);
    _soup_message_set_request_body_from_bytes (souphttpsink->message,
        NULL, souphttpsink->request_body);
  }

  if (souphttpsink->offset != 0) {
    char *s;
    s = g_strdup_printf ("bytes %" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT "/*",
        souphttpsink->offset, souphttpsink->offset + n - 1);
    _soup_message_headers_append (_soup_message_get_request_headers
        (souphttpsink->message), "Content-Range", s);
    g_free (s);
  }

  if (n == 0) {
    GST_DEBUG_OBJECT (souphttpsink,
        "total size of buffers queued is 0, freeing everything");
    g_list_free_full (souphttpsink->queued_buffers,
        (GDestroyNotify) gst_buffer_unref);
    souphttpsink->queued_buffers = NULL;
    g_clear_object (&souphttpsink->message);
    g_clear_pointer (&souphttpsink->request_body, g_bytes_unref);
    return;
  }

  in_stream =
      _soup_session_send (souphttpsink->session, souphttpsink->message, NULL,
      NULL);
  if (in_stream == NULL) {
    GError *error = NULL;

    if (!send_handle_status (souphttpsink->message, error, souphttpsink)) {
      g_object_unref (souphttpsink->message);
      g_clear_pointer (&souphttpsink->request_body, g_bytes_unref);
      g_clear_error (&error);
      return;
    }
  }
  souphttpsink->sent_buffers = souphttpsink->queued_buffers;

  g_clear_pointer (&souphttpsink->request_body, g_bytes_unref);
  g_object_unref (in_stream);

  g_list_free_full (souphttpsink->sent_buffers,
      (GDestroyNotify) gst_buffer_unref);
  souphttpsink->sent_buffers = NULL;
  souphttpsink->failures = 0;
  souphttpsink->queued_buffers = NULL;
  g_clear_object (&souphttpsink->message);
  souphttpsink->offset += n;
}

static gboolean
send_message (GstSoupHttpClientSink * souphttpsink)
{
  g_mutex_lock (&souphttpsink->mutex);
  send_message_locked (souphttpsink);
  if (souphttpsink->timer) {
    g_source_destroy (souphttpsink->timer);
    g_source_unref (souphttpsink->timer);
    souphttpsink->timer = NULL;
  }
  g_mutex_unlock (&souphttpsink->mutex);

  return FALSE;
}

static gboolean
send_handle_status (SoupMessage * msg, GError * error,
    GstSoupHttpClientSink * sink)
{
  if (error) {
    GST_DEBUG_OBJECT (sink, "callback error=%d %s",
        error->code, error->message);
  } else {
    GST_DEBUG_OBJECT (sink, "callback status=%d %s",
        _soup_message_get_status (msg), _soup_message_get_reason_phrase (msg));
  }

  if (error || !SOUP_STATUS_IS_SUCCESSFUL (_soup_message_get_status (msg))) {
    sink->failures++;
    if (sink->retries && (sink->retries < 0 || sink->retries >= sink->failures)) {
      guint64 retry_delay;
      const char *retry_after;
      SoupMessageHeaders *res_hdrs;
      if (error) {
        retry_delay = sink->retry_delay;
        GST_WARNING_OBJECT (sink, "Could not write to HTTP URI: "
            "error: %d %s (retrying PUT after %" G_GINT64_FORMAT
            " seconds)", error->code, error->message, retry_delay);
        goto err_done;
      }
      res_hdrs = _soup_message_get_response_headers (msg);
      retry_after = _soup_message_headers_get_one (res_hdrs, "Retry-After");
      if (retry_after) {
        gchar *end = NULL;
        retry_delay = g_ascii_strtoull (retry_after, &end, 10);
        if (end || errno) {
          retry_delay = sink->retry_delay;
        } else {
          retry_delay = MAX (retry_delay, sink->retry_delay);
        }
        GST_WARNING_OBJECT (sink, "Could not write to HTTP URI: "
            "status: %d %s (retrying PUT after %" G_GINT64_FORMAT
            " seconds with Retry-After: %s)",
            _soup_message_get_status (msg),
            _soup_message_get_reason_phrase (msg), retry_delay, retry_after);
      } else {
        retry_delay = sink->retry_delay;
        GST_WARNING_OBJECT (sink, "Could not write to HTTP URI: "
            "status: %d %s (retrying PUT after %" G_GINT64_FORMAT
            " seconds)",
            _soup_message_get_status (msg),
            _soup_message_get_reason_phrase (msg), retry_delay);
      }
    err_done:
      sink->timer = g_timeout_source_new_seconds (retry_delay);
      g_source_set_callback (sink->timer, (GSourceFunc) (send_message),
          sink, NULL);
      g_source_attach (sink->timer, sink->context);
    } else {
      sink->status_code = _soup_message_get_status (msg);
      sink->reason_phrase = g_strdup (_soup_message_get_reason_phrase (msg));
    }
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_soup_http_client_sink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstSoupHttpClientSink *souphttpsink = GST_SOUP_HTTP_CLIENT_SINK (sink);
  GSource *source;

  if (souphttpsink->status_code != 0) {
    GST_ELEMENT_ERROR (souphttpsink, RESOURCE, WRITE,
        ("Could not write to HTTP URI"),
        ("status: %d %s", souphttpsink->status_code,
            souphttpsink->reason_phrase));
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&souphttpsink->mutex);
  if (souphttpsink->location != NULL) {
    souphttpsink->queued_buffers =
        g_list_append (souphttpsink->queued_buffers, gst_buffer_ref (buffer));

    GST_DEBUG_OBJECT (souphttpsink, "setting callback for new buffers");
    source = g_idle_source_new ();
    g_source_set_callback (source, (GSourceFunc) (send_message),
        souphttpsink, NULL);
    g_source_attach (source, souphttpsink->context);
    g_source_unref (source);
  }
  g_mutex_unlock (&souphttpsink->mutex);

  return GST_FLOW_OK;
}

static void
restarted (SoupMessage * msg, GBytes * body)
{
  _soup_message_set_request_body_from_bytes (msg, NULL, body);
}

static gboolean
authenticate (SoupMessage * msg, SoupAuth * auth,
    gboolean retrying, gpointer user_data)
{
  GstSoupHttpClientSink *souphttpsink = GST_SOUP_HTTP_CLIENT_SINK (user_data);

  if (!retrying) {
    SoupStatus status_code = _soup_message_get_status (msg);
    /* First time authentication only, if we fail and are called again with retry true fall through */
    if (status_code == SOUP_STATUS_UNAUTHORIZED) {
      if (souphttpsink->user_id && souphttpsink->user_pw)
        _soup_auth_authenticate (auth, souphttpsink->user_id,
            souphttpsink->user_pw);
    } else if (status_code == SOUP_STATUS_PROXY_AUTHENTICATION_REQUIRED) {
      if (souphttpsink->proxy_id && souphttpsink->proxy_pw)
        _soup_auth_authenticate (auth, souphttpsink->proxy_id,
            souphttpsink->proxy_pw);
    }
  }
  return FALSE;
}

static gboolean
souphttpclientsink_element_init (GstPlugin * plugin)
{
  gboolean ret = TRUE;

  GST_DEBUG_CATEGORY_INIT (souphttpclientsink_dbg, "souphttpclientsink", 0,
      "souphttpclientsink element");

  if (!soup_element_init (plugin))
    return TRUE;

  ret =
      gst_element_register (plugin, "souphttpclientsink", GST_RANK_NONE,
      GST_TYPE_SOUP_HTTP_CLIENT_SINK);

  return ret;
}
