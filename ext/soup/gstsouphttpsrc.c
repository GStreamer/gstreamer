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
 * @short_description: Read from an HTTP/HTTPS/WebDAV/Icecast/Shoutcast
 * location.
 *
 * <refsect2>
 * <para>
 * This plugin reads data from a remote location specified by a URI.
 * Supported protocols are 'http', 'https'.
 * </para>
 * <para>
 * An HTTP proxy must be specified by its URL.
 * If the "http_proxy" environment variable is set, its value is used.
 * The element-souphttpsrc::proxy property can be used to override the
 * default.
 * </para>
 * <para>
 * In case the element-souphttpsrc::iradio-mode property is set and the
 * location is an HTTP resource, souphttpsrc will send special Icecast HTTP
 * headers to the server to request additional Icecast meta-information. If
 * the server is not an Icecast server, it will behave as if the
 * element-souphttpsrc::iradio-mode property were not set. If it is,
 * souphttpsrc will output data with a media type of application/x-icy,
 * in which case you will need to use the #ICYDemux element as follow-up
 * element to extract the Icecast metadata and to determine the underlying
 * media type.
 * </para>
 * <para>
 * Example pipeline:
 * <programlisting>
 * gst-launch -v souphttpsrc location=https://some.server.org/index.html
 *     ! filesink location=/home/joe/server.html
 * </programlisting>
 * The above pipeline reads a web page from a server using the HTTPS protocol
 * and writes it to a local file.
 * </para>
 * <para>
 * Another example pipeline:
 * <programlisting>
 * gst-launch -v souphttpsrc user-agent="FooPlayer 0.99 beta"
 *     automatic-redirect=false proxy=http://proxy.intranet.local:8080
 *     location=http://music.foobar.com/demo.mp3 ! mad ! audioconvert
 *     ! audioresample ! alsasink
 * </programlisting>
 * The above pipeline will read and decode and play an mp3 file from a
 * web server using the HTTP protocol. If the server sends redirects,
 * the request fails instead of following the redirect. The specified
 * HTTP proxy server is used. The User-Agent HTTP request header
 * is set to a custom string instead of "GStreamer souphttpsrc."
 * </para>
 * <para>
 * Yet another example pipeline:
 * <programlisting>
 * gst-launch -v souphttpsrc location=http://10.11.12.13/mjpeg
 *     do-timestamp=true ! multipartdemux
 *     ! image/jpeg,width=640,height=480 ! matroskamux
 *     ! filesink location=mjpeg.mkv
 * </programlisting>
 * The above pipeline reads a motion JPEG stream from an IP camera
 * using the HTTP protocol, encoded as mime/multipart image/jpeg
 * parts, and writes a Matroska motion JPEG file. The width and
 * height properties are set in the caps to provide the Matroska
 * multiplexer with the information to set this in the header.
 * Timestamps are set on the buffers as they arrive from the camera.
 * These are used by the mime/multipart demultiplexer to emit timestamps
 * on the JPEG-encoded video frame buffers. This allows the Matroska
 * multiplexer to timestamp the frames in the resulting file.
 * </para>
 * </refsect2>
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
#include <gst/gst-i18n-plugin.h>
#include <libsoup/soup.h>
#include "gstsouphttpsrc.h"

#include <gst/tag/tag.h>

GST_DEBUG_CATEGORY_STATIC (souphttpsrc_debug);
#define GST_CAT_DEFAULT souphttpsrc_debug

static const GstElementDetails gst_soup_http_src_details =
GST_ELEMENT_DETAILS ("HTTP client source",
    "Source/Network",
    "Receive data as a client over the network via HTTP using SOUP",
    "Wouter Cloetens <wouter@mind.be>");

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_USER_AGENT,
  PROP_AUTOMATIC_REDIRECT,
  PROP_PROXY,
  PROP_COOKIES,
  PROP_IRADIO_MODE,
  PROP_IRADIO_NAME,
  PROP_IRADIO_GENRE,
  PROP_IRADIO_URL,
  PROP_IRADIO_TITLE
};

#define DEFAULT_USER_AGENT           "GStreamer souphttpsrc "

static void gst_soup_http_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static void gst_soup_http_src_init (GstSoupHTTPSrc * src,
    GstSoupHTTPSrcClass * g_class);
static void gst_soup_http_src_dispose (GObject * gobject);

static void gst_soup_http_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_soup_http_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_soup_http_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_soup_http_src_start (GstBaseSrc * bsrc);

static gboolean gst_soup_http_src_stop (GstBaseSrc * bsrc);

static gboolean gst_soup_http_src_get_size (GstBaseSrc * bsrc, guint64 * size);

static gboolean gst_soup_http_src_is_seekable (GstBaseSrc * bsrc);

static gboolean gst_soup_http_src_do_seek (GstBaseSrc * bsrc,
    GstSegment * segment);
static gboolean gst_soup_http_src_unlock (GstBaseSrc * bsrc);

static gboolean gst_soup_http_src_unlock_stop (GstBaseSrc * bsrc);

static gboolean gst_soup_http_src_set_location (GstSoupHTTPSrc * src,
    const gchar * uri);
static gboolean gst_soup_http_src_set_proxy (GstSoupHTTPSrc * src,
    const gchar * uri);

static char *gst_soup_http_src_unicodify (const char *str);

static gboolean gst_soup_http_src_build_message (GstSoupHTTPSrc * src);

static void gst_soup_http_src_cancel_message (GstSoupHTTPSrc * src);

static void gst_soup_http_src_queue_message (GstSoupHTTPSrc * src);

static gboolean gst_soup_http_src_add_range_header (GstSoupHTTPSrc * src,
    guint64 offset);
static void gst_soup_http_src_session_unpause_message (GstSoupHTTPSrc * src);

static void gst_soup_http_src_session_pause_message (GstSoupHTTPSrc * src);

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


static void
_do_init (GType type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_soup_http_src_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (type, GST_TYPE_URI_HANDLER, &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (souphttpsrc_debug, "souphttpsrc", 0,
      "SOUP HTTP src");
}

GST_BOILERPLATE_FULL (GstSoupHTTPSrc, gst_soup_http_src, GstPushSrc,
    GST_TYPE_PUSH_SRC, _do_init);

static void
gst_soup_http_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_details (element_class, &gst_soup_http_src_details);
}

static void
gst_soup_http_src_class_init (GstSoupHTTPSrcClass * klass)
{
  GObjectClass *gobject_class;

  GstBaseSrcClass *gstbasesrc_class;

  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_soup_http_src_set_property;
  gobject_class->get_property = gst_soup_http_src_get_property;
  gobject_class->dispose = gst_soup_http_src_dispose;

  g_object_class_install_property (gobject_class,
      PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Location to read from", "", G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
      PROP_USER_AGENT,
      g_param_spec_string ("user-agent", "User-Agent",
          "Value of the User-Agent HTTP request header field",
          DEFAULT_USER_AGENT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
      PROP_AUTOMATIC_REDIRECT,
      g_param_spec_boolean ("automatic-redirect", "automatic-redirect",
          "Automatically follow HTTP redirects (HTTP Status Code 3xx)",
          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
      PROP_PROXY,
      g_param_spec_string ("proxy", "Proxy",
          "HTTP proxy server URI", "", G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
      PROP_COOKIES, g_param_spec_boxed ("cookies", "Cookies",
          "HTTP request cookies", G_TYPE_STRV, G_PARAM_READWRITE));

  /* icecast stuff */
  g_object_class_install_property (gobject_class,
      PROP_IRADIO_MODE,
      g_param_spec_boolean ("iradio-mode",
          "iradio-mode",
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
  g_object_class_install_property (gobject_class,
      PROP_IRADIO_TITLE,
      g_param_spec_string ("iradio-title",
          "iradio-title",
          "Name of currently playing song", NULL, G_PARAM_READABLE));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_soup_http_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_soup_http_src_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_soup_http_src_unlock);
  gstbasesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_soup_http_src_unlock_stop);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_soup_http_src_get_size);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_soup_http_src_is_seekable);
  gstbasesrc_class->do_seek = GST_DEBUG_FUNCPTR (gst_soup_http_src_do_seek);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_soup_http_src_create);

  GST_DEBUG_CATEGORY_INIT (souphttpsrc_debug, "souphttpsrc", 0,
      "SOUP HTTP Client Source");
}

static void
gst_soup_http_src_init (GstSoupHTTPSrc * src, GstSoupHTTPSrcClass * g_class)
{
  const gchar *proxy;

  src->location = NULL;
  src->automatic_redirect = TRUE;
  src->user_agent = g_strdup (DEFAULT_USER_AGENT);
  src->cookies = NULL;
  src->icy_caps = NULL;
  src->iradio_mode = FALSE;
  src->iradio_name = NULL;
  src->iradio_genre = NULL;
  src->iradio_url = NULL;
  src->iradio_title = NULL;
  src->loop = NULL;
  src->context = NULL;
  src->session = NULL;
  src->msg = NULL;
  src->interrupted = FALSE;
  src->retry = FALSE;
  src->have_size = FALSE;
  src->seekable = TRUE;
  src->read_position = 0;
  src->request_position = 0;
  proxy = g_getenv ("http_proxy");
  if (proxy && !gst_soup_http_src_set_proxy (src, proxy)) {
    GST_WARNING_OBJECT (src,
        "The proxy in the http_proxy env var (\"%s\") cannot be parsed.",
        proxy);
  }
}

static void
gst_soup_http_src_dispose (GObject * gobject)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (gobject);

  GST_DEBUG_OBJECT (src, "dispose");
  g_free (src->location);
  src->location = NULL;
  g_free (src->user_agent);
  src->user_agent = NULL;
  if (src->proxy != NULL) {
    soup_uri_free (src->proxy);
    src->proxy = NULL;
  }
  g_strfreev (src->cookies);
  g_free (src->iradio_name);
  src->iradio_name = NULL;
  g_free (src->iradio_genre);
  src->iradio_genre = NULL;
  g_free (src->iradio_url);
  src->iradio_url = NULL;
  g_free (src->iradio_title);
  src->iradio_title = NULL;
  if (src->icy_caps) {
    gst_caps_unref (src->icy_caps);
    src->icy_caps = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (gobject);
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
      if (!gst_soup_http_src_set_location (src, location)) {
        GST_WARNING ("badly formatted location");
        goto done;
      }
      break;
    }
    case PROP_USER_AGENT:
      if (src->user_agent)
        g_free (src->user_agent);
      src->user_agent = g_value_dup_string (value);
      break;
    case PROP_IRADIO_MODE:
      src->iradio_mode = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    case PROP_AUTOMATIC_REDIRECT:
      src->automatic_redirect = g_value_get_boolean (value);
      break;
    case PROP_PROXY:
    {
      const gchar *proxy;

      proxy = g_value_get_string (value);

      if (proxy == NULL) {
        GST_WARNING ("proxy property cannot be NULL");
        goto done;
      }
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
        g_value_set_string (value, "");
      else {
        char *proxy = soup_uri_to_string (src->proxy, FALSE);

        g_value_set_string (value, proxy);
        g_free (proxy);
      }
      break;
    case PROP_COOKIES:
      g_value_set_boxed (value, g_strdupv (src->cookies));
      break;
    case PROP_IRADIO_MODE:
      g_value_set_boolean (value, src->iradio_mode);
      break;
    case PROP_IRADIO_NAME:
      g_value_set_string (value, src->iradio_name);
      break;
    case PROP_IRADIO_GENRE:
      g_value_set_string (value, src->iradio_genre);
      break;
    case PROP_IRADIO_URL:
      g_value_set_string (value, src->iradio_url);
      break;
    case PROP_IRADIO_TITLE:
      g_value_set_string (value, src->iradio_title);
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
gst_soup_http_src_add_range_header (GstSoupHTTPSrc * src, guint64 offset)
{
  gchar buf[64];

  gint rc;

  soup_message_headers_remove (src->msg->request_headers, "Range");
  if (offset) {
    rc = g_snprintf (buf, sizeof (buf), "bytes=%" G_GUINT64_FORMAT "-", offset);
    if (rc > sizeof (buf) || rc < 0)
      return FALSE;
    soup_message_headers_append (src->msg->request_headers, "Range", buf);
  }
  src->read_position = offset;
  return TRUE;
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

static void
gst_soup_http_src_session_close (GstSoupHTTPSrc * src)
{
  if (src->session) {
    soup_session_abort (src->session);  /* This unrefs the message. */
    g_object_unref (src->session);
    src->session = NULL;
    src->msg = NULL;
  }
}

static void
gst_soup_http_src_got_headers_cb (SoupMessage * msg, GstSoupHTTPSrc * src)
{
  const char *value;

  GstTagList *tag_list;

  GstBaseSrc *basesrc;

  guint64 newsize;

  GST_DEBUG_OBJECT (src, "got headers");

  if (src->automatic_redirect && SOUP_STATUS_IS_REDIRECTION (msg->status_code)) {
    GST_DEBUG_OBJECT (src, "%u redirect to \"%s\"", msg->status_code,
        soup_message_headers_get (msg->response_headers, "Location"));
    return;
  }

  if (msg->status_code == SOUP_STATUS_UNAUTHORIZED)
    return;

  src->session_io_status = GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING;

  /* Parse Content-Length. */
  if (soup_message_headers_get_encoding (msg->response_headers) ==
      SOUP_ENCODING_CONTENT_LENGTH) {
    newsize = src->request_position +
        soup_message_headers_get_content_length (msg->response_headers);
    if (!src->have_size || (src->content_size != newsize)) {
      src->content_size = newsize;
      src->have_size = TRUE;
      GST_DEBUG_OBJECT (src, "size = %" G_GUINT64_FORMAT, src->content_size);

      basesrc = GST_BASE_SRC_CAST (src);
      gst_segment_set_duration (&basesrc->segment, GST_FORMAT_BYTES,
          src->content_size);
      gst_element_post_message (GST_ELEMENT (src),
          gst_message_new_duration (GST_OBJECT (src), GST_FORMAT_BYTES,
              src->content_size));
    }
  }

  /* Icecast stuff */
  tag_list = gst_tag_list_new ();

  if ((value =
          soup_message_headers_get (msg->response_headers,
              "icy-metaint")) != NULL) {
    gint icy_metaint = atoi (value);

    GST_DEBUG_OBJECT (src, "icy-metaint: %s (parsed: %d)", value, icy_metaint);
    if (icy_metaint > 0) {
      if (src->icy_caps)
        gst_caps_unref (src->icy_caps);

      src->icy_caps = gst_caps_new_simple ("application/x-icy",
          "metadata-interval", G_TYPE_INT, icy_metaint, NULL);
    }
  }

  if ((value =
          soup_message_headers_get (msg->response_headers,
              "icy-name")) != NULL) {
    g_free (src->iradio_name);
    src->iradio_name = gst_soup_http_src_unicodify (value);
    if (src->iradio_name) {
      g_object_notify (G_OBJECT (src), "iradio-name");
      gst_tag_list_add (tag_list, GST_TAG_MERGE_REPLACE, GST_TAG_ORGANIZATION,
          src->iradio_name, NULL);
    }
  }
  if ((value =
          soup_message_headers_get (msg->response_headers,
              "icy-genre")) != NULL) {
    g_free (src->iradio_genre);
    src->iradio_genre = gst_soup_http_src_unicodify (value);
    if (src->iradio_genre) {
      g_object_notify (G_OBJECT (src), "iradio-genre");
      gst_tag_list_add (tag_list, GST_TAG_MERGE_REPLACE, GST_TAG_GENRE,
          src->iradio_genre, NULL);
    }
  }
  if ((value = soup_message_headers_get (msg->response_headers, "icy-url"))
      != NULL) {
    g_free (src->iradio_url);
    src->iradio_url = gst_soup_http_src_unicodify (value);
    if (src->iradio_url) {
      g_object_notify (G_OBJECT (src), "iradio-url");
      gst_tag_list_add (tag_list, GST_TAG_MERGE_REPLACE, GST_TAG_LOCATION,
          src->iradio_url, NULL);
    }
  }
  if (!gst_tag_list_is_empty (tag_list)) {
    GST_DEBUG_OBJECT (src,
        "calling gst_element_found_tags with %" GST_PTR_FORMAT, tag_list);
    gst_element_found_tags (GST_ELEMENT_CAST (src), tag_list);
  } else {
    gst_tag_list_free (tag_list);
  }

  /* Handle HTTP errors. */
  gst_soup_http_src_parse_status (msg, src);

  /* Check if Range header was respected. */
  if (src->ret == GST_FLOW_CUSTOM_ERROR &&
      src->read_position && msg->status_code != SOUP_STATUS_PARTIAL_CONTENT) {
    src->seekable = FALSE;
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        ("\"%s\": failed to seek; server does not accept Range HTTP header",
            src->location), (NULL));
    src->ret = GST_FLOW_ERROR;
  }
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
  src->ret = GST_FLOW_UNEXPECTED;
  if (src->loop)
    g_main_loop_quit (src->loop);
  gst_soup_http_src_session_pause_message (src);
}

/* Finished. Signal EOS. */
static void
gst_soup_http_src_finished_cb (SoupMessage * msg, GstSoupHTTPSrc * src)
{
  if (G_UNLIKELY (msg != src->msg)) {
    GST_DEBUG_OBJECT (src, "finished, but not for current message");
    return;
  }
  GST_DEBUG_OBJECT (src, "finished");
  src->ret = GST_FLOW_UNEXPECTED;
  if (src->session_io_status == GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_CANCELLED) {
    /* gst_soup_http_src_cancel_message() triggered this; probably a seek
     * that occurred in the QUEUEING state; i.e. before the connection setup
     * was complete. Do nothing */
  } else if (src->session_io_status ==
      GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING && src->read_position > 0) {
    /* The server disconnected while streaming. Reconnect and seeking to the
     * last location. */
    src->retry = TRUE;
    src->ret = GST_FLOW_CUSTOM_ERROR;
  } else if (G_UNLIKELY (src->session_io_status !=
          GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING)) {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
        ("%s", msg->reason_phrase),
        ("libsoup status code %d", msg->status_code));
  }
  if (src->loop)
    g_main_loop_quit (src->loop);
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

static void
gst_soup_http_src_chunk_free (gpointer gstbuf)
{
  gst_buffer_unref (GST_BUFFER_CAST (gstbuf));
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

  if (max_len)
    length = MIN (basesrc->blocksize, max_len);
  else
    length = basesrc->blocksize;
  GST_DEBUG_OBJECT (src, "alloc %" G_GSIZE_FORMAT " bytes <= %" G_GSIZE_FORMAT,
      length, max_len);


  rc = gst_pad_alloc_buffer (GST_BASE_SRC_PAD (basesrc),
      GST_BUFFER_OFFSET_NONE, length,
      src->icy_caps ? src->icy_caps :
      GST_PAD_CAPS (GST_BASE_SRC_PAD (basesrc)), &gstbuf);
  if (G_UNLIKELY (rc != GST_FLOW_OK)) {
    /* Failed to allocate buffer. Stall SoupSession and return error code
     * to create(). */
    src->ret = rc;
    g_main_loop_quit (src->loop);
    return NULL;
  }

  soupbuf = soup_buffer_new_with_owner (GST_BUFFER_DATA (gstbuf), length,
      gstbuf, gst_soup_http_src_chunk_free);

  return soupbuf;
}

static void
gst_soup_http_src_got_chunk_cb (SoupMessage * msg, SoupBuffer * chunk,
    GstSoupHTTPSrc * src)
{
  GstBaseSrc *basesrc;

  guint64 new_position;

  if (G_UNLIKELY (msg != src->msg)) {
    GST_DEBUG_OBJECT (src, "got chunk, but not for current message");
    return;
  }
  if (G_UNLIKELY (src->session_io_status !=
          GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING)) {
    /* Probably a redirect. */
    return;
  }
  basesrc = GST_BASE_SRC_CAST (src);
  GST_DEBUG_OBJECT (src, "got chunk of %" G_GSIZE_FORMAT " bytes",
      chunk->length);

  /* Extract the GstBuffer from the SoupBuffer and set its fields. */
  *src->outbuf = GST_BUFFER_CAST (soup_buffer_get_owner (chunk));
  gst_buffer_ref (*src->outbuf);
  GST_BUFFER_SIZE (*src->outbuf) = chunk->length;
  GST_BUFFER_OFFSET (*src->outbuf) = basesrc->segment.last_stop;

  gst_buffer_set_caps (*src->outbuf,
      (src->
          icy_caps) ? src->icy_caps :
      GST_PAD_CAPS (GST_BASE_SRC_PAD (basesrc)));

  new_position = src->read_position + chunk->length;
  if (G_LIKELY (src->request_position == src->read_position))
    src->request_position = new_position;
  src->read_position = new_position;

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
  GST_DEBUG_OBJECT (src, "got response %d: %s", msg->status_code,
      msg->reason_phrase);
  if (src->session_io_status == GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_RUNNING &&
      src->read_position > 0) {
    /* The server disconnected while streaming. Reconnect and seeking to the
     * last location. */
    src->retry = TRUE;
  } else
    gst_soup_http_src_parse_status (msg, src);
  /* The session's SoupMessage object expires after this callback returns. */
  src->msg = NULL;
  g_main_loop_quit (src->loop);
}

static void
gst_soup_http_src_parse_status (SoupMessage * msg, GstSoupHTTPSrc * src)
{
  if (SOUP_STATUS_IS_TRANSPORT_ERROR (msg->status_code)) {
    switch (msg->status_code) {
      case SOUP_STATUS_CANT_RESOLVE:
        GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
            ("\"%s\": %s", src->location, msg->reason_phrase),
            ("libsoup status code %d", msg->status_code));
        src->ret = GST_FLOW_ERROR;
        break;
      case SOUP_STATUS_CANT_RESOLVE_PROXY:
        GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
            ("%s", msg->reason_phrase),
            ("libsoup status code %d", msg->status_code));
        src->ret = GST_FLOW_ERROR;
        break;
      case SOUP_STATUS_CANT_CONNECT:
      case SOUP_STATUS_CANT_CONNECT_PROXY:
      case SOUP_STATUS_SSL_FAILED:
        GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
            ("\"%s\": %s", src->location, msg->reason_phrase),
            ("libsoup status code %d", msg->status_code));
        src->ret = GST_FLOW_ERROR;
        break;
      case SOUP_STATUS_IO_ERROR:
      case SOUP_STATUS_MALFORMED:
        GST_ELEMENT_ERROR (src, RESOURCE, READ,
            ("\"%s\": %s", src->location, msg->reason_phrase),
            ("libsoup status code %d", msg->status_code));
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
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("\"%s\": %s", src->location, msg->reason_phrase),
        ("%d %s", msg->status_code, msg->reason_phrase));
    src->ret = GST_FLOW_ERROR;
  }
}

static gboolean
gst_soup_http_src_build_message (GstSoupHTTPSrc * src)
{
  src->msg = soup_message_new (SOUP_METHOD_GET, src->location);
  if (!src->msg) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (NULL), ("Error parsing URL \"%s\"", src->location));
    return FALSE;
  }
  src->session_io_status = GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_IDLE;
  soup_message_headers_append (src->msg->request_headers, "Connection",
      "close");
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
  gst_soup_http_src_add_range_header (src, src->request_position);

  return TRUE;
}

static GstFlowReturn
gst_soup_http_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstSoupHTTPSrc *src;

  src = GST_SOUP_HTTP_SRC (psrc);

  if (src->msg && (src->request_position != src->read_position)) {
    if (src->session_io_status == GST_SOUP_HTTP_SRC_SESSION_IO_STATUS_IDLE) {
      gst_soup_http_src_add_range_header (src, src->request_position);
    } else {
      GST_DEBUG_OBJECT (src, "Seek from position %" G_GUINT64_FORMAT
          " to %" G_GUINT64_FORMAT ": requeueing connection request",
          src->read_position, src->request_position);
      gst_soup_http_src_cancel_message (src);
    }
  }
  if (!src->msg)
    if (!gst_soup_http_src_build_message (src))
      return GST_FLOW_ERROR;

  src->ret = GST_FLOW_CUSTOM_ERROR;
  src->outbuf = outbuf;
  do {
    if (src->interrupted) {
      GST_DEBUG_OBJECT (src, "interrupted");
      break;
    }
    if (src->retry) {
      GST_DEBUG_OBJECT (src, "Reconnecting");
      if (!gst_soup_http_src_build_message (src))
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
        GST_DEBUG_OBJECT (src, "Queueing connection request");
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

    if (src->ret == GST_FLOW_CUSTOM_ERROR)
      g_main_loop_run (src->loop);
  } while (src->ret == GST_FLOW_CUSTOM_ERROR);

  if (src->ret == GST_FLOW_CUSTOM_ERROR)
    src->ret = GST_FLOW_UNEXPECTED;
  return src->ret;
}

static gboolean
gst_soup_http_src_start (GstBaseSrc * bsrc)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (bsrc);

  GST_DEBUG_OBJECT (src, "start(\"%s\")", src->location);

  if (!src->location) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (NULL), ("Missing location property"));
    return FALSE;
  }

  src->context = g_main_context_new ();

  src->loop = g_main_loop_new (src->context, TRUE);
  if (!src->loop) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        (NULL), ("Failed to start GMainLoop"));
    g_main_context_unref (src->context);
    return FALSE;
  }

  if (src->proxy == NULL)
    src->session =
        soup_session_async_new_with_options (SOUP_SESSION_ASYNC_CONTEXT,
        src->context, SOUP_SESSION_USER_AGENT, src->user_agent, NULL);
  else
    src->session =
        soup_session_async_new_with_options (SOUP_SESSION_ASYNC_CONTEXT,
        src->context, SOUP_SESSION_PROXY_URI, src->proxy,
        SOUP_SESSION_USER_AGENT, src->user_agent, NULL);
  if (!src->session) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        (NULL), ("Failed to create async session"));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_soup_http_src_stop (GstBaseSrc * bsrc)
{
  GstSoupHTTPSrc *src;

  src = GST_SOUP_HTTP_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "stop()");
  gst_soup_http_src_session_close (src);
  if (src->loop) {
    g_main_loop_unref (src->loop);
    g_main_context_unref (src->context);
    src->loop = NULL;
    src->context = NULL;
  }

  return TRUE;
}

/* Interrupt a blocking request. */
static gboolean
gst_soup_http_src_unlock (GstBaseSrc * bsrc)
{
  GstSoupHTTPSrc *src;

  src = GST_SOUP_HTTP_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "unlock()");

  src->interrupted = TRUE;
  if (src->loop)
    g_main_loop_quit (src->loop);
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

static gboolean
gst_soup_http_src_is_seekable (GstBaseSrc * bsrc)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (bsrc);

  return src->seekable;
}

static gboolean
gst_soup_http_src_do_seek (GstBaseSrc * bsrc, GstSegment * segment)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (bsrc);

  GST_DEBUG_OBJECT (src, "do_seek(%" G_GUINT64_FORMAT ")", segment->start);

  if (src->read_position == segment->start)
    return TRUE;

  if (!src->seekable)
    return FALSE;

  /* Wait for create() to handle the jump in offset. */
  src->request_position = segment->start;
  return TRUE;
}

static gboolean
gst_soup_http_src_set_location (GstSoupHTTPSrc * src, const gchar * uri)
{
  if (src->location) {
    g_free (src->location);
    src->location = NULL;
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
  if (g_str_has_prefix (uri, "http://")) {
    src->proxy = soup_uri_new (uri);
  } else {
    gchar *new_uri = g_strconcat ("http://", uri, NULL);

    src->proxy = soup_uri_new (new_uri);
    g_free (new_uri);
  }

  return TRUE;
}

static guint
gst_soup_http_src_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_soup_http_src_uri_get_protocols (void)
{
  static gchar *protocols[] = { "http", "https", NULL };
  return protocols;
}

static const gchar *
gst_soup_http_src_uri_get_uri (GstURIHandler * handler)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (handler);

  return src->location;
}

static gboolean
gst_soup_http_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstSoupHTTPSrc *src = GST_SOUP_HTTP_SRC (handler);

  return gst_soup_http_src_set_location (src, uri);
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
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "souphttpsrc", GST_RANK_PRIMARY,
      GST_TYPE_SOUP_HTTP_SRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "soup",
    "libsoup HTTP client src",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
