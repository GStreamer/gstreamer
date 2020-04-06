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
 *
 * ## Children Properties:
 *
 * You can use the following children properties through the
 * #ges_track_element_set_child_property and alike set of methods:
 *
 * - #gdouble `alpha`: The desired alpha for the stream.
 * - #gint `posx`: The desired x position for the stream.
 * - #gint `posy`: The desired y position for the stream
 * - #gint `width`: The desired width for that source.
 *   Set to 0 if size is not mandatory, will be set to width of the current track.
 * - #gint `height`: The desired height for that source.
 *   Set to 0 if size is not mandatory, will be set to height of the current track.
 * - #GstDeinterlaceModes `deinterlace-mode`: Deinterlace Mode
 * - #GstDeinterlaceFields `deinterlace-fields`: Fields to use for deinterlacing
 * - #GstDeinterlaceFieldLayout `deinterlace-tff`: Deinterlace top field first
 * - #GstVideoOrientationMethod `video-direction`: The desired video rotation and flipping.
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

static void
post_missing_element_message (GstElement * element, const gchar * name)
{
  GstMessage *msg;

  msg = gst_missing_element_message_new (element, name);
  gst_element_post_message (element, msg);
}

static gboolean
ges_video_source_create_filters (GESVideoSource * self, GPtrArray * elements,
    gboolean needs_converters)
{
  GESTrackElement *trksrc = GES_TRACK_ELEMENT (self);
  GstElement *positioner, *videoflip, *capsfilter, *deinterlace;
  const gchar *positioner_props[] =
      { "alpha", "posx", "posy", "width", "height", NULL };
  const gchar *deinterlace_props[] = { "mode", "fields", "tff", NULL };
  const gchar *videoflip_props[] = { "video-direction", NULL };

  g_ptr_array_add (elements, gst_element_factory_make ("queue", NULL));

  /* That positioner will add metadata to buffers according to its
     properties, acting like a proxy for our smart-mixer dynamic pads. */
  positioner = gst_element_factory_make ("framepositioner", "frame_tagger");
  g_object_set (positioner, "zorder",
      G_MAXUINT - GES_TIMELINE_ELEMENT_PRIORITY (self), NULL);
  g_ptr_array_add (elements, positioner);

  /* If there's image-orientation tag, make sure the image is correctly oriented
   * before we scale it. */
  videoflip = gst_element_factory_make ("videoflip", "track-element-videoflip");
  g_object_set (videoflip, "video-direction", GST_VIDEO_ORIENTATION_AUTO, NULL);
  g_ptr_array_add (elements, videoflip);

  if (needs_converters) {
    g_ptr_array_add (elements, gst_element_factory_make ("videoscale",
            "track-element-videoscale"));
    g_ptr_array_add (elements, gst_element_factory_make ("videoconvert",
            "track-element-videoconvert"));
  }
  g_ptr_array_add (elements, gst_element_factory_make ("videorate",
          "track-element-videorate"));
  capsfilter =
      gst_element_factory_make ("capsfilter", "track-element-capsfilter");
  g_ptr_array_add (elements, capsfilter);

  ges_frame_positioner_set_source_and_filter (GST_FRAME_POSITIONNER
      (positioner), trksrc, capsfilter);

  ges_track_element_add_children_props (trksrc, positioner, NULL, NULL,
      positioner_props);
  ges_track_element_add_children_props (trksrc, videoflip, NULL, NULL,
      videoflip_props);

  deinterlace = gst_element_factory_make ("deinterlace", "deinterlace");
  if (deinterlace == NULL) {
    post_missing_element_message (ges_track_element_get_nleobject (trksrc),
        "deinterlace");

    GST_ELEMENT_WARNING (ges_track_element_get_nleobject (trksrc), CORE,
        MISSING_PLUGIN,
        ("Missing element '%s' - check your GStreamer installation.",
            "deinterlace"), ("deinterlacing won't work"));
  } else {
    g_ptr_array_add (elements, deinterlace);
    ges_track_element_add_children_props (trksrc, deinterlace, NULL, NULL,
        deinterlace_props);
  }

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
  GESVideoSourceClass *source_class = GES_VIDEO_SOURCE_GET_CLASS (trksrc);
  GESVideoSource *self;
  gboolean needs_converters = TRUE;
  GPtrArray *elements;

  if (!source_class->create_source)
    return NULL;

  sub_element = source_class->create_source (trksrc);

  self = (GESVideoSource *) trksrc;
  if (source_class->ABI.abi.needs_converters)
    needs_converters = source_class->ABI.abi.needs_converters (self);

  elements = g_ptr_array_new ();
  g_assert (source_class->ABI.abi.create_filters);
  if (!source_class->ABI.abi.create_filters (self, elements, needs_converters)) {
    g_ptr_array_free (elements, TRUE);

    return NULL;
  }

  topbin = ges_source_create_topbin ("videosrcbin", sub_element, elements);
  g_ptr_array_free (elements, TRUE);

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

  track_element_class->nleobject_factorytype = "nlesource";
  track_element_class->create_element = ges_video_source_create_element;
  track_element_class->ABI.abi.default_track_type = GES_TRACK_TYPE_VIDEO;

  video_source_class->create_source = NULL;
  video_source_class->ABI.abi.create_filters = ges_video_source_create_filters;
}

static void
ges_video_source_init (GESVideoSource * self)
{
  if (g_once_init_enter (&parent_extractable_iface)) {
    GESExtractableInterface *iface, *parent_iface;

    iface =
        G_TYPE_INSTANCE_GET_INTERFACE (self, GES_TYPE_EXTRACTABLE,
        GESExtractableInterface);
    parent_iface = g_type_interface_peek_parent (iface);
    g_once_init_leave (&parent_extractable_iface, parent_iface);
  }

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
