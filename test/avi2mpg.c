#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glib.h>
#include <gst/gst.h>

#define BUFFER 1

extern gboolean _gst_plugin_spew;
gboolean idle_func(gpointer data);
int fd;
char *outfile;
GstElement *mux;

void eof(GstSrc *src) {
  g_print("have eos, quitting\n");
  exit(0);
}

void new_pad_created(GstElement *parse,GstPad *pad,GstElement *pipeline) {
  GstElement *parse_audio, *parse_video, *decode, *decode_video, *audio_encode;
  GstElement *encode, *smooth, *median;
  GstElement *audio_queue, *video_queue;
  GstElement *audio_thread, *video_thread;

  GtkWidget *appwindow;

  g_print("***** a new pad %s was created\n", gst_pad_get_name(pad));

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

    gtk_object_set(GTK_OBJECT(mux),"audio","00",NULL);

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(audio_thread),
                              gst_element_get_pad(audio_encode,"sink"));
    gst_pad_connect(gst_element_get_pad(audio_encode,"src"),
                           gst_element_get_pad(mux,"audio_00"));

    // construct queue and connect everything in the main pipelie
    audio_queue = gst_elementfactory_make("queue","audio_queue");
    gtk_object_set(GTK_OBJECT(audio_queue),"max_level",BUFFER,NULL);
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

    gst_plugin_load("smooth");
    gst_plugin_load("median");
    gst_plugin_load("mpeg2enc");
    // construct internal pipeline elements
    smooth = gst_elementfactory_make("smooth","smooth");
    g_return_if_fail(smooth != NULL);
    median = gst_elementfactory_make("median","median");
    g_return_if_fail(median != NULL);
    gtk_object_set(GTK_OBJECT(median),"filtersize",5,NULL);
    gtk_object_set(GTK_OBJECT(median),"active",FALSE,NULL);

    gtk_object_set(GTK_OBJECT(smooth),"filtersize",16,NULL);
    gtk_object_set(GTK_OBJECT(smooth),"tolerance",16,NULL);
    gtk_object_set(GTK_OBJECT(smooth),"active",TRUE,NULL);

    encode = gst_elementfactory_make("mpeg2enc","encode");
    g_return_if_fail(encode != NULL);
    
    gtk_object_set(GTK_OBJECT(mux),"video","00",NULL);

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
                              gst_element_get_pad(median,"sink"));

    // construct queue and connect everything in the main pipeline
    video_queue = gst_elementfactory_make("queue","video_queue");
    gtk_object_set(GTK_OBJECT(video_queue),"max_level",BUFFER,NULL);
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
  GstElement *src, *parse, *fdsink;
  GstElementFactory *fdsinkfactory;

  g_print("have %d args\n",argc);

  //_gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);
  gst_plugin_load("parseavi");
  gst_plugin_load("system_encode");

  pipeline = gst_pipeline_new("pipeline");
  g_return_if_fail(pipeline != NULL);

  src = gst_elementfactory_make("disksrc","src");
  g_return_if_fail(src != NULL);
  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  g_print("should be using file '%s'\n",argv[1]);
  parse = gst_elementfactory_make("parseavi","parse");
  g_return_if_fail(parse != NULL);

  mux = gst_elementfactory_make("system_encode","mux");
  g_return_if_fail(mux != NULL);
  g_print("should be using output file '%s'\n",argv[2]);
  outfile = argv[2];
  fd = open(argv[2],O_CREAT|O_RDWR|O_TRUNC);
  fdsinkfactory = gst_elementfactory_find("fdsink");
  g_return_if_fail(fdsinkfactory != NULL);
  fdsink = gst_elementfactory_create(fdsinkfactory,"fdsink");
  g_return_if_fail(fdsink != NULL);
  gtk_object_set(GTK_OBJECT(fdsink),"fd",fd,NULL);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(parse));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(mux));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(fdsink));

  gtk_signal_connect(GTK_OBJECT(parse),"new_pad",
                      GTK_SIGNAL_FUNC(new_pad_created),pipeline);

  gtk_signal_connect(GTK_OBJECT(src),"eos",
                      GTK_SIGNAL_FUNC(eof),NULL);

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));
  gst_pad_connect(gst_element_get_pad(mux,"src"),
                  gst_element_get_pad(fdsink,"sink"));

  g_print("setting to RUNNING state\n");
  gst_element_set_state(GST_ELEMENT(pipeline),GST_STATE_RUNNING);

  xmlSaveFile("aviparse.xml",gst_xml_write(GST_ELEMENT(pipeline)));

  g_print("about to enter loop\n");

  while (1) {
    gst_src_push(GST_SRC(src));
  }
  // this does not work due to multithreading
  /*
  g_idle_add(idle_func,src);

  gtk_main();
  */
}

gboolean idle_func(gpointer data) {
  gst_src_push(GST_SRC(data));
  return TRUE;
}
