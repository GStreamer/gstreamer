/*-*- Mode: C; c-basic-offset: 2 -*-*/

/*  GStreamer pulseaudio plugin
 *
 *  Copyright (c) 2011 Intel Corporation
 *                2011 Collabora
 *                2011 Arun Raghavan <arun.raghavan@collabora.co.uk>
 *                2011 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 *  gst-pulse is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or (at your option) any later version.
 *
 *  gst-pulse is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with gst-pulse; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA.
 */

/**
 * SECTION:element-pulseaudiosink
 * @see_also: pulsesink, pulsesrc, pulsemixer
 *
 * This element outputs audio to a
 * <ulink href="http://www.pulseaudio.org">PulseAudio sound server</ulink> via
 * the @pulsesink element. It transparently takes care of passing compressed
 * format as-is if the sink supports it, decoding if necessary, and changes
 * to supported formats at runtime.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! pulseaudiosink
 * ]| Decode and play an Ogg/Vorbis file.
 * |[
 * gst-launch -v filesrc location=test.mp3 ! mp3parse ! pulseaudiosink stream-properties="props,media.title=test"
 * ]| Play an MP3 file on a sink that supports decoding directly, plug in a
 * decoder if/when required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_PULSE_1_0

#include <gst/pbutils/pbutils.h>
#include <gst/gst-i18n-plugin.h>
#include <gst/glib-compat-private.h>

#include <gst/audio/gstaudioiec61937.h>
#include "pulsesink.h"

GST_DEBUG_CATEGORY (pulseaudiosink_debug);
#define GST_CAT_DEFAULT (pulseaudiosink_debug)

#define GST_PULSE_AUDIO_SINK_LOCK(obj) G_STMT_START {                    \
    GST_LOG_OBJECT (obj,                                              \
                    "locking from thread %p",                         \
                    g_thread_self ());                                \
    g_mutex_lock (GST_PULSE_AUDIO_SINK_CAST(obj)->lock);                 \
    GST_LOG_OBJECT (obj,                                              \
                    "locked from thread %p",                          \
                    g_thread_self ());                                \
} G_STMT_END

#define GST_PULSE_AUDIO_SINK_UNLOCK(obj) G_STMT_START {                  \
    GST_LOG_OBJECT (obj,                                              \
                    "unlocking from thread %p",                       \
                    g_thread_self ());                                \
    g_mutex_unlock (GST_PULSE_AUDIO_SINK_CAST(obj)->lock);               \
} G_STMT_END

typedef struct
{
  GstBin parent;
  GMutex *lock;

  GstPad *sinkpad;
  GstPad *sink_proxypad;
  GstPadEventFunction sinkpad_old_eventfunc;
  GstPadEventFunction proxypad_old_eventfunc;

  GstPulseSink *psink;
  GstElement *dbin2;

  GstSegment segment;

  guint event_probe_id;
  gulong pad_added_id;

  gboolean format_lost;
} GstPulseAudioSink;

typedef struct
{
  GstBinClass parent_class;
  guint n_prop_own;
  guint n_prop_total;
} GstPulseAudioSinkClass;

static void gst_pulse_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_pulse_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pulse_audio_sink_dispose (GObject * object);
static gboolean gst_pulse_audio_sink_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_pulse_audio_sink_sink_event (GstPad * pad,
    GstEvent * event);
static gboolean gst_pulse_audio_sink_sink_acceptcaps (GstPad * pad,
    GstCaps * caps);
static gboolean gst_pulse_audio_sink_sink_setcaps (GstPad * pad,
    GstCaps * caps);
static GstStateChangeReturn
gst_pulse_audio_sink_change_state (GstElement * element,
    GstStateChange transition);

static void
gst_pulse_audio_sink_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (pulseaudiosink_debug, "pulseaudiosink", 0,
      "Bin that wraps pulsesink for handling compressed formats");
}

GST_BOILERPLATE_FULL (GstPulseAudioSink, gst_pulse_audio_sink, GstBin,
    GST_TYPE_BIN, gst_pulse_audio_sink_do_init);

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (PULSE_SINK_TEMPLATE_CAPS));

static void
gst_pulse_audio_sink_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &sink_template);

  gst_element_class_set_details_simple (element_class,
      "Bin wrapping pulsesink", "Sink/Audio/Bin",
      "Correctly handles sink changes when streaming compressed formats to "
      "pulsesink", "Arun Raghavan <arun.raghavan@collabora.co.uk>");
}

static GParamSpec *
param_spec_copy (GParamSpec * spec)
{
  const char *name, *nick, *blurb;
  GParamFlags flags;

  name = g_param_spec_get_name (spec);
  nick = g_param_spec_get_nick (spec);
  blurb = g_param_spec_get_blurb (spec);
  flags = spec->flags;

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_BOOLEAN) {
    return g_param_spec_boolean (name, nick, blurb,
        G_PARAM_SPEC_BOOLEAN (spec)->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_BOXED) {
    return g_param_spec_boxed (name, nick, blurb, spec->value_type, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_CHAR) {
    GParamSpecChar *cspec = G_PARAM_SPEC_CHAR (spec);
    return g_param_spec_char (name, nick, blurb, cspec->minimum,
        cspec->maximum, cspec->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_DOUBLE) {
    GParamSpecDouble *dspec = G_PARAM_SPEC_DOUBLE (spec);
    return g_param_spec_double (name, nick, blurb, dspec->minimum,
        dspec->maximum, dspec->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_ENUM) {
    return g_param_spec_enum (name, nick, blurb, spec->value_type,
        G_PARAM_SPEC_ENUM (spec)->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_FLAGS) {
    return g_param_spec_flags (name, nick, blurb, spec->value_type,
        G_PARAM_SPEC_ENUM (spec)->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_FLOAT) {
    GParamSpecFloat *fspec = G_PARAM_SPEC_FLOAT (spec);
    return g_param_spec_double (name, nick, blurb, fspec->minimum,
        fspec->maximum, fspec->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_GTYPE) {
    return g_param_spec_gtype (name, nick, blurb,
        G_PARAM_SPEC_GTYPE (spec)->is_a_type, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_INT) {
    GParamSpecInt *ispec = G_PARAM_SPEC_INT (spec);
    return g_param_spec_int (name, nick, blurb, ispec->minimum,
        ispec->maximum, ispec->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_INT64) {
    GParamSpecInt64 *ispec = G_PARAM_SPEC_INT64 (spec);
    return g_param_spec_int64 (name, nick, blurb, ispec->minimum,
        ispec->maximum, ispec->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_LONG) {
    GParamSpecLong *lspec = G_PARAM_SPEC_LONG (spec);
    return g_param_spec_long (name, nick, blurb, lspec->minimum,
        lspec->maximum, lspec->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_OBJECT) {
    return g_param_spec_object (name, nick, blurb, spec->value_type, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_PARAM) {
    return g_param_spec_param (name, nick, blurb, spec->value_type, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_POINTER) {
    return g_param_spec_pointer (name, nick, blurb, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_STRING) {
    return g_param_spec_string (name, nick, blurb,
        G_PARAM_SPEC_STRING (spec)->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_UCHAR) {
    GParamSpecUChar *cspec = G_PARAM_SPEC_UCHAR (spec);
    return g_param_spec_uchar (name, nick, blurb, cspec->minimum,
        cspec->maximum, cspec->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_UINT) {
    GParamSpecUInt *ispec = G_PARAM_SPEC_UINT (spec);
    return g_param_spec_uint (name, nick, blurb, ispec->minimum,
        ispec->maximum, ispec->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_UINT64) {
    GParamSpecUInt64 *ispec = G_PARAM_SPEC_UINT64 (spec);
    return g_param_spec_uint64 (name, nick, blurb, ispec->minimum,
        ispec->maximum, ispec->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_ULONG) {
    GParamSpecULong *lspec = G_PARAM_SPEC_ULONG (spec);
    return g_param_spec_ulong (name, nick, blurb, lspec->minimum,
        lspec->maximum, lspec->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_UNICHAR) {
    return g_param_spec_unichar (name, nick, blurb,
        G_PARAM_SPEC_UNICHAR (spec)->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == G_TYPE_PARAM_VARIANT) {
    GParamSpecVariant *vspec = G_PARAM_SPEC_VARIANT (spec);
    return g_param_spec_variant (name, nick, blurb, vspec->type,
        vspec->default_value, flags);
  }

  if (G_PARAM_SPEC_TYPE (spec) == GST_TYPE_PARAM_MINI_OBJECT) {
    return gst_param_spec_mini_object (name, nick, blurb, spec->value_type,
        flags);
  }

  g_warning ("Unknown param type %ld for '%s'",
      (long) G_PARAM_SPEC_TYPE (spec), name);
  g_assert_not_reached ();
}

static void
gst_pulse_audio_sink_class_init (GstPulseAudioSinkClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstPulseSinkClass *psink_class =
      GST_PULSESINK_CLASS (g_type_class_ref (GST_TYPE_PULSESINK));
  GParamSpec **specs;
  guint n, i, j;

  gobject_class->get_property = gst_pulse_audio_sink_get_property;
  gobject_class->set_property = gst_pulse_audio_sink_set_property;
  gobject_class->dispose = gst_pulse_audio_sink_dispose;
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_pulse_audio_sink_change_state);

  /* Find out how many properties we already have */
  specs = g_object_class_list_properties (gobject_class, &klass->n_prop_own);
  g_free (specs);

  /* Proxy pulsesink's properties */
  specs = g_object_class_list_properties (G_OBJECT_CLASS (psink_class), &n);
  for (i = 0, j = klass->n_prop_own; i < n; i++) {
    if (g_object_class_find_property (gobject_class,
            g_param_spec_get_name (specs[i]))) {
      /* We already inherited this property from a parent, skip */
      j--;
    } else {
      g_object_class_install_property (gobject_class, i + j + 1,
          param_spec_copy (specs[i]));
    }
  }

  klass->n_prop_total = i + j;

  g_free (specs);
  g_type_class_unref (psink_class);
}

static GstPad *
get_proxypad (GstPad * sinkpad)
{
  GstIterator *iter = NULL;
  GstPad *proxypad = NULL;

  iter = gst_pad_iterate_internal_links (sinkpad);
  if (iter) {
    if (gst_iterator_next (iter, (gpointer) & proxypad) != GST_ITERATOR_OK)
      proxypad = NULL;
    gst_iterator_free (iter);
  }

  return proxypad;
}

static void
post_missing_element_message (GstPulseAudioSink * pbin, const gchar * name)
{
  GstMessage *msg;

  msg = gst_missing_element_message_new (GST_ELEMENT_CAST (pbin), name);
  gst_element_post_message (GST_ELEMENT_CAST (pbin), msg);
}

static void
notify_cb (GObject * selector, GParamSpec * pspec, GstPulseAudioSink * pbin)
{
  g_object_notify (G_OBJECT (pbin), g_param_spec_get_name (pspec));
}

static void
gst_pulse_audio_sink_init (GstPulseAudioSink * pbin,
    GstPulseAudioSinkClass * klass)
{
  GstPadTemplate *template;
  GstPad *pad = NULL;
  GParamSpec **specs;
  GString *prop;
  guint i;

  pbin->lock = g_mutex_new ();

  gst_segment_init (&pbin->segment, GST_FORMAT_UNDEFINED);

  pbin->psink = GST_PULSESINK (gst_element_factory_make ("pulsesink",
          "pulseaudiosink-sink"));
  g_assert (pbin->psink != NULL);

  if (!gst_bin_add (GST_BIN (pbin), GST_ELEMENT (pbin->psink))) {
    GST_ERROR_OBJECT (pbin, "Failed to add pulsesink to bin");
    goto error;
  }

  pad = gst_element_get_static_pad (GST_ELEMENT (pbin->psink), "sink");
  template = gst_static_pad_template_get (&sink_template);
  pbin->sinkpad = gst_ghost_pad_new_from_template ("sink", pad, template);
  gst_object_unref (template);

  pbin->sinkpad_old_eventfunc = GST_PAD_EVENTFUNC (pbin->sinkpad);
  gst_pad_set_event_function (pbin->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pulse_audio_sink_sink_event));
  gst_pad_set_setcaps_function (pbin->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pulse_audio_sink_sink_setcaps));
  gst_pad_set_acceptcaps_function (pbin->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pulse_audio_sink_sink_acceptcaps));

  gst_element_add_pad (GST_ELEMENT (pbin), pbin->sinkpad);

  if (!(pbin->sink_proxypad = get_proxypad (pbin->sinkpad)))
    GST_ERROR_OBJECT (pbin, "Failed to get proxypad of srcpad");
  else {
    pbin->proxypad_old_eventfunc = GST_PAD_EVENTFUNC (pbin->sink_proxypad);
    gst_pad_set_event_function (pbin->sink_proxypad,
        GST_DEBUG_FUNCPTR (gst_pulse_audio_sink_src_event));
  }

  /* Now proxy all the notify::* signals */
  specs = g_object_class_list_properties (G_OBJECT_CLASS (klass), &i);
  prop = g_string_sized_new (30);

  for (i--; i >= klass->n_prop_own; i--) {
    g_string_printf (prop, "notify::%s", g_param_spec_get_name (specs[i]));
    g_signal_connect (pbin->psink, prop->str, G_CALLBACK (notify_cb), pbin);
  }

  g_string_free (prop, TRUE);
  g_free (specs);

  pbin->format_lost = FALSE;

out:
  if (pad)
    gst_object_unref (pad);

  return;

error:
  if (pbin->psink)
    gst_object_unref (pbin->psink);
  goto out;
}

static void
gst_pulse_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPulseAudioSink *pbin = GST_PULSE_AUDIO_SINK (object);
  GstPulseAudioSinkClass *klass =
      GST_PULSE_AUDIO_SINK_CLASS (G_OBJECT_GET_CLASS (object));

  g_return_if_fail (prop_id <= klass->n_prop_total);

  g_object_set_property (G_OBJECT (pbin->psink), g_param_spec_get_name (pspec),
      value);
}

static void
gst_pulse_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPulseAudioSink *pbin = GST_PULSE_AUDIO_SINK (object);
  GstPulseAudioSinkClass *klass =
      GST_PULSE_AUDIO_SINK_CLASS (G_OBJECT_GET_CLASS (object));

  g_return_if_fail (prop_id <= klass->n_prop_total);

  g_object_get_property (G_OBJECT (pbin->psink), g_param_spec_get_name (pspec),
      value);
}

static void
gst_pulse_audio_sink_free_dbin2 (GstPulseAudioSink * pbin)
{
  g_signal_handler_disconnect (pbin->dbin2, pbin->pad_added_id);
  gst_element_set_state (pbin->dbin2, GST_STATE_NULL);

  gst_bin_remove (GST_BIN (pbin), pbin->dbin2);

  pbin->dbin2 = NULL;
}

static void
gst_pulse_audio_sink_dispose (GObject * object)
{
  GstPulseAudioSink *pbin = GST_PULSE_AUDIO_SINK (object);

  if (pbin->lock) {
    g_mutex_free (pbin->lock);
    pbin->lock = NULL;
  }

  if (pbin->sink_proxypad) {
    gst_object_unref (pbin->sink_proxypad);
    pbin->sink_proxypad = NULL;
  }

  if (pbin->dbin2) {
    g_signal_handler_disconnect (pbin->dbin2, pbin->pad_added_id);
    pbin->dbin2 = NULL;
  }

  pbin->sinkpad = NULL;
  pbin->psink = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_pulse_audio_sink_update_sinkpad (GstPulseAudioSink * pbin, GstPad * sinkpad)
{
  gboolean ret;

  ret = gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (pbin->sinkpad), sinkpad);

  if (!ret)
    GST_WARNING_OBJECT (pbin, "Could not update ghostpad target");

  return ret;
}

static void
distribute_running_time (GstElement * element, const GstSegment * segment)
{
  GstEvent *event;
  GstPad *pad;

  pad = gst_element_get_static_pad (element, "sink");

  /* FIXME: Some decoders collect newsegments and send them out at once, making
   * them lose accumulator events (and thus making dbin2_event_probe() hard to
   * do right if we're sending these as well. We can get away with not sending
   * these at the moment, but this should be fixed! */
#if 0
  if (segment->accum) {
    event = gst_event_new_new_segment_full (FALSE, segment->rate,
        segment->applied_rate, segment->format, 0, segment->accum, 0);
    gst_pad_send_event (pad, event);
  }
#endif

  event = gst_event_new_new_segment_full (FALSE, segment->rate,
      segment->applied_rate, segment->format,
      segment->start, segment->stop, segment->time);
  gst_pad_send_event (pad, event);

  gst_object_unref (pad);
}

static gboolean
dbin2_event_probe (GstPad * pad, GstMiniObject * obj, gpointer data)
{
  GstPulseAudioSink *pbin = GST_PULSE_AUDIO_SINK (data);
  GstEvent *event = GST_EVENT (obj);

  if (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT) {
    GST_DEBUG_OBJECT (pbin, "Got newsegment - dropping");
    gst_pad_remove_event_probe (pad, pbin->event_probe_id);
    return FALSE;
  }

  return TRUE;
}

static void
pad_added_cb (GstElement * dbin2, GstPad * pad, gpointer * data)
{
  GstPulseAudioSink *pbin;
  GstPad *sinkpad = NULL;

  pbin = GST_PULSE_AUDIO_SINK (data);
  sinkpad = gst_element_get_static_pad (GST_ELEMENT (pbin->psink), "sink");

  GST_PULSE_AUDIO_SINK_LOCK (pbin);
  if (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK)
    GST_ERROR_OBJECT (pbin, "Failed to link decodebin2 to pulsesink");
  else
    GST_DEBUG_OBJECT (pbin, "Linked new pad to pulsesink");
  GST_PULSE_AUDIO_SINK_UNLOCK (pbin);

  gst_object_unref (sinkpad);
}

/* Called with pbin lock held */
static void
gst_pulse_audio_sink_add_dbin2 (GstPulseAudioSink * pbin)
{
  GstPad *sinkpad = NULL;

  g_assert (pbin->dbin2 == NULL);

  pbin->dbin2 = gst_element_factory_make ("decodebin2", "pulseaudiosink-dbin2");

  if (!pbin->dbin2) {
    post_missing_element_message (pbin, "decodebin2");
    GST_ELEMENT_WARNING (pbin, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "decodebin2"), ("audio playback might fail"));
    goto out;
  }

  if (!gst_bin_add (GST_BIN (pbin), pbin->dbin2)) {
    GST_ERROR_OBJECT (pbin, "Failed to add decodebin2 to bin");
    goto out;
  }

  pbin->pad_added_id = g_signal_connect (pbin->dbin2, "pad-added",
      G_CALLBACK (pad_added_cb), pbin);

  if (!gst_element_sync_state_with_parent (pbin->dbin2)) {
    GST_ERROR_OBJECT (pbin, "Failed to set decodebin2 to parent state");
    goto out;
  }

  /* Trap the newsegment events that we feed the decodebin and discard them */
  sinkpad = gst_element_get_static_pad (GST_ELEMENT (pbin->psink), "sink");
  pbin->event_probe_id = gst_pad_add_event_probe_full (sinkpad,
      G_CALLBACK (dbin2_event_probe), gst_object_ref (pbin),
      (GDestroyNotify) gst_object_unref);
  gst_object_unref (sinkpad);
  sinkpad = NULL;

  GST_DEBUG_OBJECT (pbin, "Distributing running time to decodebin");
  distribute_running_time (pbin->dbin2, &pbin->segment);

  sinkpad = gst_element_get_static_pad (pbin->dbin2, "sink");

  gst_pulse_audio_sink_update_sinkpad (pbin, sinkpad);

out:
  if (sinkpad)
    gst_object_unref (sinkpad);
}

static void
update_eac3_alignment (GstPulseAudioSink * pbin)
{
  GstCaps *caps = gst_pad_peer_get_caps_reffed (pbin->sinkpad);
  GstStructure *st;

  if (!caps)
    return;

  st = gst_caps_get_structure (caps, 0);

  if (g_str_equal (gst_structure_get_name (st), "audio/x-eac3")) {
    GstStructure *event_st = gst_structure_new ("ac3parse-set-alignment",
        "alignment", G_TYPE_STRING, pbin->dbin2 ? "frame" : "iec61937", NULL);

    if (!gst_pad_push_event (pbin->sinkpad,
            gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM, event_st)))
      GST_WARNING_OBJECT (pbin->sinkpad, "Could not update alignment");
  }

  gst_caps_unref (caps);
}

static void
proxypad_blocked_cb (GstPad * pad, gboolean blocked, gpointer data)
{
  GstPulseAudioSink *pbin = GST_PULSE_AUDIO_SINK (data);
  GstCaps *caps;
  GstPad *sinkpad = NULL;

  if (!blocked) {
    /* Unblocked, don't need to do anything */
    GST_DEBUG_OBJECT (pbin, "unblocked");
    return;
  }

  GST_DEBUG_OBJECT (pbin, "blocked");

  GST_PULSE_AUDIO_SINK_LOCK (pbin);

  if (!pbin->format_lost) {
    sinkpad = gst_element_get_static_pad (GST_ELEMENT (pbin->psink), "sink");

    if (GST_PAD_CAPS (pbin->sinkpad)) {
      /* See if we already got caps on our sinkpad */
      caps = gst_caps_ref (GST_PAD_CAPS (pbin->sinkpad));
    } else {
      /* We haven't, so get caps from upstream */
      caps = gst_pad_get_caps_reffed (pad);
    }

    if (gst_pad_accept_caps (sinkpad, caps)) {
      if (pbin->dbin2) {
        GST_DEBUG_OBJECT (pbin, "Removing decodebin");
        gst_pulse_audio_sink_free_dbin2 (pbin);
        gst_pulse_audio_sink_update_sinkpad (pbin, sinkpad);
      } else
        GST_DEBUG_OBJECT (pbin, "Doing nothing");

      gst_caps_unref (caps);
      gst_object_unref (sinkpad);
      goto done;
    }
    /* pulsesink doesn't accept the incoming caps, so add a decodebin
     * (potentially after removing the existing once, since decodebin2 can't
     * renegotiate). */
  } else {
    /* Format lost, proceed to try plugging a decodebin */
    pbin->format_lost = FALSE;
  }

  if (pbin->dbin2 != NULL) {
    /* decodebin2 doesn't support reconfiguration, so throw this one away and
     * create a new one. */
    gst_pulse_audio_sink_free_dbin2 (pbin);
  }

  GST_DEBUG_OBJECT (pbin, "Adding decodebin");
  gst_pulse_audio_sink_add_dbin2 (pbin);

done:
  update_eac3_alignment (pbin);

  gst_pad_set_blocked_async_full (pad, FALSE, proxypad_blocked_cb,
      gst_object_ref (pbin), (GDestroyNotify) gst_object_unref);

  GST_PULSE_AUDIO_SINK_UNLOCK (pbin);
}

static gboolean
gst_pulse_audio_sink_src_event (GstPad * pad, GstEvent * event)
{
  GstPulseAudioSink *pbin = NULL;
  GstPad *ghostpad = NULL;
  gboolean ret = FALSE;

  ghostpad = GST_PAD_CAST (gst_pad_get_parent (pad));
  if (G_UNLIKELY (!ghostpad)) {
    GST_WARNING_OBJECT (pad, "Could not get ghostpad");
    goto out;
  }

  pbin = GST_PULSE_AUDIO_SINK (gst_pad_get_parent (ghostpad));
  if (G_UNLIKELY (!pbin)) {
    GST_WARNING_OBJECT (pad, "Could not get pulseaudiosink");
    goto out;
  }

  if (G_UNLIKELY (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_UPSTREAM) &&
      (gst_event_has_name (event, "pulse-format-lost") ||
          gst_event_has_name (event, "pulse-sink-changed"))) {
    g_return_val_if_fail (pad->mode != GST_ACTIVATE_PULL, FALSE);

    GST_PULSE_AUDIO_SINK_LOCK (pbin);
    if (gst_event_has_name (event, "pulse-format-lost"))
      pbin->format_lost = TRUE;

    if (!gst_pad_is_blocked (pad))
      gst_pad_set_blocked_async_full (pad, TRUE, proxypad_blocked_cb,
          gst_object_ref (pbin), (GDestroyNotify) gst_object_unref);
    GST_PULSE_AUDIO_SINK_UNLOCK (pbin);

    ret = TRUE;
  } else if (pbin->proxypad_old_eventfunc) {
    ret = pbin->proxypad_old_eventfunc (pad, event);
    event = NULL;
  }

out:
  if (ghostpad)
    gst_object_unref (ghostpad);
  if (pbin)
    gst_object_unref (pbin);
  if (event)
    gst_event_unref (event);

  return ret;
}

static gboolean
gst_pulse_audio_sink_sink_event (GstPad * pad, GstEvent * event)
{
  GstPulseAudioSink *pbin = GST_PULSE_AUDIO_SINK (gst_pad_get_parent (pad));
  gboolean ret;

  ret = pbin->sinkpad_old_eventfunc (pad, gst_event_ref (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      GST_PULSE_AUDIO_SINK_LOCK (pbin);
      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      GST_DEBUG_OBJECT (pbin,
          "newsegment: update %d, rate %g, arate %g, start %" GST_TIME_FORMAT
          ", stop %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT,
          update, rate, arate, GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
          GST_TIME_ARGS (time));

      if (format == GST_FORMAT_TIME) {
        /* Store the values for feeding to sub-elements */
        gst_segment_set_newsegment_full (&pbin->segment, update,
            rate, arate, format, start, stop, time);
      } else {
        GST_WARNING_OBJECT (pbin, "Got a non-TIME format segment");
        gst_segment_init (&pbin->segment, GST_FORMAT_TIME);
      }
      GST_PULSE_AUDIO_SINK_UNLOCK (pbin);

      break;
    }

    case GST_EVENT_FLUSH_STOP:
      GST_PULSE_AUDIO_SINK_LOCK (pbin);
      gst_segment_init (&pbin->segment, GST_FORMAT_UNDEFINED);
      GST_PULSE_AUDIO_SINK_UNLOCK (pbin);
      break;

    default:
      break;
  }

  gst_object_unref (pbin);
  gst_event_unref (event);

  return ret;
}

/* The bin's acceptcaps should be exactly equivalent to a pulsesink that is
 * connected to a sink that supports all the formats in template caps. This
 * means that upstream will have to have everything possibly upto a parser
 * plugged and we plugin a decoder whenever required. */
static gboolean
gst_pulse_audio_sink_sink_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstPulseAudioSink *pbin = GST_PULSE_AUDIO_SINK (gst_pad_get_parent (pad));
  GstRingBufferSpec spec = { 0 };
  const GstStructure *st;
  GstCaps *pad_caps = NULL;
  gboolean ret = FALSE;

  pad_caps = gst_pad_get_caps_reffed (pad);
  if (!pad_caps || !gst_caps_can_intersect (pad_caps, caps))
    goto out;

  /* If we've not got fixed caps, creating a stream might fail, so let's just
   * return from here with default acceptcaps behaviour */
  if (!gst_caps_is_fixed (caps))
    goto out;

  spec.latency_time = GST_BASE_AUDIO_SINK (pbin->psink)->latency_time;
  if (!gst_ring_buffer_parse_caps (&spec, caps))
    goto out;

  /* Make sure non-raw input is framed (one frame per buffer) and can be
   * payloaded */
  st = gst_caps_get_structure (caps, 0);

  if (!g_str_has_prefix (gst_structure_get_name (st), "audio/x-raw")) {
    gboolean framed = FALSE, parsed = FALSE;

    gst_structure_get_boolean (st, "framed", &framed);
    gst_structure_get_boolean (st, "parsed", &parsed);
    if ((!framed && !parsed) || gst_audio_iec61937_frame_size (&spec) <= 0)
      goto out;
  }

  ret = TRUE;

out:
  if (pad_caps)
    gst_caps_unref (pad_caps);

  gst_object_unref (pbin);

  return ret;
}

static gboolean
gst_pulse_audio_sink_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstPulseAudioSink *pbin = GST_PULSE_AUDIO_SINK (gst_pad_get_parent (pad));
  gboolean ret = TRUE;

  GST_PULSE_AUDIO_SINK_LOCK (pbin);

  if (!gst_pad_is_blocked (pbin->sinkpad))
    gst_pad_set_blocked_async_full (pbin->sink_proxypad, TRUE,
        proxypad_blocked_cb, gst_object_ref (pbin),
        (GDestroyNotify) gst_object_unref);

  GST_PULSE_AUDIO_SINK_UNLOCK (pbin);

  gst_object_unref (pbin);

  return ret;
}

static GstStateChangeReturn
gst_pulse_audio_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstPulseAudioSink *pbin = GST_PULSE_AUDIO_SINK (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  /* Nothing to do for upward transitions */
  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_PULSE_AUDIO_SINK_LOCK (pbin);
      if (gst_pad_is_blocked (pbin->sinkpad)) {
        gst_pad_set_blocked_async_full (pbin->sink_proxypad, FALSE,
            proxypad_blocked_cb, gst_object_ref (pbin),
            (GDestroyNotify) gst_object_unref);
      }
      GST_PULSE_AUDIO_SINK_UNLOCK (pbin);
      break;

    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS) {
    GST_DEBUG_OBJECT (pbin, "Base class returned %d on state change", ret);
    goto out;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_PULSE_AUDIO_SINK_LOCK (pbin);
      gst_segment_init (&pbin->segment, GST_FORMAT_UNDEFINED);

      if (pbin->dbin2) {
        GstPad *pad = gst_element_get_static_pad (GST_ELEMENT (pbin->psink),
            "sink");

        gst_pulse_audio_sink_free_dbin2 (pbin);
        gst_pulse_audio_sink_update_sinkpad (pbin, pad);

        gst_object_unref (pad);

      }
      GST_PULSE_AUDIO_SINK_UNLOCK (pbin);

      break;

    default:
      break;
  }

out:
  return ret;
}

#endif /* HAVE_PULSE_1_0 */
