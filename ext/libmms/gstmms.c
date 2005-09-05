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
static void gst_mms_base_init (GstMMSClass * klass);
static void gst_mms_init (GstMMS * mmssrc);

static void gst_mms_uri_handler_init (gpointer g_iface, gpointer iface_data);


static void gst_mms_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mms_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static const GstQueryType *gst_mms_get_query_types (GstPad * pad);
static const GstFormat *gst_mms_get_formats (GstPad * pad);
static gboolean gst_mms_srcpad_query (GstPad * pad, GstQueryType type,
    GstFormat * fmt, gint64 * value);
static GstStateChangeReturn gst_mms_change_state (GstElement * elem);

static GstData *gst_mms_get (GstPad * pad);

static GstElementClass *parent_class = NULL;

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
}


GType
gst_mms_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type) {
    static const GTypeInfo plugin_info = {
      sizeof (GstMMSClass),
      (GBaseInitFunc) gst_mms_base_init,
      NULL,
      (GClassInitFunc) gst_mms_class_init,
      NULL,
      NULL,
      sizeof (GstMMS),
      0,
      (GInstanceInitFunc) gst_mms_init,
    };
    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstMMS", &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_mms_base_init (GstMMSClass * klass)
{
  static GstElementDetails plugin_details = {
    "MMS streaming protocol support",
    "Source/Network",
    "Receive data streamed via MSFT Multi Media Server protocol",
    "Maciej Katafiasz <mathrick@users.sourceforge.net>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (mmssrc_debug, "mmssrc", 0, "MMS Source Element");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

/* initialize the plugin's class */
static void
gst_mms_class_init (GstMMSClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  _urihandler_init (GST_TYPE_MMS);

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "location",
          "Host URL to connect to. Accepted are mms://, mmsu://, mmst:// URL types",
          NULL, G_PARAM_READWRITE));


  g_object_class_install_property (gobject_class, ARG_BLOCKSIZE,
      g_param_spec_int ("blocksize", "blocksize",
          "How many bytes should be read at once", 0, 65536, 2048,
          G_PARAM_READWRITE));

  gobject_class->set_property = gst_mms_set_property;
  gobject_class->get_property = gst_mms_get_property;
  gstelement_class->change_state = gst_mms_change_state;
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_mms_init (GstMMS * mmssrc)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (mmssrc);

  mmssrc->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_get_function (mmssrc->srcpad, gst_mms_get);
  gst_pad_set_query_function (mmssrc->srcpad, gst_mms_srcpad_query);
  gst_pad_set_query_type_function (mmssrc->srcpad, gst_mms_get_query_types);
  gst_pad_set_formats_function (mmssrc->srcpad, gst_mms_get_formats);
  gst_element_add_pad (GST_ELEMENT (mmssrc), mmssrc->srcpad);

  mmssrc->uri_name = NULL;
  mmssrc->connection = NULL;
  mmssrc->blocksize = 2048;
}

/*
 * location querying and so on.
 */

static const GstQueryType *
gst_mms_get_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return types;
}

static const GstFormat *
gst_mms_get_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_BYTES,
    0,
  };

  return formats;
}

static gboolean
gst_mms_srcpad_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstMMS *mmssrc = GST_MMS (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  if (*format != GST_FORMAT_BYTES)
    return FALSE;

  switch (type) {
    case GST_QUERY_TOTAL:
      *value = (gint64) mms_get_length (mmssrc->connection);
      break;
    case GST_QUERY_POSITION:
      *value = (gint64) mms_get_current_pos (mmssrc->connection);
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

/* get function
 * this function generates new data when needed
 */

static GstData *
gst_mms_get (GstPad * pad)
{
  GstMMS *mmssrc;
  GstBuffer *buf;
  guint8 *data;
  gint result;

  /* DEBUG */
  GstFormat fmt = GST_FORMAT_BYTES;
  gint64 query_res;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  mmssrc = GST_MMS (GST_OBJECT_PARENT (pad));
  g_return_val_if_fail (GST_IS_MMS (mmssrc), NULL);

  buf = gst_buffer_new ();
  g_return_val_if_fail (buf, NULL);

  data = g_malloc0 (mmssrc->blocksize);
  GST_BUFFER_DATA (buf) = data;
  GST_DEBUG ("mms: data: %p\n", data);
  g_return_val_if_fail (GST_BUFFER_DATA (buf) != NULL, NULL);

  GST_BUFFER_SIZE (buf) = 0;
  GST_DEBUG ("reading %d bytes", mmssrc->blocksize);
  result = mms_read (NULL, mmssrc->connection, data, mmssrc->blocksize);
  GST_BUFFER_OFFSET (buf) = mms_get_current_pos (mmssrc->connection) - result;
  GST_BUFFER_SIZE (buf) = result;


  /* DEBUG */
  gst_pad_query (gst_element_get_pad (GST_ELEMENT (mmssrc), "src"),
      GST_QUERY_POSITION, &fmt, &query_res);
  GST_DEBUG ("mms position: %lld\n", query_res);

  /* EOS? */
  if (result == 0) {
    gst_buffer_unref (buf);
    GST_DEBUG ("Returning EOS");
    gst_element_set_eos (GST_ELEMENT (mmssrc));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  }

  return GST_DATA (buf);
}

static GstStateChangeReturn
gst_mms_change_state (GstElement * elem)
{
  GstMMS *mms = GST_MMS (elem);

  switch (GST_STATE_TRANSITION (elem)) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!mms->uri_name)
        return GST_STATE_CHANGE_FAILURE;
      /* FIXME: pass some sane arguments here */
      mms->connection = mms_connect (NULL, NULL, mms->uri_name, 128 * 1024);
      if (!mms->connection) {
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (elem);

  return GST_STATE_CHANGE_SUCCESS;
}

static void
gst_mms_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMMS *mmssrc;

  g_return_if_fail (GST_IS_MMS (object));
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

  g_return_if_fail (GST_IS_MMS (object));
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
  if (strcmp (protocol, "mms") != 0) {
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
