#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

void eof(GstSrc *src) {
   g_print("have eof, quitting\n");
   exit(0);
}

int main(int argc,char *argv[]) {
  GstPipeline *pipeline;
  GstElementFactory *srcfactory, *parsefactory, *decodefactory, *playfactory;
  GstElement *src, *parse, *decode, *play;
  GstPad *infopad;

  g_print("have %d args\n",argc);

  _gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);
// gst_plugin_load("mp3parse");
  gst_plugin_load_all();

  pipeline = gst_pipeline_new("pipeline");
  g_return_if_fail(pipeline != NULL);

  srcfactory = gst_elementfactory_find("disksrc");
  g_return_if_fail(srcfactory != NULL);
  parsefactory = gst_elementfactory_find("mp3parse");
  g_return_if_fail(parsefactory != NULL);
  decodefactory = gst_elementfactory_find("mpg123");
  g_return_if_fail(decodefactory != NULL);
  playfactory = gst_elementfactory_find("audiosink");
  g_return_if_fail(playfactory != NULL);

  src = gst_elementfactory_create(srcfactory,"src");
  g_return_if_fail(src != NULL);
  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  g_print("should be using file '%s'\n",argv[1]);
  parse = gst_elementfactory_create(parsefactory,"parse");
  g_return_if_fail(parse != NULL);
  decode = gst_elementfactory_create(decodefactory,"decode");
  g_return_if_fail(decode != NULL);
  play = gst_elementfactory_create(playfactory,"play");
  g_return_if_fail(play != NULL);

  gtk_signal_connect(GTK_OBJECT(src),"eos",
	                       GTK_SIGNAL_FUNC(eof),NULL);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(parse));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(decode));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(play));

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(parse,"src"),
                  gst_element_get_pad(decode,"sink"));
  gst_pad_connect(gst_element_get_pad(decode,"src"),
                  gst_element_get_pad(play,"sink"));

  g_print("setting to RUNNING state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_RUNNING);

  g_print("about to enter loop\n");
  while (1)
    gst_src_push(GST_SRC(src));
}
