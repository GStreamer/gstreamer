#include <gst/gst.h>

#include "mem.h"

extern gboolean _gst_plugin_spew;

int main(int argc,char *argv[]) {
  GstElement *pipeline, *decodethread, *playthread;
  GstElement *src, *parse, *decode, *play;
  GstElement *queue;
  GstPad *infopad;

  g_print("have %d args\n",argc);

  _gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);
// gst_plugin_load("ac3parse");
  gst_plugin_load_all();

  pipeline = gst_elementfactory_make("pipeline","ac3player");
  g_return_if_fail(pipeline != NULL);
  decodethread = gst_elementfactory_make("thread","decodethread");
  g_return_if_fail(decodethread != NULL);
  playthread = gst_elementfactory_make("thread","playthread");
  g_return_if_fail(playthread != NULL);
  queue = gst_elementfactory_make("queue","queue");
  g_return_if_fail(queue != NULL);

  src = gst_elementfactory_make("disksrc","src");
  g_return_if_fail(src != NULL);
  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  g_print("should be using file '%s'\n",argv[1]);
  parse = gst_elementfactory_make("ac3parse","parse");
  g_return_if_fail(parse != NULL);
  decode = gst_elementfactory_make("ac3dec","decode");
  g_return_if_fail(decode != NULL);
  play = gst_elementfactory_make("audiosink","play");
  g_return_if_fail(play != NULL);

  // construct the decode thread
  g_print("constructing the decode thread\n");
  gst_bin_add(GST_BIN(decodethread),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(decodethread),GST_ELEMENT(parse));
  gst_bin_add(GST_BIN(decodethread),GST_ELEMENT(decode));
  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(parse,"src"),
                  gst_element_get_pad(decode,"sink"));
  gst_element_add_ghost_pad(GST_ELEMENT(decodethread),
                            gst_element_get_pad(decode,"src"));

  // construct the play thread
  g_print("constructing the play thread\n");
  gst_bin_add(GST_BIN(playthread),GST_ELEMENT(play));
  gst_element_add_ghost_pad(GST_ELEMENT(playthread),
                            gst_element_get_pad(play,"sink"));

  // construct the outer pipeline
  g_print("constructing the main pipeline\n");
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(decodethread));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(queue));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(playthread));
  gst_pad_connect(gst_element_get_pad(decodethread,"src"),
                  gst_element_get_pad(queue,"sink"));
  gst_pad_connect(gst_element_get_pad(queue,"src"),
                  gst_element_get_pad(playthread,"sink"));

  // set thread start state
  gtk_object_set(GTK_OBJECT(decodethread),"create_thread",TRUE,NULL);
  gtk_object_set(GTK_OBJECT(playthread),"create_thread",FALSE,NULL);

  g_print("setting to RUNNING state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_RUNNING);
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

//  sleep(1);
  g_print("about to enter loop\n");
  while (1) {
    gst_thread_iterate(GST_THREAD(playthread));
    g_print("using %d bytes\n",vmsize());
  }

  xmlSaveFile("ac3play.xml",gst_xml_write(GST_ELEMENT(pipeline)));
}
