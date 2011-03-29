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
 * |[
 * gst-launch -v audiotestsrc ! pulsesink stream-properties="props,media.title=test"
 * ]| Play a sine wave and set a stream property. The property can be checked
 * with "pactl list".
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
#include <gst/audio/gstaudioiec61937.h>

#include <gst/pbutils/pbutils.h>        /* only used for GST_PLUGINS_BASE_VERSION_* */

#include "pulsesink.h"
#include "pulseutil.h"

GST_DEBUG_CATEGORY_EXTERN (pulse_debug);
#define GST_CAT_DEFAULT pulse_debug

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
  PROP_CLIENT,
  PROP_STREAM_PROPERTIES,
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

typedef struct _GstPulseContext GstPulseContext;

/* Store the PA contexts in a hash table to allow easy sharing among
 * multiple instances of the sink. Keys are $context_name@$server_name
 * (strings) and values should be GstPulseContext pointers.
 */
struct _GstPulseContext
{
  pa_context *context;
  GSList *ring_buffers;
};

static GHashTable *gst_pulse_shared_contexts = NULL;

/* use one static main-loop for all instances
 * this is needed to make the context sharing work as the contexts are
 * released when releasing their parent main-loop
 */
static pa_threaded_mainloop *mainloop = NULL;
static guint mainloop_ref_ct = 0;

/* lock for access to shared resources */
static GMutex *pa_shared_resource_mutex = NULL;

/* We keep a custom ringbuffer that is backed up by data allocated by
 * pulseaudio. We must also overide the commit function to write into
 * pulseaudio memory instead. */
struct _GstPulseRingBuffer
{
  GstRingBuffer object;

  gchar *context_name;
  gchar *stream_name;

  pa_context *context;
  pa_stream *stream;

#ifdef HAVE_PULSE_1_0
  pa_format_info *format;
  guint channels;
#else
  pa_sample_spec sample_spec;
#endif

  void *m_data;
  size_t m_towrite;
  size_t m_writable;
  gint64 m_offset;
  gint64 m_lastoffset;

  gboolean corked:1;
  gboolean in_commit:1;
  gboolean paused:1;
};
struct _GstPulseRingBufferClass
{
  GstRingBufferClass parent_class;
};

static GType gst_pulseringbuffer_get_type (void);
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

G_DEFINE_TYPE (GstPulseRingBuffer, gst_pulseringbuffer, GST_TYPE_RING_BUFFER);

static void
gst_pulsesink_init_contexts (void)
{
  g_assert (pa_shared_resource_mutex == NULL);
  pa_shared_resource_mutex = g_mutex_new ();
  gst_pulse_shared_contexts = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, NULL);
}

static void
gst_pulseringbuffer_class_init (GstPulseRingBufferClass * klass)
{
  GObjectClass *gobject_class;
  GstRingBufferClass *gstringbuffer_class;

  gobject_class = (GObjectClass *) klass;
  gstringbuffer_class = (GstRingBufferClass *) klass;

  ring_parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_pulseringbuffer_finalize;

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
}

static void
gst_pulseringbuffer_init (GstPulseRingBuffer * pbuf)
{
  pbuf->stream_name = NULL;
  pbuf->context = NULL;
  pbuf->stream = NULL;

#ifdef HAVE_PULSE_1_0
  pbuf->format = NULL;
  pbuf->channels = 0;
#else
  pa_sample_spec_init (&pbuf->sample_spec);
#endif

  pbuf->m_data = NULL;
  pbuf->m_towrite = 0;
  pbuf->m_writable = 0;
  pbuf->m_offset = 0;
  pbuf->m_lastoffset = 0;

  pbuf->corked = TRUE;
  pbuf->in_commit = FALSE;
  pbuf->paused = FALSE;
}

static void
gst_pulsering_destroy_stream (GstPulseRingBuffer * pbuf)
{
  if (pbuf->stream) {

    if (pbuf->m_data) {
      /* drop shm memory buffer */
      pa_stream_cancel_write (pbuf->stream);

      /* reset internal variables */
      pbuf->m_data = NULL;
      pbuf->m_towrite = 0;
      pbuf->m_writable = 0;
      pbuf->m_offset = 0;
      pbuf->m_lastoffset = 0;
    }
#ifdef HAVE_PULSE_1_0
    if (pbuf->format) {
      pa_format_info_free (pbuf->format);
      pbuf->format = NULL;
      pbuf->channels = 0;
    }
#endif

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
  g_mutex_lock (pa_shared_resource_mutex);

  GST_DEBUG_OBJECT (pbuf, "destroying ringbuffer %p", pbuf);

  gst_pulsering_destroy_stream (pbuf);

  if (pbuf->context) {
    pa_context_unref (pbuf->context);
    pbuf->context = NULL;
  }

  if (pbuf->context_name) {
    GstPulseContext *pctx;

    pctx = g_hash_table_lookup (gst_pulse_shared_contexts, pbuf->context_name);

    GST_DEBUG_OBJECT (pbuf, "releasing context with name %s, pbuf=%p, pctx=%p",
        pbuf->context_name, pbuf, pctx);

    if (pctx) {
      pctx->ring_buffers = g_slist_remove (pctx->ring_buffers, pbuf);
      if (pctx->ring_buffers == NULL) {
        GST_DEBUG_OBJECT (pbuf,
            "destroying final context with name %s, pbuf=%p, pctx=%p",
            pbuf->context_name, pbuf, pctx);

        pa_context_disconnect (pctx->context);

        /* Make sure we don't get any further callbacks */
        pa_context_set_state_callback (pctx->context, NULL, NULL);
        pa_context_set_subscribe_callback (pctx->context, NULL, NULL);

        g_hash_table_remove (gst_pulse_shared_contexts, pbuf->context_name);

        pa_context_unref (pctx->context);
        g_slice_free (GstPulseContext, pctx);
      }
    }
    g_free (pbuf->context_name);
    pbuf->context_name = NULL;
  }
  g_mutex_unlock (pa_shared_resource_mutex);
}

static void
gst_pulseringbuffer_finalize (GObject * object)
{
  GstPulseRingBuffer *ringbuffer;

  ringbuffer = GST_PULSERING_BUFFER_CAST (object);

  gst_pulsering_destroy_context (ringbuffer);
  G_OBJECT_CLASS (ring_parent_class)->finalize (object);
}


#define CONTEXT_OK(c) ((c) && PA_CONTEXT_IS_GOOD (pa_context_get_state ((c))))
#define STREAM_OK(s) ((s) && PA_STREAM_IS_GOOD (pa_stream_get_state ((s))))

static gboolean
gst_pulsering_is_dead (GstPulseSink * psink, GstPulseRingBuffer * pbuf,
    gboolean check_stream)
{
  if (!CONTEXT_OK (pbuf->context))
    goto error;

  if (check_stream && !STREAM_OK (pbuf->stream))
    goto error;

  return FALSE;

error:
  {
    const gchar *err_str =
        pbuf->context ? pa_strerror (pa_context_errno (pbuf->context)) : NULL;
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED, ("Disconnected: %s",
            err_str), (NULL));
    return TRUE;
  }
}

static void
gst_pulsering_context_state_cb (pa_context * c, void *userdata)
{
  pa_context_state_t state;
  pa_threaded_mainloop *mainloop = (pa_threaded_mainloop *) userdata;

  state = pa_context_get_state (c);

  GST_LOG ("got new context state %d", state);

  switch (state) {
    case PA_CONTEXT_READY:
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_FAILED:
      GST_LOG ("signaling");
      pa_threaded_mainloop_signal (mainloop, 0);
      break;

    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
      break;
  }
}

static void
gst_pulsering_context_subscribe_cb (pa_context * c,
    pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
  GstPulseSink *psink;
  GstPulseContext *pctx = (GstPulseContext *) userdata;
  GSList *walk;

  if (t != (PA_SUBSCRIPTION_EVENT_SINK_INPUT | PA_SUBSCRIPTION_EVENT_CHANGE) &&
      t != (PA_SUBSCRIPTION_EVENT_SINK_INPUT | PA_SUBSCRIPTION_EVENT_NEW))
    return;

  for (walk = pctx->ring_buffers; walk; walk = g_slist_next (walk)) {
    GstPulseRingBuffer *pbuf = (GstPulseRingBuffer *) walk->data;
    psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

    GST_LOG_OBJECT (psink, "type %d, idx %u", t, idx);

    if (!pbuf->stream)
      continue;

    if (idx != pa_stream_get_index (pbuf->stream))
      continue;

#ifdef HAVE_PULSE_1_0
    if (psink->device && pa_format_info_is_pcm (pbuf->format) &&
        !g_str_equal (psink->device,
            pa_stream_get_device_name (pbuf->stream))) {
      /* Underlying sink changed. And this is not a passthrough stream. Let's
       * see if someone upstream wants to try to renegotiate. */
      GstEvent *renego;

      g_free (psink->device);
      psink->device = g_strdup (pa_stream_get_device_name (pbuf->stream));

      GST_INFO_OBJECT (psink, "emitting sink-changed");

      renego = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
          gst_structure_new ("pulse-sink-changed", NULL));

      if (!gst_pad_push_event (GST_BASE_SINK (psink)->sinkpad, renego))
        GST_DEBUG_OBJECT (psink, "Emitted sink-changed - nobody was listening");
    }
#endif

    /* Actually this event is also triggered when other properties of
     * the stream change that are unrelated to the volume. However it is
     * probably cheaper to signal the change here and check for the
     * volume when the GObject property is read instead of querying it always. */

    /* inform streaming thread to notify */
    g_atomic_int_compare_and_exchange (&psink->notify, 0, 1);
  }
}

/* will be called when the device should be opened. In this case we will connect
 * to the server. We should not try to open any streams in this state. */
static gboolean
gst_pulseringbuffer_open_device (GstRingBuffer * buf)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;
  GstPulseContext *pctx;
  pa_mainloop_api *api;
  gboolean need_unlock_shared;

  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (buf));
  pbuf = GST_PULSERING_BUFFER_CAST (buf);

  g_assert (!pbuf->stream);
  g_assert (psink->client_name);

  if (psink->server)
    pbuf->context_name = g_strdup_printf ("%s@%s", psink->client_name,
        psink->server);
  else
    pbuf->context_name = g_strdup (psink->client_name);

  pa_threaded_mainloop_lock (mainloop);

  g_mutex_lock (pa_shared_resource_mutex);
  need_unlock_shared = TRUE;

  pctx = g_hash_table_lookup (gst_pulse_shared_contexts, pbuf->context_name);
  if (pctx == NULL) {
    pctx = g_slice_new0 (GstPulseContext);

    /* get the mainloop api and create a context */
    GST_INFO_OBJECT (psink, "new context with name %s, pbuf=%p, pctx=%p",
        pbuf->context_name, pbuf, pctx);
    api = pa_threaded_mainloop_get_api (mainloop);
    if (!(pctx->context = pa_context_new (api, pbuf->context_name)))
      goto create_failed;

    pctx->ring_buffers = g_slist_prepend (pctx->ring_buffers, pbuf);
    g_hash_table_insert (gst_pulse_shared_contexts,
        g_strdup (pbuf->context_name), (gpointer) pctx);
    /* register some essential callbacks */
    pa_context_set_state_callback (pctx->context,
        gst_pulsering_context_state_cb, mainloop);
    pa_context_set_subscribe_callback (pctx->context,
        gst_pulsering_context_subscribe_cb, pctx);

    /* try to connect to the server and wait for completion, we don't want to
     * autospawn a deamon */
    GST_LOG_OBJECT (psink, "connect to server %s",
        GST_STR_NULL (psink->server));
    if (pa_context_connect (pctx->context, psink->server,
            PA_CONTEXT_NOAUTOSPAWN, NULL) < 0)
      goto connect_failed;
  } else {
    GST_INFO_OBJECT (psink,
        "reusing shared context with name %s, pbuf=%p, pctx=%p",
        pbuf->context_name, pbuf, pctx);
    pctx->ring_buffers = g_slist_prepend (pctx->ring_buffers, pbuf);
  }

  g_mutex_unlock (pa_shared_resource_mutex);
  need_unlock_shared = FALSE;

  /* context created or shared okay */
  pbuf->context = pa_context_ref (pctx->context);

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
    pa_threaded_mainloop_wait (mainloop);
  }

  GST_LOG_OBJECT (psink, "opened the device");

  pa_threaded_mainloop_unlock (mainloop);

  return TRUE;

  /* ERRORS */
unlock_and_fail:
  {
    if (need_unlock_shared)
      g_mutex_unlock (pa_shared_resource_mutex);
    gst_pulsering_destroy_context (pbuf);
    pa_threaded_mainloop_unlock (mainloop);
    return FALSE;
  }
create_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("Failed to create context"), (NULL));
    g_slice_free (GstPulseContext, pctx);
    goto unlock_and_fail;
  }
connect_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED, ("Failed to connect: %s",
            pa_strerror (pa_context_errno (pctx->context))), (NULL));
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

  pa_threaded_mainloop_lock (mainloop);
  gst_pulsering_destroy_context (pbuf);
  pa_threaded_mainloop_unlock (mainloop);

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
      pa_threaded_mainloop_signal (mainloop, 0);
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
    pa_threaded_mainloop_signal (mainloop, 0);
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
  sink_usec = info->configured_sink_usec;

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

static void
gst_pulsering_stream_started_cb (pa_stream * p, void *userdata)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;

  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  GST_DEBUG_OBJECT (psink, "stream started");
}

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
#ifdef HAVE_PULSE_1_0
  } else if (!strcmp (name, PA_STREAM_EVENT_FORMAT_LOST)) {
    GstEvent *renego;

    if (g_atomic_int_get (&psink->format_lost)) {
      /* Duplicate event before we're done reconfiguring, discard */
      return;
    }

    GST_DEBUG_OBJECT (psink, "got FORMAT LOST");
    g_atomic_int_set (&psink->format_lost, 1);
    psink->format_lost_time = g_ascii_strtoull (pa_proplist_gets (pl,
            "stream-time"), NULL, 0) * 1000;

    g_free (psink->device);
    psink->device = g_strdup (pa_proplist_gets (pl, "device"));

    renego = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
        gst_structure_new ("pulse-format-lost", NULL));

    if (!gst_pad_push_event (GST_BASE_SINK (psink)->sinkpad, renego)) {
      /* Nobody handled the format change - emit an error */
      GST_ELEMENT_ERROR (psink, STREAM, FORMAT, ("Sink format changed"),
          ("Sink format changed"));
    }
#endif
  } else {
    GST_DEBUG_OBJECT (psink, "got unknown event %s", name);
  }
}

/* Called with the mainloop locked */
static gboolean
gst_pulsering_wait_for_stream_ready (GstPulseSink * psink, pa_stream * stream)
{
  pa_stream_state_t state;

  for (;;) {
    state = pa_stream_get_state (stream);

    GST_LOG_OBJECT (psink, "stream state is now %d", state);

    if (!PA_STREAM_IS_GOOD (state))
      return FALSE;

    if (state == PA_STREAM_READY)
      return TRUE;

    /* Wait until the stream is ready */
    pa_threaded_mainloop_wait (mainloop);
  }
}


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
#ifdef HAVE_PULSE_1_0
  pa_format_info *formats[1];
#ifndef GST_DISABLE_GST_DEBUG
  gchar print_buf[PA_FORMAT_INFO_SNPRINT_MAX];
#endif
#endif

  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (buf));
  pbuf = GST_PULSERING_BUFFER_CAST (buf);

  GST_LOG_OBJECT (psink, "creating sample spec");
  /* convert the gstreamer sample spec to the pulseaudio format */
#ifdef HAVE_PULSE_1_0
  if (!gst_pulse_fill_format_info (spec, &pbuf->format, &pbuf->channels))
    goto invalid_spec;
#else
  if (!gst_pulse_fill_sample_spec (spec, &pbuf->sample_spec))
    goto invalid_spec;
#endif

  pa_threaded_mainloop_lock (mainloop);

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
#ifdef HAVE_PULSE_1_0
  if (pa_format_info_is_pcm (pbuf->format) &&
      gst_pulse_gst_to_channel_map (&channel_map, spec))
    pa_format_info_set_channel_map (pbuf->format, &channel_map);
#else
  gst_pulse_gst_to_channel_map (&channel_map, spec);
#endif

  /* find a good name for the stream */
  if (psink->stream_name)
    name = psink->stream_name;
  else
    name = "Playback Stream";

  /* create a stream */
#ifdef HAVE_PULSE_1_0
  formats[0] = pbuf->format;
  if (!(pbuf->stream = pa_stream_new_extended (pbuf->context, name, formats, 1,
              psink->proplist)))
    goto stream_failed;
#else
  GST_LOG_OBJECT (psink, "creating stream with name %s", name);
  if (!(pbuf->stream = pa_stream_new_with_proplist (pbuf->context, name,
              &pbuf->sample_spec, &channel_map, psink->proplist)))
    goto stream_failed;
#endif

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
  pa_stream_set_started_callback (pbuf->stream,
      gst_pulsering_stream_started_cb, pbuf);
  pa_stream_set_event_callback (pbuf->stream,
      gst_pulsering_stream_event_cb, pbuf);

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
#ifdef HAVE_PULSE_1_0
    if (pa_format_info_is_pcm (pbuf->format))
      gst_pulse_cvolume_from_linear (pv, pbuf->channels, psink->volume);
    else {
      GST_DEBUG_OBJECT (psink, "passthrough stream, not setting volume");
      pv = NULL;
    }
#else
    gst_pulse_cvolume_from_linear (pv, pbuf->sample_spec.channels,
        psink->volume);
#endif
  } else {
    pv = NULL;
  }
#endif

  /* construct the flags */
  flags = PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE |
      PA_STREAM_ADJUST_LATENCY | PA_STREAM_START_CORKED;

  if (psink->mute_set && psink->mute)
    flags |= PA_STREAM_START_MUTED;

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

  if (!gst_pulsering_wait_for_stream_ready (psink, pbuf->stream))
    goto connect_failed;

#ifdef HAVE_PULSE_1_0
  g_free (psink->device);
  psink->device = g_strdup (pa_stream_get_device_name (pbuf->stream));

#ifndef GST_DISABLE_GST_DEBUG
  pa_format_info_snprint (print_buf, sizeof (print_buf),
      pa_stream_get_format_info (pbuf->stream));
  GST_INFO_OBJECT (psink, "negotiated to: %s", print_buf);
#endif
#endif

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

  pa_threaded_mainloop_unlock (mainloop);

  return TRUE;

  /* ERRORS */
unlock_and_fail:
  {
    gst_pulsering_destroy_stream (pbuf);
    pa_threaded_mainloop_unlock (mainloop);

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
  GstPulseRingBuffer *pbuf;

  pbuf = GST_PULSERING_BUFFER_CAST (buf);

  pa_threaded_mainloop_lock (mainloop);
  gst_pulsering_destroy_stream (pbuf);
  pa_threaded_mainloop_unlock (mainloop);

#ifdef HAVE_PULSE_1_0
  {
    GstPulseSink *psink;

    psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));
    g_atomic_int_set (&psink->format_lost, FALSE);
    psink->format_lost_time = GST_CLOCK_TIME_NONE;
  }
#endif

  return TRUE;
}

static void
gst_pulsering_success_cb (pa_stream * s, int success, void *userdata)
{
  pa_threaded_mainloop_signal (mainloop, 0);
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

#ifdef HAVE_PULSE_1_0
  if (g_atomic_int_get (&psink->format_lost)) {
    /* Sink format changed, stream's gone so fake being paused */
    return TRUE;
  }
#endif

  GST_DEBUG_OBJECT (psink, "setting corked state to %d", corked);
  if (pbuf->corked != corked) {
    if (!(o = pa_stream_cork (pbuf->stream, corked,
                gst_pulsering_success_cb, pbuf)))
      goto cork_failed;

    while (wait && pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
      pa_threaded_mainloop_wait (mainloop);
      if (gst_pulsering_is_dead (psink, pbuf, TRUE))
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

  pa_threaded_mainloop_lock (mainloop);
  GST_DEBUG_OBJECT (psink, "clearing");
  if (pbuf->stream) {
    /* don't wait for the flush to complete */
    if ((o = pa_stream_flush (pbuf->stream, NULL, pbuf)))
      pa_operation_unref (o);
  }
  pa_threaded_mainloop_unlock (mainloop);
}

/* called from pulse with the mainloop lock */
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

  g_return_if_fail (pulsesink->defer_pending);
  pulsesink->defer_pending--;
  pa_threaded_mainloop_signal (mainloop, 0);
}

/* start/resume playback ASAP, we don't uncork here but in the commit method */
static gboolean
gst_pulseringbuffer_start (GstRingBuffer * buf)
{
  GstPulseSink *psink;
  GstPulseRingBuffer *pbuf;

  pbuf = GST_PULSERING_BUFFER_CAST (buf);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  pa_threaded_mainloop_lock (mainloop);

  GST_DEBUG_OBJECT (psink, "scheduling stream status");
  psink->defer_pending++;
  pa_mainloop_api_once (pa_threaded_mainloop_get_api (mainloop),
      mainloop_enter_defer_cb, psink);

  GST_DEBUG_OBJECT (psink, "starting");
  pbuf->paused = FALSE;

  /* EOS needs running clock */
  if (GST_BASE_SINK_CAST (psink)->eos ||
      g_atomic_int_get (&GST_BASE_AUDIO_SINK (psink)->abidata.ABI.
          eos_rendering))
    gst_pulsering_set_corked (pbuf, FALSE, FALSE);

  pa_threaded_mainloop_unlock (mainloop);

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

  pa_threaded_mainloop_lock (mainloop);
  GST_DEBUG_OBJECT (psink, "pausing and corking");
  /* make sure the commit method stops writing */
  pbuf->paused = TRUE;
  res = gst_pulsering_set_corked (pbuf, TRUE, TRUE);
  if (pbuf->in_commit) {
    /* we are waiting in a commit, signal */
    GST_DEBUG_OBJECT (psink, "signal commit");
    pa_threaded_mainloop_signal (mainloop, 0);
  }
  pa_threaded_mainloop_unlock (mainloop);

  return res;
}

/* called from pulse with the mainloop lock */
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

  g_return_if_fail (pulsesink->defer_pending);
  pulsesink->defer_pending--;
  pa_threaded_mainloop_signal (mainloop, 0);
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

  pa_threaded_mainloop_lock (mainloop);

  pbuf->paused = TRUE;
  res = gst_pulsering_set_corked (pbuf, TRUE, TRUE);

  /* Inform anyone waiting in _commit() call that it shall wakeup */
  if (pbuf->in_commit) {
    GST_DEBUG_OBJECT (psink, "signal commit thread");
    pa_threaded_mainloop_signal (mainloop, 0);
  }
#ifdef HAVE_PULSE_1_0
  if (g_atomic_int_get (&psink->format_lost)) {
    /* Don't try to flush, the stream's probably gone by now */
    res = TRUE;
    goto cleanup;
  }
#endif

  /* then try to flush, it's not fatal when this fails */
  GST_DEBUG_OBJECT (psink, "flushing");
  if ((o = pa_stream_flush (pbuf->stream, gst_pulsering_success_cb, pbuf))) {
    while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
      GST_DEBUG_OBJECT (psink, "wait for completion");
      pa_threaded_mainloop_wait (mainloop);
      if (gst_pulsering_is_dead (psink, pbuf, TRUE))
        goto server_dead;
    }
    GST_DEBUG_OBJECT (psink, "flush completed");
  }
  res = TRUE;

cleanup:
  if (o) {
    pa_operation_cancel (o);
    pa_operation_unref (o);
  }

  GST_DEBUG_OBJECT (psink, "scheduling stream status");
  psink->defer_pending++;
  pa_mainloop_api_once (pa_threaded_mainloop_get_api (mainloop),
      mainloop_leave_defer_cb, psink);

  pa_threaded_mainloop_unlock (mainloop);

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

  pa_threaded_mainloop_lock (mainloop);

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

#ifdef HAVE_PULSE_1_0
  if (g_atomic_int_get (&psink->format_lost)) {
    /* Sink format changed, drop the data and hope upstream renegotiates */
    goto fake_done;
  }
#endif

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

    if (offset != pbuf->m_lastoffset)
      GST_LOG_OBJECT (psink, "discontinuity, offset is %" G_GINT64_FORMAT ", "
          "last offset was %" G_GINT64_FORMAT, offset, pbuf->m_lastoffset);

    towrite = out_samples * bps;

    /* Wait for at least segsize bytes to become available */
    if (towrite > buf->spec.segsize)
      towrite = buf->spec.segsize;

    if ((pbuf->m_writable < towrite) || (offset != pbuf->m_lastoffset)) {
      /* if no room left or discontinuity in offset,
         we need to flush data and get a new buffer */

      /* flush the buffer if possible */
      if ((pbuf->m_data != NULL) && (pbuf->m_towrite > 0)) {

        GST_LOG_OBJECT (psink,
            "flushing %u samples at offset %" G_GINT64_FORMAT,
            (guint) pbuf->m_towrite / bps, pbuf->m_offset);

        if (pa_stream_write (pbuf->stream, (uint8_t *) pbuf->m_data,
                pbuf->m_towrite, NULL, pbuf->m_offset, PA_SEEK_ABSOLUTE) < 0) {
          goto write_failed;
        }
      }
      pbuf->m_towrite = 0;
      pbuf->m_offset = offset;  /* keep track of current offset */

      /* get a buffer to write in for now on */
      for (;;) {
        pbuf->m_writable = pa_stream_writable_size (pbuf->stream);

#ifdef HAVE_PULSE_1_0
        if (g_atomic_int_get (&psink->format_lost)) {
          /* Sink format changed, give up and hope upstream renegotiates */
          goto fake_done;
        }
#endif

        if (pbuf->m_writable == (size_t) - 1)
          goto writable_size_failed;

        pbuf->m_writable /= bps;
        pbuf->m_writable *= bps;        /* handle only complete samples */

        if (pbuf->m_writable >= towrite)
          break;

        /* see if we need to uncork because we have no free space */
        if (pbuf->corked) {
          if (!gst_pulsering_set_corked (pbuf, FALSE, FALSE))
            goto uncork_failed;
        }

        /* we can't write segsize bytes, wait a bit */
        GST_LOG_OBJECT (psink, "waiting for free space");
        pa_threaded_mainloop_wait (mainloop);

        if (pbuf->paused)
          goto was_paused;
      }

      /* Recalculate what we can write in the next chunk */
      towrite = out_samples * bps;
      if (pbuf->m_writable > towrite)
        pbuf->m_writable = towrite;

      GST_LOG_OBJECT (psink, "requesting %" G_GSIZE_FORMAT " bytes of "
          "shared memory", pbuf->m_writable);

      if (pa_stream_begin_write (pbuf->stream, &pbuf->m_data,
              &pbuf->m_writable) < 0) {
        GST_LOG_OBJECT (psink, "pa_stream_begin_write() failed");
        goto writable_size_failed;
      }

      GST_LOG_OBJECT (psink, "got %" G_GSIZE_FORMAT " bytes of shared memory",
          pbuf->m_writable);

    }

    if (towrite > pbuf->m_writable)
      towrite = pbuf->m_writable;
    avail = towrite / bps;

    GST_LOG_OBJECT (psink, "writing %u samples at offset %" G_GUINT64_FORMAT,
        (guint) avail, offset);

#ifdef HAVE_PULSE_1_0
    /* No trick modes for passthrough streams */
    if (G_UNLIKELY (inr != outr || reverse)) {
      GST_WARNING_OBJECT (psink, "Passthrough stream can't run in trick mode");
      goto unlock_and_fail;
    }
#endif

    if (G_LIKELY (inr == outr && !reverse)) {
      /* no rate conversion, simply write out the samples */
      /* copy the data into internal buffer */

      memcpy ((guint8 *) pbuf->m_data + pbuf->m_towrite, data, towrite);
      pbuf->m_towrite += towrite;
      pbuf->m_writable -= towrite;

      data += towrite;
      in_samples -= avail;
      out_samples -= avail;
    } else {
      guint8 *dest, *d, *d_end;

      /* write into the PulseAudio shm buffer */
      dest = d = (guint8 *) pbuf->m_data + pbuf->m_towrite;
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
      pbuf->m_towrite += towrite;
      pbuf->m_writable -= towrite;

      avail = towrite / bps;
    }

    /* flush the buffer if it's full */
    if ((pbuf->m_data != NULL) && (pbuf->m_towrite > 0)
        && (pbuf->m_writable == 0)) {
      GST_LOG_OBJECT (psink, "flushing %u samples at offset %" G_GINT64_FORMAT,
          (guint) pbuf->m_towrite / bps, pbuf->m_offset);

      if (pa_stream_write (pbuf->stream, (uint8_t *) pbuf->m_data,
              pbuf->m_towrite, NULL, pbuf->m_offset, PA_SEEK_ABSOLUTE) < 0) {
        goto write_failed;
      }
      pbuf->m_towrite = 0;
      pbuf->m_offset = offset + towrite;        /* keep track of current offset */
    }

    *sample += avail;
    offset += avail * bps;
    pbuf->m_lastoffset = offset;

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

#ifdef HAVE_PULSE_1_0
fake_done:
#endif
  /* we consumed all samples here */
  data = data_end + bps;

  pbuf->in_commit = FALSE;
  pa_threaded_mainloop_unlock (mainloop);

done:
  result = inr - ((data_end - data) / bps);
  GST_LOG_OBJECT (psink, "wrote %d samples", result);

  return result;

  /* ERRORS */
unlock_and_fail:
  {
    pbuf->in_commit = FALSE;
    GST_LOG_OBJECT (psink, "we are reset");
    pa_threaded_mainloop_unlock (mainloop);
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
    pa_threaded_mainloop_unlock (mainloop);
    goto done;
  }
was_paused:
  {
    pbuf->in_commit = FALSE;
    GST_LOG_OBJECT (psink, "we are paused");
    pa_threaded_mainloop_unlock (mainloop);
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

/* write pending local samples, must be called with the mainloop lock */
static void
gst_pulsering_flush (GstPulseRingBuffer * pbuf)
{
  GstPulseSink *psink;

  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));
  GST_DEBUG_OBJECT (psink, "entering flush");

  /* flush the buffer if possible */
  if (pbuf->stream && (pbuf->m_data != NULL) && (pbuf->m_towrite > 0)) {
#ifndef GST_DISABLE_GST_DEBUG
    gint bps;

    bps = (GST_RING_BUFFER_CAST (pbuf))->spec.bytes_per_sample;
    GST_LOG_OBJECT (psink,
        "flushing %u samples at offset %" G_GINT64_FORMAT,
        (guint) pbuf->m_towrite / bps, pbuf->m_offset);
#endif

    if (pa_stream_write (pbuf->stream, (uint8_t *) pbuf->m_data,
            pbuf->m_towrite, NULL, pbuf->m_offset, PA_SEEK_ABSOLUTE) < 0) {
      goto write_failed;
    }

    pbuf->m_towrite = 0;
    pbuf->m_offset += pbuf->m_towrite;  /* keep track of current offset */
  }

done:
  return;

  /* ERRORS */
write_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("pa_stream_write() failed: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto done;
  }
}

static void gst_pulsesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pulsesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_pulsesink_finalize (GObject * object);

static gboolean gst_pulsesink_event (GstBaseSink * sink, GstEvent * event);

static GstStateChangeReturn gst_pulsesink_change_state (GstElement * element,
    GstStateChange transition);

static void gst_pulsesink_init_interfaces (GType type);

GST_IMPLEMENT_PULSEPROBE_METHODS (GstPulseSink, gst_pulsesink);

#define _do_init(type) \
  gst_pulsesink_init_contexts (); \
  gst_pulsesink_init_interfaces (type);

GST_BOILERPLATE_FULL (GstPulseSink, gst_pulsesink, GstBaseAudioSink,
    GST_TYPE_BASE_AUDIO_SINK, _do_init);

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
  static const GInterfaceInfo svol_iface_info = {
    NULL, NULL, NULL
  };

  g_type_add_interface_static (type, GST_TYPE_STREAM_VOLUME, &svol_iface_info);
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
      GST_STATIC_CAPS (PULSE_SINK_TEMPLATE_CAPS));

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

static GstBuffer *
gst_pulsesink_payload (GstBaseAudioSink * sink, GstBuffer * buf)
{
  switch (sink->ringbuffer->spec.type) {
    case GST_BUFTYPE_AC3:
    case GST_BUFTYPE_EAC3:
    case GST_BUFTYPE_DTS:
    case GST_BUFTYPE_MPEG:
    {
      /* FIXME: alloc memory from PA if possible */
      gint framesize = gst_audio_iec61937_frame_size (&sink->ringbuffer->spec);
      GstBuffer *out;

      if (framesize <= 0)
        return NULL;

      out = gst_buffer_new_and_alloc (framesize);

      if (!gst_audio_iec61937_payload (GST_BUFFER_DATA (buf),
              GST_BUFFER_SIZE (buf), GST_BUFFER_DATA (out),
              GST_BUFFER_SIZE (out), &sink->ringbuffer->spec)) {
        gst_buffer_unref (out);
        return NULL;
      }

      gst_buffer_copy_metadata (out, buf, GST_BUFFER_COPY_ALL);
      return out;
    }

    default:
      return gst_buffer_ref (buf);
  }
}

static void
gst_pulsesink_class_init (GstPulseSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);
  GstBaseSinkClass *bc;
  GstBaseAudioSinkClass *gstaudiosink_class = GST_BASE_AUDIO_SINK_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_pulsesink_finalize;
  gobject_class->set_property = gst_pulsesink_set_property;
  gobject_class->get_property = gst_pulsesink_get_property;

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_pulsesink_event);

  /* restore the original basesink pull methods */
  bc = g_type_class_peek (GST_TYPE_BASE_SINK);
  gstbasesink_class->activate_pull = GST_DEBUG_FUNCPTR (bc->activate_pull);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_pulsesink_change_state);

  gstaudiosink_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_pulsesink_create_ringbuffer);
  gstaudiosink_class->payload = GST_DEBUG_FUNCPTR (gst_pulsesink_payload);

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

  /**
   * GstPulseSink:client
   *
   * The PulseAudio client name to use.
   *
   * Since: 0.10.25
   */
  g_object_class_install_property (gobject_class,
      PROP_CLIENT,
      g_param_spec_string ("client", "Client",
          "The PulseAudio client name to use", gst_pulse_client_name (),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  /**
   * GstPulseSink:stream-properties
   *
   * List of pulseaudio stream properties. A list of defined properties can be
   * found in the <ulink url="http://0pointer.de/lennart/projects/pulseaudio/doxygen/proplist_8h.html">pulseaudio api docs</ulink>.
   *
   * Below is an example for registering as a music application to pulseaudio.
   * |[
   * GstStructure *props;
   *
   * props = gst_structure_from_string ("props,media.role=music", NULL);
   * g_object_set (pulse, "stream-properties", props, NULL);
   * gst_structure_free
   * ]|
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (gobject_class,
      PROP_STREAM_PROPERTIES,
      g_param_spec_boxed ("stream-properties", "stream properties",
          "list of pulseaudio stream properties",
          GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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

#ifdef HAVE_PULSE_1_0
  if (g_atomic_int_get (&psink->format_lost)) {
    /* Stream was lost in a format change, it'll get set up again once
     * upstream renegotiates */
    return psink->format_lost_time;
  }
#endif

  pa_threaded_mainloop_lock (mainloop);
  if (gst_pulsering_is_dead (psink, pbuf, TRUE))
    goto server_dead;

  /* if we don't have enough data to get a timestamp, just return NONE, which
   * will return the last reported time */
  if (pa_stream_get_time (pbuf->stream, &time) < 0) {
    GST_DEBUG_OBJECT (psink, "could not get time");
    time = GST_CLOCK_TIME_NONE;
  } else
    time *= 1000;
  pa_threaded_mainloop_unlock (mainloop);

  GST_LOG_OBJECT (psink, "current time is %" GST_TIME_FORMAT,
      GST_TIME_ARGS (time));

  return time;

  /* ERRORS */
server_dead:
  {
    GST_DEBUG_OBJECT (psink, "the server is dead");
    pa_threaded_mainloop_unlock (mainloop);

    return GST_CLOCK_TIME_NONE;
  }
}

static void
gst_pulsesink_sink_info_cb (pa_context * c, const pa_sink_info * i, int eol,
    void *userdata)
{
  GstPulseRingBuffer *pbuf;
  GstPulseSink *psink;
#ifdef HAVE_PULSE_1_0
  GList *l;
  guint8 j;
#endif

  pbuf = GST_PULSERING_BUFFER_CAST (userdata);
  psink = GST_PULSESINK_CAST (GST_OBJECT_PARENT (pbuf));

  if (!i)
    goto done;

  g_free (psink->device_description);
  psink->device_description = g_strdup (i->description);

#ifdef HAVE_PULSE_1_0
  g_mutex_lock (psink->sink_formats_lock);

  for (l = g_list_first (psink->sink_formats); l; l = g_list_next (l))
    pa_format_info_free ((pa_format_info *) l->data);

  g_list_free (psink->sink_formats);
  psink->sink_formats = NULL;

  for (j = 0; j < i->n_formats; j++)
    psink->sink_formats = g_list_prepend (psink->sink_formats,
        pa_format_info_copy (i->formats[j]));

  g_mutex_unlock (psink->sink_formats_lock);
#endif

done:
  pa_threaded_mainloop_signal (mainloop, 0);
}

#ifdef HAVE_PULSE_1_0
/* NOTE: If you're making changes here, see if pulseaudiosink acceptcaps also
 * needs to be changed accordingly. */
static gboolean
gst_pulsesink_pad_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstPulseSink *psink = GST_PULSESINK (gst_pad_get_parent_element (pad));
  GstPulseRingBuffer *pbuf = GST_PULSERING_BUFFER_CAST (GST_BASE_AUDIO_SINK
      (psink)->ringbuffer);
  GstCaps *pad_caps;
  GstStructure *st;
  gboolean ret = FALSE;

  GstRingBufferSpec spec = { 0 };
  pa_stream *stream = NULL;
  pa_operation *o = NULL;
  pa_channel_map channel_map;
  pa_stream_flags_t flags;
  pa_format_info *format = NULL, *formats[1];
  guint channels;

  pad_caps = gst_pad_get_caps_reffed (pad);
  if (pad_caps) {
    ret = gst_caps_can_intersect (pad_caps, caps);
    gst_caps_unref (pad_caps);
  }

  /* Either template caps didn't match, or we're still in NULL state */
  if (!ret || !pbuf->context)
    goto done;

  /* If we've not got fixed caps, creating a stream might fail, so let's just
   * return from here with default acceptcaps behaviour */
  if (!gst_caps_is_fixed (caps))
    goto done;

  ret = FALSE;

  pa_threaded_mainloop_lock (mainloop);

  spec.latency_time = GST_BASE_AUDIO_SINK (psink)->latency_time;
  if (!gst_ring_buffer_parse_caps (&spec, caps))
    goto out;

  if (!gst_pulse_fill_format_info (&spec, &format, &channels))
    goto out;

  /* Make sure input is framed (one frame per buffer) and can be payloaded */
  if (!pa_format_info_is_pcm (format)) {
    gboolean framed = FALSE, parsed = FALSE;
    st = gst_caps_get_structure (caps, 0);

    gst_structure_get_boolean (st, "framed", &framed);
    gst_structure_get_boolean (st, "parsed", &parsed);
    if ((!framed && !parsed) || gst_audio_iec61937_frame_size (&spec) <= 0)
      goto out;
  }

  /* initialize the channel map */
  if (pa_format_info_is_pcm (format) &&
      gst_pulse_gst_to_channel_map (&channel_map, &spec))
    pa_format_info_set_channel_map (format, &channel_map);

  if (pbuf->stream) {
    /* We're already in PAUSED or above, so just reuse this stream to query
     * sink formats and use those. */
    GList *i;

    if (!(o = pa_context_get_sink_info_by_name (pbuf->context, psink->device,
                gst_pulsesink_sink_info_cb, pbuf)))
      goto info_failed;

    while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
      pa_threaded_mainloop_wait (mainloop);
      if (gst_pulsering_is_dead (psink, pbuf, TRUE))
        goto out;
    }

    g_mutex_lock (psink->sink_formats_lock);
    for (i = g_list_first (psink->sink_formats); i; i = g_list_next (i)) {
      if (pa_format_info_is_compatible ((pa_format_info *) i->data, format)) {
        ret = TRUE;
        break;
      }
    }
    g_mutex_unlock (psink->sink_formats_lock);
  } else {
    /* We're in READY, let's connect a stream to see if the format is
     * accpeted by whatever sink we're routed to */
    formats[0] = format;

    if (!(stream = pa_stream_new_extended (pbuf->context, "pulsesink probe",
                formats, 1, psink->proplist)))
      goto out;

    /* construct the flags */
    flags = PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE |
        PA_STREAM_ADJUST_LATENCY | PA_STREAM_START_CORKED;

    pa_stream_set_state_callback (stream, gst_pulsering_stream_state_cb, pbuf);

    if (pa_stream_connect_playback (stream, psink->device, NULL, flags, NULL,
            NULL) < 0)
      goto out;

    ret = gst_pulsering_wait_for_stream_ready (psink, stream);
  }

out:
  if (format)
    pa_format_info_free (format);

  if (o)
    pa_operation_unref (o);

  if (stream) {
    pa_stream_set_state_callback (stream, NULL, NULL);
    pa_stream_disconnect (stream);
    pa_stream_unref (stream);
  }

  pa_threaded_mainloop_unlock (mainloop);

done:
  gst_object_unref (psink);
  return ret;

info_failed:
  {
    GST_ELEMENT_ERROR (psink, RESOURCE, FAILED,
        ("pa_context_get_sink_input_info() failed: %s",
            pa_strerror (pa_context_errno (pbuf->context))), (NULL));
    goto out;
  }
}
#endif

static void
gst_pulsesink_init (GstPulseSink * pulsesink, GstPulseSinkClass * klass)
{
  pulsesink->server = NULL;
  pulsesink->device = NULL;
  pulsesink->device_description = NULL;
  pulsesink->client_name = gst_pulse_client_name ();

#ifdef HAVE_PULSE_1_0
  pulsesink->sink_formats_lock = g_mutex_new ();
  pulsesink->sink_formats = NULL;
#endif

  pulsesink->volume = DEFAULT_VOLUME;
  pulsesink->volume_set = FALSE;

  pulsesink->mute = DEFAULT_MUTE;
  pulsesink->mute_set = FALSE;

  pulsesink->notify = 0;

#ifdef HAVE_PULSE_1_0
  g_atomic_int_set (&pulsesink->format_lost, FALSE);
  pulsesink->format_lost_time = GST_CLOCK_TIME_NONE;
#endif

  pulsesink->properties = NULL;
  pulsesink->proplist = NULL;

  /* override with a custom clock */
  if (GST_BASE_AUDIO_SINK (pulsesink)->provided_clock)
    gst_object_unref (GST_BASE_AUDIO_SINK (pulsesink)->provided_clock);

  GST_BASE_AUDIO_SINK (pulsesink)->provided_clock =
      gst_audio_clock_new ("GstPulseSinkClock",
      (GstAudioClockGetTimeFunc) gst_pulsesink_get_time, pulsesink);

#ifdef HAVE_PULSE_1_0
  gst_pad_set_acceptcaps_function (GST_BASE_SINK (pulsesink)->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pulsesink_pad_acceptcaps));
#endif

  /* TRUE for sinks, FALSE for sources */
  pulsesink->probe = gst_pulseprobe_new (G_OBJECT (pulsesink),
      G_OBJECT_GET_CLASS (pulsesink), PROP_DEVICE, pulsesink->device,
      TRUE, FALSE);
}

static void
gst_pulsesink_finalize (GObject * object)
{
  GstPulseSink *pulsesink = GST_PULSESINK_CAST (object);
#ifdef HAVE_PULSE_1_0
  GList *i;
#endif

  g_free (pulsesink->server);
  g_free (pulsesink->device);
  g_free (pulsesink->device_description);
  g_free (pulsesink->client_name);

#ifdef HAVE_PULSE_1_0
  for (i = g_list_first (pulsesink->sink_formats); i; i = g_list_next (i))
    pa_format_info_free ((pa_format_info *) i->data);

  g_list_free (pulsesink->sink_formats);
  g_mutex_free (pulsesink->sink_formats_lock);
#endif

  if (pulsesink->properties)
    gst_structure_free (pulsesink->properties);
  if (pulsesink->proplist)
    pa_proplist_free (pulsesink->proplist);

  if (pulsesink->probe) {
    gst_pulseprobe_free (pulsesink->probe);
    pulsesink->probe = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_pulsesink_set_volume (GstPulseSink * psink, gdouble volume)
{
  pa_cvolume v;
  pa_operation *o = NULL;
  GstPulseRingBuffer *pbuf;
  uint32_t idx;

  if (!mainloop)
    goto no_mainloop;

  pa_threaded_mainloop_lock (mainloop);

  GST_DEBUG_OBJECT (psink, "setting volume to %f", volume);

  pbuf = GST_PULSERING_BUFFER_CAST (GST_BASE_AUDIO_SINK (psink)->ringbuffer);
  if (pbuf == NULL || pbuf->stream == NULL)
    goto no_buffer;

  if ((idx = pa_stream_get_index (pbuf->stream)) == PA_INVALID_INDEX)
    goto no_index;

#ifdef HAVE_PULSE_1_0
  if (pa_format_info_is_pcm (pbuf->format))
    gst_pulse_cvolume_from_linear (&v, pbuf->channels, volume);
  else
    /* FIXME: this will eventually be superceded by checks to see if the volume
     * is readable/writable */
    goto unlock;
#else
  gst_pulse_cvolume_from_linear (&v, pbuf->sample_spec.channels, volume);
#endif

  if (!(o = pa_context_set_sink_input_volume (pbuf->context, idx,
              &v, NULL, NULL)))
    goto volume_failed;

  /* We don't really care about the result of this call */
unlock:

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (mainloop);

  return;

  /* ERRORS */
no_mainloop:
  {
    psink->volume = volume;
    psink->volume_set = TRUE;

    GST_DEBUG_OBJECT (psink, "we have no mainloop");
    return;
  }
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

  if (!mainloop)
    goto no_mainloop;

  pa_threaded_mainloop_lock (mainloop);

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

  pa_threaded_mainloop_unlock (mainloop);

  return;

  /* ERRORS */
no_mainloop:
  {
    psink->mute = mute;
    psink->mute_set = TRUE;

    GST_DEBUG_OBJECT (psink, "we have no mainloop");
    return;
  }
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
  pa_threaded_mainloop_signal (mainloop, 0);
}

static gdouble
gst_pulsesink_get_volume (GstPulseSink * psink)
{
  GstPulseRingBuffer *pbuf;
  pa_operation *o = NULL;
  gdouble v = DEFAULT_VOLUME;
  uint32_t idx;

  if (!mainloop)
    goto no_mainloop;

  pa_threaded_mainloop_lock (mainloop);

  pbuf = GST_PULSERING_BUFFER_CAST (GST_BASE_AUDIO_SINK (psink)->ringbuffer);
  if (pbuf == NULL || pbuf->stream == NULL)
    goto no_buffer;

  if ((idx = pa_stream_get_index (pbuf->stream)) == PA_INVALID_INDEX)
    goto no_index;

  if (!(o = pa_context_get_sink_input_info (pbuf->context, idx,
              gst_pulsesink_sink_input_info_cb, pbuf)))
    goto info_failed;

  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
    pa_threaded_mainloop_wait (mainloop);
    if (gst_pulsering_is_dead (psink, pbuf, TRUE))
      goto unlock;
  }

unlock:
  v = psink->volume;

  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (mainloop);

  if (v > MAX_VOLUME) {
    GST_WARNING_OBJECT (psink, "Clipped volume from %f to %f", v, MAX_VOLUME);
    v = MAX_VOLUME;
  }

  return v;

  /* ERRORS */
no_mainloop:
  {
    v = psink->volume;
    GST_DEBUG_OBJECT (psink, "we have no mainloop");
    return v;
  }
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

  if (!mainloop)
    goto no_mainloop;

  pa_threaded_mainloop_lock (mainloop);
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
    pa_threaded_mainloop_wait (mainloop);
    if (gst_pulsering_is_dead (psink, pbuf, TRUE))
      goto unlock;
  }

unlock:
  if (o)
    pa_operation_unref (o);

  pa_threaded_mainloop_unlock (mainloop);

  return mute;

  /* ERRORS */
no_mainloop:
  {
    mute = psink->mute;
    GST_DEBUG_OBJECT (psink, "we have no mainloop");
    return mute;
  }
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

static gchar *
gst_pulsesink_device_description (GstPulseSink * psink)
{
  GstPulseRingBuffer *pbuf;
  pa_operation *o = NULL;
  gchar *t;

  if (!mainloop)
    goto no_mainloop;

  pa_threaded_mainloop_lock (mainloop);
  pbuf = GST_PULSERING_BUFFER_CAST (GST_BASE_AUDIO_SINK (psink)->ringbuffer);
  if (pbuf == NULL)
    goto no_buffer;

  if (!(o = pa_context_get_sink_info_by_name (pbuf->context,
              psink->device, gst_pulsesink_sink_info_cb, pbuf)))
    goto info_failed;

  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
    pa_threaded_mainloop_wait (mainloop);
    if (gst_pulsering_is_dead (psink, pbuf, FALSE))
      goto unlock;
  }

unlock:
  if (o)
    pa_operation_unref (o);

  t = g_strdup (psink->device_description);
  pa_threaded_mainloop_unlock (mainloop);

  return t;

  /* ERRORS */
no_mainloop:
  {
    GST_DEBUG_OBJECT (psink, "we have no mainloop");
    return NULL;
  }
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
    case PROP_VOLUME:
      gst_pulsesink_set_volume (pulsesink, g_value_get_double (value));
      break;
    case PROP_MUTE:
      gst_pulsesink_set_mute (pulsesink, g_value_get_boolean (value));
      break;
    case PROP_CLIENT:
      g_free (pulsesink->client_name);
      if (!g_value_get_string (value)) {
        GST_WARNING_OBJECT (pulsesink,
            "Empty PulseAudio client name not allowed. Resetting to default value");
        pulsesink->client_name = gst_pulse_client_name ();
      } else
        pulsesink->client_name = g_value_dup_string (value);
      break;
    case PROP_STREAM_PROPERTIES:
      if (pulsesink->properties)
        gst_structure_free (pulsesink->properties);
      pulsesink->properties =
          gst_structure_copy (gst_value_get_structure (value));
      if (pulsesink->proplist)
        pa_proplist_free (pulsesink->proplist);
      pulsesink->proplist = gst_pulse_make_proplist (pulsesink->properties);
      break;
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
    case PROP_VOLUME:
      g_value_set_double (value, gst_pulsesink_get_volume (pulsesink));
      break;
    case PROP_MUTE:
      g_value_set_boolean (value, gst_pulsesink_get_mute (pulsesink));
      break;
    case PROP_CLIENT:
      g_value_set_string (value, pulsesink->client_name);
      break;
    case PROP_STREAM_PROPERTIES:
      gst_value_set_structure (value, pulsesink->properties);
      break;
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

  pa_threaded_mainloop_lock (mainloop);

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
  pa_threaded_mainloop_unlock (mainloop);

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

  pa_threaded_mainloop_lock (mainloop);
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

  pa_threaded_mainloop_unlock (mainloop);

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

static void
gst_pulsesink_flush_ringbuffer (GstPulseSink * psink)
{
  GstPulseRingBuffer *pbuf;

  pa_threaded_mainloop_lock (mainloop);

  pbuf = GST_PULSERING_BUFFER_CAST (GST_BASE_AUDIO_SINK (psink)->ringbuffer);

  if (pbuf == NULL || pbuf->stream == NULL)
    goto no_buffer;

  gst_pulsering_flush (pbuf);

  /* Uncork if we haven't already (happens when waiting to get enough data
   * to send out the first time) */
  if (pbuf->corked)
    gst_pulsering_set_corked (pbuf, FALSE, FALSE);

  /* We're not interested if this operation failed or not */
unlock:
  pa_threaded_mainloop_unlock (mainloop);

  return;

  /* ERRORS */
no_buffer:
  {
    GST_DEBUG_OBJECT (psink, "we have no ringbuffer");
    goto unlock;
  }
}

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

      gst_pulsesink_change_props (pulsesink, l);

      break;
    }
    case GST_EVENT_EOS:
      gst_pulsesink_flush_ringbuffer (pulsesink);
      break;
    default:
      ;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (sink, event);
}

static void
gst_pulsesink_release_mainloop (GstPulseSink * psink)
{
  if (!mainloop)
    return;

  pa_threaded_mainloop_lock (mainloop);
  while (psink->defer_pending) {
    GST_DEBUG_OBJECT (psink, "waiting for stream status message emission");
    pa_threaded_mainloop_wait (mainloop);
  }
  pa_threaded_mainloop_unlock (mainloop);

  g_mutex_lock (pa_shared_resource_mutex);
  mainloop_ref_ct--;
  if (!mainloop_ref_ct) {
    GST_INFO_OBJECT (psink, "terminating pa main loop thread");
    pa_threaded_mainloop_stop (mainloop);
    pa_threaded_mainloop_free (mainloop);
    mainloop = NULL;
  }
  g_mutex_unlock (pa_shared_resource_mutex);
}

static GstStateChangeReturn
gst_pulsesink_change_state (GstElement * element, GstStateChange transition)
{
  GstPulseSink *pulsesink = GST_PULSESINK (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      g_mutex_lock (pa_shared_resource_mutex);
      if (!mainloop_ref_ct) {
        GST_INFO_OBJECT (element, "new pa main loop thread");
        if (!(mainloop = pa_threaded_mainloop_new ()))
          goto mainloop_failed;
        mainloop_ref_ct = 1;
        pa_threaded_mainloop_start (mainloop);
        g_mutex_unlock (pa_shared_resource_mutex);
      } else {
        GST_INFO_OBJECT (element, "reusing pa main loop thread");
        mainloop_ref_ct++;
        g_mutex_unlock (pa_shared_resource_mutex);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_element_post_message (element,
          gst_message_new_clock_provide (GST_OBJECT_CAST (element),
              GST_BASE_AUDIO_SINK (pulsesink)->provided_clock, TRUE));
      break;

    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto state_failure;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* format_lost is reset in release() in baseaudiosink */
      gst_element_post_message (element,
          gst_message_new_clock_lost (GST_OBJECT_CAST (element),
              GST_BASE_AUDIO_SINK (pulsesink)->provided_clock));
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_pulsesink_release_mainloop (pulsesink);
      break;
    default:
      break;
  }

  return ret;

  /* ERRORS */
mainloop_failed:
  {
    g_mutex_unlock (pa_shared_resource_mutex);
    GST_ELEMENT_ERROR (pulsesink, RESOURCE, FAILED,
        ("pa_threaded_mainloop_new() failed"), (NULL));
    return GST_STATE_CHANGE_FAILURE;
  }
state_failure:
  {
    if (transition == GST_STATE_CHANGE_NULL_TO_READY) {
      /* Clear the PA mainloop if baseaudiosink failed to open the ring_buffer */
      g_assert (mainloop);
      gst_pulsesink_release_mainloop (pulsesink);
    }
    return ret;
  }
}
