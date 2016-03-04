/*
 * GStreamer
 *
 *  Copyright 2006 Collabora Ltd,
 *  Copyright 2006 Nokia Corporation
 *   @author: Philippe Kalaf <philippe.kalaf@collabora.co.uk>.
 *  Copyright 2012-2015 Pexip
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstnetsim.h"

GST_DEBUG_CATEGORY (netsim_debug);
#define GST_CAT_DEFAULT (netsim_debug)


enum
{
  ARG_0,
  ARG_MIN_DELAY,
  ARG_MAX_DELAY,
  ARG_DELAY_PROBABILITY,
  ARG_DROP_PROBABILITY,
  ARG_DUPLICATE_PROBABILITY,
  ARG_DROP_PACKETS
};

struct _GstNetSimPrivate
{
  GstPad *sinkpad, *srcpad;

  GMutex loop_mutex;
  GCond start_cond;
  GMainLoop *main_loop;
  gboolean running;

  GRand *rand_seed;
  gint min_delay;
  gint max_delay;
  gfloat delay_probability;
  gfloat drop_probability;
  gfloat duplicate_probability;
  guint drop_packets;
};

/* these numbers are nothing but wild guesses and dont reflect any reality */
#define DEFAULT_MIN_DELAY 200
#define DEFAULT_MAX_DELAY 400
#define DEFAULT_DELAY_PROBABILITY 0.0
#define DEFAULT_DROP_PROBABILITY 0.0
#define DEFAULT_DUPLICATE_PROBABILITY 0.0
#define DEFAULT_DROP_PACKETS 0

#define GST_NET_SIM_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_TYPE_NET_SIM, \
                                GstNetSimPrivate))

static GstStaticPadTemplate gst_net_sim_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_net_sim_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

G_DEFINE_TYPE (GstNetSim, gst_net_sim, GST_TYPE_ELEMENT);

static void
gst_net_sim_loop (GstNetSim * netsim)
{
  GMainLoop *loop;

  GST_TRACE_OBJECT (netsim, "TASK: begin");

  g_mutex_lock (&netsim->priv->loop_mutex);
  loop = g_main_loop_ref (netsim->priv->main_loop);
  netsim->priv->running = TRUE;
  GST_TRACE_OBJECT (netsim, "TASK: signal start");
  g_cond_signal (&netsim->priv->start_cond);
  g_mutex_unlock (&netsim->priv->loop_mutex);

  GST_TRACE_OBJECT (netsim, "TASK: run");
  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  g_mutex_lock (&netsim->priv->loop_mutex);
  GST_TRACE_OBJECT (netsim, "TASK: pause");
  gst_pad_pause_task (netsim->priv->srcpad);
  netsim->priv->running = FALSE;
  GST_TRACE_OBJECT (netsim, "TASK: signal end");
  g_cond_signal (&netsim->priv->start_cond);
  g_mutex_unlock (&netsim->priv->loop_mutex);
  GST_TRACE_OBJECT (netsim, "TASK: end");
}

static gboolean
_main_loop_quit_and_remove_source (gpointer user_data)
{
  GMainLoop *main_loop = user_data;
  GST_DEBUG ("MAINLOOP: Quit %p", main_loop);
  g_main_loop_quit (main_loop);
  g_assert (!g_main_loop_is_running (main_loop));
  return FALSE;                 /* Remove source */
}

static gboolean
gst_net_sim_src_activatemode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  GstNetSim *netsim = GST_NET_SIM (parent);
  gboolean result = FALSE;

  (void) pad;
  (void) mode;

  g_mutex_lock (&netsim->priv->loop_mutex);
  if (active) {
    if (netsim->priv->main_loop == NULL) {
      GMainContext *main_context = g_main_context_new ();
      netsim->priv->main_loop = g_main_loop_new (main_context, FALSE);
      g_main_context_unref (main_context);

      GST_TRACE_OBJECT (netsim, "ACT: Starting task on srcpad");
      result = gst_pad_start_task (netsim->priv->srcpad,
          (GstTaskFunction) gst_net_sim_loop, netsim, NULL);

      GST_TRACE_OBJECT (netsim, "ACT: Wait for task to start");
      g_assert (!netsim->priv->running);
      while (!netsim->priv->running)
        g_cond_wait (&netsim->priv->start_cond, &netsim->priv->loop_mutex);
      GST_TRACE_OBJECT (netsim, "ACT: Task on srcpad started");
    }
  } else {
    if (netsim->priv->main_loop != NULL) {
      GSource *source;
      guint id;

      /* Adds an Idle Source which quits the main loop from within.
       * This removes the possibility for run/quit race conditions. */
      GST_TRACE_OBJECT (netsim, "DEACT: Stopping main loop on deactivate");
      source = g_idle_source_new ();
      g_source_set_callback (source, _main_loop_quit_and_remove_source,
          g_main_loop_ref (netsim->priv->main_loop),
          (GDestroyNotify) g_main_loop_unref);
      id = g_source_attach (source,
          g_main_loop_get_context (netsim->priv->main_loop));
      g_source_unref (source);
      g_assert_cmpuint (id, >, 0);
      g_main_loop_unref (netsim->priv->main_loop);
      netsim->priv->main_loop = NULL;

      GST_TRACE_OBJECT (netsim, "DEACT: Wait for mainloop and task to pause");
      g_assert (netsim->priv->running);
      while (netsim->priv->running)
        g_cond_wait (&netsim->priv->start_cond, &netsim->priv->loop_mutex);

      GST_TRACE_OBJECT (netsim, "DEACT: Stopping task on srcpad");
      result = gst_pad_stop_task (netsim->priv->srcpad);
      GST_TRACE_OBJECT (netsim, "DEACT: Mainloop and GstTask stopped");
    }
  }
  g_mutex_unlock (&netsim->priv->loop_mutex);

  return result;
}

typedef struct
{
  GstPad *pad;
  GstBuffer *buf;
} PushBufferCtx;

G_INLINE_FUNC PushBufferCtx *
push_buffer_ctx_new (GstPad * pad, GstBuffer * buf)
{
  PushBufferCtx *ctx = g_slice_new (PushBufferCtx);
  ctx->pad = gst_object_ref (pad);
  ctx->buf = gst_buffer_ref (buf);
  return ctx;
}

G_INLINE_FUNC void
push_buffer_ctx_free (PushBufferCtx * ctx)
{
  if (G_LIKELY (ctx != NULL)) {
    gst_buffer_unref (ctx->buf);
    gst_object_unref (ctx->pad);
    g_slice_free (PushBufferCtx, ctx);
  }
}

static gboolean
push_buffer_ctx_push (PushBufferCtx * ctx)
{
  GST_DEBUG_OBJECT (ctx->pad, "Pushing buffer now");
  gst_pad_push (ctx->pad, gst_buffer_ref (ctx->buf));
  return FALSE;
}

static GstFlowReturn
gst_net_sim_delay_buffer (GstNetSim * netsim, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;

  g_mutex_lock (&netsim->priv->loop_mutex);
  if (netsim->priv->main_loop != NULL && netsim->priv->delay_probability > 0 &&
      g_rand_double (netsim->priv->rand_seed) < netsim->priv->delay_probability)
  {
    PushBufferCtx *ctx = push_buffer_ctx_new (netsim->priv->srcpad, buf);
    gint delay = g_rand_int_range (netsim->priv->rand_seed,
        netsim->priv->min_delay, netsim->priv->max_delay);
    GSource *source = g_timeout_source_new (delay);

    GST_DEBUG_OBJECT (netsim, "Delaying packet by %d", delay);
    g_source_set_callback (source, (GSourceFunc) push_buffer_ctx_push,
        ctx, (GDestroyNotify) push_buffer_ctx_free);
    g_source_attach (source, g_main_loop_get_context (netsim->priv->main_loop));
    g_source_unref (source);
  } else {
    ret = gst_pad_push (netsim->priv->srcpad, gst_buffer_ref (buf));
  }
  g_mutex_unlock (&netsim->priv->loop_mutex);

  return ret;
}

static GstFlowReturn
gst_net_sim_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstNetSim *netsim = GST_NET_SIM (parent);
  GstFlowReturn ret = GST_FLOW_OK;

  (void) pad;

  if (netsim->priv->drop_packets > 0) {
    netsim->priv->drop_packets--;
    GST_DEBUG_OBJECT (netsim, "Dropping packet (%d left)",
        netsim->priv->drop_packets);
  } else if (netsim->priv->drop_probability > 0
      && g_rand_double (netsim->priv->rand_seed) <
      (gdouble) netsim->priv->drop_probability) {
    GST_DEBUG_OBJECT (netsim, "Dropping packet");
  } else if (netsim->priv->duplicate_probability > 0 &&
      g_rand_double (netsim->priv->rand_seed) <
      (gdouble) netsim->priv->duplicate_probability) {
    GST_DEBUG_OBJECT (netsim, "Duplicating packet");
    gst_net_sim_delay_buffer (netsim, buf);
    ret = gst_net_sim_delay_buffer (netsim, buf);
  } else {
    ret = gst_net_sim_delay_buffer (netsim, buf);
  }

  gst_buffer_unref (buf);

  return ret;
}


static void
gst_net_sim_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstNetSim *netsim = GST_NET_SIM (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    case ARG_MIN_DELAY:
      netsim->priv->min_delay = g_value_get_int (value);
      break;
    case ARG_MAX_DELAY:
      netsim->priv->max_delay = g_value_get_int (value);
      break;
    case ARG_DELAY_PROBABILITY:
      netsim->priv->delay_probability = g_value_get_float (value);
      break;
    case ARG_DROP_PROBABILITY:
      netsim->priv->drop_probability = g_value_get_float (value);
      break;
    case ARG_DUPLICATE_PROBABILITY:
      netsim->priv->duplicate_probability = g_value_get_float (value);
      break;
    case ARG_DROP_PACKETS:
      netsim->priv->drop_packets = g_value_get_uint (value);
      break;
  }
}

static void
gst_net_sim_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstNetSim *netsim = GST_NET_SIM (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    case ARG_MIN_DELAY:
      g_value_set_int (value, netsim->priv->min_delay);
      break;
    case ARG_MAX_DELAY:
      g_value_set_int (value, netsim->priv->max_delay);
      break;
    case ARG_DELAY_PROBABILITY:
      g_value_set_float (value, netsim->priv->delay_probability);
      break;
    case ARG_DROP_PROBABILITY:
      g_value_set_float (value, netsim->priv->drop_probability);
      break;
    case ARG_DUPLICATE_PROBABILITY:
      g_value_set_float (value, netsim->priv->duplicate_probability);
      break;
    case ARG_DROP_PACKETS:
      g_value_set_uint (value, netsim->priv->drop_packets);
      break;
  }
}


static void
gst_net_sim_init (GstNetSim * netsim)
{
  netsim->priv = GST_NET_SIM_GET_PRIVATE (netsim);

  netsim->priv->srcpad =
      gst_pad_new_from_static_template (&gst_net_sim_src_template, "src");
  netsim->priv->sinkpad =
      gst_pad_new_from_static_template (&gst_net_sim_sink_template, "sink");

  gst_element_add_pad (GST_ELEMENT (netsim), netsim->priv->srcpad);
  gst_element_add_pad (GST_ELEMENT (netsim), netsim->priv->sinkpad);

  g_mutex_init (&netsim->priv->loop_mutex);
  g_cond_init (&netsim->priv->start_cond);
  netsim->priv->rand_seed = g_rand_new ();
  netsim->priv->main_loop = NULL;

  GST_OBJECT_FLAG_SET (netsim->priv->sinkpad,
      GST_PAD_FLAG_PROXY_CAPS | GST_PAD_FLAG_PROXY_ALLOCATION);

  gst_pad_set_chain_function (netsim->priv->sinkpad,
      GST_DEBUG_FUNCPTR (gst_net_sim_chain));
  gst_pad_set_activatemode_function (netsim->priv->srcpad,
      GST_DEBUG_FUNCPTR (gst_net_sim_src_activatemode));
}

static void
gst_net_sim_finalize (GObject * object)
{
  GstNetSim *netsim = GST_NET_SIM (object);

  g_rand_free (netsim->priv->rand_seed);
  g_mutex_clear (&netsim->priv->loop_mutex);
  g_cond_clear (&netsim->priv->start_cond);

  G_OBJECT_CLASS (gst_net_sim_parent_class)->finalize (object);
}

static void
gst_net_sim_dispose (GObject * object)
{
  GstNetSim *netsim = GST_NET_SIM (object);

  g_assert (netsim->priv->main_loop == NULL);

  G_OBJECT_CLASS (gst_net_sim_parent_class)->dispose (object);
}

static void
gst_net_sim_class_init (GstNetSimClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstNetSimPrivate));

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_net_sim_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_net_sim_sink_template);

  gst_element_class_set_metadata (gstelement_class,
      "Network Simulator",
      "Filter/Network",
      "An element that simulates network jitter, "
      "packet loss and packet duplication",
      "Philippe Kalaf <philippe.kalaf@collabora.co.uk>");

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_net_sim_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_net_sim_finalize);

  gobject_class->set_property = gst_net_sim_set_property;
  gobject_class->get_property = gst_net_sim_get_property;

  g_object_class_install_property (gobject_class, ARG_MIN_DELAY,
      g_param_spec_int ("min-delay", "Minimum delay (ms)",
          "The minimum delay in ms to apply to buffers",
          G_MININT, G_MAXINT, DEFAULT_MIN_DELAY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_MAX_DELAY,
      g_param_spec_int ("max-delay", "Maximum delay (ms)",
          "The maximum delay in ms to apply to buffers",
          G_MININT, G_MAXINT, DEFAULT_MAX_DELAY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_DELAY_PROBABILITY,
      g_param_spec_float ("delay-probability", "Delay Probability",
          "The Probability a buffer is delayed",
          0.0, 1.0, DEFAULT_DELAY_PROBABILITY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_DROP_PROBABILITY,
      g_param_spec_float ("drop-probability", "Drop Probability",
          "The Probability a buffer is dropped",
          0.0, 1.0, DEFAULT_DROP_PROBABILITY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_DUPLICATE_PROBABILITY,
      g_param_spec_float ("duplicate-probability", "Duplicate Probability",
          "The Probability a buffer is duplicated",
          0.0, 1.0, DEFAULT_DUPLICATE_PROBABILITY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_DROP_PACKETS,
      g_param_spec_uint ("drop-packets", "Drop Packets",
          "Drop the next n packets",
          0, G_MAXUINT, DEFAULT_DROP_PACKETS,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (netsim_debug, "netsim", 0, "Network simulator");
}

static gboolean
gst_net_sim_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "netsim",
      GST_RANK_MARGINAL, GST_TYPE_NET_SIM);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    netsim,
    "Network Simulator",
    gst_net_sim_plugin_init, PACKAGE_VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
