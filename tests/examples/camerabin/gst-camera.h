/*
 * GStreamer
 * Copyright (C) 2008 Nokia Corporation <multimedia@maemo.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/*
 * This is a demo application to test the camerabin element.
 * If you have question don't hesitate in contact me edgard.lima@indt.org.br
 */

#ifndef __GST_CAMERA_BIN_H__
#define __GST_CAMERA_BIN_H__

#include <gtk/gtk.h>

void
on_windowMain_delete_event (GtkWidget * widget, GdkEvent * event, gpointer data);

void
on_buttonShot_clicked (GtkButton * button, gpointer user_data);

void
on_buttonPause_clicked (GtkButton * button, gpointer user_data);

void
on_drawingareaView_realize (GtkWidget * widget, gpointer data);

gboolean
on_drawingareaView_configure_event (GtkWidget * widget,
    GdkEventConfigure * event, gpointer data);

void
on_comboboxResolution_changed (GtkComboBox * widget, gpointer user_data);

void
on_radiobuttonImageCapture_toggled (GtkToggleButton * togglebutton,
    gpointer user_data);

void
on_radiobuttonVideoCapture_toggled (GtkToggleButton * togglebutton,
    gpointer user_data);

void
on_rbBntVidEffNone_toggled (GtkToggleButton * togglebutton, gpointer data);

void
on_rbBntVidEffEdge_toggled (GtkToggleButton * togglebutton, gpointer data);

void
on_rbBntVidEffAging_toggled (GtkToggleButton * togglebutton, gpointer user_data);

void
on_rbBntVidEffDice_toggled (GtkToggleButton * togglebutton, gpointer user_data);

void
on_rbBntVidEffWarp_toggled (GtkToggleButton * togglebutton, gpointer data);

void
on_rbBntVidEffShagadelic_toggled (GtkToggleButton * togglebutton, gpointer data);

void
on_rbBntVidEffVertigo_toggled (GtkToggleButton * togglebutton, gpointer data);

void
on_rbBntVidEffRev_toggled (GtkToggleButton * togglebutton, gpointer data);

void
on_rbBntVidEffQuark_toggled (GtkToggleButton * togglebutton, gpointer data);

void
on_chkbntMute_toggled (GtkToggleButton * togglebutton, gpointer data);

void
on_chkbtnRawMsg_toggled (GtkToggleButton * togglebutton, gpointer data);

void
on_hscaleZoom_value_changed (GtkRange * range, gpointer user_data);

void
on_color_control_value_changed (GtkRange * range, gpointer user_data);

gboolean
on_key_released (GtkWidget * widget, GdkEventKey * event, gpointer user_data);

gboolean
on_key_pressed (GtkWidget * widget, GdkEventKey * event, gpointer user_data);

#endif /* __GST_CAMERA_BIN_H__ */
