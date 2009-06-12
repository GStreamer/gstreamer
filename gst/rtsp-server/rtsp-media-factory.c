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

#include "rtsp-media-factory.h"

#define DEFAULT_LAUNCH         NULL
#define DEFAULT_SHARED         FALSE

enum
{
  PROP_0,
  PROP_LAUNCH,
  PROP_SHARED,
  PROP_LAST
};

static void gst_rtsp_media_factory_get_property (GObject *object, guint propid,
    GValue *value, GParamSpec *pspec);
static void gst_rtsp_media_factory_set_property (GObject *object, guint propid,
    const GValue *value, GParamSpec *pspec);
static void gst_rtsp_media_factory_finalize (GObject * obj);

static gchar * default_gen_key (GstRTSPMediaFactory *factory, const GstRTSPUrl *url);
static GstElement * default_get_element (GstRTSPMediaFactory *factory, const GstRTSPUrl *url);
static GstRTSPMedia * default_construct (GstRTSPMediaFactory *factory, const GstRTSPUrl *url);
static void default_configure (GstRTSPMediaFactory *factory, GstRTSPMedia *media);
static GstElement* default_create_pipeline (GstRTSPMediaFactory *factory, GstRTSPMedia *media);

G_DEFINE_TYPE (GstRTSPMediaFactory, gst_rtsp_media_factory, G_TYPE_OBJECT);

static void
gst_rtsp_media_factory_class_init (GstRTSPMediaFactoryClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_rtsp_media_factory_get_property;
  gobject_class->set_property = gst_rtsp_media_factory_set_property;
  gobject_class->finalize = gst_rtsp_media_factory_finalize;

  /**
   * GstRTSPMediaFactory::launch
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
  g_object_class_install_property (gobject_class, PROP_LAUNCH,
      g_param_spec_string ("launch", "Launch", "A launch description of the pipeline",
          DEFAULT_LAUNCH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SHARED,
      g_param_spec_boolean ("shared", "Shared", "If media from this factory is shared",
          DEFAULT_SHARED, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  klass->gen_key = default_gen_key;
  klass->get_element = default_get_element;
  klass->construct = default_construct;
  klass->configure = default_configure;
  klass->create_pipeline = default_create_pipeline;
}

static void
gst_rtsp_media_factory_init (GstRTSPMediaFactory * factory)
{
  factory->launch = g_strdup (DEFAULT_LAUNCH);
  factory->shared = DEFAULT_SHARED;

  factory->lock = g_mutex_new ();
  factory->medias_lock = g_mutex_new ();
  factory->medias = g_hash_table_new_full (g_str_hash, g_str_equal,
		  g_free, g_object_unref);
}

static void
gst_rtsp_media_factory_finalize (GObject * obj)
{
  GstRTSPMediaFactory *factory = GST_RTSP_MEDIA_FACTORY (obj);

  g_hash_table_unref (factory->medias);
  g_mutex_free (factory->medias_lock);
  g_free (factory->launch);
  g_mutex_free (factory->lock);

  G_OBJECT_CLASS (gst_rtsp_media_factory_parent_class)->finalize (obj);
}

static void
gst_rtsp_media_factory_get_property (GObject *object, guint propid,
    GValue *value, GParamSpec *pspec)
{
  GstRTSPMediaFactory *factory = GST_RTSP_MEDIA_FACTORY (object);

  switch (propid) {
    case PROP_LAUNCH:
      g_value_take_string (value, gst_rtsp_media_factory_get_launch (factory));
      break;
    case PROP_SHARED:
      g_value_set_boolean (value, gst_rtsp_media_factory_is_shared (factory));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

static void
gst_rtsp_media_factory_set_property (GObject *object, guint propid,
    const GValue *value, GParamSpec *pspec)
{
  GstRTSPMediaFactory *factory = GST_RTSP_MEDIA_FACTORY (object);

  switch (propid) {
    case PROP_LAUNCH:
      gst_rtsp_media_factory_set_launch (factory, g_value_get_string (value));
      break;
    case PROP_SHARED:
      gst_rtsp_media_factory_set_shared (factory, g_value_get_boolean (value));
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
 * Returns: a new #GstRTSPMediaFactory object.
 */
GstRTSPMediaFactory *
gst_rtsp_media_factory_new (void)
{
  GstRTSPMediaFactory *result;

  result = g_object_new (GST_TYPE_RTSP_MEDIA_FACTORY, NULL);

  return result;
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
gst_rtsp_media_factory_set_launch (GstRTSPMediaFactory *factory, const gchar *launch)
{
  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));
  g_return_if_fail (launch != NULL);

  g_mutex_lock (factory->lock);
  g_free (factory->launch);
  factory->launch = g_strdup (launch);
  g_mutex_unlock (factory->lock);
}

/**
 * gst_rtsp_media_factory_get_launch:
 * @factory: a #GstRTSPMediaFactory
 *
 * Get the gst_parse_launch() pipeline description that will be used in the
 * default prepare vmethod.
 *
 * Returns: the configured launch description. g_free() after usage.
 */
gchar *
gst_rtsp_media_factory_get_launch (GstRTSPMediaFactory *factory)
{
  gchar *result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), NULL);

  g_mutex_lock (factory->lock);
  result = g_strdup (factory->launch);
  g_mutex_unlock (factory->lock);

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
gst_rtsp_media_factory_set_shared (GstRTSPMediaFactory *factory,
    gboolean shared)
{
  g_return_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory));

  g_mutex_lock (factory->lock);
  factory->shared = shared;
  g_mutex_unlock (factory->lock);
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
gst_rtsp_media_factory_is_shared (GstRTSPMediaFactory *factory)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_RTSP_MEDIA_FACTORY (factory), FALSE);

  g_mutex_lock (factory->lock);
  result = factory->shared;
  g_mutex_unlock (factory->lock);

  return result;
}

static gboolean
compare_media (gpointer key, GstRTSPMedia *media1, GstRTSPMedia *media2)
{
  return (media1 == media2);
}

static void
media_unprepared (GstRTSPMedia *media, GstRTSPMediaFactory *factory)
{
  g_mutex_lock (factory->medias_lock);
  g_hash_table_foreach_remove (factory->medias, (GHRFunc) compare_media,
       media);
  g_mutex_unlock (factory->medias_lock);
}

/**
 * gst_rtsp_media_factory_construct:
 * @factory: a #GstRTSPMediaFactory
 * @url: the url used
 *
 * Prepare the media object and create its streams. Implementations
 * should create the needed gstreamer elements and add them to the result
 * object. No state changes should be performed on them yet.
 *
 * One or more GstRTSPMediaStream objects should be added to the result with
 * the srcpad member set to a source pad that produces buffer of type 
 * application/x-rtp.
 *
 * Returns: a new #GstRTSPMedia if the media could be prepared.
 */
GstRTSPMedia *
gst_rtsp_media_factory_construct (GstRTSPMediaFactory *factory, const GstRTSPUrl *url)
{
  gchar *key;
  GstRTSPMedia *media;
  GstRTSPMediaFactoryClass *klass;

  klass = GST_RTSP_MEDIA_FACTORY_GET_CLASS (factory);

  /* convert the url to a key for the hashtable. NULL return or a NULL function
   * will not cache anything for this factory. */
  if (klass->gen_key)
    key = klass->gen_key (factory, url);
  else
    key = NULL;

  g_mutex_lock (factory->medias_lock);
  if (key) {
    /* we have a key, see if we find a cached media */
    media = g_hash_table_lookup (factory->medias, key);
    if (media)
      g_object_ref (media);
  }
  else
    media = NULL;

  if (media == NULL) {
    /* nothing cached found, try to create one */
    if (klass->construct)
      media = klass->construct (factory, url);
    else
      media = NULL;

    if (media) {
      /* configure the media */
      if (klass->configure)
        klass->configure (factory, media);

      /* check if we can cache this media */
      if (gst_rtsp_media_is_shared (media)) {
        /* insert in the hashtable, takes ownership of the key */
        g_object_ref (media);
        g_hash_table_insert (factory->medias, key, media);
        key = NULL;
      }
      if (!gst_rtsp_media_is_reusable (media)) {
	/* when not reusable, connect to the unprepare signal to remove the item
	 * from our cache when it gets unprepared */
	g_signal_connect (media, "unprepared", (GCallback) media_unprepared,
			factory);
      }
    }
  }
  g_mutex_unlock (factory->medias_lock);

  if (key)
    g_free (key);

  g_message ("constructed media %p for url %s", media, url->abspath);

  return media;
}

static gchar *
default_gen_key (GstRTSPMediaFactory *factory, const GstRTSPUrl *url)
{
  gchar *result;

  result = gst_rtsp_url_get_request_uri ((GstRTSPUrl *)url);

  return result;
}

static GstElement *
default_get_element (GstRTSPMediaFactory *factory, const GstRTSPUrl *url)
{
  GstElement *element;
  GError *error = NULL;

  g_mutex_lock (factory->lock);
  /* we need a parse syntax */
  if (factory->launch == NULL)
    goto no_launch;

  /* parse the user provided launch line */
  element = gst_parse_launch (factory->launch, &error);
  if (element == NULL)
    goto parse_error;

  g_mutex_unlock (factory->lock);

  if (error != NULL) {
    /* a recoverable error was encountered */
    g_warning ("recoverable parsing error: %s", error->message);
    g_error_free (error);
  }
  return element;

  /* ERRORS */
no_launch:
  {
    g_mutex_unlock (factory->lock);
    g_critical ("no launch line specified");
    return NULL;
  }
parse_error:
  {
    g_mutex_unlock (factory->lock);
    g_critical ("could not parse launch syntax (%s): %s", factory->launch, 
         (error ? error->message : "unknown reason"));
    if (error)
      g_error_free (error);
    return NULL;
  }
}

/* try to find all the payloader elements, they should be named 'pay%d'. for
 * each of the payloaders we will create a stream and collect the source pad. */
void
gst_rtsp_media_factory_collect_streams (GstRTSPMediaFactory *factory, const GstRTSPUrl *url,
    GstRTSPMedia *media)
{
  GstElement *element, *elem;
  GstPad * pad;
  gint i;
  GstRTSPMediaStream *stream;
  gboolean have_elem;

  element = media->element;

  have_elem = TRUE;
  for (i = 0; have_elem ; i++) {
    gchar *name;

    have_elem = FALSE;

    name = g_strdup_printf ("pay%d", i);
    if ((elem = gst_bin_get_by_name (GST_BIN (element), name))) {
      /* create the stream */
      stream = g_new0 (GstRTSPMediaStream, 1);
      stream->payloader = elem;

      g_message ("found stream %d with payloader %p", i, elem);

      pad = gst_element_get_static_pad (elem, "src");

      /* ghost the pad of the payloader to the element */
      stream->srcpad = gst_ghost_pad_new (name, pad);
      gst_element_add_pad (media->element, stream->srcpad);
      gst_object_unref (elem);

      /* add stream now */
      g_array_append_val (media->streams, stream);
      have_elem = TRUE;
    }
    g_free (name);

    name = g_strdup_printf ("dynpay%d", i);
    if ((elem = gst_bin_get_by_name (GST_BIN (element), name))) {
      /* a stream that will dynamically create pads to provide RTP packets */

      g_message ("found dynamic element %d, %p", i, elem);

      media->dynamic = g_list_prepend (media->dynamic, elem);

      have_elem = TRUE;
    }
    g_free (name);
  }
}

static GstRTSPMedia *
default_construct (GstRTSPMediaFactory *factory, const GstRTSPUrl *url)
{
  GstRTSPMedia *media;
  GstElement *element;
  GstRTSPMediaFactoryClass *klass;

  klass = GST_RTSP_MEDIA_FACTORY_GET_CLASS (factory);

  if (klass->get_element)
    element = klass->get_element (factory, url);
  else
    element = NULL;
  if (element == NULL)
    goto no_element;

  /* create a new empty media */
  media = gst_rtsp_media_new ();
  media->element = element;

  if (!klass->create_pipeline)
    goto no_pipeline;

  media->pipeline = klass->create_pipeline (factory, media);

  gst_rtsp_media_factory_collect_streams (factory, url, media);

  return media;

  /* ERRORS */
no_element:
  {
    g_critical ("could not create element");
    return NULL;
  }
no_pipeline:
  {
    g_critical ("could not create pipeline");
    return FALSE;
  }
}

static GstElement*
default_create_pipeline (GstRTSPMediaFactory *factory, GstRTSPMedia *media) {
  GstElement *pipeline;

  pipeline = gst_pipeline_new ("media-pipeline");
  gst_bin_add (GST_BIN_CAST (pipeline), media->element);

  return pipeline;
}

static void
default_configure (GstRTSPMediaFactory *factory, GstRTSPMedia *media)
{
  gboolean shared;

  /* configure the sharedness */
  g_mutex_lock (factory->lock);
  shared = factory->shared;
  g_mutex_unlock (factory->lock);

  gst_rtsp_media_set_shared (media, shared);
}
