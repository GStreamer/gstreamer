/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <config.h>
#include <gnome.h>
#include <liboaf/liboaf.h>

#include <bonobo.h>

#include "gstplay.h"

#include "pause.xpm"
#include "play.xpm"
#include "stop.xpm"

/*
 * Number of running objects
 */
static int running_objects = 0;
static BonoboGenericFactory *factory = NULL;

/*
 * BonoboControl data
 */
typedef struct
{
  BonoboControl *bonobo_object;
  BonoboUIComponent *uic;

  GstPlay *play;
} control_data_t;

/*
 * This callback is invoked when the BonoboControl object
 * encounters a fatal CORBA exception.
 */
static void
control_system_exception_cb (BonoboControl * control, CORBA_Object corba_object,
    CORBA_Environment * ev, gpointer data)
{
  bonobo_object_unref (BONOBO_OBJECT (control));
}

static void
control_update (control_data_t * control_data)
{
  gtk_widget_queue_draw (GTK_WIDGET (control_data->play));
}

static void
verb_Play_cb (BonoboUIComponent * uic, gpointer user_data, const char *cname)
{
  control_data_t *control_data = (control_data_t *) user_data;

  gst_play_play (control_data->play);
  control_update (control_data);
}

static void
verb_Pause_cb (BonoboUIComponent * uic, gpointer user_data, const char *cname)
{
  control_data_t *control_data = (control_data_t *) user_data;

  gst_play_pause (control_data->play);
  control_update (control_data);
}

static void
verb_Stop_cb (BonoboUIComponent * uic, gpointer user_data, const char *cname)
{
  control_data_t *control_data = (control_data_t *) user_data;

  gst_play_stop (control_data->play);
  control_update (control_data);
}

typedef struct
{
  control_data_t *control_data;
  GtkFileSelection *selector;
} file_select_struct;

static void
filename_selected (GtkButton * ok, gpointer user_data)
{
  file_select_struct *select = (file_select_struct *) user_data;

  gchar *selected_filename;

  selected_filename =
      gtk_file_selection_get_filename (GTK_FILE_SELECTION (select->selector));

  gst_play_set_uri (select->control_data->play, selected_filename);

  gst_play_play (select->control_data->play);
  control_update (select->control_data);

  g_free (select);
}

static void
verb_Open_cb (BonoboUIComponent * uic, gpointer user_data, const char *cname)
{
  control_data_t *control_data = (control_data_t *) user_data;
  file_select_struct *data = g_new0 (file_select_struct, 1);
  GtkWidget *file_selector;

  file_selector = gtk_file_selection_new ("Select a media file");

  data->selector = GTK_FILE_SELECTION (file_selector);
  data->control_data = control_data;

  gtk_signal_connect (GTK_OBJECT (GTK_FILE_SELECTION (file_selector)->
	  ok_button), "clicked", GTK_SIGNAL_FUNC (filename_selected), data);

  gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION (file_selector)->
	  ok_button), "clicked", GTK_SIGNAL_FUNC (gtk_widget_destroy),
      (gpointer) file_selector);

  gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION (file_selector)->
	  cancel_button), "clicked", GTK_SIGNAL_FUNC (gtk_widget_destroy),
      (gpointer) file_selector);

  gtk_widget_show (file_selector);
}

/*
 * When one of our controls is activated, we merge our menus
 * in with our container's menus.
 */
static void
control_create_menus (control_data_t * control_data)
{
  BonoboControl *control = control_data->bonobo_object;
  Bonobo_UIContainer remote_uic;
  GdkPixbuf *pixbuf;

  static char ui[] =
      "<Root>"
      "  <commands>"
      "    <cmd name=\"Play\" _label=\"Play\" _tip=\"Play\"/>"
      "    <cmd name=\"Pause\" _label=\"Pause\" _tip=\"Pause\"/>"
      "    <cmd name=\"Stop\" _label=\"Stop\" _tip=\"Stop\"/>"
      "    <cmd name=\"Open\" _label=\"Open Media\" _tip=\"Open a media stream\"/>"
      "  </commands>"
      "  <menu>"
      "    <submenu name=\"Player\" _label=\"_Player\">"
      "      <menuitem name=\"Open\" pixtype=\"stock\" pixname=\"Open\" verb=\"\"/>"
      "      <separator/>"
      "      <menuitem name=\"Play\" verb=\"\"/>"
      "      <menuitem name=\"Pause\" verb=\"\"/>"
      "      <menuitem name=\"Stop\" verb=\"\"/>"
      "    </submenu>"
      "  </menu>"
      "  <dockitem name=\"GstMediaPlay\">"
      "    <toolitem name=\"Play\" type=\"toggle\" verb=\"\"/>"
      "    <toolitem name=\"Pause\" type=\"toggle\" verb=\"\"/>"
      "    <toolitem name=\"Stop\" type=\"toggle\" verb=\"\"/>"
      "  </dockitem>" "</Root>";

  g_print ("create menu\n");
  /*
   * Get our container's UIContainer server.
   */
  remote_uic = bonobo_control_get_remote_ui_container (control);

  /*
   * We have to deal gracefully with containers
   * which don't have a UIContainer running.
   */
  if (remote_uic == CORBA_OBJECT_NIL) {
    g_warning ("No UI container!");
    return;
  }


  /*
   * Give our BonoboUIComponent object a reference to the
   * container's UIContainer server.
   */
  bonobo_ui_component_set_container (control_data->uic, remote_uic);

  /*
   * Unref the UI container we have been passed.
   */
  bonobo_object_release_unref (remote_uic, NULL);

  /* Set up the UI from the XML string. */
  {
    BonoboUINode *node;

    node = bonobo_ui_node_from_string (ui);
    bonobo_ui_util_translate_ui (node);
    bonobo_ui_util_fixup_help (control_data->uic, node,
	DATADIR, "gstmediaplay");

    bonobo_ui_component_set_tree (control_data->uic, "/", node, NULL);

    bonobo_ui_node_free (node);
  }

  pixbuf = gdk_pixbuf_new_from_xpm_data ((const char **) play_back_xpm);
  bonobo_ui_util_set_pixbuf (control_data->uic, "/commands/Play", pixbuf);
  gdk_pixbuf_unref (pixbuf);

  pixbuf = gdk_pixbuf_new_from_xpm_data ((const char **) pause_xpm);
  bonobo_ui_util_set_pixbuf (control_data->uic, "/commands/Pause", pixbuf);
  gdk_pixbuf_unref (pixbuf);

  pixbuf = gdk_pixbuf_new_from_xpm_data ((const char **) stop_back_xpm);
  bonobo_ui_util_set_pixbuf (control_data->uic, "/commands/Stop", pixbuf);
  gdk_pixbuf_unref (pixbuf);

  g_print ("create menu done\n");
}

static void
control_remove_menus (control_data_t * control_data)
{
  bonobo_ui_component_unset_container (control_data->uic);
}

/*
 * Clean up our supplementary BonoboControl data sturctures.
 */
static void
control_destroy_cb (BonoboControl * control, gpointer data)
{
  control_data_t *control_data = (control_data_t *) data;

  control_data->play = NULL;

  g_free (control_data);

  running_objects--;
  if (running_objects > 0)
    return;

  /*
   * When the last object has gone, unref the factory & quit.
   */
  bonobo_object_unref (BONOBO_OBJECT (factory));
  gtk_main_quit ();
}

static void
control_activate_cb (BonoboControl * control, gboolean activate, gpointer data)
{
  control_data_t *control_data = (control_data_t *) data;

  g_message ("control_activate");
  /*
   * The ControlFrame has just asked the Control (that's us) to be
   * activated or deactivated.  We must reply to the ControlFrame
   * and say whether or not we want our activation state to
   * change.  We are an acquiescent BonoboControl, so we just agree
   * with whatever the ControlFrame told us.  Most components
   * should behave this way.
   */
  bonobo_control_activate_notify (control, activate);

  /*
   * If we were just activated, we merge in our menu entries.
   * If we were just deactivated, we remove them.
   */
  if (activate)
    control_create_menus (control_data);
  else
    control_remove_menus (control_data);
}

static void
control_set_frame_cb (BonoboControl * control, gpointer data)
{
  control_create_menus ((control_data_t *) data);
}

static BonoboObject *
bonobo_gstmediaplay_factory (BonoboGenericFactory * this, void *data)
{
  BonoboControl *bonobo_object;
  control_data_t *control_data;
  GtkWidget *vbox;

  gst_init (NULL, NULL);
  /*
   * Create a data structure in which we can store
   * Control-object-specific data about this document.
   */
  control_data = g_new0 (control_data_t, 1);
  if (control_data == NULL)
    return NULL;

  control_data->play = gst_play_new ();


  vbox = gtk_vbox_new (TRUE, 0);

  gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (control_data->play),
      TRUE, TRUE, 0);
  gtk_widget_show_all (vbox);

  gst_play_set_uri (control_data->play, "/opt/data/armageddon1.mpg");
  gst_play_play (control_data->play);

  /*
   * Create the BonoboControl object.
   */
  bonobo_object = bonobo_control_new (vbox);

  if (bonobo_object == NULL) {
    gtk_widget_destroy (vbox);
    g_free (control_data);
    return NULL;
  }

  control_data->bonobo_object = bonobo_object;

  control_data->uic = bonobo_control_get_ui_component (bonobo_object);

  /*
   * When our container wants to activate this component, we will get
   * the "activate" signal.
   */
  gtk_signal_connect (GTK_OBJECT (bonobo_object), "activate",
      GTK_SIGNAL_FUNC (control_activate_cb), control_data);
  gtk_signal_connect (GTK_OBJECT (bonobo_object), "set_frame",
      GTK_SIGNAL_FUNC (control_set_frame_cb), control_data);

  /*
   * The "system_exception" signal is raised when the BonoboControl
   * encounters a fatal CORBA exception.
   */
  gtk_signal_connect (GTK_OBJECT (bonobo_object), "system_exception",
      GTK_SIGNAL_FUNC (control_system_exception_cb), control_data);

  /*
   * We'll need to be able to cleanup when this control gets
   * destroyed.
   */
  gtk_signal_connect (GTK_OBJECT (bonobo_object), "destroy",
      GTK_SIGNAL_FUNC (control_destroy_cb), control_data);

  bonobo_ui_component_add_verb (control_data->uic, "Play",
      verb_Play_cb, control_data);
  bonobo_ui_component_add_verb (control_data->uic, "Pause",
      verb_Pause_cb, control_data);
  bonobo_ui_component_add_verb (control_data->uic, "Stop",
      verb_Stop_cb, control_data);

  bonobo_ui_component_add_verb (control_data->uic, "Open",
      verb_Open_cb, control_data);

  /*
   * Count the new running object
   */
  running_objects++;
  g_print ("running objects: %d\n", running_objects);

  return BONOBO_OBJECT (bonobo_object);
}

static void
init_gstmediaplay_factory (void)
{
  factory =
      bonobo_generic_factory_new
      ("OAFIID:bonobo_gstmediaplay_factory:420f20ca-55d7-4a33-b327-0b246136db18",
      bonobo_gstmediaplay_factory, NULL);
}

static void
init_server_factory (int argc, char **argv)
{
  CORBA_Environment ev;
  CORBA_ORB orb;

  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);

  CORBA_exception_init (&ev);

  gnome_init_with_popt_table ("bonobo-gstmediaplay", VERSION,
      argc, argv, oaf_popt_options, 0, NULL);
  orb = oaf_init (argc, argv);

  if (bonobo_init (orb, NULL, NULL) == FALSE)
    g_error (_("Could not initialize Bonobo!"));

  CORBA_exception_free (&ev);
}

int
main (int argc, char **argv)
{
  /* g_thread_init (NULL); */
  /*
   * Setup the factory.
   */
  init_server_factory (argc, argv);
  init_gstmediaplay_factory ();

  /*
   * Start processing.
   */
  bonobo_main ();

  return 0;
}
