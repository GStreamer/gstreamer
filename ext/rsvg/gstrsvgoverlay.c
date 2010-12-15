/* GStreamer
 * Copyright (C) 2010 Olivier Aubert <olivier.aubert@liris.cnrs.fr>
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
 * SECTION:element-rsvgoverlay
 *
 * This elements overlays SVG graphics over the video. SVG data can
 * either be specified through properties, or fed through the
 * data-sink pad.
 *
 * Note: setting the x or y parameter to a non-zero value will implicitly disable the fit-to-frame behaviour.
 *
 * <refsect2>
 * 
 * <title>Example launch lines</title>
 * |[
 * gst-launch -v videotestsrc ! ffmpegcolorspace ! rsvgoverlay location=foo.svg ! ffmpegcolorspace ! autovideosink
 * ]| specifies the SVG location through the filename property. 
 * |[
 * gst-launch -v videotestsrc ! ffmpegcolorspace ! rsvgoverlay name=overlay ! ffmpegcolorspace ! autovideosink filesrc location=foo.svg ! image/svg ! overlay.data_sink
 * ]| does the same by feeding data through the data_sink pad. You can also specify the SVG data itself as parameter:
 * |[
 * gst-launch -v videotestsrc ! ffmpegcolorspace ! rsvgoverlay data='<svg viewBox="0 0 800 600"><image x="80%" y="80%" width="10%" height="10%" xlink:href="foo.jpg" /></svg>' ! ffmpegcolorspace ! autovideosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstrsvgoverlay.h"
#include <string.h>

#include <gst/video/video.h>

#include <librsvg/rsvg.h>
#include <librsvg/rsvg-cairo.h>

enum
{
  PROP_0,
  PROP_DATA,
  PROP_FILENAME,
  PROP_FIT_TO_FRAME,
  PROP_X,
  PROP_Y,
};

#define GST_RSVG_LOCK(overlay) G_STMT_START { \
  GST_LOG_OBJECT (overlay, "Locking rsvgoverlay from thread %p", g_thread_self ()); \
  g_static_mutex_lock (&overlay->rsvg_lock); \
  GST_LOG_OBJECT (overlay, "Locked rsvgoverlay from thread %p", g_thread_self ()); \
} G_STMT_END

#define GST_RSVG_UNLOCK(overlay) G_STMT_START { \
  GST_LOG_OBJECT (overlay, "Unlocking rsvgoverlay from thread %p", g_thread_self ()); \
  g_static_mutex_unlock (&overlay->rsvg_lock); \
} G_STMT_END

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define GST_RSVG_VIDEO_CAPS GST_VIDEO_CAPS_BGRA
#else
#define GST_RSVG_VIDEO_CAPS GST_VIDEO_CAPS_ARGB
#endif

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_RSVG_VIDEO_CAPS)
    );

static GstStaticPadTemplate video_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_RSVG_VIDEO_CAPS)
    );

static GstStaticPadTemplate data_sink_template =
    GST_STATIC_PAD_TEMPLATE ("data_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/svg+xml; image/svg; text/plain"));

GST_BOILERPLATE (GstRsvgOverlay, gst_rsvg_overlay, GstVideoFilter,
    GST_TYPE_VIDEO_FILTER);

static void
gst_rsvg_overlay_set_svg_data (GstRsvgOverlay * overlay, const gchar * data,
    gboolean consider_as_filename)
{
  GstBaseTransform *btrans = GST_BASE_TRANSFORM (overlay);
  gsize size;
  GError *error = NULL;

  if (overlay->handle) {
    g_object_unref (overlay->handle);
    overlay->handle = NULL;
    gst_base_transform_set_passthrough (btrans, TRUE);
  }

  /* data may be NULL */
  if (data) {
    size = strlen (data);
    if (size) {
      /* Read data either from string or from file */
      if (consider_as_filename)
        overlay->handle = rsvg_handle_new_from_file (data, &error);
      else
        overlay->handle =
            rsvg_handle_new_from_data ((guint8 *) data, size, &error);
      if (error) {
        GST_ERROR_OBJECT (overlay, "Cannot read SVG data: %s\n%s",
            error->message, data);
        g_error_free (error);
      } else {
        /* Get SVG dimension. */
        RsvgDimensionData svg_dimension;
        rsvg_handle_get_dimensions (overlay->handle, &svg_dimension);
        overlay->width = svg_dimension.width;
        overlay->height = svg_dimension.height;
        gst_base_transform_set_passthrough (btrans, FALSE);
      }
    }
  }
}

static void
gst_rsvg_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRsvgOverlay *overlay = GST_RSVG_OVERLAY (object);

  GST_RSVG_LOCK (overlay);

  switch (prop_id) {
    case PROP_DATA:
    {
      gst_rsvg_overlay_set_svg_data (overlay, g_value_get_string (value),
          FALSE);
      break;
    }
    case PROP_FILENAME:
    {
      gst_rsvg_overlay_set_svg_data (overlay, g_value_get_string (value), TRUE);
      break;
    }
    case PROP_FIT_TO_FRAME:
    {
      overlay->fit_to_frame = g_value_get_boolean (value);
      break;
    }
    case PROP_X:
    {
      overlay->x_offset = g_value_get_int (value);
      break;
    }
    case PROP_Y:
    {
      overlay->y_offset = g_value_get_int (value);
      break;
    }

    default:
    {
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
  }

  GST_RSVG_UNLOCK (overlay);
}

static void
gst_rsvg_overlay_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRsvgOverlay *overlay = GST_RSVG_OVERLAY (object);

  switch (prop_id) {
    case PROP_X:
      g_value_set_int (value, overlay->x_offset);
      break;
    case PROP_Y:
      g_value_set_int (value, overlay->y_offset);
      break;
    case PROP_FIT_TO_FRAME:
      g_value_set_boolean (value, overlay->fit_to_frame);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_rsvg_overlay_data_sink_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRsvgOverlay *overlay = GST_RSVG_OVERLAY (GST_PAD_PARENT (pad));

  gst_adapter_push (overlay->adapter, buffer);
  return GST_FLOW_OK;
}

static gboolean
gst_rsvg_overlay_data_sink_event (GstPad * pad, GstEvent * event)
{
  GstRsvgOverlay *overlay = GST_RSVG_OVERLAY (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (pad, "Got %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {

    case GST_EVENT_EOS:{
      guint data_size;

      GST_RSVG_LOCK (overlay);
      /* FIXME: rsvgdec looks for </svg> in data to determine the end
         of SVG code. Should we really do the same here? IOW, could
         data be sent and _pushed() before the EOS gets processed? */
      data_size = gst_adapter_available (overlay->adapter);
      if (data_size) {
        gst_rsvg_overlay_set_svg_data (overlay,
            (const gchar *) gst_adapter_take (overlay->adapter, data_size),
            FALSE);
        gst_adapter_clear (overlay->adapter);
      }
      GST_RSVG_UNLOCK (overlay);
    }

    case GST_EVENT_FLUSH_START:
      gst_adapter_clear (overlay->adapter);
      break;

    default:
      break;
  }

  /* Dropping all events here */
  gst_event_unref (event);
  return TRUE;
}

static gboolean
gst_rsvg_overlay_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstRsvgOverlay *overlay = GST_RSVG_OVERLAY (btrans);

  return G_LIKELY (gst_video_format_parse_caps (incaps,
          &overlay->caps_format, &overlay->caps_width, &overlay->caps_height));
}

static GstFlowReturn
gst_rsvg_overlay_transform_ip (GstBaseTransform * btrans, GstBuffer * buf)
{
  GstRsvgOverlay *overlay = GST_RSVG_OVERLAY (btrans);
  cairo_surface_t *surface;
  cairo_t *cr;

  GST_RSVG_LOCK (overlay);
  if (!overlay->handle) {
    GST_RSVG_UNLOCK (overlay);
    return GST_FLOW_OK;
  }

  surface =
      cairo_image_surface_create_for_data (GST_BUFFER_DATA (buf),
      CAIRO_FORMAT_ARGB32, overlay->caps_width, overlay->caps_height,
      overlay->caps_width * 4);
  if (G_UNLIKELY (!surface))
    return GST_FLOW_ERROR;

  cr = cairo_create (surface);
  if (G_UNLIKELY (!cr)) {
    cairo_surface_destroy (surface);
    return GST_FLOW_ERROR;
  }

  /* If x or y offset is specified, do not fit-to-frame. */
  if (overlay->x_offset || overlay->y_offset)
    cairo_translate (cr, (double) overlay->x_offset,
        (double) overlay->y_offset);
  else if (overlay->fit_to_frame && overlay->width && overlay->height)
    cairo_scale (cr, (float) overlay->caps_width / overlay->width,
        (float) overlay->caps_height / overlay->height);

  rsvg_handle_render_cairo (overlay->handle, cr);
  GST_RSVG_UNLOCK (overlay);

  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  return GST_FLOW_OK;
}

static gboolean
gst_rsvg_overlay_stop (GstBaseTransform * btrans)
{
  GstRsvgOverlay *overlay = GST_RSVG_OVERLAY (btrans);

  if (overlay->handle) {
    g_object_unref (overlay->handle);
    g_object_unref (overlay->adapter);
    overlay->handle = NULL;
  }

  return TRUE;
}

static void
gst_rsvg_overlay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&data_sink_template));

  gst_element_class_set_details_simple (element_class, "RSVG overlay",
      "Filter/Editor/Video",
      "Overlays SVG graphics over a video stream",
      "Olivier Aubert <olivier.aubert@liris.cnrs.fr>");
}

static void
gst_rsvg_overlay_class_init (GstRsvgOverlayClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *basetransform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_rsvg_overlay_set_property;
  gobject_class->get_property = gst_rsvg_overlay_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DATA,
      g_param_spec_string ("data", "data", "SVG data.", "",
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FILENAME,
      g_param_spec_string ("location", "location", "SVG file location.", "",
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FIT_TO_FRAME,
      g_param_spec_boolean ("fit-to-frame", "fit to frame",
          "Fit the SVG to fill the whole frame.", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_X,
      g_param_spec_int ("x", "x offset",
          "Specify an x offset.", 0, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_Y,
      g_param_spec_int ("y", "y offset",
          "Specify a y offset.", 0, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  basetransform_class->set_caps = gst_rsvg_overlay_set_caps;
  basetransform_class->transform_ip = gst_rsvg_overlay_transform_ip;
  basetransform_class->stop = gst_rsvg_overlay_stop;
  basetransform_class->passthrough_on_same_caps = FALSE;
}

static void
gst_rsvg_overlay_init (GstRsvgOverlay * overlay, GstRsvgOverlayClass * klass)
{
  overlay->fit_to_frame = 1;
  overlay->adapter = gst_adapter_new ();

  /* data sink */
  overlay->data_sinkpad =
      gst_pad_new_from_static_template (&data_sink_template, "data_sink");
  gst_pad_set_chain_function (overlay->data_sinkpad,
      GST_DEBUG_FUNCPTR (gst_rsvg_overlay_data_sink_chain));
  gst_pad_set_event_function (overlay->data_sinkpad,
      GST_DEBUG_FUNCPTR (gst_rsvg_overlay_data_sink_event));
  gst_element_add_pad (GST_ELEMENT (overlay), overlay->data_sinkpad);
}
