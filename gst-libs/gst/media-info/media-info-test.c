/* media-info test app */

#include <gst/gst.h>
#include <string.h>
#include "media-info.h"

static void
info_print (GstMediaInfoStream *stream)
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
  if (p == NULL)
  {
    g_print ("- no track information, probably an error\n");
    return;
  }
  for (i = 0; i < stream->length_tracks; ++i)
  {
    g_print ("- track %d\n", i);
    track = (GstMediaInfoTrack *) p->data;
    g_print ("  - metadata:\n");
    g_print ("%s\n", gst_caps_to_string (track->metadata));
    g_print ("  - streaminfo:\n");
    g_print ("%s\n", gst_caps_to_string (track->streaminfo));
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
  gint i;

  g_assert (argc > 1);

  gst_init (&argc, &argv);
  gst_media_info_init ();

  info = g_object_new (GST_MEDIA_INFO_TYPE, NULL);
  g_print ("stream: %p, &stream: %p\n", stream, &stream);
  for (i = 1; i < argc; ++i)
  {

    /*
    stream = gst_media_info_read (info, argv[i], GST_MEDIA_INFO_ALL);
    */
    gst_media_info_read_with_idler (info, argv[i], GST_MEDIA_INFO_ALL);
    while (gst_media_info_read_idler (info, &stream) && stream == NULL)
      /* keep idling */ g_print ("+");
    g_print ("\nFILE: %s\n", argv[i]);
    g_print ("stream: %p, &stream: %p\n", stream, &stream);
    if (stream)
      info_print (stream);
    else
      g_print ("no media info found.\n");
    stream = NULL;
  }

  return 0;
}
