#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

void eof(GstSrc *src) {
    g_print("have eof, quitting\n");
   exit(0);
}

void mp3parse_info_chain(GstPad *pad,GstBuffer *buf) {
  g_print("sink : got buffer of size %d\n",GST_BUFFER_SIZE(buf));
  gst_buffer_unref(buf);
}

int main(int argc,char *argv[]) {
  GstPipeline *pipeline;
  GstElementFactory *srcfactory, *parsefactory;
  GstElement *src, *parse;
  GstPad *infopad;

  g_print("have %d args\n",argc);

  _gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);
// gst_plugin_load("mp3parse");
  gst_plugin_load_all();

  pipeline = gst_pipeline_new("pipeline");
  g_return_val_if_fail(pipeline != NULL, -1);

  srcfactory = gst_elementfactory_find("disksrc");
  g_return_val_if_fail(srcfactory != NULL, -1);
  parsefactory = gst_elementfactory_find("mp3parse");
  g_return_val_if_fail(parsefactory != NULL, -1);

  src = gst_elementfactory_create(srcfactory,"src");
  g_return_val_if_fail(src != NULL, -1);
  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  g_print("should be using file '%s'\n",argv[1]);
  parse = gst_elementfactory_create(parsefactory,"parse");
  g_return_val_if_fail(parse != NULL, -1);

  infopad = gst_pad_new("sink",GST_PAD_SINK);
  gst_pad_set_chain_function(infopad,mp3parse_info_chain);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(parse));

  gtk_signal_connect(GTK_OBJECT(src),"eos",
                      GTK_SIGNAL_FUNC(eof),NULL);

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(parse,"src"),
                  infopad);

  g_print("setting to RUNNING state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_READY);

  g_print("about to enter loop\n");
  while (1)
    gst_bin_iterate(GST_BIN(pipeline));
}
