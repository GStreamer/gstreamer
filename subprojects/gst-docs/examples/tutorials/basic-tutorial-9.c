#include <string.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData
{
  GstDiscoverer *discoverer;
  GMainLoop *loop;
} CustomData;

/* Print a tag in a human-readable format (name: value) */
static void
print_tag_foreach (const GstTagList * tags, const gchar * tag,
    gpointer user_data)
{
  GValue val = { 0, };
  gchar *str;
  gint depth = GPOINTER_TO_INT (user_data);

  gst_tag_list_copy_value (&val, tags, tag);

  if (G_VALUE_HOLDS_STRING (&val))
    str = g_value_dup_string (&val);
  else
    str = gst_value_serialize (&val);

  g_print ("%*s%s: %s\n", 2 * depth, " ", gst_tag_get_nick (tag), str);
  g_free (str);

  g_value_unset (&val);
}

/* Print information regarding a stream */
static void
print_stream_info (GstDiscovererStreamInfo * info, gint depth)
{
  gchar *desc = NULL;
  GstCaps *caps;
  const GstTagList *tags;

  caps = gst_discoverer_stream_info_get_caps (info);

  if (caps) {
    if (gst_caps_is_fixed (caps))
      desc = gst_pb_utils_get_codec_description (caps);
    else
      desc = gst_caps_to_string (caps);
    gst_caps_unref (caps);
  }

  g_print ("%*s%s: %s\n", 2 * depth, " ",
      gst_discoverer_stream_info_get_stream_type_nick (info),
      (desc ? desc : ""));

  if (desc) {
    g_free (desc);
    desc = NULL;
  }

  tags = gst_discoverer_stream_info_get_tags (info);
  if (tags) {
    g_print ("%*sTags:\n", 2 * (depth + 1), " ");
    gst_tag_list_foreach (tags, print_tag_foreach, GINT_TO_POINTER (depth + 2));
  }
}

/* Print information regarding a stream and its substreams, if any */
static void
print_topology (GstDiscovererStreamInfo * info, gint depth)
{
  GstDiscovererStreamInfo *next;

  if (!info)
    return;

  print_stream_info (info, depth);

  next = gst_discoverer_stream_info_get_next (info);
  if (next) {
    print_topology (next, depth + 1);
    gst_discoverer_stream_info_unref (next);
  } else if (GST_IS_DISCOVERER_CONTAINER_INFO (info)) {
    GList *tmp, *streams;

    streams =
        gst_discoverer_container_info_get_streams (GST_DISCOVERER_CONTAINER_INFO
        (info));
    for (tmp = streams; tmp; tmp = tmp->next) {
      GstDiscovererStreamInfo *tmpinf = (GstDiscovererStreamInfo *) tmp->data;
      print_topology (tmpinf, depth + 1);
    }
    gst_discoverer_stream_info_list_free (streams);
  }
}

/* This function is called every time the discoverer has information regarding
 * one of the URIs we provided.*/
static void
on_discovered_cb (GstDiscoverer * discoverer, GstDiscovererInfo * info,
    GError * err, CustomData * data)
{
  GstDiscovererResult result;
  const gchar *uri;
  const GstTagList *tags;
  GstDiscovererStreamInfo *sinfo;

  uri = gst_discoverer_info_get_uri (info);
  result = gst_discoverer_info_get_result (info);
  switch (result) {
    case GST_DISCOVERER_URI_INVALID:
      g_print ("Invalid URI '%s'\n", uri);
      break;
    case GST_DISCOVERER_ERROR:
      g_print ("Discoverer error: %s\n", err->message);
      break;
    case GST_DISCOVERER_TIMEOUT:
      g_print ("Timeout\n");
      break;
    case GST_DISCOVERER_BUSY:
      g_print ("Busy\n");
      break;
    case GST_DISCOVERER_MISSING_PLUGINS:{
      const GstStructure *s;
      gchar *str;

      s = gst_discoverer_info_get_misc (info);
      str = gst_structure_to_string (s);

      g_print ("Missing plugins: %s\n", str);
      g_free (str);
      break;
    }
    case GST_DISCOVERER_OK:
      g_print ("Discovered '%s'\n", uri);
      break;
  }

  if (result != GST_DISCOVERER_OK) {
    g_printerr ("This URI cannot be played\n");
    return;
  }

  /* If we got no error, show the retrieved information */

  g_print ("\nDuration: %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (gst_discoverer_info_get_duration (info)));

  tags = gst_discoverer_info_get_tags (info);
  if (tags) {
    g_print ("Tags:\n");
    gst_tag_list_foreach (tags, print_tag_foreach, GINT_TO_POINTER (1));
  }

  g_print ("Seekable: %s\n",
      (gst_discoverer_info_get_seekable (info) ? "yes" : "no"));

  g_print ("\n");

  sinfo = gst_discoverer_info_get_stream_info (info);
  if (!sinfo)
    return;

  g_print ("Stream information:\n");

  print_topology (sinfo, 1);

  gst_discoverer_stream_info_unref (sinfo);

  g_print ("\n");
}

/* This function is called when the discoverer has finished examining
 * all the URIs we provided.*/
static void
on_finished_cb (GstDiscoverer * discoverer, CustomData * data)
{
  g_print ("Finished discovering\n");

  g_main_loop_quit (data->loop);
}

int
tutorial_main (int argc, char **argv)
{
  CustomData data;
  GError *err = NULL;
  gchar *uri =
      "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm";

  /* if a URI was provided, use it instead of the default one */
  if (argc > 1) {
    uri = argv[1];
  }

  /* Initialize cumstom data structure */
  memset (&data, 0, sizeof (data));

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  g_print ("Discovering '%s'\n", uri);

  /* Instantiate the Discoverer */
  data.discoverer = gst_discoverer_new (5 * GST_SECOND, &err);
  if (!data.discoverer) {
    g_print ("Error creating discoverer instance: %s\n", err->message);
    g_clear_error (&err);
    return -1;
  }

  /* Connect to the interesting signals */
  g_signal_connect (data.discoverer, "discovered",
      G_CALLBACK (on_discovered_cb), &data);
  g_signal_connect (data.discoverer, "finished", G_CALLBACK (on_finished_cb),
      &data);

  /* Start the discoverer process (nothing to do yet) */
  gst_discoverer_start (data.discoverer);

  /* Add a request to process asynchronously the URI passed through the command line */
  if (!gst_discoverer_discover_uri_async (data.discoverer, uri)) {
    g_print ("Failed to start discovering URI '%s'\n", uri);
    g_object_unref (data.discoverer);
    return -1;
  }

  /* Create a GLib Main Loop and set it to run, so we can wait for the signals */
  data.loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.loop);

  /* Stop the discoverer process */
  gst_discoverer_stop (data.discoverer);

  /* Free resources */
  g_object_unref (data.discoverer);
  g_main_loop_unref (data.loop);

  return 0;
}

int
main (int argc, char *argv[])
{
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
  return gst_macos_main (tutorial_main, argc, argv, NULL);
#else
  return tutorial_main (argc, argv);
#endif
}
