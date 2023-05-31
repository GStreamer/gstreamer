/* GStreamer
 *
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
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

#include <string.h>
#include <stdint.h>
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <gst/video/videooverlay.h>

/* helper library for webrtc things */
#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>

/* For signalling */
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

#define DEFAULT_SIGNALLING_SERVER "wss://webrtc.gstreamer.net:8443"

#define GET_CUSTOM_DATA(env, thiz, fieldID) (WebRTC *)(gintptr)(*env)->GetLongField (env, thiz, fieldID)
#define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env)->SetLongField (env, thiz, fieldID, (jlong)(gintptr)data)

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
  PEER_CONNECTING = 3000,
  PEER_CONNECTION_ERROR,
  PEER_CONNECTED,
  PEER_CALL_NEGOTIATING = 4000,
  PEER_CALL_STARTED,
  PEER_CALL_STOPPING,
  PEER_CALL_STOPPED,
  PEER_CALL_ERROR,
};

typedef struct _WebRTC
{
  jobject java_webrtc;
  GstElement *pipe;
  GThread *thread;
  GMainLoop *loop;
  GMutex lock;
  GCond cond;
  ANativeWindow *native_window;
  SoupWebsocketConnection *ws_conn;
  gchar *signalling_server;
  gchar *peer_id;
  enum AppState app_state;
  GstElement *webrtcbin, *video_sink;
} WebRTC;

static pthread_key_t current_jni_env;
static JavaVM *java_vm;
static jfieldID native_webrtc_field_id;

static gboolean
cleanup_and_quit_loop (WebRTC * webrtc, const gchar * msg, enum AppState state)
{
  if (msg)
    g_printerr ("%s\n", msg);
  if (state > 0)
    webrtc->app_state = state;

  if (webrtc->ws_conn) {
    if (soup_websocket_connection_get_state (webrtc->ws_conn) ==
        SOUP_WEBSOCKET_STATE_OPEN)
      /* This will call us again */
      soup_websocket_connection_close (webrtc->ws_conn, 1000, "");
    else
      g_object_unref (webrtc->ws_conn);
  }

  if (webrtc->loop) {
    g_main_loop_quit (webrtc->loop);
    webrtc->loop = NULL;
  }

  if (webrtc->pipe) {
    GstBus *bus;

    gst_element_set_state (webrtc->pipe, GST_STATE_NULL);

    bus = gst_pipeline_get_bus (GST_PIPELINE (webrtc->pipe));
    gst_bus_remove_watch (bus);
    gst_object_unref (bus);

    gst_object_unref (webrtc->pipe);
    webrtc->pipe = NULL;
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

static GstElement *
handle_media_stream (GstPad * pad, GstElement * pipe, const char *convert_name,
    const char *sink_name)
{
  GstPad *qpad;
  GstElement *q, *conv, *sink;
  GstPadLinkReturn ret;

  q = gst_element_factory_make ("queue", NULL);
  g_assert (q);
  conv = gst_element_factory_make (convert_name, NULL);
  g_assert (conv);
  sink = gst_element_factory_make (sink_name, NULL);
  g_assert (sink);
  if (g_strcmp0 (convert_name, "audioconvert") == 0) {
    GstElement *resample = gst_element_factory_make ("audioresample", NULL);
    g_assert_nonnull (resample);
    gst_bin_add_many (GST_BIN (pipe), q, conv, resample, sink, NULL);
    gst_element_sync_state_with_parent (q);
    gst_element_sync_state_with_parent (conv);
    gst_element_sync_state_with_parent (resample);
    gst_element_sync_state_with_parent (sink);
    gst_element_link_many (q, conv, resample, sink, NULL);
  } else {
    gst_bin_add_many (GST_BIN (pipe), q, conv, sink, NULL);
    gst_element_sync_state_with_parent (q);
    gst_element_sync_state_with_parent (conv);
    gst_element_sync_state_with_parent (sink);
    gst_element_link_many (q, conv, sink, NULL);
  }

  qpad = gst_element_get_static_pad (q, "sink");

  ret = gst_pad_link (pad, qpad);
  g_assert (ret == GST_PAD_LINK_OK);
  gst_object_unref (qpad);

  return sink;
}

static void
on_incoming_decodebin_stream (GstElement * decodebin, GstPad * pad,
    WebRTC * webrtc)
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
    GstElement *sink =
        handle_media_stream (pad, webrtc->pipe, "videoconvert", "glimagesink");
    if (webrtc->video_sink == NULL) {
      webrtc->video_sink = sink;
      if (webrtc->native_window)
        gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (sink),
            (gpointer) webrtc->native_window);
    }
  } else if (g_str_has_prefix (name, "audio")) {
    handle_media_stream (pad, webrtc->pipe, "audioconvert", "autoaudiosink");
  } else {
    g_printerr ("Unknown pad %s, ignoring", GST_PAD_NAME (pad));
  }

  gst_caps_unref (caps);
}

static void
on_incoming_stream (GstElement * webrtcbin, GstPad * pad, WebRTC * webrtc)
{
  GstElement *decodebin;

  if (GST_PAD_DIRECTION (pad) != GST_PAD_SRC)
    return;

  decodebin = gst_element_factory_make ("decodebin", NULL);
  g_signal_connect (decodebin, "pad-added",
      G_CALLBACK (on_incoming_decodebin_stream), webrtc);
  gst_bin_add (GST_BIN (webrtc->pipe), decodebin);
  gst_element_sync_state_with_parent (decodebin);
  gst_element_link (webrtcbin, decodebin);
}

static void
send_ice_candidate_message (GstElement * webrtcbin G_GNUC_UNUSED,
    guint mlineindex, gchar * candidate, WebRTC * webrtc)
{
  gchar *text;
  JsonObject *ice, *msg;

  if (webrtc->app_state < PEER_CALL_NEGOTIATING) {
    cleanup_and_quit_loop (webrtc, "Can't send ICE, not in call",
        APP_STATE_ERROR);
    return;
  }

  ice = json_object_new ();
  json_object_set_string_member (ice, "candidate", candidate);
  json_object_set_int_member (ice, "sdpMLineIndex", mlineindex);
  msg = json_object_new ();
  json_object_set_object_member (msg, "ice", ice);
  text = get_string_from_json_object (msg);
  json_object_unref (msg);

  soup_websocket_connection_send_text (webrtc->ws_conn, text);
  g_free (text);
}

static void
send_sdp_offer (WebRTC * webrtc, GstWebRTCSessionDescription * offer)
{
  gchar *text;
  JsonObject *msg, *sdp;

  if (webrtc->app_state < PEER_CALL_NEGOTIATING) {
    cleanup_and_quit_loop (webrtc, "Can't send offer, not in call",
        APP_STATE_ERROR);
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

  soup_websocket_connection_send_text (webrtc->ws_conn, text);
  g_free (text);
}

/* Offer created by our pipeline, to be sent to the peer */
static void
on_offer_created (GstPromise * promise, WebRTC * webrtc)
{
  GstWebRTCSessionDescription *offer = NULL;
  const GstStructure *reply;

  g_assert (webrtc->app_state == PEER_CALL_NEGOTIATING);

  g_assert (gst_promise_wait (promise) == GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "offer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
  gst_promise_unref (promise);

  promise = gst_promise_new ();
  g_signal_emit_by_name (webrtc->webrtcbin, "set-local-description", offer,
      promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  /* Send offer to peer */
  send_sdp_offer (webrtc, offer);
  gst_webrtc_session_description_free (offer);
}

static void
on_negotiation_needed (GstElement * element, WebRTC * webrtc)
{
  GstPromise *promise;

  webrtc->app_state = PEER_CALL_NEGOTIATING;
  promise = gst_promise_new_with_change_func (on_offer_created, webrtc, NULL);;
  g_signal_emit_by_name (webrtc->webrtcbin, "create-offer", NULL, promise);
}

static void
add_fec_to_offer (GstElement * webrtc)
{
  GstWebRTCRTPTransceiver *trans = NULL;

  /* A transceiver has already been created when a sink pad was
   * requested on the sending webrtcbin */
  g_signal_emit_by_name (webrtc, "get-transceiver", 0, &trans);

  g_object_set (trans, "fec-type", GST_WEBRTC_FEC_TYPE_ULP_RED,
      "fec-percentage", 25, "do-nack", FALSE, NULL);
}

static gboolean
bus_watch_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
  WebRTC *webrtc = user_data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *error = NULL;
      gchar *debug = NULL;

      gst_message_parse_error (message, &error, &debug);
      cleanup_and_quit_loop (webrtc, "ERROR: error on bus", APP_STATE_ERROR);
      g_warning ("Error on bus: %s (debug: %s)", error->message, debug);
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
      gst_bin_recalculate_latency (GST_BIN (webrtc->pipe));
      break;
    default:
      break;
  }

  return G_SOURCE_CONTINUE;
}

#define RTP_CAPS_OPUS "application/x-rtp,media=audio,encoding-name=OPUS,payload=100"
#define RTP_CAPS_VP8 "application/x-rtp,media=video,encoding-name=VP8,payload=101"

static gboolean
start_pipeline (WebRTC * webrtc)
{
  GstStateChangeReturn ret;
  GError *error = NULL;
  GstPad *pad;
  GstBus *bus;

  webrtc->pipe =
      gst_parse_launch ("webrtcbin name=sendrecv "
      "ahcsrc device-facing=front ! video/x-raw,width=[320,1280] ! queue max-size-buffers=1 ! videoconvert ! "
      "vp8enc keyframe-max-dist=30 deadline=1 error-resilient=default ! rtpvp8pay picture-id-mode=15-bit mtu=1300 ! "
      "queue max-size-time=300000000 ! " RTP_CAPS_VP8 " ! sendrecv.sink_0 "
      "openslessrc ! queue ! audioconvert ! audioresample ! audiorate ! queue ! opusenc ! rtpopuspay ! "
      "queue ! " RTP_CAPS_OPUS " ! sendrecv.sink_1 ", &error);

  if (error) {
    g_printerr ("Failed to parse launch: %s\n", error->message);
    g_error_free (error);
    goto err;
  }

  bus = gst_pipeline_get_bus (GST_PIPELINE (webrtc->pipe));
  gst_bus_add_watch (bus, bus_watch_cb, webrtc);
  gst_object_unref (bus);

  webrtc->webrtcbin = gst_bin_get_by_name (GST_BIN (webrtc->pipe), "sendrecv");
  g_assert (webrtc->webrtcbin != NULL);
  add_fec_to_offer (webrtc->webrtcbin);

  /* This is the gstwebrtc entry point where we create the offer and so on. It
   * will be called when the pipeline goes to PLAYING. */
  g_signal_connect (webrtc->webrtcbin, "on-negotiation-needed",
      G_CALLBACK (on_negotiation_needed), webrtc);
  /* We need to transmit this ICE candidate to the browser via the websockets
   * signalling server. Incoming ice candidates from the browser need to be
   * added by us too, see on_server_message() */
  g_signal_connect (webrtc->webrtcbin, "on-ice-candidate",
      G_CALLBACK (send_ice_candidate_message), webrtc);
  /* Incoming streams will be exposed via this signal */
  g_signal_connect (webrtc->webrtcbin, "pad-added",
      G_CALLBACK (on_incoming_stream), webrtc);
  /* Lifetime is the same as the pipeline itself */
  gst_object_unref (webrtc->webrtcbin);

  g_print ("Starting pipeline\n");
  ret = gst_element_set_state (GST_ELEMENT (webrtc->pipe), GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto err;

  return TRUE;

err:
  if (webrtc->pipe)
    g_clear_object (&webrtc->pipe);
  if (webrtc->webrtcbin)
    webrtc->webrtcbin = NULL;
  return FALSE;
}

static gboolean
setup_call (WebRTC * webrtc)
{
  gchar *msg;

  if (soup_websocket_connection_get_state (webrtc->ws_conn) !=
      SOUP_WEBSOCKET_STATE_OPEN)
    return FALSE;

  if (!webrtc->peer_id)
    return FALSE;

  g_print ("Setting up signalling server call with %s\n", webrtc->peer_id);
  webrtc->app_state = PEER_CONNECTING;
  msg = g_strdup_printf ("SESSION %s", webrtc->peer_id);
  soup_websocket_connection_send_text (webrtc->ws_conn, msg);
  g_free (msg);
  return TRUE;
}

static gboolean
register_with_server (WebRTC * webrtc)
{
  gchar *hello;
  gint32 our_id;

  if (soup_websocket_connection_get_state (webrtc->ws_conn) !=
      SOUP_WEBSOCKET_STATE_OPEN)
    return FALSE;

  our_id = g_random_int_range (10, 10000);
  g_print ("Registering id %i with server\n", our_id);
  webrtc->app_state = SERVER_REGISTERING;

  /* Register with the server with a random integer id. Reply will be received
   * by on_server_message() */
  hello = g_strdup_printf ("HELLO %i", our_id);
  soup_websocket_connection_send_text (webrtc->ws_conn, hello);
  g_free (hello);

  return TRUE;
}

static void
on_server_closed (SoupWebsocketConnection * conn G_GNUC_UNUSED, WebRTC * webrtc)
{
  webrtc->app_state = SERVER_CLOSED;
  cleanup_and_quit_loop (webrtc, "Server connection closed", 0);
}

/* One mega message handler for our asynchronous calling mechanism */
static void
on_server_message (SoupWebsocketConnection * conn, SoupWebsocketDataType type,
    GBytes * message, WebRTC * webrtc)
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
    if (webrtc->app_state != SERVER_REGISTERING) {
      cleanup_and_quit_loop (webrtc,
          "ERROR: Received HELLO when not registering", APP_STATE_ERROR);
      goto out;
    }
    webrtc->app_state = SERVER_REGISTERED;
    g_print ("Registered with server\n");
    /* Ask signalling server to connect us with a specific peer */
    if (!setup_call (webrtc)) {
      cleanup_and_quit_loop (webrtc, "ERROR: Failed to setup call",
          PEER_CALL_ERROR);
      goto out;
    }
    /* Call has been setup by the server, now we can start negotiation */
  } else if (g_strcmp0 (text, "SESSION_OK") == 0) {
    if (webrtc->app_state != PEER_CONNECTING) {
      cleanup_and_quit_loop (webrtc,
          "ERROR: Received SESSION_OK when not calling", PEER_CONNECTION_ERROR);
      goto out;
    }

    webrtc->app_state = PEER_CONNECTED;
    /* Start negotiation (exchange SDP and ICE candidates) */
    if (!start_pipeline (webrtc))
      cleanup_and_quit_loop (webrtc, "ERROR: failed to start pipeline",
          PEER_CALL_ERROR);
    /* Handle errors */
  } else if (g_str_has_prefix (text, "ERROR")) {
    switch (webrtc->app_state) {
      case SERVER_CONNECTING:
        webrtc->app_state = SERVER_CONNECTION_ERROR;
        break;
      case SERVER_REGISTERING:
        webrtc->app_state = SERVER_REGISTRATION_ERROR;
        break;
      case PEER_CONNECTING:
        webrtc->app_state = PEER_CONNECTION_ERROR;
        break;
      case PEER_CONNECTED:
      case PEER_CALL_NEGOTIATING:
        webrtc->app_state = PEER_CALL_ERROR;
        break;
      default:
        webrtc->app_state = APP_STATE_ERROR;
    }
    cleanup_and_quit_loop (webrtc, text, 0);
    /* Look for JSON messages containing SDP and ICE candidates */
  } else {
    JsonNode *root;
    JsonObject *object;
    JsonParser *parser = json_parser_new ();

    g_print ("Got server message %s", text);

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

      g_assert (webrtc->app_state == PEER_CALL_NEGOTIATING);

      object = json_object_get_object_member (object, "sdp");

      g_assert (json_object_has_member (object, "type"));

      /* In this example, we always create the offer and receive one answer.
       * See tests/examples/webrtcbidirectional.c in gst-plugins-bad for how to
       * handle offers from peers and reply with answers using webrtcbin. */
      g_assert_cmpstr (json_object_get_string_member (object, "type"), ==,
          "answer");

      text = json_object_get_string_member (object, "sdp");

      g_print ("Received answer:\n%s\n", text);

      ret = gst_sdp_message_new (&sdp);
      g_assert (ret == GST_SDP_OK);

      ret = gst_sdp_message_parse_buffer (text, strlen (text), sdp);
      g_assert (ret == GST_SDP_OK);

      answer = gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_ANSWER,
          sdp);
      g_assert (answer);

      /* Set remote description on our pipeline */
      {
        GstPromise *promise = gst_promise_new ();
        g_signal_emit_by_name (webrtc->webrtcbin, "set-remote-description",
            answer, promise);
        gst_promise_interrupt (promise);
        gst_promise_unref (promise);
      }

      webrtc->app_state = PEER_CALL_STARTED;
    } else if (json_object_has_member (object, "ice")) {
      JsonObject *ice;
      const gchar *candidate;
      gint sdpmlineindex;

      ice = json_object_get_object_member (object, "ice");
      candidate = json_object_get_string_member (ice, "candidate");
      sdpmlineindex = json_object_get_int_member (ice, "sdpMLineIndex");

      /* Add ice candidate sent by remote peer */
      g_signal_emit_by_name (webrtc->webrtcbin, "add-ice-candidate",
          sdpmlineindex, candidate);
    } else {
      g_printerr ("Ignoring unknown JSON message:\n%s\n", text);
    }
    g_object_unref (parser);
  }

out:
  g_free (text);
}

static void
on_server_connected (SoupSession * session, GAsyncResult * res, WebRTC * webrtc)
{
  GError *error = NULL;

  webrtc->ws_conn =
      soup_session_websocket_connect_finish (session, res, &error);
  if (error) {
    cleanup_and_quit_loop (webrtc, error->message, SERVER_CONNECTION_ERROR);
    g_error_free (error);
    return;
  }

  g_assert (webrtc->ws_conn != NULL);

  webrtc->app_state = SERVER_CONNECTED;
  g_print ("Connected to signalling server\n");

  g_signal_connect (webrtc->ws_conn, "closed", G_CALLBACK (on_server_closed),
      webrtc);
  g_signal_connect (webrtc->ws_conn, "message", G_CALLBACK (on_server_message),
      webrtc);

  /* Register with the server so it knows about us and can accept commands */
  register_with_server (webrtc);
}

/*
 * Connect to the signalling server. This is the entrypoint for everything else.
 */
static gboolean
connect_to_websocket_server_async (WebRTC * webrtc)
{
  SoupLogger *logger;
  SoupMessage *message;
  SoupSession *session;
  const char *https_aliases[] = { "wss", NULL };
  const gchar *ca_certs;

  ca_certs = g_getenv ("CA_CERTIFICATES");
  g_assert (ca_certs != NULL);
  g_print ("ca-certificates %s", ca_certs);
  session = soup_session_new_with_options (SOUP_SESSION_SSL_STRICT, FALSE,
      //                                 SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
      SOUP_SESSION_SSL_CA_FILE, ca_certs,
      SOUP_SESSION_HTTPS_ALIASES, https_aliases, NULL);

  logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));
  g_object_unref (logger);

  message = soup_message_new (SOUP_METHOD_GET, webrtc->signalling_server);

  g_print ("Connecting to server...\n");

  /* Once connected, we will register */
  soup_session_websocket_connect_async (session, message, NULL, NULL, NULL,
      (GAsyncReadyCallback) on_server_connected, webrtc);
  webrtc->app_state = SERVER_CONNECTING;

  return G_SOURCE_REMOVE;
}

/* Register this thread with the VM */
static JNIEnv *
attach_current_thread (void)
{
  JNIEnv *env;
  JavaVMAttachArgs args;

  GST_DEBUG ("Attaching thread %p", g_thread_self ());
  args.version = JNI_VERSION_1_4;
  args.name = NULL;
  args.group = NULL;

  if ((*java_vm)->AttachCurrentThread (java_vm, &env, &args) < 0) {
    GST_ERROR ("Failed to attach current thread");
    return NULL;
  }

  return env;
}

/* Unregister this thread from the VM */
static void
detach_current_thread (void *env)
{
  GST_DEBUG ("Detaching thread %p", g_thread_self ());
  (*java_vm)->DetachCurrentThread (java_vm);
}

/* Retrieve the JNI environment for this thread */
static JNIEnv *
get_jni_env (void)
{
  JNIEnv *env;

  if ((env = pthread_getspecific (current_jni_env)) == NULL) {
    env = attach_current_thread ();
    pthread_setspecific (current_jni_env, env);
  }

  return env;
}

/*
 * Java Bindings
 */

static void
native_end_call (JNIEnv * env, jobject thiz)
{
  WebRTC *webrtc = GET_CUSTOM_DATA (env, thiz, native_webrtc_field_id);

  if (!webrtc)
    return;

  g_mutex_lock (&webrtc->lock);
  if (webrtc->loop) {
    GThread *thread = webrtc->thread;

    GST_INFO ("Ending current call");
    cleanup_and_quit_loop (webrtc, NULL, 0);
    webrtc->thread = NULL;
    g_mutex_unlock (&webrtc->lock);
    g_thread_join (thread);
  } else {
    g_mutex_unlock (&webrtc->lock);
  }
}

static gboolean
_unlock_mutex (GMutex * m)
{
  g_mutex_unlock (m);
  return G_SOURCE_REMOVE;
}

static gpointer
_call_thread (WebRTC * webrtc)
{
  GMainContext *context = NULL;
  JNIEnv *env = attach_current_thread ();

  g_mutex_lock (&webrtc->lock);

  context = g_main_context_new ();
  webrtc->loop = g_main_loop_new (context, FALSE);
  g_main_context_invoke (context, (GSourceFunc) _unlock_mutex, &webrtc->lock);
  g_main_context_invoke (context,
      (GSourceFunc) connect_to_websocket_server_async, webrtc);
  g_main_context_push_thread_default (context);
  g_cond_broadcast (&webrtc->cond);
  g_main_loop_run (webrtc->loop);
  g_main_context_pop_thread_default (context);

  detach_current_thread (env);

  return NULL;
}

static void
native_call_other_party (JNIEnv * env, jobject thiz)
{
  WebRTC *webrtc = GET_CUSTOM_DATA (env, thiz, native_webrtc_field_id);

  if (!webrtc)
    return;

  if (webrtc->thread)
    native_end_call (env, thiz);

  GST_INFO ("calling other party");

  webrtc->thread = g_thread_new ("webrtc", (GThreadFunc) _call_thread, webrtc);
  g_mutex_lock (&webrtc->lock);
  while (!webrtc->loop)
    g_cond_wait (&webrtc->cond, &webrtc->lock);
  g_mutex_unlock (&webrtc->lock);
}

static void
native_new (JNIEnv * env, jobject thiz)
{
  WebRTC *webrtc = g_new0 (WebRTC, 1);

  SET_CUSTOM_DATA (env, thiz, native_webrtc_field_id, webrtc);
  webrtc->java_webrtc = (*env)->NewGlobalRef (env, thiz);

  webrtc->signalling_server = g_strdup (DEFAULT_SIGNALLING_SERVER);

  g_mutex_init (&webrtc->lock);
  g_cond_init (&webrtc->cond);
}

static void
native_free (JNIEnv * env, jobject thiz)
{
  WebRTC *webrtc = GET_CUSTOM_DATA (env, thiz, native_webrtc_field_id);

  if (!webrtc)
    return;

  (*env)->DeleteGlobalRef (env, webrtc->java_webrtc);

  native_end_call (env, thiz);

  g_cond_clear (&webrtc->cond);
  g_mutex_clear (&webrtc->lock);
  g_free (webrtc->peer_id);
  g_free (webrtc->signalling_server);
  g_free (webrtc);
  SET_CUSTOM_DATA (env, thiz, native_webrtc_field_id, NULL);
}

static void
native_class_init (JNIEnv * env, jclass klass)
{
  native_webrtc_field_id =
      (*env)->GetFieldID (env, klass, "native_webrtc", "J");

  if (!native_webrtc_field_id) {
    static const gchar *message =
        "The calling class does not implement all necessary interface methods";
    jclass exception_class = (*env)->FindClass (env, "java/lang/Exception");
    __android_log_print (ANDROID_LOG_ERROR, "GstPlayer", "%s", message);
    (*env)->ThrowNew (env, exception_class, message);
  }
  GST_DEBUG_CATEGORY_INIT (debug_category, "webrtc", 0,
      "GStreamer Android WebRTC");
  //gst_debug_set_threshold_from_string ("gl*:7", FALSE);
}

static void
native_set_surface (JNIEnv * env, jobject thiz, jobject surface)
{
  WebRTC *webrtc = GET_CUSTOM_DATA (env, thiz, native_webrtc_field_id);
  ANativeWindow *new_native_window;

  if (!webrtc)
    return;

  new_native_window = surface ? ANativeWindow_fromSurface (env, surface) : NULL;
  GST_DEBUG ("Received surface %p (native window %p)", surface,
      new_native_window);

  if (webrtc->native_window) {
    ANativeWindow_release (webrtc->native_window);
  }

  webrtc->native_window = new_native_window;
  if (webrtc->video_sink)
    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (webrtc->video_sink),
        (guintptr) new_native_window);
}

static void
native_set_signalling_server (JNIEnv * env, jobject thiz, jstring server)
{
  WebRTC *webrtc = GET_CUSTOM_DATA (env, thiz, native_webrtc_field_id);
  const gchar *s;

  if (!webrtc)
    return;

  s = (*env)->GetStringUTFChars (env, server, NULL);
  if (webrtc->signalling_server)
    g_free (webrtc->signalling_server);
  webrtc->signalling_server = g_strdup (s);
  (*env)->ReleaseStringUTFChars (env, server, s);
}

static void
native_set_call_id (JNIEnv * env, jobject thiz, jstring peer_id)
{
  WebRTC *webrtc = GET_CUSTOM_DATA (env, thiz, native_webrtc_field_id);
  const gchar *s;

  if (!webrtc)
    return;

  s = (*env)->GetStringUTFChars (env, peer_id, NULL);
  g_free (webrtc->peer_id);
  webrtc->peer_id = g_strdup (s);
  (*env)->ReleaseStringUTFChars (env, peer_id, s);
}

/* List of implemented native methods */
static JNINativeMethod native_methods[] = {
  {"nativeClassInit", "()V", (void *) native_class_init},
  {"nativeNew", "()V", (void *) native_new},
  {"nativeFree", "()V", (void *) native_free},
  {"nativeSetSurface", "(Landroid/view/Surface;)V",
      (void *) native_set_surface},
  {"nativeSetSignallingServer", "(Ljava/lang/String;)V",
      (void *) native_set_signalling_server},
  {"nativeSetCallID", "(Ljava/lang/String;)V",
      (void *) native_set_call_id},
  {"nativeCallOtherParty", "()V",
      (void *) native_call_other_party},
  {"nativeEndCall", "()V",
      (void *) native_end_call}
};

/* Library initializer */
jint
JNI_OnLoad (JavaVM * vm, void *reserved)
{
  JNIEnv *env = NULL;

  java_vm = vm;

  if ((*vm)->GetEnv (vm, (void **) &env, JNI_VERSION_1_4) != JNI_OK) {
    __android_log_print (ANDROID_LOG_ERROR, "GstWebRTC",
        "Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = (*env)->FindClass (env, "org/freedesktop/gstreamer/WebRTC");
  if (!klass) {
    __android_log_print (ANDROID_LOG_ERROR, "GstWebRTC",
        "Could not retrieve class org.freedesktop.gstreamer.WebRTC");
    return 0;
  }
  if ((*env)->RegisterNatives (env, klass, native_methods,
          G_N_ELEMENTS (native_methods))) {
    __android_log_print (ANDROID_LOG_ERROR, "GstWebRTC",
        "Could not register native methods for org.freedesktop.gstreamer.WebRTC");
    return 0;
  }

  pthread_key_create (&current_jni_env, detach_current_thread);

  return JNI_VERSION_1_4;
}
