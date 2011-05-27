/* GStreamer
 * Copyright (C) <2011> Stefan Kost <ensonic@users.sf.net>
 *
 * gstbasescope.h: base class for audio visualisation elements
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:gstbasescope
 *
 * A basclass for scopes. Takes care of re-fitting the audio-rate to video-rate.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "gstbasescope.h"

GST_DEBUG_CATEGORY_STATIC (base_scope_debug);
#define GST_CAT_DEFAULT (base_scope_debug)

static GstBaseTransformClass *parent_class = NULL;

static void gst_base_scope_class_init (GstBaseScopeClass * klass);
static void gst_base_scope_init (GstBaseScope * scope,
    GstBaseScopeClass * g_class);
static void gst_base_scope_dispose (GObject * object);

static gboolean gst_base_scope_src_negotiate (GstBaseScope * scope);
static gboolean gst_base_scope_src_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_base_scope_sink_setcaps (GstPad * pad, GstCaps * caps);

static GstFlowReturn gst_base_scope_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn gst_base_scope_change_state (GstElement * element,
    GstStateChange transition);

GType
gst_base_scope_get_type (void)
{
  static volatile gsize base_scope_type = 0;

  if (g_once_init_enter (&base_scope_type)) {
    static const GTypeInfo base_scope_info = {
      sizeof (GstBaseScopeClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_base_scope_class_init,
      NULL,
      NULL,
      sizeof (GstBaseScope),
      0,
      (GInstanceInitFunc) gst_base_scope_init,
    };
    GType _type;

    _type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseScope", &base_scope_info, G_TYPE_FLAG_ABSTRACT);
    g_once_init_leave (&base_scope_type, _type);
  }
  return (GType) base_scope_type;
}

static void
gst_base_scope_class_init (GstBaseScopeClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (base_scope_debug, "basescope", 0,
      "scope audio visualisation base class");

  gobject_class->dispose = gst_base_scope_dispose;
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_base_scope_change_state);
}

static void
gst_base_scope_init (GstBaseScope * scope, GstBaseScopeClass * g_class)
{
  GstPadTemplate *pad_template;

  /* create the sink and src pads */
  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "sink");
  g_return_if_fail (pad_template != NULL);
  scope->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_chain_function (scope->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_scope_chain));
  gst_pad_set_setcaps_function (scope->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_scope_sink_setcaps));
  gst_element_add_pad (GST_ELEMENT (scope), scope->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (g_class), "src");
  g_return_if_fail (pad_template != NULL);
  scope->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_pad_set_setcaps_function (scope->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_scope_src_setcaps));
  gst_element_add_pad (GST_ELEMENT (scope), scope->srcpad);

  scope->adapter = gst_adapter_new ();
  scope->inbuf = gst_buffer_new ();

  /* reset the initial video state */
  scope->width = 320;
  scope->height = 200;
  scope->fps_n = 25;            /* desired frame rate */
  scope->fps_d = 1;
  scope->frame_duration = GST_CLOCK_TIME_NONE;

  /* reset the initial audio state */
  scope->rate = GST_AUDIO_DEF_RATE;
  scope->channels = 2;

  scope->next_ts = GST_CLOCK_TIME_NONE;

}

static void
gst_base_scope_dispose (GObject * object)
{
  GstBaseScope *scope = GST_BASE_SCOPE (object);

  if (scope->adapter) {
    g_object_unref (scope->adapter);
    scope->adapter = NULL;
  }
  if (scope->inbuf) {
    gst_buffer_unref (scope->inbuf);
    scope->inbuf = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_base_scope_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseScope *scope;
  GstStructure *structure;
  gint channels;
  gint rate;
  gboolean res = TRUE;

  scope = GST_BASE_SCOPE (gst_pad_get_parent (pad));
  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "channels", &channels) ||
      !gst_structure_get_int (structure, "rate", &rate))
    goto missing_caps_details;

  if (channels != 2)
    goto wrong_channels;

  if (rate <= 0)
    goto wrong_rate;

  scope->channels = channels;
  scope->rate = rate;

  GST_DEBUG_OBJECT (scope, "audio: channels %d, rate %d",
      scope->channels, scope->rate);

done:
  gst_object_unref (scope);
  return res;

  /* Errors */
missing_caps_details:
  {
    GST_WARNING_OBJECT (scope, "missing channels or rate in the caps");
    res = FALSE;
    goto done;
  }
wrong_channels:
  {
    GST_WARNING_OBJECT (scope, "number of channels must be 2, but is %d",
        channels);
    res = FALSE;
    goto done;
  }
wrong_rate:
  {
    GST_WARNING_OBJECT (scope, "sample rate must be >0, but is %d", rate);
    res = FALSE;
    goto done;
  }
}

static gboolean
gst_base_scope_src_negotiate (GstBaseScope * scope)
{
  GstCaps *othercaps, *target, *intersect;
  GstStructure *structure;
  const GstCaps *templ;

  templ = gst_pad_get_pad_template_caps (scope->srcpad);

  GST_DEBUG_OBJECT (scope, "performing negotiation");

  /* see what the peer can do */
  othercaps = gst_pad_peer_get_caps (scope->srcpad);
  if (othercaps) {
    intersect = gst_caps_intersect (othercaps, templ);
    gst_caps_unref (othercaps);

    if (gst_caps_is_empty (intersect))
      goto no_format;

    target = gst_caps_copy_nth (intersect, 0);
    gst_caps_unref (intersect);
  } else {
    target = gst_caps_ref ((GstCaps *) templ);
  }

  structure = gst_caps_get_structure (target, 0);
  gst_structure_fixate_field_nearest_int (structure, "width", scope->width);
  gst_structure_fixate_field_nearest_int (structure, "height", scope->height);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate",
      scope->fps_n, scope->fps_d);

  GST_DEBUG_OBJECT (scope, "final caps are %" GST_PTR_FORMAT, target);

  gst_pad_set_caps (scope->srcpad, target);
  gst_caps_unref (target);

  return TRUE;

no_format:
  {
    gst_caps_unref (intersect);
    return FALSE;
  }
}

static gboolean
gst_base_scope_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseScope *scope;
  GstBaseScopeClass *klass;
  gint w, h;
  gint num, denom;
  GstVideoFormat format;
  gboolean res = TRUE;

  scope = GST_BASE_SCOPE (gst_pad_get_parent (pad));
  klass = GST_BASE_SCOPE_CLASS (G_OBJECT_GET_CLASS (scope));

  if (!gst_video_format_parse_caps (caps, &format, &w, &h)) {
    goto missing_caps_details;
  }
  if (!gst_video_parse_caps_framerate (caps, &num, &denom)) {
    goto missing_caps_details;
  }

  scope->width = w;
  scope->height = h;
  scope->fps_n = num;
  scope->fps_d = denom;
  scope->video_format = format;

  scope->frame_duration = gst_util_uint64_scale_int (GST_SECOND,
      scope->fps_d, scope->fps_n);
  scope->spf = gst_util_uint64_scale_int (scope->rate,
      scope->fps_d, scope->fps_n);
  scope->req_spf = scope->spf;

  if (klass->setup)
    res = klass->setup (scope);

  GST_DEBUG_OBJECT (scope, "video: dimension %dx%d, framerate %d/%d",
      scope->width, scope->height, scope->fps_n, scope->fps_d);
  GST_DEBUG_OBJECT (scope, "blocks: spf %u, req_spf %u",
      scope->spf, scope->req_spf);
done:
  gst_object_unref (scope);
  return res;

  /* Errors */
missing_caps_details:
  {
    GST_WARNING_OBJECT (scope,
        "missing width, height or framerate in the caps");
    res = FALSE;
    goto done;
  }
}

static GstFlowReturn
gst_base_scope_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBaseScope *scope;
  GstBaseScopeClass *klass;
  GstBuffer *inbuf;
  guint avail, sbpf;
  guint bpp;
  gboolean (*render) (GstBaseScope * scope, GstBuffer * audio,
      GstBuffer * video);

  scope = GST_BASE_SCOPE (gst_pad_get_parent (pad));
  klass = GST_BASE_SCOPE_CLASS (G_OBJECT_GET_CLASS (scope));

  render = klass->render;

  GST_LOG_OBJECT (scope, "chainfunc called");

  /* resync on DISCONT */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    scope->next_ts = GST_CLOCK_TIME_NONE;
    gst_adapter_clear (scope->adapter);
  }

  if (GST_PAD_CAPS (scope->srcpad) == NULL) {
    if (!gst_base_scope_src_negotiate (scope))
      return GST_FLOW_NOT_NEGOTIATED;
  }

  /* Match timestamps from the incoming audio */
  if (GST_BUFFER_TIMESTAMP (buffer) != GST_CLOCK_TIME_NONE)
    scope->next_ts = GST_BUFFER_TIMESTAMP (buffer);

  gst_adapter_push (scope->adapter, buffer);

  /* this is what we want */
  sbpf = scope->req_spf * scope->channels * sizeof (gint16);

  bpp = gst_video_format_get_pixel_stride (scope->video_format, 0);

  inbuf = scope->inbuf;
  /* FIXME: the timestamp in the adapter would be different */
  gst_buffer_copy_metadata (inbuf, buffer, GST_BUFFER_COPY_ALL);

  /* this is what we have */
  avail = gst_adapter_available (scope->adapter);
  while (avail > sbpf) {
    GstBuffer *outbuf;

    ret = gst_pad_alloc_buffer_and_set_caps (scope->srcpad,
        GST_BUFFER_OFFSET_NONE,
        scope->width * scope->height * bpp,
        GST_PAD_CAPS (scope->srcpad), &outbuf);

    /* no buffer allocated, we don't care why. */
    if (ret != GST_FLOW_OK)
      break;

    GST_BUFFER_TIMESTAMP (outbuf) = scope->next_ts;
    GST_BUFFER_DURATION (outbuf) = scope->frame_duration;
    memset (GST_BUFFER_DATA (outbuf), 0, GST_BUFFER_SIZE (outbuf));

    GST_BUFFER_DATA (inbuf) =
        (guint8 *) gst_adapter_peek (scope->adapter, sbpf);
    GST_BUFFER_SIZE (inbuf) = sbpf;

    /* call class->render() vmethod */
    if (render)
      if (!render (scope, inbuf, outbuf)) {
        ret = GST_FLOW_ERROR;
      }

    ret = gst_pad_push (scope->srcpad, outbuf);
    outbuf = NULL;

    GST_LOG_OBJECT (scope, "avail: %u, bpf: %u", avail, sbpf);
    /* we want to take less or more, depending on spf : req_spf */
    if (avail - sbpf > sbpf)
      gst_adapter_flush (scope->adapter, sbpf);
    else if (avail - sbpf > 0)
      gst_adapter_flush (scope->adapter, (avail - sbpf));
    avail = gst_adapter_available (scope->adapter);

    if (ret != GST_FLOW_OK)
      break;

    if (scope->next_ts != GST_CLOCK_TIME_NONE)
      scope->next_ts += scope->frame_duration;

    avail = gst_adapter_available (scope->adapter);
  }

  gst_object_unref (scope);

  return ret;
}

static GstStateChangeReturn
gst_base_scope_change_state (GstElement * element, GstStateChange transition)
{
  GstBaseScope *scope;

  scope = GST_BASE_SCOPE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      scope->next_ts = GST_CLOCK_TIME_NONE;
      gst_adapter_clear (scope->adapter);
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}
