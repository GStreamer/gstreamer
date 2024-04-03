#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <gst/webrtc/webrtc.h>

#include <string.h>

static GMainLoop *loop;
static GstElement *pipe1, *webrtc1, *webrtc2;
static GstBus *bus1;

static gboolean
_bus_watch (GstBus * bus, GstMessage * msg, GstElement * pipe)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_STATE_CHANGED:
      if (GST_ELEMENT (msg->src) == pipe) {
        GstState old, new, pending;

        gst_message_parse_state_changed (msg, &old, &new, &pending);

        {
          gchar *dump_name = g_strconcat ("state_changed-",
              gst_element_state_get_name (old), "_",
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

      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipe),
          GST_DEBUG_GRAPH_SHOW_ALL, "error");

      gst_message_parse_error (msg, &err, &dbg_info);
      g_printerr ("ERROR from element %s: %s\n",
          GST_OBJECT_NAME (msg->src), err->message);
      g_printerr ("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
      g_error_free (err);
      g_free (dbg_info);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:{
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipe),
          GST_DEBUG_GRAPH_SHOW_ALL, "eos");
      g_print ("EOS received\n");
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static void
_webrtc_pad_added (GstElement * webrtc, GstPad * new_pad, GstElement * pipe)
{
  GstElement *out = NULL;
  GstPad *sink = NULL;
  GstCaps *caps;
  GstStructure *s;
  const gchar *encoding_name;

  if (GST_PAD_DIRECTION (new_pad) != GST_PAD_SRC)
    return;

  caps = gst_pad_get_current_caps (new_pad);
  if (!caps)
    caps = gst_pad_query_caps (new_pad, NULL);
  GST_ERROR_OBJECT (new_pad, "caps %" GST_PTR_FORMAT, caps);
  g_assert (gst_caps_is_fixed (caps));
  s = gst_caps_get_structure (caps, 0);
  encoding_name = gst_structure_get_string (s, "encoding-name");
  if (g_strcmp0 (encoding_name, "VP8") == 0) {
    out = gst_parse_bin_from_description ("rtpvp8depay ! vp8dec ! "
        "videoconvert ! queue ! xvimagesink sync=false", TRUE, NULL);
  } else if (g_strcmp0 (encoding_name, "OPUS") == 0) {
    out = gst_parse_bin_from_description ("rtpopusdepay ! opusdec ! "
        "audioconvert ! audioresample ! audiorate ! queue ! autoaudiosink",
        TRUE, NULL);
  } else {
    g_critical ("Unknown encoding name %s", encoding_name);
    g_assert_not_reached ();
  }
  gst_bin_add (GST_BIN (pipe), out);
  gst_element_sync_state_with_parent (out);
  sink = out->sinkpads->data;

  gst_pad_link (new_pad, sink);

  gst_caps_unref (caps);
}

static void
_on_answer_received (GstPromise * promise, gpointer user_data)
{
  GstWebRTCSessionDescription *answer = NULL;
  const GstStructure *reply;
  gchar *desc;

  g_assert (gst_promise_wait (promise) == GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "answer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
  gst_promise_unref (promise);
  desc = gst_sdp_message_as_text (answer->sdp);
  g_print ("Created answer:\n%s\n", desc);
  g_free (desc);

  g_signal_emit_by_name (webrtc1, "set-remote-description", answer, NULL);
  g_signal_emit_by_name (webrtc2, "set-local-description", answer, NULL);

  gst_webrtc_session_description_free (answer);
}

static void
_on_offer_received (GstPromise * promise, gpointer user_data)
{
  GstWebRTCSessionDescription *offer = NULL;
  const GstStructure *reply;
  gchar *desc;

  g_assert (gst_promise_wait (promise) == GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "offer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
  gst_promise_unref (promise);
  desc = gst_sdp_message_as_text (offer->sdp);
  g_print ("Created offer:\n%s\n", desc);
  g_free (desc);

  g_signal_emit_by_name (webrtc1, "set-local-description", offer, NULL);
  g_signal_emit_by_name (webrtc2, "set-remote-description", offer, NULL);

  promise = gst_promise_new_with_change_func (_on_answer_received, user_data,
      NULL);
  g_signal_emit_by_name (webrtc2, "create-answer", NULL, promise);

  gst_webrtc_session_description_free (offer);
}

static void
_on_negotiation_needed (GstElement * element, gpointer user_data)
{
  GstPromise *promise;

  promise = gst_promise_new_with_change_func (_on_offer_received, user_data,
      NULL);
  g_signal_emit_by_name (webrtc1, "create-offer", NULL, promise);
}

static void
_on_ice_candidate (GstElement * webrtc, guint mlineindex, gchar * candidate,
    GstElement * other)
{
  g_signal_emit_by_name (other, "add-ice-candidate", mlineindex, candidate);
}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);
  pipe1 =
      gst_parse_launch ("webrtcbin name=smpte webrtcbin name=ball "
      "videotestsrc pattern=smpte ! queue ! vp8enc ! rtpvp8pay ! queue ! "
      "application/x-rtp,media=video,payload=96,encoding-name=VP8 ! smpte.sink_0 "
      "audiotestsrc ! opusenc perfect-timestamp=true ! rtpopuspay ! queue ! "
      "application/x-rtp,media=audio,payload=97,encoding-name=OPUS ! smpte.sink_1 "
      "videotestsrc pattern=ball ! queue ! vp8enc ! rtpvp8pay ! queue ! "
      "application/x-rtp,media=video,payload=96,encoding-name=VP8 ! ball.sink_1 "
      "audiotestsrc wave=saw ! opusenc perfect-timestamp=true ! rtpopuspay ! queue ! "
      "application/x-rtp,media=audio,payload=97,encoding-name=OPUS ! ball.sink_0 ",
      NULL);
  bus1 = gst_pipeline_get_bus (GST_PIPELINE (pipe1));
  gst_bus_add_watch (bus1, (GstBusFunc) _bus_watch, pipe1);

  webrtc1 = gst_bin_get_by_name (GST_BIN (pipe1), "smpte");
  g_signal_connect (webrtc1, "on-negotiation-needed",
      G_CALLBACK (_on_negotiation_needed), NULL);
  g_signal_connect (webrtc1, "pad-added", G_CALLBACK (_webrtc_pad_added),
      pipe1);
  webrtc2 = gst_bin_get_by_name (GST_BIN (pipe1), "ball");
  g_signal_connect (webrtc2, "pad-added", G_CALLBACK (_webrtc_pad_added),
      pipe1);
  g_signal_connect (webrtc1, "on-ice-candidate",
      G_CALLBACK (_on_ice_candidate), webrtc2);
  g_signal_connect (webrtc2, "on-ice-candidate",
      G_CALLBACK (_on_ice_candidate), webrtc1);

  g_print ("Starting pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_PLAYING);

  g_main_loop_run (loop);

  gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_NULL);
  g_print ("Pipeline stopped\n");

  gst_object_unref (webrtc1);
  gst_object_unref (webrtc2);
  gst_bus_remove_watch (bus1);
  gst_object_unref (bus1);
  gst_object_unref (pipe1);

  gst_deinit ();

  return 0;
}
