#include <gst/gst.h>

void eof(GstSrc *src) {
  g_print("have eof, quitting\n");
  exit(0);
}

int main(int argc,char *argv[]) {
  GList *factories;
  GstElementFactory *parsefactory;
  GstElement *bin, *disksrc, *parse, *audiosink;
  GList *padlist;
  guchar *filename;
  int i;

  if (argc == 2)
    filename = argv[1];
  else
    filename = "ctp2.mp3";

  gst_init(&argc,&argv);
  gst_plugin_load_all();
  g_print("\n");

  bin = gst_bin_new("bin");

  disksrc = gst_disksrc_new("disksrc");
  g_print("created disksrc\n");
  gtk_object_set(GTK_OBJECT(disksrc),"location",filename,NULL);
  gtk_object_set(GTK_OBJECT(disksrc),"bytesperread",1048576,NULL);

  /* now it's time to get the parser */
  parsefactory = gst_plugin_find_elementfactory("xing");
  parse = gst_elementfactory_create(parsefactory,"parser");
  if (parse == NULL) {
    g_print("sorry, couldn't create parser\n");
    return 1;
  }


  audiosink = gst_audiosink_new("audiosink");

  gtk_signal_connect(GTK_OBJECT(disksrc),"eof",
                     GTK_SIGNAL_FUNC(eof),NULL);

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(bin),GST_OBJECT(disksrc));
  gst_bin_add(GST_BIN(bin),GST_OBJECT(parse));
  gst_bin_add(GST_BIN(bin),GST_OBJECT(audiosink));

  /* connect src to sink */
  gst_pad_connect(gst_element_get_pad(disksrc,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(parse,"src"),
                  gst_element_get_pad(audiosink,"sink"));

  for (i=0;i<4;i++) {
    g_print("\n");
    gst_disksrc_push(GST_SRC(disksrc));
  }

  gst_object_destroy(GST_OBJECT(audiosink));
  gst_object_destroy(GST_OBJECT(parse));
  gst_object_destroy(GST_OBJECT(disksrc));
  gst_object_destroy(GST_OBJECT(bin));
}

