#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glib.h>
#include <gst/gst.h>


extern gboolean _gst_plugin_spew;
gboolean idle_func(gpointer data);
int fd;
char *outfile;

void eof(GstSrc *src) {
  g_print("have eos, quitting\n");
  exit(0);
}

void new_pad_created(GstElement *parse,GstPad *pad,GstElement *pipeline) {
  GstElement *parse_audio, *parse_video, *decode, *decode_video, *play, *encode, *smooth;
  GstElement *audio_queue, *video_queue;
  GstElement *audio_thread, *video_thread;
  GstElement *fdsink;
  GstElementFactory *fdsinkfactory;

  GtkWidget *appwindow;

  g_print("***** a new pad %s was created\n", gst_pad_get_name(pad));

  // connect to audio pad
  //if (0) {
  if (strncmp(gst_pad_get_name(pad), "audio_", 6) == 0) {
    // construct internal pipeline elements
    play = gst_elementfactory_make("audiosink","play_audio");
    g_return_if_fail(play != NULL);

    // create the thread and pack stuff into it
    audio_thread = gst_thread_new("audio_thread");
    g_return_if_fail(audio_thread != NULL);
    gst_bin_add(GST_BIN(audio_thread),GST_ELEMENT(play));

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(audio_thread),
                              gst_element_get_pad(play,"sink"));

    // construct queue and connect everything in the main pipelie
    audio_queue = gst_elementfactory_make("queue","audio_queue");
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
    //smooth = gst_elementfactory_make("median","median");
    g_return_if_fail(smooth != NULL);
    gtk_object_set(GTK_OBJECT(smooth),"filtersize",2,NULL);
    gtk_object_set(GTK_OBJECT(smooth),"tolerance",4,NULL);
    encode = gst_elementfactory_make("mpeg2enc","encode");
    g_return_if_fail(encode != NULL);
    fd = open(outfile,O_CREAT|O_RDWR|O_TRUNC);
    
    fdsinkfactory = gst_elementfactory_find("fdsink");
    g_return_if_fail(fdsinkfactory != NULL);
    fdsink = gst_elementfactory_create(fdsinkfactory,"fdsink");
    g_return_if_fail(fdsink != NULL);
    gtk_object_set(GTK_OBJECT(fdsink),"fd",fd,NULL);

    // create the thread and pack stuff into it
    video_thread = gst_thread_new("video_thread");
    g_return_if_fail(video_thread != NULL);
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(smooth));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(encode));
    gst_bin_add(GST_BIN(video_thread),GST_ELEMENT(fdsink));
    gst_pad_connect(gst_element_get_pad(smooth,"src"),
                    gst_element_get_pad(encode,"sink"));
    gst_pad_connect(gst_element_get_pad(encode,"src"),
                    gst_element_get_pad(fdsink,"sink"));

    // set up pad connections
    gst_element_add_ghost_pad(GST_ELEMENT(video_thread),
                              gst_element_get_pad(smooth,"sink"));
                              //gst_element_get_pad(encode,"sink"));

    // construct queue and connect everything in the main pipeline
    video_queue = gst_elementfactory_make("queue","video_queue");
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
  GstElement *src, *parse;

  g_print("have %d args\n",argc);

  //_gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);
  gst_plugin_load("parseavi");

  pipeline = gst_pipeline_new("pipeline");
  g_return_if_fail(pipeline != NULL);

  src = gst_elementfactory_make("disksrc","src");
  g_return_if_fail(src != NULL);
  gtk_object_set(GTK_OBJECT(src),"location",argv[1],NULL);
  g_print("should be using file '%s'\n",argv[1]);

  g_print("should be using output file '%s'\n",argv[2]);
  outfile = argv[2];
  
  parse = gst_elementfactory_make("parseavi","parse");
  g_return_if_fail(parse != NULL);

  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(src));
  gst_bin_add(GST_BIN(pipeline),GST_ELEMENT(parse));

  gtk_signal_connect(GTK_OBJECT(parse),"new_pad",
                      GTK_SIGNAL_FUNC(new_pad_created),pipeline);

  gtk_signal_connect(GTK_OBJECT(src),"eos",
                      GTK_SIGNAL_FUNC(eof),NULL);

  gst_pad_connect(gst_element_get_pad(src,"src"),
                  gst_element_get_pad(parse,"sink"));

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
