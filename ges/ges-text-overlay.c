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
 * SECTION:gestextoverlay
 * @short_description: render text onto another video stream in a GESLayer
 *
 */

#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-title-source.h"
#include "ges-text-overlay.h"

G_DEFINE_TYPE (GESTextOverlay, ges_text_overlay, GES_TYPE_OPERATION);

struct _GESTextOverlayPrivate
{
  gchar *text;
  gchar *font_desc;
  GESTextHAlign halign;
  GESTextVAlign valign;
  guint32 color;
  gdouble xpos;
  gdouble ypos;
  GstElement *text_el;
};

enum
{
  PROP_0,
};

static void ges_text_overlay_dispose (GObject * object);

static void ges_text_overlay_finalize (GObject * object);

static void ges_text_overlay_get_property (GObject * object, guint
    property_id, GValue * value, GParamSpec * pspec);

static void ges_text_overlay_set_property (GObject * object, guint
    property_id, const GValue * value, GParamSpec * pspec);

static GstElement *ges_text_overlay_create_element (GESTrackElement * self);

static void
ges_text_overlay_class_init (GESTextOverlayClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackElementClass *bg_class = GES_TRACK_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTextOverlayPrivate));

  object_class->get_property = ges_text_overlay_get_property;
  object_class->set_property = ges_text_overlay_set_property;
  object_class->dispose = ges_text_overlay_dispose;
  object_class->finalize = ges_text_overlay_finalize;

  bg_class->create_element = ges_text_overlay_create_element;
}

static void
ges_text_overlay_init (GESTextOverlay * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TEXT_OVERLAY, GESTextOverlayPrivate);

  self->priv->text = NULL;
  self->priv->font_desc = NULL;
  self->priv->text_el = NULL;
  self->priv->halign = DEFAULT_HALIGNMENT;
  self->priv->valign = DEFAULT_VALIGNMENT;
  self->priv->color = G_MAXUINT32;
  self->priv->xpos = 0.5;
  self->priv->ypos = 0.5;
}

static void
ges_text_overlay_dispose (GObject * object)
{
  GESTextOverlay *self = GES_TEXT_OVERLAY (object);
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

  G_OBJECT_CLASS (ges_text_overlay_parent_class)->dispose (object);
}

static void
ges_text_overlay_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_text_overlay_parent_class)->finalize (object);
}

static void
ges_text_overlay_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_text_overlay_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static GstElement *
ges_text_overlay_create_element (GESTrackElement * track_element)
{
  GstElement *ret, *text, *iconv, *oconv;
  GstPad *src_target, *sink_target;
  GstPad *src, *sink;
  GESTextOverlay *self = GES_TEXT_OVERLAY (track_element);
  const gchar *child_props[] =
      { "xpos", "ypos", "deltax", "deltay", "auto-resize", "outline-color",
    NULL
  };

  text = gst_element_factory_make ("textoverlay", NULL);
  iconv = gst_element_factory_make ("videoconvert", NULL);
  oconv = gst_element_factory_make ("videoconvert", NULL);
  self->priv->text_el = text;
  gst_object_ref (text);

  if (self->priv->text)
    g_object_set (text, "text", (gchar *) self->priv->text, NULL);
  if (self->priv->font_desc)
    g_object_set (text, "font-desc", (gchar *) self->priv->font_desc, NULL);

  g_object_set (text, "halignment", (gint) self->priv->halign, "valignment",
      (gint) self->priv->valign, NULL);
  g_object_set (text, "color", (guint) self->priv->color, NULL);
  g_object_set (text, "xpos", (gdouble) self->priv->xpos, NULL);
  g_object_set (text, "ypos", (gdouble) self->priv->ypos, NULL);

  ges_track_element_add_children_props (track_element, text, NULL, NULL,
      child_props);

  ret = gst_bin_new ("overlay-bin");
  gst_bin_add_many (GST_BIN (ret), text, iconv, oconv, NULL);
  gst_element_link_many (iconv, text, oconv, NULL);

  src_target = gst_element_get_static_pad (oconv, "src");
  sink_target = gst_element_get_static_pad (iconv, "sink");

  src = gst_ghost_pad_new ("src", src_target);
  sink = gst_ghost_pad_new ("video_sink", sink_target);
  gst_object_unref (src_target);
  gst_object_unref (sink_target);

  gst_element_add_pad (ret, src);
  gst_element_add_pad (ret, sink);

  return ret;
}

/**
 * ges_text_overlay_set_text:
 * @self: the #GESTextOverlay* to set text on
 * @text: the text to render. an internal copy of this text will be
 * made.
 *
 * Sets the text this track element will render.
 *
 */
void
ges_text_overlay_set_text (GESTextOverlay * self, const gchar * text)
{
  GST_DEBUG ("self:%p, text:%s", self, text);

  if (self->priv->text)
    g_free (self->priv->text);

  self->priv->text = g_strdup (text);
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "text", text, NULL);
}

/**
 * ges_text_overlay_set_font_desc:
 * @self: the #GESTextOverlay
 * @font_desc: the pango font description
 *
 * Sets the pango font description of the text this track element
 * will render.
 *
 */
void
ges_text_overlay_set_font_desc (GESTextOverlay * self, const gchar * font_desc)
{
  GST_DEBUG ("self:%p, font_desc:%s", self, font_desc);

  if (self->priv->font_desc)
    g_free (self->priv->font_desc);

  self->priv->font_desc = g_strdup (font_desc);
  GST_LOG ("setting font-desc to '%s'", font_desc);
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "font-desc", font_desc, NULL);
}

/**
 * ges_text_overlay_set_valignment:
 * @self: the #GESTextOverlay* to set text on
 * @valign: The #GESTextVAlign defining the vertical alignment
 * of the text render by @self.
 *
 * Sets the vertical aligment of the text.
 *
 */
void
ges_text_overlay_set_valignment (GESTextOverlay * self, GESTextVAlign valign)
{
  GST_DEBUG ("self:%p, halign:%d", self, valign);

  self->priv->valign = valign;
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "valignment", valign, NULL);
}

/**
 * ges_text_overlay_set_halignment:
 * @self: the #GESTextOverlay* to set text on
 * @halign: The #GESTextHAlign defining the horizontal alignment
 * of the text render by @self.
 *
 * Sets the horizontal aligment of the text.
 *
 */
void
ges_text_overlay_set_halignment (GESTextOverlay * self, GESTextHAlign halign)
{
  GST_DEBUG ("self:%p, halign:%d", self, halign);

  self->priv->halign = halign;
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "halignment", halign, NULL);
}

/**
 * ges_text_overlay_set_color:
 * @self: the #GESTextOverlay* to set
 * @color: The color @self is being set to
 *
 * Sets the color of the text.
 */
void
ges_text_overlay_set_color (GESTextOverlay * self, guint32 color)
{
  GST_DEBUG ("self:%p, color:%d", self, color);

  self->priv->color = color;
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "color", color, NULL);
}

/**
 * ges_text_overlay_set_xpos:
 * @self: the #GESTextOverlay* to set
 * @position: The horizontal position @self is being set to
 *
 * Sets the horizontal position of the text.
 */
void
ges_text_overlay_set_xpos (GESTextOverlay * self, gdouble position)
{
  GST_DEBUG ("self:%p, xpos:%f", self, position);

  self->priv->xpos = position;
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "xpos", position, NULL);
}

/**
 * ges_text_overlay_set_ypos:
 * @self: the #GESTextOverlay* to set
 * @position: The vertical position @self is being set to
 *
 * Sets the vertical position of the text.
 */
void
ges_text_overlay_set_ypos (GESTextOverlay * self, gdouble position)
{
  GST_DEBUG ("self:%p, ypos:%f", self, position);

  self->priv->ypos = position;
  if (self->priv->text_el)
    g_object_set (self->priv->text_el, "ypos", position, NULL);
}

/**
 * ges_text_overlay_get_text:
 * @self: a GESTextOverlay
 *
 * Get the text currently set on @source.
 *
 * Returns: The text currently set on @source.
 */
const gchar *
ges_text_overlay_get_text (GESTextOverlay * self)
{
  return self->priv->text;
}

/**
 * ges_text_overlay_get_font_desc:
 * @self: a GESTextOverlay
 *
 * Get the pango font description currently set on @source.
 *
 * Returns: The pango font description currently set on @source.
 */
const char *
ges_text_overlay_get_font_desc (GESTextOverlay * self)
{
  return self->priv->font_desc;
}

/**
 * ges_text_overlay_get_halignment:
 * @self: a GESTextOverlay
 *
 * Get the horizontal aligment used by @source.
 *
 * Returns: The horizontal aligment used by @source.
 */
GESTextHAlign
ges_text_overlay_get_halignment (GESTextOverlay * self)
{
  return self->priv->halign;
}

/**
 * ges_text_overlay_get_valignment:
 * @self: a GESTextOverlay
 *
 * Get the vertical aligment used by @source.
 *
 * Returns: The vertical aligment used by @source.
 */
GESTextVAlign
ges_text_overlay_get_valignment (GESTextOverlay * self)
{
  return self->priv->valign;
}

/**
 * ges_text_overlay_get_color:
 * @self: a GESTextOverlay
 *
 * Get the color used by @source.
 *
 * Returns: The color used by @source.
 */
const guint32
ges_text_overlay_get_color (GESTextOverlay * self)
{
  return self->priv->color;
}

/**
 * ges_text_overlay_get_xpos:
 * @self: a GESTextOverlay
 *
 * Get the horizontal position used by @source.
 *
 * Returns: The horizontal position used by @source.
 */
const gdouble
ges_text_overlay_get_xpos (GESTextOverlay * self)
{
  return self->priv->xpos;
}

/**
 * ges_text_overlay_get_ypos:
 * @self: a GESTextOverlay
 *
 * Get the vertical position used by @source.
 *
 * Returns: The vertical position used by @source.
 */
const gdouble
ges_text_overlay_get_ypos (GESTextOverlay * self)
{
  return self->priv->ypos;
}

/**
 * ges_text_overlay_new:
 *
 * Creates a new #GESTextOverlay.
 *
 * Returns: The newly created #GESTextOverlay or %NULL if something went
 * wrong.
 */
GESTextOverlay *
ges_text_overlay_new (void)
{
  return g_object_new (GES_TYPE_TEXT_OVERLAY, "track-type",
      GES_TRACK_TYPE_VIDEO, NULL);
}
