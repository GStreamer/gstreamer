/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 * SECTION:element-videorate
 *
 * This element takes an incoming stream of timestamped video frames.
 * It will produce a perfect stream that matches the source pad's framerate.
 *
 * The correction is performed by dropping and duplicating frames, no fancy
 * algorithm is used to interpolate frames (yet).
 *
 * By default the element will simply negotiate the same framerate on its
 * source and sink pad.
 *
 * This operation is useful to link to elements that require a perfect stream.
 * Typical examples are formats that do not store timestamps for video frames,
 * but only store a framerate, like Ogg and AVI.
 *
 * A conversion to a specific framerate can be forced by using filtered caps on
 * the source pad.
 *
 * The properties #GstVideoRate:in, #GstVideoRate:out, #GstVideoRate:duplicate
 * and #GstVideoRate:drop can be read to obtain information about number of
 * input frames, output frames, dropped frames (i.e. the number of unused input
 * frames) and duplicated frames (i.e. the number of times an input frame was
 * duplicated, beside being used normally).
 *
 * An input stream that needs no adjustments will thus never have dropped or
 * duplicated frames.
 *
 * When the #GstVideoRate:silent property is set to FALSE, a GObject property
 * notification will be emitted whenever one of the #GstVideoRate:duplicate or
 * #GstVideoRate:drop values changes.
 * This can potentially cause performance degradation.
 * Note that property notification will happen from the streaming thread, so
 * applications should be prepared for this.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=videotestsrc.ogg ! oggdemux ! theoradec ! videorate ! video/x-raw-yuv,framerate=15/1 ! xvimagesink
 * ]| Decode an Ogg/Theora file and adjust the framerate to 15 fps before playing.
 * To create the test Ogg/Theora file refer to the documentation of theoraenc.
 * |[
 * gst-launch -v v4lsrc ! videorate ! video/x-raw-yuv,framerate=25/2 ! theoraenc ! oggmux ! filesink location=v4l.ogg
 * ]| Capture video from a V4L device, and adjust the stream to 12.5 fps before
 * encoding to Ogg/Theora.
 * </refsect2>
 *
 * Last reviewed on 2006-09-02 (0.10.11)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideorate.h"

GST_DEBUG_CATEGORY_STATIC (video_rate_debug);
#define GST_CAT_DEFAULT video_rate_debug

/* GstVideoRate signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_SILENT          TRUE
#define DEFAULT_NEW_PREF        1.0
#define DEFAULT_SKIP_TO_FIRST   FALSE

enum
{
  ARG_0,
  ARG_IN,
  ARG_OUT,
  ARG_DUP,
  ARG_DROP,
  ARG_SILENT,
  ARG_NEW_PREF,
  ARG_SKIP_TO_FIRST
      /* FILL ME */
};

static GstStaticPadTemplate gst_video_rate_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv;"
        "video/x-raw-rgb;" "video/x-raw-gray;" "image/jpeg;" "image/png")
    );

static GstStaticPadTemplate gst_video_rate_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv;"
        "video/x-raw-rgb;" "video/x-raw-gray;" "image/jpeg;" "image/png")
    );

static void gst_video_rate_swap_prev (GstVideoRate * videorate,
    GstBuffer * buffer, gint64 time);
static gboolean gst_video_rate_event (GstPad * pad, GstEvent * event);
static gboolean gst_video_rate_query (GstPad * pad, GstQuery * query);
static GstFlowReturn gst_video_rate_chain (GstPad * pad, GstBuffer * buffer);

static void gst_video_rate_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_video_rate_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_video_rate_change_state (GstElement * element,
    GstStateChange transition);

/*static guint gst_video_rate_signals[LAST_SIGNAL] = { 0 }; */

GST_BOILERPLATE (GstVideoRate, gst_video_rate, GstElement, GST_TYPE_ELEMENT);

static void
gst_video_rate_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "Video rate adjuster", "Filter/Effect/Video",
      "Drops/duplicates/adjusts timestamps on video frames to make a perfect stream",
      "Wim Taymans <wim@fluendo.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_video_rate_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_video_rate_src_template));
}

static void
gst_video_rate_class_init (GstVideoRateClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->set_property = gst_video_rate_set_property;
  object_class->get_property = gst_video_rate_get_property;

  g_object_class_install_property (object_class, ARG_IN,
      g_param_spec_uint64 ("in", "In",
          "Number of input frames", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, ARG_OUT,
      g_param_spec_uint64 ("out", "Out", "Number of output frames", 0,
          G_MAXUINT64, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, ARG_DUP,
      g_param_spec_uint64 ("duplicate", "Duplicate",
          "Number of duplicated frames", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, ARG_DROP,
      g_param_spec_uint64 ("drop", "Drop", "Number of dropped frames", 0,
          G_MAXUINT64, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, ARG_SILENT,
      g_param_spec_boolean ("silent", "silent",
          "Don't emit notify for dropped and duplicated frames", DEFAULT_SILENT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, ARG_NEW_PREF,
      g_param_spec_double ("new-pref", "New Pref",
          "Value indicating how much to prefer new frames (unused)", 0.0, 1.0,
          DEFAULT_NEW_PREF, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstVideoRate:skip-to-first:
   * 
   * Don't produce buffers before the first one we receive.
   *
   * Since: 0.10.25
   */
  g_object_class_install_property (object_class, ARG_SKIP_TO_FIRST,
      g_param_spec_boolean ("skip-to-first", "Skip to first buffer",
          "Don't produce buffers before the first one we receive",
          DEFAULT_SKIP_TO_FIRST, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_video_rate_change_state);
}

/* return the caps that can be used on out_pad given in_caps on in_pad */
static gboolean
gst_video_rate_transformcaps (GstPad * in_pad, GstCaps * in_caps,
    GstPad * out_pad, GstCaps ** out_caps)
{
  GstCaps *intersect;
  const GstCaps *in_templ;
  gint i;
  GSList *extra_structures = NULL;
  GSList *iter;

  in_templ = gst_pad_get_pad_template_caps (in_pad);
  intersect = gst_caps_intersect (in_caps, in_templ);

  /* all possible framerates are allowed */
  for (i = 0; i < gst_caps_get_size (intersect); i++) {
    GstStructure *structure;

    structure = gst_caps_get_structure (intersect, i);

    if (gst_structure_has_field (structure, "framerate")) {
      GstStructure *copy_structure;

      copy_structure = gst_structure_copy (structure);
      gst_structure_set (copy_structure,
          "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
      extra_structures = g_slist_append (extra_structures, copy_structure);
    }
  }

  /* append the extra structures */
  for (iter = extra_structures; iter != NULL; iter = g_slist_next (iter)) {
    gst_caps_append_structure (intersect, (GstStructure *) iter->data);
  }
  g_slist_free (extra_structures);

  *out_caps = intersect;

  return TRUE;
}

static GstCaps *
gst_video_rate_getcaps (GstPad * pad)
{
  GstVideoRate *videorate;
  GstPad *otherpad;
  GstCaps *caps;

  videorate = GST_VIDEO_RATE (GST_PAD_PARENT (pad));

  otherpad = (pad == videorate->srcpad) ? videorate->sinkpad :
      videorate->srcpad;

  /* we can do what the peer can */
  caps = gst_pad_peer_get_caps (otherpad);
  if (caps) {
    GstCaps *transform;

    gst_video_rate_transformcaps (otherpad, caps, pad, &transform);
    gst_caps_unref (caps);
    caps = transform;
  } else {
    /* no peer, our padtemplate is enough then */
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  return caps;
}

static gboolean
gst_video_rate_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVideoRate *videorate;
  GstStructure *structure;
  gboolean ret = TRUE;
  GstPad *otherpad, *opeer;
  gint rate_numerator, rate_denominator;

  videorate = GST_VIDEO_RATE (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pad, "setcaps called %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_fraction (structure, "framerate",
          &rate_numerator, &rate_denominator))
    goto no_framerate;

  if (pad == videorate->srcpad) {
    videorate->to_rate_numerator = rate_numerator;
    videorate->to_rate_denominator = rate_denominator;
    otherpad = videorate->sinkpad;
  } else {
    videorate->from_rate_numerator = rate_numerator;
    videorate->from_rate_denominator = rate_denominator;
    otherpad = videorate->srcpad;
  }

  /* now try to find something for the peer */
  opeer = gst_pad_get_peer (otherpad);
  if (opeer) {
    if (gst_pad_accept_caps (opeer, caps)) {
      /* the peer accepts the caps as they are */
      gst_pad_set_caps (otherpad, caps);

      ret = TRUE;
    } else {
      GstCaps *peercaps;
      GstCaps *transform = NULL;

      ret = FALSE;

      /* see how we can transform the input caps */
      if (!gst_video_rate_transformcaps (pad, caps, otherpad, &transform))
        goto no_transform;

      /* see what the peer can do */
      peercaps = gst_pad_get_caps (opeer);

      GST_DEBUG_OBJECT (opeer, "icaps %" GST_PTR_FORMAT, peercaps);
      GST_DEBUG_OBJECT (videorate, "transform %" GST_PTR_FORMAT, transform);

      /* filter against our possibilities */
      caps = gst_caps_intersect (peercaps, transform);
      gst_caps_unref (peercaps);
      gst_caps_unref (transform);

      GST_DEBUG_OBJECT (videorate, "intersect %" GST_PTR_FORMAT, caps);

      /* take first possibility */
      gst_caps_truncate (caps);
      structure = gst_caps_get_structure (caps, 0);

      /* and fixate */
      gst_structure_fixate_field_nearest_fraction (structure, "framerate",
          rate_numerator, rate_denominator);

      gst_structure_get_fraction (structure, "framerate",
          &rate_numerator, &rate_denominator);

      if (otherpad == videorate->srcpad) {
        videorate->to_rate_numerator = rate_numerator;
        videorate->to_rate_denominator = rate_denominator;
      } else {
        videorate->from_rate_numerator = rate_numerator;
        videorate->from_rate_denominator = rate_denominator;
      }

      if (gst_structure_has_field (structure, "interlaced"))
        gst_structure_fixate_field_boolean (structure, "interlaced", FALSE);
      if (gst_structure_has_field (structure, "color-matrix"))
        gst_structure_fixate_field_string (structure, "color-matrix", "sdtv");
      if (gst_structure_has_field (structure, "chroma-site"))
        gst_structure_fixate_field_string (structure, "chroma-site", "mpeg2");

      gst_pad_set_caps (otherpad, caps);
      gst_caps_unref (caps);
      ret = TRUE;
    }
    gst_object_unref (opeer);
  }
done:
  /* After a setcaps, our caps may have changed. In that case, we can't use
   * the old buffer, if there was one (it might have different dimensions) */
  GST_DEBUG_OBJECT (videorate, "swapping old buffers");
  gst_video_rate_swap_prev (videorate, NULL, GST_CLOCK_TIME_NONE);

  gst_object_unref (videorate);
  return ret;

no_framerate:
  {
    GST_DEBUG_OBJECT (videorate, "no framerate specified");
    goto done;
  }
no_transform:
  {
    GST_DEBUG_OBJECT (videorate, "no framerate transform possible");
    ret = FALSE;
    goto done;
  }
}

static void
gst_video_rate_reset (GstVideoRate * videorate)
{
  GST_DEBUG_OBJECT (videorate, "resetting internal variables");

  videorate->in = 0;
  videorate->out = 0;
  videorate->segment_out = 0;
  videorate->drop = 0;
  videorate->dup = 0;
  videorate->next_ts = GST_CLOCK_TIME_NONE;
  videorate->last_ts = GST_CLOCK_TIME_NONE;
  videorate->discont = TRUE;
  gst_video_rate_swap_prev (videorate, NULL, 0);

  gst_segment_init (&videorate->segment, GST_FORMAT_TIME);
}

static void
gst_video_rate_init (GstVideoRate * videorate, GstVideoRateClass * klass)
{
  videorate->sinkpad =
      gst_pad_new_from_static_template (&gst_video_rate_sink_template, "sink");
  gst_pad_set_event_function (videorate->sinkpad,
      GST_DEBUG_FUNCPTR (gst_video_rate_event));
  gst_pad_set_chain_function (videorate->sinkpad,
      GST_DEBUG_FUNCPTR (gst_video_rate_chain));
  gst_pad_set_getcaps_function (videorate->sinkpad,
      GST_DEBUG_FUNCPTR (gst_video_rate_getcaps));
  gst_pad_set_setcaps_function (videorate->sinkpad,
      GST_DEBUG_FUNCPTR (gst_video_rate_setcaps));
  gst_element_add_pad (GST_ELEMENT (videorate), videorate->sinkpad);

  videorate->srcpad =
      gst_pad_new_from_static_template (&gst_video_rate_src_template, "src");
  gst_pad_set_query_function (videorate->srcpad,
      GST_DEBUG_FUNCPTR (gst_video_rate_query));
  gst_pad_set_getcaps_function (videorate->srcpad,
      GST_DEBUG_FUNCPTR (gst_video_rate_getcaps));
  gst_pad_set_setcaps_function (videorate->srcpad,
      GST_DEBUG_FUNCPTR (gst_video_rate_setcaps));
  gst_element_add_pad (GST_ELEMENT (videorate), videorate->srcpad);

  gst_video_rate_reset (videorate);
  videorate->silent = DEFAULT_SILENT;
  videorate->new_pref = DEFAULT_NEW_PREF;

  videorate->from_rate_numerator = 0;
  videorate->from_rate_denominator = 0;
  videorate->to_rate_numerator = 0;
  videorate->to_rate_denominator = 0;
}

/* flush the oldest buffer */
static GstFlowReturn
gst_video_rate_flush_prev (GstVideoRate * videorate)
{
  GstFlowReturn res;
  GstBuffer *outbuf;
  GstClockTime push_ts;

  if (!videorate->prevbuf)
    goto eos_before_buffers;

  /* make sure we can write to the metadata */
  outbuf = gst_buffer_make_metadata_writable
      (gst_buffer_ref (videorate->prevbuf));

  GST_BUFFER_OFFSET (outbuf) = videorate->out;
  GST_BUFFER_OFFSET_END (outbuf) = videorate->out + 1;

  if (videorate->discont) {
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    videorate->discont = FALSE;
  } else
    GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_FLAG_DISCONT);

  /* this is the timestamp we put on the buffer */
  push_ts = videorate->next_ts;

  videorate->out++;
  videorate->segment_out++;
  if (videorate->to_rate_numerator) {
    /* interpolate next expected timestamp in the segment */
    videorate->next_ts = videorate->segment.accum + videorate->segment.start +
        gst_util_uint64_scale (videorate->segment_out,
        videorate->to_rate_denominator * GST_SECOND,
        videorate->to_rate_numerator);
    GST_BUFFER_DURATION (outbuf) = videorate->next_ts - push_ts;
  }

  /* adapt for looping, bring back to time in current segment. */
  GST_BUFFER_TIMESTAMP (outbuf) = push_ts - videorate->segment.accum;

  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (videorate->srcpad));

  GST_LOG_OBJECT (videorate,
      "old is best, dup, pushing buffer outgoing ts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (push_ts));

  res = gst_pad_push (videorate->srcpad, outbuf);

  return res;

  /* WARNINGS */
eos_before_buffers:
  {
    GST_INFO_OBJECT (videorate, "got EOS before any buffer was received");
    return GST_FLOW_OK;
  }
}

static void
gst_video_rate_swap_prev (GstVideoRate * videorate, GstBuffer * buffer,
    gint64 time)
{
  GST_LOG_OBJECT (videorate, "swap_prev: storing buffer %p in prev", buffer);
  if (videorate->prevbuf)
    gst_buffer_unref (videorate->prevbuf);
  videorate->prevbuf = buffer;
  videorate->prev_ts = time;
}

#define MAGIC_LIMIT  25
static gboolean
gst_video_rate_event (GstPad * pad, GstEvent * event)
{
  GstVideoRate *videorate;
  gboolean ret;

  videorate = GST_VIDEO_RATE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gint64 start, stop, time;
      gdouble rate, arate;
      gboolean update;
      GstFormat format;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      if (format != GST_FORMAT_TIME)
        goto format_error;

      GST_DEBUG_OBJECT (videorate, "handle NEWSEGMENT");

      /* close up the previous segment, if appropriate */
      if (!update && videorate->prevbuf) {
        gint count = 0;
        GstFlowReturn res;

        res = GST_FLOW_OK;
        /* fill up to the end of current segment,
         * or only send out the stored buffer if there is no specific stop.
         * regardless, prevent going loopy in strange cases */
        while (res == GST_FLOW_OK && count <= MAGIC_LIMIT &&
            ((GST_CLOCK_TIME_IS_VALID (videorate->segment.stop) &&
                    videorate->next_ts - videorate->segment.accum
                    < videorate->segment.stop)
                || count < 1)) {
          gst_video_rate_flush_prev (videorate);
          count++;
        }
        if (count > 1) {
          videorate->dup += count - 1;
          if (!videorate->silent)
            g_object_notify (G_OBJECT (videorate), "duplicate");
        } else if (count == 0) {
          videorate->drop++;
          if (!videorate->silent)
            g_object_notify (G_OBJECT (videorate), "drop");
        }
        /* clean up for the new one; _chain will resume from the new start */
        videorate->segment_out = 0;
        gst_video_rate_swap_prev (videorate, NULL, 0);
        videorate->next_ts = GST_CLOCK_TIME_NONE;
      }

      /* We just want to update the accumulated stream_time  */
      gst_segment_set_newsegment_full (&videorate->segment, update, rate, arate,
          format, start, stop, time);

      GST_DEBUG_OBJECT (videorate, "updated segment: %" GST_SEGMENT_FORMAT,
          &videorate->segment);
      break;
    }
    case GST_EVENT_EOS:
      /* flush last queued frame */
      GST_DEBUG_OBJECT (videorate, "Got EOS");
      gst_video_rate_flush_prev (videorate);
      break;
    case GST_EVENT_FLUSH_STOP:
      /* also resets the segment */
      GST_DEBUG_OBJECT (videorate, "Got FLUSH_STOP");
      gst_video_rate_reset (videorate);
      break;
    default:
      break;
  }

  ret = gst_pad_push_event (videorate->srcpad, event);

done:
  gst_object_unref (videorate);

  return ret;

  /* ERRORS */
format_error:
  {
    GST_WARNING_OBJECT (videorate,
        "Got segment but doesn't have GST_FORMAT_TIME value");
    gst_event_unref (event);
    ret = FALSE;
    goto done;
  }
}

static gboolean
gst_video_rate_query (GstPad * pad, GstQuery * query)
{
  GstVideoRate *videorate;
  gboolean res = FALSE;

  videorate = GST_VIDEO_RATE (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime min, max;
      gboolean live;
      guint64 latency;
      GstPad *peer;

      if ((peer = gst_pad_get_peer (videorate->sinkpad))) {
        if ((res = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);

          GST_DEBUG_OBJECT (videorate, "Peer latency: min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          if (videorate->from_rate_numerator != 0) {
            /* add latency. We don't really know since we hold on to the frames
             * until we get a next frame, which can be anything. We assume
             * however that this will take from_rate time. */
            latency = gst_util_uint64_scale (GST_SECOND,
                videorate->from_rate_denominator,
                videorate->from_rate_numerator);
          } else {
            /* no input framerate, we don't know */
            latency = 0;
          }

          GST_DEBUG_OBJECT (videorate, "Our latency: %"
              GST_TIME_FORMAT, GST_TIME_ARGS (latency));

          min += latency;
          if (max != -1)
            max += latency;

          GST_DEBUG_OBJECT (videorate, "Calculated total latency : min %"
              GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
              GST_TIME_ARGS (min), GST_TIME_ARGS (max));

          gst_query_set_latency (query, live, min, max);
        }
        gst_object_unref (peer);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  gst_object_unref (videorate);

  return res;
}

static GstFlowReturn
gst_video_rate_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVideoRate *videorate;
  GstFlowReturn res = GST_FLOW_OK;
  GstClockTime intime, in_ts, in_dur;

  videorate = GST_VIDEO_RATE (GST_PAD_PARENT (pad));

  /* make sure the denominators are not 0 */
  if (videorate->from_rate_denominator == 0 ||
      videorate->to_rate_denominator == 0)
    goto not_negotiated;

  in_ts = GST_BUFFER_TIMESTAMP (buffer);
  in_dur = GST_BUFFER_DURATION (buffer);

  if (G_UNLIKELY (in_ts == GST_CLOCK_TIME_NONE)) {
    in_ts = videorate->last_ts;
    if (G_UNLIKELY (in_ts == GST_CLOCK_TIME_NONE))
      goto invalid_buffer;
  }

  /* get the time of the next expected buffer timestamp, we use this when the
   * next buffer has -1 as a timestamp */
  videorate->last_ts = in_ts;
  if (in_dur != GST_CLOCK_TIME_NONE)
    videorate->last_ts += in_dur;

  GST_DEBUG_OBJECT (videorate, "got buffer with timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (in_ts));

  /* the input time is the time in the segment + all previously accumulated
   * segments */
  intime = in_ts + videorate->segment.accum;

  /* we need to have two buffers to compare */
  if (videorate->prevbuf == NULL) {
    gst_video_rate_swap_prev (videorate, buffer, intime);
    videorate->in++;
    if (!GST_CLOCK_TIME_IS_VALID (videorate->next_ts)) {
      /* new buffer, we expect to output a buffer that matches the first
       * timestamp in the segment */
      if (videorate->skip_to_first) {
        videorate->next_ts = in_ts;
        videorate->segment_out = gst_util_uint64_scale (in_ts,
            videorate->to_rate_numerator,
            videorate->to_rate_denominator * GST_SECOND) -
            (videorate->segment.accum + videorate->segment.start);
      } else {
        videorate->next_ts =
            videorate->segment.start + videorate->segment.accum;
      }
    }
  } else {
    GstClockTime prevtime;
    gint count = 0;
    gint64 diff1, diff2;

    prevtime = videorate->prev_ts;

    GST_LOG_OBJECT (videorate,
        "BEGINNING prev buf %" GST_TIME_FORMAT " new buf %" GST_TIME_FORMAT
        " outgoing ts %" GST_TIME_FORMAT, GST_TIME_ARGS (prevtime),
        GST_TIME_ARGS (intime), GST_TIME_ARGS (videorate->next_ts));

    videorate->in++;

    /* drop new buffer if it's before previous one */
    if (intime < prevtime) {
      GST_DEBUG_OBJECT (videorate,
          "The new buffer (%" GST_TIME_FORMAT
          ") is before the previous buffer (%"
          GST_TIME_FORMAT "). Dropping new buffer.",
          GST_TIME_ARGS (intime), GST_TIME_ARGS (prevtime));
      videorate->drop++;
      if (!videorate->silent)
        g_object_notify (G_OBJECT (videorate), "drop");
      gst_buffer_unref (buffer);
      goto done;
    }

    /* got 2 buffers, see which one is the best */
    do {

      diff1 = prevtime - videorate->next_ts;
      diff2 = intime - videorate->next_ts;

      /* take absolute values, beware: abs and ABS don't work for gint64 */
      if (diff1 < 0)
        diff1 = -diff1;
      if (diff2 < 0)
        diff2 = -diff2;

      GST_LOG_OBJECT (videorate,
          "diff with prev %" GST_TIME_FORMAT " diff with new %"
          GST_TIME_FORMAT " outgoing ts %" GST_TIME_FORMAT,
          GST_TIME_ARGS (diff1), GST_TIME_ARGS (diff2),
          GST_TIME_ARGS (videorate->next_ts));

      /* output first one when its the best */
      if (diff1 <= diff2) {
        count++;

        /* on error the _flush function posted a warning already */
        if ((res = gst_video_rate_flush_prev (videorate)) != GST_FLOW_OK) {
          gst_buffer_unref (buffer);
          goto done;
        }
      }
      /* continue while the first one was the best, if they were equal avoid
       * going into an infinite loop */
    }
    while (diff1 < diff2);

    /* if we outputed the first buffer more then once, we have dups */
    if (count > 1) {
      videorate->dup += count - 1;
      if (!videorate->silent)
        g_object_notify (G_OBJECT (videorate), "duplicate");
    }
    /* if we didn't output the first buffer, we have a drop */
    else if (count == 0) {
      videorate->drop++;

      if (!videorate->silent)
        g_object_notify (G_OBJECT (videorate), "drop");

      GST_LOG_OBJECT (videorate,
          "new is best, old never used, drop, outgoing ts %"
          GST_TIME_FORMAT, GST_TIME_ARGS (videorate->next_ts));
    }
    GST_LOG_OBJECT (videorate,
        "END, putting new in old, diff1 %" GST_TIME_FORMAT
        ", diff2 %" GST_TIME_FORMAT ", next_ts %" GST_TIME_FORMAT
        ", in %" G_GUINT64_FORMAT ", out %" G_GUINT64_FORMAT ", drop %"
        G_GUINT64_FORMAT ", dup %" G_GUINT64_FORMAT, GST_TIME_ARGS (diff1),
        GST_TIME_ARGS (diff2), GST_TIME_ARGS (videorate->next_ts),
        videorate->in, videorate->out, videorate->drop, videorate->dup);

    /* swap in new one when it's the best */
    gst_video_rate_swap_prev (videorate, buffer, intime);
  }
done:
  return res;

  /* ERRORS */
not_negotiated:
  {
    GST_WARNING_OBJECT (videorate, "no framerate negotiated");
    gst_buffer_unref (buffer);
    res = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }

invalid_buffer:
  {
    GST_WARNING_OBJECT (videorate,
        "Got buffer with GST_CLOCK_TIME_NONE timestamp, discarding it");
    gst_buffer_unref (buffer);
    goto done;
  }
}

static void
gst_video_rate_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstVideoRate *videorate = GST_VIDEO_RATE (object);

  switch (prop_id) {
    case ARG_SILENT:
      videorate->silent = g_value_get_boolean (value);
      break;
    case ARG_NEW_PREF:
      videorate->new_pref = g_value_get_double (value);
      break;
    case ARG_SKIP_TO_FIRST:
      videorate->skip_to_first = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_rate_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVideoRate *videorate = GST_VIDEO_RATE (object);

  switch (prop_id) {
    case ARG_IN:
      g_value_set_uint64 (value, videorate->in);
      break;
    case ARG_OUT:
      g_value_set_uint64 (value, videorate->out);
      break;
    case ARG_DUP:
      g_value_set_uint64 (value, videorate->dup);
      break;
    case ARG_DROP:
      g_value_set_uint64 (value, videorate->drop);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, videorate->silent);
      break;
    case ARG_NEW_PREF:
      g_value_set_double (value, videorate->new_pref);
      break;
    case ARG_SKIP_TO_FIRST:
      g_value_set_boolean (value, videorate->skip_to_first);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_video_rate_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstVideoRate *videorate;

  videorate = GST_VIDEO_RATE (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      videorate->discont = TRUE;
      videorate->last_ts = -1;
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_video_rate_reset (videorate);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (video_rate_debug, "videorate", 0,
      "VideoRate stream fixer");

  return gst_element_register (plugin, "videorate", GST_RANK_NONE,
      GST_TYPE_VIDEO_RATE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videorate",
    "Adjusts video frames",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
