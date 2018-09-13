/* GStreamer
 *
 * Unit tests for webrtcbin
 *
 * Copyright (C) 2017 Matthew Waters <matthew@centricular.com>
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/webrtc/webrtc.h>

#define OPUS_RTP_CAPS(pt) "application/x-rtp,payload=" G_STRINGIFY(pt) ",encoding-name=OPUS,media=audio,clock-rate=48000,ssrc=(uint)3384078950"
#define VP8_RTP_CAPS(pt) "application/x-rtp,payload=" G_STRINGIFY(pt) ",encoding-name=VP8,media=video,clock-rate=90000,ssrc=(uint)3484078950"

typedef enum
{
  STATE_NEW,
  STATE_NEGOTATION_NEEDED,
  STATE_OFFER_CREATED,
  STATE_ANSWER_CREATED,
  STATE_EOS,
  STATE_ERROR,
  STATE_CUSTOM,
} TestState;

/* basic premise of this is that webrtc1 and webrtc2 are attempting to connect
 * to each other in various configurations */
struct test_webrtc;
struct test_webrtc
{
  GList *harnesses;
  GThread *thread;
  GMainLoop *loop;
  GstBus *bus1;
  GstBus *bus2;
  GstElement *webrtc1;
  GstElement *webrtc2;
  GMutex lock;
  GCond cond;
  TestState state;
  guint offerror;
  gpointer user_data;
  GDestroyNotify data_notify;
/* *INDENT-OFF* */
  void      (*on_negotiation_needed)    (struct test_webrtc * t,
                                         GstElement * element,
                                         gpointer user_data);
  gpointer negotiation_data;
  GDestroyNotify negotiation_notify;
  void      (*on_ice_candidate)         (struct test_webrtc * t,
                                         GstElement * element,
                                         guint mlineindex,
                                         gchar * candidate,
                                         GstElement * other,
                                         gpointer user_data);
  gpointer ice_candidate_data;
  GDestroyNotify ice_candidate_notify;
  GstWebRTCSessionDescription * (*on_offer_created)     (struct test_webrtc * t,
                                                         GstElement * element,
                                                         GstPromise * promise,
                                                         gpointer user_data);
  gpointer offer_data;
  GDestroyNotify offer_notify;
  GstWebRTCSessionDescription * (*on_answer_created)    (struct test_webrtc * t,
                                                         GstElement * element,
                                                         GstPromise * promise,
                                                         gpointer user_data);
  gpointer data_channel_data;
  GDestroyNotify data_channel_notify;
  void       (*on_data_channel)                         (struct test_webrtc * t,
                                                         GstElement * element,
                                                         GObject *data_channel,
                                                         gpointer user_data);
  gpointer answer_data;
  GDestroyNotify answer_notify;
  void      (*on_pad_added)             (struct test_webrtc * t,
                                         GstElement * element,
                                         GstPad * pad,
                                         gpointer user_data);
  gpointer pad_added_data;
  GDestroyNotify pad_added_notify;
  void      (*bus_message)              (struct test_webrtc * t,
                                         GstBus * bus,
                                         GstMessage * msg,
                                         gpointer user_data);
  gpointer bus_data;
  GDestroyNotify bus_notify;
/* *INDENT-ON* */
};

static void
_on_answer_received (GstPromise * promise, gpointer user_data)
{
  struct test_webrtc *t = user_data;
  GstElement *offeror = t->offerror == 1 ? t->webrtc1 : t->webrtc2;
  GstElement *answerer = t->offerror == 2 ? t->webrtc1 : t->webrtc2;
  const GstStructure *reply;
  GstWebRTCSessionDescription *answer = NULL;
  gchar *desc;

  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "answer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
  desc = gst_sdp_message_as_text (answer->sdp);
  GST_INFO ("Created Answer: %s", desc);
  g_free (desc);

  g_mutex_lock (&t->lock);
  if (t->on_answer_created) {
    gst_webrtc_session_description_free (answer);
    answer = t->on_answer_created (t, answerer, promise, t->answer_data);
  }
  gst_promise_unref (promise);

  g_signal_emit_by_name (answerer, "set-local-description", answer, NULL);
  g_signal_emit_by_name (offeror, "set-remote-description", answer, NULL);

  t->state = STATE_ANSWER_CREATED;
  g_cond_broadcast (&t->cond);
  g_mutex_unlock (&t->lock);

  gst_webrtc_session_description_free (answer);
}

static void
_on_offer_received (GstPromise * promise, gpointer user_data)
{
  struct test_webrtc *t = user_data;
  GstElement *offeror = t->offerror == 1 ? t->webrtc1 : t->webrtc2;
  GstElement *answerer = t->offerror == 2 ? t->webrtc1 : t->webrtc2;
  const GstStructure *reply;
  GstWebRTCSessionDescription *offer = NULL;
  gchar *desc;

  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "offer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
  desc = gst_sdp_message_as_text (offer->sdp);
  GST_INFO ("Created offer: %s", desc);
  g_free (desc);

  g_mutex_lock (&t->lock);
  if (t->on_offer_created) {
    gst_webrtc_session_description_free (offer);
    offer = t->on_offer_created (t, offeror, promise, t->offer_data);
  }
  gst_promise_unref (promise);

  g_signal_emit_by_name (offeror, "set-local-description", offer, NULL);
  g_signal_emit_by_name (answerer, "set-remote-description", offer, NULL);

  promise = gst_promise_new_with_change_func (_on_answer_received, t, NULL);
  g_signal_emit_by_name (answerer, "create-answer", NULL, promise);

  t->state = STATE_OFFER_CREATED;
  g_cond_broadcast (&t->cond);
  g_mutex_unlock (&t->lock);

  gst_webrtc_session_description_free (offer);
}

static gboolean
_bus_watch (GstBus * bus, GstMessage * msg, struct test_webrtc *t)
{
  g_mutex_lock (&t->lock);
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_STATE_CHANGED:
      if (GST_ELEMENT (msg->src) == t->webrtc1
          || GST_ELEMENT (msg->src) == t->webrtc2) {
        GstState old, new, pending;

        gst_message_parse_state_changed (msg, &old, &new, &pending);

        {
          gchar *dump_name = g_strconcat ("%s-state_changed-",
              GST_OBJECT_NAME (msg->src), gst_element_state_get_name (old), "_",
              gst_element_state_get_name (new), NULL);
          GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (msg->src),
              GST_DEBUG_GRAPH_SHOW_ALL, dump_name);
          g_free (dump_name);
        }
      }
      break;
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *dbg_info = NULL;

      {
        gchar *dump_name;
        dump_name =
            g_strconcat ("%s-error", GST_OBJECT_NAME (t->webrtc1), NULL);
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (t->webrtc1),
            GST_DEBUG_GRAPH_SHOW_ALL, dump_name);
        g_free (dump_name);
        dump_name =
            g_strconcat ("%s-error", GST_OBJECT_NAME (t->webrtc2), NULL);
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (t->webrtc2),
            GST_DEBUG_GRAPH_SHOW_ALL, dump_name);
        g_free (dump_name);
      }

      gst_message_parse_error (msg, &err, &dbg_info);
      GST_WARNING ("ERROR from element %s: %s\n",
          GST_OBJECT_NAME (msg->src), err->message);
      GST_WARNING ("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
      g_error_free (err);
      g_free (dbg_info);
      t->state = STATE_ERROR;
      g_cond_broadcast (&t->cond);
      break;
    }
    case GST_MESSAGE_EOS:{
      {
        gchar *dump_name;
        dump_name = g_strconcat ("%s-eos", GST_OBJECT_NAME (t->webrtc1), NULL);
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (t->webrtc1),
            GST_DEBUG_GRAPH_SHOW_ALL, dump_name);
        g_free (dump_name);
        dump_name = g_strconcat ("%s-eos", GST_OBJECT_NAME (t->webrtc2), NULL);
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (t->webrtc2),
            GST_DEBUG_GRAPH_SHOW_ALL, dump_name);
        g_free (dump_name);
      }
      GST_INFO ("EOS received\n");
      t->state = STATE_EOS;
      g_cond_broadcast (&t->cond);
      break;
    }
    default:
      break;
  }

  if (t->bus_message)
    t->bus_message (t, bus, msg, t->bus_data);
  g_mutex_unlock (&t->lock);

  return TRUE;
}

static void
_on_negotiation_needed (GstElement * webrtc, struct test_webrtc *t)
{
  g_mutex_lock (&t->lock);
  if (t->on_negotiation_needed)
    t->on_negotiation_needed (t, webrtc, t->negotiation_data);
  if (t->state == STATE_NEW)
    t->state = STATE_NEGOTATION_NEEDED;
  g_cond_broadcast (&t->cond);
  g_mutex_unlock (&t->lock);
}

static void
_on_ice_candidate (GstElement * webrtc, guint mlineindex, gchar * candidate,
    struct test_webrtc *t)
{
  GstElement *other;

  g_mutex_lock (&t->lock);
  other = webrtc == t->webrtc1 ? t->webrtc2 : t->webrtc1;

  if (t->on_ice_candidate)
    t->on_ice_candidate (t, webrtc, mlineindex, candidate, other,
        t->ice_candidate_data);

  g_signal_emit_by_name (other, "add-ice-candidate", mlineindex, candidate);
  g_mutex_unlock (&t->lock);
}

static void
_on_pad_added (GstElement * webrtc, GstPad * new_pad, struct test_webrtc *t)
{
  g_mutex_lock (&t->lock);
  if (t->on_pad_added)
    t->on_pad_added (t, webrtc, new_pad, t->pad_added_data);
  g_mutex_unlock (&t->lock);
}

static void
_on_data_channel (GstElement * webrtc, GObject * data_channel,
    struct test_webrtc *t)
{
  g_mutex_lock (&t->lock);
  if (t->on_data_channel)
    t->on_data_channel (t, webrtc, data_channel, t->data_channel_data);
  g_mutex_unlock (&t->lock);
}

static void
_pad_added_not_reached (struct test_webrtc *t, GstElement * element,
    GstPad * pad, gpointer user_data)
{
  g_assert_not_reached ();
}

static void
_ice_candidate_not_reached (struct test_webrtc *t, GstElement * element,
    guint mlineindex, gchar * candidate, GstElement * other, gpointer user_data)
{
  g_assert_not_reached ();
}

static void
_negotiation_not_reached (struct test_webrtc *t, GstElement * element,
    gpointer user_data)
{
  g_assert_not_reached ();
}

static void
_bus_no_errors (struct test_webrtc *t, GstBus * bus, GstMessage * msg,
    gpointer user_data)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      g_assert_not_reached ();
      break;
    }
    default:
      break;
  }
}

static GstWebRTCSessionDescription *
_offer_answer_not_reached (struct test_webrtc *t, GstElement * element,
    GstPromise * promise, gpointer user_data)
{
  g_assert_not_reached ();
}

static void
_on_data_channel_not_reached (struct test_webrtc *t, GstElement * element,
    GObject * data_channel, gpointer user_data)
{
  g_assert_not_reached ();
}

static void
_broadcast (struct test_webrtc *t)
{
  g_mutex_lock (&t->lock);
  g_cond_broadcast (&t->cond);
  g_mutex_unlock (&t->lock);
}

static gboolean
_unlock_create_thread (GMutex * lock)
{
  g_mutex_unlock (lock);
  return G_SOURCE_REMOVE;
}

static gpointer
_bus_thread (struct test_webrtc *t)
{
  g_mutex_lock (&t->lock);
  t->loop = g_main_loop_new (NULL, FALSE);
  g_idle_add ((GSourceFunc) _unlock_create_thread, &t->lock);
  g_cond_broadcast (&t->cond);

  g_main_loop_run (t->loop);

  g_mutex_lock (&t->lock);
  g_main_loop_unref (t->loop);
  t->loop = NULL;
  g_cond_broadcast (&t->cond);
  g_mutex_unlock (&t->lock);

  return NULL;
}

static void
element_added_disable_sync (GstBin * bin, GstBin * sub_bin,
    GstElement * element, gpointer user_data)
{
  GObjectClass *class = G_OBJECT_GET_CLASS (element);
  if (g_object_class_find_property (class, "async"))
    g_object_set (element, "async", FALSE, NULL);
  if (g_object_class_find_property (class, "sync"))
    g_object_set (element, "sync", FALSE, NULL);
}

static struct test_webrtc *
test_webrtc_new (void)
{
  struct test_webrtc *ret = g_new0 (struct test_webrtc, 1);

  ret->on_negotiation_needed = _negotiation_not_reached;
  ret->on_ice_candidate = _ice_candidate_not_reached;
  ret->on_pad_added = _pad_added_not_reached;
  ret->on_offer_created = _offer_answer_not_reached;
  ret->on_answer_created = _offer_answer_not_reached;
  ret->on_data_channel = _on_data_channel_not_reached;
  ret->bus_message = _bus_no_errors;

  g_mutex_init (&ret->lock);
  g_cond_init (&ret->cond);

  ret->bus1 = gst_bus_new ();
  ret->bus2 = gst_bus_new ();
  gst_bus_add_watch (ret->bus1, (GstBusFunc) _bus_watch, ret);
  gst_bus_add_watch (ret->bus2, (GstBusFunc) _bus_watch, ret);
  ret->webrtc1 = gst_element_factory_make ("webrtcbin", NULL);
  ret->webrtc2 = gst_element_factory_make ("webrtcbin", NULL);
  fail_unless (ret->webrtc1 != NULL && ret->webrtc2 != NULL);

  gst_element_set_bus (ret->webrtc1, ret->bus1);
  gst_element_set_bus (ret->webrtc2, ret->bus2);

  g_signal_connect (ret->webrtc1, "deep-element-added",
      G_CALLBACK (element_added_disable_sync), NULL);
  g_signal_connect (ret->webrtc2, "deep-element-added",
      G_CALLBACK (element_added_disable_sync), NULL);
  g_signal_connect (ret->webrtc1, "on-negotiation-needed",
      G_CALLBACK (_on_negotiation_needed), ret);
  g_signal_connect (ret->webrtc2, "on-negotiation-needed",
      G_CALLBACK (_on_negotiation_needed), ret);
  g_signal_connect (ret->webrtc1, "on-ice-candidate",
      G_CALLBACK (_on_ice_candidate), ret);
  g_signal_connect (ret->webrtc2, "on-ice-candidate",
      G_CALLBACK (_on_ice_candidate), ret);
  g_signal_connect (ret->webrtc1, "on-data-channel",
      G_CALLBACK (_on_data_channel), ret);
  g_signal_connect (ret->webrtc2, "on-data-channel",
      G_CALLBACK (_on_data_channel), ret);
  g_signal_connect (ret->webrtc1, "pad-added", G_CALLBACK (_on_pad_added), ret);
  g_signal_connect (ret->webrtc2, "pad-added", G_CALLBACK (_on_pad_added), ret);
  g_signal_connect_swapped (ret->webrtc1, "notify::ice-gathering-state",
      G_CALLBACK (_broadcast), ret);
  g_signal_connect_swapped (ret->webrtc2, "notify::ice-gathering-state",
      G_CALLBACK (_broadcast), ret);
  g_signal_connect_swapped (ret->webrtc1, "notify::ice-connection-state",
      G_CALLBACK (_broadcast), ret);
  g_signal_connect_swapped (ret->webrtc2, "notify::ice-connection-state",
      G_CALLBACK (_broadcast), ret);

  ret->thread = g_thread_new ("test-webrtc", (GThreadFunc) _bus_thread, ret);

  g_mutex_lock (&ret->lock);
  while (!ret->loop)
    g_cond_wait (&ret->cond, &ret->lock);
  g_mutex_unlock (&ret->lock);

  return ret;
}

static void
test_webrtc_free (struct test_webrtc *t)
{
  /* Otherwise while one webrtcbin is being destroyed, the other could
   * generate a signal that calls into the destroyed webrtcbin */
  g_signal_handlers_disconnect_by_data (t->webrtc1, t);
  g_signal_handlers_disconnect_by_data (t->webrtc2, t);

  g_main_loop_quit (t->loop);
  g_mutex_lock (&t->lock);
  while (t->loop)
    g_cond_wait (&t->cond, &t->lock);
  g_mutex_unlock (&t->lock);

  g_thread_join (t->thread);

  gst_bus_remove_watch (t->bus1);
  gst_bus_remove_watch (t->bus2);

  gst_bus_set_flushing (t->bus1, TRUE);
  gst_bus_set_flushing (t->bus2, TRUE);

  gst_object_unref (t->bus1);
  gst_object_unref (t->bus2);

  g_list_free_full (t->harnesses, (GDestroyNotify) gst_harness_teardown);

  if (t->data_notify)
    t->data_notify (t->user_data);
  if (t->negotiation_notify)
    t->negotiation_notify (t->negotiation_data);
  if (t->ice_candidate_notify)
    t->ice_candidate_notify (t->ice_candidate_data);
  if (t->offer_notify)
    t->offer_notify (t->offer_data);
  if (t->answer_notify)
    t->answer_notify (t->answer_data);
  if (t->pad_added_notify)
    t->pad_added_notify (t->pad_added_data);
  if (t->data_channel_notify)
    t->data_channel_notify (t->data_channel_data);

  fail_unless_equals_int (GST_STATE_CHANGE_SUCCESS,
      gst_element_set_state (t->webrtc1, GST_STATE_NULL));
  fail_unless_equals_int (GST_STATE_CHANGE_SUCCESS,
      gst_element_set_state (t->webrtc2, GST_STATE_NULL));

  gst_object_unref (t->webrtc1);
  gst_object_unref (t->webrtc2);

  g_mutex_clear (&t->lock);
  g_cond_clear (&t->cond);

  g_free (t);
}

static void
test_webrtc_create_offer (struct test_webrtc *t, GstElement * webrtc)
{
  GstPromise *promise;

  t->offerror = webrtc == t->webrtc1 ? 1 : 2;
  promise = gst_promise_new_with_change_func (_on_offer_received, t, NULL);
  g_signal_emit_by_name (webrtc, "create-offer", NULL, promise);
}

static void
test_webrtc_wait_for_state_mask (struct test_webrtc *t, TestState state)
{
  g_mutex_lock (&t->lock);
  while (((1 << t->state) & state) == 0) {
    GST_INFO ("test state 0x%x, current 0x%x", state, (1 << t->state));
    g_cond_wait (&t->cond, &t->lock);
  }
  GST_INFO ("have test state 0x%x, current 0x%x", state, 1 << t->state);
  g_mutex_unlock (&t->lock);
}

static void
test_webrtc_wait_for_answer_error_eos (struct test_webrtc *t)
{
  TestState states = 0;
  states |= (1 << STATE_ANSWER_CREATED);
  states |= (1 << STATE_EOS);
  states |= (1 << STATE_ERROR);
  test_webrtc_wait_for_state_mask (t, states);
}

static void
test_webrtc_signal_state_unlocked (struct test_webrtc *t, TestState state)
{
  t->state = state;
  g_cond_broadcast (&t->cond);
}

static void
test_webrtc_signal_state (struct test_webrtc *t, TestState state)
{
  g_mutex_lock (&t->lock);
  test_webrtc_signal_state_unlocked (t, state);
  g_mutex_unlock (&t->lock);
}

#if 0
static void
test_webrtc_wait_for_ice_gathering_complete (struct test_webrtc *t)
{
  GstWebRTCICEGatheringState ice_state1, ice_state2;
  g_mutex_lock (&t->lock);
  g_object_get (t->webrtc1, "ice-gathering-state", &ice_state1, NULL);
  g_object_get (t->webrtc2, "ice-gathering-state", &ice_state2, NULL);
  while (ice_state1 != GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE &&
      ice_state2 != GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE) {
    g_cond_wait (&t->cond, &t->lock);
    g_object_get (t->webrtc1, "ice-gathering-state", &ice_state1, NULL);
    g_object_get (t->webrtc2, "ice-gathering-state", &ice_state2, NULL);
  }
  g_mutex_unlock (&t->lock);
}

static void
test_webrtc_wait_for_ice_connection (struct test_webrtc *t,
    GstWebRTCICEConnectionState states)
{
  GstWebRTCICEConnectionState ice_state1, ice_state2, current;
  g_mutex_lock (&t->lock);
  g_object_get (t->webrtc1, "ice-connection-state", &ice_state1, NULL);
  g_object_get (t->webrtc2, "ice-connection-state", &ice_state2, NULL);
  current = (1 << ice_state1) | (1 << ice_state2);
  while ((current & states) == 0 || (current & ~states)) {
    g_cond_wait (&t->cond, &t->lock);
    g_object_get (t->webrtc1, "ice-connection-state", &ice_state1, NULL);
    g_object_get (t->webrtc2, "ice-connection-state", &ice_state2, NULL);
    current = (1 << ice_state1) | (1 << ice_state2);
  }
  g_mutex_unlock (&t->lock);
}
#endif
static void
_pad_added_fakesink (struct test_webrtc *t, GstElement * element,
    GstPad * pad, gpointer user_data)
{
  GstHarness *h;

  if (GST_PAD_DIRECTION (pad) != GST_PAD_SRC)
    return;

  h = gst_harness_new_with_element (element, NULL, "src_%u");
  gst_harness_add_sink_parse (h, "fakesink async=false sync=false");

  t->harnesses = g_list_prepend (t->harnesses, h);
}

static GstWebRTCSessionDescription *
_count_num_sdp_media (struct test_webrtc *t, GstElement * element,
    GstPromise * promise, gpointer user_data)
{
  GstWebRTCSessionDescription *offer = NULL;
  guint expected = GPOINTER_TO_UINT (user_data);
  const GstStructure *reply;
  const gchar *field;

  field = t->offerror == 1 && t->webrtc1 == element ? "offer" : "answer";

  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, field,
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);

  fail_unless_equals_int (gst_sdp_message_medias_len (offer->sdp), expected);

  return offer;
}

GST_START_TEST (test_sdp_no_media)
{
  struct test_webrtc *t = test_webrtc_new ();

  /* check that a no stream connection creates 0 media sections */

  t->on_negotiation_needed = NULL;
  t->offer_data = GUINT_TO_POINTER (0);
  t->on_offer_created = _count_num_sdp_media;
  t->answer_data = GUINT_TO_POINTER (0);
  t->on_answer_created = _count_num_sdp_media;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless (t->state == STATE_ANSWER_CREATED);
  test_webrtc_free (t);
}

GST_END_TEST;

static void
add_fake_audio_src_harness (GstHarness * h, gint pt)
{
  GstCaps *caps = gst_caps_from_string (OPUS_RTP_CAPS (pt));
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gst_structure_set (s, "payload", G_TYPE_INT, pt, NULL);
  gst_harness_set_src_caps (h, caps);
  gst_harness_add_src_parse (h, "fakesrc is-live=true", TRUE);
}

static void
add_fake_video_src_harness (GstHarness * h, gint pt)
{
  GstCaps *caps = gst_caps_from_string (VP8_RTP_CAPS (pt));
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gst_structure_set (s, "payload", G_TYPE_INT, pt, NULL);
  gst_harness_set_src_caps (h, caps);
  gst_harness_add_src_parse (h, "fakesrc is-live=true", TRUE);
}

static struct test_webrtc *
create_audio_test (void)
{
  struct test_webrtc *t = test_webrtc_new ();
  GstHarness *h;

  t->on_negotiation_needed = NULL;
  t->on_pad_added = _pad_added_fakesink;

  h = gst_harness_new_with_element (t->webrtc1, "sink_0", NULL);
  add_fake_audio_src_harness (h, 96);
  t->harnesses = g_list_prepend (t->harnesses, h);

  return t;
}

GST_START_TEST (test_audio)
{
  struct test_webrtc *t = create_audio_test ();

  /* check that a single stream connection creates the associated number
   * of media sections */

  t->on_negotiation_needed = NULL;
  t->offer_data = GUINT_TO_POINTER (1);
  t->on_offer_created = _count_num_sdp_media;
  t->answer_data = GUINT_TO_POINTER (1);
  t->on_answer_created = _count_num_sdp_media;
  t->on_ice_candidate = NULL;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ANSWER_CREATED, t->state);
  test_webrtc_free (t);
}

GST_END_TEST;

static struct test_webrtc *
create_audio_video_test (void)
{
  struct test_webrtc *t = test_webrtc_new ();
  GstHarness *h;

  t->on_negotiation_needed = NULL;
  t->on_pad_added = _pad_added_fakesink;

  h = gst_harness_new_with_element (t->webrtc1, "sink_0", NULL);
  add_fake_audio_src_harness (h, 96);
  t->harnesses = g_list_prepend (t->harnesses, h);

  h = gst_harness_new_with_element (t->webrtc1, "sink_1", NULL);
  add_fake_video_src_harness (h, 97);
  t->harnesses = g_list_prepend (t->harnesses, h);

  return t;
}

GST_START_TEST (test_audio_video)
{
  struct test_webrtc *t = create_audio_video_test ();

  /* check that a dual stream connection creates the associated number
   * of media sections */

  t->on_negotiation_needed = NULL;
  t->offer_data = GUINT_TO_POINTER (2);
  t->on_offer_created = _count_num_sdp_media;
  t->answer_data = GUINT_TO_POINTER (2);
  t->on_answer_created = _count_num_sdp_media;
  t->on_ice_candidate = NULL;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ANSWER_CREATED, t->state);
  test_webrtc_free (t);
}

GST_END_TEST;

typedef void (*ValidateSDPFunc) (struct test_webrtc * t, GstElement * element,
    GstWebRTCSessionDescription * desc, gpointer user_data);

struct validate_sdp
{
  ValidateSDPFunc validate;
  gpointer user_data;
};

static GstWebRTCSessionDescription *
validate_sdp (struct test_webrtc *t, GstElement * element,
    GstPromise * promise, gpointer user_data)
{
  struct validate_sdp *validate = user_data;
  GstWebRTCSessionDescription *offer = NULL;
  const GstStructure *reply;
  const gchar *field;

  field = t->offerror == 1 && t->webrtc1 == element ? "offer" : "answer";

  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, field,
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);

  validate->validate (t, element, offer, validate->user_data);

  return offer;
}

static void
on_sdp_media_direction (struct test_webrtc *t, GstElement * element,
    GstWebRTCSessionDescription * desc, gpointer user_data)
{
  gchar **expected_directions = user_data;
  int i;

  for (i = 0; i < gst_sdp_message_medias_len (desc->sdp); i++) {
    const GstSDPMedia *media = gst_sdp_message_get_media (desc->sdp, i);
    gboolean have_direction = FALSE;
    int j;

    for (j = 0; j < gst_sdp_media_attributes_len (media); j++) {
      const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, j);

      if (g_strcmp0 (attr->key, "inactive") == 0) {
        fail_unless (have_direction == FALSE,
            "duplicate/multiple directions for media %u", j);
        have_direction = TRUE;
        fail_unless (g_strcmp0 (attr->key, expected_directions[i]) == 0);
      } else if (g_strcmp0 (attr->key, "sendonly") == 0) {
        fail_unless (have_direction == FALSE,
            "duplicate/multiple directions for media %u", j);
        have_direction = TRUE;
        fail_unless (g_strcmp0 (attr->key, expected_directions[i]) == 0);
      } else if (g_strcmp0 (attr->key, "recvonly") == 0) {
        fail_unless (have_direction == FALSE,
            "duplicate/multiple directions for media %u", j);
        have_direction = TRUE;
        fail_unless (g_strcmp0 (attr->key, expected_directions[i]) == 0);
      } else if (g_strcmp0 (attr->key, "sendrecv") == 0) {
        fail_unless (have_direction == FALSE,
            "duplicate/multiple directions for media %u", j);
        have_direction = TRUE;
        fail_unless (g_strcmp0 (attr->key, expected_directions[i]) == 0);
      }
    }
    fail_unless (have_direction, "no direction attribute in media %u", j);
  }
}

GST_START_TEST (test_media_direction)
{
  struct test_webrtc *t = create_audio_video_test ();
  const gchar *expected_offer[] = { "sendrecv", "sendrecv" };
  const gchar *expected_answer[] = { "sendrecv", "recvonly" };
  struct validate_sdp offer = { on_sdp_media_direction, expected_offer };
  struct validate_sdp answer = { on_sdp_media_direction, expected_answer };
  GstHarness *h;

  /* check the default media directions for transceivers */

  h = gst_harness_new_with_element (t->webrtc2, "sink_0", NULL);
  add_fake_audio_src_harness (h, 96);
  t->harnesses = g_list_prepend (t->harnesses, h);

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ANSWER_CREATED, t->state);
  test_webrtc_free (t);
}

GST_END_TEST;

static void
on_sdp_media_payload_types (struct test_webrtc *t, GstElement * element,
    GstWebRTCSessionDescription * desc, gpointer user_data)
{
  const GstSDPMedia *vmedia;
  guint j;

  fail_unless_equals_int (gst_sdp_message_medias_len (desc->sdp), 2);

  vmedia = gst_sdp_message_get_media (desc->sdp, 1);

  for (j = 0; j < gst_sdp_media_attributes_len (vmedia); j++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (vmedia, j);

    if (!g_strcmp0 (attr->key, "rtpmap")) {
      if (g_str_has_prefix (attr->value, "97")) {
        fail_unless_equals_string (attr->value, "97 VP8/90000");
      } else if (g_str_has_prefix (attr->value, "96")) {
        fail_unless_equals_string (attr->value, "96 red/90000");
      } else if (g_str_has_prefix (attr->value, "98")) {
        fail_unless_equals_string (attr->value, "98 ulpfec/90000");
      } else if (g_str_has_prefix (attr->value, "99")) {
        fail_unless_equals_string (attr->value, "99 rtx/90000");
      } else if (g_str_has_prefix (attr->value, "100")) {
        fail_unless_equals_string (attr->value, "100 rtx/90000");
      }
    }
  }
}

/* In this test we verify that webrtcbin will pick available payload
 * types when it needs to, in that example for RTX and FEC */
GST_START_TEST (test_payload_types)
{
  struct test_webrtc *t = create_audio_video_test ();
  struct validate_sdp offer = { on_sdp_media_payload_types, NULL };
  GstWebRTCRTPTransceiver *trans;
  GArray *transceivers;

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->on_ice_candidate = NULL;
  /* We don't really care about the answer here */
  t->on_answer_created = NULL;

  g_signal_emit_by_name (t->webrtc1, "get-transceivers", &transceivers);
  fail_unless_equals_int (transceivers->len, 2);
  trans = g_array_index (transceivers, GstWebRTCRTPTransceiver *, 1);
  g_object_set (trans, "fec-type", GST_WEBRTC_FEC_TYPE_ULP_RED, "do-nack", TRUE,
      NULL);
  g_array_unref (transceivers);

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ANSWER_CREATED, t->state);
  test_webrtc_free (t);
}

GST_END_TEST;

static void
on_sdp_media_setup (struct test_webrtc *t, GstElement * element,
    GstWebRTCSessionDescription * desc, gpointer user_data)
{
  gchar **expected_setup = user_data;
  int i;

  for (i = 0; i < gst_sdp_message_medias_len (desc->sdp); i++) {
    const GstSDPMedia *media = gst_sdp_message_get_media (desc->sdp, i);
    gboolean have_setup = FALSE;
    int j;

    for (j = 0; j < gst_sdp_media_attributes_len (media); j++) {
      const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, j);

      if (g_strcmp0 (attr->key, "setup") == 0) {
        fail_unless (have_setup == FALSE,
            "duplicate/multiple setup for media %u", j);
        have_setup = TRUE;
        fail_unless (g_strcmp0 (attr->value, expected_setup[i]) == 0);
      }
    }
    fail_unless (have_setup, "no setup attribute in media %u", j);
  }
}

GST_START_TEST (test_media_setup)
{
  struct test_webrtc *t = create_audio_test ();
  const gchar *expected_offer[] = { "actpass" };
  const gchar *expected_answer[] = { "active" };
  struct validate_sdp offer = { on_sdp_media_setup, expected_offer };
  struct validate_sdp answer = { on_sdp_media_setup, expected_answer };

  /* check the default dtls setup negotiation values */

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ANSWER_CREATED, t->state);
  test_webrtc_free (t);
}

GST_END_TEST;

GST_START_TEST (test_no_nice_elements_request_pad)
{
  struct test_webrtc *t = test_webrtc_new ();
  GstPluginFeature *nicesrc, *nicesink;
  GstRegistry *registry;
  GstPad *pad;

  /* check that the absence of libnice elements posts an error on the bus
   * when requesting a pad */

  registry = gst_registry_get ();
  nicesrc = gst_registry_lookup_feature (registry, "nicesrc");
  nicesink = gst_registry_lookup_feature (registry, "nicesink");

  if (nicesrc)
    gst_registry_remove_feature (registry, nicesrc);
  if (nicesink)
    gst_registry_remove_feature (registry, nicesink);

  t->bus_message = NULL;

  pad = gst_element_get_request_pad (t->webrtc1, "sink_0");
  fail_unless (pad == NULL);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ERROR, t->state);
  test_webrtc_free (t);

  if (nicesrc)
    gst_registry_add_feature (registry, nicesrc);
  if (nicesink)
    gst_registry_add_feature (registry, nicesink);
}

GST_END_TEST;

GST_START_TEST (test_no_nice_elements_state_change)
{
  struct test_webrtc *t = test_webrtc_new ();
  GstPluginFeature *nicesrc, *nicesink;
  GstRegistry *registry;

  /* check that the absence of libnice elements posts an error on the bus */

  registry = gst_registry_get ();
  nicesrc = gst_registry_lookup_feature (registry, "nicesrc");
  nicesink = gst_registry_lookup_feature (registry, "nicesink");

  if (nicesrc)
    gst_registry_remove_feature (registry, nicesrc);
  if (nicesink)
    gst_registry_remove_feature (registry, nicesink);

  t->bus_message = NULL;
  gst_element_set_state (t->webrtc1, GST_STATE_READY);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ERROR, t->state);
  test_webrtc_free (t);

  if (nicesrc)
    gst_registry_add_feature (registry, nicesrc);
  if (nicesink)
    gst_registry_add_feature (registry, nicesink);
}

GST_END_TEST;

static void
validate_rtc_stats (const GstStructure * s)
{
  GstWebRTCStatsType type = 0;
  double ts = 0.;
  gchar *id = NULL;

  fail_unless (gst_structure_get (s, "type", GST_TYPE_WEBRTC_STATS_TYPE, &type,
          NULL));
  fail_unless (gst_structure_get (s, "id", G_TYPE_STRING, &id, NULL));
  fail_unless (gst_structure_get (s, "timestamp", G_TYPE_DOUBLE, &ts, NULL));
  fail_unless (type != 0);
  fail_unless (ts != 0.);
  fail_unless (id != NULL);

  g_free (id);
}

static void
validate_codec_stats (const GstStructure * s)
{
  guint pt = 0, clock_rate = 0;

  fail_unless (gst_structure_get (s, "payload-type", G_TYPE_UINT, &pt, NULL));
  fail_unless (gst_structure_get (s, "clock-rate", G_TYPE_UINT, &clock_rate,
          NULL));
  fail_unless (pt >= 0 && pt <= 127);
  fail_unless (clock_rate >= 0);
}

static void
validate_rtc_stream_stats (const GstStructure * s, const GstStructure * stats)
{
  gchar *codec_id, *transport_id;
  GstStructure *codec, *transport;

  fail_unless (gst_structure_get (s, "codec-id", G_TYPE_STRING, &codec_id,
          NULL));
  fail_unless (gst_structure_get (s, "transport-id", G_TYPE_STRING,
          &transport_id, NULL));

  fail_unless (gst_structure_get (stats, codec_id, GST_TYPE_STRUCTURE, &codec,
          NULL));
  fail_unless (gst_structure_get (stats, transport_id, GST_TYPE_STRUCTURE,
          &transport, NULL));

  fail_unless (codec != NULL);
  fail_unless (transport != NULL);

  gst_structure_free (transport);
  gst_structure_free (codec);

  g_free (codec_id);
  g_free (transport_id);
}

static void
validate_inbound_rtp_stats (const GstStructure * s, const GstStructure * stats)
{
  guint ssrc, fir, pli, nack;
  gint packets_lost;
  guint64 packets_received, bytes_received;
  double jitter;
  gchar *remote_id;
  GstStructure *remote;

  validate_rtc_stream_stats (s, stats);

  fail_unless (gst_structure_get (s, "ssrc", G_TYPE_UINT, &ssrc, NULL));
  fail_unless (gst_structure_get (s, "fir-count", G_TYPE_UINT, &fir, NULL));
  fail_unless (gst_structure_get (s, "pli-count", G_TYPE_UINT, &pli, NULL));
  fail_unless (gst_structure_get (s, "nack-count", G_TYPE_UINT, &nack, NULL));
  fail_unless (gst_structure_get (s, "packets-received", G_TYPE_UINT64,
          &packets_received, NULL));
  fail_unless (gst_structure_get (s, "bytes-received", G_TYPE_UINT64,
          &bytes_received, NULL));
  fail_unless (gst_structure_get (s, "jitter", G_TYPE_DOUBLE, &jitter, NULL));
  fail_unless (gst_structure_get (s, "packets-lost", G_TYPE_INT, &packets_lost,
          NULL));
  fail_unless (gst_structure_get (s, "remote-id", G_TYPE_STRING, &remote_id,
          NULL));
  fail_unless (gst_structure_get (stats, remote_id, GST_TYPE_STRUCTURE, &remote,
          NULL));
  fail_unless (remote != NULL);

  gst_structure_free (remote);
  g_free (remote_id);
}

static void
validate_remote_inbound_rtp_stats (const GstStructure * s,
    const GstStructure * stats)
{
  guint ssrc;
  gint packets_lost;
  double jitter, rtt;
  gchar *local_id;
  GstStructure *local;

  validate_rtc_stream_stats (s, stats);

  fail_unless (gst_structure_get (s, "ssrc", G_TYPE_UINT, &ssrc, NULL));
  fail_unless (gst_structure_get (s, "jitter", G_TYPE_DOUBLE, &jitter, NULL));
  fail_unless (gst_structure_get (s, "packets-lost", G_TYPE_INT, &packets_lost,
          NULL));
  fail_unless (gst_structure_get (s, "round-trip-time", G_TYPE_DOUBLE, &rtt,
          NULL));
  fail_unless (gst_structure_get (s, "local-id", G_TYPE_STRING, &local_id,
          NULL));
  fail_unless (gst_structure_get (stats, local_id, GST_TYPE_STRUCTURE, &local,
          NULL));
  fail_unless (local != NULL);

  gst_structure_free (local);
  g_free (local_id);
}

static void
validate_outbound_rtp_stats (const GstStructure * s, const GstStructure * stats)
{
  guint ssrc, fir, pli, nack;
  guint64 packets_sent, bytes_sent;
  gchar *remote_id;
  GstStructure *remote;

  validate_rtc_stream_stats (s, stats);

  fail_unless (gst_structure_get (s, "ssrc", G_TYPE_UINT, &ssrc, NULL));
  fail_unless (gst_structure_get (s, "fir-count", G_TYPE_UINT, &fir, NULL));
  fail_unless (gst_structure_get (s, "pli-count", G_TYPE_UINT, &pli, NULL));
  fail_unless (gst_structure_get (s, "nack-count", G_TYPE_UINT, &nack, NULL));
  fail_unless (gst_structure_get (s, "packets-sent", G_TYPE_UINT64,
          &packets_sent, NULL));
  fail_unless (gst_structure_get (s, "bytes-sent", G_TYPE_UINT64, &bytes_sent,
          NULL));
  fail_unless (gst_structure_get (s, "remote-id", G_TYPE_STRING, &remote_id,
          NULL));
  fail_unless (gst_structure_get (stats, remote_id, GST_TYPE_STRUCTURE, &remote,
          NULL));
  fail_unless (remote != NULL);

  gst_structure_free (remote);
  g_free (remote_id);
}

static void
validate_remote_outbound_rtp_stats (const GstStructure * s,
    const GstStructure * stats)
{
  guint ssrc;
  gchar *local_id;
  GstStructure *local;

  validate_rtc_stream_stats (s, stats);

  fail_unless (gst_structure_get (s, "ssrc", G_TYPE_UINT, &ssrc, NULL));
  fail_unless (gst_structure_get (s, "local-id", G_TYPE_STRING, &local_id,
          NULL));
  fail_unless (gst_structure_get (stats, local_id, GST_TYPE_STRUCTURE, &local,
          NULL));
  fail_unless (local != NULL);

  gst_structure_free (local);
  g_free (local_id);
}

static gboolean
validate_stats_foreach (GQuark field_id, const GValue * value,
    const GstStructure * stats)
{
  const gchar *field = g_quark_to_string (field_id);
  GstWebRTCStatsType type;
  const GstStructure *s;

  fail_unless (GST_VALUE_HOLDS_STRUCTURE (value));

  s = gst_value_get_structure (value);

  GST_INFO ("validating field %s %" GST_PTR_FORMAT, field, s);

  validate_rtc_stats (s);
  gst_structure_get (s, "type", GST_TYPE_WEBRTC_STATS_TYPE, &type, NULL);
  if (type == GST_WEBRTC_STATS_CODEC) {
    validate_codec_stats (s);
  } else if (type == GST_WEBRTC_STATS_INBOUND_RTP) {
    validate_inbound_rtp_stats (s, stats);
  } else if (type == GST_WEBRTC_STATS_OUTBOUND_RTP) {
    validate_outbound_rtp_stats (s, stats);
  } else if (type == GST_WEBRTC_STATS_REMOTE_INBOUND_RTP) {
    validate_remote_inbound_rtp_stats (s, stats);
  } else if (type == GST_WEBRTC_STATS_REMOTE_OUTBOUND_RTP) {
    validate_remote_outbound_rtp_stats (s, stats);
  } else if (type == GST_WEBRTC_STATS_CSRC) {
  } else if (type == GST_WEBRTC_STATS_PEER_CONNECTION) {
  } else if (type == GST_WEBRTC_STATS_DATA_CHANNEL) {
  } else if (type == GST_WEBRTC_STATS_STREAM) {
  } else if (type == GST_WEBRTC_STATS_TRANSPORT) {
  } else if (type == GST_WEBRTC_STATS_CANDIDATE_PAIR) {
  } else if (type == GST_WEBRTC_STATS_LOCAL_CANDIDATE) {
  } else if (type == GST_WEBRTC_STATS_REMOTE_CANDIDATE) {
  } else if (type == GST_WEBRTC_STATS_CERTIFICATE) {
  } else {
    g_assert_not_reached ();
  }

  return TRUE;
}

static void
validate_stats (const GstStructure * stats)
{
  gst_structure_foreach (stats,
      (GstStructureForeachFunc) validate_stats_foreach, (gpointer) stats);
}

static void
_on_stats (GstPromise * promise, gpointer user_data)
{
  struct test_webrtc *t = user_data;
  const GstStructure *reply = gst_promise_get_reply (promise);
  int i;

  validate_stats (reply);
  i = GPOINTER_TO_INT (t->user_data);
  i++;
  t->user_data = GINT_TO_POINTER (i);
  if (i >= 2)
    test_webrtc_signal_state (t, STATE_CUSTOM);

  gst_promise_unref (promise);
}

GST_START_TEST (test_session_stats)
{
  struct test_webrtc *t = test_webrtc_new ();
  GstPromise *p;

  /* test that the stats generated without any streams are sane */

  t->on_negotiation_needed = NULL;
  t->on_offer_created = NULL;
  t->on_answer_created = NULL;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ANSWER_CREATED, t->state);

  p = gst_promise_new_with_change_func (_on_stats, t, NULL);
  g_signal_emit_by_name (t->webrtc1, "get-stats", NULL, p);
  p = gst_promise_new_with_change_func (_on_stats, t, NULL);
  g_signal_emit_by_name (t->webrtc2, "get-stats", NULL, p);

  test_webrtc_wait_for_state_mask (t, 1 << STATE_CUSTOM);

  test_webrtc_free (t);
}

GST_END_TEST;

GST_START_TEST (test_add_transceiver)
{
  struct test_webrtc *t = test_webrtc_new ();
  GstWebRTCRTPTransceiverDirection direction;
  GstWebRTCRTPTransceiver *trans;

  direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV;
  g_signal_emit_by_name (t->webrtc1, "add-transceiver", direction, NULL,
      &trans);
  fail_unless (trans != NULL);
  fail_unless_equals_int (direction, trans->direction);

  gst_object_unref (trans);

  test_webrtc_free (t);
}

GST_END_TEST;

GST_START_TEST (test_get_transceivers)
{
  struct test_webrtc *t = create_audio_test ();
  GstWebRTCRTPTransceiver *trans;
  GArray *transceivers;

  g_signal_emit_by_name (t->webrtc1, "get-transceivers", &transceivers);
  fail_unless (transceivers != NULL);
  fail_unless_equals_int (1, transceivers->len);

  trans = g_array_index (transceivers, GstWebRTCRTPTransceiver *, 0);
  fail_unless (trans != NULL);

  g_array_unref (transceivers);

  test_webrtc_free (t);
}

GST_END_TEST;

GST_START_TEST (test_add_recvonly_transceiver)
{
  struct test_webrtc *t = test_webrtc_new ();
  GstWebRTCRTPTransceiverDirection direction;
  GstWebRTCRTPTransceiver *trans;
  const gchar *expected_offer[] = { "recvonly" };
  const gchar *expected_answer[] = { "sendonly" };
  struct validate_sdp offer = { on_sdp_media_direction, expected_offer };
  struct validate_sdp answer = { on_sdp_media_direction, expected_answer };
  GstCaps *caps;
  GstHarness *h;

  /* add a transceiver that will only receive an opus stream and check that
   * the created offer is marked as recvonly */

  t->on_negotiation_needed = NULL;
  t->on_pad_added = _pad_added_fakesink;
  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;

  /* setup recvonly transceiver */
  caps = gst_caps_from_string (OPUS_RTP_CAPS (96));
  direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY;
  g_signal_emit_by_name (t->webrtc1, "add-transceiver", direction, caps,
      &trans);
  gst_caps_unref (caps);
  fail_unless (trans != NULL);
  gst_object_unref (trans);

  /* setup sendonly peer */
  h = gst_harness_new_with_element (t->webrtc2, "sink_0", NULL);
  add_fake_audio_src_harness (h, 96);
  t->harnesses = g_list_prepend (t->harnesses, h);

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ANSWER_CREATED, t->state);
  test_webrtc_free (t);
}

GST_END_TEST;

GST_START_TEST (test_recvonly_sendonly)
{
  struct test_webrtc *t = test_webrtc_new ();
  GstWebRTCRTPTransceiverDirection direction;
  GstWebRTCRTPTransceiver *trans;
  const gchar *expected_offer[] = { "recvonly", "sendonly" };
  const gchar *expected_answer[] = { "sendonly", "recvonly" };
  struct validate_sdp offer = { on_sdp_media_direction, expected_offer };
  struct validate_sdp answer = { on_sdp_media_direction, expected_answer };
  GstCaps *caps;
  GstHarness *h;
  GArray *transceivers;

  /* add a transceiver that will only receive an opus stream and check that
   * the created offer is marked as recvonly */

  t->on_negotiation_needed = NULL;
  t->on_pad_added = _pad_added_fakesink;
  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;

  /* setup recvonly transceiver */
  caps = gst_caps_from_string (OPUS_RTP_CAPS (96));
  direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY;
  g_signal_emit_by_name (t->webrtc1, "add-transceiver", direction, caps,
      &trans);
  gst_caps_unref (caps);
  fail_unless (trans != NULL);
  gst_object_unref (trans);

  /* setup sendonly stream */
  h = gst_harness_new_with_element (t->webrtc1, "sink_1", NULL);
  add_fake_audio_src_harness (h, 96);
  t->harnesses = g_list_prepend (t->harnesses, h);
  g_signal_emit_by_name (t->webrtc1, "get-transceivers", &transceivers);
  fail_unless (transceivers != NULL);
  trans = g_array_index (transceivers, GstWebRTCRTPTransceiver *, 1);
  trans->direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;

  g_array_unref (transceivers);

  /* setup sendonly peer */
  h = gst_harness_new_with_element (t->webrtc2, "sink_0", NULL);
  add_fake_audio_src_harness (h, 96);
  t->harnesses = g_list_prepend (t->harnesses, h);

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ANSWER_CREATED, t->state);
  test_webrtc_free (t);
}

GST_END_TEST;

static gboolean
_message_media_is_datachannel (const GstSDPMessage * msg, guint media_id)
{
  const GstSDPMedia *media;

  if (!msg)
    return FALSE;

  if (gst_sdp_message_medias_len (msg) <= media_id)
    return FALSE;

  media = gst_sdp_message_get_media (msg, media_id);

  if (g_strcmp0 (gst_sdp_media_get_media (media), "application") != 0)
    return FALSE;

  if (gst_sdp_media_formats_len (media) != 1)
    return FALSE;

  if (g_strcmp0 (gst_sdp_media_get_format (media, 0),
          "webrtc-datachannel") != 0)
    return FALSE;

  return TRUE;
}

static void
on_sdp_has_datachannel (struct test_webrtc *t, GstElement * element,
    GstWebRTCSessionDescription * desc, gpointer user_data)
{
  gboolean have_data_channel = FALSE;
  int i;

  for (i = 0; i < gst_sdp_message_medias_len (desc->sdp); i++) {
    if (_message_media_is_datachannel (desc->sdp, i)) {
      /* there should only be one data channel m= section */
      fail_unless_equals_int (FALSE, have_data_channel);
      have_data_channel = TRUE;
    }
  }

  fail_unless_equals_int (TRUE, have_data_channel);
}

static void
on_channel_error_not_reached (GObject * channel, GError * error,
    gpointer user_data)
{
  g_assert_not_reached ();
}

GST_START_TEST (test_data_channel_create)
{
  struct test_webrtc *t = test_webrtc_new ();
  GObject *channel = NULL;
  struct validate_sdp offer = { on_sdp_has_datachannel, NULL };
  struct validate_sdp answer = { on_sdp_has_datachannel, NULL };
  gchar *label;

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  g_signal_emit_by_name (t->webrtc1, "create-data-channel", "label", NULL,
      &channel);
  g_assert_nonnull (channel);
  g_object_get (channel, "label", &label, NULL);
  g_assert_cmpstr (label, ==, "label");
  g_signal_connect (channel, "on-error",
      G_CALLBACK (on_channel_error_not_reached), NULL);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ANSWER_CREATED, t->state);
  g_object_unref (channel);
  g_free (label);
  test_webrtc_free (t);
}

GST_END_TEST;

static void
have_data_channel (struct test_webrtc *t, GstElement * element,
    GObject * our, gpointer user_data)
{
  GObject *other = user_data;
  gchar *our_label, *other_label;

  g_signal_connect (our, "on-error", G_CALLBACK (on_channel_error_not_reached),
      NULL);

  g_object_get (our, "label", &our_label, NULL);
  g_object_get (other, "label", &other_label, NULL);

  g_assert_cmpstr (our_label, ==, other_label);

  g_free (our_label);
  g_free (other_label);

  test_webrtc_signal_state_unlocked (t, STATE_CUSTOM);
}

GST_START_TEST (test_data_channel_remote_notify)
{
  struct test_webrtc *t = test_webrtc_new ();
  GObject *channel = NULL;
  struct validate_sdp offer = { on_sdp_has_datachannel, NULL };
  struct validate_sdp answer = { on_sdp_has_datachannel, NULL };

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;
  t->on_data_channel = have_data_channel;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  g_signal_emit_by_name (t->webrtc1, "create-data-channel", "label", NULL,
      &channel);
  g_assert_nonnull (channel);
  t->data_channel_data = channel;
  g_signal_connect (channel, "on-error",
      G_CALLBACK (on_channel_error_not_reached), NULL);

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_state_mask (t, 1 << STATE_CUSTOM);

  g_object_unref (channel);
  test_webrtc_free (t);
}

GST_END_TEST;

static const gchar *test_string = "GStreamer WebRTC is awesome!";

static void
on_message_string (GObject * channel, const gchar * str, struct test_webrtc *t)
{
  gchar *expected = g_object_steal_data (channel, "expected");
  g_assert_cmpstr (expected, ==, str);
  g_free (expected);

  test_webrtc_signal_state (t, STATE_CUSTOM);
}

static void
have_data_channel_transfer_string (struct test_webrtc *t, GstElement * element,
    GObject * our, gpointer user_data)
{
  GObject *other = user_data;
  GstWebRTCDataChannelState state;

  g_object_get (our, "ready-state", &state, NULL);
  fail_unless_equals_int (GST_WEBRTC_DATA_CHANNEL_STATE_OPEN, state);
  g_object_get (other, "ready-state", &state, NULL);
  fail_unless_equals_int (GST_WEBRTC_DATA_CHANNEL_STATE_OPEN, state);

  g_object_set_data_full (our, "expected", g_strdup (test_string), g_free);
  g_signal_connect (our, "on-message-string", G_CALLBACK (on_message_string),
      t);

  g_signal_connect (other, "on-error",
      G_CALLBACK (on_channel_error_not_reached), NULL);
  g_signal_emit_by_name (other, "send-string", test_string);
}

GST_START_TEST (test_data_channel_transfer_string)
{
  struct test_webrtc *t = test_webrtc_new ();
  GObject *channel = NULL;
  struct validate_sdp offer = { on_sdp_has_datachannel, NULL };
  struct validate_sdp answer = { on_sdp_has_datachannel, NULL };

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;
  t->on_data_channel = have_data_channel_transfer_string;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  g_signal_emit_by_name (t->webrtc1, "create-data-channel", "label", NULL,
      &channel);
  g_assert_nonnull (channel);
  t->data_channel_data = channel;
  g_signal_connect (channel, "on-error",
      G_CALLBACK (on_channel_error_not_reached), NULL);

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_state_mask (t, 1 << STATE_CUSTOM);

  g_object_unref (channel);
  test_webrtc_free (t);
}

GST_END_TEST;

#define g_assert_cmpbytes(b1, b2)                       \
    G_STMT_START {                                      \
      gsize l1, l2;                                     \
      const guint8 *d1 = g_bytes_get_data (b1, &l1);    \
      const guint8 *d2 = g_bytes_get_data (b2, &l2);    \
      g_assert_cmpmem (d1, l1, d2, l2);                 \
    } G_STMT_END;

static void
on_message_data (GObject * channel, GBytes * data, struct test_webrtc *t)
{
  GBytes *expected = g_object_steal_data (channel, "expected");
  g_assert_cmpbytes (data, expected);
  g_bytes_unref (expected);

  test_webrtc_signal_state (t, STATE_CUSTOM);
}

static void
have_data_channel_transfer_data (struct test_webrtc *t, GstElement * element,
    GObject * our, gpointer user_data)
{
  GObject *other = user_data;
  GBytes *data = g_bytes_new_static (test_string, strlen (test_string));
  GstWebRTCDataChannelState state;

  g_object_get (our, "ready-state", &state, NULL);
  fail_unless_equals_int (GST_WEBRTC_DATA_CHANNEL_STATE_OPEN, state);
  g_object_get (other, "ready-state", &state, NULL);
  fail_unless_equals_int (GST_WEBRTC_DATA_CHANNEL_STATE_OPEN, state);

  g_object_set_data_full (our, "expected", g_bytes_ref (data),
      (GDestroyNotify) g_bytes_unref);
  g_signal_connect (our, "on-message-data", G_CALLBACK (on_message_data), t);

  g_signal_connect (other, "on-error",
      G_CALLBACK (on_channel_error_not_reached), NULL);
  g_signal_emit_by_name (other, "send-data", data);
}

GST_START_TEST (test_data_channel_transfer_data)
{
  struct test_webrtc *t = test_webrtc_new ();
  GObject *channel = NULL;
  struct validate_sdp offer = { on_sdp_has_datachannel, NULL };
  struct validate_sdp answer = { on_sdp_has_datachannel, NULL };

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;
  t->on_data_channel = have_data_channel_transfer_data;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  g_signal_emit_by_name (t->webrtc1, "create-data-channel", "label", NULL,
      &channel);
  g_assert_nonnull (channel);
  t->data_channel_data = channel;
  g_signal_connect (channel, "on-error",
      G_CALLBACK (on_channel_error_not_reached), NULL);

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_state_mask (t, 1 << STATE_CUSTOM);

  g_object_unref (channel);
  test_webrtc_free (t);
}

GST_END_TEST;

static void
have_data_channel_create_data_channel (struct test_webrtc *t,
    GstElement * element, GObject * our, gpointer user_data)
{
  GObject *another;

  t->on_data_channel = have_data_channel_transfer_string;

  g_signal_emit_by_name (t->webrtc1, "create-data-channel", "label", NULL,
      &another);
  g_assert_nonnull (another);
  t->data_channel_data = another;
  g_signal_connect (another, "on-error",
      G_CALLBACK (on_channel_error_not_reached), NULL);
}

GST_START_TEST (test_data_channel_create_after_negotiate)
{
  struct test_webrtc *t = test_webrtc_new ();
  GObject *channel = NULL;
  struct validate_sdp offer = { on_sdp_has_datachannel, NULL };
  struct validate_sdp answer = { on_sdp_has_datachannel, NULL };

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;
  t->on_data_channel = have_data_channel_create_data_channel;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  g_signal_emit_by_name (t->webrtc1, "create-data-channel", "prev-label", NULL,
      &channel);
  g_assert_nonnull (channel);
  t->data_channel_data = channel;
  g_signal_connect (channel, "on-error",
      G_CALLBACK (on_channel_error_not_reached), NULL);

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_state_mask (t, 1 << STATE_CUSTOM);

  g_object_unref (channel);
  test_webrtc_free (t);
}

GST_END_TEST;

static void
on_buffered_amount_low_emitted (GObject * channel, struct test_webrtc *t)
{
  test_webrtc_signal_state (t, STATE_CUSTOM);
}

static void
have_data_channel_check_low_threshold_emitted (struct test_webrtc *t,
    GstElement * element, GObject * our, gpointer user_data)
{
  g_signal_connect (our, "on-buffered-amount-low",
      G_CALLBACK (on_buffered_amount_low_emitted), t);
  g_object_set (our, "buffered-amount-low-threshold", 1, NULL);

  g_signal_connect (our, "on-error", G_CALLBACK (on_channel_error_not_reached),
      NULL);
  g_signal_emit_by_name (our, "send-string", "DATA");
}

GST_START_TEST (test_data_channel_low_threshold)
{
  struct test_webrtc *t = test_webrtc_new ();
  GObject *channel = NULL;
  struct validate_sdp offer = { on_sdp_has_datachannel, NULL };
  struct validate_sdp answer = { on_sdp_has_datachannel, NULL };

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;
  t->on_data_channel = have_data_channel_check_low_threshold_emitted;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  g_signal_emit_by_name (t->webrtc1, "create-data-channel", "label", NULL,
      &channel);
  g_assert_nonnull (channel);
  t->data_channel_data = channel;
  g_signal_connect (channel, "on-error",
      G_CALLBACK (on_channel_error_not_reached), NULL);

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_state_mask (t, 1 << STATE_CUSTOM);

  g_object_unref (channel);
  test_webrtc_free (t);
}

GST_END_TEST;

static void
on_channel_error (GObject * channel, GError * error, struct test_webrtc *t)
{
  g_assert_nonnull (error);

  test_webrtc_signal_state (t, STATE_CUSTOM);
}

static void
have_data_channel_transfer_large_data (struct test_webrtc *t,
    GstElement * element, GObject * our, gpointer user_data)
{
  GObject *other = user_data;
  const gsize size = 1024 * 1024;
  guint8 *random_data = g_new (guint8, size);
  GBytes *data;
  gsize i;

  for (i = 0; i < size; i++)
    random_data[i] = (guint8) (i & 0xff);

  data = g_bytes_new_static (random_data, size);

  g_object_set_data_full (our, "expected", g_bytes_ref (data),
      (GDestroyNotify) g_bytes_unref);
  g_signal_connect (our, "on-message-data", G_CALLBACK (on_message_data), t);

  g_signal_connect (other, "on-error", G_CALLBACK (on_channel_error), t);
  g_signal_emit_by_name (other, "send-data", data);
}

GST_START_TEST (test_data_channel_max_message_size)
{
  struct test_webrtc *t = test_webrtc_new ();
  GObject *channel = NULL;
  struct validate_sdp offer = { on_sdp_has_datachannel, NULL };
  struct validate_sdp answer = { on_sdp_has_datachannel, NULL };

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;
  t->on_data_channel = have_data_channel_transfer_large_data;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  g_signal_emit_by_name (t->webrtc1, "create-data-channel", "label", NULL,
      &channel);
  g_assert_nonnull (channel);
  t->data_channel_data = channel;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_state_mask (t, 1 << STATE_CUSTOM);

  g_object_unref (channel);
  test_webrtc_free (t);
}

GST_END_TEST;

static void
_on_ready_state_notify (GObject * channel, GParamSpec * pspec,
    struct test_webrtc *t)
{
  gint *n_ready = t->data_channel_data;
  GstWebRTCDataChannelState ready_state;

  g_object_get (channel, "ready-state", &ready_state, NULL);

  if (ready_state == GST_WEBRTC_DATA_CHANNEL_STATE_OPEN) {
    if (++(*n_ready) >= 2)
      test_webrtc_signal_state (t, STATE_CUSTOM);
  }
}

GST_START_TEST (test_data_channel_pre_negotiated)
{
  struct test_webrtc *t = test_webrtc_new ();
  GObject *channel1 = NULL, *channel2 = NULL;
  struct validate_sdp offer = { on_sdp_has_datachannel, NULL };
  struct validate_sdp answer = { on_sdp_has_datachannel, NULL };
  GstStructure *s;
  gint n_ready = 0;

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  s = gst_structure_new ("application/data-channel", "negotiated",
      G_TYPE_BOOLEAN, TRUE, "id", G_TYPE_INT, 1, NULL);

  g_signal_emit_by_name (t->webrtc1, "create-data-channel", "label", s,
      &channel1);
  g_assert_nonnull (channel1);
  g_signal_emit_by_name (t->webrtc2, "create-data-channel", "label", s,
      &channel2);
  g_assert_nonnull (channel2);

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);
  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless (t->state == STATE_ANSWER_CREATED);

  t->data_channel_data = &n_ready;

  g_signal_connect (channel1, "notify::ready-state",
      G_CALLBACK (_on_ready_state_notify), t);
  g_signal_connect (channel2, "notify::ready-state",
      G_CALLBACK (_on_ready_state_notify), t);

  test_webrtc_wait_for_state_mask (t, 1 << STATE_CUSTOM);
  test_webrtc_signal_state (t, STATE_NEW);

  have_data_channel_transfer_string (t, t->webrtc1, channel1, channel2);

  test_webrtc_wait_for_state_mask (t, 1 << STATE_CUSTOM);

  g_object_unref (channel1);
  g_object_unref (channel2);
  gst_structure_free (s);
  test_webrtc_free (t);
}

GST_END_TEST;

typedef struct
{
  guint num_media;
  guint num_active_media;
  const gchar **bundled;
  const gchar **bundled_only;
} BundleCheckData;

static gboolean
_parse_bundle (GstSDPMessage * sdp, GStrv * bundled)
{
  const gchar *group;
  gboolean ret = FALSE;

  group = gst_sdp_message_get_attribute_val (sdp, "group");

  if (group && g_str_has_prefix (group, "BUNDLE ")) {
    *bundled = g_strsplit (group + strlen ("BUNDLE "), " ", 0);

    if (!(*bundled)[0]) {
      GST_ERROR
          ("Invalid format for BUNDLE group, expected at least one mid (%s)",
          group);
      goto done;
    }
  } else {
    ret = TRUE;
    goto done;
  }

  ret = TRUE;

done:
  return ret;
}

static gboolean
_media_has_attribute_key (const GstSDPMedia * media, const gchar * key)
{
  int i;
  for (i = 0; i < gst_sdp_media_attributes_len (media); i++) {
    const GstSDPAttribute *attr = gst_sdp_media_get_attribute (media, i);

    if (g_strcmp0 (attr->key, key) == 0)
      return TRUE;
  }

  return FALSE;
}

static void
_check_bundled_sdp_media (struct test_webrtc *t, GstElement * element,
    GstWebRTCSessionDescription * sd, gpointer user_data)
{
  gchar **bundled = NULL;
  BundleCheckData *data = (BundleCheckData *) user_data;
  guint i;
  guint active_media;

  fail_unless_equals_int (gst_sdp_message_medias_len (sd->sdp),
      data->num_media);

  fail_unless (_parse_bundle (sd->sdp, &bundled));

  if (!bundled) {
    fail_unless_equals_int (g_strv_length ((GStrv) data->bundled), 0);
  } else {
    fail_unless_equals_int (g_strv_length (bundled),
        g_strv_length ((GStrv) data->bundled));
  }

  for (i = 0; data->bundled[i]; i++) {
    fail_unless (g_strv_contains ((const gchar **) bundled, data->bundled[i]));
  }

  active_media = 0;

  for (i = 0; i < gst_sdp_message_medias_len (sd->sdp); i++) {
    const GstSDPMedia *media = gst_sdp_message_get_media (sd->sdp, i);
    const gchar *mid = gst_sdp_media_get_attribute_val (media, "mid");

    if (g_strv_contains ((const gchar **) data->bundled_only, mid))
      fail_unless (_media_has_attribute_key (media, "bundle-only"));

    if (gst_sdp_media_get_port (media) != 0)
      active_media += 1;
  }

  fail_unless_equals_int (active_media, data->num_active_media);

  if (bundled)
    g_strfreev (bundled);
}

GST_START_TEST (test_bundle_audio_video_max_bundle_max_bundle)
{
  struct test_webrtc *t = create_audio_video_test ();
  const gchar *bundle[] = { "audio0", "video1", NULL };
  const gchar *offer_bundle_only[] = { "video1", NULL };
  const gchar *answer_bundle_only[] = { NULL };
  /* We set a max-bundle policy on the offering webrtcbin,
   * this means that all the offered medias should be part
   * of the group:BUNDLE attribute, and they should be marked
   * as bundle-only
   */
  BundleCheckData offer_data = {
    2,
    1,
    bundle,
    offer_bundle_only,
  };
  /* We also set a max-bundle policy on the answering webrtcbin,
   * this means that all the offered medias should be part
   * of the group:BUNDLE attribute, but need not be marked
   * as bundle-only.
   */
  BundleCheckData answer_data = {
    2,
    2,
    bundle,
    answer_bundle_only,
  };
  struct validate_sdp offer = { _check_bundled_sdp_media, &offer_data };
  struct validate_sdp answer = { _check_bundled_sdp_media, &answer_data };

  gst_util_set_object_arg (G_OBJECT (t->webrtc1), "bundle-policy",
      "max-bundle");
  gst_util_set_object_arg (G_OBJECT (t->webrtc2), "bundle-policy",
      "max-bundle");

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ANSWER_CREATED, t->state);

  test_webrtc_free (t);
}

GST_END_TEST;

GST_START_TEST (test_bundle_audio_video_max_compat_max_bundle)
{
  struct test_webrtc *t = create_audio_video_test ();
  const gchar *bundle[] = { "audio0", "video1", NULL };
  const gchar *bundle_only[] = { NULL };
  /* We set a max-compat policy on the offering webrtcbin,
   * this means that all the offered medias should be part
   * of the group:BUNDLE attribute, and they should *not* be marked
   * as bundle-only
   */
  BundleCheckData offer_data = {
    2,
    2,
    bundle,
    bundle_only,
  };
  /* We set a max-bundle policy on the answering webrtcbin,
   * this means that all the offered medias should be part
   * of the group:BUNDLE attribute, but need not be marked
   * as bundle-only.
   */
  BundleCheckData answer_data = {
    2,
    2,
    bundle,
    bundle_only,
  };
  struct validate_sdp offer = { _check_bundled_sdp_media, &offer_data };
  struct validate_sdp answer = { _check_bundled_sdp_media, &answer_data };

  gst_util_set_object_arg (G_OBJECT (t->webrtc1), "bundle-policy",
      "max-compat");
  gst_util_set_object_arg (G_OBJECT (t->webrtc2), "bundle-policy",
      "max-bundle");

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ANSWER_CREATED, t->state);

  test_webrtc_free (t);
}

GST_END_TEST;

GST_START_TEST (test_bundle_audio_video_max_bundle_none)
{
  struct test_webrtc *t = create_audio_video_test ();
  const gchar *offer_bundle[] = { "audio0", "video1", NULL };
  const gchar *offer_bundle_only[] = { "video1", NULL };
  const gchar *answer_bundle[] = { NULL };
  const gchar *answer_bundle_only[] = { NULL };
  /* We set a max-bundle policy on the offering webrtcbin,
   * this means that all the offered medias should be part
   * of the group:BUNDLE attribute, and they should be marked
   * as bundle-only
   */
  BundleCheckData offer_data = {
    2,
    1,
    offer_bundle,
    offer_bundle_only,
  };
  /* We set a none policy on the answering webrtcbin,
   * this means that the answer should contain no bundled
   * medias, and as the bundle-policy of the offering webrtcbin
   * is set to max-bundle, only one media should be active.
   */
  BundleCheckData answer_data = {
    2,
    1,
    answer_bundle,
    answer_bundle_only,
  };
  struct validate_sdp offer = { _check_bundled_sdp_media, &offer_data };
  struct validate_sdp answer = { _check_bundled_sdp_media, &answer_data };

  gst_util_set_object_arg (G_OBJECT (t->webrtc1), "bundle-policy",
      "max-bundle");
  gst_util_set_object_arg (G_OBJECT (t->webrtc2), "bundle-policy", "none");

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ANSWER_CREATED, t->state);

  test_webrtc_free (t);
}

GST_END_TEST;

GST_START_TEST (test_bundle_audio_video_data)
{
  struct test_webrtc *t = create_audio_video_test ();
  const gchar *bundle[] = { "audio0", "video1", "application2", NULL };
  const gchar *offer_bundle_only[] = { "video1", "application2", NULL };
  const gchar *answer_bundle_only[] = { NULL };
  GObject *channel = NULL;
  /* We set a max-bundle policy on the offering webrtcbin,
   * this means that all the offered medias should be part
   * of the group:BUNDLE attribute, and they should be marked
   * as bundle-only
   */
  BundleCheckData offer_data = {
    3,
    1,
    bundle,
    offer_bundle_only,
  };
  /* We also set a max-bundle policy on the answering webrtcbin,
   * this means that all the offered medias should be part
   * of the group:BUNDLE attribute, but need not be marked
   * as bundle-only.
   */
  BundleCheckData answer_data = {
    3,
    3,
    bundle,
    answer_bundle_only,
  };
  struct validate_sdp offer = { _check_bundled_sdp_media, &offer_data };
  struct validate_sdp answer = { _check_bundled_sdp_media, &answer_data };

  gst_util_set_object_arg (G_OBJECT (t->webrtc1), "bundle-policy",
      "max-bundle");
  gst_util_set_object_arg (G_OBJECT (t->webrtc2), "bundle-policy",
      "max-bundle");

  t->on_negotiation_needed = NULL;
  t->offer_data = &offer;
  t->on_offer_created = validate_sdp;
  t->answer_data = &answer;
  t->on_answer_created = validate_sdp;
  t->on_ice_candidate = NULL;

  fail_if (gst_element_set_state (t->webrtc1,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);
  fail_if (gst_element_set_state (t->webrtc2,
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  g_signal_emit_by_name (t->webrtc1, "create-data-channel", "label", NULL,
      &channel);

  test_webrtc_create_offer (t, t->webrtc1);

  test_webrtc_wait_for_answer_error_eos (t);
  fail_unless_equals_int (STATE_ANSWER_CREATED, t->state);

  g_object_unref (channel);
  test_webrtc_free (t);
}

GST_END_TEST;

static Suite *
webrtcbin_suite (void)
{
  Suite *s = suite_create ("webrtcbin");
  TCase *tc = tcase_create ("general");
  GstPluginFeature *nicesrc, *nicesink, *dtlssrtpdec, *dtlssrtpenc;
  GstPluginFeature *sctpenc, *sctpdec;
  GstRegistry *registry;

  registry = gst_registry_get ();
  nicesrc = gst_registry_lookup_feature (registry, "nicesrc");
  nicesink = gst_registry_lookup_feature (registry, "nicesink");
  dtlssrtpenc = gst_registry_lookup_feature (registry, "dtlssrtpenc");
  dtlssrtpdec = gst_registry_lookup_feature (registry, "dtlssrtpdec");
  sctpenc = gst_registry_lookup_feature (registry, "sctpenc");
  sctpdec = gst_registry_lookup_feature (registry, "sctpdec");

  tcase_add_test (tc, test_sdp_no_media);
  tcase_add_test (tc, test_no_nice_elements_request_pad);
  tcase_add_test (tc, test_no_nice_elements_state_change);
  tcase_add_test (tc, test_session_stats);
  if (nicesrc && nicesink && dtlssrtpenc && dtlssrtpdec) {
    tcase_add_test (tc, test_audio);
    tcase_add_test (tc, test_audio_video);
    tcase_add_test (tc, test_media_direction);
    tcase_add_test (tc, test_media_setup);
    tcase_add_test (tc, test_add_transceiver);
    tcase_add_test (tc, test_get_transceivers);
    tcase_add_test (tc, test_add_recvonly_transceiver);
    tcase_add_test (tc, test_recvonly_sendonly);
    tcase_add_test (tc, test_payload_types);
    tcase_add_test (tc, test_bundle_audio_video_max_bundle_max_bundle);
    tcase_add_test (tc, test_bundle_audio_video_max_bundle_none);
    tcase_add_test (tc, test_bundle_audio_video_max_compat_max_bundle);
    if (sctpenc && sctpdec) {
      tcase_add_test (tc, test_data_channel_create);
      tcase_add_test (tc, test_data_channel_remote_notify);
      tcase_add_test (tc, test_data_channel_transfer_string);
      tcase_add_test (tc, test_data_channel_transfer_data);
      tcase_add_test (tc, test_data_channel_create_after_negotiate);
      tcase_add_test (tc, test_data_channel_low_threshold);
      tcase_add_test (tc, test_data_channel_max_message_size);
      tcase_add_test (tc, test_data_channel_pre_negotiated);
      tcase_add_test (tc, test_bundle_audio_video_data);
    } else {
      GST_WARNING ("Some required elements were not found. "
          "All datachannel are disabled. sctpenc %p, sctpdec %p", sctpenc,
          sctpdec);
    }
  } else {
    GST_WARNING ("Some required elements were not found. "
        "All media tests are disabled. nicesrc %p, nicesink %p, "
        "dtlssrtpenc %p, dtlssrtpdec %p", nicesrc, nicesink, dtlssrtpenc,
        dtlssrtpdec);
  }

  if (nicesrc)
    gst_object_unref (nicesrc);
  if (nicesink)
    gst_object_unref (nicesink);
  if (dtlssrtpdec)
    gst_object_unref (dtlssrtpdec);
  if (dtlssrtpenc)
    gst_object_unref (dtlssrtpenc);
  if (sctpenc)
    gst_object_unref (sctpenc);
  if (sctpdec)
    gst_object_unref (sctpdec);

  suite_add_tcase (s, tc);

  return s;
}

GST_CHECK_MAIN (webrtcbin);
