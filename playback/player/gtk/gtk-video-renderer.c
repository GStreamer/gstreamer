/* GStreamer
 *
 * Copyright (C) 2015 Sebastian Dröge <sebastian@centricular.com>
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

#include "gtk-video-renderer.h"

struct _GstPlayGtkVideoRenderer
{
  GObject parent;

  GstElement *sink;
  GtkWidget *widget;
};

struct _GstPlayGtkVideoRendererClass
{
  GObjectClass parent_class;
};

static void
    gst_player_gtk_video_renderer_interface_init
    (GstPlayVideoRendererInterface * iface);

enum
{
  GTK_VIDEO_RENDERER_PROP_0,
  GTK_VIDEO_RENDERER_PROP_WIDGET,
  GTK_VIDEO_RENDERER_PROP_LAST
};

G_DEFINE_TYPE_WITH_CODE (GstPlayGtkVideoRenderer,
    gst_player_gtk_video_renderer, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_PLAY_VIDEO_RENDERER,
        gst_player_gtk_video_renderer_interface_init));

static GParamSpec
    * gtk_video_renderer_param_specs[GTK_VIDEO_RENDERER_PROP_LAST] = { NULL, };

static void
gst_player_gtk_video_renderer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstPlayGtkVideoRenderer *self = GST_PLAY_GTK_VIDEO_RENDERER (object);

  switch (prop_id) {
    case GTK_VIDEO_RENDERER_PROP_WIDGET:
      g_value_set_object (value, self->widget);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_player_gtk_video_renderer_finalize (GObject * object)
{
  GstPlayGtkVideoRenderer *self = GST_PLAY_GTK_VIDEO_RENDERER (object);

  if (self->sink)
    gst_object_unref (self->sink);
  if (self->widget)
    g_object_unref (self->widget);

  G_OBJECT_CLASS
      (gst_player_gtk_video_renderer_parent_class)->finalize (object);
}

static void
    gst_player_gtk_video_renderer_class_init
    (GstPlayGtkVideoRendererClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_player_gtk_video_renderer_get_property;
  gobject_class->finalize = gst_player_gtk_video_renderer_finalize;

  gtk_video_renderer_param_specs
      [GTK_VIDEO_RENDERER_PROP_WIDGET] =
      g_param_spec_object ("widget", "Widget",
      "Widget to render the video into", GTK_TYPE_WIDGET,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
      GTK_VIDEO_RENDERER_PROP_LAST, gtk_video_renderer_param_specs);
}

static void
gst_player_gtk_video_renderer_init (GstPlayGtkVideoRenderer * self)
{
  GstElement *gtk_sink = gst_element_factory_make ("gtkglsink", NULL);

  if (gtk_sink) {
    GstElement *sink = gst_element_factory_make ("glsinkbin", NULL);
    g_object_set (sink, "sink", gtk_sink, NULL);

    self->sink = sink;
  } else {
    gtk_sink = gst_element_factory_make ("gtksink", NULL);
    self->sink = gst_object_ref_sink (gtk_sink);
  }

  g_assert (self->sink != NULL);

  g_object_get (gtk_sink, "widget", &self->widget, NULL);
}

static GstElement *gst_player_gtk_video_renderer_create_video_sink
    (GstPlayVideoRenderer * iface, GstPlay * player)
{
  GstPlayGtkVideoRenderer *self = GST_PLAY_GTK_VIDEO_RENDERER (iface);

  return gst_object_ref (self->sink);
}

static void
    gst_player_gtk_video_renderer_interface_init
    (GstPlayVideoRendererInterface * iface)
{
  iface->create_video_sink = gst_player_gtk_video_renderer_create_video_sink;
}

/**
 * gst_play_gtk_video_renderer_new:
 *
 * Returns: (transfer full):
 */
GstPlayVideoRenderer *
gst_play_gtk_video_renderer_new (void)
{
  GstElementFactory *factory;

  factory = gst_element_factory_find ("gtkglsink");
  if (!factory)
    factory = gst_element_factory_find ("gtksink");
  if (!factory)
    return NULL;

  gst_object_unref (factory);

  return g_object_new (GST_TYPE_PLAY_GTK_VIDEO_RENDERER, NULL);
}

/**
 * gst_play_gtk_video_renderer_get_widget:
 * @self: #GstPlayVideoRenderer instance
 *
 * Returns: (transfer full): The GtkWidget
 */
GtkWidget *gst_play_gtk_video_renderer_get_widget
    (GstPlayGtkVideoRenderer * self)
{
  GtkWidget *widget;

  g_return_val_if_fail (GST_IS_PLAY_GTK_VIDEO_RENDERER (self), NULL);

  g_object_get (self, "widget", &widget, NULL);

  return widget;
}
