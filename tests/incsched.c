#include <stdlib.h>
#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstBin *thread, *bin;
  GstElement *src, *identity, *sink, *identity2;

  gst_init(&argc,&argv);
  gst_info_set_categories(-1);
  gst_debug_set_categories(-1);

  g_print("\n\nConstructing stuff:\n");
  thread = GST_BIN (gst_pipeline_new("thread"));
  bin = GST_BIN (gst_bin_new("bin"));
  src = gst_elementfactory_make("fakesrc","src");
  identity = gst_elementfactory_make("identity","identity");
  sink = gst_elementfactory_make("fakesink","sink");
  identity2 = gst_elementfactory_make("identity","identity2");

  g_print("\nAdding src to thread:\n");
  gst_bin_add(thread,src);
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nAdding identity to thread:\n");
  gst_bin_add(thread,identity);
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nRemoving identity from thread:\n");
  gst_bin_remove(thread, identity);
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nAdding identity to thread:\n");
  gst_bin_add(thread,identity);
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nConnecting src to identity:\n");
  gst_element_connect(src,"src",identity,"sink");
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nDisconnecting src from identity:\n");
  gst_element_disconnect(src,"src",identity,"sink");
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nConnecting src to identity:\n");
  gst_element_connect(src,"src",identity,"sink");
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nAdding sink to bin:\n");
  gst_bin_add(bin,sink);
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nAdding bin to thread:\n");
  gst_bin_add(thread, GST_ELEMENT(bin));
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nConnecting identity to sink:\n");
  gst_element_connect(identity,"src",sink,"sink");
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nDisconnecting sink:\n");
  gst_element_disconnect(identity,"src",sink,"sink");
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nAdding identity2 to bin:\n");
  gst_bin_add(bin, identity2);
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nConnecting identity2 to sink\n");
  gst_element_connect(identity2,"src",sink,"sink");
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nConnecting identity to identity2\n");
  gst_element_connect(identity,"src",identity2,"sink");
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\n\nNow setting state from NULL to READY:\n");
  gst_element_set_state(GST_ELEMENT(thread),GST_STATE_READY);
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\n\nNow setting state from READY to PLAYING:\n");
  gst_element_set_state(GST_ELEMENT(thread),GST_STATE_PLAYING);
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\n\nIterating:\n");
  gst_bin_iterate(thread);

  g_print("\n\nNow setting state from PLAYING to READY:\n");
  gst_element_set_state(GST_ELEMENT(thread),GST_STATE_READY);
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\n\nNow setting state from READY to PLAYING:\n");
  gst_element_set_state(GST_ELEMENT(thread),GST_STATE_PLAYING);
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\n\nIterating:\n");
  gst_bin_iterate(thread);

  g_print("\n\nNow setting state from PLAYING to READY:\n");
  gst_element_set_state(GST_ELEMENT(thread),GST_STATE_READY);
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nDisconnecting identity from identity2:\n");
  gst_element_disconnect(identity,"src",identity2,"sink");
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nDisconnecting identity2 from sink:\n");
  gst_element_disconnect(identity2,"src",sink,"sink");
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\nConnecting identity to sink:\n");
  gst_element_connect(identity,"src",sink,"sink");
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\n\nNow setting identity2 to NULL:\n");
  gst_element_set_state(identity2,GST_STATE_NULL);

  g_print("\nRemoving identity2 from bin:\n");
  gst_bin_remove(bin, identity2);
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\n\nNow setting state from READY to PLAYING:\n");
  gst_element_set_state(GST_ELEMENT(thread),GST_STATE_PLAYING);
  gst_schedule_show(GST_ELEMENT_SCHED(thread));

  g_print("\n\nIterating:\n");
  gst_bin_iterate(thread);

//return;
  g_print("\n\nSetting EOS on fakesrc and iterating again:\n");
  gtk_object_set(GTK_OBJECT(src),"eos",TRUE,NULL);
  gst_bin_iterate(thread);

  g_print("\n\nIterating:\n");
  gst_bin_iterate(thread);

  return 0;
}
