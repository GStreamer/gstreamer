/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include <gst/gst.h>

static gboolean verbose = FALSE;
static gboolean quiet = FALSE;

static void
entry_added (GstIndex *index, GstIndexEntry *entry)
{
  switch (entry->type) {
    case GST_INDEX_ENTRY_ID:
      g_print ("id %d describes writer %s\n", entry->id, 
		      GST_INDEX_ID_DESCRIPTION (entry));
      break;
    case GST_INDEX_ENTRY_FORMAT:
      g_print ("%d: registered format %d for %s\n", entry->id, 
		      GST_INDEX_FORMAT_FORMAT (entry),
		      GST_INDEX_FORMAT_KEY (entry));
      break;
    case GST_INDEX_ENTRY_ASSOCIATION:
    {
      gint i;

      g_print ("%d: %08x ", entry->id, GST_INDEX_ASSOC_FLAGS (entry));
      for (i = 0; i < GST_INDEX_NASSOCS (entry); i++) {
	g_print ("%d %lld ", GST_INDEX_ASSOC_FORMAT (entry, i), 
			     GST_INDEX_ASSOC_VALUE (entry, i));
      }
      g_print ("\n");
      break;
    }
    default:
      break;
  }
}

typedef struct
{
  const gchar   *padname;
  GstPad        *target;
  GstElement    *bin;
  GstElement    *pipeline;
  GstIndex      *index;
} dyn_connect;

static void
dynamic_connect (GstPadTemplate *templ, GstPad *newpad, gpointer data)
{
  dyn_connect *connect = (dyn_connect *) data;

  if (!strcmp (gst_pad_get_name (newpad), connect->padname)) {
    gst_element_set_state (connect->pipeline, GST_STATE_PAUSED);
    gst_bin_add (GST_BIN (connect->pipeline), connect->bin);
    gst_pad_connect (newpad, connect->target);
    gst_element_set_index (connect->bin, connect->index);
    gst_element_set_state (connect->pipeline, GST_STATE_PLAYING);
  }
}

static void
setup_dynamic_connection (GstElement *pipeline, 
		          GstElement *element, 
			  const gchar *padname, 
			  GstPad *target, 
			  GstElement *bin,
			  GstIndex *index)
{
  dyn_connect *connect;

  connect = g_new0 (dyn_connect, 1);
  connect->padname      = g_strdup (padname);
  connect->target       = target;
  connect->bin          = bin;
  connect->pipeline     = pipeline;
  connect->index     	= index;

  g_signal_connect (G_OBJECT (element), "new_pad", G_CALLBACK (dynamic_connect), connect);
}

static GstElement*
make_mpeg_systems_pipeline (const gchar *path, GstIndex *index)
{
  GstElement *pipeline;
  GstElement *src, *demux;

  pipeline = gst_pipeline_new ("pipeline");

  src = gst_element_factory_make ("filesrc", "src");
  g_object_set (G_OBJECT (src), "location", path, NULL);

  demux = gst_element_factory_make ("mpegdemux", "demux");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);

  if (index) {
    gst_element_set_index (pipeline, index);
  }

  gst_element_connect_pads (src, "src", demux, "sink");
  
  return pipeline;
}

static GstElement*
make_mpeg_decoder_pipeline (const gchar *path, GstIndex *index)
{
  GstElement *pipeline;
  GstElement *src, *demux;
  GstElement *video_bin, *audio_bin;
  GstElement *video_decoder, *audio_decoder;

  pipeline = gst_pipeline_new ("pipeline");

  src = gst_element_factory_make ("filesrc", "src");
  g_object_set (G_OBJECT (src), "location", path, NULL);

  demux = gst_element_factory_make ("mpegdemux", "demux");

  gst_bin_add (GST_BIN (pipeline), src);
  gst_bin_add (GST_BIN (pipeline), demux);

  gst_element_connect_pads (src, "src", demux, "sink");

  video_bin = gst_bin_new ("video_bin");
  video_decoder = gst_element_factory_make ("mpeg2dec", "video_decoder");

  gst_bin_add (GST_BIN (video_bin), video_decoder);
  
  setup_dynamic_connection (pipeline, demux, "video_00", 
		            gst_element_get_pad (video_decoder, "sink"),
			    video_bin, index);

  audio_bin = gst_bin_new ("audio_bin");
  audio_decoder = gst_element_factory_make ("mad", "audio_decoder");

  gst_bin_add (GST_BIN (audio_bin), audio_decoder);

  if (index) {
    gst_element_set_index (pipeline, index);
  }
  
  return pipeline;
}

static void
print_progress (GstPad *pad)
{
  gint i = 0;
  gchar status[53];
  GstFormat format;
  gboolean res;
  gint64 value;
  gint percent = 0;

  status[0] = '|';

  format = GST_FORMAT_PERCENT;
  res = gst_pad_query (pad, GST_PAD_QUERY_POSITION, &format, &value);
  if (res) {
    percent = value / (2 * GST_FORMAT_PERCENT_SCALE); 
  }
  
  for (i = 0; i < percent; i++) {
    status[i+1] = '=';
  }
  for (i = percent; i < 50; i++) {
    status[i+1] = ' ';
  }
  status[51] = '|';
  status[52] = 0;

  g_print ("%s\r", status);
}

gint 
main (gint argc, gchar *argv[])
{
  GstElement *pipeline;
  GstElement *src;
  GstPad *pad;
  GstIndex *index;
  gint count = 0;
  GstEvent *event;
  gboolean res;
  gint i;
  struct poptOption options[] = {
    { "verbose",  'v',  POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   &verbose,   0,
      "Print index entries", NULL},
    { "quiet",  'q',  POPT_ARG_NONE|POPT_ARGFLAG_STRIP,   &quiet,   0,
      "don't print progress bar", NULL},
      POPT_TABLEEND
    };

  if (!gst_init_with_popt_table (&argc, &argv, options) || argc < 3) {
    g_print ("usage: %s [-v] <type> <filename>  \n" 
	     "  type can be: 0 mpeg_systems\n"
	     "               1 mpeg_decoder\n"
	     "  -v : report added index entries\n"
	     "  -q : don't print progress\n" , argv[0]);
    return -1;
  }

  /* create index that elements can fill */
  index = gst_index_factory_make ("memindex");
  if (verbose)
    g_signal_connect (G_OBJECT (index), "entry_added", G_CALLBACK (entry_added), NULL);

  /* construct pipeline */
  switch (atoi (argv[1])) {
    case 0:
      pipeline = make_mpeg_systems_pipeline (argv[2], index);
      break;
    case 1:
      pipeline = make_mpeg_decoder_pipeline (argv[2], index);
      break;
    default:
      g_print ("unkown type %d\n", atoi (argv[1]));
      return -1;
  }

  /* setup some default info/error handlers */
  g_signal_connect (G_OBJECT (pipeline), "deep_notify", 
		    G_CALLBACK (gst_element_default_deep_notify), NULL);
  g_signal_connect (G_OBJECT (pipeline), "error", 
		    G_CALLBACK (gst_element_default_error), NULL);

  /* get a pad to perform progress reporting on */
  src = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  pad = gst_element_get_pad (src, "src");

  /* prepare for iteration */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_print ("indexing %s...\n", argv [2]);
  /* run through the complete stream to let it generate an index */
  while (gst_bin_iterate (GST_BIN (pipeline))) {
    if (!quiet && (count % 1000 == 0)) {
      print_progress (pad);
    }
    count++;
  }
  g_print ("\n");

  /* bring to ready to restart the pipeline */
  gst_element_set_state (pipeline, GST_STATE_READY);
  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  GST_FLAG_UNSET (index, GST_INDEX_WRITABLE);

  src = gst_bin_get_by_name (GST_BIN (pipeline), "video_decoder");
  pad = gst_element_get_pad (src, "src");

  g_print ("seeking %s...\n", argv [2]);
  event = gst_event_new_seek (GST_FORMAT_TIME |
                              GST_SEEK_METHOD_SET |
                              GST_SEEK_FLAG_FLUSH, 1 * GST_SECOND);

  res = gst_pad_send_event (pad, event);
  if (!res) {
    g_warning ("seek failed");
  }

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  for (i = 0; i < 100; i++) {
    gst_bin_iterate (GST_BIN (pipeline));
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);

  return 1;
}

