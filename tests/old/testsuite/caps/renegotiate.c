
#include <gst/gst.h>
#include <string.h>
#include <unistd.h>


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
      gst_caps_unref (icaps);
      g_print ("returning %d\n", rate);
      return gst_caps_copy (mycaps);
    }
    gst_caps_unref (icaps);
  }

  return NULL;
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline;
  GError *error = NULL;
  GstIterator *iter1, *iter2;
  gint done1 = FALSE, done2 = FALSE;
  gpointer element;

  gst_init (&argc, &argv);

  /* change sinesrk to sinesrc once gst_parse_launch is fixed */
  pipeline = gst_parse_launch ("sinesrc ! audioconvert ! "
      "audio/x-raw-int, channels=2, rate=48000;"
      "audio/x-raw-int, channels=1, rate=44100 !" "fakesink", &error);

  if (error != NULL) {
    g_print
        ("oops, couldn't build pipeline.  You probably don't have audioconvert or sinesrc\n"
        "the error was: %s\n", error->message);
    g_error_free (error);
    exit (0);
  }

  iter1 = gst_bin_iterate_elements (GST_BIN (pipeline));
  while (!done1) {
    switch (gst_iterator_next (iter1, &element)) {
      case GST_ITERATOR_OK:
      {
        gpointer pad;

        iter2 = gst_element_iterate_pads (element);
        while (!done2) {
          switch (gst_iterator_next (iter2, &pad)) {
            case GST_ITERATOR_OK:
              if (gst_pad_get_direction (pad) == GST_PAD_SRC) {
                g_signal_connect (G_OBJECT (pad), "fixate",
                    G_CALLBACK (my_fixate), NULL);
              }
              gst_object_unref (pad);
              break;
            case GST_ITERATOR_DONE:
              done2 = TRUE;
              break;
            case GST_ITERATOR_RESYNC:
            case GST_ITERATOR_ERROR:
              exit (1);
              break;
          }
        }
        gst_iterator_free (iter2);

        gst_object_unref (element);
        break;
      }
      case GST_ITERATOR_DONE:
        done1 = TRUE;
        break;
      case GST_ITERATOR_RESYNC:
      case GST_ITERATOR_ERROR:
        exit (1);
        break;
    }
  }
  gst_iterator_free (iter1);

  /*g_signal_connect (pipeline, "deep_notify",
     G_CALLBACK (gst_element_default_deep_notify), NULL); */

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /*
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
   */
  /* Like totally not sure how to do this in THREADED. Punting for now! */

  sleep (5);

  return 0;
}
