#include <gnome.h>
#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

gboolean idle_func(gpointer data);

GstElement *videosink;
GstElement *src;

int main(int argc,char *argv[]) {
  GstElement *bin;
  GstElementFactory *srcfactory;
  GstElementFactory *videosinkfactory;

  GtkWidget *appwindow;
  GtkWidget *vbox1;
  GtkWidget *button;
  GtkWidget *draw;


  //_gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);
  gst_plugin_load("v4lsrc");
  gst_plugin_load("videosink");

  gnome_init("Videotest","0.0.1",argc,argv);

  bin = gst_bin_new("bin");

  srcfactory = gst_elementfactory_find("v4lsrc");
  g_return_if_fail(srcfactory != NULL);
  videosinkfactory = gst_elementfactory_find("videosink");
  g_return_if_fail(videosinkfactory != NULL);

  src = gst_elementfactory_create(srcfactory,"src");
  videosink = gst_elementfactory_create(videosinkfactory,"videosink");
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
  //gtk_widget_set_usize (button, 50, 50);
  //gtk_widget_set_usize (button, 0, 0);

  draw = gst_util_get_widget_arg(GTK_OBJECT(videosink),"widget"),
  gtk_box_pack_start (GTK_BOX (vbox1), 
				draw,
  				TRUE, TRUE, 0);
  gtk_widget_show (draw);
	
  gnome_app_set_contents(GNOME_APP(appwindow), vbox1);
								
  gtk_object_set(GTK_OBJECT(appwindow),"allow_grow",TRUE,NULL);
  gtk_object_set(GTK_OBJECT(appwindow),"allow_shrink",TRUE,NULL);

  gtk_widget_show_all(appwindow);

  gst_element_set_state(GST_ELEMENT(bin),GST_STATE_RUNNING);
  gst_element_set_state(GST_ELEMENT(bin),GST_STATE_PLAYING);

  //gtk_object_set(GTK_OBJECT(src),"tune",133250,NULL);
  g_idle_add(idle_func,src);

  gtk_main();
}

gboolean idle_func(gpointer data) {
  static int i=0;
  //g_print("pushing %d\n",i++);
  gst_src_push(GST_SRC(data));
  return TRUE;
}
