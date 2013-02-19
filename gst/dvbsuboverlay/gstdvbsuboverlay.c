/* GStreamer DVB subtitles overlay
 * Copyright (c) 2010 Mart Raudsepp <mart.raudsepp@collabora.co.uk>
 * Copyright (c) 2010 ONELAN Ltd.
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
 * SECTION:element-dvbsuboverlay
 *
 * Renders DVB subtitles on top of a video stream.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[ FIXME
 * gst-launch -v filesrc location=/path/to/ts ! mpegtsdemux name=d ! queue ! mp3parse ! mad ! audioconvert ! autoaudiosink \
 *     d. ! queue ! mpeg2dec ! videoconvert ! r. \
 *     d. ! queue ! "subpicture/x-dvb" ! dvbsuboverlay name=r ! videoconvert ! autovideosink
 * ]| This pipeline demuxes a MPEG-TS file with MPEG2 video, MP3 audio and embedded DVB subtitles and renders the subtitles on top of the video.
 * </refsect2>
 */


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/glib-compat-private.h>
#include "gstdvbsuboverlay.h"

#include <gst/video/gstvideometa.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_dvbsub_overlay_debug);
#define GST_CAT_DEFAULT gst_dvbsub_overlay_debug

/* Filter signals and props */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_ENABLE,
  PROP_MAX_PAGE_TIMEOUT,
  PROP_FORCE_END
};

#define DEFAULT_ENABLE (TRUE)
#define DEFAULT_MAX_PAGE_TIMEOUT (0)
#define DEFAULT_FORCE_END (FALSE)

#define VIDEO_FORMATS GST_VIDEO_OVERLAY_COMPOSITION_BLEND_FORMATS

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (VIDEO_FORMATS))
    );

static GstStaticPadTemplate video_sink_factory =
GST_STATIC_PAD_TEMPLATE ("video_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (VIDEO_FORMATS))
    );

static GstStaticPadTemplate text_sink_factory =
GST_STATIC_PAD_TEMPLATE ("text_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("subpicture/x-dvb")
    );

static void gst_dvbsub_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dvbsub_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_dvbsub_overlay_finalize (GObject * object);

static GstStateChangeReturn gst_dvbsub_overlay_change_state (GstElement *
    element, GstStateChange transition);

#define gst_dvbsub_overlay_parent_class parent_class
G_DEFINE_TYPE (GstDVBSubOverlay, gst_dvbsub_overlay, GST_TYPE_ELEMENT);

static GstCaps *gst_dvbsub_overlay_getcaps (GstDVBSubOverlay * render,
    GstPad * pad, GstCaps * filter);

static GstFlowReturn gst_dvbsub_overlay_chain_video (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static GstFlowReturn gst_dvbsub_overlay_chain_text (GstPad * pad,
    GstObject * parent, GstBuffer * buf);

static gboolean gst_dvbsub_overlay_event_video (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_dvbsub_overlay_event_text (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_dvbsub_overlay_event_src (GstPad * pad, GstObject * parent,
    GstEvent * event);

static void new_dvb_subtitles_cb (DvbSub * dvb_sub, DVBSubtitles * subs,
    gpointer user_data);

static gboolean gst_dvbsub_overlay_query_video (GstPad * pad,
    GstObject * parent, GstQuery * query);
static gboolean gst_dvbsub_overlay_query_src (GstPad * pad, GstObject * parent,
    GstQuery * query);

/* initialize the plugin's class */
static void
gst_dvbsub_overlay_class_init (GstDVBSubOverlayClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_dvbsub_overlay_set_property;
  gobject_class->get_property = gst_dvbsub_overlay_get_property;
  gobject_class->finalize = gst_dvbsub_overlay_finalize;

  g_object_class_install_property (gobject_class, PROP_ENABLE, g_param_spec_boolean ("enable", "Enable",        /* FIXME: "enable" vs "silent"? */
          "Enable rendering of subtitles", DEFAULT_ENABLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_PAGE_TIMEOUT,
      g_param_spec_int ("max-page-timeout", "max-page-timeout",
          "Limit maximum display time of a subtitle page (0 - disabled, value in seconds)",
          0, G_MAXINT, DEFAULT_MAX_PAGE_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FORCE_END,
      g_param_spec_boolean ("force-end", "Force End",
          "Assume PES-aligned subtitles and force end-of-display",
          DEFAULT_FORCE_END, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_change_state);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&video_sink_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&text_sink_factory));

  gst_element_class_set_static_metadata (gstelement_class,
      "DVB Subtitles Overlay",
      "Mixer/Video/Overlay/Subtitle",
      "Renders DVB subtitles", "Mart Raudsepp <mart.raudsepp@collabora.co.uk>");
}

static void
gst_dvbsub_overlay_flush_subtitles (GstDVBSubOverlay * render)
{
  DVBSubtitles *subs;

  g_mutex_lock (&render->dvbsub_mutex);
  while ((subs = g_queue_pop_head (render->pending_subtitles))) {
    dvb_subtitles_free (subs);
  }

  if (render->current_subtitle)
    dvb_subtitles_free (render->current_subtitle);
  render->current_subtitle = NULL;

  if (render->current_comp)
    gst_video_overlay_composition_unref (render->current_comp);
  render->current_comp = NULL;

  if (render->dvb_sub)
    dvb_sub_free (render->dvb_sub);

  render->dvb_sub = dvb_sub_new ();

  {
    DvbSubCallbacks dvbsub_callbacks = { &new_dvb_subtitles_cb, };
    dvb_sub_set_callbacks (render->dvb_sub, &dvbsub_callbacks, render);
  }

  render->last_text_pts = GST_CLOCK_TIME_NONE;
  render->pending_sub = FALSE;

  g_mutex_unlock (&render->dvbsub_mutex);
}

static void
gst_dvbsub_overlay_init (GstDVBSubOverlay * render)
{
  GST_DEBUG_OBJECT (render, "init");

  render->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  render->video_sinkpad =
      gst_pad_new_from_static_template (&video_sink_factory, "video_sink");
  render->text_sinkpad =
      gst_pad_new_from_static_template (&text_sink_factory, "text_sink");

  gst_pad_set_chain_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_chain_video));
  gst_pad_set_chain_function (render->text_sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_chain_text));

  gst_pad_set_event_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_event_video));
  gst_pad_set_event_function (render->text_sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_event_text));
  gst_pad_set_event_function (render->srcpad,
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_event_src));

  gst_pad_set_query_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_query_video));
  gst_pad_set_query_function (render->srcpad,
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_query_src));

  GST_PAD_SET_PROXY_ALLOCATION (render->video_sinkpad);

  gst_element_add_pad (GST_ELEMENT (render), render->srcpad);
  gst_element_add_pad (GST_ELEMENT (render), render->video_sinkpad);
  gst_element_add_pad (GST_ELEMENT (render), render->text_sinkpad);

  gst_video_info_init (&render->info);

  render->current_subtitle = NULL;
  render->pending_subtitles = g_queue_new ();

  render->enable = DEFAULT_ENABLE;
  render->max_page_timeout = DEFAULT_MAX_PAGE_TIMEOUT;
  render->force_end = DEFAULT_FORCE_END;

  g_mutex_init (&render->dvbsub_mutex);
  gst_dvbsub_overlay_flush_subtitles (render);

  gst_segment_init (&render->video_segment, GST_FORMAT_TIME);
  gst_segment_init (&render->subtitle_segment, GST_FORMAT_TIME);

  GST_DEBUG_OBJECT (render, "init complete");
}

static void
gst_dvbsub_overlay_finalize (GObject * object)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (object);
  DVBSubtitles *subs;

  while ((subs = g_queue_pop_head (overlay->pending_subtitles))) {
    dvb_subtitles_free (subs);
  }
  g_queue_free (overlay->pending_subtitles);

  if (overlay->current_subtitle)
    dvb_subtitles_free (overlay->current_subtitle);
  overlay->current_subtitle = NULL;

  if (overlay->current_comp)
    gst_video_overlay_composition_unref (overlay->current_comp);
  overlay->current_comp = NULL;

  if (overlay->dvb_sub)
    dvb_sub_free (overlay->dvb_sub);

  g_mutex_clear (&overlay->dvbsub_mutex);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_dvbsub_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (object);

  switch (prop_id) {
    case PROP_ENABLE:
      g_atomic_int_set (&overlay->enable, g_value_get_boolean (value));
      break;
    case PROP_MAX_PAGE_TIMEOUT:
      g_atomic_int_set (&overlay->max_page_timeout, g_value_get_int (value));
      break;
    case PROP_FORCE_END:
      g_atomic_int_set (&overlay->force_end, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dvbsub_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (object);

  switch (prop_id) {
    case PROP_ENABLE:
      g_value_set_boolean (value, g_atomic_int_get (&overlay->enable));
      break;
    case PROP_MAX_PAGE_TIMEOUT:
      g_value_set_int (value, g_atomic_int_get (&overlay->max_page_timeout));
      break;
    case PROP_FORCE_END:
      g_value_set_boolean (value, g_atomic_int_get (&overlay->force_end));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_dvbsub_overlay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_segment_init (&render->video_segment, GST_FORMAT_TIME);
      gst_segment_init (&render->subtitle_segment, GST_FORMAT_TIME);
      break;
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_dvbsub_overlay_flush_subtitles (render);
      gst_segment_init (&render->video_segment, GST_FORMAT_TIME);
      gst_segment_init (&render->subtitle_segment, GST_FORMAT_TIME);
      gst_video_info_init (&render->info);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }


  return ret;
}

static gboolean
gst_dvbsub_overlay_query_src (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (parent);
  gboolean ret;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_dvbsub_overlay_getcaps (render, pad, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

static gboolean
gst_dvbsub_overlay_event_src (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (parent);
  gboolean ret = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstSeekFlags flags;

      GST_DEBUG_OBJECT (render, "seek received, driving from here");

      gst_event_parse_seek (event, NULL, NULL, &flags, NULL, NULL, NULL, NULL);

      /* Flush downstream, only for flushing seek */
      if (flags & GST_SEEK_FLAG_FLUSH)
        gst_pad_push_event (render->srcpad, gst_event_new_flush_start ());

      gst_dvbsub_overlay_flush_subtitles (render);

      /* Seek on each sink pad */
      gst_event_ref (event);
      ret = gst_pad_push_event (render->video_sinkpad, event);
      if (ret) {
        ret = gst_pad_push_event (render->text_sinkpad, event);
      } else {
        gst_event_unref (event);
      }
      break;
    }
    default:
      gst_event_ref (event);
      ret = gst_pad_push_event (render->video_sinkpad, event);
      gst_pad_push_event (render->text_sinkpad, event);
      break;
  }

  return ret;
}

static GstCaps *
gst_dvbsub_overlay_getcaps (GstDVBSubOverlay * render, GstPad * pad,
    GstCaps * filter)
{
  GstPad *otherpad;
  GstCaps *caps, *templ;

  if (pad == render->srcpad)
    otherpad = render->video_sinkpad;
  else
    otherpad = render->srcpad;

  templ = gst_pad_get_pad_template_caps (otherpad);

  /* we can do what the peer can */
  caps = gst_pad_peer_query_caps (otherpad, filter);
  if (caps) {
    GstCaps *temp;

    /* filtered against our padtemplate */
    temp = gst_caps_intersect (caps, templ);
    gst_caps_unref (templ);
    gst_caps_unref (caps);
    /* this is what we can do */
    caps = temp;
  } else {
    /* no peer, our padtemplate is enough then */
    caps = templ;
  }

  return caps;
}

/* only negotiate/query video overlay composition support for now */
static gboolean
gst_dvbsub_overlay_negotiate (GstDVBSubOverlay * overlay)
{
  GstCaps *target;
  GstQuery *query;
  gboolean attach = FALSE;

  GST_DEBUG_OBJECT (overlay, "performing negotiation");

  target = gst_pad_get_current_caps (overlay->srcpad);

  if (!target || gst_caps_is_empty (target))
    goto no_format;

  /* find supported meta */
  query = gst_query_new_allocation (target, TRUE);

  if (!gst_pad_peer_query (overlay->srcpad, query)) {
    /* no problem, we use the query defaults */
    GST_DEBUG_OBJECT (overlay, "ALLOCATION query failed");
  }

  if (gst_query_find_allocation_meta (query,
          GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, NULL))
    attach = TRUE;

  overlay->attach_compo_to_buffer = attach;

  gst_query_unref (query);
  gst_caps_unref (target);

  return TRUE;

no_format:
  {
    if (target)
      gst_caps_unref (target);
    return FALSE;
  }
}

static gboolean
gst_dvbsub_overlay_setcaps_video (GstPad * pad, GstCaps * caps)
{
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (gst_pad_get_parent (pad));
  gboolean ret = FALSE;
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps))
    goto invalid_caps;

  render->info = info;

  ret = gst_pad_set_caps (render->srcpad, caps);
  if (!ret)
    goto out;

  gst_dvbsub_overlay_negotiate (render);

  GST_DEBUG_OBJECT (render, "ass renderer setup complete");

out:
  gst_object_unref (render);

  return ret;

  /* ERRORS */
invalid_caps:
  {
    GST_ERROR_OBJECT (render, "Can't parse caps: %" GST_PTR_FORMAT, caps);
    ret = FALSE;
    goto out;
  }
}

static void
gst_dvbsub_overlay_process_text (GstDVBSubOverlay * overlay, GstBuffer * buffer,
    guint64 pts)
{
  GstMapInfo map;

  GST_DEBUG_OBJECT (overlay,
      "Processing subtitles with PTS=%" G_GUINT64_FORMAT
      " which is a time of %" GST_TIME_FORMAT, pts, GST_TIME_ARGS (pts));

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  GST_DEBUG_OBJECT (overlay, "Feeding %" G_GSIZE_FORMAT " bytes to libdvbsub",
      map.size);

  g_mutex_lock (&overlay->dvbsub_mutex);
  overlay->pending_sub = TRUE;
  dvb_sub_feed_with_pts (overlay->dvb_sub, pts, map.data, map.size);
  g_mutex_unlock (&overlay->dvbsub_mutex);

  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  if (overlay->pending_sub && overlay->force_end) {
    GST_DEBUG_OBJECT (overlay, "forcing subtitle end");
    dvb_sub_feed_with_pts (overlay->dvb_sub, overlay->last_text_pts, NULL, 0);
    g_assert (overlay->pending_sub == FALSE);
  }
}

static void
new_dvb_subtitles_cb (DvbSub * dvb_sub, DVBSubtitles * subs, gpointer user_data)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (user_data);
  int max_page_timeout;
  guint64 start, stop;

  max_page_timeout = g_atomic_int_get (&overlay->max_page_timeout);
  if (max_page_timeout > 0)
    subs->page_time_out = MIN (subs->page_time_out, max_page_timeout);

  GST_INFO_OBJECT (overlay,
      "New DVB subtitles arrived with a page_time_out of %d and %d regions for "
      "PTS=%" G_GUINT64_FORMAT ", which should be at time %" GST_TIME_FORMAT,
      subs->page_time_out, subs->num_rects, subs->pts,
      GST_TIME_ARGS (subs->pts));

  /* spec says page_time_out is not to be taken very accurately anyway,
   * and 0 does not make useful sense anyway */
  if (!subs->page_time_out) {
    GST_WARNING_OBJECT (overlay, "overriding page_time_out 0");
    subs->page_time_out = 1;
  }

  /* clip and convert to running time */
  start = subs->pts;
  stop = subs->pts + subs->page_time_out;

  if (!(gst_segment_clip (&overlay->subtitle_segment, GST_FORMAT_TIME,
              start, stop, &start, &stop)))
    goto out_of_segment;

  subs->page_time_out = stop - start;

  gst_segment_to_running_time (&overlay->subtitle_segment, GST_FORMAT_TIME,
      start);
  g_assert (GST_CLOCK_TIME_IS_VALID (start));
  subs->pts = start;

  GST_DEBUG_OBJECT (overlay, "SUBTITLE real running time: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (start));

  g_queue_push_tail (overlay->pending_subtitles, subs);
  overlay->pending_sub = FALSE;

  return;

out_of_segment:
  {
    GST_DEBUG_OBJECT (overlay, "subtitle out of segment, discarding");
    dvb_subtitles_free (subs);
  }
}

static GstFlowReturn
gst_dvbsub_overlay_chain_text (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (parent);

  GST_INFO_OBJECT (overlay,
      "subpicture/x-dvb buffer with size %" G_GSIZE_FORMAT,
      gst_buffer_get_size (buffer));

  GST_LOG_OBJECT (overlay,
      "Video segment: %" GST_SEGMENT_FORMAT " --- Subtitle segment: %"
      GST_SEGMENT_FORMAT " --- BUFFER: ts=%" GST_TIME_FORMAT,
      &overlay->video_segment, &overlay->subtitle_segment,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

  /* DVB subtitle packets are required to carry the PTS */
  if (G_UNLIKELY (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer))) {
    GST_WARNING_OBJECT (overlay,
        "Text buffer without valid timestamp, dropping");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  /* spec states multiple PES packets may have same PTS,
   * and same PTS packets make up a display set */
  if (overlay->pending_sub &&
      overlay->last_text_pts != GST_BUFFER_TIMESTAMP (buffer)) {
    GST_DEBUG_OBJECT (overlay, "finishing previous subtitle");
    dvb_sub_feed_with_pts (overlay->dvb_sub, overlay->last_text_pts, NULL, 0);
    overlay->pending_sub = FALSE;
  }

  overlay->last_text_pts = GST_BUFFER_TIMESTAMP (buffer);

  /* As the passed start and stop is equal, we shouldn't need to care about out of segment at all,
   * the subtitle data for the PTS is completely out of interest to us. A given display set must
   * carry the same PTS value. */
  /* FIXME: Consider with larger than 64kB display sets, which would be cut into multiple packets,
   * FIXME: does our waiting + render code work when there are more than one packets before
   * FIXME: rendering callback will get called? */

  overlay->subtitle_segment.position = GST_BUFFER_TIMESTAMP (buffer);

  gst_dvbsub_overlay_process_text (overlay, buffer,
      GST_BUFFER_TIMESTAMP (buffer));

  return GST_FLOW_OK;
}

static GstVideoOverlayComposition *
gst_dvbsub_overlay_subs_to_comp (GstDVBSubOverlay * overlay,
    DVBSubtitles * subs)
{
  GstVideoOverlayComposition *comp = NULL;
  GstVideoOverlayRectangle *rect;
  gint width, height, dw, dh, wx, wy;
  gint i;

  g_return_val_if_fail (subs != NULL && subs->num_rects > 0, NULL);

  width = GST_VIDEO_INFO_WIDTH (&overlay->info);
  height = GST_VIDEO_INFO_HEIGHT (&overlay->info);

  dw = subs->display_def.display_width;
  dh = subs->display_def.display_height;

  GST_LOG_OBJECT (overlay,
      "converting %d rectangles for display %dx%d -> video %dx%d",
      subs->num_rects, dw, dh, width, height);

  if (subs->display_def.window_flag) {
    wx = subs->display_def.window_x;
    wy = subs->display_def.window_y;
    GST_LOG_OBJECT (overlay, "display window %dx%d @ (%d, %d)",
        subs->display_def.window_width, subs->display_def.window_height,
        wx, wy);
  } else {
    wx = 0;
    wy = 0;
  }

  for (i = 0; i < subs->num_rects; i++) {
    DVBSubtitleRect *srect = &subs->rects[i];
    GstBuffer *buf;
    gint w, h;
    guint8 *in_data;
    guint32 *palette, *data;
    gint rx, ry, rw, rh, stride;
    gint k, l;
    GstMapInfo map;

    GST_LOG_OBJECT (overlay, "rectangle %d: %dx%d @ (%d, %d)", i,
        srect->w, srect->h, srect->x, srect->y);

    w = srect->w;
    h = srect->h;

    buf = gst_buffer_new_and_alloc (w * h * 4);
    gst_buffer_map (buf, &map, GST_MAP_WRITE);
    data = (guint32 *) map.data;
    in_data = srect->pict.data;
    palette = srect->pict.palette;
    stride = srect->pict.rowstride;
    for (k = 0; k < h; k++) {
      for (l = 0; l < w; l++) {
        guint32 ayuv;

        ayuv = palette[*in_data];
        GST_WRITE_UINT32_BE (data, ayuv);
        in_data++;
        data++;
      }
      in_data += stride - w;
    }
    gst_buffer_unmap (buf, &map);

    /* this is assuming the subtitle rectangle coordinates are relative
     * to the window (if there is one) within a display of specified dimension.
     * Coordinate wrt the latter is then scaled to the actual dimension of
     * the video we are dealing with here. */
    rx = gst_util_uint64_scale (wx + srect->x, width, dw);
    ry = gst_util_uint64_scale (wy + srect->y, height, dh);
    rw = gst_util_uint64_scale (srect->w, width, dw);
    rh = gst_util_uint64_scale (srect->h, height, dh);

    GST_LOG_OBJECT (overlay, "rectangle %d rendered: %dx%d @ (%d, %d)", i,
        rw, rh, rx, ry);

    gst_buffer_add_video_meta (buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_YUV, w, h);
    rect = gst_video_overlay_rectangle_new_raw (buf, rx, ry, rw, rh, 0);
    g_assert (rect);
    if (comp) {
      gst_video_overlay_composition_add_rectangle (comp, rect);
    } else {
      comp = gst_video_overlay_composition_new (rect);
    }
    gst_video_overlay_rectangle_unref (rect);
    gst_buffer_unref (buf);
  }

  return comp;
}

static GstFlowReturn
gst_dvbsub_overlay_chain_video (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (parent);
  GstFlowReturn ret = GST_FLOW_OK;
  gint64 start, stop;
  guint64 cstart, cstop;
  gboolean in_seg;
  GstClockTime vid_running_time, vid_running_time_end;

  if (GST_VIDEO_INFO_FORMAT (&overlay->info) == GST_VIDEO_FORMAT_UNKNOWN)
    return GST_FLOW_NOT_NEGOTIATED;

  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    goto missing_timestamp;

  start = GST_BUFFER_TIMESTAMP (buffer);

  GST_LOG_OBJECT (overlay,
      "Video segment: %" GST_SEGMENT_FORMAT " --- Subtitle position: %"
      GST_TIME_FORMAT " --- BUFFER: ts=%" GST_TIME_FORMAT,
      &overlay->video_segment,
      GST_TIME_ARGS (overlay->subtitle_segment.position),
      GST_TIME_ARGS (start));

  /* ignore buffers that are outside of the current segment */
  if (!GST_BUFFER_DURATION_IS_VALID (buffer)) {
    stop = GST_CLOCK_TIME_NONE;
  } else {
    stop = start + GST_BUFFER_DURATION (buffer);
  }

  in_seg = gst_segment_clip (&overlay->video_segment, GST_FORMAT_TIME,
      start, stop, &cstart, &cstop);
  if (!in_seg) {
    GST_DEBUG_OBJECT (overlay, "Buffer outside configured segment -- dropping");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  buffer = gst_buffer_make_writable (buffer);
  GST_BUFFER_TIMESTAMP (buffer) = cstart;
  if (GST_BUFFER_DURATION_IS_VALID (buffer))
    GST_BUFFER_DURATION (buffer) = cstop - cstart;

  vid_running_time =
      gst_segment_to_running_time (&overlay->video_segment, GST_FORMAT_TIME,
      cstart);
  if (GST_BUFFER_DURATION_IS_VALID (buffer))
    vid_running_time_end =
        gst_segment_to_running_time (&overlay->video_segment, GST_FORMAT_TIME,
        cstop);
  else
    vid_running_time_end = vid_running_time;

  GST_DEBUG_OBJECT (overlay, "Video running time: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (vid_running_time));

  overlay->video_segment.position = GST_BUFFER_TIMESTAMP (buffer);

  g_mutex_lock (&overlay->dvbsub_mutex);
  if (!g_queue_is_empty (overlay->pending_subtitles)) {
    DVBSubtitles *tmp, *candidate = NULL;

    while (!g_queue_is_empty (overlay->pending_subtitles)) {
      tmp = g_queue_peek_head (overlay->pending_subtitles);

      if (tmp->pts > vid_running_time_end) {
        /* For a future video frame */
        break;
      } else if (tmp->num_rects == 0) {
        /* Clear screen */
        if (overlay->current_subtitle)
          dvb_subtitles_free (overlay->current_subtitle);
        overlay->current_subtitle = NULL;
        if (candidate)
          dvb_subtitles_free (candidate);
        candidate = NULL;
        g_queue_pop_head (overlay->pending_subtitles);
        dvb_subtitles_free (tmp);
        tmp = NULL;
      } else if (tmp->pts + tmp->page_time_out * GST_SECOND *
          ABS (overlay->subtitle_segment.rate) >= vid_running_time) {
        if (candidate)
          dvb_subtitles_free (candidate);
        candidate = tmp;
        g_queue_pop_head (overlay->pending_subtitles);
      } else {
        /* Too late */
        dvb_subtitles_free (tmp);
        tmp = NULL;
        g_queue_pop_head (overlay->pending_subtitles);
      }
    }

    if (candidate) {
      GST_DEBUG_OBJECT (overlay,
          "Time to show the next subtitle page (%" GST_TIME_FORMAT " >= %"
          GST_TIME_FORMAT ") - it has %u regions",
          GST_TIME_ARGS (vid_running_time), GST_TIME_ARGS (candidate->pts),
          candidate->num_rects);
      dvb_subtitles_free (overlay->current_subtitle);
      overlay->current_subtitle = candidate;
      if (overlay->current_comp)
        gst_video_overlay_composition_unref (overlay->current_comp);
      overlay->current_comp =
          gst_dvbsub_overlay_subs_to_comp (overlay, overlay->current_subtitle);
    }
  }

  /* Check that we haven't hit the fallback timeout for current subtitle page */
  if (overlay->current_subtitle
      && vid_running_time >
      (overlay->current_subtitle->pts +
          overlay->current_subtitle->page_time_out * GST_SECOND *
          ABS (overlay->subtitle_segment.rate))) {
    GST_INFO_OBJECT (overlay,
        "Subtitle page not redefined before fallback page_time_out of %u seconds (missed data?) - deleting current page",
        overlay->current_subtitle->page_time_out);
    dvb_subtitles_free (overlay->current_subtitle);
    overlay->current_subtitle = NULL;
  }

  /* Now render it */
  if (g_atomic_int_get (&overlay->enable) && overlay->current_subtitle) {
    GstVideoFrame frame;

    g_assert (overlay->current_comp);
    if (overlay->attach_compo_to_buffer) {
      GST_DEBUG_OBJECT (overlay, "Attaching overlay image to video buffer");
      gst_buffer_add_video_overlay_composition_meta (buffer,
          overlay->current_comp);
    } else {
      GST_DEBUG_OBJECT (overlay, "Blending overlay image to video buffer");
      gst_video_frame_map (&frame, &overlay->info, buffer, GST_MAP_WRITE);
      gst_video_overlay_composition_blend (overlay->current_comp, &frame);
      gst_video_frame_unmap (&frame);
    }
  }
  g_mutex_unlock (&overlay->dvbsub_mutex);

  ret = gst_pad_push (overlay->srcpad, buffer);

  return ret;

missing_timestamp:
  {
    GST_WARNING_OBJECT (overlay, "video buffer without timestamp, discarding");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
}

static gboolean
gst_dvbsub_overlay_query_video (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstDVBSubOverlay *render = (GstDVBSubOverlay *) parent;
  gboolean ret;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_dvbsub_overlay_getcaps (render, pad, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }

  return ret;
}

static gboolean
gst_dvbsub_overlay_event_video (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean ret = FALSE;
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (parent);

  GST_DEBUG_OBJECT (pad, "received video event %s",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_dvbsub_overlay_setcaps_video (pad, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment seg;

      GST_DEBUG_OBJECT (render, "received new segment");

      gst_event_copy_segment (event, &seg);

      if (seg.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (render, "VIDEO SEGMENT now: %" GST_SEGMENT_FORMAT,
            &render->video_segment);

        render->video_segment = seg;

        GST_DEBUG_OBJECT (render, "VIDEO SEGMENT after: %" GST_SEGMENT_FORMAT,
            &render->video_segment);
        ret = gst_pad_push_event (render->srcpad, event);
      } else {
        GST_ELEMENT_WARNING (render, STREAM, MUX, (NULL),
            ("received non-TIME newsegment event on video input"));
        ret = FALSE;
        gst_event_unref (event);
      }
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (&render->video_segment, GST_FORMAT_TIME);
    default:
      ret = gst_pad_push_event (render->srcpad, event);
      break;
  }

  return ret;
}

static gboolean
gst_dvbsub_overlay_event_text (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  gboolean ret = FALSE;
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (parent);

  GST_DEBUG_OBJECT (pad, "received text event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      GstSegment seg;

      GST_DEBUG_OBJECT (render, "received new segment");

      gst_event_copy_segment (event, &seg);

      if (seg.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (render, "SUBTITLE SEGMENT now: %" GST_SEGMENT_FORMAT,
            &render->subtitle_segment);

        render->subtitle_segment = seg;

        GST_DEBUG_OBJECT (render,
            "SUBTITLE SEGMENT after: %" GST_SEGMENT_FORMAT,
            &render->subtitle_segment);
        ret = TRUE;
        gst_event_unref (event);
      } else {
        GST_ELEMENT_WARNING (render, STREAM, MUX, (NULL),
            ("received non-TIME newsegment event on subtitle sinkpad"));
        ret = FALSE;
        gst_event_unref (event);
      }
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (render, "stop flushing");
      gst_dvbsub_overlay_flush_subtitles (render);
      gst_segment_init (&render->subtitle_segment, GST_FORMAT_TIME);
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (render, "begin flushing");
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_EOS:
      GST_INFO_OBJECT (render, "text EOS");
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_CAPS:
      /* don't want to forward the subtitle caps */
      gst_event_unref (event);
      ret = TRUE;
      break;
    default:
      ret = gst_pad_push_event (render->srcpad, event);
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_dvbsub_overlay_debug, "dvbsuboverlay",
      0, "DVB subtitle overlay");

  return gst_element_register (plugin, "dvbsuboverlay",
      GST_RANK_PRIMARY, GST_TYPE_DVBSUB_OVERLAY);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dvbsuboverlay,
    "DVB subtitle renderer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
