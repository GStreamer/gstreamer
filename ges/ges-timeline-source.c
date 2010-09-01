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
 * SECTION:ges-timeline-source
 * @short_description: Base Class for sources of a #GESTimelineLayer
 */

#include "ges-internal.h"
#include "ges-timeline-object.h"
#include "ges-timeline-source.h"
#include "ges-track-source.h"
#include "ges-track-text-overlay.h"

/* FIXME: really need to put this in a common header file */

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

G_DEFINE_TYPE (GESTimelineSource, ges_timeline_source,
    GES_TYPE_TIMELINE_OBJECT);

static GESTrackObject
    * ges_timeline_source_create_track_object (GESTimelineObject * obj,
    GESTrack * track);

static gboolean
ges_timeline_source_create_track_objects (GESTimelineObject * obj,
    GESTrack * track);

static void
ges_timeline_source_set_text (GESTimelineSource * self, const gchar * text);

static void
ges_timeline_source_set_font_desc (GESTimelineSource * self, const gchar *
    font_desc);

static void
ges_timeline_source_set_valign (GESTimelineSource * self, GESTextVAlign valign);

static void
ges_timeline_source_set_halign (GESTimelineSource * self, GESTextHAlign halign);

static void
ges_timeline_source_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelineSource *ts = (GESTimelineSource *) object;
  switch (property_id) {
    case PROP_TEXT:
      g_value_set_string (value, ts->text);
      break;
    case PROP_FONT_DESC:
      g_value_set_string (value, ts->font_desc);
      break;
    case PROP_HALIGNMENT:
      g_value_set_enum (value, ts->halign);
      break;
    case PROP_VALIGNMENT:
      g_value_set_enum (value, ts->valign);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_source_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelineSource *ts = (GESTimelineSource *) object;
  switch (property_id) {
    case PROP_TEXT:
      ges_timeline_source_set_text (ts, g_value_get_string (value));
      break;
    case PROP_FONT_DESC:
      ges_timeline_source_set_font_desc (ts, g_value_get_string (value));
      break;
    case PROP_HALIGNMENT:
      ges_timeline_source_set_halign (ts, g_value_get_enum (value));
      break;
    case PROP_VALIGNMENT:
      ges_timeline_source_set_valign (ts, g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_source_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_timeline_source_parent_class)->dispose (object);
}

static void
ges_timeline_source_finalize (GObject * object)
{
  GESTimelineSource *source = (GESTimelineSource *) object;

  if (source->text)
    g_free (source->text);
  if (source->font_desc)
    g_free (source->font_desc);

  G_OBJECT_CLASS (ges_timeline_source_parent_class)->finalize (object);
}

static void
ges_timeline_source_class_init (GESTimelineSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *timobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

  object_class->get_property = ges_timeline_source_get_property;
  object_class->set_property = ges_timeline_source_set_property;
  object_class->dispose = ges_timeline_source_dispose;
  object_class->finalize = ges_timeline_source_finalize;

  /**
   * GESTimelineTextOverlay:text
   *
   * The text to diplay
   */

  g_object_class_install_property (object_class, PROP_TEXT,
      g_param_spec_string ("text", "Text", "The text to display",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTimelineTextOverlay:font-desc
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


  timobj_class->create_track_object = ges_timeline_source_create_track_object;

  timobj_class->create_track_objects = ges_timeline_source_create_track_objects;
}

static void
ges_timeline_source_init (GESTimelineSource * self)
{
  self->halign = DEFAULT_PROP_HALIGNMENT;
  self->valign = DEFAULT_PROP_VALIGNMENT;
}

GESTimelineSource *
ges_timeline_source_new (void)
{
  return g_object_new (GES_TYPE_TIMELINE_SOURCE, NULL);
}

static GESTrackObject *
ges_timeline_source_create_track_object (GESTimelineObject * obj,
    GESTrack * track)
{
  GST_DEBUG ("Creating a GESTrackSource");
  /* FIXME : Implement properly ! */
  return (GESTrackObject *) ges_track_source_new ();
}

static gboolean
ges_timeline_source_create_track_objects (GESTimelineObject * obj,
    GESTrack * track)
{
  GESTrackObject *primary, *overlay;
  GESTimelineSource *self;
  gboolean success = FALSE;

  self = (GESTimelineSource *) obj;

  /* calls add_track_object() for us. we already own this reference */
  primary = ges_timeline_object_create_track_object (obj, track);
  if (!primary) {
    GST_WARNING ("couldn't create primary track object");
    return FALSE;
  }

  success = ges_track_add_object (track, primary);

  /* create priority space for the text overlay. do this regardless of
   * wthether we create an overlay so that track objects have a consistent
   * priority between tracks. */
  g_object_set (primary, "priority-offset", (guint) 1, NULL);

  if (track->type == GES_TRACK_TYPE_VIDEO) {
    overlay = (GESTrackObject *) ges_track_text_overlay_new ();
    /* will check for null */
    if (!ges_timeline_object_add_track_object (obj, overlay)) {
      GST_ERROR ("couldn't add textoverlay");
      return FALSE;
    }

    success = ges_track_add_object (track, overlay);

    if ((self->text) && *(self->text)) {
      ges_track_text_overlay_set_text ((GESTrackTextOverlay *) overlay,
          self->text);
    }

    else {
      ges_track_object_set_active (overlay, FALSE);
    }

    if (self->font_desc)
      ges_track_text_overlay_set_font_desc ((GESTrackTextOverlay *) overlay,
          self->font_desc);
    ges_track_text_overlay_set_halignment ((GESTrackTextOverlay *) overlay,
        self->halign);
    ges_track_text_overlay_set_valignment ((GESTrackTextOverlay *) overlay,
        self->valign);

  }

  return success;
}

static void
ges_timeline_source_set_text (GESTimelineSource * self, const gchar * text)
{
  GList *tmp;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG ("self:%p, text:%s", self, text);

  if (self->text)
    g_free (self->text);

  self->text = g_strdup (text);

  for (tmp = object->trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (GES_IS_TRACK_TEXT_OVERLAY (trackobject)) {
      ges_track_text_overlay_set_text ((GESTrackTextOverlay *)
          (trackobject), self->text);
      ges_track_object_set_active (trackobject, (text && (*text)) ? TRUE :
          FALSE);
    }
  }
}

static void
ges_timeline_source_set_font_desc (GESTimelineSource * self, const gchar *
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

    if (GES_IS_TRACK_TEXT_OVERLAY (trackobject))
      ges_track_text_overlay_set_font_desc ((GESTrackTextOverlay *)
          (trackobject), self->font_desc);
  }

}

static void
ges_timeline_source_set_halign (GESTimelineSource * self, GESTextHAlign halign)
{
  GList *tmp;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG ("self:%p, halign:%d", self, halign);

  self->halign = halign;

  for (tmp = object->trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (GES_IS_TRACK_TEXT_OVERLAY (trackobject))
      ges_track_text_overlay_set_halignment ((GESTrackTextOverlay *)
          (trackobject), self->halign);
  }

}

static void
ges_timeline_source_set_valign (GESTimelineSource * self, GESTextVAlign valign)
{
  GList *tmp;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG ("self:%p, valign:%d", self, valign);

  self->valign = valign;

  for (tmp = object->trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (GES_IS_TRACK_TEXT_OVERLAY (trackobject))
      ges_track_text_overlay_set_valignment ((GESTrackTextOverlay *)
          (trackobject), self->valign);
  }

}
