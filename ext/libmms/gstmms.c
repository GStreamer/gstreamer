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

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LOCATION,
  ARG_BLOCKSIZE
};


GST_DEBUG_CATEGORY (mmssrc_debug);
#define GST_CATEGORY_DEFAULT mmssrc_debug

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-ms-asf")
    );

static void gst_mms_class_init (GstMMSClass * klass);
static void gst_mms_base_init (gpointer g_class);
static void gst_mms_init (GstMMS * mmssrc, GstMMSClass * g_class);

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
_urihandler_init (GType mms_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_mms_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (mms_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);

  GST_DEBUG_CATEGORY_INIT (mmssrc_debug, "mmssrc", 0, "MMS Source Element");
}

GST_BOILERPLATE_FULL (GstMMS, gst_mms, GstPushSrc, GST_TYPE_PUSH_SRC,
    _urihandler_init);

static void
gst_mms_base_init (gpointer g_class)
{
  static GstElementDetails plugin_details = {
    "MMS streaming protocol support",
    "Source/Network",
    "Receive data streamed via MSFT Multi Media Server protocol",
    "Maciej Katafiasz <mathrick@users.sourceforge.net>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_set_details (element_class, &plugin_details);
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

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "location",
          "Host URL to connect to. Accepted are mms://, mmsu://, mmst:// URL types",
          NULL, G_PARAM_READWRITE));


  g_object_class_install_property (gobject_class, ARG_BLOCKSIZE,
      g_param_spec_int ("blocksize", "blocksize",
          "How many bytes should be read at once", 0, 65536, 2048,
          G_PARAM_READWRITE));

  gstbasesrc_class->start = gst_mms_start;
  gstbasesrc_class->stop = gst_mms_stop;

  gstpushsrc_class->create = gst_mms_create;

}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_mms_init (GstMMS * mmssrc, GstMMSClass * g_class)
{
  gst_pad_set_query_function (GST_BASE_SRC (mmssrc)->srcpad, gst_mms_src_query);
  gst_pad_set_query_type_function (GST_BASE_SRC (mmssrc)->srcpad,
      gst_mms_get_query_types);

  mmssrc->uri_name = NULL;
  mmssrc->connection = NULL;
  mmssrc->connection_h = NULL;
  mmssrc->blocksize = 2048;
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
  gint result;
  GstFlowReturn ret = GST_FLOW_OK;

  /* DEBUG */
  GstFormat fmt = GST_FORMAT_BYTES;
  gint64 query_res;
  GstQuery *query;

  *buf = NULL;
  mmssrc = GST_MMS (psrc);
  *buf = gst_buffer_new_and_alloc (mmssrc->blocksize);

  if (NULL == *buf) {
    ret = GST_FLOW_ERROR;
    goto done;
  }

  data = GST_BUFFER_DATA (*buf);
  GST_DEBUG ("mms: data: %p\n", data);

  if (NULL == GST_BUFFER_DATA (*buf)) {
    ret = GST_FLOW_ERROR;
    gst_buffer_unref (*buf);
    *buf = NULL;
    goto done;
  }

  GST_BUFFER_SIZE (*buf) = 0;
  GST_DEBUG ("reading %d bytes", mmssrc->blocksize);
  if (mmssrc->connection) {
    result =
        mms_read (NULL, mmssrc->connection, (char *) data, mmssrc->blocksize);
  } else {
    result =
        mmsh_read (NULL, mmssrc->connection_h, (char *) data,
        mmssrc->blocksize);
  }

  /* EOS? */
  if (result == 0) {
    GstPad *peer;

    gst_buffer_unref (*buf);
    *buf = NULL;
    GST_DEBUG ("Returning EOS");
    peer = gst_pad_get_peer (GST_BASE_SRC_PAD (mmssrc));
    if (!gst_pad_send_event (peer, gst_event_new_eos ())) {
      ret = GST_FLOW_ERROR;
    }
    gst_object_unref (peer);
    goto done;
  }

  if (mmssrc->connection) {
    GST_BUFFER_OFFSET (*buf) =
        mms_get_current_pos (mmssrc->connection) - result;
  } else {
    GST_BUFFER_OFFSET (*buf) =
        mmsh_get_current_pos (mmssrc->connection_h) - result;
  }
  GST_BUFFER_SIZE (*buf) = result;

  /* DEBUG */
  query = gst_query_new_position (GST_QUERY_POSITION);
  gst_pad_query (GST_BASE_SRC (mmssrc)->srcpad, query);
  gst_query_parse_position (query, &fmt, &query_res);
  gst_query_unref (query);
  GST_DEBUG ("mms position: %lld\n", query_res);

done:

  return ret;
}

static gboolean
gst_mms_start (GstBaseSrc * bsrc)
{
  GstMMS *mms;
  gboolean ret = FALSE;

  mms = GST_MMS (bsrc);

  if (!mms->uri_name) {
    ret = FALSE;
    goto done;
  }
  /* FIXME: pass some sane arguments here */
  gst_mms_stop (bsrc);

  mms->connection = mms_connect (NULL, NULL, mms->uri_name, 128 * 1024);
  if (mms->connection) {
    ret = TRUE;
  } else {
    mms->connection_h = mmsh_connect (NULL, NULL, mms->uri_name, 128 * 1024);
    if (mms->connection_h) {
      ret = TRUE;
    }

  }

done:
  return ret;

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

  switch (prop_id) {
    case ARG_LOCATION:
      mmssrc->uri_name = g_value_dup_string (value);
      break;
    case ARG_BLOCKSIZE:
      mmssrc->blocksize = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mms_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMMS *mmssrc;

  mmssrc = GST_MMS (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, mmssrc->uri_name);
      break;
    case ARG_BLOCKSIZE:
      g_value_set_int (value, mmssrc->blocksize);
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
  return gst_element_register (plugin, "mmssrc", GST_RANK_NONE, GST_TYPE_MMS);
}

static guint
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
    plugin_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
