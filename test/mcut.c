#include <gst/gst.h>

void eof(GstSrc *src) {
  g_print("have eof, quitting\n");
  exit(0);
}

int main(int argc,char *argv[]) {
  GstType *type;
  GList *factories;
  GstElementFactory *parsefactory;
  GstElement *bin, *src, *parse, *sink;
  GList *padlist;
  guchar *filename;
  glong length = 0, size = 4180, skip = 8360, offset = 0;

  if (argc == 2)
    filename = argv[1];
  else {
    g_print("sorry, need a filename now\n");
    exit(1);
  }

  gst_init(&argc,&argv);
  gst_plugin_load_all();

  bin = gst_bin_new("bin");

  src = gst_asyncdisksrc_new("src");
  g_print("created disksrc\n");
  gtk_object_set(GTK_OBJECT(src),"location",filename,NULL);
  length = gst_util_get_long_arg(GST_OBJECT(src),"length");
  g_print("file is %d bytes long\n",length);

  /* now it's time to get the parser */
  type = gst_type_get_by_mime("audio/mp3");
  factories = gst_type_get_sinks(type);
  if (factories != NULL)
    parsefactory = GST_ELEMENTFACTORY(factories->data);
  else {
    g_print("sorry, can't find anyone registered to sink 'mp3'\n");
    return 1;
  }
  parse = gst_elementfactory_create(parsefactory,"parser");
  if (parse == NULL) {
    g_print("sorry, couldn't create parser\n");
    return 1;
  }


  sink = gst_audiosink_new("audiosink");

  gtk_signal_connect(GTK_OBJECT(src),"eof",
                     GTK_SIGNAL_FUNC(eof),NULL);

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(bin),src);
  gst_bin_add(GST_BIN(bin),parse);
  gst_bin_add(GST_BIN(bin),sink);

  /* connect src to sink */
  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(parse,"src"),
                  gst_element_get_pad(sink,"sink"));

  while(offset < length) {
    gst_src_push_region(GST_SRC(src),offset,size);
    offset += skip;
  }

  gst_object_destroy(GST_OBJECT(sink));
  gst_object_destroy(GST_OBJECT(parse));
  gst_object_destroy(GST_OBJECT(src));
  gst_object_destroy(GST_OBJECT(bin));
}

