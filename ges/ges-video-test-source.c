/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
 * Copyright (C) 2020 Igalia S.L
 *     Author: 2020 Thibault Saunier <tsaunier@igalia.com>
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
 * SECTION:gesvideotestsource
 * @title: GESVideoTestSource
 * @short_description: produce solid colors and patterns, possibly with a time
 * overlay.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-video-test-source.h"

#define DEFAULT_VPATTERN GES_VIDEO_TEST_PATTERN_SMPTE

struct _GESVideoTestSourcePrivate
{
  GESVideoTestPattern pattern;

  GstElement *capsfilter;
};

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->check_id = ges_test_source_asset_check_id;
  iface->asset_type = GES_TYPE_TRACK_ELEMENT_ASSET;
}

static GstStructure *
ges_video_test_source_asset_get_config (GESAsset * asset)
{
  const gchar *id = ges_asset_get_id (asset);
  if (g_strcmp0 (id, g_type_name (ges_asset_get_extractable_type (asset))))
    return gst_structure_from_string (ges_asset_get_id (asset), NULL);

  return NULL;
}

G_DEFINE_TYPE_WITH_CODE (GESVideoTestSource, ges_video_test_source,
    GES_TYPE_VIDEO_SOURCE, G_ADD_PRIVATE (GESVideoTestSource)
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));

static GstElement *ges_video_test_source_create_source (GESSource * source);

static gboolean
get_natural_size (GESVideoSource * source, gint * width, gint * height)
{
  gboolean res = FALSE;
  GESTimelineElement *parent = GES_TIMELINE_ELEMENT_PARENT (source);

  if (parent) {
    GESAsset *asset = ges_extractable_get_asset (GES_EXTRACTABLE (parent));

    if (asset)
      res = ges_test_clip_asset_get_natural_size (asset, width, height);
  }

  if (!res) {
    *width = DEFAULT_WIDTH;
    *height = DEFAULT_HEIGHT;
  }

  return TRUE;
}

static gboolean
_set_parent (GESTimelineElement * element, GESTimelineElement * parent)
{
  gint width, height, fps_n, fps_d;
  GstCaps *caps;
  GESVideoTestSource *self = GES_VIDEO_TEST_SOURCE (element);


  if (!parent)
    goto done;

  g_assert (self->priv->capsfilter);
  /* Setting the parent ourself as we need it to get the natural size */
  element->parent = parent;
  if (!ges_video_source_get_natural_size (GES_VIDEO_SOURCE (self), &width,
          &height)) {
    width = DEFAULT_WIDTH;
    height = DEFAULT_HEIGHT;
  }

  if (!ges_timeline_element_get_natural_framerate (parent, &fps_n, &fps_d)) {
    fps_n = DEFAULT_FRAMERATE_N;
    fps_d = DEFAULT_FRAMERATE_D;
  }

  caps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, fps_n, fps_d, NULL);
  g_object_set (self->priv->capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

done:
  return
      GES_TIMELINE_ELEMENT_CLASS
      (ges_video_test_source_parent_class)->set_parent (element, parent);
}

static gboolean
_get_natural_framerate (GESTimelineElement * element, gint * fps_n,
    gint * fps_d)
{
  gboolean res = FALSE;
  GESTimelineElement *parent = GES_TIMELINE_ELEMENT_PARENT (element);

  if (parent) {
    GESAsset *asset = ges_extractable_get_asset (GES_EXTRACTABLE (parent));

    if (asset) {
      res =
          ges_clip_asset_get_natural_framerate (GES_CLIP_ASSET (asset), fps_n,
          fps_d);
    }
  }

  if (!res) {
    *fps_n = DEFAULT_FRAMERATE_N;
    *fps_d = DEFAULT_FRAMERATE_D;
  }

  return TRUE;
}

static void
dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_video_test_source_parent_class)->dispose (object);
}

static void
ges_video_test_source_class_init (GESVideoTestSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESVideoSourceClass *vsource_class = GES_VIDEO_SOURCE_CLASS (klass);
  GESSourceClass *source_class = GES_SOURCE_CLASS (klass);

  source_class->create_source = ges_video_test_source_create_source;
  vsource_class->ABI.abi.get_natural_size = get_natural_size;

  object_class->dispose = dispose;

  GES_TIMELINE_ELEMENT_CLASS (klass)->set_parent = _set_parent;
  GES_TIMELINE_ELEMENT_CLASS (klass)->get_natural_framerate =
      _get_natural_framerate;
}

static void
ges_video_test_source_init (GESVideoTestSource * self)
{
  self->priv = ges_video_test_source_get_instance_private (self);

  self->priv->pattern = DEFAULT_VPATTERN;
}

static GstStructure *
ges_video_test_source_get_config (GESVideoTestSource * self)
{
  GESAsset *asset = ges_extractable_get_asset (GES_EXTRACTABLE (self));
  if (asset)
    return ges_video_test_source_asset_get_config (asset);

  return NULL;
}

static GstElement *
ges_video_test_source_create_overlay (GESVideoTestSource * self)
{
  const gchar *bindesc = NULL;
  GstStructure *config = ges_video_test_source_get_config (self);

  if (!config)
    return NULL;

  if (gst_structure_has_name (config, "time-overlay")) {
    gboolean disable_timecodestamper;

    if (gst_structure_get_boolean (config, "disable-timecodestamper",
            &disable_timecodestamper))
      bindesc = "timeoverlay";
    else
      bindesc = "timecodestamper ! timeoverlay";
  }
  gst_structure_free (config);

  if (!bindesc)
    return NULL;

  return gst_parse_bin_from_description (bindesc, TRUE, NULL);
}

static GstElement *
ges_video_test_source_create_source (GESSource * source)
{
  GstCaps *caps;
  gint pattern;
  GstElement *testsrc, *res;
  GstElement *overlay;
  const gchar *props[] =
      { "pattern", "background-color", "foreground-color", NULL };
  GPtrArray *elements;
  GESVideoTestSource *self = GES_VIDEO_TEST_SOURCE (source);
  GESTrackElement *element = GES_TRACK_ELEMENT (source);

  g_assert (!GES_TIMELINE_ELEMENT_PARENT (source));
  testsrc = gst_element_factory_make ("videotestsrc", NULL);
  self->priv->capsfilter = gst_element_factory_make ("capsfilter", NULL);
  pattern = self->priv->pattern;

  g_object_set (testsrc, "pattern", pattern, NULL);

  elements = g_ptr_array_new ();
  g_ptr_array_add (elements, self->priv->capsfilter);
  caps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, DEFAULT_WIDTH,
      "height", G_TYPE_INT, DEFAULT_HEIGHT,
      "framerate", GST_TYPE_FRACTION, DEFAULT_FRAMERATE_N, DEFAULT_FRAMERATE_D,
      NULL);
  g_object_set (self->priv->capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  overlay = ges_video_test_source_create_overlay (self);
  if (overlay) {
    const gchar *overlay_props[] =
        { "time-mode", "text-y", "text-x", "text-width", "test-height",
      "halignment", "valignment", "font-desc", NULL
    };

    ges_track_element_add_children_props (element, overlay, NULL,
        NULL, overlay_props);
    g_ptr_array_add (elements, overlay);
  }

  ges_track_element_add_children_props (element, testsrc, NULL, NULL, props);

  res = ges_source_create_topbin (GES_SOURCE (element), "videotestsrc", testsrc,
      elements);

  return res;
}

/**
 * ges_video_test_source_set_pattern:
 * @self: a #GESVideoTestSource
 * @pattern: a #GESVideoTestPattern
 *
 * Sets the source to use the given @pattern.
 */
void
ges_video_test_source_set_pattern (GESVideoTestSource
    * self, GESVideoTestPattern pattern)
{
  GstElement *element =
      ges_track_element_get_element (GES_TRACK_ELEMENT (self));

  self->priv->pattern = pattern;

  if (element) {
    GValue val = { 0 };

    g_value_init (&val, GES_VIDEO_TEST_PATTERN_TYPE);
    g_value_set_enum (&val, pattern);
    ges_track_element_set_child_property (GES_TRACK_ELEMENT (self), "pattern",
        &val);
  }
}

/**
 * ges_video_test_source_get_pattern:
 * @source: a #GESVideoTestPattern
 *
 * Get the video pattern used by the @source.
 *
 * Returns: The video pattern used by the @source.
 */
GESVideoTestPattern
ges_video_test_source_get_pattern (GESVideoTestSource * source)
{
  GValue val = { 0 };

  ges_track_element_get_child_property (GES_TRACK_ELEMENT (source), "pattern",
      &val);
  return g_value_get_enum (&val);
}

/**
 * ges_video_test_source_new:
 *
 * Creates a new #GESVideoTestSource.
 *
 * Returns: (transfer floating) (nullable): The newly created
 * #GESVideoTestSource, or %NULL if there was an error.
 */
GESVideoTestSource *
ges_video_test_source_new (void)
{
  GESVideoTestSource *res;
  GESAsset *asset = ges_asset_request (GES_TYPE_VIDEO_TEST_SOURCE, NULL, NULL);

  res = GES_VIDEO_TEST_SOURCE (ges_asset_extract (asset, NULL));
  gst_object_unref (asset);

  return res;
}
