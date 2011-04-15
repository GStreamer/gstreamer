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
 * gst-launch -v filesrc location=/path/to/mkv ! matroskademux name=d ! queue ! mp3parse ! mad ! audioconvert ! autoaudiosink  d. ! queue ! ffdec_h264 ! ffmpegcolorspace ! r.   d. ! queue ! "application/x-ass" ! assrender name=r ! ffmpegcolorspace ! autovideosink
 * ]| This pipeline demuxes a Matroska file with h.264 video, MP3 audio and embedded ASS subtitles and renders the subtitles on top of the video.
 * </refsect2>
 */


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

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB ";" GST_VIDEO_CAPS_BGR ";"
        GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate video_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("video_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB ";" GST_VIDEO_CAPS_BGR ";"
        GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate text_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("text_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-ass; application/x-ssa")
    );

static void gst_ass_render_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ass_render_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_ass_render_finalize (GObject * object);

static GstStateChangeReturn gst_ass_render_change_state (GstElement * element,
    GstStateChange transition);

GST_BOILERPLATE (GstAssRender, gst_ass_render, GstElement, GST_TYPE_ELEMENT);

static GstCaps *gst_ass_render_getcaps (GstPad * pad);

static gboolean gst_ass_render_setcaps_video (GstPad * pad, GstCaps * caps);
static gboolean gst_ass_render_setcaps_text (GstPad * pad, GstCaps * caps);

static GstFlowReturn gst_ass_render_chain_video (GstPad * pad, GstBuffer * buf);
static GstFlowReturn gst_ass_render_chain_text (GstPad * pad, GstBuffer * buf);

static gboolean gst_ass_render_event_video (GstPad * pad, GstEvent * event);
static gboolean gst_ass_render_event_text (GstPad * pad, GstEvent * event);
static gboolean gst_ass_render_event_src (GstPad * pad, GstEvent * event);

static GstFlowReturn gst_ass_render_bufferalloc_video (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buffer);

static gboolean gst_ass_render_query_src (GstPad * pad, GstQuery * query);

static void
gst_ass_render_base_init (gpointer gclass)
{
  GstElementClass *element_class = (GstElementClass *) gclass;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&text_sink_factory));

  gst_element_class_set_details_simple (element_class, "ASS/SSA Render",
      "Mixer/Video/Overlay/Subtitle",
      "Renders ASS/SSA subtitles with libass",
      "Benjamin Schmitz <vortex@wolpzone.de>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

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
gst_ass_render_init (GstAssRender * render, GstAssRenderClass * gclass)
{
  GST_DEBUG_OBJECT (render, "init");

  render->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  render->video_sinkpad =
      gst_pad_new_from_static_template (&video_sink_factory, "video_sink");
  render->text_sinkpad =
      gst_pad_new_from_static_template (&text_sink_factory, "text_sink");

  gst_pad_set_setcaps_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_ass_render_setcaps_video));
  gst_pad_set_setcaps_function (render->text_sinkpad,
      GST_DEBUG_FUNCPTR (gst_ass_render_setcaps_text));

  gst_pad_set_getcaps_function (render->srcpad,
      GST_DEBUG_FUNCPTR (gst_ass_render_getcaps));
  gst_pad_set_getcaps_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_ass_render_getcaps));

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

  gst_pad_set_bufferalloc_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_ass_render_bufferalloc_video));

  gst_pad_set_query_function (render->srcpad,
      GST_DEBUG_FUNCPTR (gst_ass_render_query_src));

  gst_element_add_pad (GST_ELEMENT (render), render->srcpad);
  gst_element_add_pad (GST_ELEMENT (render), render->video_sinkpad);
  gst_element_add_pad (GST_ELEMENT (render), render->text_sinkpad);

  render->width = 0;
  render->height = 0;

  render->subtitle_mutex = g_mutex_new ();
  render->subtitle_cond = g_cond_new ();

  render->renderer_init_ok = FALSE;
  render->track_init_ok = FALSE;
  render->enable = TRUE;
  render->embeddedfonts = TRUE;

  gst_segment_init (&render->video_segment, GST_FORMAT_TIME);
  gst_segment_init (&render->subtitle_segment, GST_FORMAT_TIME);

  render->ass_mutex = g_mutex_new ();
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

  if (render->subtitle_mutex)
    g_mutex_free (render->subtitle_mutex);

  if (render->subtitle_cond)
    g_cond_free (render->subtitle_cond);

  if (render->ass_track) {
    ass_free_track (render->ass_track);
  }

  if (render->ass_renderer) {
    ass_renderer_done (render->ass_renderer);
  }

  if (render->ass_library) {
    ass_library_done (render->ass_library);
  }

  if (render->ass_mutex)
    g_mutex_free (render->ass_mutex);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ass_render_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAssRender *render = GST_ASS_RENDER (object);

  switch (prop_id) {
    case PROP_ENABLE:
      render->enable = g_value_get_boolean (value);
      break;
    case PROP_EMBEDDEDFONTS:
      render->embeddedfonts = g_value_get_boolean (value);
      g_mutex_lock (render->ass_mutex);
      ass_set_extract_fonts (render->ass_library, render->embeddedfonts);
      g_mutex_unlock (render->ass_mutex);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ass_render_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAssRender *render = GST_ASS_RENDER (object);

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
}

static GstStateChangeReturn
gst_ass_render_change_state (GstElement * element, GstStateChange transition)
{
  GstAssRender *render = GST_ASS_RENDER (element);
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
      g_mutex_lock (render->ass_mutex);
      if (render->ass_track)
        ass_free_track (render->ass_track);
      render->ass_track = NULL;
      g_mutex_unlock (render->ass_mutex);
      render->track_init_ok = FALSE;
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
gst_ass_render_query_src (GstPad * pad, GstQuery * query)
{
  GstAssRender *render = GST_ASS_RENDER (gst_pad_get_parent (pad));
  gboolean ret;

  ret = gst_pad_peer_query (render->video_sinkpad, query);

  gst_object_unref (render);
  return ret;
}

static gboolean
gst_ass_render_event_src (GstPad * pad, GstEvent * event)
{
  GstAssRender *render = GST_ASS_RENDER (gst_pad_get_parent (pad));
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
gst_ass_render_getcaps (GstPad * pad)
{
  GstAssRender *render = GST_ASS_RENDER (gst_pad_get_parent (pad));
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

#define CREATE_RGB_BLIT_FUNCTION(name,bpp,R,G,B) \
static void \
blit_##name (GstAssRender * render, ASS_Image * ass_image, GstBuffer * buffer) \
{ \
  guint counter = 0; \
  gint alpha, r, g, b, k; \
  const guint8 *src; \
  guint8 *dst; \
  gint x, y, w, h; \
  gint width = render->width; \
  gint height = render->height; \
  gint dst_stride = GST_ROUND_UP_4 (width * bpp); \
  gint dst_skip; \
  gint src_skip; \
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
    dst = buffer->data + ass_image->dst_y * dst_stride + ass_image->dst_x * bpp; \
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
blit_i420 (GstAssRender * render, ASS_Image * ass_image, GstBuffer * buffer)
{
  guint counter = 0;
  gint alpha, r, g, b, k, k2;
  gint Y, U, V;
  const guint8 *src;
  guint8 *dst_y, *dst_u, *dst_v;
  gint x, y, w, h;
  gint w2;
  gint width = render->width;
  gint height = render->height;
  gint src_stride;
  gint y_offset, y_stride;
  gint u_offset, u_stride;
  gint v_offset, v_stride;

  y_offset =
      gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420, 0, width,
      height);
  u_offset =
      gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420, 1, width,
      height);
  v_offset =
      gst_video_format_get_component_offset (GST_VIDEO_FORMAT_I420, 2, width,
      height);

  y_stride = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420, 0, width);
  u_stride = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420, 1, width);
  v_stride = gst_video_format_get_row_stride (GST_VIDEO_FORMAT_I420, 2, width);

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

    w2 = (w + 1) / 2;

    src_stride = ass_image->stride;

    src = ass_image->bitmap;
    dst_y =
        buffer->data + y_offset + ass_image->dst_y * y_stride +
        ass_image->dst_x;
    dst_u =
        buffer->data + u_offset + (ass_image->dst_y / 2) * u_stride +
        ass_image->dst_x / 2;
    dst_v =
        buffer->data + v_offset + (ass_image->dst_y / 2) * v_stride +
        ass_image->dst_x / 2;

    for (y = 0; y < h; y++) {
      dst_y = buffer->data + y_offset + (ass_image->dst_y + y) * y_stride +
          ass_image->dst_x;
      for (x = 0; x < w; x++) {
        k = src[y * ass_image->w + x] * alpha / 255;
        dst_y[x] = (k * Y + (255 - k) * dst_y[x]) / 255;
      }
    }

    y = 0;
    if (ass_image->dst_y & 1) {
      dst_u =
          buffer->data + u_offset + (ass_image->dst_y / 2) * u_stride +
          ass_image->dst_x / 2;
      dst_v =
          buffer->data + v_offset + (ass_image->dst_y / 2) * v_stride +
          ass_image->dst_x / 2;
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
      dst_u =
          buffer->data + u_offset + ((ass_image->dst_y + y) / 2) * u_stride +
          ass_image->dst_x / 2;
      dst_v =
          buffer->data + v_offset + ((ass_image->dst_y + y) / 2) * v_stride +
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
      dst_u =
          buffer->data + u_offset + (ass_image->dst_y / 2) * u_stride +
          ass_image->dst_x / 2;
      dst_v =
          buffer->data + v_offset + (ass_image->dst_y / 2) * v_stride +
          ass_image->dst_x / 2;
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

  g_mutex_lock (render->ass_mutex);
  ass_set_frame_size (render->ass_renderer, render->width, render->height);

  dar = (((gdouble) par_n) * ((gdouble) render->width))
      / (((gdouble) par_d) * ((gdouble) render->height));
#if !defined(LIBASS_VERSION) || LIBASS_VERSION < 0x00907000
  ass_set_aspect_ratio (render->ass_renderer, dar);
#else
  ass_set_aspect_ratio (render->ass_renderer,
      dar, ((gdouble) render->width) / ((gdouble) render->height));
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
  g_mutex_unlock (render->ass_mutex);

  render->renderer_init_ok = TRUE;

  GST_DEBUG_OBJECT (render, "ass renderer setup complete");

out:
  gst_object_unref (render);

  return ret;
}

static gboolean
gst_ass_render_setcaps_text (GstPad * pad, GstCaps * caps)
{
  GstAssRender *render = GST_ASS_RENDER (gst_pad_get_parent (pad));
  GstStructure *structure;
  const GValue *value;
  GstBuffer *priv;
  gchar *codec_private;
  guint codec_private_size;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (render, "text pad linked with caps:  %" GST_PTR_FORMAT,
      caps);

  value = gst_structure_get_value (structure, "codec_data");

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

  gst_object_unref (render);

  return ret;
}


static void
gst_ass_render_process_text (GstAssRender * render, GstBuffer * buffer,
    GstClockTime running_time, GstClockTime duration)
{
  gchar *data = (gchar *) GST_BUFFER_DATA (buffer);
  guint size = GST_BUFFER_SIZE (buffer);
  gdouble pts_start, pts_end;

  pts_start = running_time;
  pts_start /= GST_MSECOND;
  pts_end = duration;
  pts_end /= GST_MSECOND;

  GST_DEBUG_OBJECT (render,
      "Processing subtitles with running time %" GST_TIME_FORMAT
      " and duration %" GST_TIME_FORMAT, GST_TIME_ARGS (running_time),
      GST_TIME_ARGS (duration));
  g_mutex_lock (render->ass_mutex);
  ass_process_chunk (render->ass_track, data, size, pts_start, pts_end);
  g_mutex_unlock (render->ass_mutex);
  gst_buffer_unref (buffer);
}

static GstFlowReturn
gst_ass_render_bufferalloc_video (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buffer)
{
  GstAssRender *render = GST_ASS_RENDER (gst_pad_get_parent (pad));
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
gst_ass_render_chain_video (GstPad * pad, GstBuffer * buffer)
{
  GstAssRender *render = GST_ASS_RENDER (GST_PAD_PARENT (pad));
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean in_seg = FALSE;
  gint64 start, stop, clip_start = 0, clip_stop = 0;
  ASS_Image *ass_image;

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
    buffer = gst_buffer_make_metadata_writable (buffer);
    GST_BUFFER_TIMESTAMP (buffer) = clip_start;
    if (stop != -1)
      GST_BUFFER_DURATION (buffer) = clip_stop - clip_start;
  }

  gst_segment_set_last_stop (&render->video_segment, GST_FORMAT_TIME,
      clip_start);

  g_mutex_lock (render->subtitle_mutex);
  if (render->subtitle_pending) {
    GstClockTime sub_running_time, vid_running_time;
    GstClockTime sub_running_time_end, vid_running_time_end;

    sub_running_time =
        gst_segment_to_running_time (&render->subtitle_segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (render->subtitle_pending));
    sub_running_time_end =
        gst_segment_to_running_time (&render->subtitle_segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (render->subtitle_pending) +
        GST_BUFFER_DURATION (render->subtitle_pending));
    vid_running_time =
        gst_segment_to_running_time (&render->video_segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buffer));
    vid_running_time_end =
        gst_segment_to_running_time (&render->video_segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer));

    if (sub_running_time_end < vid_running_time) {
      gst_buffer_unref (render->subtitle_pending);
      GST_DEBUG_OBJECT (render,
          "Too late text buffer, dropping (%" GST_TIME_FORMAT " < %"
          GST_TIME_FORMAT, GST_TIME_ARGS (sub_running_time_end),
          GST_TIME_ARGS (vid_running_time));
      render->subtitle_pending = NULL;
      g_cond_signal (render->subtitle_cond);
    } else if (sub_running_time <= vid_running_time_end + GST_SECOND / 2) {
      gst_ass_render_process_text (render, render->subtitle_pending,
          sub_running_time, sub_running_time_end - sub_running_time);
      render->subtitle_pending = NULL;
      g_cond_signal (render->subtitle_cond);
    }
  }
  g_mutex_unlock (render->subtitle_mutex);

  /* now start rendering subtitles, if all conditions are met */
  if (render->renderer_init_ok && render->track_init_ok && render->enable) {
    GstClockTime running_time;
    gdouble timestamp;
#ifndef GST_DISABLE_GST_DEBUG
    gdouble step;
#endif

    running_time =
        gst_segment_to_running_time (&render->video_segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buffer));
    GST_DEBUG_OBJECT (render,
        "rendering frame for running time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (running_time));
    /* libass needs timestamps in ms */
    timestamp = running_time / GST_MSECOND;

    g_mutex_lock (render->ass_mutex);
#ifndef GST_DISABLE_GST_DEBUG
    /* only for testing right now. could possibly be used for optimizations? */
    step = ass_step_sub (render->ass_track, timestamp, 1);
    GST_DEBUG_OBJECT (render, "Current running time: %" GST_TIME_FORMAT
        " // Next event: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (running_time), GST_TIME_ARGS (step * GST_MSECOND));
#endif

    /* not sure what the last parameter to this call is for (detect_change) */
    ass_image = ass_render_frame (render->ass_renderer, render->ass_track,
        timestamp, NULL);
    g_mutex_unlock (render->ass_mutex);

    if (ass_image != NULL) {
      buffer = gst_buffer_make_writable (buffer);
      render->blit (render, ass_image, buffer);
    } else {
      GST_LOG_OBJECT (render, "nothing to render right now");
    }
  } else {
    GST_LOG_OBJECT (render, "rendering disabled, doing buffer passthrough");
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
gst_ass_render_chain_text (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstAssRender *render = GST_ASS_RENDER (GST_PAD_PARENT (pad));
  GstClockTime timestamp, duration;
  GstClockTime sub_running_time, vid_running_time;
  GstClockTime sub_running_time_end;
  gint64 cstart, cstop;
  gboolean in_seg;

  if (render->subtitle_flushing) {
    gst_buffer_unref (buffer);
    return GST_FLOW_WRONG_STATE;
  }

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (timestamp)
          || !GST_CLOCK_TIME_IS_VALID (duration))) {
    GST_WARNING_OBJECT (render,
        "Text buffer without valid timestamp" " or duration, dropping");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  in_seg =
      gst_segment_clip (&render->subtitle_segment, GST_FORMAT_TIME, timestamp,
      timestamp + duration, &cstart, &cstop);
  if (!in_seg) {
    GST_DEBUG_OBJECT (render,
        "Text buffer before segment start (%" GST_TIME_FORMAT " < %"
        GST_TIME_FORMAT ")", GST_TIME_ARGS (timestamp),
        GST_TIME_ARGS (render->subtitle_segment.start));
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  GST_BUFFER_TIMESTAMP (buffer) = timestamp = cstart;
  GST_BUFFER_DURATION (buffer) = duration = cstop - cstart;

  gst_segment_set_last_stop (&render->subtitle_segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (buffer));

  sub_running_time =
      gst_segment_to_running_time (&render->subtitle_segment, GST_FORMAT_TIME,
      timestamp);
  sub_running_time_end =
      gst_segment_to_running_time (&render->subtitle_segment, GST_FORMAT_TIME,
      timestamp + duration);
  vid_running_time =
      gst_segment_to_running_time (&render->video_segment, GST_FORMAT_TIME,
      render->video_segment.last_stop);

  if (render->fps_n && render->fps_d)
    vid_running_time +=
        gst_util_uint64_scale (GST_SECOND, render->fps_d, render->fps_n);

  if (sub_running_time > vid_running_time + GST_SECOND / 2) {
    g_assert (render->subtitle_pending == NULL);
    g_mutex_lock (render->subtitle_mutex);
    if (G_UNLIKELY (render->subtitle_flushing)) {
      GST_DEBUG_OBJECT (render, "Text pad flushing");
      gst_object_unref (buffer);
      g_mutex_unlock (render->subtitle_mutex);
      return GST_FLOW_WRONG_STATE;
    }
    GST_DEBUG_OBJECT (render,
        "Too early text buffer, waiting (%" GST_TIME_FORMAT " > %"
        GST_TIME_FORMAT, GST_TIME_ARGS (sub_running_time),
        GST_TIME_ARGS (vid_running_time));
    render->subtitle_pending = buffer;
    g_cond_wait (render->subtitle_cond, render->subtitle_mutex);
    g_mutex_unlock (render->subtitle_mutex);
  } else if (sub_running_time_end < vid_running_time) {
    GST_DEBUG_OBJECT (render,
        "Too late text buffer, dropping (%" GST_TIME_FORMAT " < %"
        GST_TIME_FORMAT, GST_TIME_ARGS (sub_running_time_end),
        GST_TIME_ARGS (vid_running_time));
    gst_buffer_unref (buffer);
    ret = GST_FLOW_OK;
  } else {
    gst_ass_render_process_text (render, buffer, sub_running_time,
        sub_running_time_end - sub_running_time);
    ret = GST_FLOW_OK;
  }

  GST_DEBUG_OBJECT (render,
      "processed text packet with timestamp %" GST_TIME_FORMAT
      " and duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp), GST_TIME_ARGS (duration));

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
  guint index;

  if (!taglist)
    return;

  tag_size = gst_tag_list_get_tag_size (taglist, GST_TAG_ATTACHMENT);
  if (tag_size > 0 && render->embeddedfonts) {
    const GValue *value;
    GstBuffer *buf;
    GstCaps *caps;
    GstStructure *structure;
    gboolean valid_mimetype, valid_extension;
    guint j;
    const gchar *filename;

    GST_DEBUG_OBJECT (render, "TAG event has attachments");

    for (index = 0; index < tag_size; index++) {
      value = gst_tag_list_get_value_index (taglist, GST_TAG_ATTACHMENT, index);
      buf = gst_value_get_buffer (value);
      if (!buf || !GST_BUFFER_CAPS (buf))
        continue;

      caps = GST_BUFFER_CAPS (buf);
      structure = gst_caps_get_structure (caps, 0);

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
        g_mutex_lock (render->ass_mutex);
        ass_add_font (render->ass_library, (gchar *) filename,
            (gchar *) GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
        GST_DEBUG_OBJECT (render, "registered new font %s", filename);
        g_mutex_unlock (render->ass_mutex);
      }
    }
  }
}

static gboolean
gst_ass_render_event_video (GstPad * pad, GstEvent * event)
{
  gboolean ret = FALSE;
  GstAssRender *render = GST_ASS_RENDER (gst_pad_get_parent (pad));

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
    case GST_EVENT_TAG:
    {
      GstTagList *taglist = NULL;

      /* tag events may contain attachments which might be fonts */
      GST_DEBUG_OBJECT (render, "got TAG event");

      gst_event_parse_tag (event, &taglist);
      gst_ass_render_handle_tags (render, taglist);
      ret = gst_pad_push_event (render->srcpad, event);
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
gst_ass_render_event_text (GstPad * pad, GstEvent * event)
{
  gint i;
  gboolean ret = FALSE;
  GstAssRender *render = GST_ASS_RENDER (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pad, "received text event %s", GST_EVENT_TYPE_NAME (event));

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
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      gst_segment_init (&render->subtitle_segment, GST_FORMAT_TIME);
      render->subtitle_flushing = FALSE;
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (render, "begin flushing");
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
      g_mutex_lock (render->subtitle_mutex);
      if (render->subtitle_pending)
        gst_buffer_unref (render->subtitle_pending);
      render->subtitle_pending = NULL;
      render->subtitle_flushing = TRUE;
      g_cond_signal (render->subtitle_cond);
      g_mutex_unlock (render->subtitle_mutex);
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
      GstTagList *taglist = NULL;

      /* tag events may contain attachments which might be fonts */
      GST_DEBUG_OBJECT (render, "got TAG event");

      gst_event_parse_tag (event, &taglist);
      gst_ass_render_handle_tags (render, taglist);
      ret = gst_pad_push_event (render->srcpad, event);
      break;
    }
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
  GST_DEBUG_CATEGORY_INIT (gst_ass_render_debug, "assrender",
      0, "ASS/SSA subtitle renderer");
  GST_DEBUG_CATEGORY_INIT (gst_ass_render_lib_debug, "assrender_library",
      0, "ASS/SSA subtitle renderer library");

  return gst_element_register (plugin, "assrender",
      GST_RANK_PRIMARY, GST_TYPE_ASS_RENDER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "assrender",
    "ASS/SSA subtitle renderer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
