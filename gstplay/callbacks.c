#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gnome.h>
#include <gst/gst.h>

#include "callbacks.h"
#include "interface.h"
#include "support.h"

extern GstElement *src;

void
on_file1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_open1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_close1_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_media1_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_play2_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_pause1_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_stop1_activate                      (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}


void
on_about1_activate                     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{

}

void
on_hscale1_value_changed               (GtkAdjustment 	*adj,
                                        gpointer         user_data)
{
  gtk_object_set(GTK_OBJECT(src),"offset",10000000,NULL);
  
}

