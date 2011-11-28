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

/**
 * SECTION:element-curlsink
 * @short_description: sink that uploads data to a server using libcurl
 * @see_also:
 *
 * This is a network sink that uses libcurl as a client to upload data to
 * a server (e.g. a HTTP/FTP server).
 *
 * <refsect2>
 * <title>Example launch line (upload a JPEG file to an HTTP server)</title>
 * |[
 * gst-launch filesrc filesrc location=image.jpg ! jpegparse ! curlsink  \
 *     file-name=image.jpg  \
 *     location=http://192.168.0.1:8080/cgi-bin/patupload.cgi/  \
 *     user=test passwd=test  \
 *     content-type=image/jpeg  \
 *     use-content-length=false
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

  gst_element_class_add_static_pad_template (element_class, &sinktemplate);
  gst_element_class_set_details_simple (element_class,
      "Curl sink",
      "Sink/Network",
      "Upload data over the network to a server using libcurl",
      "Patricia Muscalu <patricia@axis.com>");
}

static void
gst_curl_sink_class_init (GstCurlSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = (GstBaseSinkClass *) klass;

  GST_DEBUG_OBJECT (klass, "class_init");

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_curl_sink_event);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_curl_sink_render);
  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_curl_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_curl_sink_stop);
  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_curl_sink_unlock);
  gstbasesink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_curl_sink_unlock_stop);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_curl_sink_finalize);

  gobject_class->set_property = gst_curl_sink_set_property;
  gobject_class->get_property = gst_curl_sink_get_property;

  /* FIXME: check against souphttpsrc and use same names for same properties */
  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "URI location to write to", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_USER_NAME,
      g_param_spec_string ("user", "User name",
          "User name to use for server authentication", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_USER_PASSWD,
      g_param_spec_string ("passwd", "User password",
          "User password to use for server authentication", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PROXY,
      g_param_spec_string ("proxy", "Proxy", "HTTP proxy server URI", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PROXY_PORT,
      g_param_spec_int ("proxy-port", "Proxy port",
          "HTTP proxy server port", 0, G_MAXINT, DEFAULT_PROXY_PORT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PROXY_USER_NAME,
      g_param_spec_string ("proxy-user", "Proxy user name",
          "Proxy user name to use for proxy authentication",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PROXY_USER_PASSWD,
      g_param_spec_string ("proxy-passwd", "Proxy user password",
          "Proxy user password to use for proxy authentication",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FILE_NAME,
      g_param_spec_string ("file-name", "Base file name",
          "The base file name for the uploaded images", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_int ("timeout", "Timeout",
          "Number of seconds waiting to write before timeout",
          0, G_MAXINT, DEFAULT_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_QOS_DSCP,
      g_param_spec_int ("qos-dscp",
          "QoS diff srv code point",
          "Quality of Service, differentiated services code point (0 default)",
          DSCP_MIN, DSCP_MAX, DEFAULT_QOS_DSCP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ACCEPT_SELF_SIGNED,
      g_param_spec_boolean ("accept-self-signed",
          "Accept self-signed certificates",
          "Accept self-signed SSL/TLS certificates",
          DEFAULT_ACCEPT_SELF_SIGNED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_USE_CONTENT_LENGTH,
      g_param_spec_boolean ("use-content-length", "Use content length header",
          "Use the Content-Length HTTP header instead of "
          "Transfer-Encoding header", DEFAULT_USE_CONTENT_LENGTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CONTENT_TYPE,
      g_param_spec_string ("content-type", "Content type",
          "The mime type of the body of the request", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_curl_sink_init (GstCurlSink * sink, GstCurlSinkClass * klass)
{
  sink->transfer_buf = g_malloc (sizeof (TransferBuffer));
  sink->transfer_cond = g_malloc (sizeof (TransferCondition));
  sink->transfer_cond->cond = g_cond_new ();
  sink->transfer_cond->data_sent = FALSE;
  sink->transfer_cond->data_available = FALSE;
  sink->timeout = DEFAULT_TIMEOUT;
  sink->proxy_port = DEFAULT_PROXY_PORT;
  sink->qos_dscp = DEFAULT_QOS_DSCP;
  sink->url = g_strdup (DEFAULT_URL);
  sink->header_list = NULL;
  sink->accept_self_signed = DEFAULT_ACCEPT_SELF_SIGNED;
  sink->use_content_length = DEFAULT_USE_CONTENT_LENGTH;
  sink->transfer_thread_close = FALSE;
  sink->new_file = TRUE;
  sink->proxy_headers_set = FALSE;
  sink->content_type = NULL;
}

static void
gst_curl_sink_finalize (GObject * gobject)
{
  GstCurlSink *this = GST_CURL_SINK (gobject);

  GST_DEBUG ("finalizing curlsink");
  if (this->transfer_thread != NULL) {
    g_thread_join (this->transfer_thread);
  }

  gst_curl_sink_transfer_cleanup (this);
  g_cond_free (this->transfer_cond->cond);
  g_free (this->transfer_cond);

  g_free (this->transfer_buf);

  g_free (this->url);
  g_free (this->user);
  g_free (this->passwd);
  g_free (this->proxy);
  g_free (this->proxy_user);
  g_free (this->proxy_passwd);
  g_free (this->file_name);
  g_free (this->content_type);

  if (this->header_list) {
    curl_slist_free_all (this->header_list);
    this->header_list = NULL;
  }

  if (this->fdset != NULL) {
    gst_poll_free (this->fdset);
    this->fdset = NULL;
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

  if (sink->content_type == NULL) {
    GstCaps *caps;
    GstStructure *structure;
    const gchar *mime_type;

    caps = buf->caps;
    structure = gst_caps_get_structure (caps, 0);
    mime_type = gst_structure_get_name (structure);
    sink->content_type = g_strdup (mime_type);
  }

  GST_OBJECT_LOCK (sink);

  /* check if the transfer thread has encountered problems while the
   * pipeline thread was working elsewhere */
  if (sink->flow_ret != GST_FLOW_OK) {
    goto done;
  }

  g_assert (sink->transfer_cond->data_available == FALSE);

  /* if there is no transfer thread created, lets create one */
  if (sink->transfer_thread == NULL) {
    if (!gst_curl_sink_transfer_start_unlocked (sink)) {
      sink->flow_ret = GST_FLOW_ERROR;
      goto done;
    }
  }

  /* make data available for the transfer thread and notify */
  sink->transfer_buf->ptr = data;
  sink->transfer_buf->len = size;
  sink->transfer_buf->offset = 0;
  gst_curl_sink_transfer_thread_notify_unlocked (sink);

  /* wait for the transfer thread to send the data. This will be notified
   * either when transfer is completed by the curl read callback or by
   * the thread function if an error has occured. */
  gst_curl_sink_wait_for_transfer_thread_to_send_unlocked (sink);

done:
  ret = sink->flow_ret;
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
      if (sink->transfer_thread != NULL) {
        g_thread_join (sink->transfer_thread);
        sink->transfer_thread = NULL;
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

  if ((sink->fdset = gst_poll_new (TRUE)) == NULL) {
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
  if (sink->fdset != NULL) {
    gst_poll_free (sink->fdset);
    sink->fdset = NULL;
  }

  return TRUE;
}

static gboolean
gst_curl_sink_unlock (GstBaseSink * bsink)
{
  GstCurlSink *sink;

  sink = GST_CURL_SINK (bsink);

  GST_LOG_OBJECT (sink, "Flushing");
  gst_poll_set_flushing (sink->fdset, TRUE);

  return TRUE;
}

static gboolean
gst_curl_sink_unlock_stop (GstBaseSink * bsink)
{
  GstCurlSink *sink;

  sink = GST_CURL_SINK (bsink);

  GST_LOG_OBJECT (sink, "No longer flushing");
  gst_poll_set_flushing (sink->fdset, FALSE);

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
        g_free (sink->url);
        sink->url = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "url set to %s", sink->url);
        break;
      case PROP_USER_NAME:
        g_free (sink->user);
        sink->user = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "user set to %s", sink->user);
        break;
      case PROP_USER_PASSWD:
        g_free (sink->passwd);
        sink->passwd = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "passwd set to %s", sink->passwd);
        break;
      case PROP_PROXY:
        g_free (sink->proxy);
        sink->proxy = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "proxy set to %s", sink->proxy);
        break;
      case PROP_PROXY_PORT:
        sink->proxy_port = g_value_get_int (value);
        GST_DEBUG_OBJECT (sink, "proxy port set to %d", sink->proxy_port);
        break;
      case PROP_PROXY_USER_NAME:
        g_free (sink->proxy_user);
        sink->proxy_user = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "proxy user set to %s", sink->proxy_user);
        break;
      case PROP_PROXY_USER_PASSWD:
        g_free (sink->proxy_passwd);
        sink->proxy_passwd = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "proxy password set to %s", sink->proxy_passwd);
        break;
      case PROP_FILE_NAME:
        g_free (sink->file_name);
        sink->file_name = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "file_name set to %s", sink->file_name);
        break;
      case PROP_TIMEOUT:
        sink->timeout = g_value_get_int (value);
        GST_DEBUG_OBJECT (sink, "timeout set to %d", sink->timeout);
        break;
      case PROP_QOS_DSCP:
        sink->qos_dscp = g_value_get_int (value);
        gst_curl_sink_setup_dscp_unlocked (sink);
        GST_DEBUG_OBJECT (sink, "dscp set to %d", sink->qos_dscp);
        break;
      case PROP_ACCEPT_SELF_SIGNED:
        sink->accept_self_signed = g_value_get_boolean (value);
        GST_DEBUG_OBJECT (sink, "accept_self_signed set to %d",
            sink->accept_self_signed);
        break;
      case PROP_USE_CONTENT_LENGTH:
        sink->use_content_length = g_value_get_boolean (value);
        GST_DEBUG_OBJECT (sink, "use_content_length set to %d",
            sink->use_content_length);
        break;
      case PROP_CONTENT_TYPE:
        g_free (sink->content_type);
        sink->content_type = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "content type set to %s", sink->content_type);
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
      g_free (sink->file_name);
      sink->file_name = g_value_dup_string (value);
      GST_DEBUG_OBJECT (sink, "file_name set to %s", sink->file_name);
      gst_curl_sink_new_file_notify_unlocked (sink);
      break;
    case PROP_TIMEOUT:
      sink->timeout = g_value_get_int (value);
      GST_DEBUG_OBJECT (sink, "timeout set to %d", sink->timeout);
      break;
    case PROP_QOS_DSCP:
      sink->qos_dscp = g_value_get_int (value);
      gst_curl_sink_setup_dscp_unlocked (sink);
      GST_DEBUG_OBJECT (sink, "dscp set to %d", sink->qos_dscp);
      break;
    case PROP_CONTENT_TYPE:
      g_free (sink->content_type);
      sink->content_type = g_value_dup_string (value);
      GST_DEBUG_OBJECT (sink, "content type set to %s", sink->content_type);
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
      g_value_set_string (value, sink->url);
      break;
    case PROP_USER_NAME:
      g_value_set_string (value, sink->user);
      break;
    case PROP_USER_PASSWD:
      g_value_set_string (value, sink->passwd);
      break;
    case PROP_PROXY:
      g_value_set_string (value, sink->proxy);
      break;
    case PROP_PROXY_PORT:
      g_value_set_int (value, sink->proxy_port);
      break;
    case PROP_PROXY_USER_NAME:
      g_value_set_string (value, sink->proxy_user);
      break;
    case PROP_PROXY_USER_PASSWD:
      g_value_set_string (value, sink->proxy_passwd);
      break;
    case PROP_FILE_NAME:
      g_value_set_string (value, sink->file_name);
      break;
    case PROP_TIMEOUT:
      g_value_set_int (value, sink->timeout);
      break;
    case PROP_QOS_DSCP:
      g_value_set_int (value, sink->qos_dscp);
      break;
    case PROP_ACCEPT_SELF_SIGNED:
      g_value_set_boolean (value, sink->accept_self_signed);
      break;
    case PROP_USE_CONTENT_LENGTH:
      g_value_set_boolean (value, sink->use_content_length);
      break;
    case PROP_CONTENT_TYPE:
      g_value_set_string (value, sink->content_type);
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

  if (sink->header_list) {
    curl_slist_free_all (sink->header_list);
    sink->header_list = NULL;
  }

  if (proxy_auth && !sink->proxy_headers_set && !proxy_conn_established) {
    sink->header_list =
        curl_slist_append (sink->header_list, "Content-Length: 0");
    sink->proxy_headers_set = TRUE;
    goto set_headers;
  }
  if (sink->use_content_length) {
    /* if content length is used we assume that every buffer is one
     * entire file, which is the case when uploading several jpegs */
    tmp = g_strdup_printf ("Content-Length: %d", (int) sink->transfer_buf->len);
    sink->header_list = curl_slist_append (sink->header_list, tmp);
    g_free (tmp);
  } else {
    /* when sending a POST request to a HTTP 1.1 server, you can send data
     * without knowing the size before starting the POST if you use chunked
     * encoding */
    sink->header_list = curl_slist_append (sink->header_list,
        "Transfer-Encoding: chunked");
  }

  tmp = g_strdup_printf ("Content-Type: %s", sink->content_type);
  sink->header_list = curl_slist_append (sink->header_list, tmp);
  g_free (tmp);

set_headers:

  tmp = g_strdup_printf ("Content-Disposition: attachment; filename="
      "\"%s\"", sink->file_name);
  sink->header_list = curl_slist_append (sink->header_list, tmp);
  g_free (tmp);
  curl_easy_setopt (sink->curl, CURLOPT_HTTPHEADER, sink->header_list);
}

static gboolean
gst_curl_sink_transfer_set_options_unlocked (GstCurlSink * sink)
{
#ifdef DEBUG
  curl_easy_setopt (sink->curl, CURLOPT_VERBOSE, 1);
#endif

  curl_easy_setopt (sink->curl, CURLOPT_URL, sink->url);
  curl_easy_setopt (sink->curl, CURLOPT_CONNECTTIMEOUT, sink->timeout);

  curl_easy_setopt (sink->curl, CURLOPT_SOCKOPTDATA, sink);
  curl_easy_setopt (sink->curl, CURLOPT_SOCKOPTFUNCTION,
      gst_curl_sink_transfer_socket_cb);

  if (sink->user != NULL && strlen (sink->user)) {
    curl_easy_setopt (sink->curl, CURLOPT_USERNAME, sink->user);
    curl_easy_setopt (sink->curl, CURLOPT_PASSWORD, sink->passwd);
    curl_easy_setopt (sink->curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
  }

  if (sink->accept_self_signed && g_str_has_prefix (sink->url, "https")) {
    /* TODO verify the authenticity of the peer's certificate */
    curl_easy_setopt (sink->curl, CURLOPT_SSL_VERIFYPEER, 0L);
    /* TODO check the servers's claimed identity */
    curl_easy_setopt (sink->curl, CURLOPT_SSL_VERIFYHOST, 0L);
  }

  /* proxy settings */
  if (sink->proxy != NULL && strlen (sink->proxy)) {
    if (curl_easy_setopt (sink->curl, CURLOPT_PROXY, sink->proxy)
        != CURLE_OK) {
      return FALSE;
    }
    if (curl_easy_setopt (sink->curl, CURLOPT_PROXYPORT, sink->proxy_port)
        != CURLE_OK) {
      return FALSE;
    }
    if (sink->proxy_user != NULL &&
        strlen (sink->proxy_user) &&
        sink->proxy_passwd != NULL && strlen (sink->proxy_passwd)) {
      curl_easy_setopt (sink->curl, CURLOPT_PROXYUSERNAME, sink->proxy_user);
      curl_easy_setopt (sink->curl, CURLOPT_PROXYPASSWORD, sink->proxy_passwd);
      curl_easy_setopt (sink->curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
      proxy_auth = TRUE;
    }
    /* tunnel all operations through a given HTTP proxy */
    if (curl_easy_setopt (sink->curl, CURLOPT_HTTPPROXYTUNNEL, 1L)
        != CURLE_OK) {
      return FALSE;
    }
  }

  /* POST options */
  curl_easy_setopt (sink->curl, CURLOPT_POST, 1L);

  curl_easy_setopt (sink->curl, CURLOPT_READFUNCTION,
      gst_curl_sink_transfer_read_cb);
  curl_easy_setopt (sink->curl, CURLOPT_READDATA, sink);
  curl_easy_setopt (sink->curl, CURLOPT_WRITEFUNCTION,
      gst_curl_sink_transfer_write_cb);

  return TRUE;
}

static size_t
gst_curl_sink_transfer_read_cb (void *curl_ptr, size_t size, size_t nmemb,
    void *stream)
{
  GstCurlSink *sink;
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
  buffer = sink->transfer_buf;

  buf_len = buffer->len;
  GST_LOG ("write buf len=%" G_GSIZE_FORMAT ", offset=%" G_GSIZE_FORMAT,
      buffer->len, buffer->offset);

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

    GST_LOG ("sent : %" G_GSIZE_FORMAT, bytes_to_send);

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
    while ((msg = curl_multi_info_read (sink->multi_handle, &msgs_left))) {
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
  timeout = sink->timeout;
  GST_OBJECT_UNLOCK (sink);

  /* Receiving CURLM_CALL_MULTI_PERFORM means that libcurl may have more data
     available to send or receive - call simply curl_multi_perform before
     poll() on more actions */
  do {
    m_code = curl_multi_perform (sink->multi_handle, &running_handles);
  } while (m_code == CURLM_CALL_MULTI_PERFORM);

  while (running_handles && (m_code == CURLM_OK)) {
    if (!proxy_conn_established && (resp_proxy != RESPONSE_CONNECT_PROXY)
        && proxy_auth) {
      curl_easy_getinfo (sink->curl, CURLINFO_HTTP_CONNECTCODE, &resp_proxy);
      if ((resp_proxy == RESPONSE_CONNECT_PROXY)) {
        GST_LOG ("received HTTP/1.0 200 Connection Established");
        /* Workaround: redefine HTTP headers before connecting to HTTP server.
         * When talking to proxy, the Content-Length: 0 is send with the request.
         */
        curl_multi_remove_handle (sink->multi_handle, sink->curl);
        gst_curl_sink_set_http_header_unlocked (sink);
        curl_multi_add_handle (sink->multi_handle, sink->curl);
        proxy_conn_established = TRUE;
      }
    }

    retval = gst_poll_wait (sink->fdset, timeout * GST_SECOND);
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
      m_code = curl_multi_perform (sink->multi_handle, &running_handles);
    } while (m_code == CURLM_CALL_MULTI_PERFORM);

    if (resp != RESPONSE_100_CONTINUE) {
      curl_easy_getinfo (sink->curl, CURLINFO_RESPONSE_CODE, &resp);
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
  curl_easy_getinfo (sink->curl, CURLINFO_RESPONSE_CODE, &resp);
  GST_DEBUG_OBJECT (sink, "response code: %ld", resp);
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
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, ("%s",
            curl_multi_strerror (m_code)), (NULL));
    return GST_FLOW_ERROR;
  }

curl_easy_error:
  {
    GST_DEBUG_OBJECT (sink, "curl easy error");
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, ("%s",
            curl_easy_strerror (e_code)), (NULL));
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

  gst_poll_fd_init (&sink->fd);
  sink->fd.fd = curlfd;

  ret = ret && gst_poll_add_fd (sink->fdset, &sink->fd);
  ret = ret && gst_poll_fd_ctl_write (sink->fdset, &sink->fd, TRUE);
  ret = ret && gst_poll_fd_ctl_read (sink->fdset, &sink->fd, TRUE);
  GST_DEBUG ("fd: %d", sink->fd.fd);
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
  sink->transfer_thread_close = FALSE;
  sink->new_file = TRUE;
  sink->transfer_thread =
      g_thread_create ((GThreadFunc) gst_curl_sink_transfer_thread_func, sink,
      TRUE, &error);

  if (sink->transfer_thread == NULL || error != NULL) {
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
    sink->flow_ret = GST_FLOW_ERROR;
    goto done;
  }

  while (!sink->transfer_thread_close && sink->flow_ret == GST_FLOW_OK) {
    /* we are working on a new file, clearing flag and setting file
     * name in http header */
    sink->new_file = FALSE;

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
      curl_multi_add_handle (sink->multi_handle, sink->curl);

      /* Start driving the transfer. */
      ret = gst_curl_sink_handle_transfer (sink);

      /* easy handle will be possibly re-used for next transfer, thus it needs to
       * be removed from the multi stack and re-added again */
      curl_multi_remove_handle (sink->multi_handle, sink->curl);
    }

    /* lock again before looping to check the thread closed flag */
    GST_OBJECT_LOCK (sink);

    /* if we have transfered data, then set the return code */
    if (data_available) {
      sink->flow_ret = ret;
    }
  }

done:
  /* if there is a flow error, always notify the render function so it
   * can return the flow error up along the pipeline */
  if (sink->flow_ret != GST_FLOW_OK) {
    gst_curl_sink_data_sent_notify_unlocked (sink);
  }

  GST_OBJECT_UNLOCK (sink);
  GST_DEBUG ("exit thread func - transfer thread close flag: %d",
      sink->transfer_thread_close);

  return NULL;
}

static gboolean
gst_curl_sink_transfer_setup_unlocked (GstCurlSink * sink)
{
  g_assert (sink);

  if (sink->curl == NULL) {
    /* curl_easy_init automatically calls curl_global_init(3) */
    if ((sink->curl = curl_easy_init ()) == NULL) {
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
  if (sink->multi_handle == NULL) {
    if ((sink->multi_handle = curl_multi_init ()) == NULL) {
      return FALSE;
    }
  }

  return TRUE;
}

static void
gst_curl_sink_transfer_cleanup (GstCurlSink * sink)
{
  if (sink->curl != NULL) {
    if (sink->multi_handle != NULL) {
      curl_multi_remove_handle (sink->multi_handle, sink->curl);
    }
    curl_easy_cleanup (sink->curl);
    sink->curl = NULL;
  }

  if (sink->multi_handle != NULL) {
    curl_multi_cleanup (sink->multi_handle);
    sink->multi_handle = NULL;
  }
}

static gboolean
gst_curl_sink_wait_for_data_unlocked (GstCurlSink * sink)
{
  gboolean data_available = FALSE;

  GST_LOG ("waiting for data");
  while (!sink->transfer_cond->data_available &&
      !sink->transfer_thread_close && !sink->new_file) {
    g_cond_wait (sink->transfer_cond->cond, GST_OBJECT_GET_LOCK (sink));
  }

  if (sink->transfer_thread_close) {
    GST_LOG ("wait for data aborted due to thread close");
  } else if (sink->new_file) {
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
  sink->transfer_cond->data_available = TRUE;
  sink->transfer_cond->data_sent = FALSE;
  g_cond_signal (sink->transfer_cond->cond);
}

static void
gst_curl_sink_new_file_notify_unlocked (GstCurlSink * sink)
{
  GST_LOG ("new file name");
  sink->new_file = TRUE;
  g_cond_signal (sink->transfer_cond->cond);
}

static void
gst_curl_sink_transfer_thread_close_unlocked (GstCurlSink * sink)
{
  GST_LOG ("setting transfer thread close flag");
  sink->transfer_thread_close = TRUE;
  g_cond_signal (sink->transfer_cond->cond);
}

static void
gst_curl_sink_wait_for_transfer_thread_to_send_unlocked (GstCurlSink * sink)
{
  GST_LOG ("waiting for buffer send to complete");

  /* this function should not check if the transfer thread is set to be closed
   * since that flag only can be set by the EoS event (by the pipeline thread).
   * This can therefore never happen while this function is running since this
   * function also is called by the pipeline thread (in the render function) */
  while (!sink->transfer_cond->data_sent) {
    g_cond_wait (sink->transfer_cond->cond, GST_OBJECT_GET_LOCK (sink));
  }
  GST_LOG ("buffer send completed");
}

static void
gst_curl_sink_data_sent_notify_unlocked (GstCurlSink * sink)
{
  GST_LOG ("transfer completed");
  sink->transfer_cond->data_available = FALSE;
  sink->transfer_cond->data_sent = TRUE;
  g_cond_signal (sink->transfer_cond->cond);
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

  if (getsockname (sink->fd.fd, &sa.sa, &slen) < 0) {
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
  tos = (sink->qos_dscp & 0x3f) << 2;

  switch (af) {
    case AF_INET:
      ret = setsockopt (sink->fd.fd, IPPROTO_IP, IP_TOS, &tos, sizeof (tos));
      break;
    case AF_INET6:
#ifdef IPV6_TCLASS
      ret = setsockopt (sink->fd.fd, IPPROTO_IPV6, IPV6_TCLASS, &tos,
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
