#include <gst/gst.h>

// Important: catch a signal( unix sig. ) to end the prog. normally

int main(int argc, char *argv[] )
{
   // The Main Bin( Pipeline )
   GstElement *main_pipe ;

   // The thread 
   GstElement *sinkThread ; 
   
   // Now the elements for our Sink Bin
   GstElement *gsmDec, *ossSink, *udpSrc ;
   
   // And a queue
   GstElement *queue ;
 
   gst_init( &argc, &argv ) ; // ughh...

   // Get the Boss: 
   main_pipe = gst_pipeline_new( "main_pipe" ) ;
   // & the lower level Bosses
   sinkThread = gst_thread_new( "sinkThread"   ) ; 
   
   //register_sigs( (GstElement *)srcThread ) ; // forget this line

   // NOTE: I have changed the osssink to a disksink, as i can't have the two
   // processes( server & client ) accessing the same soundcard.
   // Get the sound Card
   ossSink = gst_elementfactory_make( "osssink", "audio_sink" ) ;

   // Get the GSM codecs
   gsmDec = gst_elementfactory_make( "gsmdec", "gsmDec" ) ;
   
   // Get a queue
   queue = gst_elementfactory_make( "queue", "sink_queue" ) ;

   // Get the UDP connection to yourself
   udpSrc  = gst_elementfactory_make( "udpsrc", "udpSrc" ) ;
   
   // Asserttions:
   g_assert( main_pipe != NULL ) ;
   g_assert( sinkThread != NULL ) ;
   g_assert( ossSink != NULL ) ;
   g_assert( gsmDec != NULL ) ;
   g_assert( queue != NULL ) ;
   g_assert( udpSrc != NULL ) ;

   // Got to set the sound card:
   //g_object_set( G_OBJECT( ossSink ), "frequency", 1000, NULL ) ;
   //g_object_set( G_OBJECT( ossSink ), "channels", 1, NULL ) ;
   //g_object_set( G_OBJECT( ossSink ), "location", "recorded", NULL ) ;
  
   // Set the connections:
   g_object_set( G_OBJECT( udpSrc ), "port", 9323, NULL ) ;
   
   // make gsm encoders & decoder the ghost pads
   gst_element_add_ghost_pad( GST_ELEMENT( sinkThread ), 
		              gst_element_get_pad( ossSink, "sink" ), "sink" ) ;
   
   // Connect the appropritate elements
   gst_pad_connect( gst_element_get_pad( udpSrc, "src" ) ,
		    gst_element_get_pad( gsmDec, "sink" ) ) ;
   gst_pad_connect( gst_element_get_pad( gsmDec, "src" ) ,
		    gst_element_get_pad( queue, "sink" ) ) ;
   gst_pad_connect( gst_element_get_pad( queue, "src" ) ,
		    gst_element_get_pad( GST_ELEMENT( sinkThread ), "sink" ) ) ;
   
   // Add all element to their appropriate bins
   gst_bin_add( GST_BIN( sinkThread ), ossSink ) ;
   gst_bin_add( GST_BIN( main_pipe ), gsmDec  ) ; 
   gst_bin_add( GST_BIN( main_pipe ), queue ) ; 
   gst_bin_add( GST_BIN( main_pipe ), udpSrc  ) ; 
   
   gst_bin_add( GST_BIN( main_pipe ), GST_ELEMENT( sinkThread ) ) ;
   
   
   // Lets get started
   gst_element_set_state( GST_ELEMENT( main_pipe ), GST_STATE_PLAYING ) ; 

   while( gst_bin_iterate( GST_BIN( main_pipe ) ) ) ;

   gst_element_set_state( GST_ELEMENT( main_pipe ), GST_STATE_NULL ) ; 
  
   gst_object_destroy( GST_OBJECT( main_pipe  )  ) ;  

   g_print( "Normal Program Termination\n" ) ;
   
   exit (0) ;
}
