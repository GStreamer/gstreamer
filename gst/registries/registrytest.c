#include <gst/gst.h>

#include "gstxmlregistry.h"

gint
main (gint argc, gchar * argv[])
{
  GstRegistry *registry;

  gst_init (&argc, &argv);

  registry = gst_xml_registry_new ("test", "reg.xml");

  gst_registry_load (registry);


  return 0;

}
