#include "gconf.h"

int
main (int argc, char *argv[])
{
  printf ("Default video sink : %s\n", 
          gst_gconf_get_string ("default/videosink"));
  printf ("Default audio sink : %s\n", 
          gst_gconf_get_string ("default/audiosink"));
  return 0;
}
