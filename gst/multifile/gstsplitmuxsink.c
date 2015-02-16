/* GStreamer Muxer bin that splits output stream by size/time
 * Copyright (C) <2014> Jan Schmidt <jan@centricular.com>
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
 * SECTION:element-splitmuxsink
 * @short_description: Muxer wrapper for splitting output stream by size or time
 *
 * This element wraps a muxer and a sink, and starts a new file when the mux
 * contents are about to cross a threshold of maximum size of maximum time,
 * splitting at video keyframe boundaries. Exactly one input video stream
 * is required, with as many accompanying audio and subtitle streams as
 * desired.
 *
 * By default, it uses mp4mux and filesink, but they can be changed via
 * the 'muxer' and 'sink' properties.
 *
 * The minimum file size is 1 GOP, however - so limits may be overrun if the
 * distance between any 2 keyframes is larger than the limits.
 *
 * The splitting process is driven by the video stream contents, and
 * the video stream must contain closed GOPs for the output file parts
 * to be played individually correctly.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch-1.0 -e v4l2src num-buffers=500 ! video/x-raw,width=320,height=240 ! videoconvert ! queue ! timeoverlay ! x264enc key-int-max=10 ! h264parse ! splitmuxsink location=video%02d.mov max-size-time=10000000000 max-size-bytes=1000000
 * ]|
 *
 * Records a video stream captured from a v4l2 device and muxes it into
 * ISO mp4 files, splitting as needed to limit size/duration to 10 seconds
 * and 1MB maximum size.
 *
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstsplitmuxsink.h"

GST_DEBUG_CATEGORY_STATIC (splitmux_debug);
#define GST_CAT_DEFAULT splitmux_debug

#define GST_SPLITMUX_LOCK(s) g_mutex_lock(&(s)->lock)
#define GST_SPLITMUX_UNLOCK(s) g_mutex_unlock(&(s)->lock)
#define GST_SPLITMUX_WAIT(s) g_cond_wait (&(s)->data_cond, &(s)->lock)
#define GST_SPLITMUX_BROADCAST(s) g_cond_broadcast (&(s)->data_cond)

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_MAX_SIZE_TIME,
  PROP_MAX_SIZE_BYTES,
  PROP_MUXER_OVERHEAD,
  PROP_MUXER,
  PROP_SINK
};

#define DEFAULT_MAX_SIZE_TIME       0
#define DEFAULT_MAX_SIZE_BYTES      0
#define DEFAULT_MUXER_OVERHEAD      0.02
#define DEFAULT_MUXER "mp4mux"
#define DEFAULT_SINK "filesink"

static GstStaticPadTemplate video_sink_template =
GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate audio_sink_template =
GST_STATIC_PAD_TEMPLATE ("audio_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate subtitle_sink_template =
GST_STATIC_PAD_TEMPLATE ("subtitle_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GQuark PAD_CONTEXT;

static void
_do_init (void)
{
  PAD_CONTEXT = g_quark_from_static_string ("pad-context");
}

#define gst_splitmux_sink_parent_class parent_class
G_DEFINE_TYPE_EXTENDED (GstSplitMuxSink, gst_splitmux_sink, GST_TYPE_BIN, 0,
    _do_init ());

static gboolean create_elements (GstSplitMuxSink * splitmux);
static gboolean create_sink (GstSplitMuxSink * splitmux);
static void gst_splitmux_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_splitmux_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_splitmux_sink_dispose (GObject * object);
static void gst_splitmux_sink_finalize (GObject * object);

static GstPad *gst_splitmux_sink_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_splitmux_sink_release_pad (GstElement * element, GstPad * pad);

static GstStateChangeReturn gst_splitmux_sink_change_state (GstElement *
    element, GstStateChange transition);

static void bus_handler (GstBin * bin, GstMessage * msg);
static void set_next_filename (GstSplitMuxSink * splitmux);
static void start_next_fragment (GstSplitMuxSink * splitmux);
static void check_queue_length (GstSplitMuxSink * splitmux, MqStreamCtx * ctx);
static void mq_stream_ctx_unref (MqStreamCtx * ctx);

static MqStreamBuf *
mq_stream_buf_new (void)
{
  return g_slice_new0 (MqStreamBuf);
}

static void
mq_stream_buf_free (MqStreamBuf * data)
{
  g_slice_free (MqStreamBuf, data);
}

static void
gst_splitmux_sink_class_init (GstSplitMuxSinkClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBinClass *gstbin_class = (GstBinClass *) klass;

  gobject_class->set_property = gst_splitmux_sink_set_property;
  gobject_class->get_property = gst_splitmux_sink_get_property;
  gobject_class->dispose = gst_splitmux_sink_dispose;
  gobject_class->finalize = gst_splitmux_sink_finalize;

  gst_element_class_set_static_metadata (gstelement_class,
      "Split Muxing Bin", "Generic/Bin/Muxer",
      "Convenience bin that muxes incoming streams into multiple time/size limited files",
      "Jan Schmidt <jan@centricular.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&video_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&audio_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&subtitle_sink_template));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_splitmux_sink_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_splitmux_sink_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_splitmux_sink_release_pad);

  gstbin_class->handle_message = bus_handler;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "File Output Pattern",
          "Format string pattern for the location of the files to write (e.g. video%05d.mp4)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MUXER_OVERHEAD,
      g_param_spec_double ("mux-overhead", "Muxing Overhead",
          "Extra size overhead of muxing (0.02 = 2%)", 0.0, 1.0,
          DEFAULT_MUXER_OVERHEAD,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_TIME,
      g_param_spec_uint64 ("max-size-time", "Max. size (ns)",
          "Max. amount of time per file (in ns, 0=disable)", 0, G_MAXUINT64,
          DEFAULT_MAX_SIZE_TIME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_SIZE_BYTES,
      g_param_spec_uint64 ("max-size-bytes", "Max. size bytes",
          "Max. amount of data per file (in bytes, 0=disable)", 0, G_MAXUINT64,
          DEFAULT_MAX_SIZE_BYTES, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MUXER,
      g_param_spec_object ("muxer", "Muxer",
          "The muxer element to use (NULL = default mp4mux)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SINK,
      g_param_spec_object ("sink", "Sink",
          "The sink element (or element chain) to use (NULL = default filesink)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_splitmux_sink_init (GstSplitMuxSink * splitmux)
{
  g_mutex_init (&splitmux->lock);
  g_cond_init (&splitmux->data_cond);

  splitmux->mux_overhead = DEFAULT_MUXER_OVERHEAD;
  splitmux->threshold_time = DEFAULT_MAX_SIZE_TIME;
  splitmux->threshold_bytes = DEFAULT_MAX_SIZE_BYTES;

  GST_OBJECT_FLAG_SET (splitmux, GST_ELEMENT_FLAG_SINK);
}

static void
gst_splitmux_reset (GstSplitMuxSink * splitmux)
{
  if (splitmux->mq)
    gst_bin_remove (GST_BIN (splitmux), splitmux->mq);
  if (splitmux->muxer)
    gst_bin_remove (GST_BIN (splitmux), splitmux->muxer);
  if (splitmux->active_sink)
    gst_bin_remove (GST_BIN (splitmux), splitmux->active_sink);

  splitmux->sink = splitmux->active_sink = splitmux->muxer = splitmux->mq =
      NULL;
}

static void
gst_splitmux_sink_dispose (GObject * object)
{
  GstSplitMuxSink *splitmux = GST_SPLITMUX_SINK (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  /* Calling parent dispose invalidates all child pointers */
  splitmux->sink = splitmux->active_sink = splitmux->muxer = splitmux->mq =
      NULL;
}

static void
gst_splitmux_sink_finalize (GObject * object)
{
  GstSplitMuxSink *splitmux = GST_SPLITMUX_SINK (object);
  g_cond_clear (&splitmux->data_cond);
  if (splitmux->provided_sink)
    gst_object_unref (splitmux->provided_sink);
  if (splitmux->provided_muxer)
    gst_object_unref (splitmux->provided_muxer);

  g_free (splitmux->location);

  /* Make sure to free any un-released contexts */
  g_list_foreach (splitmux->contexts, (GFunc) mq_stream_ctx_unref, NULL);
  g_list_free (splitmux->contexts);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_splitmux_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSplitMuxSink *splitmux = GST_SPLITMUX_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:{
      GST_OBJECT_LOCK (splitmux);
      g_free (splitmux->location);
      splitmux->location = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (splitmux);
      break;
    }
    case PROP_MAX_SIZE_BYTES:
      GST_OBJECT_LOCK (splitmux);
      splitmux->threshold_bytes = g_value_get_uint64 (value);
      GST_OBJECT_UNLOCK (splitmux);
      break;
    case PROP_MAX_SIZE_TIME:
      GST_OBJECT_LOCK (splitmux);
      splitmux->threshold_time = g_value_get_uint64 (value);
      GST_OBJECT_UNLOCK (splitmux);
      break;
    case PROP_MUXER_OVERHEAD:
      GST_OBJECT_LOCK (splitmux);
      splitmux->mux_overhead = g_value_get_double (value);
      GST_OBJECT_UNLOCK (splitmux);
      break;
    case PROP_SINK:
      GST_SPLITMUX_LOCK (splitmux);
      if (splitmux->provided_sink)
        gst_object_unref (splitmux->provided_sink);
      splitmux->provided_sink = g_value_dup_object (value);
      GST_SPLITMUX_UNLOCK (splitmux);
      break;
    case PROP_MUXER:
      GST_SPLITMUX_LOCK (splitmux);
      if (splitmux->provided_muxer)
        gst_object_unref (splitmux->provided_muxer);
      splitmux->provided_muxer = g_value_dup_object (value);
      GST_SPLITMUX_UNLOCK (splitmux);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_splitmux_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSplitMuxSink *splitmux = GST_SPLITMUX_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      GST_OBJECT_LOCK (splitmux);
      g_value_set_string (value, splitmux->location);
      GST_OBJECT_UNLOCK (splitmux);
      break;
    case PROP_MAX_SIZE_BYTES:
      GST_OBJECT_LOCK (splitmux);
      g_value_set_uint64 (value, splitmux->threshold_bytes);
      GST_OBJECT_UNLOCK (splitmux);
      break;
    case PROP_MAX_SIZE_TIME:
      GST_OBJECT_LOCK (splitmux);
      g_value_set_uint64 (value, splitmux->threshold_time);
      GST_OBJECT_UNLOCK (splitmux);
      break;
    case PROP_MUXER_OVERHEAD:
      GST_OBJECT_LOCK (splitmux);
      g_value_set_double (value, splitmux->mux_overhead);
      GST_OBJECT_UNLOCK (splitmux);
      break;
    case PROP_SINK:
      GST_SPLITMUX_LOCK (splitmux);
      g_value_set_object (value, splitmux->provided_sink);
      GST_SPLITMUX_UNLOCK (splitmux);
      break;
    case PROP_MUXER:
      GST_SPLITMUX_LOCK (splitmux);
      g_value_set_object (value, splitmux->provided_muxer);
      GST_SPLITMUX_UNLOCK (splitmux);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPad *
mq_sink_to_src (GstElement * mq, GstPad * sink_pad)
{
  gchar *tmp, *sinkname, *srcname;
  GstPad *mq_src;

  sinkname = gst_pad_get_name (sink_pad);
  tmp = sinkname + 5;
  srcname = g_strdup_printf ("src_%s", tmp);

  mq_src = gst_element_get_static_pad (mq, srcname);

  g_free (sinkname);
  g_free (srcname);

  return mq_src;
}

static gboolean
get_pads_from_mq (GstSplitMuxSink * splitmux, GstPad ** sink_pad,
    GstPad ** src_pad)
{
  GstPad *mq_sink;
  GstPad *mq_src;

  /* Request a pad from multiqueue, then connect this one, then
   * discover the corresponding output pad and return both */
  mq_sink = gst_element_get_request_pad (splitmux->mq, "sink_%u");
  if (mq_sink == NULL)
    return FALSE;

  mq_src = mq_sink_to_src (splitmux->mq, mq_sink);
  if (mq_src == NULL)
    goto fail;

  *sink_pad = mq_sink;
  *src_pad = mq_src;

  return TRUE;

fail:
  gst_element_release_request_pad (splitmux->mq, mq_sink);
  return FALSE;
}

static MqStreamCtx *
mq_stream_ctx_new (GstSplitMuxSink * splitmux)
{
  MqStreamCtx *ctx;

  ctx = g_new0 (MqStreamCtx, 1);
  g_atomic_int_set (&ctx->refcount, 1);
  ctx->splitmux = splitmux;
  gst_segment_init (&ctx->in_segment, GST_FORMAT_UNDEFINED);
  gst_segment_init (&ctx->out_segment, GST_FORMAT_UNDEFINED);
  ctx->in_running_time = ctx->out_running_time = 0;
  g_queue_init (&ctx->queued_bufs);
  return ctx;
}

static void
mq_stream_ctx_free (MqStreamCtx * ctx)
{
  g_queue_foreach (&ctx->queued_bufs, (GFunc) mq_stream_buf_free, NULL);
  g_queue_clear (&ctx->queued_bufs);
  g_free (ctx);
}

static void
mq_stream_ctx_unref (MqStreamCtx * ctx)
{
  if (g_atomic_int_dec_and_test (&ctx->refcount))
    mq_stream_ctx_free (ctx);
}

static void
mq_stream_ctx_ref (MqStreamCtx * ctx)
{
  g_atomic_int_inc (&ctx->refcount);
}

static void
_pad_block_destroy_sink_notify (MqStreamCtx * ctx)
{
  ctx->sink_pad_block_id = 0;
  mq_stream_ctx_unref (ctx);
}

static void
_pad_block_destroy_src_notify (MqStreamCtx * ctx)
{
  ctx->src_pad_block_id = 0;
  mq_stream_ctx_unref (ctx);
}

/* Called with lock held, drops the lock to send EOS to the
 * pad
 */
static void
send_eos (GstSplitMuxSink * splitmux, MqStreamCtx * ctx)
{
  GstEvent *eos;
  GstPad *pad;

  eos = gst_event_new_eos ();
  pad = gst_pad_get_peer (ctx->srcpad);

  ctx->out_eos = TRUE;

  GST_INFO_OBJECT (splitmux, "Sending EOS on %" GST_PTR_FORMAT, pad);
  GST_SPLITMUX_UNLOCK (splitmux);
  gst_pad_send_event (pad, eos);
  GST_SPLITMUX_LOCK (splitmux);

  gst_object_unref (pad);
}

/* Called with splitmux lock held to check if this output
 * context needs to sleep to wait for the release of the
 * next GOP, or to send EOS to close out the current file
 */
static void
complete_or_wait_on_out (GstSplitMuxSink * splitmux, MqStreamCtx * ctx)
{
  do {

    GST_LOG_OBJECT (ctx->srcpad,
        "Checking running time %" GST_TIME_FORMAT " against max %"
        GST_TIME_FORMAT, GST_TIME_ARGS (ctx->out_running_time),
        GST_TIME_ARGS (splitmux->max_out_running_time));

    if (splitmux->max_out_running_time == GST_CLOCK_TIME_NONE ||
        ctx->out_running_time < splitmux->max_out_running_time)
      return;

    if (ctx->flushing || splitmux->state == SPLITMUX_STATE_STOPPED)
      return;

    if (splitmux->state == SPLITMUX_STATE_ENDING_FILE) {
      if (ctx->out_eos == FALSE) {
        send_eos (splitmux, ctx);
        continue;
      }
    } else if (splitmux->state == SPLITMUX_STATE_START_NEXT_FRAGMENT) {
      start_next_fragment (splitmux);
      continue;
    }

    GST_INFO_OBJECT (ctx->srcpad,
        "Sleeping for running time %"
        GST_TIME_FORMAT " (max %" GST_TIME_FORMAT ")",
        GST_TIME_ARGS (ctx->out_running_time),
        GST_TIME_ARGS (splitmux->max_out_running_time));
    ctx->out_blocked = TRUE;
    /* Expand the mq if needed before sleeping */
    check_queue_length (splitmux, ctx);
    GST_SPLITMUX_WAIT (splitmux);
    ctx->out_blocked = FALSE;
    GST_INFO_OBJECT (ctx->srcpad,
        "Woken for new max running time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (splitmux->max_out_running_time));
  } while (1);
}

static GstPadProbeReturn
handle_mq_output (GstPad * pad, GstPadProbeInfo * info, MqStreamCtx * ctx)
{
  GstSplitMuxSink *splitmux = ctx->splitmux;
  MqStreamBuf *buf_info = NULL;

  GST_LOG_OBJECT (pad, "Fired probe type 0x%x\n", info->type);

  /* FIXME: Handle buffer lists, until then make it clear they won't work */
  if (info->type & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
    g_warning ("Buffer list handling not implemented");
    return GST_PAD_PROBE_DROP;
  }
  if (info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    GstEvent *event = gst_pad_probe_info_get_event (info);

    GST_LOG_OBJECT (pad, "Event %" GST_PTR_FORMAT, event);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_SEGMENT:
        gst_event_copy_segment (event, &ctx->out_segment);
        break;
      case GST_EVENT_FLUSH_STOP:
        GST_SPLITMUX_LOCK (splitmux);
        gst_segment_init (&ctx->out_segment, GST_FORMAT_UNDEFINED);
        g_queue_foreach (&ctx->queued_bufs, (GFunc) mq_stream_buf_free, NULL);
        g_queue_clear (&ctx->queued_bufs);
        ctx->flushing = FALSE;
        GST_SPLITMUX_UNLOCK (splitmux);
        break;
      case GST_EVENT_FLUSH_START:
        GST_SPLITMUX_LOCK (splitmux);
        GST_LOG_OBJECT (pad, "Flush start");
        ctx->flushing = TRUE;
        GST_SPLITMUX_BROADCAST (splitmux);
        GST_SPLITMUX_UNLOCK (splitmux);
        break;
      case GST_EVENT_EOS:
        GST_SPLITMUX_LOCK (splitmux);
        if (splitmux->state == SPLITMUX_STATE_STOPPED)
          goto beach;
        ctx->out_eos = TRUE;
        GST_SPLITMUX_UNLOCK (splitmux);
        break;
      case GST_EVENT_GAP:{
        GstClockTime gap_ts;

        gst_event_parse_gap (event, &gap_ts, NULL);
        if (gap_ts == GST_CLOCK_TIME_NONE)
          break;

        GST_SPLITMUX_LOCK (splitmux);

        gap_ts = gst_segment_to_running_time (&ctx->out_segment,
            GST_FORMAT_TIME, gap_ts);

        GST_LOG_OBJECT (pad, "Have GAP w/ ts %" GST_TIME_FORMAT,
            GST_TIME_ARGS (gap_ts));

        if (splitmux->state == SPLITMUX_STATE_STOPPED)
          goto beach;
        ctx->out_running_time = gap_ts;
        complete_or_wait_on_out (splitmux, ctx);
        GST_SPLITMUX_UNLOCK (splitmux);
        break;
      }
      default:
        break;
    }
    return GST_PAD_PROBE_PASS;
  }

  /* Allow everything through until the configured next stopping point */
  GST_SPLITMUX_LOCK (splitmux);

  buf_info = g_queue_pop_tail (&ctx->queued_bufs);
  if (buf_info == NULL)
    /* Can only happen due to a poorly timed flush */
    goto beach;

  /* If we have popped a keyframe, decrement the queued_gop count */
  if (buf_info->keyframe && splitmux->queued_gops > 0)
    splitmux->queued_gops--;

  ctx->out_running_time = buf_info->run_ts;

  GST_LOG_OBJECT (splitmux,
      "Pad %" GST_PTR_FORMAT " buffer with TS %" GST_TIME_FORMAT
      " size %" G_GSIZE_FORMAT,
      pad, GST_TIME_ARGS (ctx->out_running_time), buf_info->buf_size);

  complete_or_wait_on_out (splitmux, ctx);

  if (splitmux->muxed_out_time == GST_CLOCK_TIME_NONE ||
      splitmux->muxed_out_time < buf_info->run_ts)
    splitmux->muxed_out_time = buf_info->run_ts;

  splitmux->muxed_out_bytes += buf_info->buf_size;

#ifndef GST_DISABLE_GST_DEBUG
  {
    GstBuffer *buf = gst_pad_probe_info_get_buffer (info);
    GST_LOG_OBJECT (pad, "Returning to pass buffer %" GST_PTR_FORMAT
        " run ts %" GST_TIME_FORMAT, buf,
        GST_TIME_ARGS (ctx->out_running_time));
  }
#endif

  GST_SPLITMUX_UNLOCK (splitmux);

  mq_stream_buf_free (buf_info);

  return GST_PAD_PROBE_PASS;

beach:
  GST_SPLITMUX_UNLOCK (splitmux);
  return GST_PAD_PROBE_DROP;
}

static gboolean
resend_sticky (GstPad * pad, GstEvent ** event, GstPad * peer)
{
  return gst_pad_send_event (peer, gst_event_ref (*event));
}

static void
restart_context (MqStreamCtx * ctx, GstSplitMuxSink * splitmux)
{
  GstPad *peer = gst_pad_get_peer (ctx->srcpad);

  gst_pad_sticky_events_foreach (ctx->srcpad,
      (GstPadStickyEventsForeachFunction) (resend_sticky), peer);

  /* Clear EOS flag */
  ctx->out_eos = FALSE;

  gst_object_unref (peer);
}

/* Called with lock held when a fragment
 * reaches EOS and it is time to restart
 * a new fragment
 */
static void
start_next_fragment (GstSplitMuxSink * splitmux)
{
  /* 1 change to new file */
  gst_element_set_state (splitmux->muxer, GST_STATE_NULL);
  gst_element_set_state (splitmux->active_sink, GST_STATE_NULL);

  set_next_filename (splitmux);

  gst_element_sync_state_with_parent (splitmux->active_sink);
  gst_element_sync_state_with_parent (splitmux->muxer);

  g_list_foreach (splitmux->contexts, (GFunc) restart_context, splitmux);

  /* Switch state and go back to processing */
  splitmux->state = SPLITMUX_STATE_COLLECTING_GOP_START;
  if (!splitmux->video_ctx->in_eos)
    splitmux->max_out_running_time = splitmux->video_ctx->in_running_time;
  else
    splitmux->max_out_running_time = GST_CLOCK_TIME_NONE;

  /* Store the overflow parameters as the basis for the next fragment */
  splitmux->mux_start_time = splitmux->muxed_out_time;
  splitmux->mux_start_bytes = splitmux->muxed_out_bytes;

  GST_DEBUG_OBJECT (splitmux,
      "Restarting flow for new fragment. New running time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (splitmux->max_out_running_time));

  GST_SPLITMUX_BROADCAST (splitmux);
}

static void
bus_handler (GstBin * bin, GstMessage * message)
{
  GstSplitMuxSink *splitmux = GST_SPLITMUX_SINK (bin);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      /* If the state is draining out the current file, drop this EOS */
      GST_SPLITMUX_LOCK (splitmux);
      if (splitmux->state == SPLITMUX_STATE_ENDING_FILE &&
          splitmux->max_out_running_time != GST_CLOCK_TIME_NONE) {
        GST_DEBUG_OBJECT (splitmux, "Caught EOS at end of fragment, dropping");
        splitmux->state = SPLITMUX_STATE_START_NEXT_FRAGMENT;
        GST_SPLITMUX_BROADCAST (splitmux);

        gst_message_unref (message);
        GST_SPLITMUX_UNLOCK (splitmux);
        return;
      }
      GST_SPLITMUX_UNLOCK (splitmux);
      break;
    default:
      break;
  }

  GST_BIN_CLASS (parent_class)->handle_message (bin, message);
}

/* Called with splitmux lock held */
/* Called when entering ProcessingCompleteGop state
 * Assess if mq contents overflowed the current file
 *   -> If yes, need to switch to new file
 *   -> if no, set max_out_running_time to let this GOP in and
 *      go to COLLECTING_GOP_START state
 */
static void
handle_gathered_gop (GstSplitMuxSink * splitmux)
{
  GList *cur;
  gsize queued_bytes = 0;
  GstClockTime queued_time = 0;

  /* Assess if the multiqueue contents overflowed the current file */
  for (cur = g_list_first (splitmux->contexts);
      cur != NULL; cur = g_list_next (cur)) {
    MqStreamCtx *tmpctx = (MqStreamCtx *) (cur->data);
    if (tmpctx->in_running_time > queued_time)
      queued_time = tmpctx->in_running_time;
    queued_bytes += tmpctx->in_bytes;
  }

  g_assert (queued_bytes >= splitmux->mux_start_bytes);
  g_assert (queued_time >= splitmux->mux_start_time);

  queued_bytes -= splitmux->mux_start_bytes;
  queued_time -= splitmux->mux_start_time;

  /* Expand queued bytes estimate by muxer overhead */
  queued_bytes += (queued_bytes * splitmux->mux_overhead);

  GST_LOG_OBJECT (splitmux, "mq at TS %" GST_TIME_FORMAT
      " bytes %" G_GSIZE_FORMAT, GST_TIME_ARGS (queued_time), queued_bytes);

  /* Check for overrun - have we output at least one byte and overrun
   * either threshold? */
  if ((splitmux->mux_start_bytes < splitmux->muxed_out_bytes) &&
      ((splitmux->threshold_bytes > 0 &&
              queued_bytes >= splitmux->threshold_bytes) ||
          (splitmux->threshold_time > 0 &&
              queued_time >= splitmux->threshold_time))) {

    splitmux->state = SPLITMUX_STATE_ENDING_FILE;

    GST_INFO_OBJECT (splitmux,
        "mq overflowed since last, draining out. max out TS is %"
        GST_TIME_FORMAT, GST_TIME_ARGS (splitmux->max_out_running_time));
    GST_SPLITMUX_BROADCAST (splitmux);

  } else {
    /* No overflow */
    GST_LOG_OBJECT (splitmux,
        "This GOP didn't overflow the fragment. Bytes sent %" G_GSIZE_FORMAT
        " queued %" G_GSIZE_FORMAT " time %" GST_TIME_FORMAT " Continuing.",
        splitmux->muxed_out_bytes - splitmux->mux_start_bytes,
        queued_bytes, GST_TIME_ARGS (queued_time));

    /* Wake everyone up to push this one GOP, then sleep */
    splitmux->state = SPLITMUX_STATE_COLLECTING_GOP_START;
    if (!splitmux->video_ctx->in_eos)
      splitmux->max_out_running_time = splitmux->video_ctx->in_running_time;
    else
      splitmux->max_out_running_time = GST_CLOCK_TIME_NONE;

    GST_LOG_OBJECT (splitmux, "Waking output for complete GOP, TS %"
        GST_TIME_FORMAT, GST_TIME_ARGS (splitmux->max_out_running_time));
    GST_SPLITMUX_BROADCAST (splitmux);
  }

}

/* Called with splitmux lock held */
/* Called from each input pad when it is has all the pieces
 * for a GOP or EOS, starting with the video pad which has set the
 * splitmux->max_in_running_time
 */
static void
check_completed_gop (GstSplitMuxSink * splitmux, MqStreamCtx * ctx)
{
  GList *cur;
  gboolean ready = TRUE;

  if (splitmux->state == SPLITMUX_STATE_WAITING_GOP_COMPLETE) {
    /* Iterate each pad, and check that the input running time is at least
     * up to the video runnning time, and if so handle the collected GOP */
    GST_LOG_OBJECT (splitmux, "Checking GOP collected, ctx %p", ctx);
    for (cur = g_list_first (splitmux->contexts);
        cur != NULL; cur = g_list_next (cur)) {
      MqStreamCtx *tmpctx = (MqStreamCtx *) (cur->data);

      GST_LOG_OBJECT (splitmux,
          "Context %p (src pad %" GST_PTR_FORMAT ") TS %" GST_TIME_FORMAT
          " EOS %d", tmpctx, tmpctx->srcpad,
          GST_TIME_ARGS (tmpctx->in_running_time), tmpctx->in_eos);

      if (tmpctx->in_running_time < splitmux->max_in_running_time &&
          !tmpctx->in_eos) {
        GST_LOG_OBJECT (splitmux,
            "Context %p (src pad %" GST_PTR_FORMAT ") not ready. We'll sleep",
            tmpctx, tmpctx->srcpad);
        ready = FALSE;
        break;
      }
    }
    if (ready) {
      GST_DEBUG_OBJECT (splitmux,
          "Collected GOP is complete. Processing (ctx %p)", ctx);
      /* All pads have a complete GOP, release it into the multiqueue */
      handle_gathered_gop (splitmux);
    }
  }

  /* Some pad is not yet ready, or GOP is being pushed
   * either way, sleep and wait to get woken */
  while ((splitmux->state == SPLITMUX_STATE_WAITING_GOP_COMPLETE ||
          splitmux->state == SPLITMUX_STATE_START_NEXT_FRAGMENT) &&
      !ctx->flushing) {

    GST_LOG_OBJECT (splitmux, "Sleeping for %s (ctx %p)",
        splitmux->state == SPLITMUX_STATE_WAITING_GOP_COMPLETE ?
        "GOP complete" : "EOF draining", ctx);
    GST_SPLITMUX_WAIT (splitmux);

    GST_LOG_OBJECT (splitmux, "Done waiting for complete GOP (ctx %p)", ctx);
  }
}

static void
check_queue_length (GstSplitMuxSink * splitmux, MqStreamCtx * ctx)
{
  GList *cur;
  guint cur_len = g_queue_get_length (&ctx->queued_bufs);

  GST_DEBUG_OBJECT (ctx->sinkpad,
      "Checking queue length len %u cur_max %u queued gops %u",
      cur_len, splitmux->mq_max_buffers, splitmux->queued_gops);

  if (cur_len >= splitmux->mq_max_buffers) {
    gboolean allow_grow = FALSE;

    /* If collecting a GOP and this pad might block,
     * and there isn't already a pending GOP in the queue
     * then grow
     */
    if (splitmux->state == SPLITMUX_STATE_WAITING_GOP_COMPLETE &&
        ctx->in_running_time < splitmux->max_in_running_time &&
        splitmux->queued_gops <= 1) {
      allow_grow = TRUE;
    } else if (splitmux->state == SPLITMUX_STATE_COLLECTING_GOP_START &&
        ctx->is_video) {
      allow_grow = TRUE;
    }

    if (!allow_grow) {
      for (cur = g_list_first (splitmux->contexts);
          cur != NULL; cur = g_list_next (cur)) {
        MqStreamCtx *tmpctx = (MqStreamCtx *) (cur->data);
        GST_DEBUG_OBJECT (tmpctx->sinkpad,
            " len %u out_blocked %d",
            g_queue_get_length (&tmpctx->queued_bufs), tmpctx->out_blocked);
        /* If another stream is starving, grow */
        if (tmpctx != ctx && g_queue_get_length (&tmpctx->queued_bufs) < 1) {
          allow_grow = TRUE;
        }
      }
    }

    if (allow_grow) {
      splitmux->mq_max_buffers = cur_len + 1;

      GST_INFO_OBJECT (splitmux,
          "Multiqueue overrun - enlarging to %u buffers ctx %p",
          splitmux->mq_max_buffers, ctx);

      g_object_set (splitmux->mq, "max-size-buffers",
          splitmux->mq_max_buffers, NULL);
    }
  }
}

static GstPadProbeReturn
handle_mq_input (GstPad * pad, GstPadProbeInfo * info, MqStreamCtx * ctx)
{
  GstSplitMuxSink *splitmux = ctx->splitmux;
  GstBuffer *buf;
  MqStreamBuf *buf_info = NULL;
  GstClockTime ts;
  gboolean loop_again;
  gboolean keyframe = FALSE;

  GST_LOG_OBJECT (pad, "Fired probe type 0x%x\n", info->type);

  /* FIXME: Handle buffer lists, until then make it clear they won't work */
  if (info->type & GST_PAD_PROBE_TYPE_BUFFER_LIST) {
    g_warning ("Buffer list handling not implemented");
    return GST_PAD_PROBE_DROP;
  }
  if (info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    GstEvent *event = gst_pad_probe_info_get_event (info);
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_SEGMENT:
        gst_event_copy_segment (event, &ctx->in_segment);
        break;
      case GST_EVENT_FLUSH_STOP:
        GST_SPLITMUX_LOCK (splitmux);
        gst_segment_init (&ctx->in_segment, GST_FORMAT_UNDEFINED);
        ctx->in_eos = FALSE;
        ctx->in_bytes = 0;
        ctx->in_running_time = 0;
        GST_SPLITMUX_UNLOCK (splitmux);
        break;
      case GST_EVENT_EOS:
        GST_SPLITMUX_LOCK (splitmux);
        ctx->in_eos = TRUE;

        if (splitmux->state == SPLITMUX_STATE_STOPPED)
          goto beach;

        if (ctx->is_video) {
          GST_INFO_OBJECT (splitmux, "Got Video EOS. Finishing up");
          /* Act as if this is a new keyframe with infinite timestamp */
          splitmux->max_in_running_time = GST_CLOCK_TIME_NONE;
          splitmux->state = SPLITMUX_STATE_WAITING_GOP_COMPLETE;
          /* Wake up other input pads to collect this GOP */
          GST_SPLITMUX_BROADCAST (splitmux);
          check_completed_gop (splitmux, ctx);
        } else if (splitmux->state == SPLITMUX_STATE_WAITING_GOP_COMPLETE) {
          /* If we are waiting for a GOP to be completed (ie, for aux
           * pads to catch up), then this pad is complete, so check
           * if the whole GOP is.
           */
          check_completed_gop (splitmux, ctx);
        }
        GST_SPLITMUX_UNLOCK (splitmux);
        break;
      default:
        break;
    }
    return GST_PAD_PROBE_PASS;
  }

  buf = gst_pad_probe_info_get_buffer (info);
  ctx->in_running_time = gst_segment_to_running_time (&ctx->in_segment,
      GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (buf));
  buf_info = mq_stream_buf_new ();

  if (GST_BUFFER_PTS_IS_VALID (buf))
    ts = GST_BUFFER_PTS (buf);
  else
    ts = GST_BUFFER_DTS (buf);

  GST_SPLITMUX_LOCK (splitmux);

  if (splitmux->state == SPLITMUX_STATE_STOPPED)
    goto beach;

  /* If this buffer has a timestamp, advance the input timestamp of the
   * stream */
  if (GST_CLOCK_TIME_IS_VALID (ts)) {
    GstClockTime running_time =
        gst_segment_to_running_time (&ctx->in_segment, GST_FORMAT_TIME,
        GST_BUFFER_TIMESTAMP (buf));

    if (GST_CLOCK_TIME_IS_VALID (running_time) &&
        (ctx->in_running_time == GST_CLOCK_TIME_NONE
            || running_time > ctx->in_running_time))
      ctx->in_running_time = running_time;
  }

  /* Try to make sure we have a valid running time */
  if (!GST_CLOCK_TIME_IS_VALID (ctx->in_running_time)) {
    ctx->in_running_time =
        gst_segment_to_running_time (&ctx->in_segment, GST_FORMAT_TIME,
        ctx->in_segment.start);
  }

  buf_info->run_ts = ctx->in_running_time;
  buf_info->buf_size = gst_buffer_get_size (buf);

  /* Update total input byte counter for overflow detect */
  ctx->in_bytes += buf_info->buf_size;

  GST_DEBUG_OBJECT (pad, "Buf TS %" GST_TIME_FORMAT
      " total in_bytes %" G_GSIZE_FORMAT,
      GST_TIME_ARGS (buf_info->run_ts), ctx->in_bytes);

  loop_again = TRUE;
  do {
    if (ctx->flushing)
      break;

    switch (splitmux->state) {
      case SPLITMUX_STATE_COLLECTING_GOP_START:
        if (ctx->is_video) {
          /* If a keyframe, we have a complete GOP */
          if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT) ||
              !GST_CLOCK_TIME_IS_VALID (ctx->in_running_time) ||
              splitmux->max_in_running_time >= ctx->in_running_time) {
            /* Pass this buffer through */
            loop_again = FALSE;
            break;
          }
          GST_INFO_OBJECT (pad,
              "Have keyframe with running time %" GST_TIME_FORMAT,
              GST_TIME_ARGS (ctx->in_running_time));
          keyframe = TRUE;
          splitmux->state = SPLITMUX_STATE_WAITING_GOP_COMPLETE;
          splitmux->max_in_running_time = ctx->in_running_time;
          /* Wake up other input pads to collect this GOP */
          GST_SPLITMUX_BROADCAST (splitmux);
          check_completed_gop (splitmux, ctx);
        } else {
          /* We're still waiting for a keyframe on the video pad, sleep */
          GST_LOG_OBJECT (pad, "Sleeping for GOP start");
          GST_SPLITMUX_WAIT (splitmux);
          GST_LOG_OBJECT (pad, "Done sleeping for GOP start state now %d",
              splitmux->state);
        }
        break;
      case SPLITMUX_STATE_WAITING_GOP_COMPLETE:
        /* After a GOP start is found, this buffer might complete the GOP */
        /* If we overran the target timestamp, it might be time to process
         * the GOP, otherwise bail out for more data
         */
        GST_LOG_OBJECT (pad,
            "Checking TS %" GST_TIME_FORMAT " against max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (ctx->in_running_time),
            GST_TIME_ARGS (splitmux->max_in_running_time));

        if (ctx->in_running_time < splitmux->max_in_running_time) {
          loop_again = FALSE;
          break;
        }

        GST_LOG_OBJECT (pad,
            "Collected last packet of GOP. Checking other pads");
        check_completed_gop (splitmux, ctx);
        break;
      case SPLITMUX_STATE_ENDING_FILE:
      case SPLITMUX_STATE_START_NEXT_FRAGMENT:
        /* A fragment is ending, wait until that's done before continuing */
        GST_DEBUG_OBJECT (pad, "Sleeping for fragment restart");
        GST_SPLITMUX_WAIT (splitmux);
        GST_DEBUG_OBJECT (pad,
            "Done sleeping for fragment restart state now %d", splitmux->state);
        break;
      default:
        loop_again = FALSE;
        break;
    }
  } while (loop_again);

  if (keyframe) {
    splitmux->queued_gops++;
    buf_info->keyframe = TRUE;
  }

  /* Now add this buffer to the queue just before returning */
  g_queue_push_head (&ctx->queued_bufs, buf_info);

  /* Check the buffer will fit in the mq */
  check_queue_length (splitmux, ctx);

  GST_LOG_OBJECT (pad, "Returning to queue buffer %" GST_PTR_FORMAT
      " run ts %" GST_TIME_FORMAT, buf, GST_TIME_ARGS (ctx->in_running_time));

  GST_SPLITMUX_UNLOCK (splitmux);
  return GST_PAD_PROBE_PASS;

beach:
  GST_SPLITMUX_UNLOCK (splitmux);
  if (buf_info)
    mq_stream_buf_free (buf_info);
  return GST_PAD_PROBE_PASS;
}

static GstPad *
gst_splitmux_sink_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{
  GstSplitMuxSink *splitmux = (GstSplitMuxSink *) element;
  GstPadTemplate *mux_template = NULL;
  GstPad *res = NULL;
  GstPad *mq_sink, *mq_src;
  gchar *gname;
  gboolean is_video = FALSE;
  MqStreamCtx *ctx;

  GST_DEBUG_OBJECT (element, "templ:%s, name:%s", templ->name_template, name);

  GST_SPLITMUX_LOCK (splitmux);
  if (!create_elements (splitmux))
    goto fail;

  if (templ->name_template) {
    if (g_str_equal (templ->name_template, "video")) {
      /* FIXME: Look for a pad template with matching caps, rather than by name */
      mux_template =
          gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS
          (splitmux->muxer), "video_%u");
      is_video = TRUE;
      name = NULL;
    } else {
      mux_template =
          gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS
          (splitmux->muxer), templ->name_template);
    }
  }

  res = gst_element_request_pad (splitmux->muxer, mux_template, name, caps);
  if (res == NULL)
    goto fail;

  if (is_video)
    gname = g_strdup ("video");
  else if (name == NULL)
    gname = gst_pad_get_name (res);
  else
    gname = g_strdup (name);

  if (!get_pads_from_mq (splitmux, &mq_sink, &mq_src)) {
    gst_element_release_request_pad (splitmux->muxer, res);
    gst_object_unref (GST_OBJECT (res));
    goto fail;
  }

  if (gst_pad_link (mq_src, res) != GST_PAD_LINK_OK) {
    gst_element_release_request_pad (splitmux->muxer, res);
    gst_object_unref (GST_OBJECT (res));
    gst_element_release_request_pad (splitmux->mq, mq_sink);
    gst_object_unref (GST_OBJECT (mq_sink));
    goto fail;
  }

  gst_object_unref (GST_OBJECT (res));

  ctx = mq_stream_ctx_new (splitmux);
  ctx->is_video = is_video;
  ctx->srcpad = mq_src;
  ctx->sinkpad = mq_sink;

  mq_stream_ctx_ref (ctx);
  ctx->src_pad_block_id =
      gst_pad_add_probe (mq_src, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
      (GstPadProbeCallback) handle_mq_output, ctx, (GDestroyNotify)
      _pad_block_destroy_src_notify);
  if (is_video)
    splitmux->video_ctx = ctx;

  res = gst_ghost_pad_new (gname, mq_sink);
  g_object_set_qdata ((GObject *) (res), PAD_CONTEXT, ctx);

  mq_stream_ctx_ref (ctx);
  ctx->sink_pad_block_id =
      gst_pad_add_probe (res, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
      (GstPadProbeCallback) handle_mq_input, ctx, (GDestroyNotify)
      _pad_block_destroy_sink_notify);

  GST_DEBUG_OBJECT (splitmux, "Request pad %" GST_PTR_FORMAT
      " is mq pad %" GST_PTR_FORMAT, res, mq_sink);

  splitmux->contexts = g_list_prepend (splitmux->contexts, ctx);

  g_free (gname);

  gst_object_unref (mq_sink);
  gst_object_unref (mq_src);

  gst_pad_set_active (res, TRUE);
  gst_element_add_pad (element, res);
  GST_SPLITMUX_UNLOCK (splitmux);

  return res;
fail:
  GST_SPLITMUX_UNLOCK (splitmux);
  return NULL;
}

static void
gst_splitmux_sink_release_pad (GstElement * element, GstPad * pad)
{
  GstSplitMuxSink *splitmux = (GstSplitMuxSink *) element;
  GstPad *mqsink, *mqsrc, *muxpad;
  MqStreamCtx *ctx =
      (MqStreamCtx *) (g_object_get_qdata ((GObject *) (pad), PAD_CONTEXT));

  GST_SPLITMUX_LOCK (splitmux);

  if (splitmux->muxer == NULL || splitmux->mq == NULL)
    goto fail;                  /* Elements don't exist yet - nothing to release */

  GST_INFO_OBJECT (pad, "releasing request pad");

  mqsink = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));
  mqsrc = mq_sink_to_src (splitmux->mq, mqsink);
  muxpad = gst_pad_get_peer (mqsrc);

  /* Remove the context from our consideration */
  splitmux->contexts = g_list_remove (splitmux->contexts, ctx);

  if (ctx->sink_pad_block_id)
    gst_pad_remove_probe (ctx->sinkpad, ctx->sink_pad_block_id);

  if (ctx->src_pad_block_id)
    gst_pad_remove_probe (ctx->srcpad, ctx->src_pad_block_id);

  /* Can release the context now */
  mq_stream_ctx_unref (ctx);

  /* Release and free the mq input */
  gst_element_release_request_pad (splitmux->mq, mqsink);

  /* Release and free the muxer input */
  gst_element_release_request_pad (splitmux->muxer, muxpad);

  gst_object_unref (mqsink);
  gst_object_unref (mqsrc);
  gst_object_unref (muxpad);

  gst_element_remove_pad (element, pad);

fail:
  GST_SPLITMUX_UNLOCK (splitmux);
}

static GstElement *
create_element (GstSplitMuxSink * splitmux,
    const gchar * factory, const gchar * name)
{
  GstElement *ret = gst_element_factory_make (factory, name);
  if (ret == NULL) {
    g_warning ("Failed to create %s - splitmuxsink will not work", name);
    return NULL;
  }

  if (!gst_bin_add (GST_BIN (splitmux), ret)) {
    g_warning ("Could not add %s element - splitmuxsink will not work", name);
    gst_object_unref (ret);
    return NULL;
  }

  return ret;
}

static gboolean
create_elements (GstSplitMuxSink * splitmux)
{
  /* Create internal elements */
  if (splitmux->mq == NULL) {
    if ((splitmux->mq =
            create_element (splitmux, "multiqueue", "multiqueue")) == NULL)
      goto fail;

    splitmux->mq_max_buffers = 5;
    /* No bytes or time limit, we limit buffers manually */
    g_object_set (splitmux->mq, "max-size-bytes", 0, "max-size-time",
        (guint64) 0, "max-size-buffers", splitmux->mq_max_buffers, NULL);
  }

  if (splitmux->muxer == NULL) {
    if (splitmux->provided_muxer == NULL) {
      if ((splitmux->muxer =
              create_element (splitmux, "mp4mux", "muxer")) == NULL)
        goto fail;
    } else {
      splitmux->muxer = splitmux->provided_muxer;
      if (!gst_bin_add (GST_BIN (splitmux), splitmux->provided_muxer)) {
        g_warning ("Could not add muxer element - splitmuxsink will not work");
        goto fail;
      }
    }
  }

  return TRUE;
fail:
  return FALSE;
}

static GstElement *
find_sink (GstElement * e)
{
  GstElement *res = NULL;
  GstIterator *iter;
  gboolean done = FALSE;
  GValue data = { 0, };

  if (!GST_IS_BIN (e))
    return e;

  iter = gst_bin_iterate_sinks (GST_BIN (e));
  while (!done) {
    switch (gst_iterator_next (iter, &data)) {
      case GST_ITERATOR_OK:
      {
        GstElement *child = g_value_get_object (&data);
        if (g_object_class_find_property (G_OBJECT_GET_CLASS (child),
                "location") != NULL) {
          res = child;
          done = TRUE;
        }
        g_value_reset (&data);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_ERROR:
        g_assert_not_reached ();
        break;
    }
  }
  g_value_unset (&data);
  gst_iterator_free (iter);

  return res;
}

static gboolean
create_sink (GstSplitMuxSink * splitmux)
{
  g_return_val_if_fail (splitmux->active_sink == NULL, TRUE);

  if (splitmux->provided_sink == NULL) {
    if ((splitmux->sink =
            create_element (splitmux, DEFAULT_SINK, "sink")) == NULL)
      goto fail;
    splitmux->active_sink = splitmux->sink;
  } else {
    if (!gst_bin_add (GST_BIN (splitmux), splitmux->provided_sink)) {
      g_warning ("Could not add sink elements - splitmuxsink will not work");
      goto fail;
    }

    splitmux->active_sink = splitmux->provided_sink;

    /* Find the sink element */
    splitmux->sink = find_sink (splitmux->active_sink);
    if (splitmux->sink == NULL) {
      g_warning
          ("Could not locate sink element in provided sink - splitmuxsink will not work");
      goto fail;
    }
  }

  if (!gst_element_link (splitmux->muxer, splitmux->active_sink)) {
    g_warning ("Failed to link muxer and sink- splitmuxsink will not work");
    goto fail;
  }

  return TRUE;
fail:
  return FALSE;
}

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
static void
set_next_filename (GstSplitMuxSink * splitmux)
{
  if (splitmux->location) {
    gchar *fname;

    fname = g_strdup_printf (splitmux->location, splitmux->fragment_id);
    GST_INFO_OBJECT (splitmux, "Setting file to %s", fname);
    g_object_set (splitmux->sink, "location", fname, NULL);
    g_free (fname);

    splitmux->fragment_id++;
  }
}

static GstStateChangeReturn
gst_splitmux_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstSplitMuxSink *splitmux = (GstSplitMuxSink *) element;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:{
      GST_SPLITMUX_LOCK (splitmux);
      if (!create_elements (splitmux) || !create_sink (splitmux)) {
        ret = GST_STATE_CHANGE_FAILURE;
        GST_SPLITMUX_UNLOCK (splitmux);
        goto beach;
      }
      GST_SPLITMUX_UNLOCK (splitmux);
      splitmux->fragment_id = 0;
      set_next_filename (splitmux);
      break;
    }
    case GST_STATE_CHANGE_READY_TO_PAUSED:{
      GST_SPLITMUX_LOCK (splitmux);
      /* Start by collecting one input on each pad */
      splitmux->state = SPLITMUX_STATE_COLLECTING_GOP_START;
      splitmux->max_in_running_time = 0;
      splitmux->muxed_out_time = splitmux->mux_start_time = 0;
      splitmux->muxed_out_bytes = splitmux->mux_start_bytes = 0;
      GST_SPLITMUX_UNLOCK (splitmux);
      break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_SPLITMUX_LOCK (splitmux);
      splitmux->state = SPLITMUX_STATE_STOPPED;
      /* Wake up any blocked threads */
      GST_LOG_OBJECT (splitmux,
          "State change -> NULL or READY. Waking threads");
      GST_SPLITMUX_BROADCAST (splitmux);
      GST_SPLITMUX_UNLOCK (splitmux);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto beach;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_SPLITMUX_LOCK (splitmux);
      splitmux->fragment_id = 0;
      gst_splitmux_reset (splitmux);
      GST_SPLITMUX_UNLOCK (splitmux);
      break;
    default:
      break;
  }

beach:

  if (transition == GST_STATE_CHANGE_NULL_TO_READY &&
      ret == GST_STATE_CHANGE_FAILURE) {
    /* Cleanup elements on failed transition out of NULL */
    gst_splitmux_reset (splitmux);
  }
  return ret;
}

gboolean
register_splitmuxsink (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (splitmux_debug, "splitmuxsink", 0,
      "Split File Muxing Sink");

  return gst_element_register (plugin, "splitmuxsink", GST_RANK_NONE,
      GST_TYPE_SPLITMUX_SINK);
}
