#include <gst/gst.h>
#include <gnome.h>

static void
gst_play_have_type (GstElement *sink, GstElement *sink2, gpointer data)
{
  GST_DEBUG (0,"GstPipeline: play have type %p\n", (gboolean *)data);

  *(gboolean *)data = TRUE;
}

gboolean idle_func(gpointer data) {
  return gst_bin_iterate(GST_BIN(data));
}

static GstCaps*
gst_play_typefind (GstBin *bin, GstElement *element)
{
  gboolean found = FALSE;
  GstElement *typefind;
  GstCaps *caps = NULL;

  GST_DEBUG (0,"GstPipeline: typefind for element \"%s\" %p\n",
             GST_ELEMENT_NAME(element), &found);

  typefind = gst_elementfactory_make ("typefind", "typefind");
  g_return_val_if_fail (typefind != NULL, FALSE);

  gtk_signal_connect (GTK_OBJECT (typefind), "have_type",
                      GTK_SIGNAL_FUNC (gst_play_have_type), &found);

  gst_pad_connect (gst_element_get_pad (element, "src"),
                   gst_element_get_pad (typefind, "sink"));

  gst_bin_add (bin, typefind);

  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);

  // push a buffer... the have_type signal handler will set the found flag
  gst_bin_iterate (bin);

  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);

  caps = gst_pad_get_caps (gst_element_get_pad (element, "src"));

  gst_pad_disconnect (gst_element_get_pad (element, "src"),
                      gst_element_get_pad (typefind, "sink"));
  gst_bin_remove (bin, typefind);
  gst_object_unref (GST_OBJECT (typefind));

  return caps;
}

int main(int argc,char *argv[]) 
{
  GstElement *disksrc, *audiosink, *videosink;
  GstElement *bin;
  GtkWidget *appwindow;
  GstCaps *srccaps;
  GstElement *new_element;
  GstAutoplug *autoplug;

  g_thread_init(NULL);
  gst_init(&argc,&argv);
  gnome_init("autoplug","0.0.1", argc,argv);

  if (argc != 2) {
    g_print("usage: %s <filename>\n", argv[0]);
    exit(-1);
  }

  /* create a new bin to hold the elements */
  bin = gst_bin_new("bin");
  g_assert(bin != NULL);

  /* create a disk reader */
  disksrc = gst_elementfactory_make("disksrc", "disk_source");
  g_assert(disksrc != NULL);
  gtk_object_set(GTK_OBJECT(disksrc),"location", argv[1],NULL);

  gst_bin_add (GST_BIN (bin), disksrc);

  srccaps = gst_play_typefind (GST_BIN (bin), disksrc);

  if (!srccaps) {
    g_print ("could not autoplug, unknown media type...\n");
    exit (-1);
  }
  
  /* and an audio sink */
  audiosink = gst_elementfactory_make("audiosink", "play_audio");
  g_assert(audiosink != NULL);

  /* and an video sink */
  videosink = gst_elementfactory_make("videosink", "play_video");
  g_assert(videosink != NULL);
  gtk_object_set(GTK_OBJECT(videosink),"xv_enabled", FALSE,NULL);

  autoplug = gst_autoplugfactory_make ("staticrender");
  g_assert (autoplug != NULL);

  new_element = gst_autoplug_to_renderers (autoplug,
           srccaps,
           videosink,
           audiosink,
           NULL);

  if (!new_element) {
    g_print ("could not autoplug, no suitable codecs found...\n");
    exit (-1);
  }

  gst_bin_add (GST_BIN (bin), new_element);

  gst_element_connect (disksrc, "src", new_element, "sink");

  appwindow = gnome_app_new("autoplug demo","autoplug demo");
  gnome_app_set_contents(GNOME_APP(appwindow),
    gst_util_get_widget_arg(GTK_OBJECT(videosink),"widget"));
  gtk_widget_show_all(appwindow);

  xmlSaveFile("xmlTest.gst", gst_xml_write(GST_ELEMENT(bin)));

  /* start playing */
  gst_element_set_state(GST_ELEMENT(bin), GST_STATE_PLAYING);

  gtk_idle_add(idle_func, bin);

  gst_main();

  /* stop the bin */
  gst_element_set_state(GST_ELEMENT(bin), GST_STATE_NULL);

  gst_pipeline_destroy(bin);

  exit(0);
}

