#include <config.h>
#include <gnome.h>
#include "gstmediaplay.h"

int
main (int argc, char *argv[])
{
	GstMediaPlay *play;

	gst_init (&argc,&argv);
	gnome_init ("gstreamer", VERSION, argc, argv);
	glade_init();
	glade_gnome_init();

	play = gst_media_play_new ();
	
	if (argc > 1) {
		int i;

		//gst_media_play_show_playlist (play);
		gst_media_play_start_uri (play, argv[1]);

		for (i=1;i<argc;i++) {
			gst_media_play_addto_playlist (play, argv[i]);
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
