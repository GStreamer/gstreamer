#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>

#define BUFFER 1

extern gboolean _gst_plugin_spew;
gboolean idle_func(gpointer data);
int fd;
char *outfile;
GstElement *mux;

void eof(GstElement *src) {
  g_print("have eos, quitting\n");
  exit(0);
}

void new_pad_created(GstElement *parse,GstPad *pad,GstElement *pipeline) {
  GstElement *audio_encode;
  GstElement *encode, *smooth, *median;
  GstElement *audio_queue, *video_queue;
  GstElement *audio_thread, *video_thread;

  g_print("***** a new pad %s was created\n", gst_pad_get_name(pad));
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PAUSED);

  // connect to audio pad
  //if (0) {
  if (strncmp(gst_pad_get_name(pad), "audio_", 6) == 0) {
    gst_plugin_load("mpegaudio");
    // construct internal pipeline elements
    audio_encode = gst_elementfactory_make("mpegaudio","audio_encode");
    g_return_if_fail(audio_encode != NULL);

    // create the thread and pack stuff into it
    audio_thread = gst_thread_new("audio_thread");
    g_return_if_fail(audio_thread != NULL);
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(audio_encode));

    g_object_set(G_OBJECT(mux),"audio","00",NULL);

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(audio_thread),
                              gst_element_get_pad(audio_encode,"sink"),"sink");
    gst_pad_connect(gst_element_get_pad(audio_encode,"src"),
                           gst_element_get_pad(mux,"audio_00"));

    // construct queue and connect everything in the main pipelie
    audio_queue = gst_elementfactory_make("queue","audio_queue");
    g_object_set(G_OBJECT(audio_queue),"max_level",BUFFER,NULL);
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

    gst_plugin_load("smooth");
    gst_plugin_load("median");
    gst_plugin_load("mpeg2enc");
    // construct internal pipeline elements
    smooth = gst_elementfactory_make("smooth","smooth");
    g_return_if_fail(smooth != NULL);
    median = gst_elementfactory_make("median","median");
    g_return_if_fail(median != NULL);
    g_object_set(G_OBJECT(median),"filtersize",5,NULL);
    g_object_set(G_OBJECT(median),"active",TRUE,NULL);

    g_object_set(G_OBJECT(smooth),"filtersize",16,NULL);
    g_object_set(G_OBJECT(smooth),"tolerance",16,NULL);
    g_object_set(G_OBJECT(smooth),"active",FALSE,NULL);

    encode = gst_elementfactory_make("mpeg2enc","encode");
    g_return_if_fail(encode != NULL);
    
    g_object_set(G_OBJECT(mux),"video","00",NULL);

    // create the thread and pack stuff into it
    video_thread = gst_thread_new("video_thread");
    g_return_if_fail(video_thread != NULL);
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(smooth));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(median));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(encode));
    gst_pad_connect(gst_element_get_pad(median,"src"),
                    gst_element_get_pad(smooth,"sink"));
    gst_pad_connect(gst_element_get_pad(smooth,"src"),
                    gst_element_get_pad(encode,"sink"));
    gst_pad_connect(gst_element_get_pad(encode,"src"),
                    gst_element_get_pad(mux,"video_00"));

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(video_thread),
                              gst_element_get_pad(median,"sink"),"sink");

    // construct queue and connect everything in the main pipeline
    video_queue = gst_elementfactory_make("queue","video_queue");
    g_object_set(G_OBJECT(video_queue),"max_level",BUFFER,NULL);
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
  GstElement *pipeline, *src, *parse, *fdsink;
  GstElementFactory *fdsinkfactory;

  g_print("have %d args\n",argc);

  //_gst_plugin_spew = TRUE;
  g_thread_init(NULL);
  gst_init(&argc,&argv);
  gst_plugin_load("parseavi");
  gst_plugin_load("system_encode");

  pipeline = gst_pipeline_new("pipeline");
  g_return_val_if_fail(pipeline != NULL, -1);

  src = gst_elementfactory_make("disksrc","src");
  g_return_val_if_fail(src != NULL, -1);
  g_object_set(G_OBJECT(src),"location",argv[1],NULL);
  g_print("should be using file '%s'\n",argv[1]);
  parse = gst_elementfactory_make("avidecoder","parse");
  g_return_val_if_fail(parse != NULL, -1);

  mux = gst_elementfactory_make("system_encode","mux");
  g_return_val_if_fail(mux != NULL, -1);
  g_print("should be using output file '%s'\n",argv[2]);
  outfile = argv[2];
  fd = open(argv[2],O_CREAT|O_RDWR|O_TRUNC);
  fdsinkfactory = gst_elementfactory_find("fdsink");
  g_return_val_if_fail(fdsinkfactory != NULL, -1);
  fdsink = gst_elementfactory_create(fdsinkfactory,"fdsink");
  g_return_val_if_fail(fdsink != NULL, -1);
  g_object_set(G_OBJECT(fdsink),"fd",fd,NULL);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(parse));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(mux));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(fdsink));

  g_signal_connect(G_OBJECT(parse),"new_pad",
                      G_CALLBACK(new_pad_created),pipeline);

  g_signal_connect(G_OBJECT(src),"eos",
                      G_CALLBACK(eof),NULL);

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(mux,"src"),
                  gst_element_get_pad(fdsink,"sink"));

  g_print("setting to READY state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_PLAYING);

  g_print("about to enter loop\n");

  g_idle_add(idle_func,pipeline);

  //gtk_main();

  return 0;
}

gboolean idle_func(gpointer data) {
  gst_bin_iterate(GST_BIN(data));
  return TRUE;
}
