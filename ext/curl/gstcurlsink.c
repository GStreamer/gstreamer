/* GStreamer
 * Copyright (C) 2011 Axis Communications <dev-gstreamer@axis.com>
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

#include <curl/curl.h>
#include <string.h>
#include <stdio.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstcurlsink.h"

/* Default values */
#define GST_CAT_DEFAULT                gst_curl_sink_debug
#define DEFAULT_URL                    "localhost:5555"
#define DEFAULT_TIMEOUT                30
#define DEFAULT_PROXY_PORT             3128
#define DEFAULT_QOS_DSCP               0
#define DEFAULT_ACCEPT_SELF_SIGNED     FALSE
#define DEFAULT_USE_CONTENT_LENGTH     FALSE

#define DSCP_MIN                       0
#define DSCP_MAX                       63
#define RESPONSE_100_CONTINUE          100
#define RESPONSE_CONNECT_PROXY         200

/* Plugin specific settings */
static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_curl_sink_debug);

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_USER_NAME,
  PROP_USER_PASSWD,
  PROP_PROXY,
  PROP_PROXY_PORT,
  PROP_PROXY_USER_NAME,
  PROP_PROXY_USER_PASSWD,
  PROP_FILE_NAME,
  PROP_TIMEOUT,
  PROP_QOS_DSCP,
  PROP_ACCEPT_SELF_SIGNED,
  PROP_USE_CONTENT_LENGTH,
  PROP_CONTENT_TYPE
};
static gboolean proxy_auth = FALSE;
static gboolean proxy_conn_established = FALSE;

/* Object class function declarations */
static void gst_curl_sink_finalize (GObject * gobject);
static void gst_curl_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_curl_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/* BaseSink class function declarations */
static GstFlowReturn gst_curl_sink_render (GstBaseSink * bsink,
    GstBuffer * buf);
static gboolean gst_curl_sink_event (GstBaseSink * bsink, GstEvent * event);
static gboolean gst_curl_sink_start (GstBaseSink * bsink);
static gboolean gst_curl_sink_stop (GstBaseSink * bsink);
static gboolean gst_curl_sink_unlock (GstBaseSink * bsink);
static gboolean gst_curl_sink_unlock_stop (GstBaseSink * bsink);

/* private functions */
static gboolean gst_curl_sink_transfer_setup_unlocked (GstCurlSink * sink);
static gboolean gst_curl_sink_transfer_set_options_unlocked (GstCurlSink
    * sink);
static gboolean gst_curl_sink_transfer_start_unlocked (GstCurlSink * sink);
static void gst_curl_sink_transfer_cleanup (GstCurlSink * sink);
static size_t gst_curl_sink_transfer_read_cb (void *ptr, size_t size,
    size_t nmemb, void *stream);
static size_t gst_curl_sink_transfer_write_cb (void *ptr, size_t size,
    size_t nmemb, void *stream);
static GstFlowReturn gst_curl_sink_handle_transfer (GstCurlSink * sink);
static int gst_curl_sink_transfer_socket_cb (void *clientp,
    curl_socket_t curlfd, curlsocktype purpose);
static gpointer gst_curl_sink_transfer_thread_func (gpointer data);
static CURLcode gst_curl_sink_transfer_check (GstCurlSink * sink);
static gint gst_curl_sink_setup_dscp_unlocked (GstCurlSink * sink);

static gboolean gst_curl_sink_wait_for_data_unlocked (GstCurlSink * sink);
static void gst_curl_sink_new_file_notify_unlocked (GstCurlSink * sink);
static void gst_curl_sink_transfer_thread_notify_unlocked (GstCurlSink * sink);
static void gst_curl_sink_transfer_thread_close_unlocked (GstCurlSink * sink);
static void gst_curl_sink_wait_for_transfer_thread_to_send_unlocked (GstCurlSink
    * sink);
static void gst_curl_sink_data_sent_notify_unlocked (GstCurlSink * sink);

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (gst_curl_sink_debug, "curlsink", 0,
      "curl sink element");
}

GST_BOILERPLATE_FULL (GstCurlSink, gst_curl_sink, GstBaseSink,
    GST_TYPE_BASE_SINK, _do_init);

static void
gst_curl_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details_simple (element_class,
      "Curl sink",
      "Sink/Network",
      "Send over network using curl", "Patricia Muscalu <patricia@axis.com>");
}

static void
gst_curl_sink_class_init (GstCurlSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = (GstBaseSinkClass *) klass;
  GParamSpec *loc_prspec = g_param_spec_string ("location",
      "Location",
      "URI location to write to",
      NULL,
      G_PARAM_READWRITE);
  GParamSpec *user_prspec = g_param_spec_string ("user",
      "User nanme)",
      "User name to use for server authentication",
      NULL,
      G_PARAM_READWRITE);
  GParamSpec *passwd_prspec = g_param_spec_string ("passwd",
      "User password)",
      "User password to use for server authentication",
      NULL,
      G_PARAM_READWRITE);
  GParamSpec *proxy_prspec = g_param_spec_string ("proxy",
      "proxy",
      "HTTP proxy server URI",
      NULL,
      G_PARAM_READWRITE);

  GParamSpec *proxy_port_prspec = g_param_spec_int ("proxy-port",
      "proxy port",
      "HTTP proxy server port",
      0,
      G_MAXINT,
      DEFAULT_PROXY_PORT,
      G_PARAM_READWRITE);
  GParamSpec *proxy_user_prspec = g_param_spec_string ("proxy-user",
      "Proxy user name)",
      "Proxy user name to use for proxy authentication",
      NULL,
      G_PARAM_READWRITE);
  GParamSpec *proxy_passwd_prspec = g_param_spec_string ("proxy-passwd",
      "Prpxu user password)",
      "Proxy user password to use for proxy authentication",
      NULL,
      G_PARAM_READWRITE);
  GParamSpec *file_name_prspec = g_param_spec_string ("file-name",
      "Base file name",
      "The base file name for the uploaded images",
      NULL,
      G_PARAM_READWRITE);

  GParamSpec *timeout_prspec = g_param_spec_int ("timeout",
      "timeout",
      "Number of seconds waiting to write before timeout",
      0,
      G_MAXINT,
      DEFAULT_TIMEOUT,
      G_PARAM_READWRITE);
  GParamSpec *qos_dscp_prspec = g_param_spec_int ("qos-dscp",
      "QoS diff srv code point",
      "Quality of Service, differentiated services code point (0 default)",
      DSCP_MIN,
      DSCP_MAX,
      DEFAULT_QOS_DSCP,
      G_PARAM_READWRITE);
  GParamSpec *self_cert_prspec = g_param_spec_boolean ("accept-self-signed",
      "Accept self-signed certificates",
      "Accept self-signed SSL/TLS certificates",
      DEFAULT_ACCEPT_SELF_SIGNED,
      G_PARAM_READWRITE);
  GParamSpec *content_lngth_prspec = g_param_spec_boolean ("use-content-length",
      "Use content length header",
      "Use the Content-Length HTTP header instead of Transfer-Encoding header",
      DEFAULT_USE_CONTENT_LENGTH,
      G_PARAM_READWRITE);
  GParamSpec *content_type_prspec = g_param_spec_string ("content-type",
      "Content type header)",
      "The mime type of the body of the request",
      NULL,
      G_PARAM_READWRITE);

  GST_DEBUG_OBJECT (klass, "class_init");

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_curl_sink_event);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_curl_sink_render);
  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_curl_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_curl_sink_stop);
  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_curl_sink_unlock);
  gstbasesink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_curl_sink_unlock_stop);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_curl_sink_finalize);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_curl_sink_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_curl_sink_get_property);
  g_object_class_install_property (gobject_class, PROP_LOCATION, loc_prspec);
  g_object_class_install_property (gobject_class, PROP_USER_NAME, user_prspec);
  g_object_class_install_property (gobject_class, PROP_USER_PASSWD,
      passwd_prspec);
  g_object_class_install_property (gobject_class, PROP_PROXY, proxy_prspec);
  g_object_class_install_property (gobject_class, PROP_PROXY_PORT,
      proxy_port_prspec);
  g_object_class_install_property (gobject_class, PROP_PROXY_USER_NAME,
      proxy_user_prspec);
  g_object_class_install_property (gobject_class, PROP_PROXY_USER_PASSWD,
      proxy_passwd_prspec);
  g_object_class_install_property (gobject_class, PROP_FILE_NAME,
      file_name_prspec);
  g_object_class_install_property (gobject_class, PROP_TIMEOUT, timeout_prspec);
  g_object_class_install_property (gobject_class, PROP_QOS_DSCP,
      qos_dscp_prspec);
  g_object_class_install_property (gobject_class, PROP_ACCEPT_SELF_SIGNED,
      self_cert_prspec);
  g_object_class_install_property (gobject_class, PROP_USE_CONTENT_LENGTH,
      content_lngth_prspec);
  g_object_class_install_property (gobject_class, PROP_CONTENT_TYPE,
      content_type_prspec);

  g_type_class_add_private (klass, sizeof (GstCurlSinkPrivate));
}

static void
gst_curl_sink_init (GstCurlSink * sink, GstCurlSinkClass * klass)
{
  sink->priv = G_TYPE_INSTANCE_GET_PRIVATE (sink, GST_TYPE_CURL_SINK,
      GstCurlSinkPrivate);

  sink->priv->transfer_buf = g_malloc (sizeof (TransferBuffer));
  sink->priv->transfer_cond = g_malloc (sizeof (TransferCondition));
  sink->priv->transfer_cond->cond = g_cond_new ();
  sink->priv->transfer_cond->data_sent = FALSE;
  sink->priv->transfer_cond->data_available = FALSE;
  sink->priv->timeout = DEFAULT_TIMEOUT;
  sink->priv->proxy_port = DEFAULT_PROXY_PORT;
  sink->priv->qos_dscp = DEFAULT_QOS_DSCP;
  sink->priv->url = g_strdup (DEFAULT_URL);
  sink->priv->header_list = NULL;
  sink->priv->accept_self_signed = DEFAULT_ACCEPT_SELF_SIGNED;
  sink->priv->use_content_length = DEFAULT_USE_CONTENT_LENGTH;
  sink->priv->transfer_thread_close = FALSE;
  sink->priv->new_file = TRUE;
  sink->priv->proxy_headers_set = FALSE;
  sink->priv->content_type = NULL;
}

static void
gst_curl_sink_finalize (GObject * gobject)
{
  GstCurlSink *this = GST_CURL_SINK (gobject);

  GST_DEBUG ("finalizing curlsink");
  if (this->priv->transfer_thread != NULL) {
    g_thread_join (this->priv->transfer_thread);
  }

  gst_curl_sink_transfer_cleanup (this);
  g_cond_free (this->priv->transfer_cond->cond);
  g_free (this->priv->transfer_cond);

  g_free (this->priv->transfer_buf);

  g_free (this->priv->url);
  g_free (this->priv->user);
  g_free (this->priv->passwd);
  g_free (this->priv->proxy);
  g_free (this->priv->proxy_user);
  g_free (this->priv->proxy_passwd);
  g_free (this->priv->file_name);
  g_free (this->priv->content_type);

  if (this->priv->header_list) {
    curl_slist_free_all (this->priv->header_list);
    this->priv->header_list = NULL;
  }

  if (this->priv->fdset != NULL) {
    gst_poll_free (this->priv->fdset);
    this->priv->fdset = NULL;
  }
  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static GstFlowReturn
gst_curl_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstCurlSink *sink = GST_CURL_SINK (bsink);
  guint8 *data;
  size_t size;
  GstFlowReturn ret;

  GST_LOG ("enter render");

  sink = GST_CURL_SINK (bsink);
  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  if (sink->priv->content_type == NULL) {
    GstCaps *caps;
    GstStructure *structure;
    const gchar *mime_type;

    caps = buf->caps;
    structure = gst_caps_get_structure (caps, 0);
    mime_type = gst_structure_get_name (structure);
    sink->priv->content_type = g_strdup (mime_type);
  }

  GST_OBJECT_LOCK (sink);

  /* check if the transfer thread has encountered problems while the
   * pipeline thread was working elsewhere */
  if (sink->priv->flow_ret != GST_FLOW_OK) {
    goto done;
  }

  g_assert (sink->priv->transfer_cond->data_available == FALSE);

  /* if there is no transfer thread created, lets create one */
  if (sink->priv->transfer_thread == NULL) {
    if (!gst_curl_sink_transfer_start_unlocked (sink)) {
      sink->priv->flow_ret = GST_FLOW_ERROR;
      goto done;
    }
  }

  /* make data available for the transfer thread and notify */
  sink->priv->transfer_buf->ptr = data;
  sink->priv->transfer_buf->len = size;
  sink->priv->transfer_buf->offset = 0;
  gst_curl_sink_transfer_thread_notify_unlocked (sink);

  /* wait for the transfer thread to send the data. This will be notified
   * either when transfer is completed by the curl read callback or by
   * the thread function if an error has occured. */
  gst_curl_sink_wait_for_transfer_thread_to_send_unlocked (sink);

done:
  ret = sink->priv->flow_ret;
  GST_OBJECT_UNLOCK (sink);

  GST_LOG ("exit render");

  return ret;
}

static gboolean
gst_curl_sink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstCurlSink *sink = GST_CURL_SINK (bsink);

  switch (event->type) {
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (sink, "received EOS");
      GST_OBJECT_LOCK (sink);
      gst_curl_sink_transfer_thread_close_unlocked (sink);
      GST_OBJECT_UNLOCK (sink);
      if (sink->priv->transfer_thread != NULL) {
        g_thread_join (sink->priv->transfer_thread);
        sink->priv->transfer_thread = NULL;
      }
      break;
    default:
      break;
  }
  return TRUE;
}

static gboolean
gst_curl_sink_start (GstBaseSink * bsink)
{
  GstCurlSink *sink;

  sink = GST_CURL_SINK (bsink);

  if ((sink->priv->fdset = gst_poll_new (TRUE)) == NULL) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_READ_WRITE,
        ("gst_poll_new failed: %s", g_strerror (errno)), (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_curl_sink_stop (GstBaseSink * bsink)
{
  GstCurlSink *sink = GST_CURL_SINK (bsink);

  GST_OBJECT_LOCK (sink);
  gst_curl_sink_transfer_thread_close_unlocked (sink);
  GST_OBJECT_UNLOCK (sink);
  if (sink->priv->fdset != NULL) {
    gst_poll_free (sink->priv->fdset);
    sink->priv->fdset = NULL;
  }

  return TRUE;
}

static gboolean
gst_curl_sink_unlock (GstBaseSink * bsink)
{
  GstCurlSink *sink;

  sink = GST_CURL_SINK (bsink);

  GST_LOG_OBJECT (sink, "Flushing");
  gst_poll_set_flushing (sink->priv->fdset, TRUE);

  return TRUE;
}

static gboolean
gst_curl_sink_unlock_stop (GstBaseSink * bsink)
{
  GstCurlSink *sink;

  sink = GST_CURL_SINK (bsink);

  GST_LOG_OBJECT (sink, "No longer flushing");
  gst_poll_set_flushing (sink->priv->fdset, FALSE);

  return TRUE;
}

static void
gst_curl_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCurlSink *sink;
  GstState cur_state;

  g_return_if_fail (GST_IS_CURL_SINK (object));
  sink = GST_CURL_SINK (object);

  gst_element_get_state (GST_ELEMENT (sink), &cur_state, NULL, 0);
  if (cur_state != GST_STATE_PLAYING && cur_state != GST_STATE_PAUSED) {
    GST_OBJECT_LOCK (sink);

    switch (prop_id) {
      case PROP_LOCATION:
        g_free (sink->priv->url);
        sink->priv->url = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "url set to %s", sink->priv->url);
        break;
      case PROP_USER_NAME:
        g_free (sink->priv->user);
        sink->priv->user = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "user set to %s", sink->priv->user);
        break;
      case PROP_USER_PASSWD:
        g_free (sink->priv->passwd);
        sink->priv->passwd = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "passwd set to %s", sink->priv->passwd);
        break;
      case PROP_PROXY:
        g_free (sink->priv->proxy);
        sink->priv->proxy = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "proxy set to %s", sink->priv->proxy);
        break;
      case PROP_PROXY_PORT:
        sink->priv->proxy_port = g_value_get_int (value);
        GST_DEBUG_OBJECT (sink, "proxy port set to %d", sink->priv->proxy_port);
        break;
      case PROP_PROXY_USER_NAME:
        g_free (sink->priv->proxy_user);
        sink->priv->proxy_user = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "proxy user set to %s", sink->priv->proxy_user);
        break;
      case PROP_PROXY_USER_PASSWD:
        g_free (sink->priv->proxy_passwd);
        sink->priv->proxy_passwd = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "proxy password set to %s",
            sink->priv->proxy_passwd);
        break;
      case PROP_FILE_NAME:
        g_free (sink->priv->file_name);
        sink->priv->file_name = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "file_name set to %s", sink->priv->file_name);
        break;
      case PROP_TIMEOUT:
        sink->priv->timeout = g_value_get_int (value);
        GST_DEBUG_OBJECT (sink, "timeout set to %d", sink->priv->timeout);
        break;
      case PROP_QOS_DSCP:
        sink->priv->qos_dscp = g_value_get_int (value);
        gst_curl_sink_setup_dscp_unlocked (sink);
        GST_DEBUG_OBJECT (sink, "dscp set to %d", sink->priv->qos_dscp);
        break;
      case PROP_ACCEPT_SELF_SIGNED:
        sink->priv->accept_self_signed = g_value_get_boolean (value);
        GST_DEBUG_OBJECT (sink, "accept_self_signed set to %d",
            sink->priv->accept_self_signed);
        break;
      case PROP_USE_CONTENT_LENGTH:
        sink->priv->use_content_length = g_value_get_boolean (value);
        GST_DEBUG_OBJECT (sink, "use_content_length set to %d",
            sink->priv->use_content_length);
        break;
      case PROP_CONTENT_TYPE:
        g_free (sink->priv->content_type);
        sink->priv->content_type = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "content type set to %s",
            sink->priv->content_type);
        break;
      default:
        GST_DEBUG_OBJECT (sink, "invalid property id %d", prop_id);
        break;
    }

    GST_OBJECT_UNLOCK (sink);

    return;
  }

  /* in PLAYING or PAUSED state */
  GST_OBJECT_LOCK (sink);

  switch (prop_id) {
    case PROP_FILE_NAME:
      g_free (sink->priv->file_name);
      sink->priv->file_name = g_value_dup_string (value);
      GST_DEBUG_OBJECT (sink, "file_name set to %s", sink->priv->file_name);
      gst_curl_sink_new_file_notify_unlocked (sink);
      break;
    case PROP_TIMEOUT:
      sink->priv->timeout = g_value_get_int (value);
      GST_DEBUG_OBJECT (sink, "timeout set to %d", sink->priv->timeout);
      break;
    case PROP_QOS_DSCP:
      sink->priv->qos_dscp = g_value_get_int (value);
      gst_curl_sink_setup_dscp_unlocked (sink);
      GST_DEBUG_OBJECT (sink, "dscp set to %d", sink->priv->qos_dscp);
      break;
    case PROP_CONTENT_TYPE:
      g_free (sink->priv->content_type);
      sink->priv->content_type = g_value_dup_string (value);
      GST_DEBUG_OBJECT (sink, "content type set to %s",
          sink->priv->content_type);
      break;
    default:
      GST_WARNING_OBJECT (sink, "cannot set property when PLAYING");
      break;
  }

  GST_OBJECT_UNLOCK (sink);
}

static void
gst_curl_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCurlSink *sink;

  g_return_if_fail (GST_IS_CURL_SINK (object));
  sink = GST_CURL_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, sink->priv->url);
      break;
    case PROP_USER_NAME:
      g_value_set_string (value, sink->priv->user);
      break;
    case PROP_USER_PASSWD:
      g_value_set_string (value, sink->priv->passwd);
      break;
    case PROP_PROXY:
      g_value_set_string (value, sink->priv->proxy);
      break;
    case PROP_PROXY_PORT:
      g_value_set_int (value, sink->priv->proxy_port);
      break;
    case PROP_PROXY_USER_NAME:
      g_value_set_string (value, sink->priv->proxy_user);
      break;
    case PROP_PROXY_USER_PASSWD:
      g_value_set_string (value, sink->priv->proxy_passwd);
      break;
    case PROP_FILE_NAME:
      g_value_set_string (value, sink->priv->file_name);
      break;
    case PROP_TIMEOUT:
      g_value_set_int (value, sink->priv->timeout);
      break;
    case PROP_QOS_DSCP:
      g_value_set_int (value, sink->priv->qos_dscp);
      break;
    case PROP_ACCEPT_SELF_SIGNED:
      g_value_set_boolean (value, sink->priv->accept_self_signed);
      break;
    case PROP_USE_CONTENT_LENGTH:
      g_value_set_boolean (value, sink->priv->use_content_length);
      break;
    case PROP_CONTENT_TYPE:
      g_value_set_string (value, sink->priv->content_type);
      break;
    default:
      GST_DEBUG_OBJECT (sink, "invalid property id");
      break;
  }
}

static void
gst_curl_sink_set_http_header_unlocked (GstCurlSink * sink)
{
  gchar *tmp;

  if (sink->priv->header_list) {
    curl_slist_free_all (sink->priv->header_list);
    sink->priv->header_list = NULL;
  }

  if (proxy_auth && !sink->priv->proxy_headers_set && !proxy_conn_established) {
    sink->priv->header_list =
        curl_slist_append (sink->priv->header_list, "Content-Length: 0");
    sink->priv->proxy_headers_set = TRUE;
    goto set_headers;
  }
  if (sink->priv->use_content_length) {
    /* if content length is used we assume that every buffer is one
     * entire file, which is the case when uploading several jpegs */
    tmp =
        g_strdup_printf ("Content-Length: %d",
        (int) sink->priv->transfer_buf->len);
    sink->priv->header_list = curl_slist_append (sink->priv->header_list, tmp);
    g_free (tmp);
  } else {
    /* when sending a POST request to a HTTP 1.1 server, you can send data
     * without knowing the size before starting the POST if you use chunked
     * encoding */
    sink->priv->header_list = curl_slist_append (sink->priv->header_list,
        "Transfer-Encoding: chunked");
  }

  tmp = g_strdup_printf ("Content-Type: %s", sink->priv->content_type);
  sink->priv->header_list = curl_slist_append (sink->priv->header_list, tmp);
  g_free (tmp);

set_headers:

  tmp = g_strdup_printf ("Content-Disposition: attachment; filename="
      "\"%s\"", sink->priv->file_name);
  sink->priv->header_list = curl_slist_append (sink->priv->header_list, tmp);
  g_free (tmp);
  curl_easy_setopt (sink->priv->curl, CURLOPT_HTTPHEADER,
      sink->priv->header_list);
}

static gboolean
gst_curl_sink_transfer_set_options_unlocked (GstCurlSink * sink)
{
#ifdef DEBUG
  curl_easy_setopt (sink->priv->curl, CURLOPT_VERBOSE, 1);
#endif

  curl_easy_setopt (sink->priv->curl, CURLOPT_URL, sink->priv->url);
  curl_easy_setopt (sink->priv->curl, CURLOPT_CONNECTTIMEOUT,
      sink->priv->timeout);

  curl_easy_setopt (sink->priv->curl, CURLOPT_SOCKOPTDATA, sink);
  curl_easy_setopt (sink->priv->curl, CURLOPT_SOCKOPTFUNCTION,
      gst_curl_sink_transfer_socket_cb);

  if (sink->priv->user != NULL && strlen (sink->priv->user)) {
    curl_easy_setopt (sink->priv->curl, CURLOPT_USERNAME, sink->priv->user);
    curl_easy_setopt (sink->priv->curl, CURLOPT_PASSWORD, sink->priv->passwd);
    curl_easy_setopt (sink->priv->curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
  }

  if (sink->priv->accept_self_signed && g_str_has_prefix (sink->priv->url,
          "https")) {
    /* TODO verify the authenticity of the peer's certificate */
    curl_easy_setopt (sink->priv->curl, CURLOPT_SSL_VERIFYPEER, 0L);
    /* TODO check the servers's claimed identity */
    curl_easy_setopt (sink->priv->curl, CURLOPT_SSL_VERIFYHOST, 0L);
  }

  /* proxy settings */
  if (sink->priv->proxy != NULL && strlen (sink->priv->proxy)) {
    if (curl_easy_setopt (sink->priv->curl, CURLOPT_PROXY, sink->priv->proxy)
        != CURLE_OK) {
      return FALSE;
    }
    if (curl_easy_setopt (sink->priv->curl, CURLOPT_PROXYPORT,
            sink->priv->proxy_port)
        != CURLE_OK) {
      return FALSE;
    }
    if (sink->priv->proxy_user != NULL &&
        strlen (sink->priv->proxy_user) &&
        sink->priv->proxy_passwd != NULL && strlen (sink->priv->proxy_passwd)) {
      curl_easy_setopt (sink->priv->curl, CURLOPT_PROXYUSERNAME,
          sink->priv->proxy_user);
      curl_easy_setopt (sink->priv->curl, CURLOPT_PROXYPASSWORD,
          sink->priv->proxy_passwd);
      curl_easy_setopt (sink->priv->curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
      proxy_auth = TRUE;
    }
    /* tunnel all operations through a given HTTP proxy */
    if (curl_easy_setopt (sink->priv->curl, CURLOPT_HTTPPROXYTUNNEL, 1L)
        != CURLE_OK) {
      return FALSE;
    }
  }

  /* POST options */
  curl_easy_setopt (sink->priv->curl, CURLOPT_POST, 1L);

  curl_easy_setopt (sink->priv->curl, CURLOPT_READFUNCTION,
      gst_curl_sink_transfer_read_cb);
  curl_easy_setopt (sink->priv->curl, CURLOPT_READDATA, sink);
  curl_easy_setopt (sink->priv->curl, CURLOPT_WRITEFUNCTION,
      gst_curl_sink_transfer_write_cb);

  return TRUE;
}

static size_t
gst_curl_sink_transfer_read_cb (void *curl_ptr, size_t size, size_t nmemb,
    void *stream)
{
  GstCurlSink *sink;
  GstCurlSinkPrivate *pr;
  TransferBuffer *buffer;
  size_t max_bytes_to_send;
  guint buf_len;

  sink = (GstCurlSink *) stream;

  /* wait for data to come available, if new file or thread close is set
   * then zero will be returned to indicate end of current transfer */
  GST_OBJECT_LOCK (sink);
  if (gst_curl_sink_wait_for_data_unlocked (sink) == FALSE) {
    GST_LOG ("returning 0, no more data to send in this file");
    GST_OBJECT_UNLOCK (sink);
    return 0;
  }
  GST_OBJECT_UNLOCK (sink);


  max_bytes_to_send = size * nmemb;
  pr = sink->priv;
  buffer = pr->transfer_buf;

  buf_len = buffer->len;
  GST_LOG ("write buf len=%d, offset=%d", buffer->len, buffer->offset);

  /* more data in buffer */
  if (buffer->len > 0) {
    size_t bytes_to_send = MIN (max_bytes_to_send, buf_len);

    memcpy ((guint8 *) curl_ptr, buffer->ptr + buffer->offset, bytes_to_send);

    buffer->offset = buffer->offset + bytes_to_send;
    buffer->len = buffer->len - bytes_to_send;

    /* the last data chunk */
    if (bytes_to_send == buf_len) {
      buffer->ptr = NULL;
      buffer->offset = 0;
      buffer->len = 0;
      GST_OBJECT_LOCK (sink);
      gst_curl_sink_data_sent_notify_unlocked (sink);
      GST_OBJECT_UNLOCK (sink);
    }

    GST_LOG ("sent : %d (%x)", bytes_to_send, bytes_to_send);

    return bytes_to_send;
  } else {
    GST_WARNING ("got zero-length buffer");
    return 0;
  }
}

static size_t
gst_curl_sink_transfer_write_cb (void G_GNUC_UNUSED * ptr, size_t size,
    size_t nmemb, void G_GNUC_UNUSED * stream)
{
  size_t realsize = size * nmemb;

  GST_DEBUG ("response %s", (gchar *) ptr);
  return realsize;
}

static CURLcode
gst_curl_sink_transfer_check (GstCurlSink * sink)
{
  CURLcode code = CURLE_OK;
  CURL *easy;
  CURLMsg *msg;
  gint msgs_left;
  gchar *eff_url = NULL;

  do {
    easy = NULL;
    while ((msg = curl_multi_info_read (sink->priv->multi_handle, &msgs_left))) {
      if (msg->msg == CURLMSG_DONE) {
        easy = msg->easy_handle;
        code = msg->data.result;
        break;
      }
    }
    if (easy) {
      curl_easy_getinfo (easy, CURLINFO_EFFECTIVE_URL, &eff_url);
      GST_DEBUG ("transfer done %s (%s-%d)\n", eff_url,
          curl_easy_strerror (code), code);
    }
  } while (easy);

  return code;
}

static GstFlowReturn
gst_curl_sink_handle_transfer (GstCurlSink * sink)
{
  gint retval;
  gint running_handles;
  gint timeout;
  CURLMcode m_code;
  CURLcode e_code;
  glong resp = -1;
  glong resp_proxy = -1;

  GST_OBJECT_LOCK (sink);
  timeout = sink->priv->timeout;
  GST_OBJECT_UNLOCK (sink);

  /* Receiving CURLM_CALL_MULTI_PERFORM means that libcurl may have more data
     available to send or receive - call simply curl_multi_perform before
     poll() on more actions */
  do {
    m_code = curl_multi_perform (sink->priv->multi_handle, &running_handles);
  } while (m_code == CURLM_CALL_MULTI_PERFORM);

  while (running_handles && (m_code == CURLM_OK)) {
    if (!proxy_conn_established && (resp_proxy != RESPONSE_CONNECT_PROXY)
        && proxy_auth) {
      curl_easy_getinfo (sink->priv->curl, CURLINFO_HTTP_CONNECTCODE,
          &resp_proxy);
      if ((resp_proxy == RESPONSE_CONNECT_PROXY)) {
        GST_LOG ("received HTTP/1.0 200 Connection Established");
        /* Workaround: redefine HTTP headers before connecting to HTTP server.
         * When talking to proxy, the Content-Length: 0 is send with the request.
         */
        curl_multi_remove_handle (sink->priv->multi_handle, sink->priv->curl);
        gst_curl_sink_set_http_header_unlocked (sink);
        curl_multi_add_handle (sink->priv->multi_handle, sink->priv->curl);
        proxy_conn_established = TRUE;
      }
    }

    retval = gst_poll_wait (sink->priv->fdset, timeout * GST_SECOND);
    if (G_UNLIKELY (retval == -1)) {
      if (errno == EAGAIN || errno == EINTR) {
        GST_DEBUG_OBJECT (sink, "interrupted by signal");
      } else if (errno == EBUSY) {
        goto poll_stopped;
      } else {
        goto poll_error;
      }
    } else if (G_UNLIKELY (retval == 0)) {
      GST_DEBUG ("timeout");
      goto poll_timeout;
    }

    /* readable/writable sockets */
    do {
      m_code = curl_multi_perform (sink->priv->multi_handle, &running_handles);
    } while (m_code == CURLM_CALL_MULTI_PERFORM);

    if (resp != RESPONSE_100_CONTINUE) {
      curl_easy_getinfo (sink->priv->curl, CURLINFO_RESPONSE_CODE, &resp);
    }
  }

  if (resp != RESPONSE_100_CONTINUE) {
    /* No 100 Continue response received. Using POST with HTTP 1.1 implies
     * the use of a "Expect: 100-continue" header. If the server doesn't
     * send HTTP/1.1 100 Continue, libcurl will not call transfer_read_cb
     * in order to send POST data.
     */
    goto no_100_continue_response;
  }

  if (m_code != CURLM_OK) {
    goto curl_multi_error;
  }

  /* problems still might have occurred on individual transfers even when
   * curl_multi_perform returns CURLM_OK */
  if ((e_code = gst_curl_sink_transfer_check (sink)) != CURLE_OK) {
    goto curl_easy_error;
  }

  /* check response code */
  curl_easy_getinfo (sink->priv->curl, CURLINFO_RESPONSE_CODE, &resp);
  GST_DEBUG_OBJECT (sink, "response code: %d", resp);
  if (resp < 200 || resp >= 300) {
    goto response_error;
  }

  return GST_FLOW_OK;

poll_error:
  {
    GST_DEBUG_OBJECT (sink, "poll failed: %s", g_strerror (errno));
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, ("poll failed"), (NULL));
    return GST_FLOW_ERROR;
  }

poll_stopped:
  {
    GST_DEBUG_OBJECT (sink, "poll stopped");
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, ("poll stopped"), (NULL));
    return GST_FLOW_ERROR;
  }

poll_timeout:
  {
    GST_DEBUG_OBJECT (sink, "poll timed out");
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, ("poll timed out"), (NULL));
    return GST_FLOW_ERROR;
  }

curl_multi_error:
  {
    GST_DEBUG_OBJECT (sink, "curl multi error");
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (curl_multi_strerror (m_code)),
        (NULL));
    return GST_FLOW_ERROR;
  }

curl_easy_error:
  {
    GST_DEBUG_OBJECT (sink, "curl easy error");
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (curl_easy_strerror (e_code)),
        (NULL));
    return GST_FLOW_ERROR;
  }

no_100_continue_response:
  {
    GST_DEBUG_OBJECT (sink, "100 continue response missing");
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, ("100 continue response missing"),
        (NULL));
    return GST_FLOW_ERROR;
  }

response_error:
  {
    GST_DEBUG_OBJECT (sink, "response error");
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, ("response error: %ld", resp),
        (NULL));
    return GST_FLOW_ERROR;
  }
}

/* This function gets called by libcurl after the socket() call but before
 * the connect() call. */
static int
gst_curl_sink_transfer_socket_cb (void *clientp, curl_socket_t curlfd,
    curlsocktype G_GNUC_UNUSED purpose)
{
  GstCurlSink *sink;
  gboolean ret = TRUE;

  sink = (GstCurlSink *) clientp;

  g_assert (sink);

  if (curlfd < 0) {
    /* signal an unrecoverable error to the library which will close the socket
       and return CURLE_COULDNT_CONNECT
     */
    return 1;
  }

  gst_poll_fd_init (&sink->priv->fd);
  sink->priv->fd.fd = curlfd;

  ret = ret && gst_poll_add_fd (sink->priv->fdset, &sink->priv->fd);
  ret = ret && gst_poll_fd_ctl_write (sink->priv->fdset, &sink->priv->fd, TRUE);
  ret = ret && gst_poll_fd_ctl_read (sink->priv->fdset, &sink->priv->fd, TRUE);
  GST_DEBUG ("fd: %d", sink->priv->fd.fd);
  GST_OBJECT_LOCK (sink);
  gst_curl_sink_setup_dscp_unlocked (sink);
  GST_OBJECT_UNLOCK (sink);

  /* success */
  if (ret) {
    return 0;
  } else {
    return 1;
  }
}

static gboolean
gst_curl_sink_transfer_start_unlocked (GstCurlSink * sink)
{
  GError *error = NULL;
  gboolean ret = TRUE;

  GST_LOG ("creating transfer thread");
  sink->priv->transfer_thread_close = FALSE;
  sink->priv->new_file = TRUE;
  sink->priv->transfer_thread =
      g_thread_create ((GThreadFunc) gst_curl_sink_transfer_thread_func, sink,
      TRUE, &error);

  if (sink->priv->transfer_thread == NULL || error != NULL) {
    ret = FALSE;
    if (error) {
      GST_ERROR_OBJECT (sink, "could not create thread %s", error->message);
      g_error_free (error);
    } else {
      GST_ERROR_OBJECT (sink, "could not create thread for unknown reason");
    }
  }

  return ret;
}

static gpointer
gst_curl_sink_transfer_thread_func (gpointer data)
{
  GstCurlSink *sink = (GstCurlSink *) data;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean data_available;

  GST_LOG ("transfer thread started");
  GST_OBJECT_LOCK (sink);
  if (!gst_curl_sink_transfer_setup_unlocked (sink)) {
    GST_DEBUG_OBJECT (sink, "curl setup error");
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, ("curl setup error"), (NULL));
    sink->priv->flow_ret = GST_FLOW_ERROR;
    goto done;
  }

  while (!sink->priv->transfer_thread_close &&
      sink->priv->flow_ret == GST_FLOW_OK) {
    /* we are working on a new file, clearing flag and setting file
     * name in http header */
    sink->priv->new_file = FALSE;

    /* wait for data to arrive for this new file, if we get a new file name
     * again before getting data we will simply skip transfering anything
     * for this file and go directly to the new file */
    data_available = gst_curl_sink_wait_for_data_unlocked (sink);
    if (data_available) {
      gst_curl_sink_set_http_header_unlocked (sink);
    }

    /* stay unlocked while handling the actual transfer */
    GST_OBJECT_UNLOCK (sink);

    if (data_available) {
      curl_multi_add_handle (sink->priv->multi_handle, sink->priv->curl);

      /* Start driving the transfer. */
      ret = gst_curl_sink_handle_transfer (sink);

      /* easy handle will be possibly re-used for next transfer, thus it needs to
       * be removed from the multi stack and re-added again */
      curl_multi_remove_handle (sink->priv->multi_handle, sink->priv->curl);
    }

    /* lock again before looping to check the thread closed flag */
    GST_OBJECT_LOCK (sink);

    /* if we have transfered data, then set the return code */
    if (data_available) {
      sink->priv->flow_ret = ret;
    }
  }

done:
  /* if there is a flow error, always notify the render function so it
   * can return the flow error up along the pipeline */
  if (sink->priv->flow_ret != GST_FLOW_OK) {
    gst_curl_sink_data_sent_notify_unlocked (sink);
  }

  GST_OBJECT_UNLOCK (sink);
  GST_DEBUG ("exit thread func - transfer thread close flag: %d",
      sink->priv->transfer_thread_close);

  return NULL;
}

static gboolean
gst_curl_sink_transfer_setup_unlocked (GstCurlSink * sink)
{
  g_assert (sink);

  if (sink->priv->curl == NULL) {
    /* curl_easy_init automatically calls curl_global_init(3) */
    if ((sink->priv->curl = curl_easy_init ()) == NULL) {
      g_warning ("Failed to init easy handle");
      return FALSE;
    }
  }

  if (!gst_curl_sink_transfer_set_options_unlocked (sink)) {
    g_warning ("Failed to setup easy handle");
    GST_OBJECT_UNLOCK (sink);
    return FALSE;
  }

  /* init a multi stack (non-blocking interface to liburl) */
  if (sink->priv->multi_handle == NULL) {
    if ((sink->priv->multi_handle = curl_multi_init ()) == NULL) {
      return FALSE;
    }
  }

  return TRUE;
}

static void
gst_curl_sink_transfer_cleanup (GstCurlSink * sink)
{
  if (sink->priv->curl != NULL) {
    if (sink->priv->multi_handle != NULL) {
      curl_multi_remove_handle (sink->priv->multi_handle, sink->priv->curl);
    }
    curl_easy_cleanup (sink->priv->curl);
    sink->priv->curl = NULL;
  }

  if (sink->priv->multi_handle != NULL) {
    curl_multi_cleanup (sink->priv->multi_handle);
    sink->priv->multi_handle = NULL;
  }
}

static gboolean
gst_curl_sink_wait_for_data_unlocked (GstCurlSink * sink)
{
  gboolean data_available = FALSE;

  GST_LOG ("waiting for data");
  while (!sink->priv->transfer_cond->data_available &&
      !sink->priv->transfer_thread_close && !sink->priv->new_file) {
    g_cond_wait (sink->priv->transfer_cond->cond, GST_OBJECT_GET_LOCK (sink));
  }

  if (sink->priv->transfer_thread_close) {
    GST_LOG ("wait for data aborted due to thread close");
  } else if (sink->priv->new_file) {
    GST_LOG ("wait for data aborted due to new file name");
  } else {
    GST_LOG ("wait for data completed");
    data_available = TRUE;
  }

  return data_available;
}

static void
gst_curl_sink_transfer_thread_notify_unlocked (GstCurlSink * sink)
{
  GST_LOG ("more data to send");
  sink->priv->transfer_cond->data_available = TRUE;
  sink->priv->transfer_cond->data_sent = FALSE;
  g_cond_signal (sink->priv->transfer_cond->cond);
}

static void
gst_curl_sink_new_file_notify_unlocked (GstCurlSink * sink)
{
  GST_LOG ("new file name");
  sink->priv->new_file = TRUE;
  g_cond_signal (sink->priv->transfer_cond->cond);
}

static void
gst_curl_sink_transfer_thread_close_unlocked (GstCurlSink * sink)
{
  GST_LOG ("setting transfer thread close flag");
  sink->priv->transfer_thread_close = TRUE;
  g_cond_signal (sink->priv->transfer_cond->cond);
}

static void
gst_curl_sink_wait_for_transfer_thread_to_send_unlocked (GstCurlSink * sink)
{
  GST_LOG ("waiting for buffer send to complete");

  /* this function should not check if the transfer thread is set to be closed
   * since that flag only can be set by the EoS event (by the pipeline thread).
   * This can therefore never happen while this function is running since this
   * function also is called by the pipeline thread (in the render function) */
  while (!sink->priv->transfer_cond->data_sent) {
    g_cond_wait (sink->priv->transfer_cond->cond, GST_OBJECT_GET_LOCK (sink));
  }
  GST_LOG ("buffer send completed");
}

static void
gst_curl_sink_data_sent_notify_unlocked (GstCurlSink * sink)
{
  GST_LOG ("transfer completed");
  sink->priv->transfer_cond->data_available = FALSE;
  sink->priv->transfer_cond->data_sent = TRUE;
  g_cond_signal (sink->priv->transfer_cond->cond);
}

static gint
gst_curl_sink_setup_dscp_unlocked (GstCurlSink * sink)
{
  gint tos;
  gint af;
  gint ret = -1;
  union
  {
    struct sockaddr sa;
    struct sockaddr_in6 sa_in6;
    struct sockaddr_storage sa_stor;
  } sa;
  socklen_t slen = sizeof (sa);

  if (getsockname (sink->priv->fd.fd, &sa.sa, &slen) < 0) {
    GST_DEBUG_OBJECT (sink, "could not get sockname: %s", g_strerror (errno));
    return ret;
  }
  af = sa.sa.sa_family;

  /* if this is an IPv4-mapped address then do IPv4 QoS */
  if (af == AF_INET6) {
    GST_DEBUG_OBJECT (sink, "check IP6 socket");
    if (IN6_IS_ADDR_V4MAPPED (&(sa.sa_in6.sin6_addr))) {
      GST_DEBUG_OBJECT (sink, "mapped to IPV4");
      af = AF_INET;
    }
  }
  /* extract and shift 6 bits of the DSCP */
  tos = (sink->priv->qos_dscp & 0x3f) << 2;

  switch (af) {
    case AF_INET:
      ret = setsockopt (sink->priv->fd.fd, IPPROTO_IP, IP_TOS, &tos,
          sizeof (tos));
      break;
    case AF_INET6:
#ifdef IPV6_TCLASS
      ret = setsockopt (sink->priv->fd.fd, IPPROTO_IPV6, IPV6_TCLASS, &tos,
          sizeof (tos));
      break;
#endif
    default:
      GST_ERROR_OBJECT (sink, "unsupported AF");
      break;
  }
  if (ret) {
    GST_DEBUG_OBJECT (sink, "could not set DSCP: %s", g_strerror (errno));
  }

  return ret;
}
