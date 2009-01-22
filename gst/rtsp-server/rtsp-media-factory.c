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

enum
{
  PROP_0,
  PROP_LAUNCH,
  PROP_LAST
};

static void gst_rtsp_media_factory_get_property (GObject *object, guint propid,
    GValue *value, GParamSpec *pspec);
static void gst_rtsp_media_factory_set_property (GObject *object, guint propid,
    const GValue *value, GParamSpec *pspec);
static void gst_rtsp_media_factory_finalize (GObject * obj);

static GstRTSPMediaBin * default_construct (GstRTSPMediaFactory *factory, const gchar *location);
static GstElement * default_get_element (GstRTSPMediaFactory *factory, const gchar *location);

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

  klass->construct = default_construct;
  klass->get_element = default_get_element;
}

static void
gst_rtsp_media_factory_init (GstRTSPMediaFactory * factory)
{
}

static void
gst_rtsp_media_factory_finalize (GObject * obj)
{
  GstRTSPMediaFactory *factory = GST_RTSP_MEDIA_FACTORY (obj);

  g_free (factory->launch);

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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, propid, pspec);
  }
}

/**
 * gst_rtsp_media_factory_new:
 *
 * Create a new #GstRTSPMediaFactory instance.
 *
 * Returns: a new #GstRTSPMediaFactory object or %NULL when location did not contain a
 * valid or understood URL.
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

  factory->launch = g_strdup (launch);
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

  result = g_strdup (factory->launch);

  return result;
}

/**
 * gst_rtsp_media_factory_construct:
 * @factory: a #GstRTSPMediaFactory
 * @location: the url used
 *
 * Prepare the media bin object and create its streams. Implementations
 * should create the needed gstreamer elements and add them to the result
 * object. No state changes should be performed on them yet.
 *
 * One or more GstRTSPMediaStream objects should be added to the result with
 * the srcpad member set to a source pad that produces buffer of type 
 * application/x-rtp.
 *
 * Returns: a new #GstRTSPMediaBin if the media could be prepared.
 */
GstRTSPMediaBin *
gst_rtsp_media_factory_construct (GstRTSPMediaFactory *factory, const gchar *location)
{
  GstRTSPMediaBin *res;
  GstRTSPMediaFactoryClass *klass;

  klass = GST_RTSP_MEDIA_FACTORY_GET_CLASS (factory);

  if (klass->construct)
    res = klass->construct (factory, location);
  else
    res = NULL;

  g_message ("constructed mediabin %p for location %s", res, location);

  return res;
}

static void
caps_notify (GstPad * pad, GParamSpec * unused, GstRTSPMediaStream * stream)
{
  if (stream->caps)
    gst_caps_unref (stream->caps);
  if ((stream->caps = GST_PAD_CAPS (pad)))
    gst_caps_ref (stream->caps);
}

static GstElement *
default_get_element (GstRTSPMediaFactory *factory, const gchar *location)
{
  GstElement *element;
  GError *error = NULL;

  /* we need a parse syntax */
  if (factory->launch == NULL)
    goto no_launch;

  /* parse the user provided launch line */
  element = gst_parse_launch (factory->launch, &error);
  if (element == NULL)
    goto parse_error;

  if (error != NULL) {
    /* a recoverable error was encountered */
    g_warning ("recoverable parsing error: %s", error->message);
    g_error_free (error);
  }
  return element;

  /* ERRORS */
no_launch:
  {
    g_critical ("no launch line specified");
    return NULL;
  }
parse_error:
  {
    g_critical ("could not parse launch syntax (%s): %s", factory->launch, 
         (error ? error->message : "unknown reason"));
    if (error)
      g_error_free (error);
    return NULL;
  }
}

static GstRTSPMediaBin *
default_construct (GstRTSPMediaFactory *factory, const gchar *location)
{
  GstRTSPMediaBin *bin;
  GstRTSPMediaStream *stream;
  GstElement *pay, *element;
  GstPad * pad;
  gint i;
  GstRTSPMediaFactoryClass *klass;

  klass = GST_RTSP_MEDIA_FACTORY_GET_CLASS (factory);

  if (klass->get_element)
    element = klass->get_element (factory, location);
  else
    element = NULL;
  if (element == NULL)
    goto no_element;

  bin = g_object_new (GST_TYPE_RTSP_MEDIA_BIN, NULL);
  bin->element = element;

  /* try to find all the payloader elements, they should be named 'pay%d'. for
   * each of the payloaders we will create a stream, collect the source pad and
   * add a notify::caps on the pad. */
  for (i = 0; ; i++) {
    gchar *name;

    name = g_strdup_printf ("pay%d", i);

    if (!(pay = gst_bin_get_by_name (GST_BIN (element), name))) {
      /* no more payloaders found, we have found all the streams and we can
       * end the loop */
      g_free (name);
      break;
    }
    
    /* create the stream */
    stream = g_new0 (GstRTSPMediaStream, 1);
    stream->mediabin = bin;
    stream->element = element;
    stream->payloader = pay;
    stream->idx = bin->streams->len;

    pad = gst_element_get_static_pad (pay, "src");

    /* ghost the pad of the payloader to the element */
    stream->srcpad = gst_ghost_pad_new (name, pad);
    gst_element_add_pad (stream->element, stream->srcpad);

    stream->caps_sig = g_signal_connect (pad, "notify::caps", (GCallback) caps_notify, stream);
    gst_object_unref (pad);

    /* add stream now */
    g_array_append_val (bin->streams, stream);
    gst_object_unref (pay);

    g_free (name);
  }

  return bin;

  /* ERRORS */
no_element:
  {
    g_critical ("could not create element");
    return NULL;
  }
}
