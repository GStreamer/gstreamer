#include <glib.h>
#include <gst/gst.h>

void eof(GstSrc *src) {
  g_print("have eof, quitting\n");
  exit(0);
}

int main(int argc,char *argv[]) {
  GstElement *bin, *disksrc, *p, *audiosink;
  GList *padlist;

  gst_init(&argc,&argv);

  bin = gst_bin_new("bin");

  disksrc = gst_disksrc_new("disksrc");
  g_print("created disksrc\n");
  if (argc == 2)
    gst_disksrc_set_filename(disksrc,argv[1]);
  else
    gst_disksrc_set_filename(disksrc,"mendelssohn.1.raw");
  gst_disksrc_set_bytesperread(disksrc,32768);
  g_print("loaded file '%s'\n",gst_disksrc_get_filename(disksrc));

  p = gst_plugin_find_elementfactory("pipe");
  audiosink = gst_audiosink_new("audiosink");

  gtk_signal_connect(GTK_OBJECT(disksrc),"eof",
                     GTK_SIGNAL_FUNC(eof),NULL);

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(bin),GST_OBJECT(disksrc));
  gst_bin_add(GST_BIN(bin),GST_OBJECT(p));
  gst_bin_add(GST_BIN(bin),GST_OBJECT(audiosink));

  /* connect src to sink */
  gst_pad_connect(gst_element_get_pad(disksrc,"src"),
                  gst_element_get_pad(p,"sink"));
  gst_pad_connect(gst_element_get_pad(p,"src"),
                  gst_element_get_pad(audiosink,"sink"));

  /* set soundcard properties */
  gst_audiosink_set_format(GST_AUDIOSINK(audiosink),AFMT_S16_BE);
  gst_audiosink_set_channels(GST_AUDIOSINK(audiosink),2);
  gst_audiosink_set_frequency(GST_AUDIOSINK(audiosink),44100);

  while(1)
    gst_disksrc_push(GST_SRC(disksrc));

  gst_object_destroy(GST_OBJECT(audiosink));
  gst_object_destroy(GST_OBJECT(disksrc));
  gst_object_destroy(GST_OBJECT(bin));
}

