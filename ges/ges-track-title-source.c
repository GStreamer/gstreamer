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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:ges-track-title-source
 * @short_description: render stand-alone text titles
 * 
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-title-source.h"
#include "ges-track-video-test-source.h"

G_DEFINE_TYPE (GESTrackTitleSource, ges_track_title_source,
    GES_TYPE_TRACK_SOURCE);

struct _GESTrackTitleSourcePrivate
{
  gchar *text;
  gchar *font_desc;
  GESTextHAlign halign;
  GESTextVAlign valign;
  guint32 color;
  GstElement *text_el;
  GstElement *background_el;
};

enum
{
  PROP_0,
};

static void ges_track_title_source_dispose (GObject * object);

static void ges_track_title_source_get_property (GObject * object, guint
    property_id, GValue * value, GParamSpec * pspec);

static void ges_track_title_source_set_property (GObject * object, guint
    property_id, const GValue * value, GParamSpec * pspec);

static GstElement *ges_track_title_source_create_element (GESTrackObject *
    self);

static void
ges_track_title_source_class_init (GESTrackTitleSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackObjectClass *bg_class = GES_TRACK_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTrackTitleSourcePrivate));

  object_class->get_property = ges_track_title_source_get_property;
  object_class->set_property = ges_track_title_source_set_property;
  object_class->dispose = ges_track_title_source_dispose;

  bg_class->create_element = ges_track_title_source_create_element;
}

static void
ges_track_title_source_init (GESTrackTitleSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK_TITLE_SOURCE, GESTrackTitleSourcePrivate);

  self->priv->text = NULL;
  self->priv->font_desc = NULL;
  self->priv->text_el = NULL;
  self->priv->halign = DEFAULT_HALIGNMENT;
  self->priv->valign = DEFAULT_VALIGNMENT;
  self->priv->color = G_MAXUINT32;
  self->priv->background_el = NULL;
}

static void
ges_track_title_source_dispose (GObject * object)
{
  GESTrackTitleSource *self = GES_TRACK_TITLE_SOURCE (object);
  if (self->priv->text) {
    g_free (self->priv->text);
  }

  if (self->priv->font_desc) {
    g_free (self->priv->font_desc);
  }

  if (self->priv->text_el) {
    g_object_unref (self->priv->text_el);
    self->priv->text_el = NULL;
  }

  if (self->priv->background_el) {
    g_object_unref (self->priv->background_el);
    self->priv->background_el = NULL;
  }

  G_OBJECT_CLASS (ges_track_title_source_parent_class)->dispose (object);
}

static void
ges_track_title_source_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_title_source_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static GstElement *
ges_track_title_source_create_element (GESTrackObject * object)
{
  GESTrackTitleSource *self = GES_TRACK_TITLE_SOURCE (object);
  GESTrackTitleSourcePrivate *priv = self->priv;
  GstElement *topbin, *background, *text;
  GstPad *src;

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

  g_object_set (background, "pattern", (gint) GES_VIDEO_TEST_PATTERN_BLACK,
      NULL);
  g_object_set (text, "color", (guint32) self->priv->color, NULL);

  gst_bin_add_many (GST_BIN (topbin), background, text, NULL);

  gst_element_link_pads_full (background, "src", text, "video_sink",
      GST_PAD_LINK_CHECK_NOTHING);

  src = gst_ghost_pad_new ("src", gst_element_get_static_pad (text, "src"));
  gst_element_add_pad (topbin, src);

  g_object_ref (text);
  g_object_ref (background);

  priv->text_el = text;
  priv->background_el = background;

  return topbin;
}

/**
 * ges_track_title_source_set_text:
 * @self: the #GESTrackTitleSource* to set text on
 * @text: the text to render. an internal copy of this text will be
 * made.
 * 
 * Sets the text this track object will render.
 */

void
ges_track_title_source_set_text (GESTrackTitleSource * self, const gchar * text)
{
  if (self->priv->text)
    g_free (self->priv->text);

  GST_DEBUG ("self:%p, text:%s", self, text);

  self->priv->text = g_strdup (text);
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "text", text, NULL);
}

/**
 * ges_track_title_source_set_font_desc:
 * @self: the #GESTrackTitleSource
 * @font_desc: the pango font description
 * 
 * Set the pango font description this source will use to render
 * the text.
 */

void
ges_track_title_source_set_font_desc (GESTrackTitleSource * self,
    const gchar * font_desc)
{
  if (self->priv->font_desc)
    g_free (self->priv->font_desc);

  GST_DEBUG ("self:%p, font_dec:%s", self, font_desc);

  self->priv->font_desc = g_strdup (font_desc);
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "font-desc", font_desc, NULL);
}

/**
 * ges_track_title_source_set_valignment:
 * @self: the #GESTrackTitleSource* to set text on
 * @valign: #GESTextVAlign
 *
 * Sets the vertical aligment of the text.
 */
void
ges_track_title_source_set_valignment (GESTrackTitleSource * self,
    GESTextVAlign valign)
{
  GST_DEBUG ("self:%p, valign:%d", self, valign);

  self->priv->valign = valign;
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "valignment", valign, NULL);
}

/**
 * ges_track_title_source_set_halignment:
 * @self: the #GESTrackTitleSource* to set text on
 * @halign: #GESTextHAlign
 *
 * Sets the vertical aligment of the text.
 */
void
ges_track_title_source_set_halignment (GESTrackTitleSource * self,
    GESTextHAlign halign)
{
  GST_DEBUG ("self:%p, halign:%d", self, halign);

  self->priv->halign = halign;
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "halignment", halign, NULL);
}

/**
 * ges_track_title_source_set_color:
 * @self: the #GESTrackTitleSource* to set
 * @color: the color @self is being set to
 *
 * Sets the color of the text.
 */
void
ges_track_title_source_set_color (GESTrackTitleSource * self, guint32 color)
{
  GST_DEBUG ("self:%p, color:%d", self, color);

  self->priv->color = color;
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "color", color, NULL);
}

/**
 * ges_track_title_source_get_text:
 * @source: a #GESTrackTitleSource
 *
 * Get the text currently set on the @source.
 *
 * Returns: (transfer none): The text currently set on the @source.
 */
const gchar *
ges_track_title_source_get_text (GESTrackTitleSource * source)
{
  return source->priv->text;
}

/**
 * ges_track_title_source_get_font_desc:
 * @source: a #GESTrackTitleSource
 *
 * Get the pango font description used by @source.
 *
 * Returns: (transfer none): The pango font description used by this
 * @source.
 */
const gchar *
ges_track_title_source_get_font_desc (GESTrackTitleSource * source)
{
  return source->priv->font_desc;
}

/**
 * ges_track_title_source_get_halignment:
 * @source: a #GESTrackTitleSource
 *
 * Get the horizontal aligment used by @source.
 *
 * Returns: The horizontal aligment used by @source.
 */
GESTextHAlign
ges_track_title_source_get_halignment (GESTrackTitleSource * source)
{
  return source->priv->halign;
}

/**
 * ges_track_title_source_get_valignment:
 * @source: a #GESTrackTitleSource
 *
 * Get the vertical aligment used by @source.
 *
 * Returns: The vertical aligment used by @source.
 */
GESTextVAlign
ges_track_title_source_get_valignment (GESTrackTitleSource * source)
{
  return source->priv->valign;
}

/**
 * ges_track_title_source_get_color:
 * @source: a #GESTrackTitleSource
 *
 * Get the color used by @source.
 *
 * Returns: The color used by @source.
 */
const guint32
ges_track_title_source_get_color (GESTrackTitleSource * source)
{
  return source->priv->color;
}


/**
 * ges_track_title_source_new:
 *
 * Creates a new #GESTrackTitleSource.
 *
 * Returns: The newly created #GESTrackTitleSource, or %NULL if there was an
 * error.
 */
GESTrackTitleSource *
ges_track_title_source_new (void)
{
  return g_object_new (GES_TYPE_TRACK_TITLE_SOURCE, NULL);
}
