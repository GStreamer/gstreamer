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
 * SECTION:gestextoverlayclip
 * @short_description: Render text onto another stream in a GESLayer
 *
 * Renders text onto the next lower priority stream using textrender.
 */

#include "ges-internal.h"
#include "ges-text-overlay-clip.h"
#include "ges-track-element.h"
#include "ges-text-overlay.h"
#include <string.h>

G_DEFINE_TYPE (GESTextOverlayClip, ges_text_overlay_clip,
    GES_TYPE_OVERLAY_CLIP);

#define DEFAULT_PROP_TEXT ""
#define DEFAULT_PROP_FONT_DESC "Serif 36"
#define DEFAULT_PROP_VALIGNMENT GES_TEXT_VALIGN_BASELINE
#define DEFAULT_PROP_HALIGNMENT GES_TEXT_HALIGN_CENTER
#

struct _GESTextOverlayClipPrivate
{
  gchar *text;
  gchar *font_desc;
  GESTextHAlign halign;
  GESTextVAlign valign;
  guint32 color;
  gdouble xpos;
  gdouble ypos;
};

enum
{
  PROP_0,
  PROP_TEXT,
  PROP_FONT_DESC,
  PROP_HALIGNMENT,
  PROP_VALIGNMENT,
  PROP_COLOR,
  PROP_XPOS,
  PROP_YPOS,
};

static GESTrackElement
    * ges_text_overlay_clip_create_track_element (GESClip * clip,
    GESTrackType type);

static void
ges_text_overlay_clip_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTextOverlayClipPrivate *priv = GES_OVERLAY_TEXT_CLIP (object)->priv;

  switch (property_id) {
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
ges_text_overlay_clip_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTextOverlayClip *uriclip = GES_OVERLAY_TEXT_CLIP (object);

  switch (property_id) {
    case PROP_TEXT:
      ges_text_overlay_clip_set_text (uriclip, g_value_get_string (value));
      break;
    case PROP_FONT_DESC:
      ges_text_overlay_clip_set_font_desc (uriclip, g_value_get_string (value));
      break;
    case PROP_HALIGNMENT:
      ges_text_overlay_clip_set_halign (uriclip, g_value_get_enum (value));
      break;
    case PROP_VALIGNMENT:
      ges_text_overlay_clip_set_valign (uriclip, g_value_get_enum (value));
      break;
    case PROP_COLOR:
      ges_text_overlay_clip_set_color (uriclip, g_value_get_uint (value));
      break;
    case PROP_XPOS:
      ges_text_overlay_clip_set_xpos (uriclip, g_value_get_double (value));
      break;
    case PROP_YPOS:
      ges_text_overlay_clip_set_ypos (uriclip, g_value_get_double (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_text_overlay_clip_dispose (GObject * object)
{
  GESTextOverlayClipPrivate *priv = GES_OVERLAY_TEXT_CLIP (object)->priv;

  if (priv->text)
    g_free (priv->text);
  if (priv->font_desc)
    g_free (priv->font_desc);

  G_OBJECT_CLASS (ges_text_overlay_clip_parent_class)->dispose (object);
}

static void
ges_text_overlay_clip_class_init (GESTextOverlayClipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESClipClass *timobj_class = GES_CLIP_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTextOverlayClipPrivate));

  object_class->get_property = ges_text_overlay_clip_get_property;
  object_class->set_property = ges_text_overlay_clip_set_property;
  object_class->dispose = ges_text_overlay_clip_dispose;

  /**
   * GESTextOverlayClip:text:
   *
   * The text to diplay
   */

  g_object_class_install_property (object_class, PROP_TEXT,
      g_param_spec_string ("text", "Text", "The text to display",
          DEFAULT_PROP_TEXT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTextOverlayClip:font-desc:
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
   * GESTextOverlayClip:valignment:
   *
   * Vertical alignent of the text
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_VALIGNMENT,
      g_param_spec_enum ("valignment", "vertical alignment",
          "Vertical alignment of the text", GES_TEXT_VALIGN_TYPE,
          DEFAULT_PROP_VALIGNMENT, G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
          G_PARAM_STATIC_STRINGS));
  /**
   * GESTextOverlayClip:halignment:
   *
   * Horizontal alignment of the text
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_HALIGNMENT,
      g_param_spec_enum ("halignment", "horizontal alignment",
          "Horizontal alignment of the text",
          GES_TEXT_HALIGN_TYPE, DEFAULT_PROP_HALIGNMENT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  timobj_class->create_track_element =
      ges_text_overlay_clip_create_track_element;

  /**
   * GESTextOverlayClip:color:
   *
   * The color of the text
   */

  g_object_class_install_property (object_class, PROP_COLOR,
      g_param_spec_uint ("color", "Color", "The color of the text",
          0, G_MAXUINT32, G_MAXUINT32, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTextOverlayClip:xpos:
   *
   * The horizontal position of the text
   */

  g_object_class_install_property (object_class, PROP_XPOS,
      g_param_spec_double ("xpos", "Xpos", "The horizontal position",
          0, 1, 0.5, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTextOverlayClip:ypos:
   *
   * The vertical position of the text
   */

  g_object_class_install_property (object_class, PROP_YPOS,
      g_param_spec_double ("ypos", "Ypos", "The vertical position",
          0, 1, 0.5, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
}

static void
ges_text_overlay_clip_init (GESTextOverlayClip * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_OVERLAY_TEXT_CLIP, GESTextOverlayClipPrivate);

  GES_TIMELINE_ELEMENT (self)->duration = 0;
  /* Not 100% needed since gobject contents are memzero'd when created */
  self->priv->text = NULL;
  self->priv->font_desc = NULL;
  self->priv->halign = DEFAULT_PROP_HALIGNMENT;
  self->priv->valign = DEFAULT_PROP_VALIGNMENT;
  self->priv->color = G_MAXUINT32;
  self->priv->xpos = 0.5;
  self->priv->ypos = 0.5;
}

/**
 * ges_text_overlay_clip_set_text:
 * @self: the #GESTextOverlayClip* to set text on
 * @text: the text to render. an internal copy of this text will be
 * made.
 *
 * Sets the text this clip will render.
 *
 */
void
ges_text_overlay_clip_set_text (GESTextOverlayClip * self, const gchar * text)
{
  GList *tmp;

  GST_DEBUG ("self:%p, text:%s", self, text);

  if (self->priv->text)
    g_free (self->priv->text);

  self->priv->text = g_strdup (text);

  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;

    if (ges_track_element_get_track (trackelement)->type ==
        GES_TRACK_TYPE_VIDEO)
      ges_text_overlay_set_text (GES_TEXT_OVERLAY (trackelement),
          self->priv->text);
  }
}

/**
 * ges_text_overlay_clip_set_font_desc:
 * @self: the #GESTextOverlayClip*
 * @font_desc: the pango font description
 *
 * Sets the pango font description of the text
 *
 */
void
ges_text_overlay_clip_set_font_desc (GESTextOverlayClip * self,
    const gchar * font_desc)
{
  GList *tmp;

  GST_DEBUG ("self:%p, font_desc:%s", self, font_desc);

  if (self->priv->font_desc)
    g_free (self->priv->font_desc);

  self->priv->font_desc = g_strdup (font_desc);

  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;

    if (ges_track_element_get_track (trackelement)->type ==
        GES_TRACK_TYPE_VIDEO)
      ges_text_overlay_set_font_desc (GES_TEXT_OVERLAY
          (trackelement), self->priv->font_desc);
  }

}

/**
 * ges_text_overlay_clip_set_halign:
 * @self: the #GESTextOverlayClip* to set horizontal alignement of text on
 * @halign: #GESTextHAlign
 *
 * Sets the horizontal aligment of the text.
 *
 */
void
ges_text_overlay_clip_set_halign (GESTextOverlayClip * self,
    GESTextHAlign halign)
{
  GList *tmp;

  GST_DEBUG ("self:%p, halign:%d", self, halign);

  self->priv->halign = halign;

  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;

    if (ges_track_element_get_track (trackelement)->type ==
        GES_TRACK_TYPE_VIDEO)
      ges_text_overlay_set_halignment (GES_TEXT_OVERLAY
          (trackelement), self->priv->halign);
  }

}

/**
 * ges_text_overlay_clip_set_valign:
 * @self: the #GESTextOverlayClip* to set vertical alignement of text on
 * @valign: #GESTextVAlign
 *
 * Sets the vertical aligment of the text.
 *
 */
void
ges_text_overlay_clip_set_valign (GESTextOverlayClip * self,
    GESTextVAlign valign)
{
  GList *tmp;

  GST_DEBUG ("self:%p, valign:%d", self, valign);

  self->priv->valign = valign;

  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;

    if (ges_track_element_get_track (trackelement)->type ==
        GES_TRACK_TYPE_VIDEO)
      ges_text_overlay_set_valignment (GES_TEXT_OVERLAY
          (trackelement), self->priv->valign);
  }

}

/**
 * ges_text_overlay_clip_set_color:
 * @self: the #GESTextOverlayClip* to set
 * @color: The color @self is being set to
 *
 * Sets the color of the text.
 */
void
ges_text_overlay_clip_set_color (GESTextOverlayClip * self, guint32 color)
{
  GList *tmp;

  GST_DEBUG ("self:%p, color:%d", self, color);

  self->priv->color = color;

  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;

    if (ges_track_element_get_track (trackelement)->type ==
        GES_TRACK_TYPE_VIDEO)
      ges_text_overlay_set_color (GES_TEXT_OVERLAY (trackelement),
          self->priv->color);
  }
}

/**
 * ges_text_overlay_clip_set_xpos:
 * @self: the #GESTextOverlayClip* to set
 * @position: The horizontal position @self is being set to
 *
 * Sets the horizontal position of the text.
 */
void
ges_text_overlay_clip_set_xpos (GESTextOverlayClip * self, gdouble position)
{
  GList *tmp;

  GST_DEBUG ("self:%p, xpos:%f", self, position);

  self->priv->xpos = position;

  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;

    if (ges_track_element_get_track (trackelement)->type ==
        GES_TRACK_TYPE_VIDEO)
      ges_text_overlay_set_xpos (GES_TEXT_OVERLAY (trackelement),
          self->priv->xpos);
  }
}

/**
 * ges_text_overlay_clip_set_ypos:
 * @self: the #GESTextOverlayClip* to set
 * @position: The vertical position @self is being set to
 *
 * Sets the vertical position of the text.
 */
void
ges_text_overlay_clip_set_ypos (GESTextOverlayClip * self, gdouble position)
{
  GList *tmp;

  GST_DEBUG ("self:%p, ypos:%f", self, position);

  self->priv->ypos = position;

  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    GESTrackElement *trackelement = (GESTrackElement *) tmp->data;

    if (ges_track_element_get_track (trackelement)->type ==
        GES_TRACK_TYPE_VIDEO)
      ges_text_overlay_set_ypos (GES_TEXT_OVERLAY (trackelement),
          self->priv->ypos);
  }
}

/**
 * ges_text_overlay_clip_get_text:
 * @self: a #GESTextOverlayClip
 *
 * Get the text currently set on @self.
 *
 * Returns: The text currently set on @self.
 *
 */
const gchar *
ges_text_overlay_clip_get_text (GESTextOverlayClip * self)
{
  return self->priv->text;
}

/**
 * ges_text_overlay_clip_get_font_desc:
 * @self: a #GESTextOverlayClip
 *
 * Get the pango font description used by @self.
 *
 * Returns: The pango font description used by @self.
 */
const char *
ges_text_overlay_clip_get_font_desc (GESTextOverlayClip * self)
{
  return self->priv->font_desc;
}

/**
 * ges_text_overlay_clip_get_halignment:
 * @self: a #GESTextOverlayClip
 *
 * Get the horizontal aligment used by @self.
 *
 * Returns: The horizontal aligment used by @self.
 */
GESTextHAlign
ges_text_overlay_clip_get_halignment (GESTextOverlayClip * self)
{
  return self->priv->halign;
}

/**
 * ges_text_overlay_clip_get_valignment:
 * @self: a #GESTextOverlayClip
 *
 * Get the vertical aligment used by @self.
 *
 * Returns: The vertical aligment used by @self.
 */
GESTextVAlign
ges_text_overlay_clip_get_valignment (GESTextOverlayClip * self)
{
  return self->priv->valign;
}

/**
 * ges_text_overlay_clip_get_color:
 * @self: a #GESTextOverlayClip
 *
 * Get the color used by @source.
 *
 * Returns: The color used by @source.
 */

const guint32
ges_text_overlay_clip_get_color (GESTextOverlayClip * self)
{
  return self->priv->color;
}

/**
 * ges_text_overlay_clip_get_xpos:
 * @self: a #GESTextOverlayClip
 *
 * Get the horizontal position used by @source.
 *
 * Returns: The horizontal position used by @source.
 */

const gdouble
ges_text_overlay_clip_get_xpos (GESTextOverlayClip * self)
{
  return self->priv->xpos;
}

/**
 * ges_text_overlay_clip_get_ypos:
 * @self: a #GESTextOverlayClip
 *
 * Get the vertical position used by @source.
 *
 * Returns: The vertical position used by @source.
 */

const gdouble
ges_text_overlay_clip_get_ypos (GESTextOverlayClip * self)
{
  return self->priv->ypos;
}

static GESTrackElement *
ges_text_overlay_clip_create_track_element (GESClip * clip, GESTrackType type)
{

  GESTextOverlayClipPrivate *priv = GES_OVERLAY_TEXT_CLIP (clip)->priv;
  GESTrackElement *res = NULL;

  GST_DEBUG ("Creating a GESTrackOverlay");

  if (type == GES_TRACK_TYPE_VIDEO) {
    res = (GESTrackElement *) ges_text_overlay_new ();
    GST_DEBUG ("Setting text property");
    ges_text_overlay_set_text ((GESTextOverlay *) res, priv->text);
    ges_text_overlay_set_font_desc ((GESTextOverlay *) res, priv->font_desc);
    ges_text_overlay_set_halignment ((GESTextOverlay *) res, priv->halign);
    ges_text_overlay_set_valignment ((GESTextOverlay *) res, priv->valign);
    ges_text_overlay_set_color ((GESTextOverlay *) res, priv->color);
    ges_text_overlay_set_xpos ((GESTextOverlay *) res, priv->xpos);
    ges_text_overlay_set_ypos ((GESTextOverlay *) res, priv->ypos);
  }

  return res;
}

/**
 * ges_text_overlay_clip_new:
 *
 * Creates a new #GESTextOverlayClip
 *
 * Returns: The newly created #GESTextOverlayClip, or NULL if there was an
 * error.
 */
GESTextOverlayClip *
ges_text_overlay_clip_new (void)
{
  GESTextOverlayClip *new_clip;
  GESAsset *asset = ges_asset_request (GES_TYPE_OVERLAY_TEXT_CLIP, NULL, NULL);

  new_clip = GES_OVERLAY_TEXT_CLIP (ges_asset_extract (asset, NULL));
  gst_object_unref (asset);

  return new_clip;
}
