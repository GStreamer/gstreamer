#include <gst/gst.h>

/* test to make sure that we can do gst_main and gst_main_quit in succession */
/* FIXME: use mutexes */

gboolean mainloop = FALSE;

static gboolean
quit_main (gpointer data)
{
  if (mainloop) {
    mainloop = FALSE;
    g_print ("-");
    gst_main_quit ();
  }
  return TRUE;
}

int
main (int argc, gchar * argv[])
{
  int i;

  g_timeout_add (1, quit_main, NULL);
  for (i = 0; i < 1000; ++i) {
    mainloop = TRUE;
    g_print ("+");
    gst_main ();
  }
  g_print ("\n");
  return 0;
}
