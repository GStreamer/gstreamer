#include <gst/gst.h>

int main(int argc,char *argv[]) 
{
  GstElement *fakesrc, *fakesink;
  GstElement *thread, *pipeline;
  gint x;

  gst_init(&argc,&argv);

  thread = gst_thread_new("thread");
  g_assert(thread != NULL);

  pipeline = gst_pipeline_new("pipeline");
  g_assert(pipeline != NULL);

  gst_bin_add(GST_BIN(thread), GST_ELEMENT(pipeline));

  fakesrc = gst_element_factory_make("fakesrc", "fake_source");
  g_assert(fakesrc != NULL);

  fakesink = gst_element_factory_make("fakesink", "fake_sink");
  g_assert(fakesink != NULL);

  gst_bin_add_many (GST_BIN(pipeline), fakesrc, fakesink, NULL);
  gst_element_connect (fakesrc, fakesink);

  for (x = 0 ; x < 10 ; x++){
    g_print("playing %d\n", x);
    gst_element_set_state(GST_ELEMENT(thread), GST_STATE_PLAYING);
    sleep(1);

    g_print("pausing %d\n", x);
    gst_element_set_state(GST_ELEMENT(thread), GST_STATE_PAUSED);
    sleep(1);
  }

  gst_main();

  exit(0);
}

