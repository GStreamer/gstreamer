#include <gtk/gtk.h>

#include "gst/gst.h"
#include "gst/gstparse.h"

typedef struct  
{
    GstElement *pipeline;
    GstElement *src;
    
    GstElement *ap_element;
    
    GstElement *audio_sink;
    GstElement *video_sink;

    GstElement *autobin;
    GstElement *typefind;
    GstElement *autoplugcache;

} GstPlayInfo;

// Global GStreamer elements
static GstPlayInfo *info;


void destroy( GtkWidget *widget, gpointer data )
{
    gtk_main_quit();
}



static void
gst_play_cache_empty (GstElement *element, GstElement *pipeline)
{
  GstElement *autobin;
  GstElement *disksrc;
  GstElement *cache;
  GstElement *new_element;

  fprintf (stderr, "have cache empty\n");

  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  disksrc = gst_bin_get_by_name (GST_BIN (pipeline), "disk_source");
  autobin = gst_bin_get_by_name (GST_BIN (pipeline), "autobin");
  cache = gst_bin_get_by_name (GST_BIN (autobin), "cache");
  new_element = gst_bin_get_by_name (GST_BIN (autobin), "new_element");

  gst_element_disconnect (disksrc, "src", cache, "sink");
  gst_element_disconnect (cache, "src", new_element, "sink");
  gst_bin_remove (GST_BIN (autobin), cache);
  gst_element_connect (disksrc, "src", new_element, "sink");

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  fprintf (stderr, "done with cache_empty\n");
}

static void 
eos(GstElement *element, GstPlayInfo *info)  
{
  printf("Got EOS signal\n");

  //gst_element_set_state( GST_ELEMENT(pipeline), GST_STATE_NULL );

  gst_element_set_state (GST_ELEMENT (info->pipeline), GST_STATE_NULL);
  //gtk_object_set (GTK_OBJECT (info->src), "offset", 0, NULL);
  
}


gboolean 
idle_func (gpointer ptr)
{
   GstPlayInfo *info = (GstPlayInfo*) ptr;

   return gst_bin_iterate (GST_BIN (info->pipeline));
}

static void
gst_play_have_type (GstElement *typefind, GstCaps *caps, GstPlayInfo *info)
{
  GstElement *vis_bin;
  GstElement *new_element;
  GstAutoplug *autoplug;
  GstElement *autobin;
  GstElement *disksrc;
  GstElement *cache;

  GstElement *pipeline = info->pipeline;

  printf( "In gst_play_have_type()\n" );

  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  disksrc = gst_bin_get_by_name (GST_BIN (pipeline), "disk_source");
  autobin = gst_bin_get_by_name (GST_BIN (pipeline), "autobin");
  cache = gst_bin_get_by_name (GST_BIN (autobin), "cache");

  // disconnect the typefind from the pipeline and remove it
  gst_element_disconnect (cache, "src", typefind, "sink");
  gst_bin_remove (GST_BIN (autobin), typefind);
      
  // XXXXXXXXXXX
  
  printf( "About to autoplug\n" );

  autoplug = gst_autoplugfactory_make ("staticrender");
  g_assert (autoplug != NULL);

  //gtk_signal_connect (GTK_OBJECT (autoplug), "new_object", GTK_SIGNAL_FUNC(gst_play_object_added), info);

  printf( "-- 1 --\n" );

  info->ap_element = gst_autoplug_to_renderers (autoplug,
           caps,
           //vis_bin,
           info->video_sink,
           info->audio_sink,
           NULL);

  printf( "-- 2 --\n" );

  if (!info->ap_element) {
    g_print ("could not autoplug, no suitable codecs found...\n");
    exit (-1);
  }

  gst_element_set_name (info->ap_element, "new_element");

  gst_bin_add (GST_BIN (autobin), info->ap_element);

  gtk_object_set (GTK_OBJECT (cache), "reset", TRUE, NULL);

  gst_element_connect (cache, "src", info->ap_element, "sink");
                                  
  // Add the "eos" handler to the main pipeline
  gtk_signal_connect( GTK_OBJECT(disksrc),"eos", GTK_SIGNAL_FUNC(eos), info);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
      
  xmlSaveFile("xmlTest.gst", gst_xml_write (GST_ELEMENT (pipeline)));
}


// Handles setting up a stream and playing
void play( GtkWidget *widget, gpointer data )
{
    gchar *fileName = gtk_entry_get_text( GTK_ENTRY(data) );

    printf( "In Play()\n" );

    if( info->pipeline )
    {
        GstObject *parent;
	
	parent = gst_element_get_parent (info->audio_sink);
        if (parent) {
          gst_object_ref (GST_OBJECT (info->audio_sink));
          gst_bin_remove( GST_BIN(parent), info->audio_sink );
	}

	parent = gst_element_get_parent (info->video_sink);
        if (parent) {
          gst_object_ref (GST_OBJECT (info->video_sink));
          gst_bin_remove( GST_BIN(parent), info->video_sink );
	}
                                                            
        gst_pipeline_destroy( info->pipeline );
    }

    // Create a new pipeline
    info->pipeline = gst_pipeline_new( "pipeline" );

    // Create a disksrc
    info->src = gst_elementfactory_make( "disksrc", "disk_source" );
    
    // Set the location of the disksrc
    gtk_object_set( GTK_OBJECT(info->src), "location", fileName, NULL );
    
    gst_bin_add( GST_BIN(info->pipeline), info->src );

    // Setup a bin to store the typefind and autoplugcache elements
    info->autobin = gst_bin_new( "autobin" );

    // Create the typefind element
    info->typefind = gst_elementfactory_make( "typefind", "typefind" );
    gtk_signal_connect( GTK_OBJECT(info->typefind), "have_type", 
                        GTK_SIGNAL_FUNC(gst_play_have_type), info );

    // Create the autoplugcache element
    info->autoplugcache = gst_elementfactory_make( "autoplugcache", "cache" );
    gtk_signal_connect( GTK_OBJECT(info->autoplugcache), "cache_empty", 
                        GTK_SIGNAL_FUNC(gst_play_cache_empty), info->pipeline );

    gst_bin_add( GST_BIN(info->autobin), info->typefind );
    gst_bin_add( GST_BIN(info->autobin), info->autoplugcache );

    // Connect the autoplugcache element to the typefind element
    gst_element_connect( info->autoplugcache, "src", info->typefind, "sink" );
    gst_element_add_ghost_pad( info->autobin, gst_element_get_pad( info->autoplugcache, "sink" ), "sink" );

    // Add the autobin to the main pipeline and connect the disksrc to the autobin
    gst_bin_add( GST_BIN(info->pipeline), info->autobin );
    gst_element_connect( info->src, "src", info->autobin, "sink" );

    // Set the state to GST_STATE_PLAYING
    gst_element_set_state( GST_ELEMENT(info->pipeline), GST_STATE_PLAYING );

    gtk_idle_add( idle_func, info );

    printf( "Leaving Play()\n" );

}


void playMP3(  GtkWidget *widget, gpointer data )
{
    GstElement *mp3parse;
    GstElement *mpg123;
    GstElement *osssink;
    gchar *fileName;
     
    
    printf( "In playMP3()\n" );

    fileName = gtk_entry_get_text( GTK_ENTRY(data) );

    
    if( info->pipeline )
    {
        gst_pipeline_destroy( info->pipeline );
    }

    // Create a new pipeline
    info->pipeline = gst_pipeline_new( "pipeline" );

    // Create a disksrc
    info->src = gst_elementfactory_make( "disksrc", "disk_source" );
    
    // Set the location of the disksrc
    gtk_object_set( GTK_OBJECT(info->src), "location", fileName, NULL );
    
    gst_bin_add( GST_BIN(info->pipeline), info->src );

    mp3parse = gst_elementfactory_make( "mp3parse", "parser" );
    gst_element_connect( info->src, "src", mp3parse, "sink" );
    gst_bin_add( GST_BIN(info->pipeline), mp3parse );

    mpg123 = gst_elementfactory_make( "mpg123", "decoder" );
    gst_element_connect( mp3parse, "src", mpg123, "sink" );
    gst_bin_add( GST_BIN(info->pipeline), mpg123 );

    osssink = gst_elementfactory_make( "osssink", "audio_sink" );
    gst_element_connect( mpg123, "src", osssink, "sink" );
    gst_bin_add( GST_BIN(info->pipeline), osssink );
                                                    
    // Add the "eos" handler to the main pipeline
    gtk_signal_connect( GTK_OBJECT(info->src),"eos", GTK_SIGNAL_FUNC(eos), info);

    // Set the state to GST_STATE_PLAYING
    gst_element_set_state( GST_ELEMENT(info->pipeline), GST_STATE_PLAYING );

    gtk_idle_add( idle_func, info );
}


// Handles stopping a playing stream and tearing down the pipeline
void stop( GtkWidget *widget, gpointer data )
{
    if( info->pipeline )
        gst_element_set_state( GST_ELEMENT(info->pipeline), GST_STATE_NULL );    
}

// Handles pausing the playing stream
/*
void pause( GtkWidget *widget, gpointer data )
{

}
*/

int main( int argc, char *argv[] )
{
    GtkWidget *window;
    GtkWidget *textField;
    GtkWidget *socket;
    GtkWidget *playBtn, *playMp3Btn, *stopBtn;
    GtkWidget *vbox, *hbox;
    

    gtk_init( &argc, &argv );
    gst_init( &argc, &argv );



    // Allocate the struct for storing GStreamer elements
    info = (GstPlayInfo*) calloc( 1, sizeof(GstPlayInfo) );

    // create an audio sink
    info->audio_sink = gst_elementfactory_make("osssink", "play_audio");
  
    // create a video sink
    info->video_sink = gst_elementfactory_make("xvideosink", "play_video");




    // Create a new window
    window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_widget_set_usize( GTK_WIDGET(window), 300, 400 );
                           
    gtk_signal_connect( GTK_OBJECT(window), "delete_event", 
                        GTK_SIGNAL_FUNC(destroy), NULL );

    gtk_signal_connect( GTK_OBJECT(window), "destroy",
                        GTK_SIGNAL_FUNC(destroy), NULL );

    gtk_container_set_border_width( GTK_CONTAINER(window), 10 );
   
    // Create a vertical box
    vbox = gtk_vbox_new( FALSE, 0 );
    gtk_container_add( GTK_CONTAINER(window), vbox );
    gtk_widget_show( vbox );

    // Create the socket widget
    socket = gtk_socket_new();
    gtk_box_pack_start( GTK_BOX(vbox), socket, TRUE, TRUE, 0 );
    gtk_widget_show( GTK_WIDGET(socket) );
    gtk_widget_set_usize( GTK_WIDGET(socket), 300, 300 );

    // Connect the socket widget to the xvideosink element
    gtk_widget_realize (socket);
    gtk_socket_steal( GTK_SOCKET(socket), 
          gst_util_get_int_arg( (GObject*)info->video_sink, "xid" ));


    // Create the text entry widget 
    textField = gtk_entry_new();
    gtk_box_pack_start( GTK_BOX(vbox), textField, TRUE, TRUE, 0 );
    gtk_widget_show( GTK_WIDGET(textField) );
                                             
    // Create a horizontal box
    hbox = gtk_hbox_new( FALSE, 0 );
    gtk_container_add( GTK_CONTAINER(vbox), hbox );
    gtk_widget_show( hbox );
    
    // Create the play button
    playBtn = gtk_button_new_with_label( "Play" );

    gtk_signal_connect( GTK_OBJECT(playBtn), "clicked", 
                        GTK_SIGNAL_FUNC(play), textField );

    gtk_box_pack_start( GTK_BOX(hbox), playBtn, TRUE, TRUE, 0 );
    gtk_widget_show( GTK_WIDGET(playBtn) );

    // Create the Play Mp3 button
    playMp3Btn = gtk_button_new_with_label( "Play Mp3" );

    gtk_signal_connect( GTK_OBJECT(playMp3Btn), "clicked", 
                        GTK_SIGNAL_FUNC(playMP3), textField );

    gtk_box_pack_start( GTK_BOX(hbox), playMp3Btn, TRUE, TRUE, 0 );
    gtk_widget_show( GTK_WIDGET(playMp3Btn) );

    // Create the stop button
    stopBtn = gtk_button_new_with_label( "Stop" );

    gtk_signal_connect( GTK_OBJECT(stopBtn), "clicked", 
                        GTK_SIGNAL_FUNC(stop), textField );

    gtk_box_pack_start( GTK_BOX(hbox), stopBtn, TRUE, TRUE, 0 );
    gtk_widget_show( GTK_WIDGET(stopBtn) );
    


    gtk_widget_show( window );

    gtk_main();

    return(0);

}
