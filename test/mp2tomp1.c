
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>

static GstElement *mux = NULL;
//static GstElement *merge_subtitles;
static GstElement *fdsink;

static void 
eof(GstElement *src) 
{
  g_print("have eos, quitting\n");
  exit(0);
}

static GstPad* 
create_muxer (GstElement *pipeline, gchar *type, gchar *number) 
{
  if (!mux) {
    mux = gst_elementfactory_make("system_encode","mux");
    g_return_val_if_fail(mux != NULL, NULL);

    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(mux));
    gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(fdsink));
    gst_pad_connect(gst_element_get_pad(mux,"src"),
                    gst_element_get_pad(fdsink,"sink"));
  }
  g_object_set(G_OBJECT(mux), type, number,NULL);
  
  return gst_element_get_pad(mux,g_strconcat (type, "_", number, NULL));
}

static void 
mp2tomp1 (GstElement *parser, GstPad *pad, GstElement *pipeline) 
{
  GstElement *decode, *decode_video, *play, *encode, *audio_resample;
  GstElement *smooth, *median;
  GstElement *audio_queue, *video_queue;
  GstElement *audio_thread, *video_thread;
  GstElement *videoscale, *audio_encode;
  GstPad *muxerpad;

  g_print("***** a new pad %s was created\n", gst_pad_get_name(pad));
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PAUSED);

  // connect to audio pad
  //if (0) {
  if (strncmp(gst_pad_get_name(pad), "private_stream_1.0", 18) == 0) {
    // construct internal pipeline elements
    decode = gst_elementfactory_make("ac3dec","decode_audio");
    g_return_if_fail(decode != NULL);
    audio_resample = gst_elementfactory_make("audioscale","audioscale");
    g_return_if_fail(audio_resample != NULL);
    g_object_set(G_OBJECT(audio_resample),"frequency", 44100, NULL);

    audio_encode = gst_elementfactory_make("mpegaudio","audio_encode");
    //audio_encode = gst_elementfactory_make("pipefilter","audio_encode");
    g_return_if_fail(audio_encode != NULL);
    //g_object_set(G_OBJECT(audio_encode),"command", "lame -x - -", NULL);

    // create the thread and pack stuff into it
    audio_thread = gst_thread_new("audio_thread");
    g_return_if_fail(audio_thread != NULL);
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(decode));
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(audio_resample));
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(audio_encode));

    muxerpad = create_muxer (pipeline, "audio", "00");

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(audio_thread),
                              gst_element_get_pad(decode,"sink"),"sink");
    gst_pad_connect(gst_element_get_pad(decode,"src"),
                    gst_element_get_pad(audio_resample,"sink"));
    gst_pad_connect(gst_element_get_pad(audio_resample,"src"),
                    gst_element_get_pad(audio_encode,"sink"));
    gst_pad_connect(gst_element_get_pad(audio_encode,"src"),
                    muxerpad);

    // construct queue and connect everything in the main pipelie
    audio_queue = gst_elementfactory_make("queue","audio_queue");
    g_object_set(G_OBJECT(audio_queue),"max_level",1,NULL);
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
  } else if (strncmp(gst_pad_get_name(pad), "subtitle_stream_4", 17) == 0) {
    //gst_pad_connect(pad,
    //                gst_element_get_pad(merge_subtitles,"subtitle"));
  } else if (strncmp(gst_pad_get_name(pad), "audio_", 6) == 0) {
    // construct internal pipeline elements
    decode = gst_elementfactory_make("mad","decode_audio");
    g_return_if_fail(decode != NULL);
    play = gst_elementfactory_make("osssink","play_audio");
    g_return_if_fail(play != NULL);

    // create the thread and pack stuff into it
    audio_thread = gst_thread_new("audio_thread");
    g_return_if_fail(audio_thread != NULL);
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(decode));
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(play));


    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(audio_thread),
                              gst_element_get_pad(decode,"sink"),"sink");
    gst_pad_connect(gst_element_get_pad(decode,"src"),
                    gst_element_get_pad(play,"sink"));

    // construct queue and connect everything in the main pipelie
    audio_queue = gst_elementfactory_make("queue","audio_queue");
    g_object_set(G_OBJECT(audio_queue),"max_level",1,NULL);
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
  } else if (strncmp(gst_pad_get_name(pad), "video_", 6) == 0) {
  //} else if (0) {

    //gst_plugin_load("mpeg1encoder");
    // construct internal pipeline elements
    decode_video = gst_elementfactory_make("mpeg2dec","decode_video");
    g_return_if_fail(decode_video != NULL);
    //merge_subtitles = gst_elementfactory_make("mpeg2subt","merge_subtitles");
    //g_return_if_fail(merge_subtitles != NULL);
    videoscale = gst_elementfactory_make("videoscale","videoscale");
    g_return_if_fail(videoscale != NULL);
    g_object_set(G_OBJECT(videoscale),"width",352, "height", 288,NULL);
    median = gst_elementfactory_make("median","median");
    g_return_if_fail(median != NULL);
    g_object_set(G_OBJECT(median),"filtersize",5,NULL);
    g_object_set(G_OBJECT(median),"active",TRUE,NULL);
    smooth = gst_elementfactory_make("smooth","smooth");
    g_return_if_fail(smooth != NULL);
    g_object_set(G_OBJECT(smooth),"filtersize",5,NULL);
    g_object_set(G_OBJECT(smooth),"tolerance",9,NULL);
    g_object_set(G_OBJECT(smooth),"active",FALSE,NULL);
    encode = gst_elementfactory_make("mpeg2enc","encode");
    g_return_if_fail(encode != NULL);
    g_object_set(G_OBJECT(encode),"frames_per_second",25.0,NULL);
    //g_object_set(G_OBJECT(encode),"frames_per_second",29.97,NULL);
    //encode = gst_elementfactory_make("mpeg1encoder","encode");
    //g_object_set(G_OBJECT(show),"width",640, "height", 480,NULL);

    muxerpad = create_muxer (pipeline, "video", "00");
    g_return_if_fail(muxerpad != NULL);
    

    // create the thread and pack stuff into it
    video_thread = gst_thread_new("video_thread");
    g_return_if_fail(video_thread != NULL);
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(decode_video));
    //gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(merge_subtitles));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(median));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(smooth));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(videoscale));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(encode));

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(video_thread),
                              gst_element_get_pad(decode_video,"sink"),"sink");
    gst_pad_connect(gst_element_get_pad(decode_video,"src"),
                    gst_element_get_pad(median,"sink"));
    gst_pad_connect(gst_element_get_pad(median,"src"),
    //                gst_element_get_pad(merge_subtitles,"video"));
    //gst_pad_connect(gst_element_get_pad(merge_subtitles,"src"),
                    gst_element_get_pad(videoscale,"sink"));
    gst_pad_connect(gst_element_get_pad(videoscale,"src"),
                    gst_element_get_pad(smooth,"sink"));
    gst_pad_connect(gst_element_get_pad(smooth,"src"),
                    gst_element_get_pad(encode,"sink"));
    gst_pad_connect(gst_element_get_pad(encode,"src"),
                    muxerpad);

    // construct queue and connect everything in the main pipeline
    video_queue = gst_elementfactory_make("queue","video_queue");
    g_object_set(G_OBJECT(video_queue),"max_level",1,NULL);
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
  }
  g_print("\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);
}

int main(int argc,char *argv[]) {
  GstElement *pipeline, *src, *parse;
  GstElementFactory *fdsinkfactory;
  int fd;

  g_print("have %d args\n",argc);

  gst_init(&argc,&argv);

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

  parse = gst_elementfactory_make("mpeg2parse","parse");
  g_return_val_if_fail(parse != NULL, -1);

  fd = open(argv[2],O_CREAT|O_RDWR|O_TRUNC, S_IREAD|S_IWRITE);
  g_return_val_if_fail(fd >= 0, -1);
  fdsinkfactory = gst_elementfactory_find("fdsink");
  g_return_val_if_fail(fdsinkfactory != NULL, -1);
  fdsink = gst_elementfactory_create(fdsinkfactory,"fdsink");
  g_return_val_if_fail(fdsink != NULL, -1);
  g_object_set(G_OBJECT(fdsink),"fd",fd,NULL);

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

  return 0;
}
