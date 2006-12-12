/* GStreamer
 * Copyright (C) <2005> Edgard Lima <edgard.lima@indt.org.br>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstneonhttpsrc.h"
#include <string.h>
#include <unistd.h>

#include <ne_redirect.h>

#ifndef NE_FREE
#define NEON_026_OR_LATER  1
#endif

#define HTTP_DEFAULT_HOST        "localhost"
#define HTTP_DEFAULT_PORT        80
#define HTTPS_DEFAULT_PORT       443

GST_DEBUG_CATEGORY_STATIC (neonhttpsrc_debug);
#define GST_CAT_DEFAULT neonhttpsrc_debug

#define MAX_READ_SIZE                   (4 * 1024)

/* max number of HTTP redirects, when iterating over a sequence of HTTP 302 status code */
#define MAX_HTTP_REDIRECTS_NUMBER	5

static const GstElementDetails gst_neonhttp_src_details =
GST_ELEMENT_DETAILS ("HTTP client source",
    "Source/Network",
    "Receive data as a client over the network via HTTP using NEON",
    "Edgard Lima <edgard.lima@indt.org.br>");

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_URI,
  PROP_PROXY,
  PROP_USER_AGENT,
  PROP_IRADIO_MODE,
  PROP_IRADIO_NAME,
  PROP_IRADIO_GENRE,
  PROP_IRADIO_URL,
  PROP_NEON_HTTP_REDIRECT
#ifndef GST_DISABLE_GST_DEBUG
      , PROP_NEON_HTTP_DBG
#endif
};

static void oom_callback ();

static gboolean set_proxy (GstNeonhttpSrc * src, const char *uri,
    ne_uri * parsed, gboolean set_default);
static gboolean set_uri (GstNeonhttpSrc * src, const char *uri, ne_uri * parsed,
    gboolean * ishttps, gchar ** uristr, gboolean set_default);

static void gst_neonhttp_src_finalize (GObject * gobject);

static GstFlowReturn gst_neonhttp_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_neonhttp_src_start (GstBaseSrc * bsrc);
static gboolean gst_neonhttp_src_stop (GstBaseSrc * bsrc);
static gboolean gst_neonhttp_src_get_size (GstBaseSrc * bsrc, guint64 * size);

static void gst_neonhttp_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_neonhttp_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_neonhttp_src_uri_handler_init (gpointer g_iface, gpointer iface_data);

static void
_urihandler_init (GType type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_neonhttp_src_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (type, GST_TYPE_URI_HANDLER, &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (neonhttpsrc_debug, "neonhttpsrc", 0,
      "NEON HTTP src");
}

GST_BOILERPLATE_FULL (GstNeonhttpSrc, gst_neonhttp_src, GstPushSrc,
    GST_TYPE_PUSH_SRC, _urihandler_init);

static void
gst_neonhttp_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_details (element_class, &gst_neonhttp_src_details);
}

static void
gst_neonhttp_src_class_init (GstNeonhttpSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpush_src_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpush_src_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_neonhttp_src_set_property;
  gobject_class->get_property = gst_neonhttp_src_get_property;
  gobject_class->finalize = gst_neonhttp_src_finalize;

  g_object_class_install_property
      (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "The location. In the form:"
          "\n\t\t\thttp://a.com/file.txt - default port '80' "
          "\n\t\t\thttp://a.com:80/file.txt "
          "\n\t\t\ta.com/file.txt - defualt scheme 'HTTP' "
          "\n\t\t\thttps://a.com/file.txt - default port '443' "
          "\n\t\t\thttp:///file.txt - default host '" HTTP_DEFAULT_HOST "'",
          "", G_PARAM_READWRITE));

  g_object_class_install_property
      (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "Uri",
          "The location in form of a URI (deprecated; use location)",
          "", G_PARAM_READWRITE));

  g_object_class_install_property
      (gobject_class, PROP_PROXY,
      g_param_spec_string ("proxy", "Proxy",
          "The proxy. In the form myproxy.mycompany.com:8080. "
          "\n\t\t\tIf nothing is passed g_getenv(\"http_proxy\") will be used "
          "\n\t\t\tIf that http_proxy enviroment var isn't define no proxy is used",
          "", G_PARAM_READWRITE));

  g_object_class_install_property
      (gobject_class, PROP_USER_AGENT,
      g_param_spec_string ("user-agent", "User-Agent",
          "The User-Agent used for connection.",
          "neonhttpsrc", G_PARAM_READWRITE));

  g_object_class_install_property
      (gobject_class, PROP_IRADIO_MODE,
      g_param_spec_boolean ("iradio-mode", "iradio-mode",
          "Enable internet radio mode (extraction of shoutcast/icecast metadata)",
          FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_IRADIO_NAME,
      g_param_spec_string ("iradio-name",
          "iradio-name", "Name of the stream", NULL, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class,
      PROP_IRADIO_GENRE,
      g_param_spec_string ("iradio-genre",
          "iradio-genre", "Genre of the stream", NULL, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class,
      PROP_IRADIO_URL,
      g_param_spec_string ("iradio-url",
          "iradio-url",
          "Homepage URL for radio stream", NULL, G_PARAM_READABLE));

  g_object_class_install_property
      (gobject_class, PROP_NEON_HTTP_REDIRECT,
      g_param_spec_boolean ("automatic-redirect", "automatic-redirect",
          "Enable Neon HTTP Redirects (HTTP Status Code 302)",
          FALSE, G_PARAM_READWRITE));

#ifndef GST_DISABLE_GST_DEBUG
  g_object_class_install_property
      (gobject_class, PROP_NEON_HTTP_DBG,
      g_param_spec_boolean ("neon-http-debug", "neon-http-debug",
          "Enable Neon HTTP debug messages", FALSE, G_PARAM_READWRITE));
#endif

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_neonhttp_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_neonhttp_src_stop);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_neonhttp_src_get_size);

  gstpush_src_class->create = GST_DEBUG_FUNCPTR (gst_neonhttp_src_create);

  GST_DEBUG_CATEGORY_INIT (neonhttpsrc_debug, "neonhttpsrc", 0,
      "NEON HTTP Client Source");
}

static void
gst_neonhttp_src_init (GstNeonhttpSrc * this, GstNeonhttpSrcClass * g_class)
{
  this->session = NULL;
  this->request = NULL;

  memset (&this->uri, 0, sizeof (this->uri));
  this->uristr = NULL;
  memset (&this->proxy, 0, sizeof (this->proxy));
  this->ishttps = FALSE;
  this->content_size = -1;

  set_uri (this, NULL, &this->uri, &this->ishttps, &this->uristr, TRUE);
  set_proxy (this, NULL, &this->proxy, TRUE);

  this->user_agent = g_strdup ("neonhttpsrc");

  this->iradio_mode = FALSE;
  this->iradio_name = NULL;
  this->iradio_genre = NULL;
  this->iradio_url = NULL;
  this->icy_caps = NULL;
  this->icy_metaint = 0;
}

static void
gst_neonhttp_src_finalize (GObject * gobject)
{
  GstNeonhttpSrc *this = GST_NEONHTTP_SRC (gobject);

  ne_uri_free (&this->uri);
  ne_uri_free (&this->proxy);

  g_free (this->user_agent);
  g_free (this->iradio_name);
  g_free (this->iradio_genre);
  g_free (this->iradio_url);

  if (this->icy_caps) {
    gst_caps_unref (this->icy_caps);
    this->icy_caps = NULL;
  }

  if (this->request) {
    ne_request_destroy (this->request);
    this->request = NULL;
  }

  if (this->session) {
    ne_close_connection (this->session);
    ne_session_destroy (this->session);
    this->session = NULL;
  }

  if (this->uristr) {
    ne_free (this->uristr);
  }

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static int
request_dispatch (GstNeonhttpSrc * src, GstBuffer * outbuf)
{
  int ret;
  int read = 0;
  int sizetoread = GST_BUFFER_SIZE (outbuf);

  /* Loop sending the request:
   * Retry whilst authentication fails and we supply it. */

  ssize_t len = 0;

  while (sizetoread > 0) {
    len = ne_read_response_block (src->request,
        (char *) GST_BUFFER_DATA (outbuf) + read, sizetoread);
    if (len > 0) {
      read += len;
      sizetoread -= len;
    } else {
      break;
    }

  }

  GST_BUFFER_SIZE (outbuf) = read;

  if (len < 0) {
    read = -2;
    goto done;
  } else if (len == 0) {
    ret = ne_end_request (src->request);
    if (ret != NE_RETRY) {
      if (ret == NE_OK) {
        src->eos = TRUE;
      } else {
        read = -3;
      }
    }
    goto done;
  }

done:
  return read;
}


static GstFlowReturn
gst_neonhttp_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstNeonhttpSrc *src;
  GstBaseSrc *basesrc;
  GstFlowReturn ret;
  int read;

  src = GST_NEONHTTP_SRC (psrc);
  basesrc = GST_BASE_SRC_CAST (psrc);

  /* The caller should know the number of bytes and not read beyond EOS. */
  if (G_UNLIKELY (src->eos))
    goto eos;

  /* Create the buffer. */
  ret = gst_pad_alloc_buffer (GST_BASE_SRC_PAD (basesrc),
      basesrc->segment.last_stop, basesrc->blocksize,
      src->icy_caps ? src->icy_caps :
      GST_PAD_CAPS (GST_BASE_SRC_PAD (basesrc)), outbuf);

  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto done;

  read = request_dispatch (src, *outbuf);
  if (G_UNLIKELY (read < 0))
    goto read_error;

done:
  return ret;

  /* ERRORS */
eos:
  {
    GST_DEBUG_OBJECT (src, "EOS reached");
    return GST_FLOW_UNEXPECTED;
  }
read_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        (NULL), ("Could not read any bytes (%i, %s)", read,
            ne_get_error (src->session)));
    return GST_FLOW_ERROR;
  }
}

/* The following two charset mangling functions were copied from gnomevfssrc.
 * Preserve them under the unverified assumption that they do something vaguely
 * worthwhile.
 */
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
gst_neonhttp_src_unicodify (const char *str)
{
  return unicodify (str, -1, "locale", "ISO-8859-1", NULL);
}

#define HTTP_SOCKET_ERROR		-2
#define HTTP_REQUEST_WRONG_PROXY	-1

/**
 * Try to send the HTTP request to the Icecast server, and if possible deals with
 * all the probable redirections (HTTP status code == 302)
 */
static gint
send_request_and_redirect (GstNeonhttpSrc * src, gboolean do_redir)
{
  gint res;
  gint http_status = 0;

  const gchar *redir = g_strdup ("");

  guint request_count = 0;

#ifndef GST_DISABLE_GST_DEBUG
  if (src->neon_http_msgs_dbg)
    ne_debug_init (stderr, NE_DBG_HTTP);
#endif

  ne_oom_callback (oom_callback);

  if ((res = ne_sock_init ()) != 0)
    return HTTP_SOCKET_ERROR;

  do {

    src->session =
        ne_session_create (src->uri.scheme, src->uri.host, src->uri.port);

    if (src->proxy.host && src->proxy.port) {
      ne_session_proxy (src->session, src->proxy.host, src->proxy.port);
    } else if (src->proxy.host || src->proxy.port) {
      /* both proxy host and port must be specified or none */
      return HTTP_REQUEST_WRONG_PROXY;
    }

    src->request = ne_request_create (src->session, "GET", src->uri.path);

    if (src->user_agent) {
      ne_add_request_header (src->request, "User-Agent", src->user_agent);
    }

    if (src->iradio_mode) {
      ne_add_request_header (src->request, "icy-metadata", "1");
    }

    res = ne_begin_request (src->request);

    if (res == NE_OK) {
      /* When the HTTP status code is 302, it is not the SHOUTcast streaming content yet;
       * Reload the HTTP request with a new URI value */
      http_status = ne_get_status (src->request)->code;
      if (http_status == 302) {
        /* the new URI value to go when redirecting can be found on the 'Location' HTTP header */
        redir = ne_get_response_header (src->request, "Location");
        if (redir != NULL) {
          ne_uri_free (&src->uri);
          set_uri (src, redir, &src->uri, &src->ishttps, &src->uristr, FALSE);
#ifndef GST_DISABLE_GST_DEBUG
          if (src->neon_http_msgs_dbg)
            GST_LOG_OBJECT (src,
                "--> Got HTTP Status Code %d; Using 'Location' header [%s]",
                http_status, src->uri.host);
#endif
        }

      }
      /* if - http_status == 302 */
    }
    /* if - NE_OK */
    ++request_count;
    if (http_status == 302) {
      GST_WARNING_OBJECT (src, "%s %s.",
          (request_count < MAX_HTTP_REDIRECTS_NUMBER)
          && do_redir ? "Redirecting to" :
          "WILL NOT redirect, try it again with a different URI; an alternative is",
          src->uri.host);
      /* FIXME: when not redirecting automatically, shouldn't we post a
       * redirect element message on the bus? */
    }
#ifndef GST_DISABLE_GST_DEBUG
    if (src->neon_http_msgs_dbg)
      GST_LOG_OBJECT (src, "--> request_count = %d", request_count);
#endif

    /* do the redirect, go back to send another HTTP request now using the 'Location' */
  } while (do_redir && (request_count < MAX_HTTP_REDIRECTS_NUMBER)
      && http_status == 302);

  return res;

}

/* create a socket for connecting to remote server */
static gboolean
gst_neonhttp_src_start (GstBaseSrc * bsrc)
{
  GstNeonhttpSrc *src = GST_NEONHTTP_SRC (bsrc);
  const char *content_length;

  gint res = send_request_and_redirect (src, src->neon_http_redirect);

  if (res != NE_OK) {
    if (res == HTTP_SOCKET_ERROR) {
#ifndef GST_DISABLE_GST_DEBUG
      if (src->neon_http_msgs_dbg) {
        GST_ERROR_OBJECT (src, "HTTP Request failed when opening socket!");
      }
#endif
      goto init_failed;
    } else if (res == HTTP_REQUEST_WRONG_PROXY) {
#ifndef GST_DISABLE_GST_DEBUG
      if (src->neon_http_msgs_dbg) {
        GST_ERROR_OBJECT (src,
            "Proxy Server URI is invalid to the HTTP Request!");
      }
#endif
      goto wrong_proxy;
    } else {
#ifndef GST_DISABLE_GST_DEBUG
      if (src->neon_http_msgs_dbg) {
        GST_ERROR_OBJECT (src, "HTTP Request failed, error unrecognized!");
      }
#endif
      goto begin_req_failed;
    }
  }

  content_length = ne_get_response_header (src->request, "Content-Length");

  if (content_length)
    src->content_size = g_ascii_strtoull (content_length, NULL, 10);
  else
    src->content_size = -1;

  if (src->iradio_mode) {
    /* Icecast stuff */
    const char *str_value;
    gint gint_value;

    str_value = ne_get_response_header (src->request, "icy-metaint");
    if (str_value) {
      if (sscanf (str_value, "%d", &gint_value) == 1) {
        if (src->icy_caps) {
          gst_caps_unref (src->icy_caps);
          src->icy_caps = NULL;
        }
        src->icy_metaint = gint_value;
        src->icy_caps = gst_caps_new_simple ("application/x-icy",
            "metadata-interval", G_TYPE_INT, src->icy_metaint, NULL);
      }
    }

    str_value = ne_get_response_header (src->request, "icy-name");
    if (str_value) {
      if (src->iradio_name) {
        g_free (src->iradio_name);
        src->iradio_name = NULL;
      }
      src->iradio_name = gst_neonhttp_src_unicodify (str_value);
    }
    str_value = ne_get_response_header (src->request, "icy-genre");
    if (str_value) {
      if (src->iradio_genre) {
        g_free (src->iradio_genre);
        src->iradio_genre = NULL;
      }
      src->iradio_genre = gst_neonhttp_src_unicodify (str_value);
    }
    str_value = ne_get_response_header (src->request, "icy-url");
    if (str_value) {
      if (src->iradio_url) {
        g_free (src->iradio_url);
        src->iradio_url = NULL;
      }
      src->iradio_url = gst_neonhttp_src_unicodify (str_value);
    }
  }

  return TRUE;

  /* ERRORS */
init_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        (NULL), ("Could not initialize neon library (%i)", res));
    return FALSE;
  }
wrong_proxy:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        (NULL), ("Both proxy host and port must be specified or none"));
    return FALSE;
  }
begin_req_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        (NULL), ("Could not begin request (%i)", res));
    return FALSE;
  }
}

static gboolean
gst_neonhttp_src_get_size (GstBaseSrc * bsrc, guint64 * size)
{
  GstNeonhttpSrc *src;

  src = GST_NEONHTTP_SRC (bsrc);

  if (src->content_size != -1)
    return FALSE;

  *size = src->content_size;

  return TRUE;
}

/* close the socket and associated resources
 * used both to recover from errors and go to NULL state */
static gboolean
gst_neonhttp_src_stop (GstBaseSrc * bsrc)
{
  GstNeonhttpSrc *src;

  src = GST_NEONHTTP_SRC (bsrc);

  if (src->iradio_name) {
    g_free (src->iradio_name);
    src->iradio_name = NULL;
  }

  if (src->iradio_genre) {
    g_free (src->iradio_genre);
    src->iradio_genre = NULL;
  }

  if (src->iradio_url) {
    g_free (src->iradio_url);
    src->iradio_url = NULL;
  }

  if (src->icy_caps) {
    gst_caps_unref (src->icy_caps);
    src->icy_caps = NULL;
  }

  if (src->request) {
    ne_request_destroy (src->request);
    src->request = NULL;
  }

  if (src->session) {
    ne_close_connection (src->session);
    ne_session_destroy (src->session);
    src->session = NULL;
  }

  src->eos = FALSE;

  return TRUE;
}

static gboolean
set_proxy (GstNeonhttpSrc * src, const char *uri, ne_uri * parsed,
    gboolean set_default)
{
  ne_uri_free (parsed);

  if (set_default) {
    const char *str = g_getenv ("http_proxy");

    if (str) {
      if (ne_uri_parse (str, parsed) != 0)
        goto cannot_parse;
    }
    return TRUE;
  }

  if (ne_uri_parse (uri, parsed) != 0)
    goto error;

  if (parsed->scheme)
    GST_WARNING ("The proxy schema shouldn't be defined");

  if (parsed->host && !parsed->port)
    goto error;

#ifdef NEON_026_OR_LATER
  if (!parsed->path || parsed->userinfo)
    goto error;
#else
  if (!parsed->path || parsed->authinfo)
    goto error;
#endif
  return TRUE;

  /* ERRORS */
error:
  {
    ne_uri_free (parsed);
    return FALSE;
  }
cannot_parse:
  {
    GST_WARNING_OBJECT (src,
        "The proxy set on http_proxy env var isn't well formated");
    ne_uri_free (parsed);
    return TRUE;
  }
}

static gboolean
set_uri (GstNeonhttpSrc * src, const char *uri, ne_uri * parsed,
    gboolean * ishttps, gchar ** uristr, gboolean set_default)
{

  ne_uri_free (parsed);
  if (uristr && *uristr) {
    ne_free (*uristr);
    *uristr = NULL;
  }

  if (set_default) {
    parsed->scheme = g_strdup ("http");
    parsed->host = g_strdup (HTTP_DEFAULT_HOST);
    parsed->port = HTTP_DEFAULT_PORT;
    parsed->path = g_strdup ("/");
    *ishttps = FALSE;
    goto done;
  }

  if (ne_uri_parse (uri, parsed) != 0)
    goto parse_error;

  if (parsed->scheme == NULL) {
    parsed->scheme = g_strdup ("http");
    *ishttps = FALSE;
  } else {
    if (strcmp (parsed->scheme, "https") == 0)
      *ishttps = TRUE;
    else
      *ishttps = FALSE;
  }

  if (parsed->host == NULL)
    parsed->host = g_strdup (HTTP_DEFAULT_HOST);

  if (parsed->port == 0) {
    if (*ishttps)
      parsed->port = HTTPS_DEFAULT_PORT;
    else
      parsed->port = HTTP_DEFAULT_PORT;
  }

  if (!parsed->path)
    parsed->path = g_strdup ("");

done:
  if (uristr)
    *uristr = ne_uri_unparse (parsed);

  return TRUE;

  /* ERRORS */
parse_error:
  {
    if (uristr && *uristr) {
      ne_free (*uristr);
      *uristr = NULL;
    }
    ne_uri_free (parsed);
    return FALSE;
  }
}

static void
gst_neonhttp_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNeonhttpSrc *this = GST_NEONHTTP_SRC (object);

  switch (prop_id) {
    case PROP_PROXY:
    {
      if (!g_value_get_string (value)) {
        GST_WARNING ("proxy property cannot be NULL");
        goto done;
      }
      if (!set_proxy (this, g_value_get_string (value), &this->proxy, FALSE)) {
        GST_WARNING ("badly formated proxy");
        goto done;
      }
      break;
    }
    case PROP_URI:
    case PROP_LOCATION:
    {
      if (!g_value_get_string (value)) {
        GST_WARNING ("location property cannot be NULL");
        goto done;
      }
      if (!set_uri (this, g_value_get_string (value), &this->uri,
              &this->ishttps, &this->uristr, FALSE)) {
        GST_WARNING ("badly formated location");
        goto done;
      }
      break;
    }
    case PROP_USER_AGENT:
    {
      if (this->user_agent) {
        g_free (this->user_agent);
        this->user_agent = NULL;
      }
      if (g_value_get_string (value)) {
        this->user_agent = g_strdup (g_value_get_string (value));
      }
      break;
    }
    case PROP_IRADIO_MODE:
    {
      this->iradio_mode = g_value_get_boolean (value);
      break;
    }
    case PROP_NEON_HTTP_REDIRECT:
    {
      this->neon_http_redirect = g_value_get_boolean (value);
      break;
    }
#ifndef GST_DISABLE_GST_DEBUG
    case PROP_NEON_HTTP_DBG:
    {
      this->neon_http_msgs_dbg = g_value_get_boolean (value);
      break;
    }
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
done:
  return;
}

static void
gst_neonhttp_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNeonhttpSrc *neonhttpsrc = GST_NEONHTTP_SRC (object);

  switch (prop_id) {
    case PROP_PROXY:
    {
      char *str;

      if (neonhttpsrc->proxy.host) {
        str = ne_uri_unparse (&neonhttpsrc->proxy);
        if (!str)
          break;
        g_value_set_string (value, str);
        ne_free (str);
      } else {
        g_value_set_string (value, "");
      }
      break;
    }
    case PROP_URI:
    case PROP_LOCATION:
    {
      char *str;

      if (neonhttpsrc->uri.host) {
        str = ne_uri_unparse (&neonhttpsrc->uri);
        if (!str)
          break;
        g_value_set_string (value, str);
        ne_free (str);
      } else {
        g_value_set_string (value, "");
      }
      break;
    }
    case PROP_USER_AGENT:
    {
      g_value_set_string (value, neonhttpsrc->user_agent);
      break;
    }
    case PROP_IRADIO_MODE:
      g_value_set_boolean (value, neonhttpsrc->iradio_mode);
      break;
    case PROP_IRADIO_NAME:
      g_value_set_string (value, neonhttpsrc->iradio_name);
      break;
    case PROP_IRADIO_GENRE:
      g_value_set_string (value, neonhttpsrc->iradio_genre);
      break;
    case PROP_IRADIO_URL:
      g_value_set_string (value, neonhttpsrc->iradio_url);
      break;
    case PROP_NEON_HTTP_REDIRECT:
      g_value_set_boolean (value, neonhttpsrc->neon_http_redirect);
      break;
#ifndef GST_DISABLE_GST_DEBUG
    case PROP_NEON_HTTP_DBG:
      g_value_set_boolean (value, neonhttpsrc->neon_http_msgs_dbg);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "neonhttpsrc", GST_RANK_NONE,
      GST_TYPE_NEONHTTP_SRC);
}

/* this is the structure that gst-register looks for
 * so keep the name plugin_desc, or you cannot get your plug-in registered */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "neon",
    "lib neon http client src",
    plugin_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")


/*** GSTURIHANDLER INTERFACE *************************************************/
     static guint gst_neonhttp_src_uri_get_type (void)
{
  return GST_URI_SRC;
}
static gchar **
gst_neonhttp_src_uri_get_protocols (void)
{
  static gchar *protocols[] = { "http", "https", NULL };

  return protocols;
}

static const gchar *
gst_neonhttp_src_uri_get_uri (GstURIHandler * handler)
{
  GstNeonhttpSrc *src = GST_NEONHTTP_SRC (handler);

  return src->uristr;
}

static gboolean
gst_neonhttp_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstNeonhttpSrc *src = GST_NEONHTTP_SRC (handler);

  return set_uri (src, uri, &src->uri, &src->ishttps, &src->uristr, FALSE);

}

static void
gst_neonhttp_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_neonhttp_src_uri_get_type;
  iface->get_protocols = gst_neonhttp_src_uri_get_protocols;
  iface->get_uri = gst_neonhttp_src_uri_get_uri;
  iface->set_uri = gst_neonhttp_src_uri_set_uri;
}


/* NEON CALLBACK */
static void
oom_callback ()
{
  GST_ERROR ("memory exeception in neon");
}

void
size_header_handler (void *userdata, const char *value)
{
  GstNeonhttpSrc *src = GST_NEONHTTP_SRC (userdata);

  src->content_size = g_ascii_strtoull (value, NULL, 10);

  GST_DEBUG_OBJECT (src, "content size = %lld bytes", src->content_size);
}
