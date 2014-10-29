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
 * SECTION:gestitlesource
 * @short_description: render stand-alone text titles
 * 
 */

#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-title-source.h"
#include "ges-video-test-source.h"

G_DEFINE_TYPE (GESTitleSource, ges_title_source, GES_TYPE_VIDEO_SOURCE);

struct _GESTitleSourcePrivate
{
  gchar *text;
  gchar *font_desc;
  GESTextHAlign halign;
  GESTextVAlign valign;
  guint32 color;
  guint32 background;
  gdouble xpos;
  gdouble ypos;
  GstElement *text_el;
  GstElement *background_el;
};

enum
{
  PROP_0,
};

static void ges_title_source_dispose (GObject * object);

static void ges_title_source_get_property (GObject * object, guint
    property_id, GValue * value, GParamSpec * pspec);

static void ges_title_source_set_property (GObject * object, guint
    property_id, const GValue * value, GParamSpec * pspec);

static GstElement *ges_title_source_create_source (GESTrackElement * self);

static void
ges_title_source_class_init (GESTitleSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESVideoSourceClass *source_class = GES_VIDEO_SOURCE_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTitleSourcePrivate));

  object_class->get_property = ges_title_source_get_property;
  object_class->set_property = ges_title_source_set_property;
  object_class->dispose = ges_title_source_dispose;

  source_class->create_source = ges_title_source_create_source;
}

static void
ges_title_source_init (GESTitleSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TITLE_SOURCE, GESTitleSourcePrivate);

  self->priv->text = NULL;
  self->priv->font_desc = NULL;
  self->priv->text_el = NULL;
  self->priv->halign = DEFAULT_HALIGNMENT;
  self->priv->valign = DEFAULT_VALIGNMENT;
  self->priv->color = G_MAXUINT32;
  self->priv->background = G_MAXUINT32;
  self->priv->xpos = 0.5;
  self->priv->ypos = 0.5;
  self->priv->background_el = NULL;
}

static void
ges_title_source_dispose (GObject * object)
{
  GESTitleSource *self = GES_TITLE_SOURCE (object);
  if (self->priv->text) {
    g_free (self->priv->text);
  }

  if (self->priv->font_desc) {
    g_free (self->priv->font_desc);
  }

  if (self->priv->text_el) {
    gst_object_unref (self->priv->text_el);
    self->priv->text_el = NULL;
  }

  if (self->priv->background_el) {
    gst_object_unref (self->priv->background_el);
    self->priv->background_el = NULL;
  }

  G_OBJECT_CLASS (ges_title_source_parent_class)->dispose (object);
}

static void
ges_title_source_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_title_source_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static GstElement *
ges_title_source_create_source (GESTrackElement * object)
{
  GstElement *topbin, *background, *text;
  GstPad *src, *pad;

  GESTitleSource *self = GES_TITLE_SOURCE (object);
  GESTitleSourcePrivate *priv = self->priv;
  const gchar *bg_props[] = { "pattern", "foreground-color", NULL };
  const gchar *text_props[] = { "text", "font-desc", "valignment", "halignment",
    "color", "xpos", "ypos", NULL
  };

  topbin = gst_bin_new ("titlesrc-bin");
  background = gst_element_factory_make ("videotestsrc", "titlesrc-bg");

  text = gst_element_factory_make ("textoverlay", "titlsrc-text");
  if (priv->text) {
    g_object_set (text, "text", priv->text, NULL);
  }
  if (priv->font_desc) {
    g_object_set (text, "font-desc", priv->font_desc, NULL);
  }
  g_object_set (text, "valignment", (gint) priv->valign, "halignment",
      (gint) priv->halign, NULL);
  g_object_set (text, "color", (guint) self->priv->color, NULL);
  g_object_set (text, "xpos", (gdouble) self->priv->xpos, NULL);
  g_object_set (text, "ypos", (gdouble) self->priv->ypos, NULL);


  g_object_set (background, "pattern", (gint) GES_VIDEO_TEST_PATTERN_SOLID,
      NULL);
  g_object_set (background, "foreground-color", (guint) self->priv->background,
      NULL);

  gst_bin_add_many (GST_BIN (topbin), background, text, NULL);

  gst_element_link_pads_full (background, "src", text, "video_sink",
      GST_PAD_LINK_CHECK_NOTHING);

  pad = gst_element_get_static_pad (text, "src");
  src = gst_ghost_pad_new ("src", pad);
  gst_object_unref (pad);
  gst_element_add_pad (topbin, src);

  gst_object_ref (text);
  gst_object_ref (background);

  priv->text_el = text;
  priv->background_el = background;

  ges_track_element_add_children_props (object, text, NULL, NULL, text_props);
  ges_track_element_add_children_props (object, background, NULL, NULL,
      bg_props);

  return topbin;
}

/**
 * ges_title_source_set_text:
 * @self: the #GESTitleSource* to set text on
 * @text: the text to render. an internal copy of this text will be
 * made.
 * 
 * Sets the text this track element will render.
 *
 * Deprecated: use ges_track_element_get/set_children_properties on the
 * GESTrackElement instead
 */

void
ges_title_source_set_text (GESTitleSource * self, const gchar * text)
{
  if (self->priv->text)
    g_free (self->priv->text);

  GST_DEBUG ("self:%p, text:%s", self, text);

  self->priv->text = g_strdup (text);
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "text", text, NULL);
}

/**
 * ges_title_source_set_font_desc:
 * @self: the #GESTitleSource
 * @font_desc: the pango font description
 * 
 * Set the pango font description this source will use to render
 * the text.
 */

void
ges_title_source_set_font_desc (GESTitleSource * self, const gchar * font_desc)
{
  if (self->priv->font_desc)
    g_free (self->priv->font_desc);

  GST_DEBUG ("self:%p, font_dec:%s", self, font_desc);

  self->priv->font_desc = g_strdup (font_desc);
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "font-desc", font_desc, NULL);
}

/**
 * ges_title_source_set_valignment:
 * @self: the #GESTitleSource* to set text on
 * @valign: #GESTextVAlign
 *
 * Sets the vertical aligment of the text.
 */
void
ges_title_source_set_valignment (GESTitleSource * self, GESTextVAlign valign)
{
  GST_DEBUG ("self:%p, valign:%d", self, valign);

  self->priv->valign = valign;
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "valignment", valign, NULL);
}

/**
 * ges_title_source_set_halignment:
 * @self: the #GESTitleSource* to set text on
 * @halign: #GESTextHAlign
 *
 * Sets the vertical aligment of the text.
 */
void
ges_title_source_set_halignment (GESTitleSource * self, GESTextHAlign halign)
{
  GST_DEBUG ("self:%p, halign:%d", self, halign);

  self->priv->halign = halign;
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "halignment", halign, NULL);
}

/**
 * ges_title_source_set_text_color:
 * @self: the #GESTitleSource* to set
 * @color: the color @self is being set to
 *
 * Sets the color of the text.
 */
void
ges_title_source_set_text_color (GESTitleSource * self, guint32 color)
{
  GST_DEBUG ("self:%p, color:%d", self, color);

  self->priv->color = color;
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "color", color, NULL);
}

/**
 * ges_title_source_set_background_color:
 * @self: the #GESTitleSource* to set
 * @color: the color @self is being set to
 *
 * Sets the color of the background
 */
void
ges_title_source_set_background_color (GESTitleSource * self, guint32 color)
{
  GST_DEBUG ("self:%p, background color:%d", self, color);

  self->priv->background = color;
  if (self->priv->background_el)
    g_object_set (self->priv->background_el, "foreground-color", color, NULL);
}

/**
 * ges_title_source_set_xpos:
 * @self: the #GESTitleSource* to set
 * @position: the horizontal position @self is being set to
 *
 * Sets the horizontal position of the text.
 */
void
ges_title_source_set_xpos (GESTitleSource * self, gdouble position)
{
  GST_DEBUG ("self:%p, xpos:%f", self, position);

  self->priv->xpos = position;
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "xpos", position, NULL);
}

/**
 * ges_title_source_set_ypos:
 * @self: the #GESTitleSource* to set
 * @position: the color @self is being set to
 *
 * Sets the vertical position of the text.
 */
void
ges_title_source_set_ypos (GESTitleSource * self, gdouble position)
{
  GST_DEBUG ("self:%p, ypos:%f", self, position);

  self->priv->ypos = position;
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "ypos", position, NULL);
}

/**
 * ges_title_source_get_text:
 * @source: a #GESTitleSource
 *
 * Get the text currently set on the @source.
 *
 * Returns: (transfer none): The text currently set on the @source.
 */
const gchar *
ges_title_source_get_text (GESTitleSource * source)
{
  gchar *text;

  ges_track_element_get_child_properties (GES_TRACK_ELEMENT (source), "text",
      &text, NULL);

  return text;
}

/**
 * ges_title_source_get_font_desc:
 * @source: a #GESTitleSource
 *
 * Get the pango font description used by @source.
 *
 * Returns: (transfer none): The pango font description used by this
 * @source.
 */
const gchar *
ges_title_source_get_font_desc (GESTitleSource * source)
{
  gchar *font_desc;

  ges_track_element_get_child_properties (GES_TRACK_ELEMENT (source),
      "font-desc", &font_desc, NULL);

  return font_desc;
}

/**
 * ges_title_source_get_halignment:
 * @source: a #GESTitleSource
 *
 * Get the horizontal aligment used by @source.
 *
 * Returns: The horizontal aligment used by @source.
 */
GESTextHAlign
ges_title_source_get_halignment (GESTitleSource * source)
{
  GESTextHAlign halign;

  ges_track_element_get_child_properties (GES_TRACK_ELEMENT (source),
      "halignment", &halign, NULL);

  return halign;
}

/**
 * ges_title_source_get_valignment:
 * @source: a #GESTitleSource
 *
 * Get the vertical aligment used by @source.
 *
 * Returns: The vertical aligment used by @source.
 */
GESTextVAlign
ges_title_source_get_valignment (GESTitleSource * source)
{
  GESTextVAlign valign;

  ges_track_element_get_child_properties (GES_TRACK_ELEMENT (source),
      "valignment", &valign, NULL);

  return valign;
}

/**
 * ges_title_source_get_text_color:
 * @source: a #GESTitleSource
 *
 * Get the color used by @source.
 *
 * Returns: The color used by @source.
 */
const guint32
ges_title_source_get_text_color (GESTitleSource * source)
{
  guint32 color;

  ges_track_element_get_child_properties (GES_TRACK_ELEMENT (source), "color",
      &color, NULL);

  return color;
}

/**
 * ges_title_source_get_background_color:
 * @source: a #GESTitleSource
 *
 * Get the background used by @source.
 *
 * Returns: The background used by @source.
 */
const guint32
ges_title_source_get_background_color (GESTitleSource * source)
{
  guint32 color;

  ges_track_element_get_child_properties (GES_TRACK_ELEMENT (source),
      "foreground-color", &color, NULL);

  return color;
}

/**
 * ges_title_source_get_xpos:
 * @source: a #GESTitleSource
 *
 * Get the horizontal position used by @source.
 *
 * Returns: The horizontal position used by @source.
 */
const gdouble
ges_title_source_get_xpos (GESTitleSource * source)
{
  gdouble xpos;

  ges_track_element_get_child_properties (GES_TRACK_ELEMENT (source), "xpos",
      &xpos, NULL);

  return xpos;
}

/**
 * ges_title_source_get_ypos:
 * @source: a #GESTitleSource
 *
 * Get the vertical position used by @source.
 *
 * Returns: The vertical position used by @source.
 */
const gdouble
ges_title_source_get_ypos (GESTitleSource * source)
{
  gdouble ypos;

  ges_track_element_get_child_properties (GES_TRACK_ELEMENT (source), "ypos",
      &ypos, NULL);

  return ypos;
}

/**
 * ges_title_source_new:
 *
 * Creates a new #GESTitleSource.
 *
 * Returns: The newly created #GESTitleSource, or %NULL if there was an
 * error.
 */
GESTitleSource *
ges_title_source_new (void)
{
  return g_object_new (GES_TYPE_TITLE_SOURCE, "track-type",
      GES_TRACK_TYPE_VIDEO, NULL);
}
