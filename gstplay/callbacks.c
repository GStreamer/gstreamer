#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gnome.h>
#include <glade/glade.h>
#include <gst/gst.h>
#include <sys/stat.h>
#include <unistd.h>
#include "gstmediaplay.h"
#include "gstplay.h"
#include "callbacks.h"

GtkFileSelection *open_file_selection;

void
on_save1_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	printf ("file1 activate\n");
}

void
on_save_as1_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	printf ("file1 activate\n");
}

void
on_media2_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	printf ("file1 activate\n");
}

void
on_original_size_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	GstMediaPlay *mplay;

	mplay = GST_MEDIA_PLAY (user_data);

	gst_media_play_set_original_size (mplay);
}

void
on_double_size_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	GstMediaPlay *mplay;
	
	mplay = GST_MEDIA_PLAY (user_data);

	gst_media_play_set_double_size (mplay);
}

void
on_full_screen_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	GstMediaPlay *mplay;

	mplay = GST_MEDIA_PLAY (user_data);

	gst_media_play_set_fullscreen (mplay);
}

void
on_preferences1_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	GladeXML *xml;
	struct stat statbuf;

	if (stat (DATADIR"gstmediaplay.glade", &statbuf) == 0) {
		xml = gst_glade_xml_new (DATADIR"gstmediaplay.glade", "preferences");
	}
	else
		xml = gst_glade_xml_new ("gstmediaplay.glade", "preferences");
}

void on_about_activate (GtkWidget *widget, gpointer data)
{
	GladeXML *xml;
	struct stat statbuf;

	if (stat (DATADIR"gstmediaplay.glade", &statbuf) == 0) {
		xml = gst_glade_xml_new (DATADIR"gstmediaplay.glade", "about");
	}
	else
		xml = gst_glade_xml_new ("gstmediaplay.glade", "about");

	/* connect the signals in the interface */
	glade_xml_signal_autoconnect (xml);
}

void on_gstplay_destroy (GtkWidget *widget, gpointer data)
{
	gst_main_quit();
}

