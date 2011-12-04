/* GStreamer
 * Copyright (C) <2011> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) <2011> Vincent Penquerch <vincent.penquerch@collabora.co.uk>
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
#include "config.h"
#endif

#include "gstplaysinkconvertbin.h"

#include <gst/pbutils/pbutils.h>
#include <gst/gst-i18n-plugin.h>
#include "gst/glib-compat-private.h"

GST_DEBUG_CATEGORY_STATIC (gst_play_sink_convert_bin_debug);
#define GST_CAT_DEFAULT gst_play_sink_convert_bin_debug

#define parent_class gst_play_sink_convert_bin_parent_class

G_DEFINE_TYPE (GstPlaySinkConvertBin, gst_play_sink_convert_bin, GST_TYPE_BIN);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static gboolean
is_raw_caps (GstCaps * caps, gboolean audio)
{
  gint i, n;
  GstStructure *s;
  const gchar *name;
  const gchar *prefix = audio ? "audio/x-raw-" : "video/x-raw-";

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    s = gst_caps_get_structure (caps, i);
    name = gst_structure_get_name (s);
    if (!g_str_has_prefix (name, prefix))
      return FALSE;
  }

  return TRUE;
}

static void
gst_play_sink_convert_bin_post_missing_element_message (GstPlaySinkConvertBin *
    self, const gchar * name)
{
  GstMessage *msg;

  msg = gst_missing_element_message_new (GST_ELEMENT_CAST (self), name);
  gst_element_post_message (GST_ELEMENT_CAST (self), msg);
}

static void
distribute_running_time (GstElement * element, const GstSegment * segment)
{
  GstEvent *event;
  GstPad *pad;

  pad = gst_element_get_static_pad (element, "sink");

  gst_pad_send_event (pad, gst_event_new_flush_start ());
  gst_pad_send_event (pad, gst_event_new_flush_stop ());

  if (segment->accum && segment->format != GST_FORMAT_UNDEFINED) {
    event = gst_event_new_new_segment_full (FALSE, segment->rate,
        segment->applied_rate, segment->format, 0, segment->accum, 0);
    gst_pad_send_event (pad, event);
  }

  if (segment->format != GST_FORMAT_UNDEFINED) {
    event = gst_event_new_new_segment_full (FALSE, segment->rate,
        segment->applied_rate, segment->format,
        segment->start, segment->stop, segment->time);
    gst_pad_send_event (pad, event);
  }

  gst_object_unref (pad);
}

void
gst_play_sink_convert_bin_add_conversion_element (GstPlaySinkConvertBin * self,
    GstElement * el)
{
  self->conversion_elements = g_list_append (self->conversion_elements, el);
  gst_bin_add (GST_BIN (self), gst_object_ref (el));
}

GstElement *
gst_play_sink_convert_bin_add_conversion_element_factory (GstPlaySinkConvertBin
    * self, const char *factory, const char *name)
{
  GstElement *el;

  el = gst_element_factory_make (factory, name);
  if (el == NULL) {
    gst_play_sink_convert_bin_post_missing_element_message (self, factory);
    GST_ELEMENT_WARNING (self, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            factory),
        (self->audio ? "audio rendering might fail" :
            "video rendering might fail"));
  } else {
    gst_play_sink_convert_bin_add_conversion_element (self, el);
  }
  return el;
}

void
gst_play_sink_convert_bin_add_identity (GstPlaySinkConvertBin * self)
{
  if (self->identity)
    return;

  self->identity = gst_element_factory_make ("identity", "identity");
  if (self->identity == NULL) {
    gst_play_sink_convert_bin_post_missing_element_message (self, "identity");
    GST_ELEMENT_WARNING (self, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "identity"), (self->audio ?
            "audio rendering might fail" : "video rendering might fail")

        );
  } else {
    g_object_set (self->identity, "silent", TRUE, "signal-handoffs", FALSE,
        NULL);
    gst_bin_add (GST_BIN_CAST (self), self->identity);
  }
}

static void
gst_play_sink_convert_bin_set_targets (GstPlaySinkConvertBin * self,
    gboolean passthrough)
{
  GstPad *pad;
  GstElement *head, *tail;

  GST_DEBUG_OBJECT (self, "Setting pad targets with passthrough %d",
      passthrough);
  if (self->conversion_elements == NULL || passthrough) {
    GST_DEBUG_OBJECT (self, "no conversion elements, using identity (%p) as "
        "head/tail", self->identity);
    if (!passthrough) {
      GST_WARNING_OBJECT (self,
          "Doing passthrough as no converter elements were added");
    }
    head = tail = self->identity;
  } else {
    head = GST_ELEMENT (g_list_first (self->conversion_elements)->data);
    tail = GST_ELEMENT (g_list_last (self->conversion_elements)->data);
    GST_DEBUG_OBJECT (self, "conversion elements in use, picking "
        "head:%s and tail:%s", GST_OBJECT_NAME (head), GST_OBJECT_NAME (tail));
  }

  g_return_if_fail (head != NULL);
  g_return_if_fail (tail != NULL);

  pad = gst_element_get_static_pad (head, "sink");
  GST_DEBUG_OBJECT (self, "Ghosting bin sink pad to %" GST_PTR_FORMAT, pad);
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->sinkpad), pad);
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (tail, "src");
  GST_DEBUG_OBJECT (self, "Ghosting bin src pad to %" GST_PTR_FORMAT, pad);
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->srcpad), pad);
  gst_object_unref (pad);
}

static void
gst_play_sink_convert_bin_remove_element (GstElement * element,
    GstPlaySinkConvertBin * self)
{
  gst_element_set_state (element, GST_STATE_NULL);
  gst_bin_remove (GST_BIN_CAST (self), element);
}

static void
gst_play_sink_convert_bin_on_element_added (GstElement * element,
    GstPlaySinkConvertBin * self)
{
  gst_element_sync_state_with_parent (element);
  distribute_running_time (element, &self->segment);
}

static void
pad_blocked_cb (GstPad * pad, gboolean blocked, GstPlaySinkConvertBin * self)
{
  GstPad *peer;
  GstCaps *caps;
  gboolean raw;

  GST_PLAY_SINK_CONVERT_BIN_LOCK (self);
  self->sink_proxypad_blocked = blocked;
  GST_DEBUG_OBJECT (self, "Pad blocked: %d", blocked);
  if (!blocked)
    goto done;

  /* There must be a peer at this point */
  peer = gst_pad_get_peer (self->sinkpad);
  caps = gst_pad_get_negotiated_caps (peer);
  if (!caps)
    caps = gst_pad_get_caps_reffed (peer);
  gst_object_unref (peer);

  raw = is_raw_caps (caps, self->audio);
  GST_DEBUG_OBJECT (self, "Caps %" GST_PTR_FORMAT " are raw: %d", caps, raw);
  gst_caps_unref (caps);

  if (raw == self->raw)
    goto unblock;
  self->raw = raw;

  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->sinkpad), NULL);
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->srcpad), NULL);

  if (raw) {
    GST_DEBUG_OBJECT (self, "Switching to raw conversion pipeline");

    if (self->conversion_elements)
      g_list_foreach (self->conversion_elements,
          (GFunc) gst_play_sink_convert_bin_on_element_added, self);
  } else {

    GST_DEBUG_OBJECT (self, "Switch to passthrough pipeline");

    gst_play_sink_convert_bin_on_element_added (self->identity, self);
  }

  gst_play_sink_convert_bin_set_targets (self, !raw);

unblock:
  gst_pad_set_blocked_async_full (self->sink_proxypad, FALSE,
      (GstPadBlockCallback) pad_blocked_cb, gst_object_ref (self),
      (GDestroyNotify) gst_object_unref);

done:
  GST_PLAY_SINK_CONVERT_BIN_UNLOCK (self);
  return;
}

static gboolean
gst_play_sink_convert_bin_sink_event (GstPad * pad, GstEvent * event)
{
  GstPlaySinkConvertBin *self =
      GST_PLAY_SINK_CONVERT_BIN (gst_pad_get_parent (pad));
  gboolean ret;

  ret = gst_proxy_pad_event_default (pad, gst_event_ref (event));

  if (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT) {
    gboolean update;
    gdouble rate, applied_rate;
    GstFormat format;
    gint64 start, stop, position;

    GST_PLAY_SINK_CONVERT_BIN_LOCK (self);
    gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
        &format, &start, &stop, &position);

    GST_DEBUG_OBJECT (self, "Segment before %" GST_SEGMENT_FORMAT,
        &self->segment);
    gst_segment_set_newsegment_full (&self->segment, update, rate, applied_rate,
        format, start, stop, position);
    GST_DEBUG_OBJECT (self, "Segment after %" GST_SEGMENT_FORMAT,
        &self->segment);
    GST_PLAY_SINK_CONVERT_BIN_UNLOCK (self);
  } else if (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP) {
    GST_PLAY_SINK_CONVERT_BIN_LOCK (self);
    GST_DEBUG_OBJECT (self, "Resetting segment");
    gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
    GST_PLAY_SINK_CONVERT_BIN_UNLOCK (self);
  }

  gst_event_unref (event);
  gst_object_unref (self);

  return ret;
}

static gboolean
gst_play_sink_convert_bin_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstPlaySinkConvertBin *self =
      GST_PLAY_SINK_CONVERT_BIN (gst_pad_get_parent (pad));
  gboolean ret;
  GstStructure *s;
  const gchar *name;
  gboolean reconfigure = FALSE;
  gboolean raw;

  GST_DEBUG_OBJECT (pad, "setcaps: %" GST_PTR_FORMAT, caps);
  GST_PLAY_SINK_CONVERT_BIN_LOCK (self);
  s = gst_caps_get_structure (caps, 0);
  name = gst_structure_get_name (s);

  if (self->audio) {
    raw = g_str_has_prefix (name, "audio/x-raw-");
  } else {
    raw = g_str_has_prefix (name, "video/x-raw-");
  }

  GST_DEBUG_OBJECT (self, "raw %d, self->raw %d, blocked %d",
      raw, self->raw, gst_pad_is_blocked (self->sink_proxypad));

  if (raw) {
    if (!gst_pad_is_blocked (self->sink_proxypad)) {
      GstPad *target = gst_ghost_pad_get_target (GST_GHOST_PAD (self->sinkpad));

      if (!self->raw || (target && !gst_pad_accept_caps (target, caps))) {
        if (!self->raw)
          GST_DEBUG_OBJECT (self, "Changing caps from non-raw to raw");
        else
          GST_DEBUG_OBJECT (self, "Changing caps in an incompatible way");

        reconfigure = TRUE;
        gst_pad_set_blocked_async_full (self->sink_proxypad, TRUE,
            (GstPadBlockCallback) pad_blocked_cb, gst_object_ref (self),
            (GDestroyNotify) gst_object_unref);
      }

      if (target)
        gst_object_unref (target);
    }
  } else {
    if (self->raw && !gst_pad_is_blocked (self->sink_proxypad)) {
      GST_DEBUG_OBJECT (self, "Changing caps from raw to non-raw");
      reconfigure = TRUE;
      gst_pad_set_blocked_async_full (self->sink_proxypad, TRUE,
          (GstPadBlockCallback) pad_blocked_cb, gst_object_ref (self),
          (GDestroyNotify) gst_object_unref);
    }
  }

  /* Otherwise the setcaps below fails */
  if (reconfigure) {
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->sinkpad), NULL);
    gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (self->srcpad), NULL);
  }

  GST_PLAY_SINK_CONVERT_BIN_UNLOCK (self);
  ret = gst_ghost_pad_setcaps_default (pad, caps);

  GST_DEBUG_OBJECT (self, "Setting sink caps %" GST_PTR_FORMAT ": %d", caps,
      ret);

  gst_object_unref (self);

  return ret;
}

static GstCaps *
gst_play_sink_convert_bin_getcaps (GstPad * pad)
{
  GstPlaySinkConvertBin *self =
      GST_PLAY_SINK_CONVERT_BIN (gst_pad_get_parent (pad));
  GstCaps *ret;
  GstPad *otherpad, *peer;

  GST_PLAY_SINK_CONVERT_BIN_LOCK (self);
  if (pad == self->srcpad) {
    otherpad = self->sinkpad;
  } else if (pad == self->sinkpad) {
    otherpad = self->srcpad;
  } else {
    GST_ERROR_OBJECT (pad, "Not one of our pads");
    otherpad = NULL;
  }

  if (otherpad) {
    peer = gst_pad_get_peer (otherpad);
    if (peer) {
      GstCaps *peer_caps = gst_pad_get_caps_reffed (peer);
      gst_object_unref (peer);
      if (self->converter_caps && is_raw_caps (peer_caps, self->audio)) {
        peer_caps = gst_caps_make_writable (peer_caps);
        gst_caps_merge (peer_caps, gst_caps_ref (self->converter_caps));
        ret = peer_caps;
      } else {
        ret = peer_caps;
      }
    } else {
      ret = gst_caps_ref (self->converter_caps);
    }
  } else {
    ret = gst_caps_new_any ();
  }
  GST_PLAY_SINK_CONVERT_BIN_UNLOCK (self);

  gst_object_unref (self);

  GST_DEBUG_OBJECT (pad, "Returning caps %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_play_sink_convert_bin_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstCaps *allowed_caps;
  gboolean ret;

  allowed_caps = gst_pad_get_caps_reffed (pad);
  /* FIXME 0.11: Should be a subset check now */
  ret = gst_caps_can_intersect (caps, allowed_caps);
  gst_caps_unref (allowed_caps);

  return ret;
}

void
gst_play_sink_convert_bin_remove_elements (GstPlaySinkConvertBin * self)
{
  if (self->conversion_elements) {
    g_list_foreach (self->conversion_elements,
        (GFunc) gst_play_sink_convert_bin_remove_element, self);
    g_list_free (self->conversion_elements);
    self->conversion_elements = NULL;
  }
  if (self->converter_caps) {
    gst_caps_unref (self->converter_caps);
    self->converter_caps = NULL;
  }
}

static void
gst_play_sink_convert_bin_dispose (GObject * object)
{
  GstPlaySinkConvertBin *self = GST_PLAY_SINK_CONVERT_BIN_CAST (object);

  gst_play_sink_convert_bin_remove_elements (self);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_play_sink_convert_bin_finalize (GObject * object)
{
  GstPlaySinkConvertBin *self = GST_PLAY_SINK_CONVERT_BIN_CAST (object);

  gst_object_unref (self->sink_proxypad);
  g_mutex_free (self->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

void
gst_play_sink_convert_bin_cache_converter_caps (GstPlaySinkConvertBin * self)
{
  GstElement *head;
  GstPad *pad;

  if (self->converter_caps) {
    gst_caps_unref (self->converter_caps);
    self->converter_caps = NULL;
  }

  if (!self->conversion_elements) {
    GST_WARNING_OBJECT (self, "No conversion elements");
    return;
  }

  head = GST_ELEMENT (g_list_first (self->conversion_elements)->data);
  pad = gst_element_get_static_pad (head, "sink");
  if (!pad) {
    GST_WARNING_OBJECT (self, "No sink pad found");
    return;
  }

  self->converter_caps = gst_pad_get_caps_reffed (pad);
  GST_INFO_OBJECT (self, "Converter caps: %" GST_PTR_FORMAT,
      self->converter_caps);

  gst_object_unref (pad);
}

static GstStateChangeReturn
gst_play_sink_convert_bin_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstPlaySinkConvertBin *self = GST_PLAY_SINK_CONVERT_BIN_CAST (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_PLAY_SINK_CONVERT_BIN_LOCK (self);
      if (gst_pad_is_blocked (self->sink_proxypad))
        gst_pad_set_blocked_async_full (self->sink_proxypad, FALSE,
            (GstPadBlockCallback) pad_blocked_cb, gst_object_ref (self),
            (GDestroyNotify) gst_object_unref);
      GST_PLAY_SINK_CONVERT_BIN_UNLOCK (self);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_PLAY_SINK_CONVERT_BIN_LOCK (self);
      gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
      gst_play_sink_convert_bin_set_targets (self, TRUE);
      self->raw = FALSE;
      GST_PLAY_SINK_CONVERT_BIN_UNLOCK (self);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_PLAY_SINK_CONVERT_BIN_LOCK (self);
      gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
      gst_play_sink_convert_bin_set_targets (self, TRUE);
      self->raw = FALSE;
      GST_PLAY_SINK_CONVERT_BIN_UNLOCK (self);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_PLAY_SINK_CONVERT_BIN_LOCK (self);
      if (!gst_pad_is_blocked (self->sink_proxypad))
        gst_pad_set_blocked_async_full (self->sink_proxypad, TRUE,
            (GstPadBlockCallback) pad_blocked_cb, gst_object_ref (self),
            (GDestroyNotify) gst_object_unref);
      GST_PLAY_SINK_CONVERT_BIN_UNLOCK (self);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_play_sink_convert_bin_class_init (GstPlaySinkConvertBinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  GST_DEBUG_CATEGORY_INIT (gst_play_sink_convert_bin_debug,
      "playsinkconvertbin", 0, "play bin");

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->dispose = gst_play_sink_convert_bin_dispose;
  gobject_class->finalize = gst_play_sink_convert_bin_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_set_details_simple (gstelement_class,
      "Player Sink Converter Bin", "Bin/Converter",
      "Convenience bin for audio/video conversion",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_play_sink_convert_bin_change_state);
}

static void
gst_play_sink_convert_bin_init (GstPlaySinkConvertBin * self)
{
  GstPadTemplate *templ;

  self->lock = g_mutex_new ();
  gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);

  templ = gst_static_pad_template_get (&sinktemplate);
  self->sinkpad = gst_ghost_pad_new_no_target_from_template ("sink", templ);
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_play_sink_convert_bin_sink_event));
  gst_pad_set_setcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_play_sink_convert_bin_sink_setcaps));
  gst_pad_set_getcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_play_sink_convert_bin_getcaps));
  gst_pad_set_acceptcaps_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_play_sink_convert_bin_acceptcaps));

  self->sink_proxypad =
      GST_PAD_CAST (gst_proxy_pad_get_internal (GST_PROXY_PAD (self->sinkpad)));

  gst_element_add_pad (GST_ELEMENT_CAST (self), self->sinkpad);
  gst_object_unref (templ);

  templ = gst_static_pad_template_get (&srctemplate);
  self->srcpad = gst_ghost_pad_new_no_target_from_template ("src", templ);
  gst_pad_set_getcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_play_sink_convert_bin_getcaps));
  gst_pad_set_acceptcaps_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_play_sink_convert_bin_acceptcaps));
  gst_element_add_pad (GST_ELEMENT_CAST (self), self->srcpad);
  gst_object_unref (templ);

  gst_play_sink_convert_bin_add_identity (self);
}
