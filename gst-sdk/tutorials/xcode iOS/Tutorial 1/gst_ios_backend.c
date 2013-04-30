#include "gst_ios_backend.h"
#include "gst_ios_plugins.h"

void
gst_backend_init (void)
{
  gst_init (NULL, NULL);
  gst_backend_register_plugins ();
}
