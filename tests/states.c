#include <gst/gst.h>

gboolean state_change(GstElement *element,GstElementState state) {
  g_print(">STATES: element '%s' state set to %d(%s)\n",
	gst_element_get_name(element),state,gst_element_statename(state));
  g_print(">STATES: element state is actually %d\n",GST_STATE(element));

  return TRUE;
}

int main(int argc,char *argv[]) {
  GstElement *bin,*subbin;
  GstElement *src,*sink,*filter;

  gst_init(&argc,&argv);

  src = gst_elementfactory_make("fakesrc","src");
  g_return_val_if_fail(1,src != NULL);
  subbin = gst_bin_new("subbin");
  g_return_val_if_fail(1,subbin != NULL);
  filter = gst_elementfactory_make("identity","filter");
  g_return_val_if_fail(1,filter != NULL);
  sink = gst_elementfactory_make("fakesink","sink");
  g_return_val_if_fail(1,sink != NULL);
  bin = gst_bin_new("bin");
  g_return_val_if_fail(1,bin != NULL);

  g_signal_connectc (G_OBJECT(src),"state_change",
                     G_CALLBACK(state_change),NULL,FALSE);
  g_signal_connectc (G_OBJECT(subbin),"state_change",
                     G_CALLBACK(state_change),NULL,FALSE);
  g_signal_connectc (G_OBJECT(filter),"state_change",
                     G_CALLBACK(state_change),NULL,FALSE);
  g_signal_connectc (G_OBJECT(sink),"state_change",
                     G_CALLBACK(state_change),NULL,FALSE);
  g_signal_connectc (G_OBJECT(bin),"state_change",
                     G_CALLBACK(state_change),NULL,FALSE);

  g_print("STATES: element '%s' starts at state %d(%s)\n",gst_element_get_name(src),
	GST_STATE(src),gst_element_statename(GST_STATE(src)));
  g_print("STATES: element '%s' starts at state %d(%s)\n",gst_element_get_name(subbin),
	GST_STATE(subbin),gst_element_statename(GST_STATE(subbin)));
  g_print("STATES: element '%s' starts at state %d(%s)\n",gst_element_get_name(filter),
	GST_STATE(filter),gst_element_statename(GST_STATE(filter)));
  g_print("STATES: element '%s' starts at state %d(%s)\n",gst_element_get_name(sink),
	GST_STATE(sink),gst_element_statename(GST_STATE(sink)));
  g_print("STATES: element '%s' starts at state %d(%s)\n",gst_element_get_name(bin),
	GST_STATE(bin),gst_element_statename(GST_STATE(bin)));

  gst_bin_add(GST_BIN(subbin),filter);
  gst_element_add_ghost_pad(GST_ELEMENT(bin),gst_element_get_pad(filter,"sink"),"sink");
  gst_element_add_ghost_pad(GST_ELEMENT(bin),gst_element_get_pad(filter,"src"),"src");

  gst_bin_add(GST_BIN(bin),src);
  gst_bin_add(GST_BIN(bin),subbin);
  gst_bin_add(GST_BIN(bin),sink);

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(subbin,"sink"));
  gst_pad_connect(gst_element_get_pad(subbin,"src"),
                  gst_element_get_pad(sink,"sink"));

  gst_element_set_state (bin, GST_STATE_PLAYING);

  gst_bin_iterate (GST_BIN (bin));

  exit (0);
}
