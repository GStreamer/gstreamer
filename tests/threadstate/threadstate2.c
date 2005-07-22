#include <stdlib.h>
#include <gst/gst.h>

static GMainLoop *loop;
static gboolean EOS = FALSE;

/* this pipeline is:
 * { filesrc ! mad ! osssink }
 */

static gboolean
bus_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      g_print ("have eos, quitting\n");
      EOS = TRUE;
      if (g_main_loop_is_running (loop))
        g_main_loop_quit (loop);
      break;
    default:
      break;
  }
  return TRUE;
}

static gboolean
timeout_func (GMainLoop * loop)
{
  g_main_loop_quit (loop);

  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *filesrc, *osssink;
  GstElement *pipeline;
  GstElement *mad;
  gint x;
  GstBus *bus;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <filename>\n", argv[0]);
    exit (-1);
  }

  /* create a new pipeline to hold the elements */
  pipeline = gst_pipeline_new ("pipeline");
  g_assert (pipeline != NULL);

  /* create a disk reader */
  filesrc = gst_element_factory_make ("filesrc", "disk_source");
  g_assert (filesrc != NULL);
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  /* and an audio sink */
  osssink = gst_element_factory_make ("osssink", "play_audio");
  g_assert (osssink != NULL);

  /* did i mention that this is an mp3 player? */
  mad = gst_element_factory_make ("mad", "mp3_decoder");
  g_assert (mad != NULL);

  gst_bin_add_many (GST_BIN (pipeline), filesrc, mad, osssink, NULL);
  gst_element_link_many (filesrc, mad, osssink, NULL);

  loop = g_main_loop_new (NULL, FALSE);
  g_timeout_add (2 * 1000, (GSourceFunc) timeout_func, loop);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, (GstBusHandler) bus_handler, pipeline);

  for (x = 0; x < 10; x++) {
    g_print ("playing %d\n", x);
    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
    g_main_loop_run (loop);
    if (EOS)
      break;

    g_print ("pausing %d\n", x);
    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);
    g_main_loop_run (loop);
  }

  exit (0);
}
