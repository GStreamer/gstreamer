#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <gst/gst.h>
#include <string.h>

static GstElement *bin;

static void
unlinked (GstPad * pad, GstPad * peerpad, GstElement * pipeline)
{
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_bin_remove (GST_BIN (pipeline), bin);
  gst_element_set_state (bin, GST_STATE_READY);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
}

static void
new_pad (GstElement * elem, GstPad * newpad, GstElement * pipeline)
{
  GstScheduler *sched;
  GstClock *clock;

  g_print ("new pad %s\n", gst_pad_get_name (newpad));

  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_bin_add (GST_BIN (pipeline), bin);

  sched = gst_element_get_scheduler (GST_ELEMENT (pipeline));
  clock = gst_scheduler_get_clock (sched);
  gst_scheduler_set_clock (sched, clock);

  gst_pad_link (newpad, gst_element_get_pad (bin, "sink"));

  g_signal_connect (G_OBJECT (newpad), "unlinked", G_CALLBACK (unlinked),
      pipeline);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
}

int
main (int argc, char **argv)
{
  GstElement *pipeline;
  GstElement *filesrc;
  GstElement *oggdemux;
  GstElement *vorbisdec;
  GstElement *audioconvert;
  GstElement *audiosink;

  gst_init (&argc, &argv);

  if (argc < 2) {
    g_print ("usage: %s <oggfile>\n", argv[0]);
    return (-1);
  }

  pipeline = gst_pipeline_new ("pipeline");

  filesrc = gst_element_factory_make ("filesrc", "filesrc");
  g_assert (filesrc);
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  oggdemux = gst_element_factory_make ("oggdemux", "oggdemux");
  g_assert (oggdemux);

  gst_bin_add (GST_BIN (pipeline), filesrc);
  gst_bin_add (GST_BIN (pipeline), oggdemux);

  gst_element_link_pads (filesrc, "src", oggdemux, "sink");

  g_signal_connect (G_OBJECT (oggdemux), "new_pad", G_CALLBACK (new_pad),
      pipeline);

  bin = gst_bin_new ("bin");
  vorbisdec = gst_element_factory_make ("vorbisdec", "vorbisdec");
  g_assert (vorbisdec);
  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");
  g_assert (audioconvert);
  audiosink = gst_element_factory_make (DEFAULT_AUDIOSINK, DEFAULT_AUDIOSINK);
  g_assert (audiosink);
  gst_bin_add (GST_BIN (bin), vorbisdec);
  gst_bin_add (GST_BIN (bin), audioconvert);
  gst_bin_add (GST_BIN (bin), audiosink);

  gst_element_link_pads (vorbisdec, "src", audioconvert, "sink");
  gst_element_link_pads (audioconvert, "src", audiosink, "sink");

  gst_element_add_ghost_pad (bin, gst_element_get_pad (vorbisdec, "sink"),
      "sink");

  g_object_ref (G_OBJECT (bin));

  g_signal_connect (pipeline, "deep_notify",
      G_CALLBACK (gst_element_default_deep_notify), NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)))
    /* nop */ ;

  /* stop probe */
  gst_element_set_state (pipeline, GST_STATE_NULL);

  return 0;
}
