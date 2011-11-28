/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *                    2007 Andy Wingo <wingo at pobox.com>
 *                    2008 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * deinterleave.c: deinterleave samples
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

/* TODO: 
 *       - handle changes in number of channels
 *       - handle changes in channel positions
 *       - better capsnego by using a buffer alloc function
 *         and passing downstream caps changes upstream there
 */

/**
 * SECTION:element-deinterleave
 * @see_also: interleave
 *
 * Splits one interleaved multichannel audio stream into many mono audio streams.
 * 
 * This element handles all raw audio formats and supports changing the input caps as long as
 * all downstream elements can handle the new caps and the number of channels and the channel
 * positions stay the same. This restriction will be removed in later versions by adding or
 * removing some source pads as required.
 * 
 * In most cases a queue and an audioconvert element should be added after each source pad
 * before further processing of the audio data.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=/path/to/file.mp3 ! decodebin ! audioconvert ! "audio/x-raw-int,channels=2 ! deinterleave name=d  d.src0 ! queue ! audioconvert ! vorbisenc ! oggmux ! filesink location=channel1.ogg  d.src1 ! queue ! audioconvert ! vorbisenc ! oggmux ! filesink location=channel2.ogg
 * ]| Decodes an MP3 file and encodes the left and right channel into separate
 * Ogg Vorbis files.
 * |[
 * gst-launch filesrc location=file.mp3 ! decodebin ! audioconvert ! "audio/x-raw-int,channels=2" ! deinterleave name=d  interleave name=i ! audioconvert ! wavenc ! filesink location=test.wav    d.src0 ! queue ! audioconvert ! i.sink1    d.src1 ! queue ! audioconvert ! i.sink0
 * ]| Decodes and deinterleaves a Stereo MP3 file into separate channels and
 * then interleaves the channels again to a WAV file with the channel with the
 * channels exchanged.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>
#include "deinterleave.h"

GST_DEBUG_CATEGORY_STATIC (gst_deinterleave_debug);
#define GST_CAT_DEFAULT gst_deinterleave_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) 1, "
        "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        "width = (int) { 8, 16, 24, 32 }, "
        "depth = (int) [ 1, 32 ], "
        "signed = (boolean) { true, false }; "
        "audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) 1, "
        "endianness = (int) { LITTLE_ENDIAN , BIG_ENDIAN }, "
        "width = (int) { 32, 64 }")
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        "width = (int) { 8, 16, 24, 32 }, "
        "depth = (int) [ 1, 32 ], "
        "signed = (boolean) { true, false }; "
        "audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) { LITTLE_ENDIAN , BIG_ENDIAN }, "
        "width = (int) { 32, 64 }")
    );

#define MAKE_FUNC(type) \
static void deinterleave_##type (guint##type *out, guint##type *in, \
    guint stride, guint nframes) \
{ \
  gint i; \
  \
  for (i = 0; i < nframes; i++) { \
    out[i] = *in; \
    in += stride; \
  } \
}

MAKE_FUNC (8);
MAKE_FUNC (16);
MAKE_FUNC (32);
MAKE_FUNC (64);

static void
deinterleave_24 (guint8 * out, guint8 * in, guint stride, guint nframes)
{
  gint i;

  for (i = 0; i < nframes; i++) {
    memcpy (out, in, 3);
    out += 3;
    in += stride * 3;
  }
}

GST_BOILERPLATE (GstDeinterleave, gst_deinterleave, GstElement,
    GST_TYPE_ELEMENT);

enum
{
  PROP_0,
  PROP_KEEP_POSITIONS
};

static GstFlowReturn gst_deinterleave_chain (GstPad * pad, GstBuffer * buffer);

static gboolean gst_deinterleave_sink_setcaps (GstPad * pad, GstCaps * caps);

static GstCaps *gst_deinterleave_sink_getcaps (GstPad * pad);

static gboolean gst_deinterleave_sink_activate_push (GstPad * pad,
    gboolean active);
static gboolean gst_deinterleave_sink_event (GstPad * pad, GstEvent * event);

static gboolean gst_deinterleave_src_query (GstPad * pad, GstQuery * query);

static void gst_deinterleave_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_deinterleave_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);


static void
gst_deinterleave_finalize (GObject * obj)
{
  GstDeinterleave *self = GST_DEINTERLEAVE (obj);

  if (self->pos) {
    g_free (self->pos);
    self->pos = NULL;
  }

  if (self->pending_events) {
    g_list_foreach (self->pending_events, (GFunc) gst_mini_object_unref, NULL);
    g_list_free (self->pending_events);
    self->pending_events = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_deinterleave_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = (GstElementClass *) g_class;

  gst_element_class_set_details_simple (gstelement_class, "Audio deinterleaver",
      "Filter/Converter/Audio",
      "Splits one interleaved multichannel audio stream into many mono audio streams",
      "Andy Wingo <wingo at pobox.com>, "
      "Iain <iain@prettypeople.org>, "
      "Sebastian Dröge <slomo@circular-chaos.org>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &src_template);
}

static void
gst_deinterleave_class_init (GstDeinterleaveClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_deinterleave_debug, "deinterleave", 0,
      "deinterleave element");

  gobject_class->finalize = gst_deinterleave_finalize;
  gobject_class->set_property = gst_deinterleave_set_property;
  gobject_class->get_property = gst_deinterleave_get_property;

  /**
   * GstDeinterleave:keep-positions
   * 
   * Keep positions: When enable the caps on the output buffers will
   * contain the original channel positions. This can be used to correctly
   * interleave the output again later but can also lead to unwanted effects
   * if the output should be handled as Mono.
   *
   */
  g_object_class_install_property (gobject_class, PROP_KEEP_POSITIONS,
      g_param_spec_boolean ("keep-positions", "Keep positions",
          "Keep the original channel positions on the output buffers",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_deinterleave_init (GstDeinterleave * self, GstDeinterleaveClass * klass)
{
  self->channels = 0;
  self->pos = NULL;
  self->keep_positions = FALSE;
  self->width = 0;
  self->func = NULL;

  /* Add sink pad */
  self->sink = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (self->sink,
      GST_DEBUG_FUNCPTR (gst_deinterleave_chain));
  gst_pad_set_setcaps_function (self->sink,
      GST_DEBUG_FUNCPTR (gst_deinterleave_sink_setcaps));
  gst_pad_set_getcaps_function (self->sink,
      GST_DEBUG_FUNCPTR (gst_deinterleave_sink_getcaps));
  gst_pad_set_activatepush_function (self->sink,
      GST_DEBUG_FUNCPTR (gst_deinterleave_sink_activate_push));
  gst_pad_set_event_function (self->sink,
      GST_DEBUG_FUNCPTR (gst_deinterleave_sink_event));
  gst_element_add_pad (GST_ELEMENT (self), self->sink);
}

static void
gst_deinterleave_add_new_pads (GstDeinterleave * self, GstCaps * caps)
{
  GstPad *pad;

  guint i;

  for (i = 0; i < self->channels; i++) {
    gchar *name = g_strdup_printf ("src%d", i);

    GstCaps *srccaps;

    GstStructure *s;

    pad = gst_pad_new_from_static_template (&src_template, name);
    g_free (name);

    /* Set channel position if we know it */
    if (self->keep_positions) {
      GstAudioChannelPosition pos[1] = { GST_AUDIO_CHANNEL_POSITION_NONE };

      srccaps = gst_caps_copy (caps);
      s = gst_caps_get_structure (srccaps, 0);
      if (self->pos)
        gst_audio_set_channel_positions (s, &self->pos[i]);
      else
        gst_audio_set_channel_positions (s, pos);
    } else {
      srccaps = caps;
    }

    gst_pad_set_caps (pad, srccaps);
    gst_pad_use_fixed_caps (pad);
    gst_pad_set_query_function (pad,
        GST_DEBUG_FUNCPTR (gst_deinterleave_src_query));
    gst_pad_set_active (pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (self), pad);
    self->srcpads = g_list_prepend (self->srcpads, gst_object_ref (pad));

    if (self->keep_positions)
      gst_caps_unref (srccaps);
  }

  gst_element_no_more_pads (GST_ELEMENT (self));
  self->srcpads = g_list_reverse (self->srcpads);
}

static void
gst_deinterleave_set_pads_caps (GstDeinterleave * self, GstCaps * caps)
{
  GList *l;

  GstStructure *s;

  gint i;

  for (l = self->srcpads, i = 0; l; l = l->next, i++) {
    GstPad *pad = GST_PAD (l->data);

    GstCaps *srccaps;

    /* Set channel position if we know it */
    if (self->keep_positions) {
      GstAudioChannelPosition pos[1] = { GST_AUDIO_CHANNEL_POSITION_NONE };

      srccaps = gst_caps_copy (caps);
      s = gst_caps_get_structure (srccaps, 0);
      if (self->pos)
        gst_audio_set_channel_positions (s, &self->pos[i]);
      else
        gst_audio_set_channel_positions (s, pos);
    } else {
      srccaps = caps;
    }

    gst_pad_set_caps (pad, srccaps);

    if (self->keep_positions)
      gst_caps_unref (srccaps);
  }
}

static void
gst_deinterleave_remove_pads (GstDeinterleave * self)
{
  GList *l;

  GST_INFO_OBJECT (self, "removing pads");

  for (l = self->srcpads; l; l = l->next) {
    GstPad *pad = GST_PAD (l->data);

    gst_element_remove_pad (GST_ELEMENT_CAST (self), pad);
    gst_object_unref (pad);
  }
  g_list_free (self->srcpads);
  self->srcpads = NULL;

  gst_pad_set_caps (self->sink, NULL);
  gst_caps_replace (&self->sinkcaps, NULL);
}

static gboolean
gst_deinterleave_set_process_function (GstDeinterleave * self, GstCaps * caps)
{
  GstStructure *s;

  s = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (s, "width", &self->width))
    return FALSE;

  switch (self->width) {
    case 8:
      self->func = (GstDeinterleaveFunc) deinterleave_8;
      break;
    case 16:
      self->func = (GstDeinterleaveFunc) deinterleave_16;
      break;
    case 24:
      self->func = (GstDeinterleaveFunc) deinterleave_24;
      break;
    case 32:
      self->func = (GstDeinterleaveFunc) deinterleave_32;
      break;
    case 64:
      self->func = (GstDeinterleaveFunc) deinterleave_64;
      break;
    default:
      return FALSE;
  }
  return TRUE;
}

static gboolean
gst_deinterleave_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstDeinterleave *self;

  GstCaps *srccaps;

  GstStructure *s;

  self = GST_DEINTERLEAVE (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (self, "got caps: %" GST_PTR_FORMAT, caps);

  if (self->sinkcaps && !gst_caps_is_equal (caps, self->sinkcaps)) {
    gint new_channels, i;

    GstAudioChannelPosition *pos;

    gboolean same_layout = TRUE;

    s = gst_caps_get_structure (caps, 0);

    /* We allow caps changes as long as the number of channels doesn't change
     * and the channel positions stay the same. _getcaps() should've cared
     * for this already but better be safe.
     */
    if (!gst_structure_get_int (s, "channels", &new_channels) ||
        new_channels != self->channels ||
        !gst_deinterleave_set_process_function (self, caps))
      goto cannot_change_caps;

    /* Now check the channel positions. If we had no channel positions
     * and get them or the other way around things have changed.
     * If we had channel positions and get different ones things have
     * changed too of course
     */
    pos = gst_audio_get_channel_positions (s);
    if ((pos && !self->pos) || (!pos && self->pos))
      goto cannot_change_caps;

    if (pos) {
      for (i = 0; i < self->channels; i++) {
        if (self->pos[i] != pos[i]) {
          same_layout = FALSE;
          break;
        }
      }
      g_free (pos);
      if (!same_layout)
        goto cannot_change_caps;
    }

  } else {
    s = gst_caps_get_structure (caps, 0);

    if (!gst_structure_get_int (s, "channels", &self->channels))
      goto no_channels;

    if (!gst_deinterleave_set_process_function (self, caps))
      goto unsupported_caps;

    self->pos = gst_audio_get_channel_positions (s);
  }

  gst_caps_replace (&self->sinkcaps, caps);

  /* Get srcpad caps */
  srccaps = gst_caps_copy (caps);
  s = gst_caps_get_structure (srccaps, 0);
  gst_structure_set (s, "channels", G_TYPE_INT, 1, NULL);
  gst_structure_remove_field (s, "channel-positions");

  /* If we already have pads, update the caps otherwise
   * add new pads */
  if (self->srcpads) {
    gst_deinterleave_set_pads_caps (self, srccaps);
  } else {
    gst_deinterleave_add_new_pads (self, srccaps);
  }

  gst_caps_unref (srccaps);
  gst_object_unref (self);

  return TRUE;

cannot_change_caps:
  {
    GST_ERROR_OBJECT (self, "can't set new caps: %" GST_PTR_FORMAT, caps);
    gst_object_unref (self);
    return FALSE;
  }
unsupported_caps:
  {
    GST_ERROR_OBJECT (self, "caps not supported: %" GST_PTR_FORMAT, caps);
    gst_object_unref (self);
    return FALSE;
  }
no_channels:
  {
    GST_ERROR_OBJECT (self, "invalid caps");
    gst_object_unref (self);
    return FALSE;
  }
}

static void
__remove_channels (GstCaps * caps)
{
  GstStructure *s;

  gint i, size;

  size = gst_caps_get_size (caps);
  for (i = 0; i < size; i++) {
    s = gst_caps_get_structure (caps, i);
    gst_structure_remove_field (s, "channel-positions");
    gst_structure_remove_field (s, "channels");
  }
}

static void
__set_channels (GstCaps * caps, gint channels)
{
  GstStructure *s;

  gint i, size;

  size = gst_caps_get_size (caps);
  for (i = 0; i < size; i++) {
    s = gst_caps_get_structure (caps, i);
    if (channels > 0)
      gst_structure_set (s, "channels", G_TYPE_INT, channels, NULL);
    else
      gst_structure_set (s, "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
  }
}

static GstCaps *
gst_deinterleave_sink_getcaps (GstPad * pad)
{
  GstDeinterleave *self = GST_DEINTERLEAVE (gst_pad_get_parent (pad));

  GstCaps *ret;

  GList *l;

  GST_OBJECT_LOCK (self);
  /* Intersect all of our pad template caps with the peer caps of the pad
   * to get all formats that are possible up- and downstream.
   *
   * For the pad for which the caps are requested we don't remove the channel
   * informations as they must be in the returned caps and incompatibilities
   * will be detected here already
   */
  ret = gst_caps_new_any ();
  for (l = GST_ELEMENT (self)->pads; l != NULL; l = l->next) {
    GstPad *ourpad = GST_PAD (l->data);

    GstCaps *peercaps = NULL, *ourcaps;

    ourcaps = gst_caps_copy (gst_pad_get_pad_template_caps (ourpad));

    if (pad == ourpad) {
      if (GST_PAD_DIRECTION (pad) == GST_PAD_SINK)
        __set_channels (ourcaps, self->channels);
      else
        __set_channels (ourcaps, 1);
    } else {
      __remove_channels (ourcaps);
      /* Only ask for peer caps for other pads than pad
       * as otherwise gst_pad_peer_get_caps() might call
       * back into this function and deadlock
       */
      peercaps = gst_pad_peer_get_caps (ourpad);
    }

    /* If the peer exists and has caps add them to the intersection,
     * otherwise assume that the peer accepts everything */
    if (peercaps) {
      GstCaps *intersection;

      GstCaps *oldret = ret;

      __remove_channels (peercaps);

      intersection = gst_caps_intersect (peercaps, ourcaps);

      ret = gst_caps_intersect (ret, intersection);
      gst_caps_unref (intersection);
      gst_caps_unref (peercaps);
      gst_caps_unref (oldret);
    } else {
      GstCaps *oldret = ret;

      ret = gst_caps_intersect (ret, ourcaps);
      gst_caps_unref (oldret);
    }
    gst_caps_unref (ourcaps);
  }
  GST_OBJECT_UNLOCK (self);

  gst_object_unref (self);

  GST_DEBUG_OBJECT (pad, "Intersected caps to %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_deinterleave_sink_event (GstPad * pad, GstEvent * event)
{
  GstDeinterleave *self = GST_DEINTERLEAVE (gst_pad_get_parent (pad));

  gboolean ret;

  GST_DEBUG ("Got %s event on pad %s:%s", GST_EVENT_TYPE_NAME (event),
      GST_DEBUG_PAD_NAME (pad));

  /* Send FLUSH_STOP, FLUSH_START and EOS immediately, no matter if
   * we have src pads already or not. Queue all other events and
   * push them after we have src pads
   */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_EOS:
      ret = gst_pad_event_default (pad, event);
      break;
    default:
      if (self->srcpads) {
        ret = gst_pad_event_default (pad, event);
      } else {
        GST_OBJECT_LOCK (self);
        self->pending_events = g_list_append (self->pending_events, event);
        GST_OBJECT_UNLOCK (self);
        ret = TRUE;
      }
      break;
  }

  gst_object_unref (self);

  return ret;
}

static gboolean
gst_deinterleave_src_query (GstPad * pad, GstQuery * query)
{
  GstDeinterleave *self = GST_DEINTERLEAVE (gst_pad_get_parent (pad));

  gboolean res;

  res = gst_pad_query_default (pad, query);

  if (res && GST_QUERY_TYPE (query) == GST_QUERY_DURATION) {
    GstFormat format;

    gint64 dur;

    gst_query_parse_duration (query, &format, &dur);

    /* Need to divide by the number of channels in byte format
     * to get the correct value. All other formats should be fine
     */
    if (format == GST_FORMAT_BYTES && dur != -1)
      gst_query_set_duration (query, format, dur / self->channels);
  } else if (res && GST_QUERY_TYPE (query) == GST_QUERY_POSITION) {
    GstFormat format;

    gint64 pos;

    gst_query_parse_position (query, &format, &pos);

    /* Need to divide by the number of channels in byte format
     * to get the correct value. All other formats should be fine
     */
    if (format == GST_FORMAT_BYTES && pos != -1)
      gst_query_set_position (query, format, pos / self->channels);
  }

  gst_object_unref (self);
  return res;
}

static void
gst_deinterleave_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDeinterleave *self = GST_DEINTERLEAVE (object);

  switch (prop_id) {
    case PROP_KEEP_POSITIONS:
      self->keep_positions = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_deinterleave_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDeinterleave *self = GST_DEINTERLEAVE (object);

  switch (prop_id) {
    case PROP_KEEP_POSITIONS:
      g_value_set_boolean (value, self->keep_positions);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_deinterleave_process (GstDeinterleave * self, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;

  guint channels = self->channels;

  guint pads_pushed = 0, buffers_allocated = 0;

  guint nframes = GST_BUFFER_SIZE (buf) / channels / (self->width / 8);

  guint bufsize = nframes * (self->width / 8);

  guint i;

  GList *srcs;

  GstBuffer **buffers_out = g_new0 (GstBuffer *, channels);

  guint8 *in, *out;

  /* Send any pending events to all src pads */
  GST_OBJECT_LOCK (self);
  if (self->pending_events) {
    GList *events;

    GstEvent *event;

    GST_DEBUG_OBJECT (self, "Sending pending events to all src pads");

    for (events = self->pending_events; events != NULL; events = events->next) {
      event = GST_EVENT (events->data);

      for (srcs = self->srcpads; srcs != NULL; srcs = srcs->next)
        gst_pad_push_event (GST_PAD (srcs->data), gst_event_ref (event));
      gst_event_unref (event);
    }

    g_list_free (self->pending_events);
    self->pending_events = NULL;
  }
  GST_OBJECT_UNLOCK (self);

  /* Allocate buffers */
  for (srcs = self->srcpads, i = 0; srcs; srcs = srcs->next, i++) {
    GstPad *pad = (GstPad *) srcs->data;

    buffers_out[i] = NULL;
    ret =
        gst_pad_alloc_buffer (pad, GST_BUFFER_OFFSET_NONE, bufsize,
        GST_PAD_CAPS (pad), &buffers_out[i]);

    /* Make sure we got a correct buffer. The only other case we allow
     * here is an unliked pad */
    if (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED)
      goto alloc_buffer_failed;
    else if (buffers_out[i] && GST_BUFFER_SIZE (buffers_out[i]) != bufsize)
      goto alloc_buffer_bad_size;
    else if (buffers_out[i] &&
        !gst_caps_is_equal (GST_BUFFER_CAPS (buffers_out[i]),
            GST_PAD_CAPS (pad)))
      goto invalid_caps;

    if (buffers_out[i]) {
      gst_buffer_copy_metadata (buffers_out[i], buf,
          GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_FLAGS);
      buffers_allocated++;
    }
  }

  /* Return NOT_LINKED if no pad was linked */
  if (!buffers_allocated) {
    GST_WARNING_OBJECT (self,
        "Couldn't allocate any buffers because no pad was linked");
    ret = GST_FLOW_NOT_LINKED;
    goto done;
  }

  /* deinterleave */
  for (srcs = self->srcpads, i = 0; srcs; srcs = srcs->next, i++) {
    GstPad *pad = (GstPad *) srcs->data;

    in = (guint8 *) GST_BUFFER_DATA (buf);
    in += i * (self->width / 8);
    if (buffers_out[i]) {
      out = (guint8 *) GST_BUFFER_DATA (buffers_out[i]);

      self->func (out, in, channels, nframes);

      ret = gst_pad_push (pad, buffers_out[i]);
      buffers_out[i] = NULL;
      if (ret == GST_FLOW_OK)
        pads_pushed++;
      else if (ret == GST_FLOW_NOT_LINKED)
        ret = GST_FLOW_OK;
      else
        goto push_failed;
    }
  }

  /* Return NOT_LINKED if no pad was linked */
  if (!pads_pushed)
    ret = GST_FLOW_NOT_LINKED;

done:
  gst_buffer_unref (buf);
  g_free (buffers_out);
  return ret;

alloc_buffer_failed:
  {
    GST_WARNING ("gst_pad_alloc_buffer() returned %s", gst_flow_get_name (ret));
    goto clean_buffers;

  }
alloc_buffer_bad_size:
  {
    GST_WARNING ("called alloc_buffer(), but didn't get requested bytes");
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto clean_buffers;
  }
invalid_caps:
  {
    GST_WARNING ("called alloc_buffer(), but didn't get requested caps");
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto clean_buffers;
  }
push_failed:
  {
    GST_DEBUG ("push() failed, flow = %s", gst_flow_get_name (ret));
    goto clean_buffers;
  }
clean_buffers:
  {
    for (i = 0; i < channels; i++) {
      if (buffers_out[i])
        gst_buffer_unref (buffers_out[i]);
    }
    gst_buffer_unref (buf);
    g_free (buffers_out);
    return ret;
  }
}

static GstFlowReturn
gst_deinterleave_chain (GstPad * pad, GstBuffer * buffer)
{
  GstDeinterleave *self = GST_DEINTERLEAVE (GST_PAD_PARENT (pad));

  GstFlowReturn ret;

  g_return_val_if_fail (self->func != NULL, GST_FLOW_NOT_NEGOTIATED);
  g_return_val_if_fail (self->width > 0, GST_FLOW_NOT_NEGOTIATED);
  g_return_val_if_fail (self->channels > 0, GST_FLOW_NOT_NEGOTIATED);

  ret = gst_deinterleave_process (self, buffer);

  if (ret != GST_FLOW_OK)
    GST_DEBUG_OBJECT (self, "flow return: %s", gst_flow_get_name (ret));

  return ret;
}

static gboolean
gst_deinterleave_sink_activate_push (GstPad * pad, gboolean active)
{
  GstDeinterleave *self = GST_DEINTERLEAVE (gst_pad_get_parent (pad));

  /* Reset everything when the pad is deactivated */
  if (!active) {
    gst_deinterleave_remove_pads (self);
    if (self->pos) {
      g_free (self->pos);
      self->pos = NULL;
    }
    self->channels = 0;
    self->width = 0;
    self->func = NULL;

    if (self->pending_events) {
      g_list_foreach (self->pending_events, (GFunc) gst_mini_object_unref,
          NULL);
      g_list_free (self->pending_events);
      self->pending_events = NULL;
    }
  }

  gst_object_unref (self);

  return TRUE;
}
