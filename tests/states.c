#include <gst/gst.h>

gboolean state_change(GstElement *element,GstElementState state) {
  g_print("state_change: element '%s' state set to %d(%s)\n",
	gst_element_get_name(element),state,_gst_print_statename(state));
  g_print("state_change: element state is actually %d\n",GST_STATE(element));
}

int main(int argc,char *argv[]) {
  GstElement *bin;
  GstElement *src,*sink;

  gst_init(&argc,&argv);

  src = gst_elementfactory_make("fakesrc","src");
  g_return_val_if_fail(1,src != NULL);
  sink = gst_elementfactory_make("fakesink","sink");
  g_return_val_if_fail(1,sink != NULL);
  bin = gst_bin_new("bin");
  g_return_val_if_fail(1,bin != NULL);

  gtk_signal_connect(GTK_OBJECT(src),"state_change",
                     GTK_SIGNAL_FUNC(state_change),NULL);
  gtk_signal_connect(GTK_OBJECT(sink),"state_change",
                     GTK_SIGNAL_FUNC(state_change),NULL);
  gtk_signal_connect(GTK_OBJECT(bin),"state_change",
                     GTK_SIGNAL_FUNC(state_change),NULL);

  g_print("element '%s' starts at state %d(%s)\n",gst_element_get_name(src),
	GST_STATE(src),_gst_print_statename(GST_STATE(src)));
  g_print("element '%s' starts at state %d(%s)\n",gst_element_get_name(sink),
	GST_STATE(sink),_gst_print_statename(GST_STATE(sink)));
  g_print("element '%s' starts at state %d(%s)\n",gst_element_get_name(bin),
	GST_STATE(bin),_gst_print_statename(GST_STATE(bin)));

  gst_bin_add(GST_BIN(bin),src);
  gst_bin_add(GST_BIN(bin),sink);

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(sink,"sink"));

  gst_bin_create_plan (GST_BIN (bin));
  gst_element_set_state (bin, GST_STATE_PLAYING);

  gst_bin_iterate (GST_BIN (bin));
}
