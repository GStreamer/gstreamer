/*
 * Sample app for element embedding.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#include <gst/xoverlay/xoverlay.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

static void
cb_expose (GtkWidget * w, GdkEventExpose * ev, GstElement * e)
{
  if (GST_IS_X_OVERLAY (e) &&
      !GTK_WIDGET_NO_WINDOW (w) && GTK_WIDGET_REALIZED (w)) {
    gst_x_overlay_set_xwindow_id (GST_X_OVERLAY (e),
        GDK_WINDOW_XWINDOW (w->window));
  }
}

int
main (int argc, char *argv[])
{
  GtkWidget *window, *content;
  GstElement *testsrc, *csp, *videosink, *pipeline;

  gtk_init (&argc, &argv);
  gst_init (&argc, &argv);

  pipeline = gst_element_factory_make ("pipeline", NULL);
  testsrc = gst_element_factory_make ("videotestsrc", NULL);
  csp = gst_element_factory_make ("ffmpegcolorspace", NULL);
  videosink = gst_element_factory_make (DEFAULT_VIDEOSINK, NULL);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size (GTK_WINDOW (window), 640, 480);
  gtk_window_set_title (GTK_WINDOW (window), "My application");
  content = gtk_event_box_new ();
  gtk_container_add (GTK_CONTAINER (window), content);
  g_signal_connect (content, "expose-event", G_CALLBACK (cb_expose), videosink);
  gtk_widget_show_all (window);

  gst_bin_add_many (GST_BIN (pipeline), testsrc, csp, videosink, NULL);
  gst_element_link_many (testsrc, csp, videosink, NULL);

  g_idle_add ((GSourceFunc) gst_bin_iterate, pipeline);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  gtk_main ();

  return 0;
}
