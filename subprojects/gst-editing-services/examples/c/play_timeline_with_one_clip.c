/* This example can be found in the GStreamer Editing Services git repository in:
 * examples/c/play_timeline_with_one_clip.c
 */
#include <ges/ges.h>

int
main (int argc, char **argv)
{
  GESLayer *layer;
  GESTimeline *timeline;

  if (argc == 1) {
    gst_printerr ("Usage: play_timeline_with_one_clip file:///clip/uri\n");

    return 1;
  }

  gst_init (NULL, NULL);
  ges_init ();

  timeline = ges_timeline_new_audio_video ();
  layer = ges_timeline_append_layer (timeline);

  {
    /* Add a clip with a duration of 5 seconds */
    GESClip *clip = GES_CLIP (ges_uri_clip_new (argv[1]));

    if (clip == NULL) {
      gst_printerr
          ("%s can not be used, make sure it is a supported media file",
          argv[1]);

      return 1;
    }

    g_object_set (clip, "duration", 5 * GST_SECOND, "start", 0, NULL);
    ges_layer_add_clip (layer, clip);
  }

  /* Commiting the timeline is always necessary for changes
   * inside it to be taken into account by the Non Linear Engine */
  ges_timeline_commit (timeline);

  {
    /* Play the timeline */
    GESPipeline *pipeline = ges_pipeline_new ();
    GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

    ges_pipeline_set_timeline (pipeline, timeline);
    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

    /* Simple way to just play the pipeline until EOS or an error pops on the bus */
    gst_bus_timed_pop_filtered (bus, 10 * GST_SECOND,
        GST_MESSAGE_EOS | GST_MESSAGE_ERROR);

    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
    gst_object_unref (bus);
    gst_object_unref (pipeline);
  }


  return 0;
}
