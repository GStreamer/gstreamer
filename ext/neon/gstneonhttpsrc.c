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

#define HTTP_DEFAULT_HOST        "localhost"
#define HTTP_DEFAULT_PORT        80
#define HTTPS_DEFAULT_PORT        443

GST_DEBUG_CATEGORY (neonhttpsrc_debug);
#define GST_CAT_DEFAULT neonhttpsrc_debug

#define MAX_READ_SIZE                   (4 * 1024)


static GstElementDetails gst_neonhttp_src_details =
GST_ELEMENT_DETAILS ("NEON HTTP Client source",
    "Source/Network",
    "Receive data as a client over the network via HTTP",
    "Edgard Lima <edgard.lima@indt.org.br>");

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_PROXY
};

static void oom_callback ();

static gboolean set_proxy (const char *uri, ne_uri * parsed,
    gboolean set_default);
static gboolean set_uri (const char *uri, ne_uri * parsed, gboolean * ishttps,
    gchar ** uristr, gboolean set_default);

static void gst_neonhttp_src_finalize (GObject * gobject);

static GstFlowReturn gst_neonhttp_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_neonhttp_src_start (GstBaseSrc * bsrc);
static gboolean gst_neonhttp_src_stop (GstBaseSrc * bsrc);
static gboolean gst_neonhttp_src_unlock (GstBaseSrc * bsrc);
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
      (G_OBJECT_CLASS (klass), PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "The location. In the form:"
          "\n\t\t\thttp://a.com/file.txt - default port '80' "
          "\n\t\t\thttp://a.com:80/file.txt "
          "\n\t\t\ta.com/file.txt - defualt scheme 'HTTP' "
          "\n\t\t\thttps://a.com/file.txt - default port '443' "
          "\n\t\t\thttp:///file.txt - default host '" HTTP_DEFAULT_HOST "'",
          "", G_PARAM_READWRITE));


  g_object_class_install_property
      (G_OBJECT_CLASS (klass), PROP_PROXY,
      g_param_spec_string ("proxy", "Proxy",
          "The proxy. In the form myproxy.mycompany.com:8080. "
          "\n\t\t\tIf nothing is passed g_getenv(\"http_proxy\") will be used "
          "\n\t\t\tIf that http_proxy enviroment var isn't define no proxy is used",
          "", G_PARAM_READWRITE));

  gstbasesrc_class->start = gst_neonhttp_src_start;
  gstbasesrc_class->stop = gst_neonhttp_src_stop;
  gstbasesrc_class->unlock = gst_neonhttp_src_unlock;
  gstbasesrc_class->get_size = gst_neonhttp_src_get_size;

  gstpush_src_class->create = gst_neonhttp_src_create;

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

  set_uri (NULL, &this->uri, &this->ishttps, &this->uristr, TRUE);
  set_proxy (NULL, &this->proxy, TRUE);

  this->adapter = gst_adapter_new ();

  GST_OBJECT_FLAG_UNSET (this, GST_NEONHTTP_SRC_OPEN);
}

static void
gst_neonhttp_src_finalize (GObject * gobject)
{
  GstNeonhttpSrc *this = GST_NEONHTTP_SRC (gobject);

  ne_uri_free (&this->uri);
  ne_uri_free (&this->proxy);


  if (this->request) {
    ne_request_destroy (this->request);
    this->request = NULL;
  }

  if (this->session) {
    ne_close_connection (this->session);
    ne_session_destroy (this->session);
    this->session = NULL;
  }

  if (this->adapter) {
    g_object_unref (this->adapter);
  }

  if (this->uristr) {
    ne_free (this->uristr);
  }

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (gobject);

}

int
request_dispatch (GstNeonhttpSrc * src, GstBuffer * outbuf, gboolean * eos)
{
  int ret;
  int read = 0;
  int sizetoread = GST_BUFFER_SIZE (outbuf);

  /* Loop sending the request:
   * Retry whilst authentication fails and we supply it. */

  ssize_t len = 0;

  while (sizetoread > 0) {

    if (!GST_OBJECT_FLAG_IS_SET (src, GST_NEONHTTP_SRC_OPEN)) {
      GST_BUFFER_SIZE (outbuf) = read;
      ret = ne_end_request (src->request);
      if (ret != NE_OK) {
        GST_ERROR ("Request failed. code:%d, desc: %s\n", ret,
            ne_get_error (src->session));
      }
      return read;
    }
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
        *eos = TRUE;
      } else {
        read = -3;
        GST_ERROR ("Request failed. code:%d, desc: %s\n", ret,
            ne_get_error (src->session));
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
  GstFlowReturn ret = GST_FLOW_OK;
  int read;
  gboolean eos = FALSE;

  src = GST_NEONHTTP_SRC (psrc);

  if (!GST_OBJECT_FLAG_IS_SET (src, GST_NEONHTTP_SRC_OPEN))
    goto wrong_state;

  *outbuf = gst_buffer_new_and_alloc (GST_BASE_SRC (psrc)->blocksize);

  read = request_dispatch (src, *outbuf, &eos);
  GST_LOG ("outbuffer size = %d", read);

  if (read > 0) {

    if (*outbuf) {
      gst_buffer_set_caps (*outbuf, GST_PAD_CAPS (GST_BASE_SRC_PAD (src)));
    }

  } else if (read < 0) {
    ret = GST_FLOW_ERROR;
  }

  if (eos) {
    GstPad *peer;

    GST_DEBUG ("Returning EOS");
    peer = gst_pad_get_peer (GST_BASE_SRC_PAD (src));
    if (!gst_pad_send_event (peer, gst_event_new_eos ())) {
      ret = GST_FLOW_ERROR;
    }
    gst_object_unref (peer);
  }

  return ret;

wrong_state:
  {
    GST_DEBUG ("connection to closed, cannot read data");
    return GST_FLOW_WRONG_STATE;
  }

}

/* create a socket for connecting to remote server */
static gboolean
gst_neonhttp_src_start (GstBaseSrc * bsrc)
{
  gboolean ret = TRUE;
  GstNeonhttpSrc *src = GST_NEONHTTP_SRC (bsrc);
  const char *content_length;

  ne_oom_callback (oom_callback);

  if (0 != ne_sock_init ()) {
    ret = FALSE;
    goto done;
  }

  src->session =
      ne_session_create (src->uri.scheme, src->uri.host, src->uri.port);

  if (src->proxy.host && src->proxy.port) {
    ne_session_proxy (src->session, src->proxy.host, src->proxy.port);
  } else if (src->proxy.host || src->proxy.port) {
    /* both proxy host and port must be specified or none */
    ret = FALSE;
    goto done;
  }

  src->request = ne_request_create (src->session, "GET", src->uri.path);

  if (NE_OK != ne_begin_request (src->request)) {
    ret = FALSE;
    goto done;
  }

  content_length = ne_get_response_header (src->request, "Content-Length");

  if (content_length) {
    src->content_size = atoll (content_length);
  } else {
    src->content_size = -1;
  }

  GST_OBJECT_FLAG_SET (src, GST_NEONHTTP_SRC_OPEN);

done:

  return ret;

}

static gboolean
gst_neonhttp_src_get_size (GstBaseSrc * bsrc, guint64 * size)
{
  GstNeonhttpSrc *src;

  src = GST_NEONHTTP_SRC (bsrc);

  if (src->content_size != -1) {
    *size = src->content_size;
    return TRUE;
  }

  return FALSE;

}

static gboolean
gst_neonhttp_src_unlock (GstBaseSrc * bsrc)
{
  GstNeonhttpSrc *src;

  src = GST_NEONHTTP_SRC (bsrc);

  GST_OBJECT_FLAG_UNSET (src, GST_NEONHTTP_SRC_OPEN);

  return TRUE;

}


/* close the socket and associated resources
 * unset OPEN flag
 * used both to recover from errors and go to NULL state */
static gboolean
gst_neonhttp_src_stop (GstBaseSrc * bsrc)
{
  GstNeonhttpSrc *src;

  src = GST_NEONHTTP_SRC (bsrc);

  if (src->request) {
    ne_request_destroy (src->request);
    src->request = NULL;
  }

  if (src->session) {
    ne_close_connection (src->session);
    ne_session_destroy (src->session);
    src->session = NULL;
  }

  return TRUE;
}

gboolean
set_proxy (const char *uri, ne_uri * parsed, gboolean set_default)
{
  ne_uri_free (parsed);

  if (set_default) {
    const char *str = g_getenv ("http_proxy");

    if (str) {
      if (0 != ne_uri_parse (str, parsed)) {
        GST_WARNING ("The proxy set on http_proxy env var isn't well formated");
        ne_uri_free (parsed);
      }
    }
    return TRUE;
  }

  if (0 != ne_uri_parse (uri, parsed)) {
    goto clear;
  }


  if (parsed->scheme) {
    GST_WARNING ("The proxy schema shouldn't be defined");
  }


  if (parsed->host && !parsed->port) {
    goto clear;
  }

  if (!parsed->path || parsed->authinfo) {
    goto clear;
  }

  return TRUE;

clear:

  ne_uri_free (parsed);

  return FALSE;

}

gboolean
set_uri (const char *uri, ne_uri * parsed, gboolean * ishttps, gchar ** uristr,
    gboolean set_default)
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

  if (0 != ne_uri_parse (uri, parsed)) {
    goto clear;
  }

  if (parsed->scheme == NULL) {
    parsed->scheme = g_strdup ("http");
    *ishttps = FALSE;
  } else {
    if (0 == strcmp (parsed->scheme, "https"))
      *ishttps = TRUE;
    else
      *ishttps = FALSE;
  }

  if (parsed->host == NULL) {
    parsed->host = g_strdup (HTTP_DEFAULT_HOST);
  }

  if (parsed->port == 0) {
    if (*ishttps)
      parsed->port = HTTPS_DEFAULT_PORT;
    else
      parsed->port = HTTP_DEFAULT_PORT;
  }

  if (!parsed->path) {
    parsed->path = g_strdup ("");
  } else {
    char *str = parsed->path;

    parsed->path = ne_path_escape (parsed->path);
    ne_free (str);
  }

done:

  if (uristr) {
    *uristr = ne_uri_unparse (parsed);
  }

  return TRUE;

clear:

  if (uristr && *uristr) {
    ne_free (*uristr);
    *uristr = NULL;
  }
  ne_uri_free (parsed);

  return FALSE;

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

      if (!set_proxy (g_value_get_string (value), &this->proxy, FALSE)) {
        GST_WARNING ("bad formated proxy");
        goto done;
      }
    }

      break;
    case PROP_LOCATION:
    {
      if (!g_value_get_string (value)) {
        GST_WARNING ("location property cannot be NULL");
        goto done;
      }

      if (!set_uri (g_value_get_string (value), &this->uri, &this->ishttps,
              &this->uristr, FALSE)) {
        GST_WARNING ("bad formated location");
        goto done;
      }
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
    }
      break;
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
    }
      break;

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

  return set_uri (uri, &src->uri, &src->ishttps, &src->uristr, FALSE);

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
  GST_ERROR ("memory exeception in neon\n");
}

void
size_header_handler (void *userdata, const char *value)
{
  GstNeonhttpSrc *src = GST_NEONHTTP_SRC (userdata);

  src->content_size = atoll (value);
  GST_DEBUG ("content size = %lld bytes", src->content_size);

}
