
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>
#include <gnome.h>

static void
gst_play_have_type (GstElement *sink, GstElement *sink2, gpointer data)
{
  GST_DEBUG (0,"GstPipeline: play have type %p\n", (gboolean *)data);

  *(gboolean *)data = TRUE;
}

gboolean 
idle_func (gpointer data)
{
  return gst_bin_iterate (GST_BIN (data));
}

static GstCaps*
gst_play_typefind (GstBin *bin, GstElement *element)
{
  gboolean found = FALSE;
  GstElement *typefind;
  GstCaps *caps = NULL;

  GST_DEBUG (0,"GstPipeline: typefind for element \"%s\" %p\n",
             GST_ELEMENT_NAME(element), &found);

  typefind = gst_elementfactory_make ("typefind", "typefind");
  g_return_val_if_fail (typefind != NULL, FALSE);

  gtk_signal_connect (GTK_OBJECT (typefind), "have_type",
                      GTK_SIGNAL_FUNC (gst_play_have_type), &found);

  gst_pad_connect (gst_element_get_pad (element, "src"),
                   gst_element_get_pad (typefind, "sink"));

  gst_bin_add (bin, typefind);

  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_PLAYING);

  // push a buffer... the have_type signal handler will set the found flag
  gst_bin_iterate (bin);

  gst_element_set_state (GST_ELEMENT (bin), GST_STATE_NULL);

  caps = gst_pad_get_caps (gst_element_get_pad (element, "src"));

  gst_pad_disconnect (gst_element_get_pad (element, "src"),
                      gst_element_get_pad (typefind, "sink"));
  gst_bin_remove (bin, typefind);

  return caps;
}

static GstElement*
get_video_encoder_bin (void) 
{
  GstElement *bin;
  GstElement *encoder, *queue, *colorspace, *videoscale;

  bin = gst_bin_new ("video_encoder_bin");

  colorspace = gst_elementfactory_make ("colorspace", "colorspace");
  g_assert (colorspace != NULL);
  videoscale = gst_elementfactory_make ("videoscale", "videoscale");
  g_assert (videoscale != NULL);
  gtk_object_set (GTK_OBJECT (videoscale), "width", 352, "height", 288, NULL);
  encoder = gst_elementfactory_make ("mpeg2enc", "video_encoder");
  g_assert (encoder != NULL);
  queue = gst_elementfactory_make ("queue", "video_encoder_queue");
  g_assert (queue != NULL);

  gst_bin_add (GST_BIN (bin), colorspace);
  gst_bin_add (GST_BIN (bin), videoscale);
  gst_bin_add (GST_BIN (bin), encoder);
  gst_bin_add (GST_BIN (bin), queue);

  gst_element_connect (colorspace, "src", videoscale, "sink");
  gst_element_connect (videoscale, "src", encoder, "sink");
  gst_element_connect (encoder, "src", queue, "sink");

  gst_element_add_ghost_pad (bin, gst_element_get_pad (colorspace, "sink"), "sink");
  gst_element_add_ghost_pad (bin, gst_element_get_pad (queue, "src"), "src");

  return bin;
}

static GstElement*
get_audio_encoder_bin (void) 
{
  GstElement *bin;
  GstElement *encoder, *queue;

  bin = gst_bin_new ("audio_encoder_bin");

  encoder = gst_elementfactory_make ("mpegaudio", "audio_encoder");
  g_assert (encoder != NULL);
  queue = gst_elementfactory_make ("queue", "audio_encoder_queue");
  g_assert (queue != NULL);

  gst_bin_add (GST_BIN (bin), encoder);
  gst_bin_add (GST_BIN (bin), queue);

  gst_element_connect (encoder, "src", queue, "sink");

  gst_element_add_ghost_pad (bin, gst_element_get_pad (encoder, "sink"), "sink");
  gst_element_add_ghost_pad (bin, gst_element_get_pad (queue, "src"), "src");

  return bin;
}

int main(int argc,char *argv[]) 
{
  GstElement *filesrc, *audio_enc, *video_enc;
  GstElement *muxthread_video, *muxer, *fdsink_video;
  GstElement *muxthread_audio, *fdsink_audio;
  GstElement *bin;
  GstCaps *srccaps;
  GstElement *new_element;
  GstAutoplug *autoplug;
  gint fd_video;
  gint fd_audio;

  g_thread_init(NULL);
  gst_init(&argc,&argv);
  gnome_init("autoplug","0.0.1", argc,argv);

  if (argc != 4) {
    g_print("usage: %s <in_filename> <out_video> <out_audio>\n", argv[0]);
    exit(-1);
  }

  /* create a new bin to hold the elements */
  bin = gst_pipeline_new("pipeline");
  g_assert(bin != NULL);

  /* create a disk reader */
  filesrc = gst_elementfactory_make("filesrc", "disk_source");
  g_assert(filesrc != NULL);
  gtk_object_set(GTK_OBJECT(filesrc),"location", argv[1],NULL);

  gst_bin_add (GST_BIN (bin), filesrc);

  srccaps = gst_play_typefind (GST_BIN (bin), filesrc);

  if (!srccaps) {
    g_print ("could not autoplug, unknown media type...\n");
    exit (-1);
  }
  
  audio_enc = get_audio_encoder_bin();
  video_enc = get_video_encoder_bin();

  autoplug = gst_autoplugfactory_make ("staticrender");
  g_assert (autoplug != NULL);

  new_element = gst_autoplug_to_renderers (autoplug,
           srccaps,
           video_enc,
           audio_enc,
           NULL);

  if (!new_element) {
    g_print ("could not autoplug, no suitable codecs found...\n");
    exit (-1);
  }

  gst_object_ref (GST_OBJECT (filesrc));
  gst_bin_remove (GST_BIN (bin), filesrc);
  gst_object_destroy (GST_OBJECT (bin));

  // FIXME hack, reparent the filesrc so the scheduler doesn't break
  bin = gst_pipeline_new("pipeline");

  gst_bin_add (GST_BIN (bin), filesrc);
  gst_bin_add (GST_BIN (bin), new_element);

  gst_element_connect (filesrc, "src", new_element, "sink");

  muxer = gst_elementfactory_make ("system_encode", "muxer");
  g_assert (muxer != NULL);

  if (gst_bin_get_by_name (GST_BIN (new_element), "video_encoder_bin")) {
    muxthread_video = gst_thread_new("thread_video");

    fdsink_video = gst_elementfactory_make ("fdsink", "fdsink_video");
    g_assert (fdsink_video != NULL);
    fd_video = open (argv[2], O_CREAT|O_RDWR|O_TRUNC);
    gtk_object_set (GTK_OBJECT (fdsink_video), "fd", fd_video, NULL);

    gst_element_connect (video_enc, "src", fdsink_video, "sink");
    gst_bin_add (GST_BIN (muxthread_video), fdsink_video);

    gst_bin_add (GST_BIN (bin), muxthread_video);
  }

  if (gst_bin_get_by_name (GST_BIN (new_element), "audio_encoder_bin")) {
    muxthread_audio = gst_thread_new("thread_audio");

    fdsink_audio = gst_elementfactory_make ("fdsink", "fdsink_audio");
    g_assert (fdsink_audio != NULL);
    fd_audio = open (argv[3], O_CREAT|O_RDWR|O_TRUNC);
    gtk_object_set (GTK_OBJECT (fdsink_audio), "fd", fd_audio, NULL);

    gst_element_connect (audio_enc, "src", fdsink_audio, "sink");
    gst_bin_add (GST_BIN (muxthread_audio), fdsink_audio);

    gst_bin_add (GST_BIN (bin), muxthread_audio);
  }

  //gtk_object_set (GTK_OBJECT (muxer), "video", "00", NULL);
  //gtk_object_set (GTK_OBJECT (muxer), "audio", "00", NULL);
  
  /* start playing */
  gst_element_set_state(GST_ELEMENT(bin), GST_STATE_PLAYING);

  gtk_idle_add(idle_func, bin);

  gst_main();

  /* stop the bin */
  gst_element_set_state(GST_ELEMENT(bin), GST_STATE_NULL);

  gst_pipeline_destroy(bin);

  exit(0);
}

