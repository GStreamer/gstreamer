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
 * SECTION:ges-title-clip
 * @short_description: Render stand-alone titles in  GESTimelineLayer.
 *
 * Renders the given text in the specified font, at specified position, and
 * with the specified background pattern.
 */

#include "ges-internal.h"
#include "ges-title-clip.h"
#include "ges-source-clip.h"
#include "ges-track-element.h"
#include "ges-track-title-source.h"
#include <string.h>

G_DEFINE_TYPE (GESTitleClip, ges_title_clip, GES_TYPE_SOURCE_CLIP);

#define DEFAULT_TEXT ""
#define DEFAULT_FONT_DESC "Serif 36"
#define GES_TITLE_CLIP_VALIGN_TYPE (ges_title_clip_valign_get_type())
#define GES_TITLE_CLIP_HALIGN_TYPE (ges_title_clip_halign_get_type())

struct _GESTitleClipPrivate
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

static GESTrackElement
    * ges_title_clip_create_track_element (GESClip * obj, GESTrackType type);

static void
ges_title_clip_track_element_added (GESClip * obj,
    GESTrackElement * trackelement);
static void ges_title_clip_track_element_released (GESClip * obj,
    GESTrackElement * trackelement);

static void
ges_title_clip_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTitleClipPrivate *priv = GES_TITLE_CLIP (object)->priv;

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
ges_title_clip_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTitleClip *uriclip = GES_TITLE_CLIP (object);

  switch (property_id) {
    case PROP_MUTE:
      ges_title_clip_set_mute (uriclip, g_value_get_boolean (value));
      break;
    case PROP_TEXT:
      ges_title_clip_set_text (uriclip, g_value_get_string (value));
      break;
    case PROP_FONT_DESC:
      ges_title_clip_set_font_desc (uriclip, g_value_get_string (value));
      break;
    case PROP_HALIGNMENT:
      ges_title_clip_set_halignment (uriclip, g_value_get_enum (value));
      break;
    case PROP_VALIGNMENT:
      ges_title_clip_set_valignment (uriclip, g_value_get_enum (value));
      break;
    case PROP_COLOR:
      ges_title_clip_set_color (uriclip, g_value_get_uint (value));
      break;
    case PROP_BACKGROUND:
      ges_title_clip_set_background (uriclip, g_value_get_uint (value));
      break;
    case PROP_XPOS:
      ges_title_clip_set_xpos (uriclip, g_value_get_double (value));
      break;
    case PROP_YPOS:
      ges_title_clip_set_ypos (uriclip, g_value_get_double (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_title_clip_dispose (GObject * object)
{
  GESTitleClip *self = GES_TITLE_CLIP (object);

  if (self->priv->text)
    g_free (self->priv->text);
  if (self->priv->font_desc)
    g_free (self->priv->font_desc);

  G_OBJECT_CLASS (ges_title_clip_parent_class)->dispose (object);
}

static void
ges_title_clip_class_init (GESTitleClipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESClipClass *timobj_class = GES_CLIP_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTitleClipPrivate));

  object_class->get_property = ges_title_clip_get_property;
  object_class->set_property = ges_title_clip_set_property;
  object_class->dispose = ges_title_clip_dispose;

  /**
   * GESTitleClip:text:
   *
   * The text to diplay
   */
  g_object_class_install_property (object_class, PROP_TEXT,
      g_param_spec_string ("text", "Text", "The text to display",
          DEFAULT_TEXT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTitleClip:font-desc:
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
   * GESTitleClip:valignment:
   *
   * Vertical alignent of the text
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VALIGNMENT,
      g_param_spec_enum ("valignment", "vertical alignment",
          "Vertical alignment of the text", GES_TEXT_VALIGN_TYPE,
          DEFAULT_VALIGNMENT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  /**
   * GESTitleClip:halignment:
   *
   * Horizontal alignment of the text
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HALIGNMENT,
      g_param_spec_enum ("halignment", "horizontal alignment",
          "Horizontal alignment of the text",
          GES_TEXT_HALIGN_TYPE, DEFAULT_HALIGNMENT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  /**
   * GESTitleClip:mute:
   *
   * Whether the sound will be played or not.
   */
  g_object_class_install_property (object_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute audio track",
          FALSE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  timobj_class->create_track_element = ges_title_clip_create_track_element;
  timobj_class->need_fill_track = FALSE;
  timobj_class->track_element_added = ges_title_clip_track_element_added;
  timobj_class->track_element_released = ges_title_clip_track_element_released;

  /**
   * GESTitleClip:color:
   *
   * The color of the text
   */

  g_object_class_install_property (object_class, PROP_COLOR,
      g_param_spec_uint ("color", "Color", "The color of the text",
          0, G_MAXUINT32, G_MAXUINT32, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTitleClip:background:
   *
   * The background of the text
   */

  g_object_class_install_property (object_class, PROP_BACKGROUND,
      g_param_spec_uint ("background", "Background",
          "The background of the text", 0, G_MAXUINT32, G_MAXUINT32,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTitleClip:xpos:
   *
   * The horizontal position of the text
   */

  g_object_class_install_property (object_class, PROP_XPOS,
      g_param_spec_double ("xpos", "Xpos", "The horizontal position",
          0, 1, 0.5, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTitleClip:ypos:
   *
   * The vertical position of the text
   */

  g_object_class_install_property (object_class, PROP_YPOS,
      g_param_spec_double ("ypos", "Ypos", "The vertical position",
          0, 1, 0.5, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
ges_title_clip_init (GESTitleClip * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TITLE_CLIP, GESTitleClipPrivate);

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
 * ges_title_clip_set_text:
 * @self: the #GESTitleClip* to set text on
 * @text: the text to render. an internal copy of this text will be
 * made.
 *
 * Sets the text this timeline object will render.
 *
 */
void
ges_title_clip_set_text (GESTitleClip * self, const gchar * text)
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
 * ges_title_clip_set_font_desc:
 * @self: the #GESTitleClip*
 * @font_desc: the pango font description
 *
 * Sets the pango font description of the text.
 *
 */
void
ges_title_clip_set_font_desc (GESTitleClip * self, const gchar * font_desc)
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
 * ges_title_clip_set_halignment:
 * @self: the #GESTitleClip* to set horizontal alignement of text on
 * @halign: #GESTextHAlign
 *
 * Sets the horizontal aligment of the text.
 *
 */
void
ges_title_clip_set_halignment (GESTitleClip * self, GESTextHAlign halign)
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
 * ges_title_clip_set_valignment:
 * @self: the #GESTitleClip* to set vertical alignement of text on
 * @valign: #GESTextVAlign
 *
 * Sets the vertical aligment of the text.
 *
 */
void
ges_title_clip_set_valignment (GESTitleClip * self, GESTextVAlign valign)
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
 * ges_title_clip_set_mute:
 * @self: the #GESTitleClip on which to mute or unmute the audio track
 * @mute: %TRUE to mute the audio track, %FALSE to unmute it
 *
 * Sets whether the audio track of this timeline object is muted or not
 *
 */
void
ges_title_clip_set_mute (GESTitleClip * self, gboolean mute)
{
  GList *tmp, *trackelements;
  GESClip *object = (GESClip *) self;

  GST_DEBUG_OBJECT (self, "mute:%d", mute);

  self->priv->mute = mute;

  /* Go over tracked objects, and update 'active' status on all audio objects */
  /* FIXME : We need a much less crack way to find the trackelement to change */
  trackelements = ges_clip_get_track_elements (object);
  for (tmp = trackelements; tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;

    if (ges_track_element_get_track (trackelement)->type ==
        GES_TRACK_TYPE_AUDIO)
      ges_track_element_set_active (trackelement, !mute);

    g_object_unref (GES_TRACK_ELEMENT (tmp->data));
  }
  g_list_free (trackelements);
}

/**
 * ges_title_clip_set_color:
 * @self: the #GESTitleClip* to set
 * @color: The color @self is being set to
 *
 * Sets the color of the text.
 *
 * Since: 0.10.2
 */
void
ges_title_clip_set_color (GESTitleClip * self, guint32 color)
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
 * ges_title_clip_set_background:
 * @self: the #GESTitleClip* to set
 * @background: The color @self is being set to
 *
 * Sets the background of the text.
 */
void
ges_title_clip_set_background (GESTitleClip * self, guint32 background)
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
 * ges_title_clip_set_xpos:
 * @self: the #GESTitleClip* to set
 * @position: The horizontal position @self is being set to
 *
 * Sets the horizontal position of the text.
 *
 * Since: 0.10.2
 */
void
ges_title_clip_set_xpos (GESTitleClip * self, gdouble position)
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
 * ges_title_clip_set_ypos:
 * @self: the #GESTitleClip* to set
 * @position: The vertical position @self is being set to
 *
 * Sets the vertical position of the text.
 *
 * Since: 0.10.2
 */
void
ges_title_clip_set_ypos (GESTitleClip * self, gdouble position)
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
 * ges_title_clip_get_text:
 * @self: a #GESTitleClip
 *
 * Get the text currently set on @self.
 *
 * Returns: The text currently set on @self.
 *
 */
const gchar *
ges_title_clip_get_text (GESTitleClip * self)
{
  return self->priv->text;
}

/**
 * ges_title_clip_get_font_desc:
 * @self: a #GESTitleClip
 *
 * Get the pango font description used by @self.
 *
 * Returns: The pango font description used by @self.
 *
 */
const char *
ges_title_clip_get_font_desc (GESTitleClip * self)
{
  return self->priv->font_desc;
}

/**
 * ges_title_clip_get_halignment:
 * @self: a #GESTitleClip
 *
 * Get the horizontal aligment used by @self.
 *
 * Returns: The horizontal aligment used by @self.
 *
 */
GESTextHAlign
ges_title_clip_get_halignment (GESTitleClip * self)
{
  return self->priv->halign;
}

/**
 * ges_title_clip_get_valignment:
 * @self: a #GESTitleClip
 *
 * Get the vertical aligment used by @self.
 *
 * Returns: The vertical aligment used by @self.
 *
 */
GESTextVAlign
ges_title_clip_get_valignment (GESTitleClip * self)
{
  return self->priv->valign;
}

/**
 * ges_title_clip_is_muted:
 * @self: a #GESTitleClip
 *
 * Let you know if the audio track of @self is muted or not.
 *
 * Returns: Whether the audio track of @self is muted or not.
 *
 */
gboolean
ges_title_clip_is_muted (GESTitleClip * self)
{
  return self->priv->mute;
}

/**
 * ges_title_clip_get_color:
 * @self: a #GESTitleClip
 *
 * Get the color used by @self.
 *
 * Returns: The color used by @self.
 *
 * Since: 0.10.2
 */
const guint32
ges_title_clip_get_color (GESTitleClip * self)
{
  return self->priv->color;
}

/**
 * ges_title_clip_get_background:
 * @self: a #GESTitleClip
 *
 * Get the background used by @self.
 *
 * Returns: The color used by @self.
 */
const guint32
ges_title_clip_get_background (GESTitleClip * self)
{
  return self->priv->background;
}

/**
 * ges_title_clip_get_xpos:
 * @self: a #GESTitleClip
 *
 * Get the horizontal position used by @self.
 *
 * Returns: The horizontal position used by @self.
 *
 * Since: 0.10.2
 */
const gdouble
ges_title_clip_get_xpos (GESTitleClip * self)
{
  return self->priv->xpos;
}

/**
 * ges_title_clip_get_ypos:
 * @self: a #GESTitleClip
 *
 * Get the vertical position used by @self.
 *
 * Returns: The vertical position used by @self.
 *
 * Since: 0.10.2
 */
const gdouble
ges_title_clip_get_ypos (GESTitleClip * self)
{
  return self->priv->ypos;
}

static void
ges_title_clip_track_element_released (GESClip * obj,
    GESTrackElement * trackelement)
{
  GESTitleClipPrivate *priv = GES_TITLE_CLIP (obj)->priv;

  /* If this is called, we should be sure the trackelement exists */
  if (GES_IS_TRACK_TITLE_SOURCE (trackelement)) {
    GST_DEBUG_OBJECT (obj, "%p released from %p", trackelement, obj);
    priv->track_titles = g_slist_remove (priv->track_titles, trackelement);
    g_object_unref (trackelement);
  }
}

static void
ges_title_clip_track_element_added (GESClip * obj,
    GESTrackElement * trackelement)
{
  GESTitleClipPrivate *priv = GES_TITLE_CLIP (obj)->priv;

  if (GES_IS_TRACK_TITLE_SOURCE (trackelement)) {
    GST_DEBUG_OBJECT (obj, "%p added to %p", trackelement, obj);
    priv->track_titles =
        g_slist_prepend (priv->track_titles, g_object_ref (trackelement));
  }
}

static GESTrackElement *
ges_title_clip_create_track_element (GESClip * obj, GESTrackType type)
{

  GESTitleClipPrivate *priv = GES_TITLE_CLIP (obj)->priv;
  GESTrackElement *res = NULL;

  GST_DEBUG_OBJECT (obj, "a GESTrackTitleSource");

  if (type == GES_TRACK_TYPE_VIDEO) {
    res = (GESTrackElement *) ges_track_title_source_new ();
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
 * ges_title_clip_new:
 *
 * Creates a new #GESTitleClip
 *
 * Returns: The newly created #GESTitleClip, or NULL if there was an
 * error.
 */
GESTitleClip *
ges_title_clip_new (void)
{
  /* FIXME : Check for validity/existence of URI */
  return g_object_new (GES_TYPE_TITLE_CLIP, NULL);
}
