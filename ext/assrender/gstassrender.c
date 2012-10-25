/*
 * Copyright (c) 2008 Benjamin Schmitz <vortex@wolpzone.de>
 * Copyright (c) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-assrender
 *
 * Renders timestamped SSA/ASS subtitles on top of a video stream.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=/path/to/mkv ! matroskademux name=d ! queue ! mp3parse ! mad ! audioconvert ! autoaudiosink  d. ! queue ! ffdec_h264 ! videoconvert ! r.   d. ! queue ! "application/x-ass" ! assrender name=r ! videoconvert ! autovideosink
 * ]| This pipeline demuxes a Matroska file with h.264 video, MP3 audio and embedded ASS subtitles and renders the subtitles on top of the video.
 * </refsect2>
 */

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstassrender.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_ass_render_debug);
GST_DEBUG_CATEGORY_STATIC (gst_ass_render_lib_debug);
#define GST_CAT_DEFAULT gst_ass_render_debug

/* Filter signals and props */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_ENABLE,
  PROP_EMBEDDEDFONTS
};

#define FORMATS "{ RGB, BGR, xRGB, xBGR, RGBx, BGRx, I420 }"

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (FORMATS))
    );

static GstStaticPadTemplate video_sink_factory =
GST_STATIC_PAD_TEMPLATE ("video_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (FORMATS))
    );

static GstStaticPadTemplate text_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("text_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-ass; application/x-ssa")
    );

#define GST_ASS_RENDER_GET_LOCK(ass) (&GST_ASS_RENDER (ass)->lock)
#define GST_ASS_RENDER_GET_COND(ass) (&GST_ASS_RENDER (ass)->cond)
#define GST_ASS_RENDER_LOCK(ass)     (g_mutex_lock (GST_ASS_RENDER_GET_LOCK (ass)))
#define GST_ASS_RENDER_UNLOCK(ass)   (g_mutex_unlock (GST_ASS_RENDER_GET_LOCK (ass)))
#define GST_ASS_RENDER_WAIT(ass)     (g_cond_wait (GST_ASS_RENDER_GET_COND (ass), GST_ASS_RENDER_GET_LOCK (ass)))
#define GST_ASS_RENDER_SIGNAL(ass)   (g_cond_signal (GST_ASS_RENDER_GET_COND (ass)))
#define GST_ASS_RENDER_BROADCAST(ass)(g_cond_broadcast (GST_ASS_RENDER_GET_COND (ass)))

static void gst_ass_render_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ass_render_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_ass_render_finalize (GObject * object);

static GstStateChangeReturn gst_ass_render_change_state (GstElement * element,
    GstStateChange transition);

#define gst_ass_render_parent_class parent_class
G_DEFINE_TYPE (GstAssRender, gst_ass_render, GST_TYPE_ELEMENT);

static GstCaps *gst_ass_render_getcaps (GstPad * pad, GstCaps * filter);

static gboolean gst_ass_render_setcaps_video (GstPad * pad, GstCaps * caps);
static gboolean gst_ass_render_setcaps_text (GstPad * pad, GstCaps * caps);

static GstFlowReturn gst_ass_render_chain_video (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static GstFlowReturn gst_ass_render_chain_text (GstPad * pad,
    GstObject * parent, GstBuffer * buf);

static gboolean gst_ass_render_event_video (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_ass_render_event_text (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_ass_render_event_src (GstPad * pad, GstObject * parent,
    GstEvent * event);

static gboolean gst_ass_render_query_video (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_ass_render_query_src (GstPad * pad, GstObject * parent,
    GstQuery * query);

/* initialize the plugin's class */
static void
gst_ass_render_class_init (GstAssRenderClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_ass_render_set_property;
  gobject_class->get_property = gst_ass_render_get_property;
  gobject_class->finalize = gst_ass_render_finalize;

  g_object_class_install_property (gobject_class, PROP_ENABLE,
      g_param_spec_boolean ("enable", "Enable",
          "Enable rendering of subtitles", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_EMBEDDEDFONTS,
      g_param_spec_boolean ("embeddedfonts", "Embedded Fonts",
          "Extract and use fonts embedded in the stream", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_ass_render_change_state);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&video_sink_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&text_sink_factory));

  gst_element_class_set_static_metadata (gstelement_class, "ASS/SSA Render",
      "Mixer/Video/Overlay/Subtitle",
      "Renders ASS/SSA subtitles with libass",
      "Benjamin Schmitz <vortex@wolpzone.de>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

#if defined(LIBASS_VERSION) && LIBASS_VERSION >= 0x00907000
static void
_libass_message_cb (gint level, const gchar * fmt, va_list args,
    gpointer render)
{
  gchar *message = g_strdup_vprintf (fmt, args);

  if (level < 2)
    GST_CAT_ERROR_OBJECT (gst_ass_render_lib_debug, render, "%s", message);
  else if (level < 4)
    GST_CAT_WARNING_OBJECT (gst_ass_render_lib_debug, render, "%s", message);
  else if (level < 5)
    GST_CAT_INFO_OBJECT (gst_ass_render_lib_debug, render, "%s", message);
  else if (level < 6)
    GST_CAT_DEBUG_OBJECT (gst_ass_render_lib_debug, render, "%s", message);
  else
    GST_CAT_LOG_OBJECT (gst_ass_render_lib_debug, render, "%s", message);

  g_free (message);
}
#endif

static void
gst_ass_render_init (GstAssRender * render)
{
  GST_DEBUG_OBJECT (render, "init");

  render->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  render->video_sinkpad =
      gst_pad_new_from_static_template (&video_sink_factory, "video_sink");
  render->text_sinkpad =
      gst_pad_new_from_static_template (&text_sink_factory, "text_sink");

  gst_pad_set_chain_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_ass_render_chain_video));
  gst_pad_set_chain_function (render->text_sinkpad,
      GST_DEBUG_FUNCPTR (gst_ass_render_chain_text));

  gst_pad_set_event_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_ass_render_event_video));
  gst_pad_set_event_function (render->text_sinkpad,
      GST_DEBUG_FUNCPTR (gst_ass_render_event_text));
  gst_pad_set_event_function (render->srcpad,
      GST_DEBUG_FUNCPTR (gst_ass_render_event_src));

  gst_pad_set_query_function (render->srcpad,
      GST_DEBUG_FUNCPTR (gst_ass_render_query_src));
  gst_pad_set_query_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_ass_render_query_video));

  gst_element_add_pad (GST_ELEMENT (render), render->srcpad);
  gst_element_add_pad (GST_ELEMENT (render), render->video_sinkpad);
  gst_element_add_pad (GST_ELEMENT (render), render->text_sinkpad);

  gst_video_info_init (&render->info);

  g_mutex_init (&render->lock);
  g_cond_init (&render->cond);

  render->renderer_init_ok = FALSE;
  render->track_init_ok = FALSE;
  render->enable = TRUE;
  render->embeddedfonts = TRUE;

  gst_segment_init (&render->video_segment, GST_FORMAT_TIME);
  gst_segment_init (&render->subtitle_segment, GST_FORMAT_TIME);

  g_mutex_init (&render->ass_mutex);
  render->ass_library = ass_library_init ();
#if defined(LIBASS_VERSION) && LIBASS_VERSION >= 0x00907000
  ass_set_message_cb (render->ass_library, _libass_message_cb, render);
#endif
  ass_set_extract_fonts (render->ass_library, 1);

  render->ass_renderer = ass_renderer_init (render->ass_library);
  if (!render->ass_renderer) {
    GST_WARNING_OBJECT (render, "cannot create renderer instance");
    g_assert_not_reached ();
  }

  render->ass_track = NULL;

  GST_DEBUG_OBJECT (render, "init complete");
}

static void
gst_ass_render_finalize (GObject * object)
{
  GstAssRender *render = GST_ASS_RENDER (object);

  g_mutex_clear (&render->lock);
  g_cond_clear (&render->cond);

  if (render->ass_track) {
    ass_free_track (render->ass_track);
  }

  if (render->ass_renderer) {
    ass_renderer_done (render->ass_renderer);
  }

  if (render->ass_library) {
    ass_library_done (render->ass_library);
  }

  g_mutex_clear (&render->ass_mutex);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ass_render_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAssRender *render = GST_ASS_RENDER (object);

  GST_ASS_RENDER_LOCK (render);
  switch (prop_id) {
    case PROP_ENABLE:
      render->enable = g_value_get_boolean (value);
      break;
    case PROP_EMBEDDEDFONTS:
      render->embeddedfonts = g_value_get_boolean (value);
      g_mutex_lock (&render->ass_mutex);
      ass_set_extract_fonts (render->ass_library, render->embeddedfonts);
      g_mutex_unlock (&render->ass_mutex);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_ASS_RENDER_UNLOCK (render);
}

static void
gst_ass_render_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAssRender *render = GST_ASS_RENDER (object);

  GST_ASS_RENDER_LOCK (render);
  switch (prop_id) {
    case PROP_ENABLE:
      g_value_set_boolean (value, render->enable);
      break;
    case PROP_EMBEDDEDFONTS:
      g_value_set_boolean (value, render->embeddedfonts);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_ASS_RENDER_UNLOCK (render);
}

static GstStateChangeReturn
gst_ass_render_change_state (GstElement * element, GstStateChange transition)
{
  GstAssRender *render = GST_ASS_RENDER (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_ASS_RENDER_LOCK (render);
      render->subtitle_flushing = TRUE;
      render->video_flushing = TRUE;
      if (render->subtitle_pending)
        gst_buffer_unref (render->subtitle_pending);
      render->subtitle_pending = NULL;
      GST_ASS_RENDER_BROADCAST (render);
      GST_ASS_RENDER_UNLOCK (render);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_mutex_lock (&render->ass_mutex);
      if (render->ass_track)
        ass_free_track (render->ass_track);
      render->ass_track = NULL;
      render->track_init_ok = FALSE;
      render->renderer_init_ok = FALSE;
      g_mutex_unlock (&render->ass_mutex);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_ASS_RENDER_LOCK (render);
      render->subtitle_flushing = FALSE;
      render->video_flushing = FALSE;
      render->video_eos = FALSE;
      render->subtitle_eos = FALSE;
      gst_segment_init (&render->video_segment, GST_FORMAT_TIME);
      gst_segment_init (&render->subtitle_segment, GST_FORMAT_TIME);
      GST_ASS_RENDER_UNLOCK (render);
      break;
    default:
      break;
  }


  return ret;
}

static gboolean
gst_ass_render_query_src (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_ass_render_getcaps (pad, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static gboolean
gst_ass_render_event_src (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstAssRender *render = GST_ASS_RENDER (parent);
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (render, "received src event %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstSeekFlags flags;

      if (!render->track_init_ok) {
        GST_DEBUG_OBJECT (render, "seek received, pushing upstream");
        ret = gst_pad_push_event (render->video_sinkpad, event);
        return ret;
      }

      GST_DEBUG_OBJECT (render, "seek received, driving from here");

      gst_event_parse_seek (event, NULL, NULL, &flags, NULL, NULL, NULL, NULL);

      /* Flush downstream, only for flushing seek */
      if (flags & GST_SEEK_FLAG_FLUSH)
        gst_pad_push_event (render->srcpad, gst_event_new_flush_start ());

      /* Mark subtitle as flushing, unblocks chains */
      GST_ASS_RENDER_LOCK (render);
      render->subtitle_flushing = TRUE;
      render->video_flushing = TRUE;
      if (render->subtitle_pending)
        gst_buffer_unref (render->subtitle_pending);
      render->subtitle_pending = NULL;
      GST_ASS_RENDER_BROADCAST (render);
      GST_ASS_RENDER_UNLOCK (render);

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
      if (render->track_init_ok) {
        gst_event_ref (event);
        ret = gst_pad_push_event (render->video_sinkpad, event);
        gst_pad_push_event (render->text_sinkpad, event);
      } else {
        ret = gst_pad_push_event (render->video_sinkpad, event);
      }
      break;
  }

  return ret;
}

static GstCaps *
gst_ass_render_getcaps (GstPad * pad, GstCaps * filter)
{
  GstAssRender *render = GST_ASS_RENDER (gst_pad_get_parent (pad));
  GstPad *otherpad;
  GstCaps *caps;
  GstCaps *templ;

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
    gst_caps_unref (caps);
    gst_caps_unref (templ);
    /* this is what we can do */
    caps = temp;
  } else {
    /* no peer, our padtemplate is enough then */
    caps = templ;
  }

  gst_object_unref (render);

  return caps;
}

#define CREATE_RGB_BLIT_FUNCTION(name,bpp,R,G,B) \
static void \
blit_##name (GstAssRender * render, ASS_Image * ass_image, GstVideoFrame * frame) \
{ \
  guint counter = 0; \
  gint alpha, r, g, b, k; \
  const guint8 *src; \
  guint8 *dst, *data; \
  gint x, y, w, h; \
  gint width; \
  gint height; \
  gint dst_stride; \
  gint dst_skip; \
  gint src_skip; \
  \
  width = GST_VIDEO_FRAME_WIDTH (frame); \
  height = GST_VIDEO_FRAME_HEIGHT (frame); \
  dst_stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0); \
  data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0); \
  \
  while (ass_image) { \
    if (ass_image->dst_y > height || ass_image->dst_x > width) \
      goto next; \
    \
    /* blend subtitles onto the video frame */ \
    alpha = 255 - ((ass_image->color) & 0xff); \
    r = ((ass_image->color) >> 24) & 0xff; \
    g = ((ass_image->color) >> 16) & 0xff; \
    b = ((ass_image->color) >> 8) & 0xff; \
    src = ass_image->bitmap; \
    dst = data + ass_image->dst_y * dst_stride + ass_image->dst_x * bpp; \
    \
    w = MIN (ass_image->w, width - ass_image->dst_x); \
    h = MIN (ass_image->h, height - ass_image->dst_y); \
    src_skip = ass_image->stride - w; \
    dst_skip = dst_stride - w * bpp; \
    \
    for (y = 0; y < h; y++) { \
      for (x = 0; x < w; x++) { \
        k = src[0] * alpha / 255; \
        dst[R] = (k * r + (255 - k) * dst[R]) / 255; \
        dst[G] = (k * g + (255 - k) * dst[G]) / 255; \
        dst[B] = (k * b + (255 - k) * dst[B]) / 255; \
	src++; \
	dst += bpp; \
      } \
      src += src_skip; \
      dst += dst_skip; \
    } \
next: \
    counter++; \
    ass_image = ass_image->next; \
  } \
  GST_LOG_OBJECT (render, "amount of rendered ass_image: %u", counter); \
}

CREATE_RGB_BLIT_FUNCTION (rgb, 3, 0, 1, 2);
CREATE_RGB_BLIT_FUNCTION (bgr, 3, 2, 1, 0);
CREATE_RGB_BLIT_FUNCTION (xrgb, 4, 1, 2, 3);
CREATE_RGB_BLIT_FUNCTION (xbgr, 4, 3, 2, 1);
CREATE_RGB_BLIT_FUNCTION (rgbx, 4, 0, 1, 2);
CREATE_RGB_BLIT_FUNCTION (bgrx, 4, 2, 1, 0);

#undef CREATE_RGB_BLIT_FUNCTION

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

static void
blit_i420 (GstAssRender * render, ASS_Image * ass_image, GstVideoFrame * frame)
{
  guint counter = 0;
  gint alpha, r, g, b, k, k2;
  gint Y, U, V;
  const guint8 *src;
  guint8 *dst_y, *dst_u, *dst_v;
  gint x, y, w, h;
/* FIXME ignoring source image stride might be wrong here */
#if 0
  gint w2;
  gint src_stride;
#endif
  gint width, height;
  guint8 *y_data, *u_data, *v_data;
  gint y_stride, u_stride, v_stride;

  width = GST_VIDEO_FRAME_WIDTH (frame);
  height = GST_VIDEO_FRAME_HEIGHT (frame);

  y_data = GST_VIDEO_FRAME_COMP_DATA (frame, 0);
  u_data = GST_VIDEO_FRAME_COMP_DATA (frame, 1);
  v_data = GST_VIDEO_FRAME_COMP_DATA (frame, 2);

  y_stride = GST_VIDEO_FRAME_COMP_STRIDE (frame, 0);
  u_stride = GST_VIDEO_FRAME_COMP_STRIDE (frame, 1);
  v_stride = GST_VIDEO_FRAME_COMP_STRIDE (frame, 2);

  while (ass_image) {
    if (ass_image->dst_y > height || ass_image->dst_x > width)
      goto next;

    /* blend subtitles onto the video frame */
    alpha = 255 - ((ass_image->color) & 0xff);
    r = ((ass_image->color) >> 24) & 0xff;
    g = ((ass_image->color) >> 16) & 0xff;
    b = ((ass_image->color) >> 8) & 0xff;

    Y = rgb_to_y (r, g, b);
    U = rgb_to_u (r, g, b);
    V = rgb_to_v (r, g, b);

    w = MIN (ass_image->w, width - ass_image->dst_x);
    h = MIN (ass_image->h, height - ass_image->dst_y);

#if 0
    w2 = (w + 1) / 2;

    src_stride = ass_image->stride;
#endif

    src = ass_image->bitmap;
#if 0
    dst_y = y_data + ass_image->dst_y * y_stride + ass_image->dst_x;
    dst_u = u_data + (ass_image->dst_y / 2) * u_stride + ass_image->dst_x / 2;
    dst_v = v_data + (ass_image->dst_y / 2) * v_stride + ass_image->dst_x / 2;
#endif

    for (y = 0; y < h; y++) {
      dst_y = y_data + (ass_image->dst_y + y) * y_stride + ass_image->dst_x;
      for (x = 0; x < w; x++) {
        k = src[y * ass_image->w + x] * alpha / 255;
        dst_y[x] = (k * Y + (255 - k) * dst_y[x]) / 255;
      }
    }

    y = 0;
    if (ass_image->dst_y & 1) {
      dst_u = u_data + (ass_image->dst_y / 2) * u_stride + ass_image->dst_x / 2;
      dst_v = v_data + (ass_image->dst_y / 2) * v_stride + ass_image->dst_x / 2;
      x = 0;
      if (ass_image->dst_x & 1) {
        k2 = src[y * ass_image->w + x] * alpha / 255;
        k2 = (k2 + 2) >> 2;
        dst_u[0] = (k2 * U + (255 - k2) * dst_u[0]) / 255;
        dst_v[0] = (k2 * V + (255 - k2) * dst_v[0]) / 255;
        x++;
        dst_u++;
        dst_v++;
      }
      for (; x < w - 1; x += 2) {
        k2 = src[y * ass_image->w + x] * alpha / 255;
        k2 += src[y * ass_image->w + x + 1] * alpha / 255;
        k2 = (k2 + 2) >> 2;
        dst_u[0] = (k2 * U + (255 - k2) * dst_u[0]) / 255;
        dst_v[0] = (k2 * V + (255 - k2) * dst_v[0]) / 255;
        dst_u++;
        dst_v++;
      }
      if (x < w) {
        k2 = src[y * ass_image->w + x] * alpha / 255;
        k2 = (k2 + 2) >> 2;
        dst_u[0] = (k2 * U + (255 - k2) * dst_u[0]) / 255;
        dst_v[0] = (k2 * V + (255 - k2) * dst_v[0]) / 255;
      }
    }

    for (; y < h - 1; y += 2) {
      dst_u = u_data + ((ass_image->dst_y + y) / 2) * u_stride +
          ass_image->dst_x / 2;
      dst_v = v_data + ((ass_image->dst_y + y) / 2) * v_stride +
          ass_image->dst_x / 2;
      x = 0;
      if (ass_image->dst_x & 1) {
        k2 = src[y * ass_image->w + x] * alpha / 255;
        k2 += src[(y + 1) * ass_image->w + x] * alpha / 255;
        k2 = (k2 + 2) >> 2;
        dst_u[0] = (k2 * U + (255 - k2) * dst_u[0]) / 255;
        dst_v[0] = (k2 * V + (255 - k2) * dst_v[0]) / 255;
        x++;
        dst_u++;
        dst_v++;
      }
      for (; x < w - 1; x += 2) {
        k2 = src[y * ass_image->w + x] * alpha / 255;
        k2 += src[y * ass_image->w + x + 1] * alpha / 255;
        k2 += src[(y + 1) * ass_image->w + x] * alpha / 255;
        k2 += src[(y + 1) * ass_image->w + x + 1] * alpha / 255;
        k2 = (k2 + 2) >> 2;
        dst_u[0] = (k2 * U + (255 - k2) * dst_u[0]) / 255;
        dst_v[0] = (k2 * V + (255 - k2) * dst_v[0]) / 255;
        dst_u++;
        dst_v++;
      }
      if (x < w) {
        k2 = src[y * ass_image->w + x] * alpha / 255;
        k2 += src[(y + 1) * ass_image->w + x] * alpha / 255;
        k2 = (k2 + 2) >> 2;
        dst_u[0] = (k2 * U + (255 - k2) * dst_u[0]) / 255;
        dst_v[0] = (k2 * V + (255 - k2) * dst_v[0]) / 255;
      }
    }

    if (y < h) {
      dst_u = u_data + (ass_image->dst_y / 2) * u_stride + ass_image->dst_x / 2;
      dst_v = v_data + (ass_image->dst_y / 2) * v_stride + ass_image->dst_x / 2;
      x = 0;
      if (ass_image->dst_x & 1) {
        k2 = src[y * ass_image->w + x] * alpha / 255;
        k2 = (k2 + 2) >> 2;
        dst_u[0] = (k2 * U + (255 - k2) * dst_u[0]) / 255;
        dst_v[0] = (k2 * V + (255 - k2) * dst_v[0]) / 255;
        x++;
        dst_u++;
        dst_v++;
      }
      for (; x < w - 1; x += 2) {
        k2 = src[y * ass_image->w + x] * alpha / 255;
        k2 += src[y * ass_image->w + x + 1] * alpha / 255;
        k2 = (k2 + 2) >> 2;
        dst_u[0] = (k2 * U + (255 - k2) * dst_u[0]) / 255;
        dst_v[0] = (k2 * V + (255 - k2) * dst_v[0]) / 255;
        dst_u++;
        dst_v++;
      }
      if (x < w) {
        k2 = src[y * ass_image->w + x] * alpha / 255;
        k2 = (k2 + 2) >> 2;
        dst_u[0] = (k2 * U + (255 - k2) * dst_u[0]) / 255;
        dst_v[0] = (k2 * V + (255 - k2) * dst_v[0]) / 255;
      }
    }



  next:
    counter++;
    ass_image = ass_image->next;
  }

  GST_LOG_OBJECT (render, "amount of rendered ass_image: %u", counter);
}

static gboolean
gst_ass_render_setcaps_video (GstPad * pad, GstCaps * caps)
{
  GstAssRender *render = GST_ASS_RENDER (gst_pad_get_parent (pad));
  gboolean ret = FALSE;
  gint par_n = 1, par_d = 1;
  gdouble dar;
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps))
    goto invalid_caps;

  render->info = info;

  ret = gst_pad_set_caps (render->srcpad, caps);
  if (!ret)
    goto out;

  switch (GST_VIDEO_INFO_FORMAT (&info)) {
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
    case GST_VIDEO_FORMAT_I420:
      render->blit = blit_i420;
      break;
    default:
      ret = FALSE;
      goto out;
  }

  g_mutex_lock (&render->ass_mutex);
  ass_set_frame_size (render->ass_renderer, info.width, info.height);

  dar = (((gdouble) par_n) * ((gdouble) info.width))
      / (((gdouble) par_d) * ((gdouble) info.height));
#if !defined(LIBASS_VERSION) || LIBASS_VERSION < 0x00907000
  ass_set_aspect_ratio (render->ass_renderer, dar);
#else
  ass_set_aspect_ratio (render->ass_renderer,
      dar, ((gdouble) info.width) / ((gdouble) info.height));
#endif
  ass_set_font_scale (render->ass_renderer, 1.0);
  ass_set_hinting (render->ass_renderer, ASS_HINTING_LIGHT);

#if !defined(LIBASS_VERSION) || LIBASS_VERSION < 0x00907000
  ass_set_fonts (render->ass_renderer, "Arial", "sans-serif");
  ass_set_fonts (render->ass_renderer, NULL, "Sans");
#else
  ass_set_fonts (render->ass_renderer, "Arial", "sans-serif", 1, NULL, 1);
  ass_set_fonts (render->ass_renderer, NULL, "Sans", 1, NULL, 1);
#endif
  ass_set_margins (render->ass_renderer, 0, 0, 0, 0);
  ass_set_use_margins (render->ass_renderer, 0);
  g_mutex_unlock (&render->ass_mutex);

  render->renderer_init_ok = TRUE;

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

static gboolean
gst_ass_render_setcaps_text (GstPad * pad, GstCaps * caps)
{
  GstAssRender *render = GST_ASS_RENDER (gst_pad_get_parent (pad));
  GstStructure *structure;
  const GValue *value;
  GstBuffer *priv;
  GstMapInfo map;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (render, "text pad linked with caps:  %" GST_PTR_FORMAT,
      caps);

  value = gst_structure_get_value (structure, "codec_data");

  g_mutex_lock (&render->ass_mutex);
  if (value != NULL) {
    priv = gst_value_get_buffer (value);
    g_return_val_if_fail (priv != NULL, FALSE);

    gst_buffer_map (priv, &map, GST_MAP_READ);

    if (!render->ass_track)
      render->ass_track = ass_new_track (render->ass_library);

    ass_process_codec_private (render->ass_track, (char *) map.data, map.size);

    gst_buffer_unmap (priv, &map);

    GST_DEBUG_OBJECT (render, "ass track created");

    render->track_init_ok = TRUE;

    ret = TRUE;
  } else if (!render->ass_track) {
    render->ass_track = ass_new_track (render->ass_library);

    render->track_init_ok = TRUE;

    ret = TRUE;
  }
  g_mutex_unlock (&render->ass_mutex);

  gst_object_unref (render);

  return ret;
}


static void
gst_ass_render_process_text (GstAssRender * render, GstBuffer * buffer,
    GstClockTime running_time, GstClockTime duration)
{
  GstMapInfo map;
  gdouble pts_start, pts_end;

  pts_start = running_time;
  pts_start /= GST_MSECOND;
  pts_end = duration;
  pts_end /= GST_MSECOND;

  GST_DEBUG_OBJECT (render,
      "Processing subtitles with running time %" GST_TIME_FORMAT
      " and duration %" GST_TIME_FORMAT, GST_TIME_ARGS (running_time),
      GST_TIME_ARGS (duration));

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  g_mutex_lock (&render->ass_mutex);
  ass_process_chunk (render->ass_track, (gchar *) map.data, map.size,
      pts_start, pts_end);
  g_mutex_unlock (&render->ass_mutex);

  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);
}

static GstFlowReturn
gst_ass_render_chain_video (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstAssRender *render = GST_ASS_RENDER (parent);
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean in_seg = FALSE;
  guint64 start, stop, clip_start = 0, clip_stop = 0;
  ASS_Image *ass_image;

  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    goto missing_timestamp;

  /* ignore buffers that are outside of the current segment */
  start = GST_BUFFER_TIMESTAMP (buffer);

  if (!GST_BUFFER_DURATION_IS_VALID (buffer)) {
    stop = GST_CLOCK_TIME_NONE;
  } else {
    stop = start + GST_BUFFER_DURATION (buffer);
  }

  /* segment_clip() will adjust start unconditionally to segment_start if
   * no stop time is provided, so handle this ourselves */
  if (stop == GST_CLOCK_TIME_NONE && start < render->video_segment.start)
    goto out_of_segment;

  in_seg =
      gst_segment_clip (&render->video_segment, GST_FORMAT_TIME, start, stop,
      &clip_start, &clip_stop);

  if (!in_seg)
    goto out_of_segment;

  /* if the buffer is only partially in the segment, fix up stamps */
  if (clip_start != start || (stop != -1 && clip_stop != stop)) {
    GST_DEBUG_OBJECT (render, "clipping buffer timestamp/duration to segment");
    buffer = gst_buffer_make_writable (buffer);
    GST_BUFFER_TIMESTAMP (buffer) = clip_start;
    if (stop != -1)
      GST_BUFFER_DURATION (buffer) = clip_stop - clip_start;
  }

  /* now, after we've done the clipping, fix up end time if there's no
   * duration (we only use those estimated values internally though, we
   * don't want to set bogus values on the buffer itself) */
  if (stop == -1) {
    if (render->info.fps_n && render->info.fps_d) {
      GST_DEBUG_OBJECT (render, "estimating duration based on framerate");
      stop =
          start + gst_util_uint64_scale_int (GST_SECOND, render->info.fps_d,
          render->info.fps_n);
    } else {
      GST_WARNING_OBJECT (render, "no duration, assuming minimal duration");
      stop = start + 1;         /* we need to assume some interval */
    }
  }

wait_for_text_buf:

  GST_ASS_RENDER_LOCK (render);

  if (render->video_flushing)
    goto flushing;

  if (render->video_eos)
    goto have_eos;

  if (render->renderer_init_ok && render->track_init_ok && render->enable) {
    /* Text pad linked, check if we have a text buffer queued */
    if (render->subtitle_pending) {
      gboolean pop_text = FALSE, valid_text_time = TRUE;
      GstClockTime text_start = GST_CLOCK_TIME_NONE;
      GstClockTime text_end = GST_CLOCK_TIME_NONE;
      GstClockTime text_running_time = GST_CLOCK_TIME_NONE;
      GstClockTime text_running_time_end = GST_CLOCK_TIME_NONE;
      GstClockTime vid_running_time, vid_running_time_end;

      /* if the text buffer isn't stamped right, pop it off the
       * queue and display it for the current video frame only */
      if (!GST_BUFFER_TIMESTAMP_IS_VALID (render->subtitle_pending) ||
          !GST_BUFFER_DURATION_IS_VALID (render->subtitle_pending)) {
        GST_WARNING_OBJECT (render,
            "Got text buffer with invalid timestamp or duration");
        valid_text_time = FALSE;
      } else {
        text_start = GST_BUFFER_TIMESTAMP (render->subtitle_pending);
        text_end = text_start + GST_BUFFER_DURATION (render->subtitle_pending);
      }

      vid_running_time =
          gst_segment_to_running_time (&render->video_segment, GST_FORMAT_TIME,
          start);
      vid_running_time_end =
          gst_segment_to_running_time (&render->video_segment, GST_FORMAT_TIME,
          stop);

      /* If timestamp and duration are valid */
      if (valid_text_time) {
        text_running_time =
            gst_segment_to_running_time (&render->video_segment,
            GST_FORMAT_TIME, text_start);
        text_running_time_end =
            gst_segment_to_running_time (&render->video_segment,
            GST_FORMAT_TIME, text_end);
      }

      GST_LOG_OBJECT (render, "T: %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
          GST_TIME_ARGS (text_running_time),
          GST_TIME_ARGS (text_running_time_end));
      GST_LOG_OBJECT (render, "V: %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
          GST_TIME_ARGS (vid_running_time),
          GST_TIME_ARGS (vid_running_time_end));

      /* Text too old or in the future */
      if (valid_text_time && text_running_time_end <= vid_running_time) {
        /* text buffer too old, get rid of it and do nothing  */
        GST_DEBUG_OBJECT (render, "text buffer too old, popping");
        pop_text = FALSE;
        gst_buffer_unref (render->subtitle_pending);
        render->subtitle_pending = NULL;
        GST_ASS_RENDER_BROADCAST (render);
        GST_ASS_RENDER_UNLOCK (render);
        goto wait_for_text_buf;
      } else if (valid_text_time && vid_running_time_end <= text_running_time) {
        gdouble timestamp;

        GST_ASS_RENDER_UNLOCK (render);

        timestamp = vid_running_time / GST_MSECOND;

        g_mutex_lock (&render->ass_mutex);

        /* not sure what the last parameter to this call is for (detect_change) */
        ass_image = ass_render_frame (render->ass_renderer, render->ass_track,
            timestamp, NULL);

        g_mutex_unlock (&render->ass_mutex);

        if (ass_image != NULL) {
          GstVideoFrame frame;

          buffer = gst_buffer_make_writable (buffer);
          gst_video_frame_map (&frame, &render->info, buffer, GST_MAP_WRITE);
          render->blit (render, ass_image, &frame);
          gst_video_frame_unmap (&frame);
        } else {
          GST_LOG_OBJECT (render, "nothing to render right now");
        }

        /* Push the video frame */
        ret = gst_pad_push (render->srcpad, buffer);
      } else {
        gdouble timestamp;

        gst_ass_render_process_text (render, render->subtitle_pending,
            text_running_time, text_running_time_end - text_running_time);
        render->subtitle_pending = NULL;
        GST_ASS_RENDER_BROADCAST (render);
        GST_ASS_RENDER_UNLOCK (render);

        /* libass needs timestamps in ms */
        timestamp = vid_running_time / GST_MSECOND;

        g_mutex_lock (&render->ass_mutex);
        /* not sure what the last parameter to this call is for (detect_change) */
        ass_image = ass_render_frame (render->ass_renderer, render->ass_track,
            timestamp, NULL);
        g_mutex_unlock (&render->ass_mutex);

        if (ass_image != NULL) {
          GstVideoFrame frame;

          buffer = gst_buffer_make_writable (buffer);
          gst_video_frame_map (&frame, &render->info, buffer, GST_MAP_WRITE);
          render->blit (render, ass_image, &frame);
          gst_video_frame_unmap (&frame);
        } else {
          GST_DEBUG_OBJECT (render, "nothing to render right now");
        }

        ret = gst_pad_push (render->srcpad, buffer);

        if (valid_text_time && text_running_time_end <= vid_running_time_end) {
          GST_LOG_OBJECT (render, "text buffer not needed any longer");
          pop_text = TRUE;
        }
      }
      if (pop_text) {
        GST_ASS_RENDER_LOCK (render);
        if (render->subtitle_pending)
          gst_buffer_unref (render->subtitle_pending);
        render->subtitle_pending = NULL;
        GST_ASS_RENDER_BROADCAST (render);
        GST_ASS_RENDER_UNLOCK (render);
      }
    } else {
      gboolean wait_for_text_buf = TRUE;

      if (render->subtitle_eos)
        wait_for_text_buf = FALSE;

      /* Text pad linked, but no text buffer available - what now? */
      if (render->subtitle_segment.format == GST_FORMAT_TIME) {
        GstClockTime text_start_running_time, text_last_stop_running_time;
        GstClockTime vid_running_time;

        vid_running_time =
            gst_segment_to_running_time (&render->video_segment,
            GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (buffer));
        text_start_running_time =
            gst_segment_to_running_time (&render->subtitle_segment,
            GST_FORMAT_TIME, render->subtitle_segment.start);
        text_last_stop_running_time =
            gst_segment_to_running_time (&render->subtitle_segment,
            GST_FORMAT_TIME, render->subtitle_segment.position);

        if ((GST_CLOCK_TIME_IS_VALID (text_start_running_time) &&
                vid_running_time < text_start_running_time) ||
            (GST_CLOCK_TIME_IS_VALID (text_last_stop_running_time) &&
                vid_running_time < text_last_stop_running_time)) {
          wait_for_text_buf = FALSE;
        }
      }

      if (wait_for_text_buf) {
        GST_DEBUG_OBJECT (render, "no text buffer, need to wait for one");
        GST_ASS_RENDER_WAIT (render);
        GST_DEBUG_OBJECT (render, "resuming");
        GST_ASS_RENDER_UNLOCK (render);
        goto wait_for_text_buf;
      } else {
        GST_ASS_RENDER_UNLOCK (render);
        GST_LOG_OBJECT (render, "no need to wait for a text buffer");
        ret = gst_pad_push (render->srcpad, buffer);
      }
    }
  } else {
    GST_LOG_OBJECT (render, "rendering disabled, doing buffer passthrough");

    GST_ASS_RENDER_UNLOCK (render);
    ret = gst_pad_push (render->srcpad, buffer);
    return ret;
  }

  GST_DEBUG_OBJECT (render, "leaving chain for buffer %p ret=%d", buffer, ret);

  /* Update last_stop */
  render->video_segment.position = clip_start;

  return ret;

missing_timestamp:
  {
    GST_WARNING_OBJECT (render, "buffer without timestamp, discarding");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
flushing:
  {
    GST_ASS_RENDER_UNLOCK (render);
    GST_DEBUG_OBJECT (render, "flushing, discarding buffer");
    gst_buffer_unref (buffer);
    return GST_FLOW_FLUSHING;
  }
have_eos:
  {
    GST_ASS_RENDER_UNLOCK (render);
    GST_DEBUG_OBJECT (render, "eos, discarding buffer");
    gst_buffer_unref (buffer);
    return GST_FLOW_EOS;
  }
out_of_segment:
  {
    GST_DEBUG_OBJECT (render, "buffer out of segment, discarding");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_ass_render_chain_text (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstAssRender *render = GST_ASS_RENDER (parent);
  gboolean in_seg = FALSE;
  guint64 clip_start = 0, clip_stop = 0;

  GST_DEBUG_OBJECT (render, "entering chain for buffer %p", buffer);

  GST_ASS_RENDER_LOCK (render);

  if (render->subtitle_flushing) {
    GST_ASS_RENDER_UNLOCK (render);
    ret = GST_FLOW_FLUSHING;
    GST_LOG_OBJECT (render, "text flushing");
    goto beach;
  }

  if (render->subtitle_eos) {
    GST_ASS_RENDER_UNLOCK (render);
    ret = GST_FLOW_EOS;
    GST_LOG_OBJECT (render, "text EOS");
    goto beach;
  }

  if (G_LIKELY (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))) {
    GstClockTime stop;

    if (G_LIKELY (GST_BUFFER_DURATION_IS_VALID (buffer)))
      stop = GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
    else
      stop = GST_CLOCK_TIME_NONE;

    in_seg = gst_segment_clip (&render->subtitle_segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buffer), stop, &clip_start, &clip_stop);
  } else {
    in_seg = TRUE;
  }

  if (in_seg) {
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
      GST_BUFFER_TIMESTAMP (buffer) = clip_start;
    else if (GST_BUFFER_DURATION_IS_VALID (buffer))
      GST_BUFFER_DURATION (buffer) = clip_stop - clip_start;

    if (render->subtitle_pending
        && (!GST_BUFFER_TIMESTAMP_IS_VALID (render->subtitle_pending)
            || !GST_BUFFER_DURATION_IS_VALID (render->subtitle_pending))) {
      gst_buffer_unref (render->subtitle_pending);
      render->subtitle_pending = NULL;
      GST_ASS_RENDER_BROADCAST (render);
    } else {
      /* Wait for the previous buffer to go away */
      while (render->subtitle_pending != NULL) {
        GST_DEBUG ("Pad %s:%s has a buffer queued, waiting",
            GST_DEBUG_PAD_NAME (pad));
        GST_ASS_RENDER_WAIT (render);
        GST_DEBUG ("Pad %s:%s resuming", GST_DEBUG_PAD_NAME (pad));
        if (render->subtitle_flushing) {
          GST_ASS_RENDER_UNLOCK (render);
          ret = GST_FLOW_FLUSHING;
          goto beach;
        }
      }
    }

    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
      render->subtitle_segment.position = clip_start;

    GST_DEBUG_OBJECT (render,
        "New buffer arrived for timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));
    render->subtitle_pending = gst_buffer_ref (buffer);

    /* in case the video chain is waiting for a text buffer, wake it up */
    GST_ASS_RENDER_BROADCAST (render);
  }

  GST_ASS_RENDER_UNLOCK (render);

beach:
  GST_DEBUG_OBJECT (render, "leaving chain for buffer %p", buffer);

  gst_buffer_unref (buffer);
  return ret;
}

static void
gst_ass_render_handle_tags (GstAssRender * render, GstTagList * taglist)
{
  static const gchar *mimetypes[] = {
    "application/x-font-ttf",
    "application/x-font-otf",
    "application/x-truetype-font"
  };
  static const gchar *extensions[] = {
    ".otf",
    ".ttf"
  };
  guint tag_size;

  if (!taglist)
    return;

  tag_size = gst_tag_list_get_tag_size (taglist, GST_TAG_ATTACHMENT);
  if (tag_size > 0 && render->embeddedfonts) {
    GstSample *sample;
    GstBuffer *buf;
    const GstStructure *structure;
    gboolean valid_mimetype, valid_extension;
    guint j;
    const gchar *filename;
    guint index;
    GstMapInfo map;

    GST_DEBUG_OBJECT (render, "TAG event has attachments");

    for (index = 0; index < tag_size; index++) {
      if (!gst_tag_list_get_sample_index (taglist, GST_TAG_ATTACHMENT, index,
              &sample))
        continue;
      buf = gst_sample_get_buffer (sample);
      structure = gst_sample_get_info (sample);
      if (!buf || !structure)
        continue;

      valid_mimetype = FALSE;
      valid_extension = FALSE;

      for (j = 0; j < G_N_ELEMENTS (mimetypes); j++) {
        if (gst_structure_has_name (structure, mimetypes[j])) {
          valid_mimetype = TRUE;
          break;
        }
      }
      filename = gst_structure_get_string (structure, "filename");
      if (!filename)
        continue;

      if (!valid_mimetype) {
        guint len = strlen (filename);
        const gchar *extension = filename + len - 4;
        for (j = 0; j < G_N_ELEMENTS (extensions); j++) {
          if (g_ascii_strcasecmp (extension, extensions[j]) == 0) {
            valid_extension = TRUE;
            break;
          }
        }
      }

      if (valid_mimetype || valid_extension) {
        g_mutex_lock (&render->ass_mutex);
        gst_buffer_map (buf, &map, GST_MAP_READ);
        ass_add_font (render->ass_library, (gchar *) filename,
            (gchar *) map.data, map.size);
        gst_buffer_unmap (buf, &map);
        GST_DEBUG_OBJECT (render, "registered new font %s", filename);
        g_mutex_unlock (&render->ass_mutex);
      }
    }
  }
}

static gboolean
gst_ass_render_event_video (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret = FALSE;
  GstAssRender *render = GST_ASS_RENDER (parent);

  GST_DEBUG_OBJECT (pad, "received video event %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_ass_render_setcaps_video (pad, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment;

      GST_DEBUG_OBJECT (render, "received new segment");

      gst_event_copy_segment (event, &segment);

      if (segment.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (render, "VIDEO SEGMENT now: %" GST_SEGMENT_FORMAT,
            &render->video_segment);

        render->video_segment = segment;

        GST_DEBUG_OBJECT (render, "VIDEO SEGMENT after: %" GST_SEGMENT_FORMAT,
            &render->video_segment);
        ret = gst_pad_event_default (pad, parent, event);
      } else {
        GST_ELEMENT_WARNING (render, STREAM, MUX, (NULL),
            ("received non-TIME newsegment event on video input"));
        ret = FALSE;
        gst_event_unref (event);
      }
      break;
    }
    case GST_EVENT_TAG:
    {
      GstTagList *taglist = NULL;

      /* tag events may contain attachments which might be fonts */
      GST_DEBUG_OBJECT (render, "got TAG event");

      gst_event_parse_tag (event, &taglist);
      gst_ass_render_handle_tags (render, taglist);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    case GST_EVENT_EOS:
      GST_ASS_RENDER_LOCK (render);
      GST_INFO_OBJECT (render, "video EOS");
      render->video_eos = TRUE;
      GST_ASS_RENDER_UNLOCK (render);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_FLUSH_START:
      GST_ASS_RENDER_LOCK (render);
      GST_INFO_OBJECT (render, "video flush start");
      render->video_flushing = TRUE;
      GST_ASS_RENDER_BROADCAST (render);
      GST_ASS_RENDER_UNLOCK (render);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_ASS_RENDER_LOCK (render);
      GST_INFO_OBJECT (render, "video flush stop");
      render->video_flushing = FALSE;
      render->video_eos = FALSE;
      gst_segment_init (&render->video_segment, GST_FORMAT_TIME);
      GST_ASS_RENDER_UNLOCK (render);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
gst_ass_render_query_video (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_ass_render_getcaps (pad, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static gboolean
gst_ass_render_event_text (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gint i;
  gboolean ret = FALSE;
  GstAssRender *render = GST_ASS_RENDER (parent);

  GST_DEBUG_OBJECT (pad, "received text event %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_ass_render_setcaps_text (pad, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment;

      GST_ASS_RENDER_LOCK (render);
      render->subtitle_eos = FALSE;
      GST_ASS_RENDER_UNLOCK (render);

      gst_event_copy_segment (event, &segment);

      GST_ASS_RENDER_LOCK (render);
      if (segment.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (render, "TEXT SEGMENT now: %" GST_SEGMENT_FORMAT,
            &render->subtitle_segment);

        render->subtitle_segment = segment;

        GST_DEBUG_OBJECT (render,
            "TEXT SEGMENT after: %" GST_SEGMENT_FORMAT,
            &render->subtitle_segment);
      } else {
        GST_ELEMENT_WARNING (render, STREAM, MUX, (NULL),
            ("received non-TIME newsegment event on subtitle input"));
      }

      gst_event_unref (event);
      ret = TRUE;

      /* wake up the video chain, it might be waiting for a text buffer or
       * a text segment update */
      GST_ASS_RENDER_BROADCAST (render);
      GST_ASS_RENDER_UNLOCK (render);
      break;
    }
    case GST_EVENT_GAP:{
      GstClockTime start, duration;

      gst_event_parse_gap (event, &start, &duration);
      if (GST_CLOCK_TIME_IS_VALID (duration))
        start += duration;
      /* we do not expect another buffer until after gap,
       * so that is our position now */
      GST_ASS_RENDER_LOCK (render);
      render->subtitle_segment.position = start;

      /* wake up the video chain, it might be waiting for a text buffer or
       * a text segment update */
      GST_ASS_RENDER_BROADCAST (render);
      GST_ASS_RENDER_UNLOCK (render);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      GST_ASS_RENDER_LOCK (render);
      GST_INFO_OBJECT (render, "text flush stop");
      render->subtitle_flushing = FALSE;
      render->subtitle_eos = FALSE;
      if (render->subtitle_pending)
        gst_buffer_unref (render->subtitle_pending);
      render->subtitle_pending = NULL;
      GST_ASS_RENDER_BROADCAST (render);
      gst_segment_init (&render->subtitle_segment, GST_FORMAT_TIME);
      GST_ASS_RENDER_UNLOCK (render);
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (render, "text flush start");
      g_mutex_lock (&render->ass_mutex);
      if (render->ass_track) {
        /* delete any events on the ass_track */
        for (i = 0; i < render->ass_track->n_events; i++) {
          GST_DEBUG_OBJECT (render, "deleted event with eid %i", i);
          ass_free_event (render->ass_track, i);
        }
        render->ass_track->n_events = 0;
        GST_DEBUG_OBJECT (render, "done flushing");
      }
      g_mutex_unlock (&render->ass_mutex);
      GST_ASS_RENDER_LOCK (render);
      render->subtitle_flushing = TRUE;
      GST_ASS_RENDER_BROADCAST (render);
      GST_ASS_RENDER_UNLOCK (render);
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_EOS:
      GST_ASS_RENDER_LOCK (render);
      render->subtitle_eos = TRUE;
      GST_INFO_OBJECT (render, "text EOS");
      /* wake up the video chain, it might be waiting for a text buffer or
       * a text segment update */
      GST_ASS_RENDER_BROADCAST (render);
      GST_ASS_RENDER_UNLOCK (render);
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_TAG:
    {
      GstTagList *taglist = NULL;

      /* tag events may contain attachments which might be fonts */
      GST_DEBUG_OBJECT (render, "got TAG event");

      gst_event_parse_tag (event, &taglist);
      gst_ass_render_handle_tags (render, taglist);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_ass_render_debug, "assrender",
      0, "ASS/SSA subtitle renderer");
  GST_DEBUG_CATEGORY_INIT (gst_ass_render_lib_debug, "assrender_library",
      0, "ASS/SSA subtitle renderer library");

  /* FIXME: fix unit tests before upping rank again */
  return gst_element_register (plugin, "assrender",
      GST_RANK_NONE, GST_TYPE_ASS_RENDER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    assrender,
    "ASS/SSA subtitle renderer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
