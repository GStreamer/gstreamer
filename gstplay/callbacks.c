#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gnome.h>
#include <glade/glade.h>
#include <gst/gst.h>

#include "gstplay.h"
#include "callbacks.h"
#include "interface.h"

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

void
on_open2_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
  GladeXML *xml;
  xml = glade_xml_new(DATADIR "gstmediaplay.glade", "fileselection1");
  /* connect the signals in the interface */
  glade_xml_signal_autoconnect(xml);
  open_file_selection = GTK_FILE_SELECTION(glade_xml_get_widget(xml, "fileselection1"));
}

void
on_hscale1_value_changed               (GtkAdjustment 	*adj,
                                        gpointer         user_data)
{
  //int size = gst_util_get_int_arg(GTK_OBJECT(src),"size");

  //gtk_object_set(GTK_OBJECT(src),"offset",(int)(adj->value*size/100.0),NULL);
  
  //if (state != GSTPLAY_PLAYING) {
  //  show_next_picture();
  //}
}

void on_about_activate(GtkWidget *widget, gpointer data)
{
  GladeXML *xml;
  xml = glade_xml_new(DATADIR "gstmediaplay.glade", "about");
  /* connect the signals in the interface */
  glade_xml_signal_autoconnect(xml);
}

void on_gstplay_destroy(GtkWidget *widget, gpointer data)
{
  gst_main_quit();
}

