/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) <2006> Julien Moutte <julien@moutte.net>
 * Copyright (C) <2006> Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (C) <2006-2008> Tim-Philipp Müller <tim centricular net>
 * Copyright (C) <2009> Young-Ho Cha <ganadist@gmail.com>
 * Copyright (C) <2015> British Broadcasting Corporation <dash@rd.bbc.co.uk>
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
 * SECTION:element-ttmlrender
 *
 * Renders timed text on top of a video stream. It receives text in buffers
 * from a ttmlparse element; each text string is in its own #GstMemory within
 * the GstBuffer, and the styling and layout associated with each text string
 * is in metadata attached to the #GstBuffer.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch-1.0 filesrc location=<media file location> ! video/quicktime ! qtdemux name=q ttmlrender name=r q. ! queue ! h264parse ! avdec_h264 ! autovideoconvert ! r.video_sink filesrc location=<subtitle file location> blocksize=16777216 ! queue ! ttmlparse ! r.text_sink r. ! ximagesink q. ! queue ! aacparse ! avdec_aac ! audioconvert ! alsasink
 * ]| Parse and render TTML subtitles contained in a single XML file over an
 * MP4 stream containing H.264 video and AAC audio:
 * </refsect2>
 */

#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/video-overlay-composition.h>
#include <pango/pangocairo.h>

#include <string.h>
#include <math.h>

#include "gstttmlrender.h"
#include "subtitle.h"
#include "subtitlemeta.h"

#define VIDEO_FORMATS GST_VIDEO_OVERLAY_COMPOSITION_BLEND_FORMATS

#define TTML_RENDER_CAPS GST_VIDEO_CAPS_MAKE (VIDEO_FORMATS)

#define TTML_RENDER_ALL_CAPS TTML_RENDER_CAPS ";" \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS_ALL)

GST_DEBUG_CATEGORY_EXTERN (ttmlrender_debug);
#define GST_CAT_DEFAULT ttmlrender_debug

static GstStaticCaps sw_template_caps = GST_STATIC_CAPS (TTML_RENDER_CAPS);

static GstStaticPadTemplate src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (TTML_RENDER_ALL_CAPS)
    );

static GstStaticPadTemplate video_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("video_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (TTML_RENDER_ALL_CAPS)
    );

static GstStaticPadTemplate text_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("text_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-raw(meta:GstSubtitleMeta)")
    );

#define GST_TTML_RENDER_GET_LOCK(ov) (&GST_TTML_RENDER (ov)->lock)
#define GST_TTML_RENDER_GET_COND(ov) (&GST_TTML_RENDER (ov)->cond)
#define GST_TTML_RENDER_LOCK(ov)     (g_mutex_lock (GST_TTML_RENDER_GET_LOCK (ov)))
#define GST_TTML_RENDER_UNLOCK(ov)   (g_mutex_unlock (GST_TTML_RENDER_GET_LOCK (ov)))
#define GST_TTML_RENDER_WAIT(ov)     (g_cond_wait (GST_TTML_RENDER_GET_COND (ov), GST_TTML_RENDER_GET_LOCK (ov)))
#define GST_TTML_RENDER_SIGNAL(ov)   (g_cond_signal (GST_TTML_RENDER_GET_COND (ov)))
#define GST_TTML_RENDER_BROADCAST(ov)(g_cond_broadcast (GST_TTML_RENDER_GET_COND (ov)))

static GstElementClass *parent_class = NULL;
static void gst_ttml_render_base_init (gpointer g_class);
static void gst_ttml_render_class_init (GstTtmlRenderClass * klass);
static void gst_ttml_render_init (GstTtmlRender * render,
    GstTtmlRenderClass * klass);

static GstStateChangeReturn gst_ttml_render_change_state (GstElement *
    element, GstStateChange transition);

static GstCaps *gst_ttml_render_get_videosink_caps (GstPad * pad,
    GstTtmlRender * render, GstCaps * filter);
static GstCaps *gst_ttml_render_get_src_caps (GstPad * pad,
    GstTtmlRender * render, GstCaps * filter);
static gboolean gst_ttml_render_setcaps (GstTtmlRender * render,
    GstCaps * caps);
static gboolean gst_ttml_render_src_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_ttml_render_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);

static gboolean gst_ttml_render_video_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_ttml_render_video_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static GstFlowReturn gst_ttml_render_video_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);

static gboolean gst_ttml_render_text_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static GstFlowReturn gst_ttml_render_text_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstPadLinkReturn gst_ttml_render_text_pad_link (GstPad * pad,
    GstObject * parent, GstPad * peer);
static void gst_ttml_render_text_pad_unlink (GstPad * pad, GstObject * parent);
static void gst_ttml_render_pop_text (GstTtmlRender * render);

static void gst_ttml_render_finalize (GObject * object);

static gboolean gst_ttml_render_can_handle_caps (GstCaps * incaps);

static GstTtmlRenderRenderedImage *gst_ttml_render_rendered_image_new
    (GstBuffer * image, gint x, gint y, guint width, guint height);
static GstTtmlRenderRenderedImage *gst_ttml_render_rendered_image_new_empty
    (void);
static GstTtmlRenderRenderedImage *gst_ttml_render_rendered_image_copy
    (GstTtmlRenderRenderedImage * image);
static void gst_ttml_render_rendered_image_free
    (GstTtmlRenderRenderedImage * image);

GType
gst_ttml_render_get_type (void)
{
  static GType type = 0;

  if (g_once_init_enter ((gsize *) & type)) {
    static const GTypeInfo info = {
      sizeof (GstTtmlRenderClass),
      (GBaseInitFunc) gst_ttml_render_base_init,
      NULL,
      (GClassInitFunc) gst_ttml_render_class_init,
      NULL,
      NULL,
      sizeof (GstTtmlRender),
      0,
      (GInstanceInitFunc) gst_ttml_render_init,
    };

    g_once_init_leave ((gsize *) & type,
        g_type_register_static (GST_TYPE_ELEMENT, "GstTtmlRender", &info, 0));
  }

  return type;
}

static void
gst_ttml_render_base_init (gpointer g_class)
{
  GstTtmlRenderClass *klass = GST_TTML_RENDER_CLASS (g_class);
  PangoFontMap *fontmap;

  /* Only lock for the subclasses here, the base class
   * doesn't have this mutex yet and it's not necessary
   * here */
  if (klass->pango_lock)
    g_mutex_lock (klass->pango_lock);
  fontmap = pango_cairo_font_map_get_default ();
  klass->pango_context =
      pango_font_map_create_context (PANGO_FONT_MAP (fontmap));
  if (klass->pango_lock)
    g_mutex_unlock (klass->pango_lock);
}

static void
gst_ttml_render_class_init (GstTtmlRenderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_ttml_render_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&video_sink_template_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&text_sink_template_factory));

  gst_element_class_set_static_metadata (gstelement_class,
      "TTML subtitle renderer", "Overlay/Subtitle",
      "Renders timed-text subtitles on top of video buffers",
      "David Schleef <ds@schleef.org>, Zeeshan Ali <zeeshan.ali@nokia.com>, "
      "Chris Bass <dash@rd.bbc.co.uk>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_ttml_render_change_state);

  klass->pango_lock = g_slice_new (GMutex);
  g_mutex_init (klass->pango_lock);
}

static void
gst_ttml_render_finalize (GObject * object)
{
  GstTtmlRender *render = GST_TTML_RENDER (object);

  if (render->compositions) {
    g_list_free_full (render->compositions,
        (GDestroyNotify) gst_video_overlay_composition_unref);
    render->compositions = NULL;
  }

  if (render->text_buffer) {
    gst_buffer_unref (render->text_buffer);
    render->text_buffer = NULL;
  }

  g_mutex_clear (&render->lock);
  g_cond_clear (&render->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ttml_render_init (GstTtmlRender * render, GstTtmlRenderClass * klass)
{
  GstPadTemplate *template;

  /* video sink */
  template = gst_static_pad_template_get (&video_sink_template_factory);
  render->video_sinkpad = gst_pad_new_from_template (template, "video_sink");
  gst_object_unref (template);
  gst_pad_set_event_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_ttml_render_video_event));
  gst_pad_set_chain_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_ttml_render_video_chain));
  gst_pad_set_query_function (render->video_sinkpad,
      GST_DEBUG_FUNCPTR (gst_ttml_render_video_query));
  GST_PAD_SET_PROXY_ALLOCATION (render->video_sinkpad);
  gst_element_add_pad (GST_ELEMENT (render), render->video_sinkpad);

  template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass),
      "text_sink");
  if (template) {
    /* text sink */
    render->text_sinkpad = gst_pad_new_from_template (template, "text_sink");

    gst_pad_set_event_function (render->text_sinkpad,
        GST_DEBUG_FUNCPTR (gst_ttml_render_text_event));
    gst_pad_set_chain_function (render->text_sinkpad,
        GST_DEBUG_FUNCPTR (gst_ttml_render_text_chain));
    gst_pad_set_link_function (render->text_sinkpad,
        GST_DEBUG_FUNCPTR (gst_ttml_render_text_pad_link));
    gst_pad_set_unlink_function (render->text_sinkpad,
        GST_DEBUG_FUNCPTR (gst_ttml_render_text_pad_unlink));
    gst_element_add_pad (GST_ELEMENT (render), render->text_sinkpad);
  }

  /* (video) source */
  template = gst_static_pad_template_get (&src_template_factory);
  render->srcpad = gst_pad_new_from_template (template, "src");
  gst_object_unref (template);
  gst_pad_set_event_function (render->srcpad,
      GST_DEBUG_FUNCPTR (gst_ttml_render_src_event));
  gst_pad_set_query_function (render->srcpad,
      GST_DEBUG_FUNCPTR (gst_ttml_render_src_query));
  gst_element_add_pad (GST_ELEMENT (render), render->srcpad);

  g_mutex_lock (GST_TTML_RENDER_GET_CLASS (render)->pango_lock);

  render->wait_text = TRUE;
  render->need_render = TRUE;
  render->text_buffer = NULL;
  render->text_linked = FALSE;

  render->compositions = NULL;

  g_mutex_init (&render->lock);
  g_cond_init (&render->cond);
  gst_segment_init (&render->segment, GST_FORMAT_TIME);
  g_mutex_unlock (GST_TTML_RENDER_GET_CLASS (render)->pango_lock);
}


/* only negotiate/query video render composition support for now */
static gboolean
gst_ttml_render_negotiate (GstTtmlRender * render, GstCaps * caps)
{
  GstQuery *query;
  gboolean attach = FALSE;
  gboolean caps_has_meta = TRUE;
  gboolean ret;
  GstCapsFeatures *f;
  GstCaps *original_caps;
  gboolean original_has_meta = FALSE;
  gboolean allocation_ret = TRUE;

  GST_DEBUG_OBJECT (render, "performing negotiation");

  gst_pad_check_reconfigure (render->srcpad);

  if (!caps)
    caps = gst_pad_get_current_caps (render->video_sinkpad);
  else
    gst_caps_ref (caps);

  if (!caps || gst_caps_is_empty (caps))
    goto no_format;

  original_caps = caps;

  /* Try to use the render meta if possible */
  f = gst_caps_get_features (caps, 0);

  /* if the caps doesn't have the render meta, we query if downstream
   * accepts it before trying the version without the meta
   * If upstream already is using the meta then we can only use it */
  if (!f
      || !gst_caps_features_contains (f,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION)) {
    GstCaps *overlay_caps;

    /* In this case we added the meta, but we can work without it
     * so preserve the original caps so we can use it as a fallback */
    overlay_caps = gst_caps_copy (caps);

    f = gst_caps_get_features (overlay_caps, 0);
    gst_caps_features_add (f,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);

    ret = gst_pad_peer_query_accept_caps (render->srcpad, overlay_caps);
    GST_DEBUG_OBJECT (render, "Downstream accepts the render meta: %d", ret);
    if (ret) {
      gst_caps_unref (caps);
      caps = overlay_caps;

    } else {
      /* fallback to the original */
      gst_caps_unref (overlay_caps);
      caps_has_meta = FALSE;
    }
  } else {
    original_has_meta = TRUE;
  }
  GST_DEBUG_OBJECT (render, "Using caps %" GST_PTR_FORMAT, caps);
  ret = gst_pad_set_caps (render->srcpad, caps);

  if (ret) {
    /* find supported meta */
    query = gst_query_new_allocation (caps, FALSE);

    if (!gst_pad_peer_query (render->srcpad, query)) {
      /* no problem, we use the query defaults */
      GST_DEBUG_OBJECT (render, "ALLOCATION query failed");
      allocation_ret = FALSE;
    }

    if (caps_has_meta && gst_query_find_allocation_meta (query,
            GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, NULL))
      attach = TRUE;

    gst_query_unref (query);
  }

  if (!allocation_ret && render->video_flushing) {
    ret = FALSE;
  } else if (original_caps && !original_has_meta && !attach) {
    if (caps_has_meta) {
      /* Some elements (fakesink) claim to accept the meta on caps but won't
         put it in the allocation query result, this leads below
         check to fail. Prevent this by removing the meta from caps */
      gst_caps_unref (caps);
      caps = gst_caps_ref (original_caps);
      ret = gst_pad_set_caps (render->srcpad, caps);
      if (ret && !gst_ttml_render_can_handle_caps (caps))
        ret = FALSE;
    }
  }

  if (!ret) {
    GST_DEBUG_OBJECT (render, "negotiation failed, schedule reconfigure");
    gst_pad_mark_reconfigure (render->srcpad);
  }

  gst_caps_unref (caps);

  if (!ret)
    gst_pad_mark_reconfigure (render->srcpad);

  return ret;

no_format:
  {
    if (caps)
      gst_caps_unref (caps);
    gst_pad_mark_reconfigure (render->srcpad);
    return FALSE;
  }
}

static gboolean
gst_ttml_render_can_handle_caps (GstCaps * incaps)
{
  gboolean ret;
  GstCaps *caps;
  static GstStaticCaps static_caps = GST_STATIC_CAPS (TTML_RENDER_CAPS);

  caps = gst_static_caps_get (&static_caps);
  ret = gst_caps_is_subset (incaps, caps);
  gst_caps_unref (caps);

  return ret;
}

static gboolean
gst_ttml_render_setcaps (GstTtmlRender * render, GstCaps * caps)
{
  GstVideoInfo info;
  gboolean ret = FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    goto invalid_caps;

  render->info = info;
  render->format = GST_VIDEO_INFO_FORMAT (&info);
  render->width = GST_VIDEO_INFO_WIDTH (&info);
  render->height = GST_VIDEO_INFO_HEIGHT (&info);

  ret = gst_ttml_render_negotiate (render, caps);

  GST_TTML_RENDER_LOCK (render);
  g_mutex_lock (GST_TTML_RENDER_GET_CLASS (render)->pango_lock);
  if (!gst_ttml_render_can_handle_caps (caps)) {
    GST_DEBUG_OBJECT (render, "unsupported caps %" GST_PTR_FORMAT, caps);
    ret = FALSE;
  }

  g_mutex_unlock (GST_TTML_RENDER_GET_CLASS (render)->pango_lock);
  GST_TTML_RENDER_UNLOCK (render);

  return ret;

  /* ERRORS */
invalid_caps:
  {
    GST_DEBUG_OBJECT (render, "could not parse caps");
    return FALSE;
  }
}


static gboolean
gst_ttml_render_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret = FALSE;
  GstTtmlRender *render;

  render = GST_TTML_RENDER (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_ttml_render_get_src_caps (pad, render, filter);
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
gst_ttml_render_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstTtmlRender *render;
  gboolean ret;

  render = GST_TTML_RENDER (parent);

  if (render->text_linked) {
    gst_event_ref (event);
    ret = gst_pad_push_event (render->video_sinkpad, event);
    gst_pad_push_event (render->text_sinkpad, event);
  } else {
    ret = gst_pad_push_event (render->video_sinkpad, event);
  }

  return ret;
}

/**
 * gst_ttml_render_add_feature_and_intersect:
 *
 * Creates a new #GstCaps containing the (given caps +
 * given caps feature) + (given caps intersected by the
 * given filter).
 *
 * Returns: the new #GstCaps
 */
static GstCaps *
gst_ttml_render_add_feature_and_intersect (GstCaps * caps,
    const gchar * feature, GstCaps * filter)
{
  int i, caps_size;
  GstCaps *new_caps;

  new_caps = gst_caps_copy (caps);

  caps_size = gst_caps_get_size (new_caps);
  for (i = 0; i < caps_size; i++) {
    GstCapsFeatures *features = gst_caps_get_features (new_caps, i);

    if (!gst_caps_features_is_any (features)) {
      gst_caps_features_add (features, feature);
    }
  }

  gst_caps_append (new_caps, gst_caps_intersect_full (caps,
          filter, GST_CAPS_INTERSECT_FIRST));

  return new_caps;
}

/**
 * gst_ttml_render_intersect_by_feature:
 *
 * Creates a new #GstCaps based on the following filtering rule.
 *
 * For each individual caps contained in given caps, if the
 * caps uses the given caps feature, keep a version of the caps
 * with the feature and an another one without. Otherwise, intersect
 * the caps with the given filter.
 *
 * Returns: the new #GstCaps
 */
static GstCaps *
gst_ttml_render_intersect_by_feature (GstCaps * caps,
    const gchar * feature, GstCaps * filter)
{
  int i, caps_size;
  GstCaps *new_caps;

  new_caps = gst_caps_new_empty ();

  caps_size = gst_caps_get_size (caps);
  for (i = 0; i < caps_size; i++) {
    GstStructure *caps_structure = gst_caps_get_structure (caps, i);
    GstCapsFeatures *caps_features =
        gst_caps_features_copy (gst_caps_get_features (caps, i));
    GstCaps *filtered_caps;
    GstCaps *simple_caps =
        gst_caps_new_full (gst_structure_copy (caps_structure), NULL);
    gst_caps_set_features (simple_caps, 0, caps_features);

    if (gst_caps_features_contains (caps_features, feature)) {
      gst_caps_append (new_caps, gst_caps_copy (simple_caps));

      gst_caps_features_remove (caps_features, feature);
      filtered_caps = gst_caps_ref (simple_caps);
    } else {
      filtered_caps = gst_caps_intersect_full (simple_caps, filter,
          GST_CAPS_INTERSECT_FIRST);
    }

    gst_caps_unref (simple_caps);
    gst_caps_append (new_caps, filtered_caps);
  }

  return new_caps;
}

static GstCaps *
gst_ttml_render_get_videosink_caps (GstPad * pad,
    GstTtmlRender * render, GstCaps * filter)
{
  GstPad *srcpad = render->srcpad;
  GstCaps *peer_caps = NULL, *caps = NULL, *overlay_filter = NULL;

  if (G_UNLIKELY (!render))
    return gst_pad_get_pad_template_caps (pad);

  if (filter) {
    /* filter caps + composition feature + filter caps
     * filtered by the software caps. */
    GstCaps *sw_caps = gst_static_caps_get (&sw_template_caps);
    overlay_filter = gst_ttml_render_add_feature_and_intersect (filter,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, sw_caps);
    gst_caps_unref (sw_caps);

    GST_DEBUG_OBJECT (render, "render filter %" GST_PTR_FORMAT, overlay_filter);
  }

  peer_caps = gst_pad_peer_query_caps (srcpad, overlay_filter);

  if (overlay_filter)
    gst_caps_unref (overlay_filter);

  if (peer_caps) {

    GST_DEBUG_OBJECT (pad, "peer caps  %" GST_PTR_FORMAT, peer_caps);

    if (gst_caps_is_any (peer_caps)) {
      /* if peer returns ANY caps, return filtered src pad template caps */
      caps = gst_caps_copy (gst_pad_get_pad_template_caps (srcpad));
    } else {

      /* duplicate caps which contains the composition into one version with
       * the meta and one without. Filter the other caps by the software caps */
      GstCaps *sw_caps = gst_static_caps_get (&sw_template_caps);
      caps = gst_ttml_render_intersect_by_feature (peer_caps,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, sw_caps);
      gst_caps_unref (sw_caps);
    }

    gst_caps_unref (peer_caps);

  } else {
    /* no peer, our padtemplate is enough then */
    caps = gst_pad_get_pad_template_caps (pad);
  }

  if (filter) {
    GstCaps *intersection = gst_caps_intersect_full (filter, caps,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = intersection;
  }

  GST_DEBUG_OBJECT (render, "returning  %" GST_PTR_FORMAT, caps);

  return caps;
}

static GstCaps *
gst_ttml_render_get_src_caps (GstPad * pad, GstTtmlRender * render,
    GstCaps * filter)
{
  GstPad *sinkpad = render->video_sinkpad;
  GstCaps *peer_caps = NULL, *caps = NULL, *overlay_filter = NULL;

  if (G_UNLIKELY (!render))
    return gst_pad_get_pad_template_caps (pad);

  if (filter) {
    /* duplicate filter caps which contains the composition into one version
     * with the meta and one without. Filter the other caps by the software
     * caps */
    GstCaps *sw_caps = gst_static_caps_get (&sw_template_caps);
    overlay_filter =
        gst_ttml_render_intersect_by_feature (filter,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, sw_caps);
    gst_caps_unref (sw_caps);
  }

  peer_caps = gst_pad_peer_query_caps (sinkpad, overlay_filter);

  if (overlay_filter)
    gst_caps_unref (overlay_filter);

  if (peer_caps) {

    GST_DEBUG_OBJECT (pad, "peer caps  %" GST_PTR_FORMAT, peer_caps);

    if (gst_caps_is_any (peer_caps)) {

      /* if peer returns ANY caps, return filtered sink pad template caps */
      caps = gst_caps_copy (gst_pad_get_pad_template_caps (sinkpad));

    } else {

      /* return upstream caps + composition feature + upstream caps
       * filtered by the software caps. */
      GstCaps *sw_caps = gst_static_caps_get (&sw_template_caps);
      caps = gst_ttml_render_add_feature_and_intersect (peer_caps,
          GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, sw_caps);
      gst_caps_unref (sw_caps);
    }

    gst_caps_unref (peer_caps);

  } else {
    /* no peer, our padtemplate is enough then */
    caps = gst_pad_get_pad_template_caps (pad);
  }

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = intersection;
  }
  GST_DEBUG_OBJECT (render, "returning  %" GST_PTR_FORMAT, caps);

  return caps;
}


static GstFlowReturn
gst_ttml_render_push_frame (GstTtmlRender * render, GstBuffer * video_frame)
{
  GstVideoFrame frame;
  GList *compositions = render->compositions;

  if (compositions == NULL) {
    GST_CAT_DEBUG (ttmlrender_debug, "No compositions.");
    goto done;
  }

  if (gst_pad_check_reconfigure (render->srcpad)) {
    if (!gst_ttml_render_negotiate (render, NULL)) {
      gst_pad_mark_reconfigure (render->srcpad);
      gst_buffer_unref (video_frame);
      if (GST_PAD_IS_FLUSHING (render->srcpad))
        return GST_FLOW_FLUSHING;
      else
        return GST_FLOW_NOT_NEGOTIATED;
    }
  }

  video_frame = gst_buffer_make_writable (video_frame);

  if (!gst_video_frame_map (&frame, &render->info, video_frame,
          GST_MAP_READWRITE))
    goto invalid_frame;

  while (compositions) {
    GstVideoOverlayComposition *composition = compositions->data;
    gst_video_overlay_composition_blend (composition, &frame);
    compositions = compositions->next;
  }

  gst_video_frame_unmap (&frame);

done:

  return gst_pad_push (render->srcpad, video_frame);

  /* ERRORS */
invalid_frame:
  {
    gst_buffer_unref (video_frame);
    GST_DEBUG_OBJECT (render, "received invalid buffer");
    return GST_FLOW_OK;
  }
}

static GstPadLinkReturn
gst_ttml_render_text_pad_link (GstPad * pad, GstObject * parent, GstPad * peer)
{
  GstTtmlRender *render;

  render = GST_TTML_RENDER (parent);
  if (G_UNLIKELY (!render))
    return GST_PAD_LINK_REFUSED;

  GST_DEBUG_OBJECT (render, "Text pad linked");

  render->text_linked = TRUE;

  return GST_PAD_LINK_OK;
}

static void
gst_ttml_render_text_pad_unlink (GstPad * pad, GstObject * parent)
{
  GstTtmlRender *render;

  /* don't use gst_pad_get_parent() here, will deadlock */
  render = GST_TTML_RENDER (parent);

  GST_DEBUG_OBJECT (render, "Text pad unlinked");

  render->text_linked = FALSE;

  gst_segment_init (&render->text_segment, GST_FORMAT_UNDEFINED);
}

static gboolean
gst_ttml_render_text_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret = FALSE;
  GstTtmlRender *render = NULL;

  render = GST_TTML_RENDER (parent);

  GST_LOG_OBJECT (pad, "received event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;

      render->text_eos = FALSE;

      gst_event_parse_segment (event, &segment);

      if (segment->format == GST_FORMAT_TIME) {
        GST_TTML_RENDER_LOCK (render);
        gst_segment_copy_into (segment, &render->text_segment);
        GST_DEBUG_OBJECT (render, "TEXT SEGMENT now: %" GST_SEGMENT_FORMAT,
            &render->text_segment);
        GST_TTML_RENDER_UNLOCK (render);
      } else {
        GST_ELEMENT_WARNING (render, STREAM, MUX, (NULL),
            ("received non-TIME newsegment event on text input"));
      }

      gst_event_unref (event);
      ret = TRUE;

      /* wake up the video chain, it might be waiting for a text buffer or
       * a text segment update */
      GST_TTML_RENDER_LOCK (render);
      GST_TTML_RENDER_BROADCAST (render);
      GST_TTML_RENDER_UNLOCK (render);
      break;
    }
    case GST_EVENT_GAP:
    {
      GstClockTime start, duration;

      gst_event_parse_gap (event, &start, &duration);
      if (GST_CLOCK_TIME_IS_VALID (duration))
        start += duration;
      /* we do not expect another buffer until after gap,
       * so that is our position now */
      render->text_segment.position = start;

      /* wake up the video chain, it might be waiting for a text buffer or
       * a text segment update */
      GST_TTML_RENDER_LOCK (render);
      GST_TTML_RENDER_BROADCAST (render);
      GST_TTML_RENDER_UNLOCK (render);

      gst_event_unref (event);
      ret = TRUE;
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      GST_TTML_RENDER_LOCK (render);
      GST_INFO_OBJECT (render, "text flush stop");
      render->text_flushing = FALSE;
      render->text_eos = FALSE;
      gst_ttml_render_pop_text (render);
      gst_segment_init (&render->text_segment, GST_FORMAT_TIME);
      GST_TTML_RENDER_UNLOCK (render);
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_FLUSH_START:
      GST_TTML_RENDER_LOCK (render);
      GST_INFO_OBJECT (render, "text flush start");
      render->text_flushing = TRUE;
      GST_TTML_RENDER_BROADCAST (render);
      GST_TTML_RENDER_UNLOCK (render);
      gst_event_unref (event);
      ret = TRUE;
      break;
    case GST_EVENT_EOS:
      GST_TTML_RENDER_LOCK (render);
      render->text_eos = TRUE;
      GST_INFO_OBJECT (render, "text EOS");
      /* wake up the video chain, it might be waiting for a text buffer or
       * a text segment update */
      GST_TTML_RENDER_BROADCAST (render);
      GST_TTML_RENDER_UNLOCK (render);
      gst_event_unref (event);
      ret = TRUE;
      break;
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
gst_ttml_render_video_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret = FALSE;
  GstTtmlRender *render = NULL;

  render = GST_TTML_RENDER (parent);

  GST_DEBUG_OBJECT (pad, "received event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      gint prev_width = render->width;
      gint prev_height = render->height;

      gst_event_parse_caps (event, &caps);
      ret = gst_ttml_render_setcaps (render, caps);
      if (render->width != prev_width || render->height != prev_height)
        render->need_render = TRUE;
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *segment;

      GST_DEBUG_OBJECT (render, "received new segment");

      gst_event_parse_segment (event, &segment);

      if (segment->format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (render, "VIDEO SEGMENT now: %" GST_SEGMENT_FORMAT,
            &render->segment);

        gst_segment_copy_into (segment, &render->segment);
      } else {
        GST_ELEMENT_WARNING (render, STREAM, MUX, (NULL),
            ("received non-TIME newsegment event on video input"));
      }

      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    case GST_EVENT_EOS:
      GST_TTML_RENDER_LOCK (render);
      GST_INFO_OBJECT (render, "video EOS");
      render->video_eos = TRUE;
      GST_TTML_RENDER_UNLOCK (render);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_FLUSH_START:
      GST_TTML_RENDER_LOCK (render);
      GST_INFO_OBJECT (render, "video flush start");
      render->video_flushing = TRUE;
      GST_TTML_RENDER_BROADCAST (render);
      GST_TTML_RENDER_UNLOCK (render);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_TTML_RENDER_LOCK (render);
      GST_INFO_OBJECT (render, "video flush stop");
      render->video_flushing = FALSE;
      render->video_eos = FALSE;
      gst_segment_init (&render->segment, GST_FORMAT_TIME);
      GST_TTML_RENDER_UNLOCK (render);
      ret = gst_pad_event_default (pad, parent, event);
      break;
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}

static gboolean
gst_ttml_render_video_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret = FALSE;
  GstTtmlRender *render;

  render = GST_TTML_RENDER (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_ttml_render_get_videosink_caps (pad, render, filter);
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

/* Called with lock held */
static void
gst_ttml_render_pop_text (GstTtmlRender * render)
{
  g_return_if_fail (GST_IS_TTML_RENDER (render));

  if (render->text_buffer) {
    GST_DEBUG_OBJECT (render, "releasing text buffer %p", render->text_buffer);
    gst_buffer_unref (render->text_buffer);
    render->text_buffer = NULL;
  }

  /* Let the text task know we used that buffer */
  GST_TTML_RENDER_BROADCAST (render);
}

/* We receive text buffers here. If they are out of segment we just ignore them.
   If the buffer is in our segment we keep it internally except if another one
   is already waiting here, in that case we wait that it gets kicked out */
static GstFlowReturn
gst_ttml_render_text_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstTtmlRender *render = NULL;
  gboolean in_seg = FALSE;
  guint64 clip_start = 0, clip_stop = 0;

  render = GST_TTML_RENDER (parent);

  GST_TTML_RENDER_LOCK (render);

  if (render->text_flushing) {
    GST_TTML_RENDER_UNLOCK (render);
    ret = GST_FLOW_FLUSHING;
    GST_LOG_OBJECT (render, "text flushing");
    goto beach;
  }

  if (render->text_eos) {
    GST_TTML_RENDER_UNLOCK (render);
    ret = GST_FLOW_EOS;
    GST_LOG_OBJECT (render, "text EOS");
    goto beach;
  }

  GST_LOG_OBJECT (render, "%" GST_SEGMENT_FORMAT "  BUFFER: ts=%"
      GST_TIME_FORMAT ", end=%" GST_TIME_FORMAT, &render->segment,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer) +
          GST_BUFFER_DURATION (buffer)));

  if (G_LIKELY (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))) {
    GstClockTime stop;

    if (G_LIKELY (GST_BUFFER_DURATION_IS_VALID (buffer)))
      stop = GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
    else
      stop = GST_CLOCK_TIME_NONE;

    in_seg = gst_segment_clip (&render->text_segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buffer), stop, &clip_start, &clip_stop);
  } else {
    in_seg = TRUE;
  }

  if (in_seg) {
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
      GST_BUFFER_TIMESTAMP (buffer) = clip_start;
    else if (GST_BUFFER_DURATION_IS_VALID (buffer))
      GST_BUFFER_DURATION (buffer) = clip_stop - clip_start;

    /* Wait for the previous buffer to go away */
    while (render->text_buffer != NULL) {
      GST_DEBUG ("Pad %s:%s has a buffer queued, waiting",
          GST_DEBUG_PAD_NAME (pad));
      GST_TTML_RENDER_WAIT (render);
      GST_DEBUG ("Pad %s:%s resuming", GST_DEBUG_PAD_NAME (pad));
      if (render->text_flushing) {
        GST_TTML_RENDER_UNLOCK (render);
        ret = GST_FLOW_FLUSHING;
        goto beach;
      }
    }

    if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
      render->text_segment.position = clip_start;

    render->text_buffer = buffer;
    /* That's a new text buffer we need to render */
    render->need_render = TRUE;

    /* in case the video chain is waiting for a text buffer, wake it up */
    GST_TTML_RENDER_BROADCAST (render);
  }

  GST_TTML_RENDER_UNLOCK (render);

beach:

  return ret;
}


/* Free returned string after use. */
static gchar *
gst_ttml_render_color_to_string (GstSubtitleColor color)
{
#if PANGO_VERSION_CHECK (1,38,0)
  return g_strdup_printf ("#%02x%02x%02x%02x",
      color.r, color.g, color.b, color.a);
#else
  return g_strdup_printf ("#%02x%02x%02x", color.r, color.g, color.b);
#endif
}


static GstBuffer *
gst_ttml_render_draw_rectangle (guint width, guint height,
    GstSubtitleColor color)
{
  GstMapInfo map;
  cairo_surface_t *surface;
  cairo_t *cairo_state;
  GstBuffer *buffer = gst_buffer_new_allocate (NULL, 4 * width * height, NULL);

  gst_buffer_map (buffer, &map, GST_MAP_READWRITE);
  surface = cairo_image_surface_create_for_data (map.data,
      CAIRO_FORMAT_ARGB32, width, height, width * 4);
  cairo_state = cairo_create (surface);

  /* clear surface */
  cairo_set_operator (cairo_state, CAIRO_OPERATOR_CLEAR);
  cairo_paint (cairo_state);
  cairo_set_operator (cairo_state, CAIRO_OPERATOR_OVER);

  cairo_save (cairo_state);
  cairo_set_source_rgba (cairo_state, color.r / 255.0, color.g / 255.0,
      color.b / 255.0, color.a / 255.0);
  cairo_paint (cairo_state);
  cairo_restore (cairo_state);
  cairo_destroy (cairo_state);
  cairo_surface_destroy (surface);
  gst_buffer_unmap (buffer, &map);

  return buffer;
}


typedef struct
{
  guint first_char;
  guint last_char;
} TextRange;

static void
_text_range_free (TextRange * range)
{
  g_slice_free (TextRange, range);
}


/* Choose fonts for generic fontnames based upon IMSC1 and HbbTV specs. */
static gchar *
gst_ttml_render_resolve_generic_fontname (const gchar * name)
{
  if ((g_strcmp0 (name, "default") == 0)) {
    return
        g_strdup ("TiresiasScreenfont,Liberation Mono,Courier New,monospace");
  } else if ((g_strcmp0 (name, "monospace") == 0)) {
    return g_strdup ("Letter Gothic,Liberation Mono,Courier New,monospace");
  } else if ((g_strcmp0 (name, "sansSerif") == 0)) {
    return g_strdup ("TiresiasScreenfont,sans");
  } else if ((g_strcmp0 (name, "serif") == 0)) {
    return g_strdup ("serif");
  } else if ((g_strcmp0 (name, "monospaceSansSerif") == 0)) {
    return g_strdup ("Letter Gothic,monospace");
  } else if ((g_strcmp0 (name, "monospaceSerif") == 0)) {
    return g_strdup ("Courier New,Liberation Mono,monospace");
  } else if ((g_strcmp0 (name, "proportionalSansSerif") == 0)) {
    return g_strdup ("TiresiasScreenfont,Arial,Helvetica,Liberation Sans,sans");
  } else if ((g_strcmp0 (name, "proportionalSerif") == 0)) {
    return g_strdup ("serif");
  } else {
    return NULL;
  }
}


static gchar *
gst_ttml_render_get_text_from_buffer (GstBuffer * buf, guint index)
{
  GstMapInfo map;
  GstMemory *mem;
  gchar *buf_text = NULL;

  mem = gst_buffer_get_memory (buf, index);
  if (!mem) {
    GST_CAT_ERROR (ttmlrender_debug, "Failed to access memory at index %u.",
        index);
    return NULL;
  }

  if (!gst_memory_map (mem, &map, GST_MAP_READ)) {
    GST_CAT_ERROR (ttmlrender_debug, "Failed to map memory at index %u.",
        index);
    goto map_fail;
  }

  buf_text = g_strndup ((const gchar *) map.data, map.size);
  if (!g_utf8_validate (buf_text, -1, NULL)) {
    GST_CAT_ERROR (ttmlrender_debug, "Text in buffer us not valid UTF-8");
    g_free (buf_text);
    buf_text = NULL;
  }

  gst_memory_unmap (mem, &map);
map_fail:
  gst_memory_unref (mem);
  return buf_text;
}


typedef struct
{
  const GstSubtitleElement *element;
  gchar *text;
} UnifiedElement;


static void
_unified_element_free (UnifiedElement * unified_element)
{
  g_free (unified_element->text);
  g_slice_free (UnifiedElement, unified_element);
}


typedef struct
{
  GPtrArray *unified_elements;
} UnifiedBlock;


static void
_unified_block_free (UnifiedBlock * unified_block)
{
  g_ptr_array_unref (unified_block->unified_elements);
  g_slice_free (UnifiedBlock, unified_block);
}


static UnifiedElement *
_unified_block_get_element (UnifiedBlock * block, guint index)
{
  if (index >= block->unified_elements->len)
    return NULL;
  else
    return g_ptr_array_index (block->unified_elements, index);
}


static void
gst_ttml_render_handle_whitespace (UnifiedBlock * block)
{
  UnifiedElement *last = NULL;
  UnifiedElement *cur = _unified_block_get_element (block, 0);
  UnifiedElement *next = _unified_block_get_element (block, 1);
  guint i;

  for (i = 2; cur; ++i) {
    if (cur->element->suppress_whitespace) {
      if (!last || (g_strcmp0 (last->text, "\n") == 0)) {
        /* Strip leading whitespace. */
        if (cur->text[0] == 0x20) {
          gchar *tmp = cur->text;
          GST_CAT_LOG (ttmlrender_debug, "Stripping leading whitespace.");
          cur->text = g_strdup (cur->text + 1);
          g_free (tmp);
        }
      }
      if (!next || (g_strcmp0 (next->text, "\n") == 0)) {
        /* Strip trailing whitespace. */
        if (cur->text[strlen (cur->text) - 1] == 0x20) {
          gchar *tmp = cur->text;
          GST_CAT_LOG (ttmlrender_debug, "Stripping trailing whitespace.");
          cur->text = g_strndup (cur->text, strlen (cur->text) - 1);
          g_free (tmp);
        }
      }
    }
    last = cur;
    cur = next;
    next = _unified_block_get_element (block, i);
  }
}


static UnifiedBlock *
gst_ttml_render_unify_block (const GstSubtitleBlock * block, GstBuffer * buf)
{
  guint i;
  UnifiedBlock *ret = g_slice_new0 (UnifiedBlock);
  ret->unified_elements =
      g_ptr_array_new_with_free_func ((GDestroyNotify) _unified_element_free);

  for (i = 0; i < gst_subtitle_block_get_element_count (block); ++i) {
    UnifiedElement *ue = g_slice_new0 (UnifiedElement);
    ue->element = gst_subtitle_block_get_element (block, i);
    ue->text =
        gst_ttml_render_get_text_from_buffer (buf, ue->element->text_index);
    g_ptr_array_add (ret->unified_elements, ue);
  }
  return ret;
}


/* From the elements within @block, generate a string of the subtitle text
 * marked-up using pango-markup. Also, store the ranges of characters belonging
 * to the text of each element in @text_ranges. */
static gchar *
gst_ttml_render_generate_marked_up_string (GstTtmlRender * render,
    const GstSubtitleBlock * block, GstBuffer * text_buf,
    GPtrArray * text_ranges)
{
  gchar *escaped_text, *joined_text, *old_text, *font_family, *font_size,
      *fgcolor;
  const gchar *font_style, *font_weight, *underline;
  guint total_text_length = 0U;
  guint element_count = gst_subtitle_block_get_element_count (block);
  UnifiedBlock *unified_block;
  guint i;

  joined_text = g_strdup ("");
  unified_block = gst_ttml_render_unify_block (block, text_buf);
  gst_ttml_render_handle_whitespace (unified_block);

  for (i = 0; i < element_count; ++i) {
    TextRange *range = g_slice_new0 (TextRange);
    UnifiedElement *unified_element =
        _unified_block_get_element (unified_block, i);

    escaped_text = g_markup_escape_text (unified_element->text, -1);
    GST_CAT_DEBUG (ttmlrender_debug, "Escaped text is: \"%s\"", escaped_text);
    range->first_char = total_text_length;

    fgcolor =
        gst_ttml_render_color_to_string (unified_element->element->
        style_set->color);
    font_size =
        g_strdup_printf ("%u",
        (guint) (round (unified_element->element->style_set->font_size *
                render->height)));
    font_family =
        gst_ttml_render_resolve_generic_fontname (unified_element->
        element->style_set->font_family);
    if (!font_family)
      font_family = g_strdup (unified_element->element->style_set->font_family);
    font_style =
        (unified_element->element->style_set->font_style ==
        GST_SUBTITLE_FONT_STYLE_NORMAL) ? "normal" : "italic";
    font_weight =
        (unified_element->element->style_set->font_weight ==
        GST_SUBTITLE_FONT_WEIGHT_NORMAL) ? "normal" : "bold";
    underline =
        (unified_element->element->style_set->text_decoration ==
        GST_SUBTITLE_TEXT_DECORATION_UNDERLINE) ? "single" : "none";

    old_text = joined_text;
    joined_text = g_strconcat (joined_text,
        "<span "
        "fgcolor=\"", fgcolor, "\" ",
        "font=\"", font_size, "px\" ",
        "font_family=\"", font_family, "\" ",
        "font_style=\"", font_style, "\" ",
        "font_weight=\"", font_weight, "\" ",
        "underline=\"", underline, "\" ", ">", escaped_text, "</span>", NULL);
    GST_CAT_DEBUG (ttmlrender_debug, "Joined text is now: %s", joined_text);

    total_text_length += strlen (unified_element->text);
    range->last_char = total_text_length - 1;
    GST_CAT_DEBUG (ttmlrender_debug,
        "First character index: %u; last character  " "index: %u",
        range->first_char, range->last_char);
    g_ptr_array_insert (text_ranges, i, range);

    g_free (old_text);
    g_free (escaped_text);
    g_free (fgcolor);
    g_free (font_family);
    g_free (font_size);
  }

  _unified_block_free (unified_block);
  return joined_text;
}


/* Render the text in a pango-markup string. */
static GstTtmlRenderRenderedText *
gst_ttml_render_draw_text (GstTtmlRender * render, const gchar * text,
    guint max_width, PangoAlignment alignment, guint line_height,
    guint max_font_size, gboolean wrap)
{
  GstTtmlRenderClass *class;
  GstTtmlRenderRenderedText *ret;
  cairo_surface_t *surface, *cropped_surface;
  cairo_t *cairo_state, *cropped_state;
  GstMapInfo map;
  PangoRectangle logical_rect, ink_rect;
  gint spacing = 0;
  guint buf_width, buf_height;
  gint stride;
  PangoLayoutLine *line;
  PangoRectangle line_extents;
  gint bounding_box_x1, bounding_box_x2, bounding_box_y1, bounding_box_y2;

  ret = g_slice_new0 (GstTtmlRenderRenderedText);
  ret->text_image = gst_ttml_render_rendered_image_new_empty ();

  class = GST_TTML_RENDER_GET_CLASS (render);
  ret->layout = pango_layout_new (class->pango_context);

  pango_layout_set_markup (ret->layout, text, strlen (text));
  GST_CAT_DEBUG (ttmlrender_debug, "Layout text: %s",
      pango_layout_get_text (ret->layout));
  if (wrap) {
    pango_layout_set_width (ret->layout, max_width * PANGO_SCALE);
    pango_layout_set_wrap (ret->layout, PANGO_WRAP_WORD_CHAR);
  } else {
    pango_layout_set_width (ret->layout, -1);
  }

  pango_layout_set_alignment (ret->layout, alignment);
  line = pango_layout_get_line_readonly (ret->layout, 0);
  pango_layout_line_get_pixel_extents (line, NULL, &line_extents);

  GST_CAT_LOG (ttmlrender_debug, "Requested line_height: %u", line_height);
  spacing = line_height - line_extents.height;
  pango_layout_set_spacing (ret->layout, PANGO_SCALE * spacing);
  GST_CAT_LOG (ttmlrender_debug, "Line spacing set to %d",
      pango_layout_get_spacing (ret->layout) / PANGO_SCALE);

  pango_layout_get_pixel_extents (ret->layout, &ink_rect, &logical_rect);
  GST_CAT_DEBUG (ttmlrender_debug, "logical_rect.x: %d   logical_rect.y: %d   "
      "logical_rect.width: %d   logical_rect.height: %d", logical_rect.x,
      logical_rect.y, logical_rect.width, logical_rect.height);

  bounding_box_x1 = MIN (logical_rect.x, ink_rect.x);
  bounding_box_x2 = MAX (logical_rect.x + logical_rect.width,
      ink_rect.x + ink_rect.width);
  bounding_box_y1 = MIN (logical_rect.y, ink_rect.y);
  bounding_box_y2 = MAX (logical_rect.y + logical_rect.height,
      ink_rect.y + ink_rect.height);

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, bounding_box_x2,
      bounding_box_y2);
  cairo_state = cairo_create (surface);
  cairo_set_operator (cairo_state, CAIRO_OPERATOR_CLEAR);
  cairo_paint (cairo_state);
  cairo_set_operator (cairo_state, CAIRO_OPERATOR_OVER);

  /* Render layout. */
  cairo_save (cairo_state);
  pango_cairo_show_layout (cairo_state, ret->layout);
  cairo_restore (cairo_state);

  buf_width = bounding_box_x2 - bounding_box_x1;
  buf_height = (bounding_box_y2 - bounding_box_y1) + spacing;
  GST_CAT_DEBUG (ttmlrender_debug, "Output buffer width: %u  height: %u",
      buf_width, buf_height);

  /* Depending on whether the text is wrapped and its alignment, the image
   * created by rendering a PangoLayout will contain more than just the
   * rendered text: it may also contain blankspace around the rendered text.
   * The following code crops blankspace from around the rendered text,
   * returning only the rendered text itself in a GstBuffer. */
  ret->text_image->image =
      gst_buffer_new_allocate (NULL, 4 * buf_width * buf_height, NULL);
  gst_buffer_memset (ret->text_image->image, 0, 0U, 4 * buf_width * buf_height);
  gst_buffer_map (ret->text_image->image, &map, GST_MAP_READWRITE);

  stride = cairo_format_stride_for_width (CAIRO_FORMAT_ARGB32, buf_width);
  cropped_surface =
      cairo_image_surface_create_for_data (map.data, CAIRO_FORMAT_ARGB32,
      buf_width, buf_height, stride);
  cropped_state = cairo_create (cropped_surface);
  cairo_set_source_surface (cropped_state, surface, -bounding_box_x1,
      -(bounding_box_y1 - spacing / 2.0));
  cairo_rectangle (cropped_state, 0, 0, buf_width, buf_height);
  cairo_fill (cropped_state);

  cairo_destroy (cairo_state);
  cairo_surface_destroy (surface);
  cairo_destroy (cropped_state);
  cairo_surface_destroy (cropped_surface);
  gst_buffer_unmap (ret->text_image->image, &map);

  ret->text_image->width = buf_width;
  ret->text_image->height = buf_height;
  ret->horiz_offset = bounding_box_x1;

  return ret;
}


/* If any of an array of elements has line wrapping enabled, return TRUE. */
static gboolean
gst_ttml_render_elements_are_wrapped (GPtrArray * elements)
{
  GstSubtitleElement *element;
  guint i;

  for (i = 0; i < elements->len; ++i) {
    element = g_ptr_array_index (elements, i);
    if (element->style_set->wrap_option == GST_SUBTITLE_WRAPPING_ON)
      return TRUE;
  }

  return FALSE;
}


/* Return the maximum font size used in an array of elements. */
static gdouble
gst_ttml_render_get_max_font_size (GPtrArray * elements)
{
  GstSubtitleElement *element;
  guint i;
  gdouble max_size = 0.0;

  for (i = 0; i < elements->len; ++i) {
    element = g_ptr_array_index (elements, i);
    if (element->style_set->font_size > max_size)
      max_size = element->style_set->font_size;
  }

  return max_size;
}


static GstTtmlRenderRenderedImage *
gst_ttml_render_rendered_image_new (GstBuffer * image, gint x, gint y,
    guint width, guint height)
{
  GstTtmlRenderRenderedImage *ret;

  ret = g_slice_new0 (GstTtmlRenderRenderedImage);

  ret->image = image;
  ret->x = x;
  ret->y = y;
  ret->width = width;
  ret->height = height;

  return ret;
}


static GstTtmlRenderRenderedImage *
gst_ttml_render_rendered_image_new_empty (void)
{
  return gst_ttml_render_rendered_image_new (NULL, 0, 0, 0, 0);
}


static inline GstTtmlRenderRenderedImage *
gst_ttml_render_rendered_image_copy (GstTtmlRenderRenderedImage * image)
{
  GstTtmlRenderRenderedImage *ret = g_slice_new0 (GstTtmlRenderRenderedImage);

  ret->image = gst_buffer_ref (image->image);
  ret->x = image->x;
  ret->y = image->y;
  ret->width = image->width;
  ret->height = image->height;

  return ret;
}


static void
gst_ttml_render_rendered_image_free (GstTtmlRenderRenderedImage * image)
{
  if (!image)
    return;
  gst_buffer_unref (image->image);
  g_slice_free (GstTtmlRenderRenderedImage, image);
}


/* The order of arguments is significant: @image2 will be rendered on top of
 * @image1. */
static GstTtmlRenderRenderedImage *
gst_ttml_render_rendered_image_combine (GstTtmlRenderRenderedImage * image1,
    GstTtmlRenderRenderedImage * image2)
{
  GstTtmlRenderRenderedImage *ret;
  GstMapInfo map1, map2, map_dest;
  cairo_surface_t *sfc1, *sfc2, *sfc_dest;
  cairo_t *state_dest;

  if (image1 && !image2)
    return gst_ttml_render_rendered_image_copy (image1);
  if (image2 && !image1)
    return gst_ttml_render_rendered_image_copy (image2);

  ret = g_slice_new0 (GstTtmlRenderRenderedImage);

  /* Work out dimensions of combined image. */
  ret->x = MIN (image1->x, image2->x);
  ret->y = MIN (image1->y, image2->y);
  ret->width = MAX (image1->x + image1->width, image2->x + image2->width)
      - ret->x;
  ret->height = MAX (image1->y + image1->height, image2->y + image2->height)
      - ret->y;

  GST_CAT_LOG (ttmlrender_debug, "Dimensions of combined image:  x:%u  y:%u  "
      "width:%u  height:%u", ret->x, ret->y, ret->width, ret->height);

  /* Create cairo_surface from src images. */
  gst_buffer_map (image1->image, &map1, GST_MAP_READ);
  sfc1 =
      cairo_image_surface_create_for_data (map1.data, CAIRO_FORMAT_ARGB32,
      image1->width, image1->height,
      cairo_format_stride_for_width (CAIRO_FORMAT_ARGB32, image1->width));

  gst_buffer_map (image2->image, &map2, GST_MAP_READ);
  sfc2 =
      cairo_image_surface_create_for_data (map2.data, CAIRO_FORMAT_ARGB32,
      image2->width, image2->height,
      cairo_format_stride_for_width (CAIRO_FORMAT_ARGB32, image2->width));

  /* Create cairo_surface for resultant image. */
  ret->image = gst_buffer_new_allocate (NULL, 4 * ret->width * ret->height,
      NULL);
  gst_buffer_memset (ret->image, 0, 0U, 4 * ret->width * ret->height);
  gst_buffer_map (ret->image, &map_dest, GST_MAP_READWRITE);
  sfc_dest =
      cairo_image_surface_create_for_data (map_dest.data, CAIRO_FORMAT_ARGB32,
      ret->width, ret->height,
      cairo_format_stride_for_width (CAIRO_FORMAT_ARGB32, ret->width));
  state_dest = cairo_create (sfc_dest);

  /* Blend image1 into destination surface. */
  cairo_set_source_surface (state_dest, sfc1, image1->x - ret->x,
      image1->y - ret->y);
  cairo_rectangle (state_dest, image1->x - ret->x, image1->y - ret->y,
      image1->width, image1->height);
  cairo_fill (state_dest);

  /* Blend image2 into destination surface. */
  cairo_set_source_surface (state_dest, sfc2, image2->x - ret->x,
      image2->y - ret->y);
  cairo_rectangle (state_dest, image2->x - ret->x, image2->y - ret->y,
      image2->width, image2->height);
  cairo_fill (state_dest);

  /* Return destination image. */
  cairo_destroy (state_dest);
  cairo_surface_destroy (sfc1);
  cairo_surface_destroy (sfc2);
  cairo_surface_destroy (sfc_dest);
  gst_buffer_unmap (image1->image, &map1);
  gst_buffer_unmap (image2->image, &map2);
  gst_buffer_unmap (ret->image, &map_dest);

  return ret;
}


static GstTtmlRenderRenderedImage *
gst_ttml_render_rendered_image_crop (GstTtmlRenderRenderedImage * image,
    gint x, gint y, guint width, guint height)
{
  GstTtmlRenderRenderedImage *ret;
  GstMapInfo map_src, map_dest;
  cairo_surface_t *sfc_src, *sfc_dest;
  cairo_t *state_dest;

  if ((x <= image->x) && (y <= image->y) && (width >= image->width)
      && (height >= image->height))
    return gst_ttml_render_rendered_image_copy (image);

  if (image->x >= (x + (gint) width)
      || (image->x + (gint) image->width) <= x
      || image->y >= (y + (gint) height)
      || (image->y + (gint) image->height) <= y) {
    GST_CAT_WARNING (ttmlrender_debug,
        "Crop rectangle doesn't intersect image.");
    return NULL;
  }

  ret = g_slice_new0 (GstTtmlRenderRenderedImage);

  ret->x = MAX (image->x, x);
  ret->y = MAX (image->y, y);
  ret->width = MIN ((image->x + image->width) - ret->x, (x + width) - ret->x);
  ret->height = MIN ((image->y + image->height) - ret->y,
      (y + height) - ret->y);

  GST_CAT_LOG (ttmlrender_debug, "Dimensions of cropped image:  x:%u  y:%u  "
      "width:%u  height:%u", ret->x, ret->y, ret->width, ret->height);

  /* Create cairo_surface from src image. */
  gst_buffer_map (image->image, &map_src, GST_MAP_READ);
  sfc_src =
      cairo_image_surface_create_for_data (map_src.data, CAIRO_FORMAT_ARGB32,
      image->width, image->height,
      cairo_format_stride_for_width (CAIRO_FORMAT_ARGB32, image->width));

  /* Create cairo_surface for cropped image. */
  ret->image = gst_buffer_new_allocate (NULL, 4 * ret->width * ret->height,
      NULL);
  gst_buffer_memset (ret->image, 0, 0U, 4 * ret->width * ret->height);
  gst_buffer_map (ret->image, &map_dest, GST_MAP_READWRITE);
  sfc_dest =
      cairo_image_surface_create_for_data (map_dest.data, CAIRO_FORMAT_ARGB32,
      ret->width, ret->height,
      cairo_format_stride_for_width (CAIRO_FORMAT_ARGB32, ret->width));
  state_dest = cairo_create (sfc_dest);

  /* Copy section of image1 into destination surface. */
  cairo_set_source_surface (state_dest, sfc_src, (image->x - ret->x),
      (image->y - ret->y));
  cairo_rectangle (state_dest, 0, 0, ret->width, ret->height);
  cairo_fill (state_dest);

  cairo_destroy (state_dest);
  cairo_surface_destroy (sfc_src);
  cairo_surface_destroy (sfc_dest);
  gst_buffer_unmap (image->image, &map_src);
  gst_buffer_unmap (ret->image, &map_dest);

  return ret;
}


static gboolean
gst_ttml_render_color_is_transparent (GstSubtitleColor * color)
{
  return (color->a == 0);
}


/* Render the background rectangles to be placed behind each element. */
static GstTtmlRenderRenderedImage *
gst_ttml_render_render_element_backgrounds (const GstSubtitleBlock * block,
    GPtrArray * char_ranges, PangoLayout * layout, guint origin_x,
    guint origin_y, guint line_height, guint line_padding, guint horiz_offset)
{
  gint first_line, last_line, cur_line;
  guint padding;
  PangoLayoutLine *line;
  PangoRectangle first_char_pos, last_char_pos, line_extents;
  TextRange *range;
  const GstSubtitleElement *element;
  guint rect_width;
  GstBuffer *rectangle;
  guint first_char_start, last_char_end;
  guint i;
  GstTtmlRenderRenderedImage *ret = NULL;

  for (i = 0; i < char_ranges->len; ++i) {
    range = g_ptr_array_index (char_ranges, i);
    element = gst_subtitle_block_get_element (block, i);

    GST_CAT_LOG (ttmlrender_debug, "First char index: %u   Last char index: %u",
        range->first_char, range->last_char);
    pango_layout_index_to_pos (layout, range->first_char, &first_char_pos);
    pango_layout_index_to_pos (layout, range->last_char, &last_char_pos);
    pango_layout_index_to_line_x (layout, range->first_char, 1,
        &first_line, NULL);
    pango_layout_index_to_line_x (layout, range->last_char, 0,
        &last_line, NULL);

    first_char_start = PANGO_PIXELS (first_char_pos.x) - horiz_offset;
    last_char_end = PANGO_PIXELS (last_char_pos.x + last_char_pos.width)
        - horiz_offset;

    GST_CAT_LOG (ttmlrender_debug, "First char start: %u  Last char end: %u",
        first_char_start, last_char_end);
    GST_CAT_LOG (ttmlrender_debug, "First line: %u  Last line: %u", first_line,
        last_line);

    for (cur_line = first_line; cur_line <= last_line; ++cur_line) {
      guint line_start, line_end;
      guint area_start, area_end;
      gint first_char_index;
      PangoRectangle line_pos;
      padding = 0;

      line = pango_layout_get_line (layout, cur_line);
      pango_layout_line_get_pixel_extents (line, NULL, &line_extents);

      pango_layout_line_x_to_index (line, 0, &first_char_index, NULL);
      pango_layout_index_to_pos (layout, first_char_index, &line_pos);
      GST_CAT_LOG (ttmlrender_debug, "First char index:%d  position_X:%d  "
          "position_Y:%d", first_char_index, PANGO_PIXELS (line_pos.x),
          PANGO_PIXELS (line_pos.y));

      line_start = PANGO_PIXELS (line_pos.x) - horiz_offset;
      line_end = (PANGO_PIXELS (line_pos.x) + line_extents.width)
          - horiz_offset;

      GST_CAT_LOG (ttmlrender_debug, "line_extents.x:%d  line_extents.y:%d  "
          "line_extents.width:%d  line_extents.height:%d", line_extents.x,
          line_extents.y, line_extents.width, line_extents.height);
      GST_CAT_LOG (ttmlrender_debug, "cur_line:%u  line start:%u  line end:%u "
          "first_char_start: %u  last_char_end: %u", cur_line, line_start,
          line_end, first_char_start, last_char_end);

      if ((cur_line == first_line) && (first_char_start != line_start)) {
        area_start = first_char_start + line_padding;
        GST_CAT_LOG (ttmlrender_debug,
            "First line, but there is preceding text in line.");
      } else {
        GST_CAT_LOG (ttmlrender_debug,
            "Area contains first text on the line; adding padding...");
        ++padding;
        area_start = line_start;
      }

      if ((cur_line == last_line) && (last_char_end != line_end)) {
        GST_CAT_LOG (ttmlrender_debug,
            "Last line, but there is following text in line.");
        area_end = last_char_end + line_padding;
      } else {
        GST_CAT_LOG (ttmlrender_debug,
            "Area contains last text on the line; adding padding...");
        ++padding;
        area_end = line_end + (2 * line_padding);
      }

      rect_width = (area_end - area_start);

      if (rect_width > 0) {     /* <br>s will result in zero-width rectangle */
        GstTtmlRenderRenderedImage *image, *tmp;
        rectangle = gst_ttml_render_draw_rectangle (rect_width, line_height,
            element->style_set->background_color);
        image = gst_ttml_render_rendered_image_new (rectangle,
            origin_x + area_start,
            origin_y + (cur_line * line_height), rect_width, line_height);
        tmp = ret;
        ret = gst_ttml_render_rendered_image_combine (ret, image);
        if (tmp)
          gst_ttml_render_rendered_image_free (tmp);
        gst_ttml_render_rendered_image_free (image);
      }
    }
  }

  return ret;
}


static PangoAlignment
gst_ttml_render_get_alignment (GstSubtitleStyleSet * style_set)
{
  PangoAlignment align = PANGO_ALIGN_LEFT;

  switch (style_set->multi_row_align) {
    case GST_SUBTITLE_MULTI_ROW_ALIGN_START:
      align = PANGO_ALIGN_LEFT;
      break;
    case GST_SUBTITLE_MULTI_ROW_ALIGN_CENTER:
      align = PANGO_ALIGN_CENTER;
      break;
    case GST_SUBTITLE_MULTI_ROW_ALIGN_END:
      align = PANGO_ALIGN_RIGHT;
      break;
    case GST_SUBTITLE_MULTI_ROW_ALIGN_AUTO:
      switch (style_set->text_align) {
        case GST_SUBTITLE_TEXT_ALIGN_START:
        case GST_SUBTITLE_TEXT_ALIGN_LEFT:
          align = PANGO_ALIGN_LEFT;
          break;
        case GST_SUBTITLE_TEXT_ALIGN_CENTER:
          align = PANGO_ALIGN_CENTER;
          break;
        case GST_SUBTITLE_TEXT_ALIGN_END:
        case GST_SUBTITLE_TEXT_ALIGN_RIGHT:
          align = PANGO_ALIGN_RIGHT;
          break;
        default:
          GST_CAT_ERROR (ttmlrender_debug, "Illegal textAlign value (%d)",
              style_set->text_align);
          break;
      }
      break;
    default:
      GST_CAT_ERROR (ttmlrender_debug, "Illegal multiRowAlign value (%d)",
          style_set->multi_row_align);
      break;
  }
  return align;
}


static GstTtmlRenderRenderedImage *
gst_ttml_render_stitch_blocks (GList * blocks)
{
  guint vert_offset = 0;
  GList *block_entry;
  GstTtmlRenderRenderedImage *ret = NULL;

  for (block_entry = g_list_first (blocks); block_entry;
      block_entry = block_entry->next) {
    GstTtmlRenderRenderedImage *block, *tmp;
    block = (GstTtmlRenderRenderedImage *) block_entry->data;
    tmp = ret;

    block->y += vert_offset;
    GST_CAT_LOG (ttmlrender_debug, "Rendering block at vertical offset %u",
        vert_offset);
    vert_offset = block->y + block->height;
    ret = gst_ttml_render_rendered_image_combine (ret, block);
    if (tmp)
      gst_ttml_render_rendered_image_free (tmp);
  }

  if (ret) {
    GST_CAT_LOG (ttmlrender_debug, "Height of stitched image: %u", ret->height);
    ret->image = gst_buffer_make_writable (ret->image);
  }
  return ret;
}


static void
gst_ttml_render_rendered_text_free (GstTtmlRenderRenderedText * text)
{
  if (text->text_image)
    gst_ttml_render_rendered_image_free (text->text_image);
  if (text->layout)
    g_object_unref (text->layout);
  g_slice_free (GstTtmlRenderRenderedText, text);
}


static GstTtmlRenderRenderedImage *
gst_ttml_render_render_text_block (GstTtmlRender * render,
    const GstSubtitleBlock * block, GstBuffer * text_buf, guint width,
    gboolean overflow)
{
  GPtrArray *char_ranges =
      g_ptr_array_new_with_free_func ((GDestroyNotify) _text_range_free);
  gchar *marked_up_string;
  PangoAlignment alignment;
  guint max_font_size;
  guint line_height;
  guint line_padding;
  gint text_offset = 0;
  GstTtmlRenderRenderedText *rendered_text;
  GstTtmlRenderRenderedImage *backgrounds = NULL;
  GstTtmlRenderRenderedImage *ret;

  /* Join text from elements to form a single marked-up string. */
  marked_up_string = gst_ttml_render_generate_marked_up_string (render, block,
      text_buf, char_ranges);

  max_font_size = (guint) (gst_ttml_render_get_max_font_size (block->elements)
      * render->height);
  GST_CAT_DEBUG (ttmlrender_debug, "Max font size: %u", max_font_size);
  line_height = (guint) round (block->style_set->line_height * max_font_size);

  line_padding = (guint) (block->style_set->line_padding * render->width);
  alignment = gst_ttml_render_get_alignment (block->style_set);

  /* Render text to buffer. */
  rendered_text = gst_ttml_render_draw_text (render, marked_up_string,
      (width - (2 * line_padding)), alignment, line_height, max_font_size,
      gst_ttml_render_elements_are_wrapped (block->elements));

  switch (block->style_set->text_align) {
    case GST_SUBTITLE_TEXT_ALIGN_START:
    case GST_SUBTITLE_TEXT_ALIGN_LEFT:
      text_offset = line_padding;
      break;
    case GST_SUBTITLE_TEXT_ALIGN_CENTER:
      text_offset = ((gint) width - rendered_text->text_image->width);
      text_offset /= 2;
      break;
    case GST_SUBTITLE_TEXT_ALIGN_END:
    case GST_SUBTITLE_TEXT_ALIGN_RIGHT:
      text_offset = (gint) width
          - (rendered_text->text_image->width + line_padding);
      break;
  }

  rendered_text->text_image->x = text_offset;

  /* Render background rectangles, if any. */
  backgrounds = gst_ttml_render_render_element_backgrounds (block, char_ranges,
      rendered_text->layout, text_offset - line_padding, 0,
      (guint) round (block->style_set->line_height * max_font_size),
      line_padding, rendered_text->horiz_offset);

  /* Render block background, if non-transparent. */
  if (!gst_ttml_render_color_is_transparent (&block->style_set->
          background_color)) {
    GstTtmlRenderRenderedImage *block_background;
    GstTtmlRenderRenderedImage *tmp = backgrounds;

    GstBuffer *block_bg_image = gst_ttml_render_draw_rectangle (width,
        backgrounds->height, block->style_set->background_color);
    block_background = gst_ttml_render_rendered_image_new (block_bg_image, 0,
        0, width, backgrounds->height);
    backgrounds = gst_ttml_render_rendered_image_combine (block_background,
        backgrounds);
    gst_ttml_render_rendered_image_free (tmp);
    gst_ttml_render_rendered_image_free (block_background);
  }

  /* Combine text and background images. */
  ret = gst_ttml_render_rendered_image_combine (backgrounds,
      rendered_text->text_image);
  gst_ttml_render_rendered_image_free (backgrounds);
  gst_ttml_render_rendered_text_free (rendered_text);

  g_free (marked_up_string);
  g_ptr_array_unref (char_ranges);
  GST_CAT_DEBUG (ttmlrender_debug, "block width: %u   block height: %u",
      ret->width, ret->height);
  return ret;
}


static GstVideoOverlayComposition *
gst_ttml_render_compose_overlay (GstTtmlRenderRenderedImage * image)
{
  GstVideoOverlayRectangle *rectangle;
  GstVideoOverlayComposition *ret = NULL;

  gst_buffer_add_video_meta (image->image, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_RGB, image->width, image->height);

  rectangle = gst_video_overlay_rectangle_new_raw (image->image, image->x,
      image->y, image->width, image->height,
      GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);

  ret = gst_video_overlay_composition_new (rectangle);
  gst_video_overlay_rectangle_unref (rectangle);
  return ret;
}


static GstVideoOverlayComposition *
gst_ttml_render_render_text_region (GstTtmlRender * render,
    GstSubtitleRegion * region, GstBuffer * text_buf)
{
  GList *blocks = NULL;
  guint region_x, region_y, region_width, region_height;
  guint window_x, window_y, window_width, window_height;
  guint padding_start, padding_end, padding_before, padding_after;
  GstTtmlRenderRenderedImage *region_image = NULL;
  GstTtmlRenderRenderedImage *blocks_image;
  GstVideoOverlayComposition *ret = NULL;
  guint i;

  region_width = (guint) (round (region->style_set->extent_w * render->width));
  region_height =
      (guint) (round (region->style_set->extent_h * render->height));
  region_x = (guint) (round (region->style_set->origin_x * render->width));
  region_y = (guint) (round (region->style_set->origin_y * render->height));

  padding_start =
      (guint) (round (region->style_set->padding_start * render->width));
  padding_end =
      (guint) (round (region->style_set->padding_end * render->width));
  padding_before =
      (guint) (round (region->style_set->padding_before * render->height));
  padding_after =
      (guint) (round (region->style_set->padding_after * render->height));

  /* "window" here refers to the section of the region that we're allowed to
   * render into, i.e., the region minus padding. */
  window_x = region_x + padding_start;
  window_y = region_y + padding_before;
  window_width = region_width - (padding_start + padding_end);
  window_height = region_height - (padding_before + padding_after);

  GST_CAT_DEBUG (ttmlrender_debug,
      "Padding: start: %u  end: %u  before: %u  after: %u",
      padding_start, padding_end, padding_before, padding_after);

  /* Render region background, if non-transparent. */
  if (!gst_ttml_render_color_is_transparent (&region->style_set->
          background_color)) {
    GstBuffer *bg_rect;

    bg_rect = gst_ttml_render_draw_rectangle (region_width, region_height,
        region->style_set->background_color);
    region_image = gst_ttml_render_rendered_image_new (bg_rect, region_x,
        region_y, region_width, region_height);
  }

  /* Render each block and append to list. */
  for (i = 0; i < gst_subtitle_region_get_block_count (region); ++i) {
    const GstSubtitleBlock *block;
    GstTtmlRenderRenderedImage *rendered_block;

    block = gst_subtitle_region_get_block (region, i);
    rendered_block = gst_ttml_render_render_text_block (render, block, text_buf,
        window_width, TRUE);

    blocks = g_list_append (blocks, rendered_block);
  }

  if (blocks) {
    GstTtmlRenderRenderedImage *tmp;

    blocks_image = gst_ttml_render_stitch_blocks (blocks);
    g_list_free_full (blocks,
        (GDestroyNotify) gst_ttml_render_rendered_image_free);
    blocks_image->x += window_x;

    switch (region->style_set->display_align) {
      case GST_SUBTITLE_DISPLAY_ALIGN_BEFORE:
        blocks_image->y = window_y;
        break;
      case GST_SUBTITLE_DISPLAY_ALIGN_CENTER:
        blocks_image->y = region_y + ((gint) ((region_height + padding_before)
                - (padding_after + blocks_image->height))) / 2;
        break;
      case GST_SUBTITLE_DISPLAY_ALIGN_AFTER:
        blocks_image->y = (region_y + region_height)
            - (padding_after + blocks_image->height);
        break;
    }

    if ((region->style_set->overflow == GST_SUBTITLE_OVERFLOW_MODE_HIDDEN)
        && ((blocks_image->height > window_height)
            || (blocks_image->width > window_width))) {
      GstTtmlRenderRenderedImage *tmp = blocks_image;
      blocks_image = gst_ttml_render_rendered_image_crop (blocks_image,
          window_x, window_y, window_width, window_height);
      gst_ttml_render_rendered_image_free (tmp);
    }

    tmp = region_image;
    if (region_image || blocks_image) {
      region_image =
          gst_ttml_render_rendered_image_combine (region_image, blocks_image);
    } else {
      GST_CAT_DEBUG (ttmlrender_debug, "Nothing to render");
      return NULL;
    }

    if (tmp)
      gst_ttml_render_rendered_image_free (tmp);
    gst_ttml_render_rendered_image_free (blocks_image);
  }

  if (region_image) {
    GST_CAT_DEBUG (ttmlrender_debug, "Height of rendered region: %u",
        region_image->height);
    ret = gst_ttml_render_compose_overlay (region_image);
    gst_ttml_render_rendered_image_free (region_image);
  }
  return ret;
}


static GstFlowReturn
gst_ttml_render_video_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstTtmlRender *render;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean in_seg = FALSE;
  guint64 start, stop, clip_start = 0, clip_stop = 0;
  gchar *text = NULL;

  render = GST_TTML_RENDER (parent);

  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    goto missing_timestamp;

  /* ignore buffers that are outside of the current segment */
  start = GST_BUFFER_TIMESTAMP (buffer);

  if (!GST_BUFFER_DURATION_IS_VALID (buffer)) {
    stop = GST_CLOCK_TIME_NONE;
  } else {
    stop = start + GST_BUFFER_DURATION (buffer);
  }

  GST_LOG_OBJECT (render, "%" GST_SEGMENT_FORMAT "  BUFFER: ts=%"
      GST_TIME_FORMAT ", end=%" GST_TIME_FORMAT, &render->segment,
      GST_TIME_ARGS (start), GST_TIME_ARGS (stop));

  /* segment_clip() will adjust start unconditionally to segment_start if
   * no stop time is provided, so handle this ourselves */
  if (stop == GST_CLOCK_TIME_NONE && start < render->segment.start)
    goto out_of_segment;

  in_seg = gst_segment_clip (&render->segment, GST_FORMAT_TIME, start, stop,
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
      stop = start + gst_util_uint64_scale_int (GST_SECOND,
          render->info.fps_d, render->info.fps_n);
    } else {
      GST_LOG_OBJECT (render, "no duration, assuming minimal duration");
      stop = start + 1;         /* we need to assume some interval */
    }
  }

  gst_object_sync_values (GST_OBJECT (render), GST_BUFFER_TIMESTAMP (buffer));

wait_for_text_buf:

  GST_TTML_RENDER_LOCK (render);

  if (render->video_flushing)
    goto flushing;

  if (render->video_eos)
    goto have_eos;

  /* Text pad not linked; push input video frame */
  if (!render->text_linked) {
    GST_LOG_OBJECT (render, "Text pad not linked");
    GST_TTML_RENDER_UNLOCK (render);
    ret = gst_pad_push (render->srcpad, buffer);
    goto not_linked;
  }

  /* Text pad linked, check if we have a text buffer queued */
  if (render->text_buffer) {
    gboolean pop_text = FALSE, valid_text_time = TRUE;
    GstClockTime text_start = GST_CLOCK_TIME_NONE;
    GstClockTime text_end = GST_CLOCK_TIME_NONE;
    GstClockTime text_running_time = GST_CLOCK_TIME_NONE;
    GstClockTime text_running_time_end = GST_CLOCK_TIME_NONE;
    GstClockTime vid_running_time, vid_running_time_end;

    /* if the text buffer isn't stamped right, pop it off the
     * queue and display it for the current video frame only */
    if (!GST_BUFFER_TIMESTAMP_IS_VALID (render->text_buffer) ||
        !GST_BUFFER_DURATION_IS_VALID (render->text_buffer)) {
      GST_WARNING_OBJECT (render,
          "Got text buffer with invalid timestamp or duration");
      pop_text = TRUE;
      valid_text_time = FALSE;
    } else {
      text_start = GST_BUFFER_TIMESTAMP (render->text_buffer);
      text_end = text_start + GST_BUFFER_DURATION (render->text_buffer);
    }

    vid_running_time =
        gst_segment_to_running_time (&render->segment, GST_FORMAT_TIME, start);
    vid_running_time_end =
        gst_segment_to_running_time (&render->segment, GST_FORMAT_TIME, stop);

    /* If timestamp and duration are valid */
    if (valid_text_time) {
      text_running_time =
          gst_segment_to_running_time (&render->text_segment,
          GST_FORMAT_TIME, text_start);
      text_running_time_end =
          gst_segment_to_running_time (&render->text_segment,
          GST_FORMAT_TIME, text_end);
    }

    GST_LOG_OBJECT (render, "T: %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
        GST_TIME_ARGS (text_running_time),
        GST_TIME_ARGS (text_running_time_end));
    GST_LOG_OBJECT (render, "V: %" GST_TIME_FORMAT " - %" GST_TIME_FORMAT,
        GST_TIME_ARGS (vid_running_time), GST_TIME_ARGS (vid_running_time_end));

    /* Text too old or in the future */
    if (valid_text_time && text_running_time_end <= vid_running_time) {
      /* text buffer too old, get rid of it and do nothing  */
      GST_LOG_OBJECT (render, "text buffer too old, popping");
      pop_text = FALSE;
      gst_ttml_render_pop_text (render);
      GST_TTML_RENDER_UNLOCK (render);
      goto wait_for_text_buf;
    } else if (valid_text_time && vid_running_time_end <= text_running_time) {
      GST_LOG_OBJECT (render, "text in future, pushing video buf");
      GST_TTML_RENDER_UNLOCK (render);
      /* Push the video frame */
      ret = gst_pad_push (render->srcpad, buffer);
    } else {
      if (render->need_render) {
        GstSubtitleRegion *region = NULL;
        GstSubtitleMeta *subtitle_meta = NULL;
        guint i;

        if (render->compositions) {
          g_list_free_full (render->compositions,
              (GDestroyNotify) gst_video_overlay_composition_unref);
          render->compositions = NULL;
        }

        subtitle_meta = gst_buffer_get_subtitle_meta (render->text_buffer);
        if (!subtitle_meta) {
          GST_CAT_WARNING (ttmlrender_debug, "Failed to get subtitle meta.");
        } else {
          for (i = 0; i < subtitle_meta->regions->len; ++i) {
            GstVideoOverlayComposition *composition;
            region = g_ptr_array_index (subtitle_meta->regions, i);
            composition = gst_ttml_render_render_text_region (render, region,
                render->text_buffer);
            if (composition) {
              render->compositions = g_list_append (render->compositions,
                  composition);
            }
          }
        }
        render->need_render = FALSE;
      }

      GST_TTML_RENDER_UNLOCK (render);
      ret = gst_ttml_render_push_frame (render, buffer);

      if (valid_text_time && text_running_time_end <= vid_running_time_end) {
        GST_LOG_OBJECT (render, "text buffer not needed any longer");
        pop_text = TRUE;
      }
    }
    if (pop_text) {
      GST_TTML_RENDER_LOCK (render);
      gst_ttml_render_pop_text (render);
      GST_TTML_RENDER_UNLOCK (render);
    }
  } else {
    gboolean wait_for_text_buf = TRUE;

    if (render->text_eos)
      wait_for_text_buf = FALSE;

    if (!render->wait_text)
      wait_for_text_buf = FALSE;

    /* Text pad linked, but no text buffer available - what now? */
    if (render->text_segment.format == GST_FORMAT_TIME) {
      GstClockTime text_start_running_time, text_position_running_time;
      GstClockTime vid_running_time;

      vid_running_time =
          gst_segment_to_running_time (&render->segment, GST_FORMAT_TIME,
          GST_BUFFER_TIMESTAMP (buffer));
      text_start_running_time =
          gst_segment_to_running_time (&render->text_segment,
          GST_FORMAT_TIME, render->text_segment.start);
      text_position_running_time =
          gst_segment_to_running_time (&render->text_segment,
          GST_FORMAT_TIME, render->text_segment.position);

      if ((GST_CLOCK_TIME_IS_VALID (text_start_running_time) &&
              vid_running_time < text_start_running_time) ||
          (GST_CLOCK_TIME_IS_VALID (text_position_running_time) &&
              vid_running_time < text_position_running_time)) {
        wait_for_text_buf = FALSE;
      }
    }

    if (wait_for_text_buf) {
      GST_DEBUG_OBJECT (render, "no text buffer, need to wait for one");
      GST_TTML_RENDER_WAIT (render);
      GST_DEBUG_OBJECT (render, "resuming");
      GST_TTML_RENDER_UNLOCK (render);
      goto wait_for_text_buf;
    } else {
      GST_TTML_RENDER_UNLOCK (render);
      GST_LOG_OBJECT (render, "no need to wait for a text buffer");
      ret = gst_pad_push (render->srcpad, buffer);
    }
  }

not_linked:
  g_free (text);

  /* Update position */
  render->segment.position = clip_start;

  return ret;

missing_timestamp:
  {
    GST_WARNING_OBJECT (render, "buffer without timestamp, discarding");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

flushing:
  {
    GST_TTML_RENDER_UNLOCK (render);
    GST_DEBUG_OBJECT (render, "flushing, discarding buffer");
    gst_buffer_unref (buffer);
    return GST_FLOW_FLUSHING;
  }
have_eos:
  {
    GST_TTML_RENDER_UNLOCK (render);
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

static GstStateChangeReturn
gst_ttml_render_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstTtmlRender *render = GST_TTML_RENDER (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_TTML_RENDER_LOCK (render);
      render->text_flushing = TRUE;
      render->video_flushing = TRUE;
      /* pop_text will broadcast on the GCond and thus also make the video
       * chain exit if it's waiting for a text buffer */
      gst_ttml_render_pop_text (render);
      GST_TTML_RENDER_UNLOCK (render);
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_TTML_RENDER_LOCK (render);
      render->text_flushing = FALSE;
      render->video_flushing = FALSE;
      render->video_eos = FALSE;
      render->text_eos = FALSE;
      gst_segment_init (&render->segment, GST_FORMAT_TIME);
      gst_segment_init (&render->text_segment, GST_FORMAT_TIME);
      GST_TTML_RENDER_UNLOCK (render);
      break;
    default:
      break;
  }

  return ret;
}
