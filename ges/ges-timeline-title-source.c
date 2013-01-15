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
 * SECTION:ges-timeline-title-source
 * @short_description: Render stand-alone titles in  GESTimelineLayer.
 *
 * Renders the given text in the specified font, at specified position, and
 * with the specified background pattern.
 */

#include "ges-internal.h"
#include "ges-timeline-title-source.h"
#include "ges-timeline-source.h"
#include "ges-track-object.h"
#include "ges-track-title-source.h"
#include <string.h>

G_DEFINE_TYPE (GESTimelineTitleSource, ges_timeline_title_source,
    GES_TYPE_TIMELINE_SOURCE);

#define DEFAULT_TEXT ""
#define DEFAULT_FONT_DESC "Serif 36"
#define GES_TIMELINE_TITLE_SOURCE_VALIGN_TYPE (ges_timeline_title_source_valign_get_type())
#define GES_TIMELINE_TITLE_SOURCE_HALIGN_TYPE (ges_timeline_title_source_halign_get_type())

struct _GESTimelineTitleSourcePrivate
{
  gboolean mute;
  gchar *text;
  gchar *font_desc;
  GESTextHAlign halign;
  GESTextVAlign valign;
  GSList *track_titles;
  guint32 color;
  guint32 background;
  gdouble xpos;
  gdouble ypos;
};

enum
{
  PROP_0,
  PROP_MUTE,
  PROP_TEXT,
  PROP_FONT_DESC,
  PROP_HALIGNMENT,
  PROP_VALIGNMENT,
  PROP_COLOR,
  PROP_BACKGROUND,
  PROP_XPOS,
  PROP_YPOS,
};

static GESTrackObject
    * ges_timeline_title_source_create_track_object (GESTimelineObject * obj,
    GESTrackType type);

static void
ges_timeline_title_source_track_object_added (GESTimelineObject * obj,
    GESTrackObject * tckobj);
static void
ges_timeline_title_source_track_object_released (GESTimelineObject * obj,
    GESTrackObject * tckobj);

static void
ges_timeline_title_source_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelineTitleSourcePrivate *priv =
      GES_TIMELINE_TITLE_SOURCE (object)->priv;

  switch (property_id) {
    case PROP_MUTE:
      g_value_set_boolean (value, priv->mute);
      break;
    case PROP_TEXT:
      g_value_set_string (value, priv->text);
      break;
    case PROP_FONT_DESC:
      g_value_set_string (value, priv->font_desc);
      break;
    case PROP_HALIGNMENT:
      g_value_set_enum (value, priv->halign);
      break;
    case PROP_VALIGNMENT:
      g_value_set_enum (value, priv->valign);
      break;
    case PROP_COLOR:
      g_value_set_uint (value, priv->color);
      break;
    case PROP_BACKGROUND:
      g_value_set_uint (value, priv->background);
      break;
    case PROP_XPOS:
      g_value_set_double (value, priv->xpos);
      break;
    case PROP_YPOS:
      g_value_set_double (value, priv->ypos);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_title_source_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelineTitleSource *tfs = GES_TIMELINE_TITLE_SOURCE (object);

  switch (property_id) {
    case PROP_MUTE:
      ges_timeline_title_source_set_mute (tfs, g_value_get_boolean (value));
      break;
    case PROP_TEXT:
      ges_timeline_title_source_set_text (tfs, g_value_get_string (value));
      break;
    case PROP_FONT_DESC:
      ges_timeline_title_source_set_font_desc (tfs, g_value_get_string (value));
      break;
    case PROP_HALIGNMENT:
      ges_timeline_title_source_set_halignment (tfs, g_value_get_enum (value));
      break;
    case PROP_VALIGNMENT:
      ges_timeline_title_source_set_valignment (tfs, g_value_get_enum (value));
      break;
    case PROP_COLOR:
      ges_timeline_title_source_set_color (tfs, g_value_get_uint (value));
      break;
    case PROP_BACKGROUND:
      ges_timeline_title_source_set_background (tfs, g_value_get_uint (value));
      break;
    case PROP_XPOS:
      ges_timeline_title_source_set_xpos (tfs, g_value_get_double (value));
      break;
    case PROP_YPOS:
      ges_timeline_title_source_set_ypos (tfs, g_value_get_double (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_title_source_dispose (GObject * object)
{
  GESTimelineTitleSource *self = GES_TIMELINE_TITLE_SOURCE (object);

  if (self->priv->text)
    g_free (self->priv->text);
  if (self->priv->font_desc)
    g_free (self->priv->font_desc);

  G_OBJECT_CLASS (ges_timeline_title_source_parent_class)->dispose (object);
}

static void
ges_timeline_title_source_class_init (GESTimelineTitleSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *timobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTimelineTitleSourcePrivate));

  object_class->get_property = ges_timeline_title_source_get_property;
  object_class->set_property = ges_timeline_title_source_set_property;
  object_class->dispose = ges_timeline_title_source_dispose;

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

  timobj_class->create_track_object =
      ges_timeline_title_source_create_track_object;
  timobj_class->need_fill_track = FALSE;
  timobj_class->track_object_added =
      ges_timeline_title_source_track_object_added;
  timobj_class->track_object_released =
      ges_timeline_title_source_track_object_released;

  /**
   * GESTimelineTitleSource:color:
   *
   * The color of the text
   */

  g_object_class_install_property (object_class, PROP_COLOR,
      g_param_spec_uint ("color", "Color", "The color of the text",
          0, G_MAXUINT32, G_MAXUINT32, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTimelineTitleSource:background:
   *
   * The background of the text
   */

  g_object_class_install_property (object_class, PROP_BACKGROUND,
      g_param_spec_uint ("background", "Background",
          "The background of the text", 0, G_MAXUINT32, G_MAXUINT32,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTimelineTitleSource:xpos:
   *
   * The horizontal position of the text
   */

  g_object_class_install_property (object_class, PROP_XPOS,
      g_param_spec_double ("xpos", "Xpos", "The horizontal position",
          0, 1, 0.5, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTimelineTitleSource:ypos:
   *
   * The vertical position of the text
   */

  g_object_class_install_property (object_class, PROP_YPOS,
      g_param_spec_double ("ypos", "Ypos", "The vertical position",
          0, 1, 0.5, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
ges_timeline_title_source_init (GESTimelineTitleSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE_TITLE_SOURCE, GESTimelineTitleSourcePrivate);

  GES_TIMELINE_ELEMENT (self)->duration = 0;
  /* Not 100% required since a new gobject's content will always be memzero'd */
  self->priv->mute = FALSE;
  self->priv->text = NULL;
  self->priv->font_desc = NULL;
  self->priv->halign = DEFAULT_HALIGNMENT;
  self->priv->valign = DEFAULT_VALIGNMENT;
  self->priv->color = G_MAXUINT32;
  self->priv->background = G_MAXUINT32;
  self->priv->xpos = 0.5;
  self->priv->ypos = 0.5;
}

/**
 * ges_timeline_title_source_set_text:
 * @self: the #GESTimelineTitleSource* to set text on
 * @text: the text to render. an internal copy of this text will be
 * made.
 *
 * Sets the text this timeline object will render.
 *
 */
void
ges_timeline_title_source_set_text (GESTimelineTitleSource * self,
    const gchar * text)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "text:%s", text);

  if (self->priv->text)
    g_free (self->priv->text);

  self->priv->text = g_strdup (text);

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_track_title_source_set_text (GES_TRACK_TITLE_SOURCE (tmp->data),
        self->priv->text);
  }
}

/**
 * ges_timeline_title_source_set_font_desc:
 * @self: the #GESTimelineTitleSource*
 * @font_desc: the pango font description
 *
 * Sets the pango font description of the text.
 *
 */
void
ges_timeline_title_source_set_font_desc (GESTimelineTitleSource * self,
    const gchar * font_desc)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "font_desc:%s", font_desc);

  if (self->priv->font_desc)
    g_free (self->priv->font_desc);

  self->priv->font_desc = g_strdup (font_desc);

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_track_title_source_set_font_desc (GES_TRACK_TITLE_SOURCE (tmp->data),
        self->priv->font_desc);
  }
}

/**
 * ges_timeline_title_source_set_halignment:
 * @self: the #GESTimelineTitleSource* to set horizontal alignement of text on
 * @halign: #GESTextHAlign
 *
 * Sets the horizontal aligment of the text.
 *
 */
void
ges_timeline_title_source_set_halignment (GESTimelineTitleSource * self,
    GESTextHAlign halign)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "halign:%d", halign);

  self->priv->halign = halign;

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_track_title_source_set_halignment (GES_TRACK_TITLE_SOURCE (tmp->data),
        self->priv->halign);
  }
}

/**
 * ges_timeline_title_source_set_valignment:
 * @self: the #GESTimelineTitleSource* to set vertical alignement of text on
 * @valign: #GESTextVAlign
 *
 * Sets the vertical aligment of the text.
 *
 */
void
ges_timeline_title_source_set_valignment (GESTimelineTitleSource * self,
    GESTextVAlign valign)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "valign:%d", valign);

  self->priv->valign = valign;

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_track_title_source_set_valignment (GES_TRACK_TITLE_SOURCE (tmp->data),
        self->priv->valign);
  }
}

/**
 * ges_timeline_title_source_set_mute:
 * @self: the #GESTimelineTitleSource on which to mute or unmute the audio track
 * @mute: %TRUE to mute the audio track, %FALSE to unmute it
 *
 * Sets whether the audio track of this timeline object is muted or not
 *
 */
void
ges_timeline_title_source_set_mute (GESTimelineTitleSource * self,
    gboolean mute)
{
  GList *tmp, *trackobjects;
  GESTimelineObject *object = (GESTimelineObject *) self;

  GST_DEBUG_OBJECT (self, "mute:%d", mute);

  self->priv->mute = mute;

  /* Go over tracked objects, and update 'active' status on all audio objects */
  /* FIXME : We need a much less crack way to find the trackobject to change */
  trackobjects = ges_timeline_object_get_track_objects (object);
  for (tmp = trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *trackobject = (GESTrackObject *) tmp->data;

    if (ges_track_object_get_track (trackobject)->type == GES_TRACK_TYPE_AUDIO)
      ges_track_object_set_active (trackobject, !mute);

    g_object_unref (GES_TRACK_OBJECT (tmp->data));
  }
  g_list_free (trackobjects);
}

/**
 * ges_timeline_title_source_set_color:
 * @self: the #GESTimelineTitleSource* to set
 * @color: The color @self is being set to
 *
 * Sets the color of the text.
 *
 * Since: 0.10.2
 */
void
ges_timeline_title_source_set_color (GESTimelineTitleSource * self,
    guint32 color)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "color:%d", color);

  self->priv->color = color;

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_track_title_source_set_color (GES_TRACK_TITLE_SOURCE (tmp->data),
        self->priv->color);
  }
}

/**
 * ges_timeline_title_source_set_background:
 * @self: the #GESTimelineTitleSource* to set
 * @background: The color @self is being set to
 *
 * Sets the background of the text.
 */
void
ges_timeline_title_source_set_background (GESTimelineTitleSource * self,
    guint32 background)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "background:%d", background);

  self->priv->background = background;

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_track_title_source_set_background (GES_TRACK_TITLE_SOURCE (tmp->data),
        self->priv->background);
  }
}


/**
 * ges_timeline_title_source_set_xpos:
 * @self: the #GESTimelineTitleSource* to set
 * @position: The horizontal position @self is being set to
 *
 * Sets the horizontal position of the text.
 *
 * Since: 0.10.2
 */
void
ges_timeline_title_source_set_xpos (GESTimelineTitleSource * self,
    gdouble position)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "xpos:%f", position);

  self->priv->xpos = position;

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_track_title_source_set_xpos (GES_TRACK_TITLE_SOURCE (tmp->data),
        self->priv->xpos);
  }
}

/**
 * ges_timeline_title_source_set_ypos:
 * @self: the #GESTimelineTitleSource* to set
 * @position: The vertical position @self is being set to
 *
 * Sets the vertical position of the text.
 *
 * Since: 0.10.2
 */
void
ges_timeline_title_source_set_ypos (GESTimelineTitleSource * self,
    gdouble position)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "ypos:%f", position);

  self->priv->ypos = position;

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_track_title_source_set_ypos (GES_TRACK_TITLE_SOURCE (tmp->data),
        self->priv->ypos);
  }
}

/**
 * ges_timeline_title_source_get_text:
 * @self: a #GESTimelineTitleSource
 *
 * Get the text currently set on @self.
 *
 * Returns: The text currently set on @self.
 *
 */
const gchar *
ges_timeline_title_source_get_text (GESTimelineTitleSource * self)
{
  return self->priv->text;
}

/**
 * ges_timeline_title_source_get_font_desc:
 * @self: a #GESTimelineTitleSource
 *
 * Get the pango font description used by @self.
 *
 * Returns: The pango font description used by @self.
 *
 */
const char *
ges_timeline_title_source_get_font_desc (GESTimelineTitleSource * self)
{
  return self->priv->font_desc;
}

/**
 * ges_timeline_title_source_get_halignment:
 * @self: a #GESTimelineTitleSource
 *
 * Get the horizontal aligment used by @self.
 *
 * Returns: The horizontal aligment used by @self.
 *
 */
GESTextHAlign
ges_timeline_title_source_get_halignment (GESTimelineTitleSource * self)
{
  return self->priv->halign;
}

/**
 * ges_timeline_title_source_get_valignment:
 * @self: a #GESTimelineTitleSource
 *
 * Get the vertical aligment used by @self.
 *
 * Returns: The vertical aligment used by @self.
 *
 */
GESTextVAlign
ges_timeline_title_source_get_valignment (GESTimelineTitleSource * self)
{
  return self->priv->valign;
}

/**
 * ges_timeline_title_source_is_muted:
 * @self: a #GESTimelineTitleSource
 *
 * Let you know if the audio track of @self is muted or not.
 *
 * Returns: Whether the audio track of @self is muted or not.
 *
 */
gboolean
ges_timeline_title_source_is_muted (GESTimelineTitleSource * self)
{
  return self->priv->mute;
}

/**
 * ges_timeline_title_source_get_color:
 * @self: a #GESTimelineTitleSource
 *
 * Get the color used by @self.
 *
 * Returns: The color used by @self.
 *
 * Since: 0.10.2
 */
const guint32
ges_timeline_title_source_get_color (GESTimelineTitleSource * self)
{
  return self->priv->color;
}

/**
 * ges_timeline_title_source_get_background:
 * @self: a #GESTimelineTitleSource
 *
 * Get the background used by @self.
 *
 * Returns: The color used by @self.
 */
const guint32
ges_timeline_title_source_get_background (GESTimelineTitleSource * self)
{
  return self->priv->background;
}

/**
 * ges_timeline_title_source_get_xpos:
 * @self: a #GESTimelineTitleSource
 *
 * Get the horizontal position used by @self.
 *
 * Returns: The horizontal position used by @self.
 *
 * Since: 0.10.2
 */
const gdouble
ges_timeline_title_source_get_xpos (GESTimelineTitleSource * self)
{
  return self->priv->xpos;
}

/**
 * ges_timeline_title_source_get_ypos:
 * @self: a #GESTimelineTitleSource
 *
 * Get the vertical position used by @self.
 *
 * Returns: The vertical position used by @self.
 *
 * Since: 0.10.2
 */
const gdouble
ges_timeline_title_source_get_ypos (GESTimelineTitleSource * self)
{
  return self->priv->ypos;
}

static void
ges_timeline_title_source_track_object_released (GESTimelineObject * obj,
    GESTrackObject * tckobj)
{
  GESTimelineTitleSourcePrivate *priv = GES_TIMELINE_TITLE_SOURCE (obj)->priv;

  /* If this is called, we should be sure the tckobj exists */
  if (GES_IS_TRACK_TITLE_SOURCE (tckobj)) {
    GST_DEBUG_OBJECT (obj, "%p released from %p", tckobj, obj);
    priv->track_titles = g_slist_remove (priv->track_titles, tckobj);
    g_object_unref (tckobj);
  }
}

static void
ges_timeline_title_source_track_object_added (GESTimelineObject * obj,
    GESTrackObject * tckobj)
{
  GESTimelineTitleSourcePrivate *priv = GES_TIMELINE_TITLE_SOURCE (obj)->priv;

  if (GES_IS_TRACK_TITLE_SOURCE (tckobj)) {
    GST_DEBUG_OBJECT (obj, "%p added to %p", tckobj, obj);
    priv->track_titles =
        g_slist_prepend (priv->track_titles, g_object_ref (tckobj));
  }
}

static GESTrackObject *
ges_timeline_title_source_create_track_object (GESTimelineObject * obj,
    GESTrackType type)
{

  GESTimelineTitleSourcePrivate *priv = GES_TIMELINE_TITLE_SOURCE (obj)->priv;
  GESTrackObject *res = NULL;

  GST_DEBUG_OBJECT (obj, "a GESTrackTitleSource");

  if (type == GES_TRACK_TYPE_VIDEO) {
    res = (GESTrackObject *) ges_track_title_source_new ();
    GST_DEBUG_OBJECT (obj, "text property");
    ges_track_title_source_set_text ((GESTrackTitleSource *) res, priv->text);
    ges_track_title_source_set_font_desc ((GESTrackTitleSource *) res,
        priv->font_desc);
    ges_track_title_source_set_halignment ((GESTrackTitleSource *) res,
        priv->halign);
    ges_track_title_source_set_valignment ((GESTrackTitleSource *) res,
        priv->valign);
    ges_track_title_source_set_color ((GESTrackTitleSource *) res, priv->color);
    ges_track_title_source_set_background ((GESTrackTitleSource *) res,
        priv->background);
    ges_track_title_source_set_xpos ((GESTrackTitleSource *) res, priv->xpos);
    ges_track_title_source_set_ypos ((GESTrackTitleSource *) res, priv->ypos);
  }

  return res;
}

/**
 * ges_timeline_title_source_new:
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
