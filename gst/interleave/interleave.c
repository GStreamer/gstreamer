/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *                    2007 Andy Wingo <wingo at pobox.com>
 *                    2008 Sebastian Dröge <slomo@circular-chaos.rg>
 *
 * interleave.c: interleave samples, mostly based on adder.
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
 *       - handle caps changes
 *       - handle more queries/events
 */

/**
 * SECTION:element-interleave
 * @see_also: deinterleave
 *
 * Merges separate mono inputs into one interleaved stream.
 * 
 * This element handles all raw floating point sample formats and all signed integer sample formats. The first
 * caps on one of the sinkpads will set the caps of the output so usually an audioconvert element should be
 * placed before every sinkpad of interleave.
 * 
 * It's possible to change the number of channels while the pipeline is running by adding or removing
 * some of the request pads but this will change the caps of the output buffers. Changing the input
 * caps is _not_ supported yet.
 * 
 * The channel number of every sinkpad in the out can be retrieved from the "channel" property of the pad.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=file.mp3 ! decodebin ! audioconvert ! "audio/x-raw-int,channels=2" ! deinterleave name=d  interleave name=i ! audioconvert ! wavenc ! filesink location=test.wav    d.src0 ! queue ! audioconvert ! i.sink1    d.src1 ! queue ! audioconvert ! i.sink0
 * ]| Decodes and deinterleaves a Stereo MP3 file into separate channels and
 * then interleaves the channels again to a WAV file with the channel with the
 * channels exchanged.
 * |[
 * gst-launch interleave name=i ! audioconvert ! wavenc ! filesink location=file.wav  filesrc location=file1.wav ! decodebin ! audioconvert ! "audio/x-raw-int,channels=1" ! queue ! i.sink0   filesrc location=file2.wav ! decodebin ! audioconvert ! "audio/x-raw-int,channels=1" ! queue ! i.sink1
 * ]| Interleaves two Mono WAV files to a single Stereo WAV file.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>
#include "interleave.h"

#include <gst/audio/multichannel.h>

GST_DEBUG_CATEGORY_STATIC (gst_interleave_debug);
#define GST_CAT_DEFAULT gst_interleave_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) 1, "
        "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        "width = (int) { 8, 16, 24, 32 }, "
        "depth = (int) [ 1, 32 ], "
        "signed = (boolean) true; "
        "audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) 1, "
        "endianness = (int) { LITTLE_ENDIAN , BIG_ENDIAN }, "
        "width = (int) { 32, 64 }")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
        "width = (int) { 8, 16, 24, 32 }, "
        "depth = (int) [ 1, 32 ], "
        "signed = (boolean) true; "
        "audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) { LITTLE_ENDIAN , BIG_ENDIAN }, "
        "width = (int) { 32, 64 }")
    );

#define MAKE_FUNC(type) \
static void interleave_##type (guint##type *out, guint##type *in, \
    guint stride, guint nframes) \
{ \
  gint i; \
  \
  for (i = 0; i < nframes; i++) { \
    *out = in[i]; \
    out += stride; \
  } \
}

MAKE_FUNC (8);
MAKE_FUNC (16);
MAKE_FUNC (32);
MAKE_FUNC (64);

static void
interleave_24 (guint8 * out, guint8 * in, guint stride, guint nframes)
{
  gint i;

  for (i = 0; i < nframes; i++) {
    memcpy (out, in, 3);
    out += stride * 3;
    in += 3;
  }
}

typedef struct
{
  GstPad parent;
  guint channel;
} GstInterleavePad;

enum
{
  PROP_PAD_0,
  PROP_PAD_CHANNEL
};

static void gst_interleave_pad_class_init (GstPadClass * klass);

#define GST_TYPE_INTERLEAVE_PAD (gst_interleave_pad_get_type())
#define GST_INTERLEAVE_PAD(pad) (G_TYPE_CHECK_INSTANCE_CAST((pad),GST_TYPE_INTERLEAVE_PAD,GstInterleavePad))
#define GST_INTERLEAVE_PAD_CAST(pad) ((GstInterleavePad *) pad)
#define GST_IS_INTERLEAVE_PAD(pad) (G_TYPE_CHECK_INSTANCE_TYPE((pad),GST_TYPE_INTERLEAVE_PAD))
static GType
gst_interleave_pad_get_type (void)
{
  static GType type = 0;

  if (G_UNLIKELY (type == 0)) {
    type = g_type_register_static_simple (GST_TYPE_PAD,
        g_intern_static_string ("GstInterleavePad"), sizeof (GstPadClass),
        (GClassInitFunc) gst_interleave_pad_class_init,
        sizeof (GstInterleavePad), NULL, 0);
  }
  return type;
}

static void
gst_interleave_pad_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstInterleavePad *self = GST_INTERLEAVE_PAD (object);

  switch (prop_id) {
    case PROP_PAD_CHANNEL:
      g_value_set_uint (value, self->channel);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_interleave_pad_class_init (GstPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->get_property = gst_interleave_pad_get_property;

  g_object_class_install_property (gobject_class,
      PROP_PAD_CHANNEL,
      g_param_spec_uint ("channel",
          "Channel number",
          "Number of the channel of this pad in the output", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

GST_BOILERPLATE (GstInterleave, gst_interleave, GstElement, GST_TYPE_ELEMENT);

enum
{
  PROP_0,
  PROP_CHANNEL_POSITIONS,
  PROP_CHANNEL_POSITIONS_FROM_INPUT
};

static void gst_interleave_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_interleave_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPad *gst_interleave_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_interleave_release_pad (GstElement * element, GstPad * pad);

static GstStateChangeReturn gst_interleave_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_interleave_src_query (GstPad * pad, GstQuery * query);

static gboolean gst_interleave_src_event (GstPad * pad, GstEvent * event);

static gboolean gst_interleave_sink_event (GstPad * pad, GstEvent * event);

static gboolean gst_interleave_sink_setcaps (GstPad * pad, GstCaps * caps);

static GstCaps *gst_interleave_sink_getcaps (GstPad * pad);

static GstFlowReturn gst_interleave_collected (GstCollectPads * pads,
    GstInterleave * self);

static void
gst_interleave_finalize (GObject * object)
{
  GstInterleave *self = GST_INTERLEAVE (object);

  if (self->collect) {
    gst_object_unref (self->collect);
    self->collect = NULL;
  }

  if (self->channel_positions
      && self->channel_positions != self->input_channel_positions) {
    g_value_array_free (self->channel_positions);
    self->channel_positions = NULL;
  }

  if (self->input_channel_positions) {
    g_value_array_free (self->input_channel_positions);
    self->input_channel_positions = NULL;
  }

  gst_caps_replace (&self->sinkcaps, NULL);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_interleave_check_channel_positions (GValueArray * positions)
{
  gint i;
  guint channels;
  GstAudioChannelPosition *pos;
  gboolean ret;

  channels = positions->n_values;
  pos = g_new (GstAudioChannelPosition, positions->n_values);

  for (i = 0; i < channels; i++) {
    GValue *v = g_value_array_get_nth (positions, i);

    pos[i] = g_value_get_enum (v);
  }

  ret = gst_audio_check_channel_positions (pos, channels);
  g_free (pos);

  return ret;
}

static void
gst_interleave_set_channel_positions (GstInterleave * self, GstStructure * s)
{
  GValue pos_array = { 0, };
  gint i;

  g_value_init (&pos_array, GST_TYPE_ARRAY);

  if (self->channel_positions
      && self->channels == self->channel_positions->n_values
      && gst_interleave_check_channel_positions (self->channel_positions)) {
    GST_DEBUG_OBJECT (self, "Using provided channel positions");
    for (i = 0; i < self->channels; i++)
      gst_value_array_append_value (&pos_array,
          g_value_array_get_nth (self->channel_positions, i));
  } else {
    GValue pos_none = { 0, };

    GST_WARNING_OBJECT (self, "Using NONE channel positions");

    g_value_init (&pos_none, GST_TYPE_AUDIO_CHANNEL_POSITION);
    g_value_set_enum (&pos_none, GST_AUDIO_CHANNEL_POSITION_NONE);

    for (i = 0; i < self->channels; i++)
      gst_value_array_append_value (&pos_array, &pos_none);

    g_value_unset (&pos_none);
  }
  gst_structure_set_value (s, "channel-positions", &pos_array);
  g_value_unset (&pos_array);
}

static void
gst_interleave_base_init (gpointer g_class)
{
  gst_element_class_set_details_simple (g_class, "Audio interleaver",
      "Filter/Converter/Audio",
      "Folds many mono channels into one interleaved audio stream",
      "Andy Wingo <wingo at pobox.com>, "
      "Sebastian Dröge <slomo@circular-chaos.org>");

  gst_element_class_add_pad_template (g_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (g_class,
      gst_static_pad_template_get (&src_template));
}

static void
gst_interleave_class_init (GstInterleaveClass * klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_interleave_debug, "interleave", 0,
      "interleave element");

  /* Reference GstInterleavePad class to have the type registered from
   * a threadsafe context
   */
  g_type_class_ref (GST_TYPE_INTERLEAVE_PAD);

  gobject_class->finalize = gst_interleave_finalize;
  gobject_class->set_property = gst_interleave_set_property;
  gobject_class->get_property = gst_interleave_get_property;

  /**
   * GstInterleave:channel-positions
   * 
   * Channel positions: This property controls the channel positions
   * that are used on the src caps. The number of elements should be
   * the same as the number of sink pads and the array should contain
   * a valid list of channel positions. The n-th element of the array
   * is the position of the n-th sink pad.
   *
   * These channel positions will only be used if they're valid and the
   * number of elements is the same as the number of channels. If this
   * is not given a NONE layout will be used.
   *
   */
  g_object_class_install_property (gobject_class, PROP_CHANNEL_POSITIONS,
      g_param_spec_value_array ("channel-positions", "Channel positions",
          "Channel positions used on the output",
          g_param_spec_enum ("channel-position", "Channel position",
              "Channel position of the n-th input",
              GST_TYPE_AUDIO_CHANNEL_POSITION,
              GST_AUDIO_CHANNEL_POSITION_NONE,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstInterleave:channel-positions-from-input
   * 
   * Channel positions from input: If this property is set to %TRUE the channel
   * positions will be taken from the input caps if valid channel positions for
   * the output can be constructed from them. If this is set to %TRUE setting the
   * channel-positions property overwrites this property again.
   *
   */
  g_object_class_install_property (gobject_class,
      PROP_CHANNEL_POSITIONS_FROM_INPUT,
      g_param_spec_boolean ("channel-positions-from-input",
          "Channel positions from input",
          "Take channel positions from the input", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_interleave_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_interleave_release_pad);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_interleave_change_state);
}

static void
gst_interleave_init (GstInterleave * self, GstInterleaveClass * klass)
{
  self->src = gst_pad_new_from_static_template (&src_template, "src");

  gst_pad_set_query_function (self->src,
      GST_DEBUG_FUNCPTR (gst_interleave_src_query));
  gst_pad_set_event_function (self->src,
      GST_DEBUG_FUNCPTR (gst_interleave_src_event));

  gst_element_add_pad (GST_ELEMENT (self), self->src);

  self->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (self->collect,
      (GstCollectPadsFunction) gst_interleave_collected, self);

  self->input_channel_positions = g_value_array_new (0);
  self->channel_positions_from_input = TRUE;
  self->channel_positions = self->input_channel_positions;
}

static void
gst_interleave_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstInterleave *self = GST_INTERLEAVE (object);

  switch (prop_id) {
    case PROP_CHANNEL_POSITIONS:
      if (self->channel_positions &&
          self->channel_positions != self->input_channel_positions)
        g_value_array_free (self->channel_positions);

      self->channel_positions = g_value_dup_boxed (value);
      self->channel_positions_from_input = FALSE;
      break;
    case PROP_CHANNEL_POSITIONS_FROM_INPUT:
      self->channel_positions_from_input = g_value_get_boolean (value);

      if (self->channel_positions_from_input) {
        if (self->channel_positions &&
            self->channel_positions != self->input_channel_positions)
          g_value_array_free (self->channel_positions);
        self->channel_positions = self->input_channel_positions;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_interleave_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstInterleave *self = GST_INTERLEAVE (object);

  switch (prop_id) {
    case PROP_CHANNEL_POSITIONS:
      g_value_set_boxed (value, self->channel_positions);
      break;
    case PROP_CHANNEL_POSITIONS_FROM_INPUT:
      g_value_set_boolean (value, self->channel_positions_from_input);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPad *
gst_interleave_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * req_name)
{
  GstInterleave *self = GST_INTERLEAVE (element);
  GstPad *new_pad;
  gchar *pad_name;
  gint channels, padnumber;
  GValue val = { 0, };

  if (templ->direction != GST_PAD_SINK)
    goto not_sink_pad;

#if GLIB_CHECK_VERSION(2,29,5)
  channels = g_atomic_int_add (&self->channels, 1);
  padnumber = g_atomic_int_add (&self->padcounter, 1);
#else
  channels = g_atomic_int_exchange_and_add (&self->channels, 1);
  padnumber = g_atomic_int_exchange_and_add (&self->padcounter, 1);
#endif

  pad_name = g_strdup_printf ("sink%d", padnumber);
  new_pad = GST_PAD_CAST (g_object_new (GST_TYPE_INTERLEAVE_PAD,
          "name", pad_name, "direction", templ->direction,
          "template", templ, NULL));
  GST_INTERLEAVE_PAD_CAST (new_pad)->channel = channels;
  GST_DEBUG_OBJECT (self, "requested new pad %s", pad_name);
  g_free (pad_name);

  gst_pad_set_setcaps_function (new_pad,
      GST_DEBUG_FUNCPTR (gst_interleave_sink_setcaps));
  gst_pad_set_getcaps_function (new_pad,
      GST_DEBUG_FUNCPTR (gst_interleave_sink_getcaps));

  gst_collect_pads_add_pad (self->collect, new_pad, sizeof (GstCollectData));

  /* FIXME: hacked way to override/extend the event function of
   * GstCollectPads; because it sets its own event function giving the
   * element no access to events */
  self->collect_event = (GstPadEventFunction) GST_PAD_EVENTFUNC (new_pad);
  gst_pad_set_event_function (new_pad,
      GST_DEBUG_FUNCPTR (gst_interleave_sink_event));

  if (!gst_element_add_pad (element, new_pad))
    goto could_not_add;

  g_value_init (&val, GST_TYPE_AUDIO_CHANNEL_POSITION);
  g_value_set_enum (&val, GST_AUDIO_CHANNEL_POSITION_NONE);
  self->input_channel_positions =
      g_value_array_append (self->input_channel_positions, &val);
  g_value_unset (&val);

  /* Update the src caps if we already have them */
  if (self->sinkcaps) {
    GstCaps *srccaps;
    GstStructure *s;

    /* Take lock to make sure processing finishes first */
    GST_OBJECT_LOCK (self->collect);

    srccaps = gst_caps_copy (self->sinkcaps);
    s = gst_caps_get_structure (srccaps, 0);

    gst_structure_set (s, "channels", G_TYPE_INT, self->channels, NULL);
    gst_interleave_set_channel_positions (self, s);

    gst_pad_set_caps (self->src, srccaps);
    gst_caps_unref (srccaps);

    GST_OBJECT_UNLOCK (self->collect);
  }

  return new_pad;

  /* errors */
not_sink_pad:
  {
    g_warning ("interleave: requested new pad that is not a SINK pad\n");
    return NULL;
  }
could_not_add:
  {
    GST_DEBUG_OBJECT (self, "could not add pad %s", GST_PAD_NAME (new_pad));
    gst_collect_pads_remove_pad (self->collect, new_pad);
    gst_object_unref (new_pad);
    return NULL;
  }
}

static void
gst_interleave_release_pad (GstElement * element, GstPad * pad)
{
  GstInterleave *self = GST_INTERLEAVE (element);
  GList *l;

  g_return_if_fail (GST_IS_INTERLEAVE_PAD (pad));

  /* Take lock to make sure we're not changing this when processing buffers */
  GST_OBJECT_LOCK (self->collect);

  g_atomic_int_add (&self->channels, -1);

  g_value_array_remove (self->input_channel_positions,
      GST_INTERLEAVE_PAD_CAST (pad)->channel);

  /* Update channel numbers */
  GST_OBJECT_LOCK (self);
  for (l = GST_ELEMENT_CAST (self)->sinkpads; l != NULL; l = l->next) {
    GstInterleavePad *ipad = GST_INTERLEAVE_PAD (l->data);

    if (GST_INTERLEAVE_PAD_CAST (pad)->channel < ipad->channel)
      ipad->channel--;
  }
  GST_OBJECT_UNLOCK (self);

  /* Update the src caps if we already have them */
  if (self->sinkcaps) {
    if (self->channels > 0) {
      GstCaps *srccaps;
      GstStructure *s;

      srccaps = gst_caps_copy (self->sinkcaps);
      s = gst_caps_get_structure (srccaps, 0);

      gst_structure_set (s, "channels", G_TYPE_INT, self->channels, NULL);
      gst_interleave_set_channel_positions (self, s);

      gst_pad_set_caps (self->src, srccaps);
      gst_caps_unref (srccaps);
    } else {
      gst_caps_replace (&self->sinkcaps, NULL);
      gst_pad_set_caps (self->src, NULL);
    }
  }

  GST_OBJECT_UNLOCK (self->collect);

  gst_collect_pads_remove_pad (self->collect, pad);
  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_interleave_change_state (GstElement * element, GstStateChange transition)
{
  GstInterleave *self;
  GstStateChangeReturn ret;

  self = GST_INTERLEAVE (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->timestamp = 0;
      self->offset = 0;
      self->segment_pending = TRUE;
      self->segment_position = 0;
      self->segment_rate = 1.0;
      gst_segment_init (&self->segment, GST_FORMAT_UNDEFINED);
      gst_collect_pads_start (self->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  /* Stop before calling the parent's state change function as
   * GstCollectPads might take locks and we would deadlock in that
   * case
   */
  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY)
    gst_collect_pads_stop (self->collect);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_pad_set_caps (self->src, NULL);
      gst_caps_replace (&self->sinkcaps, NULL);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
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

/* we can only accept caps that we and downstream can handle. */
static GstCaps *
gst_interleave_sink_getcaps (GstPad * pad)
{
  GstInterleave *self = GST_INTERLEAVE (gst_pad_get_parent (pad));
  GstCaps *result, *peercaps, *sinkcaps;

  GST_OBJECT_LOCK (self);

  /* If we already have caps on one of the sink pads return them */
  if (self->sinkcaps) {
    result = gst_caps_copy (self->sinkcaps);
  } else {
    /* get the downstream possible caps */
    peercaps = gst_pad_peer_get_caps (self->src);
    /* get the allowed caps on this sinkpad */
    sinkcaps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
    __remove_channels (sinkcaps);
    if (peercaps) {
      __remove_channels (peercaps);
      /* if the peer has caps, intersect */
      GST_DEBUG_OBJECT (pad, "intersecting peer and template caps");
      result = gst_caps_intersect (peercaps, sinkcaps);
      gst_caps_unref (peercaps);
      gst_caps_unref (sinkcaps);
    } else {
      /* the peer has no caps (or there is no peer), just use the allowed caps
       * of this sinkpad. */
      GST_DEBUG_OBJECT (pad, "no peer caps, using sinkcaps");
      result = sinkcaps;
    }
    __set_channels (result, 1);
  }

  GST_OBJECT_UNLOCK (self);

  gst_object_unref (self);

  GST_DEBUG_OBJECT (pad, "Returning caps %" GST_PTR_FORMAT, result);

  return result;
}

static void
gst_interleave_set_process_function (GstInterleave * self)
{
  switch (self->width) {
    case 8:
      self->func = (GstInterleaveFunc) interleave_8;
      break;
    case 16:
      self->func = (GstInterleaveFunc) interleave_16;
      break;
    case 24:
      self->func = (GstInterleaveFunc) interleave_24;
      break;
    case 32:
      self->func = (GstInterleaveFunc) interleave_32;
      break;
    case 64:
      self->func = (GstInterleaveFunc) interleave_64;
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static gboolean
gst_interleave_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstInterleave *self;

  g_return_val_if_fail (GST_IS_INTERLEAVE_PAD (pad), FALSE);

  self = GST_INTERLEAVE (gst_pad_get_parent (pad));

  /* First caps that are set on a sink pad are used as output caps */
  /* TODO: handle caps changes */
  if (self->sinkcaps && !gst_caps_is_subset (caps, self->sinkcaps)) {
    goto cannot_change_caps;
  } else {
    GstCaps *srccaps;
    GstStructure *s;
    gboolean res;

    s = gst_caps_get_structure (caps, 0);

    if (!gst_structure_get_int (s, "width", &self->width))
      goto no_width;

    if (!gst_structure_get_int (s, "rate", &self->rate))
      goto no_rate;

    gst_interleave_set_process_function (self);

    if (gst_structure_has_field (s, "channel-positions")) {
      const GValue *pos_array;

      pos_array = gst_structure_get_value (s, "channel-positions");
      if (GST_VALUE_HOLDS_ARRAY (pos_array)
          && gst_value_array_get_size (pos_array) == 1) {
        const GValue *pos = gst_value_array_get_value (pos_array, 0);

        GValue *apos = g_value_array_get_nth (self->input_channel_positions,
            GST_INTERLEAVE_PAD_CAST (pad)->channel);

        g_value_set_enum (apos, g_value_get_enum (pos));
      }
    }

    srccaps = gst_caps_copy (caps);
    s = gst_caps_get_structure (srccaps, 0);

    gst_structure_set (s, "channels", G_TYPE_INT, self->channels, NULL);
    gst_interleave_set_channel_positions (self, s);

    res = gst_pad_set_caps (self->src, srccaps);
    gst_caps_unref (srccaps);

    if (!res)
      goto src_did_not_accept;
  }

  if (!self->sinkcaps) {
    GstCaps *sinkcaps = gst_caps_copy (caps);
    GstStructure *s = gst_caps_get_structure (sinkcaps, 0);

    gst_structure_remove_field (s, "channel-positions");

    gst_caps_replace (&self->sinkcaps, sinkcaps);

    gst_caps_unref (sinkcaps);
  }

  gst_object_unref (self);

  return TRUE;

cannot_change_caps:
  {
    GST_WARNING_OBJECT (self, "caps of %" GST_PTR_FORMAT " already set, can't "
        "change", self->sinkcaps);
    gst_object_unref (self);
    return FALSE;
  }
src_did_not_accept:
  {
    GST_WARNING_OBJECT (self, "src did not accept setcaps()");
    gst_object_unref (self);
    return FALSE;
  }
no_width:
  {
    GST_WARNING_OBJECT (self, "caps did not have width: %" GST_PTR_FORMAT,
        caps);
    gst_object_unref (self);
    return FALSE;
  }
no_rate:
  {
    GST_WARNING_OBJECT (self, "caps did not have rate: %" GST_PTR_FORMAT, caps);
    gst_object_unref (self);
    return FALSE;
  }
}

static gboolean
gst_interleave_sink_event (GstPad * pad, GstEvent * event)
{
  GstInterleave *self = GST_INTERLEAVE (gst_pad_get_parent (pad));
  gboolean ret;

  GST_DEBUG ("Got %s event on pad %s:%s", GST_EVENT_TYPE_NAME (event),
      GST_DEBUG_PAD_NAME (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      /* mark a pending new segment. This event is synchronized
       * with the streaming thread so we can safely update the
       * variable without races. It's somewhat weird because we
       * assume the collectpads forwarded the FLUSH_STOP past us
       * and downstream (using our source pad, the bastard!).
       */
      self->segment_pending = TRUE;
      break;
    default:
      break;
  }

  /* now GstCollectPads can take care of the rest, e.g. EOS */
  ret = self->collect_event (pad, event);

  gst_object_unref (self);
  return ret;
}

static gboolean
gst_interleave_src_query_duration (GstInterleave * self, GstQuery * query)
{
  gint64 max;
  gboolean res;
  GstFormat format;
  GstIterator *it;
  gboolean done;

  /* parse format */
  gst_query_parse_duration (query, &format, NULL);

  max = -1;
  res = TRUE;
  done = FALSE;

  /* Take maximum of all durations */
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (self));
  while (!done) {
    GstIteratorResult ires;

    gpointer item;

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = GST_PAD_CAST (item);

        gint64 duration;

        /* ask sink peer for duration */
        res &= gst_pad_query_peer_duration (pad, &format, &duration);
        /* take max from all valid return values */
        if (res) {
          /* valid unknown length, stop searching */
          if (duration == -1) {
            max = duration;
            done = TRUE;
          }
          /* else see if bigger than current max */
          else if (duration > max)
            max = duration;
        }
        gst_object_unref (pad);
        break;
      }
      case GST_ITERATOR_RESYNC:
        max = -1;
        res = TRUE;
        gst_iterator_resync (it);
        break;
      default:
        res = FALSE;
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (it);

  if (res) {
    /* If in bytes format we have to multiply with the number of channels
     * to get the correct results. All other formats should be fine */
    if (format == GST_FORMAT_BYTES && max != -1)
      max *= self->channels;

    /* and store the max */
    GST_DEBUG_OBJECT (self, "Total duration in format %s: %"
        GST_TIME_FORMAT, gst_format_get_name (format), GST_TIME_ARGS (max));
    gst_query_set_duration (query, format, max);
  }

  return res;
}

static gboolean
gst_interleave_src_query_latency (GstInterleave * self, GstQuery * query)
{
  GstClockTime min, max;
  gboolean live;
  gboolean res;
  GstIterator *it;
  gboolean done;

  res = TRUE;
  done = FALSE;

  live = FALSE;
  min = 0;
  max = GST_CLOCK_TIME_NONE;

  /* Take maximum of all latency values */
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (self));
  while (!done) {
    GstIteratorResult ires;
    gpointer item;

    ires = gst_iterator_next (it, &item);
    switch (ires) {
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_OK:
      {
        GstPad *pad = GST_PAD_CAST (item);
        GstQuery *peerquery;
        GstClockTime min_cur, max_cur;
        gboolean live_cur;

        peerquery = gst_query_new_latency ();

        /* Ask peer for latency */
        res &= gst_pad_peer_query (pad, peerquery);

        /* take max from all valid return values */
        if (res) {
          gst_query_parse_latency (peerquery, &live_cur, &min_cur, &max_cur);

          if (min_cur > min)
            min = min_cur;

          if (max_cur != GST_CLOCK_TIME_NONE &&
              ((max != GST_CLOCK_TIME_NONE && max_cur > max) ||
                  (max == GST_CLOCK_TIME_NONE)))
            max = max_cur;

          live = live || live_cur;
        }

        gst_query_unref (peerquery);
        gst_object_unref (pad);
        break;
      }
      case GST_ITERATOR_RESYNC:
        live = FALSE;
        min = 0;
        max = GST_CLOCK_TIME_NONE;
        res = TRUE;
        gst_iterator_resync (it);
        break;
      default:
        res = FALSE;
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (it);

  if (res) {
    /* store the results */
    GST_DEBUG_OBJECT (self, "Calculated total latency: live %s, min %"
        GST_TIME_FORMAT ", max %" GST_TIME_FORMAT,
        (live ? "yes" : "no"), GST_TIME_ARGS (min), GST_TIME_ARGS (max));
    gst_query_set_latency (query, live, min, max);
  }

  return res;
}

static gboolean
gst_interleave_src_query (GstPad * pad, GstQuery * query)
{
  GstInterleave *self = GST_INTERLEAVE (gst_pad_get_parent (pad));
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          /* FIXME, bring to stream time, might be tricky */
          gst_query_set_position (query, format, self->timestamp);
          res = TRUE;
          break;
        case GST_FORMAT_BYTES:
          gst_query_set_position (query, format,
              self->offset * self->channels * self->width);
          res = TRUE;
          break;
        case GST_FORMAT_DEFAULT:
          gst_query_set_position (query, format, self->offset);
          res = TRUE;
          break;
        default:
          break;
      }
      break;
    }
    case GST_QUERY_DURATION:
      res = gst_interleave_src_query_duration (self, query);
      break;
    case GST_QUERY_LATENCY:
      res = gst_interleave_src_query_latency (self, query);
      break;
    default:
      /* FIXME, needs a custom query handler because we have multiple
       * sinkpads */
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (self);
  return res;
}

static gboolean
forward_event_func (GstPad * pad, GValue * ret, GstEvent * event)
{
  gst_event_ref (event);
  GST_LOG_OBJECT (pad, "About to send event %s", GST_EVENT_TYPE_NAME (event));
  if (!gst_pad_push_event (pad, event)) {
    g_value_set_boolean (ret, FALSE);
    GST_WARNING_OBJECT (pad, "Sending event  %p (%s) failed.",
        event, GST_EVENT_TYPE_NAME (event));
  } else {
    GST_LOG_OBJECT (pad, "Sent event  %p (%s).",
        event, GST_EVENT_TYPE_NAME (event));
  }
  gst_object_unref (pad);
  return TRUE;
}

static gboolean
forward_event (GstInterleave * self, GstEvent * event)
{
  GstIterator *it;
  GValue vret = { 0 };

  GST_LOG_OBJECT (self, "Forwarding event %p (%s)", event,
      GST_EVENT_TYPE_NAME (event));

  g_value_init (&vret, G_TYPE_BOOLEAN);
  g_value_set_boolean (&vret, TRUE);
  it = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (self));
  gst_iterator_fold (it, (GstIteratorFoldFunction) forward_event_func, &vret,
      event);
  gst_iterator_free (it);
  gst_event_unref (event);

  return g_value_get_boolean (&vret);
}


static gboolean
gst_interleave_src_event (GstPad * pad, GstEvent * event)
{
  GstInterleave *self = GST_INTERLEAVE (gst_pad_get_parent (pad));
  gboolean result;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
      /* QoS might be tricky */
      result = FALSE;
      break;
    case GST_EVENT_SEEK:
    {
      GstSeekFlags flags;
      GstSeekType curtype;
      gint64 cur;

      /* parse the seek parameters */
      gst_event_parse_seek (event, &self->segment_rate, NULL, &flags, &curtype,
          &cur, NULL, NULL);

      /* check if we are flushing */
      if (flags & GST_SEEK_FLAG_FLUSH) {
        /* make sure we accept nothing anymore and return WRONG_STATE */
        gst_collect_pads_set_flushing (self->collect, TRUE);

        /* flushing seek, start flush downstream, the flush will be done
         * when all pads received a FLUSH_STOP. */
        gst_pad_push_event (self->src, gst_event_new_flush_start ());
      }

      /* now wait for the collected to be finished and mark a new
       * segment */
      GST_OBJECT_LOCK (self->collect);
      if (curtype == GST_SEEK_TYPE_SET)
        self->segment_position = cur;
      else
        self->segment_position = 0;
      self->segment_pending = TRUE;
      GST_OBJECT_UNLOCK (self->collect);

      result = forward_event (self, event);
      break;
    }
    case GST_EVENT_NAVIGATION:
      /* navigation is rather pointless. */
      result = FALSE;
      break;
    default:
      /* just forward the rest for now */
      result = forward_event (self, event);
      break;
  }
  gst_object_unref (self);

  return result;
}

static GstFlowReturn
gst_interleave_collected (GstCollectPads * pads, GstInterleave * self)
{
  guint size;
  GstBuffer *outbuf;
  GstFlowReturn ret = GST_FLOW_OK;
  GSList *collected;
  guint nsamples;
  guint ncollected = 0;
  gboolean empty = TRUE;
  gint width = self->width / 8;

  g_return_val_if_fail (self->func != NULL, GST_FLOW_NOT_NEGOTIATED);
  g_return_val_if_fail (self->width > 0, GST_FLOW_NOT_NEGOTIATED);
  g_return_val_if_fail (self->channels > 0, GST_FLOW_NOT_NEGOTIATED);
  g_return_val_if_fail (self->rate > 0, GST_FLOW_NOT_NEGOTIATED);

  size = gst_collect_pads_available (pads);

  g_return_val_if_fail (size % width == 0, GST_FLOW_ERROR);

  GST_DEBUG_OBJECT (self, "Starting to collect %u bytes from %d channels", size,
      self->channels);

  nsamples = size / width;

  ret =
      gst_pad_alloc_buffer (self->src, GST_BUFFER_OFFSET_NONE,
      size * self->channels, GST_PAD_CAPS (self->src), &outbuf);

  if (ret != GST_FLOW_OK) {
    return ret;
  } else if (outbuf == NULL || GST_BUFFER_SIZE (outbuf) < size * self->channels) {
    gst_buffer_unref (outbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  } else if (!gst_caps_is_equal (GST_BUFFER_CAPS (outbuf),
          GST_PAD_CAPS (self->src))) {
    gst_buffer_unref (outbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  memset (GST_BUFFER_DATA (outbuf), 0, size * self->channels);

  for (collected = pads->data; collected != NULL; collected = collected->next) {
    GstCollectData *cdata;
    GstBuffer *inbuf;
    guint8 *outdata;

    cdata = (GstCollectData *) collected->data;

    inbuf = gst_collect_pads_take_buffer (pads, cdata, size);
    if (inbuf == NULL) {
      GST_DEBUG_OBJECT (cdata->pad, "No buffer available");
      goto next;
    }
    ncollected++;

    if (GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_GAP))
      goto next;

    empty = FALSE;
    outdata =
        GST_BUFFER_DATA (outbuf) +
        width * GST_INTERLEAVE_PAD_CAST (cdata->pad)->channel;

    self->func (outdata, GST_BUFFER_DATA (inbuf), self->channels, nsamples);

  next:
    if (inbuf)
      gst_buffer_unref (inbuf);
  }

  if (ncollected == 0)
    goto eos;

  if (self->segment_pending) {
    GstEvent *event;

    event = gst_event_new_new_segment_full (FALSE, self->segment_rate,
        1.0, GST_FORMAT_TIME, self->timestamp, -1, self->segment_position);

    gst_pad_push_event (self->src, event);
    self->segment_pending = FALSE;
    self->segment_position = 0;
  }

  GST_BUFFER_TIMESTAMP (outbuf) = self->timestamp;
  GST_BUFFER_OFFSET (outbuf) = self->offset;

  self->offset += nsamples;
  self->timestamp = gst_util_uint64_scale_int (self->offset,
      GST_SECOND, self->rate);

  GST_BUFFER_DURATION (outbuf) = self->timestamp -
      GST_BUFFER_TIMESTAMP (outbuf);

  if (empty)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_GAP);

  GST_LOG_OBJECT (self, "pushing outbuf, timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));
  ret = gst_pad_push (self->src, outbuf);

  return ret;

eos:
  {
    GST_DEBUG_OBJECT (self, "no data available, must be EOS");
    gst_buffer_unref (outbuf);
    gst_pad_push_event (self->src, gst_event_new_eos ());
    return GST_FLOW_UNEXPECTED;
  }
}
