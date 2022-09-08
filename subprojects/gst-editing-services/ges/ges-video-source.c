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
 * SECTION:gesvideosource
 * @title: GESVideoSource
 * @short_description: Base Class for video sources
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/pbutils/missing-plugins.h>
#include <gst/video/video.h>

#include "ges-internal.h"
#include "ges/ges-meta-container.h"
#include "ges-track-element.h"
#include "ges-video-source.h"
#include "ges-layer.h"
#include "gstframepositioner.h"
#include "ges-extractable.h"

#define parent_class ges_video_source_parent_class
static GESExtractableInterface *parent_extractable_iface = NULL;

struct _GESVideoSourcePrivate
{
  GstFramePositioner *positioner;
  GstElement *capsfilter;
};

static void
ges_video_source_set_asset (GESExtractable * extractable, GESAsset * asset)
{
  GESVideoSource *self = GES_VIDEO_SOURCE (extractable);

  parent_extractable_iface->set_asset (extractable, asset);

  ges_video_source_get_natural_size (self,
      &self->priv->positioner->natural_width,
      &self->priv->positioner->natural_height);
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->set_asset = ges_video_source_set_asset;

  parent_extractable_iface = g_type_interface_peek_parent (iface);
}

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GESVideoSource, ges_video_source,
    GES_TYPE_SOURCE, G_ADD_PRIVATE (GESVideoSource)
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));

/* TrackElement VMethods */

static gboolean
_set_priority (GESTimelineElement * element, guint32 priority)
{
  gboolean res;
  GESVideoSource *self = GES_VIDEO_SOURCE (element);

  res = GES_TIMELINE_ELEMENT_CLASS (parent_class)->set_priority (element,
      priority);

  if (res && self->priv->positioner)
    g_object_set (self->priv->positioner, "zorder", G_MAXUINT - priority, NULL);

  return res;
}


static gboolean
_set_parent (GESTimelineElement * element, GESTimelineElement * parent)
{
  GESVideoSource *self = GES_VIDEO_SOURCE (element);

  if (!parent)
    return TRUE;

  /* Some subclass might have different access to its natural size only
   * once it knows its parent */
  ges_video_source_get_natural_size (GES_VIDEO_SOURCE (self),
      &self->priv->positioner->natural_width,
      &self->priv->positioner->natural_height);

  return TRUE;
}


static gboolean
ges_video_source_create_filters (GESVideoSource * self, GPtrArray * elements,
    gboolean needs_converters)
{
  GESTrackElement *trksrc = GES_TRACK_ELEMENT (self);
  GstElement *positioner, *videoflip, *capsfilter, *videorate;
  const gchar *positioner_props[]
  = { "alpha", "posx", "posy", "width", "height", "operator", NULL };
  const gchar *videoflip_props[] = { "video-direction", NULL };
  gchar *ename = NULL;

  g_ptr_array_add (elements, gst_element_factory_make ("queue", NULL));

  /* That positioner will add metadata to buffers according to its
     properties, acting like a proxy for our smart-mixer dynamic pads. */
  positioner = gst_element_factory_make ("framepositioner", NULL);
  g_object_set (positioner, "zorder",
      G_MAXUINT - GES_TIMELINE_ELEMENT_PRIORITY (self), NULL);
  g_ptr_array_add (elements, positioner);

  if (needs_converters)
    g_ptr_array_add (elements, gst_element_factory_make ("videoconvert", NULL));

  /* If there's image-orientation tag, make sure the image is correctly oriented
   * before we scale it. */
  videoflip = gst_element_factory_make ("videoflip", "track-element-videoflip");
  g_object_set (videoflip, "video-direction", GST_VIDEO_ORIENTATION_AUTO, NULL);
  g_ptr_array_add (elements, videoflip);


  if (needs_converters) {
    ename =
        g_strdup_printf ("ges%s-videoscale", GES_TIMELINE_ELEMENT_NAME (self));
    g_ptr_array_add (elements, gst_element_factory_make ("videoscale", ename));
    g_free (ename);
    ename = g_strdup_printf ("ges%s-convert", GES_TIMELINE_ELEMENT_NAME (self));
    g_ptr_array_add (elements, gst_element_factory_make ("videoconvert",
            ename));
    g_free (ename);
  }
  ename = g_strdup_printf ("ges%s-rate", GES_TIMELINE_ELEMENT_NAME (self));
  videorate = gst_element_factory_make ("videorate", ename);
  g_object_set (videorate, "max-closing-segment-duplication-duration",
      GST_CLOCK_TIME_NONE, NULL);
  g_ptr_array_add (elements, videorate);

  g_free (ename);
  ename =
      g_strdup_printf ("ges%s-capsfilter", GES_TIMELINE_ELEMENT_NAME (self));
  capsfilter = gst_element_factory_make ("capsfilter", ename);
  g_free (ename);
  g_ptr_array_add (elements, capsfilter);

  ges_frame_positioner_set_source_and_filter (GST_FRAME_POSITIONNER
      (positioner), trksrc, capsfilter);

  ges_track_element_add_children_props (trksrc, positioner, NULL, NULL,
      positioner_props);
  ges_track_element_add_children_props (trksrc, videoflip, NULL, NULL,
      videoflip_props);

  self->priv->positioner = GST_FRAME_POSITIONNER (positioner);
  self->priv->positioner->scale_in_compositor =
      !GES_VIDEO_SOURCE_GET_CLASS (self)->ABI.abi.disable_scale_in_compositor;
  ges_video_source_get_natural_size (self,
      &self->priv->positioner->natural_width,
      &self->priv->positioner->natural_height);

  self->priv->capsfilter = capsfilter;

  return TRUE;
}

static GstElement *
ges_video_source_create_element (GESTrackElement * trksrc)
{
  GstElement *topbin;
  GstElement *sub_element;
  GESVideoSourceClass *vsource_class = GES_VIDEO_SOURCE_GET_CLASS (trksrc);
  GESSourceClass *source_class = GES_SOURCE_GET_CLASS (trksrc);
  GESVideoSource *self;
  gboolean needs_converters = TRUE;
  GPtrArray *elements;

  if (!source_class->create_source)
    return NULL;

  sub_element = source_class->create_source (GES_SOURCE (trksrc));

  self = (GESVideoSource *) trksrc;
  if (vsource_class->ABI.abi.needs_converters)
    needs_converters = vsource_class->ABI.abi.needs_converters (self);

  elements = g_ptr_array_new ();
  g_assert (vsource_class->ABI.abi.create_filters);
  if (!vsource_class->ABI.abi.create_filters (self, elements, needs_converters)) {
    g_ptr_array_free (elements, TRUE);

    return NULL;
  }

  topbin = ges_source_create_topbin (GES_SOURCE (trksrc), "videosrcbin",
      sub_element, elements);

  return topbin;
}

static gboolean
_lookup_child (GESTimelineElement * object,
    const gchar * prop_name, GObject ** element, GParamSpec ** pspec)
{
  gboolean res;

  gchar *clean_name;

  if (!g_strcmp0 (prop_name, "deinterlace-fields"))
    clean_name = g_strdup ("GstDeinterlace::fields");
  else if (!g_strcmp0 (prop_name, "deinterlace-mode"))
    clean_name = g_strdup ("GstDeinterlace::mode");
  else if (!g_strcmp0 (prop_name, "deinterlace-tff"))
    clean_name = g_strdup ("GstDeinterlace::tff");
  else if (!g_strcmp0 (prop_name, "tff") ||
      !g_strcmp0 (prop_name, "fields") || !g_strcmp0 (prop_name, "mode")) {
    GST_DEBUG_OBJECT (object, "Not allowed to use GstDeinterlace %s"
        " property without prefixing its name", prop_name);
    return FALSE;
  } else
    clean_name = g_strdup (prop_name);

  res =
      GES_TIMELINE_ELEMENT_CLASS (ges_video_source_parent_class)->lookup_child
      (object, clean_name, element, pspec);

  g_free (clean_name);

  return res;
}

static void
ges_video_source_class_init (GESVideoSourceClass * klass)
{
  GESTrackElementClass *track_element_class = GES_TRACK_ELEMENT_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);
  GESVideoSourceClass *video_source_class = GES_VIDEO_SOURCE_CLASS (klass);

  element_class->set_priority = _set_priority;
  element_class->lookup_child = _lookup_child;
  element_class->set_parent = _set_parent;

  track_element_class->nleobject_factorytype = "nlesource";
  track_element_class->create_element = ges_video_source_create_element;
  track_element_class->ABI.abi.default_track_type = GES_TRACK_TYPE_VIDEO;

  video_source_class->ABI.abi.create_filters = ges_video_source_create_filters;
}

static void
ges_video_source_init (GESVideoSource * self)
{
  self->priv = ges_video_source_get_instance_private (self);
  self->priv->positioner = NULL;
  self->priv->capsfilter = NULL;
}

/**
 * ges_video_source_get_natural_size:
 * @self: A #GESVideoSource
 * @width: (out): The natural width of the underlying source
 * @height: (out): The natural height of the underlying source
 *
 * Retrieves the natural size of the video stream. The natural size, is
 * the size at which it will be displayed if no scaling is being applied.
 *
 * NOTE: The sources take into account the potential video rotation applied
 * by the #videoflip element that is inside the source, effects applied on
 * the clip which potentially also rotate the element are not taken into
 * account.
 *
 * Returns: %TRUE if the object has a natural size, %FALSE otherwise.
 *
 * Since: 1.18
 */
gboolean
ges_video_source_get_natural_size (GESVideoSource * self, gint * width,
    gint * height)
{
  GESVideoSourceClass *klass = GES_VIDEO_SOURCE_GET_CLASS (self);

  if (!klass->ABI.abi.get_natural_size)
    return FALSE;

  return klass->ABI.abi.get_natural_size (self, width, height);
}
