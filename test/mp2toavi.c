
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>

GstElement *mux;
GstElement *merge_subtitles;

void eof(GstSrc *src) {
  g_print("have eos, quitting\n");
  exit(0);
}

void frame_encoded(GstElement *element, gint framenum, gpointer data) {
  gulong frame_size;
  static gulong total = 0;

  frame_size = gst_util_get_long_arg(GTK_OBJECT(element),"last_frame_size");

  total+=frame_size;

  g_print("encoded frame %d %ld %ld\n", framenum, frame_size, total/(framenum+1));
}

void mp2tomp1(GstElement *parser,GstPad *pad, GstElement *pipeline) {
  GstElement *parse_audio, *parse_video, *decode, *decode_video, *play, *encode, *audio_resample;
  GstElement *smooth, *median;
  GstElement *audio_queue, *video_queue;
  GstElement *audio_thread, *video_thread;
  GstElement *videoscale, *audio_encode;

  g_print("***** a new pad %s was created\n", gst_pad_get_name(pad));
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PAUSED);

  // connect to audio pad
  if (0) {
  //if (strncmp(gst_pad_get_name(pad), "private_stream_1.0", 18) == 0) {
    gst_plugin_load("ac3parse");
    gst_plugin_load("ac3dec");
    gst_plugin_load("audioscale");
    gst_plugin_load("mpegaudio");
    // construct internal pipeline elements
    parse_audio = gst_elementfactory_make("ac3parse","parse_audio");
    g_return_if_fail(parse_audio != NULL);
    gtk_object_set(GTK_OBJECT(parse_audio),"skip", 15, NULL);
    decode = gst_elementfactory_make("ac3dec","decode_audio");
    g_return_if_fail(decode != NULL);
    audio_resample = gst_elementfactory_make("audioscale","audioscale");
    g_return_if_fail(audio_resample != NULL);
    gtk_object_set(GTK_OBJECT(audio_resample),"frequency", 44100, NULL);

    audio_encode = gst_elementfactory_make("mpegaudio","audio_encode");
    //audio_encode = gst_elementfactory_make("pipefilter","audio_encode");
    g_return_if_fail(audio_encode != NULL);
    //gtk_object_set(GTK_OBJECT(audio_encode),"command", "lame -x - -", NULL);

    // create the thread and pack stuff into it
    audio_thread = gst_thread_new("audio_thread");
    g_return_if_fail(audio_thread != NULL);
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(parse_audio));
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(decode));
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(audio_resample));
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(audio_encode));

    gtk_object_set(GTK_OBJECT(mux),"audio","00",NULL);

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(audio_thread),
                              gst_element_get_pad(parse_audio,"sink"));
    gst_pad_connect(gst_element_get_pad(parse_audio,"src"),
                    gst_element_get_pad(decode,"sink"));
    gst_pad_connect(gst_element_get_pad(decode,"src"),
                    gst_element_get_pad(audio_resample,"sink"));
    gst_pad_connect(gst_element_get_pad(audio_resample,"src"),
                    gst_element_get_pad(audio_encode,"sink"));
    gst_pad_connect(gst_element_get_pad(audio_encode,"src"),
                    gst_element_get_pad(mux,"audio_00"));

    // construct queue and connect everything in the main pipelie
    audio_queue = gst_elementfactory_make("queue","audio_queue");
    gtk_object_set(GTK_OBJECT(audio_queue),"max_level",1,NULL);
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_queue));
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_thread));
    gst_pad_connect(pad,
                    gst_element_get_pad(audio_queue,"sink"));
    gst_pad_connect(gst_element_get_pad(audio_queue,"src"),
                    gst_element_get_pad(audio_thread,"sink"));

    // set up thread state and kick things off
    gtk_object_set(GTK_OBJECT(audio_thread),"create_thread",TRUE,NULL);
    g_print("setting to READY state\n");
    gst_element_set_state(GST_ELEMENT(audio_thread),GST_STATE_READY);
  } else if (strncmp(gst_pad_get_name(pad), "subtitle_stream_4", 17) == 0) {
    gst_pad_connect(pad,
                    gst_element_get_pad(merge_subtitles,"subtitle"));
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
    gtk_object_set(GTK_OBJECT(audio_queue),"max_level",1,NULL);
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_queue));
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_thread));
    gst_pad_connect(pad,
                    gst_element_get_pad(audio_queue,"sink"));
    gst_pad_connect(gst_element_get_pad(audio_queue,"src"),
                    gst_element_get_pad(audio_thread,"sink"));

    // set up thread state and kick things off
    gtk_object_set(GTK_OBJECT(audio_thread),"create_thread",TRUE,NULL);
    g_print("setting to READY state\n");
    gst_element_set_state(GST_ELEMENT(audio_thread),GST_STATE_READY);
  } else if (strncmp(gst_pad_get_name(pad), "video_", 6) == 0) {
  //} else if (0) {

    gst_plugin_load("mp1videoparse");
    gst_plugin_load("mpeg2play");
    gst_plugin_load("mpeg2subt");
    gst_plugin_load("smooth");
    gst_plugin_load("median");
    gst_plugin_load("videoscale");
    gst_plugin_load("wincodec");
    //gst_plugin_load("mpeg1encoder");
    // construct internal pipeline elements
    parse_video = gst_elementfactory_make("mp1videoparse","parse_video");
    g_return_if_fail(parse_video != NULL);
    decode_video = gst_elementfactory_make("mpeg2play","decode_video");
    g_return_if_fail(decode_video != NULL);
    merge_subtitles = gst_elementfactory_make("mpeg2subt","merge_subtitles");
    g_return_if_fail(merge_subtitles != NULL);
    videoscale = gst_elementfactory_make("videoscale","videoscale");
    g_return_if_fail(videoscale != NULL);
    //gtk_object_set(GTK_OBJECT(videoscale),"width",352, "height", 288,NULL);
    gtk_object_set(GTK_OBJECT(videoscale),"width",640, "height", 480,NULL);
    median = gst_elementfactory_make("median","median");
    g_return_if_fail(median != NULL);
    gtk_object_set(GTK_OBJECT(median),"filtersize",9,NULL);
    gtk_object_set(GTK_OBJECT(median),"active",TRUE,NULL);
    smooth = gst_elementfactory_make("smooth","smooth");
    g_return_if_fail(smooth != NULL);
    gtk_object_set(GTK_OBJECT(smooth),"filtersize",5,NULL);
    gtk_object_set(GTK_OBJECT(smooth),"tolerance",9,NULL);
    gtk_object_set(GTK_OBJECT(smooth),"active",FALSE,NULL);
    encode = gst_elementfactory_make("winenc","encode");
    g_return_if_fail(encode != NULL);
    gtk_signal_connect(GTK_OBJECT(encode),"frame_encoded",GTK_SIGNAL_FUNC(frame_encoded),NULL);
    gtk_object_set(GTK_OBJECT(encode),"bitrate",800*4,NULL);
    gtk_object_set(GTK_OBJECT(encode),"quality",10000,NULL);
    //gtk_object_set(GTK_OBJECT(encode),"compression",NULL,NULL);
    //encode = gst_elementfactory_make("mpeg1encoder","encode");
    //gtk_object_set(GTK_OBJECT(show),"width",640, "height", 480,NULL);

    gtk_object_set(GTK_OBJECT(mux),"video","00:DIV3",NULL);

    // create the thread and pack stuff into it
    video_thread = gst_thread_new("video_thread");
    g_return_if_fail(video_thread != NULL);
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(parse_video));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(decode_video));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(merge_subtitles));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(median));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(smooth));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(videoscale));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(encode));
    gst_bin_use_cothreads(GST_BIN(video_thread), FALSE);

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(video_thread),
                              gst_element_get_pad(parse_video,"sink"));
    gst_pad_connect(gst_element_get_pad(parse_video,"src"),
                    gst_element_get_pad(decode_video,"sink"));
    gst_pad_connect(gst_element_get_pad(decode_video,"src"),
                    gst_element_get_pad(median,"sink"));
    gst_pad_connect(gst_element_get_pad(median,"src"),
                    gst_element_get_pad(merge_subtitles,"video"));
    gst_pad_connect(gst_element_get_pad(merge_subtitles,"src"),
                    gst_element_get_pad(videoscale,"sink"));
    gst_pad_connect(gst_element_get_pad(videoscale,"src"),
                    gst_element_get_pad(smooth,"sink"));
    gst_pad_connect(gst_element_get_pad(smooth,"src"),
                    gst_element_get_pad(encode,"sink"));
    gst_pad_connect(gst_element_get_pad(encode,"src"),
                    gst_element_get_pad(mux,"video_00"));

    // construct queue and connect everything in the main pipeline
    video_queue = gst_elementfactory_make("queue","video_queue");
    gtk_object_set(GTK_OBJECT(video_queue),"max_level",1,NULL);
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(video_queue));
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(video_thread));
    gst_pad_connect(pad,
                    gst_element_get_pad(video_queue,"sink"));
    gst_pad_connect(gst_element_get_pad(video_queue,"src"),
                    gst_element_get_pad(video_thread,"sink"));

    // set up thread state and kick things off
    gtk_object_set(GTK_OBJECT(video_thread),"create_thread",TRUE,NULL);
    g_print("setting to READY state\n");
    gst_element_set_state(GST_ELEMENT(video_thread),GST_STATE_READY);
  }
  g_print("\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);
}

int main(int argc,char *argv[]) {
  GstPipeline *pipeline;
  GstElement *src, *parse;
  GstElement *fdsink;
  GstElementFactory *fdsinkfactory;
  int fd;

  g_print("have %d args\n",argc);

  gst_init(&argc,&argv);
  gst_plugin_load("mpeg2parse");
  gst_plugin_load("aviencoder");

  pipeline = gst_pipeline_new("pipeline");
  g_return_val_if_fail(pipeline != NULL, -1);

  if (strstr(argv[1],"video_ts")) {
    src = gst_elementfactory_make("dvdsrc","src");
    g_print("using DVD source\n");
  } else
    src = gst_elementfactory_make("disksrc","src");

  g_return_val_if_fail(src != NULL, -1);
  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  g_print("should be using file '%s'\n",argv[1]);

  g_print("should be using output file '%s'\n",argv[2]);

  parse = gst_elementfactory_make("mpeg2parse","parse");
  g_return_val_if_fail(parse != NULL, -1);

  mux = gst_elementfactory_make("aviencoder","mux");
  g_return_val_if_fail(mux != NULL, -1);
  fd = open(argv[2],O_CREAT|O_RDWR|O_TRUNC, S_IREAD|S_IWRITE);
  fdsinkfactory = gst_elementfactory_find("fdsink");
  g_return_val_if_fail(fdsinkfactory != NULL, -1);
  fdsink = gst_elementfactory_create(fdsinkfactory,"fdsink");
  g_return_val_if_fail(fdsink != NULL, -1);
  gtk_object_set(GTK_OBJECT(fdsink),"fd",fd,NULL);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(parse));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(mux));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(fdsink));

  gtk_signal_connect(GTK_OBJECT(parse),"new_pad",mp2tomp1, pipeline);

  gtk_signal_connect(GTK_OBJECT(src),"eos",GTK_SIGNAL_FUNC(eof),NULL);

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(mux,"src"),
                  gst_element_get_pad(fdsink,"sink"));

  g_print("setting to READY state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

  while (1) {
    gst_bin_iterate(GST_BIN(pipeline));
  }
}
