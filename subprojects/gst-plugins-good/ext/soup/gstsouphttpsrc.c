/* GStreamer
 * Copyright (C) 2007-2008 Wouter Cloetens <wouter@mind.be>
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
 * Library General Public License for more
 */

/**
 * SECTION:element-souphttpsrc
 * @title: souphttpsrc
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
 * need to use the #GstICYDemux element as follow-up element to extract the Icecast
 * metadata and to determine the underlying media type.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v souphttpsrc location=https://some.server.org/index.html
 *     ! filesink location=/home/joe/server.html
 * ]| The above pipeline reads a web page from a server using the HTTPS protocol
 * and writes it to a local file.
 * |[
 * gst-launch-1.0 -v souphttpsrc user-agent="FooPlayer 0.99 beta"
 *     automatic-redirect=false proxy=http://proxy.intranet.local:8080
 *     location=http://music.foobar.com/demo.mp3 ! mpgaudioparse
 *     ! mpg123audiodec ! audioconvert ! audioresample ! autoaudiosink
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>             /* atoi() */
#endif
#include <gst/gstelement.h>
#include <glib/gi18n-lib.h>
#include "gstsoupelements.h"
#include "gstsouphttpsrc.h"
#include "gstsouputils.h"

#include <gst/tag/tag.h>

#include <gst/glib-compat-private.h>

/* this is a simple wrapper class around SoupSession; it exists in order to
 * have a refcountable owner for the actual SoupSession + the thread it runs
 * in and its main loop (we cannot inverse the ownership hierarchy, because
 * the thread + loop are actually longer lived than the session)
 *
 * it is entirely private to this implementation
 */

#define GST_TYPE_SOUP_SESSION (gst_soup_session_get_type())
#define GST_SOUP_SESSION(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SOUP_SESSION, GstSoupSession))
#define gst_soup_session_parent_class session_parent_class

GType gst_soup_session_get_type (void);

typedef struct _GstSoupSessionClass GstSoupSessionClass;

struct _GstSoupSession
{
  GObject parent_instance;

  SoupSession *session;
  GThread *thread;
  GMainLoop *loop;
};

struct _GstSoupSessionClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (GstSoupSession, gst_soup_session, G_TYPE_OBJECT);

static void
gst_soup_session_init (GstSoupSession * sess)
{
}

static gboolean
_soup_session_finalize_cb (gpointer user_data)
{
  GstSoupSession *sess = user_data;

  g_main_loop_quit (sess->loop);

  return FALSE;
}

static void
gst_soup_session_finalize (GObject * obj)
{
  GstSoupSession *sess = GST_SOUP_SESSION (obj);
  GSource *src;

  /* handle disposing of failure cases */
  if (!sess->loop) {
    goto cleanup;
  }

  src = g_idle_source_new ();

  g_source_set_callback (src, _soup_session_finalize_cb, sess, NULL);
  g_source_attach (src, g_main_loop_get_context (sess->loop));
  g_source_unref (src);

  /* finish off thread and the loop; ensure it's not from the thread */
  g_assert (!g_main_context_is_owner (g_main_loop_get_context (sess->loop)));
  g_thread_join (sess->thread);
  g_main_loop_unref (sess->loop);
cleanup:
  G_OBJECT_CLASS (session_parent_class)->finalize (obj);
}

static void
gst_soup_session_class_init (GstSoupSessionClass * klass)
{
  GObjectClass *gclass = G_OBJECT_CLASS (klass);

  gclass->finalize = gst_soup_session_finalize;
}

GST_DEBUG_CATEGORY_STATIC (souphttpsrc_debug);
#define GST_CAT_DEFAULT souphttpsrc_debug

#define GST_SOUP_SESSION_CONTEXT "gst.soup.session"

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

enum
{
  SIGNAL_ACCEPT_CERTIFICATE,
  LAST_SIGNAL,
};

static guint gst_soup_http_src_signals[LAST_SIGNAL] = { 0 };

#define DEFAULT_USER_AGENT           "GStreamer souphttpsrc {VERSION} "
#define DEFAULT_IRADIO_MODE          TRUE
#define DEFAULT_SOUP_LOG_LEVEL       SOUP_LOGGER_LOG_HEADERS
#define DEFAULT_COMPRESS             FALSE
#define DEFAULT_KEEP_ALIVE           TRUE
#define DEFAULT_SSL_STRICT           TRUE
#define DEFAULT_SSL_CA_FILE          NULL
#define DEFAULT_SSL_USE_SYSTEM_CA_FILE TRUE
#define DEFAULT_TLS_DATABASE         NULL
#define DEFAULT_TLS_INTERACTION      NULL
#define DEFAULT_TIMEOUT              15
#define DEFAULT_RETRIES              3
#define DEFAULT_SOUP_METHOD          NULL

#define GROW_BLOCKSIZE_LIMIT 1
#define GROW_BLOCKSIZE_COUNT 1
#define GROW_BLOCKSIZE_FACTOR 2
#define REDUCE_BLOCKSIZE_LIMIT 0.20
#define REDUCE_BLOCKSIZE_COUNT 2
#define REDUCE_BLOCKSIZE_FACTOR 0.5
#define GROW_TIME_LIMIT (1 * GST_SECOND)

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
static void gst_soup_http_src_set_context (GstElement * element,
    GstContext * context);
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
static gboolean gst_soup_http_src_add_range_header (GstSoupHTTPSrc * src,
    guint64 offset, guint64 stop_offset);
static gboolean gst_soup_http_src_session_open (GstSoupHTTPSrc * src);
static void gst_soup_http_src_session_close (GstSoupHTTPSrc * src);
static GstFlowReturn gst_soup_http_src_parse_status (SoupMessage * msg,
    GstSoupHTTPSrc * src);
static GstFlowReturn gst_soup_http_src_got_headers (GstSoupHTTPSrc * src,
    SoupMessage * msg);
static void gst_soup_http_src_authenticate_cb_2 (SoupSession *,
    SoupMessage * msg, SoupAuth * auth, gboolean retrying, gpointer);
static gboolean gst_soup_http_src_authenticate_cb (SoupMessage * msg,
    SoupAuth * auth, gboolean retrying, gpointer);
static gboolean gst_soup_http_src_accept_certificate_cb (SoupMessage * msg,
    GTlsCertificate * tls_certificate, GTlsCertificateFlags tls_errors,
    gpointer user_data);

#define gst_soup_http_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstSoupHTTPSrc, gst_soup_http_src, GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_soup_http_src_uri_handler_init));

static gboolean souphttpsrc_element_init (GstPlugin * plugin);
GST_ELEMENT_REGISTER_DEFINE_CUSTOM (souphttpsrc, souphttpsrc_element_init);

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
          _soup_logger_log_level_get_type (),
          DEFAULT_SOUP_LOG_LEVEL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
   * Deprecated: Use #GstSoupHTTPSrc::tls-database property instead. This
   * property is no-op when libsoup3 is being used at runtime.
   *
   * Since: 1.4
   */
  g_object_class_install_property (gobject_class, PROP_SSL_CA_FILE,
      g_param_spec_string ("ssl-ca-file", "SSL CA File",
          "Location of a SSL anchor CA file to use", DEFAULT_SSL_CA_FILE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
          | GST_PARAM_DOC_SHOW_DEFAULT));

  /**
   * GstSoupHTTPSrc::ssl-use-system-ca-file:
   *
   * If set to %TRUE, souphttpsrc will use the system's CA file for
   * checking certificates, unless #GstSoupHTTPSrc::ssl-ca-file or
   * #GstSoupHTTPSrc::tls-database are non-%NULL.
   *
   * Deprecated: This property is no-op when libsoup3 is being used at runtime.
   *
   * Since: 1.4
   */
  g_object_class_install_property (gobject_class, PROP_SSL_USE_SYSTEM_CA_FILE,
      g_param_spec_boolean ("ssl-use-system-ca-file", "Use System CA File",
          "Use system CA file", DEFAULT_SSL_USE_SYSTEM_CA_FILE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
          | GST_PARAM_DOC_SHOW_DEFAULT));

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

  /**
   * GstSoupHTTPSrc::accept-certificate:
   * @souphttpsrc: a #GstSoupHTTPSrc
   * @peer_cert: the peer's #GTlsCertificate
   * @errors: the problems with @peer_cert
   *
   * This will directly map to #SoupMessage 's "accept-certificate" after
   * an unacceptable TLS certificate has been received, and only for libsoup 3.x
   * or above. If "ssl-strict" was set to %FALSE, this signal will not be
   * emitted.
   *
   * Returns: %TRUE to accept the TLS certificate and stop other handlers from
   * being invoked, or %FALSE to propagate the event further.
   *
   * Since: 1.24
   */
  gst_soup_http_src_signals[SIGNAL_ACCEPT_CERTIFICATE] =
      g_signal_new ("accept-certificate", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, g_signal_accumulator_true_handled, NULL, NULL,
      G_TYPE_BOOLEAN, 2, G_TYPE_TLS_CERTIFICATE, G_TYPE_TLS_CERTIFICATE_FLAGS);

  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);

  gst_element_class_set_static_metadata (gstelement_class, "HTTP client source",
      "Source/Network",
      "Receive data as a client over the network via HTTP using SOUP",
      "Wouter Cloetens <wouter@mind.be>");
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_soup_http_src_change_state);
  gstelement_class->set_context =
      GST_DEBUG_FUNCPTR (gst_soup_http_src_set_context);

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
}

static void
gst_soup_http_src_reset (GstSoupHTTPSrc * src)
{
  src->retry_count = 0;
  src->have_size = FALSE;
  src->got_headers = FALSE;
  src->headers_ret = GST_FLOW_OK;
  src->seekable = FALSE;
  src->read_position = 0;
  src->request_position = 0;
  src->stop_position = -1;
  src->content_size = 0;
  src->have_body = FALSE;

  src->reduce_blocksize_count = 0;
  src->increase_blocksize_count = 0;
  src->last_socket_read_time = 0;

  g_cancellable_reset (src->cancellable);

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

  g_mutex_init (&src->session_mutex);
  g_cond_init (&src->session_cond);
  src->cancellable = g_cancellable_new ();
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
  src->session = NULL;
  src->external_session = NULL;
  src->msg = NULL;
  src->timeout = DEFAULT_TIMEOUT;
  src->log_level = DEFAULT_SOUP_LOG_LEVEL;
  src->compress = DEFAULT_COMPRESS;
  src->keep_alive = DEFAULT_KEEP_ALIVE;
  src->ssl_strict = DEFAULT_SSL_STRICT;
  src->ssl_use_system_ca_file = DEFAULT_SSL_USE_SYSTEM_CA_FILE;
  src->tls_database = DEFAULT_TLS_DATABASE;
  src->tls_interaction = DEFAULT_TLS_INTERACTION;
  src->max_retries = DEFAULT_RETRIES;
  src->method = DEFAULT_SOUP_METHOD;
  src->minimum_blocksize = gst_base_src_get_blocksize (GST_BASE_SRC_CAST (src));
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

  g_clear_object (&src->external_session);

  G_OBJECT_CLASS (parent_class)->dispose (gobject);
}

static void
gst_soup_http_src_finalize (GObject * gobject)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (gobject);

  GST_DEBUG_OBJECT (src, "finalize");

  g_mutex_clear (&src->session_mutex);
  g_cond_clear (&src->session_cond);
  g_object_unref (src->cancellable);
  g_free (src->location);
  g_free (src->redirection_uri);
  g_free (src->user_agent);
  if (src->proxy != NULL) {
    gst_soup_uri_free (src->proxy);
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
    case PROP_SSL_CA_FILE:
      if (gst_soup_loader_get_api_version () == 2) {
        g_free (src->ssl_ca_file);
        src->ssl_ca_file = g_value_dup_string (value);
      }
      break;
    case PROP_SSL_USE_SYSTEM_CA_FILE:
      if (gst_soup_loader_get_api_version () == 2) {
        src->ssl_use_system_ca_file = g_value_get_boolean (value);
      }
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
        char *proxy = gst_soup_uri_to_string (src->proxy);
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
    case PROP_SSL_CA_FILE:
      if (gst_soup_loader_get_api_version () == 2)
        g_value_set_string (value, src->ssl_ca_file);
      break;
    case PROP_SSL_USE_SYSTEM_CA_FILE:
      if (gst_soup_loader_get_api_version () == 2)
        g_value_set_boolean (value, src->ssl_use_system_ca_file);
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

static gboolean
gst_soup_http_src_add_range_header (GstSoupHTTPSrc * src, guint64 offset,
    guint64 stop_offset)
{
  gchar buf[64];
  gint rc;
  SoupMessageHeaders *request_headers =
      _soup_message_get_request_headers (src->msg);

  _soup_message_headers_remove (request_headers, "Range");
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
    _soup_message_headers_append (request_headers, "Range", buf);
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
  SoupMessageHeaders *request_headers =
      _soup_message_get_request_headers (src->msg);

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
  _soup_message_headers_append (request_headers, field_name, field_content);

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

static gpointer
thread_func (gpointer user_data)
{
  GstSoupHTTPSrc *src = user_data;
  GstSoupSession *session = src->session;
  GMainContext *ctx;

  GST_DEBUG_OBJECT (src, "thread start");

  ctx = g_main_loop_get_context (session->loop);

  g_main_context_push_thread_default (ctx);

  /* We explicitly set User-Agent to NULL here and overwrite it per message
   * to be able to have the same session with different User-Agents per
   * source */
  session->session =
      _soup_session_new_with_options ("user-agent", NULL,
      "timeout", src->timeout, "tls-interaction", src->tls_interaction,
      /* Unset the limit the number of maximum allowed connections */
      "max-conns", src->session_is_shared ? G_MAXINT : 10,
      "max-conns-per-host", src->session_is_shared ? G_MAXINT : 2, NULL);
  g_assert (session->session);

  if (gst_soup_loader_get_api_version () == 3) {
    if (src->proxy != NULL) {
      GProxyResolver *proxy_resolver;
      char *proxy_string = gst_soup_uri_to_string (src->proxy);
      proxy_resolver = g_simple_proxy_resolver_new (proxy_string, NULL);
      g_free (proxy_string);
      g_object_set (src->session->session, "proxy-resolver", proxy_resolver,
          NULL);
      g_object_unref (proxy_resolver);
    }
#if !defined(STATIC_SOUP) || STATIC_SOUP == 2
  } else {
    g_object_set (session->session, "ssl-strict", src->ssl_strict, NULL);
    if (src->proxy != NULL) {
      /* Need #if because there's no proxy->soup_uri when STATIC_SOUP == 3 */
      g_object_set (session->session, "proxy-uri", src->proxy->soup_uri, NULL);
    }
#endif
  }

  gst_soup_util_log_setup (session->session, src->log_level,
      G_OBJECT (session));
  if (gst_soup_loader_get_api_version () < 3) {
    _soup_session_add_feature_by_type (session->session,
        _soup_content_decoder_get_type ());
  }
  _soup_session_add_feature_by_type (session->session,
      _soup_cookie_jar_get_type ());

  /* soup2: connect the authenticate handler for the src that spawned the
   * session (i.e. the first owner); other users of this session will connect
   * their own after fetching the external session; the callback will handle
   * this correctly (it checks if the message belongs to the current src
   * and exits early if it does not)
   */
  if (gst_soup_loader_get_api_version () < 3) {
    g_signal_connect (session->session, "authenticate",
        G_CALLBACK (gst_soup_http_src_authenticate_cb_2), src);
  }

  if (!src->session_is_shared) {
    if (src->tls_database)
      g_object_set (src->session->session, "tls-database", src->tls_database,
          NULL);
    else if (gst_soup_loader_get_api_version () == 2) {
      if (src->ssl_ca_file)
        g_object_set (src->session->session, "ssl-ca-file", src->ssl_ca_file,
            NULL);
      else
        g_object_set (src->session->session, "ssl-use-system-ca-file",
            src->ssl_use_system_ca_file, NULL);
    }
  }

  /* Once the main loop is running, the source element that created this
   * session might disappear if the session is shared with other source
   * elements.
   */
  src = NULL;

  g_main_loop_run (session->loop);

  /* Abort any pending operations on the session ... */
  _soup_session_abort (session->session);
  g_clear_object (&session->session);

  /* ... and iterate the main context until nothing is pending anymore */
  while (g_main_context_iteration (ctx, FALSE));

  g_main_context_pop_thread_default (ctx);

  GST_DEBUG_OBJECT (session, "thread stop");

  return NULL;
}

static gboolean
_session_ready_cb (gpointer user_data)
{
  GstSoupHTTPSrc *src = user_data;

  GST_DEBUG_OBJECT (src, "thread ready");

  g_mutex_lock (&src->session_mutex);
  g_cond_signal (&src->session_cond);
  g_mutex_unlock (&src->session_mutex);

  return FALSE;
}

/* called with session_mutex taken */
static gboolean
gst_soup_http_src_session_open (GstSoupHTTPSrc * src)
{
  GstQuery *query;
  gboolean can_share;

  if (src->session) {
    GST_DEBUG_OBJECT (src, "Session is already open");
    return TRUE;
  }

  if (!src->location) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (_("No URL set.")),
        ("Missing location property"));
    return FALSE;
  }

  can_share = (src->timeout == DEFAULT_TIMEOUT)
      && (src->cookies == NULL)
      && (src->ssl_strict == DEFAULT_SSL_STRICT)
      && (src->tls_interaction == NULL) && (src->proxy == NULL)
      && (src->tls_database == DEFAULT_TLS_DATABASE);

  if (gst_soup_loader_get_api_version () == 2)
    can_share = can_share && (src->ssl_ca_file == DEFAULT_SSL_CA_FILE) &&
        (src->ssl_use_system_ca_file == DEFAULT_SSL_USE_SYSTEM_CA_FILE);

  query = gst_query_new_context (GST_SOUP_SESSION_CONTEXT);
  if (gst_pad_peer_query (GST_BASE_SRC_PAD (src), query)) {
    GstContext *context;

    gst_query_parse_context (query, &context);
    gst_element_set_context (GST_ELEMENT_CAST (src), context);
  } else {
    GstMessage *message;

    message =
        gst_message_new_need_context (GST_OBJECT_CAST (src),
        GST_SOUP_SESSION_CONTEXT);
    gst_element_post_message (GST_ELEMENT_CAST (src), message);
  }
  gst_query_unref (query);

  GST_OBJECT_LOCK (src);

  src->session_is_shared = can_share;

  if (src->external_session && can_share) {
    GST_DEBUG_OBJECT (src, "Using external session %p", src->external_session);
    src->session = g_object_ref (src->external_session);
    /* for soup2, connect another authenticate handler; see thread_func */
    if (gst_soup_loader_get_api_version () < 3) {
      g_signal_connect (src->session->session, "authenticate",
          G_CALLBACK (gst_soup_http_src_authenticate_cb_2), src);
    }
  } else {
    GMainContext *ctx;
    GSource *source;

    GST_DEBUG_OBJECT (src, "Creating session (can share %d)", can_share);

    src->session =
        GST_SOUP_SESSION (g_object_new (GST_TYPE_SOUP_SESSION, NULL));

    GST_DEBUG_OBJECT (src, "Created session %p", src->session);

    ctx = g_main_context_new ();

    src->session->loop = g_main_loop_new (ctx, FALSE);
    /* now owned by the loop */
    g_main_context_unref (ctx);

    src->session->thread = g_thread_try_new ("souphttpsrc-thread",
        thread_func, src, NULL);

    if (!src->session->thread) {
      goto err;
    }

    source = g_idle_source_new ();
    g_source_set_callback (source, _session_ready_cb, src, NULL);
    g_source_attach (source, ctx);
    g_source_unref (source);

    GST_DEBUG_OBJECT (src, "Waiting for thread to start...");
    while (!g_main_loop_is_running (src->session->loop))
      g_cond_wait (&src->session_cond, &src->session_mutex);
    GST_DEBUG_OBJECT (src, "Soup thread started");
  }

  GST_OBJECT_UNLOCK (src);

  if (src->session_is_shared) {
    GstContext *context;
    GstMessage *message;
    GstStructure *s;

    GST_DEBUG_OBJECT (src->session, "Sharing session %p", src->session);

    context = gst_context_new (GST_SOUP_SESSION_CONTEXT, TRUE);
    s = gst_context_writable_structure (context);
    gst_structure_set (s, "session", GST_TYPE_SOUP_SESSION, src->session, NULL);

    gst_element_set_context (GST_ELEMENT_CAST (src), context);
    message = gst_message_new_have_context (GST_OBJECT_CAST (src), context);
    gst_element_post_message (GST_ELEMENT_CAST (src), message);
  }

  return TRUE;

err:
  g_clear_object (&src->session);
  GST_ELEMENT_ERROR (src, LIBRARY, INIT, (NULL), ("Failed to create session"));
  GST_OBJECT_UNLOCK (src);

  return FALSE;
}

static gboolean
_session_close_cb (gpointer user_data)
{
  GstSoupHTTPSrc *src = user_data;

  if (src->msg) {
    gst_soup_session_cancel_message (src->session->session, src->msg,
        src->cancellable);
    g_clear_object (&src->msg);
  }

  /* there may be multiple of this callback attached to the session,
   * each with different data pointer; disconnect the one we are closing
   * the session for, leave the others alone
   */
  g_signal_handlers_disconnect_by_func (src->session->session,
      G_CALLBACK (gst_soup_http_src_authenticate_cb_2), src);

  g_mutex_lock (&src->session_mutex);
  g_clear_object (&src->session);
  g_cond_signal (&src->session_cond);
  g_mutex_unlock (&src->session_mutex);

  return FALSE;
}

static void
gst_soup_http_src_session_close (GstSoupHTTPSrc * src)
{
  GSource *source;
  GstSoupSession *sess;

  GST_DEBUG_OBJECT (src, "Closing session");

  if (!src->session) {
    return;
  }

  /* ensure _session_close_cb does not deadlock us */
  sess = g_object_ref (src->session);

  source = g_idle_source_new ();

  g_mutex_lock (&src->session_mutex);

  g_source_set_callback (source, _session_close_cb, src, NULL);
  g_source_attach (source, g_main_loop_get_context (src->session->loop));
  g_source_unref (source);

  while (src->session)
    g_cond_wait (&src->session_cond, &src->session_mutex);

  g_mutex_unlock (&src->session_mutex);

  /* finally dispose of our reference from the gst thread */
  g_object_unref (sess);
}

static void
gst_soup_http_src_authenticate_cb_2 (SoupSession * session, SoupMessage * msg,
    SoupAuth * auth, gboolean retrying, gpointer data)
{
  gst_soup_http_src_authenticate_cb (msg, auth, retrying, data);
}

static gboolean
gst_soup_http_src_authenticate_cb (SoupMessage * msg, SoupAuth * auth,
    gboolean retrying, gpointer data)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (data);
  SoupStatus status_code;

  /* Might be from another user of the shared session */
  if (!GST_IS_SOUP_HTTP_SRC (src) || msg != src->msg)
    return FALSE;

  status_code = _soup_message_get_status (msg);

  if (!retrying) {
    /* First time authentication only, if we fail and are called again with
     * retry true fall through */
    if (status_code == SOUP_STATUS_UNAUTHORIZED) {
      if (src->user_id && src->user_pw) {
        _soup_auth_authenticate (auth, src->user_id, src->user_pw);
      }
    } else if (status_code == SOUP_STATUS_PROXY_AUTHENTICATION_REQUIRED) {
      if (src->proxy_id && src->proxy_pw) {
        _soup_auth_authenticate (auth, src->proxy_id, src->proxy_pw);
      }
    }
  }

  return FALSE;
}

static gboolean
gst_soup_http_src_accept_certificate_cb (SoupMessage * msg,
    GTlsCertificate * tls_certificate, GTlsCertificateFlags tls_errors,
    gpointer user_data)
{
  GstSoupHTTPSrc *src = user_data;
  gboolean accept = FALSE;

  /* Might be from another user of the shared session */
  if (!GST_IS_SOUP_HTTP_SRC (src) || msg != src->msg)
    return FALSE;

  /* Accept invalid certificates */
  if (!src->ssl_strict)
    return TRUE;

  g_signal_emit (src, gst_soup_http_src_signals[SIGNAL_ACCEPT_CERTIFICATE], 0,
      tls_certificate, tls_errors, &accept);

  return accept;
}

static void
insert_http_header (const gchar * name, const gchar * value, gpointer user_data)
{
  GstStructure *headers = user_data;
  const GValue *gv;

  if (!g_utf8_validate (name, -1, NULL) || !g_utf8_validate (value, -1, NULL))
    return;

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

static GstFlowReturn
gst_soup_http_src_got_headers (GstSoupHTTPSrc * src, SoupMessage * msg)
{
  const char *value;
  GstTagList *tag_list;
  GstBaseSrc *basesrc;
  guint64 newsize;
  GHashTable *params = NULL;
  GstEvent *http_headers_event;
  GstStructure *http_headers, *headers;
  const gchar *accept_ranges;
  SoupMessageHeaders *request_headers = _soup_message_get_request_headers (msg);
  SoupMessageHeaders *response_headers =
      _soup_message_get_response_headers (msg);
  SoupStatus status_code = _soup_message_get_status (msg);

  GST_INFO_OBJECT (src, "got headers");

  if (status_code == SOUP_STATUS_PROXY_AUTHENTICATION_REQUIRED &&
      src->proxy_id && src->proxy_pw) {
    /* wait for authenticate callback */
    return GST_FLOW_OK;
  }

  http_headers = gst_structure_new_empty ("http-headers");
  gst_structure_set (http_headers, "uri", G_TYPE_STRING, src->location,
      "http-status-code", G_TYPE_UINT, status_code, NULL);
  if (src->redirection_uri)
    gst_structure_set (http_headers, "redirection-uri", G_TYPE_STRING,
        src->redirection_uri, NULL);
  headers = gst_structure_new_empty ("request-headers");
  _soup_message_headers_foreach (request_headers, insert_http_header, headers);
  gst_structure_set (http_headers, "request-headers", GST_TYPE_STRUCTURE,
      headers, NULL);
  gst_structure_free (headers);
  headers = gst_structure_new_empty ("response-headers");
  _soup_message_headers_foreach (response_headers, insert_http_header, headers);
  gst_structure_set (http_headers, "response-headers", GST_TYPE_STRUCTURE,
      headers, NULL);
  gst_structure_free (headers);

  gst_element_post_message (GST_ELEMENT_CAST (src),
      gst_message_new_element (GST_OBJECT_CAST (src),
          gst_structure_copy (http_headers)));

  if (status_code == SOUP_STATUS_UNAUTHORIZED) {
    /* force an error */
    gst_structure_free (http_headers);
    return gst_soup_http_src_parse_status (msg, src);
  }

  src->got_headers = TRUE;

  http_headers_event =
      gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM_STICKY, http_headers);
  gst_event_replace (&src->http_headers_event, http_headers_event);
  gst_event_unref (http_headers_event);

  /* Parse Content-Length. */
  if (SOUP_STATUS_IS_SUCCESSFUL (status_code) &&
      (_soup_message_headers_get_encoding (response_headers) ==
          SOUP_ENCODING_CONTENT_LENGTH)) {
    newsize = src->request_position +
        _soup_message_headers_get_content_length (response_headers);
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
          _soup_message_headers_get_one (response_headers, "Accept-Ranges"))) {
    if (g_ascii_strcasecmp (accept_ranges, "none") == 0)
      src->seekable = FALSE;
  }

  /* Icecast stuff */
  tag_list = gst_tag_list_new_empty ();

  if ((value =
          _soup_message_headers_get_one (response_headers,
              "icy-metaint")) != NULL) {
    gint icy_metaint;

    if (g_utf8_validate (value, -1, NULL)) {
      icy_metaint = atoi (value);

      GST_DEBUG_OBJECT (src, "icy-metaint: %s (parsed: %d)", value,
          icy_metaint);
      if (icy_metaint > 0) {
        if (src->src_caps)
          gst_caps_unref (src->src_caps);

        src->src_caps = gst_caps_new_simple ("application/x-icy",
            "metadata-interval", G_TYPE_INT, icy_metaint, NULL);

        gst_base_src_set_caps (GST_BASE_SRC (src), src->src_caps);
      }
    }
  }
  if ((value =
          _soup_message_headers_get_content_type (response_headers,
              &params)) != NULL) {
    if (!g_utf8_validate (value, -1, NULL)) {
      GST_WARNING_OBJECT (src, "Content-Type is invalid UTF-8");
    } else if (g_ascii_strcasecmp (value, "audio/L16") == 0) {
      gint channels = 2;
      gint rate = 44100;
      char *param;

      GST_DEBUG_OBJECT (src, "Content-Type: %s", value);

      if (src->src_caps) {
        gst_caps_unref (src->src_caps);
        src->src_caps = NULL;
      }

      param = g_hash_table_lookup (params, "channels");
      if (param != NULL) {
        guint64 val = g_ascii_strtoull (param, NULL, 10);
        if (val < 64)
          channels = val;
        else
          channels = 0;
      }

      param = g_hash_table_lookup (params, "rate");
      if (param != NULL) {
        guint64 val = g_ascii_strtoull (param, NULL, 10);
        if (val < G_MAXINT)
          rate = val;
        else
          rate = 0;
      }

      if (rate > 0 && channels > 0) {
        src->src_caps = gst_caps_new_simple ("audio/x-unaligned-raw",
            "format", G_TYPE_STRING, "S16BE",
            "layout", G_TYPE_STRING, "interleaved",
            "channels", G_TYPE_INT, channels, "rate", G_TYPE_INT, rate, NULL);

        gst_base_src_set_caps (GST_BASE_SRC (src), src->src_caps);
      }
    } else {
      GST_DEBUG_OBJECT (src, "Content-Type: %s", value);

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
          _soup_message_headers_get_one (response_headers,
              "icy-name")) != NULL) {
    if (g_utf8_validate (value, -1, NULL)) {
      g_free (src->iradio_name);
      src->iradio_name = gst_soup_http_src_unicodify (value);
      if (src->iradio_name) {
        gst_tag_list_add (tag_list, GST_TAG_MERGE_REPLACE, GST_TAG_ORGANIZATION,
            src->iradio_name, NULL);
      }
    }
  }
  if ((value =
          _soup_message_headers_get_one (response_headers,
              "icy-genre")) != NULL) {
    if (g_utf8_validate (value, -1, NULL)) {
      g_free (src->iradio_genre);
      src->iradio_genre = gst_soup_http_src_unicodify (value);
      if (src->iradio_genre) {
        gst_tag_list_add (tag_list, GST_TAG_MERGE_REPLACE, GST_TAG_GENRE,
            src->iradio_genre, NULL);
      }
    }
  }
  if ((value = _soup_message_headers_get_one (response_headers, "icy-url"))
      != NULL) {
    if (g_utf8_validate (value, -1, NULL)) {
      g_free (src->iradio_url);
      src->iradio_url = gst_soup_http_src_unicodify (value);
      if (src->iradio_url) {
        gst_tag_list_add (tag_list, GST_TAG_MERGE_REPLACE, GST_TAG_LOCATION,
            src->iradio_url, NULL);
      }
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
  return gst_soup_http_src_parse_status (msg, src);
}

static GstBuffer *
gst_soup_http_src_alloc_buffer (GstSoupHTTPSrc * src)
{
  GstBaseSrc *basesrc = GST_BASE_SRC_CAST (src);
  GstFlowReturn rc;
  GstBuffer *gstbuf;

  rc = GST_BASE_SRC_CLASS (parent_class)->alloc (basesrc, -1,
      basesrc->blocksize, &gstbuf);
  if (G_UNLIKELY (rc != GST_FLOW_OK)) {
    return NULL;
  }

  return gstbuf;
}

#define SOUP_HTTP_SRC_ERROR(src,soup_msg,cat,code,error_message)     \
  do { \
    GST_ELEMENT_ERROR_WITH_DETAILS ((src), cat, code, ("%s", error_message), \
        ("%s (%d), URL: %s, Redirect to: %s", _soup_message_get_reason_phrase (soup_msg), \
            _soup_message_get_status (soup_msg), (src)->location, GST_STR_NULL ((src)->redirection_uri)), \
            ("http-status-code", G_TYPE_UINT, _soup_message_get_status (soup_msg), \
             "http-redirect-uri", G_TYPE_STRING, GST_STR_NULL ((src)->redirection_uri), NULL)); \
  } while(0)

static GstFlowReturn
gst_soup_http_src_parse_status (SoupMessage * msg, GstSoupHTTPSrc * src)
{
  SoupStatus status_code = _soup_message_get_status (msg);
  if (_soup_message_get_method (msg) == SOUP_METHOD_HEAD) {
    if (!SOUP_STATUS_IS_SUCCESSFUL (status_code))
      GST_DEBUG_OBJECT (src, "Ignoring error %d during HEAD request",
          status_code);
    return GST_FLOW_OK;
  }

  /* SOUP_STATUS_IS_TRANSPORT_ERROR was replaced with GError in libsoup-3.0 */
#if !defined(STATIC_SOUP) || STATIC_SOUP == 2
  if (SOUP_STATUS_IS_TRANSPORT_ERROR (status_code)) {
    switch (status_code) {
      case SOUP_STATUS_CANT_RESOLVE:
      case SOUP_STATUS_CANT_RESOLVE_PROXY:
        SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, NOT_FOUND,
            _("Could not resolve server name."));
        return GST_FLOW_ERROR;
      case SOUP_STATUS_CANT_CONNECT:
      case SOUP_STATUS_CANT_CONNECT_PROXY:
        SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, OPEN_READ,
            _("Could not establish connection to server."));
        return GST_FLOW_ERROR;
      case SOUP_STATUS_SSL_FAILED:
        SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, OPEN_READ,
            _("Secure connection setup failed."));
        return GST_FLOW_ERROR;
      case SOUP_STATUS_IO_ERROR:
        if (src->max_retries == -1 || src->retry_count < src->max_retries)
          return GST_FLOW_CUSTOM_ERROR;
        SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, READ,
            _("A network error occurred, or the server closed the connection "
                "unexpectedly."));
        return GST_FLOW_ERROR;
      case SOUP_STATUS_MALFORMED:
        SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, READ,
            _("Server sent bad data."));
        return GST_FLOW_ERROR;
      case SOUP_STATUS_CANCELLED:
        /* No error message when interrupted by program. */
        break;
      default:
        break;
    }
    return GST_FLOW_OK;
  }
#endif

  if (SOUP_STATUS_IS_CLIENT_ERROR (status_code) ||
      SOUP_STATUS_IS_REDIRECTION (status_code) ||
      SOUP_STATUS_IS_SERVER_ERROR (status_code)) {
    const gchar *reason_phrase;

    reason_phrase = _soup_message_get_reason_phrase (msg);
    if (reason_phrase && !g_utf8_validate (reason_phrase, -1, NULL)) {
      GST_ERROR_OBJECT (src, "Invalid UTF-8 in reason");
      reason_phrase = "(invalid)";
    }

    /* Report HTTP error. */

    /* when content_size is unknown and we have just finished receiving
     * a body message, requests that go beyond the content limits will result
     * in an error. Here we convert those to EOS */
    if (status_code == SOUP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE &&
        src->have_body && (!src->have_size ||
            (src->request_position >= src->content_size))) {
      GST_DEBUG_OBJECT (src, "Requested range out of limits and received full "
          "body, returning EOS");
      return GST_FLOW_EOS;
    }

    /* FIXME: reason_phrase is not translated and not suitable for user
     * error dialog according to libsoup documentation.
     */
    if (status_code == SOUP_STATUS_NOT_FOUND) {
      SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, NOT_FOUND, (reason_phrase));
    } else if (status_code == SOUP_STATUS_UNAUTHORIZED
        || status_code == SOUP_STATUS_PAYMENT_REQUIRED
        || status_code == SOUP_STATUS_FORBIDDEN
        || status_code == SOUP_STATUS_PROXY_AUTHENTICATION_REQUIRED) {
      SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, NOT_AUTHORIZED, (reason_phrase));
    } else {
      SOUP_HTTP_SRC_ERROR (src, msg, RESOURCE, OPEN_READ, (reason_phrase));
    }
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static void
gst_soup_http_src_restarted_cb (SoupMessage * msg, GstSoupHTTPSrc * src)
{
  SoupStatus status = _soup_message_get_status (msg);

  if (!SOUP_STATUS_IS_REDIRECTION (status))
    return;

  src->redirection_uri = gst_soup_message_uri_to_string (msg);
  src->redirection_permanent = (status == SOUP_STATUS_MOVED_PERMANENTLY);

  GST_DEBUG_OBJECT (src, "%u redirect to \"%s\" (permanent %d)",
      status, src->redirection_uri, src->redirection_permanent);
}

static gboolean
gst_soup_http_src_build_message (GstSoupHTTPSrc * src, const gchar * method)
{
  SoupMessageHeaders *request_headers;

  g_return_val_if_fail (src->msg == NULL, FALSE);

  src->msg = _soup_message_new (method, src->location);
  if (!src->msg) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("Error parsing URL."), ("URL: %s", src->location));
    return FALSE;
  }

  request_headers = _soup_message_get_request_headers (src->msg);

  /* Duplicating the defaults of libsoup here. We don't want to set a
   * User-Agent in the session as each source might have its own User-Agent
   * set */
  GString *user_agent = g_string_new (src->user_agent);
  g_string_replace (user_agent, "{VERSION}", PACKAGE_VERSION, 0);
  if (user_agent->len == 0 || g_str_has_suffix (user_agent->str, " ")) {
    g_string_append_printf (user_agent, "libsoup/%u.%u.%u",
        _soup_get_major_version (), _soup_get_minor_version (),
        _soup_get_micro_version ());
  }
  _soup_message_headers_append (request_headers, "User-Agent", user_agent->str);
  g_string_free (user_agent, TRUE);
  user_agent = NULL;

  if (!src->keep_alive) {
    _soup_message_headers_append (request_headers, "Connection", "close");
  }
  if (src->iradio_mode) {
    _soup_message_headers_append (request_headers, "icy-metadata", "1");
  }
  if (src->cookies) {
    gchar **cookie;

    for (cookie = src->cookies; *cookie != NULL; cookie++) {
      _soup_message_headers_append (request_headers, "Cookie", *cookie);
    }

    _soup_message_disable_feature (src->msg, _soup_cookie_jar_get_type ());
  }

  if (!src->compress) {
    _soup_message_headers_append (_soup_message_get_request_headers (src->msg),
        "Accept-Encoding", "identity");
  }

  if (gst_soup_loader_get_api_version () == 3) {
    g_signal_connect (src->msg, "accept-certificate",
        G_CALLBACK (gst_soup_http_src_accept_certificate_cb), src);
    g_signal_connect (src->msg, "authenticate",
        G_CALLBACK (gst_soup_http_src_authenticate_cb), src);
  }

  {
    SoupMessageFlags flags =
        src->automatic_redirect ? 0 : SOUP_MESSAGE_NO_REDIRECT;

    /* SOUP_MESSAGE_OVERWRITE_CHUNKS is gone in libsoup-3.0, and
     * soup_message_body_set_accumulate() requires SoupMessageBody, which
     * can only be fetched from SoupServerMessage, not SoupMessage */
#if !defined(STATIC_SOUP) || STATIC_SOUP == 2
    if (gst_soup_loader_get_api_version () == 2)
      flags |= SOUP_MESSAGE_OVERWRITE_CHUNKS;
#endif

    _soup_message_set_flags (src->msg, flags);
  }

  if (src->automatic_redirect) {
    g_signal_connect (src->msg, "restarted",
        G_CALLBACK (gst_soup_http_src_restarted_cb), src);
  }

  gst_soup_http_src_add_range_header (src, src->request_position,
      src->stop_position);

  gst_soup_http_src_add_extra_headers (src);

  return TRUE;
}

struct GstSoupSendSrc
{
  GstSoupHTTPSrc *src;
  GError *error;
};

static void
_session_send_cb (GObject * source, GAsyncResult * res, gpointer user_data)
{
  struct GstSoupSendSrc *msrc = user_data;
  GstSoupHTTPSrc *src = msrc->src;
  GError *error = NULL;

  g_mutex_lock (&src->session_mutex);

  src->input_stream = _soup_session_send_finish (src->session->session,
      res, &error);

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    src->headers_ret = GST_FLOW_FLUSHING;
  } else {
    src->headers_ret = gst_soup_http_src_got_headers (src, src->msg);
  }

  if (!src->input_stream) {
    GST_DEBUG_OBJECT (src, "Sending message failed: %s", error->message);
    msrc->error = error;
  }

  g_cond_broadcast (&src->session_cond);
  g_mutex_unlock (&src->session_mutex);
}

static gboolean
_session_send_idle_cb (gpointer user_data)
{
  struct GstSoupSendSrc *msrc = user_data;
  GstSoupHTTPSrc *src = msrc->src;

  _soup_session_send_async (src->session->session, src->msg, src->cancellable,
      _session_send_cb, msrc);

  return FALSE;
}

/* called with session lock taken */
static GstFlowReturn
gst_soup_http_src_send_message (GstSoupHTTPSrc * src)
{
  GstFlowReturn ret;
  GSource *source;
  struct GstSoupSendSrc msrc;

  g_return_val_if_fail (src->msg != NULL, GST_FLOW_ERROR);
  g_assert (src->input_stream == NULL);

  msrc.src = src;
  msrc.error = NULL;

  source = g_idle_source_new ();

  src->headers_ret = GST_FLOW_OK;

  g_source_set_callback (source, _session_send_idle_cb, &msrc, NULL);
  g_source_attach (source, g_main_loop_get_context (src->session->loop));
  g_source_unref (source);

  while (!src->input_stream && !msrc.error)
    g_cond_wait (&src->session_cond, &src->session_mutex);

  ret = src->headers_ret;

  if (ret != GST_FLOW_OK) {
    goto done;
  }

  if (!src->input_stream) {
    GST_DEBUG_OBJECT (src, "Didn't get an input stream: %s",
        msrc.error->message);
    ret = GST_FLOW_ERROR;
    goto done;
  }

  /* if an input stream exists, it was always successful */
  GST_DEBUG_OBJECT (src, "Successfully got a reply");

done:
  g_clear_error (&msrc.error);
  return ret;
}

/* called with session lock taken */
static GstFlowReturn
gst_soup_http_src_do_request (GstSoupHTTPSrc * src, const gchar * method)
{
  GstFlowReturn ret;
  SoupMessageHeaders *request_headers;

  if (src->max_retries != -1 && src->retry_count > src->max_retries) {
    GST_DEBUG_OBJECT (src, "Max retries reached");
    return GST_FLOW_ERROR;
  }

  src->retry_count++;
  /* EOS immediately if we have an empty segment */
  if (src->request_position == src->stop_position)
    return GST_FLOW_EOS;

  GST_LOG_OBJECT (src, "Running request for method: %s", method);

  if (src->msg)
    request_headers = _soup_message_get_request_headers (src->msg);

  /* Update the position if we are retrying */
  if (src->msg && src->request_position > 0) {
    gst_soup_http_src_add_range_header (src, src->request_position,
        src->stop_position);
  } else if (src->msg && src->request_position == 0)
    _soup_message_headers_remove (request_headers, "Range");

  /* add_range_header() has the side effect of setting read_position to
   * the requested position. This *needs* to be set regardless of having
   * a message or not. Failure to do so would result in calculation being
   * done with stale/wrong read position */
  src->read_position = src->request_position;

  if (!src->msg) {
    if (!gst_soup_http_src_build_message (src, method)) {
      return GST_FLOW_ERROR;
    }
  }

  if (g_cancellable_is_cancelled (src->cancellable)) {
    GST_INFO_OBJECT (src, "interrupted");
    return GST_FLOW_FLUSHING;
  }

  ret = gst_soup_http_src_send_message (src);

  /* Check if Range header was respected. */
  if (ret == GST_FLOW_OK && src->request_position > 0 &&
      _soup_message_get_status (src->msg) != SOUP_STATUS_PARTIAL_CONTENT) {
    src->seekable = FALSE;
    GST_ELEMENT_ERROR_WITH_DETAILS (src, RESOURCE, SEEK,
        (_("Server does not support seeking.")),
        ("Server does not accept Range HTTP header, URL: %s, Redirect to: %s",
            src->location, GST_STR_NULL (src->redirection_uri)),
        ("http-status-code", G_TYPE_UINT, _soup_message_get_status (src->msg),
            "http-redirection-uri", G_TYPE_STRING,
            GST_STR_NULL (src->redirection_uri), NULL));
    ret = GST_FLOW_ERROR;
  }

  return ret;
}

/*
 * Check if the bytes_read is above a certain threshold of the blocksize, if
 * that happens a few times in a row, increase the blocksize; Do the same in
 * the opposite direction to reduce the blocksize.
 */
static void
gst_soup_http_src_check_update_blocksize (GstSoupHTTPSrc * src,
    gint64 bytes_read)
{
  guint blocksize = gst_base_src_get_blocksize (GST_BASE_SRC_CAST (src));

  gint64 time_since_last_read =
      g_get_monotonic_time () * GST_USECOND - src->last_socket_read_time;

  GST_LOG_OBJECT (src, "Checking to update blocksize. Read: %" G_GINT64_FORMAT
      " bytes, blocksize: %u bytes, time since last read: %" GST_TIME_FORMAT,
      bytes_read, blocksize, GST_TIME_ARGS (time_since_last_read));

  if (bytes_read >= blocksize * GROW_BLOCKSIZE_LIMIT
      && time_since_last_read <= GROW_TIME_LIMIT) {
    src->reduce_blocksize_count = 0;
    src->increase_blocksize_count++;

    if (src->increase_blocksize_count >= GROW_BLOCKSIZE_COUNT) {
      blocksize *= GROW_BLOCKSIZE_FACTOR;
      GST_DEBUG_OBJECT (src, "Increased blocksize to %u", blocksize);
      gst_base_src_set_blocksize (GST_BASE_SRC_CAST (src), blocksize);
      src->increase_blocksize_count = 0;
    }
  } else if (bytes_read < blocksize * REDUCE_BLOCKSIZE_LIMIT
      || time_since_last_read > GROW_TIME_LIMIT) {
    src->reduce_blocksize_count++;
    src->increase_blocksize_count = 0;

    if (src->reduce_blocksize_count >= REDUCE_BLOCKSIZE_COUNT) {
      blocksize *= REDUCE_BLOCKSIZE_FACTOR;
      blocksize = MAX (blocksize, src->minimum_blocksize);
      GST_DEBUG_OBJECT (src, "Decreased blocksize to %u", blocksize);
      gst_base_src_set_blocksize (GST_BASE_SRC_CAST (src), blocksize);
      src->reduce_blocksize_count = 0;
    }
  } else {
    src->reduce_blocksize_count = src->increase_blocksize_count = 0;
  }
}

static void
gst_soup_http_src_update_position (GstSoupHTTPSrc * src, gint64 bytes_read)
{
  GstBaseSrc *basesrc = GST_BASE_SRC_CAST (src);
  guint64 new_position;

  new_position = src->read_position + bytes_read;
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
}

struct GstSoupReadResult
{
  GstSoupHTTPSrc *src;
  GError *error;
  void *buffer;
  gsize bufsize;
  gssize nbytes;
};

static void
_session_read_cb (GObject * source, GAsyncResult * ret, gpointer user_data)
{
  struct GstSoupReadResult *res = user_data;

  g_mutex_lock (&res->src->session_mutex);

  res->nbytes = g_input_stream_read_finish (G_INPUT_STREAM (source),
      ret, &res->error);

  g_cond_signal (&res->src->session_cond);
  g_mutex_unlock (&res->src->session_mutex);
}

static gboolean
_session_read_idle_cb (gpointer user_data)
{
  struct GstSoupReadResult *res = user_data;

  g_input_stream_read_async (res->src->input_stream, res->buffer,
      res->bufsize, G_PRIORITY_DEFAULT, res->src->cancellable,
      _session_read_cb, res);

  return FALSE;
}

static GstFlowReturn
gst_soup_http_src_read_buffer (GstSoupHTTPSrc * src, GstBuffer ** outbuf)
{
  struct GstSoupReadResult res;
  GstMapInfo mapinfo;
  GstBaseSrc *bsrc;
  GstFlowReturn ret;
  GSource *source;

  bsrc = GST_BASE_SRC_CAST (src);

  *outbuf = gst_soup_http_src_alloc_buffer (src);
  if (!*outbuf) {
    GST_WARNING_OBJECT (src, "Failed to allocate buffer");
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (*outbuf, &mapinfo, GST_MAP_WRITE)) {
    GST_WARNING_OBJECT (src, "Failed to map buffer");
    return GST_FLOW_ERROR;
  }

  res.src = src;
  res.buffer = mapinfo.data;
  res.bufsize = mapinfo.size;
  res.error = NULL;
  res.nbytes = -1;

  source = g_idle_source_new ();

  g_mutex_lock (&src->session_mutex);

  g_source_set_callback (source, _session_read_idle_cb, &res, NULL);
  /* invoke on libsoup thread */
  g_source_attach (source, g_main_loop_get_context (src->session->loop));
  g_source_unref (source);

  /* wait for it */
  while (!res.error && res.nbytes < 0)
    g_cond_wait (&src->session_cond, &src->session_mutex);
  g_mutex_unlock (&src->session_mutex);

  GST_DEBUG_OBJECT (src, "Read %" G_GSSIZE_FORMAT " bytes from http input",
      res.nbytes);

  if (res.error) {
    /* retry by default */
    GstFlowReturn ret = GST_FLOW_CUSTOM_ERROR;
    if (g_error_matches (res.error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      ret = GST_FLOW_FLUSHING;
    } else {
      GST_ERROR_OBJECT (src, "Got error from libsoup: %s", res.error->message);
    }
    g_error_free (res.error);
    gst_buffer_unmap (*outbuf, &mapinfo);
    gst_buffer_unref (*outbuf);
    return ret;
  }

  gst_buffer_unmap (*outbuf, &mapinfo);
  if (res.nbytes > 0) {
    gst_buffer_set_size (*outbuf, res.nbytes);
    GST_BUFFER_OFFSET (*outbuf) = bsrc->segment.position;
    ret = GST_FLOW_OK;
    gst_soup_http_src_update_position (src, res.nbytes);

    /* Got some data, reset retry counter */
    src->retry_count = 0;

    gst_soup_http_src_check_update_blocksize (src, res.nbytes);

    src->last_socket_read_time = g_get_monotonic_time () * GST_USECOND;

    /* If we're at the end of a range request, read again to let libsoup
     * finalize the request. This allows to reuse the connection again later,
     * otherwise we would have to cancel the message and close the connection
     */
    if (bsrc->segment.stop != -1
        && bsrc->segment.position + res.nbytes >= bsrc->segment.stop) {
      SoupMessage *msg = src->msg;
      guint8 tmp[128];

      res.buffer = tmp;
      res.bufsize = sizeof (tmp);
      res.nbytes = -1;

      src->msg = NULL;
      src->have_body = TRUE;

      g_mutex_lock (&src->session_mutex);

      source = g_idle_source_new ();

      g_source_set_callback (source, _session_read_idle_cb, &res, NULL);
      /* This should return immediately as we're at the end of the range */
      g_source_attach (source, g_main_loop_get_context (src->session->loop));
      g_source_unref (source);

      while (!res.error && res.nbytes < 0)
        g_cond_wait (&src->session_cond, &src->session_mutex);
      g_mutex_unlock (&src->session_mutex);

      g_clear_error (&res.error);
      g_object_unref (msg);

      if (res.nbytes > 0)
        GST_ERROR_OBJECT (src,
            "Read %" G_GSIZE_FORMAT " bytes after end of range", res.nbytes);
    }
  } else {
    gst_buffer_unref (*outbuf);
    if (src->have_size && src->read_position < src->content_size) {
      /* Maybe the server disconnected, retry */
      ret = GST_FLOW_CUSTOM_ERROR;
    } else {
      g_clear_object (&src->msg);
      src->msg = NULL;
      ret = GST_FLOW_EOS;
      src->have_body = TRUE;
    }
  }

  g_clear_error (&res.error);

  return ret;
}

static gboolean
_session_stream_clear_cb (gpointer user_data)
{
  GstSoupHTTPSrc *src = user_data;

  g_mutex_lock (&src->session_mutex);

  g_clear_object (&src->input_stream);

  g_cond_signal (&src->session_cond);
  g_mutex_unlock (&src->session_mutex);

  return FALSE;
}

static void
gst_soup_http_src_stream_clear (GstSoupHTTPSrc * src)
{
  GSource *source;

  if (!src->input_stream)
    return;

  g_mutex_lock (&src->session_mutex);

  source = g_idle_source_new ();

  g_source_set_callback (source, _session_stream_clear_cb, src, NULL);
  g_source_attach (source, g_main_loop_get_context (src->session->loop));
  g_source_unref (source);

  while (src->input_stream)
    g_cond_wait (&src->session_cond, &src->session_mutex);

  g_mutex_unlock (&src->session_mutex);
}

static GstFlowReturn
gst_soup_http_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstSoupHTTPSrc *src;
  GstFlowReturn ret = GST_FLOW_OK;
  GstEvent *http_headers_event = NULL;

  src = GST_SOUP_HTTP_SRC (psrc);

retry:

  /* Check for pending position change */
  if (src->request_position != src->read_position && src->input_stream) {
    gst_soup_http_src_stream_clear (src);
  }

  if (g_cancellable_is_cancelled (src->cancellable)) {
    ret = GST_FLOW_FLUSHING;
    goto done;
  }

  /* If we have no open connection to the server, start one */
  if (!src->input_stream) {
    *outbuf = NULL;
    g_mutex_lock (&src->session_mutex);
    ret =
        gst_soup_http_src_do_request (src,
        src->method ? src->method : SOUP_METHOD_GET);
    http_headers_event = src->http_headers_event;
    src->http_headers_event = NULL;
    g_mutex_unlock (&src->session_mutex);
  }

  if (ret == GST_FLOW_OK || ret == GST_FLOW_CUSTOM_ERROR) {
    if (http_headers_event) {
      gst_pad_push_event (GST_BASE_SRC_PAD (src), http_headers_event);
      http_headers_event = NULL;
    }
  }

  if (ret == GST_FLOW_OK)
    ret = gst_soup_http_src_read_buffer (src, outbuf);

done:
  GST_DEBUG_OBJECT (src, "Returning %d %s", ret, gst_flow_get_name (ret));
  if (ret != GST_FLOW_OK) {
    if (http_headers_event)
      gst_event_unref (http_headers_event);

    if (src->input_stream) {
      gst_soup_http_src_stream_clear (src);
    }
    if (ret == GST_FLOW_CUSTOM_ERROR) {
      ret = GST_FLOW_OK;
      goto retry;
    }
  }

  if (ret == GST_FLOW_FLUSHING) {
    src->retry_count = 0;
  }

  return ret;
}

static gboolean
gst_soup_http_src_start (GstBaseSrc * bsrc)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (bsrc);
  gboolean ret;

  GST_DEBUG_OBJECT (src, "start(\"%s\")", src->location);

  g_mutex_lock (&src->session_mutex);
  ret = gst_soup_http_src_session_open (src);
  g_mutex_unlock (&src->session_mutex);
  return ret;
}

static gboolean
gst_soup_http_src_stop (GstBaseSrc * bsrc)
{
  GstSoupHTTPSrc *src;

  src = GST_SOUP_HTTP_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "stop()");

  gst_soup_http_src_stream_clear (src);

  if (src->keep_alive && !src->msg && !src->session_is_shared)
    g_cancellable_cancel (src->cancellable);
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

static void
gst_soup_http_src_set_context (GstElement * element, GstContext * context)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (element);

  if (g_strcmp0 (gst_context_get_context_type (context),
          GST_SOUP_SESSION_CONTEXT) == 0) {
    const GstStructure *s = gst_context_get_structure (context);

    GST_OBJECT_LOCK (src);

    g_clear_object (&src->external_session);
    gst_structure_get (s, "session", GST_TYPE_SOUP_SESSION,
        &src->external_session, NULL);

    GST_DEBUG_OBJECT (src, "Setting external session %p",
        src->external_session);
    GST_OBJECT_UNLOCK (src);
  }

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

/* Interrupt a blocking request. */
static gboolean
gst_soup_http_src_unlock (GstBaseSrc * bsrc)
{
  GstSoupHTTPSrc *src;

  src = GST_SOUP_HTTP_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "unlock()");

  g_cancellable_cancel (src->cancellable);
  return TRUE;
}

/* Interrupt interrupt. */
static gboolean
gst_soup_http_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstSoupHTTPSrc *src;

  src = GST_SOUP_HTTP_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "unlock_stop()");

  g_cancellable_reset (src->cancellable);
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
    g_mutex_lock (&src->session_mutex);
    while (!src->got_headers && !g_cancellable_is_cancelled (src->cancellable)
        && ret == GST_FLOW_OK) {
      if ((src->msg && _soup_message_get_method (src->msg) != SOUP_METHOD_HEAD)) {
        /* wait for the current request to finish */
        g_cond_wait (&src->session_cond, &src->session_mutex);
        ret = src->headers_ret;
      } else {
        if (gst_soup_http_src_session_open (src)) {
          ret = gst_soup_http_src_do_request (src, SOUP_METHOD_HEAD);
        }
      }
    }
    g_mutex_unlock (&src->session_mutex);
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
    gst_soup_uri_free (src->proxy);
    src->proxy = NULL;
  }

  if (uri == NULL || *uri == '\0')
    return TRUE;

  if (g_strstr_len (uri, -1, "://")) {
    src->proxy = gst_soup_uri_new (uri);
  } else {
    gchar *new_uri = g_strconcat ("http://", uri, NULL);

    src->proxy = gst_soup_uri_new (new_uri);
    g_free (new_uri);
  }

  return (src->proxy != NULL);
}

static GstURIType
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

static gboolean
souphttpsrc_element_init (GstPlugin * plugin)
{
  gboolean ret = TRUE;

  GST_DEBUG_CATEGORY_INIT (souphttpsrc_debug, "souphttpsrc", 0,
      "SOUP HTTP src");

  if (!soup_element_init (plugin))
    return TRUE;

  ret = gst_element_register (plugin, "souphttpsrc",
      GST_RANK_PRIMARY, GST_TYPE_SOUP_HTTP_SRC);

  return ret;
}
