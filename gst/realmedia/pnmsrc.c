/* GStreamer
 * Copyright (C) <2009> Wim Taymans <wim.taymans@gmail.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include "pnmsrc.h"

GST_DEBUG_CATEGORY_STATIC (pnmsrc_debug);
#define GST_CAT_DEFAULT pnmsrc_debug

/* PNMSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_LOCATION	NULL

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_LAST
};

static GstStaticPadTemplate gst_pnm_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/vnd.rn-realmedia")
    );

static GstFlowReturn gst_pnm_src_create (GstPushSrc * psrc, GstBuffer ** buf);

static void gst_pnm_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

#define gst_pnm_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstPNMSrc, gst_pnm_src, GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_pnm_src_uri_handler_init));

static void gst_pnm_src_finalize (GObject * object);

static void gst_pnm_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pnm_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_pnm_src_class_init (GstPNMSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_pnm_src_set_property;
  gobject_class->get_property = gst_pnm_src_get_property;

  gobject_class->finalize = gst_pnm_src_finalize;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "PNM Location",
          "Location of the PNM url to read",
          DEFAULT_LOCATION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_pnm_src_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "PNM packet receiver", "Source/Network",
      "Receive data over the network via PNM",
      "Wim Taymans <wim.taymans@gmail.com>");

  gstpushsrc_class->create = gst_pnm_src_create;

  GST_DEBUG_CATEGORY_INIT (pnmsrc_debug, "pnmsrc",
      0, "Source for the pnm:// uri");
}

static void
gst_pnm_src_init (GstPNMSrc * pnmsrc)
{
  pnmsrc->location = g_strdup (DEFAULT_LOCATION);
}

gboolean
gst_pnm_src_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "pnmsrc",
      GST_RANK_MARGINAL, GST_TYPE_PNM_SRC);
}

static void
gst_pnm_src_finalize (GObject * object)
{
  GstPNMSrc *pnmsrc;

  pnmsrc = GST_PNM_SRC (object);

  g_free (pnmsrc->location);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_pnm_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPNMSrc *src;

  src = GST_PNM_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_free (src->location);
      src->location = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pnm_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPNMSrc *src;

  src = GST_PNM_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, src->location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_pnm_src_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstPNMSrc *src;
  GstMessage *m;
  gchar *url;

  src = GST_PNM_SRC (psrc);

  if (src->location == NULL)
    return GST_FLOW_ERROR;
  url = g_strdup_printf ("rtsp%s", &src->location[3]);

  /* the only thing we do is redirect to an RTSP url */
  m = gst_message_new_element (GST_OBJECT_CAST (src),
      gst_structure_new ("redirect", "new-location", G_TYPE_STRING, url, NULL));
  g_free (url);

  gst_element_post_message (GST_ELEMENT_CAST (src), m);


  return GST_FLOW_EOS;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
gst_pnm_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_pnm_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "pnm", NULL };

  return protocols;
}

static gchar *
gst_pnm_src_uri_get_uri (GstURIHandler * handler)
{
  GstPNMSrc *src = GST_PNM_SRC (handler);

  /* FIXME: make thread-safe */
  return g_strdup (src->location);
}

static gboolean
gst_pnm_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstPNMSrc *src = GST_PNM_SRC (handler);

  g_free (src->location);
  src->location = g_strdup (uri);

  return TRUE;
}

static void
gst_pnm_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_pnm_src_uri_get_type;
  iface->get_protocols = gst_pnm_src_uri_get_protocols;
  iface->get_uri = gst_pnm_src_uri_get_uri;
  iface->set_uri = gst_pnm_src_uri_set_uri;
}
