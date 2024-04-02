/*
 * Demo gstreamer app for negotiating and streaming a sendrecv audio-only webrtc
 * stream to all the peers in a multiparty room.
 *
 * gcc mp-webrtc-sendrecv.c $(pkg-config --cflags --libs gstreamer-webrtc-1.0 gstreamer-sdp-1.0 libsoup-2.4 json-glib-1.0) -o mp-webrtc-sendrecv
 *
 * Author: Nirbheek Chauhan <nirbheek@centricular.com>
 */
#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>

/* For signalling */
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include <string.h>

enum AppState
{
  APP_STATE_UNKNOWN = 0,
  APP_STATE_ERROR = 1,          /* generic error */
  SERVER_CONNECTING = 1000,
  SERVER_CONNECTION_ERROR,
  SERVER_CONNECTED,             /* Ready to register */
  SERVER_REGISTERING = 2000,
  SERVER_REGISTRATION_ERROR,
  SERVER_REGISTERED,            /* Ready to call a peer */
  SERVER_CLOSED,                /* server connection closed by us or the server */
  ROOM_JOINING = 3000,
  ROOM_JOIN_ERROR,
  ROOM_JOINED,
  ROOM_CALL_NEGOTIATING = 4000, /* negotiating with some or all peers */
  ROOM_CALL_OFFERING,           /* when we're the one sending the offer */
  ROOM_CALL_ANSWERING,          /* when we're the one answering an offer */
  ROOM_CALL_STARTED,            /* in a call with some or all peers */
  ROOM_CALL_STOPPING,
  ROOM_CALL_STOPPED,
  ROOM_CALL_ERROR,
};

static GMainLoop *loop;
static GstElement *pipeline;
static GList *peers;

static SoupWebsocketConnection *ws_conn = NULL;
static enum AppState app_state = 0;
static const gchar *default_server_url = "wss://webrtc.gstreamer.net:8443";
static gchar *server_url = NULL;
static gchar *local_id = NULL;
static gchar *room_id = NULL;
static gboolean strict_ssl = TRUE;

static GOptionEntry entries[] = {
  {"name", 0, 0, G_OPTION_ARG_STRING, &local_id,
      "Name we will send to the server", "ID"},
  {"room-id", 0, 0, G_OPTION_ARG_STRING, &room_id,
      "Room name to join or create", "ID"},
  {"server", 0, 0, G_OPTION_ARG_STRING, &server_url,
      "Signalling server to connect to", "URL"},
  {NULL}
};

static gint
compare_str_glist (gconstpointer a, gconstpointer b)
{
  return g_strcmp0 (a, b);
}

static const gchar *
find_peer_from_list (const gchar * peer_id)
{
  return (g_list_find_custom (peers, peer_id, compare_str_glist))->data;
}

static gboolean
cleanup_and_quit_loop (const gchar * msg, enum AppState state)
{
  if (msg)
    gst_printerr ("%s\n", msg);
  if (state > 0)
    app_state = state;

  if (ws_conn) {
    if (soup_websocket_connection_get_state (ws_conn) ==
        SOUP_WEBSOCKET_STATE_OPEN)
      /* This will call us again */
      soup_websocket_connection_close (ws_conn, 1000, "");
    else
      g_object_unref (ws_conn);
  }

  if (loop) {
    g_main_loop_quit (loop);
    loop = NULL;
  }

  /* To allow usage as a GSourceFunc */
  return G_SOURCE_REMOVE;
}

static gchar *
get_string_from_json_object (JsonObject * object)
{
  JsonNode *root;
  JsonGenerator *generator;
  gchar *text;

  /* Make it the root node */
  root = json_node_init_object (json_node_alloc (), object);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);
  text = json_generator_to_data (generator, NULL);

  /* Release everything */
  g_object_unref (generator);
  json_node_free (root);
  return text;
}

static gboolean
bus_watch_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GstPipeline *pipeline = user_data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      gst_message_parse_error (message, &error, &debug);
      g_error ("Error on bus: %s (debug: %s)", error->message, debug);
      g_error_free (error);
      g_free (debug);
      break;
    }
    case GST_MESSAGE_WARNING:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      gst_message_parse_warning (message, &error, &debug);
      g_warning ("Warning on bus: %s (debug: %s)", error->message, debug);
      g_error_free (error);
      g_free (debug);
      break;
    }
    case GST_MESSAGE_LATENCY:
      gst_bin_recalculate_latency (GST_BIN (pipeline));
      break;
    default:
      break;
  }

  return G_SOURCE_CONTINUE;
}

static void
handle_media_stream (GstPad * pad, GstElement * pipe, const char *convert_name,
    const char *sink_name)
{
  GstPad *qpad;
  GstElement *q, *conv, *sink;
  GstPadLinkReturn ret;

  q = gst_element_factory_make ("queue", NULL);
  g_assert_nonnull (q);
  conv = gst_element_factory_make (convert_name, NULL);
  g_assert_nonnull (conv);
  sink = gst_element_factory_make (sink_name, NULL);
  g_assert_nonnull (sink);
  gst_bin_add_many (GST_BIN (pipe), q, conv, sink, NULL);
  gst_element_sync_state_with_parent (q);
  gst_element_sync_state_with_parent (conv);
  gst_element_sync_state_with_parent (sink);
  gst_element_link_many (q, conv, sink, NULL);

  qpad = gst_element_get_static_pad (q, "sink");

  ret = gst_pad_link (pad, qpad);
  g_assert_cmpint (ret, ==, GST_PAD_LINK_OK);
}

static void
on_incoming_decodebin_stream (GstElement * decodebin, GstPad * pad,
    GstElement * pipe)
{
  GstCaps *caps;
  const gchar *name;

  if (!gst_pad_has_current_caps (pad)) {
    gst_printerr ("Pad '%s' has no caps, can't do anything, ignoring\n",
        GST_PAD_NAME (pad));
    return;
  }

  caps = gst_pad_get_current_caps (pad);
  name = gst_structure_get_name (gst_caps_get_structure (caps, 0));

  if (g_str_has_prefix (name, "video")) {
    handle_media_stream (pad, pipe, "videoconvert", "autovideosink");
  } else if (g_str_has_prefix (name, "audio")) {
    handle_media_stream (pad, pipe, "audioconvert", "autoaudiosink");
  } else {
    gst_printerr ("Unknown pad %s, ignoring", GST_PAD_NAME (pad));
  }
}

static void
on_incoming_stream (GstElement * webrtc, GstPad * pad, GstElement * pipe)
{
  GstElement *decodebin;
  GstPad *sinkpad;

  if (GST_PAD_DIRECTION (pad) != GST_PAD_SRC)
    return;

  decodebin = gst_element_factory_make ("decodebin", NULL);
  g_signal_connect (decodebin, "pad-added",
      G_CALLBACK (on_incoming_decodebin_stream), pipe);
  gst_bin_add (GST_BIN (pipe), decodebin);
  gst_element_sync_state_with_parent (decodebin);

  sinkpad = gst_element_get_static_pad (decodebin, "sink");
  gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);
}

static void
send_room_peer_msg (const gchar * text, const gchar * peer_id)
{
  gchar *msg;

  msg = g_strdup_printf ("ROOM_PEER_MSG %s %s", peer_id, text);
  soup_websocket_connection_send_text (ws_conn, msg);
  g_free (msg);
}

static void
send_ice_candidate_message (GstElement * webrtc G_GNUC_UNUSED, guint mlineindex,
    gchar * candidate, const gchar * peer_id)
{
  gchar *text;
  JsonObject *ice, *msg;

  if (app_state < ROOM_CALL_OFFERING) {
    cleanup_and_quit_loop ("Can't send ICE, not in call", APP_STATE_ERROR);
    return;
  }

  ice = json_object_new ();
  json_object_set_string_member (ice, "candidate", candidate);
  json_object_set_int_member (ice, "sdpMLineIndex", mlineindex);
  msg = json_object_new ();
  json_object_set_object_member (msg, "ice", ice);
  text = get_string_from_json_object (msg);
  json_object_unref (msg);

  send_room_peer_msg (text, peer_id);
  g_free (text);
}

static void
send_room_peer_sdp (GstWebRTCSessionDescription * desc, const gchar * peer_id)
{
  JsonObject *msg, *sdp;
  gchar *text, *sdptype, *sdptext;

  g_assert_cmpint (app_state, >=, ROOM_CALL_OFFERING);

  if (desc->type == GST_WEBRTC_SDP_TYPE_OFFER)
    sdptype = "offer";
  else if (desc->type == GST_WEBRTC_SDP_TYPE_ANSWER)
    sdptype = "answer";
  else
    g_assert_not_reached ();

  text = gst_sdp_message_as_text (desc->sdp);
  gst_print ("Sending sdp %s to %s:\n%s\n", sdptype, peer_id, text);

  sdp = json_object_new ();
  json_object_set_string_member (sdp, "type", sdptype);
  json_object_set_string_member (sdp, "sdp", text);
  g_free (text);

  msg = json_object_new ();
  json_object_set_object_member (msg, "sdp", sdp);
  sdptext = get_string_from_json_object (msg);
  json_object_unref (msg);

  send_room_peer_msg (sdptext, peer_id);
  g_free (sdptext);
}

/* Offer created by our pipeline, to be sent to the peer */
static void
on_offer_created (GstPromise * promise, const gchar * peer_id)
{
  GstElement *webrtc;
  GstWebRTCSessionDescription *offer;
  const GstStructure *reply;

  g_assert_cmpint (app_state, ==, ROOM_CALL_OFFERING);

  g_assert_cmpint (gst_promise_wait (promise), ==, GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "offer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
  gst_promise_unref (promise);

  promise = gst_promise_new ();
  webrtc = gst_bin_get_by_name (GST_BIN (pipeline), peer_id);
  g_assert_nonnull (webrtc);
  g_signal_emit_by_name (webrtc, "set-local-description", offer, promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  /* Send offer to peer */
  send_room_peer_sdp (offer, peer_id);
  gst_webrtc_session_description_free (offer);
  gst_object_unref (webrtc);
}

static void
on_negotiation_needed (GstElement * webrtc, const gchar * peer_id)
{
  GstPromise *promise;

  app_state = ROOM_CALL_OFFERING;
  promise = gst_promise_new_with_change_func (
      (GstPromiseChangeFunc) on_offer_created, (gpointer) peer_id, NULL);
  g_signal_emit_by_name (webrtc, "create-offer", NULL, promise);
}

static void
remove_peer_from_pipeline (const gchar * peer_id)
{
  gchar *qname;
  GstPad *srcpad, *sinkpad;
  GstElement *webrtc, *q, *tee;

  webrtc = gst_bin_get_by_name (GST_BIN (pipeline), peer_id);
  if (!webrtc)
    return;

  gst_bin_remove (GST_BIN (pipeline), webrtc);
  gst_element_set_state (GST_ELEMENT (webrtc), GST_STATE_NULL);
  gst_object_unref (webrtc);

  qname = g_strdup_printf ("queue-%s", peer_id);
  q = gst_bin_get_by_name (GST_BIN (pipeline), qname);
  g_free (qname);

  sinkpad = gst_element_get_static_pad (q, "sink");
  g_assert_nonnull (sinkpad);
  srcpad = gst_pad_get_peer (sinkpad);
  g_assert_nonnull (srcpad);
  gst_object_unref (sinkpad);

  gst_bin_remove (GST_BIN (pipeline), q);
  gst_element_set_state (GST_ELEMENT (q), GST_STATE_NULL);
  gst_object_unref (q);

  tee = gst_bin_get_by_name (GST_BIN (pipeline), "audiotee");
  g_assert_nonnull (tee);
  gst_element_release_request_pad (tee, srcpad);
  gst_object_unref (srcpad);
  gst_object_unref (tee);
}

static void
add_peer_to_pipeline (const gchar * peer_id, gboolean offer)
{
  int ret;
  gchar *tmp;
  GstElement *tee, *webrtc, *q;
  GstPad *srcpad, *sinkpad;

  tmp = g_strdup_printf ("queue-%s", peer_id);
  q = gst_element_factory_make ("queue", tmp);
  g_free (tmp);
  webrtc = gst_element_factory_make ("webrtcbin", peer_id);

  gst_bin_add_many (GST_BIN (pipeline), q, webrtc, NULL);

  srcpad = gst_element_get_static_pad (q, "src");
  g_assert_nonnull (srcpad);
  sinkpad = gst_element_request_pad_simple (webrtc, "sink_%u");
  g_assert_nonnull (sinkpad);
  ret = gst_pad_link (srcpad, sinkpad);
  g_assert_cmpint (ret, ==, GST_PAD_LINK_OK);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  tee = gst_bin_get_by_name (GST_BIN (pipeline), "audiotee");
  g_assert_nonnull (tee);
  srcpad = gst_element_request_pad_simple (tee, "src_%u");
  g_assert_nonnull (srcpad);
  gst_object_unref (tee);
  sinkpad = gst_element_get_static_pad (q, "sink");
  g_assert_nonnull (sinkpad);
  ret = gst_pad_link (srcpad, sinkpad);
  g_assert_cmpint (ret, ==, GST_PAD_LINK_OK);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* This is the gstwebrtc entry point where we create the offer and so on. It
   * will be called when the pipeline goes to PLAYING.
   * XXX: We must connect this after webrtcbin has been linked to a source via
   * get_request_pad() and before we go from NULL->READY otherwise webrtcbin
   * will create an SDP offer with no media lines in it. */
  if (offer)
    g_signal_connect (webrtc, "on-negotiation-needed",
        G_CALLBACK (on_negotiation_needed), (gpointer) peer_id);

  /* We need to transmit this ICE candidate to the browser via the websockets
   * signalling server. Incoming ice candidates from the browser need to be
   * added by us too, see on_server_message() */
  g_signal_connect (webrtc, "on-ice-candidate",
      G_CALLBACK (send_ice_candidate_message), (gpointer) peer_id);
  /* Incoming streams will be exposed via this signal */
  g_signal_connect (webrtc, "pad-added", G_CALLBACK (on_incoming_stream),
      pipeline);

  /* Set to pipeline branch to PLAYING */
  ret = gst_element_sync_state_with_parent (q);
  g_assert_true (ret);
  ret = gst_element_sync_state_with_parent (webrtc);
  g_assert_true (ret);
}

static void
call_peer (const gchar * peer_id)
{
  add_peer_to_pipeline (peer_id, TRUE);
}

static void
incoming_call_from_peer (const gchar * peer_id)
{
  add_peer_to_pipeline (peer_id, FALSE);
}

#define STR(x) #x
#define RTP_CAPS_OPUS(x) "application/x-rtp,media=audio,encoding-name=OPUS,payload=" STR(x)

static gboolean
start_pipeline (void)
{
  GstStateChangeReturn ret;
  GError *error = NULL;
  GstBus *bus = NULL;

  /* NOTE: webrtcbin currently does not support dynamic addition/removal of
   * streams, so we use a separate webrtcbin for each peer, but all of them are
   * inside the same pipeline. We start by connecting it to a fakesink so that
   * we can preroll early. */
  pipeline = gst_parse_launch ("tee name=audiotee ! queue ! fakesink "
      "audiotestsrc is-live=true wave=red-noise ! queue ! opusenc perfect-timestamp=true ! rtpopuspay ! "
      "queue ! " RTP_CAPS_OPUS (96) " ! audiotee. ", &error);

  if (error) {
    gst_printerr ("Failed to parse launch: %s\n", error->message);
    g_error_free (error);
    goto err;
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, bus_watch_cb, pipeline);
  gst_object_unref (bus);

  gst_print ("Starting pipeline, not transmitting yet\n");
  ret = gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto err;

  return TRUE;

err:
  gst_print ("State change failure\n");
  if (pipeline)
    g_clear_object (&pipeline);
  return FALSE;
}

static gboolean
join_room_on_server (void)
{
  gchar *msg;

  if (soup_websocket_connection_get_state (ws_conn) !=
      SOUP_WEBSOCKET_STATE_OPEN)
    return FALSE;

  if (!room_id)
    return FALSE;

  gst_print ("Joining room %s\n", room_id);
  app_state = ROOM_JOINING;
  msg = g_strdup_printf ("ROOM %s", room_id);
  soup_websocket_connection_send_text (ws_conn, msg);
  g_free (msg);
  return TRUE;
}

static gboolean
register_with_server (void)
{
  gchar *hello;

  if (soup_websocket_connection_get_state (ws_conn) !=
      SOUP_WEBSOCKET_STATE_OPEN)
    return FALSE;

  gst_print ("Registering id %s with server\n", local_id);
  app_state = SERVER_REGISTERING;

  /* Register with the server with a random integer id. Reply will be received
   * by on_server_message() */
  hello = g_strdup_printf ("HELLO %s", local_id);
  soup_websocket_connection_send_text (ws_conn, hello);
  g_free (hello);

  return TRUE;
}

static void
on_server_closed (SoupWebsocketConnection * conn G_GNUC_UNUSED,
    gpointer user_data G_GNUC_UNUSED)
{
  app_state = SERVER_CLOSED;
  cleanup_and_quit_loop ("Server connection closed", 0);
}

static gboolean
do_registration (void)
{
  if (app_state != SERVER_REGISTERING) {
    cleanup_and_quit_loop ("ERROR: Received HELLO when not registering",
        APP_STATE_ERROR);
    return FALSE;
  }
  app_state = SERVER_REGISTERED;
  gst_print ("Registered with server\n");
  /* Ask signalling server that we want to join a room */
  if (!join_room_on_server ()) {
    cleanup_and_quit_loop ("ERROR: Failed to join room", ROOM_CALL_ERROR);
    return FALSE;
  }
  return TRUE;
}

/*
 * When we join a room, we are responsible for calling by starting negotiation
 * with each peer in it by sending an SDP offer and ICE candidates.
 */
static void
do_join_room (const gchar * text)
{
  gint ii, len;
  gchar **peer_ids;

  if (app_state != ROOM_JOINING) {
    cleanup_and_quit_loop ("ERROR: Received ROOM_OK when not calling",
        ROOM_JOIN_ERROR);
    return;
  }

  app_state = ROOM_JOINED;
  gst_print ("Room joined\n");
  /* Start recording, but not transmitting */
  if (!start_pipeline ()) {
    cleanup_and_quit_loop ("ERROR: Failed to start pipeline", ROOM_CALL_ERROR);
    return;
  }

  peer_ids = g_strsplit (text, " ", -1);
  g_assert_cmpstr (peer_ids[0], ==, "ROOM_OK");
  len = g_strv_length (peer_ids);
  /* There are peers in the room already. We need to start negotiation
   * (exchange SDP and ICE candidates) and transmission of media. */
  if (len > 1 && strlen (peer_ids[1]) > 0) {
    gst_print ("Found %i peers already in room\n", len - 1);
    app_state = ROOM_CALL_OFFERING;
    for (ii = 1; ii < len; ii++) {
      gchar *peer_id = g_strdup (peer_ids[ii]);
      gst_print ("Negotiating with peer %s\n", peer_id);
      /* This might fail asynchronously */
      call_peer (peer_id);
      peers = g_list_prepend (peers, peer_id);
    }
  }

  g_strfreev (peer_ids);
  return;
}

static void
handle_error_message (const gchar * msg)
{
  switch (app_state) {
    case SERVER_CONNECTING:
      app_state = SERVER_CONNECTION_ERROR;
      break;
    case SERVER_REGISTERING:
      app_state = SERVER_REGISTRATION_ERROR;
      break;
    case ROOM_JOINING:
      app_state = ROOM_JOIN_ERROR;
      break;
    case ROOM_JOINED:
    case ROOM_CALL_NEGOTIATING:
    case ROOM_CALL_OFFERING:
    case ROOM_CALL_ANSWERING:
      app_state = ROOM_CALL_ERROR;
      break;
    case ROOM_CALL_STARTED:
    case ROOM_CALL_STOPPING:
    case ROOM_CALL_STOPPED:
      app_state = ROOM_CALL_ERROR;
      break;
    default:
      app_state = APP_STATE_ERROR;
  }
  cleanup_and_quit_loop (msg, 0);
}

static void
on_answer_created (GstPromise * promise, const gchar * peer_id)
{
  GstElement *webrtc;
  GstWebRTCSessionDescription *answer;
  const GstStructure *reply;

  g_assert_cmpint (app_state, ==, ROOM_CALL_ANSWERING);

  g_assert_cmpint (gst_promise_wait (promise), ==, GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "answer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL);
  gst_promise_unref (promise);

  promise = gst_promise_new ();
  webrtc = gst_bin_get_by_name (GST_BIN (pipeline), peer_id);
  g_assert_nonnull (webrtc);
  g_signal_emit_by_name (webrtc, "set-local-description", answer, promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  /* Send offer to peer */
  send_room_peer_sdp (answer, peer_id);
  gst_webrtc_session_description_free (answer);
  gst_object_unref (webrtc);

  app_state = ROOM_CALL_STARTED;
}

static void
handle_sdp_offer (const gchar * peer_id, const gchar * text)
{
  int ret;
  GstPromise *promise;
  GstElement *webrtc;
  GstSDPMessage *sdp;
  GstWebRTCSessionDescription *offer;

  g_assert_cmpint (app_state, ==, ROOM_CALL_ANSWERING);

  gst_print ("Received offer:\n%s\n", text);

  ret = gst_sdp_message_new (&sdp);
  g_assert_cmpint (ret, ==, GST_SDP_OK);

  ret = gst_sdp_message_parse_buffer ((guint8 *) text, strlen (text), sdp);
  g_assert_cmpint (ret, ==, GST_SDP_OK);

  offer = gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_OFFER, sdp);
  g_assert_nonnull (offer);

  /* Set remote description on our pipeline */
  promise = gst_promise_new ();
  webrtc = gst_bin_get_by_name (GST_BIN (pipeline), peer_id);
  g_assert_nonnull (webrtc);
  g_signal_emit_by_name (webrtc, "set-remote-description", offer, promise);
  /* We don't want to be notified when the action is done */
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  /* Create an answer that we will send back to the peer */
  promise = gst_promise_new_with_change_func (
      (GstPromiseChangeFunc) on_answer_created, (gpointer) peer_id, NULL);
  g_signal_emit_by_name (webrtc, "create-answer", NULL, promise);

  gst_webrtc_session_description_free (offer);
  gst_object_unref (webrtc);
}

static void
handle_sdp_answer (const gchar * peer_id, const gchar * text)
{
  int ret;
  GstPromise *promise;
  GstElement *webrtc;
  GstSDPMessage *sdp;
  GstWebRTCSessionDescription *answer;

  g_assert_cmpint (app_state, >=, ROOM_CALL_OFFERING);

  gst_print ("Received answer:\n%s\n", text);

  ret = gst_sdp_message_new (&sdp);
  g_assert_cmpint (ret, ==, GST_SDP_OK);

  ret = gst_sdp_message_parse_buffer ((guint8 *) text, strlen (text), sdp);
  g_assert_cmpint (ret, ==, GST_SDP_OK);

  answer = gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_ANSWER, sdp);
  g_assert_nonnull (answer);

  /* Set remote description on our pipeline */
  promise = gst_promise_new ();
  webrtc = gst_bin_get_by_name (GST_BIN (pipeline), peer_id);
  g_assert_nonnull (webrtc);
  g_signal_emit_by_name (webrtc, "set-remote-description", answer, promise);
  gst_object_unref (webrtc);
  /* We don't want to be notified when the action is done */
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);
}

static gboolean
handle_peer_message (const gchar * peer_id, const gchar * msg)
{
  JsonNode *root;
  JsonObject *object, *child;
  JsonParser *parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, msg, -1, NULL)) {
    gst_printerr ("Unknown message '%s' from '%s', ignoring", msg, peer_id);
    g_object_unref (parser);
    return FALSE;
  }

  root = json_parser_get_root (parser);
  if (!JSON_NODE_HOLDS_OBJECT (root)) {
    gst_printerr ("Unknown json message '%s' from '%s', ignoring", msg,
        peer_id);
    g_object_unref (parser);
    return FALSE;
  }

  gst_print ("Message from peer %s: %s\n", peer_id, msg);

  object = json_node_get_object (root);
  /* Check type of JSON message */
  if (json_object_has_member (object, "sdp")) {
    const gchar *text, *sdp_type;

    g_assert_cmpint (app_state, >=, ROOM_JOINED);

    child = json_object_get_object_member (object, "sdp");

    if (!json_object_has_member (child, "type")) {
      cleanup_and_quit_loop ("ERROR: received SDP without 'type'",
          ROOM_CALL_ERROR);
      return FALSE;
    }

    sdp_type = json_object_get_string_member (child, "type");
    text = json_object_get_string_member (child, "sdp");

    if (g_strcmp0 (sdp_type, "offer") == 0) {
      app_state = ROOM_CALL_ANSWERING;
      incoming_call_from_peer (peer_id);
      handle_sdp_offer (peer_id, text);
    } else if (g_strcmp0 (sdp_type, "answer") == 0) {
      g_assert_cmpint (app_state, >=, ROOM_CALL_OFFERING);
      handle_sdp_answer (peer_id, text);
      app_state = ROOM_CALL_STARTED;
    } else {
      cleanup_and_quit_loop ("ERROR: invalid sdp_type", ROOM_CALL_ERROR);
      return FALSE;
    }
  } else if (json_object_has_member (object, "ice")) {
    GstElement *webrtc;
    const gchar *candidate;
    gint sdpmlineindex;

    child = json_object_get_object_member (object, "ice");
    candidate = json_object_get_string_member (child, "candidate");
    sdpmlineindex = json_object_get_int_member (child, "sdpMLineIndex");

    /* Add ice candidate sent by remote peer */
    webrtc = gst_bin_get_by_name (GST_BIN (pipeline), peer_id);
    g_assert_nonnull (webrtc);
    g_signal_emit_by_name (webrtc, "add-ice-candidate", sdpmlineindex,
        candidate);
    gst_object_unref (webrtc);
  } else {
    gst_printerr ("Ignoring unknown JSON message:\n%s\n", msg);
  }
  g_object_unref (parser);
  return TRUE;
}

/* One mega message handler for our asynchronous calling mechanism */
static void
on_server_message (SoupWebsocketConnection * conn, SoupWebsocketDataType type,
    GBytes * message, gpointer user_data)
{
  gchar *text;

  switch (type) {
    case SOUP_WEBSOCKET_DATA_BINARY:
      gst_printerr ("Received unknown binary message, ignoring\n");
      return;
    case SOUP_WEBSOCKET_DATA_TEXT:{
      gsize size;
      const gchar *data = g_bytes_get_data (message, &size);
      /* Convert to NULL-terminated string */
      text = g_strndup (data, size);
      break;
    }
    default:
      g_assert_not_reached ();
  }

  /* Server has accepted our registration, we are ready to send commands */
  if (g_strcmp0 (text, "HELLO") == 0) {
    /* May fail asynchronously */
    do_registration ();
    /* Room-related message */
  } else if (g_str_has_prefix (text, "ROOM_")) {
    /* Room joined, now we can start negotiation */
    if (g_str_has_prefix (text, "ROOM_OK ")) {
      /* May fail asynchronously */
      do_join_room (text);
    } else if (g_str_has_prefix (text, "ROOM_PEER")) {
      gchar **splitm = NULL;
      const gchar *peer_id;
      /* SDP and ICE, usually */
      if (g_str_has_prefix (text, "ROOM_PEER_MSG")) {
        splitm = g_strsplit (text, " ", 3);
        peer_id = find_peer_from_list (splitm[1]);
        g_assert_nonnull (peer_id);
        /* Could be an offer or an answer, or ICE, or an arbitrary message */
        handle_peer_message (peer_id, splitm[2]);
      } else if (g_str_has_prefix (text, "ROOM_PEER_JOINED")) {
        splitm = g_strsplit (text, " ", 2);
        peers = g_list_prepend (peers, g_strdup (splitm[1]));
        peer_id = find_peer_from_list (splitm[1]);
        g_assert_nonnull (peer_id);
        gst_print ("Peer %s has joined the room\n", peer_id);
      } else if (g_str_has_prefix (text, "ROOM_PEER_LEFT")) {
        splitm = g_strsplit (text, " ", 2);
        peer_id = find_peer_from_list (splitm[1]);
        g_assert_nonnull (peer_id);
        peers = g_list_remove (peers, peer_id);
        gst_print ("Peer %s has left the room\n", peer_id);
        remove_peer_from_pipeline (peer_id);
        g_free ((gchar *) peer_id);
        /* TODO: cleanup pipeline */
      } else {
        gst_printerr ("WARNING: Ignoring unknown message %s\n", text);
      }
      g_strfreev (splitm);
    } else {
      goto err;
    }
    /* Handle errors */
  } else if (g_str_has_prefix (text, "ERROR")) {
    handle_error_message (text);
  } else {
    goto err;
  }

out:
  g_free (text);
  return;

err:
  {
    gchar *err_s = g_strdup_printf ("ERROR: unknown message %s", text);
    cleanup_and_quit_loop (err_s, 0);
    g_free (err_s);
    goto out;
  }
}

static void
on_server_connected (SoupSession * session, GAsyncResult * res,
    SoupMessage * msg)
{
  GError *error = NULL;

  ws_conn = soup_session_websocket_connect_finish (session, res, &error);
  if (error) {
    cleanup_and_quit_loop (error->message, SERVER_CONNECTION_ERROR);
    g_error_free (error);
    return;
  }

  g_assert_nonnull (ws_conn);

  app_state = SERVER_CONNECTED;
  gst_print ("Connected to signalling server\n");

  g_signal_connect (ws_conn, "closed", G_CALLBACK (on_server_closed), NULL);
  g_signal_connect (ws_conn, "message", G_CALLBACK (on_server_message), NULL);

  /* Register with the server so it knows about us and can accept commands
   * responses from the server will be handled in on_server_message() above */
  register_with_server ();
}

/*
 * Connect to the signalling server. This is the entrypoint for everything else.
 */
static void
connect_to_websocket_server_async (void)
{
  SoupLogger *logger;
  SoupMessage *message;
  SoupSession *session;
  const char *https_aliases[] = { "wss", NULL };

  session = soup_session_new_with_options (SOUP_SESSION_SSL_STRICT, strict_ssl,
      SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
      //SOUP_SESSION_SSL_CA_FILE, "/etc/ssl/certs/ca-bundle.crt",
      SOUP_SESSION_HTTPS_ALIASES, https_aliases, NULL);

  logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));
  g_object_unref (logger);

  message = soup_message_new (SOUP_METHOD_GET, server_url);

  gst_print ("Connecting to server...\n");

  /* Once connected, we will register */
  soup_session_websocket_connect_async (session, message, NULL, NULL, NULL,
      (GAsyncReadyCallback) on_server_connected, message);
  app_state = SERVER_CONNECTING;
}

static gboolean
check_plugins (void)
{
  int i;
  gboolean ret;
  GstRegistry *registry;
  const gchar *needed[] = { "opus", "nice", "webrtc", "dtls", "srtp",
    "rtpmanager", "audiotestsrc", NULL
  };

  registry = gst_registry_get ();
  ret = TRUE;
  for (i = 0; i < g_strv_length ((gchar **) needed); i++) {
    GstPlugin *plugin;
    plugin = gst_registry_find_plugin (registry, needed[i]);
    if (!plugin) {
      gst_print ("Required gstreamer plugin '%s' not found\n", needed[i]);
      ret = FALSE;
      continue;
    }
    gst_object_unref (plugin);
  }
  return ret;
}

int
main (int argc, char *argv[])
{
  GOptionContext *context;
  GstBus *bus;
  GError *error = NULL;

  context = g_option_context_new ("- gstreamer webrtc sendrecv demo");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gst_init_get_option_group ());
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    gst_printerr ("Error initializing: %s\n", error->message);
    return -1;
  }

  if (!check_plugins ())
    return -1;

  if (!room_id) {
    gst_printerr ("--room-id is a required argument\n");
    return -1;
  }

  if (!local_id)
    local_id = g_strdup_printf ("%s-%i", g_get_user_name (),
        g_random_int_range (10, 10000));
  /* Sanitize by removing whitespace, modifies string in-place */
  g_strdelimit (local_id, " \t\n\r", '-');

  gst_print ("Our local id is %s\n", local_id);

  if (!server_url)
    server_url = g_strdup (default_server_url);

  /* Don't use strict ssl when running a localhost server, because
   * it's probably a test server with a self-signed certificate */
  {
    GstUri *uri = gst_uri_from_string (server_url);
    if (g_strcmp0 ("localhost", gst_uri_get_host (uri)) == 0 ||
        g_strcmp0 ("127.0.0.1", gst_uri_get_host (uri)) == 0)
      strict_ssl = FALSE;
    gst_uri_unref (uri);
  }

  loop = g_main_loop_new (NULL, FALSE);

  connect_to_websocket_server_async ();

  g_main_loop_run (loop);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_print ("Pipeline stopped\n");

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_remove_watch (bus);
  gst_object_unref (bus);

  gst_object_unref (pipeline);
  g_free (server_url);
  g_free (local_id);
  g_free (room_id);

  return 0;
}
