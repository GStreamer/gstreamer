#include <glib.h>
#include <gst/gst.h>

void eof(GstSrc *src) {
  g_print("have eof, quitting\n");
  exit(0);
}

int main(int argc,char *argv[]) {
  GstElement *bin, *disksrc, *osssink;
  GList *padlist;

  gst_init(&argc,&argv);

  bin = gst_bin_new("bin");

  disksrc = gst_disksrc_new("disksrc");
  g_print("created disksrc\n");
  if (argc == 2)
    gst_disksrc_set_filename(disksrc,argv[1]);
  else
    gst_disksrc_set_filename(disksrc,"mendelssohn.1.raw");
  gtk_object_set(GTK_OBJECT(disksrc),"bytesperread",32768,NULL);
  g_print("loaded file '%s'\n",gst_disksrc_get_filename(disksrc));

  osssink = gst_osssink_new("osssink");

  gtk_signal_connect(GTK_OBJECT(disksrc),"eof",
                     GTK_SIGNAL_FUNC(eof),NULL);

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(bin),GST_OBJECT(disksrc));
  gst_bin_add(GST_BIN(bin),GST_OBJECT(osssink));

  /* connect src to sink */
  gst_pad_connect(gst_element_get_pad(disksrc,"src"),
                  gst_element_get_pad(osssink,"sink"));

  /* set soundcard properties */
  gst_osssink_set_format(GST_AUDIOSINK(osssink),AFMT_S16_BE);
  gst_osssink_set_channels(GST_AUDIOSINK(osssink),2);
  gst_osssink_set_frequency(GST_AUDIOSINK(osssink),44100);

  while(1)
    gst_disksrc_push(GST_SRC(disksrc));

  gst_object_destroy(GST_OBJECT(osssink));
  gst_object_destroy(GST_OBJECT(disksrc));
  gst_object_destroy(GST_OBJECT(bin));
}

