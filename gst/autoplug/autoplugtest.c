#include <gst/gst.h>

GstElement *pipeline, *src, *autobin, *cache, *typefind, *decoder, *sink;

void cache_empty(GstElement *element, gpointer private) {
  fprintf(stderr,"have cache empty\n");

  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  gst_element_disconnect(src,"src",cache,"sink");
  gst_schedule_show (GST_ELEMENT_SCHED(pipeline));
  gst_element_disconnect(cache,"src",decoder,"sink");
  gst_schedule_show (GST_ELEMENT_SCHED(pipeline));
  gst_bin_remove (GST_BIN(autobin), cache);
  gst_schedule_show (GST_ELEMENT_SCHED(pipeline));
  gst_element_connect(src,"src",decoder,"sink");
  gst_schedule_show (GST_ELEMENT_SCHED(pipeline));

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  gst_schedule_show (GST_ELEMENT_SCHED(pipeline));

  fprintf(stderr,"done with cache_empty\n");
}

void have_type(GstElement *element, GstCaps *caps, GstCaps **private_caps) {
  fprintf(stderr,"have caps, mime type is %s\n",gst_caps_get_mime(caps));

  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  // disconnect the typefind from the pipeline and remove it
  gst_element_disconnect(cache,"src",typefind,"sink");
  gst_bin_remove(GST_BIN(autobin),typefind);

  gst_schedule_show (GST_ELEMENT_SCHED(pipeline));

  if (strstr(gst_caps_get_mime(caps),"mp3")) {
    decoder = gst_elementfactory_make ("mad","decoder");
    sink = gst_elementfactory_make ("osssink","sink");
    gst_bin_add(GST_BIN(autobin),decoder);
    gst_bin_add(GST_BIN(autobin),sink);
    gst_element_connect(decoder,"src",sink,"sink");

    gtk_object_set (GTK_OBJECT(cache), "reset", TRUE, NULL);

    gst_element_connect(cache,"src",decoder,"sink");
  }
  else if (strstr(gst_caps_get_mime(caps),"x-ogg")) {
    decoder = gst_elementfactory_make ("vorbisdec","decoder");
    sink = gst_elementfactory_make ("osssink","sink");
    gst_bin_add(GST_BIN(autobin),decoder);
    gst_bin_add(GST_BIN(autobin),sink);
    gst_element_connect(decoder,"src",sink,"sink");

    gtk_object_set (GTK_OBJECT(cache), "reset", TRUE, NULL);

    gst_element_connect(cache,"src",decoder,"sink");
  }

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fprintf(stderr,"done with have_type signal\n");
}

int main (int argc,char *argv[]) {
  GstCaps *caps;
  int i;

  gst_init(&argc,&argv);

  pipeline = gst_pipeline_new("pipeline");
  src = gst_elementfactory_make ("disksrc","src");
  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  gst_bin_add (GST_BIN(pipeline),src);

  autobin = gst_bin_new("autobin");
  cache = gst_elementfactory_make ("autoplugcache","cache");
  gtk_signal_connect (GTK_OBJECT(cache),"cache_empty",GTK_SIGNAL_FUNC(cache_empty),NULL);
  typefind = gst_elementfactory_make ("typefind", "typefind");
  gtk_signal_connect (GTK_OBJECT(typefind),"have_type",GTK_SIGNAL_FUNC(have_type),&caps);
  gst_bin_add (GST_BIN(autobin),cache);
  gst_bin_add (GST_BIN(autobin),typefind);
  gst_element_connect(cache,"src",typefind,"sink");
  gst_element_add_ghost_pad(autobin,gst_element_get_pad(cache,"sink"),"sink");

  gst_bin_add (GST_BIN(pipeline), autobin);
  gst_element_connect (src,"src",autobin,"sink");

  gst_element_set_state(pipeline,GST_STATE_PLAYING);

  while (1)
    gst_bin_iterate(GST_BIN(pipeline));
}
