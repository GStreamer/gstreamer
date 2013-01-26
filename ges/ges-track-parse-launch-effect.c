/* GStreamer Editing Services
 * Copyright (C) 2010 Thibault Saunier <thibault.saunier@collabora.co.uk>
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
 * SECTION:ges-track-parse-launch-effect
 * @short_description: adds an effect build from a parse-launch style 
 * bin description to a stream in a #GESSourceClip or a #GESTimelineLayer
 */

#include "ges-internal.h"
#include "ges-extractable.h"
#include "ges-track-element.h"
#include "ges-track-effect.h"
#include "ges-track-parse-launch-effect.h"

static void ges_extractable_interface_init (GESExtractableInterface * iface);

G_DEFINE_TYPE_WITH_CODE (GESTrackParseLaunchEffect,
    ges_track_parse_launch_effect, GES_TYPE_TRACK_EFFECT,
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));

static void ges_track_parse_launch_effect_dispose (GObject * object);
static void ges_track_parse_launch_effect_finalize (GObject * object);
static GstElement *ges_track_parse_launch_effect_create_element (GESTrackElement
    * self);

struct _GESTrackParseLaunchEffectPrivate
{
  gchar *bin_description;
};

enum
{
  PROP_0,
  PROP_BIN_DESCRIPTION,
};

static gchar *
extractable_check_id (GType type, const gchar * id, GError ** error)
{
  GstElement *effect = gst_parse_bin_from_description (id, TRUE, error);

  if (effect == NULL)
    return NULL;

  gst_object_unref (effect);
  return g_strdup (id);
}

static GParameter *
extractable_get_parameters_from_id (const gchar * id, guint * n_params)
{
  GParameter *params = g_new0 (GParameter, 2);

  params[0].name = "bin-description";
  g_value_init (&params[0].value, G_TYPE_STRING);
  g_value_set_string (&params[0].value, id);

  *n_params = 1;

  return params;
}

static gchar *
extractable_get_id (GESExtractable * self)
{
  return g_strdup (GES_TRACK_PARSE_LAUNCH_EFFECT (self)->priv->bin_description);
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->check_id = (GESExtractableCheckId) extractable_check_id;
  iface->get_parameters_from_id = extractable_get_parameters_from_id;
  iface->get_id = extractable_get_id;
}

static void
ges_track_parse_launch_effect_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GESTrackParseLaunchEffectPrivate *priv =
      GES_TRACK_PARSE_LAUNCH_EFFECT (object)->priv;

  switch (property_id) {
    case PROP_BIN_DESCRIPTION:
      g_value_set_string (value, priv->bin_description);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_parse_launch_effect_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  GESTrackParseLaunchEffect *self = GES_TRACK_PARSE_LAUNCH_EFFECT (object);

  switch (property_id) {
    case PROP_BIN_DESCRIPTION:
      self->priv->bin_description = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_parse_launch_effect_class_init (GESTrackParseLaunchEffectClass *
    klass)
{
  GObjectClass *object_class;
  GESTrackElementClass *obj_bg_class;

  object_class = G_OBJECT_CLASS (klass);
  obj_bg_class = GES_TRACK_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTrackParseLaunchEffectPrivate));

  object_class->get_property = ges_track_parse_launch_effect_get_property;
  object_class->set_property = ges_track_parse_launch_effect_set_property;
  object_class->dispose = ges_track_parse_launch_effect_dispose;
  object_class->finalize = ges_track_parse_launch_effect_finalize;

  obj_bg_class->create_element = ges_track_parse_launch_effect_create_element;

  /**
   * GESTrackParseLaunchEffect:bin-description:
   *
   * The description of the effect bin with a gst-launch-style
   * pipeline description.
   *
   * Example: "videobalance saturation=1.5 hue=+0.5"
   */
  g_object_class_install_property (object_class, PROP_BIN_DESCRIPTION,
      g_param_spec_string ("bin-description",
          "bin description",
          "Bin description of the effect",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
ges_track_parse_launch_effect_init (GESTrackParseLaunchEffect * self)
{
  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self, GES_TYPE_TRACK_PARSE_LAUNCH_EFFECT,
      GESTrackParseLaunchEffectPrivate);
}

static void
ges_track_parse_launch_effect_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_track_parse_launch_effect_parent_class)->dispose (object);
}

static void
ges_track_parse_launch_effect_finalize (GObject * object)
{
  GESTrackParseLaunchEffect *self = GES_TRACK_PARSE_LAUNCH_EFFECT (object);

  if (self->priv->bin_description)
    g_free (self->priv->bin_description);

  G_OBJECT_CLASS (ges_track_parse_launch_effect_parent_class)->finalize
      (object);
}

static GstElement *
ges_track_parse_launch_effect_create_element (GESTrackElement * object)
{
  GstElement *effect;
  gchar *bin_desc;

  GError *error = NULL;
  GESTrackParseLaunchEffect *self = GES_TRACK_PARSE_LAUNCH_EFFECT (object);
  GESTrack *track = ges_track_element_get_track (object);

  if (!track) {
    GST_WARNING
        ("The object %p should be in a Track for the element to be created",
        object);
    return NULL;
  }

  if (track->type == GES_TRACK_TYPE_VIDEO) {
    bin_desc = g_strconcat ("videoconvert name=pre_video_convert ! ",
        self->priv->bin_description, " ! videoconvert name=post_video_convert",
        NULL);
  } else if (track->type == GES_TRACK_TYPE_AUDIO) {
    bin_desc =
        g_strconcat ("audioconvert ! audioresample !",
        self->priv->bin_description, NULL);
  } else {
    GST_DEBUG ("Track type not supported");
    return NULL;
  }

  effect = gst_parse_bin_from_description (bin_desc, TRUE, &error);

  g_free (bin_desc);

  if (error != NULL) {
    GST_ERROR ("An error occured while creating the GstElement: %s",
        error->message);
    g_error_free (error);
    return NULL;
  }

  GST_DEBUG ("Created effect %p", effect);

  return effect;
}

/**
 * ges_track_parse_launch_effect_new:
 * @bin_description: The gst-launch like bin description of the effect
 *
 * Creates a new #GESTrackParseLaunchEffect from the description of the bin.
 *
 * Returns: a newly created #GESTrackParseLaunchEffect, or %NULL if something went
 * wrong.
 *
 * Since: 0.10.2
 */
GESTrackParseLaunchEffect *
ges_track_parse_launch_effect_new (const gchar * bin_description)
{
  return g_object_new (GES_TYPE_TRACK_PARSE_LAUNCH_EFFECT, "bin-description",
      bin_description, NULL);
}
