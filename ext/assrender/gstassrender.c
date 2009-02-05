/*
 * Copyright (c) 2008 Benjamin Schmitz <vortex@wolpzone.de>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstassrender.h"

#include <gst/video/video.h>

GST_DEBUG_CATEGORY_STATIC (gst_assrender_debug);
#define GST_CAT_DEFAULT gst_assrender_debug

/* Filter signals and props */
enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_ENABLE,
  ARG_EMBEDDEDFONTS
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB)
    );

static GstStaticPadTemplate video_sink_factory =
GST_STATIC_PAD_TEMPLATE ("video_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB)
    );

static GstStaticPadTemplate text_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("text_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-ass; application/x-ssa")
    );

static void gst_assrender_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_assrender_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_assrender_finalize (GObject * object);

GST_BOILERPLATE (Gstassrender, gst_assrender, GstElement, GST_TYPE_ELEMENT);

static GstCaps *gst_assrender_getcaps (GstPad * pad);

static gboolean gst_assrender_setcaps_video (GstPad * pad, GstCaps * caps);
static gboolean gst_assrender_setcaps_text (GstPad * pad, GstCaps * caps);

static GstFlowReturn gst_assrender_chain_video (GstPad * pad, GstBuffer * buf);
static GstFlowReturn gst_assrender_chain_text (GstPad * pad, GstBuffer * buf);

static gboolean gst_assrender_event_video (GstPad * pad, GstEvent * event);
static gboolean gst_assrender_event_text (GstPad * pad, GstEvent * event);

static void
gst_assrender_base_init (gpointer gclass)
{
  static GstElementDetails element_details = {
    "ASS Render",
    "Filter/Effect/Video",
    "Renders ASS/SSA subtitles with libass",
    "Benjamin Schmitz <vortex@wolpzone.de>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&text_sink_factory));

  gst_element_class_set_details (element_class, &element_details);
}

/* initialize the plugin's class */
static void
gst_assrender_class_init (GstassrenderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_assrender_set_property;
  gobject_class->get_property = gst_assrender_get_property;

  gobject_class->finalize = gst_assrender_finalize;

  g_object_class_install_property (gobject_class, ARG_ENABLE,
      g_param_spec_boolean ("enable", "Toggle rendering",
          "Enable rendering of subtitles", TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_EMBEDDEDFONTS,
      g_param_spec_boolean ("embeddedfonts", "Use embedded fonts",
          "Extract and use fonts embedded in the stream",
          TRUE, G_PARAM_READWRITE));
}

static void
gst_assrender_init (Gstassrender * render, GstassrenderClass * gclass)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (render);
  GstPadTemplate *template;

  GST_DEBUG_OBJECT (render, "init");

  template = gst_element_class_get_pad_template (klass, "src");
  render->srcpad = gst_pad_new_from_template (template, "src");
  gst_object_unref (template);

  template = gst_element_class_get_pad_template (klass, "video_sink");
  render->video_sinkpad = gst_pad_new_from_template (template, "video_sink");
  gst_object_unref (template);

  template = gst_element_class_get_pad_template (klass, "text_sink");
  render->text_sinkpad = gst_pad_new_from_template (template, "text_sink");
  gst_object_unref (template);

  gst_pad_set_setcaps_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_assrender_setcaps_video));
  gst_pad_set_setcaps_function (render->text_sinkpad,
      GST_DEBUG_FUNCPTR (gst_assrender_setcaps_text));

  gst_pad_set_getcaps_function (render->srcpad,
      GST_DEBUG_FUNCPTR (gst_assrender_getcaps));

  gst_pad_set_chain_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_assrender_chain_video));
  gst_pad_set_chain_function (render->text_sinkpad,
      GST_DEBUG_FUNCPTR (gst_assrender_chain_text));

  gst_pad_set_event_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_assrender_event_video));
  gst_pad_set_event_function (render->text_sinkpad,
      GST_DEBUG_FUNCPTR (gst_assrender_event_text));

  gst_element_add_pad (GST_ELEMENT (render), render->srcpad);
  gst_element_add_pad (GST_ELEMENT (render), render->video_sinkpad);
  gst_element_add_pad (GST_ELEMENT (render), render->text_sinkpad);

  render->width = 0;
  render->height = 0;

  render->renderer_init_ok = FALSE;
  render->track_init_ok = FALSE;
  render->enable = TRUE;
  render->embeddedfonts = TRUE;

  render->video_segment = gst_segment_new ();
  if (render->video_segment) {
    gst_segment_init (render->video_segment, GST_FORMAT_TIME);
  } else {
    GST_WARNING_OBJECT (render, "video_segment creation failed");
    g_assert_not_reached ();
  }

  render->ass_library = ass_library_init ();
  ass_set_fonts_dir (render->ass_library, "./");
  ass_set_extract_fonts (render->ass_library, 0);

  render->ass_renderer = ass_renderer_init (render->ass_library);
  if (!render->ass_renderer) {
    GST_WARNING_OBJECT (render, "cannot create renderer instance");
    g_assert_not_reached ();
  }

  render->ass_track = NULL;

  GST_DEBUG_OBJECT (render, "init complete");
}

static void
gst_assrender_finalize (GObject * object)
{
  Gstassrender *render = GST_ASSRENDER (object);

  if (render->ass_track) {
    ass_free_track (render->ass_track);
  }

  if (render->ass_renderer) {
    ass_renderer_done (render->ass_renderer);
  }

  if (render->ass_library) {
    ass_library_done (render->ass_library);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_assrender_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstassrender *render = GST_ASSRENDER (object);

  switch (prop_id) {
    case ARG_ENABLE:
      render->enable = g_value_get_boolean (value);
      break;
    case ARG_EMBEDDEDFONTS:
      render->embeddedfonts = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_assrender_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstassrender *render = GST_ASSRENDER (object);

  switch (prop_id) {
    case ARG_ENABLE:
      g_value_set_boolean (value, render->enable);
      break;
    case ARG_EMBEDDEDFONTS:
      g_value_set_boolean (value, render->embeddedfonts);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_assrender_getcaps (GstPad * pad)
{
  Gstassrender *render;
  GstPad *otherpad;
  GstCaps *caps;

  render = GST_ASSRENDER (gst_pad_get_parent (pad));

  if (pad == render->srcpad)
    otherpad = render->video_sinkpad;
  else
    otherpad = render->srcpad;

  /* we can do what the peer can */
  caps = gst_pad_peer_get_caps (otherpad);
  if (caps) {
    GstCaps *temp;
    const GstCaps *templ;

    /* filtered against our padtemplate */
    templ = gst_pad_get_pad_template_caps (otherpad);
    temp = gst_caps_intersect (caps, templ);
    gst_caps_unref (caps);
    /* this is what we can do */
    caps = temp;
  } else {
    /* no peer, our padtemplate is enough then */
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }


  gst_object_unref (render);

  return caps;
}

static gboolean
gst_assrender_setcaps_video (GstPad * pad, GstCaps * caps)
{
  Gstassrender *render;
  GstStructure *structure;
  gboolean ret = FALSE;
  double aspect_ratio = 1.0;

  if (!gst_caps_is_fixed (caps)) {
    return FALSE;
  }

  render = GST_ASSRENDER (gst_pad_get_parent (pad));

  render->width = 0;
  render->height = 0;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_double (structure, "pixel-aspect-ratio", &aspect_ratio);

  if (gst_structure_get_int (structure, "width", &render->width) &&
      gst_structure_get_int (structure, "height", &render->height)) {
    ret = gst_pad_set_caps (render->srcpad, caps);
    ass_set_frame_size (render->ass_renderer, render->width, render->height);
    ass_set_aspect_ratio (render->ass_renderer, aspect_ratio);
    ass_set_font_scale (render->ass_renderer, 1.0);
    ass_set_hinting (render->ass_renderer, ASS_HINTING_NATIVE);
    ass_set_fonts (render->ass_renderer, "Arial", "sans-serif");
    ass_set_fonts (render->ass_renderer, NULL, "Sans");
    ass_set_margins (render->ass_renderer, 0, 0, 0, 0);
    ass_set_use_margins (render->ass_renderer, 0);

    render->renderer_init_ok = TRUE;

    GST_DEBUG_OBJECT (render, "ass renderer setup complete");
  }

  gst_object_unref (render);

  return ret;
}

static gboolean
gst_assrender_setcaps_text (GstPad * pad, GstCaps * caps)
{
  Gstassrender *render;
  GstStructure *structure;
  const GValue *value;
  GstBuffer *priv;
  gchar *codec_private;
  guint codec_private_size;
  gboolean ret = FALSE;

  render = GST_ASSRENDER (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (render, "text pad linked with caps:  %" GST_PTR_FORMAT,
      caps);

  if (gst_structure_has_name (structure, "application/x-ass") ||
      gst_structure_has_name (structure, "application/x-ssa")) {

    value = gst_structure_get_value (structure, "codec_data");

    if (value != NULL) {
      priv = (GstBuffer *) gst_value_get_mini_object (value);
      g_return_val_if_fail (priv != NULL, FALSE);

      gst_buffer_ref (priv);

      codec_private = (gchar *) GST_BUFFER_DATA (priv);
      codec_private_size = GST_BUFFER_SIZE (priv);

      if (render->ass_track) {
        ass_free_track (render->ass_track);
      }

      render->ass_track = ass_new_track (render->ass_library);
      ass_process_codec_private (render->ass_track,
          codec_private, codec_private_size);

      GST_DEBUG_OBJECT (render, "ass track created");

      render->track_init_ok = TRUE;

      gst_buffer_unref (priv);

      ret = TRUE;
    }
  }

  gst_object_unref (render);

  return ret;
}

static GstFlowReturn
gst_assrender_chain_video (GstPad * pad, GstBuffer * buffer)
{
  Gstassrender *render;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean in_seg = FALSE;
  gint64 start, stop, clip_start = 0, clip_stop = 0;
  double timestamp;
  double step;
  ass_image_t *ass_image;

  render = GST_ASSRENDER (GST_PAD_PARENT (pad));

  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer)) {
    GST_WARNING_OBJECT (render, "buffer without timestamp, discarding");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  /* ignore buffers that are outside of the current segment */
  start = GST_BUFFER_TIMESTAMP (buffer);

  if (!GST_BUFFER_DURATION_IS_VALID (buffer)) {
    stop = GST_CLOCK_TIME_NONE;
  } else {
    stop = start + GST_BUFFER_DURATION (buffer);
  }

  /* segment_clip() will adjust start unconditionally to segment_start if
   * no stop time is provided, so handle this ourselves */
  if (stop == GST_CLOCK_TIME_NONE && start < render->video_segment->start)
    goto out_of_segment;

  in_seg =
      gst_segment_clip (render->video_segment, GST_FORMAT_TIME, start, stop,
      &clip_start, &clip_stop);

  if (!in_seg)
    goto out_of_segment;

  /* if the buffer is only partially in the segment, fix up stamps */
  if (clip_start != start || (stop != -1 && clip_stop != stop)) {
    GST_DEBUG_OBJECT (render, "clipping buffer timestamp/duration to segment");
    buffer = gst_buffer_make_metadata_writable (buffer);
    GST_BUFFER_TIMESTAMP (buffer) = clip_start;
    if (stop != -1)
      GST_BUFFER_DURATION (buffer) = clip_stop - clip_start;
  }

  gst_segment_set_last_stop (render->video_segment, GST_FORMAT_TIME,
      clip_start);

  /* now start rendering subtitles, if all conditions are met */
  if (render->renderer_init_ok && render->track_init_ok && render->enable) {
    int counter;

    timestamp = GST_BUFFER_TIMESTAMP (buffer);
    GST_DEBUG_OBJECT (render, "rendering frame for timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timestamp));
    /* libass needs timestamps in ms */
    timestamp = timestamp / 1000 / 1000;

    /* only for testing right now. could possibly be used for optimizations? */
    step = ass_step_sub (render->ass_track, timestamp, 1);
    GST_DEBUG_OBJECT (render, "Current timestamp: %" GST_TIME_FORMAT
        " // Next event: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
        GST_TIME_ARGS (step * 1000 * 1000));

    /* not sure what the last parameter to this call is for (detect_change) */
    ass_image = ass_render_frame (render->ass_renderer, render->ass_track,
        timestamp, 0);

    if (ass_image == NULL) {
      GST_LOG_OBJECT (render, "nothing to render right now");
      ret = gst_pad_push (render->srcpad, buffer);
      return ret;
    }

    counter = 0;
    while (ass_image) {
      /* blend subtitles onto the video frame */
      guint8 alpha = 255 - ((ass_image->color) & 0xff);
      guint8 r = ((ass_image->color) >> 24) & 0xff;
      guint8 g = ((ass_image->color) >> 16) & 0xff;
      guint8 b = ((ass_image->color) >> 8) & 0xff;

      guint8 *src = ass_image->bitmap;
      guint8 *dst =
          buffer->data + ass_image->dst_y * (render->width * 3) +
          ass_image->dst_x * 3;

      guint x = 0;
      guint y = 0;

      for (y = 0; y < ass_image->h; y++) {
        for (x = 0; x < ass_image->w; x++) {
          guint8 k = ((guint8) src[x]) * alpha / 255;
          dst[x * 3] = (k * r + (255 - k) * dst[x * 3]) / 255;
          dst[x * 3 + 1] = (k * g + (255 - k) * dst[x * 3 + 1]) / 255;
          dst[x * 3 + 2] = (k * b + (255 - k) * dst[x * 3 + 2]) / 255;
        }
        src += ass_image->stride;
        dst += render->width * 3;
      }
      counter++;
      ass_image = ass_image->next;
    }
    GST_LOG_OBJECT (render, "amount of rendered ass_image: %d", counter);
  }

  ret = gst_pad_push (render->srcpad, buffer);

  return ret;

out_of_segment:
  {
    GST_DEBUG_OBJECT (render, "buffer out of segment, discarding");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_assrender_chain_text (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  Gstassrender *render;
  gchar *data;
  guint size;
  double pts_start;
  double pts_end;

  render = GST_ASSRENDER (GST_PAD_PARENT (pad));

  data = (gchar *) GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);
  pts_start = GST_BUFFER_TIMESTAMP (buffer);
  pts_start = pts_start / 1000 / 1000;
  pts_end = GST_BUFFER_DURATION (buffer);
  pts_end = pts_end / 1000 / 1000;

  ass_process_chunk (render->ass_track, data, size, pts_start, pts_end);

  GST_DEBUG_OBJECT (render,
      "processed text packet with timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

  gst_buffer_unref (buffer);

  return ret;
}

static gboolean
gst_assrender_event_video (GstPad * pad, GstEvent * event)
{
  gboolean ret = FALSE;
  Gstassrender *render;

  render = GST_ASSRENDER (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pad, "received video event %s",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate;
      gint64 start, stop, time;
      gboolean update;

      GST_DEBUG_OBJECT (render, "received new segment");

      gst_event_parse_new_segment (event, &update, &rate, &format, &start,
          &stop, &time);

      if (format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (render, "VIDEO SEGMENT now: %" GST_SEGMENT_FORMAT,
            render->video_segment);

        gst_segment_set_newsegment (render->video_segment, update, rate, format,
            start, stop, time);
      } else {
        GST_ELEMENT_WARNING (render, STREAM, MUX, (NULL),
            ("received non-TIME newsegment event on video input"));
      }

      ret = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_TAG:
    {
      GstTagList *taglist = gst_tag_list_new ();
      guint tag_size;
      guint index;

      /* tag events may contain attachments which might be fonts */
      GST_DEBUG_OBJECT (render, "got TAG event");

      gst_event_parse_tag (event, &taglist);
      tag_size = gst_tag_list_get_tag_size (taglist, GST_TAG_ATTACHMENT);
      if (tag_size > 0 && render->embeddedfonts) {
        const GValue *value;
        GstBuffer *buf;
        GstCaps *caps;
        GstStructure *structure;

        GST_DEBUG_OBJECT (render, "TAG event has attachments");

        for (index = 0; index < tag_size; index++) {
          value = gst_tag_list_get_value_index (taglist, GST_TAG_ATTACHMENT,
              index);
          buf = (GstBuffer *) gst_value_get_mini_object (value);
          if (!buf) {
            continue;
          }
          gst_buffer_ref (buf);
          caps = GST_BUFFER_CAPS (buf);
          structure = gst_caps_get_structure (caps, 0);
          if (gst_structure_has_name (structure, "application/x-truetype-font")
              && gst_structure_has_field (structure, "filename")) {
            const gchar *filename;
            filename = gst_structure_get_string (structure, "filename");
            ass_add_font (render->ass_library, (gchar *) filename,
                (gchar *) GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
            GST_DEBUG_OBJECT (render, "registered new font %s", filename);
          }
          gst_buffer_unref (buf);
        }
      }
      ret = gst_pad_event_default (pad, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (render);

  return ret;
}

static gboolean
gst_assrender_event_text (GstPad * pad, GstEvent * event)
{
  int i;
  gboolean ret = FALSE;
  Gstassrender *render;

  render = GST_ASSRENDER (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pad, "received text event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gst_event_unref (event);
      ret = TRUE;
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (render, "begin flushing");
      if (render->ass_track) {
        GST_OBJECT_LOCK (render);
        /* delete any events on the ass_track */
        for (i = 0; i < render->ass_track->n_events; i++) {
          GST_DEBUG_OBJECT (render, "deleted event with eid %i", i);
          ass_free_event (render->ass_track, i);
        }
        render->ass_track->n_events = 0;
        GST_OBJECT_UNLOCK (render);
        GST_DEBUG_OBJECT (render, "done flushing");
      }
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_EOS:
      GST_OBJECT_LOCK (render);
      GST_INFO_OBJECT (render, "text EOS");
      GST_OBJECT_UNLOCK (render);
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_TAG:
    {
      GstTagList *taglist = gst_tag_list_new ();
      guint tag_size;
      guint index;

      GST_DEBUG_OBJECT (render, "got TAG event");

      gst_event_parse_tag (event, &taglist);
      tag_size = gst_tag_list_get_tag_size (taglist, GST_TAG_ATTACHMENT);
      if (tag_size > 0 && render->embeddedfonts) {
        const GValue *value;
        GstBuffer *buf;
        GstCaps *caps;
        GstStructure *structure;

        GST_DEBUG_OBJECT (render, "TAG event has attachments");

        for (index = 0; index < tag_size; index++) {
          value = gst_tag_list_get_value_index (taglist, GST_TAG_ATTACHMENT,
              index);
          buf = (GstBuffer *) gst_value_get_mini_object (value);
          if (!buf) {
            continue;
          }
          gst_buffer_ref (buf);
          caps = GST_BUFFER_CAPS (buf);
          structure = gst_caps_get_structure (caps, 0);
          if (gst_structure_has_name (structure, "application/x-truetype-font")
              && gst_structure_has_field (structure, "filename")) {
            const gchar *filename;
            filename = gst_structure_get_string (structure, "filename");
            ass_add_font (render->ass_library, (gchar *) filename,
                (gchar *) GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
            GST_DEBUG_OBJECT (render, "registered new font %s", filename);
          }
          gst_buffer_unref (buf);
        }
      }
      ret = gst_pad_event_default (pad, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (render);

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_assrender_debug, "assrender",
      0, "ASS/SSA subtitle renderer");

  return gst_element_register (plugin, "assrender",
      GST_RANK_NONE, GST_TYPE_ASSRENDER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "assrender",
    "ASS/SSA subtitle renderer",
    plugin_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
