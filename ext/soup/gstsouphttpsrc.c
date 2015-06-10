/* GStreamer
 * Copyright (C) 2007-2008 Wouter Cloetens <wouter@mind.be>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more
 */

/**
 * SECTION:element-souphttpsrc
 *
 * This plugin reads data from a remote location specified by a URI.
 * Supported protocols are 'http', 'https'.
 *
 * An HTTP proxy must be specified by its URL.
 * If the "http_proxy" environment variable is set, its value is used.
 * If built with libsoup's GNOME integration features, the GNOME proxy
 * configuration will be used, or failing that, proxy autodetection.
 * The #GstSoupHTTPSrc:proxy property can be used to override the default.
 *
 * In case the #GstSoupHTTPSrc:iradio-mode property is set and the location is
 * an HTTP resource, souphttpsrc will send special Icecast HTTP headers to the
 * server to request additional Icecast meta-information.
 * If the server is not an Icecast server, it will behave as if the
 * #GstSoupHTTPSrc:iradio-mode property were not set. If it is, souphttpsrc will
 * output data with a media type of application/x-icy, in which case you will
 * need to use the #ICYDemux element as follow-up element to extract the Icecast
 * metadata and to determine the underlying media type.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v souphttpsrc location=https://some.server.org/index.html
 *     ! filesink location=/home/joe/server.html
 * ]| The above pipeline reads a web page from a server using the HTTPS protocol
 * and writes it to a local file.
 * |[
 * gst-launch-1.0 -v souphttpsrc user-agent="FooPlayer 0.99 beta"
 *     automatic-redirect=false proxy=http://proxy.intranet.local:8080
 *     location=http://music.foobar.com/demo.mp3 ! mad ! audioconvert
 *     ! audioresample ! alsasink
 * ]| The above pipeline will read and decode and play an mp3 file from a
 * web server using the HTTP protocol. If the server sends redirects,
 * the request fails instead of following the redirect. The specified
 * HTTP proxy server is used. The User-Agent HTTP request header
 * is set to a custom string instead of "GStreamer souphttpsrc."
 * |[
 * gst-launch-1.0 -v souphttpsrc location=http://10.11.12.13/mjpeg
 *     do-timestamp=true ! multipartdemux
 *     ! image/jpeg,width=640,height=480 ! matroskamux
 *     ! filesink location=mjpeg.mkv
 * ]| The above pipeline reads a motion JPEG stream from an IP camera
 * using the HTTP protocol, encoded as mime/multipart image/jpeg
 * parts, and writes a Matroska motion JPEG file. The width and
 * height properties are set in the caps to provide the Matroska
 * multiplexer with the information to set this in the header.
 * Timestamps are set on the buffers as they arrive from the camera.
 * These are used by the mime/multipart demultiplexer to emit timestamps
 * on the JPEG-encoded video frame buffers. This allows the Matroska
 * multiplexer to timestamp the frames in the resulting file.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>             /* atoi() */
#endif
#include <gst/gstelement.h>
#include <gst/gst-i18n-plugin.h>
#include <libsoup/soup.h>
#include "gstsouphttpsrc.h"
#include "gstsouputils.h"

/* libsoup before 2.47.0 was stealing our main context from us,
 * so we can't reliable use it to clean up all pending resources
 * once we're done... let's just continue leaking on old versions.
 * https://bugzilla.gnome.org/show_bug.cgi?id=663944
 */
#if defined(SOUP_MINOR_VERSION) && SOUP_MINOR_VERSION >= 47
#define LIBSOUP_DOES_NOT_STEAL_OUR_CONTEXT 1
#endif

#include <gst/tag/tag.h>

GST_DEBUG_CATEGORY_STATIC (souphttpsrc_debug);
#define GST_CAT_DEFAULT souphttpsrc_debug

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_IS_LIVE,
  PROP_USER_AGENT,
  PROP_AUTOMATIC_REDIRECT,
  PROP_PROXY,
  PROP_USER_ID,
  PROP_USER_PW,
  PROP_PROXY_ID,
  PROP_PROXY_PW,
  PROP_COOKIES,
  PROP_IRADIO_MODE,
  PROP_TIMEOUT,
  PROP_EXTRA_HEADERS,
  PROP_SOUP_LOG_LEVEL,
  PROP_COMPRESS,
  PROP_KEEP_ALIVE,
  PROP_SSL_STRICT,
  PROP_SSL_CA_FILE,
  PROP_SSL_USE_SYSTEM_CA_FILE,
  PROP_TLS_DATABASE,
  PROP_RETRIES,
  PROP_METHOD,
  PROP_TLS_INTERACTION,
};

#define DEFAULT_USER_AGENT           "GStreamer souphttpsrc "
#define DEFAULT_IRADIO_MODE          TRUE
#define DEFAULT_SOUP_LOG_LEVEL       SOUP_LOGGER_LOG_HEADERS
#define DEFAULT_COMPRESS             FALSE
#define DEFAULT_KEEP_ALIVE           FALSE
#define DEFAULT_SSL_STRICT           TRUE
#define DEFAULT_SSL_CA_FILE          NULL
#define DEFAULT_SSL_USE_SYSTEM_CA_FILE TRUE
#define DEFAULT_TLS_DATABASE         NULL
#define DEFAULT_TLS_INTERACTION      NULL
#define DEFAULT_TIMEOUT              15
#define DEFAULT_RETRIES              3
#define DEFAULT_SOUP_METHOD          NULL

static void gst_soup_http_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static void gst_soup_http_src_finalize (GObject * gobject);
static void gst_soup_http_src_dispose (GObject * gobject);

static void gst_soup_http_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_soup_http_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_soup_http_src_change_state (GstElement *
    element, GstStateChange transition);
static GstFlowReturn gst_soup_http_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_soup_http_src_start (GstBaseSrc * bsrc);
static gboolean gst_soup_http_src_stop (GstBaseSrc * bsrc);
static gboolean gst_soup_http_src_get_size (GstBaseSrc * bsrc, guint64 * size);
static gboolean gst_soup_http_src_is_seekable (GstBaseSrc * bsrc);
static gboolean gst_soup_http_src_do_seek (GstBaseSrc * bsrc,
    GstSegment * segment);
static gboolean gst_soup_http_src_query (GstBaseSrc * bsrc, GstQuery * query);
static gboolean gst_soup_http_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_soup_http_src_unlock_stop (GstBaseSrc * bsrc);
static gboolean gst_soup_http_src_set_location (GstSoupHTTPSrc * src,
    const gchar * uri, GError ** error);
static gboolean gst_soup_http_src_set_proxy (GstSoupHTTPSrc * src,
    const gchar * uri);
static char *gst_soup_http_src_unicodify (const char *str);
static gboolean gst_soup_http_src_build_message (GstSoupHTTPSrc * src,
    const gchar * method);
static void gst_soup_http_src_cancel_message (GstSoupHTTPSrc * src);
static void gst_soup_http_src_queue_message (GstSoupHTTPSrc * src);
static gboolean gst_soup_http_src_add_range_header (GstSoupHTTPSrc * src,
    guint64 offset, guint64 stop_offset);
static void gst_soup_http_src_session_unpause_message (GstSoupHTTPSrc * src);
static void gst_soup_http_src_session_pause_message (GstSoupHTTPSrc * src);
static gboolean gst_soup_http_src_session_open (GstSoupHTTPSrc * src);
static void gst_soup_http_src_session_close (GstSoupHTTPSrc * src);
static void gst_soup_http_src_parse_status (SoupMessage * msg,
    GstSoupHTTPSrc * src);
static void gst_soup_http_src_chunk_free (gpointer gstbuf);
static SoupBuffer *gst_soup_http_src_chunk_allocator (SoupMessage * msg,
    gsize max_len, gpointer user_data);
static void gst_soup_http_src_got_chunk_cb (SoupMessage * msg,
    SoupBuffer * chunk, GstSoupHTTPSrc * src);
static void gst_soup_http_src_response_cb (SoupSession * session,
    SoupMessage * msg, GstSoupHTTPSrc * src);
static void gst_soup_http_src_got_headers_cb (SoupMessage * msg,
    GstSoupHTTPSrc * src);
static void gst_soup_http_src_got_body_cb (SoupMessage * msg,
    GstSoupHTTPSrc * src);
static void gst_soup_http_src_finished_cb (SoupMessage * msg,
    GstSoupHTTPSrc * src);
static void gst_soup_http_src_authenticate_cb (SoupSession * session,
    SoupMessage * msg, SoupAuth * auth, gboolean retrying,
    GstSoupHTTPSrc * src);

#define gst_soup_http_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstSoupHTTPSrc, gst_soup_http_src, GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_soup_http_src_uri_handler_init));

static void
gst_soup_http_src_class_init (GstSoupHTTPSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_soup_http_src_set_property;
  gobject_class->get_property = gst_soup_http_src_get_property;
  gobject_class->finalize = gst_soup_http_src_finalize;
  gobject_class->dispose = gst_soup_http_src_dispose;

  g_object_class_install_property (gobject_class,
      PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Location to read from", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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
          "HTTP location URI user id for authentication", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_USER_PW,
      g_param_spec_string ("user-pw", "user-pw",
          "HTTP location URI user password for authentication", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PROXY_ID,
      g_param_spec_string ("proxy-id", "proxy-id",
          "HTTP proxy URI user id for authentication", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PROXY_PW,
      g_param_spec_string ("proxy-pw", "proxy-pw",
          "HTTP proxy URI user password for authentication", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_COOKIES,
      g_param_spec_boxed ("cookies", "Cookies", "HTTP request cookies",
          G_TYPE_STRV, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_IS_LIVE,
      g_param_spec_boolean ("is-live", "is-live", "Act like a live source",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_uint ("timeout", "timeout",
          "Value in seconds to timeout a blocking I/O (0 = No timeout).", 0,
          3600, DEFAULT_TIMEOUT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_EXTRA_HEADERS,
      g_param_spec_boxed ("extra-headers", "Extra Headers",
          "Extra headers to append to the HTTP request",
          GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_IRADIO_MODE,
      g_param_spec_boolean ("iradio-mode", "iradio-mode",
          "Enable internet radio mode (ask server to send shoutcast/icecast "
          "metadata interleaved with the actual stream data)",
          DEFAULT_IRADIO_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

 /**
   * GstSoupHTTPSrc::http-log-level:
   *
   * If set and > 0, captures and dumps HTTP session data as
   * log messages if log level >= GST_LEVEL_TRACE
   *
   * Since: 1.4
   */
  g_object_class_install_property (gobject_class, PROP_SOUP_LOG_LEVEL,
      g_param_spec_enum ("http-log-level", "HTTP log level",
          "Set log level for soup's HTTP session log",
          SOUP_TYPE_LOGGER_LOG_LEVEL, DEFAULT_SOUP_LOG_LEVEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

 /**
   * GstSoupHTTPSrc::compress:
   *
   * If set to %TRUE, souphttpsrc will automatically handle gzip
   * and deflate Content-Encodings. This does not make much difference
   * and causes more load for normal media files, but makes a real
   * difference in size for plaintext files.
   *
   * Since: 1.4
   */
  g_object_class_install_property (gobject_class, PROP_COMPRESS,
      g_param_spec_boolean ("compress", "Compress",
          "Allow compressed content encodings",
          DEFAULT_COMPRESS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

 /**
   * GstSoupHTTPSrc::keep-alive:
   *
   * If set to %TRUE, souphttpsrc will keep alive connections when being
   * set to READY state and only will close connections when connecting
   * to a different server or when going to NULL state..
   *
   * Since: 1.4
   */
  g_object_class_install_property (gobject_class, PROP_KEEP_ALIVE,
      g_param_spec_boolean ("keep-alive", "keep-alive",
          "Use HTTP persistent connections", DEFAULT_KEEP_ALIVE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

 /**
   * GstSoupHTTPSrc::ssl-strict:
   *
   * If set to %TRUE, souphttpsrc will reject all SSL certificates that
   * are considered invalid.
   *
   * Since: 1.4
   */
  g_object_class_install_property (gobject_class, PROP_SSL_STRICT,
      g_param_spec_boolean ("ssl-strict", "SSL Strict",
          "Strict SSL certificate checking", DEFAULT_SSL_STRICT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

 /**
   * GstSoupHTTPSrc::ssl-ca-file:
   *
   * A SSL anchor CA file that should be used for checking certificates
   * instead of the system CA file.
   *
   * If this property is non-%NULL, #GstSoupHTTPSrc::ssl-use-system-ca-file
   * value will be ignored.
   *
   * Deprecated: Use #GstSoupHTTPSrc::tls-database property instead.
   * Since: 1.4
   */
  g_object_class_install_property (gobject_class, PROP_SSL_CA_FILE,
      g_param_spec_string ("ssl-ca-file", "SSL CA File",
          "Location of a SSL anchor CA file to use", DEFAULT_SSL_CA_FILE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

 /**
   * GstSoupHTTPSrc::ssl-use-system-ca-file:
   *
   * If set to %TRUE, souphttpsrc will use the system's CA file for
   * checking certificates, unless #GstSoupHTTPSrc::ssl-ca-file or
   * #GstSoupHTTPSrc::tls-database are non-%NULL.
   *
   * Since: 1.4
   */
  g_object_class_install_property (gobject_class, PROP_SSL_USE_SYSTEM_CA_FILE,
      g_param_spec_boolean ("ssl-use-system-ca-file", "Use System CA File",
          "Use system CA file", DEFAULT_SSL_USE_SYSTEM_CA_FILE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSoupHTTPSrc::tls-database:
   *
   * TLS database with anchor certificate authorities used to validate
   * the server certificate.
   *
   * If this property is non-%NULL, #GstSoupHTTPSrc::ssl-use-system-ca-file
   * and #GstSoupHTTPSrc::ssl-ca-file values will be ignored.
   *
   * Since: 1.6
   */
  g_object_class_install_property (gobject_class, PROP_TLS_DATABASE,
      g_param_spec_object ("tls-database", "TLS database",
          "TLS database with anchor certificate authorities used to validate the server certificate",
          G_TYPE_TLS_DATABASE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSoupHTTPSrc::tls-interaction:
   *
   * A #GTlsInteraction object to be used when the connection or certificate
   * database need to interact with the user. This will be used to prompt the
   * user for passwords or certificate where necessary.
   *
   * Since: 1.8
   */
  g_object_class_install_property (gobject_class, PROP_TLS_INTERACTION,
      g_param_spec_object ("tls-interaction", "TLS interaction",
          "A GTlsInteraction object to be used when the connection or certificate database need to interact with the user.",
          G_TYPE_TLS_INTERACTION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

 /**
   * GstSoupHTTPSrc::retries:
   *
   * Maximum number of retries until giving up.
   *
   * Since: 1.4
   */
  g_object_class_install_property (gobject_class, PROP_RETRIES,
      g_param_spec_int ("retries", "Retries",
          "Maximum number of retries until giving up (-1=infinite)", -1,
          G_MAXINT, DEFAULT_RETRIES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

 /**
   * GstSoupHTTPSrc::method
   *
   * The HTTP method to use when making a request
   *
   * Since: 1.6
   */
  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_string ("method", "HTTP method",
          "The HTTP method to use (GET, HEAD, OPTIONS, etc)",
          DEFAULT_SOUP_METHOD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_static_metadata (gstelement_class, "HTTP client source",
      "Source/Network",
      "Receive data as a client over the network via HTTP using SOUP",
      "Wouter Cloetens <wouter@mind.be>");
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_soup_http_src_change_state);

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_soup_http_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_soup_http_src_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_soup_http_src_unlock);
  gstbasesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_soup_http_src_unlock_stop);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_soup_http_src_get_size);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_soup_http_src_is_seekable);
  gstbasesrc_class->do_seek = GST_DEBUG_FUNCPTR (gst_soup_http_src_do_seek);
  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (gst_soup_http_src_query);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_soup_http_src_create);

  GST_DEBUG_CATEGORY_INIT (souphttpsrc_debug, "souphttpsrc", 0,
      "SOUP HTTP src");
}

static void
gst_soup_http_src_reset (GstSoupHTTPSrc * src)
{
  src->interrupted = FALSE;
  src->retry = FALSE;
  src->retry_count = 0;
  src->have_size = FALSE;
  src->got_headers = FALSE;
  src->seekable = FALSE;
  src->read_position = 0;
  src->request_position = 0;
  src->stop_position = -1;
  src->content_size = 0;
  src->have_body = FALSE;

  src->ret = GST_FLOW_OK;

  gst_caps_replace (&src->src_caps, NULL);
  g_free (src->iradio_name);
  src->iradio_name = NULL;
  g_free (src->iradio_genre);
  src->iradio_genre = NULL;
  g_free (src->iradio_url);
  src->iradio_url = NULL;
}

static void
gst_soup_http_src_init (GstSoupHTTPSrc * src)
{
  const gchar *proxy;

  g_mutex_init (&src->mutex);
  g_cond_init (&src->request_finished_cond);
  src->location = NULL;
  src->redirection_uri = NULL;
  src->automatic_redirect = TRUE;
  src->user_agent = g_strdup (DEFAULT_USER_AGENT);
  src->user_id = NULL;
  src->user_pw = NULL;
  src->proxy_id = NULL;
  src->proxy_pw = NULL;
  src->cookies = NULL;
  src->iradio_mode = DEFAULT_IRADIO_MODE;
  src->loop = NULL;
  src->context = NULL;
  src->session = NULL;
  src->msg = NULL;
  src->timeout = DEFAULT_TIMEOUT;
  src->log_level = DEFAULT_SOUP_LOG_LEVEL;
  src->ssl_strict = DEFAULT_SSL_STRICT;
  src->ssl_use_system_ca_file = DEFAULT_SSL_USE_SYSTEM_CA_FILE;
  src->tls_database = DEFAULT_TLS_DATABASE;
  src->tls_interaction = DEFAULT_TLS_INTERACTION;
  src->max_retries = DEFAULT_RETRIES;
  src->method = DEFAULT_SOUP_METHOD;
  proxy = g_getenv ("http_proxy");
  if (!gst_soup_http_src_set_proxy (src, proxy)) {
    GST_WARNING_OBJECT (src,
        "The proxy in the http_proxy env var (\"%s\") cannot be parsed.",
        proxy);
  }

  gst_base_src_set_automatic_eos (GST_BASE_SRC (src), FALSE);

  gst_soup_http_src_reset (src);
}

static void
gst_soup_http_src_dispose (GObject * gobject)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (gobject);

  GST_DEBUG_OBJECT (src, "dispose");

  gst_soup_http_src_session_close (src);

  G_OBJECT_CLASS (parent_class)->dispose (gobject);
}

static void
gst_soup_http_src_finalize (GObject * gobject)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (gobject);

  GST_DEBUG_OBJECT (src, "finalize");

  g_mutex_clear (&src->mutex);
  g_cond_clear (&src->request_finished_cond);
  g_free (src->location);
  g_free (src->redirection_uri);
  g_free (src->user_agent);
  if (src->proxy != NULL) {
    soup_uri_free (src->proxy);
  }
  g_free (src->user_id);
  g_free (src->user_pw);
  g_free (src->proxy_id);
  g_free (src->proxy_pw);
  g_strfreev (src->cookies);

  if (src->extra_headers) {
    gst_structure_free (src->extra_headers);
    src->extra_headers = NULL;
  }

  g_free (src->ssl_ca_file);

  if (src->tls_database)
    g_object_unref (src->tls_database);
  g_free (src->method);

  if (src->tls_interaction)
    g_object_unref (src->tls_interaction);

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static void
gst_soup_http_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
    {
      const gchar *location;

      location = g_value_get_string (value);

      if (location == NULL) {
        GST_WARNING ("location property cannot be NULL");
        goto done;
      }
      if (!gst_soup_http_src_set_location (src, location, NULL)) {
        GST_WARNING ("badly formatted location");
        goto done;
      }
      break;
    }
    case PROP_USER_AGENT:
      g_free (src->user_agent);
      src->user_agent = g_value_dup_string (value);
      break;
    case PROP_IRADIO_MODE:
      src->iradio_mode = g_value_get_boolean (value);
      break;
    case PROP_AUTOMATIC_REDIRECT:
      src->automatic_redirect = g_value_get_boolean (value);
      break;
    case PROP_PROXY:
    {
      const gchar *proxy;

      proxy = g_value_get_string (value);
      if (!gst_soup_http_src_set_proxy (src, proxy)) {
        GST_WARNING ("badly formatted proxy URI");
        goto done;
      }
      break;
    }
    case PROP_COOKIES:
      g_strfreev (src->cookies);
      src->cookies = g_strdupv (g_value_get_boxed (value));
      break;
    case PROP_IS_LIVE:
      gst_base_src_set_live (GST_BASE_SRC (src), g_value_get_boolean (value));
      break;
    case PROP_USER_ID:
      g_free (src->user_id);
      src->user_id = g_value_dup_string (value);
      break;
    case PROP_USER_PW:
      g_free (src->user_pw);
      src->user_pw = g_value_dup_string (value);
      break;
    case PROP_PROXY_ID:
      g_free (src->proxy_id);
      src->proxy_id = g_value_dup_string (value);
      break;
    case PROP_PROXY_PW:
      g_free (src->proxy_pw);
      src->proxy_pw = g_value_dup_string (value);
      break;
    case PROP_TIMEOUT:
      src->timeout = g_value_get_uint (value);
      break;
    case PROP_EXTRA_HEADERS:{
      const GstStructure *s = gst_value_get_structure (value);

      if (src->extra_headers)
        gst_structure_free (src->extra_headers);

      src->extra_headers = s ? gst_structure_copy (s) : NULL;
      break;
    }
    case PROP_SOUP_LOG_LEVEL:
      src->log_level = g_value_get_enum (value);
      break;
    case PROP_COMPRESS:
      src->compress = g_value_get_boolean (value);
      break;
    case PROP_KEEP_ALIVE:
      src->keep_alive = g_value_get_boolean (value);
      break;
    case PROP_SSL_STRICT:
      src->ssl_strict = g_value_get_boolean (value);
      break;
    case PROP_SSL_CA_FILE:
      g_free (src->ssl_ca_file);
      src->ssl_ca_file = g_value_dup_string (value);
      break;
    case PROP_SSL_USE_SYSTEM_CA_FILE:
      src->ssl_use_system_ca_file = g_value_get_boolean (value);
      break;
    case PROP_TLS_DATABASE:
      g_clear_object (&src->tls_database);
      src->tls_database = g_value_dup_object (value);
      break;
    case PROP_TLS_INTERACTION:
      g_clear_object (&src->tls_interaction);
      src->tls_interaction = g_value_dup_object (value);
      break;
    case PROP_RETRIES:
      src->max_retries = g_value_get_int (value);
      break;
    case PROP_METHOD:
      g_free (src->method);
      src->method = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
done:
  return;
}

static void
gst_soup_http_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, src->location);
      break;
    case PROP_USER_AGENT:
      g_value_set_string (value, src->user_agent);
      break;
    case PROP_AUTOMATIC_REDIRECT:
      g_value_set_boolean (value, src->automatic_redirect);
      break;
    case PROP_PROXY:
      if (src->proxy == NULL)
        g_value_set_static_string (value, "");
      else {
        char *proxy = soup_uri_to_string (src->proxy, FALSE);

        g_value_set_string (value, proxy);
        g_free (proxy);
      }
      break;
    case PROP_COOKIES:
      g_value_set_boxed (value, g_strdupv (src->cookies));
      break;
    case PROP_IS_LIVE:
      g_value_set_boolean (value, gst_base_src_is_live (GST_BASE_SRC (src)));
      break;
    case PROP_IRADIO_MODE:
      g_value_set_boolean (value, src->iradio_mode);
      break;
    case PROP_USER_ID:
      g_value_set_string (value, src->user_id);
      break;
    case PROP_USER_PW:
      g_value_set_string (value, src->user_pw);
      break;
    case PROP_PROXY_ID:
      g_value_set_string (value, src->proxy_id);
      break;
    case PROP_PROXY_PW:
      g_value_set_string (value, src->proxy_pw);
      break;
    case PROP_TIMEOUT:
      g_value_set_uint (value, src->timeout);
      break;
    case PROP_EXTRA_HEADERS:
      gst_value_set_structure (value, src->extra_headers);
      break;
    case PROP_SOUP_LOG_LEVEL:
      g_value_set_enum (value, src->log_level);
      break;
    case PROP_COMPRESS:
      g_value_set_boolean (value, src->compress);
      break;
    case PROP_KEEP_ALIVE:
      g_value_set_boolean (value, src->keep_alive);
      break;
    case PROP_SSL_STRICT:
      g_value_set_boolean (value, src->ssl_strict);
      break;
    case PROP_SSL_CA_FILE:
      g_value_set_string (value, src->ssl_ca_file);
      break;
    case PROP_SSL_USE_SYSTEM_CA_FILE:
      g_value_set_boolean (value, src->ssl_use_system_ca_file);
      break;
    case PROP_TLS_DATABASE:
      g_value_set_object (value, src->tls_database);
      break;
    case PROP_TLS_INTERACTION:
      g_value_set_object (value, src->tls_interaction);
      break;
    case PROP_RETRIES:
      g_value_set_int (value, src->max_retries);
      break;
    case PROP_METHOD:
      g_value_set_string (value, src->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gchar *
gst_soup_http_src_unicodify (const gchar * str)
{
  const gchar *env_vars[] = { "GST_ICY_TAG_ENCODING",
    "GST_TAG_ENCODING", NULL
  };

  return gst_tag_freeform_string_to_utf8 (str, -1, env_vars);
}

static void
gst_soup_http_src_cancel_message (GstSoupHTTPSrc * src)
{
  if (src->msg != NULL) {
    GST_INFO_OBJECT (src, "Cancelling message");
    src->session_io_status = GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_CANCELLED;
    soup_session_cancel_message (src->session, src->msg, SOUP_STATUS_CANCELLED);
  }
  src->session_io_status = GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_IDLE;
  src->msg = NULL;
}

static void
gst_soup_http_src_queue_message (GstSoupHTTPSrc * src)
{
  soup_session_queue_message (src->session, src->msg,
      (SoupSessionCallback) gst_soup_http_src_response_cb, src);
  src->session_io_status = GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_QUEUED;
}

static gboolean
gst_soup_http_src_add_range_header (GstSoupHTTPSrc * src, guint64 offset,
    guint64 stop_offset)
{
  gchar buf[64];
  gint rc;

  soup_message_headers_remove (src->msg->request_headers, "Range");
  if (offset || stop_offset != -1) {
    if (stop_offset != -1) {
      g_assert (offset != stop_offset);

      rc = g_snprintf (buf, sizeof (buf), "bytes=%" G_GUINT64_FORMAT "-%"
          G_GUINT64_FORMAT, offset, (stop_offset > 0) ? stop_offset - 1 :
          stop_offset);
    } else {
      rc = g_snprintf (buf, sizeof (buf), "bytes=%" G_GUINT64_FORMAT "-",
          offset);
    }
    if (rc > sizeof (buf) || rc < 0)
      return FALSE;
    soup_message_headers_append (src->msg->request_headers, "Range", buf);
  }
  src->read_position = offset;
  return TRUE;
}

static gboolean
_append_extra_header (GQuark field_id, const GValue * value, gpointer user_data)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (user_data);
  const gchar *field_name = g_quark_to_string (field_id);
  gchar *field_content = NULL;

  if (G_VALUE_TYPE (value) == G_TYPE_STRING) {
    field_content = g_value_dup_string (value);
  } else {
    GValue dest = { 0, };

    g_value_init (&dest, G_TYPE_STRING);
    if (g_value_transform (value, &dest)) {
      field_content = g_value_dup_string (&dest);
    }
  }

  if (field_content == NULL) {
    GST_ERROR_OBJECT (src, "extra-headers field '%s' contains no value "
        "or can't be converted to a string", field_name);
    return FALSE;
  }

  GST_DEBUG_OBJECT (src, "Appending extra header: \"%s: %s\"", field_name,
      field_content);
  soup_message_headers_append (src->msg->request_headers, field_name,
      field_content);

  g_free (field_content);

  return TRUE;
}

static gboolean
_append_extra_headers (GQuark field_id, const GValue * value,
    gpointer user_data)
{
  if (G_VALUE_TYPE (value) == GST_TYPE_ARRAY) {
    guint n = gst_value_array_get_size (value);
    guint i;

    for (i = 0; i < n; i++) {
      const GValue *v = gst_value_array_get_value (value, i);

      if (!_append_extra_header (field_id, v, user_data))
        return FALSE;
    }
  } else if (G_VALUE_TYPE (value) == GST_TYPE_LIST) {
    guint n = gst_value_list_get_size (value);
    guint i;

    for (i = 0; i < n; i++) {
      const GValue *v = gst_value_list_get_value (value, i);

      if (!_append_extra_header (field_id, v, user_data))
        return FALSE;
    }
  } else {
    return _append_extra_header (field_id, value, user_data);
  }

  return TRUE;
}


static gboolean
gst_soup_http_src_add_extra_headers (GstSoupHTTPSrc * src)
{
  if (!src->extra_headers)
    return TRUE;

  return gst_structure_foreach (src->extra_headers, _append_extra_headers, src);
}


static void
gst_soup_http_src_session_unpause_message (GstSoupHTTPSrc * src)
{
  soup_session_unpause_message (src->session, src->msg);
}

static void
gst_soup_http_src_session_pause_message (GstSoupHTTPSrc * src)
{
  soup_session_pause_message (src->session, src->msg);
}

static gboolean
gst_soup_http_src_session_open (GstSoupHTTPSrc * src)
{
  if (src->session) {
    GST_DEBUG_OBJECT (src, "Session is already open");
    return TRUE;
  }

  if (!src->location) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (_("No URL set.")),
        ("Missing location property"));
    return FALSE;
  }

  if (!src->context)
    src->context = g_main_context_new ();

  if (!src->loop)
    src->loop = g_main_loop_new (src->context, TRUE);
  if (!src->loop) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        (NULL), ("Failed to start GMainLoop"));
    g_main_context_unref (src->context);
    return FALSE;
  }

  if (!src->session) {
    GST_DEBUG_OBJECT (src, "Creating session");
    if (src->proxy == NULL) {
      src->session =
          soup_session_async_new_with_options (SOUP_SESSION_ASYNC_CONTEXT,
          src->context, SOUP_SESSION_USER_AGENT, src->user_agent,
          SOUP_SESSION_TIMEOUT, src->timeout,
          SOUP_SESSION_SSL_STRICT, src->ssl_strict,
          SOUP_SESSION_ADD_FEATURE_BY_TYPE, SOUP_TYPE_PROXY_RESOLVER_DEFAULT,
          SOUP_SESSION_TLS_INTERACTION, src->tls_interaction, NULL);
    } else {
      src->session =
          soup_session_async_new_with_options (SOUP_SESSION_ASYNC_CONTEXT,
          src->context, SOUP_SESSION_PROXY_URI, src->proxy,
          SOUP_SESSION_TIMEOUT, src->timeout,
          SOUP_SESSION_SSL_STRICT, src->ssl_strict,
          SOUP_SESSION_USER_AGENT, src->user_agent,
          SOUP_SESSION_TLS_INTERACTION, src->tls_interaction, NULL);
    }

    if (!src->session) {
      GST_ELEMENT_ERROR (src, LIBRARY, INIT,
          (NULL), ("Failed to create async session"));
      return FALSE;
    }

    g_signal_connect (src->session, "authenticate",
        G_CALLBACK (gst_soup_http_src_authenticate_cb), src);

    /* Set up logging */
    gst_soup_util_log_setup (src->session, src->log_level, GST_ELEMENT (src));
    if (src->tls_database)
      g_object_set (src->session, "tls-database", src->tls_database, NULL);
    else if (src->ssl_ca_file)
      g_object_set (src->session, "ssl-ca-file", src->ssl_ca_file, NULL);
    else
      g_object_set (src->session, "ssl-use-system-ca-file",
          src->ssl_use_system_ca_file, NULL);
  } else {
    GST_DEBUG_OBJECT (src, "Re-using session");
  }

  if (src->compress)
    soup_session_add_feature_by_type (src->session, SOUP_TYPE_CONTENT_DECODER);
  else
    soup_session_remove_feature_by_type (src->session,
        SOUP_TYPE_CONTENT_DECODER);

  return TRUE;
}

#ifdef LIBSOUP_DOES_NOT_STEAL_OUR_CONTEXT
static gboolean
dummy_idle_cb (gpointer data)
{
  return FALSE /* Idle source is removed */ ;
}
#endif

static void
gst_soup_http_src_session_close (GstSoupHTTPSrc * src)
{
  GST_DEBUG_OBJECT (src, "Closing session");

  if (src->loop)
    g_main_loop_quit (src->loop);

  g_mutex_lock (&src->mutex);
  if (src->session) {
    soup_session_abort (src->session);  /* This unrefs the message. */
    g_object_unref (src->session);
    src->session = NULL;
    src->msg = NULL;
  }
  if (src->loop) {
#ifdef LIBSOUP_DOES_NOT_STEAL_OUR_CONTEXT
    GSource *idle_source;

    /* Iterating the main context to give GIO cancellables a chance
     * to initiate cleanups. Wihout this, resources allocated by
     * libsoup for the connection are not released and socket fd is
     * leaked. */
    idle_source = g_idle_source_new ();
    /* Suppressing "idle souce without callback" warning */
    g_source_set_callback (idle_source, dummy_idle_cb, NULL, NULL);
    g_source_set_priority (idle_source, G_PRIORITY_LOW);
    g_source_attach (idle_source, src->context);
    /* Acquiring the context. Idle source guarantees that we'll not block. */
    g_main_context_push_thread_default (src->context);
    g_main_context_iteration (src->context, TRUE);
    /* Ensuring that there's no unhandled pending events left. */
    while (g_main_context_iteration (src->context, FALSE));
    g_main_context_pop_thread_default (src->context);
    g_source_unref (idle_source);
#endif

    g_main_loop_unref (src->loop);
    g_main_context_unref (src->context);
    src->loop = NULL;
    src->context = NULL;
  }
  g_mutex_unlock (&src->mutex);
}

static void
gst_soup_http_src_authenticate_cb (SoupSession * session, SoupMessage * msg,
    SoupAuth * auth, gboolean retrying, GstSoupHTTPSrc * src)
{
  if (!retrying) {
    /* First time authentication only, if we fail and are called again with retry true fall through */
    if (msg->status_code == SOUP_STATUS_UNAUTHORIZED) {
      if (src->user_id && src->user_pw)
        soup_auth_authenticate (auth, src->user_id, src->user_pw);
    } else if (msg->status_code == SOUP_STATUS_PROXY_AUTHENTICATION_REQUIRED) {
      if (src->proxy_id && src->proxy_pw)
        soup_auth_authenticate (auth, src->proxy_id, src->proxy_pw);
    }
  }
}

static void
insert_http_header (const gchar * name, const gchar * value, gpointer user_data)
{
  GstStructure *headers = user_data;
  const GValue *gv;

  gv = gst_structure_get_value (headers, name);
  if (gv && GST_VALUE_HOLDS_ARRAY (gv)) {
    GValue v = G_VALUE_INIT;

    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, value);
    gst_value_array_append_value ((GValue *) gv, &v);
    g_value_unset (&v);
  } else if (gv && G_VALUE_HOLDS_STRING (gv)) {
    GValue arr = G_VALUE_INIT;
    GValue v = G_VALUE_INIT;
    const gchar *old_value = g_value_get_string (gv);

    g_value_init (&arr, GST_TYPE_ARRAY);
    g_value_init (&v, G_TYPE_STRING);
    g_value_set_string (&v, old_value);
    gst_value_array_append_value (&arr, &v);
    g_value_set_string (&v, value);
    gst_value_array_append_value (&arr, &v);

    gst_structure_set_value (headers, name, &arr);
    g_value_unset (&v);
    g_value_unset (&arr);
  } else {
    gst_structure_set (headers, name, G_TYPE_STRING, value, NULL);
  }
}

static void
gst_soup_http_src_got_headers_cb (SoupMessage * msg, GstSoupHTTPSrc * src)
{
  const char *value;
  GstTagList *tag_list;
  GstBaseSrc *basesrc;
  guint64 newsize;
  GHashTable *params = NULL;
  GstEvent *http_headers_event;
  GstStructure *http_headers, *headers;
  const gchar *accept_ranges;

  GST_INFO_OBJECT (src, "got headers");

  if (msg->status_code == SOUP_STATUS_PROXY_AUTHENTICATION_REQUIRED &&
      src->proxy_id && src->proxy_pw)
    return;

  if (src->automatic_redirect && SOUP_STATUS_IS_REDIRECTION (msg->status_code)) {
    src->redirection_uri = g_strdup (soup_message_headers_get_one
        (msg->response_headers, "Location"));
    src->redirection_permanent =
        (msg->status_code == SOUP_STATUS_MOVED_PERMANENTLY);
    GST_DEBUG_OBJECT (src, "%u redirect to \"%s\" (permanent %d)",
        msg->status_code, src->redirection_uri, src->redirection_permanent);
    return;
  }

  if (msg->status_code == SOUP_STATUS_UNAUTHORIZED)
    return;

  src->session_io_status = GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING;
  src->got_headers = TRUE;

  http_headers = gst_structure_new_empty ("http-headers");
  gst_structure_set (http_headers, "uri", G_TYPE_STRING, src->location, NULL);
  if (src->redirection_uri)
    gst_structure_set (http_headers, "redirection-uri", G_TYPE_STRING,
        src->redirection_uri, NULL);
  headers = gst_structure_new_empty ("request-headers");
  soup_message_headers_foreach (msg->request_headers, insert_http_header,
      headers);
  gst_structure_set (http_headers, "request-headers", GST_TYPE_STRUCTURE,
      headers, NULL);
  gst_structure_free (headers);
  headers = gst_structure_new_empty ("response-headers");
  soup_message_headers_foreach (msg->response_headers, insert_http_header,
      headers);
  gst_structure_set (http_headers, "response-headers", GST_TYPE_STRUCTURE,
      headers, NULL);
  gst_structure_free (headers);

  http_headers_event =
      gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM_STICKY, http_headers);
  gst_event_replace (&src->http_headers_event, http_headers_event);
  gst_event_unref (http_headers_event);

  /* Parse Content-Length. */
  if (soup_message_headers_get_encoding (msg->response_headers) ==
      SOUP_ENCODING_CONTENT_LENGTH) {
    newsize = src->request_position +
        soup_message_headers_get_content_length (msg->response_headers);
    if (!src->have_size || (src->content_size != newsize)) {
      src->content_size = newsize;
      src->have_size = TRUE;
      src->seekable = TRUE;
      GST_DEBUG_OBJECT (src, "size = %" G_GUINT64_FORMAT, src->content_size);

      basesrc = GST_BASE_SRC_CAST (src);
      basesrc->segment.duration = src->content_size;
      gst_element_post_message (GST_ELEMENT (src),
          gst_message_new_duration_changed (GST_OBJECT (src)));
    }
  }

  /* If the server reports Accept-Ranges: none we don't have to try
   * doing range requests at all
   */
  if ((accept_ranges =
          soup_message_headers_get_one (msg->response_headers,
              "Accept-Ranges"))) {
    if (g_ascii_strcasecmp (accept_ranges, "none") == 0)
      src->seekable = FALSE;
  }

  /* Icecast stuff */
  tag_list = gst_tag_list_new_empty ();

  if ((value =
          soup_message_headers_get_one (msg->response_headers,
              "icy-metaint")) != NULL) {
    gint icy_metaint = atoi (value);

    GST_DEBUG_OBJECT (src, "icy-metaint: %s (parsed: %d)", value, icy_metaint);
    if (icy_metaint > 0) {
      if (src->src_caps)
        gst_caps_unref (src->src_caps);

      src->src_caps = gst_caps_new_simple ("application/x-icy",
          "metadata-interval", G_TYPE_INT, icy_metaint, NULL);

      gst_base_src_set_caps (GST_BASE_SRC (src), src->src_caps);
    }
  }
  if ((value =
          soup_message_headers_get_content_type (msg->response_headers,
              &params)) != NULL) {
    GST_DEBUG_OBJECT (src, "Content-Type: %s", value);
    if (g_ascii_strcasecmp (value, "audio/L16") == 0) {
      gint channels = 2;
      gint rate = 44100;
      char *param;

      if (src->src_caps)
        gst_caps_unref (src->src_caps);

      param = g_hash_table_lookup (params, "channels");
      if (param != NULL)
        channels = atol (param);

      param = g_hash_table_lookup (params, "rate");
      if (param != NULL)
        rate = atol (param);

      src->src_caps = gst_caps_new_simple ("audio/x-raw",
          "format", G_TYPE_STRING, "S16BE",
          "layout", G_TYPE_STRING, "interleaved",
          "channels", G_TYPE_INT, channels, "rate", G_TYPE_INT, rate, NULL);

      gst_base_src_set_caps (GST_BASE_SRC (src), src->src_caps);
    } else {
      /* Set the Content-Type field on the caps */
      if (src->src_caps) {
        src->src_caps = gst_caps_make_writable (src->src_caps);
        gst_caps_set_simple (src->src_caps, "content-type", G_TYPE_STRING,
            value, NULL);
        gst_base_src_set_caps (GST_BASE_SRC (src), src->src_caps);
      }
    }
  }

  if (params != NULL)
    g_hash_table_destroy (params);

  if ((value =
          soup_message_headers_get_one (msg->response_headers,
              "icy-name")) != NULL) {
    g_free (src->iradio_name);
    src->iradio_name = gst_soup_http_src_unicodify (value);
    if (src->iradio_name) {
      gst_tag_list_add (tag_list, GST_TAG_MERGE_REPLACE, GST_TAG_ORGANIZATION,
          src->iradio_name, NULL);
    }
  }
  if ((value =
          soup_message_headers_get_one (msg->response_headers,
              "icy-genre")) != NULL) {
    g_free (src->iradio_genre);
    src->iradio_genre = gst_soup_http_src_unicodify (value);
    if (src->iradio_genre) {
      gst_tag_list_add (tag_list, GST_TAG_MERGE_REPLACE, GST_TAG_GENRE,
          src->iradio_genre, NULL);
    }
  }
  if ((value = soup_message_headers_get_one (msg->response_headers, "icy-url"))
      != NULL) {
    g_free (src->iradio_url);
    src->iradio_url = gst_soup_http_src_unicodify (value);
    if (src->iradio_url) {
      gst_tag_list_add (tag_list, GST_TAG_MERGE_REPLACE, GST_TAG_LOCATION,
          src->iradio_url, NULL);
    }
  }
  if (!gst_tag_list_is_empty (tag_list)) {
    GST_DEBUG_OBJECT (src,
        "calling gst_element_found_tags with %" GST_PTR_FORMAT, tag_list);
    gst_pad_push_event (GST_BASE_SRC_PAD (src), gst_event_new_tag (tag_list));
  } else {
    gst_tag_list_unref (tag_list);
  }

  /* Handle HTTP errors. */
  gst_soup_http_src_parse_status (msg, src);

  /* Check if Range header was respected. */
  if (src->ret == GST_FLOW_CUSTOM_ERROR &&
      src->read_position && msg->status_code != SOUP_STATUS_PARTIAL_CONTENT) {
    src->seekable = FALSE;
    GST_ELEMENT_ERROR (src, RESOURCE, SEEK,
        (_("Server does not support seeking.")),
        ("Server does not accept Range HTTP header, URL: %s, Redirect to: %s",
            src->location, GST_STR_NULL (src->redirection_uri)));
    src->ret = GST_FLOW_ERROR;
  }

  /* If we are going to error out, stop all processing right here, so we
   * don't output any data (such as an error html page), and return
   * GST_FLOW_ERROR from the create function instead of having
   * got_chunk_cb overwrite src->ret with FLOW_OK again. */
  if (src->ret == GST_FLOW_ERROR || src->ret == GST_FLOW_EOS) {
    gst_soup_http_src_session_pause_message (src);

    if (src->loop)
      g_main_loop_quit (src->loop);
  }
  g_cond_signal (&src->request_finished_cond);
}

/* Have body. Signal EOS. */
static void
gst_soup_http_src_got_body_cb (SoupMessage * msg, GstSoupHTTPSrc * src)
{
  if (G_UNLIKELY (msg != src->msg)) {
    GST_DEBUG_OBJECT (src, "got body, but not for current message");
    return;
  }
  if (G_UNLIKELY (src->session_io_status !=
          GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING)) {
    /* Probably a redirect. */
    return;
  }
  GST_DEBUG_OBJECT (src, "got body");
  src->ret = GST_FLOW_EOS;
  src->have_body = TRUE;

  /* no need to interrupt the message here, we do it on the
   * finished_cb anyway if needed. And getting the body might mean
   * that the connection was hang up before finished. This happens when
   * the pipeline is stalled for too long (long pauses during playback).
   * Best to let it continue from here and pause because it reached the
   * final bytes based on content_size or received an out of range error */
}

/* Finished. Signal EOS. */
static void
gst_soup_http_src_finished_cb (SoupMessage * msg, GstSoupHTTPSrc * src)
{
  if (G_UNLIKELY (msg != src->msg)) {
    GST_DEBUG_OBJECT (src, "finished, but not for current message");
    return;
  }
  GST_INFO_OBJECT (src, "finished, io status: %d", src->session_io_status);
  src->ret = GST_FLOW_EOS;
  if (src->session_io_status == GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_CANCELLED) {
    /* gst_soup_http_src_cancel_message() triggered this; probably a seek
     * that occurred in the QUEUEING state; i.e. before the connection setup
     * was complete. Do nothing */
    GST_DEBUG_OBJECT (src, "cancelled");
  } else if (src->session_io_status ==
      GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING && src->read_position > 0 &&
      (src->have_size && src->read_position < src->content_size) &&
      (src->max_retries == -1 || src->retry_count < src->max_retries)) {
    /* The server disconnected while streaming. Reconnect and seeking to the
     * last location. */
    src->retry = TRUE;
    src->retry_count++;
    src->ret = GST_FLOW_CUSTOM_ERROR;
  } else if (G_UNLIKELY (src->session_io_status !=
          GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING)) {
    if (msg->method == SOUP_METHOD_HEAD) {
      GST_DEBUG_OBJECT (src, "Ignoring error %d:%s during HEAD request",
          msg->status_code, msg->reason_phrase);
    } else {
      gst_soup_http_src_parse_status (msg, src);
    }
  }
  if (src->loop)
    g_main_loop_quit (src->loop);
  g_cond_signal (&src->request_finished_cond);
}

/* Buffer lifecycle management.
 *
 * gst_soup_http_src_create() runs the GMainLoop for this element, to let
 * Soup take control.
 * A GstBuffer is allocated in gst_soup_http_src_chunk_allocator() and
 * associated with a SoupBuffer.
 * Soup reads HTTP data in the GstBuffer's data buffer.
 * The gst_soup_http_src_got_chunk_cb() is then called with the SoupBuffer.
 * That sets gst_soup_http_src_create()'s return argument to the GstBuffer,
 * increments its refcount (to 2), pauses the flow of data from the HTTP
 * source to prevent gst_soup_http_src_got_chunk_cb() from being called
 * again and breaks out of the GMainLoop.
 * Because the SOUP_MESSAGE_OVERWRITE_CHUNKS flag is set, Soup frees the
 * SoupBuffer and calls gst_soup_http_src_chunk_free(), which decrements the
 * refcount (to 1).
 * gst_soup_http_src_create() returns the GstBuffer. It will be freed by a
 * downstream element.
 * If Soup fails to read HTTP data, it does not call
 * gst_soup_http_src_got_chunk_cb(), but still frees the SoupBuffer and
 * calls gst_soup_http_src_chunk_free(), which decrements the GstBuffer's
 * refcount to 0, freeing it.
 */

typedef struct
{
  GstBuffer *buffer;
  GstMapInfo map;
} SoupGstChunk;

static void
gst_soup_http_src_chunk_free (gpointer user_data)
{
  SoupGstChunk *chunk = (SoupGstChunk *) user_data;

  gst_buffer_unmap (chunk->buffer, &chunk->map);
  gst_buffer_unref (chunk->buffer);
  g_slice_free (SoupGstChunk, chunk);
}

static SoupBuffer *
gst_soup_http_src_chunk_allocator (SoupMessage * msg, gsize max_len,
    gpointer user_data)
{
  GstSoupHTTPSrc *src = (GstSoupHTTPSrc *) user_data;
  GstBaseSrc *basesrc = GST_BASE_SRC_CAST (src);
  GstBuffer *gstbuf;
  SoupBuffer *soupbuf;
  gsize length;
  GstFlowReturn rc;
  SoupGstChunk *chunk;

  if (max_len)
    length = MIN (basesrc->blocksize, max_len);
  else
    length = basesrc->blocksize;
  GST_DEBUG_OBJECT (src, "alloc %" G_GSIZE_FORMAT " bytes <= %" G_GSIZE_FORMAT,
      length, max_len);

  rc = GST_BASE_SRC_CLASS (parent_class)->alloc (basesrc, -1, length, &gstbuf);
  if (G_UNLIKELY (rc != GST_FLOW_OK)) {
    /* Failed to allocate buffer. Stall SoupSession and return error code
     * to create(). */
    src->ret = rc;
    g_main_loop_quit (src->loop);
    return NULL;
  }

  chunk = g_slice_new0 (SoupGstChunk);
  chunk->buffer = gstbuf;
  gst_buffer_map (gstbuf, &chunk->map, GST_MAP_READWRITE);

  soupbuf = soup_buffer_new_with_owner (chunk->map.data, chunk->map.size,
      chunk, gst_soup_http_src_chunk_free);

  return soupbuf;
}

static void
gst_soup_http_src_got_chunk_cb (SoupMessage * msg, SoupBuffer * chunk,
    GstSoupHTTPSrc * src)
{
  GstBaseSrc *basesrc;
  guint64 new_position;
  SoupGstChunk *gchunk;

  if (G_UNLIKELY (msg != src->msg)) {
    GST_DEBUG_OBJECT (src, "got chunk, but not for current message");
    return;
  }
  if (G_UNLIKELY (!src->outbuf)) {
    GST_DEBUG_OBJECT (src, "got chunk but we're not expecting one");
    src->ret = GST_FLOW_OK;
    gst_soup_http_src_cancel_message (src);
    g_main_loop_quit (src->loop);
    return;
  }

  /* We got data, reset the retry counter */
  src->retry_count = 0;

  src->have_body = FALSE;
  if (G_UNLIKELY (src->session_io_status !=
          GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING)) {
    /* Probably a redirect. */
    return;
  }
  basesrc = GST_BASE_SRC_CAST (src);
  GST_DEBUG_OBJECT (src, "got chunk of %" G_GSIZE_FORMAT " bytes",
      chunk->length);

  /* Extract the GstBuffer from the SoupBuffer and set its fields. */
  gchunk = (SoupGstChunk *) soup_buffer_get_owner (chunk);
  *src->outbuf = gchunk->buffer;

  gst_buffer_resize (*src->outbuf, 0, chunk->length);
  GST_BUFFER_OFFSET (*src->outbuf) = basesrc->segment.position;

  gst_buffer_ref (*src->outbuf);

  new_position = src->read_position + chunk->length;
  if (G_LIKELY (src->request_position == src->read_position))
    src->request_position = new_position;
  src->read_position = new_position;

  if (src->have_size) {
    if (new_position > src->content_size) {
      GST_DEBUG_OBJECT (src, "Got position previous estimated content size "
          "(%" G_GINT64_FORMAT " > %" G_GINT64_FORMAT ")", new_position,
          src->content_size);
      src->content_size = new_position;
      basesrc->segment.duration = src->content_size;
      gst_element_post_message (GST_ELEMENT (src),
          gst_message_new_duration_changed (GST_OBJECT (src)));
    } else if (new_position == src->content_size) {
      GST_DEBUG_OBJECT (src, "We're EOS now");
    }
  }

  src->ret = GST_FLOW_OK;
  g_main_loop_quit (src->loop);
  gst_soup_http_src_session_pause_message (src);
}

static void
gst_soup_http_src_response_cb (SoupSession * session, SoupMessage * msg,
    GstSoupHTTPSrc * src)
{
  if (G_UNLIKELY (msg != src->msg)) {
    GST_DEBUG_OBJECT (src, "got response %d: %s, but not for current message",
        msg->status_code, msg->reason_phrase);
    return;
  }
  if (G_UNLIKELY (src->session_io_status !=
          GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING)
      && SOUP_STATUS_IS_REDIRECTION (msg->status_code)) {
    /* Ignore redirections. */
    return;
  }
  GST_INFO_OBJECT (src, "got response %d: %s", msg->status_code,
      msg->reason_phrase);
  if (src->session_io_status == GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING &&
      src->read_position > 0 && (src->have_size
          && src->read_position < src->content_size) &&
      (src->max_retries == -1 || src->retry_count < src->max_retries)) {
    /* The server disconnected while streaming. Reconnect and seeking to the
     * last location. */
    src->retry = TRUE;
    src->retry_count++;
  } else {
    gst_soup_http_src_parse_status (msg, src);
  }
  /* The session's SoupMessage object expires after this callback returns. */
  src->msg = NULL;
  g_main_loop_quit (src->loop);
}

#define SOUP_HTTP_SRC_ERROR(src,soup_msg,cat,code,error_message)     \
  GST_ELEMENT_ERROR ((src), cat, code, ("%s", error_message),        \
      ("%s (%d), URL: %s, Redirect to: %s", (soup_msg)->reason_phrase,                \
          (soup_msg)->status_code, (src)->location, GST_STR_NULL ((src)->redirection_uri)));

static void
gst_soup_http_src_parse_status (SoupMessage * msg, GstSoupHTTPSrc * src)
{
  if (msg->method == SOUP_METHOD_HEAD) {
    if (!SOUP_STATUS_IS_SUCCESSFUL (msg->status_code))
      GST_DEBUG_OBJECT (src, "Ignoring error %d during HEAD request",
          msg->status_code);
  } else if (SOUP_STATUS_IS_TRANSPORT_ERROR (msg->status_code)) {
    switch (msg->status_code) {
      case SOUP_STATUS_CANT_RESOLVE:
      case SOUP_STATUS_CANT_RESOLVE_PROXY:
        SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, NOT_FOUND,
            _("Could not resolve server name."));
        src->ret = GST_FLOW_ERROR;
        break;
      case SOUP_STATUS_CANT_CONNECT:
      case SOUP_STATUS_CANT_CONNECT_PROXY:
        SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, OPEN_READ,
            _("Could not establish connection to server."));
        src->ret = GST_FLOW_ERROR;
        break;
      case SOUP_STATUS_SSL_FAILED:
        SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, OPEN_READ,
            _("Secure connection setup failed."));
        src->ret = GST_FLOW_ERROR;
        break;
      case SOUP_STATUS_IO_ERROR:
        if (src->max_retries == -1 || src->retry_count < src->max_retries) {
          src->retry = TRUE;
          src->retry_count++;
          src->ret = GST_FLOW_CUSTOM_ERROR;
        } else {
          SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, READ,
              _("A network error occurred, or the server closed the connection "
                  "unexpectedly."));
          src->ret = GST_FLOW_ERROR;
        }
        break;
      case SOUP_STATUS_MALFORMED:
        SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, READ,
            _("Server sent bad data."));
        src->ret = GST_FLOW_ERROR;
        break;
      case SOUP_STATUS_CANCELLED:
        /* No error message when interrupted by program. */
        break;
    }
  } else if (SOUP_STATUS_IS_CLIENT_ERROR (msg->status_code) ||
      SOUP_STATUS_IS_REDIRECTION (msg->status_code) ||
      SOUP_STATUS_IS_SERVER_ERROR (msg->status_code)) {
    /* Report HTTP error. */

    /* when content_size is unknown and we have just finished receiving
     * a body message, requests that go beyond the content limits will result
     * in an error. Here we convert those to EOS */
    if (msg->status_code == SOUP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE &&
        src->have_body && !src->have_size) {
      GST_DEBUG_OBJECT (src, "Requested range out of limits and received full "
          "body, returning EOS");
      src->ret = GST_FLOW_EOS;
      return;
    }

    /* FIXME: reason_phrase is not translated and not suitable for user
     * error dialog according to libsoup documentation.
     */
    if (msg->status_code == SOUP_STATUS_NOT_FOUND) {
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
          ("%s", msg->reason_phrase),
          ("%s (%d), URL: %s, Redirect to: %s", msg->reason_phrase,
              msg->status_code, src->location,
              GST_STR_NULL (src->redirection_uri)));
    } else if (msg->status_code == SOUP_STATUS_UNAUTHORIZED
        || msg->status_code == SOUP_STATUS_PAYMENT_REQUIRED
        || msg->status_code == SOUP_STATUS_FORBIDDEN
        || msg->status_code == SOUP_STATUS_PROXY_AUTHENTICATION_REQUIRED) {
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_AUTHORIZED, ("%s",
              msg->reason_phrase), ("%s (%d), URL: %s, Redirect to: %s",
              msg->reason_phrase, msg->status_code, src->location,
              GST_STR_NULL (src->redirection_uri)));
    } else {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
          ("%s", msg->reason_phrase),
          ("%s (%d), URL: %s, Redirect to: %s", msg->reason_phrase,
              msg->status_code, src->location,
              GST_STR_NULL (src->redirection_uri)));
    }
    src->ret = GST_FLOW_ERROR;
  }
}

static gboolean
gst_soup_http_src_build_message (GstSoupHTTPSrc * src, const gchar * method)
{
  g_return_val_if_fail (src->msg == NULL, FALSE);

  src->msg = soup_message_new (method, src->location);
  if (!src->msg) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("Error parsing URL."), ("URL: %s", src->location));
    return FALSE;
  }
  src->session_io_status = GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_IDLE;
  if (!src->keep_alive) {
    soup_message_headers_append (src->msg->request_headers, "Connection",
        "close");
  }
  if (src->iradio_mode) {
    soup_message_headers_append (src->msg->request_headers, "icy-metadata",
        "1");
  }
  if (src->cookies) {
    gchar **cookie;

    for (cookie = src->cookies; *cookie != NULL; cookie++) {
      soup_message_headers_append (src->msg->request_headers, "Cookie",
          *cookie);
    }
  }
  src->retry = FALSE;

  g_signal_connect (src->msg, "got_headers",
      G_CALLBACK (gst_soup_http_src_got_headers_cb), src);
  g_signal_connect (src->msg, "got_body",
      G_CALLBACK (gst_soup_http_src_got_body_cb), src);
  g_signal_connect (src->msg, "finished",
      G_CALLBACK (gst_soup_http_src_finished_cb), src);
  g_signal_connect (src->msg, "got_chunk",
      G_CALLBACK (gst_soup_http_src_got_chunk_cb), src);
  soup_message_set_flags (src->msg, SOUP_MESSAGE_OVERWRITE_CHUNKS |
      (src->automatic_redirect ? 0 : SOUP_MESSAGE_NO_REDIRECT));
  soup_message_set_chunk_allocator (src->msg,
      gst_soup_http_src_chunk_allocator, src, NULL);
  gst_soup_http_src_add_range_header (src, src->request_position,
      src->stop_position);

  gst_soup_http_src_add_extra_headers (src);

  return TRUE;
}

static GstFlowReturn
gst_soup_http_src_do_request (GstSoupHTTPSrc * src, const gchar * method,
    GstBuffer ** outbuf)
{
  /* If we're not OK, just go out of here */
  if (src->ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (src, "Previous flow return not OK: %s",
        gst_flow_get_name (src->ret));
    return src->ret;
  }

  GST_LOG_OBJECT (src, "Running request for method: %s", method);
  if (src->msg && (src->request_position != src->read_position)) {
    if (src->session_io_status == GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_IDLE) {
      /* EOS immediately if we have an empty segment */
      if (src->request_position == src->stop_position)
        return GST_FLOW_EOS;

      gst_soup_http_src_add_range_header (src, src->request_position,
          src->stop_position);
    } else {
      GST_DEBUG_OBJECT (src, "Seek from position %" G_GUINT64_FORMAT
          " to %" G_GUINT64_FORMAT ": requeueing connection request",
          src->read_position, src->request_position);
      gst_soup_http_src_cancel_message (src);
    }
  }
  if (!src->msg) {
    /* EOS immediately if we have an empty segment */
    if (src->request_position == src->stop_position)
      return GST_FLOW_EOS;

    if (!gst_soup_http_src_build_message (src, method))
      return GST_FLOW_ERROR;
  }

  src->ret = GST_FLOW_CUSTOM_ERROR;
  src->outbuf = outbuf;
  do {
    if (src->interrupted) {
      GST_INFO_OBJECT (src, "interrupted");
      src->ret = GST_FLOW_FLUSHING;
      break;
    }
    if (src->retry) {
      GST_INFO_OBJECT (src, "Reconnecting");

      /* EOS immediately if we have an empty segment */
      if (src->request_position == src->stop_position)
        return GST_FLOW_EOS;

      if (!gst_soup_http_src_build_message (src, method))
        return GST_FLOW_ERROR;
      src->retry = FALSE;
      continue;
    }
    if (!src->msg) {
      GST_DEBUG_OBJECT (src, "EOS reached");
      break;
    }

    switch (src->session_io_status) {
      case GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_IDLE:
        GST_INFO_OBJECT (src, "Queueing connection request");
        gst_soup_http_src_queue_message (src);
        break;
      case GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_QUEUED:
        break;
      case GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING:
        gst_soup_http_src_session_unpause_message (src);
        break;
      case GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_CANCELLED:
        /* Impossible. */
        break;
    }

    if (src->ret == GST_FLOW_CUSTOM_ERROR) {
      g_main_context_push_thread_default (src->context);
      g_main_loop_run (src->loop);
      g_main_context_pop_thread_default (src->context);
    }

  } while (src->ret == GST_FLOW_CUSTOM_ERROR);

  /* Let the request finish if we had a stop position and are there */
  if (src->ret == GST_FLOW_OK && src->stop_position != -1
      && src->read_position >= src->stop_position) {
    src->outbuf = NULL;
    gst_soup_http_src_session_unpause_message (src);
    g_main_context_push_thread_default (src->context);
    g_main_loop_run (src->loop);
    g_main_context_pop_thread_default (src->context);

    g_cond_signal (&src->request_finished_cond);
    /* Return OK unconditionally here, src->ret will
     * be most likely be EOS now but we want to
     * consume the buffer we got above */
    return GST_FLOW_OK;
  }

  if (src->ret == GST_FLOW_CUSTOM_ERROR)
    src->ret = GST_FLOW_EOS;
  g_cond_signal (&src->request_finished_cond);

  /* basesrc assumes that we don't return a buffer if
   * something else than OK is returned. It will just
   * leak any buffer we might accidentially provide
   * here.
   *
   * This can potentially happen during flushing.
   */
  if (src->ret != GST_FLOW_OK && outbuf && *outbuf) {
    gst_buffer_unref (*outbuf);
    *outbuf = NULL;
  }

  return src->ret;
}

static GstFlowReturn
gst_soup_http_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstSoupHTTPSrc *src;
  GstFlowReturn ret;
  GstEvent *http_headers_event;

  src = GST_SOUP_HTTP_SRC (psrc);

  g_mutex_lock (&src->mutex);
  *outbuf = NULL;
  ret =
      gst_soup_http_src_do_request (src,
      src->method ? src->method : SOUP_METHOD_GET, outbuf);
  http_headers_event = src->http_headers_event;
  src->http_headers_event = NULL;
  g_mutex_unlock (&src->mutex);

  if (http_headers_event)
    gst_pad_push_event (GST_BASE_SRC_PAD (src), http_headers_event);

  return ret;
}

static gboolean
gst_soup_http_src_start (GstBaseSrc * bsrc)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (bsrc);

  GST_DEBUG_OBJECT (src, "start(\"%s\")", src->location);

  return gst_soup_http_src_session_open (src);
}

static gboolean
gst_soup_http_src_stop (GstBaseSrc * bsrc)
{
  GstSoupHTTPSrc *src;

  src = GST_SOUP_HTTP_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "stop()");
  if (src->keep_alive && !src->msg)
    gst_soup_http_src_cancel_message (src);
  else
    gst_soup_http_src_session_close (src);

  gst_soup_http_src_reset (src);
  return TRUE;
}

static GstStateChangeReturn
gst_soup_http_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstSoupHTTPSrc *src;

  src = GST_SOUP_HTTP_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_soup_http_src_session_close (src);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

/* Interrupt a blocking request. */
static gboolean
gst_soup_http_src_unlock (GstBaseSrc * bsrc)
{
  GstSoupHTTPSrc *src;

  src = GST_SOUP_HTTP_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "unlock()");

  src->interrupted = TRUE;
  src->ret = GST_FLOW_FLUSHING;
  if (src->loop)
    g_main_loop_quit (src->loop);
  g_cond_signal (&src->request_finished_cond);
  return TRUE;
}

/* Interrupt interrupt. */
static gboolean
gst_soup_http_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstSoupHTTPSrc *src;

  src = GST_SOUP_HTTP_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "unlock_stop()");

  src->interrupted = FALSE;
  src->ret = GST_FLOW_OK;
  return TRUE;
}

static gboolean
gst_soup_http_src_get_size (GstBaseSrc * bsrc, guint64 * size)
{
  GstSoupHTTPSrc *src;

  src = GST_SOUP_HTTP_SRC (bsrc);

  if (src->have_size) {
    GST_DEBUG_OBJECT (src, "get_size() = %" G_GUINT64_FORMAT,
        src->content_size);
    *size = src->content_size;
    return TRUE;
  }
  GST_DEBUG_OBJECT (src, "get_size() = FALSE");
  return FALSE;
}

static void
gst_soup_http_src_check_seekable (GstSoupHTTPSrc * src)
{
  GstFlowReturn ret = GST_FLOW_OK;

  /* Special case to check if the server allows range requests
   * before really starting to get data in the buffer creation
   * loops.
   */
  if (!src->got_headers && GST_STATE (src) >= GST_STATE_PAUSED) {
    g_mutex_lock (&src->mutex);
    while (!src->got_headers && !src->interrupted && ret == GST_FLOW_OK) {
      if ((src->msg && src->msg->method != SOUP_METHOD_HEAD) &&
          src->session_io_status != GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_IDLE) {
        /* wait for the current request to finish */
        g_cond_wait (&src->request_finished_cond, &src->mutex);
      } else {
        if (gst_soup_http_src_session_open (src)) {
          ret = gst_soup_http_src_do_request (src, SOUP_METHOD_HEAD, NULL);
        }
      }
    }
    if (src->ret == GST_FLOW_EOS) {
      /* A HEAD request shouldn't lead to EOS */
      src->ret = GST_FLOW_OK;
    }
    /* resets status to idle */
    gst_soup_http_src_cancel_message (src);
    g_mutex_unlock (&src->mutex);
  }
}

static gboolean
gst_soup_http_src_is_seekable (GstBaseSrc * bsrc)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (bsrc);

  gst_soup_http_src_check_seekable (src);

  return src->seekable;
}

static gboolean
gst_soup_http_src_do_seek (GstBaseSrc * bsrc, GstSegment * segment)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (bsrc);

  GST_DEBUG_OBJECT (src, "do_seek(%" G_GUINT64_FORMAT "-%" G_GUINT64_FORMAT
      ")", segment->start, segment->stop);
  if (src->read_position == segment->start &&
      src->request_position == src->read_position &&
      src->stop_position == segment->stop) {
    GST_DEBUG_OBJECT (src,
        "Seek to current read/end position and no seek pending");
    return TRUE;
  }

  gst_soup_http_src_check_seekable (src);

  /* If we have no headers we don't know yet if it is seekable or not.
   * Store the start position and error out later if it isn't */
  if (src->got_headers && !src->seekable) {
    GST_WARNING_OBJECT (src, "Not seekable");
    return FALSE;
  }

  if (segment->rate < 0.0 || segment->format != GST_FORMAT_BYTES) {
    GST_WARNING_OBJECT (src, "Invalid seek segment");
    return FALSE;
  }

  if (src->have_size && segment->start >= src->content_size) {
    GST_WARNING_OBJECT (src,
        "Potentially seeking behind end of file, might EOS immediately");
  }

  /* Wait for create() to handle the jump in offset. */
  src->request_position = segment->start;
  src->stop_position = segment->stop;

  return TRUE;
}

static gboolean
gst_soup_http_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (bsrc);
  gboolean ret;
  GstSchedulingFlags flags;
  gint minsize, maxsize, align;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_URI:
      gst_query_set_uri (query, src->location);
      if (src->redirection_uri != NULL) {
        gst_query_set_uri_redirection (query, src->redirection_uri);
        gst_query_set_uri_redirection_permanent (query,
            src->redirection_permanent);
      }
      ret = TRUE;
      break;
    default:
      ret = FALSE;
      break;
  }

  if (!ret)
    ret = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_SCHEDULING:
      gst_query_parse_scheduling (query, &flags, &minsize, &maxsize, &align);
      flags |= GST_SCHEDULING_FLAG_BANDWIDTH_LIMITED;
      gst_query_set_scheduling (query, flags, minsize, maxsize, align);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_soup_http_src_set_location (GstSoupHTTPSrc * src, const gchar * uri,
    GError ** error)
{
  const char *alt_schemes[] = { "icy://", "icyx://" };
  guint i;

  if (src->location) {
    g_free (src->location);
    src->location = NULL;
  }

  if (uri == NULL)
    return FALSE;

  for (i = 0; i < G_N_ELEMENTS (alt_schemes); i++) {
    if (g_str_has_prefix (uri, alt_schemes[i])) {
      src->location =
          g_strdup_printf ("http://%s", uri + strlen (alt_schemes[i]));
      return TRUE;
    }
  }

  if (src->redirection_uri) {
    g_free (src->redirection_uri);
    src->redirection_uri = NULL;
  }

  src->location = g_strdup (uri);

  return TRUE;
}

static gboolean
gst_soup_http_src_set_proxy (GstSoupHTTPSrc * src, const gchar * uri)
{
  if (src->proxy) {
    soup_uri_free (src->proxy);
    src->proxy = NULL;
  }

  if (uri == NULL || *uri == '\0')
    return TRUE;

  if (g_str_has_prefix (uri, "http://")) {
    src->proxy = soup_uri_new (uri);
  } else {
    gchar *new_uri = g_strconcat ("http://", uri, NULL);

    src->proxy = soup_uri_new (new_uri);
    g_free (new_uri);
  }

  return (src->proxy != NULL);
}

static guint
gst_soup_http_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_soup_http_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "http", "https", "icy", "icyx", NULL };

  return protocols;
}

static gchar *
gst_soup_http_src_uri_get_uri (GstURIHandler * handler)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (handler);

  /* FIXME: make thread-safe */
  return g_strdup (src->location);
}

static gboolean
gst_soup_http_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (handler);

  return gst_soup_http_src_set_location (src, uri, error);
}

static void
gst_soup_http_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_soup_http_src_uri_get_type;
  iface->get_protocols = gst_soup_http_src_uri_get_protocols;
  iface->get_uri = gst_soup_http_src_uri_get_uri;
  iface->set_uri = gst_soup_http_src_uri_set_uri;
}
