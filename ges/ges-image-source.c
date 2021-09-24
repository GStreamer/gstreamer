/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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
 * SECTION:gesimagesource
 * @title: GESImageSource
 * @short_description: outputs the video stream from a media file as a still
 * image.
 *
 * Outputs the video stream from a given file as a still frame. The frame chosen
 * will be determined by the in-point property on the track element. For image
 * files, do not set the in-point property.
 *
 * Deprecated: 1.18: This won't be used anymore and has been replaced by
 * #GESUriSource instead which now plugs an `imagefreeze` element when
 * #ges_uri_source_asset_is_image returns %TRUE.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-image-source.h"


struct _GESImageSourcePrivate
{
  /*  Dummy variable */
  void *nothing;
};

enum
{
  PROP_0,
  PROP_URI
};

G_DEFINE_TYPE_WITH_PRIVATE (GESImageSource, ges_image_source,
    GES_TYPE_VIDEO_SOURCE);
static void
ges_image_source_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESImageSource *uriclip = GES_IMAGE_SOURCE (object);

  switch (property_id) {
    case PROP_URI:
      g_value_set_string (value, uriclip->uri);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_image_source_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESImageSource *uriclip = GES_IMAGE_SOURCE (object);

  switch (property_id) {
    case PROP_URI:
      uriclip->uri = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_image_source_dispose (GObject * object)
{
  GESImageSource *uriclip = GES_IMAGE_SOURCE (object);

  if (uriclip->uri)
    g_free (uriclip->uri);

  G_OBJECT_CLASS (ges_image_source_parent_class)->dispose (object);
}

static void
pad_added_cb (GstElement * source, GstPad * pad, GstElement * scale)
{
  GstPad *sinkpad;
  GstPadLinkReturn ret;

  sinkpad = gst_element_get_static_pad (scale, "sink");
  if (sinkpad) {
    GST_DEBUG ("got sink pad, trying to link");

    ret = gst_pad_link (pad, sinkpad);
    gst_object_unref (sinkpad);
    if (GST_PAD_LINK_SUCCESSFUL (ret)) {
      GST_DEBUG ("linked ok, returning");
      return;
    }
  }

  GST_DEBUG ("pad failed to link properly");
}

static GstElement *
ges_image_source_create_source (GESSource * source)
{
  GstElement *bin, *src, *scale, *freeze, *iconv;
  GstPad *srcpad, *target;

  bin = GST_ELEMENT (gst_bin_new ("still-image-bin"));
  src = gst_element_factory_make ("uridecodebin", NULL);
  scale = gst_element_factory_make ("videoscale", NULL);
  freeze = gst_element_factory_make ("imagefreeze", NULL);
  iconv = gst_element_factory_make ("videoconvert", NULL);

  g_object_set (scale, "add-borders", TRUE, NULL);

  gst_bin_add_many (GST_BIN (bin), src, scale, freeze, iconv, NULL);

  gst_element_link_pads_full (scale, "src", iconv, "sink",
      GST_PAD_LINK_CHECK_NOTHING);
  gst_element_link_pads_full (iconv, "src", freeze, "sink",
      GST_PAD_LINK_CHECK_NOTHING);

  /* FIXME: add capsfilter here with sink caps (see 626518) */

  target = gst_element_get_static_pad (freeze, "src");

  srcpad = gst_ghost_pad_new ("src", target);
  gst_element_add_pad (bin, srcpad);
  gst_object_unref (target);

  g_object_set (src, "uri", ((GESImageSource *) source)->uri, NULL);

  g_signal_connect (G_OBJECT (src), "pad-added",
      G_CALLBACK (pad_added_cb), scale);

  return bin;
}

static void
ges_image_source_class_init (GESImageSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESSourceClass *source_class = GES_SOURCE_CLASS (klass);
  GESVideoSourceClass *vsource_class = GES_VIDEO_SOURCE_CLASS (klass);

  object_class->get_property = ges_image_source_get_property;
  object_class->set_property = ges_image_source_set_property;
  object_class->dispose = ges_image_source_dispose;

  /**
   * GESImageSource:uri:
   *
   * The location of the file/resource to use.
   */
  g_object_class_install_property (object_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "uri of the resource",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  source_class->create_source = ges_image_source_create_source;
  vsource_class->ABI.abi.get_natural_size =
      ges_video_uri_source_get_natural_size;

  GES_TRACK_ELEMENT_CLASS_DEFAULT_HAS_INTERNAL_SOURCE (klass) = FALSE;
}

static void
ges_image_source_init (GESImageSource * self)
{
  self->priv = ges_image_source_get_instance_private (self);
}

/* @uri: the URI the source should control
 *
 * Creates a new #GESImageSource for the provided @uri.
 *
 * Returns: (transfer floating): A new #GESImageSource.
 */
GESImageSource *
ges_image_source_new (gchar * uri)
{
  GESImageSource *res;
  GESAsset *asset = ges_asset_request (GES_TYPE_IMAGE_SOURCE, uri, NULL);

  res = GES_IMAGE_SOURCE (ges_asset_extract (asset, NULL));
  res->uri = g_strdup (uri);
  gst_object_unref (asset);

  return res;
}
