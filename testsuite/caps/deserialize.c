
#include <gst/gst.h>
#include <string.h>

/* The caps_strings file is created using:
 *
 * grep '^.caps' /home/ds/.gstreamer-0.8/registry.xml | \
 *   sed 's/^.caps.\(.*\)..caps.$/\1/' | awk '{print length($ln) " " $ln; }' | \
 *   sort -n | uniq | sed 's/^[^ ]* //' >caps_strings
 *
 */


int
main (int argc, char *argv[])
{
  const char *filename;
  char *data;
  char **list;
  int i;
  int length;
  GstCaps *caps;

  gst_init (&argc, &argv);

  filename = "caps_strings";
  if (argc > 1)
    filename = argv[1];

  if (!g_file_get_contents (filename, &data, &length, NULL)) {
    abort ();
  }

  list = g_strsplit (data, "\n", 0);

  for (i = 0; list[i] != NULL; i++) {
    if (list[i][0] == 0)
      continue;

    caps = gst_caps_from_string (list[i]);
    if (caps == NULL) {
      char **list2;
      int j;

      g_print ("Could not parse: %s\n", list[i]);
      g_print ("Trying each structure...\n");

      list2 = g_strsplit (list[i], ";", 0);

      for (j = 0; list2[j] != NULL; j++) {
        caps = gst_caps_from_string (list2[j]);

        if (caps == NULL) {
          g_print ("Could not parse %s\n", list2[j]);
          abort ();
        }

        gst_caps_free (caps);
      }

      g_print ("parsed each structure individually\n");
      abort ();
    }

    gst_caps_free (caps);
    g_free (list[i]);
  }

  g_free (list);
  g_free (data);

  return 0;
}
