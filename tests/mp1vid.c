#include <gst/gst.h>

void new_pad(GstElement *parse,GstPad *pad,GstElement *pipeline) {
  GstElement *audiodecode;
  GstElement *audiosink;

  if (!strncmp(gst_pad_get_name(pad), "audio_", 6)) {
    fprintf(stderr,"have audio pad\n");

    gst_element_set_state(pipeline,GST_STATE_PAUSED);

    audiodecode = gst_elementfactory_make("mad","audiodecode");
    gst_bin_add(GST_BIN(pipeline),audiodecode);
    gst_pad_connect(pad,gst_element_get_pad(audiodecode,"sink"));

    audiosink = gst_elementfactory_make("esdsink","audiosink");
    gst_bin_add(GST_BIN(pipeline),audiosink);
    gst_element_connect(audiodecode,"src",audiosink,"sink");

    gst_element_set_state(pipeline,GST_STATE_PLAYING);

    fprintf(stderr,"done dealing with new audio pad\n");
  }
}

int main(int argc,char *argv[]) {
  GstElement *pipeline, *src, *parse;
  int i;

  gst_init(&argc,&argv);

  pipeline = gst_pipeline_new("pipeline");
  src = gst_elementfactory_make("disksrc","src");
  gtk_object_set(GTK_OBJECT(src),"location","/home/omega/media/AlienSong.mpg",NULL);
  parse = gst_elementfactory_make("mpeg1parse","parse");

  gtk_signal_connect(GTK_OBJECT(parse),"new_pad",
                      GTK_SIGNAL_FUNC(new_pad),pipeline);

  gst_bin_add(GST_BIN(pipeline),src);
  gst_bin_add(GST_BIN(pipeline),parse);

  gst_element_connect(src,"src",parse,"sink");

  gst_element_set_state(pipeline,GST_STATE_PLAYING);
  gst_schedule_show(GST_ELEMENT_SCHED(pipeline));

//  for (i=0;i<10;i++)
  while (1)
    gst_bin_iterate(GST_BIN(pipeline));
}
