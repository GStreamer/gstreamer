/*
 * GStreamer
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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

/*
 * Includes
 */
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gst-camera2.h"

#include <gst/gst.h>
#include <gst/interfaces/xoverlay.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>

#define UI_FILE CAMERA_APPS_UIDIR G_DIR_SEPARATOR_S "gst-camera2.ui"

static GstElement *camera;
static GtkBuilder *builder;

void
on_mainWindow_delete_event (GtkWidget * widget, GdkEvent * event, gpointer data)
{
  gtk_main_quit ();
}

void
on_captureButton_clicked (GtkButton * button, gpointer user_data)
{
  g_signal_emit_by_name (camera, "start-capture", NULL);
}

void
on_stopCaptureButton_clicked (GtkButton * button, gpointer user_data)
{
  g_signal_emit_by_name (camera, "stop-capture", NULL);
}

void
on_imageRButton_toggled (GtkToggleButton * button, gpointer user_data)
{
  if (gtk_toggle_button_get_active (button)) {
    g_object_set (camera, "mode", 1, NULL);     /* Image mode */
  }
}

void
on_videoRButton_toggled (GtkToggleButton * button, gpointer user_data)
{
  if (gtk_toggle_button_get_active (button)) {
    g_object_set (camera, "mode", 2, NULL);     /* Video mode */
  }
}

void
on_viewfinderArea_realize (GtkWidget * widget, gpointer data)
{
#if GTK_CHECK_VERSION (2, 18, 0)
  gdk_window_ensure_native (gtk_widget_get_window (widget));
#endif
}

static GstBusSyncReply
bus_sync_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  GtkWidget *ui_drawing;

  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
    return GST_BUS_PASS;

  if (!gst_structure_has_name (message->structure, "prepare-xwindow-id"))
    return GST_BUS_PASS;

  /* FIXME: make sure to get XID in main thread */
  ui_drawing = GTK_WIDGET (gtk_builder_get_object (builder, "viewfinderArea"));
  gst_x_overlay_set_window_handle (GST_X_OVERLAY (message->src),
#if GTK_CHECK_VERSION (2, 91, 6)
      GDK_WINDOW_XID (gtk_widget_get_window (ui_drawing)));
#else
      GDK_WINDOW_XWINDOW (gtk_widget_get_window (ui_drawing)));
#endif

  gst_message_unref (message);
  return GST_BUS_DROP;
}


static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_WARNING:{
      GError *err;
      gchar *debug;

      gst_message_parse_warning (message, &err, &debug);
      g_print ("Warning: %s\n", err->message);
      g_error_free (err);
      g_free (debug);
      break;
    }
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *debug = NULL;

      gst_message_parse_error (message, &err, &debug);
      g_print ("Error: %s : %s\n", err->message, debug);
      g_error_free (err);
      g_free (debug);

      gtk_main_quit ();
      break;
    }
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      g_print ("Eos\n");
      gtk_main_quit ();
      break;
    case GST_MESSAGE_ELEMENT:
    {
      //handle_element_message (message);
      break;
    }
    default:
      /* unhandled message */
      break;
  }
  return TRUE;
}

int
main (int argc, char *argv[])
{
  int ret = 0;
  GtkWidget *ui_main_window;
  GError *error = NULL;
  GstBus *bus;

  gst_init (&argc, &argv);
  gtk_init (&argc, &argv);

  builder = gtk_builder_new ();
  if (!gtk_builder_add_from_file (builder, UI_FILE, &error)) {
    g_warning ("Error: %s", error->message);
    g_error_free (error);
    return 1;
  }

  camera = gst_element_factory_make ("camerabin2", "camera");
  bus = gst_pipeline_get_bus (GST_PIPELINE (camera));
  gst_bus_add_watch (bus, bus_callback, NULL);
  gst_bus_set_sync_handler (bus, bus_sync_callback, NULL);
  gst_object_unref (bus);

  ui_main_window = GTK_WIDGET (gtk_builder_get_object (builder, "mainWindow"));
  gtk_builder_connect_signals (builder, NULL);
  gtk_widget_show_all (ui_main_window);

  gst_element_set_state (camera, GST_STATE_PLAYING);

  gtk_main ();

  gst_element_set_state (camera, GST_STATE_NULL);
  gst_object_unref (camera);
  return ret;
}
