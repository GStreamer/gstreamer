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
 * SECTION:ges-video-source
 * @short_description: Base Class for video sources
 */

#include "ges-internal.h"
#include "ges/ges-meta-container.h"
#include "ges-track-element.h"
#include "ges-video-source.h"
#include "ges-layer.h"
#include "gstframepositionner.h"

G_DEFINE_TYPE (GESVideoSource, ges_video_source, GES_TYPE_SOURCE);

struct _GESVideoSourcePrivate
{
  GstFramePositionner *positionner;
  GstElement *capsfilter;
};

static void
update_z_order_cb (GESClip * clip, GParamSpec * arg G_GNUC_UNUSED,
    GESVideoSource * self)
{
  GESLayer *layer = ges_clip_get_layer (clip);

  if (layer == NULL)
    return;

  /* 10000 is the max value of zorder on videomixerpad, hardcoded */

  g_object_set (self->priv->positionner, "zorder",
      10000 - ges_layer_get_priority (layer), NULL);

  gst_object_unref (layer);
}

static GstElement *
ges_video_source_create_element (GESTrackElement * trksrc)
{
  GstElement *topbin;
  GstElement *sub_element;
  GESVideoSourceClass *source_class = GES_VIDEO_SOURCE_GET_CLASS (trksrc);
  GESVideoSource *self;
  GstElement *positionner, *videoscale, *capsfilter;
  const gchar *props[] = { "alpha", "posx", "posy", "width", "height", NULL };
  GESTimelineElement *parent;

  if (!source_class->create_source)
    return NULL;

  sub_element = source_class->create_source (trksrc);

  self = (GESVideoSource *) trksrc;

  /* That positionner will add metadata to buffers according to its
     properties, acting like a proxy for our smart-mixer dynamic pads. */
  positionner = gst_element_factory_make ("framepositionner", "frame_tagger");

  videoscale =
      gst_element_factory_make ("videoscale", "track-element-videoscale");
  capsfilter =
      gst_element_factory_make ("capsfilter", "track-element-capsfilter");

  ges_frame_positionner_set_source_and_filter (GST_FRAME_POSITIONNER
      (positionner), trksrc, capsfilter);

  ges_track_element_add_children_props (trksrc, positionner, NULL, NULL, props);
  topbin =
      ges_source_create_topbin ("videosrcbin", sub_element, positionner,
      videoscale, capsfilter, NULL);
  parent = ges_timeline_element_get_parent (GES_TIMELINE_ELEMENT (trksrc));
  if (parent) {
    self->priv->positionner = GST_FRAME_POSITIONNER (positionner);
    g_signal_connect (parent, "notify::layer",
        (GCallback) update_z_order_cb, trksrc);
    update_z_order_cb (GES_CLIP (parent), NULL, self);
    gst_object_unref (parent);
  } else {
    GST_ERROR ("No parent timeline element, SHOULD NOT HAPPEN");
  }

  self->priv->capsfilter = capsfilter;

  return topbin;
}

static void
ges_video_source_class_init (GESVideoSourceClass * klass)
{
  GESTrackElementClass *track_class = GES_TRACK_ELEMENT_CLASS (klass);
  GESVideoSourceClass *video_source_class = GES_VIDEO_SOURCE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESVideoSourcePrivate));

  track_class->gnlobject_factorytype = "gnlsource";
  track_class->create_element = ges_video_source_create_element;
  video_source_class->create_source = NULL;
}

static void
ges_video_source_init (GESVideoSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_VIDEO_SOURCE, GESVideoSourcePrivate);
  self->priv->positionner = NULL;
  self->priv->capsfilter = NULL;
}
