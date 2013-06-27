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
 * SECTION:ges-source
 * @short_description: Base Class for single-media sources
 */

#include "ges/ges-meta-container.h"
#include "ges-track-element.h"
#include "ges-source.h"
#include "ges-layer.h"
#include "gstframepositionner.h"

G_DEFINE_TYPE (GESSource, ges_source, GES_TYPE_TRACK_ELEMENT);

struct _GESSourcePrivate
{
  /*  Dummy variable */
  GstFramePositionner *positionner;
};

static void
_pad_added_cb (GstElement * element, GstPad * srcpad, GstPad * sinkpad)
{
  gst_element_no_more_pads (element);
  gst_pad_link (srcpad, sinkpad);
}

static void
_ghost_pad_added_cb (GstElement * element, GstPad * srcpad, GstElement * bin)
{
  GstPad *ghost;

  ghost = gst_ghost_pad_new ("src", srcpad);
  gst_pad_set_active (ghost, TRUE);
  gst_element_add_pad (bin, ghost);
  gst_element_no_more_pads (element);
}

static GstElement *
_create_bin (const gchar * bin_name, GstElement * sub_element, ...)
{
  va_list argp;

  GstElement *element;
  GstElement *prev = NULL;
  GstElement *first = NULL;
  GstElement *bin;
  GstPad *sub_srcpad;

  va_start (argp, sub_element);
  bin = gst_bin_new (bin_name);
  gst_bin_add (GST_BIN (bin), sub_element);

  while ((element = va_arg (argp, GstElement *)) != NULL) {
    gst_bin_add (GST_BIN (bin), element);
    if (prev)
      gst_element_link (prev, element);
    prev = element;
    if (first == NULL)
      first = element;
  }

  va_end (argp);

  sub_srcpad = gst_element_get_static_pad (sub_element, "src");

  if (prev != NULL) {
    GstPad *srcpad, *sinkpad, *ghost;

    srcpad = gst_element_get_static_pad (prev, "src");
    ghost = gst_ghost_pad_new ("src", srcpad);
    gst_pad_set_active (ghost, TRUE);
    gst_element_add_pad (bin, ghost);

    sinkpad = gst_element_get_static_pad (first, "sink");
    if (sub_srcpad)
      gst_pad_link (sub_srcpad, sinkpad);
    else
      g_signal_connect (sub_element, "pad-added", G_CALLBACK (_pad_added_cb),
          sinkpad);

    gst_object_unref (srcpad);
    gst_object_unref (sinkpad);

  } else if (sub_srcpad) {
    GstPad *ghost;

    ghost = gst_ghost_pad_new ("src", sub_srcpad);
    gst_pad_set_active (ghost, TRUE);
    gst_element_add_pad (bin, ghost);
  } else {
    g_signal_connect (sub_element, "pad-added",
        G_CALLBACK (_ghost_pad_added_cb), bin);
  }

  return bin;
}

static void
_sync_element_to_layer_property_float (GESTrackElement * trksrc,
    GstElement * element, const gchar * meta, const gchar * propname)
{
  GESTimelineElement *parent;
  GESLayer *layer;
  gfloat value;

  parent = ges_timeline_element_get_parent (GES_TIMELINE_ELEMENT (trksrc));
  layer = ges_clip_get_layer (GES_CLIP (parent));

  gst_object_unref (parent);

  if (layer != NULL) {

    ges_meta_container_get_float (GES_META_CONTAINER (layer), meta, &value);
    g_object_set (element, propname, value, NULL);
    GST_DEBUG_OBJECT (trksrc, "Setting %s to %f", propname, value);

  } else {

    GST_DEBUG_OBJECT (trksrc, "NOT setting the %s", propname);
  }

  gst_object_unref (layer);
}

static void
update_z_order_cb (GESClip * clip, GParamSpec * arg G_GNUC_UNUSED,
    GESSource * self)
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
ges_source_create_element (GESTrackElement * trksrc)
{
  GstElement *topbin;
  GstElement *sub_element;
  GESSourceClass *source_class = GES_SOURCE_GET_CLASS (trksrc);
  GESTrack *track;
  GESSource *self;

  if (!source_class->create_source)
    return NULL;

  sub_element = source_class->create_source (trksrc);

  track = ges_track_element_get_track (trksrc);

  self = (GESSource *) trksrc;

  switch (track->type) {
    case GES_TRACK_TYPE_AUDIO:
    {
      const gchar *props[] = { "volume", "mute", NULL };
      GstElement *volume;

      GST_DEBUG_OBJECT (trksrc, "Creating a bin sub_element ! volume");

      volume = gst_element_factory_make ("volume", NULL);

      topbin = _create_bin ("audio-src-bin", sub_element, volume, NULL);

      _sync_element_to_layer_property_float (trksrc, volume, GES_META_VOLUME,
          "volume");
      ges_track_element_add_children_props (trksrc, volume, NULL, NULL, props);
      break;
    }
    case GES_TRACK_TYPE_VIDEO:
    {
      GstElement *positionner;
      const gchar *props[] = { "alpha", "posx", "posy", NULL };
      GESTimelineElement *parent;

      /* That positionner will add metadata to buffers according to its
         properties, acting like a proxy for our smart-mixer dynamic pads. */
      positionner =
          gst_element_factory_make ("framepositionner", "frame_tagger");

      ges_track_element_add_children_props (trksrc, positionner, NULL, NULL,
          props);
      topbin = _create_bin ("video-src-bin", sub_element, positionner, NULL);
      parent = ges_timeline_element_get_parent (GES_TIMELINE_ELEMENT (trksrc));
      if (parent) {
        self->priv->positionner = GST_FRAME_POSITIONNER (positionner);
        g_signal_connect (parent, "notify::layer",
            (GCallback) update_z_order_cb, trksrc);
        update_z_order_cb (GES_CLIP (parent), NULL, self);
        gst_object_unref (parent);
      } else {
        GST_WARNING ("No parent timeline element, SHOULD NOT HAPPEN");
      }
      break;
    }
    default:
      topbin = _create_bin ("a-questionable-name", sub_element, NULL);
      break;
  }

  return topbin;
}

static void
ges_source_class_init (GESSourceClass * klass)
{
  GESTrackElementClass *track_class = GES_TRACK_ELEMENT_CLASS (klass);
  GESSourceClass *source_class = GES_SOURCE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESSourcePrivate));

  track_class->gnlobject_factorytype = "gnlsource";
  track_class->create_element = ges_source_create_element;
  source_class->create_source = NULL;
}

static void
ges_source_init (GESSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_SOURCE, GESSourcePrivate);
  self->priv->positionner = NULL;
}
