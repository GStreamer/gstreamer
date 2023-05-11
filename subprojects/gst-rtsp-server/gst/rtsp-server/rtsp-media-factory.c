/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
 * Copyright (C) 2015 Centricular Ltd
 *     Author: Sebastian Dröge <sebastian@centricular.com>
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
 * SECTION:rtsp-media-factory
 * @short_description: A factory for media pipelines
 * @see_also: #GstRTSPMountPoints, #GstRTSPMedia
 *
 * The #GstRTSPMediaFactory is responsible for creating or recycling
 * #GstRTSPMedia objects based on the passed URL.
 *
 * The default implementation of the object can create #GstRTSPMedia objects
 * containing a pipeline created from a launch description set with
 * gst_rtsp_media_factory_set_launch().
 *
 * Media from a factory can be shared by setting the shared flag with
 * gst_rtsp_media_factory_set_shared(). When a factory is shared,
 * gst_rtsp_media_factory_construct() will return the same #GstRTSPMedia when
 * the url matches.
 *
 * Last reviewed on 2013-07-11 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rtsp-server-internal.h"
#include "rtsp-media-factory.h"

#define GST_RTSP_MEDIA_FACTORY_GET_LOCK(f)       (&(GST_RTSP_MEDIA_FACTORY_CAST(f)->priv->lock))
#define GST_RTSP_MEDIA_FACTORY_LOCK(f)           (g_mutex_lock(GST_RTSP_MEDIA_FACTORY_GET_LOCK(f)))
#define GST_RTSP_MEDIA_FACTORY_UNLOCK(f)         (g_mutex_unlock(GST_RTSP_MEDIA_FACTORY_GET_LOCK(f)))

struct _GstRTSPMediaFactoryPrivate
{
  GMutex lock;                  /* protects everything but medias */
  GstRTSPPermissions *permissions;
  gchar *launch;
  gboolean shared;
  GstRTSPSuspendMode suspend_mode;
  gboolean eos_shutdown;
  GstRTSPProfile profiles;
  GstRTSPLowerTrans protocols;
  guint buffer_size;
  gint dscp_qos;
  GstRTSPAddressPool *pool;
  GstRTSPTransportMode transport_mode;
  gboolean stop_on_disconnect;
  gchar *multicast_iface;
  guint max_mcast_ttl;
  gboolean bind_mcast_address;
  gboolean enable_rtcp;

  GstClockTime rtx_time;
  guint latency;
  gboolean do_retransmission;

  GMutex medias_lock;
  GHashTable *medias;           /* protected by medias_lock */

  GType media_gtype;

  GstClock *clock;

  GstRTSPPublishClockMode publish_clock_mode;
};

#define DEFAULT_LAUNCH          NULL
#define DEFAULT_SHARED          FALSE
#define DEFAULT_SUSPEND_MODE    GST_RTSP_SUSPEND_MODE_NONE
#define DEFAULT_EOS_SHUTDOWN    FALSE
#define DEFAULT_PROFILES        GST_RTSP_PROFILE_AVP
#define DEFAULT_PROTOCOLS       GST_RTSP_LOWER_TRANS_UDP | GST_RTSP_LOWER_TRANS_UDP_MCAST | \
                                        GST_RTSP_LOWER_TRANS_TCP
#define DEFAULT_BUFFER_SIZE     0x80000
#define DEFAULT_LATENCY         200
#define DEFAULT_MAX_MCAST_TTL   255
#define DEFAULT_BIND_MCAST_ADDRESS FALSE
#define DEFAULT_TRANSPORT_MODE  GST_RTSP_TRANSPORT_MODE_PLAY
#define DEFAULT_STOP_ON_DISCONNECT TRUE
#define DEFAULT_DO_RETRANSMISSION FALSE
#define DEFAULT_DSCP_QOS        (-1)
#define DEFAULT_ENABLE_RTCP     TRUE

enum
{
  PROP_0,
  PROP_LAUNCH,
  PROP_SHARED,
  PROP_SUSPEND_MODE,
  PROP_EOS_SHUTDOWN,
  PROP_PROFILES,
  PROP_PROTOCOLS,
  PROP_BUFFER_SIZE,
  PROP_LATENCY,
  PROP_TRANSPORT_MODE,
  PROP_STOP_ON_DISCONNECT,
  PROP_CLOCK,
  PROP_MAX_MCAST_TTL,
  PROP_BIND_MCAST_ADDRESS,
  PROP_DSCP_QOS,
  PROP_ENABLE_RTCP,
  PROP_LAST
};

enum
{
  SIGNAL_MEDIA_CONSTRUCTED,
  SIGNAL_MEDIA_CONFIGURE,
  SIGNAL_LAST
};

GST_DEBUG_CATEGORY_STATIC (rtsp_media_debug);
#define GST_CAT_DEFAULT rtsp_media_debug

static guint gst_rtsp_media_factory_signals[SIGNAL_LAST] = { 0 };

static void gst_rtsp_media_factory_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec);
static void gst_rtsp_media_factory_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_media_factory_finalize (GObject * obj);

static gchar *default_gen_key (GstRTSPMediaFactory * factory,
    const GstRTSPUrl * url);
static GstElement *default_create_element (GstRTSPMediaFactory * factory,
    const GstRTSPUrl * url);
static GstRTSPMedia *default_construct (GstRTSPMediaFactory * factory,
    const GstRTSPUrl * url);
static void default_configure (GstRTSPMediaFactory * factory,
    GstRTSPMedia * media);
static GstElement *default_create_pipeline (GstRTSPMediaFactory * factory,
    GstRTSPMedia * media);

G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPMediaFactory, gst_rtsp_media_factory,
    G_TYPE_OBJECT);

static void
gst_rtsp_media_factory_class_init (GstRTSPMediaFactoryClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_media_factory_get_property;
  gobject_class->set_property = gst_rtsp_media_factory_set_property;
  gobject_class->finalize = gst_rtsp_media_factory_finalize;

  /**
   * GstRTSPMediaFactory::launch:
   *
   * The gst_parse_launch() line to use for constructing the pipeline in the
   * default prepare vmethod.
   *
   * The pipeline description should return a GstBin as the toplevel element
   * which can be accomplished by enclosing the description with brackets '('
   * ')'.
   *
   * The description should return a pipeline with payloaders named pay0, pay1,
   * etc.. Each of the payloaders will result in a stream.
   *
   * Support for dynamic payloaders can be accomplished by adding payloaders
   * named dynpay0, dynpay1, etc..
   */
  g_object_class_install_property (gobject_class, PROP_LAUNCH,
      g_param_spec_string ("launch", "Launch",
          "A launch description of the pipeline", DEFAULT_LAUNCH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SHARED,
      g_param_spec_boolean ("shared", "Shared",
          "If media from this factory is shared", DEFAULT_SHARED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SUSPEND_MODE,
      g_param_spec_enum ("suspend-mode", "Suspend Mode",
          "Control how media will be suspended", GST_TYPE_RTSP_SUSPEND_MODE,
          DEFAULT_SUSPEND_MODE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_EOS_SHUTDOWN,
      g_param_spec_boolean ("eos-shutdown", "EOS Shutdown",
          "Send EOS down the pipeline before shutting down",
          DEFAULT_EOS_SHUTDOWN, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PROFILES,
      g_param_spec_flags ("profiles", "Profiles",
          "Allowed transfer profiles", GST_TYPE_RTSP_PROFILE,
          DEFAULT_PROFILES, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PROTOCOLS,
      g_param_spec_flags ("protocols", "Protocols",
          "Allowed lower transport protocols", GST_TYPE_RTSP_LOWER_TRANS,
          DEFAULT_PROTOCOLS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BUFFER_SIZE,
      g_param_spec_uint ("buffer-size", "Buffer Size",
          "The kernel UDP buffer size to use", 0, G_MAXUINT,
          DEFAULT_BUFFER_SIZE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_uint ("latency", "Latency",
          "Latency used for receiving media in milliseconds", 0, G_MAXUINT,
          DEFAULT_LATENCY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TRANSPORT_MODE,
      g_param_spec_flags ("transport-mode", "Transport Mode",
          "If media from this factory is for PLAY or RECORD",
          GST_TYPE_RTSP_TRANSPORT_MODE, DEFAULT_TRANSPORT_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STOP_ON_DISCONNECT,
      g_param_spec_boolean ("stop-on-disconnect", "Stop On Disconnect",
          "If media from this factory should be stopped "
          "when a client disconnects without TEARDOWN",
          DEFAULT_STOP_ON_DISCONNECT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CLOCK,
      g_param_spec_object ("clock", "Clock",
          "Clock to be used by the pipelines created for all "
          "medias of this factory", GST_TYPE_CLOCK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_MCAST_TTL,
      g_param_spec_uint ("max-mcast-ttl", "Maximum multicast ttl",
          "The maximum time-to-live value of outgoing multicast packets", 1,
          255, DEFAULT_MAX_MCAST_TTL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BIND_MCAST_ADDRESS,
      g_param_spec_boolean ("bind-mcast-address", "Bind mcast address",
          "Whether the multicast sockets should be bound to multicast addresses "
          "or INADDR_ANY",
          DEFAULT_BIND_MCAST_ADDRESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTSPMediaFactory:enable-rtcp:
   *
   * Whether the created media should send and receive RTCP
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_ENABLE_RTCP,
      g_param_spec_boolean ("enable-rtcp", "Enable RTCP",
          "Whether the created media should send and receive RTCP",
          DEFAULT_ENABLE_RTCP, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DSCP_QOS,
      g_param_spec_int ("dscp-qos", "DSCP QoS",
          "The IP DSCP field to use", -1, 63,
          DEFAULT_DSCP_QOS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_rtsp_media_factory_signals[SIGNAL_MEDIA_CONSTRUCTED] =
      g_signal_new ("media-constructed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPMediaFactoryClass,
          media_constructed), NULL, NULL, NULL,
      G_TYPE_NONE, 1, GST_TYPE_RTSP_MEDIA);

  gst_rtsp_media_factory_signals[SIGNAL_MEDIA_CONFIGURE] =
      g_signal_new ("media-configure", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPMediaFactoryClass,
          media_configure), NULL, NULL, NULL,
      G_TYPE_NONE, 1, GST_TYPE_RTSP_MEDIA);

  klass->gen_key = default_gen_key;
  klass->create_element = default_create_element;
  klass->construct = default_construct;
  klass->configure = default_configure;
  klass->create_pipeline = default_create_pipeline;

  GST_DEBUG_CATEGORY_INIT (rtsp_media_debug, "rtspmediafactory", 0,
      "GstRTSPMediaFactory");
}

static void
gst_rtsp_media_factory_init (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv =
      gst_rtsp_media_factory_get_instance_private (factory);
  factory->priv = priv;

  priv->launch = g_strdup (DEFAULT_LAUNCH);
  priv->shared = DEFAULT_SHARED;
  priv->suspend_mode = DEFAULT_SUSPEND_MODE;
  priv->eos_shutdown = DEFAULT_EOS_SHUTDOWN;
  priv->profiles = DEFAULT_PROFILES;
  priv->protocols = DEFAULT_PROTOCOLS;
  priv->buffer_size = DEFAULT_BUFFER_SIZE;
  priv->latency = DEFAULT_LATENCY;
  priv->transport_mode = DEFAULT_TRANSPORT_MODE;
  priv->stop_on_disconnect = DEFAULT_STOP_ON_DISCONNECT;
  priv->publish_clock_mode = GST_RTSP_PUBLISH_CLOCK_MODE_CLOCK;
  priv->do_retransmission = DEFAULT_DO_RETRANSMISSION;
  priv->max_mcast_ttl = DEFAULT_MAX_MCAST_TTL;
  priv->bind_mcast_address = DEFAULT_BIND_MCAST_ADDRESS;
  priv->enable_rtcp = DEFAULT_ENABLE_RTCP;
  priv->dscp_qos = DEFAULT_DSCP_QOS;

  g_mutex_init (&priv->lock);
  g_mutex_init (&priv->medias_lock);
  priv->medias = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);
  priv->media_gtype = GST_TYPE_RTSP_MEDIA;
}

static void
gst_rtsp_media_factory_finalize (GObject * obj)
{
  GstRTSPMediaFactory *factory = GST_RTSP_MEDIA_FACTORY (obj);
  GstRTSPMediaFactoryPrivate *priv = factory->priv;

  if (priv->clock)
    gst_object_unref (priv->clock);
  if (priv->permissions)
    gst_rtsp_permissions_unref (priv->permissions);
  g_hash_table_unref (priv->medias);
  g_mutex_clear (&priv->medias_lock);
  g_free (priv->launch);
  g_mutex_clear (&priv->lock);
  if (priv->pool)
    g_object_unref (priv->pool);
  g_free (priv->multicast_iface);

  G_OBJECT_CLASS (gst_rtsp_media_factory_parent_class)->finalize (obj);
}

static void
gst_rtsp_media_factory_get_property (GObject * object, guint propid,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPMediaFactory *factory = GST_RTSP_MEDIA_FACTORY (object);

  switch (propid) {
    case PROP_LAUNCH:
      g_value_take_string (value, gst_rtsp_media_factory_get_launch (factory));
      break;
    case PROP_SHARED:
      g_value_set_boolean (value, gst_rtsp_media_factory_is_shared (factory));
      break;
    case PROP_SUSPEND_MODE:
      g_value_set_enum (value,
          gst_rtsp_media_factory_get_suspend_mode (factory));
      break;
    case PROP_EOS_SHUTDOWN:
      g_value_set_boolean (value,
          gst_rtsp_media_factory_is_eos_shutdown (factory));
      break;
    case PROP_PROFILES:
      g_value_set_flags (value, gst_rtsp_media_factory_get_profiles (factory));
      break;
    case PROP_PROTOCOLS:
      g_value_set_flags (value, gst_rtsp_media_factory_get_protocols (factory));
      break;
    case PROP_BUFFER_SIZE:
      g_value_set_uint (value,
          gst_rtsp_media_factory_get_buffer_size (factory));
      break;
    case PROP_LATENCY:
      g_value_set_uint (value, gst_rtsp_media_factory_get_latency (factory));
      break;
    case PROP_TRANSPORT_MODE:
      g_value_set_flags (value,
          gst_rtsp_media_factory_get_transport_mode (factory));
      break;
    case PROP_STOP_ON_DISCONNECT:
      g_value_set_boolean (value,
          gst_rtsp_media_factory_is_stop_on_disonnect (factory));
      break;
    case PROP_CLOCK:
      g_value_take_object (value, gst_rtsp_media_factory_get_clock (factory));
      break;
    case PROP_MAX_MCAST_TTL:
      g_value_set_uint (value,
          gst_rtsp_media_factory_get_max_mcast_ttl (factory));
      break;
    case PROP_BIND_MCAST_ADDRESS:
      g_value_set_boolean (value,
          gst_rtsp_media_factory_is_bind_mcast_address (factory));
      break;
    case PROP_DSCP_QOS:
      g_value_set_int (value, gst_rtsp_media_factory_get_dscp_qos (factory));
      break;
    case PROP_ENABLE_RTCP:
      g_value_set_boolean (value,
          gst_rtsp_media_factory_is_enable_rtcp (factory));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_media_factory_set_property (GObject * object, guint propid,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPMediaFactory *factory = GST_RTSP_MEDIA_FACTORY (object);

  switch (propid) {
    case PROP_LAUNCH:
      gst_rtsp_media_factory_set_launch (factory, g_value_get_string (value));
      break;
    case PROP_SHARED:
      gst_rtsp_media_factory_set_shared (factory, g_value_get_boolean (value));
      break;
    case PROP_SUSPEND_MODE:
      gst_rtsp_media_factory_set_suspend_mode (factory,
          g_value_get_enum (value));
      break;
    case PROP_EOS_SHUTDOWN:
      gst_rtsp_media_factory_set_eos_shutdown (factory,
          g_value_get_boolean (value));
      break;
    case PROP_PROFILES:
      gst_rtsp_media_factory_set_profiles (factory, g_value_get_flags (value));
      break;
    case PROP_PROTOCOLS:
      gst_rtsp_media_factory_set_protocols (factory, g_value_get_flags (value));
      break;
    case PROP_BUFFER_SIZE:
      gst_rtsp_media_factory_set_buffer_size (factory,
          g_value_get_uint (value));
      break;
    case PROP_LATENCY:
      gst_rtsp_media_factory_set_latency (factory, g_value_get_uint (value));
      break;
    case PROP_TRANSPORT_MODE:
      gst_rtsp_media_factory_set_transport_mode (factory,
          g_value_get_flags (value));
      break;
    case PROP_STOP_ON_DISCONNECT:
      gst_rtsp_media_factory_set_stop_on_disconnect (factory,
          g_value_get_boolean (value));
      break;
    case PROP_CLOCK:
      gst_rtsp_media_factory_set_clock (factory, g_value_get_object (value));
      break;
    case PROP_MAX_MCAST_TTL:
      gst_rtsp_media_factory_set_max_mcast_ttl (factory,
          g_value_get_uint (value));
      break;
    case PROP_BIND_MCAST_ADDRESS:
      gst_rtsp_media_factory_set_bind_mcast_address (factory,
          g_value_get_boolean (value));
      break;
    case PROP_DSCP_QOS:
      gst_rtsp_media_factory_set_dscp_qos (factory, g_value_get_int (value));
      break;
    case PROP_ENABLE_RTCP:
      gst_rtsp_media_factory_set_enable_rtcp (factory,
          g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_rtsp_media_factory_new:
 *
 * Create a new #GstRTSPMediaFactory instance.
 *
 * Returns: (transfer full): a new #GstRTSPMediaFactory object.
 */
GstRTSPMediaFactory *
gst_rtsp_media_factory_new (void)
{
  GstRTSPMediaFactory *result;

  result = g_object_new (GST_TYPE_RTSP_MEDIA_FACTORY, NULL);

  return result;
}

/**
 * gst_rtsp_media_factory_set_permissions:
 * @factory: a #GstRTSPMediaFactory
 * @permissions: (transfer none) (nullable): a #GstRTSPPermissions
 *
 * Set @permissions on @factory.
 */
void
gst_rtsp_media_factory_set_permissions (GstRTSPMediaFactory * factory,
    GstRTSPPermissions * permissions)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  if (priv->permissions)
    gst_rtsp_permissions_unref (priv->permissions);
  if ((priv->permissions = permissions))
    gst_rtsp_permissions_ref (permissions);
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_get_permissions:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get the permissions object from @factory.
 *
 * Returns: (transfer full) (nullable): a #GstRTSPPermissions object, unref after usage.
 */
GstRTSPPermissions *
gst_rtsp_media_factory_get_permissions (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  GstRTSPPermissions *result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), NULL);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  if ((result = priv->permissions))
    gst_rtsp_permissions_ref (result);
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return result;
}

/**
 * gst_rtsp_media_factory_add_role:
 * @factory: a #GstRTSPMediaFactory
 * @role: a role
 * @fieldname: the first field name
 * @...: additional arguments
 *
 * A convenience method to add @role with @fieldname and additional arguments to
 * the permissions of @factory. If @factory had no permissions, new permissions
 * will be created and the role will be added to it.
 */
void
gst_rtsp_media_factory_add_role (GstRTSPMediaFactory * factory,
    const gchar * role, const gchar * fieldname, ...)
{
  GstRTSPMediaFactoryPrivate *priv;
  va_list var_args;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));
  g_return_if_fail (role != NULL);
  g_return_if_fail (fieldname != NULL);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  if (priv->permissions == NULL)
    priv->permissions = gst_rtsp_permissions_new ();

  va_start (var_args, fieldname);
  gst_rtsp_permissions_add_role_valist (priv->permissions, role, fieldname,
      var_args);
  va_end (var_args);
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_add_role_from_structure:
 *
 * A convenience wrapper around gst_rtsp_permissions_add_role_from_structure().
 * If @factory had no permissions, new permissions will be created and the
 * role will be added to it.
 *
 * Since: 1.14
 */
void
gst_rtsp_media_factory_add_role_from_structure (GstRTSPMediaFactory * factory,
    GstStructure * structure)
{
  GstRTSPMediaFactoryPrivate *priv;
  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));
  g_return_if_fail (GST_IS_STRUCTURE (structure));

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  if (priv->permissions == NULL)
    priv->permissions = gst_rtsp_permissions_new ();

  gst_rtsp_permissions_add_role_from_structure (priv->permissions, structure);
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_set_launch:
 * @factory: a #GstRTSPMediaFactory
 * @launch: the launch description
 *
 *
 * The gst_parse_launch() line to use for constructing the pipeline in the
 * default prepare vmethod.
 *
 * The pipeline description should return a GstBin as the toplevel element
 * which can be accomplished by enclosing the description with brackets '('
 * ')'.
 *
 * The description should return a pipeline with payloaders named pay0, pay1,
 * etc.. Each of the payloaders will result in a stream.
 */
void
gst_rtsp_media_factory_set_launch (GstRTSPMediaFactory * factory,
    const gchar * launch)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));
  g_return_if_fail (launch != NULL);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  g_free (priv->launch);
  priv->launch = g_strdup (launch);
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_get_launch:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get the gst_parse_launch() pipeline description that will be used in the
 * default prepare vmethod.
 *
 * Returns: (transfer full) (nullable): the configured launch description. g_free() after
 * usage.
 */
gchar *
gst_rtsp_media_factory_get_launch (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), NULL);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  result = g_strdup (priv->launch);
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return result;
}

/**
 * gst_rtsp_media_factory_set_suspend_mode:
 * @factory: a #GstRTSPMediaFactory
 * @mode: the new #GstRTSPSuspendMode
 *
 * Configure how media created from this factory will be suspended.
 */
void
gst_rtsp_media_factory_set_suspend_mode (GstRTSPMediaFactory * factory,
    GstRTSPSuspendMode mode)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv->suspend_mode = mode;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_get_suspend_mode:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get how media created from this factory will be suspended.
 *
 * Returns: a #GstRTSPSuspendMode.
 */
GstRTSPSuspendMode
gst_rtsp_media_factory_get_suspend_mode (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  GstRTSPSuspendMode result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory),
      GST_RTSP_SUSPEND_MODE_NONE);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  result = priv->suspend_mode;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return result;
}

/**
 * gst_rtsp_media_factory_set_shared:
 * @factory: a #GstRTSPMediaFactory
 * @shared: the new value
 *
 * Configure if media created from this factory can be shared between clients.
 */
void
gst_rtsp_media_factory_set_shared (GstRTSPMediaFactory * factory,
    gboolean shared)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv->shared = shared;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_is_shared:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get if media created from this factory can be shared between clients.
 *
 * Returns: %TRUE if the media will be shared between clients.
 */
gboolean
gst_rtsp_media_factory_is_shared (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  gboolean result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), FALSE);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  result = priv->shared;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return result;
}

/**
 * gst_rtsp_media_factory_set_eos_shutdown:
 * @factory: a #GstRTSPMediaFactory
 * @eos_shutdown: the new value
 *
 * Configure if media created from this factory will have an EOS sent to the
 * pipeline before shutdown.
 */
void
gst_rtsp_media_factory_set_eos_shutdown (GstRTSPMediaFactory * factory,
    gboolean eos_shutdown)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv->eos_shutdown = eos_shutdown;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_is_eos_shutdown:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get if media created from this factory will have an EOS event sent to the
 * pipeline before shutdown.
 *
 * Returns: %TRUE if the media will receive EOS before shutdown.
 */
gboolean
gst_rtsp_media_factory_is_eos_shutdown (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  gboolean result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), FALSE);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  result = priv->eos_shutdown;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return result;
}

/**
 * gst_rtsp_media_factory_set_buffer_size:
 * @factory: a #GstRTSPMedia
 * @size: the new value
 *
 * Set the kernel UDP buffer size.
 */
void
gst_rtsp_media_factory_set_buffer_size (GstRTSPMediaFactory * factory,
    guint size)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv->buffer_size = size;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_get_buffer_size:
 * @factory: a #GstRTSPMedia
 *
 * Get the kernel UDP buffer size.
 *
 * Returns: the kernel UDP buffer size.
 */
guint
gst_rtsp_media_factory_get_buffer_size (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  guint result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), 0);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  result = priv->buffer_size;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return result;
}

/**
 * gst_rtsp_media_factory_set_dscp_qos:
 * @factory: a #GstRTSPMediaFactory
 * @dscp_qos: a new dscp qos value (0-63, or -1 to disable)
 *
 * Configure the media dscp qos to @dscp_qos.
 *
 * Since: 1.18
 */
void
gst_rtsp_media_factory_set_dscp_qos (GstRTSPMediaFactory * factory,
    gint dscp_qos)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  if (dscp_qos < -1 || dscp_qos > 63) {
    GST_WARNING_OBJECT (factory, "trying to set illegal dscp qos %d", dscp_qos);
    return;
  }

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv->dscp_qos = dscp_qos;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_get_dscp_qos:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get the configured media DSCP QoS.
 *
 * Returns: the media DSCP QoS value or -1 if disabled.
 *
 * Since: 1.18
 */
gint
gst_rtsp_media_factory_get_dscp_qos (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  guint result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), 0);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  result = priv->dscp_qos;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return result;
}

/**
 * gst_rtsp_media_factory_set_address_pool:
 * @factory: a #GstRTSPMediaFactory
 * @pool: (transfer none) (nullable): a #GstRTSPAddressPool
 *
 * configure @pool to be used as the address pool of @factory.
 */
void
gst_rtsp_media_factory_set_address_pool (GstRTSPMediaFactory * factory,
    GstRTSPAddressPool * pool)
{
  GstRTSPMediaFactoryPrivate *priv;
  GstRTSPAddressPool *old;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  if ((old = priv->pool) != pool)
    priv->pool = pool ? g_object_ref (pool) : NULL;
  else
    old = NULL;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  if (old)
    g_object_unref (old);
}

/**
 * gst_rtsp_media_factory_get_address_pool:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get the #GstRTSPAddressPool used as the address pool of @factory.
 *
 * Returns: (transfer full) (nullable): the #GstRTSPAddressPool of @factory. g_object_unref() after
 * usage.
 */
GstRTSPAddressPool *
gst_rtsp_media_factory_get_address_pool (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  GstRTSPAddressPool *result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), NULL);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  if ((result = priv->pool))
    g_object_ref (result);
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return result;
}

/**
 * gst_rtsp_media_factory_set_multicast_iface:
 * @factory: a #GstRTSPMediaFactory
 * @multicast_iface: (transfer none) (nullable): a multicast interface name
 *
 * configure @multicast_iface to be used for @factory.
 */
void
gst_rtsp_media_factory_set_multicast_iface (GstRTSPMediaFactory * media_factory,
    const gchar * multicast_iface)
{
  GstRTSPMediaFactoryPrivate *priv;
  gchar *old;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (media_factory));

  priv = media_factory->priv;

  GST_LOG_OBJECT (media_factory, "set multicast interface %s", multicast_iface);

  g_mutex_lock (&priv->lock);
  if ((old = priv->multicast_iface) != multicast_iface)
    priv->multicast_iface = multicast_iface ? g_strdup (multicast_iface) : NULL;
  else
    old = NULL;
  g_mutex_unlock (&priv->lock);

  if (old)
    g_free (old);
}

/**
 * gst_rtsp_media_factory_get_multicast_iface:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get the multicast interface used for @factory.
 *
 * Returns: (transfer full) (nullable): the multicast interface for @factory. g_free() after
 * usage.
 */
gchar *
gst_rtsp_media_factory_get_multicast_iface (GstRTSPMediaFactory * media_factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (media_factory), NULL);

  priv = media_factory->priv;

  g_mutex_lock (&priv->lock);
  if ((result = priv->multicast_iface))
    result = g_strdup (result);
  g_mutex_unlock (&priv->lock);

  return result;
}

/**
 * gst_rtsp_media_factory_set_profiles:
 * @factory: a #GstRTSPMediaFactory
 * @profiles: the new flags
 *
 * Configure the allowed profiles for @factory.
 */
void
gst_rtsp_media_factory_set_profiles (GstRTSPMediaFactory * factory,
    GstRTSPProfile profiles)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_DEBUG_OBJECT (factory, "profiles %d", profiles);

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv->profiles = profiles;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_get_profiles:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get the allowed profiles of @factory.
 *
 * Returns: a #GstRTSPProfile
 */
GstRTSPProfile
gst_rtsp_media_factory_get_profiles (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  GstRTSPProfile res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory),
      GST_RTSP_PROFILE_UNKNOWN);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  res = priv->profiles;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return res;
}

/**
 * gst_rtsp_media_factory_set_protocols:
 * @factory: a #GstRTSPMediaFactory
 * @protocols: the new flags
 *
 * Configure the allowed lower transport for @factory.
 */
void
gst_rtsp_media_factory_set_protocols (GstRTSPMediaFactory * factory,
    GstRTSPLowerTrans protocols)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_DEBUG_OBJECT (factory, "protocols %d", protocols);

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv->protocols = protocols;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_get_protocols:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get the allowed protocols of @factory.
 *
 * Returns: a #GstRTSPLowerTrans
 */
GstRTSPLowerTrans
gst_rtsp_media_factory_get_protocols (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  GstRTSPLowerTrans res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory),
      GST_RTSP_LOWER_TRANS_UNKNOWN);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  res = priv->protocols;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return res;
}

/**
 * gst_rtsp_media_factory_set_stop_on_disconnect:
 * @factory: a #GstRTSPMediaFactory
 * @stop_on_disconnect: the new value
 *
 * Configure if media created from this factory should be stopped
 * when a client disconnects without sending TEARDOWN.
 */
void
gst_rtsp_media_factory_set_stop_on_disconnect (GstRTSPMediaFactory * factory,
    gboolean stop_on_disconnect)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv->stop_on_disconnect = stop_on_disconnect;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_is_stop_on_disconnect:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get if media created from this factory should be stopped when a client
 * disconnects without sending TEARDOWN.
 *
 * Returns: %TRUE if the media will be stopped when a client disconnects
 *     without sending TEARDOWN.
 */
gboolean
gst_rtsp_media_factory_is_stop_on_disonnect (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  gboolean result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), TRUE);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  result = priv->stop_on_disconnect;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return result;
}

/**
 * gst_rtsp_media_factory_set_retransmission_time:
 * @factory: a #GstRTSPMediaFactory
 * @time: a #GstClockTime
 *
 * Configure the time to store for possible retransmission
 */
void
gst_rtsp_media_factory_set_retransmission_time (GstRTSPMediaFactory * factory,
    GstClockTime time)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_DEBUG_OBJECT (factory, "retransmission time %" G_GUINT64_FORMAT, time);

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv->rtx_time = time;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_get_retransmission_time:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get the time that is stored for retransmission purposes
 *
 * Returns: a #GstClockTime
 */
GstClockTime
gst_rtsp_media_factory_get_retransmission_time (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  GstClockTime res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), 0);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  res = priv->rtx_time;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return res;
}

/**
 * gst_rtsp_media_factory_set_do_retransmission:
 *
 * Set whether retransmission requests will be sent for
 * receiving media
 *
 * Since: 1.16
 */
void
gst_rtsp_media_factory_set_do_retransmission (GstRTSPMediaFactory * factory,
    gboolean do_retransmission)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_DEBUG_OBJECT (factory, "Do retransmission %d", do_retransmission);

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv->do_retransmission = do_retransmission;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_get_do_retransmission:
 *
 * Returns: Whether retransmission requests will be sent for receiving media
 *
 * Since: 1.16
 */
gboolean
gst_rtsp_media_factory_get_do_retransmission (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  gboolean res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), 0);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  res = priv->do_retransmission;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return res;
}

/**
 * gst_rtsp_media_factory_set_latency:
 * @factory: a #GstRTSPMediaFactory
 * @latency: latency in milliseconds
 *
 * Configure the latency used for receiving media
 */
void
gst_rtsp_media_factory_set_latency (GstRTSPMediaFactory * factory,
    guint latency)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_DEBUG_OBJECT (factory, "latency %ums", latency);

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv->latency = latency;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_get_latency:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get the latency that is used for receiving media
 *
 * Returns: latency in milliseconds
 */
guint
gst_rtsp_media_factory_get_latency (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  guint res;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), 0);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  res = priv->latency;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return res;
}

static gboolean
compare_media (gpointer key, GstRTSPMedia * media1, GstRTSPMedia * media2)
{
  return (media1 == media2);
}

static void
media_unprepared (GstRTSPMedia * media, GWeakRef * ref)
{
  GstRTSPMediaFactory *factory = g_weak_ref_get (ref);
  GstRTSPMediaFactoryPrivate *priv;

  if (!factory)
    return;

  priv = factory->priv;

  g_mutex_lock (&priv->medias_lock);
  g_hash_table_foreach_remove (priv->medias, (GHRFunc) compare_media, media);
  g_mutex_unlock (&priv->medias_lock);

  g_object_unref (factory);
}

static GWeakRef *
weak_ref_new (gpointer obj)
{
  GWeakRef *ref = g_new (GWeakRef, 1);

  g_weak_ref_init (ref, obj);
  return ref;
}

static void
weak_ref_free (GWeakRef * ref)
{
  g_weak_ref_clear (ref);
  g_free (ref);
}

/**
 * gst_rtsp_media_factory_construct:
 * @factory: a #GstRTSPMediaFactory
 * @url: the url used
 *
 * Construct the media object and create its streams. Implementations
 * should create the needed gstreamer elements and add them to the result
 * object. No state changes should be performed on them yet.
 *
 * One or more GstRTSPStream objects should be created from the result
 * with gst_rtsp_media_create_stream ().
 *
 * After the media is constructed, it can be configured and then prepared
 * with gst_rtsp_media_prepare ().
 *
 * The returned media will be locked and must be unlocked afterwards.
 *
 * Returns: (transfer full) (nullable): a new #GstRTSPMedia if the media could be prepared.
 */
GstRTSPMedia *
gst_rtsp_media_factory_construct (GstRTSPMediaFactory * factory,
    const GstRTSPUrl * url)
{
  GstRTSPMediaFactoryPrivate *priv;
  gchar *key;
  GstRTSPMedia *media;
  GstRTSPMediaFactoryClass *klass;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), NULL);
  g_return_val_if_fail (url != NULL, NULL);

  priv = factory->priv;
  klass = GST_RTSP_MEDIA_FACTORY_GET_CLASS (factory);

  /* convert the url to a key for the hashtable. NULL return or a NULL function
   * will not cache anything for this factory. */
  if (klass->gen_key)
    key = klass->gen_key (factory, url);
  else
    key = NULL;

  g_mutex_lock (&priv->medias_lock);
  if (key) {
    /* we have a key, see if we find a cached media */
    media = g_hash_table_lookup (priv->medias, key);
    if (media) {
      g_object_ref (media);
      g_mutex_unlock (&priv->medias_lock);

      /* now need to check if the media is curently in the process of being
       * unprepared. That always happens while the lock is taken, so take it
       * here now and then check if we can really use the media */
      gst_rtsp_media_lock (media);
      if (!gst_rtsp_media_can_be_shared (media)) {
        gst_rtsp_media_unlock (media);
        g_object_unref (media);
        media = NULL;
      }

      if (media) {
        if (key)
          g_free (key);

        GST_INFO ("reusing cached media %p for url %s", media, url->abspath);

        return media;
      }

      g_mutex_lock (&priv->medias_lock);
    }
  }

  /* nothing cached found, try to create one */
  if (klass->construct) {
    media = klass->construct (factory, url);
    if (media)
      g_signal_emit (factory,
          gst_rtsp_media_factory_signals[SIGNAL_MEDIA_CONSTRUCTED], 0, media,
          NULL);
  }

  if (media) {
    gst_rtsp_media_lock (media);

    /* configure the media */
    if (klass->configure)
      klass->configure (factory, media);

    g_signal_emit (factory,
        gst_rtsp_media_factory_signals[SIGNAL_MEDIA_CONFIGURE], 0, media, NULL);

    /* check if we can cache this media */
    if (gst_rtsp_media_is_shared (media) && key) {
      /* insert in the hashtable, takes ownership of the key */
      g_object_ref (media);
      g_hash_table_insert (priv->medias, key, media);
      key = NULL;
    }
    if (!gst_rtsp_media_is_reusable (media)) {
      /* when not reusable, connect to the unprepare signal to remove the item
       * from our cache when it gets unprepared */
      g_signal_connect_data (media, "unprepared",
          (GCallback) media_unprepared, weak_ref_new (factory),
          (GClosureNotify) weak_ref_free, 0);
    }
  }
  g_mutex_unlock (&priv->medias_lock);

  if (key)
    g_free (key);

  GST_INFO ("constructed media %p for url %s", media, url->abspath);

  return media;
}

/**
 * gst_rtsp_media_factory_set_media_gtype:
 * @factory: a #GstRTSPMediaFactory
 * @media_gtype: the GType of the class to create
 *
 * Configure the GType of the GstRTSPMedia subclass to
 * create (by default, overridden construct vmethods
 * may of course do something different)
 *
 * Since: 1.6
 */
void
gst_rtsp_media_factory_set_media_gtype (GstRTSPMediaFactory * factory,
    GType media_gtype)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (g_type_is_a (media_gtype, GST_TYPE_RTSP_MEDIA));

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv = factory->priv;
  priv->media_gtype = media_gtype;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_get_media_gtype:
 * @factory: a #GstRTSPMediaFactory
 *
 * Return the GType of the GstRTSPMedia subclass this
 * factory will create.
 *
 * Since: 1.6
 */
GType
gst_rtsp_media_factory_get_media_gtype (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  GType ret;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv = factory->priv;
  ret = priv->media_gtype;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return ret;
}

/**
 * gst_rtsp_media_factory_set_clock:
 * @factory: a #GstRTSPMediaFactory
 * @clock: (nullable): the clock to be used by the media factory
 *
 * Configures a specific clock to be used by the pipelines
 * of all medias created from this factory.
 *
 * Since: 1.8
 */
void
gst_rtsp_media_factory_set_clock (GstRTSPMediaFactory * factory,
    GstClock * clock)
{
  GstClock **clock_p;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));
  g_return_if_fail (GST_IS_CLOCK (clock) || clock == NULL);

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  clock_p = &factory->priv->clock;
  gst_object_replace ((GstObject **) clock_p, (GstObject *) clock);
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_get_clock:
 * @factory: a #GstRTSPMediaFactory
 *
 * Returns the clock that is going to be used by the pipelines
 * of all medias created from this factory.
 *
 * Returns: (transfer full) (nullable): The GstClock
 *
 * Since: 1.8
 */
GstClock *
gst_rtsp_media_factory_get_clock (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  GstClock *ret;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), NULL);

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv = factory->priv;
  ret = priv->clock ? gst_object_ref (priv->clock) : NULL;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return ret;
}

/**
 * gst_rtsp_media_factory_set_publish_clock_mode:
 * @factory: a #GstRTSPMediaFactory
 * @mode: the clock publish mode
 *
 * Sets if and how the media clock should be published according to RFC7273.
 *
 * Since: 1.8
 */
void
gst_rtsp_media_factory_set_publish_clock_mode (GstRTSPMediaFactory * factory,
    GstRTSPPublishClockMode mode)
{
  GstRTSPMediaFactoryPrivate *priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv = factory->priv;
  priv->publish_clock_mode = mode;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_get_publish_clock_mode:
 * @factory: a #GstRTSPMediaFactory
 *
 * Gets if and how the media clock should be published according to RFC7273.
 *
 * Returns: The GstRTSPPublishClockMode
 *
 * Since: 1.8
 */
GstRTSPPublishClockMode
gst_rtsp_media_factory_get_publish_clock_mode (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  GstRTSPPublishClockMode ret;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv = factory->priv;
  ret = priv->publish_clock_mode;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return ret;
}

/**
 * gst_rtsp_media_factory_set_max_mcast_ttl:
 * @factory: a #GstRTSPMedia
 * @ttl: the new multicast ttl value
 *
 * Set the maximum time-to-live value of outgoing multicast packets.
 *
 * Returns: %TRUE if the requested ttl has been set successfully.
 *
 * Since: 1.16
 */
gboolean
gst_rtsp_media_factory_set_max_mcast_ttl (GstRTSPMediaFactory * factory,
    guint ttl)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), FALSE);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  if (ttl == 0 || ttl > DEFAULT_MAX_MCAST_TTL) {
    GST_WARNING_OBJECT (factory, "The requested mcast TTL value is not valid.");
    GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
    return FALSE;
  }
  priv->max_mcast_ttl = ttl;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return TRUE;
}

/**
 * gst_rtsp_media_factory_get_max_mcast_ttl:
 * @factory: a #GstRTSPMedia
 *
 * Get the the maximum time-to-live value of outgoing multicast packets.
 *
 * Returns: the maximum time-to-live value of outgoing multicast packets.
 *
 * Since: 1.16
 */
guint
gst_rtsp_media_factory_get_max_mcast_ttl (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  guint result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), 0);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  result = priv->max_mcast_ttl;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return result;
}

/**
 * gst_rtsp_media_factory_set_bind_mcast_address:
 * @factory: a #GstRTSPMediaFactory
 * @bind_mcast_addr: the new value
 *
 * Decide whether the multicast socket should be bound to a multicast address or
 * INADDR_ANY.
 *
 * Since: 1.16
 */
void
gst_rtsp_media_factory_set_bind_mcast_address (GstRTSPMediaFactory * factory,
    gboolean bind_mcast_addr)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv->bind_mcast_address = bind_mcast_addr;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_is_bind_mcast_address:
 * @factory: a #GstRTSPMediaFactory
 *
 * Check if multicast sockets are configured to be bound to multicast addresses.
 *
 * Returns: %TRUE if multicast sockets are configured to be bound to multicast addresses.
 *
 * Since: 1.16
 */
gboolean
gst_rtsp_media_factory_is_bind_mcast_address (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  gboolean result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), FALSE);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  result = priv->bind_mcast_address;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return result;
}

/**
 * gst_rtsp_media_factory_set_enable_rtcp:
 * @factory: a #GstRTSPMediaFactory
 * @enable: the new value
 *
 * Decide whether the created media should send and receive RTCP
 *
 * Since: 1.20
 */
void
gst_rtsp_media_factory_set_enable_rtcp (GstRTSPMediaFactory * factory,
    gboolean enable)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv->enable_rtcp = enable;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_is_enable_rtcp:
 * @factory: a #GstRTSPMediaFactory
 *
 * Check if created media will send and receive RTCP
 *
 * Returns: %TRUE if created media will send and receive RTCP
 *
 * Since: 1.20
 */
gboolean
gst_rtsp_media_factory_is_enable_rtcp (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  gboolean result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), FALSE);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  result = priv->enable_rtcp;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return result;
}

static gchar *
default_gen_key (GstRTSPMediaFactory * factory, const GstRTSPUrl * url)
{
  gchar *result;
  const gchar *pre_query;
  const gchar *query;
  guint16 port;

  pre_query = url->query ? "?" : "";
  query = url->query ? url->query : "";

  gst_rtsp_url_get_port (url, &port);

  result = g_strdup_printf ("%u%s%s%s", port, url->abspath, pre_query, query);

  return result;
}

static GstElement *
default_create_element (GstRTSPMediaFactory * factory, const GstRTSPUrl * url)
{
  GstRTSPMediaFactoryPrivate *priv = factory->priv;
  GstElement *element;
  GError *error = NULL;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  /* we need a parse syntax */
  if (priv->launch == NULL)
    goto no_launch;

  /* parse the user provided launch line */
  element =
      gst_parse_launch_full (priv->launch, NULL, GST_PARSE_FLAG_PLACE_IN_BIN,
      &error);
  if (element == NULL)
    goto parse_error;

  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  if (error != NULL) {
    /* a recoverable error was encountered */
    GST_WARNING ("recoverable parsing error: %s", error->message);
    g_error_free (error);
  }
  return element;

  /* ERRORS */
no_launch:
  {
    GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
    g_critical ("no launch line specified");
    return NULL;
  }
parse_error:
  {
    g_critical ("could not parse launch syntax (%s): %s", priv->launch,
        (error ? error->message : "unknown reason"));
    GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
    if (error)
      g_error_free (error);
    return NULL;
  }
}

static GstRTSPMedia *
default_construct (GstRTSPMediaFactory * factory, const GstRTSPUrl * url)
{
  GstRTSPMedia *media;
  GstElement *element, *pipeline;
  GstRTSPMediaFactoryClass *klass;
  GType media_gtype;
  gboolean enable_rtcp;

  klass = GST_RTSP_MEDIA_FACTORY_GET_CLASS (factory);

  if (!klass->create_pipeline)
    goto no_create;

  element = gst_rtsp_media_factory_create_element (factory, url);
  if (element == NULL)
    goto no_element;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  media_gtype = factory->priv->media_gtype;
  enable_rtcp = factory->priv->enable_rtcp;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  /* create a new empty media */
  media =
      g_object_new (media_gtype, "element", element, "transport-mode",
      factory->priv->transport_mode, NULL);

  /* We need to call this prior to collecting streams */
  gst_rtsp_media_set_enable_rtcp (media, enable_rtcp);

  gst_rtsp_media_collect_streams (media);

  pipeline = klass->create_pipeline (factory, media);
  if (pipeline == NULL)
    goto no_pipeline;

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
default_create_pipeline (GstRTSPMediaFactory * factory, GstRTSPMedia * media)
{
  GstElement *pipeline;

  pipeline = gst_pipeline_new ("media-pipeline");

  /* FIXME 2.0: This should be done by the caller, not the vfunc. Every
   * implementation of the vfunc has to call it otherwise at the end.
   * Also it does not allow use to add further behaviour here that could
   * be reused by subclasses that chain up */
  gst_rtsp_media_take_pipeline (media, GST_PIPELINE_CAST (pipeline));

  return pipeline;
}

static void
default_configure (GstRTSPMediaFactory * factory, GstRTSPMedia * media)
{
  GstRTSPMediaFactoryPrivate *priv = factory->priv;
  gboolean shared, eos_shutdown, stop_on_disconnect;
  guint size;
  gint dscp_qos;
  GstRTSPSuspendMode suspend_mode;
  GstRTSPProfile profiles;
  GstRTSPLowerTrans protocols;
  GstRTSPAddressPool *pool;
  GstRTSPPermissions *perms;
  GstClockTime rtx_time;
  guint latency;
  GstRTSPTransportMode transport_mode;
  GstClock *clock;
  gchar *multicast_iface;
  GstRTSPPublishClockMode publish_clock_mode;
  guint ttl;
  gboolean bind_mcast;

  /* configure the sharedness */
  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  suspend_mode = priv->suspend_mode;
  shared = priv->shared;
  eos_shutdown = priv->eos_shutdown;
  size = priv->buffer_size;
  dscp_qos = priv->dscp_qos;
  profiles = priv->profiles;
  protocols = priv->protocols;
  rtx_time = priv->rtx_time;
  latency = priv->latency;
  transport_mode = priv->transport_mode;
  stop_on_disconnect = priv->stop_on_disconnect;
  clock = priv->clock ? gst_object_ref (priv->clock) : NULL;
  publish_clock_mode = priv->publish_clock_mode;
  ttl = priv->max_mcast_ttl;
  bind_mcast = priv->bind_mcast_address;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  gst_rtsp_media_set_suspend_mode (media, suspend_mode);
  gst_rtsp_media_set_shared (media, shared);
  gst_rtsp_media_set_eos_shutdown (media, eos_shutdown);
  gst_rtsp_media_set_buffer_size (media, size);
  gst_rtsp_media_set_dscp_qos (media, dscp_qos);
  gst_rtsp_media_set_profiles (media, profiles);
  gst_rtsp_media_set_protocols (media, protocols);
  gst_rtsp_media_set_retransmission_time (media, rtx_time);
  gst_rtsp_media_set_do_retransmission (media, priv->do_retransmission);
  gst_rtsp_media_set_latency (media, latency);
  gst_rtsp_media_set_transport_mode (media, transport_mode);
  gst_rtsp_media_set_stop_on_disconnect (media, stop_on_disconnect);
  gst_rtsp_media_set_publish_clock_mode (media, publish_clock_mode);
  gst_rtsp_media_set_max_mcast_ttl (media, ttl);
  gst_rtsp_media_set_bind_mcast_address (media, bind_mcast);

  if (clock) {
    gst_rtsp_media_set_clock (media, clock);
    gst_object_unref (clock);
  }

  if ((pool = gst_rtsp_media_factory_get_address_pool (factory))) {
    gst_rtsp_media_set_address_pool (media, pool);
    g_object_unref (pool);
  }
  if ((multicast_iface = gst_rtsp_media_factory_get_multicast_iface (factory))) {
    gst_rtsp_media_set_multicast_iface (media, multicast_iface);
    g_free (multicast_iface);
  }
  if ((perms = gst_rtsp_media_factory_get_permissions (factory))) {
    gst_rtsp_media_set_permissions (media, perms);
    gst_rtsp_permissions_unref (perms);
  }
}

/**
 * gst_rtsp_media_factory_create_element:
 * @factory: a #GstRTSPMediaFactory
 * @url: the url used
 *
 * Construct and return a #GstElement that is a #GstBin containing
 * the elements to use for streaming the media.
 *
 * The bin should contain payloaders pay\%d for each stream. The default
 * implementation of this function returns the bin created from the
 * launch parameter.
 *
 * Returns: (transfer floating) (nullable): a new #GstElement.
 */
GstElement *
gst_rtsp_media_factory_create_element (GstRTSPMediaFactory * factory,
    const GstRTSPUrl * url)
{
  GstRTSPMediaFactoryClass *klass;
  GstElement *result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), NULL);
  g_return_val_if_fail (url != NULL, NULL);

  klass = GST_RTSP_MEDIA_FACTORY_GET_CLASS (factory);

  if (klass->create_element)
    result = klass->create_element (factory, url);
  else
    result = NULL;

  return result;
}

/**
 * gst_rtsp_media_factory_set_transport_mode:
 * @factory: a #GstRTSPMediaFactory
 * @mode: the new value
 *
 * Configure if this factory creates media for PLAY or RECORD modes.
 */
void
gst_rtsp_media_factory_set_transport_mode (GstRTSPMediaFactory * factory,
    GstRTSPTransportMode mode)
{
  GstRTSPMediaFactoryPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  priv->transport_mode = mode;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);
}

/**
 * gst_rtsp_media_factory_get_transport_mode:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get if media created from this factory can be used for PLAY or RECORD
 * methods.
 *
 * Returns: The transport mode.
 */
GstRTSPTransportMode
gst_rtsp_media_factory_get_transport_mode (GstRTSPMediaFactory * factory)
{
  GstRTSPMediaFactoryPrivate *priv;
  GstRTSPTransportMode result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), FALSE);

  priv = factory->priv;

  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  result = priv->transport_mode;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  return result;
}
