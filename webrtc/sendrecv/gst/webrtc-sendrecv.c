/*
 * Demo gstreamer app for negotiating and streaming a sendrecv webrtc stream
 * with a browser JS app.
 *
 * gcc webrtc-sendrecv.c $(pkg-config --cflags --libs gstreamer-webrtc-1.0 gstreamer-sdp-1.0 libsoup-2.4 json-glib-1.0) -o webrtc-sendrecv
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

enum AppState {
  APP_STATE_UNKNOWN = 0,
  APP_STATE_ERROR = 1, /* generic error */
  SERVER_CONNECTING = 1000,
  SERVER_CONNECTION_ERROR,
  SERVER_CONNECTED, /* Ready to register */
  SERVER_REGISTERING = 2000,
  SERVER_REGISTRATION_ERROR,
  SERVER_REGISTERED, /* Ready to call a peer */
  SERVER_CLOSED, /* server connection closed by us or the server */
  PEER_CONNECTING = 3000,
  PEER_CONNECTION_ERROR,
  PEER_CONNECTED,
  PEER_CALL_NEGOTIATING = 4000,
  PEER_CALL_STARTED,
  PEER_CALL_STOPPING,
  PEER_CALL_STOPPED,
  PEER_CALL_ERROR,
};

static GMainLoop *loop;
static GstElement *pipe1, *webrtc1;

static SoupWebsocketConnection *ws_conn = NULL;
static enum AppState app_state = 0;
static const gchar *peer_id = NULL;
static const gchar *server_url = "wss://webrtc.nirbheek.in:8443";

static GOptionEntry entries[] =
{
  { "peer-id", 0, 0, G_OPTION_ARG_STRING, &peer_id, "String ID of the peer to connect to", "ID" },
  { "server", 0, 0, G_OPTION_ARG_STRING, &server_url, "Signalling server to connect to", "URL" },
  { NULL },
};

static gboolean
cleanup_and_quit_loop (const gchar * msg, enum AppState state)
{
  if (msg)
    g_printerr ("%s\n", msg);
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

static gchar*
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

static void
handle_media_stream (GstPad * pad, GstElement * pipe, const char * convert_name,
    const char * sink_name)
{
  GstPad *qpad;
  GstElement *q, *conv, *sink;
  GstPadLinkReturn ret;

  g_print ("Trying to handle stream with %s ! %s", convert_name, sink_name);

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
  g_assert_cmphex (ret, ==, GST_PAD_LINK_OK);
}

static void
on_incoming_decodebin_stream (GstElement * decodebin, GstPad * pad,
    GstElement * pipe)
{
  GstCaps *caps;
  const gchar *name;

  if (!gst_pad_has_current_caps (pad)) {
    g_printerr ("Pad '%s' has no caps, can't do anything, ignoring\n",
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
    g_printerr ("Unknown pad %s, ignoring", GST_PAD_NAME (pad));
  }
}

static void
on_incoming_stream (GstElement * webrtc, GstPad * pad, GstElement * pipe)
{
  GstElement *decodebin;

  if (GST_PAD_DIRECTION (pad) != GST_PAD_SRC)
    return;

  decodebin = gst_element_factory_make ("decodebin", NULL);
  g_signal_connect (decodebin, "pad-added",
      G_CALLBACK (on_incoming_decodebin_stream), pipe);
  gst_bin_add (GST_BIN (pipe), decodebin);
  gst_element_sync_state_with_parent (decodebin);
  gst_element_link (webrtc, decodebin);
}

static void
send_ice_candidate_message (GstElement * webrtc G_GNUC_UNUSED, guint mlineindex,
    gchar * candidate, gpointer user_data G_GNUC_UNUSED)
{
  gchar *text;
  JsonObject *ice, *msg;

  if (app_state < PEER_CALL_NEGOTIATING) {
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

  soup_websocket_connection_send_text (ws_conn, text);
  g_free (text);
}

static void
send_sdp_offer (GstWebRTCSessionDescription * offer)
{
  gchar *text;
  JsonObject *msg, *sdp;

  if (app_state < PEER_CALL_NEGOTIATING) {
    cleanup_and_quit_loop ("Can't send offer, not in call", APP_STATE_ERROR);
    return;
  }

  text = gst_sdp_message_as_text (offer->sdp);
  g_print ("Sending offer:\n%s\n", text);

  sdp = json_object_new ();
  json_object_set_string_member (sdp, "type", "offer");
  json_object_set_string_member (sdp, "sdp", text);
  g_free (text);

  msg = json_object_new ();
  json_object_set_object_member (msg, "sdp", sdp);
  text = get_string_from_json_object (msg);
  json_object_unref (msg);

  soup_websocket_connection_send_text (ws_conn, text);
  g_free (text);
}

/* Offer created by our pipeline, to be sent to the peer */
static void
on_offer_created (GstPromise * promise, gpointer user_data)
{
  GstWebRTCSessionDescription *offer = NULL;
  const GstStructure *reply;
  gchar *desc;

  g_assert_cmphex (app_state, ==, PEER_CALL_NEGOTIATING);

  g_assert_cmphex (gst_promise_wait(promise), ==, GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "offer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
  gst_promise_unref (promise);

  promise = gst_promise_new ();
  g_signal_emit_by_name (webrtc1, "set-local-description", offer, promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  /* Send offer to peer */
  send_sdp_offer (offer);
  gst_webrtc_session_description_free (offer);
}

static void
on_negotiation_needed (GstElement * element, gpointer user_data)
{
  GstPromise *promise;

  app_state = PEER_CALL_NEGOTIATING;
  promise = gst_promise_new_with_change_func (on_offer_created, user_data, NULL);;
  g_signal_emit_by_name (webrtc1, "create-offer", NULL, promise);
}

#define STUN_SERVER " stun-server=stun://stun.l.google.com:19302 "
#define RTP_CAPS_OPUS "application/x-rtp,media=audio,encoding-name=OPUS,payload="
#define RTP_CAPS_VP8 "application/x-rtp,media=video,encoding-name=VP8,payload="

static gboolean
start_pipeline (void)
{
  GstStateChangeReturn ret;
  GError *error = NULL;

  pipe1 =
      gst_parse_launch ("webrtcbin name=sendrecv " STUN_SERVER
      "videotestsrc pattern=ball ! queue ! vp8enc deadline=1 ! rtpvp8pay ! "
      "queue ! " RTP_CAPS_VP8 "96 ! sendrecv. "
      "audiotestsrc wave=red-noise ! queue ! opusenc ! rtpopuspay ! "
      "queue ! " RTP_CAPS_OPUS "97 ! sendrecv. ",
      &error);

  if (error) {
    g_printerr ("Failed to parse launch: %s\n", error->message);
    g_error_free (error);
    goto err;
  }

  webrtc1 = gst_bin_get_by_name (GST_BIN (pipe1), "sendrecv");
  g_assert_nonnull (webrtc1);

  /* This is the gstwebrtc entry point where we create the offer and so on. It
   * will be called when the pipeline goes to PLAYING. */
  g_signal_connect (webrtc1, "on-negotiation-needed",
      G_CALLBACK (on_negotiation_needed), NULL);
  /* We need to transmit this ICE candidate to the browser via the websockets
   * signalling server. Incoming ice candidates from the browser need to be
   * added by us too, see on_server_message() */
  g_signal_connect (webrtc1, "on-ice-candidate",
      G_CALLBACK (send_ice_candidate_message), NULL);
  /* Incoming streams will be exposed via this signal */
  g_signal_connect (webrtc1, "pad-added", G_CALLBACK (on_incoming_stream),
      pipe1);
  /* Lifetime is the same as the pipeline itself */
  gst_object_unref (webrtc1);

  g_print ("Starting pipeline\n");
  ret = gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto err;

  return TRUE;

err:
  if (pipe1)
    g_clear_object (&pipe1);
  if (webrtc1)
    webrtc1 = NULL;
  return FALSE;
}

static gboolean
setup_call (void)
{
  gchar *msg;

  if (soup_websocket_connection_get_state (ws_conn) !=
      SOUP_WEBSOCKET_STATE_OPEN)
    return FALSE;

  if (!peer_id)
    return FALSE;

  g_print ("Setting up signalling server call with %s\n", peer_id);
  app_state = PEER_CONNECTING;
  msg = g_strdup_printf ("SESSION %s", peer_id);
  soup_websocket_connection_send_text (ws_conn, msg);
  g_free (msg);
  return TRUE;
}

static gboolean
register_with_server (void)
{
  gchar *hello;
  gint32 our_id;

  if (soup_websocket_connection_get_state (ws_conn) !=
      SOUP_WEBSOCKET_STATE_OPEN)
    return FALSE;

  our_id = g_random_int_range (10, 10000);
  g_print ("Registering id %i with server\n", our_id);
  app_state = SERVER_REGISTERING;

  /* Register with the server with a random integer id. Reply will be received
   * by on_server_message() */
  hello = g_strdup_printf ("HELLO %i", our_id);
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

/* One mega message handler for our asynchronous calling mechanism */
static void
on_server_message (SoupWebsocketConnection * conn, SoupWebsocketDataType type,
    GBytes * message, gpointer user_data)
{
  gsize size;
  gchar *text, *data;

  switch (type) {
    case SOUP_WEBSOCKET_DATA_BINARY:
      g_printerr ("Received unknown binary message, ignoring\n");
      g_bytes_unref (message);
      return;
    case SOUP_WEBSOCKET_DATA_TEXT:
      data = g_bytes_unref_to_data (message, &size);
      /* Convert to NULL-terminated string */
      text = g_strndup (data, size);
      g_free (data);
      break;
    default:
      g_assert_not_reached ();
  }

  /* Server has accepted our registration, we are ready to send commands */
  if (g_strcmp0 (text, "HELLO") == 0) {
    if (app_state != SERVER_REGISTERING) {
      cleanup_and_quit_loop ("ERROR: Received HELLO when not registering",
          APP_STATE_ERROR);
      goto out;
    }
    app_state = SERVER_REGISTERED;
    g_print ("Registered with server\n");
    /* Ask signalling server to connect us with a specific peer */
    if (!setup_call ()) {
      cleanup_and_quit_loop ("ERROR: Failed to setup call", PEER_CALL_ERROR);
      goto out;
    }
  /* Call has been setup by the server, now we can start negotiation */
  } else if (g_strcmp0 (text, "SESSION_OK") == 0) {
    if (app_state != PEER_CONNECTING) {
      cleanup_and_quit_loop ("ERROR: Received SESSION_OK when not calling",
          PEER_CONNECTION_ERROR);
      goto out;
    }

    app_state = PEER_CONNECTED;
    /* Start negotiation (exchange SDP and ICE candidates) */
    if (!start_pipeline ())
      cleanup_and_quit_loop ("ERROR: failed to start pipeline",
          PEER_CALL_ERROR);
  /* Handle errors */
  } else if (g_str_has_prefix (text, "ERROR")) {
    switch (app_state) {
      case SERVER_CONNECTING:
        app_state = SERVER_CONNECTION_ERROR;
        break;
      case SERVER_REGISTERING:
        app_state = SERVER_REGISTRATION_ERROR;
        break;
      case PEER_CONNECTING:
        app_state = PEER_CONNECTION_ERROR;
        break;
      case PEER_CONNECTED:
      case PEER_CALL_NEGOTIATING:
        app_state = PEER_CALL_ERROR;
      default:
        app_state = APP_STATE_ERROR;
    }
    cleanup_and_quit_loop (text, 0);
  /* Look for JSON messages containing SDP and ICE candidates */
  } else {
    JsonNode *root;
    JsonObject *object;
    JsonParser *parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, text, -1, NULL)) {
      g_printerr ("Unknown message '%s', ignoring", text);
      g_object_unref (parser);
      goto out;
    }

    root = json_parser_get_root (parser);
    if (!JSON_NODE_HOLDS_OBJECT (root)) {
      g_printerr ("Unknown json message '%s', ignoring", text);
      g_object_unref (parser);
      goto out;
    }

    object = json_node_get_object (root);
    /* Check type of JSON message */
    if (json_object_has_member (object, "sdp")) {
      int ret;
      const gchar *text;
      GstSDPMessage *sdp;
      GstWebRTCSessionDescription *answer;

      g_assert_cmphex (app_state, ==, PEER_CALL_NEGOTIATING);

      g_assert_true (json_object_has_member (object, "type"));
      /* In this example, we always create the offer and receive one answer.
       * See tests/examples/webrtcbidirectional.c in gst-plugins-bad for how to
       * handle offers from peers and reply with answers using webrtcbin. */
      g_assert_cmpstr (json_object_get_string_member (object, "type"), ==,
          "answer");

      text = json_object_get_string_member (object, "sdp");

      g_print ("Received answer:\n%s\n", text);

      ret = gst_sdp_message_new (&sdp);
      g_assert_cmphex (ret, ==, GST_SDP_OK);

      ret = gst_sdp_message_parse_buffer (text, strlen (text), sdp);
      g_assert_cmphex (ret, ==, GST_SDP_OK);

      answer = gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_ANSWER,
          sdp);
      g_assert_nonnull (answer);

      /* Set remote description on our pipeline */
      {
        GstPromise *promise = gst_promise_new ();
        g_signal_emit_by_name (webrtc1, "set-remote-description", answer,
            promise);
        gst_promise_interrupt (promise);
        gst_promise_unref (promise);
      }

      app_state = PEER_CALL_STARTED;
    } else if (json_object_has_member (object, "ice")) {
      JsonObject *ice;
      const gchar *candidate;
      gint sdpmlineindex;

      ice = json_object_get_object_member (object, "ice");
      candidate = json_object_get_string_member (ice, "candidate");
      sdpmlineindex = json_object_get_int_member (ice, "sdpMLineIndex");

      /* Add ice candidate sent by remote peer */
      g_signal_emit_by_name (webrtc1, "add-ice-candidate", sdpmlineindex,
          candidate);
    } else {
      g_printerr ("Ignoring unknown JSON message:\n%s\n", text);
    }
    g_object_unref (parser);
  }

out:
  g_free (text);
}

static void
on_server_connected (SoupSession * session, GAsyncResult * res,
    SoupMessage *msg)
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
  g_print ("Connected to signalling server\n");

  g_signal_connect (ws_conn, "closed", G_CALLBACK (on_server_closed), NULL);
  g_signal_connect (ws_conn, "message", G_CALLBACK (on_server_message), NULL);

  /* Register with the server so it knows about us and can accept commands */
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
  const char *https_aliases[] = {"wss", NULL};

  session = soup_session_new_with_options (SOUP_SESSION_SSL_STRICT, TRUE,
      SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
      //SOUP_SESSION_SSL_CA_FILE, "/etc/ssl/certs/ca-bundle.crt",
      SOUP_SESSION_HTTPS_ALIASES, https_aliases, NULL);

  logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));
  g_object_unref (logger);

  message = soup_message_new (SOUP_METHOD_GET, server_url);

  g_print ("Connecting to server...\n");

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
  GstPlugin *plugin;
  GstRegistry *registry;
  const gchar *needed[] = { "opus", "vpx", "nice", "webrtc", "dtls", "srtp",
      "rtpmanager", "videotestsrc", "audiotestsrc", NULL};

  registry = gst_registry_get ();
  ret = TRUE;
  for (i = 0; i < g_strv_length ((gchar **) needed); i++) {
    plugin = gst_registry_find_plugin (registry, needed[i]);
    if (!plugin) {
      g_print ("Required gstreamer plugin '%s' not found\n", needed[i]);
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
  SoupSession *session;
  GOptionContext *context;
  GError *error = NULL;

  context = g_option_context_new ("- gstreamer webrtc sendrecv demo");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gst_init_get_option_group ());
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_printerr ("Error initializing: %s\n", error->message);
    return -1;
  }

  if (!check_plugins ())
    return -1;

  if (!peer_id) {
    g_printerr ("--peer-id is a required argument\n");
    return -1;
  }

  loop = g_main_loop_new (NULL, FALSE);

  connect_to_websocket_server_async ();

  g_main_loop_run (loop);

  gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_NULL);
  g_print ("Pipeline stopped\n");

  gst_object_unref (pipe1);

  return 0;
}
