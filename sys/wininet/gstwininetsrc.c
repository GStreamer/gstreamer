/* GStreamer Windows network source
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
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
 * SECTION:element-wininetsrc
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v wininetsrc location="http://71.83.57.210:9000" ! application/x-icy,metadata-interval=0 ! icydemux ! mad ! audioconvert ! directsoundsink
 * ]| receive mp3 audio over http and play it back.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwininetsrc.h"

#include <string.h>

#define DEFAULT_LOCATION "http://localhost/"
#define DEFAULT_POLL_MODE FALSE
#define DEFAULT_IRADIO_MODE TRUE

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_POLL_MODE,
  PROP_IRADIO_MODE
};

GST_DEBUG_CATEGORY_STATIC (gst_win_inet_src_debug);
#define GST_CAT_DEFAULT gst_win_inet_src_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_win_inet_src_init_interfaces (GType type);
static void gst_win_inet_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void gst_win_inet_src_dispose (GObject * object);
static void gst_win_inet_src_finalize (GObject * object);
static void gst_win_inet_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_win_inet_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static gboolean gst_win_inet_src_start (GstBaseSrc * basesrc);
static gboolean gst_win_inet_src_stop (GstBaseSrc * basesrc);

static GstFlowReturn gst_win_inet_src_create (GstPushSrc * pushsrc,
    GstBuffer ** buffer);

static void gst_win_inet_src_reset (GstWinInetSrc * self);

GST_BOILERPLATE_FULL (GstWinInetSrc, gst_win_inet_src, GstPushSrc,
    GST_TYPE_PUSH_SRC, gst_win_inet_src_init_interfaces);

static void
gst_win_inet_src_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (element_class,
      "Windows Network Source", "Source/Network",
      "Receive data as a client over the network via HTTP or FTP",
      "Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>");
}

static void
gst_win_inet_src_class_init (GstWinInetSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->dispose = gst_win_inet_src_dispose;
  gobject_class->finalize = gst_win_inet_src_finalize;
  gobject_class->get_property = gst_win_inet_src_get_property;
  gobject_class->set_property = gst_win_inet_src_set_property;

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_win_inet_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_win_inet_src_stop);
  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_win_inet_src_create);

  g_object_class_install_property (gobject_class,
      PROP_LOCATION, g_param_spec_string ("location", "Location",
          "Location to read from", DEFAULT_LOCATION, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_POLL_MODE, g_param_spec_boolean ("poll-mode", "poll-mode",
          "Enable poll mode (keep re-issuing request)",
          DEFAULT_POLL_MODE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_IRADIO_MODE, g_param_spec_boolean ("iradio-mode", "iradio-mode",
          "Enable Internet radio mode "
          "(extraction of shoutcast/icecast metadata)",
          DEFAULT_IRADIO_MODE, G_PARAM_READWRITE));
}

static void
gst_win_inet_src_init_interfaces (GType type)
{
  static const GInterfaceInfo uri_handler_info = {
    gst_win_inet_src_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (type, GST_TYPE_URI_HANDLER, &uri_handler_info);

  GST_DEBUG_CATEGORY_INIT (gst_win_inet_src_debug, "wininetsrc",
      0, "Wininet source");
}

static void
gst_win_inet_src_init (GstWinInetSrc * self, GstWinInetSrcClass * gclass)
{
  self->location = g_strdup (DEFAULT_LOCATION);
  self->poll_mode = DEFAULT_POLL_MODE;
  self->iradio_mode = DEFAULT_IRADIO_MODE;

  self->inet = NULL;
  self->url = NULL;
  self->cur_offset = 0;
  self->icy_caps = NULL;
}

static void
gst_win_inet_src_dispose (GObject * object)
{
  GstWinInetSrc *self = GST_WIN_INET_SRC (object);

  gst_win_inet_src_reset (self);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_win_inet_src_finalize (GObject * object)
{
  GstWinInetSrc *self = GST_WIN_INET_SRC (object);

  g_free (self->location);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_win_inet_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstWinInetSrc *self = GST_WIN_INET_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, self->location);
      break;

    case PROP_POLL_MODE:
      g_value_set_boolean (value, self->poll_mode);
      break;

    case PROP_IRADIO_MODE:
      g_value_set_boolean (value, self->iradio_mode);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_win_inet_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWinInetSrc *self = GST_WIN_INET_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      if (GST_STATE (self) == GST_STATE_PLAYING ||
          GST_STATE (self) == GST_STATE_PAUSED) {
        GST_WARNING_OBJECT (self, "element must be in stopped or paused state "
            "in order to change location");
        break;
      }

      g_free (self->location);
      self->location = g_value_dup_string (value);
      break;

    case PROP_POLL_MODE:
      self->poll_mode = g_value_get_boolean (value);
      break;

    case PROP_IRADIO_MODE:
      self->iradio_mode = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_win_inet_src_reset (GstWinInetSrc * self)
{
  if (self->url != NULL) {
    InternetCloseHandle (self->url);
    self->url = NULL;
  }

  if (self->inet != NULL) {
    InternetCloseHandle (self->inet);
    self->inet = NULL;
  }

  if (self->icy_caps != NULL) {
    gst_caps_unref (self->icy_caps);
    self->icy_caps = NULL;
  }

  self->cur_offset = 0;
}

static gboolean
gst_win_inet_src_get_header_value_as_int (GstWinInetSrc * self,
    const gchar * header_name, gint * header_value, gboolean log_failure)
{
  gchar buf[16] = { 0, };
  DWORD buf_size = sizeof (buf);
  gint *value = (gint *) buf;

  strcpy (buf, header_name);

  if (!HttpQueryInfo (self->url, HTTP_QUERY_CUSTOM | HTTP_QUERY_FLAG_NUMBER,
          buf, &buf_size, NULL)) {
    if (log_failure) {
      DWORD error_code = GetLastError ();
      const gchar *error_str = "unknown error";

      if (error_code == ERROR_HTTP_HEADER_NOT_FOUND)
        error_str = "ERROR_HTTP_HEADER_NOT_FOUND";

      GST_WARNING_OBJECT (self, "HttpQueryInfo for header '%s' failed: %s "
          "(0x%08lx)", header_name, error_str, error_code);
    }

    return FALSE;
  }

  *header_value = *value;
  return TRUE;
}

static gboolean
gst_win_inet_src_open (GstWinInetSrc * self)
{
  const gchar *extra_headers = NULL;

  gst_win_inet_src_reset (self);

  self->inet = InternetOpen (NULL, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
  if (self->inet == NULL)
    goto error;

  if (self->iradio_mode)
    extra_headers = "Icy-MetaData:1";   /* exactly as sent by WinAmp, no space */

  self->url = InternetOpenUrl (self->inet, self->location, extra_headers,
      (extra_headers != NULL) ? -1 : 0, INTERNET_FLAG_NO_UI, (DWORD_PTR) self);
  if (self->url == NULL)
    goto error;

  if (self->iradio_mode) {
    gint value;

    if (gst_win_inet_src_get_header_value_as_int (self, "icy-metaint", &value,
            TRUE)) {
      self->icy_caps = gst_caps_new_simple ("application/x-icy",
          "metadata-interval", G_TYPE_INT, value, NULL);
    }
  }

  return TRUE;

error:
  GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, (NULL),
      ("Could not open location \"%s\" for reading: 0x%08lx",
          self->location, GetLastError ()));
  gst_win_inet_src_reset (self);

  return FALSE;
}

static gboolean
gst_win_inet_src_start (GstBaseSrc * basesrc)
{
  GstWinInetSrc *self = GST_WIN_INET_SRC (basesrc);

  return gst_win_inet_src_open (self);
}

static gboolean
gst_win_inet_src_stop (GstBaseSrc * basesrc)
{
  gst_win_inet_src_reset (GST_WIN_INET_SRC (basesrc));

  return TRUE;
}

static GstFlowReturn
gst_win_inet_src_create (GstPushSrc * pushsrc, GstBuffer ** buffer)
{
  GstWinInetSrc *self = GST_WIN_INET_SRC (pushsrc);
  GstBaseSrc *basesrc = GST_BASE_SRC (pushsrc);
  GstBuffer *buf = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  DWORD bytes_read = 0;

  do {
    GstCaps *caps = GST_PAD_CAPS (GST_BASE_SRC_PAD (self));

    if (self->icy_caps != NULL)
      caps = self->icy_caps;

    ret = gst_pad_alloc_buffer (GST_BASE_SRC_PAD (basesrc),
        self->cur_offset, basesrc->blocksize, caps, &buf);

    if (G_LIKELY (ret == GST_FLOW_OK)) {
      if (InternetReadFile (self->url, GST_BUFFER_DATA (buf),
              basesrc->blocksize, &bytes_read)) {
        if (bytes_read == 0) {
          if (self->poll_mode) {
            if (gst_win_inet_src_open (self)) {
              gst_buffer_unref (buf);
              buf = NULL;
            } else {
              ret = GST_FLOW_ERROR;
            }
          } else {
            GST_ERROR_OBJECT (self, "short read (eof?)");
            ret = GST_FLOW_UNEXPECTED;
          }
        }
      } else {
        GST_ERROR_OBJECT (self, "InternetReadFile failed: 0x%08lx",
            GetLastError ());

        ret = GST_FLOW_ERROR;
      }
    }
  }
  while (bytes_read == 0 && ret == GST_FLOW_OK);

  if (ret == GST_FLOW_OK) {
    GST_BUFFER_SIZE (buf) = bytes_read;
    self->cur_offset += bytes_read;

    *buffer = buf;
  } else {
    if (buf != NULL)
      gst_buffer_unref (buf);
  }

  return ret;
}

static GstURIType
gst_win_inet_src_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_win_inet_src_uri_get_protocols (void)
{
  static const gchar *protocols[] = { "http", "https", "ftp", NULL };

  return (gchar **) protocols;
}

static const gchar *
gst_win_inet_src_uri_get_uri (GstURIHandler * handler)
{
  GstWinInetSrc *src = GST_WIN_INET_SRC (handler);

  return src->location;
}

static gboolean
gst_win_inet_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstWinInetSrc *src = GST_WIN_INET_SRC (handler);

  g_free (src->location);
  src->location = g_strdup (uri);
  return TRUE;
}

static void
gst_win_inet_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_win_inet_src_uri_get_type;
  iface->get_protocols = gst_win_inet_src_uri_get_protocols;
  iface->get_uri = gst_win_inet_src_uri_get_uri;
  iface->set_uri = gst_win_inet_src_uri_set_uri;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "wininetsrc",
      GST_RANK_NONE, GST_TYPE_WIN_INET_SRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    wininet,
    "Windows network plugins",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
