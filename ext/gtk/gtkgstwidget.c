/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "gtkgstwidget.h"
#include <gst/video/video.h>

/**
 * SECTION:gtkgstwidget
 * @short_description: a #GtkWidget that renders GStreamer video #GstBuffers
 * @see_also: #GtkDrawingArea, #GstBuffer
 *
 * #GtkGstWidget is an #GtkWidget that renders GStreamer video buffers.
 */

G_DEFINE_TYPE (GtkGstWidget, gtk_gst_widget, GTK_TYPE_DRAWING_AREA);

#define GTK_GST_WIDGET_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
    GTK_TYPE_GST_WIDGET, GtkGstWidgetPrivate))

struct _GtkGstWidgetPrivate
{
  GMutex lock;

  gboolean negotiated;
  GstBuffer *buffer;
  GstCaps *caps;
  GstVideoInfo v_info;
};

static void
gtk_gst_widget_get_preferred_width (GtkWidget * widget, gint * min,
    gint * natural)
{
  GtkGstWidget *gst_widget = (GtkGstWidget *) widget;
  gint video_width = GST_VIDEO_INFO_WIDTH (&gst_widget->priv->v_info);

  if (!gst_widget->priv->negotiated)
    video_width = 10;

  if (min)
    *min = 1;
  if (natural)
    *natural = video_width;
}

static void
gtk_gst_widget_get_preferred_height (GtkWidget * widget, gint * min,
    gint * natural)
{
  GtkGstWidget *gst_widget = (GtkGstWidget *) widget;
  gint video_height = GST_VIDEO_INFO_HEIGHT (&gst_widget->priv->v_info);

  if (!gst_widget->priv->negotiated)
    video_height = 10;

  if (min)
    *min = 1;
  if (natural)
    *natural = video_height;
}

static gboolean
gtk_gst_widget_draw (GtkWidget * widget, cairo_t * cr)
{
  GtkGstWidget *gst_widget = (GtkGstWidget *) widget;
  guint widget_width, widget_height;
  cairo_surface_t *surface;
  GstVideoFrame frame;

  widget_width = gtk_widget_get_allocated_width (widget);
  widget_height = gtk_widget_get_allocated_height (widget);

  g_mutex_lock (&gst_widget->priv->lock);

  /* failed to map the video frame */
  if (gst_widget->priv->negotiated && gst_widget->priv->buffer
      && gst_video_frame_map (&frame, &gst_widget->priv->v_info,
          gst_widget->priv->buffer, GST_MAP_READ)) {
    gdouble scale_x =
        (gdouble) widget_width / GST_VIDEO_INFO_WIDTH (&frame.info);
    gdouble scale_y =
        (gdouble) widget_height / GST_VIDEO_INFO_HEIGHT (&frame.info);

    gst_widget->priv->v_info = frame.info;

    surface = cairo_image_surface_create_for_data (frame.data[0],
        CAIRO_FORMAT_ARGB32, frame.info.width, frame.info.height,
        frame.info.stride[0]);

    cairo_scale (cr, scale_x, scale_y);
    cairo_rectangle (cr, 0, 0, widget_width, widget_height);
    cairo_set_source_surface (cr, surface, 0, 0);
    cairo_paint (cr);

    cairo_surface_destroy (surface);

    gst_video_frame_unmap (&frame);
  } else {
    GdkRGBA color;

    gtk_style_context_get_color (gtk_widget_get_style_context (widget), 0,
        &color);
    gdk_cairo_set_source_rgba (cr, &color);
    cairo_rectangle (cr, 0, 0, widget_width, widget_height);
    cairo_fill (cr);
  }

  g_mutex_unlock (&gst_widget->priv->lock);
  return FALSE;
}

static void
gtk_gst_widget_finalize (GObject * object)
{
  GtkGstWidget *widget = GTK_GST_WIDGET_CAST (object);

  gst_buffer_replace (&widget->priv->buffer, NULL);
  g_mutex_clear (&widget->priv->lock);

  G_OBJECT_CLASS (gtk_gst_widget_parent_class)->finalize (object);
}

static void
gtk_gst_widget_class_init (GtkGstWidgetClass * klass)
{
  GtkWidgetClass *widget_klass = (GtkWidgetClass *) klass;

  g_type_class_add_private (klass, sizeof (GtkGstWidgetPrivate));

  widget_klass->draw = gtk_gst_widget_draw;
  widget_klass->get_preferred_width = gtk_gst_widget_get_preferred_width;
  widget_klass->get_preferred_height = gtk_gst_widget_get_preferred_height;

  G_OBJECT_CLASS (klass)->finalize = gtk_gst_widget_finalize;
}

static void
gtk_gst_widget_init (GtkGstWidget * widget)
{
  widget->priv = GTK_GST_WIDGET_GET_PRIVATE (widget);

  g_mutex_init (&widget->priv->lock);
}

GtkWidget *
gtk_gst_widget_new (void)
{
  return (GtkWidget *) g_object_new (GTK_TYPE_GST_WIDGET, NULL);
}

static gboolean
_queue_draw (GtkGstWidget * widget)
{
  gtk_widget_queue_draw (GTK_WIDGET (widget));

  return G_SOURCE_REMOVE;
}

void
gtk_gst_widget_set_buffer (GtkGstWidget * widget, GstBuffer * buffer)
{
  GMainContext *main_context = g_main_context_default ();

  g_return_if_fail (GTK_IS_GST_WIDGET (widget));
  g_return_if_fail (widget->priv->negotiated);

  g_mutex_lock (&widget->priv->lock);

  gst_buffer_replace (&widget->priv->buffer, buffer);

  g_mutex_unlock (&widget->priv->lock);

  g_main_context_invoke (main_context, (GSourceFunc) _queue_draw, widget);
}

static gboolean
_queue_resize (GtkGstWidget * widget)
{
  gtk_widget_queue_resize (GTK_WIDGET (widget));

  return G_SOURCE_REMOVE;
}

gboolean
gtk_gst_widget_set_caps (GtkGstWidget * widget, GstCaps * caps)
{
  GMainContext *main_context = g_main_context_default ();
  GstVideoInfo v_info;

  g_return_val_if_fail (GTK_IS_GST_WIDGET (widget), FALSE);
  g_return_val_if_fail (GST_IS_CAPS (caps), FALSE);
  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  if (widget->priv->caps && gst_caps_is_equal_fixed (widget->priv->caps, caps))
    return TRUE;

  if (!gst_video_info_from_caps (&v_info, caps))
    return FALSE;

  /* FIXME: support other formats */
  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&v_info) ==
      GST_VIDEO_FORMAT_BGRA, FALSE);

  g_mutex_lock (&widget->priv->lock);

  gst_caps_replace (&widget->priv->caps, caps);
  widget->priv->v_info = v_info;
  widget->priv->negotiated = TRUE;

  g_mutex_unlock (&widget->priv->lock);

  gtk_widget_queue_resize (GTK_WIDGET (widget));

  g_main_context_invoke (main_context, (GSourceFunc) _queue_resize, widget);

  return TRUE;
}
