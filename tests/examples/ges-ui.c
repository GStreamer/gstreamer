#include <gtk/gtk.h>
#include <glib.h>
#include <ges/ges.h>

void window_destroy_cb (GtkObject * window, gpointer user);

void quit_item_activate_cb (GtkMenuItem * item, gpointer user);

void delete_item_activate_cb (GtkMenuItem * item, gpointer user);

void add_file_item_activate_cb (GtkMenuItem * item, gpointer user);

GtkWidget *create_ui (void);

void
window_destroy_cb (GtkObject * window, gpointer user)
{
  gtk_main_quit ();
}

void
quit_item_activate_cb (GtkMenuItem * item, gpointer user)
{
  gtk_main_quit ();
}

void
delete_item_activate_cb (GtkMenuItem * item, gpointer user)
{
  g_print ("beleted!");
}

void
add_file_item_activate_cb (GtkMenuItem * item, gpointer user)
{
  g_print ("add file");
}

GtkWidget *
create_ui (void)
{
  GtkBuilder *builder;
  GtkWidget *window;

  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, "ges-ui.glade", NULL);
  window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));
  gtk_builder_connect_signals (builder, NULL);
  g_object_unref (G_OBJECT (builder));

  gtk_widget_show (window);
  return window;
}

int
main (int argc, char *argv[])
{
  GtkWidget *main_window;

  /* intialize GStreamer and GES */
  if (!g_thread_supported ())
    g_thread_init (NULL);

  gst_init (&argc, &argv);
  ges_init ();

  /* initialize UI */
  gtk_init (&argc, &argv);

  main_window = create_ui ();

  gtk_main ();
  return 0;
}
