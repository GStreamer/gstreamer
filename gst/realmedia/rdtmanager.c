/* GStreamer
 * Copyright (C) <2005,2006> Wim Taymans <wim@fluendo.com>
 *               <2013> Wim Taymans <wim.taymans@gmail.com>
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
/*
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/* Element-Checklist-Version: 5 */

/**
 * SECTION:element-rdtmanager
 * @see_also: GstRtspSrc
 *
 * A simple RTP session manager used internally by rtspsrc.
 */

/* #define HAVE_RTCP */

#include "gstrdtbuffer.h"
#include "rdtmanager.h"
#include "rdtjitterbuffer.h"

#include <gst/glib-compat-private.h>

#include <stdio.h>

GST_DEBUG_CATEGORY_STATIC (rdtmanager_debug);
#define GST_CAT_DEFAULT (rdtmanager_debug)

/* GstRDTManager signals and args */
enum
{
  SIGNAL_REQUEST_PT_MAP,
  SIGNAL_CLEAR_PT_MAP,

  SIGNAL_ON_NEW_SSRC,
  SIGNAL_ON_SSRC_COLLISION,
  SIGNAL_ON_SSRC_VALIDATED,
  SIGNAL_ON_SSRC_ACTIVE,
  SIGNAL_ON_SSRC_SDES,
  SIGNAL_ON_BYE_SSRC,
  SIGNAL_ON_BYE_TIMEOUT,
  SIGNAL_ON_TIMEOUT,
  SIGNAL_ON_NPT_STOP,
  LAST_SIGNAL
};

#define DEFAULT_LATENCY_MS      200

enum
{
  PROP_0,
  PROP_LATENCY
};

static GstStaticPadTemplate gst_rdt_manager_recv_rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtp_sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rdt")
    );

static GstStaticPadTemplate gst_rdt_manager_recv_rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtcp_sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate gst_rdt_manager_recv_rtp_src_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtp_src_%u_%u_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rdt")
    );

static GstStaticPadTemplate gst_rdt_manager_rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_src_%u",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static void gst_rdt_manager_finalize (GObject * object);
static void gst_rdt_manager_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_rdt_manager_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_rdt_manager_query_src (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_rdt_manager_src_activate_mode (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active);

static GstClock *gst_rdt_manager_provide_clock (GstElement * element);
static GstStateChangeReturn gst_rdt_manager_change_state (GstElement * element,
    GstStateChange transition);
static GstPad *gst_rdt_manager_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_rdt_manager_release_pad (GstElement * element, GstPad * pad);

static gboolean gst_rdt_manager_parse_caps (GstRDTManager * rdtmanager,
    GstRDTManagerSession * session, GstCaps * caps);
static gboolean gst_rdt_manager_event_rdt (GstPad * pad, GstObject * parent,
    GstEvent * event);

static GstFlowReturn gst_rdt_manager_chain_rdt (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstFlowReturn gst_rdt_manager_chain_rtcp (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static void gst_rdt_manager_loop (GstPad * pad);

static guint gst_rdt_manager_signals[LAST_SIGNAL] = { 0 };

#define JBUF_LOCK(sess)   (g_mutex_lock (&(sess)->jbuf_lock))

#define JBUF_LOCK_CHECK(sess,label) G_STMT_START {    \
  JBUF_LOCK (sess);                                   \
  if (sess->srcresult != GST_FLOW_OK)                 \
    goto label;                                       \
} G_STMT_END

#define JBUF_UNLOCK(sess) (g_mutex_unlock (&(sess)->jbuf_lock))
#define JBUF_WAIT(sess)   (g_cond_wait (&(sess)->jbuf_cond, &(sess)->jbuf_lock))

#define JBUF_WAIT_CHECK(sess,label) G_STMT_START {    \
  JBUF_WAIT(sess);                                    \
  if (sess->srcresult != GST_FLOW_OK)                 \
    goto label;                                       \
} G_STMT_END

#define JBUF_SIGNAL(sess) (g_cond_signal (&(sess)->jbuf_cond))

/* Manages the receiving end of the packets.
 *
 * There is one such structure for each RTP session (audio/video/...).
 * We get the RTP/RTCP packets and stuff them into the session manager. 
 */
struct _GstRDTManagerSession
{
  /* session id */
  gint id;
  /* the parent bin */
  GstRDTManager *dec;

  gboolean active;
  /* we only support one ssrc and one pt */
  guint32 ssrc;
  guint8 pt;
  gint clock_rate;
  GstCaps *caps;
  gint64 clock_base;

  GstSegment segment;

  /* the last seqnum we pushed out */
  guint32 last_popped_seqnum;
  /* the next expected seqnum */
  guint32 next_seqnum;
  /* last output time */
  GstClockTime last_out_time;

  /* the pads of the session */
  GstPad *recv_rtp_sink;
  GstPad *recv_rtp_src;
  GstPad *recv_rtcp_sink;
  GstPad *rtcp_src;

  GstFlowReturn srcresult;
  gboolean blocked;
  gboolean eos;
  gboolean waiting;
  gboolean discont;
  GstClockID clock_id;

  /* jitterbuffer, lock and cond */
  RDTJitterBuffer *jbuf;
  GMutex jbuf_lock;
  GCond jbuf_cond;

  /* some accounting */
  guint64 num_late;
  guint64 num_duplicates;
};

/* find a session with the given id */
static GstRDTManagerSession *
find_session_by_id (GstRDTManager * rdtmanager, gint id)
{
  GSList *walk;

  for (walk = rdtmanager->sessions; walk; walk = g_slist_next (walk)) {
    GstRDTManagerSession *sess = (GstRDTManagerSession *) walk->data;

    if (sess->id == id)
      return sess;
  }
  return NULL;
}

/* create a session with the given id */
static GstRDTManagerSession *
create_session (GstRDTManager * rdtmanager, gint id)
{
  GstRDTManagerSession *sess;

  sess = g_new0 (GstRDTManagerSession, 1);
  sess->id = id;
  sess->dec = rdtmanager;
  sess->jbuf = rdt_jitter_buffer_new ();
  g_mutex_init (&sess->jbuf_lock);
  g_cond_init (&sess->jbuf_cond);
  rdtmanager->sessions = g_slist_prepend (rdtmanager->sessions, sess);

  return sess;
}

static gboolean
forward_sticky_events (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  GstPad *srcpad = GST_PAD_CAST (user_data);

  gst_pad_push_event (srcpad, gst_event_ref (*event));

  return TRUE;
}

static gboolean
activate_session (GstRDTManager * rdtmanager, GstRDTManagerSession * session,
    guint32 ssrc, guint8 pt)
{
  GstPadTemplate *templ;
  GstElementClass *klass;
  gchar *name;
  GstCaps *caps;
  GValue ret = { 0 };
  GValue args[3] = { {0}
  , {0}
  , {0}
  };

  GST_DEBUG_OBJECT (rdtmanager, "creating stream");

  session->ssrc = ssrc;
  session->pt = pt;

  /* get pt map */
  g_value_init (&args[0], GST_TYPE_ELEMENT);
  g_value_set_object (&args[0], rdtmanager);
  g_value_init (&args[1], G_TYPE_UINT);
  g_value_set_uint (&args[1], session->id);
  g_value_init (&args[2], G_TYPE_UINT);
  g_value_set_uint (&args[2], pt);

  g_value_init (&ret, GST_TYPE_CAPS);
  g_value_set_boxed (&ret, NULL);

  g_signal_emitv (args, gst_rdt_manager_signals[SIGNAL_REQUEST_PT_MAP], 0,
      &ret);

  g_value_unset (&args[0]);
  g_value_unset (&args[1]);
  g_value_unset (&args[2]);
  caps = (GstCaps *) g_value_dup_boxed (&ret);
  g_value_unset (&ret);

  if (caps)
    gst_rdt_manager_parse_caps (rdtmanager, session, caps);

  name = g_strdup_printf ("recv_rtp_src_%u_%u_%u", session->id, ssrc, pt);
  klass = GST_ELEMENT_GET_CLASS (rdtmanager);
  templ = gst_element_class_get_pad_template (klass, "recv_rtp_src_%u_%u_%u");
  session->recv_rtp_src = gst_pad_new_from_template (templ, name);
  g_free (name);

  gst_pad_set_element_private (session->recv_rtp_src, session);
  gst_pad_set_query_function (session->recv_rtp_src, gst_rdt_manager_query_src);
  gst_pad_set_activatemode_function (session->recv_rtp_src,
      gst_rdt_manager_src_activate_mode);

  gst_pad_set_active (session->recv_rtp_src, TRUE);

  gst_pad_sticky_events_foreach (session->recv_rtp_sink, forward_sticky_events,
      session->recv_rtp_src);
  gst_pad_set_caps (session->recv_rtp_src, caps);
  gst_caps_unref (caps);

  gst_element_add_pad (GST_ELEMENT_CAST (rdtmanager), session->recv_rtp_src);

  return TRUE;
}

static void
free_session (GstRDTManagerSession * session)
{
  g_object_unref (session->jbuf);
  g_cond_clear (&session->jbuf_cond);
  g_mutex_clear (&session->jbuf_lock);
  g_free (session);
}

#define gst_rdt_manager_parent_class parent_class
G_DEFINE_TYPE (GstRDTManager, gst_rdt_manager, GST_TYPE_ELEMENT);

/* BOXED:UINT,UINT */
#define g_marshal_value_peek_uint(v)     g_value_get_uint (v)

static void
gst_rdt_manager_marshal_BOXED__UINT_UINT (GClosure * closure,
    GValue * return_value,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint, gpointer marshal_data)
{
  typedef gpointer (*GMarshalFunc_BOXED__UINT_UINT) (gpointer data1,
      guint arg_1, guint arg_2, gpointer data2);
  register GMarshalFunc_BOXED__UINT_UINT callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;
  gpointer v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_BOXED__UINT_UINT) (marshal_data ? marshal_data :
      cc->callback);

  v_return = callback (data1,
      g_marshal_value_peek_uint (param_values + 1),
      g_marshal_value_peek_uint (param_values + 2), data2);

  g_value_take_boxed (return_value, v_return);
}

static void
gst_rdt_manager_marshal_VOID__UINT_UINT (GClosure * closure,
    GValue * return_value,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint, gpointer marshal_data)
{
  typedef void (*GMarshalFunc_VOID__UINT_UINT) (gpointer data1,
      guint arg_1, guint arg_2, gpointer data2);
  register GMarshalFunc_VOID__UINT_UINT callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_VOID__UINT_UINT) (marshal_data ? marshal_data :
      cc->callback);

  callback (data1,
      g_marshal_value_peek_uint (param_values + 1),
      g_marshal_value_peek_uint (param_values + 2), data2);
}

static void
gst_rdt_manager_class_init (GstRDTManagerClass * g_class)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRDTManagerClass *klass;

  klass = (GstRDTManagerClass *) g_class;
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_rdt_manager_finalize;
  gobject_class->set_property = gst_rdt_manager_set_property;
  gobject_class->get_property = gst_rdt_manager_get_property;

  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_uint ("latency", "Buffer latency in ms",
          "Amount of ms to buffer", 0, G_MAXUINT, DEFAULT_LATENCY_MS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRDTManager::request-pt-map:
   * @rdtmanager: the object which received the signal
   * @session: the session
   * @pt: the pt
   *
   * Request the payload type as #GstCaps for @pt in @session.
   */
  gst_rdt_manager_signals[SIGNAL_REQUEST_PT_MAP] =
      g_signal_new ("request-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRDTManagerClass, request_pt_map),
      NULL, NULL, gst_rdt_manager_marshal_BOXED__UINT_UINT, GST_TYPE_CAPS, 2,
      G_TYPE_UINT, G_TYPE_UINT);

  /**
   * GstRDTManager::clear-pt-map:
   * @rtpbin: the object which received the signal
   *
   * Clear all previously cached pt-mapping obtained with
   * GstRDTManager::request-pt-map.
   */
  gst_rdt_manager_signals[SIGNAL_CLEAR_PT_MAP] =
      g_signal_new ("clear-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRDTManagerClass, clear_pt_map),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  /**
   * GstRDTManager::on-bye-ssrc:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @ssrc: the SSRC
   *
   * Notify of an SSRC that became inactive because of a BYE packet.
   */
  gst_rdt_manager_signals[SIGNAL_ON_BYE_SSRC] =
      g_signal_new ("on-bye-ssrc", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRDTManagerClass, on_bye_ssrc),
      NULL, NULL, gst_rdt_manager_marshal_VOID__UINT_UINT, G_TYPE_NONE, 2,
      G_TYPE_UINT, G_TYPE_UINT);
  /**
   * GstRDTManager::on-bye-timeout:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @ssrc: the SSRC
   *
   * Notify of an SSRC that has timed out because of BYE
   */
  gst_rdt_manager_signals[SIGNAL_ON_BYE_TIMEOUT] =
      g_signal_new ("on-bye-timeout", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRDTManagerClass, on_bye_timeout),
      NULL, NULL, gst_rdt_manager_marshal_VOID__UINT_UINT, G_TYPE_NONE, 2,
      G_TYPE_UINT, G_TYPE_UINT);
  /**
   * GstRDTManager::on-timeout:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @ssrc: the SSRC
   *
   * Notify of an SSRC that has timed out
   */
  gst_rdt_manager_signals[SIGNAL_ON_TIMEOUT] =
      g_signal_new ("on-timeout", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRDTManagerClass, on_timeout),
      NULL, NULL, gst_rdt_manager_marshal_VOID__UINT_UINT, G_TYPE_NONE, 2,
      G_TYPE_UINT, G_TYPE_UINT);

  /**
   * GstRDTManager::on-npt-stop:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @ssrc: the SSRC
   *
   * Notify that SSRC sender has sent data up to the configured NPT stop time.
   */
  gst_rdt_manager_signals[SIGNAL_ON_NPT_STOP] =
      g_signal_new ("on-npt-stop", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRDTManagerClass, on_npt_stop),
      NULL, NULL, gst_rdt_manager_marshal_VOID__UINT_UINT, G_TYPE_NONE, 2,
      G_TYPE_UINT, G_TYPE_UINT);


  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_rdt_manager_provide_clock);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rdt_manager_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rdt_manager_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_rdt_manager_release_pad);

  /* sink pads */
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rdt_manager_recv_rtp_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rdt_manager_recv_rtcp_sink_template));
  /* src pads */
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rdt_manager_recv_rtp_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rdt_manager_rtcp_src_template));

  gst_element_class_set_static_metadata (gstelement_class, "RTP Decoder",
      "Codec/Parser/Network",
      "Accepts raw RTP and RTCP packets and sends them forward",
      "Wim Taymans <wim.taymans@gmail.com>");

  GST_DEBUG_CATEGORY_INIT (rdtmanager_debug, "rdtmanager", 0, "RTP decoder");
}

static void
gst_rdt_manager_init (GstRDTManager * rdtmanager)
{
  rdtmanager->provided_clock = gst_system_clock_obtain ();
  rdtmanager->latency = DEFAULT_LATENCY_MS;
  GST_OBJECT_FLAG_SET (rdtmanager, GST_ELEMENT_FLAG_PROVIDE_CLOCK);
}

static void
gst_rdt_manager_finalize (GObject * object)
{
  GstRDTManager *rdtmanager;

  rdtmanager = GST_RDT_MANAGER (object);

  g_slist_foreach (rdtmanager->sessions, (GFunc) free_session, NULL);
  g_slist_free (rdtmanager->sessions);
  g_clear_object (&rdtmanager->provided_clock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rdt_manager_query_src (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstRDTManager *rdtmanager;
  gboolean res;

  rdtmanager = GST_RDT_MANAGER (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime latency;

      latency = rdtmanager->latency * GST_MSECOND;

      /* we pretend to be live with a 3 second latency */
      gst_query_set_latency (query, TRUE, latency, -1);

      GST_DEBUG_OBJECT (rdtmanager, "reporting %" GST_TIME_FORMAT " of latency",
          GST_TIME_ARGS (latency));
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
  return res;
}

static gboolean
gst_rdt_manager_src_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean result;
  GstRDTManager *rdtmanager;
  GstRDTManagerSession *session;

  session = gst_pad_get_element_private (pad);
  rdtmanager = session->dec;

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        /* allow data processing */
        JBUF_LOCK (session);
        GST_DEBUG_OBJECT (rdtmanager, "Enabling pop on queue");
        /* Mark as non flushing */
        session->srcresult = GST_FLOW_OK;
        gst_segment_init (&session->segment, GST_FORMAT_TIME);
        session->last_popped_seqnum = -1;
        session->last_out_time = -1;
        session->next_seqnum = -1;
        session->eos = FALSE;
        JBUF_UNLOCK (session);

        /* start pushing out buffers */
        GST_DEBUG_OBJECT (rdtmanager, "Starting task on srcpad");
        result =
            gst_pad_start_task (pad, (GstTaskFunction) gst_rdt_manager_loop,
            pad, NULL);
      } else {
        /* make sure all data processing stops ASAP */
        JBUF_LOCK (session);
        /* mark ourselves as flushing */
        session->srcresult = GST_FLOW_FLUSHING;
        GST_DEBUG_OBJECT (rdtmanager, "Disabling pop on queue");
        /* this unblocks any waiting pops on the src pad task */
        JBUF_SIGNAL (session);
        /* unlock clock, we just unschedule, the entry will be released by
         * the locking streaming thread. */
        if (session->clock_id)
          gst_clock_id_unschedule (session->clock_id);
        JBUF_UNLOCK (session);

        /* NOTE this will hardlock if the state change is called from the src pad
         * task thread because we will _join() the thread. */
        GST_DEBUG_OBJECT (rdtmanager, "Stopping task on srcpad");
        result = gst_pad_stop_task (pad);
      }
      break;
    default:
      result = FALSE;
      break;
  }
  return result;
}

static GstFlowReturn
gst_rdt_manager_handle_data_packet (GstRDTManagerSession * session,
    GstClockTime timestamp, GstRDTPacket * packet)
{
  GstRDTManager *rdtmanager;
  guint16 seqnum;
  gboolean tail;
  GstFlowReturn res;
  GstBuffer *buffer;

  rdtmanager = session->dec;

  res = GST_FLOW_OK;

  seqnum = 0;
  GST_DEBUG_OBJECT (rdtmanager,
      "Received packet #%d at time %" GST_TIME_FORMAT, seqnum,
      GST_TIME_ARGS (timestamp));

  buffer = gst_rdt_packet_to_buffer (packet);

  JBUF_LOCK_CHECK (session, out_flushing);

  /* insert the packet into the queue now, FIXME, use seqnum */
  if (!rdt_jitter_buffer_insert (session->jbuf, buffer, timestamp,
          session->clock_rate, &tail))
    goto duplicate;

  /* signal addition of new buffer when the _loop is waiting. */
  if (session->waiting)
    JBUF_SIGNAL (session);

finished:
  JBUF_UNLOCK (session);

  return res;

  /* ERRORS */
out_flushing:
  {
    res = session->srcresult;
    GST_DEBUG_OBJECT (rdtmanager, "flushing %s", gst_flow_get_name (res));
    gst_buffer_unref (buffer);
    goto finished;
  }
duplicate:
  {
    GST_WARNING_OBJECT (rdtmanager, "Duplicate packet #%d detected, dropping",
        seqnum);
    session->num_duplicates++;
    gst_buffer_unref (buffer);
    goto finished;
  }
}

static gboolean
gst_rdt_manager_parse_caps (GstRDTManager * rdtmanager,
    GstRDTManagerSession * session, GstCaps * caps)
{
  GstStructure *caps_struct;
  guint val;

  /* first parse the caps */
  caps_struct = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (rdtmanager, "got caps");

  /* we need a clock-rate to convert the rtp timestamps to GStreamer time and to
   * measure the amount of data in the buffer */
  if (!gst_structure_get_int (caps_struct, "clock-rate", &session->clock_rate))
    session->clock_rate = 1000;

  if (session->clock_rate <= 0)
    goto wrong_rate;

  GST_DEBUG_OBJECT (rdtmanager, "got clock-rate %d", session->clock_rate);

  /* gah, clock-base is uint. If we don't have a base, we will use the first
   * buffer timestamp as the base time. This will screw up sync but it's better
   * than nothing. */
  if (gst_structure_get_uint (caps_struct, "clock-base", &val))
    session->clock_base = val;
  else
    session->clock_base = -1;

  GST_DEBUG_OBJECT (rdtmanager, "got clock-base %" G_GINT64_FORMAT,
      session->clock_base);

  /* first expected seqnum */
  if (gst_structure_get_uint (caps_struct, "seqnum-base", &val))
    session->next_seqnum = val;
  else
    session->next_seqnum = -1;

  GST_DEBUG_OBJECT (rdtmanager, "got seqnum-base %d", session->next_seqnum);

  return TRUE;

  /* ERRORS */
wrong_rate:
  {
    GST_DEBUG_OBJECT (rdtmanager, "Invalid clock-rate %d", session->clock_rate);
    return FALSE;
  }
}

static gboolean
gst_rdt_manager_event_rdt (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstRDTManager *rdtmanager;
  GstRDTManagerSession *session;
  gboolean res;

  rdtmanager = GST_RDT_MANAGER (parent);
  /* find session */
  session = gst_pad_get_element_private (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_rdt_manager_parse_caps (rdtmanager, session, caps);
      gst_event_unref (event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }
  return res;
}

static GstFlowReturn
gst_rdt_manager_chain_rdt (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstRDTManager *rdtmanager;
  GstRDTManagerSession *session;
  GstClockTime timestamp;
  GstRDTPacket packet;
  guint32 ssrc;
  guint8 pt;
  gboolean more;

  rdtmanager = GST_RDT_MANAGER (parent);

  GST_DEBUG_OBJECT (rdtmanager, "got RDT packet");

  ssrc = 0;
  pt = 0;

  GST_DEBUG_OBJECT (rdtmanager, "SSRC %08x, PT %d", ssrc, pt);

  /* find session */
  session = gst_pad_get_element_private (pad);

  /* see if we have the pad */
  if (!session->active) {
    activate_session (rdtmanager, session, ssrc, pt);
    session->active = TRUE;
  }

  if (GST_BUFFER_IS_DISCONT (buffer)) {
    GST_DEBUG_OBJECT (rdtmanager, "received discont");
    session->discont = TRUE;
  }

  res = GST_FLOW_OK;

  /* take the timestamp of the buffer. This is the time when the packet was
   * received and is used to calculate jitter and clock skew. We will adjust
   * this timestamp with the smoothed value after processing it in the
   * jitterbuffer. */
  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  /* bring to running time */
  timestamp = gst_segment_to_running_time (&session->segment, GST_FORMAT_TIME,
      timestamp);

  more = gst_rdt_buffer_get_first_packet (buffer, &packet);
  while (more) {
    GstRDTType type;

    type = gst_rdt_packet_get_type (&packet);
    GST_DEBUG_OBJECT (rdtmanager, "Have packet of type %04x", type);

    if (GST_RDT_IS_DATA_TYPE (type)) {
      GST_DEBUG_OBJECT (rdtmanager, "We have a data packet");
      res = gst_rdt_manager_handle_data_packet (session, timestamp, &packet);
    } else {
      switch (type) {
        default:
          GST_DEBUG_OBJECT (rdtmanager, "Ignoring packet");
          break;
      }
    }
    if (res != GST_FLOW_OK)
      break;

    more = gst_rdt_packet_move_to_next (&packet);
  }

  gst_buffer_unref (buffer);

  return res;
}

/* push packets from the queue to the downstream demuxer */
static void
gst_rdt_manager_loop (GstPad * pad)
{
  GstRDTManager *rdtmanager;
  GstRDTManagerSession *session;
  GstBuffer *buffer;
  GstFlowReturn result;

  rdtmanager = GST_RDT_MANAGER (GST_PAD_PARENT (pad));

  session = gst_pad_get_element_private (pad);

  JBUF_LOCK_CHECK (session, flushing);
  GST_DEBUG_OBJECT (rdtmanager, "Peeking item");
  while (TRUE) {
    /* always wait if we are blocked */
    if (!session->blocked) {
      /* if we have a packet, we can exit the loop and grab it */
      if (rdt_jitter_buffer_num_packets (session->jbuf) > 0)
        break;
      /* no packets but we are EOS, do eos logic */
      if (session->eos)
        goto do_eos;
    }
    /* underrun, wait for packets or flushing now */
    session->waiting = TRUE;
    JBUF_WAIT_CHECK (session, flushing);
    session->waiting = FALSE;
  }

  buffer = rdt_jitter_buffer_pop (session->jbuf);

  GST_DEBUG_OBJECT (rdtmanager, "Got item %p", buffer);

  if (session->discont) {
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
    session->discont = FALSE;
  }

  JBUF_UNLOCK (session);

  result = gst_pad_push (session->recv_rtp_src, buffer);
  if (result != GST_FLOW_OK)
    goto pause;

  return;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (rdtmanager, "we are flushing");
    gst_pad_pause_task (session->recv_rtp_src);
    JBUF_UNLOCK (session);
    return;
  }
do_eos:
  {
    /* store result, we are flushing now */
    GST_DEBUG_OBJECT (rdtmanager, "We are EOS, pushing EOS downstream");
    session->srcresult = GST_FLOW_EOS;
    gst_pad_pause_task (session->recv_rtp_src);
    gst_pad_push_event (session->recv_rtp_src, gst_event_new_eos ());
    JBUF_UNLOCK (session);
    return;
  }
pause:
  {
    GST_DEBUG_OBJECT (rdtmanager, "pausing task, reason %s",
        gst_flow_get_name (result));

    JBUF_LOCK (session);
    /* store result */
    session->srcresult = result;
    /* we don't post errors or anything because upstream will do that for us
     * when we pass the return value upstream. */
    gst_pad_pause_task (session->recv_rtp_src);
    JBUF_UNLOCK (session);
    return;
  }
}

static GstFlowReturn
gst_rdt_manager_chain_rtcp (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstRDTManager *src;

#ifdef HAVE_RTCP
  gboolean valid;
  GstRTCPPacket packet;
  gboolean more;
#endif

  src = GST_RDT_MANAGER (parent);

  GST_DEBUG_OBJECT (src, "got rtcp packet");

#ifdef HAVE_RTCP
  valid = gst_rtcp_buffer_validate (buffer);
  if (!valid)
    goto bad_packet;

  /* position on first packet */
  more = gst_rtcp_buffer_get_first_packet (buffer, &packet);
  while (more) {
    switch (gst_rtcp_packet_get_type (&packet)) {
      case GST_RTCP_TYPE_SR:
      {
        guint32 ssrc, rtptime, packet_count, octet_count;
        guint64 ntptime;
        guint count, i;

        gst_rtcp_packet_sr_get_sender_info (&packet, &ssrc, &ntptime, &rtptime,
            &packet_count, &octet_count);

        GST_DEBUG_OBJECT (src,
            "got SR packet: SSRC %08x, NTP %" G_GUINT64_FORMAT
            ", RTP %u, PC %u, OC %u", ssrc, ntptime, rtptime, packet_count,
            octet_count);

        count = gst_rtcp_packet_get_rb_count (&packet);
        for (i = 0; i < count; i++) {
          guint32 ssrc, exthighestseq, jitter, lsr, dlsr;
          guint8 fractionlost;
          gint32 packetslost;

          gst_rtcp_packet_get_rb (&packet, i, &ssrc, &fractionlost,
              &packetslost, &exthighestseq, &jitter, &lsr, &dlsr);

          GST_DEBUG_OBJECT (src, "got RB packet %d: SSRC %08x, FL %u"
              ", PL %u, HS %u, JITTER %u, LSR %u, DLSR %u", ssrc, fractionlost,
              packetslost, exthighestseq, jitter, lsr, dlsr);
        }
        break;
      }
      case GST_RTCP_TYPE_RR:
      {
        guint32 ssrc;
        guint count, i;

        ssrc = gst_rtcp_packet_rr_get_ssrc (&packet);

        GST_DEBUG_OBJECT (src, "got RR packet: SSRC %08x", ssrc);

        count = gst_rtcp_packet_get_rb_count (&packet);
        for (i = 0; i < count; i++) {
          guint32 ssrc, exthighestseq, jitter, lsr, dlsr;
          guint8 fractionlost;
          gint32 packetslost;

          gst_rtcp_packet_get_rb (&packet, i, &ssrc, &fractionlost,
              &packetslost, &exthighestseq, &jitter, &lsr, &dlsr);

          GST_DEBUG_OBJECT (src, "got RB packet %d: SSRC %08x, FL %u"
              ", PL %u, HS %u, JITTER %u, LSR %u, DLSR %u", ssrc, fractionlost,
              packetslost, exthighestseq, jitter, lsr, dlsr);
        }
        break;
      }
      case GST_RTCP_TYPE_SDES:
      {
        guint chunks, i, j;
        gboolean more_chunks, more_items;

        chunks = gst_rtcp_packet_sdes_get_chunk_count (&packet);
        GST_DEBUG_OBJECT (src, "got SDES packet with %d chunks", chunks);

        more_chunks = gst_rtcp_packet_sdes_first_chunk (&packet);
        i = 0;
        while (more_chunks) {
          guint32 ssrc;

          ssrc = gst_rtcp_packet_sdes_get_ssrc (&packet);

          GST_DEBUG_OBJECT (src, "chunk %d, SSRC %08x", i, ssrc);

          more_items = gst_rtcp_packet_sdes_first_item (&packet);
          j = 0;
          while (more_items) {
            GstRTCPSDESType type;
            guint8 len;
            gchar *data;

            gst_rtcp_packet_sdes_get_item (&packet, &type, &len, &data);

            GST_DEBUG_OBJECT (src, "item %d, type %d, len %d, data %s", j,
                type, len, data);

            more_items = gst_rtcp_packet_sdes_next_item (&packet);
            j++;
          }
          more_chunks = gst_rtcp_packet_sdes_next_chunk (&packet);
          i++;
        }
        break;
      }
      case GST_RTCP_TYPE_BYE:
      {
        guint count, i;
        gchar *reason;

        reason = gst_rtcp_packet_bye_get_reason (&packet);
        GST_DEBUG_OBJECT (src, "got BYE packet (reason: %s)",
            GST_STR_NULL (reason));
        g_free (reason);

        count = gst_rtcp_packet_bye_get_ssrc_count (&packet);
        for (i = 0; i < count; i++) {
          guint32 ssrc;


          ssrc = gst_rtcp_packet_bye_get_nth_ssrc (&packet, i);

          GST_DEBUG_OBJECT (src, "SSRC: %08x", ssrc);
        }
        break;
      }
      case GST_RTCP_TYPE_APP:
        GST_DEBUG_OBJECT (src, "got APP packet");
        break;
      default:
        GST_WARNING_OBJECT (src, "got unknown RTCP packet");
        break;
    }
    more = gst_rtcp_packet_move_to_next (&packet);
  }
  gst_buffer_unref (buffer);
  return GST_FLOW_OK;

bad_packet:
  {
    GST_WARNING_OBJECT (src, "got invalid RTCP packet");
    return GST_FLOW_OK;
  }
#else
  return GST_FLOW_OK;
#endif
}

static void
gst_rdt_manager_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRDTManager *src;

  src = GST_RDT_MANAGER (object);

  switch (prop_id) {
    case PROP_LATENCY:
      src->latency = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rdt_manager_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRDTManager *src;

  src = GST_RDT_MANAGER (object);

  switch (prop_id) {
    case PROP_LATENCY:
      g_value_set_uint (value, src->latency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstClock *
gst_rdt_manager_provide_clock (GstElement * element)
{
  GstRDTManager *rdtmanager;

  rdtmanager = GST_RDT_MANAGER (element);

  return GST_CLOCK_CAST (gst_object_ref (rdtmanager->provided_clock));
}

static GstStateChangeReturn
gst_rdt_manager_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;

  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* we're NO_PREROLL when going to PAUSED */
      ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    default:
      break;
  }

  return ret;
}

/* Create a pad for receiving RTP for the session in @name
 */
static GstPad *
create_recv_rtp (GstRDTManager * rdtmanager, GstPadTemplate * templ,
    const gchar * name)
{
  guint sessid;
  GstRDTManagerSession *session;

  /* first get the session number */
  if (name == NULL || sscanf (name, "recv_rtp_sink_%u", &sessid) != 1)
    goto no_name;

  GST_DEBUG_OBJECT (rdtmanager, "finding session %d", sessid);

  /* get or create session */
  session = find_session_by_id (rdtmanager, sessid);
  if (!session) {
    GST_DEBUG_OBJECT (rdtmanager, "creating session %d", sessid);
    /* create session now */
    session = create_session (rdtmanager, sessid);
    if (session == NULL)
      goto create_error;
  }
  /* check if pad was requested */
  if (session->recv_rtp_sink != NULL)
    goto existed;

  GST_DEBUG_OBJECT (rdtmanager, "getting RTP sink pad");

  session->recv_rtp_sink = gst_pad_new_from_template (templ, name);
  gst_pad_set_element_private (session->recv_rtp_sink, session);
  gst_pad_set_event_function (session->recv_rtp_sink,
      gst_rdt_manager_event_rdt);
  gst_pad_set_chain_function (session->recv_rtp_sink,
      gst_rdt_manager_chain_rdt);
  gst_pad_set_active (session->recv_rtp_sink, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rdtmanager), session->recv_rtp_sink);

  return session->recv_rtp_sink;

  /* ERRORS */
no_name:
  {
    g_warning ("rdtmanager: invalid name given");
    return NULL;
  }
create_error:
  {
    /* create_session already warned */
    return NULL;
  }
existed:
  {
    g_warning ("rdtmanager: recv_rtp pad already requested for session %d",
        sessid);
    return NULL;
  }
}

/* Create a pad for receiving RTCP for the session in @name
 */
static GstPad *
create_recv_rtcp (GstRDTManager * rdtmanager, GstPadTemplate * templ,
    const gchar * name)
{
  guint sessid;
  GstRDTManagerSession *session;

  /* first get the session number */
  if (name == NULL || sscanf (name, "recv_rtcp_sink_%u", &sessid) != 1)
    goto no_name;

  GST_DEBUG_OBJECT (rdtmanager, "finding session %d", sessid);

  /* get the session, it must exist or we error */
  session = find_session_by_id (rdtmanager, sessid);
  if (!session)
    goto no_session;

  /* check if pad was requested */
  if (session->recv_rtcp_sink != NULL)
    goto existed;

  GST_DEBUG_OBJECT (rdtmanager, "getting RTCP sink pad");

  session->recv_rtcp_sink = gst_pad_new_from_template (templ, name);
  gst_pad_set_element_private (session->recv_rtp_sink, session);
  gst_pad_set_chain_function (session->recv_rtcp_sink,
      gst_rdt_manager_chain_rtcp);
  gst_pad_set_active (session->recv_rtcp_sink, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rdtmanager), session->recv_rtcp_sink);

  return session->recv_rtcp_sink;

  /* ERRORS */
no_name:
  {
    g_warning ("rdtmanager: invalid name given");
    return NULL;
  }
no_session:
  {
    g_warning ("rdtmanager: no session with id %d", sessid);
    return NULL;
  }
existed:
  {
    g_warning ("rdtmanager: recv_rtcp pad already requested for session %d",
        sessid);
    return NULL;
  }
}

/* Create a pad for sending RTCP for the session in @name
 */
static GstPad *
create_rtcp (GstRDTManager * rdtmanager, GstPadTemplate * templ,
    const gchar * name)
{
  guint sessid;
  GstRDTManagerSession *session;

  /* first get the session number */
  if (name == NULL || sscanf (name, "rtcp_src_%u", &sessid) != 1)
    goto no_name;

  /* get or create session */
  session = find_session_by_id (rdtmanager, sessid);
  if (!session)
    goto no_session;

  /* check if pad was requested */
  if (session->rtcp_src != NULL)
    goto existed;

  session->rtcp_src = gst_pad_new_from_template (templ, name);
  gst_pad_set_active (session->rtcp_src, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rdtmanager), session->rtcp_src);

  return session->rtcp_src;

  /* ERRORS */
no_name:
  {
    g_warning ("rdtmanager: invalid name given");
    return NULL;
  }
no_session:
  {
    g_warning ("rdtmanager: session with id %d does not exist", sessid);
    return NULL;
  }
existed:
  {
    g_warning ("rdtmanager: rtcp_src pad already requested for session %d",
        sessid);
    return NULL;
  }
}

/* 
 */
static GstPad *
gst_rdt_manager_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{
  GstRDTManager *rdtmanager;
  GstElementClass *klass;
  GstPad *result;

  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_RDT_MANAGER (element), NULL);

  rdtmanager = GST_RDT_MANAGER (element);
  klass = GST_ELEMENT_GET_CLASS (element);

  /* figure out the template */
  if (templ == gst_element_class_get_pad_template (klass, "recv_rtp_sink_%u")) {
    result = create_recv_rtp (rdtmanager, templ, name);
  } else if (templ == gst_element_class_get_pad_template (klass,
          "recv_rtcp_sink_%u")) {
    result = create_recv_rtcp (rdtmanager, templ, name);
  } else if (templ == gst_element_class_get_pad_template (klass, "rtcp_src_%u")) {
    result = create_rtcp (rdtmanager, templ, name);
  } else
    goto wrong_template;

  return result;

  /* ERRORS */
wrong_template:
  {
    g_warning ("rdtmanager: this is not our template");
    return NULL;
  }
}

static void
gst_rdt_manager_release_pad (GstElement * element, GstPad * pad)
{
}

gboolean
gst_rdt_manager_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rdtmanager",
      GST_RANK_NONE, GST_TYPE_RDT_MANAGER);
}
