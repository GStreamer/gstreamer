#include <stdlib.h>
#include <gst/gst.h>

/* returns all factories which have a maximum of maxtemplates GstPadTemplates in direction dir
 */
GList *
gst_factories_at_most_templates (GList * factories, GstPadDirection dir,
    guint maxtemplates)
{
  GList *ret = NULL;

  while (factories) {
    guint count = 0;
    GList *templs = ((GstElementFactory *) factories->data)->padtemplates;

    while (templs) {
      if (GST_PAD_TEMPLATE_DIRECTION (templs->data) == dir) {
	count++;
      }
      if (count > maxtemplates)
	break;
      templs = g_list_next (templs);
    }
    if (count <= maxtemplates)
      ret = g_list_prepend (ret, factories->data);

    factories = g_list_next (factories);
  }
  return ret;
}

static void
property_change_callback (GObject * object, GstObject * orig,
    GParamSpec * pspec)
{
  GValue value = { 0, };	/* the important thing is that value.type = 0 */
  gchar *str = 0;

  if (pspec->flags & G_PARAM_READABLE) {
    g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
    g_object_get_property (G_OBJECT (orig), pspec->name, &value);
    if (G_IS_PARAM_SPEC_STRING (pspec))
      str = g_value_dup_string (&value);
    else if (G_IS_PARAM_SPEC_ENUM (pspec))
      str = g_strdup_printf ("%d", g_value_get_enum (&value));
    else if (G_IS_PARAM_SPEC_INT64 (pspec))
      str = g_strdup_printf ("%" G_GINT64_FORMAT, g_value_get_int64 (&value));
    else
      str = g_strdup_value_contents (&value);

    g_print ("%s: %s = %s\n", GST_OBJECT_NAME (orig), pspec->name, str);
    g_free (str);
    g_value_unset (&value);
  } else {
    g_warning ("Parameter not readable. What's up with that?");
  }
}

static void
error_callback (GObject * object, GstObject * orig, gchar * error)
{
  g_print ("ERROR: %s: %s\n", GST_OBJECT_NAME (orig), error);
}

/*
 * Test program for the autoplugger.
 * Uses new API extensions (2002-01-28), too.
 *
 * USAGE: spidertest <mediafile>
 * If mediafile can be recognized, xvideo and oss audio output are tried.
 */
int
main (int argc, char *argv[])
{
  GstElement *bin, *filesrc, *decoder, *osssink, *videosink;
  GList *facs;

  if (argc < 2) {
    g_print ("usage: %s <file>\n", argv[0]);
    exit (-1);
  }

  gst_init (&argc, &argv);

  /* create a new bin to hold the elements */
  bin = gst_pipeline_new ("pipeline");
  g_signal_connect (bin, "deep_notify", G_CALLBACK (property_change_callback),
      NULL);
  g_signal_connect (bin, "error", G_CALLBACK (error_callback), NULL);

  /* create a disk reader */
  filesrc = gst_element_factory_make ("filesrc", "disk_source");
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  /* now it's time to get the decoder */
  decoder = gst_element_factory_make ("spider", "spider");
  if (!decoder) {
    g_print ("could not find plugin \"spider\"\n");
    exit (-2);
  }

  /* only use decoding plugins */
  g_object_get (decoder, "factories", &facs, NULL);
  facs = gst_factories_at_most_templates (facs, GST_PAD_SINK, 1);
  g_object_set (decoder, "factories", facs, NULL);

  /* create video and audio sink */
  osssink = gst_element_factory_make ("osssink", "audio");
  videosink = gst_element_factory_make ("xvideosink", "video");

  if ((!osssink) || (!videosink)) {
    g_print ("could not create output plugins\n");
    exit (-3);
  }

  /* add objects to the main pipeline */
  gst_bin_add (GST_BIN (bin), filesrc);
  gst_bin_add (GST_BIN (bin), decoder);
  gst_bin_add (GST_BIN (bin), osssink);
  gst_bin_add (GST_BIN (bin), videosink);

  /* link objects */
  if (!(gst_element_link (filesrc, decoder) &&
	  gst_element_link (decoder, osssink) &&
	  gst_element_link (decoder, videosink))) {
    g_print ("the pipeline could not be linked\n");
    exit (-4);
  }

/*  gst_bin_use_clock (GST_BIN (bin), gst_system_clock_obtain ());*/

  /* start playing */
  gst_element_set_state (bin, GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (bin)));

  exit (0);
}
