/*-*- Mode: C; c-basic-offset: 2 -*-*/

/*  GStreamer pulseaudio plugin
 *
 *  Copyright (c) 2004-2008 Lennart Poettering
 *            (c) 2009      Wim Taymans
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
 * SECTION:element-pulsesink
 * @see_also: pulsesrc, pulsemixer
 *
 * This element outputs audio to a
 * <ulink href="http://www.pulseaudio.org">PulseAudio sound server</ulink>.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! audioresample ! pulsesink
 * ]| Play an Ogg/Vorbis file.
 * |[
 * gst-launch -v audiotestsrc ! audioconvert ! volume volume=0.4 ! pulsesink
 * ]| Play a 440Hz sine wave.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdio.h>

#include <gst/base/gstbasesink.h>
#include <gst/gsttaglist.h>
#include <gst/interfaces/streamvolume.h>
#include <gst/gst-i18n-plugin.h>

#include "pulsesink.h"
#include "pulseutil.h"

GST_DEBUG_CATEGORY_EXTERN (pulse_debug);
#define GST_CAT_DEFAULT pulse_debug

/* according to
 * http://www.pulseaudio.org/ticket/314
 * we need pulse-0.9.12 to use sink volume properties
 */

#define DEFAULT_SERVER          NULL
#define DEFAULT_DEVICE          NULL
#define DEFAULT_DEVICE_NAME     NULL
#define DEFAULT_VOLUME          1.0
#define DEFAULT_MUTE            FALSE
#define MAX_VOLUME              10.0

enum
{
  PROP_0,
  PROP_SERVER,
  PROP_DEVICE,
  PROP_DEVICE_NAME,
  PROP_VOLUME,
  PROP_MUTE,
  PROP_LAST
};

#define GST_TYPE_PULSERING_BUFFER        \
        (gst_pulseringbuffer_get_type())
#define GST_PULSERING_BUFFER(obj)        \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PULSERING_BUFFER,GstPulseRingBuffer))
#define GST_PULSERING_BUFFER_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PULSERING_BUFFER,GstPulseRingBufferClass))
#define GST_PULSERING_BUFFER_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_PULSERING_BUFFER, GstPulseRingBufferClass))
#define GST_PULSERING_BUFFER_CAST(obj)        \
        ((GstPulseRingBuffer *)obj)
#define GST_IS_PULSERING_BUFFER(obj)     \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PULSERING_BUFFER))
#define GST_IS_PULSERING_BUFFER_CLASS(klass)\
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PULSERING_BUFFER))

typedef struct _GstPulseRingBuffer GstPulseRingBuffer;
typedef struct _GstPulseRingBufferClass GstPulseRingBufferClass;

/* We keep a custom ringbuffer that is backed up by data allocated by
 * pulseaudio. We must also overide the commit function to write into
 * pulseaudio memory instead. */
struct _GstPulseRingBuffer
{
  GstRingBuffer object;

  gchar *stream_name;

  pa_context *context;
  pa_stream *stream;

  pa_sample_spec sample_spec;

  gboolean corked:1;
  gboolean in_commit:1;
  gboolean paused:1;
};

struct _GstPulseRingBufferClass
{
  GstRingBufferClass parent_class;
};

static void gst_pulseringbuffer_class_init (GstPulseRingBufferClass * klass);
static void gst_pulseringbuffer_init (GstPulseRingBuffer * ringbuffer,
    GstPulseRingBufferClass * klass);
static void gst_pulseringbuffer_finalize (GObject * object);

static GstRingBufferClass *ring_parent_class = NULL;

static gboolean gst_pulseringbuffer_open_device (GstRingBuffer * buf);
static gboolean gst_pulseringbuffer_close_device (GstRingBuffer * buf);
static gboolean gst_pulseringbuffer_acquire (GstRingBuffer * buf,
    GstRingBufferSpec * spec);
static gboolean gst_pulseringbuffer_release (GstRingBuffer * buf);
static gboolean gst_pulseringbuffer_start (GstRingBuffer * buf);
static gboolean gst_pulseringbuffer_pause (GstRingBuffer * buf);
static gboolean gst_pulseringbuffer_stop (GstRingBuffer * buf);
static void gst_pulseringbuffer_clear (GstRingBuffer * buf);
static guint gst_pulseringbuffer_commit (GstRingBuffer * buf,
    guint64 * sample, guchar * data, gint in_samples, gint out_samples,
    gint * accum);

/* ringbuffer abstract base class */
static GType
gst_pulseringbuffer_get_type (void)
{
  static GType ringbuffer_type = 0;

  if (!ringbuffer_type) {
    static const GTypeInfo ringbuffer_info = {
      sizeof (GstPulseRingBufferClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_pulseringbuffer_class_init,
      NULL,
      NULL,
      sizeof (GstPulseRingBuffer),
      0,
      (GInstanceInitFunc) gst_pulseringbuffer_init,
      NULL
    };

    ringbuffer_type =
        g_type_register_static (GST_TYPE_RING_BUFFER, "GstPulseSinkRingBuffer",
        &ringbuffer_info, 0);
  }
  return ringbuffer_type;
}

static void
gst_pulseringbuffer_class_init (GstPulseRingBufferClass * klass)
{
  GObjectClass *gobject_class;
  GstRingBufferClass *gstringbuffer_class;

  gobject_class = (GObjectClass *) klass;
  gstringbuffer_class = (GstRingBufferClass *) klass;

  ring_parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_pulseringbuffer_finalize);

  gstringbuffer_class->open_device =
      GST_DEBUG_FUNCPTR (gst_pulseringbuffer_open_device);
  gstringbuffer_class->close_device =
      GST_DEBUG_FUNCPTR (gst_pulseringbuffer_close_device);
  gstringbuffer_class->acquire =
      GST_DEBUG_FUNCPTR (gst_pulseringbuffer_acquire);
  gstringbuffer_class->release =
      GST_DEBUG_FUNCPTR (gst_pulseringbuffer_release);
  gstringbuffer_class->start = GST_DEBUG_FUNCPTR (gst_pulseringbuffer_start);
  gstringbuffer_class->pause = GST_DEBUG_FUNCPTR (gst_pulseringbuffer_pause);
  gstringbuffer_class->resume = GST_DEBUG_FUNCPTR (gst_pulseringbuffer_start);
  gstringbuffer_class->stop = GST_DEBUG_FUNCPTR (gst_pulseringbuffer_stop);
  gstringbuffer_class->clear_all =
      GST_DEBUG_FUNCPTR (gst_pulseringbuffer_clear);

  gstringbuffer_class->commit = GST_DEBUG_FUNCPTR (gst_pulseringbuffer_commit);

  /* ref class from a thread-safe context to work around missing bit of
   * thread-safety in GObject */
  g_type_class_ref (GST_TYPE_PULSERING_BUFFER);
}

static void
gst_pulseringbuffer_init (GstPulseRingBuffer * pbuf,
    GstPulseRingBufferClass * g_class)
{
  pbuf->stream_name = NULL;
  pbuf->context = NULL;
  pbuf->stream = NULL;

#ifdef HAVE_PULSE_0_9_13
  pa_sample_spec_init (&pbuf->sample_spec);
#else
  pbuf->sample_spec.format = PA_SAMPLE_INVALID;
  pbuf->sample_spec.rate = 0;
  pbuf->sample_spec.channels = 0;
#endif

  pbuf->corked = TRUE;
  pbuf->in_commit = FALSE;
  pbuf->paused = FALSE;
}

static void
gst_pulsering_destroy_stream (GstPulseRingBuffer * pbuf)
{
  if (pbuf->stream) {
    pa_stream_disconnect (pbuf->stream);

    /* Make sure we don't get any further callbacks */
    pa_stream_set_state_callback (pbuf->stream, NULL, NULL);
    pa_stream_set_write_callback (pbuf->stream, NULL, NULL);
    pa_stream_set_underflow_callback (pbuf->stream, NULL, NULL);
    pa_stream_set_overflow_callback (pbuf->stream, NULL, NULL);

    pa_stream_unref (pbuf->stream);
    pbuf->stream = NULL;
  }

  g_free (pbuf->stream_name);
  pbuf->stream_name = NULL;
}

static void
gst_pulsering_destroy_context (GstPulseRingBuffer * pbuf)
{
  gst_pulsering_destroy_stream (pbuf);

  if (pbuf->context) {
    pa_context_disconnect (pbuf->context);

    /* Make sure we don't get any further callbacks */
    pa_context_set_state_callback (pbuf->context, NULL, NULL);
#ifdef HAVE_PULSE_0_9_12
    pa_context_set_subscribe_callback (pbuf->context, NULL, NULL);
#endif

    pa_context_unref (pbuf->context);
    pbuf->context = NULL;
  }
}

static void
gst_pulseringbuffer_finalize (GObject * object)
{
  GstPulseRingBuffer *ringbuffer;

  ringbuffer = GST_PULSERING_BUFFER_CAST (object);

  gst_pulsering_destroy_context (ringbuffer);

  G_OBJECT_CLASS (ring_parent_class)->finalize (object);
}

static gboolean
gst_pulsering_is_dead (GstPulseSink * psink, GstPulseRingBuffer * pbuf)
{
  if (!pbuf->context
      || !PA_CONTEXT_IS_GOOD (pa_context_get_state (pbuf->context))
      || !pbuf->stream
      || !PA_STREAM_IS_GOOD (pa_stream_get_state (pbuf->stream))) {
    const gchar *err_str = pbuf->context ?
        pa_strerror (pa_context_errno (pbuf->context)) : NULL;

    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED, ("Disconnected: %s",
            err_str), (NULL));
    return TRUE;
  }
  return FALSE;
}

static void
gst_pulsering_context_state_cb (pa_context * c, void *userdata)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;
  pa_context_state_t state;

  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  state = pa_context_get_state (c);
  GST_LOG_OBJECT (psink, "got new context state %d", state);

  /* psink can be null when we are shutting down and the ringbuffer is already
   * unparented */
  if (psink == NULL)
    return;

  switch (state) {
    case PA_CONTEXT_READY:
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_FAILED:
      GST_LOG_OBJECT (psink, "signaling");
      pa_threaded_mainloop_signal (psink->mainloop, 0);
      break;

    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
      break;
  }
}

#ifdef HAVE_PULSE_0_9_12
static void
gst_pulsering_context_subscribe_cb (pa_context * c,
    pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;

  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  GST_LOG_OBJECT (psink, "type %d, idx %u", t, idx);

  if (t != (PA_SUBSCRIPTION_EVENT_SINK_INPUT | PA_SUBSCRIPTION_EVENT_CHANGE) &&
      t != (PA_SUBSCRIPTION_EVENT_SINK_INPUT | PA_SUBSCRIPTION_EVENT_NEW))
    return;

  if (!pbuf->stream)
    return;

  if (idx != pa_stream_get_index (pbuf->stream))
    return;

  /* Actually this event is also triggered when other properties of
   * the stream change that are unrelated to the volume. However it is
   * probably cheaper to signal the change here and check for the
   * volume when the GObject property is read instead of querying it always. */

  /* inform streaming thread to notify */
  g_atomic_int_compare_and_exchange (&psink->notify, 0, 1);
}
#endif

/* will be called when the device should be opened. In this case we will connect
 * to the server. We should not try to open any streams in this state. */
static gboolean
gst_pulseringbuffer_open_device (GstRingBuffer * buf)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;
  gchar *name;
  pa_mainloop_api *api;

  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (buf));
  pbuf = GST_PULSERING_BUFFER_CAST (buf);

  g_assert (!pbuf->context);
  g_assert (!pbuf->stream);

  name = gst_pulse_client_name ();

  pa_threaded_mainloop_lock (psink->mainloop);

  /* get the mainloop api and create a context */
  GST_LOG_OBJECT (psink, "new context with name %s", GST_STR_NULL (name));
  api = pa_threaded_mainloop_get_api (psink->mainloop);
  if (!(pbuf->context = pa_context_new (api, name)))
    goto create_failed;

  /* register some essential callbacks */
  pa_context_set_state_callback (pbuf->context,
      gst_pulsering_context_state_cb, pbuf);
#ifdef HAVE_PULSE_0_9_12
  pa_context_set_subscribe_callback (pbuf->context,
      gst_pulsering_context_subscribe_cb, pbuf);
#endif

  /* try to connect to the server and wait for completioni, we don't want to
   * autospawn a deamon */
  GST_LOG_OBJECT (psink, "connect to server %s", GST_STR_NULL (psink->server));
  if (pa_context_connect (pbuf->context, psink->server, PA_CONTEXT_NOAUTOSPAWN,
          NULL) < 0)
    goto connect_failed;

  for (;;) {
    pa_context_state_t state;

    state = pa_context_get_state (pbuf->context);

    GST_LOG_OBJECT (psink, "context state is now %d", state);

    if (!PA_CONTEXT_IS_GOOD (state))
      goto connect_failed;

    if (state == PA_CONTEXT_READY)
      break;

    /* Wait until the context is ready */
    GST_LOG_OBJECT (psink, "waiting..");
    pa_threaded_mainloop_wait (psink->mainloop);
  }

  GST_LOG_OBJECT (psink, "opened the device");

  pa_threaded_mainloop_unlock (psink->mainloop);
  g_free (name);

  return TRUE;

  /* ERRORS */
unlock_and_fail:
  {
    gst_pulsering_destroy_context (pbuf);

    pa_threaded_mainloop_unlock (psink->mainloop);
    g_free (name);
    return FALSE;
  }
create_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("Failed to create context"), (NULL));
    goto unlock_and_fail;
  }
connect_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED, ("Failed to connect: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto unlock_and_fail;
  }
}

/* close the device */
static gboolean
gst_pulseringbuffer_close_device (GstRingBuffer * buf)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;

  pbuf = GST_PULSERING_BUFFER_CAST (buf);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (buf));

  GST_LOG_OBJECT (psink, "closing device");

  pa_threaded_mainloop_lock (psink->mainloop);
  gst_pulsering_destroy_context (pbuf);
  pa_threaded_mainloop_unlock (psink->mainloop);

  GST_LOG_OBJECT (psink, "closed device");

  return TRUE;
}

static void
gst_pulsering_stream_state_cb (pa_stream * s, void *userdata)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;
  pa_stream_state_t state;

  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  state = pa_stream_get_state (s);
  GST_LOG_OBJECT (psink, "got new stream state %d", state);

  switch (state) {
    case PA_STREAM_READY:
    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
      GST_LOG_OBJECT (psink, "signaling");
      pa_threaded_mainloop_signal (psink->mainloop, 0);
      break;
    case PA_STREAM_UNCONNECTED:
    case PA_STREAM_CREATING:
      break;
  }
}

static void
gst_pulsering_stream_request_cb (pa_stream * s, size_t length, void *userdata)
{
  GstPulseSink *psink;
  GstRingBuffer *rbuf;
  GstPulseRingBuffer *pbuf;

  rbuf = GST_RING_BUFFER_CAST (userdata);
  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  GST_LOG_OBJECT (psink, "got request for length %" G_GSIZE_FORMAT, length);

  if (pbuf->in_commit && (length >= rbuf->spec.segsize)) {
    /* only signal when we are waiting in the commit thread
     * and got request for atleast a segment */
    pa_threaded_mainloop_signal (psink->mainloop, 0);
  }
}

static void
gst_pulsering_stream_underflow_cb (pa_stream * s, void *userdata)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;

  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  GST_WARNING_OBJECT (psink, "Got underflow");
}

static void
gst_pulsering_stream_overflow_cb (pa_stream * s, void *userdata)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;

  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  GST_WARNING_OBJECT (psink, "Got overflow");
}

static void
gst_pulsering_stream_latency_cb (pa_stream * s, void *userdata)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;
  const pa_timing_info *info;
  pa_usec_t sink_usec;

  info = pa_stream_get_timing_info (s);

  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  if (!info) {
    GST_LOG_OBJECT (psink, "latency update (information unknown)");
    return;
  }
#ifdef HAVE_PULSE_0_9_11
  sink_usec = info->configured_sink_usec;
#else
  sink_usec = 0;
#endif

  GST_LOG_OBJECT (psink,
      "latency_update, %" G_GUINT64_FORMAT ", %d:%" G_GINT64_FORMAT ", %d:%"
      G_GUINT64_FORMAT ", %" G_GUINT64_FORMAT ", %" G_GUINT64_FORMAT,
      GST_TIMEVAL_TO_TIME (info->timestamp), info->write_index_corrupt,
      info->write_index, info->read_index_corrupt, info->read_index,
      info->sink_usec, sink_usec);
}

static void
gst_pulsering_stream_suspended_cb (pa_stream * p, void *userdata)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;

  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  if (pa_stream_is_suspended (p))
    GST_DEBUG_OBJECT (psink, "stream suspended");
  else
    GST_DEBUG_OBJECT (psink, "stream resumed");
}

#ifdef HAVE_PULSE_0_9_11
static void
gst_pulsering_stream_started_cb (pa_stream * p, void *userdata)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;

  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  GST_DEBUG_OBJECT (psink, "stream started");
}
#endif

#ifdef HAVE_PULSE_0_9_15
static void
gst_pulsering_stream_event_cb (pa_stream * p, const char *name,
    pa_proplist * pl, void *userdata)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;

  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  if (!strcmp (name, PA_STREAM_EVENT_REQUEST_CORK)) {
    /* the stream wants to PAUSE, post a message for the application. */
    GST_DEBUG_OBJECT (psink, "got request for CORK");
    gst_element_post_message (GST_ELEMENT_CAST (psink),
        gst_message_new_request_state (GST_OBJECT_CAST (psink),
            GST_STATE_PAUSED));

  } else if (!strcmp (name, PA_STREAM_EVENT_REQUEST_UNCORK)) {
    GST_DEBUG_OBJECT (psink, "got request for UNCORK");
    gst_element_post_message (GST_ELEMENT_CAST (psink),
        gst_message_new_request_state (GST_OBJECT_CAST (psink),
            GST_STATE_PLAYING));
  } else {
    GST_DEBUG_OBJECT (psink, "got unknown event %s", name);
  }
}
#endif

/* This method should create a new stream of the given @spec. No playback should
 * start yet so we start in the corked state. */
static gboolean
gst_pulseringbuffer_acquire (GstRingBuffer * buf, GstRingBufferSpec * spec)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;
  pa_buffer_attr wanted;
  const pa_buffer_attr *actual;
  pa_channel_map channel_map;
  pa_operation *o = NULL;
#ifdef HAVE_PULSE_0_9_20
  pa_cvolume v;
#endif
  pa_cvolume *pv = NULL;
  pa_stream_flags_t flags;
  const gchar *name;
  GstAudioClock *clock;

  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (buf));
  pbuf = GST_PULSERING_BUFFER_CAST (buf);

  GST_LOG_OBJECT (psink, "creating sample spec");
  /* convert the gstreamer sample spec to the pulseaudio format */
  if (!gst_pulse_fill_sample_spec (spec, &pbuf->sample_spec))
    goto invalid_spec;

  pa_threaded_mainloop_lock (psink->mainloop);

  /* we need a context and a no stream */
  g_assert (pbuf->context);
  g_assert (!pbuf->stream);

  /* enable event notifications */
  GST_LOG_OBJECT (psink, "subscribing to context events");
  if (!(o = pa_context_subscribe (pbuf->context,
              PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL)))
    goto subscribe_failed;

  pa_operation_unref (o);

  /* initialize the channel map */
  gst_pulse_gst_to_channel_map (&channel_map, spec);

  /* find a good name for the stream */
  if (psink->stream_name)
    name = psink->stream_name;
  else
    name = "Playback Stream";

  /* create a stream */
  GST_LOG_OBJECT (psink, "creating stream with name %s", name);
  if (!(pbuf->stream = pa_stream_new (pbuf->context,
              name, &pbuf->sample_spec, &channel_map)))
    goto stream_failed;

  /* install essential callbacks */
  pa_stream_set_state_callback (pbuf->stream,
      gst_pulsering_stream_state_cb, pbuf);
  pa_stream_set_write_callback (pbuf->stream,
      gst_pulsering_stream_request_cb, pbuf);
  pa_stream_set_underflow_callback (pbuf->stream,
      gst_pulsering_stream_underflow_cb, pbuf);
  pa_stream_set_overflow_callback (pbuf->stream,
      gst_pulsering_stream_overflow_cb, pbuf);
  pa_stream_set_latency_update_callback (pbuf->stream,
      gst_pulsering_stream_latency_cb, pbuf);
  pa_stream_set_suspended_callback (pbuf->stream,
      gst_pulsering_stream_suspended_cb, pbuf);
#ifdef HAVE_PULSE_0_9_11
  pa_stream_set_started_callback (pbuf->stream,
      gst_pulsering_stream_started_cb, pbuf);
#endif
#ifdef HAVE_PULSE_0_9_15
  pa_stream_set_event_callback (pbuf->stream,
      gst_pulsering_stream_event_cb, pbuf);
#endif

  /* buffering requirements. When setting prebuf to 0, the stream will not pause
   * when we cause an underrun, which causes time to continue. */
  memset (&wanted, 0, sizeof (wanted));
  wanted.tlength = spec->segtotal * spec->segsize;
  wanted.maxlength = -1;
  wanted.prebuf = 0;
  wanted.minreq = spec->segsize;

  GST_INFO_OBJECT (psink, "tlength:   %d", wanted.tlength);
  GST_INFO_OBJECT (psink, "maxlength: %d", wanted.maxlength);
  GST_INFO_OBJECT (psink, "prebuf:    %d", wanted.prebuf);
  GST_INFO_OBJECT (psink, "minreq:    %d", wanted.minreq);

#ifdef HAVE_PULSE_0_9_20
  /* configure volume when we changed it, else we leave the default */
  if (psink->volume_set) {
    GST_LOG_OBJECT (psink, "have volume of %f", psink->volume);
    pv = &v;
    gst_pulse_cvolume_from_linear (pv, pbuf->sample_spec.channels,
        psink->volume);
  } else {
    pv = NULL;
  }
#endif

  /* construct the flags */
  flags = PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE |
#ifdef HAVE_PULSE_0_9_11
      PA_STREAM_ADJUST_LATENCY |
#endif
      PA_STREAM_START_CORKED;

#ifdef HAVE_PULSE_0_9_12
  if (psink->mute_set && psink->mute)
    flags |= PA_STREAM_START_MUTED;
#endif

  /* we always start corked (see flags above) */
  pbuf->corked = TRUE;

  /* try to connect now */
  GST_LOG_OBJECT (psink, "connect for playback to device %s",
      GST_STR_NULL (psink->device));
  if (pa_stream_connect_playback (pbuf->stream, psink->device,
          &wanted, flags, pv, NULL) < 0)
    goto connect_failed;

  /* our clock will now start from 0 again */
  clock = GST_AUDIO_CLOCK (GST_BASE_AUDIO_SINK (psink)->provided_clock);
  gst_audio_clock_reset (clock, 0);

  for (;;) {
    pa_stream_state_t state;

    state = pa_stream_get_state (pbuf->stream);

    GST_LOG_OBJECT (psink, "stream state is now %d", state);

    if (!PA_STREAM_IS_GOOD (state))
      goto connect_failed;

    if (state == PA_STREAM_READY)
      break;

    /* Wait until the stream is ready */
    pa_threaded_mainloop_wait (psink->mainloop);
  }

  /* After we passed the volume off of to PA we never want to set it
     again, since it is PA's job to save/restore volumes.  */
  psink->volume_set = psink->mute_set = FALSE;

  GST_LOG_OBJECT (psink, "stream is acquired now");

  /* get the actual buffering properties now */
  actual = pa_stream_get_buffer_attr (pbuf->stream);

  GST_INFO_OBJECT (psink, "tlength:   %d (wanted: %d)", actual->tlength,
      wanted.tlength);
  GST_INFO_OBJECT (psink, "maxlength: %d", actual->maxlength);
  GST_INFO_OBJECT (psink, "prebuf:    %d", actual->prebuf);
  GST_INFO_OBJECT (psink, "minreq:    %d (wanted %d)", actual->minreq,
      wanted.minreq);

  spec->segsize = actual->minreq;
  spec->segtotal = actual->tlength / spec->segsize;

  pa_threaded_mainloop_unlock (psink->mainloop);

  return TRUE;

  /* ERRORS */
unlock_and_fail:
  {
    gst_pulsering_destroy_stream (pbuf);
    pa_threaded_mainloop_unlock (psink->mainloop);

    return FALSE;
  }
invalid_spec:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, SETTINGS,
        ("Invalid sample specification."), (NULL));
    return FALSE;
  }
subscribe_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("pa_context_subscribe() failed: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto unlock_and_fail;
  }
stream_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("Failed to create stream: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto unlock_and_fail;
  }
connect_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("Failed to connect stream: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto unlock_and_fail;
  }
}

/* free the stream that we acquired before */
static gboolean
gst_pulseringbuffer_release (GstRingBuffer * buf)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;

  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (buf));
  pbuf = GST_PULSERING_BUFFER_CAST (buf);

  pa_threaded_mainloop_lock (psink->mainloop);
  gst_pulsering_destroy_stream (pbuf);
  pa_threaded_mainloop_unlock (psink->mainloop);

  return TRUE;
}

static void
gst_pulsering_success_cb (pa_stream * s, int success, void *userdata)
{
  GstPulseRingBuffer *pbuf;
  GstPulseSink *psink;

  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  pa_threaded_mainloop_signal (psink->mainloop, 0);
}

/* update the corked state of a stream, must be called with the mainloop
 * lock */
static gboolean
gst_pulsering_set_corked (GstPulseRingBuffer * pbuf, gboolean corked,
    gboolean wait)
{
  pa_operation *o = NULL;
  GstPulseSink *psink;
  gboolean res = FALSE;

  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  GST_DEBUG_OBJECT (psink, "setting corked state to %d", corked);
  if (pbuf->corked != corked) {
    if (!(o = pa_stream_cork (pbuf->stream, corked,
                gst_pulsering_success_cb, pbuf)))
      goto cork_failed;

    while (wait && pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
      pa_threaded_mainloop_wait (psink->mainloop);
      if (gst_pulsering_is_dead (psink, pbuf))
        goto server_dead;
    }
    pbuf->corked = corked;
  } else {
    GST_DEBUG_OBJECT (psink, "skipping, already in requested state");
  }
  res = TRUE;

cleanup:
  if (o)
    pa_operation_unref (o);

  return res;

  /* ERRORS */
server_dead:
  {
    GST_DEBUG_OBJECT (psink, "the server is dead");
    goto cleanup;
  }
cork_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("pa_stream_cork() failed: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto cleanup;
  }
}

static void
gst_pulseringbuffer_clear (GstRingBuffer * buf)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;
  pa_operation *o = NULL;

  pbuf = GST_PULSERING_BUFFER_CAST (buf);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  pa_threaded_mainloop_lock (psink->mainloop);
  GST_DEBUG_OBJECT (psink, "clearing");
  if (pbuf->stream) {
    /* don't wait for the flush to complete */
    if ((o = pa_stream_flush (pbuf->stream, NULL, pbuf)))
      pa_operation_unref (o);
  }
  pa_threaded_mainloop_unlock (psink->mainloop);
}

static void
mainloop_enter_defer_cb (pa_mainloop_api * api, void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);
  GstMessage *message;
  GValue val = { 0 };

  g_value_init (&val, G_TYPE_POINTER);
  g_value_set_pointer (&val, g_thread_self ());

  GST_DEBUG_OBJECT (pulsesink, "posting ENTER stream status");
  message = gst_message_new_stream_status (GST_OBJECT (pulsesink),
      GST_STREAM_STATUS_TYPE_ENTER, GST_ELEMENT (pulsesink));
  gst_message_set_stream_status_object (message, &val);

  gst_element_post_message (GST_ELEMENT (pulsesink), message);

  /* signal the waiter */
  pulsesink->pa_defer_ran = TRUE;
  pa_threaded_mainloop_signal (pulsesink->mainloop, 0);
}

/* start/resume playback ASAP, we don't uncork here but in the commit method */
static gboolean
gst_pulseringbuffer_start (GstRingBuffer * buf)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;

  pbuf = GST_PULSERING_BUFFER_CAST (buf);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  pa_threaded_mainloop_lock (psink->mainloop);

  GST_DEBUG_OBJECT (psink, "scheduling stream status");
  psink->pa_defer_ran = FALSE;
  pa_mainloop_api_once (pa_threaded_mainloop_get_api (psink->mainloop),
      mainloop_enter_defer_cb, psink);

  GST_DEBUG_OBJECT (psink, "starting");
  pbuf->paused = FALSE;
  gst_pulsering_set_corked (pbuf, FALSE, FALSE);
  pa_threaded_mainloop_unlock (psink->mainloop);

  return TRUE;
}

/* pause/stop playback ASAP */
static gboolean
gst_pulseringbuffer_pause (GstRingBuffer * buf)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;
  gboolean res;

  pbuf = GST_PULSERING_BUFFER_CAST (buf);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  pa_threaded_mainloop_lock (psink->mainloop);
  GST_DEBUG_OBJECT (psink, "pausing and corking");
  /* make sure the commit method stops writing */
  pbuf->paused = TRUE;
  res = gst_pulsering_set_corked (pbuf, TRUE, FALSE);
  if (pbuf->in_commit) {
    /* we are waiting in a commit, signal */
    GST_DEBUG_OBJECT (psink, "signal commit");
    pa_threaded_mainloop_signal (psink->mainloop, 0);
  }
  pa_threaded_mainloop_unlock (psink->mainloop);

  return res;
}

static void
mainloop_leave_defer_cb (pa_mainloop_api * api, void *userdata)
{
  GstPulseSink *pulsesink = GST_PULSESINK (userdata);
  GstMessage *message;
  GValue val = { 0 };

  g_value_init (&val, G_TYPE_POINTER);
  g_value_set_pointer (&val, g_thread_self ());

  GST_DEBUG_OBJECT (pulsesink, "posting LEAVE stream status");
  message = gst_message_new_stream_status (GST_OBJECT (pulsesink),
      GST_STREAM_STATUS_TYPE_LEAVE, GST_ELEMENT (pulsesink));
  gst_message_set_stream_status_object (message, &val);
  gst_element_post_message (GST_ELEMENT (pulsesink), message);

  pulsesink->pa_defer_ran = TRUE;
  pa_threaded_mainloop_signal (pulsesink->mainloop, 0);
  gst_object_unref (pulsesink);
}

/* stop playback, we flush everything. */
static gboolean
gst_pulseringbuffer_stop (GstRingBuffer * buf)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;
  gboolean res = FALSE;
  pa_operation *o = NULL;

  pbuf = GST_PULSERING_BUFFER_CAST (buf);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  pa_threaded_mainloop_lock (psink->mainloop);
  pbuf->paused = TRUE;
  res = gst_pulsering_set_corked (pbuf, TRUE, TRUE);
  /* Inform anyone waiting in _commit() call that it shall wakeup */
  if (pbuf->in_commit) {
    GST_DEBUG_OBJECT (psink, "signal commit thread");
    pa_threaded_mainloop_signal (psink->mainloop, 0);
  }

  if (strcmp (psink->pa_version, "0.9.12")) {
    /* then try to flush, it's not fatal when this fails */
    GST_DEBUG_OBJECT (psink, "flushing");
    if ((o = pa_stream_flush (pbuf->stream, gst_pulsering_success_cb, pbuf))) {
      while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
        GST_DEBUG_OBJECT (psink, "wait for completion");
        pa_threaded_mainloop_wait (psink->mainloop);
        if (gst_pulsering_is_dead (psink, pbuf))
          goto server_dead;
      }
      GST_DEBUG_OBJECT (psink, "flush completed");
    }
  }
  res = TRUE;

cleanup:
  if (o) {
    pa_operation_cancel (o);
    pa_operation_unref (o);
  }

  GST_DEBUG_OBJECT (psink, "scheduling stream status");
  psink->pa_defer_ran = FALSE;
  gst_object_ref (psink);
  pa_mainloop_api_once (pa_threaded_mainloop_get_api (psink->mainloop),
      mainloop_leave_defer_cb, psink);

  GST_DEBUG_OBJECT (psink, "waiting for stream status");
  pa_threaded_mainloop_unlock (psink->mainloop);

  return res;

  /* ERRORS */
server_dead:
  {
    GST_DEBUG_OBJECT (psink, "the server is dead");
    goto cleanup;
  }
}

/* in_samples >= out_samples, rate > 1.0 */
#define FWD_UP_SAMPLES(s,se,d,de)               \
G_STMT_START {                                  \
  guint8 *sb = s, *db = d;                      \
  while (s <= se && d < de) {                   \
    memcpy (d, s, bps);                         \
    s += bps;                                   \
    *accum += outr;                             \
    if ((*accum << 1) >= inr) {                 \
      *accum -= inr;                            \
      d += bps;                                 \
    }                                           \
  }                                             \
  in_samples -= (s - sb)/bps;                   \
  out_samples -= (d - db)/bps;                  \
  GST_DEBUG ("fwd_up end %d/%d",*accum,*toprocess);     \
} G_STMT_END

/* out_samples > in_samples, for rates smaller than 1.0 */
#define FWD_DOWN_SAMPLES(s,se,d,de)             \
G_STMT_START {                                  \
  guint8 *sb = s, *db = d;                      \
  while (s <= se && d < de) {                   \
    memcpy (d, s, bps);                         \
    d += bps;                                   \
    *accum += inr;                              \
    if ((*accum << 1) >= outr) {                \
      *accum -= outr;                           \
      s += bps;                                 \
    }                                           \
  }                                             \
  in_samples -= (s - sb)/bps;                   \
  out_samples -= (d - db)/bps;                  \
  GST_DEBUG ("fwd_down end %d/%d",*accum,*toprocess);   \
} G_STMT_END

#define REV_UP_SAMPLES(s,se,d,de)               \
G_STMT_START {                                  \
  guint8 *sb = se, *db = d;                     \
  while (s <= se && d < de) {                   \
    memcpy (d, se, bps);                        \
    se -= bps;                                  \
    *accum += outr;                             \
    while (d < de && (*accum << 1) >= inr) {    \
      *accum -= inr;                            \
      d += bps;                                 \
    }                                           \
  }                                             \
  in_samples -= (sb - se)/bps;                  \
  out_samples -= (d - db)/bps;                  \
  GST_DEBUG ("rev_up end %d/%d",*accum,*toprocess);     \
} G_STMT_END

#define REV_DOWN_SAMPLES(s,se,d,de)             \
G_STMT_START {                                  \
  guint8 *sb = se, *db = d;                     \
  while (s <= se && d < de) {                   \
    memcpy (d, se, bps);                        \
    d += bps;                                   \
    *accum += inr;                              \
    while (s <= se && (*accum << 1) >= outr) {  \
      *accum -= outr;                           \
      se -= bps;                                \
    }                                           \
  }                                             \
  in_samples -= (sb - se)/bps;                  \
  out_samples -= (d - db)/bps;                  \
  GST_DEBUG ("rev_down end %d/%d",*accum,*toprocess);   \
} G_STMT_END


/* our custom commit function because we write into the buffer of pulseaudio
 * instead of keeping our own buffer */
static guint
gst_pulseringbuffer_commit (GstRingBuffer * buf, guint64 * sample,
    guchar * data, gint in_samples, gint out_samples, gint * accum)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;
  guint result;
  guint8 *data_end;
  gboolean reverse;
  gint *toprocess;
  gint inr, outr, bps;
  gint64 offset;
  guint bufsize;

  pbuf = GST_PULSERING_BUFFER_CAST (buf);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  /* FIXME post message rather than using a signal (as mixer interface) */
  if (g_atomic_int_compare_and_exchange (&psink->notify, 1, 0)) {
    g_object_notify (G_OBJECT (psink), "volume");
    g_object_notify (G_OBJECT (psink), "mute");
  }

  /* make sure the ringbuffer is started */
  if (G_UNLIKELY (g_atomic_int_get (&buf->state) !=
          GST_RING_BUFFER_STATE_STARTED)) {
    /* see if we are allowed to start it */
    if (G_UNLIKELY (g_atomic_int_get (&buf->abidata.ABI.may_start) == FALSE))
      goto no_start;

    GST_DEBUG_OBJECT (buf, "start!");
    if (!gst_ring_buffer_start (buf))
      goto start_failed;
  }

  pa_threaded_mainloop_lock (psink->mainloop);
  GST_DEBUG_OBJECT (psink, "entering commit");
  pbuf->in_commit = TRUE;

  bps = buf->spec.bytes_per_sample;
  bufsize = buf->spec.segsize * buf->spec.segtotal;

  /* our toy resampler for trick modes */
  reverse = out_samples < 0;
  out_samples = ABS (out_samples);

  if (in_samples >= out_samples)
    toprocess = &in_samples;
  else
    toprocess = &out_samples;

  inr = in_samples - 1;
  outr = out_samples - 1;

  GST_DEBUG_OBJECT (psink, "in %d, out %d", inr, outr);

  /* data_end points to the last sample we have to write, not past it. This is
   * needed to properly handle reverse playback: it points to the last sample. */
  data_end = data + (bps * inr);

  if (pbuf->paused)
    goto was_paused;

  /* offset is in bytes */
  offset = *sample * bps;

  while (*toprocess > 0) {
    size_t avail;
    guint towrite;

    GST_LOG_OBJECT (psink,
        "need to write %d samples at offset %" G_GINT64_FORMAT, *toprocess,
        offset);

    for (;;) {
      /* FIXME, this is not quite right */
      if ((avail = pa_stream_writable_size (pbuf->stream)) == (size_t) - 1)
        goto writable_size_failed;

      /* We always try to satisfy a request for data */
      GST_LOG_OBJECT (psink, "writable bytes %" G_GSIZE_FORMAT, avail);

      /* convert to samples, we can only deal with multiples of the
       * sample size */
      avail /= bps;

      if (avail > 0)
        break;

      /* see if we need to uncork because we have no free space */
      if (pbuf->corked) {
        if (!gst_pulsering_set_corked (pbuf, FALSE, FALSE))
          goto uncork_failed;
      }

      /* we can't write a single byte, wait a bit */
      GST_LOG_OBJECT (psink, "waiting for free space");
      pa_threaded_mainloop_wait (psink->mainloop);

      if (pbuf->paused)
        goto was_paused;
    }

    if (avail > out_samples)
      avail = out_samples;

    towrite = avail * bps;

    GST_LOG_OBJECT (psink, "writing %u samples at offset %" G_GUINT64_FORMAT,
        (guint) avail, offset);

    if (G_LIKELY (inr == outr && !reverse)) {
      /* no rate conversion, simply write out the samples */
      if (pa_stream_write (pbuf->stream, data, towrite, NULL, offset,
              PA_SEEK_ABSOLUTE) < 0)
        goto write_failed;

      data += towrite;
      in_samples -= avail;
      out_samples -= avail;
    } else {
      guint8 *dest, *d, *d_end;

      /* we need to allocate a temporary buffer to resample the data into,
       * FIXME, we should have a pulseaudio API to allocate this buffer for us
       * from the shared memory. */
      dest = d = g_malloc (towrite);
      d_end = d + towrite;

      if (!reverse) {
        if (inr >= outr)
          /* forward speed up */
          FWD_UP_SAMPLES (data, data_end, d, d_end);
        else
          /* forward slow down */
          FWD_DOWN_SAMPLES (data, data_end, d, d_end);
      } else {
        if (inr >= outr)
          /* reverse speed up */
          REV_UP_SAMPLES (data, data_end, d, d_end);
        else
          /* reverse slow down */
          REV_DOWN_SAMPLES (data, data_end, d, d_end);
      }
      /* see what we have left to write */
      towrite = (d - dest);
      if (pa_stream_write (pbuf->stream, dest, towrite,
              g_free, offset, PA_SEEK_ABSOLUTE) < 0)
        goto write_failed;

      avail = towrite / bps;
    }
    *sample += avail;
    offset += avail * bps;

    /* check if we need to uncork after writing the samples */
    if (pbuf->corked) {
      const pa_timing_info *info;

      if ((info = pa_stream_get_timing_info (pbuf->stream))) {
        GST_LOG_OBJECT (psink,
            "read_index at %" G_GUINT64_FORMAT ", offset %" G_GINT64_FORMAT,
            info->read_index, offset);

        /* we uncork when the read_index is too far behind the offset we need
         * to write to. */
        if (info->read_index + bufsize <= offset) {
          if (!gst_pulsering_set_corked (pbuf, FALSE, FALSE))
            goto uncork_failed;
        }
      } else {
        GST_LOG_OBJECT (psink, "no timing info available yet");
      }
    }
  }
  /* we consumed all samples here */
  data = data_end + bps;

  pbuf->in_commit = FALSE;
  pa_threaded_mainloop_unlock (psink->mainloop);

done:
  result = inr - ((data_end - data) / bps);
  GST_LOG_OBJECT (psink, "wrote %d samples", result);

  return result;

  /* ERRORS */
unlock_and_fail:
  {
    pbuf->in_commit = FALSE;
    GST_LOG_OBJECT (psink, "we are reset");
    pa_threaded_mainloop_unlock (psink->mainloop);
    goto done;
  }
no_start:
  {
    GST_LOG_OBJECT (psink, "we can not start");
    return 0;
  }
start_failed:
  {
    GST_LOG_OBJECT (psink, "failed to start the ringbuffer");
    return 0;
  }
uncork_failed:
  {
    pbuf->in_commit = FALSE;
    GST_ERROR_OBJECT (psink, "uncork failed");
    pa_threaded_mainloop_unlock (psink->mainloop);
    goto done;
  }
was_paused:
  {
    pbuf->in_commit = FALSE;
    GST_LOG_OBJECT (psink, "we are paused");
    pa_threaded_mainloop_unlock (psink->mainloop);
    goto done;
  }
writable_size_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("pa_stream_writable_size() failed: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto unlock_and_fail;
  }
write_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("pa_stream_write() failed: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto unlock_and_fail;
  }
}

static void gst_pulsesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pulsesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_pulsesink_finalize (GObject * object);

static gboolean gst_pulsesink_event (GstBaseSink * sink, GstEvent * event);

static void gst_pulsesink_init_interfaces (GType type);

#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
# define ENDIANNESS   "LITTLE_ENDIAN, BIG_ENDIAN"
#else
# define ENDIANNESS   "BIG_ENDIAN, LITTLE_ENDIAN"
#endif

GST_IMPLEMENT_PULSEPROBE_METHODS (GstPulseSink, gst_pulsesink);
GST_BOILERPLATE_FULL (GstPulseSink, gst_pulsesink, GstBaseAudioSink,
    GST_TYPE_BASE_AUDIO_SINK, gst_pulsesink_init_interfaces);

static gboolean
gst_pulsesink_interface_supported (GstImplementsInterface *
    iface, GType interface_type)
{
  GstPulseSink *this = GST_PULSESINK_CAST (iface);

  if (interface_type == GST_TYPE_PROPERTY_PROBE && this->probe)
    return TRUE;
  if (interface_type == GST_TYPE_STREAM_VOLUME)
    return TRUE;

  return FALSE;
}

static void
gst_pulsesink_implements_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_pulsesink_interface_supported;
}

static void
gst_pulsesink_init_interfaces (GType type)
{
  static const GInterfaceInfo implements_iface_info = {
    (GInterfaceInitFunc) gst_pulsesink_implements_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo probe_iface_info = {
    (GInterfaceInitFunc) gst_pulsesink_property_probe_interface_init,
    NULL,
    NULL,
  };
#ifdef HAVE_PULSE_0_9_12
  static const GInterfaceInfo svol_iface_info = {
    NULL, NULL, NULL
  };

  g_type_add_interface_static (type, GST_TYPE_STREAM_VOLUME, &svol_iface_info);
#endif

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_iface_info);
  g_type_add_interface_static (type, GST_TYPE_PROPERTY_PROBE,
      &probe_iface_info);
}

static void
gst_pulsesink_base_init (gpointer g_class)
{
  static GstStaticPadTemplate pad_template = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("audio/x-raw-int, "
          "endianness = (int) { " ENDIANNESS " }, "
          "signed = (boolean) TRUE, "
          "width = (int) 16, "
          "depth = (int) 16, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 32 ];"
          "audio/x-raw-float, "
          "endianness = (int) { " ENDIANNESS " }, "
          "width = (int) 32, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 32 ];"
          "audio/x-raw-int, "
          "endianness = (int) { " ENDIANNESS " }, "
          "signed = (boolean) TRUE, "
          "width = (int) 32, "
          "depth = (int) 32, "
          "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 32 ];"
#ifdef HAVE_PULSE_0_9_15
          "audio/x-raw-int, "
          "endianness = (int) { " ENDIANNESS " }, "
          "signed = (boolean) TRUE, "
          "width = (int) 24, "
          "depth = (int) 24, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 32 ];"
          "audio/x-raw-int, "
          "endianness = (int) { " ENDIANNESS " }, "
          "signed = (boolean) TRUE, "
          "width = (int) 32, "
          "depth = (int) 24, "
          "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 32 ];"
#endif
          "audio/x-raw-int, "
          "signed = (boolean) FALSE, "
          "width = (int) 8, "
          "depth = (int) 8, "
          "rate = (int) [ 1, MAX ], "
          "channels = (int) [ 1, 32 ];"
          "audio/x-alaw, "
          "rate = (int) [ 1, MAX], "
          "channels = (int) [ 1, 32 ];"
          "audio/x-mulaw, "
          "rate = (int) [ 1, MAX], " "channels = (int) [ 1, 32 ]")
      );

  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "PulseAudio Audio Sink",
      "Sink/Audio", "Plays audio to a PulseAudio server", "Lennart Poettering");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&pad_template));
}

static GstRingBuffer *
gst_pulsesink_create_ringbuffer (GstBaseAudioSink * sink)
{
  GstRingBuffer *buffer;

  GST_DEBUG_OBJECT (sink, "creating ringbuffer");
  buffer = g_object_new (GST_TYPE_PULSERING_BUFFER, NULL);
  GST_DEBUG_OBJECT (sink, "created ringbuffer @%p", buffer);

  return buffer;
}

static void
gst_pulsesink_class_init (GstPulseSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);
  GstBaseSinkClass *bc;
  GstBaseAudioSinkClass *gstaudiosink_class = GST_BASE_AUDIO_SINK_CLASS (klass);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_pulsesink_finalize);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_pulsesink_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_pulsesink_get_property);

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_pulsesink_event);

  /* restore the original basesink pull methods */
  bc = g_type_class_peek (GST_TYPE_BASE_SINK);
  gstbasesink_class->activate_pull = GST_DEBUG_FUNCPTR (bc->activate_pull);

  gstaudiosink_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_pulsesink_create_ringbuffer);

  /* Overwrite GObject fields */
  g_object_class_install_property (gobject_class,
      PROP_SERVER,
      g_param_spec_string ("server", "Server",
          "The PulseAudio server to connect to", DEFAULT_SERVER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "The PulseAudio sink device to connect to", DEFAULT_DEVICE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "Human-readable name of the sound device", DEFAULT_DEVICE_NAME,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

#ifdef HAVE_PULSE_0_9_12
  g_object_class_install_property (gobject_class,
      PROP_VOLUME,
      g_param_spec_double ("volume", "Volume",
          "Linear volume of this stream, 1.0=100%", 0.0, MAX_VOLUME,
          DEFAULT_VOLUME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute",
          "Mute state of this stream", DEFAULT_MUTE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif
}

/* returns the current time of the sink ringbuffer */
static GstClockTime
gst_pulsesink_get_time (GstClock * clock, GstBaseAudioSink * sink)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;
  pa_usec_t time;

  if (!sink->ringbuffer || !sink->ringbuffer->acquired)
    return GST_CLOCK_TIME_NONE;

  pbuf = GST_PULSERING_BUFFER_CAST (sink->ringbuffer);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  pa_threaded_mainloop_lock (psink->mainloop);
  if (gst_pulsering_is_dead (psink, pbuf))
    goto server_dead;

  /* if we don't have enough data to get a timestamp, just return NONE, which
   * will return the last reported time */
  if (pa_stream_get_time (pbuf->stream, &time) < 0) {
    GST_DEBUG_OBJECT (psink, "could not get time");
    time = GST_CLOCK_TIME_NONE;
  } else
    time *= 1000;
  pa_threaded_mainloop_unlock (psink->mainloop);

  GST_LOG_OBJECT (psink, "current time is %" GST_TIME_FORMAT,
      GST_TIME_ARGS (time));

  return time;

  /* ERRORS */
server_dead:
  {
    GST_DEBUG_OBJECT (psink, "the server is dead");
    pa_threaded_mainloop_unlock (psink->mainloop);

    return GST_CLOCK_TIME_NONE;
  }
}

static void
gst_pulsesink_init (GstPulseSink * pulsesink, GstPulseSinkClass * klass)
{
  guint res;

  pulsesink->server = NULL;
  pulsesink->device = NULL;
  pulsesink->device_description = NULL;

  pulsesink->volume = DEFAULT_VOLUME;
  pulsesink->volume_set = FALSE;

  pulsesink->mute = DEFAULT_MUTE;
  pulsesink->mute_set = FALSE;

  pulsesink->notify = 0;

  /* needed for conditional execution */
  pulsesink->pa_version = pa_get_library_version ();

  GST_DEBUG_OBJECT (pulsesink, "using pulseaudio version %s",
      pulsesink->pa_version);

  pulsesink->mainloop = pa_threaded_mainloop_new ();
  g_assert (pulsesink->mainloop != NULL);
  res = pa_threaded_mainloop_start (pulsesink->mainloop);
  g_assert (res == 0);

  /* TRUE for sinks, FALSE for sources */
  pulsesink->probe = gst_pulseprobe_new (G_OBJECT (pulsesink),
      G_OBJECT_GET_CLASS (pulsesink), PROP_DEVICE, pulsesink->device,
      TRUE, FALSE);

  /* override with a custom clock */
  if (GST_BASE_AUDIO_SINK (pulsesink)->provided_clock)
    gst_object_unref (GST_BASE_AUDIO_SINK (pulsesink)->provided_clock);
  GST_BASE_AUDIO_SINK (pulsesink)->provided_clock =
      gst_audio_clock_new ("GstPulseSinkClock",
      (GstAudioClockGetTimeFunc) gst_pulsesink_get_time, pulsesink);
}

static void
gst_pulsesink_finalize (GObject * object)
{
  GstPulseSink *pulsesink = GST_PULSESINK_CAST (object);

  pa_threaded_mainloop_stop (pulsesink->mainloop);

  g_free (pulsesink->server);
  g_free (pulsesink->device);
  g_free (pulsesink->device_description);

  pa_threaded_mainloop_free (pulsesink->mainloop);

  if (pulsesink->probe) {
    gst_pulseprobe_free (pulsesink->probe);
    pulsesink->probe = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

#ifdef HAVE_PULSE_0_9_12
static void
gst_pulsesink_set_volume (GstPulseSink * psink, gdouble volume)
{
  pa_cvolume v;
  pa_operation *o = NULL;
  GstPulseRingBuffer *pbuf;
  uint32_t idx;

  pa_threaded_mainloop_lock (psink->mainloop);

  GST_DEBUG_OBJECT (psink, "setting volume to %f", volume);

  pbuf = GST_PULSERING_BUFFER_CAST (GST_BASE_AUDIO_SINK (psink)->ringbuffer);
  if (pbuf == NULL || pbuf->stream == NULL)
    goto no_buffer;

  if ((idx = pa_stream_get_index (pbuf->stream)) == PA_INVALID_INDEX)
    goto no_index;

  gst_pulse_cvolume_from_linear (&v, pbuf->sample_spec.channels, volume);

  if (!(o = pa_context_set_sink_input_volume (pbuf->context, idx,
              &v, NULL, NULL)))
    goto volume_failed;

  /* We don't really care about the result of this call */
unlock:

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (psink->mainloop);

  return;

  /* ERRORS */
no_buffer:
  {
    psink->volume = volume;
    psink->volume_set = TRUE;

    GST_DEBUG_OBJECT (psink, "we have no ringbuffer");
    goto unlock;
  }
no_index:
  {
    GST_DEBUG_OBJECT (psink, "we don't have a stream index");
    goto unlock;
  }
volume_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("pa_stream_set_sink_input_volume() failed: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto unlock;
  }
}

static void
gst_pulsesink_set_mute (GstPulseSink * psink, gboolean mute)
{
  pa_operation *o = NULL;
  GstPulseRingBuffer *pbuf;
  uint32_t idx;

  pa_threaded_mainloop_lock (psink->mainloop);

  GST_DEBUG_OBJECT (psink, "setting mute state to %d", mute);

  pbuf = GST_PULSERING_BUFFER_CAST (GST_BASE_AUDIO_SINK (psink)->ringbuffer);
  if (pbuf == NULL || pbuf->stream == NULL)
    goto no_buffer;

  if ((idx = pa_stream_get_index (pbuf->stream)) == PA_INVALID_INDEX)
    goto no_index;

  if (!(o = pa_context_set_sink_input_mute (pbuf->context, idx,
              mute, NULL, NULL)))
    goto mute_failed;

  /* We don't really care about the result of this call */
unlock:

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (psink->mainloop);

  return;

  /* ERRORS */
no_buffer:
  {
    psink->mute = mute;
    psink->mute_set = TRUE;

    GST_DEBUG_OBJECT (psink, "we have no ringbuffer");
    goto unlock;
  }
no_index:
  {
    GST_DEBUG_OBJECT (psink, "we don't have a stream index");
    goto unlock;
  }
mute_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("pa_stream_set_sink_input_mute() failed: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto unlock;
  }
}

static void
gst_pulsesink_sink_input_info_cb (pa_context * c, const pa_sink_input_info * i,
    int eol, void *userdata)
{
  GstPulseRingBuffer *pbuf;
  GstPulseSink *psink;

  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  if (!i)
    goto done;

  if (!pbuf->stream)
    goto done;

  /* If the index doesn't match our current stream,
   * it implies we just recreated the stream (caps change)
   */
  if (i->index == pa_stream_get_index (pbuf->stream)) {
    psink->volume = pa_sw_volume_to_linear (pa_cvolume_max (&i->volume));
    psink->mute = i->mute;
  }

done:
  pa_threaded_mainloop_signal (psink->mainloop, 0);
}

static gdouble
gst_pulsesink_get_volume (GstPulseSink * psink)
{
  GstPulseRingBuffer *pbuf;
  pa_operation *o = NULL;
  gdouble v = DEFAULT_VOLUME;
  uint32_t idx;

  pa_threaded_mainloop_lock (psink->mainloop);

  pbuf = GST_PULSERING_BUFFER_CAST (GST_BASE_AUDIO_SINK (psink)->ringbuffer);
  if (pbuf == NULL || pbuf->stream == NULL)
    goto no_buffer;

  if ((idx = pa_stream_get_index (pbuf->stream)) == PA_INVALID_INDEX)
    goto no_index;

  if (!(o = pa_context_get_sink_input_info (pbuf->context, idx,
              gst_pulsesink_sink_input_info_cb, pbuf)))
    goto info_failed;

  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
    pa_threaded_mainloop_wait (psink->mainloop);
    if (gst_pulsering_is_dead (psink, pbuf))
      goto unlock;
  }

  v = psink->volume;

unlock:
  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (psink->mainloop);

  if (v > MAX_VOLUME) {
    GST_WARNING_OBJECT (psink, "Clipped volume from %f to %f", v, MAX_VOLUME);
    v = MAX_VOLUME;
  }

  return v;

  /* ERRORS */
no_buffer:
  {
    GST_DEBUG_OBJECT (psink, "we have no ringbuffer");
    goto unlock;
  }
no_index:
  {
    GST_DEBUG_OBJECT (psink, "we don't have a stream index");
    goto unlock;
  }
info_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("pa_context_get_sink_input_info() failed: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto unlock;
  }
}

static gboolean
gst_pulsesink_get_mute (GstPulseSink * psink)
{
  GstPulseRingBuffer *pbuf;
  pa_operation *o = NULL;
  uint32_t idx;
  gboolean mute = FALSE;

  pa_threaded_mainloop_lock (psink->mainloop);
  mute = psink->mute;

  pbuf = GST_PULSERING_BUFFER_CAST (GST_BASE_AUDIO_SINK (psink)->ringbuffer);
  if (pbuf == NULL || pbuf->stream == NULL)
    goto no_buffer;

  if ((idx = pa_stream_get_index (pbuf->stream)) == PA_INVALID_INDEX)
    goto no_index;

  if (!(o = pa_context_get_sink_input_info (pbuf->context, idx,
              gst_pulsesink_sink_input_info_cb, pbuf)))
    goto info_failed;

  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
    pa_threaded_mainloop_wait (psink->mainloop);
    if (gst_pulsering_is_dead (psink, pbuf))
      goto unlock;
  }

unlock:
  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (psink->mainloop);

  return mute;

  /* ERRORS */
no_buffer:
  {
    GST_DEBUG_OBJECT (psink, "we have no ringbuffer");
    goto unlock;
  }
no_index:
  {
    GST_DEBUG_OBJECT (psink, "we don't have a stream index");
    goto unlock;
  }
info_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("pa_context_get_sink_input_info() failed: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto unlock;
  }
}
#endif

static void
gst_pulsesink_sink_info_cb (pa_context * c, const pa_sink_info * i, int eol,
    void *userdata)
{
  GstPulseRingBuffer *pbuf;
  GstPulseSink *psink;

  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  if (!i)
    goto done;

  if (!pbuf->stream)
    goto done;

  g_assert (i->index == pa_stream_get_device_index (pbuf->stream));

  g_free (psink->device_description);
  psink->device_description = g_strdup (i->description);

done:
  pa_threaded_mainloop_signal (psink->mainloop, 0);
}

static gchar *
gst_pulsesink_device_description (GstPulseSink * psink)
{
  GstPulseRingBuffer *pbuf;
  pa_operation *o = NULL;
  gchar *t;

  pa_threaded_mainloop_lock (psink->mainloop);
  pbuf = GST_PULSERING_BUFFER_CAST (GST_BASE_AUDIO_SINK (psink)->ringbuffer);
  if (pbuf == NULL || pbuf->stream == NULL)
    goto no_buffer;

  if (!(o = pa_context_get_sink_info_by_index (pbuf->context,
              pa_stream_get_device_index (pbuf->stream),
              gst_pulsesink_sink_info_cb, pbuf)))
    goto info_failed;

  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
    pa_threaded_mainloop_wait (psink->mainloop);
    if (gst_pulsering_is_dead (psink, pbuf))
      goto unlock;
  }

unlock:
  if (o)
    pa_operation_unref (o);

  t = g_strdup (psink->device_description);
  pa_threaded_mainloop_unlock (psink->mainloop);

  return t;

  /* ERRORS */
no_buffer:
  {
    GST_DEBUG_OBJECT (psink, "we have no ringbuffer");
    goto unlock;
  }
info_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("pa_context_get_sink_info_by_index() failed: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto unlock;
  }
}

static void
gst_pulsesink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstPulseSink *pulsesink = GST_PULSESINK_CAST (object);

  switch (prop_id) {
    case PROP_SERVER:
      g_free (pulsesink->server);
      pulsesink->server = g_value_dup_string (value);
      if (pulsesink->probe)
        gst_pulseprobe_set_server (pulsesink->probe, pulsesink->server);
      break;
    case PROP_DEVICE:
      g_free (pulsesink->device);
      pulsesink->device = g_value_dup_string (value);
      break;
#ifdef HAVE_PULSE_0_9_12
    case PROP_VOLUME:
      gst_pulsesink_set_volume (pulsesink, g_value_get_double (value));
      break;
    case PROP_MUTE:
      gst_pulsesink_set_mute (pulsesink, g_value_get_boolean (value));
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pulsesink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{

  GstPulseSink *pulsesink = GST_PULSESINK_CAST (object);

  switch (prop_id) {
    case PROP_SERVER:
      g_value_set_string (value, pulsesink->server);
      break;
    case PROP_DEVICE:
      g_value_set_string (value, pulsesink->device);
      break;
    case PROP_DEVICE_NAME:
      g_value_take_string (value, gst_pulsesink_device_description (pulsesink));
      break;
#ifdef HAVE_PULSE_0_9_12
    case PROP_VOLUME:
      g_value_set_double (value, gst_pulsesink_get_volume (pulsesink));
      break;
    case PROP_MUTE:
      g_value_set_boolean (value, gst_pulsesink_get_mute (pulsesink));
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pulsesink_change_title (GstPulseSink * psink, const gchar * t)
{
  pa_operation *o = NULL;
  GstPulseRingBuffer *pbuf;

  pa_threaded_mainloop_lock (psink->mainloop);

  pbuf = GST_PULSERING_BUFFER_CAST (GST_BASE_AUDIO_SINK (psink)->ringbuffer);

  if (pbuf == NULL || pbuf->stream == NULL)
    goto no_buffer;

  g_free (pbuf->stream_name);
  pbuf->stream_name = g_strdup (t);

  if (!(o = pa_stream_set_name (pbuf->stream, pbuf->stream_name, NULL, NULL)))
    goto name_failed;

  /* We're not interested if this operation failed or not */
unlock:
  if (o)
    pa_operation_unref (o);
  pa_threaded_mainloop_unlock (psink->mainloop);

  return;

  /* ERRORS */
no_buffer:
  {
    GST_DEBUG_OBJECT (psink, "we have no ringbuffer");
    goto unlock;
  }
name_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("pa_stream_set_name() failed: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto unlock;
  }
}

#ifdef HAVE_PULSE_0_9_11
static void
gst_pulsesink_change_props (GstPulseSink * psink, GstTagList * l)
{
  static const gchar *const map[] = {
    GST_TAG_TITLE, PA_PROP_MEDIA_TITLE,

    /* might get overriden in the next iteration by GST_TAG_ARTIST */
    GST_TAG_PERFORMER, PA_PROP_MEDIA_ARTIST,

    GST_TAG_ARTIST, PA_PROP_MEDIA_ARTIST,
    GST_TAG_LANGUAGE_CODE, PA_PROP_MEDIA_LANGUAGE,
    GST_TAG_LOCATION, PA_PROP_MEDIA_FILENAME,
    /* We might add more here later on ... */
    NULL
  };
  pa_proplist *pl = NULL;
  const gchar *const *t;
  gboolean empty = TRUE;
  pa_operation *o = NULL;
  GstPulseRingBuffer *pbuf;

  pl = pa_proplist_new ();

  for (t = map; *t; t += 2) {
    gchar *n = NULL;

    if (gst_tag_list_get_string (l, *t, &n)) {

      if (n && *n) {
        pa_proplist_sets (pl, *(t + 1), n);
        empty = FALSE;
      }

      g_free (n);
    }
  }
  if (empty)
    goto finish;

  pa_threaded_mainloop_lock (psink->mainloop);
  pbuf = GST_PULSERING_BUFFER_CAST (GST_BASE_AUDIO_SINK (psink)->ringbuffer);
  if (pbuf == NULL || pbuf->stream == NULL)
    goto no_buffer;

  if (!(o = pa_stream_proplist_update (pbuf->stream, PA_UPDATE_REPLACE,
              pl, NULL, NULL)))
    goto update_failed;

  /* We're not interested if this operation failed or not */
unlock:

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (psink->mainloop);

finish:

  if (pl)
    pa_proplist_free (pl);

  return;

  /* ERRORS */
no_buffer:
  {
    GST_DEBUG_OBJECT (psink, "we have no ringbuffer");
    goto unlock;
  }
update_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("pa_stream_proplist_update() failed: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto unlock;
  }
}
#endif

static gboolean
gst_pulsesink_event (GstBaseSink * sink, GstEvent * event)
{
  GstPulseSink *pulsesink = GST_PULSESINK_CAST (sink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      gchar *title = NULL, *artist = NULL, *location = NULL, *description =
          NULL, *t = NULL, *buf = NULL;
      GstTagList *l;

      gst_event_parse_tag (event, &l);

      gst_tag_list_get_string (l, GST_TAG_TITLE, &title);
      gst_tag_list_get_string (l, GST_TAG_ARTIST, &artist);
      gst_tag_list_get_string (l, GST_TAG_LOCATION, &location);
      gst_tag_list_get_string (l, GST_TAG_DESCRIPTION, &description);

      if (!artist)
        gst_tag_list_get_string (l, GST_TAG_PERFORMER, &artist);

      if (title && artist)
        /* TRANSLATORS: 'song title' by 'artist name' */
        t = buf = g_strdup_printf (_("'%s' by '%s'"), g_strstrip (title),
            g_strstrip (artist));
      else if (title)
        t = g_strstrip (title);
      else if (description)
        t = g_strstrip (description);
      else if (location)
        t = g_strstrip (location);

      if (t)
        gst_pulsesink_change_title (pulsesink, t);

      g_free (title);
      g_free (artist);
      g_free (location);
      g_free (description);
      g_free (buf);

#ifdef HAVE_PULSE_0_9_11
      gst_pulsesink_change_props (pulsesink, l);
#endif

      break;
    }
    default:
      ;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (sink, event);
}
