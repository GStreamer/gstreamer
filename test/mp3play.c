#include <gst/gst.h>


void eof(GstElement *src) {
   g_print("have eof, quitting\n");
   exit(0);
}

int main(int argc,char *argv[]) {
  GstElementFactory *srcfactory, *parsefactory, *decodefactory, *playfactory;
  GstElement *pipeline, *src, *parse, *decode, *play;

  g_print("have %d args\n",argc);

  gst_init(&argc,&argv);

  pipeline = gst_pipeline_new("pipeline");
  g_return_val_if_fail(pipeline != NULL, -1);

  srcfactory = gst_elementfactory_find("disksrc");
  g_return_val_if_fail(srcfactory != NULL, -1);
  parsefactory = gst_elementfactory_find("mp3parse");
  g_return_val_if_fail(parsefactory != NULL, -1);
  decodefactory = gst_elementfactory_find("mpg123");
  g_return_val_if_fail(decodefactory != NULL, -1);
  playfactory = gst_elementfactory_find("osssink");
  g_return_val_if_fail(playfactory != NULL, -1);

  src = gst_elementfactory_create(srcfactory,"src");
  g_return_val_if_fail(src != NULL, -1);
  g_object_set(G_OBJECT(src),"location",argv[1], NULL);
  g_print("should be using file '%s'\n",argv[1]);
  parse = gst_elementfactory_create(parsefactory,"parse");
  g_return_val_if_fail(parse != NULL, -1);
  decode = gst_elementfactory_create(decodefactory,"decode");
  g_return_val_if_fail(decode != NULL, -1);
  play = gst_elementfactory_create(playfactory,"play");
  g_return_val_if_fail(play != NULL, -1);

  g_signal_connect(G_OBJECT(src),"eos",
	            G_CALLBACK(eof),NULL);

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

  g_print("setting to READY state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_READY);
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

  g_print("about to enter loop\n");
  while (1) {
    gst_bin_iterate(GST_BIN(pipeline));
  }
}
