#include "gconf.h"

int
main (int argc, char *argv[])
{
  printf ("Default video sink : %s\n", 
          gst_gconf_get_string ("default/videosink"));
  printf ("Default audio sink : %s\n", 
          gst_gconf_get_string ("default/audiosink"));
  printf ("Default video src : %s\n", 
          gst_gconf_get_string ("default/videosrc"));
  printf ("Default audio src : %s\n", 
          gst_gconf_get_string ("default/audiosrc"));
  return 0;
}
