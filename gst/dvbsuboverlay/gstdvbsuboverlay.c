/* GStreamer DVB subtitles overlay
 * Copyright (c) 2010 Mart Raudsepp <mart.raudsepp@collabora.co.uk>
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
 * SECTION:element-dvbsuboverlay
 *
 * Renders DVB subtitles on top of a video stream.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[ FIXME
 * gst-launch -v filesrc location=/path/to/ts ! mpegtsdemux name=d ! queue ! mp3parse ! mad ! audioconvert ! autoaudiosink \
 *     d. ! queue ! mpeg2dec ! ffmpegcolorspace ! r. \
 *     d. ! queue ! "private/x-dvbsub" ! dvbsuboverlay name=r ! ffmpegcolorspace ! autovideosink
 * ]| This pipeline demuxes a MPEG-TS file with MPEG2 video, MP3 audio and embedded DVB subtitles and renders the subtitles on top of the video.
 * </refsect2>
 */


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstdvbsuboverlay.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_dvbsub_overlay_debug);
GST_DEBUG_CATEGORY_STATIC (gst_dvbsub_overlay_lib_debug);
#define GST_CAT_DEFAULT gst_dvbsub_overlay_debug

/* Filter signals and props */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_ENABLE
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
#ifdef DVBSUB_OVERLAY_RGB_SUPPORT
        GST_VIDEO_CAPS_RGB ";" GST_VIDEO_CAPS_BGR ";"
        GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx ";"
#endif
        GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate video_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("video_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
#ifdef DVBSUB_OVERLAY_RGB_SUPPORT
        GST_VIDEO_CAPS_RGB ";" GST_VIDEO_CAPS_BGR ";"
        GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx ";"
#endif
        GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate text_sink_factory =
GST_STATIC_PAD_TEMPLATE ("text_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("private/x-dvbsub")
    );

static void gst_dvbsub_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dvbsub_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_dvbsub_overlay_finalize (GObject * object);

#define GST_DVBSUB_OVERLAY_GET_COND(ov)  (((GstDVBSubOverlay *)ov)->subtitle_cond)
#define GST_DVBSUB_OVERLAY_WAIT(ov)      (g_cond_wait (GST_DVBSUB_OVERLAY_GET_COND (ov), GST_OBJECT_GET_LOCK (ov)))
#define GST_DVBSUB_OVERLAY_BROADCAST(ov) (g_cond_broadcast (GST_DVBSUB_OVERLAY_GET_COND (ov)))

static GstStateChangeReturn gst_dvbsub_overlay_change_state (GstElement *
    element, GstStateChange transition);

GST_BOILERPLATE (GstDVBSubOverlay, gst_dvbsub_overlay, GstElement,
    GST_TYPE_ELEMENT);

static GstCaps *gst_dvbsub_overlay_getcaps (GstPad * pad);

static gboolean gst_dvbsub_overlay_setcaps_video (GstPad * pad, GstCaps * caps);
static gboolean gst_dvbsub_overlay_setcaps_text (GstPad * pad, GstCaps * caps);

static GstFlowReturn gst_dvbsub_overlay_chain_video (GstPad * pad,
    GstBuffer * buf);
static GstFlowReturn gst_dvbsub_overlay_chain_text (GstPad * pad,
    GstBuffer * buf);

static gboolean gst_dvbsub_overlay_event_video (GstPad * pad, GstEvent * event);
static gboolean gst_dvbsub_overlay_event_text (GstPad * pad, GstEvent * event);
static gboolean gst_dvbsub_overlay_event_src (GstPad * pad, GstEvent * event);

static void new_dvb_subtitles_cb (DvbSub * dvb_sub, DVBSubtitles * subs,
    gpointer user_data);

static GstFlowReturn gst_dvbsub_overlay_bufferalloc_video (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buffer);

static gboolean gst_dvbsub_overlay_query_src (GstPad * pad, GstQuery * query);

static void
gst_dvbsub_overlay_base_init (gpointer gclass)
{
  GstElementClass *element_class = (GstElementClass *) gclass;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&text_sink_factory));

  gst_element_class_set_details_simple (element_class, "DVB Subtitles Overlay", "Mixer/Video/Overlay/Subtitle", "Renders DVB subtitles", "Mart Raudsepp <mart.raudsepp@collabora.co.uk>");      // FIXME: Credit assrender and textoverlay?
}

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
          "Enable rendering of subtitles", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_change_state);
}

static void
_dvbsub_log_cb (GLogLevelFlags level, const gchar * fmt, va_list args,
    gpointer render)
{
  gchar *message = g_strdup_vprintf (fmt, args);

  if (level & G_LOG_LEVEL_ERROR)
    GST_CAT_ERROR_OBJECT (gst_dvbsub_overlay_lib_debug, render, "%s", message);
  else if (level & G_LOG_LEVEL_WARNING)
    GST_CAT_WARNING_OBJECT (gst_dvbsub_overlay_lib_debug, render, "%s",
        message);
  else if (level & G_LOG_LEVEL_INFO)
    GST_CAT_INFO_OBJECT (gst_dvbsub_overlay_lib_debug, render, "%s", message);
  else if (level & G_LOG_LEVEL_DEBUG)
    GST_CAT_DEBUG_OBJECT (gst_dvbsub_overlay_lib_debug, render, "%s", message);
  else
    GST_CAT_LOG_OBJECT (gst_dvbsub_overlay_lib_debug, render,
        "log level %d: %s", level, message);

  g_free (message);
}

static void
gst_dvbsub_overlay_init (GstDVBSubOverlay * render,
    GstDVBSubOverlayClass * gclass)
{
  GST_DEBUG_OBJECT (render, "init");

  render->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  render->video_sinkpad =
      gst_pad_new_from_static_template (&video_sink_factory, "video_sink");
  render->text_sinkpad =
      gst_pad_new_from_static_template (&text_sink_factory, "text_sink");

  gst_pad_set_setcaps_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_setcaps_video));
  gst_pad_set_setcaps_function (render->text_sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_setcaps_text));

  gst_pad_set_getcaps_function (render->srcpad,
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_getcaps));
  gst_pad_set_getcaps_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_getcaps));

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

  gst_pad_set_bufferalloc_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_bufferalloc_video));

  gst_pad_set_query_function (render->srcpad,
      GST_DEBUG_FUNCPTR (gst_dvbsub_overlay_query_src));

  gst_element_add_pad (GST_ELEMENT (render), render->srcpad);
  gst_element_add_pad (GST_ELEMENT (render), render->video_sinkpad);
  gst_element_add_pad (GST_ELEMENT (render), render->text_sinkpad);

  render->width = 0;
  render->height = 0;

  render->subtitle_mutex = g_mutex_new ();
  render->subtitle_cond = g_cond_new ();

  render->renderer_init_ok = FALSE;
  render->enable = TRUE;

  gst_segment_init (&render->video_segment, GST_FORMAT_TIME);
  gst_segment_init (&render->subtitle_segment, GST_FORMAT_TIME);

  render->dvbsub_mutex = g_mutex_new ();

  dvb_sub_set_global_log_cb (_dvbsub_log_cb, render);


  render->dvb_sub = dvb_sub_new ();
  if (!render->dvb_sub) {
    GST_WARNING_OBJECT (render, "cannot create dvbsub instance");
    g_assert_not_reached ();
  }

  {
    DvbSubCallbacks dvbsub_callbacks = { &new_dvb_subtitles_cb, };
    dvb_sub_set_callbacks (render->dvb_sub, &dvbsub_callbacks, render);
  }

  GST_DEBUG_OBJECT (render, "init complete");
}

static void
gst_dvbsub_overlay_finalize (GObject * object)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (object);

  if (overlay->subtitle_mutex)
    g_mutex_free (overlay->subtitle_mutex);

  if (overlay->subtitle_cond)
    g_cond_free (overlay->subtitle_cond);

  if (overlay->dvb_sub) {
    g_object_unref (overlay->dvb_sub);
  }

  if (overlay->dvbsub_mutex)
    g_mutex_free (overlay->dvbsub_mutex);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_dvbsub_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (object);

  switch (prop_id) {
    case PROP_ENABLE:
      overlay->enable = g_value_get_boolean (value);
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
      g_value_set_boolean (value, overlay->enable);
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
      render->subtitle_flushing = FALSE;
      gst_segment_init (&render->video_segment, GST_FORMAT_TIME);
      gst_segment_init (&render->subtitle_segment, GST_FORMAT_TIME);
      break;
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    default:
      break;

    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_mutex_lock (render->subtitle_mutex);
      render->subtitle_flushing = TRUE;
      if (render->subtitle_pending)
        gst_buffer_unref (render->subtitle_pending);
      render->subtitle_pending = NULL;
      g_cond_signal (render->subtitle_cond);
      g_mutex_unlock (render->subtitle_mutex);
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      render->renderer_init_ok = FALSE;
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }


  return ret;
}

static gboolean
gst_dvbsub_overlay_query_src (GstPad * pad, GstQuery * query)
{
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (gst_pad_get_parent (pad));
  gboolean ret;

  ret = gst_pad_peer_query (render->video_sinkpad, query);

  gst_object_unref (render);
  return ret;
}

static gboolean
gst_dvbsub_overlay_event_src (GstPad * pad, GstEvent * event)
{
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (gst_pad_get_parent (pad));
  gboolean ret = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstSeekFlags flags;

      GST_DEBUG_OBJECT (render, "seek received, driving from here");

      gst_event_parse_seek (event, NULL, NULL, &flags, NULL, NULL, NULL, NULL);

      /* Flush downstream, only for flushing seek */
      if (flags & GST_SEEK_FLAG_FLUSH)
        gst_pad_push_event (render->srcpad, gst_event_new_flush_start ());

      /* Mark subtitle as flushing, unblocks chains */
      g_mutex_lock (render->subtitle_mutex);
      if (render->subtitle_pending)
        gst_buffer_unref (render->subtitle_pending);
      render->subtitle_pending = NULL;
      render->subtitle_flushing = TRUE;
      g_cond_signal (render->subtitle_cond);
      g_mutex_unlock (render->subtitle_mutex);

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

  gst_object_unref (render);

  return ret;
}

static GstCaps *
gst_dvbsub_overlay_getcaps (GstPad * pad)
{
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstCaps *caps;

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

#ifdef DVBSUB_OVERLAY_RGB_SUPPORT

#define CREATE_RGB_BLIT_FUNCTION(name,bpp,R,G,B) \
static void \
blit_##name (GstDVBSubOverlay * overlay, DVBSubtitles * subs, GstBuffer * buffer) \
{ \
  guint counter; \
  DVBSubtitleRect *sub_region; \
  gint alpha, r, g, b, k; \
  guint32 color; \
  const guint8 *src; \
  guint8 *dst; \
  gint x, y, w, h; \
  gint width = overlay->width; \
  gint height = overlay->height; \
  gint dst_stride = GST_ROUND_UP_4 (width * bpp); \
  gint dst_skip; \
  gint src_stride, src_skip; \
  \
  for (counter = 0; counter < subs->num_rects; counter++) { \
    sub_region = subs->rects[counter]; \
    if (sub_region->y > height || sub_region->x > width) \
      continue; \
    \
    /* blend subtitles onto the video frame */ \
    src = sub_region->pict.data; \
    dst = buffer->data + sub_region->y * dst_stride + sub_region->x * bpp; \
    \
    w = MIN (sub_region->w, width - sub_region->x); \
    h = MIN (sub_region->h, height - sub_region->y); \
    src_stride = sub_region->pict.rowstride; \
    src_skip = sub_region->pict.rowstride - w; \
    dst_skip = dst_stride - w * bpp; \
    \
    for (y = 0; y < h; y++) { \
      for (x = 0; x < w; x++) { \
        color = sub_region->pict.palette[src[0]]; \
        alpha = 255 - (color & 0xff); /* FIXME: We get ARGB, not RGBA as assumed here */ \
        r = (color >> 24) & 0xff; \
        g = (color >> 16) & 0xff; \
        b = (color >> 8) & 0xff; \
        k = color * alpha / 255; /* FIXME */ \
        dst[R] = (k * r + (255 - k) * dst[R]) / 255; \
        dst[G] = (k * g + (255 - k) * dst[G]) / 255; \
        dst[B] = (k * b + (255 - k) * dst[B]) / 255; \
	src++; \
	dst += bpp; \
      } \
      src += src_skip; \
      dst += dst_skip; \
    } \
  } \
  GST_LOG_OBJECT (overlay, "amount of rendered DVBSubtitleRect: %u", counter); \
}

CREATE_RGB_BLIT_FUNCTION (rgb, 3, 0, 1, 2);
CREATE_RGB_BLIT_FUNCTION (bgr, 3, 2, 1, 0);
CREATE_RGB_BLIT_FUNCTION (xrgb, 4, 1, 2, 3);
CREATE_RGB_BLIT_FUNCTION (xbgr, 4, 3, 2, 1);
CREATE_RGB_BLIT_FUNCTION (rgbx, 4, 0, 1, 2);
CREATE_RGB_BLIT_FUNCTION (bgrx, 4, 2, 1, 0);

#undef CREATE_RGB_BLIT_FUNCTION

#endif

static inline gint
rgb_to_y (gint r, gint g, gint b)
{
  gint ret;

  ret = (gint) (((19595 * r) >> 16) + ((38470 * g) >> 16) + ((7471 * b) >> 16));
  ret = CLAMP (ret, 0, 255);
  return ret;
}

static inline gint
rgb_to_u (gint r, gint g, gint b)
{
  gint ret;

  ret =
      (gint) (-((11059 * r) >> 16) - ((21709 * g) >> 16) + ((32768 * b) >> 16) +
      128);
  ret = CLAMP (ret, 0, 255);
  return ret;
}

static inline gint
rgb_to_v (gint r, gint g, gint b)
{
  gint ret;

  ret =
      (gint) (((32768 * r) >> 16) - ((27439 * g) >> 16) - ((5329 * b) >> 16) +
      128);
  ret = CLAMP (ret, 0, 255);
  return ret;
}

/* FIXME: DVB-SUB actually provides us AYUV from CLUT, but libdvbsub used to convert it to ARGB */
static void
blit_i420 (GstDVBSubOverlay * overlay, DVBSubtitles * subs, GstBuffer * buffer)
{
  guint counter;
  DVBSubtitleRect *sub_region;
  gint alpha, r, g, b, k, k2;
  gint Y, U, V;
  guint32 color, color2;
  const guint8 *src;
  guint8 *dst_y, *dst_u, *dst_v;
  gint x, y, w, h;
  gint w2, h2;
  gint width = overlay->width;
  gint height = overlay->height;
  gint src_stride;
  gint y_offset, y_height, y_width, y_stride;
  gint u_offset, u_height, u_width, u_stride;
  gint v_offset, v_height, v_width, v_stride;

  y_offset =
      gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420, 0, width,
      height);
  u_offset =
      gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420, 1, width,
      height);
  v_offset =
      gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420, 2, width,
      height);

  y_height =
      gst_video_format_get_component_height (GST_VIDEO_FORMAT_I420, 0, height);
  u_height =
      gst_video_format_get_component_height (GST_VIDEO_FORMAT_I420, 1, height);
  v_height =
      gst_video_format_get_component_height (GST_VIDEO_FORMAT_I420, 2, height);

  y_width =
      gst_video_format_get_component_width (GST_VIDEO_FORMAT_I420, 0, width);
  u_width =
      gst_video_format_get_component_width (GST_VIDEO_FORMAT_I420, 1, width);
  v_width =
      gst_video_format_get_component_width (GST_VIDEO_FORMAT_I420, 2, width);

  y_stride = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420, 0, width);
  u_stride = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420, 1, width);
  v_stride = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420, 2, width);

  for (counter = 0; counter < subs->num_rects; counter++) {
    sub_region = subs->rects[counter];
    if (sub_region->y > height || sub_region->x > width)
      continue;

    /* blend subtitles onto the video frame */
    w = MIN (sub_region->w, width - sub_region->x);
    h = MIN (sub_region->h, height - sub_region->y);

    w2 = (w + 1) / 2;
    h2 = (h + 1) / 2;

    src_stride = sub_region->pict.rowstride;

    src = sub_region->pict.data;
    dst_y = buffer->data + y_offset + sub_region->y * y_stride + sub_region->x;
    dst_u =
        buffer->data + u_offset + ((sub_region->y + 1) / 2) * u_stride +
        (sub_region->x + 1) / 2;
    dst_v =
        buffer->data + v_offset + ((sub_region->y + 1) / 2) * v_stride +
        (sub_region->x + 1) / 2;

    for (y = 0; y < h - 1; y += 2) {
      for (x = 0; x < w - 1; x += 2) {
        /* FIXME: Completely wrong blending code */
        color = sub_region->pict.palette[src[0]];
        color2 = sub_region->pict.palette[src[1]];
        alpha = 255 - (color & 0xff);
        r = (color >> 24) & 0xff;
        g = (color >> 16) & 0xff;
        b = (color >> 8) & 0xff;

        Y = rgb_to_y (r, g, b);
        U = rgb_to_u (r, g, b);
        V = rgb_to_v (r, g, b);

        k = src[0] * alpha / 255;
        k2 = k;
        dst_y[0] = (k * Y + (255 - k) * dst_y[0]) / 255;

        k = src[1] * alpha / 255;
        k2 += k;
        dst_y[1] = (k * Y + (255 - k) * dst_y[1]) / 255;

        src += src_stride;
        dst_y += y_stride;

        k = src[0] * alpha / 255;
        k2 += k;
        dst_y[0] = (k * Y + (255 - k) * dst_y[0]) / 255;

        k = src[1] * alpha / 255;
        k2 += k;
        dst_y[1] = (k * Y + (255 - k) * dst_y[1]) / 255;

        k2 /= 4;
        dst_u[0] = (k2 * U + (255 - k2) * dst_u[0]) / 255;
        dst_v[0] = (k2 * V + (255 - k2) * dst_v[0]) / 255;
        dst_u++;
        dst_v++;

        src += -src_stride + 2;
        dst_y += -y_stride + 2;
      }

      if (x < w) {
        /* FIXME: Completely wrong blending code */
        color = sub_region->pict.palette[src[0]];
        color2 = sub_region->pict.palette[src[1]];
        alpha = 255 - (color & 0xff);
        r = (color >> 24) & 0xff;
        g = (color >> 16) & 0xff;
        b = (color >> 8) & 0xff;

        Y = rgb_to_y (r, g, b);
        U = rgb_to_u (r, g, b);
        V = rgb_to_v (r, g, b);

        k = src[0] * alpha / 255;
        k2 = k;
        dst_y[0] = (k * Y + (255 - k) * dst_y[0]) / 255;

        src += src_stride;
        dst_y += y_stride;

        k = src[0] * alpha / 255;
        k2 += k;
        dst_y[0] = (k * Y + (255 - k) * dst_y[0]) / 255;

        k2 /= 2;
        dst_u[0] = (k2 * U + (255 - k2) * dst_u[0]) / 255;
        dst_v[0] = (k2 * V + (255 - k2) * dst_v[0]) / 255;
        dst_u++;
        dst_v++;

        src += -src_stride + 1;
        dst_y += -y_stride + 1;
      }

      src += src_stride + (src_stride - w);
      dst_y += y_stride + (y_stride - w);
      dst_u += u_stride - w2;
      dst_v += v_stride - w2;
    }

    if (y < h) {
      for (x = 0; x < w - 1; x += 2) {
        /* FIXME: Completely wrong blending code */
        color = sub_region->pict.palette[src[0]];
        color2 = sub_region->pict.palette[src[1]];
        alpha = 255 - (color & 0xff);
        r = (color >> 24) & 0xff;
        g = (color >> 16) & 0xff;
        b = (color >> 8) & 0xff;

        Y = rgb_to_y (r, g, b);
        U = rgb_to_u (r, g, b);
        V = rgb_to_v (r, g, b);

        k = src[0] * alpha / 255;
        k2 = k;
        dst_y[0] = (k * Y + (255 - k) * dst_y[0]) / 255;

        k = src[1] * alpha / 255;
        k2 += k;
        dst_y[1] = (k * Y + (255 - k) * dst_y[1]) / 255;

        k2 /= 2;
        dst_u[0] = (k2 * U + (255 - k2) * dst_u[0]) / 255;
        dst_v[0] = (k2 * V + (255 - k2) * dst_v[0]) / 255;
        dst_u++;
        dst_v++;

        src += 2;
        dst_y += 2;
      }

      if (x < w) {
        /* FIXME: Completely wrong blending code */
        color = sub_region->pict.palette[src[0]];
        color2 = sub_region->pict.palette[src[1]];
        alpha = 255 - (color & 0xff);
        r = (color >> 24) & 0xff;
        g = (color >> 16) & 0xff;
        b = (color >> 8) & 0xff;

        Y = rgb_to_y (r, g, b);
        U = rgb_to_u (r, g, b);
        V = rgb_to_v (r, g, b);

        k = src[0] * alpha / 255;
        k2 = k;
        dst_y[0] = (k * Y + (255 - k) * dst_y[0]) / 255;

        dst_u[0] = (k2 * U + (255 - k2) * dst_u[0]) / 255;
        dst_v[0] = (k2 * V + (255 - k2) * dst_v[0]) / 255;
      }
    }
  }

  GST_LOG_OBJECT (overlay, "amount of rendered DVBSubtitleRect: %u", counter);
}

static gboolean
gst_dvbsub_overlay_setcaps_video (GstPad * pad, GstCaps * caps)
{
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (gst_pad_get_parent (pad));
  gboolean ret = FALSE;
  gint par_n = 1, par_d = 1;
  gdouble dar;

  render->width = 0;
  render->height = 0;

  if (!gst_video_format_parse_caps (caps, &render->format, &render->width,
          &render->height) ||
      !gst_video_parse_caps_framerate (caps, &render->fps_n, &render->fps_d)) {
    GST_ERROR_OBJECT (render, "Can't parse caps: %" GST_PTR_FORMAT, caps);
    ret = FALSE;
    goto out;
  }

  gst_video_parse_caps_pixel_aspect_ratio (caps, &par_n, &par_d);

  ret = gst_pad_set_caps (render->srcpad, caps);
  if (!ret)
    goto out;

  switch (render->format) {
#ifdef DVBSUB_OVERLAY_RGB_SUPPORT
    case GST_VIDEO_FORMAT_RGB:
      render->blit = blit_rgb;
      break;
    case GST_VIDEO_FORMAT_BGR:
      render->blit = blit_bgr;
      break;
    case GST_VIDEO_FORMAT_xRGB:
      render->blit = blit_xrgb;
      break;
    case GST_VIDEO_FORMAT_xBGR:
      render->blit = blit_xbgr;
      break;
    case GST_VIDEO_FORMAT_RGBx:
      render->blit = blit_rgbx;
      break;
    case GST_VIDEO_FORMAT_BGRx:
      render->blit = blit_bgrx;
      break;
#endif
    case GST_VIDEO_FORMAT_I420:
      render->blit = blit_i420;
      break;
    default:
      ret = FALSE;
      goto out;
  }

  /* FIXME: We need to handle aspect ratio ourselves */
#if 0
  g_mutex_lock (render->ass_mutex);
  ass_set_frame_size (render->ass_renderer, render->width, render->height);
#endif
  dar = (((gdouble) par_n) * ((gdouble) render->width))
      / (((gdouble) par_d) * ((gdouble) render->height));
#if 0
  ass_set_aspect_ratio (render->ass_renderer,
      dar, ((gdouble) render->width) / ((gdouble) render->height));

  g_mutex_unlock (render->ass_mutex);
#endif

  render->renderer_init_ok = TRUE;

  GST_DEBUG_OBJECT (render, "ass renderer setup complete");

out:
  gst_object_unref (render);

  return ret;
}

static gboolean
gst_dvbsub_overlay_setcaps_text (GstPad * pad, GstCaps * caps)
{
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (gst_pad_get_parent (pad));
  GstStructure *structure;
  const GValue *value;
#if 0                           // FIXME
  GstBuffer *priv;
  gchar *codec_private;
  guint codec_private_size;
#endif
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (render, "text pad linked with caps:  %" GST_PTR_FORMAT,
      caps);

  value = gst_structure_get_value (structure, "codec_data");

  /* FIXME: */
#if 0
  g_mutex_lock (render->ass_mutex);
  if (value != NULL) {
    priv = gst_value_get_buffer (value);
    g_return_val_if_fail (priv != NULL, FALSE);

    codec_private = (gchar *) GST_BUFFER_DATA (priv);
    codec_private_size = GST_BUFFER_SIZE (priv);

    if (!render->ass_track)
      render->ass_track = ass_new_track (render->ass_library);

    ass_process_codec_private (render->ass_track,
        codec_private, codec_private_size);

    GST_DEBUG_OBJECT (render, "ass track created");

    render->track_init_ok = TRUE;

    ret = TRUE;
  } else if (!render->ass_track) {
    render->ass_track = ass_new_track (render->ass_library);

    render->track_init_ok = TRUE;

    ret = TRUE;
  }
  g_mutex_unlock (render->ass_mutex);
#endif

  gst_object_unref (render);

  // FIXME
  ret = TRUE;

  return ret;
}

static void
gst_dvbsub_overlay_process_text (GstDVBSubOverlay * overlay, GstBuffer * buffer,
    guint64 pts)
{
  /* FIXME: Locking in this function? */
  guint8 *data = (guint8 *) GST_BUFFER_DATA (buffer);
  guint size = GST_BUFFER_SIZE (buffer);

  GST_DEBUG_OBJECT (overlay,
      "Processing subtitles with fake PTS=%" G_GUINT64_FORMAT
      " which is a running time of %" GST_TIME_FORMAT,
      pts, GST_TIME_ARGS (pts));
  GST_DEBUG_OBJECT (overlay, "Feeding %u bytes to libdvbsub", size);
  g_mutex_lock (overlay->dvbsub_mutex); /* FIXME: Use standard lock? */
  dvb_sub_feed_with_pts (overlay->dvb_sub, pts, data, size);
  g_mutex_unlock (overlay->dvbsub_mutex);
  gst_buffer_unref (buffer);
}

static void
new_dvb_subtitles_cb (DvbSub * dvb_sub, DVBSubtitles * subs, gpointer user_data)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (user_data);
  GST_INFO_OBJECT (overlay,
      "New DVB subtitles arrived with a page_time_out of %d and %d regions for PTS=%"
      G_GUINT64_FORMAT ", which should be at running time %" GST_TIME_FORMAT,
      subs->page_time_out, subs->num_rects, subs->pts,
      GST_TIME_ARGS (subs->pts));
  //GST_OBJECT_LOCK (overlay);
  overlay->subtitle_buffer = subs->num_rects;
  //GST_OBJECT_UNLOCK (overlay);
}

static GstFlowReturn
gst_dvbsub_overlay_bufferalloc_video (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buffer)
{
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (gst_pad_get_parent (pad));
  GstFlowReturn ret = GST_FLOW_WRONG_STATE;
  GstPad *allocpad;

  GST_OBJECT_LOCK (render);
  allocpad = render->srcpad ? gst_object_ref (render->srcpad) : NULL;
  GST_OBJECT_UNLOCK (render);

  if (allocpad) {
    ret = gst_pad_alloc_buffer (allocpad, offset, size, caps, buffer);
    gst_object_unref (allocpad);
  }

  gst_object_unref (render);

  return ret;
}

static GstFlowReturn
gst_dvbsub_overlay_chain_text (GstPad * pad, GstBuffer * buffer)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (GST_PAD_PARENT (pad));
  gint64 clip_start = 0, clip_stop = 0;
  gboolean in_seg = FALSE;
  GstClockTime sub_running_time;

  GST_INFO_OBJECT (overlay, "private/x-dvbsub buffer with size %u",
      GST_BUFFER_SIZE (buffer));

  GST_OBJECT_LOCK (overlay);

  if (overlay->subtitle_flushing) {
    GST_OBJECT_UNLOCK (overlay);
    GST_LOG_OBJECT (overlay, "text flushing");
    return GST_FLOW_WRONG_STATE;
  }

  if (overlay->subtitle_eos) {
    GST_OBJECT_UNLOCK (overlay);
    GST_LOG_OBJECT (overlay, "text EOS");
    return GST_FLOW_UNEXPECTED;
  }

  GST_LOG_OBJECT (overlay,
      "Video segment: %" GST_SEGMENT_FORMAT " --- Subtitle segment: %"
      GST_SEGMENT_FORMAT " --- BUFFER: ts=%" GST_TIME_FORMAT,
      &overlay->video_segment, &overlay->subtitle_segment,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));

  /* DVB subtitle packets are required to carry the PTS */
  if (G_UNLIKELY (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer))) {
    GST_OBJECT_UNLOCK (overlay);
    GST_WARNING_OBJECT (overlay,
        "Text buffer without valid timestamp, dropping");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  /* FIXME: Is the faking of a zero duration buffer correct here? Probably a better way to check segment inclusion then than to clip as a side effect */
  in_seg = gst_segment_clip (&overlay->subtitle_segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (buffer), GST_BUFFER_TIMESTAMP (buffer), &clip_start,
      &clip_stop);

  /* As the passed start and stop is equal, we shouldn't need to care about out of segment at all,
   * the subtitle data for the PTS is completely out of interest to us. A given display set must
   * carry the same PTS value. */
  /* FIXME: Consider with larger than 64kB display sets, which would be cut into multiple packets,
   * FIXME: does our waiting + render code work when there are more than one packets before
   * FIXME: rendering callback will get called? */

  if (!in_seg) {
    GST_DEBUG_OBJECT (overlay,
        "Subtitle timestamp (%" GST_TIME_FORMAT
        ") outside of the subtitle segment (%" GST_SEGMENT_FORMAT "), dropping",
        GST_BUFFER_TIMESTAMP (buffer), &overlay->subtitle_segment);
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
#if 0                           /* In case of DVB-SUB, we get notified of the shown subtitle going away by the next
                                 * page composition, so we can't just wait for the buffer to go away (unless we keep
                                 * non-rendered raw DVBSubtitleRects or DVBSubtitlePicture in there, which is
                                 * suboptimal */
  /* Wait for the previous buffer to go away */
  while (overlay->subtitle_buffer > 0) {
    GST_DEBUG ("Pad %s:%s has a buffer queued, waiting",
        GST_DEBUG_PAD_NAME (pad));
    GST_DVBSUB_OVERLAY_WAIT (overlay);
    GST_DEBUG ("Pad %s:%s resuming", GST_DEBUG_PAD_NAME (pad));
    if (overlay->subtitle_flushing) {
      GST_OBJECT_UNLOCK (overlay);
      /* FIXME: No need to unref buffer? */
      return GST_FLOW_WRONG_STATE;
    }
  }
#endif

  /* FIXME: How is this useful? */
  gst_segment_set_last_stop (&overlay->subtitle_segment, GST_FORMAT_TIME,
      clip_start);

  sub_running_time =
      gst_segment_to_running_time (&overlay->subtitle_segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (buffer));

  GST_DEBUG_OBJECT (overlay, "SUBTITLE real running time: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (sub_running_time));

  overlay->subtitle_buffer = 0; /*buffer FIXME: Need to do buffering elsewhere */

  /* That's a new text buffer we need to render */
  /*overlay->need_render = TRUE; *//* FIXME: Actually feed it to libdvbsub and set need_render on a callback */

  /* FIXME: We are abusing libdvbsub pts value for tracking our gstreamer running time instead of real PTS. Should be mostly fine though... */
  gst_dvbsub_overlay_process_text (overlay, buffer, sub_running_time);

  /* in case the video chain is waiting for a text buffer, wake it up */
  GST_DVBSUB_OVERLAY_BROADCAST (overlay);

  GST_OBJECT_UNLOCK (overlay);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_dvbsub_overlay_chain_video (GstPad * pad, GstBuffer * buffer)
{
  GstDVBSubOverlay *overlay = GST_DVBSUB_OVERLAY (GST_PAD_PARENT (pad));
  GstFlowReturn ret = GST_FLOW_OK;
  gint64 start, stop;

  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    goto missing_timestamp;

  start = GST_BUFFER_TIMESTAMP (buffer);

  GST_LOG_OBJECT (overlay,
      "Video segment: %" GST_SEGMENT_FORMAT " --- Subtitle last_stop: %"
      GST_TIME_FORMAT " --- BUFFER: ts=%" GST_TIME_FORMAT,
      &overlay->video_segment, &overlay->subtitle_segment.last_stop,
      GST_TIME_ARGS (start));

  /* ignore buffers that are outside of the current segment */
  if (!GST_BUFFER_DURATION_IS_VALID (buffer)) {
    stop = GST_CLOCK_TIME_NONE;
  } else {
    stop = start + GST_BUFFER_DURATION (buffer);
  }

  /* FIXME: Probably update last_stop somewhere */

  /* FIXME: Segment clipping code */

  if (overlay->subtitle_buffer > 0) {
    GST_DEBUG_OBJECT (overlay, "Should be rendering %u regions",
        overlay->subtitle_buffer);
  }

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
gst_dvbsub_overlay_event_video (GstPad * pad, GstEvent * event)
{
  gboolean ret = FALSE;
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (gst_pad_get_parent (pad));

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
            &render->video_segment);

        gst_segment_set_newsegment (&render->video_segment, update, rate,
            format, start, stop, time);

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

  gst_object_unref (render);

  return ret;
}

static gboolean
gst_dvbsub_overlay_event_text (GstPad * pad, GstEvent * event)
{
  //gint i; // FIXME
  gboolean ret = FALSE;
  GstDVBSubOverlay *render = GST_DVBSUB_OVERLAY (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pad, "received text event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate;
      gint64 start, stop, time;
      gboolean update;

      GST_DEBUG_OBJECT (render, "received new segment");

      /* FIXME: overlay */ render->subtitle_eos = FALSE;

      gst_event_parse_new_segment (event, &update, &rate, &format, &start,
          &stop, &time);

      if (format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (render, "SUBTITLE SEGMENT now: %" GST_SEGMENT_FORMAT,
            &render->subtitle_segment);

        gst_segment_set_newsegment (&render->subtitle_segment, update, rate,
            format, start, stop, time);

        GST_DEBUG_OBJECT (render,
            "SUBTITLE SEGMENT after: %" GST_SEGMENT_FORMAT,
            &render->subtitle_segment);
        ret = TRUE;
        gst_event_unref (event);
      } else {
        GST_ELEMENT_WARNING (render, STREAM, MUX, (NULL),
            ("received non-TIME newsegment event on subtitle input"));
        ret = FALSE;
        gst_event_unref (event);
      }
      /* FIXME: Not fully compared with textoverlay */
      GST_OBJECT_LOCK (render);
      GST_DVBSUB_OVERLAY_BROADCAST (render);
      GST_OBJECT_UNLOCK (render);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (&render->subtitle_segment, GST_FORMAT_TIME);
      render->subtitle_flushing = FALSE;
      /*FIXME: overlay */ render->subtitle_eos = FALSE;
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (render, "begin flushing");
#if 0                           // FIXME
      g_mutex_lock (render->ass_mutex);
      if (render->ass_track) {
        /* delete any events on the ass_track */
        for (i = 0; i < render->ass_track->n_events; i++) {
          GST_DEBUG_OBJECT (render, "deleted event with eid %i", i);
          ass_free_event (render->ass_track, i);
        }
        render->ass_track->n_events = 0;
        GST_DEBUG_OBJECT (render, "done flushing");
      }
      g_mutex_unlock (render->ass_mutex);
#endif
      g_mutex_lock (render->subtitle_mutex);
      if (render->subtitle_pending)
        gst_buffer_unref (render->subtitle_pending);
      render->subtitle_pending = NULL;
      render->subtitle_flushing = TRUE;
      GST_DVBSUB_OVERLAY_BROADCAST (render);
      g_mutex_unlock (render->subtitle_mutex);
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_EOS:
      GST_OBJECT_LOCK (render);
      /*FIXME: overlay */ render->subtitle_eos = TRUE;
      GST_INFO_OBJECT (render, "text EOS");
      /* wake up the video chain, it might be waiting for a text buffer or
       * a text segment update */
      GST_DVBSUB_OVERLAY_BROADCAST (render);
      GST_OBJECT_UNLOCK (render);
      gst_event_unref (event);
      ret = TRUE;
      break;
    default:
      ret = gst_pad_push_event (render->srcpad, event);
      break;
  }

  gst_object_unref (render);

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_dvbsub_overlay_debug, "dvbsuboverlay",
      0, "DVB subtitle overlay");
  GST_DEBUG_CATEGORY_INIT (gst_dvbsub_overlay_lib_debug, "dvbsub_library",
      0, "libdvbsub library");

  return gst_element_register (plugin, "dvbsuboverlay",
      GST_RANK_PRIMARY, GST_TYPE_DVBSUB_OVERLAY);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dvbsuboverlay",
    "DVB subtitle renderer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
