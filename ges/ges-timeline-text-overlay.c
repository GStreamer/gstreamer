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
 * SECTION:ges-timeline-text-overlay
 * @short_description: Render text onto another stream in a #GESTimelineLayer
 * 
 * Renders text onto the next lower priority stream using textrender.
 */

#include "ges-internal.h"
#include "ges-timeline-text-overlay.h"
#include "ges-track-object.h"
#include "ges-track-text-overlay.h"
#include <string.h>

G_DEFINE_TYPE (GESTimelineTextOverlay, ges_tl_text_overlay,
    GES_TYPE_TIMELINE_OVERLAY);

#define DEFAULT_PROP_TEXT ""
#define DEFAULT_PROP_FONT_DESC "Serif 36"
#define DEFAULT_PROP_VALIGNMENT GES_TEXT_VALIGN_BASELINE
#define DEFAULT_PROP_HALIGNMENT GES_TEXT_HALIGN_CENTER
#
enum
{
  PROP_0,
  PROP_TEXT,
  PROP_FONT_DESC,
  PROP_HALIGNMENT,
  PROP_VALIGNMENT,
};


static void
ges_tl_text_overlay_set_text (GESTimelineTextOverlay * self,
    const gchar * text);

static void
ges_tl_text_overlay_set_font_desc (GESTimelineTextOverlay * self, const gchar *
    font_desc);

static void
ges_tl_text_overlay_set_valign (GESTimelineTextOverlay * self,
    GESTextVAlign valign);

static void
ges_tl_text_overlay_set_halign (GESTimelineTextOverlay * self,
    GESTextHAlign halign);

static GESTrackObject
    * ges_tl_text_overlay_create_track_object (GESTimelineObject * obj,
    GESTrack * track);

static void
ges_tl_text_overlay_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelineTextOverlay *tfs = GES_TIMELINE_TEXT_OVERLAY (object);

  switch (property_id) {
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
ges_tl_text_overlay_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelineTextOverlay *tfs = GES_TIMELINE_TEXT_OVERLAY (object);

  switch (property_id) {
    case PROP_TEXT:
      ges_tl_text_overlay_set_text (tfs, g_value_get_string (value));
      break;
    case PROP_FONT_DESC:
      ges_tl_text_overlay_set_font_desc (tfs, g_value_get_string (value));
      break;
    case PROP_HALIGNMENT:
      ges_tl_text_overlay_set_halign (tfs, g_value_get_enum (value));
      break;
    case PROP_VALIGNMENT:
      ges_tl_text_overlay_set_valign (tfs, g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_tl_text_overlay_dispose (GObject * object)
{
  GESTimelineTextOverlay *self = GES_TIMELINE_TEXT_OVERLAY (object);

  if (self->text)
    g_free (self->text);
  if (self->font_desc)
    g_free (self->font_desc);

  G_OBJECT_CLASS (ges_tl_text_overlay_parent_class)->dispose (object);
}

static void
ges_tl_text_overlay_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_tl_text_overlay_parent_class)->finalize (object);
}

static void
ges_tl_text_overlay_class_init (GESTimelineTextOverlayClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *timobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

  object_class->get_property = ges_tl_text_overlay_get_property;
  object_class->set_property = ges_tl_text_overlay_set_property;
  object_class->dispose = ges_tl_text_overlay_dispose;
  object_class->finalize = ges_tl_text_overlay_finalize;

  /**
   * GESTimelineTextOverlay:text
   *
   * The text to diplay
   */

  g_object_class_install_property (object_class, PROP_TEXT,
      g_param_spec_string ("text", "Text", "The text to display",
          DEFAULT_PROP_TEXT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTimelineTextOverlay:font-dec
   *
   * Pango font description string
   */

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FONT_DESC,
      g_param_spec_string ("font-desc", "font description",
          "Pango font description of font to be used for rendering. "
          "See documentation of pango_font_description_from_string "
          "for syntax.", DEFAULT_PROP_FONT_DESC,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GESTimelineTextOverlay:valignment
   *
   * Vertical alignent of the text
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VALIGNMENT,
      g_param_spec_enum ("valignment", "vertical alignment",
          "Vertical alignment of the text", GES_TEXT_VALIGN_TYPE,
          DEFAULT_PROP_VALIGNMENT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
          G_PARAM_STATIC_STRINGS));
  /**
   * GESTimelineTextOverlay:halignment
   *
   * Horizontal alignment of the text
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HALIGNMENT,
      g_param_spec_enum ("halignment", "horizontal alignment",
          "Horizontal alignment of the text",
          GES_TEXT_HALIGN_TYPE, DEFAULT_PROP_HALIGNMENT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  timobj_class->create_track_object = ges_tl_text_overlay_create_track_object;
  timobj_class->need_fill_track = FALSE;
}

static void
ges_tl_text_overlay_init (GESTimelineTextOverlay * self)
{
  GES_TIMELINE_OBJECT (self)->duration = 0;
  self->text = NULL;
  self->text = NULL;
  self->halign = DEFAULT_PROP_HALIGNMENT;
  self->valign = DEFAULT_PROP_VALIGNMENT;
}

static void
ges_tl_text_overlay_set_text (GESTimelineTextOverlay * self, const gchar * text)
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
      ges_track_text_overlay_set_text (GES_TRACK_TEXT_OVERLAY
          (trackobject), self->text);
  }
}

static void
ges_tl_text_overlay_set_font_desc (GESTimelineTextOverlay * self, const gchar *
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
      ges_track_text_overlay_set_font_desc (GES_TRACK_TEXT_OVERLAY
          (trackobject), self->font_desc);
  }

}

static void
ges_tl_text_overlay_set_halign (GESTimelineTextOverlay * self,
    GESTextHAlign halign)
{
  GList *tmp;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG ("self:%p, halign:%d", self, halign);

  self->halign = halign;


  for (tmp = object->trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (trackobject->track->type == GES_TRACK_TYPE_VIDEO)
      ges_track_text_overlay_set_halignment (GES_TRACK_TEXT_OVERLAY
          (trackobject), self->halign);
  }

}

static void
ges_tl_text_overlay_set_valign (GESTimelineTextOverlay * self,
    GESTextVAlign valign)
{
  GList *tmp;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG ("self:%p, valign:%d", self, valign);

  self->valign = valign;

  for (tmp = object->trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (trackobject->track->type == GES_TRACK_TYPE_VIDEO)
      ges_track_text_overlay_set_valignment (GES_TRACK_TEXT_OVERLAY
          (trackobject), self->valign);
  }

}

static GESTrackObject *
ges_tl_text_overlay_create_track_object (GESTimelineObject * obj,
    GESTrack * track)
{

  GESTimelineTextOverlay *tfs = (GESTimelineTextOverlay *) obj;
  GESTrackObject *res = NULL;

  GST_DEBUG ("Creating a GESTrackOverlay");

  if (track->type == GES_TRACK_TYPE_VIDEO) {
    res = (GESTrackObject *) ges_track_text_overlay_new ();
    GST_DEBUG ("Setting text property");
    ges_track_text_overlay_set_text ((GESTrackTextOverlay *) res, tfs->text);
    ges_track_text_overlay_set_font_desc ((GESTrackTextOverlay *) res,
        tfs->font_desc);
    ges_track_text_overlay_set_halignment ((GESTrackTextOverlay *) res,
        tfs->halign);
    ges_track_text_overlay_set_valignment ((GESTrackTextOverlay *) res,
        tfs->valign);
  }

  else {
    res = NULL;
  }

  return res;
}

/**
 * ges_timeline_titlesource_new:
 *
 * Creates a new #GESTimelineTextOverlay
 *
 * Returns: The newly created #GESTimelineTextOverlay, or NULL if there was an
 * error.
 */
GESTimelineTextOverlay *
ges_timeline_text_overlay_new (void)
{
  /* FIXME : Check for validity/existence of URI */
  return g_object_new (GES_TYPE_TIMELINE_TEXT_OVERLAY, NULL);
}
