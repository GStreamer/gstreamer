#include <gnome.h>
#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

gboolean idle_func(gpointer data);

GstElement *videosink;
GstElement *src;

void eof(GstSrc *src) {
  g_print("have eos, quitting\n");
  exit(0);
}

void resize(GtkWidget *wid, GtkRequisition *req, gpointer sink) {
  g_print("have resize %d %d\n", req->width, req->height);
  gtk_object_set(GTK_OBJECT(videosink),"width",req->width,"height",req->height,NULL);
}


int main(int argc,char *argv[]) {
  GstElement *bin;
  GstElementFactory *srcfactory;
  GstElementFactory *videosinkfactory;

  GtkWidget *appwindow;


	_gst_plugin_spew = TRUE;
	gst_init(&argc,&argv);
	gst_plugin_load_all();

  gnome_init("Videotest","0.0.1",argc,argv);

  bin = gst_bin_new("bin");

  srcfactory = gst_elementfactory_find("v4lsrc");
  g_return_if_fail(srcfactory != NULL);
  videosinkfactory = gst_elementfactory_find("videosink");
  g_return_if_fail(videosinkfactory != NULL);

  src = gst_elementfactory_create(srcfactory,"src");
  videosink = gst_elementfactory_create(videosinkfactory,"videosink");
  gtk_object_set(GTK_OBJECT(videosink),"width",640,"height",480,NULL);


  gst_bin_add(GST_BIN(bin),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(bin),GST_ELEMENT(videosink));

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(videosink,"sink"));

	gtk_signal_connect(GTK_OBJECT(src),"eos",
							         GTK_SIGNAL_FUNC(eof),NULL);

  appwindow = gnome_app_new("Videotest","Videotest");
  gnome_app_set_contents(GNOME_APP(appwindow),
									gst_util_get_widget_arg(GTK_OBJECT(videosink),"widget"));
  gtk_object_set(GTK_OBJECT(appwindow),"allow_grow",TRUE,NULL);
  gtk_object_set(GTK_OBJECT(appwindow),"allow_shrink",TRUE,NULL);
	gtk_signal_connect(GTK_OBJECT(appwindow),"size-request",
						         GTK_SIGNAL_FUNC(resize),videosink);

  gtk_widget_show_all(appwindow);

  gst_element_set_state(GST_ELEMENT(bin),GST_STATE_RUNNING);
  gst_element_set_state(GST_ELEMENT(bin),GST_STATE_PLAYING);

  g_idle_add(idle_func,src);

  gtk_main();
}

gboolean idle_func(gpointer data) {
	static int i=0;
  //g_print("pushing %d\n",i++);
  gst_src_push(GST_SRC(data));
  return TRUE;
}
