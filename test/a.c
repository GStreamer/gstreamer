#include <gst/gst.h>

void eof(GstElement *src) {
  g_print("have eof, quitting\n");
  exit(0);
}

int main(int argc,char *argv[]) {
  GstType *autype;
  GList *factories;
  GstElementFactory *parsefactory;
  GstElement *bin, *disksrc, *parse, *audiosink;
  GList *padlist;

  gst_init(&argc,&argv);
  gst_plugin_load_all();

  bin = gst_bin_new("bin");

  disksrc = gst_disksrc_new("disksrc");
  g_print("created disksrc\n");
  if (argc == 2)
    gst_disksrc_set_filename(disksrc,argv[1]);
  else
    gst_disksrc_set_filename(disksrc,"Thank_you_very_much.au");
  g_print("loaded file '%s'\n",gst_disksrc_get_filename(disksrc));


  /* now it's time to get the parser */
  autype = gst_type_get_by_mime("audio/au");
  factories = gst_type_get_sinks(autype);
  if (factories != NULL)
    parsefactory = GST_ELEMENTFACTORY(factories->data);
  else {
    g_print("sorry, can't find anyone registered to sink 'au'\n");
    return 1;
  }
  parse = gst_elementfactory_create(parsefactory,"parser");
  if (parse == NULL) {
    g_print("sorry, couldn't create parser\n");
    return 1;
  }


  audiosink = gst_audiosink_new("audiosink");

  gtk_signal_connect(GTK_OBJECT(disksrc),"eos",
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

  while(1)
    gst_disksrc_push(GST_SRC(disksrc));

  gst_object_destroy(GST_OBJECT(audiosink));
  gst_object_destroy(GST_OBJECT(parse));
  gst_object_destroy(GST_OBJECT(disksrc));
  gst_object_destroy(GST_OBJECT(bin));
}

