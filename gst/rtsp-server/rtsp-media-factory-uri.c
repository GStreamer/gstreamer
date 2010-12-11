/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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

#include "rtsp-media-factory-uri.h"

#define DEFAULT_URI     NULL

enum
{
  PROP_0,
  PROP_URI,
  PROP_LAST
};

static const gchar *factory_key = "GstRTSPMediaFactoryURI";

GST_DEBUG_CATEGORY (rtsp_media_factory_uri_debug);
#define GST_CAT_DEFAULT rtsp_media_factory_uri_debug

static void gst_rtsp_media_factory_uri_get_property (GObject * object,
    guint propid, GValue * value, GParamSpec * pspec);
static void gst_rtsp_media_factory_uri_set_property (GObject * object,
    guint propid, const GValue * value, GParamSpec * pspec);
static void gst_rtsp_media_factory_uri_finalize (GObject * obj);

static GstElement *rtsp_media_factory_uri_get_element (GstRTSPMediaFactory *
    factory, const GstRTSPUrl * url);

G_DEFINE_TYPE (GstRTSPMediaFactoryURI, gst_rtsp_media_factory_uri,
    GST_TYPE_RTSP_MEDIA_FACTORY);

static void
gst_rtsp_media_factory_uri_class_init (GstRTSPMediaFactoryURIClass * klass)
{
  GObjectClass *gobject_class;
  GstRTSPMediaFactoryClass *mediafactory_class;

  gobject_class = G_OBJECT_CLASS (klass);
  mediafactory_class = GST_RTSP_MEDIA_FACTORY_CLASS (klass);

  gobject_class->get_property = gst_rtsp_media_factory_uri_get_property;
  gobject_class->set_property = gst_rtsp_media_factory_uri_set_property;
  gobject_class->finalize = gst_rtsp_media_factory_uri_finalize;

  /**
   * GstRTSPMediaFactoryURI::uri
   *
   * The uri of the resource that will be served by this factory.
   */
  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "URI",
          "The URI of the resource to stream", DEFAULT_URI,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  mediafactory_class->get_element = rtsp_media_factory_uri_get_element;

  GST_DEBUG_CATEGORY_INIT (rtsp_media_factory_uri_debug, "rtspmediafactoryuri",
      0, "GstRTSPMediaFactoryUri");
}

static void
gst_rtsp_media_factory_uri_init (GstRTSPMediaFactoryURI * factory)
{
  factory->uri = g_strdup (DEFAULT_URI);
  factory->factories =
      gst_element_factory_list_get_elements (GST_ELEMENT_FACTORY_TYPE_PAYLOADER,
      GST_RANK_NONE);
}

static void
gst_rtsp_media_factory_uri_finalize (GObject * obj)
{
  GstRTSPMediaFactoryURI *factory = GST_RTSP_MEDIA_FACTORY_URI (obj);

  g_free (factory->uri);
  gst_plugin_feature_list_free (factory->factories);

  G_OBJECT_CLASS (gst_rtsp_media_factory_uri_parent_class)->finalize (obj);
}

static void
gst_rtsp_media_factory_uri_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPMediaFactoryURI *factory = GST_RTSP_MEDIA_FACTORY_URI (object);

  switch (propid) {
    case PROP_URI:
      g_value_take_string (value, gst_rtsp_media_factory_uri_get_uri (factory));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_media_factory_uri_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPMediaFactoryURI *factory = GST_RTSP_MEDIA_FACTORY_URI (object);

  switch (propid) {
    case PROP_URI:
      gst_rtsp_media_factory_uri_set_uri (factory, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_rtsp_media_factory_uri_new:
 *
 * Create a new #GstRTSPMediaFactoryURI instance.
 *
 * Returns: a new #GstRTSPMediaFactoryURI object.
 */
GstRTSPMediaFactoryURI *
gst_rtsp_media_factory_uri_new (void)
{
  GstRTSPMediaFactoryURI *result;

  result = g_object_new (GST_TYPE_RTSP_MEDIA_FACTORY_URI, NULL);

  return result;
}

/**
 * gst_rtsp_media_factory_uri_set_uri:
 * @factory: a #GstRTSPMediaFactory
 * @uri: the uri the stream
 *
 * Set the URI of the resource that will be streamed by this factory.
 */
void
gst_rtsp_media_factory_uri_set_uri (GstRTSPMediaFactoryURI * factory,
    const gchar * uri)
{
  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY_URI (factory));
  g_return_if_fail (uri != NULL);

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  g_free (factory->uri);
  factory->uri = g_strdup (uri);
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_uri_get_uri:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get the URI that will provide media for this factory.
 *
 * Returns: the configured URI. g_free() after usage.
 */
gchar *
gst_rtsp_media_factory_uri_get_uri (GstRTSPMediaFactoryURI * factory)
{
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY_URI (factory), NULL);

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  result = g_strdup (factory->uri);
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return result;
}

static GstElementFactory *
find_payloader (GstRTSPMediaFactoryURI * urifact, GstCaps * caps)
{
  GList *list, *tmp;
  GstElementFactory *factory = NULL;

  /* find payloader that can link */
  list = gst_element_factory_list_filter (urifact->factories, caps,
      GST_PAD_SINK, FALSE);

  for (tmp = list; tmp; tmp = g_list_next (tmp)) {
    GstElementFactory *f = GST_ELEMENT_FACTORY_CAST (tmp->data);
    const gchar *name;

    name = gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (f));
    if (strcmp (name, "gdppay") == 0)
      continue;

    factory = f;
    break;
  }
  if (factory)
    g_object_ref (factory);

  gst_plugin_feature_list_free (list);

  return factory;
}

static gboolean
autoplug_continue_cb (GstElement * uribin, GstPad * pad, GstCaps * caps,
    GstElement * element)
{
  GList *list, *tmp;
  GstRTSPMediaFactoryURI *urifact;
  GstElementFactory *factory;
  gboolean res;

  GST_DEBUG ("found pad %s:%s of caps %" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);

  urifact = g_object_get_data (G_OBJECT (element), factory_key);

  if (!(factory = find_payloader (urifact, caps)))
    goto no_factory;

  /* we found a payloader, stop autoplugging */
  GST_DEBUG ("found payloader factory %s",
      gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));
  gst_object_unref (factory);

  return FALSE;

  /* ERRORS */
no_factory:
  {
    /* no payloader, continue autoplugging */
    GST_DEBUG ("no payloader found");
    return TRUE;
  }
}

static void
pad_added_cb (GstElement * uribin, GstPad * pad, GstElement * element)
{
  GstRTSPMediaFactoryURI *urifact;
  GstElementFactory *factory;
  GstElement *payloader;
  GstCaps *caps;
  GstPad *sinkpad, *srcpad, *ghostpad;

  GST_DEBUG ("added pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  /* link the element now and expose the pad */
  urifact = g_object_get_data (G_OBJECT (element), factory_key);

  caps = gst_pad_get_caps (pad);
  if (caps == NULL)
    goto no_caps;

  if (!(factory = find_payloader (urifact, caps)))
    goto no_factory;

  gst_caps_unref (caps);

  /* we have a payloader now */
  GST_DEBUG ("found payloader factory %s",
      gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));

  payloader = gst_element_factory_create (factory, NULL);
  if (payloader == NULL)
    goto no_payloader;

  /* add the payloader to the pipeline */
  gst_bin_add (GST_BIN_CAST (element), payloader);

  gst_element_set_state (payloader, GST_STATE_PLAYING);

  /* link the pad to the sinkpad of the payloader */
  sinkpad = gst_element_get_static_pad (payloader, "sink");
  gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);

  /* now expose the srcpad of the payloader as a ghostpad with the same name
   * as the uridecodebin pad name. */
  srcpad = gst_element_get_static_pad (payloader, "src");
  ghostpad = gst_ghost_pad_new (GST_PAD_NAME (pad), srcpad);
  gst_object_unref (srcpad);

  gst_pad_set_active (ghostpad, TRUE);
  gst_element_add_pad (element, ghostpad);

  return;

  /* ERRORS */
no_caps:
  {
    GST_WARNING ("could not get caps from pad");
    return;
  }
no_factory:
  {
    GST_DEBUG ("no payloader found");
    gst_caps_unref (caps);
    return;
  }
no_payloader:
  {
    GST_ERROR ("could not create payloader from factory");
    gst_caps_unref (caps);
    return;
  }
}

static void
no_more_pads_cb (GstElement * uribin, GstElement * element)
{
  GST_DEBUG ("no-more-pads");
  gst_element_no_more_pads (element);
}

static GstElement *
rtsp_media_factory_uri_get_element (GstRTSPMediaFactory * factory,
    const GstRTSPUrl * url)
{
  GstElement *topbin, *element, *uribin;
  GstRTSPMediaFactoryURI *urifact;

  urifact = GST_RTSP_MEDIA_FACTORY_URI_CAST (factory);

  GST_LOG ("creating element");

  topbin = gst_bin_new ("GstRTSPMediaFactoryURI");
  g_assert (topbin != NULL);

  /* our bin will dynamically expose payloaded pads */
  element = gst_bin_new ("dynpay0");
  g_assert (element != NULL);

  uribin = gst_element_factory_make ("uridecodebin", "uribin");
  if (uribin == NULL)
    goto no_uridecodebin;

  g_object_set (uribin, "uri", urifact->uri, NULL);

  /* keep factory around */
  g_object_set_data_full (G_OBJECT (element), factory_key,
      g_object_ref (factory), g_object_unref);

  /* connect to the signals */
  g_signal_connect (uribin, "autoplug-continue",
      (GCallback) autoplug_continue_cb, element);
  g_signal_connect (uribin, "pad-added", (GCallback) pad_added_cb, element);
  g_signal_connect (uribin, "no-more-pads", (GCallback) no_more_pads_cb,
      element);

  gst_bin_add (GST_BIN_CAST (element), uribin);
  gst_bin_add (GST_BIN_CAST (topbin), element);

  return topbin;

no_uridecodebin:
  {
    g_critical ("can't create uridecodebin element");
    g_object_unref (element);
    return NULL;
  }
}
