#include <stdlib.h>
#include <gst/gst.h>

/* 1:1 copy of gstpropsprivate.h, needed for INFO events */

#define GST_PROPS_ENTRY_IS_VARIABLE(a)  (((GstPropsEntry*)(a))->propstype > GST_PROPS_VAR_ID)

typedef struct _GstPropsEntry GstPropsEntry;

struct _GstPropsEntry {
  GQuark    propid;
  GstPropsId propstype;    

  union {
    /* flat values */
    gboolean bool_data;
    guint32  fourcc_data;
    gint     int_data;
    gfloat   float_data;

    /* structured values */
    struct {
      GList *entries;
    } list_data;
    struct {
      gchar *string;
    } string_data;
    struct {
      gint min;
      gint max;
    } int_range_data;
    struct {
      gfloat min;
      gfloat max;
    } float_range_data;
  } data;
};

/* end gstpropsprivate.h */

/* property output, stolen from gst-launch */
static void 
print_props (gpointer data, gpointer user_data)
{
  GstPropsEntry *entry = (GstPropsEntry *)data;
  GstElement *element = GST_ELEMENT (user_data);

  g_print ("%s: %s: ", gst_element_get_name (element), 
      g_quark_to_string (entry->propid));
  switch (entry->propstype) {
    case GST_PROPS_INT_ID:
      g_print ("%d\n", entry->data.int_data);
      break;
    case GST_PROPS_STRING_ID:
      g_print ("%s\n", entry->data.string_data.string);
      break;
    case GST_PROPS_FLOAT_ID:
      g_print ("%f\n", entry->data.float_data);
      break;
    default:
      g_print ("unknown\n");
  }
}

static void 
event_func (GstElement *element, GstEvent *event)
{
  GstProps *props;

  if (event == NULL)
    return;
  
  if (GST_EVENT_TYPE (event) == GST_EVENT_INFO) {
    props = GST_EVENT_INFO_PROPS (event);

    g_list_foreach (props->properties, print_props, GST_EVENT_SRC (event));
  }
}

/**
 * Test program for the autoplugger.
 * Uses new API extensions (2002-01-28), too.
 *
 * USAGE: spidertest <mediafile>
 * If mediafile can be recognized, xvideo and oss audio output are tried.
 */
int main(int argc,char *argv[]) 
{
  GstElement *bin, *filesrc, *decoder, *osssink, *videosink;
  
  if (argc != 2) {
    g_print("usage: %s <file>\n", argv[0]);
    exit(-1);
  }

   gst_init(&argc,&argv);

  /* create a new bin to hold the elements */
  bin = gst_pipeline_new("pipeline");
  g_signal_connect (G_OBJECT (bin), "event", G_CALLBACK (event_func), NULL);
 
  /* create a disk reader */
  filesrc = gst_elementfactory_make("filesrc", "disk_source");
  g_object_set(G_OBJECT(filesrc),"location", argv[1], NULL);

  /* now it's time to get the decoder */
  decoder = gst_elementfactory_make("spider", "spider");
  if (!decoder) {
    g_print ("could not find plugin \"spider\"\n");
    exit (-2);
  }
  
  /* only use decoding plugins */
  g_object_set(G_OBJECT(decoder),"plugtype", 2, NULL);

  /* create video and audio sink */
  osssink = gst_elementfactory_make("osssink", "audio");
  videosink = gst_elementfactory_make("xvideosink", "video");

  if ((!osssink) || (!videosink)) {
    g_print ("could not create output plugins\n");
    exit (-3);
  }

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(bin), filesrc);
  gst_bin_add(GST_BIN(bin), decoder);
  gst_bin_add(GST_BIN(bin), osssink);
  gst_bin_add(GST_BIN(bin), videosink);

  /* connect objects */
  if (!(gst_element_connect_elements(filesrc, decoder) &&
	gst_element_connect_elements(decoder, osssink) &&
	gst_element_connect_elements(decoder, videosink)))
  {
    g_print ("the pipeline could not be connected\n");
    exit (-4);
  }

  /* start playing */
  gst_element_set_state(bin, GST_STATE_PLAYING);

  while (gst_bin_iterate(GST_BIN(bin)));

  exit(0);
}

