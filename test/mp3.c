#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

static gboolean playing = TRUE;

void eof(GstElement *src) {
  GST_DEBUG(0,"have EOF\n");
  playing = FALSE;
}

int main(int argc,char *argv[]) {
  GstElement *bin;
  GstElementFactory *srcfactory;
  GstElement *src;
  GstElementFactory *mp3factory;
  GstElement *mp3;
  GstElementFactory *sinkfactory;
  GstElement *sink;

  _gst_plugin_spew = TRUE;

  gst_init(&argc,&argv);
  gst_plugin_load_all();

  bin = gst_bin_new("bin");

  srcfactory = gst_elementfactory_find("disksrc");
  if (argc == 3)
    mp3factory = gst_elementfactory_find(argv[2]);
  else
    mp3factory = gst_elementfactory_find("xa");
  sinkfactory = gst_elementfactory_find("osssink");

  src = gst_elementfactory_create(srcfactory,"src");
  g_return_val_if_fail(src != NULL, -1);
  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  mp3 = gst_elementfactory_create(mp3factory,"mp3");
  g_return_val_if_fail(mp3 != NULL, -1);
  sink = gst_elementfactory_create(sinkfactory,"sink");
  g_return_val_if_fail(sink != NULL, -1);

  gst_bin_add(GST_BIN(bin),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(bin),GST_ELEMENT(mp3));
  gst_bin_add(GST_BIN(bin),GST_ELEMENT(sink));

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(mp3,"sink"));
  gst_pad_connect(gst_element_get_pad(mp3,"src"),
                  gst_element_get_pad(sink,"sink"));

  gtk_signal_connect(GTK_OBJECT(src),"eof",
                     GTK_SIGNAL_FUNC(eof),NULL);   

  gst_element_set_state(GST_ELEMENT(bin),GST_STATE_READY);
  gst_element_set_state(GST_ELEMENT(bin),GST_STATE_PLAYING);

  while (playing)
    gst_bin_iterate(GST_BIN(bin));

  return 0;
}
