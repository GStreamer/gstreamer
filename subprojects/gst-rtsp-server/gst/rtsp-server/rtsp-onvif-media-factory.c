/* GStreamer
 * Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
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
 * SECTION:rtsp-onvif-media-factory
 * @short_description: A factory for ONVIF media pipelines
 * @see_also: #GstRTSPMediaFactory, #GstRTSPOnvifMedia
 *
 * The #GstRTSPOnvifMediaFactory is responsible for creating or recycling
 * #GstRTSPMedia objects based on the passed URL. Different to
 * #GstRTSPMediaFactory, this supports special ONVIF features and can create
 * #GstRTSPOnvifMedia in addition to normal #GstRTSPMedia.
 *
 * Special ONVIF features that are currently supported is a backchannel for
 * the client to send back media to the server in a normal PLAY media, see
 * gst_rtsp_onvif_media_factory_set_backchannel_launch() and
 * gst_rtsp_onvif_media_factory_set_backchannel_bandwidth().
 *
 * Since: 1.14
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "rtsp-onvif-media-factory.h"
#include "rtsp-onvif-media.h"
#include "rtsp-onvif-server.h"

struct GstRTSPOnvifMediaFactoryPrivate
{
  GMutex lock;
  gchar *backchannel_launch;
  guint backchannel_bandwidth;
  gboolean has_replay_support;
};

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPOnvifMediaFactory,
    gst_rtsp_onvif_media_factory, GST_TYPE_RTSP_MEDIA_FACTORY);

/**
 * gst_rtsp_onvif_media_factory_requires_backchannel:
 * @factory: a #GstRTSPMediaFactory
 *
 * Checks whether the client request requires backchannel.
 *
 * Returns: %TRUE if the client request requires backchannel.
 *
 * Since: 1.14
 */
gboolean
gst_rtsp_onvif_media_factory_requires_backchannel (GstRTSPMediaFactory *
    factory, GstRTSPContext * ctx)
{
  GstRTSPMessage *msg = ctx->request;
  GstRTSPResult res;
  gint i;
  gchar *reqs = NULL;

  g_return_val_if_fail (GST_IS_RTSP_ONVIF_MEDIA_FACTORY (factory), FALSE);

  i = 0;
  do {
    res = gst_rtsp_message_get_header (msg, GST_RTSP_HDR_REQUIRE, &reqs, i++);

    if (res == GST_RTSP_ENOTIMPL)
      break;

    if (strcmp (reqs, GST_RTSP_ONVIF_BACKCHANNEL_REQUIREMENT) == 0)
      return TRUE;
  } while (TRUE);

  return FALSE;
}

static gchar *
gst_rtsp_onvif_media_factory_gen_key (GstRTSPMediaFactory * factory,
    const GstRTSPUrl * url)
{
  GstRTSPContext *ctx = gst_rtsp_context_get_current ();

  /* Only medias where no backchannel was requested can be shared */
  if (gst_rtsp_onvif_media_factory_requires_backchannel (factory, ctx))
    return NULL;

  return
      GST_RTSP_MEDIA_FACTORY_CLASS
      (gst_rtsp_onvif_media_factory_parent_class)->gen_key (factory, url);
}

static GstRTSPMedia *
gst_rtsp_onvif_media_factory_construct (GstRTSPMediaFactory * factory,
    const GstRTSPUrl * url)
{
  GstRTSPMedia *media;
  GstElement *element, *pipeline;
  GstRTSPMediaFactoryClass *klass;
  GType media_gtype;
  gboolean got_backchannel_stream;
  GstRTSPContext *ctx = gst_rtsp_context_get_current ();

  /* Mostly a copy of the default implementation but with backchannel support below,
   * unfortunately we can't re-use the default one because of how the virtual
   * method is define */

  /* Everything but play is unsupported */
  if (gst_rtsp_media_factory_get_transport_mode (factory) !=
      GST_RTSP_TRANSPORT_MODE_PLAY)
    return NULL;

  /* we only support onvif media here: otherwise a plain GstRTSPMediaFactory
   * could've been used as well */
  media_gtype = gst_rtsp_media_factory_get_media_gtype (factory);
  if (!g_type_is_a (media_gtype, GST_TYPE_RTSP_ONVIF_MEDIA))
    return NULL;

  klass = GST_RTSP_MEDIA_FACTORY_GET_CLASS (factory);

  if (!klass->create_pipeline)
    goto no_create;

  element = gst_rtsp_media_factory_create_element (factory, url);
  if (element == NULL)
    goto no_element;

  /* create a new empty media */
  media =
      g_object_new (media_gtype, "element", element,
      "transport-mode", GST_RTSP_TRANSPORT_MODE_PLAY, NULL);

  /* we need to call this prior to collecting streams */
  gst_rtsp_media_set_ensure_keyunit_on_start (media,
      gst_rtsp_media_factory_get_ensure_keyunit_on_start (factory));

  /* this adds the non-backchannel streams */
  gst_rtsp_media_collect_streams (media);

  /* this adds the backchannel stream */
  got_backchannel_stream =
      gst_rtsp_onvif_media_collect_backchannel (GST_RTSP_ONVIF_MEDIA (media));
  /* FIXME: This should not happen! We checked for that before */
  if (gst_rtsp_onvif_media_factory_requires_backchannel (factory, ctx) &&
      !got_backchannel_stream) {
    g_object_unref (media);
    return NULL;
  }

  pipeline = klass->create_pipeline (factory, media);
  if (pipeline == NULL)
    goto no_pipeline;

  gst_rtsp_onvif_media_set_backchannel_bandwidth (GST_RTSP_ONVIF_MEDIA (media),
      GST_RTSP_ONVIF_MEDIA_FACTORY (factory)->priv->backchannel_bandwidth);

  return media;

  /* ERRORS */
no_create:
  {
    g_critical ("no create_pipeline function");
    return NULL;
  }
no_element:
  {
    g_critical ("could not create element");
    return NULL;
  }
no_pipeline:
  {
    g_critical ("can't create pipeline");
    g_object_unref (media);
    return NULL;
  }
}

static GstElement *
gst_rtsp_onvif_media_factory_create_element (GstRTSPMediaFactory * factory,
    const GstRTSPUrl * url)
{
  GstElement *element;
  GError *error = NULL;
  gchar *launch;
  GstRTSPContext *ctx = gst_rtsp_context_get_current ();

  /* Mostly a copy of the default implementation but with backchannel support below,
   * unfortunately we can't re-use the default one because of how the virtual
   * method is define */

  launch = gst_rtsp_media_factory_get_launch (factory);

  /* we need a parse syntax */
  if (launch == NULL)
    goto no_launch;

  /* parse the user provided launch line */
  element =
      gst_parse_launch_full (launch, NULL, GST_PARSE_FLAG_PLACE_IN_BIN, &error);
  if (element == NULL)
    goto parse_error;

  g_free (launch);

  if (error != NULL) {
    /* a recoverable error was encountered */
    GST_WARNING ("recoverable parsing error: %s", error->message);
    g_error_free (error);
  }

  /* add backchannel pipeline part, if requested */
  if (gst_rtsp_onvif_media_factory_requires_backchannel (factory, ctx)) {
    GstRTSPOnvifMediaFactory *onvif_factory =
        GST_RTSP_ONVIF_MEDIA_FACTORY (factory);
    GstElement *backchannel_bin;
    GstElement *backchannel_depay;
    GstPad *depay_pad, *depay_ghostpad;

    launch =
        gst_rtsp_onvif_media_factory_get_backchannel_launch (onvif_factory);
    if (launch == NULL)
      goto no_launch_backchannel;

    backchannel_bin =
        gst_parse_bin_from_description_full (launch, FALSE, NULL,
        GST_PARSE_FLAG_PLACE_IN_BIN, &error);
    if (backchannel_bin == NULL)
      goto parse_error_backchannel;

    g_free (launch);

    if (error != NULL) {
      /* a recoverable error was encountered */
      GST_WARNING ("recoverable parsing error: %s", error->message);
      g_error_free (error);
    }

    gst_object_set_name (GST_OBJECT (backchannel_bin), "onvif-backchannel");

    backchannel_depay =
        gst_bin_get_by_name (GST_BIN (backchannel_bin), "depay_backchannel");
    if (!backchannel_depay) {
      gst_object_unref (backchannel_bin);
      goto wrongly_formatted_backchannel_bin;
    }

    depay_pad = gst_element_get_static_pad (backchannel_depay, "sink");
    if (!depay_pad) {
      gst_object_unref (backchannel_depay);
      gst_object_unref (backchannel_bin);
      goto wrongly_formatted_backchannel_bin;
    }

    depay_ghostpad = gst_ghost_pad_new ("sink", depay_pad);
    gst_element_add_pad (backchannel_bin, depay_ghostpad);

    gst_bin_add (GST_BIN (element), backchannel_bin);
  }

  return element;

  /* ERRORS */
no_launch:
  {
    g_critical ("no launch line specified");
    g_free (launch);
    return NULL;
  }
parse_error:
  {
    g_critical ("could not parse launch syntax (%s): %s", launch,
        (error ? error->message : "unknown reason"));
    if (error)
      g_error_free (error);
    g_free (launch);
    return NULL;
  }
no_launch_backchannel:
  {
    g_critical ("no backchannel launch line specified");
    gst_object_unref (element);
    return NULL;
  }
parse_error_backchannel:
  {
    g_critical ("could not parse backchannel launch syntax (%s): %s", launch,
        (error ? error->message : "unknown reason"));
    if (error)
      g_error_free (error);
    g_free (launch);
    gst_object_unref (element);
    return NULL;
  }

wrongly_formatted_backchannel_bin:
  {
    g_critical ("invalidly formatted backchannel bin");

    gst_object_unref (element);
    return NULL;
  }
}

static gboolean
    gst_rtsp_onvif_media_factory_has_backchannel_support_default
    (GstRTSPOnvifMediaFactory * factory)
{
  /* No locking here, we just check if it's non-NULL */
  return factory->priv->backchannel_launch != NULL;
}

static void
gst_rtsp_onvif_media_factory_finalize (GObject * object)
{
  GstRTSPOnvifMediaFactory *factory = GST_RTSP_ONVIF_MEDIA_FACTORY (object);

  g_free (factory->priv->backchannel_launch);
  factory->priv->backchannel_launch = NULL;

  g_mutex_clear (&factory->priv->lock);

  G_OBJECT_CLASS (gst_rtsp_onvif_media_factory_parent_class)->finalize (object);
}

static void
gst_rtsp_onvif_media_factory_class_init (GstRTSPOnvifMediaFactoryClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstRTSPMediaFactoryClass *factory_klass = (GstRTSPMediaFactoryClass *) klass;

  gobject_class->finalize = gst_rtsp_onvif_media_factory_finalize;

  factory_klass->gen_key = gst_rtsp_onvif_media_factory_gen_key;
  factory_klass->construct = gst_rtsp_onvif_media_factory_construct;
  factory_klass->create_element = gst_rtsp_onvif_media_factory_create_element;

  klass->has_backchannel_support =
      gst_rtsp_onvif_media_factory_has_backchannel_support_default;
}

static void
gst_rtsp_onvif_media_factory_init (GstRTSPOnvifMediaFactory * factory)
{
  factory->priv = gst_rtsp_onvif_media_factory_get_instance_private (factory);
  g_mutex_init (&factory->priv->lock);
}

/**
 * gst_rtsp_onvif_media_factory_set_backchannel_launch:
 * @factory: a #GstRTSPMediaFactory
 * @launch: (nullable): the launch description
 *
 * The gst_parse_launch() line to use for constructing the ONVIF backchannel
 * pipeline in the default prepare vmethod if requested by the client.
 *
 * The pipeline description should return a GstBin as the toplevel element
 * which can be accomplished by enclosing the description with brackets '('
 * ')'.
 *
 * The description should return a pipeline with a single depayloader named
 * depay_backchannel. A caps query on the depayloader's sinkpad should return
 * all possible, complete RTP caps that are going to be supported. At least
 * the payload type, clock-rate and encoding-name need to be specified.
 *
 * Note: The pipeline part passed here must end in sinks that are not waiting
 * until pre-rolling before reaching the PAUSED state, i.e. setting
 * async=false on #GstBaseSink. Otherwise the whole media will not be able to
 * prepare.
 *
 * Since: 1.14
 */
void
gst_rtsp_onvif_media_factory_set_backchannel_launch (GstRTSPOnvifMediaFactory *
    factory, const gchar * launch)
{
  g_return_if_fail (GST_IS_RTSP_ONVIF_MEDIA_FACTORY (factory));

  g_mutex_lock (&factory->priv->lock);
  g_free (factory->priv->backchannel_launch);
  factory->priv->backchannel_launch = g_strdup (launch);
  g_mutex_unlock (&factory->priv->lock);
}

/**
 * gst_rtsp_onvif_media_factory_get_backchannel_launch:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get the gst_parse_launch() pipeline description that will be used in the
 * default prepare vmethod for generating the ONVIF backchannel pipeline.
 *
 * Returns: (transfer full) (nullable): the configured backchannel launch description. g_free() after
 * usage.
 *
 * Since: 1.14
 */
gchar *
gst_rtsp_onvif_media_factory_get_backchannel_launch (GstRTSPOnvifMediaFactory *
    factory)
{
  gchar *launch;

  g_return_val_if_fail (GST_IS_RTSP_ONVIF_MEDIA_FACTORY (factory), NULL);

  g_mutex_lock (&factory->priv->lock);
  launch = g_strdup (factory->priv->backchannel_launch);
  g_mutex_unlock (&factory->priv->lock);

  return launch;
}

/**
 * gst_rtsp_onvif_media_factory_has_backchannel_support:
 * @factory: a #GstRTSPMediaFactory
 *
 * Returns %TRUE if an ONVIF backchannel is supported by the media factory.
 *
 * Returns: %TRUE if an ONVIF backchannel is supported by the media factory.
 *
 * Since: 1.14
 */
gboolean
gst_rtsp_onvif_media_factory_has_backchannel_support (GstRTSPOnvifMediaFactory *
    factory)
{
  GstRTSPOnvifMediaFactoryClass *klass;

  g_return_val_if_fail (GST_IS_RTSP_ONVIF_MEDIA_FACTORY (factory), FALSE);

  klass = GST_RTSP_ONVIF_MEDIA_FACTORY_GET_CLASS (factory);

  if (klass->has_backchannel_support)
    return klass->has_backchannel_support (factory);

  return FALSE;
}

/**
 * gst_rtsp_onvif_media_factory_has_replay_support:
 *
 * Returns: %TRUE if ONVIF replay is supported by the media factory.
 *
 * Since: 1.18
 */
gboolean
gst_rtsp_onvif_media_factory_has_replay_support (GstRTSPOnvifMediaFactory *
    factory)
{
  gboolean has_replay_support;

  g_mutex_lock (&factory->priv->lock);
  has_replay_support = factory->priv->has_replay_support;
  g_mutex_unlock (&factory->priv->lock);

  return has_replay_support;
}

/**
 * gst_rtsp_onvif_media_factory_set_replay_support:
 *
 * Set to %TRUE if ONVIF replay is supported by the media factory.
 *
 * Since: 1.18
 */
void
gst_rtsp_onvif_media_factory_set_replay_support (GstRTSPOnvifMediaFactory *
    factory, gboolean has_replay_support)
{
  g_mutex_lock (&factory->priv->lock);
  factory->priv->has_replay_support = has_replay_support;
  g_mutex_unlock (&factory->priv->lock);
}

/**
 * gst_rtsp_onvif_media_factory_set_backchannel_bandwidth:
 * @factory: a #GstRTSPMediaFactory
 * @bandwidth: the bandwidth in bits per second
 *
 * Set the configured/supported bandwidth of the ONVIF backchannel pipeline in
 * bits per second.
 *
 * Since: 1.14
 */
void
gst_rtsp_onvif_media_factory_set_backchannel_bandwidth (GstRTSPOnvifMediaFactory
    * factory, guint bandwidth)
{
  g_return_if_fail (GST_IS_RTSP_ONVIF_MEDIA_FACTORY (factory));

  g_mutex_lock (&factory->priv->lock);
  factory->priv->backchannel_bandwidth = bandwidth;
  g_mutex_unlock (&factory->priv->lock);
}

/**
 * gst_rtsp_onvif_media_factory_get_backchannel_bandwidth:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get the configured/supported bandwidth of the ONVIF backchannel pipeline in
 * bits per second.
 *
 * Returns: the configured/supported backchannel bandwidth.
 *
 * Since: 1.14
 */
guint
gst_rtsp_onvif_media_factory_get_backchannel_bandwidth (GstRTSPOnvifMediaFactory
    * factory)
{
  guint bandwidth;

  g_return_val_if_fail (GST_IS_RTSP_ONVIF_MEDIA_FACTORY (factory), 0);

  g_mutex_lock (&factory->priv->lock);
  bandwidth = factory->priv->backchannel_bandwidth;
  g_mutex_unlock (&factory->priv->lock);

  return bandwidth;
}

/**
 * gst_rtsp_onvif_media_factory_new:
 *
 * Create a new #GstRTSPOnvifMediaFactory
 *
 * Returns: A new #GstRTSPOnvifMediaFactory
 *
 * Since: 1.14
 */
GstRTSPMediaFactory *
gst_rtsp_onvif_media_factory_new (void)
{
  return g_object_new (GST_TYPE_RTSP_ONVIF_MEDIA_FACTORY, NULL);
}
