
#include <gnome.h>
#include <gst/gst.h>

void eof(GstSrc *src) {
  g_print("have eos, quitting\n");
  exit(0);
}

void mpeg2parse_newpad(GstElement *parser,GstPad *pad, GstElement *pipeline) {
  GstElement *parse_audio, *parse_video, *decode, *decode_video, *play, *show;
  GstElement *audio_queue, *video_queue;
  GstElement *audio_thread, *video_thread;

  GtkWidget *appwindow;

  g_print("***** a new pad %s was created\n", gst_pad_get_name(pad));

  // connect to audio pad
  //if (0) {
  if (strncmp(gst_pad_get_name(pad), "private_stream_1.0", 18) == 0) {
    gst_plugin_load("ac3parse");
    gst_plugin_load("ac3dec");
    // construct internal pipeline elements
    parse_audio = gst_elementfactory_make("ac3parse","parse_audio");
    g_return_if_fail(parse_audio != NULL);
    decode = gst_elementfactory_make("ac3dec","decode_audio");
    g_return_if_fail(decode != NULL);
    play = gst_elementfactory_make("audiosink","play_audio");
    g_return_if_fail(play != NULL);

    // create the thread and pack stuff into it
    audio_thread = gst_thread_new("audio_thread");
    g_return_if_fail(audio_thread != NULL);
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(parse_audio));
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(decode));
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(play));

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(audio_thread),
                              gst_element_get_pad(parse_audio,"sink"));
    gst_pad_connect(gst_element_get_pad(parse_audio,"src"),
                    gst_element_get_pad(decode,"sink"));
    gst_pad_connect(gst_element_get_pad(decode,"src"),
                    gst_element_get_pad(play,"sink"));

    // construct queue and connect everything in the main pipelie
    audio_queue = gst_elementfactory_make("queue","audio_queue");
    gtk_object_set(GTK_OBJECT(audio_queue),"max_level",30,NULL);
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_queue));
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_thread));
    gst_pad_connect(pad,
                    gst_element_get_pad(audio_queue,"sink"));
    gst_pad_connect(gst_element_get_pad(audio_queue,"src"),
                    gst_element_get_pad(audio_thread,"sink"));

    // set up thread state and kick things off
    gtk_object_set(GTK_OBJECT(audio_thread),"create_thread",TRUE,NULL);
    g_print("setting to RUNNING state\n");
    gst_element_set_state(GST_ELEMENT(audio_thread),GST_STATE_RUNNING);
    g_print("setting to PLAYING state\n");
    gst_element_set_state(GST_ELEMENT(audio_thread),GST_STATE_PLAYING);
  } else if (strncmp(gst_pad_get_name(pad), "audio_", 6) == 0) {
    gst_plugin_load("mp3parse");
    gst_plugin_load("mpg123");
    // construct internal pipeline elements
    parse_audio = gst_elementfactory_make("mp3parse","parse_audio");
    g_return_if_fail(parse_audio != NULL);
    decode = gst_elementfactory_make("mpg123","decode_audio");
    g_return_if_fail(decode != NULL);
    play = gst_elementfactory_make("audiosink","play_audio");
    g_return_if_fail(play != NULL);

    // create the thread and pack stuff into it
    audio_thread = gst_thread_new("audio_thread");
    g_return_if_fail(audio_thread != NULL);
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(parse_audio));
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(decode));
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(play));

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(audio_thread),
                              gst_element_get_pad(parse_audio,"sink"));
    gst_pad_connect(gst_element_get_pad(parse_audio,"src"),
                    gst_element_get_pad(decode,"sink"));
    gst_pad_connect(gst_element_get_pad(decode,"src"),
                    gst_element_get_pad(play,"sink"));

    // construct queue and connect everything in the main pipelie
    audio_queue = gst_elementfactory_make("queue","audio_queue");
    gtk_object_set(GTK_OBJECT(audio_queue),"max_level",30,NULL);
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_queue));
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_thread));
    gst_pad_connect(pad,
                    gst_element_get_pad(audio_queue,"sink"));
    gst_pad_connect(gst_element_get_pad(audio_queue,"src"),
                    gst_element_get_pad(audio_thread,"sink"));

    // set up thread state and kick things off
    gtk_object_set(GTK_OBJECT(audio_thread),"create_thread",TRUE,NULL);
    g_print("setting to RUNNING state\n");
    gst_element_set_state(GST_ELEMENT(audio_thread),GST_STATE_RUNNING);
    g_print("setting to PLAYING state\n");
    gst_element_set_state(GST_ELEMENT(audio_thread),GST_STATE_PLAYING);
  } else if (strncmp(gst_pad_get_name(pad), "video_", 6) == 0) {
  //} else if (0) {

    gst_plugin_load("mp1videoparse");
    gst_plugin_load("mpeg2play");
    gst_plugin_load("videosink");
    // construct internal pipeline elements
    parse_video = gst_elementfactory_make("mp1videoparse","parse_video");
    g_return_if_fail(parse_video != NULL);
    decode_video = gst_elementfactory_make("mpeg2play","decode_video");
    g_return_if_fail(decode_video != NULL);
    show = gst_elementfactory_make("videosink","show");
    g_return_if_fail(show != NULL);
    //gtk_object_set(GTK_OBJECT(show),"width",640, "height", 480,NULL);

    appwindow = gnome_app_new("MPEG player","MPEG player");
    gnome_app_set_contents(GNOME_APP(appwindow),
      	        gst_util_get_widget_arg(GTK_OBJECT(show),"widget"));
		gtk_widget_show_all(appwindow);

    // create the thread and pack stuff into it
    video_thread = gst_thread_new("video_thread");
    g_return_if_fail(video_thread != NULL);
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(parse_video));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(decode_video));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(show));

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(video_thread),
                              gst_element_get_pad(parse_video,"sink"));
    gst_pad_connect(gst_element_get_pad(parse_video,"src"),
                    gst_element_get_pad(decode_video,"sink"));
    gst_pad_connect(gst_element_get_pad(decode_video,"src"),
                    gst_element_get_pad(show,"sink"));

    // construct queue and connect everything in the main pipeline
    video_queue = gst_elementfactory_make("queue","video_queue");
    gtk_object_set(GTK_OBJECT(video_queue),"max_level",30,NULL);
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(video_queue));
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(video_thread));
    gst_pad_connect(pad,
                    gst_element_get_pad(video_queue,"sink"));
    gst_pad_connect(gst_element_get_pad(video_queue,"src"),
                    gst_element_get_pad(video_thread,"sink"));

    // set up thread state and kick things off
    gtk_object_set(GTK_OBJECT(video_thread),"create_thread",TRUE,NULL);
    g_print("setting to RUNNING state\n");
    gst_element_set_state(GST_ELEMENT(video_thread),GST_STATE_RUNNING);
    g_print("setting to PLAYING state\n");
    gst_element_set_state(GST_ELEMENT(video_thread),GST_STATE_PLAYING);
  }
  g_print("\n");
}

int main(int argc,char *argv[]) {
  GstPipeline *pipeline;
  GstElement *src, *parse, *out;
  GstPad *infopad;
  int i,c;

  g_print("have %d args\n",argc);

  gst_init(&argc,&argv);
  gnome_init("MPEG2 Video player","0.0.1",argc,argv);
  gst_plugin_load("mpeg2parse");
  //gst_plugin_load("mpeg1parse");

  pipeline = gst_pipeline_new("pipeline");
  g_return_if_fail(pipeline != NULL);

  if (strstr(argv[1],"video_ts")) {
    src = gst_elementfactory_make("dvdsrc","src");
    g_print("using DVD source\n");
  } else
    src = gst_elementfactory_make("disksrc","src");

  g_return_if_fail(src != NULL);
  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  if (argc >= 3) {
    gtk_object_set(GTK_OBJECT(src),"bytesperread",atoi(argv[2]),NULL);
    g_print("block size is %d\n",atoi(argv[2]));
  }
  g_print("should be using file '%s'\n",argv[1]);

  parse = gst_elementfactory_make("mpeg2parse","parse");
  //parse = gst_elementfactory_make("mpeg1parse","parse");
  g_return_if_fail(parse != NULL);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(parse));

  gtk_signal_connect(GTK_OBJECT(parse),"new_pad",mpeg2parse_newpad, pipeline);

  gtk_signal_connect(GTK_OBJECT(src),"eos",GTK_SIGNAL_FUNC(eof),NULL);

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));

  g_print("setting to RUNNING state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_RUNNING);

  while (1) {
    gst_src_push(GST_SRC(src));
  }
}
