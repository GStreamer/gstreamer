#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gnome.h>
#include <glade/glade.h>
#include <gst/gst.h>

#include "gstplay.h"
#include "callbacks.h"
#include "interface.h"

extern GstElement *src;
extern gboolean picture_shown;
extern GstPlayState state;
extern guchar statusline[];
extern guchar *statustext;
extern GtkFileSelection *open_file_selection;

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
  xml = glade_xml_new(DATADIR "gstplay.glade", "fileselection1");
  /* connect the signals in the interface */
  glade_xml_signal_autoconnect(xml);
  open_file_selection = glade_xml_get_widget(xml, "fileselection1");
}

void
on_toggle_play_toggled                 (GtkToggleButton *togglebutton,
                                        gpointer         user_data)
{
  update_buttons(0);
  change_state(GSTPLAY_PLAYING);

}

void
on_toggle_pause_toggled                 (GtkToggleButton *togglebutton,
                                        gpointer         user_data)
{
  update_buttons(1);
  change_state(GSTPLAY_PAUSE);

}

void
on_toggle_stop_toggled                 (GtkToggleButton *togglebutton,
                                        gpointer         user_data)
{
  update_buttons(2);
  change_state(GSTPLAY_STOPPED);
}

void
on_hscale1_value_changed               (GtkAdjustment 	*adj,
                                        gpointer         user_data)
{
  int size = gst_util_get_int_arg(GTK_OBJECT(src),"size");

  gtk_object_set(GTK_OBJECT(src),"offset",(int)(adj->value*size/100.0),NULL);
  
  if (state != GSTPLAY_PLAYING) {
    show_next_picture();
  }
}

void
on_drawingarea1_configure_event        (GtkWidget *widget, GdkEventConfigure *event,
                                        gpointer         user_data)
{
  gdk_draw_rectangle(widget->window, 
                     widget->style->black_gc,
                     TRUE, 0,0,
                     widget->allocation.width,
                     widget->allocation.height);

  gdk_draw_string(widget->window,widget->style->font,widget->style->white_gc, 8, 15, statustext);
  gdk_draw_string(widget->window,widget->style->font,widget->style->white_gc, widget->allocation.width-100, 15, statusline);
}

void on_about_activate(GtkWidget *widget, gpointer data)
{
  GladeXML *xml;
  xml = glade_xml_new(DATADIR "gstplay.glade", "about");
  /* connect the signals in the interface */
  glade_xml_signal_autoconnect(xml);
}

void on_gstplay_destroy(GtkWidget *widget, gpointer data)
{
  gtk_main_quit();
}

