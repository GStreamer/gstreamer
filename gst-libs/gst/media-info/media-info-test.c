/* media-info test app */

#include <gst/gst.h>
#include <string.h>
#include "media-info.h"

static void
caps_print (GstCaps *caps)
{
  if (caps == NULL) return;
  /*
  if (!strcmp (gst_caps_get_mime (caps), "application/x-gst-metadata") ||
      !strcmp (gst_caps_get_mime (caps), "application/x-gst-streaminfo"))
      */
  if (TRUE)
  {
    GstProps *props = caps->properties;
    GList *walk;

    if (props == NULL)
    {
      g_print ("    none\n");
      return;
    }
    walk = props->properties;

    while (walk) {
      GstPropsEntry *entry = (GstPropsEntry *) walk->data;
      const gchar *name;
      const gchar *str_val;
      gint int_val;
      GstPropsType type;

      name = gst_props_entry_get_name (entry);
      type = gst_props_entry_get_type (entry);
      switch (type) {
        case GST_PROPS_STRING_TYPE:
          gst_props_entry_get_string (entry, &str_val);
          g_print ("      %s='%s'\n", name, str_val);
          break;
        case GST_PROPS_INT_TYPE:
          gst_props_entry_get_int (entry, &int_val);
          g_print ("      %s=%d\n", name, int_val);
          break;
        default:
          break;
      }

      walk = g_list_next (walk);
    }
  }
  else {
    g_print (" unkown caps type\n");
  }
}

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
  for (i = 0; i < stream->length_tracks; ++i)
  {
    g_print ("- track %d\n", i);
    track = (GstMediaInfoTrack *) p->data;
    g_print ("  - metadata:\n");
    caps_print (track->metadata);
    g_print ("  - streaminfo:\n");
    caps_print (track->streaminfo);
    g_print ("  - format:\n");
    caps_print (track->format);
    p = p->next;
  }
}

int
main (int argc, char *argv[])
{
  GstMediaInfo *info;
  GstMediaInfoStream *stream;
  gint i;
	
  g_assert (argc > 1);

  gst_init (&argc, &argv);

  info = g_object_new (GST_MEDIA_INFO_TYPE, NULL);
  for (i = 1; i < argc; ++i)
  {
    stream = gst_media_info_read (info, argv[i], GST_MEDIA_INFO_ALL);
    g_print ("\nFILE: %s\n", argv[i]);
    if (stream)
      info_print (stream);
    else
      g_print ("no media info found.\n");
  }
  
  return 0;	
}
