/* GStreamer Editing Services
 * Copyright (C) 2011 Thibault Saunier <thibault.saunier@collabora.co.uk>
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

/**
 * SECTION: ges-timeline-parse-launch-effect
 * @short_description: An effect created by parse-launch style bin descriptions
 * in a #GESTimelineLayer
 *
 * Should be used mainly for testing purposes.
 *
 * The effect will be applied on the sources that have lower priorities
 * (higher number) between the inpoint and the end of it.
 *
 * In a #GESSimpleTimelineLayer, the priorities will be set for you but if
 * you use another type of #GESTimelineLayer, you will have to handle it
 * yourself.
 */

#include <ges/ges.h>
#include "ges-internal.h"
#include "ges-types.h"

G_DEFINE_TYPE (GESTimelineParseLaunchEffect, ges_timeline_parse_launch_effect,
    GES_TYPE_TIMELINE_EFFECT);

struct _GESTimelineParseLaunchEffectPrivate
{
  gchar *video_bin_description;
  gchar *audio_bin_description;
};

enum
{
  PROP_0,
  PROP_VIDEO_BIN_DESCRIPTION,
  PROP_AUDIO_BIN_DESCRIPTION,
};

static GESTrackObject
    * ges_tl_parse_launch_effect_create_track_obj (GESTimelineObject * self,
    GESTrack * track);

static void
ges_timeline_parse_launch_effect_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GESTimelineParseLaunchEffectPrivate *priv =
      GES_TIMELINE_PARSE_LAUNCH_EFFECT (object)->priv;

  switch (property_id) {
    case PROP_VIDEO_BIN_DESCRIPTION:
      g_value_set_string (value, priv->video_bin_description);
      break;
    case PROP_AUDIO_BIN_DESCRIPTION:
      g_value_set_string (value, priv->audio_bin_description);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_parse_launch_effect_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  GESTimelineParseLaunchEffect *self =
      GES_TIMELINE_PARSE_LAUNCH_EFFECT (object);

  switch (property_id) {
    case PROP_VIDEO_BIN_DESCRIPTION:
      self->priv->video_bin_description = g_value_dup_string (value);
      break;
    case PROP_AUDIO_BIN_DESCRIPTION:
      self->priv->audio_bin_description = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_parse_launch_effect_class_init (GESTimelineParseLaunchEffectClass *
    klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *timobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

  g_type_class_add_private (klass,
      sizeof (GESTimelineParseLaunchEffectPrivate));

  object_class->get_property = ges_timeline_parse_launch_effect_get_property;
  object_class->set_property = ges_timeline_parse_launch_effect_set_property;

  /**
   * GESTimelineParseLaunchEffect:video_bin_description:
   *
   * The description of the video track of the effect bin with a gst-launch-style
   * pipeline description. This should be used for test purposes.
   * exemple: videobalance saturation=1.5 hue=+0.5
   */
  g_object_class_install_property (object_class, PROP_VIDEO_BIN_DESCRIPTION,
      g_param_spec_string ("video-bin-description",
          "Video bin description",
          "Description of the video track of the effect",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  /**
   * GESTimelineParseLaunchEffect:audio_bin_description:
   *
   * The description of the audio track of the effect bin with a gst-launch-style
   * pipeline description. This should be used for test purposes.
   * exemple: videobalance saturation=1.5 hue=+0.5
   */
  g_object_class_install_property (object_class, PROP_AUDIO_BIN_DESCRIPTION,
      g_param_spec_string ("audio-bin-description",
          "bin description",
          "Bin description of the audio track of the effect",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  timobj_class->create_track_object =
      ges_tl_parse_launch_effect_create_track_obj;
  timobj_class->need_fill_track = FALSE;
}

static void
ges_timeline_parse_launch_effect_init (GESTimelineParseLaunchEffect * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE_PARSE_LAUNCH_EFFECT,
      GESTimelineParseLaunchEffectPrivate);

}

static GESTrackObject *
ges_tl_parse_launch_effect_create_track_obj (GESTimelineObject * self,
    GESTrack * track)
{
  GESTimelineParseLaunchEffect *effect =
      GES_TIMELINE_PARSE_LAUNCH_EFFECT (self);


  if (track->type == GES_TRACK_TYPE_VIDEO) {
    if (effect->priv->video_bin_description != NULL) {
      GST_DEBUG ("Creating a GESTrackEffect for the video track");
      return GES_TRACK_OBJECT (ges_track_parse_launch_effect_new_from_bin_desc
          (effect->priv->video_bin_description));
    }
    GST_DEBUG ("Can't create the track Object, the\
                 video_bin_description is not set");
  }
  if (track->type == GES_TRACK_TYPE_AUDIO) {
    if (effect->priv->audio_bin_description != NULL) {
      GST_DEBUG ("Creating a GESTrackEffect for the audio track");
      return GES_TRACK_OBJECT (ges_track_parse_launch_effect_new_from_bin_desc
          (effect->priv->audio_bin_description));
    }
    GST_DEBUG ("Can't create the track Object, the\
                 audio_bin_description is not set");
  }

  GST_WARNING ("Effect doesn't handle this track type");
  return NULL;
}

/**
* ges_timeline_parse_launch_effect_new_from_bin_desc:
* @video_bin_description: The gst-launch like bin description of the effect
* @audio_bin_description: The gst-launch like bin description of the effect
*
* Creates a new #GESTimelineParseLaunchEffect from the description of the bin.
*
* Returns: a newly created #GESTimelineParseLaunchEffect, or %NULL if something went
* wrong.
*/
GESTimelineParseLaunchEffect *
ges_timeline_parse_launch_effect_new (const gchar * video_bin_description,
    const gchar * audio_bin_description)
{
  return g_object_new (GES_TYPE_TIMELINE_PARSE_LAUNCH_EFFECT,
      "video-bin-description", video_bin_description,
      "audio-bin-description", audio_bin_description, NULL);
}
