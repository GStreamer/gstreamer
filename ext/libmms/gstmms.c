/*
 *
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <string.h>
#include "gstmms.h"

#define DEFAULT_CONNECTION_SPEED    0

enum
{
  ARG_0,
  ARG_LOCATION,
  ARG_CONNECTION_SPEED
};


GST_DEBUG_CATEGORY_STATIC (mmssrc_debug);
#define GST_CAT_DEFAULT mmssrc_debug

static const GstElementDetails plugin_details =
GST_ELEMENT_DETAILS ("MMS streaming source",
    "Source/Network",
    "Receive data streamed via MSFT Multi Media Server protocol",
    "Maciej Katafiasz <mathrick@users.sourceforge.net>");

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-ms-asf")
    );

static void gst_mms_finalize (GObject * gobject);
static void gst_mms_uri_handler_init (gpointer g_iface, gpointer iface_data);

static void gst_mms_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mms_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static const GstQueryType *gst_mms_get_query_types (GstPad * pad);
static gboolean gst_mms_src_query (GstPad * pad, GstQuery * query);

static gboolean gst_mms_start (GstBaseSrc * bsrc);
static gboolean gst_mms_stop (GstBaseSrc * bsrc);
static GstFlowReturn gst_mms_create (GstPushSrc * psrc, GstBuffer ** buf);

static void
gst_mms_urihandler_init (GType mms_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_mms_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (mms_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);
}

GST_BOILERPLATE_FULL (GstMMS, gst_mms, GstPushSrc, GST_TYPE_PUSH_SRC,
    gst_mms_urihandler_init);

static void
gst_mms_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_set_details (element_class, &plugin_details);

  GST_DEBUG_CATEGORY_INIT (mmssrc_debug, "mmssrc", 0, "MMS Source Element");
}

/* initialize the plugin's class */
static void
gst_mms_class_init (GstMMSClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_mms_set_property;
  gobject_class->get_property = gst_mms_get_property;
  gobject_class->finalize = gst_mms_finalize;

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "location",
          "Host URL to connect to. Accepted are mms://, mmsu://, mmst:// URL types",
          NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_CONNECTION_SPEED,
      g_param_spec_uint ("connection-speed", "Connection Speed",
          "Network connection speed in kbps (0 = unknown)",
          0, G_MAXINT / 1000, DEFAULT_CONNECTION_SPEED, G_PARAM_READWRITE));
  /* Note: connection-speed is intentionaly limited to G_MAXINT as libmms use int for it */

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_mms_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_mms_stop);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_mms_create);

}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_mms_init (GstMMS * mmssrc, GstMMSClass * g_class)
{
  gst_pad_set_query_function (GST_BASE_SRC (mmssrc)->srcpad,
      GST_DEBUG_FUNCPTR (gst_mms_src_query));
  gst_pad_set_query_type_function (GST_BASE_SRC (mmssrc)->srcpad,
      GST_DEBUG_FUNCPTR (gst_mms_get_query_types));

  mmssrc->uri_name = NULL;
  mmssrc->connection = NULL;
  mmssrc->connection_h = NULL;
  mmssrc->connection_speed = DEFAULT_CONNECTION_SPEED;
  GST_BASE_SRC (mmssrc)->blocksize = 2048;
}

static void
gst_mms_finalize (GObject * gobject)
{
  GstMMS *mmssrc = GST_MMS (gobject);

  if (mmssrc->uri_name) {
    g_free (mmssrc->uri_name);
    mmssrc->uri_name = NULL;
  }

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (gobject);

}

/*
 * location querying and so on.
 */

static const GstQueryType *
gst_mms_get_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return types;
}

static gboolean
gst_mms_src_query (GstPad * pad, GstQuery * query)
{

  GstMMS *mmssrc = GST_MMS (gst_pad_get_parent (pad));
  gboolean res = TRUE;
  GstFormat format;
  gint64 value;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &format, &value);
      if (format != GST_FORMAT_BYTES) {
        res = FALSE;
        break;
      }
      if (mmssrc->connection) {
        value = (gint64) mms_get_current_pos (mmssrc->connection);
      } else {
        value = (gint64) mmsh_get_current_pos (mmssrc->connection_h);
      }
      gst_query_set_position (query, format, value);
      break;
    case GST_QUERY_DURATION:
      gst_query_parse_duration (query, &format, &value);
      if (format != GST_FORMAT_BYTES) {
        res = FALSE;
        break;
      }
      if (mmssrc->connection) {
        value = (gint64) mms_get_length (mmssrc->connection);
      } else {
        value = (gint64) mmsh_get_length (mmssrc->connection_h);
      }
      gst_query_set_duration (query, format, value);
      break;
    default:
      res = FALSE;
      break;
  }

  gst_object_unref (mmssrc);
  return res;

}

/* get function
 * this function generates new data when needed
 */


static GstFlowReturn
gst_mms_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstMMS *mmssrc;
  guint8 *data;
  guint blocksize;
  gint result;

  mmssrc = GST_MMS (psrc);

  GST_OBJECT_LOCK (mmssrc);
  blocksize = GST_BASE_SRC (mmssrc)->blocksize;
  GST_OBJECT_UNLOCK (mmssrc);

  *buf = gst_buffer_new_and_alloc (blocksize);

  data = GST_BUFFER_DATA (*buf);
  GST_BUFFER_SIZE (*buf) = 0;
  GST_LOG_OBJECT (mmssrc, "reading %d bytes", blocksize);
  if (mmssrc->connection) {
    result = mms_read (NULL, mmssrc->connection, (char *) data, blocksize);
  } else {
    result = mmsh_read (NULL, mmssrc->connection_h, (char *) data, blocksize);
  }

  /* EOS? */
  if (result == 0)
    goto eos;

  if (mmssrc->connection) {
    GST_BUFFER_OFFSET (*buf) =
        mms_get_current_pos (mmssrc->connection) - result;
  } else {
    GST_BUFFER_OFFSET (*buf) =
        mmsh_get_current_pos (mmssrc->connection_h) - result;
  }
  GST_BUFFER_SIZE (*buf) = result;

  GST_LOG_OBJECT (mmssrc, "Returning buffer with offset %" G_GINT64_FORMAT
      " and size %u", GST_BUFFER_OFFSET (*buf), GST_BUFFER_SIZE (*buf));

  gst_buffer_set_caps (*buf, GST_PAD_CAPS (GST_BASE_SRC_PAD (mmssrc)));

  return GST_FLOW_OK;

eos:
  {
    GST_DEBUG_OBJECT (mmssrc, "EOS");
    gst_buffer_unref (*buf);
    *buf = NULL;
    return GST_FLOW_UNEXPECTED;
  }
}

static gboolean
gst_mms_start (GstBaseSrc * bsrc)
{
  GstMMS *mms;
  guint bandwidth_avail;

  mms = GST_MMS (bsrc);

  if (!mms->uri_name || *mms->uri_name == '\0')
    goto no_uri;

  if (mms->connection_speed)
    bandwidth_avail = mms->connection_speed;
  else
    bandwidth_avail = G_MAXINT;

  /* FIXME: pass some sane arguments here */
  GST_DEBUG_OBJECT (mms,
      "Trying mms_connect (%s) with bandwidth constraint of %d bps",
      mms->uri_name, bandwidth_avail);
  mms->connection = mms_connect (NULL, NULL, mms->uri_name, bandwidth_avail);
  if (mms->connection)
    goto success;

  GST_DEBUG_OBJECT (mms,
      "Trying mmsh_connect (%s) with bandwidth constraint of %d bps",
      mms->uri_name, bandwidth_avail);
  mms->connection_h = mmsh_connect (NULL, NULL, mms->uri_name, bandwidth_avail);
  if (!mms->connection_h)
    goto no_connect;

  /* fall through */

success:
  {
    GST_DEBUG_OBJECT (mms, "Connect successful");
    return TRUE;
  }

no_uri:
  {
    GST_ELEMENT_ERROR (mms, RESOURCE, OPEN_READ,
        ("No URI to open specified"), (NULL));
    return FALSE;
  }

no_connect:
  {
    GST_ELEMENT_ERROR (mms, RESOURCE, OPEN_READ,
        ("Could not connect to this stream"), (NULL));
    return FALSE;
  }
}

static gboolean
gst_mms_stop (GstBaseSrc * bsrc)
{
  GstMMS *mms;

  mms = GST_MMS (bsrc);
  if (mms->connection != NULL) {
    mms_close (mms->connection);
    mms->connection = NULL;
  }
  if (mms->connection_h != NULL) {
    mmsh_close (mms->connection_h);
    mms->connection_h = NULL;
  }
  return TRUE;
}

static void
gst_mms_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMMS *mmssrc;

  mmssrc = GST_MMS (object);

  GST_OBJECT_LOCK (mmssrc);
  switch (prop_id) {
    case ARG_LOCATION:
      if (mmssrc->uri_name) {
        g_free (mmssrc->uri_name);
        mmssrc->uri_name = NULL;
      }
      mmssrc->uri_name = g_value_dup_string (value);
      break;
    case ARG_CONNECTION_SPEED:
      mmssrc->connection_speed = g_value_get_uint (value) * 1000;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (mmssrc);
}

static void
gst_mms_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMMS *mmssrc;

  mmssrc = GST_MMS (object);

  GST_OBJECT_LOCK (mmssrc);
  switch (prop_id) {
    case ARG_LOCATION:
      if (mmssrc->uri_name)
        g_value_set_string (value, mmssrc->uri_name);
      break;
    case ARG_CONNECTION_SPEED:
      g_value_set_uint (value, mmssrc->connection_speed / 1000);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (mmssrc);
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "mmssrc", GST_RANK_NONE, GST_TYPE_MMS);
}

static GstURIType
gst_mms_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_mms_uri_get_protocols (void)
{
  static gchar *protocols[] = { "mms", "mmsh", "mmst", "mmsu", NULL };

  return protocols;
}

static const gchar *
gst_mms_uri_get_uri (GstURIHandler * handler)
{
  GstMMS *src = GST_MMS (handler);

  return src->uri_name;
}

static gboolean
gst_mms_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gchar *protocol;
  GstMMS *src = GST_MMS (handler);

  protocol = gst_uri_get_protocol (uri);
  if ((strcmp (protocol, "mms") != 0) && (strcmp (protocol, "mmsh") != 0)) {
    g_free (protocol);
    return FALSE;
  }
  g_free (protocol);
  g_object_set (src, "location", uri, NULL);

  return TRUE;
}

static void
gst_mms_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_mms_uri_get_type;
  iface->get_protocols = gst_mms_uri_get_protocols;
  iface->get_uri = gst_mms_uri_get_uri;
  iface->set_uri = gst_mms_uri_set_uri;
}


/* this is the structure that gst-register looks for
 * so keep the name plugin_desc, or you cannot get your plug-in registered */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mms",
    "Microsoft Multi Media Server streaming protocol support",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
