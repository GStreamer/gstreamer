#include <config.h>
#include <gnome.h>
#ifdef USE_GLIB2
#include <libgnomeui/libgnomeui.h>
#include <libgnomeui/gnome-ui-init.h>
#endif
#include "gstmediaplay.h"

int
main (int argc, char *argv[])
{
	GstMediaPlay *play;

	gst_init (&argc,&argv);

#ifdef USE_GLIB2
	gnome_program_init ("gstmediaplay", "0.3",
			    LIBGNOMEUI_MODULE,
			    argc, argv, NULL);
#else
	gnome_init ("gstreamer", VERSION, argc, argv);
	glade_init();
	glade_gnome_init();
#endif

	play = gst_media_play_new ();
	
	if (argc > 1) {
		int i;

		//gst_media_play_show_playlist (play);
		gst_media_play_start_uri (play, argv[1]);

		for (i=1;i<argc;i++) {
			//gst_media_play_addto_playlist (play, argv[i]);
		}
	}
	
#ifndef GST_DISABLE_LOADSAVE
	xmlSaveFile ("gstmediaplay.gst", gst_xml_write (gst_play_get_pipeline (play->play)));
#endif
	
	gdk_threads_enter();
	gst_main();
	gdk_threads_leave();
	
	return 0;
}
