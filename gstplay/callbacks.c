#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gnome.h>
#include <glade/glade.h>
#include <gst/gst.h>

#include <sys/stat.h>
#include <unistd.h>

#include "gstplay.h"
#include "callbacks.h"

GtkFileSelection *open_file_selection;

void
on_save1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  printf("file1 activate\n");

}

void
on_save_as1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  printf("file1 activate\n");

}

void
on_media2_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  printf("file1 activate\n");

}
void
on_preferences1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  printf("file1 activate\n");

}

void on_about_activate(GtkWidget *widget, gpointer data)
{
  GladeXML *xml;
  struct stat statbuf;

  if (stat(DATADIR"gstmediaplay.glade", &statbuf) == 0) {
    xml = glade_xml_new (DATADIR"gstmediaplay.glade", "about");
  }
  else {
    xml = glade_xml_new ("gstmediaplay.glade", "about");
  }

  /* connect the signals in the interface */
  glade_xml_signal_autoconnect(xml);
}

void on_gstplay_destroy(GtkWidget *widget, gpointer data)
{
  gst_main_quit();
}

