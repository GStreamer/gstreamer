#include <gst/gst.h>

extern gboolean _gst_plugin_spew;
GstPipeline *pipeline;

void eof(GstSrc *src) {
  g_print("have eof, quitting\n");
   exit(0);
}

void mp1parse_info_chain(GstPad *pad,GstBuffer *buf) {
  g_print("sink : got buffer of size %d\n",GST_BUFFER_SIZE(buf));
  gst_buffer_unref(buf);
}

void new_pad_created(GstElement *parse, GstPad *pad) {
  GstElementFactory *parsefactory, *decodefactory, *playfactory;
  GstElement *parse_audio, *decode, *play;
  GstPipeline *audio_pipeline;

  g_print("a new pad %s was created\n", gst_pad_get_name(pad));

  // connect to audio pad
  if (strncmp(gst_pad_get_name(pad), "audio_", 6) == 0) {

	 parsefactory = gst_elementfactory_find("mp3parse");
	 g_return_if_fail(parsefactory != NULL);
	 decodefactory = gst_elementfactory_find("mpg123");
	 g_return_if_fail(decodefactory != NULL);
	 playfactory = gst_elementfactory_find("audiosink");
	 g_return_if_fail(playfactory != NULL);

	 parse_audio = gst_elementfactory_create(parsefactory,"parse");
	 g_return_if_fail(parse_audio != NULL);
	 decode = gst_elementfactory_create(decodefactory,"decode");
	 g_return_if_fail(decode != NULL);
	 play = gst_elementfactory_create(playfactory,"play");
	 g_return_if_fail(play != NULL);

    audio_pipeline = gst_pipeline_new("audio_pipeline");
    g_return_if_fail(audio_pipeline != NULL);

	 gst_bin_add(GST_BIN(audio_pipeline),GST_ELEMENT(parse_audio));
	 gst_bin_add(GST_BIN(audio_pipeline),GST_ELEMENT(decode));
	 gst_bin_add(GST_BIN(audio_pipeline),GST_ELEMENT(play));
	 gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_pipeline));

    gst_pad_connect(gst_element_get_pad(parse,gst_pad_get_name(pad)),
	                  gst_element_get_pad(parse_audio,"sink"));
	 gst_pad_connect(gst_element_get_pad(parse_audio,"src"),
	                  gst_element_get_pad(decode,"sink"));
	 gst_pad_connect(gst_element_get_pad(decode,"src"),
	                  gst_element_get_pad(play,"sink"));

	 g_print("setting to RUNNING state\n");
	 gst_element_set_state(GST_ELEMENT(audio_pipeline),GST_STATE_RUNNING);

  }
}

int main(int argc,char *argv[]) {
  GstElementFactory *srcfactory, *parsefactory;
  GstElement *src, *parse;

  g_print("have %d args\n",argc);

  _gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);
// gst_plugin_load("mp3parse");
  gst_plugin_load_all();

  pipeline = gst_pipeline_new("pipeline");
  g_return_if_fail(pipeline != NULL);

  srcfactory = gst_elementfactory_find("disksrc");
  g_return_if_fail(srcfactory != NULL);
  parsefactory = gst_elementfactory_find("mpeg1parse");
  g_return_if_fail(parsefactory != NULL);

  src = gst_elementfactory_create(srcfactory,"src");
  g_return_if_fail(src != NULL);
  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  g_print("should be using file '%s'\n",argv[1]);
  parse = gst_elementfactory_create(parsefactory,"parse");
  g_return_if_fail(parse != NULL);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(parse));

  gtk_signal_connect(GTK_OBJECT(parse),"new_pad",
                      GTK_SIGNAL_FUNC(new_pad_created),NULL);

  gtk_signal_connect(GTK_OBJECT(src),"eos",
                      GTK_SIGNAL_FUNC(eof),NULL);

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));

  g_print("setting to RUNNING state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_RUNNING);

  g_print("about to enter loop\n");
  while (1)
    gst_src_push(GST_SRC(src));
}
