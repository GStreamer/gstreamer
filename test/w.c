#include <gst/gst.h>

void eof(GstSrc *src) {
  g_print("have eof, quitting\n");
  exit(0);
}

int main(int argc,char *argv[]) {
  GstType *autype;
  GList *factories;
  GstElementFactory *parsefactory;
  GstElement *bin, *disksrc, *parse, *osssink;
  GList *padlist;

  gst_init(&argc,&argv);
  gst_plugin_load_all();

  bin = gst_bin_new("bin");

  disksrc = gst_disksrc_new("disksrc");
  g_print("created disksrc\n");
  if (argc == 2)
    gst_disksrc_set_filename(disksrc,argv[1]);
  else
    gst_disksrc_set_filename(disksrc,"futile.wav");
//  gst_disksrc_set_bytesperread(disksrc,32768);
  g_print("loaded file '%s'\n",gst_disksrc_get_filename(disksrc));


  /* now it's time to get the parser */
  autype = gst_type_get_by_mime("audio/wav");
  factories = gst_type_get_sinks(autype);
  if (factories != NULL)
    parsefactory = GST_ELEMENTFACTORY(factories->data);
  else {
    g_print("sorry, can't find anyone registered to sink 'wav'\n");
    return 1;
  }
  parse = gst_elementfactory_create(parsefactory,"parser");
  if (parse == NULL) {
    g_print("sorry, couldn't create parser\n");
    return 1;
  }


  osssink = gst_osssink_new("osssink");

  gtk_signal_connect(GTK_OBJECT(disksrc),"eof",
                     GTK_SIGNAL_FUNC(eof),NULL);

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(bin),GST_OBJECT(disksrc));
  gst_bin_add(GST_BIN(bin),GST_OBJECT(parse));
  gst_bin_add(GST_BIN(bin),GST_OBJECT(osssink));

  /* connect src to sink */
  gst_pad_connect(gst_element_get_pad(disksrc,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(parse,"src"),
                  gst_element_get_pad(osssink,"sink"));

  while(1) {
    g_print("\n");
    gst_disksrc_push(GST_SRC(disksrc));
  }

  gst_object_destroy(GST_OBJECT(osssink));
  gst_object_destroy(GST_OBJECT(parse));
  gst_object_destroy(GST_OBJECT(disksrc));
  gst_object_destroy(GST_OBJECT(bin));
}

