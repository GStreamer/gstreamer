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

#include "rtsp-media-factory.h"

#define GST_RTSP_MEDIA_FACTORY_GET_PRIVATE(obj)  \
       (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTSP_MEDIA_FACTORY, GstRTSPMediaFactoryPrivate))

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
  GstRTSPAddressPool *pool;

  GMutex medias_lock;
  GHashTable *medias;           /* protected by medias_lock */
};

#define DEFAULT_LAUNCH          NULL
#define DEFAULT_SHARED          FALSE
#define DEFAULT_SUSPEND_MODE    GST_RTSP_SUSPEND_MODE_NONE
#define DEFAULT_EOS_SHUTDOWN    FALSE
#define DEFAULT_PROFILES        GST_RTSP_PROFILE_AVP
#define DEFAULT_PROTOCOLS       GST_RTSP_LOWER_TRANS_UDP | GST_RTSP_LOWER_TRANS_UDP_MCAST | \
                                        GST_RTSP_LOWER_TRANS_TCP
#define DEFAULT_BUFFER_SIZE     0x80000

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

G_DEFINE_TYPE (GstRTSPMediaFactory, gst_rtsp_media_factory, G_TYPE_OBJECT);

static void
gst_rtsp_media_factory_class_init (GstRTSPMediaFactoryClass * klass)
{
  GObjectClass *gobject_class;

  g_type_class_add_private (klass, sizeof (GstRTSPMediaFactoryPrivate));

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
   * which can be accomplished by enclosing the dscription with brackets '('
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

  gst_rtsp_media_factory_signals[SIGNAL_MEDIA_CONSTRUCTED] =
      g_signal_new ("media-constructed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPMediaFactoryClass,
          media_constructed), NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GST_TYPE_RTSP_MEDIA);

  gst_rtsp_media_factory_signals[SIGNAL_MEDIA_CONFIGURE] =
      g_signal_new ("media-configure", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTSPMediaFactoryClass,
          media_configure), NULL, NULL, g_cclosure_marshal_generic,
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
      GST_RTSP_MEDIA_FACTORY_GET_PRIVATE (factory);
  factory->priv = priv;

  priv->launch = g_strdup (DEFAULT_LAUNCH);
  priv->shared = DEFAULT_SHARED;
  priv->suspend_mode = DEFAULT_SUSPEND_MODE;
  priv->eos_shutdown = DEFAULT_EOS_SHUTDOWN;
  priv->profiles = DEFAULT_PROFILES;
  priv->protocols = DEFAULT_PROTOCOLS;
  priv->buffer_size = DEFAULT_BUFFER_SIZE;

  g_mutex_init (&priv->lock);
  g_mutex_init (&priv->medias_lock);
  priv->medias = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);
}

static void
gst_rtsp_media_factory_finalize (GObject * obj)
{
  GstRTSPMediaFactory *factory = GST_RTSP_MEDIA_FACTORY (obj);
  GstRTSPMediaFactoryPrivate *priv = factory->priv;

  if (priv->permissions)
    gst_rtsp_permissions_unref (priv->permissions);
  g_hash_table_unref (priv->medias);
  g_mutex_clear (&priv->medias_lock);
  g_free (priv->launch);
  g_mutex_clear (&priv->lock);
  if (priv->pool)
    g_object_unref (priv->pool);

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
 * @permissions: (transfer none): a #GstRTSPPermissions
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
 * Returns: (transfer full): a #GstRTSPPermissions object, unref after usage.
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
 * gst_rtsp_media_factory_set_launch:
 * @factory: a #GstRTSPMediaFactory
 * @launch: the launch description
 *
 *
 * The gst_parse_launch() line to use for constructing the pipeline in the
 * default prepare vmethod.
 *
 * The pipeline description should return a GstBin as the toplevel element
 * which can be accomplished by enclosing the dscription with brackets '('
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
 * Returns: (transfer full): the configured launch description. g_free() after
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
 * gst_rtsp_media_factory_set_address_pool:
 * @factory: a #GstRTSPMediaFactory
 * @pool: (transfer none): a #GstRTSPAddressPool
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
 * Returns: (transfer full): the #GstRTSPAddressPool of @factory. g_object_unref() after
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

  priv = factory->priv;;

  g_mutex_lock (&priv->medias_lock);
  g_hash_table_foreach_remove (priv->medias, (GHRFunc) compare_media, media);
  g_mutex_unlock (&priv->medias_lock);

  g_object_unref (factory);
}

static GWeakRef *
weak_ref_new (gpointer obj)
{
  GWeakRef *ref = g_slice_new (GWeakRef);

  g_weak_ref_init (ref, obj);
  return ref;
}

static void
weak_ref_free (GWeakRef * ref)
{
  g_weak_ref_clear (ref);
  g_slice_free (GWeakRef, ref);
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
 * Returns: (transfer full): a new #GstRTSPMedia if the media could be prepared.
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

  priv = factory->priv;;
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
    if (media)
      g_object_ref (media);
  } else
    media = NULL;

  if (media == NULL) {
    /* nothing cached found, try to create one */
    if (klass->construct) {
      media = klass->construct (factory, url);
      if (media)
        g_signal_emit (factory,
            gst_rtsp_media_factory_signals[SIGNAL_MEDIA_CONSTRUCTED], 0, media,
            NULL);
    } else
      media = NULL;

    if (media) {
      /* configure the media */
      if (klass->configure)
        klass->configure (factory, media);

      g_signal_emit (factory,
          gst_rtsp_media_factory_signals[SIGNAL_MEDIA_CONFIGURE], 0, media,
          NULL);

      /* check if we can cache this media */
      if (gst_rtsp_media_is_shared (media)) {
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
  }
  g_mutex_unlock (&priv->medias_lock);

  if (key)
    g_free (key);

  GST_INFO ("constructed media %p for url %s", media, url->abspath);

  return media;
}

static gchar *
default_gen_key (GstRTSPMediaFactory * factory, const GstRTSPUrl * url)
{
  gchar *result;
  const gchar *pre_query;
  const gchar *query;

  pre_query = url->query ? "?" : "";
  query = url->query ? url->query : "";

  result =
      g_strdup_printf ("%u%s%s%s", url->port, url->abspath, pre_query, query);

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
  element = gst_parse_launch (priv->launch, &error);
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

  klass = GST_RTSP_MEDIA_FACTORY_GET_CLASS (factory);

  if (!klass->create_pipeline)
    goto no_create;

  element = gst_rtsp_media_factory_create_element (factory, url);
  if (element == NULL)
    goto no_element;

  /* create a new empty media */
  media = gst_rtsp_media_new (element);

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
  gst_rtsp_media_take_pipeline (media, GST_PIPELINE_CAST (pipeline));

  return pipeline;
}

static void
default_configure (GstRTSPMediaFactory * factory, GstRTSPMedia * media)
{
  GstRTSPMediaFactoryPrivate *priv = factory->priv;
  gboolean shared, eos_shutdown;
  guint size;
  GstRTSPSuspendMode suspend_mode;
  GstRTSPProfile profiles;
  GstRTSPLowerTrans protocols;
  GstRTSPAddressPool *pool;
  GstRTSPPermissions *perms;

  /* configure the sharedness */
  GST_RTSP_MEDIA_FACTORY_LOCK (factory);
  suspend_mode = priv->suspend_mode;
  shared = priv->shared;
  eos_shutdown = priv->eos_shutdown;
  size = priv->buffer_size;
  profiles = priv->profiles;
  protocols = priv->protocols;
  GST_RTSP_MEDIA_FACTORY_UNLOCK (factory);

  gst_rtsp_media_set_suspend_mode (media, suspend_mode);
  gst_rtsp_media_set_shared (media, shared);
  gst_rtsp_media_set_eos_shutdown (media, eos_shutdown);
  gst_rtsp_media_set_buffer_size (media, size);
  gst_rtsp_media_set_profiles (media, profiles);
  gst_rtsp_media_set_protocols (media, protocols);

  if ((pool = gst_rtsp_media_factory_get_address_pool (factory))) {
    gst_rtsp_media_set_address_pool (media, pool);
    g_object_unref (pool);
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
 * Returns: (transfer floating): a new #GstElement.
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
