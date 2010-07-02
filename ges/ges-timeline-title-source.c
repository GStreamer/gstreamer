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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:ges-timeline-title-source
 * @short_description: Render stand-alone titles in  GESTimelineLayer.
 * 
 * Renders the given text in the specified font, at specified position, and
 * with the specified background pattern.
 *
 * Note: creates a silent #GESTrackAudioTestSource to fill audio tracks
 */

#include "ges-internal.h"
#include "ges-timeline-title-source.h"
#include "ges-timeline-source.h"
#include "ges-track-object.h"
#include "ges-track-title-source.h"
#include "ges-track-audio-test-source.h"
#include <string.h>

G_DEFINE_TYPE (GESTimelineTitleSource, ges_tl_title_src,
    GES_TYPE_TIMELINE_SOURCE);

#define DEFAULT_TEXT ""
#define DEFAULT_FONT_DESC "Serif 36"
#define GES_TIMELINE_TITLE_SRC_VALIGN_TYPE (ges_timeline_title_source_valign_get_type())
#define GES_TIMELINE_TITLE_SRC_HALIGN_TYPE (ges_timeline_title_source_halign_get_type())

enum
{
  PROP_0,
  PROP_MUTE,
  PROP_TEXT,
  PROP_FONT_DESC,
  PROP_HALIGNMENT,
  PROP_VALIGNMENT,
};

static void
ges_tl_title_src_set_mute (GESTimelineTitleSource * self, gboolean mute);

static void
ges_tl_title_src_set_text (GESTimelineTitleSource * self, const gchar * text);

static void
ges_tl_title_src_set_font_desc (GESTimelineTitleSource * self, const gchar *
    font_desc);

static void
ges_tl_title_src_set_valign (GESTimelineTitleSource * self, GESTextVAlign);

static void
ges_tl_title_src_set_halign (GESTimelineTitleSource * self, GESTextHAlign);

static GESTrackObject
    * ges_tl_title_src_create_track_object (GESTimelineObject * obj,
    GESTrack * track);

static void
ges_tl_title_src_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelineTitleSource *tfs = GES_TIMELINE_TITLE_SOURCE (object);

  switch (property_id) {
    case PROP_MUTE:
      g_value_set_boolean (value, tfs->mute);
      break;
    case PROP_TEXT:
      g_value_set_string (value, tfs->text);
      break;
    case PROP_FONT_DESC:
      g_value_set_string (value, tfs->font_desc);
      break;
    case PROP_HALIGNMENT:
      g_value_set_enum (value, tfs->halign);
      break;
    case PROP_VALIGNMENT:
      g_value_set_enum (value, tfs->valign);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_tl_title_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelineTitleSource *tfs = GES_TIMELINE_TITLE_SOURCE (object);

  switch (property_id) {
    case PROP_MUTE:
      ges_tl_title_src_set_mute (tfs, g_value_get_boolean (value));
      break;
    case PROP_TEXT:
      ges_tl_title_src_set_text (tfs, g_value_get_string (value));
      break;
    case PROP_FONT_DESC:
      ges_tl_title_src_set_font_desc (tfs, g_value_get_string (value));
      break;
    case PROP_HALIGNMENT:
      ges_tl_title_src_set_halign (tfs, g_value_get_enum (value));
      break;
    case PROP_VALIGNMENT:
      ges_tl_title_src_set_valign (tfs, g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_tl_title_src_dispose (GObject * object)
{
  GESTimelineTitleSource *self = GES_TIMELINE_TITLE_SOURCE (object);

  if (self->text)
    g_free (self->text);
  if (self->font_desc)
    g_free (self->font_desc);

  G_OBJECT_CLASS (ges_tl_title_src_parent_class)->dispose (object);
}

static void
ges_tl_title_src_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_tl_title_src_parent_class)->finalize (object);
}

static void
ges_tl_title_src_class_init (GESTimelineTitleSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *timobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

  object_class->get_property = ges_tl_title_src_get_property;
  object_class->set_property = ges_tl_title_src_set_property;
  object_class->dispose = ges_tl_title_src_dispose;
  object_class->finalize = ges_tl_title_src_finalize;

  /**
   * GESTimelineTitleSource:text:
   *
   * The text to diplay
   */
  g_object_class_install_property (object_class, PROP_TEXT,
      g_param_spec_string ("text", "Text", "The text to display",
          DEFAULT_TEXT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTimelineTitleSource:font-desc:
   *
   * Pango font description string
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FONT_DESC,
      g_param_spec_string ("font-desc", "font description",
          "Pango font description of font to be used for rendering. "
          "See documentation of pango_font_description_from_string "
          "for syntax.", DEFAULT_FONT_DESC,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GESTimelineTitleSource:valignment:
   *
   * Vertical alignent of the text
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VALIGNMENT,
      g_param_spec_enum ("valignment", "vertical alignment",
          "Vertical alignment of the text", GES_TEXT_VALIGN_TYPE,
          DEFAULT_VALIGNMENT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  /**
   * GESTimelineTitleSource:halignment:
   *
   * Horizontal alignment of the text
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HALIGNMENT,
      g_param_spec_enum ("halignment", "horizontal alignment",
          "Horizontal alignment of the text",
          GES_TEXT_HALIGN_TYPE, DEFAULT_HALIGNMENT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  /**
   * GESTimelineTitleSource:mute:
   *
   * Whether the sound will be played or not.
   */
  g_object_class_install_property (object_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute audio track",
          FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  timobj_class->create_track_object = ges_tl_title_src_create_track_object;
  timobj_class->need_fill_track = FALSE;
}

static void
ges_tl_title_src_init (GESTimelineTitleSource * self)
{
  GES_TIMELINE_OBJECT (self)->duration = 0;
  self->text = NULL;
  self->text = NULL;
  self->halign = DEFAULT_HALIGNMENT;
  self->valign = DEFAULT_VALIGNMENT;
}

static void
ges_tl_title_src_set_text (GESTimelineTitleSource * self, const gchar * text)
{
  GList *tmp;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG ("self:%p, text:%s", self, text);

  if (self->text)
    g_free (self->text);

  self->text = g_strdup (text);

  for (tmp = object->trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (trackobject->track->type == GES_TRACK_TYPE_VIDEO)
      ges_track_title_source_set_text (GES_TRACK_TITLE_SOURCE
          (trackobject), self->text);
  }
}

static void
ges_tl_title_src_set_font_desc (GESTimelineTitleSource * self, const gchar *
    font_desc)
{
  GList *tmp;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG ("self:%p, font_desc:%s", self, font_desc);

  if (self->font_desc)
    g_free (self->font_desc);

  self->font_desc = g_strdup (font_desc);

  for (tmp = object->trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (trackobject->track->type == GES_TRACK_TYPE_VIDEO)
      ges_track_title_source_set_font_desc (GES_TRACK_TITLE_SOURCE
          (trackobject), self->font_desc);
  }
}

static void
ges_tl_title_src_set_halign (GESTimelineTitleSource * self,
    GESTextHAlign halign)
{
  GList *tmp;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG ("self:%p, halign:%d", self, halign);

  self->halign = halign;

  for (tmp = object->trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (trackobject->track->type == GES_TRACK_TYPE_VIDEO)
      ges_track_title_source_set_halignment (GES_TRACK_TITLE_SOURCE
          (trackobject), self->halign);
  }
}

static void
ges_tl_title_src_set_valign (GESTimelineTitleSource * self,
    GESTextVAlign valign)
{
  GList *tmp;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG ("self:%p, valign:%d", self, valign);

  self->valign = valign;

  for (tmp = object->trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (trackobject->track->type == GES_TRACK_TYPE_VIDEO)
      ges_track_title_source_set_valignment (GES_TRACK_TITLE_SOURCE
          (trackobject), self->valign);
  }
}

static void
ges_tl_title_src_set_mute (GESTimelineTitleSource * self, gboolean mute)
{
  GList *tmp;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG ("self:%p, mute:%d", self, mute);

  self->mute = mute;

  /* Go over tracked objects, and update 'active' status on all audio objects */
  for (tmp = object->trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (trackobject->track->type == GES_TRACK_TYPE_AUDIO)
      ges_track_object_set_active (trackobject, !mute);
  }
}

static GESTrackObject *
ges_tl_title_src_create_track_object (GESTimelineObject * obj, GESTrack * track)
{

  GESTimelineTitleSource *tfs = (GESTimelineTitleSource *) obj;
  GESTrackObject *res = NULL;

  GST_DEBUG ("Creating a GESTrackTitleSource");

  if (track->type == GES_TRACK_TYPE_VIDEO) {
    res = (GESTrackObject *) ges_track_title_source_new ();
    GST_DEBUG ("Setting text property");
    ges_track_title_source_set_text ((GESTrackTitleSource *) res, tfs->text);
    ges_track_title_source_set_font_desc ((GESTrackTitleSource *) res,
        tfs->font_desc);
    ges_track_title_source_set_halignment ((GESTrackTitleSource *) res,
        tfs->halign);
    ges_track_title_source_set_valignment ((GESTrackTitleSource *) res,
        tfs->valign);
  }

  else if (track->type == GES_TRACK_TYPE_AUDIO) {
    res = (GESTrackObject *) ges_track_audio_test_source_new ();
    if (tfs->mute)
      ges_track_object_set_active (res, FALSE);
  }

  else {
    res = NULL;
  }

  return res;
}

/**
 * ges_timeline_titlesource_new:
 *
 * Creates a new #GESTimelineTitleSource
 *
 * Returns: The newly created #GESTimelineTitleSource, or NULL if there was an
 * error.
 */
GESTimelineTitleSource *
ges_timeline_title_source_new (void)
{
  /* FIXME : Check for validity/existence of URI */
  return g_object_new (GES_TYPE_TIMELINE_TITLE_SOURCE, NULL);
}
