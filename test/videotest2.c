#include <gnome.h>
#include <gst/gst.h>

static gboolean
idle_func (gpointer data) 
{
  gst_bin_iterate(GST_BIN(data));

  return TRUE;
}

int 
main (int argc,char *argv[]) 
{
  GstElement *bin;
  GstElement *src;
  GstElement *videosink;

  GtkWidget *appwindow;
  GtkWidget *vbox1;
  GtkWidget *button;
  guint32 draw;
  GtkWidget *gtk_socket;


  gst_init(&argc,&argv);

  gnome_init("Videotest","0.0.1",argc,argv);

  bin = gst_pipeline_new("pipeline");

  src = gst_elementfactory_make ("v4lsrc", "src");
  gtk_object_set(GTK_OBJECT(src),"format",9,NULL);
  gtk_object_set(GTK_OBJECT(src),"width",320,"height",240,NULL);
  //gtk_object_set(GTK_OBJECT(src),"width",100,"height",100,NULL);

  videosink = gst_elementfactory_make ("xvideosink", "videosink");
  gtk_object_set(GTK_OBJECT(videosink),"width",320,"height",240,NULL);

  gst_bin_add(GST_BIN(bin),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(bin),GST_ELEMENT(videosink));

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(videosink,"sink"));

  appwindow = gnome_app_new("Videotest","Videotest");

  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox1);

  button = gtk_button_new_with_label(_("test"));//_with_label (_("chup"));
  gtk_widget_show (button);
  gtk_box_pack_start (GTK_BOX (vbox1), button, FALSE, FALSE, 0);

  draw = gst_util_get_int_arg (GTK_OBJECT (videosink), "xid"),

  gtk_socket = gtk_socket_new ();
  gtk_widget_show (gtk_socket);

  gnome_app_set_contents(GNOME_APP(appwindow), vbox1);

  gtk_box_pack_start (GTK_BOX (vbox1),
                      GTK_WIDGET(gtk_socket),
                      TRUE, TRUE, 0);

  gtk_widget_realize (gtk_socket);
  gtk_socket_steal (GTK_SOCKET (gtk_socket), draw);
								
  gtk_object_set(GTK_OBJECT(appwindow),"allow_grow",TRUE,NULL);
  gtk_object_set(GTK_OBJECT(appwindow),"allow_shrink",TRUE,NULL);

  gtk_widget_show_all(appwindow);

  gst_element_set_state(GST_ELEMENT(bin),GST_STATE_PLAYING);

  gtk_object_set(GTK_OBJECT(src),"bright",32000,"contrast", 32000,NULL);

  //gtk_object_set(GTK_OBJECT(src),"tune",133250,NULL);
  g_idle_add(idle_func,bin);

  gtk_main();

  exit (0);
}

