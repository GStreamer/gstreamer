#include <gst/gst.h>
#include <unistd.h>

void eof(GstSrc *src) {
  g_print("eof\n");
  exit(0);
}

int main(int argc,char *argv[]) {
  guint16 type;
  GList *factories;
  GstElementFactory *parsefactory;
  GstElement *bin, *src, *parse, *sink;
  GList *padlist;
  guchar *filename;

  if (argc == 2)
    filename = argv[1];
  else
    filename = "-";

  gst_init(&argc,&argv);
  gst_plugin_load_all();

  bin = gst_bin_new("bin");

  if (!strcmp(filename,"-"))
    src = gst_fdsrc_new_with_fd("src",STDIN_FILENO);
  else if (!strncmp(filename,"http://",7))
    src = gst_httpsrc_new_with_url("src",filename);
  else
    src = gst_asyncdisksrc_new_with_file("src",filename);

  /* now it's time to get the parser */
  type = gst_type_find_by_mime("audio/mpeg");
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


  sink = gst_osssink_new("osssink");

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

  while(1) {
    g_print(".");
    gst_src_push(GST_SRC(src));
  }

  gst_object_destroy(GST_OBJECT(sink));
  gst_object_destroy(GST_OBJECT(parse));
  gst_object_destroy(GST_OBJECT(src));
  gst_object_destroy(GST_OBJECT(bin));
}

