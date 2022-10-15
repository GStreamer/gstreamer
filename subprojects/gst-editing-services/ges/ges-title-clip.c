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
 * SECTION:gestitleclip
 * @title: GESTitleClip
 * @short_description: Render stand-alone titles in  GESLayer.
 *
 * Renders the given text in the specified font, at specified position, and
 * with the specified background pattern.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-title-clip.h"
#include "ges-source-clip.h"
#include "ges-track-element.h"
#include "ges-title-source.h"
#include <string.h>

#define DEFAULT_TEXT ""
#define DEFAULT_FONT_DESC "Serif 36"
#define GES_TITLE_CLIP_VALIGN_TYPE (ges_title_clip_valign_get_type())
#define GES_TITLE_CLIP_HALIGN_TYPE (ges_title_clip_halign_get_type())

struct _GESTitleClipPrivate
{
  GSList *track_titles;
};

enum
{
  PROP_0,
  PROP_TEXT,
  PROP_FONT_DESC,
  PROP_HALIGNMENT,
  PROP_VALIGNMENT,
  PROP_COLOR,
  PROP_BACKGROUND,
  PROP_XPOS,
  PROP_YPOS,
};

G_DEFINE_TYPE_WITH_PRIVATE (GESTitleClip, ges_title_clip, GES_TYPE_SOURCE_CLIP);

static GESTrackElement
    * ges_title_clip_create_track_element (GESClip * clip, GESTrackType type);

static void _child_added (GESContainer * container,
    GESTimelineElement * element);
static void _child_removed (GESContainer * container,
    GESTimelineElement * element);

static void
ges_title_clip_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelineElement *child, *tmpsrc = NULL;
  GESTitleClipPrivate *priv = GES_TITLE_CLIP (object)->priv;

  if (!priv->track_titles)
    child = tmpsrc = GES_TIMELINE_ELEMENT (ges_title_source_new ());
  else
    child = priv->track_titles->data;

  switch (property_id) {
      /* Falltrough all over */
    case PROP_TEXT:
    case PROP_FONT_DESC:
    case PROP_HALIGNMENT:
    case PROP_VALIGNMENT:
    case PROP_COLOR:
    case PROP_BACKGROUND:
    case PROP_XPOS:
    case PROP_YPOS:
      ges_timeline_element_get_child_property (child, pspec->name, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }

  if (tmpsrc)
    g_object_unref (tmpsrc);
}

static void
ges_title_clip_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    case PROP_TEXT:
    case PROP_FONT_DESC:
    case PROP_HALIGNMENT:
    case PROP_VALIGNMENT:
    case PROP_COLOR:
    case PROP_BACKGROUND:
    case PROP_XPOS:
    case PROP_YPOS:
      ges_timeline_element_set_child_property (GES_TIMELINE_ELEMENT (object),
          pspec->name, value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_title_clip_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_title_clip_parent_class)->dispose (object);
}

static void
ges_title_clip_class_init (GESTitleClipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESClipClass *clip_class = GES_CLIP_CLASS (klass);
  GESContainerClass *container_class = GES_CONTAINER_CLASS (klass);

  object_class->get_property = ges_title_clip_get_property;
  object_class->set_property = ges_title_clip_set_property;
  object_class->dispose = ges_title_clip_dispose;

  /**
   * GESTitleClip:text:
   *
   * The text to diplay
   *
   * Deprecated:1.6: use #ges_timeline_element_set_children_properties or
   * #ges_timeline_element_get_children_properties instead.
   * See #GESTitleSource for more information about exposed properties
   */
  g_object_class_install_property (object_class, PROP_TEXT,
      g_param_spec_string ("text", "Text", "The text to display",
          DEFAULT_TEXT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
          GES_PARAM_NO_SERIALIZATION));

  /**
   * GESTitleClip:font-desc:
   *
   * Pango font description string
   *
   * Deprecated:1.6: use #ges_timeline_element_set_children_properties or
   * #ges_timeline_element_get_children_properties instead.
   * See #GESTitleSource for more information about exposed properties
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FONT_DESC,
      g_param_spec_string ("font-desc", "font description",
          "Pango font description of font to be used for rendering. "
          "See documentation of pango_font_description_from_string "
          "for syntax.", DEFAULT_FONT_DESC,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS |
          GES_PARAM_NO_SERIALIZATION));

  /**
   * GESTitleClip:valignment:
   *
   * Vertical alignent of the text
   *
   * Deprecated:1.6: use #ges_timeline_element_set_children_properties or
   * #ges_timeline_element_get_children_properties instead.
   * See #GESTitleSource for more information about exposed properties
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VALIGNMENT,
      g_param_spec_enum ("valignment", "vertical alignment",
          "Vertical alignment of the text", GES_TEXT_VALIGN_TYPE,
          DEFAULT_VALIGNMENT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS |
          GES_PARAM_NO_SERIALIZATION));
  /**
   * GESTitleClip:halignment:
   *
   * Horizontal alignment of the text
   *
   * Deprecated:1.6: use #ges_timeline_element_set_children_properties or
   * #ges_timeline_element_get_children_properties instead.
   * See #GESTitleSource for more information about exposed properties
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HALIGNMENT,
      g_param_spec_enum ("halignment", "horizontal alignment",
          "Horizontal alignment of the text",
          GES_TEXT_HALIGN_TYPE, DEFAULT_HALIGNMENT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS |
          GES_PARAM_NO_SERIALIZATION));

  clip_class->create_track_element = ges_title_clip_create_track_element;

  container_class->child_added = _child_added;
  container_class->child_removed = _child_removed;

  /**
   * GESTitleClip:color:
   *
   * The color of the text
   *
   * Deprecated:1.6: use #ges_timeline_element_set_children_properties or
   * #ges_timeline_element_get_children_properties instead.
   * See #GESTitleSource for more information about exposed properties
   */

  g_object_class_install_property (object_class, PROP_COLOR,
      g_param_spec_uint ("color", "Color", "The color of the text",
          0, G_MAXUINT32, G_MAXUINT32, G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
          GES_PARAM_NO_SERIALIZATION));

  /**
   * GESTitleClip:background:
   *
   * The background of the text
   *
   * Deprecated:1.6: use #ges_timeline_element_set_children_properties or
   * #ges_timeline_element_get_children_properties instead.
   * See #GESTitleSource for more information about exposed properties
   */

  g_object_class_install_property (object_class, PROP_BACKGROUND,
      g_param_spec_uint ("background", "Background",
          "The background of the text", 0, G_MAXUINT32, G_MAXUINT32,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | GES_PARAM_NO_SERIALIZATION));

  /**
   * GESTitleClip:xpos:
   *
   * The horizontal position of the text
   *
   * Deprecated:1.6: use #ges_timeline_element_set_children_properties or
   * #ges_timeline_element_get_children_properties instead.
   * See #GESTitleSource for more information about exposed properties
   */

  g_object_class_install_property (object_class, PROP_XPOS,
      g_param_spec_double ("xpos", "Xpos", "The horizontal position",
          0, 1, 0.5, G_PARAM_READWRITE | G_PARAM_CONSTRUCT
          | GES_PARAM_NO_SERIALIZATION));

  /**
   * GESTitleClip:ypos:
   *
   * The vertical position of the text
   *
   * Deprecated:1.6: use #ges_timeline_element_set_children_properties or
   * #ges_timeline_element_get_children_properties instead.
   * See #GESTitleSource for more information about exposed properties
   */

  g_object_class_install_property (object_class, PROP_YPOS,
      g_param_spec_double ("ypos", "Ypos", "The vertical position",
          0, 1, 0.5, G_PARAM_READWRITE | G_PARAM_CONSTRUCT
          | GES_PARAM_NO_SERIALIZATION));
}

static void
ges_title_clip_init (GESTitleClip * self)
{
  self->priv = ges_title_clip_get_instance_private (self);

  GES_TIMELINE_ELEMENT (self)->duration = 0;
  /* Not 100% required since a new gobject's content will always be memzero'd */
}

/**
 * ges_title_clip_set_text:
 * @self: the #GESTitleClip* to set text on
 * @text: (nullable): the text to render. an internal copy of this text will be
 * made.
 *
 * Sets the text this clip will render.
 *
 * Deprecated:1.6: use #ges_timeline_element_set_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
void
ges_title_clip_set_text (GESTitleClip * self, const gchar * text)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "text:%s", text);

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_timeline_element_set_child_properties (tmp->data, "text", text, NULL);
  }
}

/**
 * ges_title_clip_set_font_desc:
 * @self: the #GESTitleClip*
 * @font_desc: (nullable): the pango font description
 *
 * Sets the pango font description of the text.
 *
 * Deprecated:1.6: use #ges_timeline_element_set_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
void
ges_title_clip_set_font_desc (GESTitleClip * self, const gchar * font_desc)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "font_desc:%s", font_desc);

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_timeline_element_set_child_properties (tmp->data,
        "font-desc", font_desc, NULL);
  }
}

/**
 * ges_title_clip_set_halignment:
 * @self: the #GESTitleClip* to set horizontal alignement of text on
 * @halign: #GESTextHAlign
 *
 * Sets the horizontal aligment of the text.
 *
 * Deprecated:1.6: use #ges_timeline_element_set_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
void
ges_title_clip_set_halignment (GESTitleClip * self, GESTextHAlign halign)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "halign:%d", halign);

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_timeline_element_set_child_properties (tmp->data,
        "halignment", halign, NULL);
  }
}

/**
 * ges_title_clip_set_valignment:
 * @self: the #GESTitleClip* to set vertical alignement of text on
 * @valign: #GESTextVAlign
 *
 * Sets the vertical aligment of the text.
 *
 * Deprecated:1.6: use #ges_timeline_element_set_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
void
ges_title_clip_set_valignment (GESTitleClip * self, GESTextVAlign valign)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "valign:%d", valign);

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_timeline_element_set_child_properties (tmp->data,
        "valignment", valign, NULL);
  }
}

/**
 * ges_title_clip_set_color:
 * @self: the #GESTitleClip* to set
 * @color: The color @self is being set to
 *
 * Sets the color of the text.
 *
 * Deprecated:1.6: use #ges_timeline_element_set_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
void
ges_title_clip_set_color (GESTitleClip * self, guint32 color)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "color:%d", color);

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_timeline_element_set_child_properties (tmp->data, "color", color, NULL);
  }
}

/**
 * ges_title_clip_set_background:
 * @self: the #GESTitleClip* to set
 * @background: The color @self is being set to
 *
 * Sets the background of the text.
 *
 * Deprecated:1.6: use #ges_timeline_element_set_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
void
ges_title_clip_set_background (GESTitleClip * self, guint32 background)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "background:%d", background);

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_timeline_element_set_child_properties (tmp->data,
        "foreground-color", background, NULL);
  }
}

/**
 * ges_title_clip_set_xpos:
 * @self: the #GESTitleClip* to set
 * @position: The horizontal position @self is being set to
 *
 * Sets the horizontal position of the text.
 *
 * Deprecated:1.6: use #ges_timeline_element_set_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
void
ges_title_clip_set_xpos (GESTitleClip * self, gdouble position)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "xpos:%f", position);

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_timeline_element_set_child_properties (tmp->data, "xpos", position,
        NULL);
  }
}

/**
 * ges_title_clip_set_ypos:
 * @self: the #GESTitleClip* to set
 * @position: The vertical position @self is being set to
 *
 * Sets the vertical position of the text.
 *
 * Deprecated:1.6: use #ges_timeline_element_set_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
void
ges_title_clip_set_ypos (GESTitleClip * self, gdouble position)
{
  GSList *tmp;

  GST_DEBUG_OBJECT (self, "ypos:%f", position);

  for (tmp = self->priv->track_titles; tmp; tmp = tmp->next) {
    ges_timeline_element_set_child_properties (tmp->data, "ypos", position,
        NULL);
  }
}

/**
 * ges_title_clip_get_text:
 * @self: a #GESTitleClip
 *
 * Get the text currently set on @self.
 *
 * Returns: (transfer full) (nullable): The text currently set on @self.
 *
 * Deprecated:1.6: use #ges_timeline_element_get_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
const gchar *
ges_title_clip_get_text (GESTitleClip * self)
{
  gchar *text;

  ges_timeline_element_get_child_properties (self->priv->track_titles->data,
      "text", &text, NULL);

  return text;
}

/**
 * ges_title_clip_get_font_desc:
 * @self: a #GESTitleClip
 *
 * Get the pango font description used by @self.
 *
 * Returns: (transfer full) (nullable): The pango font description used by @self.
 *
 * Deprecated:1.6: use #ges_timeline_element_get_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
const char *
ges_title_clip_get_font_desc (GESTitleClip * self)
{
  gchar *font_desc;

  ges_timeline_element_get_child_properties (self->priv->track_titles->data,
      "font-desc", &font_desc, NULL);

  return font_desc;
}

/**
 * ges_title_clip_get_halignment:
 * @self: a #GESTitleClip
 *
 * Get the horizontal aligment used by @self.
 *
 * Returns: The horizontal aligment used by @self.
 *
 * Deprecated:1.6: use #ges_timeline_element_get_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
GESTextHAlign
ges_title_clip_get_halignment (GESTitleClip * self)
{
  GESTextHAlign halign;

  ges_timeline_element_get_child_properties (self->priv->track_titles->data,
      "halignment", &halign, NULL);

  return halign;
}

/**
 * ges_title_clip_get_valignment:
 * @self: a #GESTitleClip
 *
 * Get the vertical aligment used by @self.
 *
 * Returns: The vertical aligment used by @self.
 *
 * Deprecated:1.6: use #ges_timeline_element_get_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
GESTextVAlign
ges_title_clip_get_valignment (GESTitleClip * self)
{
  GESTextVAlign valign;

  ges_timeline_element_get_child_properties (self->priv->track_titles->data,
      "valignment", &valign, NULL);

  return valign;
}

/**
 * ges_title_clip_get_text_color:
 * @self: a #GESTitleClip
 *
 * Get the color used by @self.
 *
 * Returns: The color used by @self.
 *
 * Deprecated:1.6: use #ges_timeline_element_get_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
const guint32
ges_title_clip_get_text_color (GESTitleClip * self)
{
  guint32 color;

  ges_timeline_element_get_child_properties (self->priv->track_titles->data,
      "color", &color, NULL);

  return color;
}

/**
 * ges_title_clip_get_background_color:
 * @self: a #GESTitleClip
 *
 * Get the background used by @self.
 *
 * Returns: The color used by @self.
 *
 * Deprecated:1.6: use #ges_timeline_element_get_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
const guint32
ges_title_clip_get_background_color (GESTitleClip * self)
{
  guint32 color;

  ges_timeline_element_get_child_properties (self->priv->track_titles->data,
      "foreground-color", &color, NULL);

  return color;
}

/**
 * ges_title_clip_get_xpos:
 * @self: a #GESTitleClip
 *
 * Get the horizontal position used by @self.
 *
 * Returns: The horizontal position used by @self.
 *
 * Deprecated:1.6: use #ges_timeline_element_get_children_properties instead.
 * See #GESTitleSource for more information about exposed properties
 */
const gdouble
ges_title_clip_get_xpos (GESTitleClip * self)
{
  gdouble xpos;

  ges_timeline_element_get_child_properties (self->priv->track_titles->data,
      "xpos", &xpos, NULL);

  return xpos;
}

/**
 * ges_title_clip_get_ypos:
 * @self: a #GESTitleClip
 *
 * Get the vertical position used by @self.
 *
 * Returns: The vertical position used by @self.
 *
 * Deprecated:1.6: use #ges_timeline_element_get_children_property instead
 */
const gdouble
ges_title_clip_get_ypos (GESTitleClip * self)
{
  gdouble ypos;

  ges_timeline_element_get_child_properties (self->priv->track_titles->data,
      "ypos", &ypos, NULL);

  return ypos;
}

static void
_child_removed (GESContainer * container, GESTimelineElement * element)
{
  GESTitleClipPrivate *priv = GES_TITLE_CLIP (container)->priv;

  /* If this is called, we should be sure the element exists */
  if (GES_IS_TITLE_SOURCE (element)) {
    GST_DEBUG_OBJECT (container, "%" GST_PTR_FORMAT " removed", element);
    priv->track_titles = g_slist_remove (priv->track_titles, element);
    gst_object_unref (element);
  }

  GES_CONTAINER_CLASS (ges_title_clip_parent_class)->child_removed (container,
      element);
}

static void
_child_added (GESContainer * container, GESTimelineElement * element)
{
  GESTitleClipPrivate *priv = GES_TITLE_CLIP (container)->priv;

  if (GES_IS_TITLE_SOURCE (element)) {
    GST_DEBUG_OBJECT (container, "%" GST_PTR_FORMAT " added", element);
    priv->track_titles = g_slist_prepend (priv->track_titles,
        gst_object_ref (element));
  }

  GES_CONTAINER_CLASS (ges_title_clip_parent_class)->child_added (container,
      element);
}

static GESTrackElement *
ges_title_clip_create_track_element (GESClip * clip, GESTrackType type)
{

  GESTrackElement *res = NULL;

  GST_DEBUG_OBJECT (clip, "a GESTitleSource");

  if (type == GES_TRACK_TYPE_VIDEO)
    res = (GESTrackElement *) ges_title_source_new ();

  return res;
}

/**
 * ges_title_clip_new:
 *
 * Creates a new #GESTitleClip
 *
 * Returns: (transfer floating) (nullable): The newly created #GESTitleClip,
 * or %NULL if there was an error.
 */
GESTitleClip *
ges_title_clip_new (void)
{
  GESTitleClip *new_clip;
  GESAsset *asset = ges_asset_request (GES_TYPE_TITLE_CLIP, NULL, NULL);

  new_clip = GES_TITLE_CLIP (ges_asset_extract (asset, NULL));
  gst_object_unref (asset);

  return new_clip;
}
