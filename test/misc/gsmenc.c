#include <gst/gst.h>

// Important: catch a signal( unix sig. ) to end the prog. normally

int main(int argc, char *argv[] )
{
   // The Main Bin( Thread )
   GstElement *main_pipe ;

   // The Two pipelines
   GstElement *srcThread  ;
   
   // The elements for our Source Bin
   GstElement *ossSrc, *gsmEnc, *udpSink ;
  
   // And a queue
   GstElement *queue ;
 
   gst_init( &argc, &argv ) ; // ughh...

   // Get the Boss: 
   main_pipe = gst_pipeline_new( "main_pipe" ) ;
   // & the lower level Bosses
   srcThread  = gst_thread_new( "sourceThread" ) ;
   
   //register_sigs( (GstElement *)srcThread ) ; forget about this line
   
   // Get the sound Card
   ossSrc  = gst_elementfactory_make( "osssrc",  "audio_src" ) ;
   // Get the GSM codecs
   gsmEnc = gst_elementfactory_make( "gsmenc", "gsmEnc" ) ;
   
   // Get a queue
   queue = gst_elementfactory_make( "queue", "src_queue" ) ;

   // Get the UDP connection to the server 
   udpSink = gst_elementfactory_make( "udpsink", "udpSink" ) ;
   
   // Asserttions:
   g_assert( main_pipe != NULL ) ;
   g_assert( srcThread != NULL ) ;
   g_assert( ossSrc != NULL ) ;
   g_assert( gsmEnc != NULL ) ;
   g_assert( queue != NULL ) ;
   g_assert( udpSink != NULL ) ;

   // Got to set the sound card:
   g_object_set( G_OBJECT( ossSrc ), "frequency", 8000, NULL ) ;
   g_object_set( G_OBJECT( ossSrc ), "channels", 1, NULL ) ;
   g_object_set( G_OBJECT( ossSrc ), "bytes_per_read", 320, NULL ) ;
   g_object_set( G_OBJECT( ossSrc ), "format", 16, NULL ) ;
 
   // Set the connections: 
   g_object_set( G_OBJECT( udpSink ), "port", 9323, NULL ) ;
   
   // make gsm encoders & decoder the ghost pads
   gst_element_add_ghost_pad( GST_ELEMENT( srcThread ), 
		              gst_element_get_pad( udpSink, "sink" ), "sink" ) ;
   
   // Connect the appropritate elements
   gst_pad_connect( gst_element_get_pad( ossSrc, "src" ) ,
		    gst_element_get_pad( gsmEnc, "sink" ) ) ;
   gst_pad_connect( gst_element_get_pad( gsmEnc, "src" ) ,
		    gst_element_get_pad( queue, "sink" ) ) ;
   gst_pad_connect( gst_element_get_pad( queue, "src" ) ,
		    gst_element_get_pad( GST_ELEMENT( srcThread ), "sink" ) ) ;
   
   // Add all element to their appropriate bins
   gst_bin_add( GST_BIN( main_pipe ), ossSrc ) ;
   gst_bin_add( GST_BIN( main_pipe ), gsmEnc ) ;
   gst_bin_add( GST_BIN( main_pipe ), queue ) ;
   gst_bin_add( GST_BIN( srcThread ), udpSink ) ;
   
   gst_bin_add( GST_BIN( main_pipe ), GST_ELEMENT( srcThread ) ) ;
   
   // Lets get started
   gst_element_set_state( GST_ELEMENT( main_pipe ), GST_STATE_PLAYING ) ; 

   while( gst_bin_iterate( GST_BIN( main_pipe ) ) ) ;

   gst_element_set_state( GST_ELEMENT( main_pipe ), GST_STATE_NULL ) ; 
  
   gst_object_destroy( GST_OBJECT( main_pipe  )  ) ;  

   g_print( "Normal Program Termination\n" ) ;
   
   exit (0) ;
}
