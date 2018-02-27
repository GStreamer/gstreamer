/*
 * GstCurlHttpSrc
 * Copyright 2017 British Broadcasting Corporation - Research and Development
 *
 * Author: Sam Hurst <samuelh@rd.bbc.co.uk>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-curlhttpsrc
 *
 * This plugin reads data from a remote location specified by a URI, when the
 * protocol is 'http' or 'https'.
 *
 * It is based on the cURL project (http://curl.haxx.se/) and is specifically
 * designed to be also used with nghttp2 (http://nghttp2.org) to enable HTTP/2
 * support for GStreamer. Your libcurl library MUST be compiled against nghttp2
 * for HTTP/2 support for this functionality. HTTPS support is dependent on
 * cURL being built with SSL support (OpenSSL/PolarSSL/NSS/GnuTLS).
 *
 * An HTTP proxy must be specified by URL.
 * If the "http_proxy" environment variable is set, its value is used.
 * The #GstCurlHttpSrc:proxy property can be used to override the default.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 curlhttpsrc location=http://127.0.1.1/index.html ! fakesink dump=1
 * ]| The above pipeline reads a web page from the local machine using HTTP and
 * dumps it to stdout.
 * |[
 * gst-launch-1.0 playbin uri=http://rdmedia.bbc.co.uk/dash/testmpds/multiperiod/bbb.php
 * ]| The above pipeline will start up a DASH streaming session from the given
 * MPD file. This requires GStreamer to have been built with dashdemux from
 * gst-plugins-bad.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst-i18n-plugin.h>

#include "gstcurlhttpsrc.h"
#include "gstcurlqueue.h"
#include "gstcurldefaults.h"

GST_DEBUG_CATEGORY_STATIC (gst_curl_http_src_debug);
#define GST_CAT_DEFAULT gst_curl_http_src_debug
GST_DEBUG_CATEGORY_STATIC (gst_curl_loop_debug);

/*
 * Make a source pad template to be able to kick out recv'd data
 */
static GstStaticPadTemplate srcpadtemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/*
 * Function Definitions
 */
/* Gstreamer generic element functions */
static void gst_curl_http_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_curl_http_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_curl_http_src_ref_multi (GstCurlHttpSrc * src);
static void gst_curl_http_src_unref_multi (GstCurlHttpSrc * src);
static void gst_curl_http_src_finalize (GObject * obj);
static GstFlowReturn gst_curl_http_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static GstFlowReturn gst_curl_http_src_handle_response (GstCurlHttpSrc * src);
static gboolean gst_curl_http_src_negotiate_caps (GstCurlHttpSrc * src);
static GstStateChangeReturn gst_curl_http_src_change_state (GstElement *
    element, GstStateChange transition);
static void gst_curl_http_src_cleanup_instance (GstCurlHttpSrc * src);
static gboolean gst_curl_http_src_query (GstBaseSrc * bsrc, GstQuery * query);
static gboolean gst_curl_http_src_get_content_length (GstBaseSrc * bsrc,
    guint64 * size);
static gboolean gst_curl_http_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_curl_http_src_unlock_stop (GstBaseSrc * bsrc);

/* URI Handler functions */
static void gst_curl_http_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static guint gst_curl_http_src_urihandler_get_type (GType type);
static const gchar *const *gst_curl_http_src_urihandler_get_protocols (GType
    type);
static gchar *gst_curl_http_src_urihandler_get_uri (GstURIHandler * handler);
static gboolean gst_curl_http_src_urihandler_set_uri (GstURIHandler * handler,
    const gchar * uri, GError ** error);

/* GstTask functions */
static void gst_curl_http_src_curl_multi_loop (gpointer thread_data);
static CURL *gst_curl_http_src_create_easy_handle (GstCurlHttpSrc * s);
static inline void gst_curl_http_src_destroy_easy_handle (GstCurlHttpSrc * src);
static size_t gst_curl_http_src_get_header (void *header, size_t size,
    size_t nmemb, void *src);
static size_t gst_curl_http_src_get_chunks (void *chunk, size_t size,
    size_t nmemb, void *src);
static void gst_curl_http_src_request_remove (GstCurlHttpSrc * src);
static char *gst_curl_http_src_strcasestr (const char *haystack,
    const char *needle);

curl_version_info_data *gst_curl_http_src_curl_capabilities;
GstCurlHttpVersion pref_http_ver;
gchar *gst_curl_http_src_default_useragent;

#define GST_TYPE_CURL_HTTP_VERSION (gst_curl_http_version_get_type ())
static GType
gst_curl_http_version_get_type (void)
{
  static GType gtype = 0;

  if (!gtype) {
    static const GEnumValue http_versions[] = {
      {GSTCURL_HTTP_VERSION_1_0, "HTTP Version 1.0", "1.0"},
      {GSTCURL_HTTP_VERSION_1_1, "HTTP Version 1.1", "1.1"},
#ifdef CURL_VERSION_HTTP2
      {GSTCURL_HTTP_VERSION_2_0, "HTTP Version 2.0", "2.0"},
#endif
      {0, NULL, NULL}
    };
    gtype = g_enum_register_static ("GstCurlHttpVersionType", http_versions);
  }
  return gtype;
}

#define gst_curl_http_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstCurlHttpSrc, gst_curl_http_src, GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_curl_http_src_uri_handler_init));

static void
gst_curl_http_src_class_init (GstCurlHttpSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;
  const gchar *http_env;
  GstCurlHttpVersion default_http_version;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_curl_http_src_debug, "curlhttpsrc",
      0, "UriHandler for libcURL");

  GST_INFO_OBJECT (klass, "class_init started!");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_curl_http_src_change_state);
  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_curl_http_src_create);
  gstbasesrc_class->query = GST_DEBUG_FUNCPTR (gst_curl_http_src_query);
  gstbasesrc_class->get_size =
      GST_DEBUG_FUNCPTR (gst_curl_http_src_get_content_length);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_curl_http_src_unlock);
  gstbasesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_curl_http_src_unlock_stop);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srcpadtemplate));

  gst_curl_http_src_curl_capabilities = curl_version_info (CURLVERSION_NOW);
#ifdef CURL_VERSION_HTTP2
  if (gst_curl_http_src_curl_capabilities->features & CURL_VERSION_HTTP2) {
    default_http_version = GSTCURL_HTTP_VERSION_2_0;
  } else
#endif
    default_http_version = GSTCURL_HTTP_VERSION_1_1;

  http_env = g_getenv ("GST_CURL_HTTP_VER");
  if (http_env != NULL) {
    GST_INFO_OBJECT (klass, "Seen env var GST_CURL_HTTP_VER with value %s",
        http_env);
    if (!strcmp (http_env, "1.0")) {
      pref_http_ver = GSTCURL_HTTP_VERSION_1_0;
    } else if (!strcmp (http_env, "1.1")) {
      pref_http_ver = GSTCURL_HTTP_VERSION_1_1;
    } else if (!strcmp (http_env, "2.0")) {
#ifdef CURL_VERSION_HTTP2
      if (gst_curl_http_src_curl_capabilities->features & CURL_VERSION_HTTP2) {
        pref_http_ver = GSTCURL_HTTP_VERSION_2_0;
      } else {
        goto unsupported_http_version;
      }
#endif
    } else {
    unsupported_http_version:
      GST_WARNING_OBJECT (klass,
          "Unsupported HTTP version: %s. Fallback to default", http_env);
      pref_http_ver = default_http_version;
    }
  } else {
    pref_http_ver = default_http_version;
  }

  gst_curl_http_src_default_useragent =
      g_strdup_printf ("GStreamer curlhttpsrc libcurl/%s",
      gst_curl_http_src_curl_capabilities->version);

  gobject_class->set_property = gst_curl_http_src_set_property;
  gobject_class->get_property = gst_curl_http_src_get_property;
  gobject_class->finalize = gst_curl_http_src_finalize;

  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("location", "Location", "URI of resource to read",
          GSTCURL_HANDLE_DEFAULT_CURLOPT_URL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_USERNAME,
      g_param_spec_string ("user-id", "user-id",
          "HTTP location URI user id for authentication",
          GSTCURL_HANDLE_DEFAULT_CURLOPT_USERNAME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PASSWORD,
      g_param_spec_string ("user-pw", "user-pw",
          "HTTP location URI password for authentication",
          GSTCURL_HANDLE_DEFAULT_CURLOPT_PASSWORD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PROXYURI,
      g_param_spec_string ("proxy", "Proxy", "URI of HTTP proxy server",
          GSTCURL_HANDLE_DEFAULT_CURLOPT_PROXY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PROXYUSERNAME,
      g_param_spec_string ("proxy-id", "proxy-id",
          "HTTP proxy URI user id for authentication",
          GSTCURL_HANDLE_DEFAULT_CURLOPT_PROXYUSERNAME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PROXYPASSWORD,
      g_param_spec_string ("proxy-pw", "proxy-pw",
          "HTTP proxy URI password for authentication",
          GSTCURL_HANDLE_DEFAULT_CURLOPT_PROXYPASSWORD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_COOKIES,
      g_param_spec_boxed ("cookies", "Cookies", "List of HTTP Cookies",
          G_TYPE_STRV, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_USERAGENT,
      g_param_spec_string ("user-agent", "User-Agent",
          "URI of resource requested", GSTCURL_HANDLE_DEFAULT_CURLOPT_USERAGENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_COMPRESS,
      g_param_spec_boolean ("compress", "Compress",
          "Allow compressed content encodings",
          GSTCURL_HANDLE_DEFAULT_CURLOPT_ACCEPT_ENCODING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_REDIRECT,
      g_param_spec_boolean ("automatic-redirect", "automatic-redirect",
          "Allow HTTP Redirections (HTTP Status Code 300 series)",
          GSTCURL_HANDLE_DEFAULT_CURLOPT_FOLLOWLOCATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAXREDIRECT,
      g_param_spec_int ("max-redirect", "Max-Redirect",
          "Maximum number of permitted redirections. -1 is unlimited.",
          GSTCURL_HANDLE_MIN_CURLOPT_MAXREDIRS,
          GSTCURL_HANDLE_MAX_CURLOPT_MAXREDIRS,
          GSTCURL_HANDLE_DEFAULT_CURLOPT_MAXREDIRS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_KEEPALIVE,
      g_param_spec_boolean ("keep-alive", "Keep-Alive",
          "Toggle keep-alive for connection reuse.",
          GSTCURL_HANDLE_DEFAULT_CURLOPT_TCP_KEEPALIVE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_int ("timeout", "Timeout",
          "Value in seconds before timeout a blocking request (0 = no timeout)",
          GSTCURL_HANDLE_MIN_CURLOPT_TIMEOUT,
          GSTCURL_HANDLE_MAX_CURLOPT_TIMEOUT,
          GSTCURL_HANDLE_DEFAULT_CURLOPT_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_HEADERS,
      g_param_spec_boxed ("extra-headers", "Extra Headers",
          "Extra headers to append to the HTTP request",
          GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STRICT_SSL,
      g_param_spec_boolean ("ssl-strict", "SSL Strict",
          "Strict SSL certificate checking",
          GSTCURL_HANDLE_DEFAULT_CURLOPT_SSL_VERIFYPEER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SSL_CA_FILE,
      g_param_spec_string ("ssl-ca-file", "SSL CA File",
          "Location of an SSL CA file to use for checking SSL certificates",
          GSTCURL_HANDLE_DEFAULT_CURLOPT_CAINFO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RETRIES,
      g_param_spec_int ("retries", "Retries",
          "Maximum number of retries until giving up (-1=infinite)",
          GSTCURL_HANDLE_MIN_RETRIES, GSTCURL_HANDLE_MAX_RETRIES,
          GSTCURL_HANDLE_DEFAULT_RETRIES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CONNECTIONMAXTIME,
      g_param_spec_uint ("max-connection-time", "Max-Connection-Time",
          "Maximum amount of time to keep-alive HTTP connections",
          GSTCURL_MIN_CONNECTION_TIME, GSTCURL_MAX_CONNECTION_TIME,
          GSTCURL_DEFAULT_CONNECTION_TIME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAXCONCURRENT_SERVER,
      g_param_spec_uint ("max-connections-per-server",
          "Max-Connections-Per-Server",
          "Maximum number of connections allowed per server for HTTP/1.x",
          GSTCURL_MIN_CONNECTIONS_SERVER, GSTCURL_MAX_CONNECTIONS_SERVER,
          GSTCURL_DEFAULT_CONNECTIONS_SERVER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAXCONCURRENT_PROXY,
      g_param_spec_uint ("max-connections-per-proxy",
          "Max-Connections-Per-Proxy",
          "Maximum number of concurrent connections allowed per proxy for HTTP/1.x",
          GSTCURL_MIN_CONNECTIONS_PROXY, GSTCURL_MAX_CONNECTIONS_PROXY,
          GSTCURL_DEFAULT_CONNECTIONS_PROXY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAXCONCURRENT_GLOBAL,
      g_param_spec_uint ("max-connections", "Max-Connections",
          "Maximum number of concurrent connections allowed for HTTP/1.x",
          GSTCURL_MIN_CONNECTIONS_GLOBAL, GSTCURL_MAX_CONNECTIONS_GLOBAL,
          GSTCURL_DEFAULT_CONNECTIONS_GLOBAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_HTTPVERSION,
      g_param_spec_enum ("http-version", "HTTP-Version",
          "The preferred HTTP protocol version",
          GST_TYPE_CURL_HTTP_VERSION, pref_http_ver,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Add a debugging task so it's easier to debug in the Multi worker thread */
  GST_DEBUG_CATEGORY_INIT (gst_curl_loop_debug, "curl_multi_loop", 0,
      "libcURL loop thread debugging");
#ifndef GST_DISABLE_GST_DEBUG
  gst_debug_log (gst_curl_loop_debug, GST_LEVEL_INFO, __FILE__, __func__,
      __LINE__, NULL, "Testing the curl_multi_loop debugging prints");
#endif

  g_mutex_init (&klass->multi_task_context.mutex);
  g_cond_init (&klass->multi_task_context.signal);
  g_rec_mutex_init (&klass->multi_task_context.task_rec_mutex);

  gst_element_class_set_static_metadata (gstelement_class,
      "HTTP Client Source using libcURL",
      "Source/Network",
      "Receiver data as a client over a network via HTTP using cURL",
      "Sam Hurst <samuelh@rd.bbc.co.uk>");
}

static void
gst_curl_http_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCurlHttpSrc *source = GST_CURLHTTPSRC (object);
  GSTCURL_FUNCTION_ENTRY (source);

  switch (prop_id) {
    case PROP_URI:
      g_free (source->uri);
      source->uri = g_value_dup_string (value);
      break;
    case PROP_USERNAME:
      g_free (source->username);
      source->username = g_value_dup_string (value);
      break;
    case PROP_PASSWORD:
      g_free (source->password);
      source->password = g_value_dup_string (value);
      break;
    case PROP_PROXYURI:
      g_free (source->proxy_uri);
      source->proxy_uri = g_value_dup_string (value);
      break;
    case PROP_PROXYUSERNAME:
      g_free (source->proxy_user);
      source->proxy_user = g_value_dup_string (value);
      break;
    case PROP_PROXYPASSWORD:
      g_free (source->proxy_pass);
      source->proxy_pass = g_value_dup_string (value);
      break;
    case PROP_COOKIES:
      g_strfreev (source->cookies);
      source->cookies = g_strdupv (g_value_get_boxed (value));
      source->number_cookies = g_strv_length (source->cookies);
      break;
    case PROP_USERAGENT:
      g_free (source->user_agent);
      source->user_agent = g_value_dup_string (value);
      break;
    case PROP_HEADERS:
    {
      const GstStructure *s = gst_value_get_structure (value);
      if (source->request_headers)
        gst_structure_free (source->request_headers);
      source->request_headers = s ? gst_structure_copy (s) : NULL;
    }
      break;
    case PROP_COMPRESS:
      source->accept_compressed_encodings = g_value_get_boolean (value);
      break;
    case PROP_REDIRECT:
      source->allow_3xx_redirect = g_value_get_boolean (value);
      break;
    case PROP_MAXREDIRECT:
      source->max_3xx_redirects = g_value_get_int (value);
      break;
    case PROP_KEEPALIVE:
      source->keep_alive = g_value_get_boolean (value);
      break;
    case PROP_TIMEOUT:
      source->timeout_secs = g_value_get_int (value);
      break;
    case PROP_STRICT_SSL:
      source->strict_ssl = g_value_get_boolean (value);
      break;
    case PROP_SSL_CA_FILE:
      source->custom_ca_file = g_value_dup_string (value);
      break;
    case PROP_RETRIES:
      source->total_retries = g_value_get_int (value);
      break;
    case PROP_CONNECTIONMAXTIME:
      source->max_connection_time = g_value_get_uint (value);
      break;
    case PROP_MAXCONCURRENT_SERVER:
      source->max_conns_per_server = g_value_get_uint (value);
      break;
    case PROP_MAXCONCURRENT_PROXY:
      source->max_conns_per_proxy = g_value_get_uint (value);
      break;
    case PROP_MAXCONCURRENT_GLOBAL:
      source->max_conns_global = g_value_get_uint (value);
      break;
    case PROP_HTTPVERSION:
      source->preferred_http_version = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GSTCURL_FUNCTION_EXIT (source);
}

static void
gst_curl_http_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCurlHttpSrc *source = GST_CURLHTTPSRC (object);
  GSTCURL_FUNCTION_ENTRY (source);

  switch (prop_id) {
    case PROP_URI:
      g_value_set_string (value, source->uri);
      break;
    case PROP_USERNAME:
      g_value_set_string (value, source->username);
      break;
    case PROP_PASSWORD:
      g_value_set_string (value, source->password);
      break;
    case PROP_PROXYURI:
      g_value_set_string (value, source->proxy_uri);
      break;
    case PROP_PROXYUSERNAME:
      g_value_set_string (value, source->proxy_user);
      break;
    case PROP_PROXYPASSWORD:
      g_value_set_string (value, source->proxy_pass);
      break;
    case PROP_COOKIES:
      g_value_set_boxed (value, source->cookies);
      break;
    case PROP_USERAGENT:
      g_value_set_string (value, source->user_agent);
      break;
    case PROP_HEADERS:
      gst_value_set_structure (value, source->request_headers);
      break;
    case PROP_COMPRESS:
      g_value_set_boolean (value, source->accept_compressed_encodings);
      break;
    case PROP_REDIRECT:
      g_value_set_boolean (value, source->allow_3xx_redirect);
      break;
    case PROP_MAXREDIRECT:
      g_value_set_int (value, source->max_3xx_redirects);
      break;
    case PROP_KEEPALIVE:
      g_value_set_boolean (value, source->keep_alive);
      break;
    case PROP_TIMEOUT:
      g_value_set_int (value, source->timeout_secs);
      break;
    case PROP_STRICT_SSL:
      g_value_set_boolean (value, source->strict_ssl);
      break;
    case PROP_SSL_CA_FILE:
      g_value_set_string (value, source->custom_ca_file);
      break;
    case PROP_RETRIES:
      g_value_set_int (value, source->total_retries);
      break;
    case PROP_CONNECTIONMAXTIME:
      g_value_set_uint (value, source->max_connection_time);
      break;
    case PROP_MAXCONCURRENT_SERVER:
      g_value_set_uint (value, source->max_conns_per_server);
      break;
    case PROP_MAXCONCURRENT_PROXY:
      g_value_set_uint (value, source->max_conns_per_proxy);
      break;
    case PROP_MAXCONCURRENT_GLOBAL:
      g_value_set_uint (value, source->max_conns_global);
      break;
    case PROP_HTTPVERSION:
      g_value_set_enum (value, source->preferred_http_version);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GSTCURL_FUNCTION_EXIT (source);
}

static void
gst_curl_http_src_init (GstCurlHttpSrc * source)
{
  GSTCURL_FUNCTION_ENTRY (source);

  /* Assume everything is already free'd */
  source->uri = NULL;
  source->redirect_uri = NULL;
  source->username = GSTCURL_HANDLE_DEFAULT_CURLOPT_USERNAME;
  source->password = GSTCURL_HANDLE_DEFAULT_CURLOPT_PASSWORD;
  source->proxy_uri = NULL;
  source->proxy_user = NULL;
  source->proxy_pass = NULL;
  source->cookies = NULL;
  source->user_agent = GSTCURL_HANDLE_DEFAULT_CURLOPT_USERAGENT;
  source->number_cookies = 0;
  source->request_headers = NULL;
  source->allow_3xx_redirect = GSTCURL_HANDLE_DEFAULT_CURLOPT_FOLLOWLOCATION;
  source->max_3xx_redirects = GSTCURL_HANDLE_DEFAULT_CURLOPT_MAXREDIRS;
  source->keep_alive = GSTCURL_HANDLE_DEFAULT_CURLOPT_TCP_KEEPALIVE;
  source->timeout_secs = GSTCURL_HANDLE_DEFAULT_CURLOPT_TIMEOUT;
  source->max_connection_time = GSTCURL_DEFAULT_CONNECTION_TIME;
  source->max_conns_per_server = GSTCURL_DEFAULT_CONNECTIONS_SERVER;
  source->max_conns_per_proxy = GSTCURL_DEFAULT_CONNECTIONS_PROXY;
  source->max_conns_global = GSTCURL_DEFAULT_CONNECTIONS_GLOBAL;
  source->strict_ssl = GSTCURL_HANDLE_DEFAULT_CURLOPT_SSL_VERIFYPEER;
  source->custom_ca_file = NULL;
  source->preferred_http_version = pref_http_ver;
  source->total_retries = GSTCURL_HANDLE_DEFAULT_RETRIES;
  source->retries_remaining = source->total_retries;
  source->slist = NULL;

  gst_caps_replace (&source->caps, NULL);
  gst_base_src_set_automatic_eos (GST_BASE_SRC (source), FALSE);

  source->proxy_uri = g_strdup (g_getenv ("http_proxy"));
  source->no_proxy_list = g_strdup (g_getenv ("no_proxy"));

  g_mutex_init (&source->uri_mutex);
  g_mutex_init (&source->buffer_mutex);
  g_cond_init (&source->signal);

  source->buffer = NULL;
  source->buffer_len = 0;
  source->state = GSTCURL_NONE;
  source->pending_state = GSTCURL_NONE;
  source->status_code = 0;

  source->http_headers = NULL;
  source->hdrs_updated = FALSE;

  source->curl_result = CURLE_OK;

  GSTCURL_FUNCTION_EXIT (source);
}

/*
 * Check if the Curl multi loop has been started. If not, initialise it and
 * start it running. If it is already running, increment the refcount.
 */
static void
gst_curl_http_src_ref_multi (GstCurlHttpSrc * src)
{
  GstCurlHttpSrcClass *klass;

  GSTCURL_FUNCTION_ENTRY (src);

  /*klass = (GstCurlHttpSrcClass) g_type_class_peek_parent (src); */
  klass = G_TYPE_INSTANCE_GET_CLASS (src, GST_TYPE_CURL_HTTP_SRC,
      GstCurlHttpSrcClass);

  g_mutex_lock (&klass->multi_task_context.mutex);
  if (klass->multi_task_context.refcount == 0) {
    /* Set up various in-task properties */

    /* NULL is treated as the start of the list, no need to allocate. */
    klass->multi_task_context.queue = NULL;

    /* set up curl */
    klass->multi_task_context.multi_handle = curl_multi_init ();

    curl_multi_setopt (klass->multi_task_context.multi_handle,
        CURLMOPT_PIPELINING, 1);
#ifdef CURLMOPT_MAX_HOST_CONNECTIONS
    curl_multi_setopt (klass->multi_task_context.multi_handle,
        CURLMOPT_MAX_HOST_CONNECTIONS, 1);
#endif

    /* Start the thread */
    klass->multi_task_context.task = gst_task_new (
        (GstTaskFunction) gst_curl_http_src_curl_multi_loop,
        (gpointer) & klass->multi_task_context, NULL);
    gst_task_set_lock (klass->multi_task_context.task,
        &klass->multi_task_context.task_rec_mutex);
    if (gst_task_start (klass->multi_task_context.task) == FALSE) {
      /*
       * This is a pretty critical failure and is not recoverable, so commit
       * sudoku and run away.
       */
      GSTCURL_ERROR_PRINT ("Couldn't start curl_multi task! Aborting.");
      abort ();
    }
    GSTCURL_INFO_PRINT ("Curl multi loop has been correctly initialised!");
  }
  klass->multi_task_context.refcount++;
  g_mutex_unlock (&klass->multi_task_context.mutex);

  GSTCURL_FUNCTION_EXIT (src);
}

/*
 * Decrement the reference count on the curl multi loop. If this is called by
 * the last instance to hold a reference, shut down the worker. (Otherwise
 * GStreamer can't close down with a thread still running). Also offers the
 * "force_all" boolean parameter, which if TRUE removes all references and shuts
 * down.
 */
static void
gst_curl_http_src_unref_multi (GstCurlHttpSrc * src)
{
  GstCurlHttpSrcClass *klass;

  GSTCURL_FUNCTION_ENTRY (src);

  klass = G_TYPE_INSTANCE_GET_CLASS (src, GST_TYPE_CURL_HTTP_SRC,
      GstCurlHttpSrcClass);

  g_mutex_lock (&klass->multi_task_context.mutex);
  klass->multi_task_context.refcount--;
  GST_INFO_OBJECT (src, "Closing instance, worker thread refcount is now %u",
      klass->multi_task_context.refcount);

  if (klass->multi_task_context.refcount <= 0) {
    /* Everything's done! Clean up. */
    gst_task_pause (klass->multi_task_context.task);
    klass->multi_task_context.state = GSTCURL_MULTI_LOOP_STATE_STOP;
    g_cond_signal (&klass->multi_task_context.signal);
    g_mutex_unlock (&klass->multi_task_context.mutex);
    gst_task_join (klass->multi_task_context.task);
  } else {
    g_mutex_unlock (&klass->multi_task_context.mutex);
  }

  GSTCURL_FUNCTION_EXIT (src);
}

static void
gst_curl_http_src_finalize (GObject * obj)
{
  GstCurlHttpSrc *src = GST_CURLHTTPSRC (obj);

  GSTCURL_FUNCTION_ENTRY (src);

  /* Cleanup all memory allocated */
  gst_curl_http_src_cleanup_instance (src);

  GSTCURL_FUNCTION_EXIT (src);
}

/*
 * Do the transfer. If the transfer hasn't begun yet, start a new curl handle
 * and pass it to the multi queue to be operated on. Then wait for any blocks
 * of data and push them to the source pad.
 */
static GstFlowReturn
gst_curl_http_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstFlowReturn ret;
  GstCurlHttpSrc *src = GST_CURLHTTPSRC (psrc);
  GstCurlHttpSrcClass *klass;
  GstStructure *empty_headers;

  klass = G_TYPE_INSTANCE_GET_CLASS (src, GST_TYPE_CURL_HTTP_SRC,
      GstCurlHttpSrcClass);

  GSTCURL_FUNCTION_ENTRY (src);
  ret = GST_FLOW_OK;

  g_mutex_lock (&src->buffer_mutex);
  if (src->state == GSTCURL_UNLOCK) {
    ret = GST_FLOW_FLUSHING;
    goto escape;
  }

retry:
  if (!src->transfer_begun) {
    GST_DEBUG_OBJECT (src, "Starting new request for URI %s", src->uri);
    /* Create the Easy Handle and set up the session. */
    src->curl_handle = gst_curl_http_src_create_easy_handle (src);
    if (src->curl_handle == NULL) {
      ret = GST_FLOW_ERROR;
      goto escape;
    }

    g_mutex_lock (&klass->multi_task_context.mutex);

    if (gst_curl_http_src_add_queue_item (&klass->multi_task_context.queue, src)
        == FALSE) {
      GST_ERROR_OBJECT (src, "Couldn't create new queue item! Aborting...");
      ret = GST_FLOW_ERROR;
      goto escape;
    }

    /* Signal the worker thread */
    klass->multi_task_context.state = GSTCURL_MULTI_LOOP_STATE_QUEUE_EVENT;
    g_cond_signal (&klass->multi_task_context.signal);
    g_mutex_unlock (&klass->multi_task_context.mutex);

    src->state = GSTCURL_OK;
    src->transfer_begun = TRUE;
    src->data_received = FALSE;

    GST_DEBUG_OBJECT (src, "Submitted request for URI %s to curl", src->uri);

    empty_headers = gst_structure_new_empty (RESPONSE_HEADERS_NAME);
    src->http_headers = gst_structure_new (HTTP_HEADERS_NAME,
        URI_NAME, G_TYPE_STRING, src->uri,
        REQUEST_HEADERS_NAME, GST_TYPE_STRUCTURE, src->request_headers,
        RESPONSE_HEADERS_NAME, GST_TYPE_STRUCTURE, empty_headers, NULL);
    gst_structure_free (empty_headers);
    GST_INFO_OBJECT (src, "Created a new headers object");
  }

  /* Wait for data to become available, then punt it downstream */
  while ((src->buffer_len == 0) && (src->state == GSTCURL_OK)) {
    g_cond_wait (&src->signal, &src->buffer_mutex);
  }

  if (src->state == GSTCURL_UNLOCK) {
    if (src->buffer_len > 0) {
      g_free (src->buffer);
      src->buffer = NULL;
      src->buffer_len = 0;
    }
    ret = GST_FLOW_FLUSHING;
    goto escape;
  }

  ret = gst_curl_http_src_handle_response (src);
  switch (ret) {
    case GST_FLOW_ERROR:
      goto escape;              /* Don't attempt a retry, just bomb out */
    case GST_FLOW_CUSTOM_ERROR:
      if (src->data_received == TRUE) {
        /*
         * If data has already been received, we can't recall previously sent
         * buffers so don't attempt a retry in this case.
         *
         * TODO: Remember the position we got to, and make a range request for
         * the resource without the bit we've already received?
         */
        GST_WARNING_OBJECT (src,
            "Failed mid-transfer, can't continue for URI %s", src->uri);
        ret = GST_FLOW_ERROR;
        goto escape;
      }
      src->retries_remaining--;
      if (src->retries_remaining == 0) {
        GST_WARNING_OBJECT (src, "Out of retries for URI %s", src->uri);
        ret = GST_FLOW_ERROR;   /* Don't attempt a retry, just bomb out */
        goto escape;
      }
      GST_INFO_OBJECT (src, "Attempting retry for URI %s", src->uri);
      src->state = GSTCURL_NONE;
      src->transfer_begun = FALSE;
      src->status_code = 0;
      src->hdrs_updated = FALSE;
      if (src->http_headers != NULL) {
        gst_structure_free (src->http_headers);
        src->http_headers = NULL;
        GST_INFO_OBJECT (src, "NULL'd the headers");
      }
      gst_curl_http_src_destroy_easy_handle (src);
      g_mutex_unlock (&src->buffer_mutex);
      goto retry;               /* Attempt a retry! */
    default:
      break;
  }

  if (((src->state == GSTCURL_OK) || (src->state == GSTCURL_DONE)) &&
      (src->buffer_len > 0)) {

    GST_DEBUG_OBJECT (src, "Pushing %u bytes of transfer for URI %s to pad",
        src->buffer_len, src->uri);
    *outbuf = gst_buffer_new_allocate (NULL, src->buffer_len, NULL);
    gst_buffer_fill (*outbuf, 0, src->buffer, src->buffer_len);

    g_free (src->buffer);
    src->buffer = NULL;
    src->buffer_len = 0;
    src->data_received = TRUE;

    /* ret should still be GST_FLOW_OK */
  } else if ((src->state == GSTCURL_DONE) && (src->buffer_len == 0)) {
    GST_INFO_OBJECT (src, "Full body received, signalling EOS for URI %s.",
        src->uri);
    src->state = GSTCURL_NONE;
    src->transfer_begun = FALSE;
    src->status_code = 0;
    src->hdrs_updated = FALSE;
    gst_curl_http_src_destroy_easy_handle (src);
    ret = GST_FLOW_EOS;
  } else {
    switch (src->state) {
      case GSTCURL_NONE:
        GST_WARNING_OBJECT (src, "Got unexpected GSTCURL_NONE state!");
        break;
      case GSTCURL_REMOVED:
        GST_WARNING_OBJECT (src, "Transfer got removed from the curl queue");
        ret = GST_FLOW_EOS;
        break;
      case GSTCURL_BAD_QUEUE_REQUEST:
        GST_ERROR_OBJECT (src, "Bad Queue Request!");
        ret = GST_FLOW_ERROR;
        break;
      case GSTCURL_TOTAL_ERROR:
        GST_ERROR_OBJECT (src, "Critical, unrecoverable error!");
        ret = GST_FLOW_ERROR;
        break;
      case GSTCURL_PIPELINE_NULL:
        GST_ERROR_OBJECT (src, "Pipeline null");
        break;
      default:
        GST_ERROR_OBJECT (src, "Unknown state of %u", src->state);
    }
  }

escape:
  g_mutex_unlock (&src->buffer_mutex);

  GSTCURL_FUNCTION_EXIT (src);
  return ret;
}

/*
 * Convert header from a GstStructure type to a curl_slist type that curl will
 * understand.
 */
static gboolean
_headers_to_curl_slist (GQuark field_id, const GValue * value, gpointer ptr)
{
  gchar *field;
  struct curl_slist **p_slist = ptr;

  field = g_strdup_printf ("%s: %s", g_quark_to_string (field_id),
      g_value_get_string (value));

  *p_slist = curl_slist_append (*p_slist, field);

  g_free (field);

  return TRUE;
}

/*
 * From the data in the queue element s, create a CURL easy handle and populate
 * options with the URL, proxy data, login options, cookies,
 */
static CURL *
gst_curl_http_src_create_easy_handle (GstCurlHttpSrc * s)
{
  CURL *handle;
  gint i;
  GSTCURL_FUNCTION_ENTRY (s);

  handle = curl_easy_init ();
  if (handle == NULL) {
    GST_ERROR_OBJECT (s, "Couldn't init a curl easy handle!");
    return NULL;
  }
  GST_INFO_OBJECT (s, "Creating a new handle for URI %s", s->uri);

  /* This is mandatory and yet not default option, so if this is NULL
   * then something very bad is going on. */
  if (s->uri == NULL) {
    GST_ERROR_OBJECT (s, "No URI for curl!");
    return NULL;
  }
  gst_curl_setopt_str (s, handle, CURLOPT_URL, s->uri);

  gst_curl_setopt_str (s, handle, CURLOPT_USERNAME, s->username);
  gst_curl_setopt_str (s, handle, CURLOPT_PASSWORD, s->password);
  gst_curl_setopt_str (s, handle, CURLOPT_PROXY, s->proxy_uri);
  gst_curl_setopt_str (s, handle, CURLOPT_NOPROXY, s->no_proxy_list);
  gst_curl_setopt_str (s, handle, CURLOPT_PROXYUSERNAME, s->proxy_user);
  gst_curl_setopt_str (s, handle, CURLOPT_PROXYPASSWORD, s->proxy_pass);

  for (i = 0; i < s->number_cookies; i++) {
    gst_curl_setopt_str (s, handle, CURLOPT_COOKIELIST, s->cookies[i]);
  }

  /* curl_slist_append dynamically allocates memory, but I need to free it */
  if (s->request_headers != NULL) {
    gst_structure_foreach (s->request_headers, _headers_to_curl_slist,
        &s->slist);
    if (curl_easy_setopt (handle, CURLOPT_HTTPHEADER, s->slist) != CURLE_OK) {
      GST_WARNING_OBJECT (s, "Failed to set HTTP headers!");
    }
  }

  gst_curl_setopt_str_default (s, handle, CURLOPT_USERAGENT, s->user_agent);

  /*
   * Unlike soup, this isn't a binary op, curl wants a string here. So if it's
   * TRUE, simply set the value as an empty string as this allows both gzip and
   * zlib compression methods.
   */
  if (s->accept_compressed_encodings == TRUE) {
    gst_curl_setopt_str (s, handle, CURLOPT_ACCEPT_ENCODING, "");
  } else {
    gst_curl_setopt_str (s, handle, CURLOPT_ACCEPT_ENCODING, "identity");
  }

  gst_curl_setopt_int (s, handle, CURLOPT_FOLLOWLOCATION,
      s->allow_3xx_redirect);
  gst_curl_setopt_int_default (s, handle, CURLOPT_MAXREDIRS,
      s->max_3xx_redirects);
  gst_curl_setopt_bool (s, handle, CURLOPT_TCP_KEEPALIVE, s->keep_alive);
  gst_curl_setopt_int (s, handle, CURLOPT_TIMEOUT, s->timeout_secs);
  gst_curl_setopt_bool (s, handle, CURLOPT_SSL_VERIFYPEER, s->strict_ssl);
  gst_curl_setopt_str (s, handle, CURLOPT_CAINFO, s->custom_ca_file);

  switch (s->preferred_http_version) {
    case GSTCURL_HTTP_VERSION_1_0:
      GST_DEBUG_OBJECT (s, "Setting version as HTTP/1.0");
      gst_curl_setopt_int (s, handle, CURLOPT_HTTP_VERSION,
          CURL_HTTP_VERSION_1_0);
      break;
    case GSTCURL_HTTP_VERSION_1_1:
      GST_DEBUG_OBJECT (s, "Setting version as HTTP/1.1");
      gst_curl_setopt_int (s, handle, CURLOPT_HTTP_VERSION,
          CURL_HTTP_VERSION_1_1);
      break;
#ifdef CURL_VERSION_HTTP2
    case GSTCURL_HTTP_VERSION_2_0:
      GST_DEBUG_OBJECT (s, "Setting version as HTTP/2.0");
      if (curl_easy_setopt (handle, CURLOPT_HTTP_VERSION,
              CURL_HTTP_VERSION_2_0) != CURLE_OK) {
        if (gst_curl_http_src_curl_capabilities->features & CURL_VERSION_HTTP2) {
          GST_WARNING_OBJECT (s,
              "Cannot set unsupported option CURLOPT_HTTP_VERSION");
        } else {
          GST_INFO_OBJECT (s, "HTTP/2 unsupported by libcurl at this time");
        }
      }
      break;
#endif
    default:
      GST_WARNING_OBJECT (s,
          "Supplied a bogus HTTP version, using curl default!");
  }

  gst_curl_setopt_generic (s, handle, CURLOPT_HEADERFUNCTION,
      gst_curl_http_src_get_header);
  gst_curl_setopt_str (s, handle, CURLOPT_HEADERDATA, s);
  gst_curl_setopt_generic (s, handle, CURLOPT_WRITEFUNCTION,
      gst_curl_http_src_get_chunks);
  gst_curl_setopt_str (s, handle, CURLOPT_WRITEDATA, s);

  gst_curl_setopt_str (s, handle, CURLOPT_ERRORBUFFER, s->curl_errbuf);

  GSTCURL_FUNCTION_EXIT (s);
  return handle;
}

/*
 * Check the return type from the curl transfer. If it was okay, then deal with
 * any headers that were received. Headers should only be dealt with once - but
 * we might get a second set if there are trailing headers (RFC7230 Section 4.4)
 */
static GstFlowReturn
gst_curl_http_src_handle_response (GstCurlHttpSrc * src)
{
  glong curl_info_long;
  gdouble curl_info_dbl;
  gchar *redirect_url;
  GstBaseSrc *basesrc;
  const GValue *response_headers;
  GstFlowReturn ret = GST_FLOW_OK;

  GSTCURL_FUNCTION_ENTRY (src);

  GST_TRACE_OBJECT (src, "status code: %d, curl return code %d",
      src->status_code, src->curl_result);

  /* Check the curl result code first - anything not 0 is probably a failure */
  if (src->curl_result != 0) {
    GST_WARNING_OBJECT (src, "Curl failed the transfer (%d): %s",
        src->curl_result, curl_easy_strerror (src->curl_result));
    GST_DEBUG_OBJECT (src, "Reason for curl failure: %s", src->curl_errbuf);
    return GST_FLOW_ERROR;
  }

  /*
   * What response code do we have?
   */
  if (src->status_code >= 400) {
    GST_WARNING_OBJECT (src, "Transfer for URI %s returned error status %u",
        src->uri, src->status_code);
    src->retries_remaining = 0;
    return GST_FLOW_ERROR;
  } else if (src->status_code == 0) {
    if (curl_easy_getinfo (src->curl_handle, CURLINFO_TOTAL_TIME,
            &curl_info_dbl) != CURLE_OK) {
      /* Curl cannot be relied on in this state, so return an error. */
      return GST_FLOW_ERROR;
    }
    if (curl_info_dbl > src->timeout_secs) {
      return GST_FLOW_CUSTOM_ERROR;
    }

    if (curl_easy_getinfo (src->curl_handle, CURLINFO_OS_ERRNO,
            &curl_info_long) != CURLE_OK) {
      /* Curl cannot be relied on in this state, so return an error. */
      return GST_FLOW_ERROR;

    }

    GST_WARNING_OBJECT (src, "Errno for CONNECT call was %ld (%s)",
        curl_info_long, g_strerror ((gint) curl_info_long));

    /* Some of these responses are retry-able, others not. Set the returned
     * state to ERROR so we crash out instead of fruitlessly retrying.
     */
    if (curl_info_long == ECONNREFUSED) {
      return GST_FLOW_ERROR;
    }
    ret = GST_FLOW_CUSTOM_ERROR;
  }


  if (ret == GST_FLOW_CUSTOM_ERROR) {
    src->hdrs_updated = FALSE;
    GSTCURL_FUNCTION_EXIT (src);
    return ret;
  }

  /* Only do this once */
  if (src->hdrs_updated == FALSE) {
    GSTCURL_FUNCTION_EXIT (src);
    return GST_FLOW_OK;
  }

  /*
   * Deal with redirections...
   */
  if (curl_easy_getinfo (src->curl_handle, CURLINFO_EFFECTIVE_URL,
          &redirect_url)
      == CURLE_OK) {
    size_t lena, lenb;
    lena = strlen (src->uri);
    lenb = strlen (redirect_url);
    if (g_ascii_strncasecmp (src->uri, redirect_url,
            (lena > lenb) ? lenb : lena) != 0) {
      GST_INFO_OBJECT (src, "Got a redirect to %s, setting as redirect URI",
          redirect_url);
      src->redirect_uri = g_strdup (redirect_url);
      gst_structure_remove_field (src->http_headers, REDIRECT_URI_NAME);
      gst_structure_set (src->http_headers, REDIRECT_URI_NAME,
          G_TYPE_STRING, redirect_url, NULL);
    }
  }

  /*
   * Push the content length
   */
  if (curl_easy_getinfo (src->curl_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD,
          &curl_info_dbl) == CURLE_OK) {
    if (curl_info_dbl == -1) {
      GST_WARNING_OBJECT (src,
          "No Content-Length was specified in the response.");
    } else {
      GST_INFO_OBJECT (src, "Content-Length was given as %.0f", curl_info_dbl);
      basesrc = GST_BASE_SRC_CAST (src);
      basesrc->segment.duration = curl_info_dbl;
      gst_element_post_message (GST_ELEMENT (src),
          gst_message_new_duration_changed (GST_OBJECT (src)));
    }
  }

  /*
   * Push all the received headers down via a sicky event
   */
  response_headers = gst_structure_get_value (src->http_headers,
      RESPONSE_HEADERS_NAME);
  if (gst_structure_n_fields (gst_value_get_structure (response_headers)) > 0) {
    GstEvent *hdrs_event;
    GstStructure *empty_headers;

    gst_element_post_message (GST_ELEMENT_CAST (src),
        gst_message_new_element (GST_OBJECT_CAST (src),
            gst_structure_copy (src->http_headers)));

    /* gst_event_new_custom takes ownership of our structure */
    hdrs_event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM_STICKY,
        src->http_headers);
    gst_pad_push_event (GST_BASE_SRC_PAD (src), hdrs_event);
    GST_INFO_OBJECT (src, "Pushed headers downstream");
    empty_headers = gst_structure_new_empty (RESPONSE_HEADERS_NAME);
    src->http_headers = gst_structure_new (HTTP_HEADERS_NAME,
        URI_NAME, G_TYPE_STRING, src->uri,
        REQUEST_HEADERS_NAME, GST_TYPE_STRUCTURE, src->request_headers,
        RESPONSE_HEADERS_NAME, GST_TYPE_STRUCTURE, empty_headers, NULL);
    gst_structure_free (empty_headers);
  }

  src->hdrs_updated = FALSE;

  GSTCURL_FUNCTION_EXIT (src);

  return ret;
}

/*
 * "Negotiate" capabilities between us and the sink.
 * I.e. tell the sink device what data to expect. We can't be told what to send
 * unless we implement "only return to me if this type" property. Potential TODO
 */
static gboolean
gst_curl_http_src_negotiate_caps (GstCurlHttpSrc * src)
{
  GST_INFO_OBJECT (src, "Negotiating caps...");
  if (src->caps && src->http_headers) {
    const GValue *response_headers = gst_structure_get_value (src->http_headers,
        RESPONSE_HEADERS_NAME);

    if (gst_structure_has_field (gst_value_get_structure (response_headers),
            "content-type") == TRUE) {
      const GValue *gv_content_type =
          gst_structure_get_value (gst_value_get_structure (response_headers),
          "content-type");
      if (G_VALUE_HOLDS_STRING (gv_content_type) == TRUE) {
        const gchar *content_type = g_value_get_string (gv_content_type);
        GST_INFO_OBJECT (src, "Setting caps as Content-Type of %s",
            content_type);
        src->caps = gst_caps_make_writable (src->caps);
        gst_caps_set_simple (src->caps, "content-type", G_TYPE_STRING,
            content_type, NULL);
        if (gst_base_src_set_caps (GST_BASE_SRC (src), src->caps) != TRUE) {
          GST_ERROR_OBJECT (src, "Setting caps failed!");
          return FALSE;
        }
      } else {
        GST_ERROR_OBJECT (src, "Content Type doesn't contain expected string");
        return FALSE;
      }
    }
  } else {
    GST_DEBUG_OBJECT (src, "No caps have been set, continue.");
  }

  return TRUE;
}

/*
 * Cleanup the CURL easy handle once we're done with it.
 */
static inline void
gst_curl_http_src_destroy_easy_handle (GstCurlHttpSrc * src)
{
  /* Thank you Handles, and well done. Well done, mate. */
  if (src->curl_handle != NULL) {
    curl_easy_cleanup (src->curl_handle);
    src->curl_handle = NULL;
  }
  /* In addition, clean up the curl header slist if it was used. */
  if (src->slist != NULL) {
    curl_slist_free_all (src->slist);
    src->slist = NULL;
  }
}

static GstStateChangeReturn
gst_curl_http_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstCurlHttpSrc *source = GST_CURLHTTPSRC (element);
  GSTCURL_FUNCTION_ENTRY (source);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      gst_curl_http_src_ref_multi (source);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (source->uri == NULL) {
        GST_ELEMENT_ERROR (element, RESOURCE, OPEN_READ, (_("No URL set.")),
            ("Missing URL"));
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* The pipeline has ended, so signal any running request to end. */
      gst_curl_http_src_request_remove (source);
      gst_curl_http_src_unref_multi (source);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  GSTCURL_FUNCTION_EXIT (source);
  return ret;
}

/*
 * Take care of any memory that may be left over from the instance that's now
 * closing before we leak it.
 */
static void
gst_curl_http_src_cleanup_instance (GstCurlHttpSrc * src)
{
  gint i;
  g_mutex_lock (&src->uri_mutex);
  g_free (src->uri);
  src->uri = NULL;
  g_free (src->redirect_uri);
  src->redirect_uri = NULL;
  g_mutex_unlock (&src->uri_mutex);
  g_mutex_clear (&src->uri_mutex);

  g_free (src->proxy_uri);
  src->proxy_uri = NULL;
  g_free (src->no_proxy_list);
  src->no_proxy_list = NULL;
  g_free (src->proxy_user);
  src->proxy_user = NULL;
  g_free (src->proxy_pass);
  src->proxy_pass = NULL;

  for (i = 0; i < src->number_cookies; i++) {
    g_free (src->cookies[i]);
    src->cookies[i] = NULL;
  }
  g_free (src->cookies);
  src->cookies = NULL;

  g_mutex_clear (&src->buffer_mutex);

  g_cond_clear (&src->signal);

  g_free (src->buffer);
  src->buffer = NULL;

  if (src->http_headers != NULL) {
    gst_structure_free (src->http_headers);
    src->http_headers = NULL;
  }

  gst_curl_http_src_destroy_easy_handle (src);
}

static gboolean
gst_curl_http_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstCurlHttpSrc *src = GST_CURLHTTPSRC (bsrc);
  gboolean ret;
  GSTCURL_FUNCTION_ENTRY (src);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_URI:
      gst_query_set_uri (query, src->uri);
      if (src->redirect_uri != NULL) {
        gst_query_set_uri_redirection (query, src->redirect_uri);
      }
      ret = TRUE;
      break;
    default:
      ret = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
      break;
  }

  GSTCURL_FUNCTION_EXIT (src);
  return ret;
}

static gboolean
gst_curl_http_src_get_content_length (GstBaseSrc * bsrc, guint64 * size)
{
  GstCurlHttpSrc *src = GST_CURLHTTPSRC (bsrc);
  const GValue *response_headers;
  gboolean ret = FALSE;

  if (src->http_headers == NULL) {
    return FALSE;
  }

  response_headers = gst_structure_get_value (src->http_headers,
      RESPONSE_HEADERS_NAME);
  if (gst_structure_has_field (gst_value_get_structure (response_headers),
          "content-length") == TRUE) {
    const GValue *content_length =
        gst_structure_get_value (gst_value_get_structure (response_headers),
        "content-length");
    if (G_VALUE_HOLDS_STRING (content_length) == TRUE) {
      const gchar *len = g_value_get_string (content_length);
      *size = (guint64) g_ascii_strtoull (len, NULL, 10);
      ret = TRUE;
    } else {
      GST_ERROR_OBJECT (src, "Content Length doesn't contain expected string");
    }
  }

  GST_DEBUG_OBJECT (src,
      "No content length has yet been set, or there was an error!");
  return ret;
}

static void
gst_curl_http_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *uri_iface = (GstURIHandlerInterface *) g_iface;

  uri_iface->get_type = gst_curl_http_src_urihandler_get_type;
  uri_iface->get_protocols = gst_curl_http_src_urihandler_get_protocols;
  uri_iface->get_uri = gst_curl_http_src_urihandler_get_uri;
  uri_iface->set_uri = gst_curl_http_src_urihandler_set_uri;
}

static guint
gst_curl_http_src_urihandler_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_curl_http_src_urihandler_get_protocols (GType type)
{
  static const gchar *protocols[] = { "http", "https", NULL };

  return protocols;
}

static gchar *
gst_curl_http_src_urihandler_get_uri (GstURIHandler * handler)
{
  gchar *ret;
  GstCurlHttpSrc *source;

  g_return_val_if_fail (GST_IS_URI_HANDLER (handler), NULL);
  source = GST_CURLHTTPSRC (handler);

  GSTCURL_FUNCTION_ENTRY (source);

  g_mutex_lock (&source->uri_mutex);
  ret = g_strdup (source->uri);
  g_mutex_unlock (&source->uri_mutex);

  GSTCURL_FUNCTION_EXIT (source);
  return ret;
}

static gboolean
gst_curl_http_src_urihandler_set_uri (GstURIHandler * handler,
    const gchar * uri, GError ** error)
{
  GstCurlHttpSrc *source = GST_CURLHTTPSRC (handler);
  GSTCURL_FUNCTION_ENTRY (source);

  g_return_val_if_fail (GST_IS_URI_HANDLER (handler), FALSE);
  g_return_val_if_fail (uri != NULL, FALSE);

  g_mutex_lock (&source->uri_mutex);

  if (source->uri != NULL) {
    GST_DEBUG_OBJECT (source,
        "URI already present as %s, updating to new URI %s", source->uri, uri);
    g_free (source->uri);
  }

  source->uri = g_strdup (uri);
  if (source->uri == NULL) {
    return FALSE;
  }
  source->retries_remaining = source->total_retries;

  g_mutex_unlock (&source->uri_mutex);

  GSTCURL_FUNCTION_EXIT (source);
  return TRUE;
}

/*
 * Cancel any currently running transfer, and then signal all the loops to drop
 * any received buffers. The ::create() method should return GST_FLOW_FLUSHING.
 */
static gboolean
gst_curl_http_src_unlock (GstBaseSrc * bsrc)
{
  GstCurlHttpSrc *src = GST_CURLHTTPSRC (bsrc);

  g_mutex_lock (&src->buffer_mutex);
  if (src->state != GSTCURL_UNLOCK) {
    if (src->state == GSTCURL_OK) {
      /* A transfer is running, cancel it */
      gst_curl_http_src_request_remove (src);
    }
    src->pending_state = src->state;
    src->state = GSTCURL_UNLOCK;
  }
  g_cond_signal (&src->signal);
  g_mutex_unlock (&src->buffer_mutex);

  return TRUE;
}

/*
 * Finish the unlock request above and return curlhttpsrc to the normal state.
 * This will probably be GSTCURL_DONE, and the next return from ::create() will
 * be GST_FLOW_EOS as we don't want to deliver parts of a HTTP body.
 */
static gboolean
gst_curl_http_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstCurlHttpSrc *src = GST_CURLHTTPSRC (bsrc);

  g_mutex_lock (&src->buffer_mutex);
  src->state = src->pending_state;
  src->pending_state = GSTCURL_NONE;
  g_cond_signal (&src->signal);
  g_mutex_unlock (&src->buffer_mutex);

  return TRUE;
}

/*****************************************************************************
 * Curl loop task functions begin
 *****************************************************************************/
static void
gst_curl_http_src_curl_multi_loop (gpointer thread_data)
{
  GstCurlHttpSrcMultiTaskContext *context;
  GstCurlHttpSrcQueueElement *qelement;
  int i, still_running;
  gboolean cond = FALSE;
  CURLMsg *curl_message;

  context = (GstCurlHttpSrcMultiTaskContext *) thread_data;

  g_mutex_lock (&context->mutex);

  /* Someone is holding a reference to us, but isn't using us so to avoid
   * unnecessary clock cycle wasting, sit in a conditional wait until woken.
   */
  while (context->state == GSTCURL_MULTI_LOOP_STATE_WAIT) {
    GSTCURL_DEBUG_PRINT ("Entering wait state...");
    g_cond_wait (&context->signal, &context->mutex);
    GSTCURL_DEBUG_PRINT ("Received wake up call!");
  }

  if (context->state == GSTCURL_MULTI_LOOP_STATE_QUEUE_EVENT) {
    GSTCURL_DEBUG_PRINT ("Received a new item on the queue!");
    if (context->queue == NULL) {
      GSTCURL_ERROR_PRINT ("Request Queue was empty on a Queue Event!");
      context->state = GSTCURL_MULTI_LOOP_STATE_WAIT;
      return;
    }

    /*
     * Use the running mutex to lock access to each element, as the
     * mutex's memory barriers stop cache optimisations from meaning
     * flag values can't be trusted. The trylock will only let us in
     * once and should fail immediately prior.
     */
    qelement = context->queue;
    while (qelement != NULL) {
      if (g_mutex_trylock (&qelement->running) == TRUE) {
        GSTCURL_DEBUG_PRINT ("Adding easy handle for URI %s", qelement->p->uri);
        cond = TRUE;
        curl_multi_add_handle (context->multi_handle, qelement->p->curl_handle);
      }
      qelement = qelement->next;
    }

    if (cond != TRUE) {
      GSTCURL_WARNING_PRINT ("All curl handles already added for QUEUE_EVENT!");
    } else {
      GSTCURL_DEBUG_PRINT ("Finished adding all handles, continuing.");
      context->state = GSTCURL_MULTI_LOOP_STATE_RUNNING;
    }
    g_mutex_unlock (&context->mutex);
  } else if (context->state == GSTCURL_MULTI_LOOP_STATE_RUNNING) {
    struct timeval timeout;
    gint rc;
    fd_set fdread, fdwrite, fdexcep;
    int maxfd = -1;
    long curl_timeo = -1;

    /* Because curl can possibly take some time here, be nice and let go of the
     * mutex so other threads can perform state/queue operations as we don't
     * care about those until the end of this. */
    g_mutex_unlock (&context->mutex);

    FD_ZERO (&fdread);
    FD_ZERO (&fdwrite);
    FD_ZERO (&fdexcep);

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    curl_multi_timeout (context->multi_handle, &curl_timeo);
    if (curl_timeo >= 0) {
      timeout.tv_sec = curl_timeo / 1000;
      if (timeout.tv_sec > 1) {
        timeout.tv_sec = 1;
      } else {
        timeout.tv_usec = (curl_timeo % 1000) * 1000;
      }
    }

    /* get file descriptors from the transfers */
    curl_multi_fdset (context->multi_handle, &fdread, &fdwrite, &fdexcep,
        &maxfd);

    rc = select (maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);

    switch (rc) {
      case -1:
        /* select error */
        break;
      case 0:
      default:
        /* timeout or readable/writable sockets */
        curl_multi_perform (context->multi_handle, &still_running);
        break;
    }

    /*
     * Check the CURL message buffer to find out if any transfers have
     * completed. If they have, call the signal_finished function which
     * will signal the g_cond_wait call in that calling instance.
     */
    i = 0;
    while (cond != TRUE) {
      curl_message = curl_multi_info_read (context->multi_handle, &i);
      if (curl_message == NULL) {
        cond = TRUE;
      } else if (curl_message->msg == CURLMSG_DONE) {
        /* A hack, but I have seen curl_message->easy_handle being
         * NULL randomly, so check for that. */
        g_mutex_lock (&context->mutex);
        if (curl_message->easy_handle == NULL) {
          break;
        }
        curl_multi_remove_handle (context->multi_handle,
            curl_message->easy_handle);
        gst_curl_http_src_remove_queue_handle (&context->queue,
            curl_message->easy_handle, curl_message->data.result);
        g_mutex_unlock (&context->mutex);
      }
    }

    if (still_running == 0) {
      /* We've finished processing, so set the state to wait.
       *
       * This is a little more complex, as we need to catch the edge
       * case of another thread adding a queue item while we've been
       * working.
       */
      g_mutex_lock (&context->mutex);
      if ((context->state != GSTCURL_MULTI_LOOP_STATE_QUEUE_EVENT) &&
          (context->state != GSTCURL_MULTI_LOOP_STATE_REQUEST_REMOVAL)) {
        context->state = GSTCURL_MULTI_LOOP_STATE_WAIT;
      }
      g_mutex_unlock (&context->mutex);
    }
  }
  /* Is the following even necessary any more...? */
  else if (context->state == GSTCURL_MULTI_LOOP_STATE_STOP) {
    g_mutex_unlock (&context->mutex);
    /* Something wants us to shut down, so best to do a full cleanup as it
     * might be that something's gone bang.
     */
    /*gst_curl_http_src_unref_multi (NULL, GSTCURL_RETURN_PIPELINE_NULL, TRUE); */
    GSTCURL_INFO_PRINT ("Got instruction to shut down");
  } else if (context->state == GSTCURL_MULTI_LOOP_STATE_REQUEST_REMOVAL) {
    qelement = context->queue;
    while (qelement != NULL) {
      if (qelement->p == context->request_removal_element) {
        g_mutex_lock (&qelement->p->buffer_mutex);
        curl_multi_remove_handle (context->multi_handle,
            context->request_removal_element->curl_handle);
        if (qelement->p->state == GSTCURL_UNLOCK) {
          qelement->p->pending_state = GSTCURL_REMOVED;
        } else {
          qelement->p->state = GSTCURL_REMOVED;
        }
        g_cond_signal (&qelement->p->signal);
        g_mutex_unlock (&qelement->p->buffer_mutex);
        gst_curl_http_src_remove_queue_item (&context->queue, qelement->p);
      }
      qelement = qelement->next;
    }
    context->request_removal_element = NULL;
    context->state = GSTCURL_MULTI_LOOP_STATE_RUNNING;
    g_mutex_unlock (&context->mutex);
  } else {
    GSTCURL_WARNING_PRINT ("Curl Loop State was invalid or unsupported");
    GSTCURL_WARNING_PRINT ("Signal State is %d, resetting to RUNNING.",
        context->state);
    /* Reset to running, so if there isn't anything to do it'll be
     * changed the WAIT once curl_multi_perform says it has no active
     * handles. */
    context->state = GSTCURL_MULTI_LOOP_STATE_RUNNING;
    g_mutex_unlock (&context->mutex);
  }
}

/*
 * Receive headers from the remote server and put them into the http_headers
 * structure to be sent downstream when we've got them all and started receiving
 * the body (see ::_handle_response())
 */
static size_t
gst_curl_http_src_get_header (void *header, size_t size, size_t nmemb,
    void *src)
{
  GstCurlHttpSrc *s = src;
  char *substr;

  GST_DEBUG_OBJECT (s, "Received header: %s", (char *) header);

  g_mutex_lock (&s->buffer_mutex);

  if (s->state == GSTCURL_UNLOCK) {
    g_mutex_unlock (&s->buffer_mutex);
    return size * nmemb;
  }

  if (s->http_headers == NULL) {
    /* Can't do anything here, so just silently swallow the header */
    GST_DEBUG_OBJECT (s, "HTTP Headers Structure has already been sent,"
        " ignoring header");
    g_mutex_unlock (&s->buffer_mutex);
    return size * nmemb;
  }

  substr = gst_curl_http_src_strcasestr (header, "HTTP");
  if (substr == header) {
    /* We have a status line! */
    gchar **status_line_fields;

    /* Have we already seen a status line? If so, delete any response headers */
    if (s->status_code > 0) {
      GstStructure *empty_headers =
          gst_structure_new_empty (RESPONSE_HEADERS_NAME);
      gst_structure_remove_field (s->http_headers, RESPONSE_HEADERS_NAME);
      gst_structure_set (s->http_headers, RESPONSE_HEADERS_NAME,
          GST_TYPE_STRUCTURE, empty_headers, NULL);
      gst_structure_free (empty_headers);

    }

    /* Process the status line */
    status_line_fields = g_strsplit ((gchar *) header, " ", 3);
    if (status_line_fields == NULL) {
      GST_ERROR_OBJECT (s, "Status line processing failed!");
    } else {
      s->status_code =
          (guint) g_ascii_strtoll (status_line_fields[1], NULL, 10);
      GST_INFO_OBJECT (s, "Received status %u for request for URI %s: %s",
          s->status_code, s->uri, status_line_fields[2]);
      gst_structure_set (s->http_headers, HTTP_STATUS_CODE,
          G_TYPE_UINT, s->status_code, NULL);
      g_strfreev (status_line_fields);
    }
  } else {
    /* Normal header line */
    gchar **header_tpl = g_strsplit ((gchar *) header, ": ", 2);
    if (header_tpl == NULL) {
      GST_ERROR_OBJECT (s, "Header processing failed! (%s)", (gchar *) header);
    } else {
      const GValue *gv_resp_hdrs = gst_structure_get_value (s->http_headers,
          RESPONSE_HEADERS_NAME);
      const GstStructure *response_headers =
          gst_value_get_structure (gv_resp_hdrs);
      /* Store header key lower case (g_ascii_strdown), makes searching through
       * later on easier - end applications shouldn't care, as all HTTP headers
       * are case-insensitive */
      gchar *header_key = g_ascii_strdown (header_tpl[0], -1);
      gchar *header_value;

      /* If header field already exists, append to the end */
      if (gst_structure_has_field (response_headers, header_key) == TRUE) {
        header_value = g_strdup_printf ("%s, %s",
            g_value_get_string (gst_structure_get_value (response_headers,
                    header_key)), header_tpl[1]);
        gst_structure_set ((GstStructure *) response_headers, header_key,
            G_TYPE_STRING, header_value, NULL);
        g_free (header_value);
      } else {
        header_value = header_tpl[1];
        gst_structure_set ((GstStructure *) response_headers, header_key,
            G_TYPE_STRING, header_value, NULL);
      }

      /* We have some special cases - deal with them here */
      if (g_strcmp0 (header_key, "content-type") == 0) {
        gst_curl_http_src_negotiate_caps (src);
      }

      g_free (header_key);
      g_strfreev (header_tpl);
    }
  }

  s->hdrs_updated = TRUE;

  g_mutex_unlock (&s->buffer_mutex);

  return size * nmemb;
}

/*
 * My own quick and dirty implementation of strcasestr. This is a GNU extension
 * (i.e. not portable) and not always guaranteed to be available.
 *
 * I know this doesn't work if the haystack and needle are the same size. But
 * this isn't necessarily a bad thing, as the only place we currently use this
 * is at a point where returning nothing even if a string match occurs but the
 * needle is the same size as the haystack actually saves us time.
 */
static char *
gst_curl_http_src_strcasestr (const char *haystack, const char *needle)
{
  int i, j, needle_len;
  char *location;

  needle_len = (int) strlen (needle);
  i = 0;
  j = 0;
  location = NULL;

  while (haystack[i] != '\0') {
    if (j == needle_len) {
      location = (char *) haystack + (i - j);
    }
    if (tolower (haystack[i]) == tolower (needle[j])) {
      j++;
    } else {
      j = 0;
    }
    i++;
  }

  return location;
}

/*
 * Receive chunks of the requested body and pass these back to the ::create()
 * loop
 */
static size_t
gst_curl_http_src_get_chunks (void *chunk, size_t size, size_t nmemb, void *src)
{
  GstCurlHttpSrc *s = src;
  size_t chunk_len = size * nmemb;
  GST_TRACE_OBJECT (s,
      "Received curl chunk for URI %s of size %d", s->uri, (int) chunk_len);
  g_mutex_lock (&s->buffer_mutex);
  if (s->state == GSTCURL_UNLOCK) {
    g_mutex_unlock (&s->buffer_mutex);
    return chunk_len;
  }
  s->buffer =
      g_realloc (s->buffer, (s->buffer_len + chunk_len + 1) * sizeof (char));
  if (s->buffer == NULL) {
    GST_ERROR_OBJECT (s, "Realloc for cURL response message failed!\n");
    return 0;
  }
  memcpy (s->buffer + s->buffer_len, chunk, chunk_len);
  s->buffer_len += chunk_len;
  g_cond_signal (&s->signal);
  g_mutex_unlock (&s->buffer_mutex);
  return chunk_len;
}

/*
 * Request a cancellation of a currently running curl handle.
 */
static void
gst_curl_http_src_request_remove (GstCurlHttpSrc * src)
{
  GstCurlHttpSrcClass *klass = G_TYPE_INSTANCE_GET_CLASS (src,
      GST_TYPE_CURL_HTTP_SRC,
      GstCurlHttpSrcClass);
  g_mutex_lock (&klass->multi_task_context.mutex);

  klass->multi_task_context.state = GSTCURL_MULTI_LOOP_STATE_REQUEST_REMOVAL;
  klass->multi_task_context.request_removal_element = src;
  g_cond_signal (&klass->multi_task_context.signal);
  g_mutex_unlock (&klass->multi_task_context.mutex);
}
