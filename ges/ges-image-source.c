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
 * SECTION:ges-image-source
 * @short_description: outputs the video stream from a media file as a still
 * image.
 * 
 * Outputs the video stream from a given file as a still frame. The frame
 * chosen will be determined by the in-point property on the track object. For
 * image files, do not set the in-point property.
 */

#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-image-source.h"

G_DEFINE_TYPE (GESImageSource, ges_image_source, GES_TYPE_SOURCE);

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
pad_added_cb (GstElement * timeline, GstPad * pad, GstElement * scale)
{
  GstPad *sinkpad;
  GstPadLinkReturn ret;

  sinkpad = gst_element_get_static_pad (scale, "sink");
  if (sinkpad) {
    GST_DEBUG ("got sink pad, trying to link");

    ret = gst_pad_link (pad, sinkpad);
    if GST_PAD_LINK_SUCCESSFUL
      (ret) {
      GST_DEBUG ("linked ok, returning");
      gst_object_unref (sinkpad);
      return;
      }
  }

  GST_DEBUG ("pad failed to link properly");
}

static GstElement *
ges_image_source_create_element (GESTrackElement * object)
{
  GstElement *bin, *source, *scale, *freeze, *iconv;
  GstPad *src, *target;

  bin = GST_ELEMENT (gst_bin_new ("still-image-bin"));
  source = gst_element_factory_make ("uridecodebin", NULL);
  scale = gst_element_factory_make ("videoscale", NULL);
  freeze = gst_element_factory_make ("imagefreeze", NULL);
  iconv = gst_element_factory_make ("videoconvert", NULL);

  g_object_set (scale, "add-borders", TRUE, NULL);

  gst_bin_add_many (GST_BIN (bin), source, scale, freeze, iconv, NULL);

  gst_element_link_pads_full (scale, "src", iconv, "sink",
      GST_PAD_LINK_CHECK_NOTHING);
  gst_element_link_pads_full (iconv, "src", freeze, "sink",
      GST_PAD_LINK_CHECK_NOTHING);

  /* FIXME: add capsfilter here with sink caps (see 626518) */

  target = gst_element_get_static_pad (freeze, "src");

  src = gst_ghost_pad_new ("src", target);
  gst_element_add_pad (bin, src);
  gst_object_unref (target);

  g_object_set (source, "uri", ((GESImageSource *) object)->uri, NULL);

  g_signal_connect (G_OBJECT (source), "pad-added",
      G_CALLBACK (pad_added_cb), scale);

  return bin;
}

static void
ges_image_source_class_init (GESImageSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackElementClass *gesobj_class = GES_TRACK_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESImageSourcePrivate));

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
  gesobj_class->create_element = ges_image_source_create_element;
}

static void
ges_image_source_init (GESImageSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_IMAGE_SOURCE, GESImageSourcePrivate);
}

/**
 * ges_image_source_new:
 * @uri: the URI the source should control
 *
 * Creates a new #GESImageSource for the provided @uri.
 *
 * Returns: A new #GESImageSource.
 */
GESImageSource *
ges_image_source_new (gchar * uri)
{
  return g_object_new (GES_TYPE_IMAGE_SOURCE, "uri", uri, "track-type",
      GES_TRACK_TYPE_VIDEO, NULL);
}
