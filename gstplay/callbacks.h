#include <gnome.h>


void
on_file1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_open1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_close1_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_media1_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_play2_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_pause1_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_stop1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data);

void
on_about1_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data);
void
on_hscale1_value_changed               (GtkAdjustment   *adj,
		                                        gpointer         user_data);

void
on_drawingarea1_configure_event        (GtkWidget *widget, GdkEventConfigure *event,
		                                        gpointer         user_data);

