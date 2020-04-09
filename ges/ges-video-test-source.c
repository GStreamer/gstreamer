/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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

  gboolean use_overlay;
  GstElement *overlay;
  GstPad *is_passthrough_pad;
  GstPad *os_passthrough_pad;
  GstPad *is_overlay_pad;
  GstPad *os_overlay_pad;

  GstElement *capsfilter;
};

G_DEFINE_TYPE_WITH_PRIVATE (GESVideoTestSource, ges_video_test_source,
    GES_TYPE_VIDEO_SOURCE);

static GstElement *ges_video_test_source_create_source (GESTrackElement * self);

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

enum
{
  PROP_0,
  PROP_USE_TIME_OVERLAY,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static void
get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESVideoTestSource *self = GES_VIDEO_TEST_SOURCE (object);

  switch (property_id) {
    case PROP_USE_TIME_OVERLAY:
      g_value_set_boolean (value, self->priv->use_overlay);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESVideoTestSource *self = GES_VIDEO_TEST_SOURCE (object);

  switch (property_id) {
    case PROP_USE_TIME_OVERLAY:
    {
      GstElement *os, *is;
      gboolean used_overlay = self->priv->use_overlay;

      if (!self->priv->overlay) {
        GST_ERROR_OBJECT (object, "Overlaying is disabled, you are probably"
            "missing some GStreamer plugin");
        return;
      }

      self->priv->use_overlay = g_value_get_boolean (value);
      if (used_overlay == self->priv->use_overlay)
        return;

      is = GST_ELEMENT (gst_object_get_parent (GST_OBJECT (self->
                  priv->is_overlay_pad)));

      if (!is)
        return;

      os = GST_ELEMENT (gst_object_get_parent (GST_OBJECT (self->
                  priv->os_overlay_pad)));
      if (!os) {
        gst_object_unref (is);
        return;
      }

      g_object_set (is, "active-pad",
          self->priv->use_overlay ? self->priv->is_overlay_pad : self->
          priv->is_passthrough_pad, NULL);
      g_object_set (os, "active-pad",
          self->priv->use_overlay ? self->priv->os_overlay_pad : self->
          priv->os_passthrough_pad, NULL);

      gst_object_unref (is);
      gst_object_unref (os);

      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
_set_child_property (GESTimelineElement * self, GObject * child,
    GParamSpec * pspec, GValue * value)
{
  GstElementFactory *f =
      GST_IS_ELEMENT (child) ? gst_element_get_factory (GST_ELEMENT (child)) :
      NULL;

  if (!f || g_strcmp0 (GST_OBJECT_NAME (f), "timeoverlay"))
    goto done;

  GST_INFO_OBJECT (self, "Activating time overlay as time mode is being set");
  g_object_set (self, "use-time-overlay", TRUE, NULL);

done:
  GES_TIMELINE_ELEMENT_CLASS (ges_video_test_source_parent_class)
      ->set_child_property (self, child, pspec, value);
}

static gboolean
_set_parent (GESTimelineElement * element, GESTimelineElement * parent)
{
  GESVideoTestSource *self = GES_VIDEO_TEST_SOURCE (element);


  if (parent) {
    gint width, height, fps_n, fps_d;
    GstCaps *caps;

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
  }

  return TRUE;
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
  GESVideoTestSourcePrivate *priv = GES_VIDEO_TEST_SOURCE (object)->priv;

  gst_clear_object (&priv->is_overlay_pad);
  gst_clear_object (&priv->is_passthrough_pad);
  gst_clear_object (&priv->os_overlay_pad);
  gst_clear_object (&priv->os_passthrough_pad);

  G_OBJECT_CLASS (ges_video_test_source_parent_class)->dispose (object);
}

static void
ges_video_test_source_class_init (GESVideoTestSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESVideoSourceClass *source_class = GES_VIDEO_SOURCE_CLASS (klass);

  source_class->create_source = ges_video_test_source_create_source;
  source_class->ABI.abi.get_natural_size = get_natural_size;

  /**
   * GESVideoTestSource:use-overlay:
   *
   * Whether to overlay the test video source with a clock time.
   * This property is also registered as a child property for the video
   * test source, so you can set it like any other child property.
   *
   * The properties of the corresponding #timeoverlay are also registered
   * as children properties. If you set any child property from the underlying
   * `timeoverlay`, this property will be set to %TRUE by default.
   */
  properties[PROP_USE_TIME_OVERLAY] =
      g_param_spec_boolean ("use-time-overlay", "Use-time-overlay",
      "Use time overlay, setting a child property corresponding to"
      "GstTimeOverlay will switch this on by default.", FALSE,
      G_PARAM_READWRITE);

  object_class->get_property = get_property;
  object_class->set_property = set_property;
  object_class->dispose = dispose;

  GES_TIMELINE_ELEMENT_CLASS (klass)->set_child_property = _set_child_property;
  GES_TIMELINE_ELEMENT_CLASS (klass)->set_parent = _set_parent;
  GES_TIMELINE_ELEMENT_CLASS (klass)->get_natural_framerate =
      _get_natural_framerate;

  g_object_class_install_properties (object_class, PROP_LAST, properties);
}

static void
ges_video_test_source_init (GESVideoTestSource * self)
{
  self->priv = ges_video_test_source_get_instance_private (self);

  self->priv->pattern = DEFAULT_VPATTERN;
}

#define CREATE_ELEMENT(var, ename)                                             \
  if (!(var = gst_element_factory_make(ename, NULL))) {                        \
    GST_ERROR_OBJECT(self, "Could not create %s", ename);                      \
    goto done;                                                                 \
  }                                                                            \
  gst_object_ref(var);

static GstElement *
ges_video_test_source_create_overlay (GESVideoTestSource * self)
{
  GstElement *bin = NULL, *os = NULL, *is = NULL, *toverlay = NULL, *tcstamper =
      NULL;
  GstPad *tmppad;

  CREATE_ELEMENT (os, "output-selector");
  CREATE_ELEMENT (is, "input-selector");
  CREATE_ELEMENT (tcstamper, "timecodestamper");
  CREATE_ELEMENT (toverlay, "timeoverlay");

  bin = gst_bin_new (NULL);

  gst_bin_add_many (GST_BIN (bin), os, is, tcstamper, toverlay, NULL);
  self->priv->os_passthrough_pad = gst_element_get_request_pad (os, "src_%u");
  self->priv->is_passthrough_pad = gst_element_get_request_pad (is, "sink_%u");

  gst_pad_link_full (self->priv->os_passthrough_pad,
      self->priv->is_passthrough_pad, GST_PAD_LINK_CHECK_NOTHING);
  gst_element_link (tcstamper, toverlay);

  self->priv->os_overlay_pad = gst_element_get_request_pad (os, "src_%u");
  tmppad = gst_element_get_static_pad (tcstamper, "sink");
  gst_pad_link_full (self->priv->os_overlay_pad, tmppad,
      GST_PAD_LINK_CHECK_NOTHING);
  gst_object_unref (tmppad);

  tmppad = gst_element_get_static_pad (toverlay, "src");
  self->priv->is_overlay_pad = gst_element_get_request_pad (is, "sink_%u");
  gst_pad_link_full (tmppad, self->priv->is_overlay_pad,
      GST_PAD_LINK_CHECK_NOTHING);
  gst_object_unref (tmppad);

  tmppad = gst_element_get_static_pad (os, "sink");
  gst_element_add_pad (GST_ELEMENT (bin), gst_ghost_pad_new ("sink", tmppad));
  gst_object_unref (tmppad);

  tmppad = gst_element_get_static_pad (is, "src");
  gst_element_add_pad (GST_ELEMENT (bin), gst_ghost_pad_new ("src", tmppad));
  gst_object_unref (tmppad);

done:
  gst_clear_object (&os);
  gst_clear_object (&is);
  gst_clear_object (&tcstamper);
  gst_clear_object (&toverlay);

  return bin;
}

static GstElement *
ges_video_test_source_create_source (GESTrackElement * element)
{
  GstCaps *caps;
  gint pattern;
  GstElement *testsrc, *res;
  const gchar *props[] =
      { "pattern", "background-color", "foreground-color", NULL };
  GPtrArray *elements;
  GESVideoTestSource *self = GES_VIDEO_TEST_SOURCE (element);

  g_assert (!GES_TIMELINE_ELEMENT_PARENT (element));
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

  self->priv->overlay = ges_video_test_source_create_overlay (self);
  if (self->priv->overlay) {
    const gchar *overlay_props[] =
        { "time-mode", "text-y", "text-x", "text-width", "test-height",
      "halignment", "valignment", "font-desc", NULL
    };

    ges_track_element_add_children_props (element, self->priv->overlay, NULL,
        NULL, overlay_props);
    ges_timeline_element_add_child_property (GES_TIMELINE_ELEMENT (self),
        properties[PROP_USE_TIME_OVERLAY], G_OBJECT (self));
    g_ptr_array_add (elements, self->priv->overlay);
  }

  ges_track_element_add_children_props (element, testsrc, NULL, NULL, props);

  res = ges_source_create_topbin ("videotestsrc", testsrc, elements);
  g_ptr_array_free (elements, TRUE);

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

/* ges_video_test_source_new:
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
