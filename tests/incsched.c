#include <stdlib.h>
#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstBin *thread, *bin;
  GstElement *src, *identity, *sink, *identity2;

  gst_init(&argc,&argv);
  gst_info_set_categories(-1);
  gst_debug_set_categories(-1);

  g_print("\n\nConstructing stuff:\n");
  thread = gst_pipeline_new("thread");
  bin = gst_bin_new("bin");
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
  gst_schedule_show(GST_ELEMENT_SCHED(bin));

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

  g_print("\nDisconnecting identity to identity2\n");
  gst_element_disconnect(identity,"src",identity2,"sink");
  gst_schedule_show(GST_ELEMENT_SCHED(thread));
}
