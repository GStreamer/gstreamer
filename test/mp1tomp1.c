
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>

int fd;
char *outfile;

void eof(GstElement *src) {
  g_print("have eos, quitting\n");
  exit(0);
}

void mp2tomp1(GstElement *parser,GstPad *pad, GstElement *pipeline) {
  GstElement *parse_audio, *parse_video, *decode, *decode_video, *play, *encode;
  GstElement *audio_queue, *video_queue;
  GstElement *audio_thread, *video_thread;
  //GstElement *videoscale;
  GstElement *fdsink;
  GstElementFactory *fdsinkfactory;

  g_print("***** a new pad %s was created\n", gst_pad_get_name(pad));

  // connect to audio pad
  if (0) {
  //if (strncmp(gst_pad_get_name(pad), "private_stream_1.0", 18) == 0) {
    gst_plugin_load("ac3parse");
    gst_plugin_load("ac3dec");
    // construct internal pipeline elements
    parse_audio = gst_elementfactory_make("ac3parse","parse_audio");
    g_return_if_fail(parse_audio != NULL);
    decode = gst_elementfactory_make("ac3dec","decode_audio");
    g_return_if_fail(decode != NULL);
    play = gst_elementfactory_make("osssink","play_audio");
    g_return_if_fail(play != NULL);

    // create the thread and pack stuff into it
    audio_thread = gst_thread_new("audio_thread");
    g_return_if_fail(audio_thread != NULL);
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(parse_audio));
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(decode));
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(play));

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(audio_thread),
                              gst_element_get_pad(parse_audio,"sink"),"sink");
    gst_pad_connect(gst_element_get_pad(parse_audio,"src"),
                    gst_element_get_pad(decode,"sink"));
    gst_pad_connect(gst_element_get_pad(decode,"src"),
                    gst_element_get_pad(play,"sink"));

    // construct queue and connect everything in the main pipelie
    audio_queue = gst_elementfactory_make("queue","audio_queue");
    g_object_set(G_OBJECT(audio_queue),"max_level",30,NULL);
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_queue));
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_thread));
    gst_pad_connect(pad,
                    gst_element_get_pad(audio_queue,"sink"));
    gst_pad_connect(gst_element_get_pad(audio_queue,"src"),
                    gst_element_get_pad(audio_thread,"sink"));

    // set up thread state and kick things off
    g_object_set(G_OBJECT(audio_thread),"create_thread",TRUE,NULL);
    g_print("setting to READY state\n");
    gst_element_set_state(GST_ELEMENT(audio_thread),GST_STATE_READY);
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
    play = gst_elementfactory_make("osssink","play_audio");
    g_return_if_fail(play != NULL);

    // create the thread and pack stuff into it
    audio_thread = gst_thread_new("audio_thread");
    g_return_if_fail(audio_thread != NULL);
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(parse_audio));
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(decode));
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(play));

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(audio_thread),
                              gst_element_get_pad(parse_audio,"sink"),"sink");
    gst_pad_connect(gst_element_get_pad(parse_audio,"src"),
                    gst_element_get_pad(decode,"sink"));
    gst_pad_connect(gst_element_get_pad(decode,"src"),
                    gst_element_get_pad(play,"sink"));

    // construct queue and connect everything in the main pipelie
    audio_queue = gst_elementfactory_make("queue","audio_queue");
    g_object_set(G_OBJECT(audio_queue),"max_level",30,NULL);
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_queue));
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(audio_thread));
    gst_pad_connect(pad,
                    gst_element_get_pad(audio_queue,"sink"));
    gst_pad_connect(gst_element_get_pad(audio_queue,"src"),
                    gst_element_get_pad(audio_thread,"sink"));

    // set up thread state and kick things off
    g_object_set(G_OBJECT(audio_thread),"create_thread",TRUE,NULL);
    g_print("setting to READY state\n");
    gst_element_set_state(GST_ELEMENT(audio_thread),GST_STATE_READY);
    g_print("setting to PLAYING state\n");
    gst_element_set_state(GST_ELEMENT(audio_thread),GST_STATE_PLAYING);
  } else if (strncmp(gst_pad_get_name(pad), "video_", 6) == 0) {
  //} else if (0) {

    gst_plugin_load("mp1videoparse");
    gst_plugin_load("mpeg_play");
    gst_plugin_load("mpeg2enc");
    // construct internal pipeline elements
    parse_video = gst_elementfactory_make("mp1videoparse","parse_video");
    g_return_if_fail(parse_video != NULL);
    decode_video = gst_elementfactory_make("mpeg_play","decode_video");
    g_return_if_fail(decode_video != NULL);
    encode = gst_elementfactory_make("mpeg2enc","encode");
    g_return_if_fail(encode != NULL);
    //g_object_set(G_OBJECT(show),"width",640, "height", 480,NULL);
    fd = open(outfile,O_CREAT|O_RDWR|O_TRUNC);

    fdsinkfactory = gst_elementfactory_find("fdsink");
    g_return_if_fail(fdsinkfactory != NULL);
    fdsink = gst_elementfactory_create(fdsinkfactory,"fdsink");
    g_return_if_fail(fdsink != NULL);
    g_object_set(G_OBJECT(fdsink),"fd",fd,NULL);

    // create the thread and pack stuff into it
    video_thread = gst_thread_new("video_thread");
    g_return_if_fail(video_thread != NULL);
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(parse_video));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(decode_video));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(encode));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(fdsink));

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(video_thread),
                              gst_element_get_pad(parse_video,"sink"),"sink");
    gst_pad_connect(gst_element_get_pad(parse_video,"src"),
                    gst_element_get_pad(decode_video,"sink"));
    gst_pad_connect(gst_element_get_pad(decode_video,"src"),
                    gst_element_get_pad(encode,"sink"));
    gst_pad_connect(gst_element_get_pad(encode,"src"),
                    gst_element_get_pad(fdsink,"sink"));

    // construct queue and connect everything in the main pipeline
    video_queue = gst_elementfactory_make("queue","video_queue");
    g_object_set(G_OBJECT(video_queue),"max_level",30,NULL);
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(video_queue));
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(video_thread));
    gst_pad_connect(pad,
                    gst_element_get_pad(video_queue,"sink"));
    gst_pad_connect(gst_element_get_pad(video_queue,"src"),
                    gst_element_get_pad(video_thread,"sink"));

    // set up thread state and kick things off
    g_object_set(G_OBJECT(video_thread),"create_thread",TRUE,NULL);
    g_print("setting to READY state\n");
    gst_element_set_state(GST_ELEMENT(video_thread),GST_STATE_READY);
    g_print("setting to PLAYING state\n");
    gst_element_set_state(GST_ELEMENT(video_thread),GST_STATE_PLAYING);
  }
  g_print("\n");
}

int main(int argc,char *argv[]) {
  GstElement *pipeline, *src, *parse;

  g_print("have %d args\n",argc);

  gst_init(&argc,&argv);
  gst_plugin_load("mpeg1parse");

  pipeline = gst_pipeline_new("pipeline");
  g_return_val_if_fail(pipeline != NULL, -1);

  if (strstr(argv[1],"video_ts")) {
    src = gst_elementfactory_make("dvdsrc","src");
    g_print("using DVD source\n");
  } else
    src = gst_elementfactory_make("disksrc","src");

  g_return_val_if_fail(src != NULL, -1);
  g_object_set(G_OBJECT(src),"location",argv[1],NULL);
  g_print("should be using file '%s'\n",argv[1]);

  g_print("should be using output file '%s'\n",argv[2]);
  outfile = argv[2];

  parse = gst_elementfactory_make("mpeg1parse","parse");
  g_return_val_if_fail(parse != NULL, -1);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(parse));

  g_signal_connectc(G_OBJECT(parse),"new_pad",mp2tomp1, pipeline, FALSE);

  g_signal_connectc(G_OBJECT(src),"eos",G_CALLBACK(eof),NULL, FALSE);

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));

  g_print("setting to READY state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

  while (1) {
    gst_bin_iterate(GST_BIN(pipeline));
  }
}
