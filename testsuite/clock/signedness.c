/*
 * testsuite program to make sure GST_SECOND doesn't cause signedness 
 * conversions
 */

#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstClockTime time[] = { 0, 1, G_MAXUINT64 / GST_SECOND };
  GstClockTimeDiff diff[] =
      { 0, 1, -1, G_MAXINT64 / GST_SECOND, G_MININT64 / GST_SECOND };
  guint i;

  gst_init (&argc, &argv);

  for (i = 0; i < G_N_ELEMENTS (time); i++) {
    g_print ("%" G_GUINT64_FORMAT " != %" G_GUINT64_FORMAT
        " * GST_SECOND / GST_SECOND ? ... ", time[i], time[i]);
    if (time[i] != (time[i] * GST_SECOND / GST_SECOND)) {
      g_print ("NO\n");
      g_assert_not_reached ();
      return 1;
    }
    g_print ("yes\n");
  }
  for (i = 0; i < G_N_ELEMENTS (diff); i++) {
    g_print ("%" G_GINT64_FORMAT " != %" G_GINT64_FORMAT
        " * GST_SECOND / GST_SECOND ? ... ", diff[i], diff[i]);
    if (diff[i] != (diff[i] * GST_SECOND / GST_SECOND)) {
      g_print ("NO\n");
      g_assert_not_reached ();
      return 1;
    }
    g_print ("yes\n");
  }

  return 0;
}
