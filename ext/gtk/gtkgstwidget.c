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

#define DEFAULT_FORCE_ASPECT_RATIO  TRUE
#define DEFAULT_PAR_N               0
#define DEFAULT_PAR_D               1
#define DEFAULT_IGNORE_ALPHA        TRUE

enum
{
  PROP_0,
  PROP_FORCE_ASPECT_RATIO,
  PROP_PIXEL_ASPECT_RATIO,
  PROP_IGNORE_ALPHA,
};

struct _GtkGstWidgetPrivate
{
  GMutex lock;

  /* properties */
  gboolean force_aspect_ratio;
  gint par_n, par_d;
  gboolean ignore_alpha;

  gint display_width;
  gint display_height;

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
  gint video_width = gst_widget->priv->display_width;

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
  gint video_height = gst_widget->priv->display_height;

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
    gdouble scale_x = (gdouble) widget_width / gst_widget->priv->display_width;
    gdouble scale_y =
        (gdouble) widget_height / gst_widget->priv->display_height;
    GstVideoRectangle result;
    cairo_format_t format;

    gst_widget->priv->v_info = frame.info;
    if (frame.info.finfo->format == GST_VIDEO_FORMAT_ARGB ||
        frame.info.finfo->format == GST_VIDEO_FORMAT_BGRA) {
      format = CAIRO_FORMAT_ARGB32;
    } else {
      format = CAIRO_FORMAT_RGB24;
    }

    surface = cairo_image_surface_create_for_data (frame.data[0],
        format, frame.info.width, frame.info.height, frame.info.stride[0]);

    if (gst_widget->priv->force_aspect_ratio) {
      GstVideoRectangle src, dst;

      src.x = 0;
      src.y = 0;
      src.w = gst_widget->priv->display_width;
      src.h = gst_widget->priv->display_height;

      dst.x = 0;
      dst.y = 0;
      dst.w = widget_width;
      dst.h = widget_height;

      gst_video_sink_center_rect (src, dst, &result, TRUE);

      scale_x = scale_y = MIN (scale_x, scale_y);
    } else {
      result.x = 0;
      result.y = 0;
      result.w = widget_width;
      result.h = widget_height;
    }

    if (gst_widget->priv->ignore_alpha) {
      GdkRGBA color = { 0.0, 0.0, 0.0, 1.0 };

      gdk_cairo_set_source_rgba (cr, &color);
      if (result.x > 0) {
        cairo_rectangle (cr, 0, 0, result.x, widget_height);
        cairo_fill (cr);
      }
      if (result.y > 0) {
        cairo_rectangle (cr, 0, 0, widget_width, result.y);
        cairo_fill (cr);
      }
      if (result.w < widget_width) {
        cairo_rectangle (cr, result.x + result.w, 0, widget_width - result.w,
            widget_height);
        cairo_fill (cr);
      }
      if (result.h < widget_height) {
        cairo_rectangle (cr, 0, result.y + result.h, widget_width,
            widget_height - result.h);
        cairo_fill (cr);
      }
    }

    scale_x *=
        (gdouble) gst_widget->priv->display_width / (gdouble) frame.info.width;
    scale_y *=
        (gdouble) gst_widget->priv->display_height /
        (gdouble) frame.info.height;

    cairo_translate (cr, result.x, result.y);
    cairo_scale (cr, scale_x, scale_y);
    cairo_rectangle (cr, 0, 0, result.w, result.h);
    cairo_set_source_surface (cr, surface, 0, 0);
    cairo_paint (cr);

    cairo_surface_destroy (surface);

    gst_video_frame_unmap (&frame);
  } else {
    GdkRGBA color;

    if (gst_widget->priv->ignore_alpha) {
      color.red = color.blue = color.green = 0.0;
      color.alpha = 1.0;
    } else {
      gtk_style_context_get_color (gtk_widget_get_style_context (widget),
          GTK_STATE_FLAG_NORMAL, &color);
    }
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
gtk_gst_widget_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GtkGstWidget *gtk_widget = GTK_GST_WIDGET (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
      gtk_widget->priv->force_aspect_ratio = g_value_get_boolean (value);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      gtk_widget->priv->par_n = gst_value_get_fraction_numerator (value);
      gtk_widget->priv->par_d = gst_value_get_fraction_denominator (value);
      break;
    case PROP_IGNORE_ALPHA:
      gtk_widget->priv->ignore_alpha = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gtk_gst_widget_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GtkGstWidget *gtk_widget = GTK_GST_WIDGET (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, gtk_widget->priv->force_aspect_ratio);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      gst_value_set_fraction (value, gtk_widget->priv->par_n,
          gtk_widget->priv->par_d);
    case PROP_IGNORE_ALPHA:
      g_value_set_boolean (value, gtk_widget->priv->ignore_alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gtk_gst_widget_class_init (GtkGstWidgetClass * klass)
{
  GObjectClass *gobject_klass = (GObjectClass *) klass;
  GtkWidgetClass *widget_klass = (GtkWidgetClass *) klass;

  g_type_class_add_private (klass, sizeof (GtkGstWidgetPrivate));

  gobject_klass->set_property = gtk_gst_widget_set_property;
  gobject_klass->get_property = gtk_gst_widget_get_property;
  gobject_klass->finalize = gtk_gst_widget_finalize;

  g_object_class_install_property (gobject_klass, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_PIXEL_ASPECT_RATIO,
      gst_param_spec_fraction ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", DEFAULT_PAR_N, DEFAULT_PAR_D,
          G_MAXINT, 1, 1, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_IGNORE_ALPHA,
      g_param_spec_boolean ("ignore-alpha", "Ignore Alpha",
          "When enabled, alpha will be ignored and converted to black",
          DEFAULT_IGNORE_ALPHA, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  widget_klass->draw = gtk_gst_widget_draw;
  widget_klass->get_preferred_width = gtk_gst_widget_get_preferred_width;
  widget_klass->get_preferred_height = gtk_gst_widget_get_preferred_height;
}

static void
gtk_gst_widget_init (GtkGstWidget * widget)
{
  widget->priv = GTK_GST_WIDGET_GET_PRIVATE (widget);

  widget->priv->force_aspect_ratio = DEFAULT_FORCE_ASPECT_RATIO;
  widget->priv->par_n = DEFAULT_PAR_N;
  widget->priv->par_d = DEFAULT_PAR_D;
  widget->priv->ignore_alpha = DEFAULT_IGNORE_ALPHA;

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

static gboolean
_calculate_par (GtkGstWidget * widget, GstVideoInfo * info)
{
  gboolean ok;
  gint width, height;
  gint par_n, par_d;
  gint display_par_n, display_par_d;
  guint display_ratio_num, display_ratio_den;

  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);

  par_n = GST_VIDEO_INFO_PAR_N (info);
  par_d = GST_VIDEO_INFO_PAR_D (info);

  if (!par_n)
    par_n = 1;

  /* get display's PAR */
  if (widget->priv->par_n != 0 && widget->priv->par_d != 0) {
    display_par_n = widget->priv->par_n;
    display_par_d = widget->priv->par_d;
  } else {
    display_par_n = 1;
    display_par_d = 1;
  }

  ok = gst_video_calculate_display_ratio (&display_ratio_num,
      &display_ratio_den, width, height, par_n, par_d, display_par_n,
      display_par_d);

  if (!ok)
    return FALSE;

  GST_LOG ("PAR: %u/%u DAR:%u/%u", par_n, par_d, display_par_n, display_par_d);

  if (height % display_ratio_den == 0) {
    GST_DEBUG ("keeping video height");
    widget->priv->display_width = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    widget->priv->display_height = height;
  } else if (width % display_ratio_num == 0) {
    GST_DEBUG ("keeping video width");
    widget->priv->display_width = width;
    widget->priv->display_height = (guint)
        gst_util_uint64_scale_int (width, display_ratio_den, display_ratio_num);
  } else {
    GST_DEBUG ("approximating while keeping video height");
    widget->priv->display_width = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    widget->priv->display_height = height;
  }
  GST_DEBUG ("scaling to %dx%d", widget->priv->display_width,
      widget->priv->display_height);

  return TRUE;
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
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&v_info) ==
      GST_VIDEO_FORMAT_BGRA || GST_VIDEO_INFO_FORMAT (&v_info) ==
      GST_VIDEO_FORMAT_BGRx, FALSE);
#else
  g_return_val_if_fail (GST_VIDEO_INFO_FORMAT (&v_info) ==
      GST_VIDEO_FORMAT_ARGB || GST_VIDEO_INFO_FORMAT (&v_info) ==
      GST_VIDEO_FORMAT_xRGB, FALSE);
#endif

  g_mutex_lock (&widget->priv->lock);

  if (!_calculate_par (widget, &v_info)) {
    g_mutex_unlock (&widget->priv->lock);
    return FALSE;
  }

  gst_caps_replace (&widget->priv->caps, caps);
  widget->priv->v_info = v_info;
  widget->priv->negotiated = TRUE;

  g_mutex_unlock (&widget->priv->lock);

  gtk_widget_queue_resize (GTK_WIDGET (widget));

  g_main_context_invoke (main_context, (GSourceFunc) _queue_resize, widget);

  return TRUE;
}
