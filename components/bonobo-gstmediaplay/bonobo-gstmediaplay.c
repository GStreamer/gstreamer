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

/*
 * Number of running objects
 */
static int running_objects = 0;
static BonoboGenericFactory *factory = NULL;

/*
 * BonoboControl data
 */
typedef struct {
	BonoboControl        *bonobo_object;
	BonoboUIComponent    *uic;

	GstPlay              *play;
} control_data_t;

/*
 * This callback is invoked when the BonoboControl object
 * encounters a fatal CORBA exception.
 */
static void
control_system_exception_cb (BonoboControl *control, CORBA_Object corba_object,
			     CORBA_Environment *ev, gpointer data)
{
	bonobo_object_unref (BONOBO_OBJECT (control));
}

static void
control_update (control_data_t *control_data)
{
	g_print("control_update\n", running_objects);
	gtk_widget_queue_draw (GTK_WIDGET (control_data->play));
	g_print("control_update done\n", running_objects);
}

static void
load_media (BonoboPersistStream        *ps,
	    const Bonobo_Stream         stream,
	    Bonobo_Persist_ContentType  type,
	    void                       *closure,
	    CORBA_Environment          *ev)
{
	control_data_t       *control_data = closure;
	GstPlay              *pl;
	Bonobo_Stream_iobuf  *buffer;
	char                 *str;
	int                   bx, by, j;

	g_return_if_fail (control_data != NULL);
	g_return_if_fail (control_data->play != NULL);

	if (*type && g_strcasecmp (type, "application/x-gstmediaplay") != 0) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_Bonobo_Persist_WrongDataType, NULL);
		return;
	}

	pl = control_data->play;

	bonobo_stream_client_read_string (stream, &str, ev);
	if (ev->_major != CORBA_NO_EXCEPTION || str == NULL) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_Bonobo_Persist_WrongDataType, NULL);
		return;
	}
	sscanf (str, "%2u%2u\n", &bx, &by);
	g_free (str);

	if (bx > 128 || by > 128) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_Bonobo_Persist_WrongDataType, NULL);
		return;
	}

	for (j = 0; j < by; j++) {
		int i;

		Bonobo_Stream_read (stream, bx * 2 + 1, &buffer, ev);
		if (ev->_major != CORBA_NO_EXCEPTION)
			return;
		else if (buffer->_length != bx * 2 + 1) {
			CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
					     ex_Bonobo_Persist_WrongDataType,
					     NULL);
			return;
		}


		CORBA_free (buffer);
	}

	control_update (control_data);
}

static void
save_media (BonoboPersistStream        *ps,
	    const Bonobo_Stream         stream,
	    Bonobo_Persist_ContentType  type,
	    void                       *closure,
	    CORBA_Environment          *ev)
{
	control_data_t       *control_data = closure;
	GstPlay              *pl;
	char                 *data;
	int                   j;

	g_return_if_fail (control_data != NULL);
	g_return_if_fail (control_data->play != NULL);

	if (*type && g_strcasecmp (type, "application/x-gstmediaplay") != 0) {
		CORBA_exception_set (ev, CORBA_USER_EXCEPTION,
				     ex_Bonobo_Persist_WrongDataType, NULL);
		return;
	}

	pl = control_data->play;

	if (ev->_major != CORBA_NO_EXCEPTION)
		return;
}

static Bonobo_Persist_ContentTypeList *
content_types (BonoboPersistStream *ps, void *closure, CORBA_Environment *ev)
{
	return bonobo_persist_generate_content_types (1, "application/x-gstmediaplay");
}

/*
 * When one of our controls is activated, we merge our menus
 * in with our container's menus.
 */
static void
control_create_menus (control_data_t *control_data)
{
	BonoboControl *control = control_data->bonobo_object;
	Bonobo_UIContainer remote_uic;
	GdkPixbuf *pixbuf;

	static char ui [] = 
		"<Root>"
		"	<commands>"
		"		<cmd name=\"NewGame\" _label=\"New game\" _tip=\"Start a new game\"/>"
		"		<cmd name=\"OpenGame\" _label=\"Open game\" _tip=\"Load a saved game\"/>"
		"	</commands>"
		"	<menu>"
		"		<submenu name=\"Game\" _label=\"_Game\">"
		"			<menuitem name=\"NewGame\" verb=\"\"/>"
		"			<menuitem name=\"OpenGame\" verb=\"\"/>"
		"		</submenu>"
		"	</menu>"
		"	<dockitem name=\"Game\">"
		"		<toolitem name=\"NewGame\" verb=\"\"/>"
		"	</dockitem>"
		"</Root>";

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
}

static void
control_remove_menus (control_data_t *control_data)
{
	bonobo_ui_component_unset_container (control_data->uic);
}

/*
 * Clean up our supplementary BonoboControl data sturctures.
 */
static void
control_destroy_cb (BonoboControl *control, gpointer data)
{
	control_data_t *control_data = (control_data_t *) data;

	g_message ("control_destroy_cb");

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
control_activate_cb (BonoboControl *control, gboolean activate, gpointer data)
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
control_set_frame_cb (BonoboControl *control, gpointer data)
{
	g_message ("control_set frame cb");
	control_create_menus ((control_data_t *) data);
	g_message ("control_set frame cb done");
}

static void
update_control (GtkWidget *widget, control_data_t *control_data)
{
	g_message ("update_control");
	control_update (control_data);
}

static BonoboObject *
bonobo_gstmediaplay_factory (BonoboGenericFactory *this, void *data)
{
	BonoboControl        *bonobo_object;
	control_data_t       *control_data;
	BonoboPersistStream  *stream;
	GtkWidget            *vbox;

	/*
	 * Create a data structure in which we can store
	 * Control-object-specific data about this document.
	 */
	control_data = g_new0 (control_data_t, 1);
	if (control_data == NULL)
		return NULL;

	g_print("creating\n");
	control_data->play = gst_play_new ();
	g_print("created\n");

	vbox = gtk_vbox_new (TRUE, 0);

	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (control_data->play),
			    TRUE, TRUE, 0);
	gtk_widget_show_all (vbox);

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

	/*
	 * Create the PersistStream object.
	 */
	stream = bonobo_persist_stream_new (load_media, save_media,
					    NULL, content_types,
					    control_data);

	if (stream == NULL) {
		bonobo_object_unref (BONOBO_OBJECT (bonobo_object));
		gtk_widget_destroy (vbox);
		g_free (control_data);
		return NULL;
	}
	bonobo_object_add_interface (BONOBO_OBJECT (bonobo_object),
				     BONOBO_OBJECT (stream));

	/*
	 * Add some verbs to the control.
	 *
	 * The container application will then have the programmatic
	 * ability to execute the verbs on the component.  It will
	 * also provide a simple mechanism whereby the user can
	 * right-click on the component to create a popup menu
	 * listing the available verbs.
	 *
	 * We provide one simple verb whose job it is to clear the
	 * window.
	 */
	control_data->uic = bonobo_control_get_ui_component (bonobo_object);

	/*
	 * Count the new running object
	 */
	running_objects++;
	g_print("running objects: %d\n", running_objects);

	return BONOBO_OBJECT (bonobo_object);
}

static void
init_gstmediaplay_factory (void)
{
	printf("init factory\n");
	factory = bonobo_generic_factory_new (
		"OAFIID:bonobo_gstmediaplay_factory:420f20ca-55d7-4a33-b327-0b246136db18",
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

        gnome_init_with_popt_table("gstmediaplay", VERSION,
				   argc, argv,
				   oaf_popt_options, 0, NULL); 
	orb = oaf_init (argc, argv);

	CORBA_exception_free (&ev);

	if (bonobo_init (orb, NULL, NULL) == FALSE)
		g_error (_("Could not initialize Bonobo!"));
}
 
int
main (int argc, char **argv)
{
	gst_init (&argc, &argv);
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
