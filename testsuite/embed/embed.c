#include <gst/gst.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>

GtkWidget *window;
GstElement *testsrc;
GstElement *xvideosink;
GstElement *filter;
GdkWindow *wind;
GstElement *pipeline;

#if 0
static int
configure (GtkWidget * widget, GdkEventConfigure * evt, gpointer data)
{
  printf ("configure\n");
  if (wind) {
    gdk_window_resize (wind, evt->width, evt->height);
    gdk_window_reparent (wind, window->window, 0, 0);
  }

  return FALSE;
}

static int
map_event (GtkWidget * widget, GdkEventConfigure * evt, gpointer data)
{
  printf ("map\n");
  return FALSE;
}
#endif

static int
have_xid (GstElement * xv, gint xid, gpointer data)
{
  printf ("have_xid\n");

  wind = gdk_window_foreign_new (xid);
  printf ("gdk_window_reparent() wind=%p window=%p xid=%d\n", wind,
      window->window, xid);
  gdk_window_reparent (wind, window->window, 0, 0);
  gdk_window_show (wind);

  return FALSE;
}

int
main (int argc, char *argv[])
{
  //GValue value = { 0 };

  gtk_init (&argc, &argv);
  gst_init (&argc, &argv);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size (GTK_WINDOW (window), 640, 480);
#if 0
  gtk_signal_connect (GTK_OBJECT (window), "configure_event",
      GTK_SIGNAL_FUNC (configure), NULL);
  gtk_signal_connect (GTK_OBJECT (window), "map",
      GTK_SIGNAL_FUNC (map_event), NULL);
#endif
  gtk_widget_show_all (window);

  pipeline = gst_element_factory_make ("pipeline", NULL);

  testsrc = gst_element_factory_make ("videotestsrc", NULL);
#if 0
  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, 640);
  g_object_set_property (G_OBJECT (testsrc), "width", &value);
  g_value_set_int (&value, 480);
  g_object_set_property (G_OBJECT (testsrc), "height", &value);
#endif

  xvideosink = gst_element_factory_make ("xvideosink", NULL);
  g_object_set (xvideosink, "toplevel", FALSE, NULL);
  g_signal_connect (xvideosink, "have_xid", (GCallback) (have_xid), NULL);

  gst_bin_add (GST_BIN (pipeline), testsrc);
  gst_bin_add (GST_BIN (pipeline), xvideosink);

  gst_element_connect (testsrc, xvideosink);

  if (pipeline == NULL) {
    g_warning ("Could not generate usable pipeline\n");
    return 1;
  }

  g_idle_add ((GSourceFunc) gst_bin_iterate, pipeline);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  gtk_main ();

  return 0;
}
