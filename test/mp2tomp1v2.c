#include <string.h>
#include <gst/gst.h>

static gchar *audio_out;
static gchar *video_out;

static void 
frame_encoded (GstElement *element, GstElement *pipeline)
{ 
  fprintf (stderr, ".");
}

static void 
new_pad (GstElement *element, GstPad *pad, GstElement *pipeline)
{
  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  if (strncmp(gst_pad_get_name(pad), "video_", 6) == 0) {
    gst_parse_launch (g_strdup_printf ("mpeg2dec[vdec] ! "
		      "ffmpegenc_mpeg1video[venc] width=352 height=288 bit_rate=1220000 ! "
		      "disksink[dv] location=%s", video_out), 
		      GST_BIN (pipeline));

    g_signal_connect (gst_bin_get_by_name (GST_BIN (pipeline), "venc"), "frame_encoded", 
		    G_CALLBACK (frame_encoded), pipeline); 
    gst_pad_connect (pad, gst_element_get_pad (gst_bin_get_by_name (GST_BIN (pipeline), "vdec"), "sink")); 
  } 
  else if (strcmp(gst_pad_get_name(pad), "private_stream_1.0") == 0) {
    gst_parse_launch (g_strdup_printf ("ac3dec[adec] ! ffmpegenc_mp2[aenc] ! "
		      "disksink[da] location=%s", audio_out), GST_BIN (pipeline));

    g_signal_connect (gst_bin_get_by_name (GST_BIN (pipeline), "aenc"), "frame_encoded", 
		    G_CALLBACK (frame_encoded), pipeline); 
    gst_pad_connect (pad, gst_element_get_pad (gst_bin_get_by_name (GST_BIN (pipeline), "adec"), "sink")); 
  }
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
}

int 
main (int argc, char *argv[]) 
{
  GstElement *pipeline;
  GstElement *parser;

  gst_init (&argc, &argv);

  if (argc != 4) {
    g_print ("usage: %s <file.vob> <out.mp2> <out.mpv>\n", argv[0]);
    return -1;
  }
  audio_out = argv[2];
  video_out = argv[3];

  pipeline = gst_pipeline_new ("main_pipeline");
  gst_parse_launch (g_strdup_printf("disksrc location=%s ! "
			  "mpeg2parse[parser]", argv[1]), GST_BIN (pipeline));

  parser = gst_bin_get_by_name (GST_BIN (pipeline), "parser");
  g_assert (parser != NULL);

  g_signal_connect (G_OBJECT (parser), "new_pad", G_CALLBACK (new_pad), pipeline);
  
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  while (gst_bin_iterate (GST_BIN (pipeline)));
  gst_element_set_state (pipeline, GST_STATE_NULL);

  return 0;
}
