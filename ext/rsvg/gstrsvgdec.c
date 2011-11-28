/* GStreamer
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-rsvgdec
 *
 * This elements renders SVG graphics.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch filesrc location=image.svg ! rsvgdec ! imagefreeze ! ffmpegcolorspace ! autovideosink
 * ]| render and show a svg image.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrsvgdec.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (rsvgdec_debug);
#define GST_CAT_DEFAULT rsvgdec_debug

static GstStaticPadTemplate sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/svg+xml; image/svg"));

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define GST_RSVG_VIDEO_CAPS GST_VIDEO_CAPS_BGRA
#else
#define GST_RSVG_VIDEO_CAPS GST_VIDEO_CAPS_ARGB
#endif

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_RSVG_VIDEO_CAPS));

GST_BOILERPLATE (GstRsvgDec, gst_rsvg_dec, GstElement, GST_TYPE_ELEMENT);

static void gst_rsvg_dec_reset (GstRsvgDec * rsvg);

static GstFlowReturn gst_rsvg_dec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_rsvg_dec_sink_set_caps (GstPad * pad, GstCaps * caps);
static gboolean gst_rsvg_dec_sink_event (GstPad * pad, GstEvent * event);

static gboolean gst_rsvg_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_rsvg_dec_src_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_rsvg_dec_src_query_type (GstPad * pad);
static gboolean gst_rsvg_dec_src_set_caps (GstPad * pad, GstCaps * caps);

static GstStateChangeReturn gst_rsvg_dec_change_state (GstElement * element,
    GstStateChange transition);

static void gst_rsvg_dec_finalize (GObject * object);

static void
gst_rsvg_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "SVG image decoder", "Codec/Decoder/Image",
      "Uses librsvg to decode SVG images",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_rsvg_dec_class_init (GstRsvgDecClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;

  GST_DEBUG_CATEGORY_INIT (rsvgdec_debug, "rsvgdec", 0, "RSVG decoder");

  gobject_class->finalize = gst_rsvg_dec_finalize;
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_rsvg_dec_change_state);
}

static void
gst_rsvg_dec_init (GstRsvgDec * rsvg, GstRsvgDecClass * klass)
{
  rsvg->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (rsvg->sinkpad, gst_rsvg_dec_sink_set_caps);
  gst_pad_set_event_function (rsvg->sinkpad, gst_rsvg_dec_sink_event);
  gst_pad_set_chain_function (rsvg->sinkpad, gst_rsvg_dec_chain);
  gst_element_add_pad (GST_ELEMENT (rsvg), rsvg->sinkpad);

  rsvg->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_event_function (rsvg->srcpad, gst_rsvg_dec_src_event);
  gst_pad_set_query_function (rsvg->srcpad, gst_rsvg_dec_src_query);
  gst_pad_set_query_type_function (rsvg->srcpad, gst_rsvg_dec_src_query_type);
  gst_pad_set_setcaps_function (rsvg->srcpad, gst_rsvg_dec_src_set_caps);
  gst_element_add_pad (GST_ELEMENT (rsvg), rsvg->srcpad);

  rsvg->adapter = gst_adapter_new ();

  gst_rsvg_dec_reset (rsvg);
}

static void
gst_rsvg_dec_finalize (GObject * object)
{
  GstRsvgDec *rsvg = GST_RSVG_DEC (object);

  if (rsvg->adapter) {
    g_object_unref (rsvg->adapter);
    rsvg->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rsvg_dec_reset (GstRsvgDec * dec)
{
  gst_adapter_clear (dec->adapter);
  dec->width = dec->height = 0;
  dec->fps_n = 0;
  dec->fps_d = 1;
  dec->first_timestamp = GST_CLOCK_TIME_NONE;
  dec->frame_count = 0;

  gst_segment_init (&dec->segment, GST_FORMAT_UNDEFINED);
  dec->need_newsegment = TRUE;

  g_list_foreach (dec->pending_events, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (dec->pending_events);
  dec->pending_events = NULL;

  if (dec->pending_tags) {
    gst_tag_list_free (dec->pending_tags);
    dec->pending_tags = NULL;
  }
}

#define CAIRO_UNPREMULTIPLY(a,r,g,b) G_STMT_START { \
  b = (a > 0) ? MIN ((b * 255 + a / 2) / a, 255) : 0; \
  g = (a > 0) ? MIN ((g * 255 + a / 2) / a, 255) : 0; \
  r = (a > 0) ? MIN ((r * 255 + a / 2) / a, 255) : 0; \
} G_STMT_END

static void
gst_rsvg_decode_unpremultiply (guint8 * data, gint width, gint height)
{
  gint i, j;
  guint a;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      a = data[3];
      data[0] = (a > 0) ? MIN ((data[0] * 255 + a / 2) / a, 255) : 0;
      data[1] = (a > 0) ? MIN ((data[1] * 255 + a / 2) / a, 255) : 0;
      data[2] = (a > 0) ? MIN ((data[2] * 255 + a / 2) / a, 255) : 0;
#else
      a = data[0];
      data[1] = (a > 0) ? MIN ((data[1] * 255 + a / 2) / a, 255) : 0;
      data[2] = (a > 0) ? MIN ((data[2] * 255 + a / 2) / a, 255) : 0;
      data[3] = (a > 0) ? MIN ((data[3] * 255 + a / 2) / a, 255) : 0;
#endif
      data += 4;
    }
  }
}

static GstFlowReturn
gst_rsvg_decode_image (GstRsvgDec * rsvg, const guint8 * data, guint size,
    GstBuffer ** buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  cairo_t *cr;
  cairo_surface_t *surface;
  RsvgHandle *handle;
  GError *error = NULL;
  RsvgDimensionData dimension;
  gdouble scalex, scaley;
  const gchar *title = NULL, *comment = NULL;

  GST_LOG_OBJECT (rsvg, "parsing svg");

  handle = rsvg_handle_new_from_data (data, size, &error);
  if (!handle) {
    GST_ERROR_OBJECT (rsvg, "Failed to parse SVG image: %s", error->message);
    g_error_free (error);
    return GST_FLOW_ERROR;
  }

  title = rsvg_handle_get_title (handle);
  comment = rsvg_handle_get_desc (handle);

  if (title || comment) {
    GST_LOG_OBJECT (rsvg, "adding tags");

    if (!rsvg->pending_tags)
      rsvg->pending_tags = gst_tag_list_new ();

    if (title && *title)
      gst_tag_list_add (rsvg->pending_tags, GST_TAG_MERGE_REPLACE_ALL,
          GST_TAG_TITLE, title, NULL);
    if (comment && *comment)
      gst_tag_list_add (rsvg->pending_tags, GST_TAG_MERGE_REPLACE_ALL,
          GST_TAG_COMMENT, comment, NULL);
  }

  rsvg_handle_get_dimensions (handle, &dimension);
  if (rsvg->width != dimension.width || rsvg->height != dimension.height) {
    GstCaps *caps1, *caps2, *caps3;
    GstStructure *s;

    GST_LOG_OBJECT (rsvg, "resolution changed, updating caps");

    caps1 = gst_caps_copy (gst_pad_get_pad_template_caps (rsvg->srcpad));
    caps2 = gst_pad_peer_get_caps (rsvg->srcpad);
    if (caps2) {
      caps3 = gst_caps_intersect (caps1, caps2);
      gst_caps_unref (caps1);
      gst_caps_unref (caps2);
      caps1 = caps3;
      caps3 = NULL;
    }

    if (gst_caps_is_empty (caps1)) {
      GST_ERROR_OBJECT (rsvg, "Unable to negotiate a format");
      gst_caps_unref (caps1);
      g_object_unref (handle);
      return GST_FLOW_NOT_NEGOTIATED;
    }

    caps2 = gst_caps_copy (gst_pad_get_pad_template_caps (rsvg->srcpad));
    s = gst_caps_get_structure (caps2, 0);
    gst_structure_set (s, "width", G_TYPE_INT, dimension.width, "height",
        G_TYPE_INT, dimension.height, "framerate", GST_TYPE_FRACTION, 0, 1,
        NULL);
    caps3 = gst_caps_intersect (caps1, caps2);
    if (!gst_caps_is_empty (caps3)) {
      gst_caps_truncate (caps3);
      gst_pad_set_caps (rsvg->srcpad, caps3);
      gst_caps_unref (caps1);
      gst_caps_unref (caps2);
      gst_caps_unref (caps3);
      rsvg->width = dimension.width;
      rsvg->height = dimension.height;
    } else {
      gst_caps_unref (caps2);
      gst_caps_unref (caps3);
      gst_caps_truncate (caps1);

      s = gst_caps_get_structure (caps1, 0);
      gst_structure_set (s, "framerate", GST_TYPE_FRACTION, 0, 1, NULL);

      if (!gst_caps_is_fixed (caps1)
          && (!gst_structure_fixate_field_nearest_int (s, "width",
                  dimension.width)
              || !gst_structure_fixate_field_nearest_int (s, "height",
                  dimension.height))) {
        g_object_unref (handle);
        GST_ERROR_OBJECT (rsvg, "Failed to fixate caps");
        return GST_FLOW_NOT_NEGOTIATED;
      }
      gst_pad_set_caps (rsvg->srcpad, caps1);
      gst_structure_get_int (s, "width", &rsvg->width);
      gst_structure_get_int (s, "height", &rsvg->height);
      gst_caps_unref (caps1);
    }
  }

  if ((ret = gst_pad_alloc_buffer_and_set_caps (rsvg->srcpad,
              GST_BUFFER_OFFSET_NONE,
              rsvg->width * rsvg->height * 4,
              GST_PAD_CAPS (rsvg->srcpad), buffer)) != GST_FLOW_OK) {
    g_object_unref (handle);
    GST_ERROR_OBJECT (rsvg, "Buffer allocation failed %s",
        gst_flow_get_name (ret));
    return ret;
  }

  GST_LOG_OBJECT (rsvg, "render image at %d x %d", rsvg->height, rsvg->width);

  surface =
      cairo_image_surface_create_for_data (GST_BUFFER_DATA (*buffer),
      CAIRO_FORMAT_ARGB32, rsvg->width, rsvg->height, rsvg->width * 4);

  cr = cairo_create (surface);
  cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
  cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.0);
  cairo_paint (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
  cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);

  scalex = scaley = 1.0;
  if (rsvg->width != dimension.width) {
    scalex = ((gdouble) rsvg->width) / ((gdouble) dimension.width);
  }
  if (rsvg->height != dimension.height) {
    scaley = ((gdouble) rsvg->height) / ((gdouble) dimension.height);
  }
  cairo_scale (cr, scalex, scaley);
  rsvg_handle_render_cairo (handle, cr);

  g_object_unref (handle);
  cairo_destroy (cr);
  cairo_surface_destroy (surface);

  /* Now unpremultiply Cairo's ARGB to match GStreamer's */
  gst_rsvg_decode_unpremultiply (GST_BUFFER_DATA (*buffer), rsvg->width,
      rsvg->height);

  return ret;
}

static GstFlowReturn
gst_rsvg_dec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRsvgDec *rsvg = GST_RSVG_DEC (GST_PAD_PARENT (pad));
  gboolean completed = FALSE;
  const guint8 *data;
  guint size;
  gboolean ret = GST_FLOW_OK;

  /* first_timestamp is used slightly differently where a framerate
     is given or not.
     If there is a frame rate, it will be used as a base.
     If there is not, it will be used to keep track of the timestamp
     of the first buffer, to be used as the timestamp of the output
     buffer. When a buffer is output, first timestamp will resync to
     the next buffer's timestamp. */
  if (rsvg->first_timestamp == GST_CLOCK_TIME_NONE) {
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
      rsvg->first_timestamp = GST_BUFFER_TIMESTAMP (buffer);
    else if (rsvg->fps_n != 0)
      rsvg->first_timestamp = 0;
  }

  gst_adapter_push (rsvg->adapter, buffer);

  size = gst_adapter_available (rsvg->adapter);

  /* "<svg></svg>" */
  while (size >= 5 + 6 && ret == GST_FLOW_OK) {
    guint i;

    data = gst_adapter_peek (rsvg->adapter, size);
    for (i = size - 6; i >= 5; i--) {
      if (memcmp (data + i, "</svg>", 6) == 0) {
        completed = TRUE;
        size = i + 6;
        break;
      }
    }

    if (completed) {
      GstBuffer *outbuf = NULL;

      GST_LOG_OBJECT (rsvg, "have complete svg of %u bytes", size);

      data = gst_adapter_peek (rsvg->adapter, size);

      ret = gst_rsvg_decode_image (rsvg, data, size, &outbuf);
      if (ret != GST_FLOW_OK)
        break;


      if (rsvg->first_timestamp != GST_CLOCK_TIME_NONE) {
        GST_BUFFER_TIMESTAMP (outbuf) = rsvg->first_timestamp;
        GST_BUFFER_DURATION (outbuf) = GST_CLOCK_TIME_NONE;
        if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
          GstClockTime end =
              GST_BUFFER_TIMESTAMP_IS_VALID (buffer) ?
              GST_BUFFER_TIMESTAMP (buffer) : rsvg->first_timestamp;
          end += GST_BUFFER_DURATION (buffer);
          GST_BUFFER_DURATION (outbuf) = end - GST_BUFFER_TIMESTAMP (outbuf);
        }
        if (rsvg->fps_n == 0) {
          rsvg->first_timestamp = GST_CLOCK_TIME_NONE;
        } else {
          GST_BUFFER_DURATION (outbuf) =
              gst_util_uint64_scale (rsvg->frame_count, rsvg->fps_d,
              rsvg->fps_n * GST_SECOND);
        }
      } else if (rsvg->fps_n != 0) {
        GST_BUFFER_TIMESTAMP (outbuf) =
            rsvg->first_timestamp + gst_util_uint64_scale (rsvg->frame_count,
            rsvg->fps_d, rsvg->fps_n * GST_SECOND);
        GST_BUFFER_DURATION (outbuf) =
            gst_util_uint64_scale (rsvg->frame_count, rsvg->fps_d,
            rsvg->fps_n * GST_SECOND);
      } else {
        GST_BUFFER_TIMESTAMP (outbuf) = rsvg->first_timestamp;
        GST_BUFFER_DURATION (outbuf) = GST_CLOCK_TIME_NONE;
      }
      rsvg->frame_count++;

      if (rsvg->need_newsegment) {
        gst_pad_push_event (rsvg->srcpad,
            gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0));
        rsvg->need_newsegment = FALSE;
      }

      if (rsvg->pending_events) {
        GList *l;

        for (l = rsvg->pending_events; l; l = l->next)
          gst_pad_push_event (rsvg->srcpad, l->data);
        g_list_free (rsvg->pending_events);
        rsvg->pending_events = NULL;
      }

      if (rsvg->pending_tags) {
        gst_element_found_tags (GST_ELEMENT_CAST (rsvg), rsvg->pending_tags);
        rsvg->pending_tags = NULL;
      }

      GST_LOG_OBJECT (rsvg, "image rendered okay");

      ret = gst_pad_push (rsvg->srcpad, outbuf);
      if (ret != GST_FLOW_OK)
        break;

      gst_adapter_flush (rsvg->adapter, size);
      size = gst_adapter_available (rsvg->adapter);
      continue;
    } else {
      break;
    }
  }

  return GST_FLOW_OK;
}

static gboolean
gst_rsvg_dec_sink_set_caps (GstPad * pad, GstCaps * caps)
{
  GstRsvgDec *rsvg = GST_RSVG_DEC (gst_pad_get_parent (pad));
  gboolean ret = TRUE;
  GstStructure *s = gst_caps_get_structure (caps, 0);

  gst_structure_get_fraction (s, "framerate", &rsvg->fps_n, &rsvg->fps_d);

  gst_object_unref (rsvg);

  return ret;
}

static gboolean
gst_rsvg_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstRsvgDec *rsvg = GST_RSVG_DEC (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      gdouble rate, arate;
      gboolean update;
      gint64 start, stop, position;
      GstFormat fmt;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &fmt,
          &start, &stop, &position);

      gst_segment_set_newsegment_full (&rsvg->segment, update, rate, arate,
          fmt, start, stop, position);

      if (fmt == GST_FORMAT_TIME) {
        rsvg->need_newsegment = FALSE;
        res = gst_pad_push_event (rsvg->srcpad, event);
      } else {
        gst_event_unref (event);
        res = TRUE;
      }
      break;
    }
    case GST_EVENT_EOS:
    case GST_EVENT_FLUSH_STOP:
      gst_adapter_clear (rsvg->adapter);
      /* fall through */
    case GST_EVENT_FLUSH_START:
      res = gst_pad_push_event (rsvg->srcpad, event);
      break;
    default:
      if (GST_PAD_CAPS (rsvg->srcpad)) {
        res = gst_pad_push_event (rsvg->srcpad, event);
      } else {
        res = TRUE;
        rsvg->pending_events = g_list_append (rsvg->pending_events, event);
      }
      break;
  }

  gst_object_unref (rsvg);

  return res;
}

static gboolean
gst_rsvg_dec_src_event (GstPad * pad, GstEvent * event)
{
  GstRsvgDec *rsvg = GST_RSVG_DEC (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    default:
      res = gst_pad_push_event (rsvg->sinkpad, event);
      break;
  }

  gst_object_unref (rsvg);

  return res;
}

static const GstQueryType *
gst_rsvg_dec_src_query_type (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    (GstQueryType) 0
  };

  return query_types;
}

static gboolean
gst_rsvg_dec_src_query (GstPad * pad, GstQuery * query)
{
  GstRsvgDec *rsvg = GST_RSVG_DEC (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (rsvg);

  return res;
}

static gboolean
gst_rsvg_dec_src_set_caps (GstPad * pad, GstCaps * caps)
{
  GstRsvgDec *rsvg = GST_RSVG_DEC (gst_pad_get_parent (pad));
  gboolean ret = TRUE;
  GstStructure *s = gst_caps_get_structure (caps, 0);

  ret &= gst_structure_get_int (s, "width", &rsvg->width);
  ret &= gst_structure_get_int (s, "height", &rsvg->height);

  gst_object_unref (rsvg);

  return ret;
}

static GstStateChangeReturn
gst_rsvg_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn res;
  GstRsvgDec *dec = GST_RSVG_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (res == GST_STATE_CHANGE_FAILURE)
    return res;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rsvg_dec_reset (dec);
      break;
    default:
      break;
  }

  return res;
}
