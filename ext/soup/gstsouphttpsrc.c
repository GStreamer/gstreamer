/* GStreamer
 * Copyright (C) <2007> Wouter Cloetens <wouter@mind.be>
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

#include <string.h>
#include <gst/gstelement.h>
#include <gst/gst-i18n-plugin.h>
#include <libsoup/soup.h>
#include "gstsouphttpsrc.h"

#include <gst/tag/tag.h>

GST_DEBUG_CATEGORY_STATIC (souphttpsrc_debug);
#define GST_CAT_DEFAULT souphttpsrc_debug

static const GstElementDetails gst_souphttp_src_details =
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
  PROP_IRADIO_MODE,
  PROP_IRADIO_NAME,
  PROP_IRADIO_GENRE,
  PROP_IRADIO_URL,
  PROP_IRADIO_TITLE
};

#define DEFAULT_USER_AGENT           "GStreamer souphttpsrc"

static void gst_souphttp_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static void gst_souphttp_src_init (GstSouphttpSrc * src,
    GstSouphttpSrcClass * g_class);
static void gst_souphttp_src_dispose (GObject * gobject);
static void gst_souphttp_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_souphttp_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_souphttp_src_create (GstPushSrc * psrc,
    GstBuffer ** outbuf);
static gboolean gst_souphttp_src_start (GstBaseSrc * bsrc);
static gboolean gst_souphttp_src_stop (GstBaseSrc * bsrc);
static gboolean gst_souphttp_src_get_size (GstBaseSrc * bsrc, guint64 * size);
static gboolean gst_souphttp_src_is_seekable (GstBaseSrc * bsrc);
static gboolean gst_souphttp_src_do_seek (GstBaseSrc * bsrc,
    GstSegment * segment);
static gboolean gst_souphttp_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_souphttp_src_unlock_stop (GstBaseSrc * bsrc);

static gboolean gst_souphttp_src_set_location (GstSouphttpSrc * src,
    const gchar * uri);
static gboolean soup_add_range_header (GstSouphttpSrc * src, guint64 offset);

static void soup_got_headers (SoupMessage * msg, GstSouphttpSrc * src);
static void soup_finished (SoupMessage * msg, GstSouphttpSrc * src);
static void soup_got_body (SoupMessage * msg, GstSouphttpSrc * src);
static void soup_got_chunk (SoupMessage * msg, GstSouphttpSrc * src);
static void soup_response (SoupMessage * msg, gpointer user_data);
static void soup_parse_status (SoupMessage * msg, GstSouphttpSrc * src);
static void soup_session_close (GstSouphttpSrc * src);

static char *gst_souphttp_src_unicodify (const char *str);

static void
_do_init (GType type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_souphttp_src_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (type, GST_TYPE_URI_HANDLER, &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (souphttpsrc_debug, "souphttpsrc", 0,
      "SOUP HTTP src");
}

GST_BOILERPLATE_FULL (GstSouphttpSrc, gst_souphttp_src, GstPushSrc,
    GST_TYPE_PUSH_SRC, _do_init);

static void
gst_souphttp_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_details (element_class, &gst_souphttp_src_details);
}

static void
gst_souphttp_src_class_init (GstSouphttpSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_souphttp_src_set_property;
  gobject_class->get_property = gst_souphttp_src_get_property;
  gobject_class->dispose = gst_souphttp_src_dispose;

  g_object_class_install_property (gobject_class,
      PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Location to read from", "", G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
      PROP_USER_AGENT,
      g_param_spec_string ("user-agent", "User-Agent",
          "Value of the User-Agent HTTP request header field",
          DEFAULT_USER_AGENT, G_PARAM_READWRITE));

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

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_souphttp_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_souphttp_src_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_souphttp_src_unlock);
  gstbasesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_souphttp_src_unlock_stop);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_souphttp_src_get_size);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_souphttp_src_is_seekable);
  gstbasesrc_class->do_seek = GST_DEBUG_FUNCPTR (gst_souphttp_src_do_seek);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_souphttp_src_create);

  GST_DEBUG_CATEGORY_INIT (souphttpsrc_debug, "souphttpsrc", 0,
      "SOUP HTTP Client Source");
}

static void
gst_souphttp_src_init (GstSouphttpSrc * src, GstSouphttpSrcClass * g_class)
{
  src->location = NULL;
  src->user_agent = g_strdup (DEFAULT_USER_AGENT);
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
  src->have_size = FALSE;
  src->seekable = TRUE;
  src->read_position = 0;
  src->request_position = 0;
}

static void
gst_souphttp_src_dispose (GObject * gobject)
{
  GstSouphttpSrc *src = GST_SOUPHTTP_SRC (gobject);

  GST_DEBUG_OBJECT (src, "dispose");
  g_free (src->location);
  src->location = NULL;
  g_free (src->user_agent);
  src->user_agent = NULL;
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
gst_souphttp_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSouphttpSrc *src = GST_SOUPHTTP_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
    {
      const gchar *location;

      location = g_value_get_string (value);

      if (location == NULL) {
        GST_WARNING ("location property cannot be NULL");
        goto done;
      }
      if (!gst_souphttp_src_set_location (src, location)) {
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
  }
done:
  return;
}

static void
gst_souphttp_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSouphttpSrc *src = GST_SOUPHTTP_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, src->location);
      break;
    case PROP_USER_AGENT:
      g_value_set_string (value, src->user_agent);
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
gst_souphttp_src_unicodify (const gchar * str)
{
  const gchar *env_vars[] = { "GST_ICY_TAG_ENCODING",
    "GST_TAG_ENCODING", NULL
  };

  return gst_tag_freeform_string_to_utf8 (str, -1, env_vars);
}

static GstFlowReturn
gst_souphttp_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstSouphttpSrc *src;

  src = GST_SOUPHTTP_SRC (psrc);

  if (src->msg && (src->request_position != src->read_position)) {
    if (src->msg->status == SOUP_MESSAGE_STATUS_IDLE) {
      soup_add_range_header (src, src->request_position);
    } else {
      GST_DEBUG_OBJECT (src, "Seek from position %" G_GUINT64_FORMAT
          " to %" G_GUINT64_FORMAT ": requeueing connection request",
          src->read_position, src->request_position);
      soup_session_cancel_message (src->session, src->msg);
      src->msg = NULL;
    }
  }
  if (!src->msg) {
    src->msg = soup_message_new (SOUP_METHOD_GET, src->location);
    if (!src->msg) {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
          (NULL), ("Error parsing URL \"%s\"", src->location));
      return GST_FLOW_ERROR;
    }
    soup_message_add_header (src->msg->request_headers, "Connection", "close");
    if (src->user_agent) {
      soup_message_add_header (src->msg->request_headers, "User-Agent",
          src->user_agent);
    }
    if (src->iradio_mode) {
      soup_message_add_header (src->msg->request_headers, "icy-metadata", "1");
    }

    g_signal_connect (src->msg, "got_headers",
        G_CALLBACK (soup_got_headers), src);
    g_signal_connect (src->msg, "got_body", G_CALLBACK (soup_got_body), src);
    g_signal_connect (src->msg, "finished", G_CALLBACK (soup_finished), src);
    g_signal_connect (src->msg, "got_chunk", G_CALLBACK (soup_got_chunk), src);
    soup_message_set_flags (src->msg, SOUP_MESSAGE_OVERWRITE_CHUNKS);
    soup_add_range_header (src, src->request_position);
  }

  src->ret = GST_FLOW_CUSTOM_ERROR;
  src->outbuf = outbuf;
  do {
    if (src->interrupted) {
      soup_session_cancel_message (src->session, src->msg);
      src->msg = NULL;
      break;
    }
    if (!src->msg) {
      GST_DEBUG_OBJECT (src, "EOS reached");
      break;
    }

    switch (src->msg->status) {
      case SOUP_MESSAGE_STATUS_IDLE:
        GST_DEBUG_OBJECT (src, "Queueing connection request");
        soup_session_queue_message (src->session, src->msg, soup_response, src);
        break;
      case SOUP_MESSAGE_STATUS_FINISHED:
        GST_DEBUG_OBJECT (src, "Connection closed");
        soup_session_cancel_message (src->session, src->msg);
        src->msg = NULL;
        break;
      case SOUP_MESSAGE_STATUS_QUEUED:
        break;
      case SOUP_MESSAGE_STATUS_CONNECTING:
      case SOUP_MESSAGE_STATUS_RUNNING:
      default:
        soup_message_io_unpause (src->msg);
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
gst_souphttp_src_start (GstBaseSrc * bsrc)
{
  GstSouphttpSrc *src = GST_SOUPHTTP_SRC (bsrc);

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

  src->session =
      soup_session_async_new_with_options (SOUP_SESSION_ASYNC_CONTEXT,
      src->context, NULL);
  if (!src->session) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        (NULL), ("Failed to create async session"));
    return FALSE;
  }

  return TRUE;
}

/* Close the socket and associated resources
 * used both to recover from errors and go to NULL state. */
static gboolean
gst_souphttp_src_stop (GstBaseSrc * bsrc)
{
  GstSouphttpSrc *src;

  src = GST_SOUPHTTP_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "stop()");
  soup_session_close (src);
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
gst_souphttp_src_unlock (GstBaseSrc * bsrc)
{
  GstSouphttpSrc *src;

  src = GST_SOUPHTTP_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "unlock()");

  src->interrupted = TRUE;
  if (src->loop)
    g_main_loop_quit (src->loop);
  return TRUE;
}

/* Interrupt interrupt. */
static gboolean
gst_souphttp_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstSouphttpSrc *src;

  src = GST_SOUPHTTP_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "unlock_stop()");

  src->interrupted = FALSE;
  return TRUE;
}

static gboolean
gst_souphttp_src_get_size (GstBaseSrc * bsrc, guint64 * size)
{
  GstSouphttpSrc *src;

  src = GST_SOUPHTTP_SRC (bsrc);

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
gst_souphttp_src_is_seekable (GstBaseSrc * bsrc)
{
  GstSouphttpSrc *src = GST_SOUPHTTP_SRC (bsrc);

  return src->seekable;
}

static gboolean
gst_souphttp_src_do_seek (GstBaseSrc * bsrc, GstSegment * segment)
{
  GstSouphttpSrc *src = GST_SOUPHTTP_SRC (bsrc);

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
soup_add_range_header (GstSouphttpSrc * src, guint64 offset)
{
  gchar buf[64];
  gint rc;

  soup_message_remove_header (src->msg->request_headers, "Range");
  if (offset) {
    rc = g_snprintf (buf, sizeof (buf), "bytes=%" G_GUINT64_FORMAT "-", offset);
    if (rc > sizeof (buf) || rc < 0)
      return FALSE;
    soup_message_add_header (src->msg->request_headers, "Range", buf);
  }
  src->read_position = offset;
  return TRUE;
}

static gboolean
gst_souphttp_src_set_location (GstSouphttpSrc * src, const gchar * uri)
{
  if (src->location) {
    g_free (src->location);
    src->location = NULL;
  }
  src->location = g_strdup (uri);

  return TRUE;
}

static void
soup_got_headers (SoupMessage * msg, GstSouphttpSrc * src)
{
  const char *value;
  GstTagList *tag_list;

  GST_DEBUG_OBJECT (src, "got headers");

  /* Parse Content-Length. */
  value = soup_message_get_header (msg->response_headers, "Content-Length");
  if (value != NULL) {
    src->content_size = g_ascii_strtoull (value, NULL, 10);
    src->have_size = TRUE;
    GST_DEBUG_OBJECT (src, "size = %llu", src->content_size);

    gst_element_post_message (GST_ELEMENT (src),
        gst_message_new_duration (GST_OBJECT (src), GST_FORMAT_BYTES,
            src->content_size));
  }

  /* Icecast stuff */
  tag_list = gst_tag_list_new ();

  if ((value =
          soup_message_get_header (msg->response_headers,
              "icy-metaint")) != NULL) {
    gint icy_metaint = atoi (value);

    GST_DEBUG_OBJECT (src, "icy-metaint: %s (parsed: %d)", value, icy_metaint);
    if (icy_metaint > 0)
      src->icy_caps = gst_caps_new_simple ("application/x-icy",
          "metadata-interval", G_TYPE_INT, icy_metaint, NULL);
  }

  if ((value =
          soup_message_get_header (msg->response_headers,
              "icy-name")) != NULL) {
    g_free (src->iradio_name);
    src->iradio_name = gst_souphttp_src_unicodify (value);
    if (src->iradio_name) {
      g_object_notify (G_OBJECT (src), "iradio-name");
      gst_tag_list_add (tag_list, GST_TAG_MERGE_REPLACE, GST_TAG_ORGANIZATION,
          src->iradio_name, NULL);
    }
  }
  if ((value =
          soup_message_get_header (msg->response_headers,
              "icy-genre")) != NULL) {
    g_free (src->iradio_genre);
    src->iradio_genre = gst_souphttp_src_unicodify (value);
    if (src->iradio_genre) {
      g_object_notify (G_OBJECT (src), "iradio-genre");
      gst_tag_list_add (tag_list, GST_TAG_MERGE_REPLACE, GST_TAG_GENRE,
          src->iradio_genre, NULL);
    }
  }
  if ((value =
          soup_message_get_header (msg->response_headers, "icy-url")) != NULL) {
    g_free (src->iradio_url);
    src->iradio_url = gst_souphttp_src_unicodify (value);
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
  soup_parse_status (msg, src);

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
soup_got_body (SoupMessage * msg, GstSouphttpSrc * src)
{
  if (msg != src->msg) {
    GST_DEBUG_OBJECT (src, "got body, but not for current message");
    return;
  }
  GST_DEBUG_OBJECT (src, "got body");
  src->ret = GST_FLOW_UNEXPECTED;
  if (src->loop)
    g_main_loop_quit (src->loop);
  soup_message_io_pause (msg);
}

/* Finished. Signal EOS. */
static void
soup_finished (SoupMessage * msg, GstSouphttpSrc * src)
{
  if (msg != src->msg) {
    GST_DEBUG_OBJECT (src, "finished, but not for current message");
    return;
  }
  GST_DEBUG_OBJECT (src, "finished");
  src->ret = GST_FLOW_UNEXPECTED;
  if (src->loop)
    g_main_loop_quit (src->loop);
}

static void
soup_got_chunk (SoupMessage * msg, GstSouphttpSrc * src)
{
  GstBaseSrc *basesrc;
  guint64 new_position;

  if (G_UNLIKELY (msg != src->msg)) {
    GST_DEBUG_OBJECT (src, "got chunk, but not for current message");
    return;
  }
  basesrc = GST_BASE_SRC_CAST (src);
  GST_DEBUG_OBJECT (src, "got chunk of %d bytes", msg->response.length);

  /* Create the buffer. */
  src->ret = gst_pad_alloc_buffer (GST_BASE_SRC_PAD (basesrc),
      basesrc->segment.last_stop, msg->response.length,
      GST_PAD_CAPS (GST_BASE_SRC_PAD (basesrc)), src->outbuf);
  if (G_LIKELY (src->ret == GST_FLOW_OK)) {
    memcpy (GST_BUFFER_DATA (*src->outbuf), msg->response.body,
        msg->response.length);
    new_position = src->read_position + msg->response.length;
    if (G_LIKELY (src->request_position == src->read_position))
      src->request_position = new_position;
    src->read_position = new_position;
  }

  g_main_loop_quit (src->loop);
  soup_message_io_pause (msg);
}

static void
soup_response (SoupMessage * msg, gpointer user_data)
{
  GstSouphttpSrc *src = (GstSouphttpSrc *) user_data;

  if (msg != src->msg) {
    GST_DEBUG_OBJECT (src, "got response %d: %s, but not for current message",
        msg->status_code, msg->reason_phrase);
    return;
  }
  GST_DEBUG_OBJECT (src, "got response %d: %s", msg->status_code,
      msg->reason_phrase);
  soup_parse_status (msg, src);
  g_main_loop_quit (src->loop);
}

static void
soup_parse_status (SoupMessage * msg, GstSouphttpSrc * src)
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
      SOUP_STATUS_IS_SERVER_ERROR (msg->status_code)) {
    /* Report HTTP error. */
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        ("\"%s\": %s", src->location, msg->reason_phrase),
        ("%d %s", msg->status_code, msg->reason_phrase));
    src->ret = GST_FLOW_ERROR;
  }
}

static void
soup_session_close (GstSouphttpSrc * src)
{
  if (src->session) {
    soup_session_abort (src->session);  /* This unrefs the message. */
    g_object_unref (src->session);
    src->session = NULL;
    src->msg = NULL;
  }
}

static guint
gst_souphttp_src_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_souphttp_src_uri_get_protocols (void)
{
  static gchar *protocols[] = { "http", "https", NULL };
  return protocols;
}

static const gchar *
gst_souphttp_src_uri_get_uri (GstURIHandler * handler)
{
  GstSouphttpSrc *src = GST_SOUPHTTP_SRC (handler);

  return src->location;
}

static gboolean
gst_souphttp_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstSouphttpSrc *src = GST_SOUPHTTP_SRC (handler);

  return gst_souphttp_src_set_location (src, uri);
}

static void
gst_souphttp_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_souphttp_src_uri_get_type;
  iface->get_protocols = gst_souphttp_src_uri_get_protocols;
  iface->get_uri = gst_souphttp_src_uri_get_uri;
  iface->set_uri = gst_souphttp_src_uri_set_uri;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* note: do not upgrade rank before we depend on a libsoup version where
   * icecast is supported properly out of the box */
  return gst_element_register (plugin, "souphttpsrc", GST_RANK_NONE,
      GST_TYPE_SOUPHTTP_SRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "soup",
    "libsoup http client src",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
