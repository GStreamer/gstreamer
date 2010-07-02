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

G_DEFINE_TYPE (GESTrackTitleSource, ges_track_title_src, GES_TYPE_TRACK_SOURCE);

enum
{
  PROP_0,
};

static void ges_track_title_src_dispose (GObject * object);

static void ges_track_title_src_finalize (GObject * object);

static void ges_track_title_src_get_property (GObject * object, guint
    property_id, GValue * value, GParamSpec * pspec);

static void ges_track_title_src_set_property (GObject * object, guint
    property_id, const GValue * value, GParamSpec * pspec);

static GstElement *ges_track_title_src_create_element (GESTrackSource * self);

static void
ges_track_title_src_class_init (GESTrackTitleSourceClass * klass)
{
  GObjectClass *object_class;
  GESTrackSourceClass *bg_class;

  object_class = G_OBJECT_CLASS (klass);
  bg_class = GES_TRACK_SOURCE_CLASS (klass);

  object_class->get_property = ges_track_title_src_get_property;
  object_class->set_property = ges_track_title_src_set_property;
  object_class->dispose = ges_track_title_src_dispose;
  object_class->finalize = ges_track_title_src_finalize;

  bg_class->create_element = ges_track_title_src_create_element;
}

static void
ges_track_title_src_init (GESTrackTitleSource * self)
{
  self->text = NULL;
  self->font_desc = NULL;
  self->text_el = NULL;
  self->halign = DEFAULT_HALIGNMENT;
  self->valign = DEFAULT_VALIGNMENT;
  self->background_el = NULL;
}

static void
ges_track_title_src_dispose (GObject * object)
{
  GESTrackTitleSource *self = GES_TRACK_TITLE_SOURCE (object);
  if (self->text) {
    g_free (self->text);
  }

  if (self->font_desc) {
    g_free (self->font_desc);
  }

  if (self->text_el) {
    g_object_unref (self->text_el);
    self->text_el = NULL;
  }

  if (self->background_el) {
    g_object_unref (self->background_el);
    self->background_el = NULL;
  }

  G_OBJECT_CLASS (ges_track_title_src_parent_class)->dispose (object);
}

static void
ges_track_title_src_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_title_src_parent_class)->finalize (object);
}

static void
ges_track_title_src_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_title_src_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static GstElement *
ges_track_title_src_create_element (GESTrackSource * object)
{
  GESTrackTitleSource *self = GES_TRACK_TITLE_SOURCE (object);
  GstElement *topbin, *background, *text;
  GstPad *src;

  topbin = gst_bin_new ("titlesrc-bin");
  background = gst_element_factory_make ("videotestsrc", "titlesrc-bg");

  text = gst_element_factory_make ("textoverlay", "titlsrc-text");
  if (self->text) {
    g_object_set (text, "text", self->text, NULL);
  }
  if (self->font_desc) {
    g_object_set (text, "font-desc", self->font_desc, NULL);
  }
  g_object_set (text, "valignment", (gint) self->valign, "halignment",
      (gint) self->halign, NULL);

  g_object_set (background, "pattern", (gint) GES_TRACK_VIDEO_BG_SRC_BLACK,
      NULL);

  gst_bin_add_many (GST_BIN (topbin), background, text, NULL);

  gst_element_link (background, text);

  src = gst_ghost_pad_new ("src", gst_element_get_static_pad (text, "src"));
  gst_element_add_pad (topbin, src);

  g_object_ref (text);
  g_object_ref (background);

  self->text_el = text;
  self->background_el = background;

  return topbin;
}

/**
 * ges_track_title_source_set_text:
 * @self: the #GESTrackTitleSource* to set text on
 * @text: the text to render. an internal copy of this text will be
 * made.
 * 
 * Sets the text this track object will render.
 *
 */

void
ges_track_title_source_set_text (GESTrackTitleSource * self, const gchar * text)
{
  if (self->text)
    g_free (self->text);

  self->text = g_strdup (text);
  if (self->text_el)
    g_object_set (self->text_el, "text", text, NULL);
}

/**
 * ges_track_title_source_set_font_desc:
 * @self: the #GESTrackTitleSource
 * @font_desc: the pango font description
 * 
 * Sets the text this track object will render.
 *
 */

void
ges_track_title_source_set_font_desc (GESTrackTitleSource * self,
    const gchar * font_desc)
{
  if (self->font_desc)
    g_free (self->font_desc);

  self->font_desc = g_strdup (font_desc);
  GST_LOG ("setting font-desc to '%s'", font_desc);
  if (self->text_el)
    g_object_set (self->text_el, "font-desc", font_desc, NULL);
}

/**
 * ges_track_title_source_valignment:
 * @self: the #GESTrackTitleSource* to set text on
 * @valign: #GESTextVAlign
 *
 * Sets the vertical aligment of the text.
 */
void
ges_track_title_source_set_valignment (GESTrackTitleSource * self,
    GESTextVAlign valign)
{
  self->valign = valign;
  GST_LOG ("set valignment to: %d", valign);
  if (self->text_el)
    g_object_set (self->text_el, "valignment", valign, NULL);
}

/**
 * ges_track_title_source_halignment:
 * @self: the #GESTrackTitleSource* to set text on
 * @halign: #GESTextHAlign
 *
 * Sets the vertical aligment of the text.
 */
void
ges_track_title_source_set_halignment (GESTrackTitleSource * self,
    GESTextHAlign halign)
{
  self->halign = halign;
  GST_LOG ("set halignment to: %d", halign);
  if (self->text_el)
    g_object_set (self->text_el, "halignment", halign, NULL);
}

GESTrackTitleSource *
ges_track_title_source_new (void)
{
  return g_object_new (GES_TYPE_TRACK_TITLE_SOURCE, NULL);
}
