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
#include <libsoup/soup.h>
#include "gstsouphttpsrc.h"

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
};

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
static gboolean gst_souphttp_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_souphttp_src_unlock_stop (GstBaseSrc * bsrc);

static gboolean gst_souphttp_src_set_location (GstSouphttpSrc * src,
    const gchar * uri);

static void soup_got_headers (SoupMessage * msg, GstSouphttpSrc * src);
static void soup_finished (SoupMessage * msg, GstSouphttpSrc * src);
static void soup_got_body (SoupMessage * msg, GstSouphttpSrc * src);
static void soup_got_chunk (SoupMessage * msg, GstSouphttpSrc * src);
static void soup_response (SoupMessage * msg, gpointer user_data);
static void soup_session_close (GstSouphttpSrc * src);

static void
_do_init (GType type)
{
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

  g_object_class_install_property
      (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Location to read from", "", G_PARAM_READWRITE));

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_souphttp_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_souphttp_src_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_souphttp_src_unlock);
  gstbasesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_souphttp_src_unlock_stop);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_souphttp_src_get_size);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_souphttp_src_create);

  GST_DEBUG_CATEGORY_INIT (souphttpsrc_debug, "souphttpsrc", 0,
      "SOUP HTTP Client Source");
}

static void
gst_souphttp_src_init (GstSouphttpSrc * src, GstSouphttpSrcClass * g_class)
{
  src->location = NULL;
  src->loop = NULL;
  src->context = NULL;
  src->session = NULL;
  src->msg = NULL;
  src->interrupted = FALSE;
  src->have_size = FALSE;
}

static void
gst_souphttp_src_dispose (GObject * gobject)
{
  GstSouphttpSrc *src = GST_SOUPHTTP_SRC (gobject);

  GST_DEBUG_OBJECT (src, "dispose");
  soup_session_close (src);
  if (src->loop) {
    g_main_loop_unref (src->loop);
    g_main_context_unref (src->context);
    src->loop = NULL;
    src->context = NULL;
  }
  if (src->location) {
    g_free (src->location);
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
  GstSouphttpSrc *souphttpsrc = GST_SOUPHTTP_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, souphttpsrc->location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_souphttp_src_create (GstPushSrc * psrc, GstBuffer ** outbuf)
{
  GstSouphttpSrc *src;

  src = GST_SOUPHTTP_SRC (psrc);

  src->ret = GST_FLOW_CUSTOM_ERROR;
  src->outbuf = outbuf;
  do {
    if (src->interrupted) {
      soup_session_close (src);
      src->ret = GST_FLOW_UNEXPECTED;
      break;
    }
    if (!src->session) {
      GST_DEBUG_OBJECT (src, "EOS reached");
      soup_session_close (src);
      src->ret = GST_FLOW_UNEXPECTED;
      break;
    }

    switch (src->msg->status) {
      case SOUP_MESSAGE_STATUS_IDLE:
        GST_DEBUG_OBJECT (src, "Queueing connection request");
        soup_session_queue_message (src->session, src->msg, soup_response, src);
        break;
      case SOUP_MESSAGE_STATUS_FINISHED:
        GST_DEBUG_OBJECT (src, "Connection closed");
        soup_session_close (src);
        src->ret = GST_FLOW_UNEXPECTED;
        break;
      case SOUP_MESSAGE_STATUS_QUEUED:
      case SOUP_MESSAGE_STATUS_CONNECTING:
      case SOUP_MESSAGE_STATUS_RUNNING:
      default:
        soup_message_io_unpause (src->msg);
        break;
    }

    if (src->ret == GST_FLOW_CUSTOM_ERROR)
      g_main_loop_run (src->loop);
  } while (src->ret == GST_FLOW_CUSTOM_ERROR);

  return src->ret;
}

static gboolean
gst_souphttp_src_start (GstBaseSrc * bsrc)
{
  GstSouphttpSrc *src = GST_SOUPHTTP_SRC (bsrc);

  if (!src->location) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
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

  src->msg = soup_message_new (SOUP_METHOD_GET, src->location);
  if (!src->msg) {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, (NULL), ("Error parsing URL"));
    return FALSE;
  }
  soup_message_add_header (src->msg->request_headers, "Connection", "close");

  g_signal_connect (src->msg, "got_headers",
      G_CALLBACK (soup_got_headers), src);
  g_signal_connect (src->msg, "got_body", G_CALLBACK (soup_got_body), src);
  g_signal_connect (src->msg, "finished", G_CALLBACK (soup_finished), src);
  g_signal_connect (src->msg, "got_chunk", G_CALLBACK (soup_got_chunk), src);
  soup_message_set_flags (src->msg, SOUP_MESSAGE_OVERWRITE_CHUNKS);

  return TRUE;
}

/* close the socket and associated resources
 * used both to recover from errors and go to NULL state */
static gboolean
gst_souphttp_src_stop (GstBaseSrc * bsrc)
{
  GstSouphttpSrc *src;

  src = GST_SOUPHTTP_SRC (bsrc);
  GST_DEBUG_OBJECT (src, "stop()");
  soup_session_close (src);

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
    GST_DEBUG_OBJECT (src, "get_size() = %llu", src->content_size);
    *size = src->content_size;
    return TRUE;
  }
  GST_DEBUG_OBJECT (src, "get_size() = FALSE");
  return FALSE;
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

  GST_DEBUG_OBJECT (src, "got headers");

  value = soup_message_get_header (msg->response_headers, "Content-Length");
  if (value != NULL) {
    src->content_size = g_ascii_strtoull (value, NULL, 10);
    src->have_size = TRUE;
    GST_DEBUG_OBJECT (src, "size = %llu", src->content_size);

    gst_element_post_message (GST_ELEMENT (src),
        gst_message_new_duration (GST_OBJECT (src), GST_FORMAT_BYTES,
            src->content_size));
  }
}

/* Have body. Signal EOS. */
static void
soup_got_body (SoupMessage * msg, GstSouphttpSrc * src)
{
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
  GST_DEBUG_OBJECT (src, "finished");
  src->ret = GST_FLOW_UNEXPECTED;
  if (src->loop)
    g_main_loop_quit (src->loop);
  soup_message_io_pause (msg);
}

static void
soup_got_chunk (SoupMessage * msg, GstSouphttpSrc * src)
{
  GstBaseSrc *basesrc;

  basesrc = GST_BASE_SRC_CAST (src);
  GST_DEBUG_OBJECT (src, "got chunk of %d bytes", msg->response.length);

  /* Create the buffer. */
  src->ret = gst_pad_alloc_buffer (GST_BASE_SRC_PAD (basesrc),
      basesrc->segment.last_stop, msg->response.length,
      GST_PAD_CAPS (GST_BASE_SRC_PAD (basesrc)), src->outbuf);
  if (G_LIKELY (src->ret == GST_FLOW_OK)) {
    memcpy (GST_BUFFER_DATA (*src->outbuf), msg->response.body,
        msg->response.length);
  }

  g_main_loop_quit (src->loop);
  soup_message_io_pause (msg);
}

static void
soup_response (SoupMessage * msg, gpointer user_data)
{
  GstSouphttpSrc *src = (GstSouphttpSrc *) user_data;

  GST_DEBUG_OBJECT (src, "got response %d: %s", msg->status_code,
      msg->reason_phrase);
  g_main_loop_quit (src->loop);
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

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "souphttpsrc", GST_RANK_NONE,
      GST_TYPE_SOUPHTTP_SRC);
}

/* this is the structure that gst-register looks for
 * so keep the name plugin_desc, or you cannot get your plug-in registered */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "soup",
    "libsoup http client src",
    plugin_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
