/* GStreamer
 * Copyright (C) 2008 Jan Schmidt <thaytan@noraisin.net>
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
#  include <config.h>
#endif

#include <string.h>

#include <gst/gst.h>
#include <gst/video/video.h>

#include "rsnaudiomunge.h"

GST_DEBUG_CATEGORY_STATIC (rsn_audiomunge_debug);
#define GST_CAT_DEFAULT rsn_audiomunge_debug

#define AUDIO_FILL_THRESHOLD (GST_SECOND/5)

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

G_DEFINE_TYPE (RsnAudioMunge, rsn_audiomunge, GST_TYPE_ELEMENT);

static void rsn_audiomunge_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void rsn_audiomunge_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean rsn_audiomunge_set_caps (GstPad * pad, GstCaps * caps);
static GstFlowReturn rsn_audiomunge_chain (GstPad * pad, GstBuffer * buf);
static gboolean rsn_audiomunge_sink_event (GstPad * pad, GstEvent * event);

static GstStateChangeReturn
rsn_audiomunge_change_state (GstElement * element, GstStateChange transition);

static void
rsn_audiomunge_class_init (RsnAudioMungeClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) (klass);
  GstElementClass *element_class = (GstElementClass *) (klass);

  GST_DEBUG_CATEGORY_INIT (rsn_audiomunge_debug, "rsnaudiomunge",
      0, "ResinDVD audio stream regulator");

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class,
      &sink_template);

  gst_element_class_set_details_simple (element_class, "RsnAudioMunge",
      "Audio/Filter",
      "Resin DVD audio stream regulator", "Jan Schmidt <thaytan@noraisin.net>");

  gobject_class->set_property = rsn_audiomunge_set_property;
  gobject_class->get_property = rsn_audiomunge_get_property;

  element_class->change_state = rsn_audiomunge_change_state;
}

static void
rsn_audiomunge_init (RsnAudioMunge * munge)
{
  munge->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_setcaps_function (munge->sinkpad,
      GST_DEBUG_FUNCPTR (rsn_audiomunge_set_caps));
  gst_pad_set_getcaps_function (munge->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_chain_function (munge->sinkpad,
      GST_DEBUG_FUNCPTR (rsn_audiomunge_chain));
  gst_pad_set_event_function (munge->sinkpad,
      GST_DEBUG_FUNCPTR (rsn_audiomunge_sink_event));
  gst_element_add_pad (GST_ELEMENT (munge), munge->sinkpad);

  munge->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_getcaps_function (munge->srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_element_add_pad (GST_ELEMENT (munge), munge->srcpad);
}

static void
rsn_audiomunge_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //RsnAudioMunge *munge = RSN_AUDIOMUNGE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
rsn_audiomunge_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //RsnAudioMunge *munge = RSN_AUDIOMUNGE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
rsn_audiomunge_set_caps (GstPad * pad, GstCaps * caps)
{
  RsnAudioMunge *munge = RSN_AUDIOMUNGE (gst_pad_get_parent (pad));
  GstPad *otherpad;
  gboolean ret;

  g_return_val_if_fail (munge != NULL, FALSE);

  otherpad = (pad == munge->srcpad) ? munge->sinkpad : munge->srcpad;

  ret = gst_pad_set_caps (otherpad, caps);
  gst_object_unref (munge);
  return ret;
}

static void
rsn_audiomunge_reset (RsnAudioMunge * munge)
{
  munge->have_audio = FALSE;
  munge->in_still = FALSE;
  gst_segment_init (&munge->sink_segment, GST_FORMAT_TIME);
}

static GstFlowReturn
rsn_audiomunge_chain (GstPad * pad, GstBuffer * buf)
{
  RsnAudioMunge *munge = RSN_AUDIOMUNGE (GST_OBJECT_PARENT (pad));

  if (!munge->have_audio) {
    GST_INFO_OBJECT (munge,
        "First audio after flush has TS %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
  }

  munge->have_audio = TRUE;

  /* just push out the incoming buffer without touching it */
  return gst_pad_push (munge->srcpad, buf);
}

/* Create and send a silence buffer downstream */
static GstFlowReturn
rsn_audiomunge_make_audio (RsnAudioMunge * munge,
    GstClockTime start, GstClockTime fill_time)
{
  GstFlowReturn ret;
  GstBuffer *audio_buf;
  GstCaps *caps;
  guint buf_size;

  /* Just generate a 48khz stereo buffer for now */
  /* FIXME: Adapt to the allowed formats, according to the currently
   * plugged decoder, or at least add a source pad that accepts the
   * caps we're outputting if the upstream decoder does not */
#if 0
  caps =
      gst_caps_from_string
      ("audio/x-raw-int,rate=48000,channels=2,width=16,depth=16,signed=(boolean)true,endianness=4321");
  buf_size = 4 * (48000 * fill_time / GST_SECOND);
#else
  caps = gst_caps_from_string ("audio/x-raw-float, endianness=(int)1234,"
      "width=(int)32, channels=(int)2, rate=(int)48000");
  buf_size = 2 * 4 * (48000 * fill_time / GST_SECOND);
#endif

  audio_buf = gst_buffer_new_and_alloc (buf_size);

  gst_buffer_set_caps (audio_buf, caps);
  gst_caps_unref (caps);

  GST_BUFFER_TIMESTAMP (audio_buf) = start;
  GST_BUFFER_DURATION (audio_buf) = fill_time;
  GST_BUFFER_FLAG_SET (audio_buf, GST_BUFFER_FLAG_DISCONT);

  memset (GST_BUFFER_DATA (audio_buf), 0, buf_size);

  GST_LOG_OBJECT (munge, "Sending %u bytes (%" GST_TIME_FORMAT
      ") of audio data with TS %" GST_TIME_FORMAT,
      buf_size, GST_TIME_ARGS (fill_time), GST_TIME_ARGS (start));

  ret = gst_pad_push (munge->srcpad, audio_buf);

  return ret;
}

static gboolean
rsn_audiomunge_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = FALSE;
  RsnAudioMunge *munge = RSN_AUDIOMUNGE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      rsn_audiomunge_reset (munge);
      ret = gst_pad_push_event (munge->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstSegment *segment;
      gboolean update;
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      /* we need TIME format */
      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      /* now configure the values */
      segment = &munge->sink_segment;

      gst_segment_set_newsegment_full (segment, update,
          rate, arate, format, start, stop, time);

      /*
       * FIXME:
       * If this is a segment update and accum >= threshold,
       * or we're in a still frame and there's been no audio received,
       * then we need to generate some audio data.
       *
       * If caused by a segment start update (time advancing in a gap) adjust
       * the new-segment and send the buffer.
       *
       * Otherwise, send the buffer before the newsegment, so that it appears
       * in the closing segment.
       */
      if (!update) {
        GST_DEBUG_OBJECT (munge,
            "Sending newsegment: update %d start %" GST_TIME_FORMAT " stop %"
            GST_TIME_FORMAT " accum now %" GST_TIME_FORMAT, update,
            GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
            GST_TIME_ARGS (segment->accum));

        ret = gst_pad_push_event (munge->srcpad, event);
      }

      if (!munge->have_audio) {
        if ((update && segment->accum >= AUDIO_FILL_THRESHOLD)
            || munge->in_still) {
          GST_DEBUG_OBJECT (munge,
              "Sending audio fill with ts %" GST_TIME_FORMAT ": accum = %"
              GST_TIME_FORMAT " still-state=%d", GST_TIME_ARGS (segment->start),
              GST_TIME_ARGS (segment->accum), munge->in_still);

          /* Just generate a 200ms silence buffer for now. FIXME: Fill the gap */
          if (rsn_audiomunge_make_audio (munge, segment->start,
                  GST_SECOND / 5) == GST_FLOW_OK)
            munge->have_audio = TRUE;
        } else {
          GST_LOG_OBJECT (munge, "Not sending audio fill buffer: "
              "Not segment update, or segment accum below thresh: accum = %"
              GST_TIME_FORMAT, GST_TIME_ARGS (segment->accum));
        }
      }

      if (update) {
        GST_DEBUG_OBJECT (munge,
            "Sending newsegment: update %d start %" GST_TIME_FORMAT " stop %"
            GST_TIME_FORMAT " accum now %" GST_TIME_FORMAT, update,
            GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
            GST_TIME_ARGS (segment->accum));

        ret = gst_pad_push_event (munge->srcpad, event);
      }

      break;
    }
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      gboolean in_still;

      if (gst_video_event_parse_still_frame (event, &in_still)) {
        /* Remember the still-frame state, so we can generate a pre-roll
         * buffer when a new-segment arrives */
        munge->in_still = in_still;
        GST_INFO_OBJECT (munge, "AUDIO MUNGE: still-state now %d",
            munge->in_still);
      }

      ret = gst_pad_push_event (munge->srcpad, event);
      break;
    }
    default:
      ret = gst_pad_push_event (munge->srcpad, event);
      break;
  }

  gst_object_unref (munge);
  return ret;

newseg_wrong_format:

  GST_DEBUG_OBJECT (munge, "received non TIME newsegment");
  gst_event_unref (event);
  gst_object_unref (munge);
  return FALSE;
}

static GstStateChangeReturn
rsn_audiomunge_change_state (GstElement * element, GstStateChange transition)
{
  RsnAudioMunge *munge = RSN_AUDIOMUNGE (element);
  GstStateChangeReturn ret;

  if (transition == GST_STATE_CHANGE_READY_TO_PAUSED)
    rsn_audiomunge_reset (munge);

  ret =
      GST_ELEMENT_CLASS (rsn_audiomunge_parent_class)->change_state (element,
      transition);

  return ret;
}
