#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstType *mp3type;
  GList *factories;
  GstElement *src;
  GList *padlist;

  gst_init(&argc,&argv);
  gst_plugin_load_all();

  bin = gst_bin_new("bin");

  filesrc = gst_filesrc_new("filesrc");
  g_print("created filesrc\n");
  if (argc == 2)
    gst_filesrc_set_filename(filesrc,argv[1]);
  else
    gst_filesrc_set_filename(filesrc,"Thank_you_very_much.au");
  g_print("loaded file '%s'\n",gst_filesrc_get_filename(filesrc));


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


  osssink = gst_osssink_new("osssink");

  gtk_signal_connect(GTK_OBJECT(filesrc),"eof",
                     GTK_SIGNAL_FUNC(eof),NULL);

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(bin),GST_OBJECT(filesrc));
  gst_bin_add(GST_BIN(bin),GST_OBJECT(parse));
  gst_bin_add(GST_BIN(bin),GST_OBJECT(osssink));

  /* connect src to sink */
  gst_pad_connect(gst_element_get_pad(filesrc,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(parse,"src"),
                  gst_element_get_pad(osssink,"sink"));

  while(1)
    gst_filesrc_push(GST_SRC(filesrc));

  gst_object_destroy(GST_OBJECT(osssink));
  gst_object_destroy(GST_OBJECT(parse));
  gst_object_destroy(GST_OBJECT(filesrc));
  gst_object_destroy(GST_OBJECT(bin));
}

