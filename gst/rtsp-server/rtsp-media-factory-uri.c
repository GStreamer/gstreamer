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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:rtsp-media-factory-uri
 * @short_description: A factory for URI sources
 * @see_also: #GstRTSPMediaFactory, #GstRTSPMedia
 *
 * This specialized #GstRTSPMediaFactory constructs media pipelines from a URI,
 * given with gst_rtsp_media_factory_uri_set_uri().
 *
 * It will automatically demux and payload the different streams found in the
 * media at URL.
 *
 * Last reviewed on 2013-07-11 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "rtsp-media-factory-uri.h"

struct _GstRTSPMediaFactoryURIPrivate
{
  GMutex lock;
  gchar *uri;                   /* protected by lock */
  gboolean use_gstpay;

  GstCaps *raw_vcaps;
  GstCaps *raw_acaps;
  GList *demuxers;
  GList *payloaders;
  GList *decoders;
};

#define DEFAULT_URI         NULL
#define DEFAULT_USE_GSTPAY  FALSE

enum
{
  PROP_0,
  PROP_URI,
  PROP_USE_GSTPAY,
  PROP_LAST
};


#define RAW_VIDEO_CAPS \
    "video/x-raw"

#define RAW_AUDIO_CAPS \
    "audio/x-raw"

static GstStaticCaps raw_video_caps = GST_STATIC_CAPS (RAW_VIDEO_CAPS);
static GstStaticCaps raw_audio_caps = GST_STATIC_CAPS (RAW_AUDIO_CAPS);

typedef struct
{
  GstRTSPMediaFactoryURI *factory;
  guint pt;
} FactoryData;

static void
free_data (FactoryData * data)
{
  g_object_unref (data->factory);
  g_free (data);
}

static const gchar *factory_key = "GstRTSPMediaFactoryURI";

GST_DEBUG_CATEGORY_STATIC (rtsp_media_factory_uri_debug);
#define GST_CAT_DEFAULT rtsp_media_factory_uri_debug

static void gst_rtsp_media_factory_uri_get_property (GObject * object,
    guint propid, GValue * value, GParamSpec * pspec);
static void gst_rtsp_media_factory_uri_set_property (GObject * object,
    guint propid, const GValue * value, GParamSpec * pspec);
static void gst_rtsp_media_factory_uri_finalize (GObject * obj);

static GstElement *rtsp_media_factory_uri_create_element (GstRTSPMediaFactory *
    factory, const GstRTSPUrl * url);

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPMediaFactoryURI, gst_rtsp_media_factory_uri,
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
   * GstRTSPMediaFactoryURI::uri:
   *
   * The uri of the resource that will be served by this factory.
   */
  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "URI",
          "The URI of the resource to stream", DEFAULT_URI,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPMediaFactoryURI::use-gstpay:
   *
   * Allow the usage of gstpay in order to avoid decoding of compressed formats
   * without a payloader.
   */
  g_object_class_install_property (gobject_class, PROP_USE_GSTPAY,
      g_param_spec_boolean ("use-gstpay", "Use gstpay",
          "Use the gstpay payloader to avoid decoding", DEFAULT_USE_GSTPAY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  mediafactory_class->create_element = rtsp_media_factory_uri_create_element;

  GST_DEBUG_CATEGORY_INIT (rtsp_media_factory_uri_debug, "rtspmediafactoryuri",
      0, "GstRTSPMediaFactoryUri");
}

typedef struct
{
  GList *demux;
  GList *payload;
  GList *decode;
} FilterData;

static gboolean
payloader_filter (GstPluginFeature * feature, FilterData * data)
{
  const gchar *klass;
  GstElementFactory *fact;
  GList **list = NULL;

  /* we only care about element factories */
  if (G_UNLIKELY (!GST_IS_ELEMENT_FACTORY (feature)))
    return FALSE;

  if (gst_plugin_feature_get_rank (feature) < GST_RANK_MARGINAL)
    return FALSE;

  fact = GST_ELEMENT_FACTORY_CAST (feature);

  klass = gst_element_factory_get_metadata (fact, GST_ELEMENT_METADATA_KLASS);

  if (strstr (klass, "Decoder"))
    list = &data->decode;
  else if (strstr (klass, "Demux"))
    list = &data->demux;
  else if (strstr (klass, "Parser") && strstr (klass, "Codec"))
    list = &data->demux;
  else if (strstr (klass, "Payloader") && strstr (klass, "RTP"))
    list = &data->payload;

  if (list) {
    GST_DEBUG ("adding %s", GST_OBJECT_NAME (fact));
    *list = g_list_prepend (*list, gst_object_ref (fact));
  }

  return FALSE;
}

static void
gst_rtsp_media_factory_uri_init (GstRTSPMediaFactoryURI * factory)
{
  GstRTSPMediaFactoryURIPrivate *priv =
      gst_rtsp_media_factory_uri_get_instance_private (factory);
  FilterData data = { NULL, NULL, NULL };

  GST_DEBUG_OBJECT (factory, "new");

  factory->priv = priv;

  priv->uri = g_strdup (DEFAULT_URI);
  priv->use_gstpay = DEFAULT_USE_GSTPAY;
  g_mutex_init (&priv->lock);

  /* get the feature list using the filter */
  gst_registry_feature_filter (gst_registry_get (), (GstPluginFeatureFilter)
      payloader_filter, FALSE, &data);
  /* sort */
  priv->demuxers =
      g_list_sort (data.demux, gst_plugin_feature_rank_compare_func);
  priv->payloaders =
      g_list_sort (data.payload, gst_plugin_feature_rank_compare_func);
  priv->decoders =
      g_list_sort (data.decode, gst_plugin_feature_rank_compare_func);

  priv->raw_vcaps = gst_static_caps_get (&raw_video_caps);
  priv->raw_acaps = gst_static_caps_get (&raw_audio_caps);
}

static void
gst_rtsp_media_factory_uri_finalize (GObject * obj)
{
  GstRTSPMediaFactoryURI *factory = GST_RTSP_MEDIA_FACTORY_URI (obj);
  GstRTSPMediaFactoryURIPrivate *priv = factory->priv;

  GST_DEBUG_OBJECT (factory, "finalize");

  g_free (priv->uri);
  gst_plugin_feature_list_free (priv->demuxers);
  gst_plugin_feature_list_free (priv->payloaders);
  gst_plugin_feature_list_free (priv->decoders);
  gst_caps_unref (priv->raw_vcaps);
  gst_caps_unref (priv->raw_acaps);
  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (gst_rtsp_media_factory_uri_parent_class)->finalize (obj);
}

static void
gst_rtsp_media_factory_uri_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPMediaFactoryURI *factory = GST_RTSP_MEDIA_FACTORY_URI (object);
  GstRTSPMediaFactoryURIPrivate *priv = factory->priv;

  switch (propid) {
    case PROP_URI:
      g_value_take_string (value, gst_rtsp_media_factory_uri_get_uri (factory));
      break;
    case PROP_USE_GSTPAY:
      g_value_set_boolean (value, priv->use_gstpay);
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
  GstRTSPMediaFactoryURIPrivate *priv = factory->priv;

  switch (propid) {
    case PROP_URI:
      gst_rtsp_media_factory_uri_set_uri (factory, g_value_get_string (value));
      break;
    case PROP_USE_GSTPAY:
      priv->use_gstpay = g_value_get_boolean (value);
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
 * Returns: (transfer full): a new #GstRTSPMediaFactoryURI object.
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
  GstRTSPMediaFactoryURIPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY_URI (factory));
  g_return_if_fail (uri != NULL);

  priv = factory->priv;

  g_mutex_lock (&priv->lock);
  g_free (priv->uri);
  priv->uri = g_strdup (uri);
  g_mutex_unlock (&priv->lock);
}

/**
 * gst_rtsp_media_factory_uri_get_uri:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get the URI that will provide media for this factory.
 *
 * Returns: (transfer full): the configured URI. g_free() after usage.
 */
gchar *
gst_rtsp_media_factory_uri_get_uri (GstRTSPMediaFactoryURI * factory)
{
  GstRTSPMediaFactoryURIPrivate *priv;
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY_URI (factory), NULL);

  priv = factory->priv;

  g_mutex_lock (&priv->lock);
  result = g_strdup (priv->uri);
  g_mutex_unlock (&priv->lock);

  return result;
}

static GstElementFactory *
find_payloader (GstRTSPMediaFactoryURI * urifact, GstCaps * caps)
{
  GstRTSPMediaFactoryURIPrivate *priv = urifact->priv;
  GList *list;
  GstElementFactory *factory = NULL;
  gboolean autoplug_more = FALSE;

  /* first find a demuxer that can link */
  list = gst_element_factory_list_filter (priv->demuxers, caps,
      GST_PAD_SINK, FALSE);

  if (list) {
    GstStructure *structure = gst_caps_get_structure (caps, 0);
    gboolean parsed = FALSE;
    gint mpegversion = 0;

    if (!gst_structure_get_boolean (structure, "parsed", &parsed) &&
        gst_structure_has_name (structure, "audio/mpeg") &&
        gst_structure_get_int (structure, "mpegversion", &mpegversion) &&
        (mpegversion == 2 || mpegversion == 4)) {
      /* for AAC it's framed=true instead of parsed=true */
      gst_structure_get_boolean (structure, "framed", &parsed);
    }

    /* Avoid plugging parsers in a loop. This is not 100% correct, as some
     * parsers don't set parsed=true in caps. We should do something like
     * decodebin does and track decode chains and elements plugged in those
     * chains...
     */
    if (parsed) {
      GList *walk;
      const gchar *klass;

      for (walk = list; walk; walk = walk->next) {
        factory = GST_ELEMENT_FACTORY (walk->data);
        klass = gst_element_factory_get_metadata (factory,
            GST_ELEMENT_METADATA_KLASS);
        if (strstr (klass, "Parser"))
          /* caps have parsed=true, so skip this parser to avoid loops */
          continue;

        autoplug_more = TRUE;
        break;
      }
    } else {
      /* caps don't have parsed=true set and we have a demuxer/parser */
      autoplug_more = TRUE;
    }

    gst_plugin_feature_list_free (list);
  }

  if (autoplug_more)
    /* we have a demuxer, try that one first */
    return NULL;

  /* no demuxer try a depayloader */
  list = gst_element_factory_list_filter (priv->payloaders, caps,
      GST_PAD_SINK, FALSE);

  if (list == NULL) {
    if (priv->use_gstpay) {
      /* no depayloader or parser/demuxer, use gstpay when allowed */
      factory = gst_element_factory_find ("rtpgstpay");
    } else {
      /* no depayloader, try a decoder, we'll get to a payloader for a decoded
       * video or audio format, worst case. */
      list = gst_element_factory_list_filter (priv->decoders, caps,
          GST_PAD_SINK, FALSE);

      if (list != NULL) {
        /* we have a decoder, try that one first */
        gst_plugin_feature_list_free (list);
        return NULL;
      }
    }
  }

  if (list != NULL) {
    factory = GST_ELEMENT_FACTORY_CAST (list->data);
    g_object_ref (factory);
    gst_plugin_feature_list_free (list);
  }
  return factory;
}

static gboolean
autoplug_continue_cb (GstElement * uribin, GstPad * pad, GstCaps * caps,
    GstElement * element)
{
  FactoryData *data;
  GstElementFactory *factory;

  GST_DEBUG ("found pad %s:%s of caps %" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);

  data = g_object_get_data (G_OBJECT (element), factory_key);

  if (!(factory = find_payloader (data->factory, caps)))
    goto no_factory;

  /* we found a payloader, stop autoplugging so we can plug the
   * payloader. */
  GST_DEBUG ("found factory %s",
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
  GstRTSPMediaFactoryURIPrivate *priv;
  FactoryData *data;
  GstElementFactory *factory;
  GstElement *payloader;
  GstCaps *caps;
  GstPad *sinkpad, *srcpad, *ghostpad;
  GstElement *convert;
  gchar *padname, *payloader_name;

  GST_DEBUG ("added pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  /* link the element now and expose the pad */
  data = g_object_get_data (G_OBJECT (element), factory_key);
  urifact = data->factory;
  priv = urifact->priv;

  /* ref to make refcounting easier later */
  gst_object_ref (pad);
  padname = gst_pad_get_name (pad);

  /* get pad caps first, then call get_caps, then fail */
  if ((caps = gst_pad_get_current_caps (pad)) == NULL)
    if ((caps = gst_pad_query_caps (pad, NULL)) == NULL)
      goto no_caps;

  /* check for raw caps */
  if (gst_caps_can_intersect (caps, priv->raw_vcaps)) {
    /* we have raw video caps, insert converter */
    convert = gst_element_factory_make ("videoconvert", NULL);
  } else if (gst_caps_can_intersect (caps, priv->raw_acaps)) {
    /* we have raw audio caps, insert converter */
    convert = gst_element_factory_make ("audioconvert", NULL);
  } else {
    convert = NULL;
  }

  if (convert) {
    gst_bin_add (GST_BIN_CAST (element), convert);
    gst_element_set_state (convert, GST_STATE_PLAYING);

    sinkpad = gst_element_get_static_pad (convert, "sink");
    gst_pad_link (pad, sinkpad);
    gst_object_unref (sinkpad);

    /* unref old pad, we reffed before */
    gst_object_unref (pad);
    gst_caps_unref (caps);

    /* continue with new pad and caps */
    pad = gst_element_get_static_pad (convert, "src");
    if ((caps = gst_pad_get_current_caps (pad)) == NULL)
      if ((caps = gst_pad_query_caps (pad, NULL)) == NULL)
        goto no_caps;
  }

  if (!(factory = find_payloader (urifact, caps)))
    goto no_factory;

  gst_caps_unref (caps);

  /* we have a payloader now */
  GST_DEBUG ("found payloader factory %s",
      gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));

  payloader_name = g_strdup_printf ("pay_%s", padname);
  payloader = gst_element_factory_create (factory, payloader_name);
  g_free (payloader_name);
  if (payloader == NULL)
    goto no_payloader;

  g_object_set (payloader, "pt", data->pt, NULL);
  data->pt++;

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (payloader),
          "buffer-list"))
    g_object_set (payloader, "buffer-list", TRUE, NULL);

  /* add the payloader to the pipeline */
  gst_bin_add (GST_BIN_CAST (element), payloader);
  gst_element_set_state (payloader, GST_STATE_PLAYING);

  /* link the pad to the sinkpad of the payloader */
  sinkpad = gst_element_get_static_pad (payloader, "sink");
  gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);
  gst_object_unref (pad);

  /* now expose the srcpad of the payloader as a ghostpad with the same name
   * as the uridecodebin pad name. */
  srcpad = gst_element_get_static_pad (payloader, "src");
  ghostpad = gst_ghost_pad_new (padname, srcpad);
  gst_object_unref (srcpad);
  g_free (padname);

  gst_pad_set_active (ghostpad, TRUE);
  gst_element_add_pad (element, ghostpad);

  return;

  /* ERRORS */
no_caps:
  {
    GST_WARNING ("could not get caps from pad");
    g_free (padname);
    gst_object_unref (pad);
    return;
  }
no_factory:
  {
    GST_DEBUG ("no payloader found");
    g_free (padname);
    gst_caps_unref (caps);
    gst_object_unref (pad);
    return;
  }
no_payloader:
  {
    GST_ERROR ("could not create payloader from factory");
    g_free (padname);
    gst_caps_unref (caps);
    gst_object_unref (pad);
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
rtsp_media_factory_uri_create_element (GstRTSPMediaFactory * factory,
    const GstRTSPUrl * url)
{
  GstRTSPMediaFactoryURIPrivate *priv;
  GstElement *topbin, *element, *uribin;
  GstRTSPMediaFactoryURI *urifact;
  FactoryData *data;

  urifact = GST_RTSP_MEDIA_FACTORY_URI_CAST (factory);
  priv = urifact->priv;

  GST_LOG ("creating element");

  topbin = gst_bin_new ("GstRTSPMediaFactoryURI");
  g_assert (topbin != NULL);

  /* our bin will dynamically expose payloaded pads */
  element = gst_bin_new ("dynpay0");
  g_assert (element != NULL);

  uribin = gst_element_factory_make ("uridecodebin", "uribin");
  if (uribin == NULL)
    goto no_uridecodebin;

  g_object_set (uribin, "uri", priv->uri, NULL);

  /* keep factory data around */
  data = g_new0 (FactoryData, 1);
  data->factory = g_object_ref (urifact);
  data->pt = 96;

  g_object_set_data_full (G_OBJECT (element), factory_key,
      data, (GDestroyNotify) free_data);

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
    gst_object_unref (element);
    return NULL;
  }
}
