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
 * SECTION:ges-track-source
 * @short_description: Base Class for single-media sources
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-title-source.h"
#include "ges-track-video-overlay.h"

G_DEFINE_TYPE (GESTrackVideoOverlay, ges_track_video_overlay,
    GES_TYPE_TRACK_OVERLAY);

enum
{
  PROP_0,
};

static void ges_track_video_overlay_dispose (GObject * object);

static void ges_track_video_overlay_finalize (GObject * object);

static void ges_track_video_overlay_get_property (GObject * object, guint
    property_id, GValue * value, GParamSpec * pspec);

static void ges_track_video_overlay_set_property (GObject * object, guint
    property_id, const GValue * value, GParamSpec * pspec);

static GstElement *ges_track_video_overlay_create_element (GESTrackOverlay
    * self);

static void
ges_track_video_overlay_class_init (GESTrackVideoOverlayClass * klass)
{
  GObjectClass *object_class;
  GESTrackOverlayClass *bg_class;

  object_class = G_OBJECT_CLASS (klass);
  bg_class = GES_TRACK_OVERLAY_CLASS (klass);

  object_class->get_property = ges_track_video_overlay_get_property;
  object_class->set_property = ges_track_video_overlay_set_property;
  object_class->dispose = ges_track_video_overlay_dispose;
  object_class->finalize = ges_track_video_overlay_finalize;

  bg_class->create_element = ges_track_video_overlay_create_element;
}

static void
ges_track_video_overlay_init (GESTrackVideoOverlay * self)
{
  self->text = NULL;
  self->font_desc = g_strdup (DEFAULT_FONT_DESC);
  self->text_el = NULL;
  self->halign = DEFAULT_HALIGNMENT;
  self->valign = DEFAULT_VALIGNMENT;
}

static void
ges_track_video_overlay_dispose (GObject * object)
{
  GESTrackVideoOverlay *self = GES_TRACK_VIDEO_OVERLAY (object);
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

  G_OBJECT_CLASS (ges_track_video_overlay_parent_class)->dispose (object);
}

static void
ges_track_video_overlay_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_video_overlay_parent_class)->finalize (object);
}

static void
ges_track_video_overlay_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_video_overlay_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static GstElement *
ges_track_video_overlay_create_element (GESTrackOverlay * object)
{
  GstElement *ret, *text;
  GstPad *src_target, *sink_target;
  GstPad *src, *sink;
  GESTrackVideoOverlay *self = GES_TRACK_VIDEO_OVERLAY (object);

  text = gst_element_factory_make ("textoverlay", NULL);
  self->text_el = text;
  g_object_ref (text);

  if (self->text)
    g_object_set (text, "text", (gchar *) self->text, NULL);
  if (self->font_desc)
    g_object_set (text, "font-desc", (gchar *) self->font_desc, NULL);

  g_object_set (text, "halignment", (gint) self->halign, "valignment",
      (gint) self->valign, NULL);

  ret = gst_bin_new ("overlay-bin");
  gst_bin_add (GST_BIN (ret), text);

  src_target = gst_element_get_static_pad (text, "src");
  sink_target = gst_element_get_static_pad (text, "video_sink");

  src = gst_ghost_pad_new ("src", src_target);
  sink = gst_ghost_pad_new ("video_sink", sink_target);
  g_object_unref (src_target);
  g_object_unref (sink_target);

  gst_element_add_pad (ret, src);
  gst_element_add_pad (ret, sink);

  return ret;
}

/**
 * ges_track_video_overlay_set_text:
 * @self: the #GESTrackVideoOverlay* to set text on
 * @text: the text to render. an internal copy of this text will be
 * made.
 * 
 * Sets the text this track object will render.
 *
 */

void
ges_track_video_overlay_set_text (GESTrackVideoOverlay * self,
    const gchar * text)
{
  if (self->text)
    g_free (self->text);

  self->text = g_strdup (text);
  if (self->text_el)
    g_object_set (self->text_el, "text", text, NULL);
}

/**
 * ges_track_video_overlay_set_font_desc:
 * @self: the #GESTrackVideoOverlay
 * @font_desc: the pango font description
 * 
 * Sets the text this track object will render.
 *
 */

void
ges_track_video_overlay_set_font_desc (GESTrackVideoOverlay * self,
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
 * ges_track_video_overlay_valignment:
 * @self: the #GESTrackVideoOverlay* to set text on
 * @valign: #GESTrackVideoTitleSrcVAlign
 *
 * Sets the vertical aligment of the text.
 */
void
ges_track_video_overlay_set_valignment (GESTrackVideoOverlay * self,
    GESTrackVideoTitleSrcVAlign valign)
{
  self->valign = valign;
  GST_LOG ("set valignment to: %d", valign);
  if (self->text_el)
    g_object_set (self->text_el, "valignment", valign, NULL);
}

/**
 * ges_track_video_overlay_halignment:
 * @self: the #GESTrackVideoOverlay* to set text on
 * @halign: #GESTrackVideoTitleSrcHAlign
 *
 * Sets the vertical aligment of the text.
 */
void
ges_track_video_overlay_set_halignment (GESTrackVideoOverlay * self,
    GESTrackVideoTitleSrcHAlign halign)
{
  self->halign = halign;
  GST_LOG ("set halignment to: %d", halign);
  if (self->text_el)
    g_object_set (self->text_el, "halignment", halign, NULL);
}

GESTrackVideoOverlay *
ges_track_video_overlay_new (void)
{
  return g_object_new (GES_TYPE_TRACK_VIDEO_OVERLAY, NULL);
}
