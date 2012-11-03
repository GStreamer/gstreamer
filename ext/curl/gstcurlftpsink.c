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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-curlftpsink
 * @short_description: sink that uploads data to a server using libcurl
 * @see_also:
 *
 * This is a network sink that uses libcurl as a client to upload data to
 * an FTP server.
 *
 * <refsect2>
 * <title>Example launch line (upload a JPEG file to /home/test/images directory)</title>
 * |[
 * gst-launch filesrc location=image.jpg ! jpegparse ! curlftpsink  \
 *     file-name=image.jpg  \
 *     location=ftp://192.168.0.1/images/
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <curl/curl.h>
#include <string.h>
#include <stdio.h>

#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include <sys/types.h>
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#include <unistd.h>
#if HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif
#if HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>

#include "gstcurltlssink.h"
#include "gstcurlftpsink.h"

/* Default values */
#define GST_CAT_DEFAULT                gst_curl_ftp_sink_debug


/* Plugin specific settings */

GST_DEBUG_CATEGORY_STATIC (gst_curl_ftp_sink_debug);

enum
{
  PROP_0,
  PROP_FTP_PORT_ARG,
  PROP_EPSV_MODE,
  PROP_CREATE_DIRS
};


/* Object class function declarations */


/* private functions */
static void gst_curl_ftp_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_curl_ftp_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_curl_ftp_sink_finalize (GObject * gobject);

static gboolean set_ftp_options_unlocked (GstCurlBaseSink * curlbasesink);
static gboolean set_ftp_dynamic_options_unlocked
    (GstCurlBaseSink * curlbasesink);

#define gst_curl_ftp_sink_parent_class parent_class
G_DEFINE_TYPE (GstCurlFtpSink, gst_curl_ftp_sink, GST_TYPE_CURL_TLS_SINK);

static void
gst_curl_ftp_sink_class_init (GstCurlFtpSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstCurlBaseSinkClass *gstcurlbasesink_class = (GstCurlBaseSinkClass *) klass;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_curl_ftp_sink_debug, "curlftpsink", 0,
      "curl ftp sink element");
  GST_DEBUG_OBJECT (klass, "class_init");

  gst_element_class_set_static_metadata (element_class,
      "Curl ftp sink",
      "Sink/Network",
      "Upload data over FTP protocol using libcurl",
      "Patricia Muscalu <patricia@axis.com>");

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_curl_ftp_sink_finalize);

  gobject_class->set_property = gst_curl_ftp_sink_set_property;
  gobject_class->get_property = gst_curl_ftp_sink_get_property;

  gstcurlbasesink_class->set_protocol_dynamic_options_unlocked =
      set_ftp_dynamic_options_unlocked;
  gstcurlbasesink_class->set_options_unlocked = set_ftp_options_unlocked;

  g_object_class_install_property (gobject_class, PROP_FTP_PORT_ARG,
      g_param_spec_string ("ftp-port", "IP address for FTP PORT instruction",
          "The PORT instruction tells the remote server to connect to"
          " the IP address", "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_EPSV_MODE,
      g_param_spec_boolean ("epsv-mode", "Extended passive mode",
          "Enable the use of the EPSV command when doing passive FTP transfers",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CREATE_DIRS,
      g_param_spec_boolean ("create-dirs", "Create missing directories",
          "Attempt to create missing directory included in the path",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_curl_ftp_sink_init (GstCurlFtpSink * sink)
{
}

static void
gst_curl_ftp_sink_finalize (GObject * gobject)
{
  GstCurlFtpSink *this = GST_CURL_FTP_SINK (gobject);

  GST_DEBUG ("finalizing curlftpsink");
  g_free (this->ftp_port_arg);

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static gboolean
set_ftp_dynamic_options_unlocked (GstCurlBaseSink * basesink)
{
  gchar *tmp = g_strdup_printf ("%s%s", basesink->url, basesink->file_name);

  curl_easy_setopt (basesink->curl, CURLOPT_URL, tmp);

  g_free (tmp);

  return TRUE;
}

static gboolean
set_ftp_options_unlocked (GstCurlBaseSink * basesink)
{
  GstCurlFtpSink *sink = GST_CURL_FTP_SINK (basesink);

  curl_easy_setopt (basesink->curl, CURLOPT_UPLOAD, 1L);

  if (sink->ftp_port_arg != NULL && (strlen (sink->ftp_port_arg) > 0)) {
    /* Connect data stream actively. */
    CURLcode res = curl_easy_setopt (basesink->curl, CURLOPT_FTPPORT,
        sink->ftp_port_arg);

    if (res != CURLE_OK) {
      GST_DEBUG_OBJECT (sink, "Failed to set up active mode: %s",
          curl_easy_strerror (res));
      GST_ELEMENT_ERROR (sink, RESOURCE, WRITE,
          ("Failed to set up active mode: %s", curl_easy_strerror (res)),
          (NULL));

      return FALSE;
    }

    goto end;
  }

  /* Connect data stream passively.
   * libcurl will always attempt to use EPSV before PASV.
   */
  if (!sink->epsv_mode) {
    /* send only plain PASV command */
    curl_easy_setopt (basesink->curl, CURLOPT_FTP_USE_EPSV, 0);
  }

end:
  if (sink->create_dirs) {
    curl_easy_setopt (basesink->curl, CURLOPT_FTP_CREATE_MISSING_DIRS, 1L);
  }

  return TRUE;
}

static void
gst_curl_ftp_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCurlFtpSink *sink;
  GstState cur_state;

  g_return_if_fail (GST_IS_CURL_FTP_SINK (object));
  sink = GST_CURL_FTP_SINK (object);

  gst_element_get_state (GST_ELEMENT (sink), &cur_state, NULL, 0);
  if (cur_state != GST_STATE_PLAYING && cur_state != GST_STATE_PAUSED) {
    GST_OBJECT_LOCK (sink);

    switch (prop_id) {
      case PROP_FTP_PORT_ARG:
        g_free (sink->ftp_port_arg);
        sink->ftp_port_arg = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "ftp-port set to %s", sink->ftp_port_arg);
        break;
      case PROP_EPSV_MODE:
        sink->epsv_mode = g_value_get_boolean (value);
        GST_DEBUG_OBJECT (sink, "epsv-mode set to %d", sink->epsv_mode);
        break;
      case PROP_CREATE_DIRS:
        sink->create_dirs = g_value_get_boolean (value);
        GST_DEBUG_OBJECT (sink, "create-dirs set to %d", sink->create_dirs);
        break;

      default:
        GST_DEBUG_OBJECT (sink, "invalid property id %d", prop_id);
        break;
    }

    GST_OBJECT_UNLOCK (sink);
  }
}

static void
gst_curl_ftp_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCurlFtpSink *sink;

  g_return_if_fail (GST_IS_CURL_FTP_SINK (object));
  sink = GST_CURL_FTP_SINK (object);

  switch (prop_id) {
    case PROP_FTP_PORT_ARG:
      g_value_set_string (value, sink->ftp_port_arg);
      break;
    case PROP_EPSV_MODE:
      g_value_set_boolean (value, sink->epsv_mode);
      break;
    case PROP_CREATE_DIRS:
      g_value_set_boolean (value, sink->create_dirs);
      break;
    default:
      GST_DEBUG_OBJECT (sink, "invalid property id");
      break;
  }
}
