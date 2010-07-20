#include <gtk/gtk.h>

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

int
main (int argc, char *argv[])
{
  GtkBuilder *builder;
  GtkWidget *window;
  gtk_init (&argc, &argv);

  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, "ges-ui.glade", NULL);
  window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));
  gtk_builder_connect_signals (builder, NULL);
  g_object_unref (G_OBJECT (builder));

  gtk_widget_show (window);
  gtk_main ();
  return 0;
}
