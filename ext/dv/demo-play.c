#include <gnome.h>
#include <gst/gst.h>

static gboolean
idle_func (gpointer data)
{
  gst_bin_iterate (GST_BIN (data));

  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *bin;
  GstElement *src;
  GstElement *dvdec;

  /*GstElement *cspace; */
  GstElement *deint;
  GstElement *videosink;

  GtkWidget *appwindow;
  GtkWidget *vbox1;
  GtkWidget *button;
  guint32 draw;
  GtkWidget *gtk_socket;


  gst_init (&argc, &argv);

  gnome_init ("Videotest", "0.0.1", argc, argv);

  bin = gst_pipeline_new ("pipeline");

  if (argc == 1) {
    src = gst_element_factory_make ("dv1394src", "src");
  } else {
    src = gst_element_factory_make ("filesrc", "src");
    g_object_set (G_OBJECT (src), "location", argv[1], "bytesperread", 480,
	NULL);
  }
  dvdec = gst_element_factory_make ("dvdec", "decoder");
  if (!dvdec)
    fprintf (stderr, "no dvdec\n"), exit (1);
/*  cspace = gst_element_factory_make ("colorspace", "cspace"); */
  deint = gst_element_factory_make ("deinterlace", "deinterlace");
  videosink = gst_element_factory_make ("xvideosink", "videosink");
  if (!videosink)
    fprintf (stderr, "no dvdec\n"), exit (1);
  g_object_set (G_OBJECT (videosink), "width", 720, "height", 576, NULL);

  gst_bin_add (GST_BIN (bin), GST_ELEMENT (src));
  gst_bin_add (GST_BIN (bin), GST_ELEMENT (dvdec));
/*  gst_bin_add(GST_BIN(bin),GST_ELEMENT(cspace)); */
  gst_bin_add (GST_BIN (bin), GST_ELEMENT (videosink));

  gst_element_link (src, "src", dvdec, "sink");
/*  gst_element_link(cspace,"src",videosink,"sink"); */
/*  gst_element_link(dvdec,"video",cspace,"sink"); */
  gst_element_link (dvdec, "video", deint, "sink");
  gst_element_link (deint, "src", videosink, "sink");

  appwindow = gnome_app_new ("Videotest", "Videotest");

  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox1);

  button = gtk_button_new_with_label ("test");
  gtk_widget_show (button);
  gtk_box_pack_start (GTK_BOX (vbox1), button, FALSE, FALSE, 0);

  draw = gst_util_get_int_arg (GTK_OBJECT (videosink), "xid"),
      gtk_socket = gtk_socket_new ();
  gtk_widget_set_usize (gtk_socket, 720, 576);
  gtk_widget_show (gtk_socket);

  gnome_app_set_contents (GNOME_APP (appwindow), vbox1);

  gtk_box_pack_start (GTK_BOX (vbox1), GTK_WIDGET (gtk_socket), TRUE, TRUE, 0);

  gtk_widget_realize (gtk_socket);
  gtk_socket_steal (GTK_SOCKET (gtk_socket), draw);

  gtk_object_set (GTK_OBJECT (appwindow), "allow_grow", TRUE, NULL);
  gtk_object_set (GTK_OBJECT (appwindow), "allow_shrink", TRUE, NULL);

  gtk_widget_show_all (appwindow);

#ifndef GST_DISABLE_LOADSAVE
  xmlSaveFile ("dvshow.xml", gst_xml_write (GST_ELEMENT (bin)));
#endif

  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);

  g_idle_add (idle_func, bin);

  gtk_main ();

  exit (0);
}
