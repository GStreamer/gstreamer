
#include <gst/gst.h>
#include <string.h>

static GstPad *sinesrcpad;

static GstStaticCaps caps1 = GST_STATIC_CAPS ("audio/x-raw-int, "
    "endianness=(int)1234, "
    "signed=(boolean)true, "
    "width=(int)16, " "depth=(int)16, " "rate=(int)48000, " "channels=(int)1");
static GstStaticCaps caps2 = GST_STATIC_CAPS ("audio/x-raw-int, "
    "endianness=(int)1234, "
    "signed=(boolean)true, "
    "width=(int)16, " "depth=(int)16, " "rate=(int)44100, " "channels=(int)1");

int stage = 0;

static GstCaps *
my_fixate (GstPad * pad, GstCaps * caps, gpointer user_data)
{
  const char *element_name;
  const char *pad_name;

  element_name = gst_element_get_name (gst_pad_get_parent (pad));
  pad_name = gst_pad_get_name (pad);

  g_print ("%s:%s: %s\n", element_name, pad_name, gst_caps_to_string (caps));

  if (strcmp (element_name, "sinesrc0") == 0 && strcmp (pad_name, "src") == 0) {
    GstCaps *icaps;
    const GstCaps *mycaps;
    int rate;

    sinesrcpad = pad;

    if (stage == 0) {
      mycaps = gst_static_caps_get (&caps1);
      rate = 48000;
    } else {
      mycaps = gst_static_caps_get (&caps2);
      rate = 44100;
    }
    icaps = gst_caps_intersect (caps, mycaps);
    if (!gst_caps_is_empty (icaps)) {
      gst_caps_free (icaps);
      g_print ("returning %d\n", rate);
      return gst_caps_copy (mycaps);
    }
    gst_caps_free (icaps);
  }

  return NULL;
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline;
  const GList *list;
  const GList *l2;
  int i;
  int ret;

  gst_init (&argc, &argv);

  /* change sinesrk to sinesrc once gst_parse_launch is fixed */
  pipeline = gst_parse_launch ("sinesrk ! audioconvert ! "
      "audio/x-raw-int, channels=2, rate=48000;"
      "audio/x-raw-int, channels=1, rate=44100 !" "fakesink", NULL);

  if (pipeline == NULL) {
    g_print
        ("oops, couldn't build pipeline.  You probably don't have audioconvert or sinesrc\n");
    exit (0);
  }

  list = gst_bin_get_list (GST_BIN (pipeline));
  while (list) {
    GstElement *element = GST_ELEMENT (list->data);

    l2 = gst_element_get_pad_list (element);
    while (l2) {
      GstPad *pad = GST_PAD (l2->data);

      if (gst_pad_get_direction (pad) == GST_PAD_SRC) {
        g_signal_connect (G_OBJECT (pad), "fixate", G_CALLBACK (my_fixate),
            NULL);
      }
      l2 = g_list_next (l2);
    }
    list = g_list_next (list);
  }

  g_signal_connect (pipeline, "deep_notify",
      G_CALLBACK (gst_element_default_deep_notify), NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  i = 0;
  while (1) {
    gst_bin_iterate (GST_BIN (pipeline));
    i++;
    if (i == 10) {
      stage = 1;
      g_print ("10 iterations\n");
      ret = gst_pad_renegotiate (sinesrcpad);
      g_print ("negotiation returned %d\n", ret);
    }
    if (i == 20) {
      g_print ("20 iterations\n");
      exit (0);
    }
  }

  return 0;
}
