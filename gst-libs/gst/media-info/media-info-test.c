/* media-info test app */

#include <gst/gst.h>
#include <string.h>
#include "media-info.h"

static void
print_tag (const GstTagList * list, const gchar * tag, gpointer unused)
{
  gint i, count;

  count = gst_tag_list_get_tag_size (list, tag);

  for (i = 0; i < count; i++) {
    gchar *str;

    if (gst_tag_get_type (tag) == G_TYPE_STRING) {
      g_assert (gst_tag_list_get_string_index (list, tag, i, &str));
    } else {
      str =
          g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, i));
    }

    if (i == 0) {
      g_print ("%15s: %s\n", gst_tag_get_nick (tag), str);
    } else {
      g_print ("               : %s\n", str);
    }

    g_free (str);
  }
}

static void
info_print (GstMediaInfoStream * stream)
{
  int i;
  GList *p;
  GstMediaInfoTrack *track;

  g_print ("- mime type: %s\n", stream->mime);
  g_print ("- length: %.3f seconds\n",
      (gdouble) stream->length_time / GST_SECOND);
  g_print ("- bitrate: %.3f kbps\n", stream->bitrate / 1000.0);
  g_print ("- number of tracks: %ld\n", stream->length_tracks);
  p = stream->tracks;
  if (p == NULL) {
    g_print ("- no track information, probably an error\n");
    return;
  }
  for (i = 0; i < stream->length_tracks; ++i) {
    g_print ("- track %d\n", i);
    track = (GstMediaInfoTrack *) p->data;
    g_print ("  - metadata:\n");
    if (track->metadata)
      gst_tag_list_foreach (track->metadata, print_tag, NULL);
    else
      g_print ("  (none found)\n");
    g_print ("  - streaminfo:\n");
    gst_tag_list_foreach (track->streaminfo, print_tag, NULL);
    g_print ("  - format:\n");
    g_print ("%s\n", gst_caps_to_string (track->format));
    p = p->next;
  }
}

int
main (int argc, char *argv[])
{
  GstMediaInfo *info;
  GstMediaInfoStream *stream = NULL;
  GError *error = NULL;
  gint i;

  g_assert (argc > 1);

  gst_media_info_init ();
  gst_init (&argc, &argv);

  info = gst_media_info_new (&error);
  if (error != NULL) {
    g_print ("Error creating media-info object: %s\n", error->message);
    g_error_free (error);
    return -1;
  }

  g_assert (G_IS_OBJECT (info));
  if (!gst_media_info_set_source (info, "gnomevfssrc", &error)) {
    g_print ("Could not set gnomevfssrc as a source\n");
    g_print ("reason: %s\n", error->message);
    g_error_free (error);
    return -1;
  }

  g_print ("stream: %p, &stream: %p\n", stream, &stream);
  for (i = 1; i < argc; ++i) {

    /*
       stream = gst_media_info_read (info, argv[i], GST_MEDIA_INFO_ALL);
     */
    gst_media_info_read_with_idler (info, argv[i], GST_MEDIA_INFO_ALL, &error);
    while (gst_media_info_read_idler (info, &stream, &error) && stream == NULL)
      /* keep idling */
      g_print ("+");
    g_print ("\nFILE: %s\n", argv[i]);
    g_print ("stream: %p, &stream: %p\n", stream, &stream);
    if (error) {
      g_print ("Error reading media info: %s\n", error->message);
      g_error_free (error);
    }
    if (stream)
      info_print (stream);
    else
      g_print ("no media info found.\n");
    stream = NULL;
  }

  return 0;
}
