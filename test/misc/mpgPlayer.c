#include "gsmtest.h"

// The Main Bin( Thread )
GstElement *main_thread ;

// The primary srcs & sinks:
GstElement *diskSrc , *ossSink, *videoSink ;
// Parsers, filters & Decoders: 
GstElement *mpgParser, *mpgVideoParser, *mp3Parser ;
GstElement *mpgDecoder, *mp3Decoder ;
GstElement *colorSpace ; // see if including this works

gboolean playing ;

void eos( GstElement *element, gpointer data )
{
  g_print( "eos reached, ending..." ) ;
  playing = FALSE ;
}

void mpg_parser_connect( GstElement *parser, GstPad *pad )
{
  g_print( "new pad %s created\n", gst_pad_get_name( pad ) ) ;

  gst_element_set_state( GST_ELEMENT( main_thread ), GST_STATE_PAUSED ) ;
  
  if( strncmp( gst_pad_get_name( pad ), "audio", 5 ) == 0 )
  {
    gst_pad_connect( pad, gst_element_get_pad( mp3Parser, "sink" ) ) ;
    gst_pad_connect( gst_element_get_pad( mp3Parser, "src" ) ,
		     gst_element_get_pad( mp3Decoder, "sink" ) ) ;
    gst_pad_connect( gst_element_get_pad( mp3Decoder, "src" ) ,
		     gst_element_get_pad( ossSink, "sink" ) ) ;
    
    gst_bin_add( GST_BIN( main_thread ), mp3Parser ) ;
    gst_bin_add( GST_BIN( main_thread ), mp3Decoder ) ;
    gst_bin_add( GST_BIN( main_thread ), ossSink ) ;
  }
 
  if( strncmp( gst_pad_get_name( pad ), "video", 5 ) == 0 )
  { 
    gst_pad_connect( pad, gst_element_get_pad( mpgVideoParser, "sink" ) ) ;
 
    gst_pad_connect( gst_element_get_pad( mpgVideoParser, "src" ) ,
	             gst_element_get_pad( mpgDecoder, "sink" ) ) ;
    gst_pad_connect( gst_element_get_pad( mpgDecoder, "src" ) ,
	             gst_element_get_pad( colorSpace, "sink" ) ) ;
    gst_pad_connect( gst_element_get_pad( colorSpace, "src" ) ,
	             gst_element_get_pad( videoSink, "sink" ) ) ;
  
    gst_bin_add( GST_BIN( main_thread ), mpgVideoParser ) ;
    gst_bin_add( GST_BIN( main_thread ), mpgDecoder ) ;
    gst_bin_add( GST_BIN( main_thread ), colorSpace ) ;
    gst_bin_add( GST_BIN( main_thread ), videoSink ) ;
  }  
  
  gst_element_set_state( GST_ELEMENT( main_thread ), GST_STATE_PLAYING ) ;
}

int main(int argc, char *argv[] )
{
   gst_init( &argc, &argv ) ; // ughh...

   // Get the Boss: 
   main_thread = gst_pipeline_new( "main_thread" ) ;
   
   // Get the main Srcs & sinks 
   diskSrc  = gst_elementfactory_make( "disksrc",  "movie_file" ) ;
   g_object_set( G_OBJECT( diskSrc ), "location", argv[ 1 ], NULL ) ;
   g_signal_connectc( G_OBJECT( diskSrc ), "eos", 
		      G_CALLBACK( eos ), main_thread, FALSE ) ;
   
   ossSink = gst_elementfactory_make( "osssink", "audio_sink" ) ;
   videoSink = gst_elementfactory_make( "xvideosink", "video_sink" ) ;
   colorSpace = gst_elementfactory_make( "colorspace", "video_filter" ) ;
   
   // Get the parsers
   mp3Parser = gst_elementfactory_make( "mp3parse", "mp3parser" ) ;
   mpgVideoParser = gst_elementfactory_make( "mp2videoparse", "mp2parser" ) ;
   mpgParser = gst_elementfactory_make( "mpeg2parse", "mpgparser" ) ;
   g_signal_connectc( G_OBJECT( mpgParser ), "new_pad", 
		      G_CALLBACK( mpg_parser_connect ), NULL, FALSE ) ;

   //The mpeg decoders
   mpgDecoder = gst_elementfactory_make( "mpeg2dec", "mpegdecoder" ) ;
   mp3Decoder = gst_elementfactory_make( "mpg123", "mp3decoder" ) ;
   
   // Asserttions:
   g_assert( main_thread != NULL ) ;
   g_assert( videoSink != NULL ) ;
   g_assert( diskSrc != NULL ) ;
   g_assert( ossSink != NULL ) ;
   g_assert( mpgParser != NULL ) ;
   g_assert( mp3Parser != NULL ) ;
   g_assert( mp3Decoder != NULL ) ;
   g_assert( mpgDecoder != NULL ) ;
   g_assert( colorSpace != NULL ) ;
   g_assert( mpgVideoParser != NULL ) ;

   //g_object_set( G_OBJECT( ossSink ), "frequency", 1000, NULL ) ;
   //g_object_set( G_OBJECT( ossSink ), "channels", 1, NULL ) ;
   
   // Connect the appropritate elements
   gst_pad_connect( gst_element_get_pad( diskSrc, "src" ) ,
		    gst_element_get_pad( mpgParser, "sink" ) ) ;
   
   // Add all element to their appropriate bins
   gst_bin_add( GST_BIN( main_thread ), diskSrc ) ;
   gst_bin_add( GST_BIN( main_thread ), mpgParser ) ;
   
   // Lets get started
   gst_element_set_state( GST_ELEMENT( main_thread ), GST_STATE_PLAYING ) ;
   playing = TRUE ;
   
   while( playing ) 
   gst_bin_iterate( GST_BIN( main_thread ) ) ;

   gst_element_set_state( GST_ELEMENT( main_thread ), GST_STATE_NULL ) ; 
  
   gst_object_destroy( GST_OBJECT( main_thread  )  ) ;  

   g_print( "Normal Program Termination\n" ) ;
   
   exit (0) ;
}
