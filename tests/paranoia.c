#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstPipeline *pipeline;
  GstElement *paranoia,*queue,*audio_thread,*audiosink;
  int i;

  DEBUG_ENTER("(%d)",argc);

  gst_init(&argc,&argv);

  pipeline = GST_PIPELINE(gst_pipeline_new("paranoia"));
  g_return_if_fail(pipeline != NULL);
  audio_thread = gst_thread_new("audio_thread");
  g_return_if_fail(audio_thread != NULL);

  paranoia = gst_elementfactory_make("cdparanoia","paranoia");
  g_return_val_if_fail(1,paranoia != NULL);
//  gtk_object_set(GTK_OBJECT(paranoia),"extra_paranoia",FALSE,"cdda2wav_paranoia",FALSE,NULL);

  queue = gst_elementfactory_make("queue","queue");
  g_return_val_if_fail(2,queue != NULL);

  audiosink = gst_elementfactory_make("audiosink","audiosink");
  g_return_val_if_fail(2,audiosink != NULL);

  gst_bin_add(GST_BIN(pipeline),paranoia);
  gst_bin_add(GST_BIN(pipeline),queue);
  gst_bin_add(GST_BIN(audio_thread),audiosink);
  gst_bin_add(GST_BIN(pipeline),audio_thread);
  gst_element_add_ghost_pad(GST_ELEMENT(audio_thread),gst_element_get_pad(audiosink,"sink"));

  gst_element_connect(paranoia,"src",queue,"sink");
  gst_element_connect(queue,"src",audio_thread,"sink");

  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);
  if (GST_STATE(paranoia) != GST_STATE_PLAYING) fprintf(stderr,"error: state not set\n");

  while (1) {
    gst_bin_iterate(GST_BIN(pipeline));
  }
}
