#include <gst/gst.h>
#include <stdio.h>

GST_DEBUG_CATEGORY_STATIC (srtp_client_debug);
#define GST_CAT_DEFAULT srtp_client_debug

#define ERROR_STR_NULL(err)                                                    \
  ((err) != NULL ? ((err)->message != NULL ? (err->message) : "(NULL)")        \
                 : "(NULL)")

#define MAKE_AND_ADD(var, pipe, name, out_label, elem_name)                    \
  G_STMT_START {                                                               \
    if (G_UNLIKELY(!(var = (gst_element_factory_make(name, elem_name))))) {    \
      GST_ERROR("Could not create element %s", name);                          \
      goto out_label;                                                          \
    }                                                                          \
    if (G_UNLIKELY(!gst_bin_add(GST_BIN_CAST(pipe), var))) {                   \
      GST_ERROR("Could not add element %s", name);                             \
      goto out_label;                                                          \
    }                                                                          \
  }                                                                            \
  G_STMT_END

static GMainLoop *loop = NULL;
static GstElement *pipeline = NULL;
static GstElement *rtspsrc = NULL;
static GList *streams = NULL;

typedef struct
{
  GMutex lock;
  guint key_size;
  guint32 mki;
  GstCaps *key_caps;
  GstCaps *rekey_caps;
} KeyParam;

static KeyParam *
key_param_new (guint key_size, guint32 mki)
{
  KeyParam *key_param = NULL;
  guint8 *data, *mki_data;
  guint data_size = GST_ROUND_UP_4 (key_size);
  GstBuffer *srtp_key, *mki_buf;
  guint i;

  key_param = g_malloc0 (sizeof (KeyParam));

  g_mutex_init (&key_param->lock);

  key_param->key_size = data_size;
  key_param->mki = mki;

  data = g_malloc (data_size);
  for (i = 0; i < data_size; i += 4) {
    GST_WRITE_UINT32_BE (data + i, g_random_int ());
  }
  srtp_key = gst_buffer_new_wrapped (data, key_size);

  mki_data = g_malloc (sizeof (guint32));
  GST_WRITE_UINT32_BE (mki_data, mki);
  mki_buf = gst_buffer_new_wrapped (mki_data, sizeof (guint32));

  /* parameters for MIKEY SETUP and srtpdec */
  key_param->key_caps =
      gst_caps_new_static_str_simple ("application/x-srtp", "srtp-key",
      GST_TYPE_BUFFER, srtp_key, "srtp-cipher", G_TYPE_STRING, "aes-128-icm",
      "srtp-auth", G_TYPE_STRING, "hmac-sha1-80", "mki", GST_TYPE_BUFFER,
      mki_buf, "srtcp-cipher", G_TYPE_STRING, "aes-128-icm", "srtcp-auth",
      G_TYPE_STRING, "hmac-sha1-80", NULL);

  /* parameters for re-keying */
  key_param->rekey_caps =
      gst_caps_new_static_str_simple ("application/x-srtp", "srtp-key",
      GST_TYPE_BUFFER, srtp_key, "mki", GST_TYPE_BUFFER, mki_buf, NULL);

  gst_buffer_unref (mki_buf);
  gst_buffer_unref (srtp_key);

  return key_param;
}

static void
key_param_free (KeyParam * key_param)
{
  g_mutex_lock (&key_param->lock);

  gst_caps_unref (key_param->rekey_caps);
  gst_caps_unref (key_param->key_caps);

  g_mutex_unlock (&key_param->lock);

  g_free (key_param);
}

static GstCaps *
key_param_get_srtp_param (KeyParam * key_param)
{
  GstCaps *caps;

  g_mutex_lock (&key_param->lock);
  caps = gst_caps_ref (key_param->key_caps);
  g_mutex_unlock (&key_param->lock);

  return caps;
}

static GstCaps *
key_param_get_rekey_mikey (KeyParam * key_param)
{
  GstCaps *caps;

  g_mutex_lock (&key_param->lock);
  caps = gst_caps_ref (key_param->rekey_caps);
  g_mutex_unlock (&key_param->lock);

  return caps;
}

static void
key_param_inc_mki (KeyParam * key_param)
{
  GstBuffer *mki_buf;
  guint8 *mki_data = g_malloc (sizeof (guint32));

  g_mutex_lock (&key_param->lock);

  key_param->mki += 1;
  GST_INFO ("Incrementing mki to: %u", key_param->mki);

  GST_WRITE_UINT32_BE (mki_data, key_param->mki);
  mki_buf = gst_buffer_new_wrapped (mki_data, sizeof (guint32));

  key_param->key_caps = gst_caps_make_writable (key_param->key_caps);
  gst_caps_set_simple_static_str (key_param->key_caps, "mki", GST_TYPE_BUFFER,
      mki_buf, NULL);

  key_param->rekey_caps = gst_caps_make_writable (key_param->rekey_caps);
  gst_caps_set_simple_static_str (key_param->rekey_caps, "mki", GST_TYPE_BUFFER,
      mki_buf, NULL);

  g_mutex_unlock (&key_param->lock);

  gst_buffer_unref (mki_buf);
}

/* Called when a key is required:
 *
 * * When configuring srtpenc for RTCP.
 * * When preparing the KeyMgmt parameter for the SETUP request.
 * * When srtpdec needs a key to decrypt an incoming packet.
 * * After 'remove-key', which we call when re-keying.
 */
static GstCaps *
request_key (G_GNUC_UNUSED GstElement * src,
    G_GNUC_UNUSED guint stream, gpointer user_data)
{
  GstCaps *caps;
  KeyParam *key_param = (KeyParam *) user_data;

  caps = key_param_get_srtp_param (key_param);
  GST_DEBUG ("Got key: %" GST_PTR_FORMAT, caps);

  return caps;
}

typedef struct
{
  KeyParam *key_param;
  GList *streams_to_rekey;
} RekeyData;

static RekeyData *
rekey_data_new (KeyParam * key_param, GList * streams_to_renew)
{
  RekeyData *this = g_malloc0 (sizeof (RekeyData));
  this->key_param = key_param;
  this->streams_to_rekey = streams_to_renew;

  return this;
}

static void
rekey_data_free (RekeyData * data)
{
  g_list_free (data->streams_to_rekey);
  /* key_param lifetime is handled elsewhere */

  g_free (data);
}

static void on_rekey_reply (GstPromise * promise, gpointer user_data);

static gboolean
rekey_next_stream (gpointer user_data)
{
  RekeyData *data = (RekeyData *) user_data;
  GList *first;
  guint stream_id;
  GstCaps *mikey;
  GstPromise *promise;
  gboolean res;

  first = g_list_first (data->streams_to_rekey);
  if (!first) {
    GST_DEBUG ("No more streams to re-key");
    rekey_data_free (data);
    goto out;
  }

  stream_id = GPOINTER_TO_UINT (first->data);
  GST_INFO ("Re-keying stream with id %u", stream_id);

  promise = gst_promise_new_with_change_func (on_rekey_reply, data, NULL);

  mikey = key_param_get_rekey_mikey (data->key_param);
  g_signal_emit_by_name (rtspsrc, "set-mikey-parameter", stream_id, mikey,
      promise, &res);

  if (!res) {
    GST_ERROR ("Failed to emit set-mikey-parameter for stream with id %u",
        stream_id);
    rekey_data_free (data);
    gst_promise_unref (promise);
  }

out:
  /* next stream will be processed when the promise is complete */
  return G_SOURCE_REMOVE;
}

static void
on_rekey_reply (GstPromise * promise, gpointer user_data)
{
  RekeyData *data = (RekeyData *) user_data;
  GList *first;
  guint stream_id;
  const GstStructure *reply;
  gint result, code;
  gboolean res;

  first = g_list_first (data->streams_to_rekey);
  if (!first) {
    GST_WARNING ("on_rekey_reply called but there are no more streams");
    goto unrecoverable_err;
  }
  stream_id = GPOINTER_TO_UINT (first->data);

  if (gst_promise_wait (promise) != GST_PROMISE_RESULT_REPLIED) {
    GST_WARNING ("set-mikey-parameter interrupted or expired");
    /* will try again */
    goto next;
  }

  /* First stream was either processed or there was an unrecoverable error */
  data->streams_to_rekey =
      g_list_remove (data->streams_to_rekey, GUINT_TO_POINTER (stream_id));

  reply = gst_promise_get_reply (promise);
  GST_DEBUG ("renew-mikey replied %" GST_PTR_FORMAT, reply);

  if (!gst_structure_get_int (reply, "rtsp-result", &result) || result != 0) {
    GST_ERROR ("Failed to send MIKEY parameter to server: %" GST_PTR_FORMAT,
        reply);
    goto unrecoverable_err;
  }
  if (!gst_structure_get_int (reply, "rtsp-code", &code) || code != 200) {
    GST_ERROR ("Setting MIKEY failed for stream with id %u. Reply from server: "
        "%" GST_PTR_FORMAT, stream_id, reply);
    goto next;
  }

  g_signal_emit_by_name (rtspsrc, "remove-key", stream_id, &res);
  if (!res) {
    GST_ERROR ("Failed to remove key from client for stream with id %u",
        stream_id);
    goto next;
  }

  GST_DEBUG ("Re-keying complete for stream with id %u", stream_id);
next:
  if (data->streams_to_rekey)
    g_idle_add (rekey_next_stream, data);

  gst_promise_unref (promise);
  return;

unrecoverable_err:
  rekey_data_free (data);
  gst_promise_unref (promise);
}

static gboolean
rekey_all (gpointer user_data)
{
  KeyParam *key_param = (KeyParam *) user_data;
  RekeyData *data;

  if (!rtspsrc) {
    GST_DEBUG ("Skipping rekey_all because rtspsrc is not ready yet");
    goto out;
  }

  key_param_inc_mki (key_param);

  /* rtspsrc can only process one SET_PARAMETER at once.
   * We will chain SET_PARAMETER then remove-key for each stream.
   */
  data = rekey_data_new (key_param, g_list_copy (streams));
  rekey_next_stream (data);

out:
  return G_SOURCE_CONTINUE;
}

static void
on_soft_limit (GstElement * rtspsrc, guint stream_id, gpointer user_data)
{
  GST_INFO ("Reached soft-limit for stream with id %u", stream_id);

  /* this is where we should re-new the key
   * in this test, we wait for hard-limit though to show both signals.
   */
}

static void
on_hard_limit (GstElement * rtspsrc, guint stream_id, gpointer user_data)
{
  KeyParam *key_param = (KeyParam *) user_data;
  GList *list = NULL;
  RekeyData *data;

  GST_INFO ("Reached hard-limit for stream with id %u", stream_id);

  key_param_inc_mki (key_param);

  list = g_list_append (list, GUINT_TO_POINTER (stream_id));
  data = rekey_data_new (key_param, list);

  g_idle_add (rekey_next_stream, data);
}

static gboolean
setup_h264_pipeline (GstElement * element, GstPad * pad, GstPad ** decode_pad)
{
  gboolean ret = FALSE;
  GstObject *parent = NULL;
  GstElement *depay = NULL;
  GstElement *decode = NULL;
  GstPad *sinkpad = NULL;

  parent = gst_object_get_parent (GST_OBJECT (element));

  MAKE_AND_ADD (depay, parent, "rtph264depay", out, NULL);
  MAKE_AND_ADD (decode, parent, "avdec_h264", out, NULL);

  if (!gst_element_link (depay, decode)) {
    GST_ERROR ("failed linking h264 elements");
    goto out;
  }

  sinkpad = gst_element_get_static_pad (depay, "sink");
  if (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK) {
    GST_ERROR ("failed linking video depayloader");
    goto out;
  }

  (void) gst_element_sync_state_with_parent (decode);
  (void) gst_element_sync_state_with_parent (depay);

  *decode_pad = gst_element_get_static_pad (decode, "src");

  ret = TRUE;

out:
  g_clear_object (&sinkpad);
  gst_object_unref (parent);

  return ret;
}

static gboolean
setup_h265_pipeline (GstElement * element, GstPad * pad, GstPad ** decode_pad)
{
  gboolean ret = FALSE;
  GstObject *parent = NULL;
  GstElement *depay = NULL;
  GstElement *decode = NULL;
  GstPad *sinkpad = NULL;

  parent = gst_object_get_parent (GST_OBJECT (element));

  MAKE_AND_ADD (depay, parent, "rtph265depay", out, NULL);
  MAKE_AND_ADD (decode, parent, "avdec_h265", out, NULL);

  if (!gst_element_link (depay, decode)) {
    GST_ERROR ("failed linking h265 elements");
    goto out;
  }

  sinkpad = gst_element_get_static_pad (depay, "sink");
  if (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK) {
    GST_ERROR ("failed linking video depayloader");
    goto out;
  }

  (void) gst_element_sync_state_with_parent (decode);
  (void) gst_element_sync_state_with_parent (depay);

  *decode_pad = gst_element_get_static_pad (decode, "src");

  ret = TRUE;

out:
  g_clear_object (&sinkpad);
  gst_object_unref (parent);

  return ret;
}

static void
setup_video_sink (GstElement * element, GstPad * pad, GstStructure * st)
{
  GstObject *parent = NULL;
  GstElement *scale = NULL;
  GstElement *convert = NULL;
  GstElement *queue = NULL;
  GstElement *sink = NULL;
  GstPad *decode_pad = NULL;
  GstPad *sinkpad = NULL;
  const gchar *encoding;

  encoding = gst_structure_get_string (st, "encoding-name");
  if (g_str_equal (encoding, "H264")) {
    if (!setup_h264_pipeline (element, pad, &decode_pad)) {
      GST_WARNING ("skipping H264 stream");
      goto out;
    }
  } else if (g_str_equal (encoding, "H265")) {
    if (!setup_h265_pipeline (element, pad, &decode_pad)) {
      GST_WARNING ("skipping H265 stream");
      goto out;
    }
  } else {
    /* TODO: add more formats */
    GST_FIXME ("unhandled encoding: %s", encoding);
    goto out;
  }

  parent = gst_object_get_parent (GST_OBJECT (element));

  MAKE_AND_ADD (scale, parent, "videoscale", out, NULL);
  MAKE_AND_ADD (convert, parent, "videoconvert", out, NULL);
  MAKE_AND_ADD (queue, parent, "queue", out, NULL);
  g_object_set (queue, "max-size-buffers", 1, "max-size-bytes", 0,
      "max-size-time", 0, NULL);
  MAKE_AND_ADD (sink, parent, "autovideosink", out, NULL);

  if (!gst_element_link_many (scale, convert, queue, sink, NULL)) {
    GST_ERROR ("failed linking video elements");
    goto out;
  }

  sinkpad = gst_element_get_static_pad (scale, "sink");
  if (gst_pad_link (decode_pad, sinkpad) != GST_PAD_LINK_OK) {
    GST_ERROR ("failed linking video pipeline");
    goto out;
  }

  (void) gst_element_sync_state_with_parent (sink);
  (void) gst_element_sync_state_with_parent (queue);
  (void) gst_element_sync_state_with_parent (convert);
  (void) gst_element_sync_state_with_parent (scale);

out:
  g_clear_object (&decode_pad);
  g_clear_object (&sinkpad);
  gst_object_unref (parent);
}

static gboolean
setup_aac_pipeline (GstElement * element, GstPad * pad, GstPad ** decode_pad)
{
  gboolean ret = FALSE;
  GstObject *parent = NULL;
  GstElement *depay = NULL;
  GstElement *decode = NULL;
  GstPad *sinkpad = NULL;

  parent = gst_object_get_parent (GST_OBJECT (element));

  MAKE_AND_ADD (depay, parent, "rtpmp4gdepay", out, NULL);
  MAKE_AND_ADD (decode, parent, "avdec_aac", out, NULL);

  if (!gst_element_link (depay, decode)) {
    GST_ERROR ("failed linking audio elements");
    goto out;
  }

  sinkpad = gst_element_get_static_pad (depay, "sink");
  if (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK) {
    GST_ERROR ("linking sink failed");
    goto out;
  }

  (void) gst_element_sync_state_with_parent (decode);
  (void) gst_element_sync_state_with_parent (depay);

  *decode_pad = gst_element_get_static_pad (decode, "src");

  ret = TRUE;

out:
  g_clear_object (&sinkpad);
  gst_object_unref (parent);

  return ret;
}

static void
setup_audio_sink (GstElement * element, GstPad * pad, GstStructure * st)
{
  GstObject *parent = NULL;
  GstElement *convert = NULL;
  GstElement *queue = NULL;
  GstElement *sink = NULL;
  GstPad *decode_pad = NULL;
  GstPad *sinkpad = NULL;
  const gchar *encoding, *mode;

  encoding = gst_structure_get_string (st, "encoding-name");
  mode = gst_structure_get_string (st, "mode");
  if (g_str_equal (encoding, "MPEG4-GENERIC") && g_str_has_prefix (mode, "AAC")) {
    if (!setup_aac_pipeline (element, pad, &decode_pad)) {
      GST_WARNING ("skipping aac stream");
      goto out;
    }
  } else {
    GST_FIXME ("unhandled: encoding %s / mode: %s", encoding, mode);
    goto out;
  }

  parent = gst_object_get_parent (GST_OBJECT (element));

  MAKE_AND_ADD (convert, parent, "audioconvert", out, NULL);
  MAKE_AND_ADD (queue, parent, "queue", out, NULL);
  g_object_set (queue, "max-size-buffers", 1, "max-size-bytes", 0,
      "max-size-time", 0, NULL);
  MAKE_AND_ADD (sink, parent, "autoaudiosink", out, NULL);

  if (!gst_element_link_many (convert, queue, sink, NULL)) {
    GST_ERROR ("failed linking audio elements");
    goto out;
  }

  sinkpad = gst_element_get_static_pad (convert, "sink");
  if (gst_pad_link (decode_pad, sinkpad) != GST_PAD_LINK_OK) {
    GST_ERROR ("failed linking audio pipeline");
    goto out;
  }

  (void) gst_element_sync_state_with_parent (sink);
  (void) gst_element_sync_state_with_parent (queue);
  (void) gst_element_sync_state_with_parent (convert);

out:
  g_clear_object (&decode_pad);
  g_clear_object (&sinkpad);
  gst_object_unref (parent);
}

static void
pad_added (GstElement * element, GstPad * pad, G_GNUC_UNUSED gpointer user_data)
{
  GstCaps *caps;
  GstStructure *st;
  const gchar *name;
  const gchar *media;

  caps = gst_pad_get_current_caps (pad);

  GST_DEBUG ("new pad %" GST_PTR_FORMAT " with caps %" GST_PTR_FORMAT, pad,
      caps);

  st = gst_caps_get_structure (caps, 0);
  name = gst_structure_get_name (st);

  if (!g_str_equal (name, "application/x-rtp")) {
    GST_ERROR ("caps not understood");
    gst_caps_unref (caps);
    return;
  }

  media = gst_structure_get_string (st, "media");
  if (media == NULL) {
    GST_ERROR ("no media in caps");
    gst_caps_unref (caps);
    return;
  }

  if (g_str_equal (media, "video")) {
    setup_video_sink (element, pad, st);
  } else if (g_str_equal (media, "audio")) {
    setup_audio_sink (element, pad, st);
  } else {
    GST_WARNING ("media not understood");
  }

  gst_caps_unref (caps);
}

static gboolean
select_stream (G_GNUC_UNUSED GstElement * rtspsrc,
    guint stream_id, GstCaps * caps, G_GNUC_UNUSED gpointer user_data)
{
  GST_INFO ("Selecting stream with id: %u, %" GST_PTR_FORMAT, stream_id, caps);
  streams = g_list_append (streams, GUINT_TO_POINTER (stream_id));
  return TRUE;
}

static gboolean
build_pipeline (const gchar * location, KeyParam * key_param)
{
  GstElement *src = NULL;
  gboolean ret = FALSE;

  GST_DEBUG ("building pipeline for: %s", location);

  pipeline = gst_pipeline_new ("srtp pipeline");

  MAKE_AND_ADD (src, pipeline, "rtspsrc", out, NULL);
  rtspsrc = gst_object_ref (src);

  g_object_set (src, "location", location, "tls-validation-flags", 0x20,
      "client-managed-mikey", TRUE, NULL);

  g_signal_connect (src, "pad-added", G_CALLBACK (pad_added), NULL);

  if (key_param == NULL) {
    GST_WARNING ("no key available");
    ret = TRUE;
    goto out;
  }

  g_signal_connect (src, "select-stream", G_CALLBACK (select_stream), NULL);

  g_signal_connect (src, "request-rtp-key", G_CALLBACK (request_key),
      key_param);
  g_signal_connect (src, "request-rtcp-key", G_CALLBACK (request_key),
      key_param);

  g_signal_connect (src, "soft-limit", G_CALLBACK (on_soft_limit), NULL);
  g_signal_connect (src, "hard-limit", G_CALLBACK (on_hard_limit), key_param);

  ret = TRUE;

out:
  if (!ret) {
    gst_object_unref (pipeline);
    pipeline = NULL;
  }

  return ret;
}

static gboolean
bus_message (G_GNUC_UNUSED GstBus * bus, GstMessage * message,
    G_GNUC_UNUSED gpointer user_data)
{
  GST_TRACE ("got %" GST_PTR_FORMAT, message);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *name, *debug = NULL;
      gst_message_parse_error (message, &err, &debug);
      name = gst_object_get_path_string (message->src);
      GST_ERROR ("ERROR from %s: %s", name, err->message);
      if (debug != NULL) {
        GST_ERROR ("debug; %s", debug);
      }
      g_error_free (err);
      g_free (debug);
      g_free (name);
    }
      /* fall through */
    case GST_MESSAGE_EOS:
      GST_DEBUG ("stopping the main loop");
      g_main_loop_quit (loop);
      break;
    default:
      break;
  }

  return TRUE;
}

gint
main (gint argc, gchar ** argv)
{
  gint res = EXIT_FAILURE;
  GstBus *bus = NULL;
  KeyParam *key_param = NULL;
  gchar *location = NULL;
  guint32 key_len, mki, rekey_int = 0;
  if (argc != 5) {
    g_printerr
        ("Usage:\n\ttest-client-managed-mikey KEY_LEN MKI REKEY_INT LOCATION\n"
        "\n\tWhere:\n" "\t\tKEY_LEN  : len of the key (e.g. 30)\n"
        "\t\tMKI      : Master Key Index (e.g. 1200)\n"
        "\t\tREKEY_INT: re-keying interval in seconds (e.g. 10). 0 to disable\n"
        "\t\tLOCATION : rtsps://user:pass@host:port/resource (e.g. port "
        "322)\n");
    goto out;
  }
  if (!sscanf (argv[1], "%u", &key_len)) {
    g_printerr ("Expected an integer for KEY_LEN, got: %s", argv[1]);
    goto out;
  }
  if (!sscanf (argv[2], "%u", &mki)) {
    g_printerr ("Expected an integer for MKI, got: %s", argv[2]);
    goto out;
  }
  if (!sscanf (argv[3], "%u", &rekey_int)) {
    g_printerr ("Expected an integer for REKEY_INT, got: %s", argv[3]);
    goto out;
  }
  location = argv[4];
  gst_init (&argc, &argv);
  GST_DEBUG_CATEGORY_INIT (srtp_client_debug, "test-client-managed-mikey", 0,
      "test-client-managed-mikey debug");
  loop = g_main_loop_new (NULL, TRUE);
  key_param = key_param_new (key_len, mki);
  if (!build_pipeline (location, key_param)) {
    GST_ERROR ("Pipeline could not be built");
    goto out;
  }

  bus = gst_element_get_bus (pipeline);
  if (bus == NULL) {
    GST_ERROR ("Could not get the pipeline bus");
    goto out;
  }

  (void) gst_bus_add_watch (bus, bus_message, NULL);
  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_ERROR ("Could not set the pipeline in playing state");
    goto out;
  }

  if (rekey_int) {
    /* Automatically renew MKI */
    g_timeout_add_seconds (rekey_int, rekey_all, key_param);
  } else {
    GST_INFO ("Not using re-keying interval. Will wait for hard-limit");
  }

  g_main_loop_run (loop);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_bus_remove_watch (bus);
  res = EXIT_SUCCESS;

out:
  g_clear_pointer (&key_param, key_param_free);
  g_clear_pointer (&streams, g_list_free);
  g_clear_object (&rtspsrc);
  g_clear_object (&bus);
  g_clear_object (&pipeline);
  g_clear_pointer (&loop, g_main_loop_unref);

  return res;
}
