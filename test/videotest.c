#include <gnome.h>
#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

gboolean idle_func(gpointer data);

GstElement *videosink;
GstElement *videosink2;
GstElement *src;

int main(int argc,char *argv[]) {
  GstElement *bin;
  GstElement *tee;
  GstElementFactory *srcfactory;
  GstElementFactory *videosinkfactory;

  GtkWidget *appwindow;
  GtkWidget *appwindow2;
  GtkWidget *vbox1;
  GtkWidget *button;
  GtkWidget *draw;
  GtkWidget *draw2;


  //_gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);

  gnome_init("Videotest","0.0.1",argc,argv);

  bin = gst_bin_new("bin");

  srcfactory = gst_elementfactory_find("v4lsrc");
  g_return_val_if_fail(srcfactory != NULL,-1);
  videosinkfactory = gst_elementfactory_find("videosink");
  g_return_val_if_fail(videosinkfactory != NULL,-1);

  src = gst_elementfactory_create(srcfactory,"src");
  gtk_object_set(GTK_OBJECT(src),"format",3,NULL);
  gtk_object_set(GTK_OBJECT(src),"width",320,"height",240,NULL);

  videosink = gst_elementfactory_create(videosinkfactory,"videosink");
  gtk_object_set(GTK_OBJECT(videosink),"xv_enabled",FALSE,NULL);
  gtk_object_set(GTK_OBJECT(videosink),"width",320,"height",240,NULL);

  videosink2 = gst_elementfactory_create(videosinkfactory,"videosink2");
  gtk_object_set(GTK_OBJECT(videosink2),"xv_enabled",FALSE,NULL);
  gtk_object_set(GTK_OBJECT(videosink2),"width",320,"height",240,NULL);

  tee = gst_elementfactory_make ("tee", "tee");

  gst_bin_add(GST_BIN(bin),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(bin),GST_ELEMENT(tee));
  gst_bin_add(GST_BIN(bin),GST_ELEMENT(videosink));
  gst_bin_add(GST_BIN(bin),GST_ELEMENT(videosink2));

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(tee,"sink"));
  gst_pad_connect(gst_element_request_pad_by_name (tee,"src%d"),
                  gst_element_get_pad(videosink,"sink"));
  gst_pad_connect(gst_element_request_pad_by_name (tee,"src%d"),
                  gst_element_get_pad(videosink2,"sink"));

  appwindow = gnome_app_new("Videotest","Videotest");
  appwindow2 = gnome_app_new("Videotest2","Videotest2");

  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox1);

  button = gtk_button_new_with_label(_("test"));//_with_label (_("chup"));
  gtk_widget_show (button);
  gtk_box_pack_start (GTK_BOX (vbox1), button, FALSE, FALSE, 0);
  //gtk_widget_set_usize (button, 50, 50);
  //gtk_widget_set_usize (button, 0, 0);

  draw = gst_util_get_pointer_arg(GTK_OBJECT(videosink),"widget"),
  gtk_box_pack_start (GTK_BOX (vbox1), 
				draw,
  				TRUE, TRUE, 0);
  gtk_widget_show (draw);

  draw2 = gst_util_get_pointer_arg(GTK_OBJECT(videosink2),"widget"),
  gtk_widget_show (draw2);
	
  gnome_app_set_contents(GNOME_APP(appwindow), vbox1);
								
  gnome_app_set_contents(GNOME_APP(appwindow2), draw2);

  gtk_object_set(GTK_OBJECT(appwindow),"allow_grow",TRUE,NULL);
  gtk_object_set(GTK_OBJECT(appwindow),"allow_shrink",TRUE,NULL);

  gtk_widget_show_all(appwindow);
  gtk_widget_show_all(appwindow2);

  gst_element_set_state(GST_ELEMENT(bin),GST_STATE_PLAYING);

  gtk_object_set(GTK_OBJECT(src),"bright",32000,"contrast", 32000,NULL);

  //gtk_object_set(GTK_OBJECT(src),"tune",133250,NULL);
  g_idle_add(idle_func,bin);

  gtk_main();

  return 0;
}

gboolean idle_func(gpointer data) {
  //static int i=0;
  //g_print("pushing %d\n",i++);
  gst_bin_iterate(GST_BIN(data));
  return TRUE;
}
